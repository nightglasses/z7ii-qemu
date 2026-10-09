/*
 * Finite 20205 configuration and disconnected external-event profile.
 * Cold readback, opaque controls and W1C are virtual conventions. Packet
 * count is not observable here; no physical bound or reset is inferred.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/nikon/devices.h"

#define CONFIG_WORDS (0x318 / 4)
OBJECT_DECLARE_SIMPLE_TYPE(NikonSharedParameterEventsState, NIKON_SHARED_PARAMETER_EVENTS)

struct NikonSharedParameterEventsState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    qemu_irq irq;
    uint32_t config[CONFIG_WORDS], pending;
    uint8_t events;
    bool reported_error;
};

/* Known physical words, not an invented packet-count register or guard. */
static bool nikon_control_config(hwaddr offset, uint32_t *mask, bool *rmw)
{
    *rmw = false;
    if ((offset >= 0x94 && offset <= 0xb0) ||
        (offset >= 0xdc && offset <= 0xf0) ||
        (offset >= 0x128 && offset <= 0x144) ||
        (offset >= 0x170 && offset <= 0x184)) {
        *mask = 0xf;
        return true;
    }
    if ((offset >= 0xb4 && offset <= 0xd8) ||
        (offset >= 0xf4 && offset <= 0x118) ||
        (offset >= 0x148 && offset <= 0x16c) ||
        (offset >= 0x188 && offset <= 0x1ac)) {
        *mask = 0x7f;
        return true;
    }
    switch (offset) {
    case 0: case 0x300: case 0x68: case 0x84: case 0x8c: case 0x124:
        *mask = 1;
        break;
    case 4: case 0x310: case 0x314:
        *mask = 1;
        *rmw = true;
        break;
    case 0x308: case 0x30c:
        *mask = 3;
        *rmw = true;
        break;
    case 0x40: case 0x44: case 0x88: case 0x90: case 0x120:
        *mask = 3;
        break;
    case 0x48: case 0x50:
        *mask = 0x3ffe;
        break;
    case 0x4c: case 0x54:
        *mask = 0x1ffe;
        break;
    case 0x20: case 0x58:
        *mask = UINT32_MAX;
        break;
    case 0x5c:
        *mask = 0x17ff0;
        *rmw = true;
        break;
    case 0x60:
        *mask = 0xfffffff0;
        *rmw = true;
        break;
    case 0x64:
        *mask = 0x3ff;
        break;
    case 0x6c: case 0x70: case 0x74:
        *mask = 0x7ff;
        break;
    case 0x78: case 0x7c: case 0x80:
        *mask = 0x7fff;
        break;
    case 0x11c:
        /* Original mode setter overwrites its initial bool with full u8. */
        *mask = 0xff;
        break;
    default:
        return false;
    }
    return true;
}

static void nikon_control_irq(NikonSharedParameterEventsState *s)
{
    /* Offset20 is a full parameter, not a proven interrupt mask. */
    qemu_set_irq(s->irq, !!s->pending);
}

static void nikon_control_event(void *opaque, int index, int level)
{
    NikonSharedParameterEventsState *s = opaque;
    static const uint32_t pending[] = { 1, 4 };
    bool rising = level && !(s->events & (1 << index));

    s->events = (s->events & ~(1 << index)) | (!!level << index);
    if (rising) {
        s->pending |= pending[index];
        nikon_control_irq(s);
    }
}

static MemTxResult nikon_control_error(NikonSharedParameterEventsState *s,
                                    hwaddr offset, unsigned size,
                                    bool write, uint64_t value)
{
    nikon_access_error(&s->reported_error, s->regs.addr + offset, size,
                      write, value,
                      "shared-parameter-events-access-or-activation-not-supported");
    return MEMTX_ERROR;
}

static MemTxResult nikon_control_read(void *opaque, hwaddr offset,
                                   uint64_t *value, unsigned size,
                                   MemTxAttrs attrs)
{
    NikonSharedParameterEventsState *s = opaque;
    uint32_t mask;
    bool rmw;

    *value = 0;
    if (size != 4 || (offset & 3)) {
        return nikon_control_error(s, offset, size, false, 0);
    }
    if (nikon_control_config(offset, &mask, &rmw)) {
        *value = s->config[offset / 4];
    } else if (offset == 0x2c) {
        *value = s->pending;
    } else {
        return nikon_control_error(s, offset, size, false, 0);
    }
    return MEMTX_OK;
}

static MemTxResult nikon_control_write(void *opaque, hwaddr offset,
                                    uint64_t value, unsigned size,
                                    MemTxAttrs attrs)
{
    NikonSharedParameterEventsState *s = opaque;
    uint32_t mask;
    bool rmw;

    if (size != 4 || (offset & 3)) {
        return nikon_control_error(s, offset, size, true, value);
    }
    if (nikon_control_config(offset, &mask, &rmw)) {
        uint32_t changed = rmw ? value ^ s->config[offset / 4] : value;

        if (changed & ~mask) {
            return nikon_control_error(s, offset, size, true, value);
        }
        s->config[offset / 4] = value;
    } else if (offset == 0x2c) {
        s->pending &= ~(value & 5);
        nikon_control_irq(s);
    } else {
        return nikon_control_error(s, offset, size, true, value);
    }
    return MEMTX_OK;
}

static const MemoryRegionOps nikon_control_ops = {
    .read_with_attrs = nikon_control_read,
    .write_with_attrs = nikon_control_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
};

static void nikon_control_reset(DeviceState *dev)
{
    NikonSharedParameterEventsState *s = NIKON_SHARED_PARAMETER_EVENTS(dev);

    memset(s->config, 0, sizeof(s->config));
    s->pending = 0;
    s->reported_error = false;
    /* Keep actual provider levels so reset cannot synthesize an edge. */
    nikon_control_irq(s);
}

static void nikon_control_init(Object *obj)
{
    NikonSharedParameterEventsState *s = NIKON_SHARED_PARAMETER_EVENTS(obj);

    memory_region_init_io(&s->regs, obj, &nikon_control_ops, s,
                          "nikon.shared-parameter-events", 0x318);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
    qdev_init_gpio_in_named(DEVICE(obj), nikon_control_event,
                           "external-event", 2);
}

static const VMStateDescription nikon_control_vmstate = {
    .name = "nikon-shared-parameter-events",
    .unmigratable = true,
};

static void nikon_control_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &nikon_control_vmstate;
    dc->user_creatable = false;
    device_class_set_legacy_reset(dc, nikon_control_reset);
}

static const TypeInfo nikon_control_type = {
    .name = TYPE_NIKON_SHARED_PARAMETER_EVENTS,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonSharedParameterEventsState),
    .instance_init = nikon_control_init,
    .class_init = nikon_control_class_init,
};

static void nikon_control_register_types(void)
{
    type_register_static(&nikon_control_type);
}
type_init(nikon_control_register_types)
