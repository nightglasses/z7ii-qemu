/*
 * Three source-bound sixteen-bit counter configuration/status windows.
 * The three software mode setters are supported as fields, without guessing
 * their physical counting clock, pulse generation or external input protocol.
 * Explicit providers alone publish states/events; guest writes only configure
 * and acknowledge. Cold/readback/W0C laws are functional conventions.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "hw/nikon/devices.h"
#include "hw/nikon/counter.h"

OBJECT_DECLARE_SIMPLE_TYPE(NikonCounterState, NIKON_COUNTER)

typedef struct NikonCounterChannel {
    uint16_t quantity, mode;
    uint8_t control, pending, levels, state;
    qemu_irq irq;
} NikonCounterChannel;

struct NikonCounterState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    NikonCounterChannel channel[3];
    bool reported_error;
};

static void counter_irq(NikonCounterChannel *c)
{
    qemu_set_irq(c->irq, !!c->pending);
}

static void counter_event(void *opaque, int n, int level)
{
    NikonCounterState *s = opaque;
    NikonCounterChannel *c = &s->channel[n / 4];
    uint8_t bit = 1U << (n % 4);
    bool rising = level && !(c->levels & bit);

    c->levels = (c->levels & ~bit) | (level ? bit : 0);
    if (rising) {
        c->pending |= bit;
        counter_irq(c);
    }
}

static void counter_state(void *opaque, int n, int level)
{
    NikonCounterState *s = opaque;
    NikonCounterChannel *c = &s->channel[n / 2];
    uint8_t bit = 1U << (n % 2);

    c->state = (c->state & ~bit) | (level ? bit : 0);
}

static MemTxResult counter_error(NikonCounterState *s, hwaddr offset,
                                 unsigned size, bool write, uint64_t value)
{
    nikon_access_error(&s->reported_error, s->regs.addr + offset, size,
                      write, value, "counter-access-not-supported");
    return MEMTX_ERROR;
}

static MemTxResult counter_read(void *opaque, hwaddr offset, uint64_t *value,
                               unsigned size, MemTxAttrs attrs)
{
    NikonCounterState *s = opaque;
    NikonCounterChannel *c;

    *value = 0;
    if (size != 4 || (offset & 3) || offset >= 0x60) {
        return counter_error(s, offset, size, false, 0);
    }
    c = &s->channel[offset / 0x20];
    switch (offset % 0x20) {
    case 4:
        *value = c->quantity;
        break;
    case 0xc:
        *value = c->control | c->state | ((c->pending & 7) << 2);
        break;
    case 0x14:
        *value = c->mode | ((c->pending & 8) ? 0x4000 : 0);
        break;
    default:
        return counter_error(s, offset, size, false, 0);
    }
    return MEMTX_OK;
}

static MemTxResult counter_write(void *opaque, hwaddr offset, uint64_t value,
                                unsigned size, MemTxAttrs attrs)
{
    NikonCounterState *s = opaque;
    NikonCounterChannel *c;

    if (size != 4 || (offset & 3) || offset >= 0x60) {
        return counter_error(s, offset, size, true, value);
    }
    c = &s->channel[offset / 0x20];
    switch (offset % 0x20) {
    case 4:
        if (value & ~0xffffU) {
            return counter_error(s, offset, size, true, value);
        }
        c->quantity = value;
        break;
    case 0xc:
        if (value & ~0x7fU) {
            return counter_error(s, offset, size, true, value);
        }
        c->control = value & 0x60;
        /* Source RMW clears observed flags2/3/4; state0/1 is external. */
        c->pending &= ((value >> 2) & 7) | 8;
        counter_irq(c);
        break;
    case 0x14:
        if (value & ~0xffffU) {
            return counter_error(s, offset, size, true, value);
        }
        c->mode = value & ~0x4000U;
        if (!(value & 0x4000)) {
            c->pending &= ~8;
        }
        /* Retain source submit bit15. It is not a done producer or a clock
         * start: source duration and the external producer remain unbound. */
        counter_irq(c);
        break;
    default:
        return counter_error(s, offset, size, true, value);
    }
    return MEMTX_OK;
}

static const MemoryRegionOps counter_ops = {
    .read_with_attrs = counter_read,
    .write_with_attrs = counter_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
};

static void counter_reset(DeviceState *dev)
{
    NikonCounterState *s = NIKON_COUNTER(dev);

    for (unsigned i = 0; i < ARRAY_SIZE(s->channel); i++) {
        NikonCounterChannel *c = &s->channel[i];

        c->quantity = c->mode = c->control = c->pending = 0;
        /* Live provider levels survive reset without producing new edges. */
        counter_irq(c);
    }
    s->reported_error = false;
}

static void counter_init(Object *obj)
{
    NikonCounterState *s = NIKON_COUNTER(obj);

    memory_region_init_io(&s->regs, obj, &counter_ops, s, "nikon.counter", 0x60);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
    for (unsigned i = 0; i < ARRAY_SIZE(s->channel); i++) {
        sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->channel[i].irq);
    }
    qdev_init_gpio_in_named(DEVICE(obj), counter_event, "event", 12);
    qdev_init_gpio_in_named(DEVICE(obj), counter_state, "state", 6);
}

static const VMStateDescription counter_vmstate = {
    .name = "nikon-counter", .unmigratable = true,
};

static void counter_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &counter_vmstate;
    dc->user_creatable = false;
    device_class_set_legacy_reset(dc, counter_reset);
}

static const TypeInfo counter_type = {
    .name = TYPE_NIKON_COUNTER,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonCounterState),
    .instance_init = counter_init,
    .class_init = counter_class_init,
};

static void counter_register_types(void)
{
    type_register_static(&counter_type);
}
type_init(counter_register_types)
