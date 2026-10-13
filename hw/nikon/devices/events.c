/*
 * Three-cause externally produced event interface.
 * Official SLA source 100578fc acknowledges status bits 1..3; 100579fe
 * updates control bits 10..12. W0C, paired enables and masked cold reset
 * are explicit functional conventions. No timer/data source is inferred.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "hw/nikon/devices.h"
#include "hw/nikon/events.h"

#define EVENTS_CAUSES 0x0eU
#define EVENTS_CONTROL0 0x57ffU
#define EVENTS_CONTROL4 0x7000U
#define EVENTS_ENABLES 0x1fffU
#define EVENTS_DATA 0x03ffU

OBJECT_DECLARE_SIMPLE_TYPE(NikonEventsState, NIKON_EVENTS)

struct NikonEventsState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    qemu_irq irq;
    uint32_t control[2];
    uint32_t enables;
    uint32_t data[2];
    uint8_t pending;
    bool reported_error;
};

static void nikon_events_update(NikonEventsState *s)
{
    qemu_set_irq(s->irq, !!(s->pending & (s->enables >> 9)));
}

bool nikon_events_publish(DeviceState *dev, uint32_t causes, Error **errp)
{
    NikonEventsState *s = NIKON_EVENTS(dev);

    if (!causes || (causes & ~EVENTS_CAUSES)) {
        error_setg(errp, "events provider requires cause bits 1..3");
        return false;
    }
    s->pending |= causes;
    nikon_events_update(s);
    return true;
}

static void nikon_events_publication(void *opaque, int n, int causes)
{
    DeviceState *dev = opaque;

    /* Delivery explicitly publishes events, not an electrical level or edge.
     * Zero is transport deassertion and cannot erase published causes. */
    if (causes) {
        nikon_events_publish(dev, causes, NULL);
    }
}

static MemTxResult nikon_events_error(NikonEventsState *s, hwaddr offset,
                                     unsigned size, bool write, uint64_t value)
{
    nikon_access_error(&s->reported_error, s->regs.addr + offset, size,
                       write, value, "events-access-not-supported");
    return MEMTX_ERROR;
}

static MemTxResult nikon_events_read(void *opaque, hwaddr offset,
                                    uint64_t *value, unsigned size,
                                    MemTxAttrs attrs)
{
    NikonEventsState *s = opaque;

    *value = 0;
    if (size != 4 || (offset & 3)) {
        return nikon_events_error(s, offset, size, false, 0);
    }
    switch (offset) {
    case 0:
    case 4:
        *value = s->control[offset / 4];
        break;
    case 8:
        *value = s->enables;
        break;
    case 0xc:
        *value = s->pending;
        break;
    case 0x40:
    case 0x44:
        *value = s->data[(offset - 0x40) / 4];
        break;
    default:
        return nikon_events_error(s, offset, size, false, 0);
    }
    return MEMTX_OK;
}

static MemTxResult nikon_events_write(void *opaque, hwaddr offset,
                                     uint64_t value, unsigned size,
                                     MemTxAttrs attrs)
{
    NikonEventsState *s = opaque;

    if (size != 4 || (offset & 3)) {
        return nikon_events_error(s, offset, size, true, value);
    }
    switch (offset) {
    case 0:
        if (value & ~EVENTS_CONTROL0) {
            return nikon_events_error(s, offset, size, true, value);
        }
        s->control[0] = value;
        break;
    case 4:
        if (value & ~EVENTS_CONTROL4) {
            return nikon_events_error(s, offset, size, true, value);
        }
        s->control[1] = value;
        break;
    case 8:
        if (value & ~EVENTS_ENABLES) {
            return nikon_events_error(s, offset, size, true, value);
        }
        s->enables = value;
        break;
    case 0xc:
        if (value & ~EVENTS_CAUSES) {
            return nikon_events_error(s, offset, size, true, value);
        }
        /* Ones preserve existing causes; they cannot produce new causes. */
        s->pending &= value;
        break;
    case 0x40:
    case 0x44:
        if (value & ~EVENTS_DATA) {
            return nikon_events_error(s, offset, size, true, value);
        }
        s->data[(offset - 0x40) / 4] = value;
        break;
    default:
        return nikon_events_error(s, offset, size, true, value);
    }
    nikon_events_update(s);
    return MEMTX_OK;
}

static const MemoryRegionOps nikon_events_ops = {
    .read_with_attrs = nikon_events_read,
    .write_with_attrs = nikon_events_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
};

static void nikon_events_reset(DeviceState *dev)
{
    NikonEventsState *s = NIKON_EVENTS(dev);

    memset(s->control, 0, sizeof(s->control));
    s->enables = 0;
    memset(s->data, 0, sizeof(s->data));
    s->pending = 0;
    s->reported_error = false;
    nikon_events_update(s);
}

static void nikon_events_init(Object *obj)
{
    NikonEventsState *s = NIKON_EVENTS(obj);

    memory_region_init_io(&s->regs, obj, &nikon_events_ops, s,
                          "nikon.events", 0x48);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
    qdev_init_gpio_in_named(DEVICE(obj), nikon_events_publication, "publish", 1);
}

static const VMStateDescription nikon_events_vmstate = {
    .name = "nikon-events",
    .unmigratable = true,
};

static void nikon_events_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &nikon_events_vmstate;
    dc->user_creatable = false;
    device_class_set_legacy_reset(dc, nikon_events_reset);
}

static const TypeInfo nikon_events_type = {
    .name = TYPE_NIKON_EVENTS,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonEventsState),
    .instance_init = nikon_events_init,
    .class_init = nikon_events_class_init,
};

static void nikon_events_register_types(void)
{
    type_register_static(&nikon_events_type);
}
type_init(nikon_events_register_types)
