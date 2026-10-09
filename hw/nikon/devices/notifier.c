/*
 * Source-bound parameter and three-notification interface.
 * External providers alone publish events and busy state. Active-low status,
 * acknowledgement, cold state and retained readback are functional conventions;
 * no physical endpoint, clock, sample format or DMA protocol is inferred.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "hw/nikon/devices.h"
#include "hw/nikon/notifier.h"

OBJECT_DECLARE_SIMPLE_TYPE(NikonNotifierState, NIKON_NOTIFIER)

struct NikonNotifierState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    qemu_irq irq;
    uint32_t control, parameters[6];
    uint8_t pending, levels;
    bool busy, reported_error;
};

static void notifier_irq(NikonNotifierState *s)
{
    qemu_set_irq(s->irq, !!s->pending);
}

static void notifier_event(void *opaque, int n, int level)
{
    NikonNotifierState *s = opaque;
    uint8_t bit = 1U << n;
    bool rising = level && !(s->levels & bit);

    s->levels = (s->levels & ~bit) | (level ? bit : 0);
    if (rising) {
        s->pending |= bit;
        notifier_irq(s);
    }
}

static void notifier_busy(void *opaque, int n, int level)
{
    NikonNotifierState *s = opaque;

    s->busy = !!level;
}

static MemTxResult notifier_error(NikonNotifierState *s, hwaddr offset,
                                  unsigned size, bool write, uint64_t value)
{
    nikon_access_error(&s->reported_error, s->regs.addr + offset, size,
                      write, value, "notifier-access-not-supported");
    return MEMTX_ERROR;
}

static MemTxResult notifier_read(void *opaque, hwaddr offset, uint64_t *value,
                                unsigned size, MemTxAttrs attrs)
{
    NikonNotifierState *s = opaque;

    *value = 0;
    if (size != 4 || (offset & 3)) {
        return notifier_error(s, offset, size, false, 0);
    }
    if (!offset) {
        *value = s->control;
    } else if (offset >= 4 && offset <= 0x18) {
        *value = s->parameters[offset / 4 - 1];
        if (offset == 4) {
            /* The guest inverts these three bits before acknowledgement. */
            *value |= ~s->pending & 7;
        }
    } else if (offset == 0x20) {
        *value = s->busy;
    } else {
        return notifier_error(s, offset, size, false, 0);
    }
    return MEMTX_OK;
}

static MemTxResult notifier_write(void *opaque, hwaddr offset, uint64_t value,
                                 unsigned size, MemTxAttrs attrs)
{
    NikonNotifierState *s = opaque;
    uint32_t mask;

    if (size != 4 || (offset & 3)) {
        return notifier_error(s, offset, size, true, value);
    }
    switch (offset) {
    case 0:
        if (value & ~0xfU) {
            return notifier_error(s, offset, size, true, value);
        }
        /* Complete source RMW fields, without unbound start/reset effects. */
        s->control = value;
        break;
    case 4:
        if (value & ~0xffffU) {
            return notifier_error(s, offset, size, true, value);
        }
        s->parameters[0] = value & ~7U;
        s->pending &= ~(value & 7);
        notifier_irq(s);
        break;
    case 8:
    case 0xc:
    case 0x10:
    case 0x14:
    case 0x18:
        mask = offset <= 0xc ? 0xff : UINT32_MAX;
        if (value & ~mask) {
            return notifier_error(s, offset, size, true, value);
        }
        s->parameters[offset / 4 - 1] = value;
        break;
    default:
        return notifier_error(s, offset, size, true, value);
    }
    return MEMTX_OK;
}

static const MemoryRegionOps notifier_ops = {
    .read_with_attrs = notifier_read,
    .write_with_attrs = notifier_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
};

static void notifier_reset(DeviceState *dev)
{
    NikonNotifierState *s = NIKON_NOTIFIER(dev);

    s->control = 0;
    memset(s->parameters, 0, sizeof(s->parameters));
    s->pending = 0;
    s->reported_error = false;
    /* Keep real provider levels; reset never synthesizes a new event. */
    notifier_irq(s);
}

static void notifier_init(Object *obj)
{
    NikonNotifierState *s = NIKON_NOTIFIER(obj);

    memory_region_init_io(&s->regs, obj, &notifier_ops, s, "nikon.notifier", 0x24);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
    qdev_init_gpio_in_named(DEVICE(obj), notifier_event, "event", 3);
    qdev_init_gpio_in_named(DEVICE(obj), notifier_busy, "busy", 1);
}

static const VMStateDescription notifier_vmstate = {
    .name = "nikon-notifier", .unmigratable = true,
};

static void notifier_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &notifier_vmstate;
    dc->user_creatable = false;
    device_class_set_legacy_reset(dc, notifier_reset);
}

static const TypeInfo notifier_type = {
    .name = TYPE_NIKON_NOTIFIER,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonNotifierState),
    .instance_init = notifier_init,
    .class_init = notifier_class_init,
};

static void notifier_register_types(void)
{
    type_register_static(&notifier_type);
}
type_init(notifier_register_types)
