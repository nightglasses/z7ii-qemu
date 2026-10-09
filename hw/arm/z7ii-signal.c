/*
 * Five-route signal-control functional profile, not a bound physical IP.
 * The original interface supplies permutations and fields. Reset/gate/mask
 * polarity, raw/filtered banks, W1C and 1MHz debounce are virtual assumptions.
 * External inputs alone produce sampled edges. Five outputs aggregate to IRQ237.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "migration/vmstate.h"
#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "hw/core/qdev-clock.h"
#include "z7ii.h"

#define SIGNAL_BASE 0x2b000000ULL
OBJECT_DECLARE_SIMPLE_TYPE(Z7IISignalState, Z7II_SIGNAL)

typedef struct Z7IISignalRoute {
    Z7IISignalState *owner;
    QEMUTimer *timer;
    qemu_irq irq;
    unsigned index;
} Z7IISignalRoute;

struct Z7IISignalState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    MemoryRegion configuration_input;
    Clock *clock;
    Z7IISignalRoute route[5];
    uint32_t released, gates, masks, debounce, config;
    uint32_t opaque_configuration;
    uint16_t pending;
    uint8_t raw, filtered;
    bool reported_error;
};

static const unsigned signal_bits[] = { 0, 1, 4, 2, 3 };
static const unsigned signal_gates[] = { 4, 5, 3, 2, 2 };

static bool z7ii_signal_active(Z7IISignalState *s, unsigned i)
{
    return (s->released & (1U << signal_bits[i])) &&
           (s->gates & (1U << signal_gates[i]));
}

static void z7ii_signal_outputs(Z7IISignalState *s)
{
    unsigned i;

    for (i = 0; i < 5; i++) {
        unsigned pair = 3U << (2 * signal_bits[i]);

        qemu_set_irq(s->route[i].irq, !!(s->pending & s->masks & pair));
    }
}

static void z7ii_signal_sample(void *opaque)
{
    Z7IISignalRoute *r = opaque;
    Z7IISignalState *s = r->owner;
    unsigned bit = 1U << signal_bits[r->index];
    bool level = !!(s->raw & bit);

    if (!z7ii_signal_active(s, r->index)) {
        return;
    }
    if (!!(s->filtered & bit) != level) {
        s->filtered = (s->filtered & ~bit) | (level ? bit : 0);
        s->pending |= 1U << (2 * signal_bits[r->index] + !level);
        z7ii_signal_outputs(s);
    }
}

static void z7ii_signal_rebase(Z7IISignalState *s, unsigned i)
{
    unsigned bit = 1U << signal_bits[i];

    timer_del(s->route[i].timer);
    if (z7ii_signal_active(s, i)) {
        s->filtered = (s->filtered & ~bit) | (s->raw & bit);
    } else {
        s->filtered &= ~bit;
        s->pending &= ~(3U << (2 * signal_bits[i]));
    }
}

static void z7ii_signal_input(void *opaque, int n, int level)
{
    Z7IISignalState *s = opaque;
    unsigned bit = 1U << signal_bits[n];
    unsigned code;

    if (!!(s->raw & bit) == !!level) {
        return;
    }
    s->raw = (s->raw & ~bit) | (level ? bit : 0);
    timer_del(s->route[n].timer);
    if (!z7ii_signal_active(s, n)) {
        return;
    }
    if (!(s->debounce & bit)) {
        z7ii_signal_sample(&s->route[n]);
        return;
    }
    code = (s->debounce >> (8 + 4 * signal_bits[n])) & 3;
    timer_mod(s->route[n].timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
              clock_ticks_to_ns(s->clock, 1U << code));
}

static MemTxResult z7ii_signal_error(Z7IISignalState *s, hwaddr offset,
                                    unsigned size, bool write, uint64_t value)
{
    z7ii_access_error(&s->reported_error, SIGNAL_BASE + offset, size,
                      write, value, "signal-access-not-supported");
    return MEMTX_ERROR;
}

static MemTxResult z7ii_signal_read(void *opaque, hwaddr offset,
                                   uint64_t *value, unsigned size,
                                   MemTxAttrs attrs)
{
    Z7IISignalState *s = opaque;

    *value = 0;
    if (size != 4 || (offset & 3)) {
        return z7ii_signal_error(s, offset, size, false, 0);
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
        return z7ii_signal_error(s, offset, size, false, 0);
    }
    return MEMTX_OK;
}

static MemTxResult z7ii_signal_write(void *opaque, hwaddr offset,
                                    uint64_t value, unsigned size,
                                    MemTxAttrs attrs)
{
    Z7IISignalState *s = opaque;
    uint32_t changed;
    unsigned i;

    if (size != 4 || (offset & 3)) {
        return z7ii_signal_error(s, offset, size, true, value);
    }
    switch (offset) {
    case 0:
        if (value & ~0x1fU) {
            return z7ii_signal_error(s, offset, size, true, value);
        }
        changed = s->released ^ value;
        s->released = value;
        for (i = 0; i < 5; i++) {
            if (changed & (1U << signal_bits[i])) {
                z7ii_signal_rebase(s, i);
            }
        }
        break;
    case 4:
        if (value & ~0x3cU) {
            return z7ii_signal_error(s, offset, size, true, value);
        }
        changed = s->gates ^ value;
        s->gates = value;
        for (i = 0; i < 5; i++) {
            if (changed & (1U << signal_gates[i])) {
                z7ii_signal_rebase(s, i);
            }
        }
        break;
    case 8:
    case 0xc:
        if (value & ~0x3ffU) {
            return z7ii_signal_error(s, offset, size, true, value);
        }
        if (offset == 8) {
            s->pending &= ~value;
        } else {
            s->masks = value;
        }
        break;
    case 0x14:
        if (value & ~0x0333331fU) {
            return z7ii_signal_error(s, offset, size, true, value);
        }
        changed = s->debounce ^ value;
        s->debounce = value;
        for (i = 0; i < 5; i++) {
            unsigned bit = signal_bits[i];

            if (changed & ((1U << bit) | (3U << (8 + 4 * bit)))) {
                z7ii_signal_rebase(s, i);
            }
        }
        break;
    case 0x18:
        s->config = value;
        break;
    default:
        return z7ii_signal_error(s, offset, size, true, value);
    }
    z7ii_signal_outputs(s);
    return MEMTX_OK;
}

static const MemoryRegionOps z7ii_signal_ops = {
    .read_with_attrs = z7ii_signal_read,
    .write_with_attrs = z7ii_signal_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
};

/*
 * Only the complete original word write is bound at this separate aperture.
 * Full-value retention and reset0 are virtual conventions, with no IRQ,
 * forwarding, security or completion meaning assigned to this input.
 */
