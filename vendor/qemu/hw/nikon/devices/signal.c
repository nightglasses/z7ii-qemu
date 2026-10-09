/*
 * Five-route signal-control functional profile, not a bound physical IP.
 * The original interface supplies permutations and fields. Reset/gate/mask
 * polarity, raw/filtered banks, W1C and 1MHz debounce are virtual assumptions.
 * External inputs alone produce sampled edges.
 * Five outputs aggregate to IRQ237.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "migration/vmstate.h"
#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "hw/core/qdev-clock.h"
#include "nikon.h"

OBJECT_DECLARE_SIMPLE_TYPE(NikonSignalState, NIKON_SIGNAL)

typedef struct NikonSignalRoute {
    NikonSignalState *owner;
    QEMUTimer *timer;
    qemu_irq irq;
    unsigned index;
} NikonSignalRoute;

struct NikonSignalState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    MemoryRegion configuration_input;
    Clock *clock;
    NikonSignalRoute route[5];
    uint32_t released, gates, masks, debounce, config;
    uint32_t opaque_configuration;
    uint16_t pending;
    uint8_t raw, filtered;
    bool reported_error;
};

static const unsigned signal_bits[] = { 0, 1, 4, 2, 3 };
static const unsigned signal_gates[] = { 4, 5, 3, 2, 2 };

static bool nikon_signal_active(NikonSignalState *s, unsigned i)
{
    return (s->released & (1U << signal_bits[i])) &&
           (s->gates & (1U << signal_gates[i]));
}

static void nikon_signal_outputs(NikonSignalState *s)
{
    unsigned i;

    for (i = 0; i < 5; i++) {
        unsigned pair = 3U << (2 * signal_bits[i]);

        qemu_set_irq(s->route[i].irq, !!(s->pending & s->masks & pair));
    }
}

static void nikon_signal_sample(void *opaque)
{
    NikonSignalRoute *r = opaque;
    NikonSignalState *s = r->owner;
    unsigned bit = 1U << signal_bits[r->index];
    bool level = !!(s->raw & bit);

    if (!nikon_signal_active(s, r->index)) {
        return;
    }
    if (!!(s->filtered & bit) != level) {
        s->filtered = (s->filtered & ~bit) | (level ? bit : 0);
        s->pending |= 1U << (2 * signal_bits[r->index] + !level);
        nikon_signal_outputs(s);
    }
}

static void nikon_signal_rebase(NikonSignalState *s, unsigned i)
{
    unsigned bit = 1U << signal_bits[i];

    timer_del(s->route[i].timer);
    if (nikon_signal_active(s, i)) {
        s->filtered = (s->filtered & ~bit) | (s->raw & bit);
    } else {
        s->filtered &= ~bit;
        s->pending &= ~(3U << (2 * signal_bits[i]));
    }
}

static void nikon_signal_input(void *opaque, int n, int level)
{
    NikonSignalState *s = opaque;
    unsigned bit = 1U << signal_bits[n];
    unsigned code;

    if (!!(s->raw & bit) == !!level) {
        return;
    }
    s->raw = (s->raw & ~bit) | (level ? bit : 0);
    timer_del(s->route[n].timer);
    if (!nikon_signal_active(s, n)) {
        return;
    }
    if (!(s->debounce & bit)) {
        nikon_signal_sample(&s->route[n]);
        return;
    }
    code = (s->debounce >> (8 + 4 * signal_bits[n])) & 3;
    timer_mod(s->route[n].timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
              clock_ticks_to_ns(s->clock, 1U << code));
}

static MemTxResult nikon_signal_error(NikonSignalState *s, hwaddr offset,
                                    unsigned size, bool write, uint64_t value)
{
    nikon_access_error(&s->reported_error, s->regs.addr + offset, size,
                      write, value, "signal-access-not-supported");
    return MEMTX_ERROR;
}

static MemTxResult nikon_signal_read(void *opaque, hwaddr offset,
                                   uint64_t *value, unsigned size,
                                   MemTxAttrs attrs)
{
    NikonSignalState *s = opaque;

    *value = 0;
    if (size != 4 || (offset & 3)) {
        return nikon_signal_error(s, offset, size, false, 0);
    }
    switch (offset) {
    case 0:
        *value = s->released;
        break;
    case 4:
        *value = s->gates;
        break;
    case 8:
        /* Unfiltered private pending is a named virtual read convention. */
        *value = s->pending;
        break;
    case 0xc:
        *value = s->masks;
        break;
    case 0x10:
        *value = s->raw | (s->filtered << 8);
        break;
    case 0x14:
        *value = s->debounce;
        break;
    case 0x18:
        *value = s->config;
        break;
    default:
        return nikon_signal_error(s, offset, size, false, 0);
    }
    return MEMTX_OK;
}