static MemTxResult z7ii_signal_configuration_read(void *opaque, hwaddr offset,
                                                uint64_t *value, unsigned size,
                                                MemTxAttrs attrs)
{
    Z7IISignalState *s = opaque;

    *value = 0;
    z7ii_access_error(&s->reported_error, 0x28000000ULL + offset, size,
                      false, 0, "signal-configuration-input-not-supported");
    return MEMTX_ERROR;
}

static MemTxResult z7ii_signal_configuration_write(void *opaque, hwaddr offset,
                                                 uint64_t value, unsigned size,
                                                 MemTxAttrs attrs)
{
    Z7IISignalState *s = opaque;

    if (offset || size != 4) {
        z7ii_access_error(&s->reported_error, 0x28000000ULL + offset, size,
                          true, value,
                          "signal-configuration-input-not-supported");
        return MEMTX_ERROR;
    }
    s->opaque_configuration = value;
    return MEMTX_OK;
}

static const MemoryRegionOps z7ii_signal_configuration_ops = {
    .read_with_attrs = z7ii_signal_configuration_read,
    .write_with_attrs = z7ii_signal_configuration_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
};

static void z7ii_signal_reset(DeviceState *dev)
{
    Z7IISignalState *s = Z7II_SIGNAL(dev);
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
    z7ii_signal_outputs(s);
}

static void z7ii_signal_init(Object *obj)
{
    Z7IISignalState *s = Z7II_SIGNAL(obj);
    unsigned i;

    s->clock = qdev_init_clock_out(DEVICE(obj), "nominal-sample-clock");
    memory_region_init_io(&s->regs, obj, &z7ii_signal_ops, s,
                          "z7ii.signal", 0x1c);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
    memory_region_init_io(&s->configuration_input, obj,
                          &z7ii_signal_configuration_ops, s,
                          "z7ii.signal-configuration-input", 4);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->configuration_input);
    qdev_init_gpio_in_named(DEVICE(obj), z7ii_signal_input, "signal", 5);
    for (i = 0; i < 5; i++) {
        Z7IISignalRoute *r = &s->route[i];

        r->owner = s;
        r->index = i;
        r->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, z7ii_signal_sample, r);
        sysbus_init_irq(SYS_BUS_DEVICE(obj), &r->irq);
    }
}

static void z7ii_signal_finalize(Object *obj)
{
    Z7IISignalState *s = Z7II_SIGNAL(obj);
    unsigned i;

    for (i = 0; i < 5; i++) {
        timer_free(s->route[i].timer);
    }
}

static const VMStateDescription z7ii_signal_vmstate = {
    .name = "z7ii-signal",
    .unmigratable = true,
};

static void z7ii_signal_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &z7ii_signal_vmstate;
    dc->user_creatable = false;
    device_class_set_legacy_reset(dc, z7ii_signal_reset);
}

static const TypeInfo z7ii_signal_type = {
    .name = TYPE_Z7II_SIGNAL,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Z7IISignalState),
    .instance_init = z7ii_signal_init,
    .instance_finalize = z7ii_signal_finalize,
    .class_init = z7ii_signal_class_init,
};

static void z7ii_signal_register_types(void)
{
    type_register_static(&z7ii_signal_type);
}
type_init(z7ii_signal_register_types)