static MemTxResult nikon_signal_write(void *opaque, hwaddr offset,
                                    uint64_t value, unsigned size,
                                    MemTxAttrs attrs)
{
    NikonSignalState *s = opaque;
    uint32_t changed;
    unsigned i;

    if (size != 4 || (offset & 3)) {
        return nikon_signal_error(s, offset, size, true, value);
    }
    switch (offset) {
    case 0:
        if (value & ~0x1fU) {
            return nikon_signal_error(s, offset, size, true, value);
        }
        changed = s->released ^ value;
        s->released = value;
        for (i = 0; i < 5; i++) {
            if (changed & (1U << signal_bits[i])) {
                nikon_signal_rebase(s, i);
            }
        }
        break;
    case 4:
        if (value & ~0x3cU) {
            return nikon_signal_error(s, offset, size, true, value);
        }
        changed = s->gates ^ value;
        s->gates = value;
        for (i = 0; i < 5; i++) {
            if (changed & (1U << signal_gates[i])) {
                nikon_signal_rebase(s, i);
            }
        }
        break;
    case 8:
    case 0xc:
        if (value & ~0x3ffU) {
            return nikon_signal_error(s, offset, size, true, value);
        }
        if (offset == 8) {
            s->pending &= ~value;
        } else {
            s->masks = value;
        }
        break;
    case 0x14:
        if (value & ~0x0333331fU) {
            return nikon_signal_error(s, offset, size, true, value);
        }
        changed = s->debounce ^ value;
        s->debounce = value;
        for (i = 0; i < 5; i++) {
            unsigned bit = signal_bits[i];

            if (changed & ((1U << bit) | (3U << (8 + 4 * bit)))) {
                nikon_signal_rebase(s, i);
            }
        }
        break;
    case 0x18:
        s->config = value;
        break;
    default:
        return nikon_signal_error(s, offset, size, true, value);
    }
    nikon_signal_outputs(s);
    return MEMTX_OK;
}

static const MemoryRegionOps nikon_signal_ops = {
    .read_with_attrs = nikon_signal_read,
    .write_with_attrs = nikon_signal_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
};

/*
 * Only the complete original word write is bound at this separate aperture.
 * Full-value retention and reset0 are virtual conventions, with no IRQ,
 * forwarding, security or completion meaning assigned to this input.
 */
static MemTxResult nikon_signal_configuration_read(void *opaque, hwaddr offset,
                                                uint64_t *value, unsigned size,
                                                MemTxAttrs attrs)
{
    NikonSignalState *s = opaque;

    *value = 0;
    nikon_access_error(&s->reported_error, s->configuration_input.addr + offset, size,
                      false, 0, "signal-configuration-input-not-supported");
    return MEMTX_ERROR;
}

static MemTxResult nikon_signal_configuration_write(void *opaque, hwaddr offset,
                                                 uint64_t value, unsigned size,
                                                 MemTxAttrs attrs)
{
    NikonSignalState *s = opaque;

    if (offset || size != 4) {
        nikon_access_error(&s->reported_error, s->configuration_input.addr + offset, size,
                          true, value,
                          "signal-configuration-input-not-supported");
        return MEMTX_ERROR;
    }
    s->opaque_configuration = value;
    return MEMTX_OK;
}

static const MemoryRegionOps nikon_signal_configuration_ops = {
    .read_with_attrs = nikon_signal_configuration_read,
    .write_with_attrs = nikon_signal_configuration_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
};

static void nikon_signal_reset(DeviceState *dev)
{
    NikonSignalState *s = NIKON_SIGNAL(dev);
    unsigned i;

    s->released = s->gates = s->masks = s->debounce = s->config = 0;
    s->filtered = s->pending = 0;
    s->opaque_configuration = 0;
    s->reported_error = false;
    /* Preserve live provider levels; only cold disconnected inputs are low. */
    clock_update_hz(s->clock, 1000000);
    for (i = 0; i < 5; i++) {
        timer_del(s->route[i].timer);
    }
    nikon_signal_outputs(s);
}

static void nikon_signal_init(Object *obj)
{
    NikonSignalState *s = NIKON_SIGNAL(obj);
    unsigned i;

    s->clock = qdev_init_clock_out(DEVICE(obj), "nominal-sample-clock");
    memory_region_init_io(&s->regs, obj, &nikon_signal_ops, s,
                          "nikon.signal", 0x1c);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
    memory_region_init_io(&s->configuration_input, obj,
                          &nikon_signal_configuration_ops, s,
                          "nikon.signal-configuration-input", 4);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->configuration_input);
    qdev_init_gpio_in_named(DEVICE(obj), nikon_signal_input, "signal", 5);
    for (i = 0; i < 5; i++) {
        NikonSignalRoute *r = &s->route[i];

        r->owner = s;
        r->index = i;
        r->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, nikon_signal_sample, r);
        sysbus_init_irq(SYS_BUS_DEVICE(obj), &r->irq);
    }
}

static void nikon_signal_finalize(Object *obj)
{
    NikonSignalState *s = NIKON_SIGNAL(obj);
    unsigned i;

    for (i = 0; i < 5; i++) {
        timer_free(s->route[i].timer);
    }
}

static const VMStateDescription nikon_signal_vmstate = {
    .name = "nikon-signal",
    .unmigratable = true,
};

static void nikon_signal_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &nikon_signal_vmstate;
    dc->user_creatable = false;
    device_class_set_legacy_reset(dc, nikon_signal_reset);
}

static const TypeInfo nikon_signal_type = {
    .name = TYPE_NIKON_SIGNAL,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonSignalState),
    .instance_init = nikon_signal_init,
    .instance_finalize = nikon_signal_finalize,
    .class_init = nikon_signal_class_init,
};

static void nikon_signal_register_types(void)
{
    type_register_static(&nikon_signal_type);
}
type_init(nikon_signal_register_types)
