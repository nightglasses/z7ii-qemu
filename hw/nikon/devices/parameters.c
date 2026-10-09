/*
 * Finite 20203 MMIO configuration and disconnected external-event profile.
 * Guest packed shadow/packet writes remain ordinary guest RAM operations.
 * Cold readback, opaque controls and W1C are named virtual conventions.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/nikon/devices.h"

#define CONFIG_WORDS (0x100 / 4)
OBJECT_DECLARE_SIMPLE_TYPE(NikonParameterEventsState, NIKON_PARAMETER_EVENTS)

struct NikonParameterEventsState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    qemu_irq irq;
    uint32_t config[CONFIG_WORDS], pending;
    uint8_t events;
    bool reported_error;
};

/* Only actual MMIO setters, never software-shadow BFI offsets or masks. */
static bool nikon_control_config(hwaddr offset, uint32_t *mask)
{
    if (offset >= 0xa0 && offset <= 0xfc) {
        *mask = UINT32_MAX;
        return true;
    }
    switch (offset) {
    case 0: case 4: case 0x44: case 0x4c: case 0x50: case 0x54:
    case 0x60: case 0x64: case 0x80: case 0x84:
        *mask = 1;
        break;
    case 0x40: case 0x5c: case 0x6c: case 0x8c:
        *mask = 0xff;
        break;
    case 0x10: case 0x14: case 0x20: case 0x24: case 0x48:
    case 0x68: case 0x70: case 0x88: case 0x90:
        *mask = UINT32_MAX;
        break;
    default:
        return false;
    }
    return true;
}

static void nikon_control_irq(NikonParameterEventsState *s)
{
    /* Offset20 is a full guest parameter, without proven mask semantics. */
    qemu_set_irq(s->irq, !!s->pending);
}

static void nikon_control_event(void *opaque, int index, int level)
{
    NikonParameterEventsState *s = opaque;
    static const uint32_t pending[] = { 1, 0x400 };
    bool rising = level && !(s->events & (1 << index));

    s->events = (s->events & ~(1 << index)) | (!!level << index);
    if (rising) {
        s->pending |= pending[index];
        nikon_control_irq(s);
    }
}

static MemTxResult nikon_control_error(NikonParameterEventsState *s,
                                    hwaddr offset, unsigned size,
                                    bool write, uint64_t value)
{
    nikon_access_error(&s->reported_error, s->regs.addr + offset, size,
                      write, value,
                      "parameter-events-access-or-activation-not-supported");
    return MEMTX_ERROR;
}

static MemTxResult nikon_control_read(void *opaque, hwaddr offset,
                                   uint64_t *value, unsigned size,
                                   MemTxAttrs attrs)
{
    NikonParameterEventsState *s = opaque;
    uint32_t mask;

    *value = 0;
    if (size != 4 || (offset & 3)) {
        return nikon_control_error(s, offset, size, false, 0);
    }
    if (nikon_control_config(offset, &mask)) {
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
    NikonParameterEventsState *s = opaque;
    uint32_t mask;

    if (size != 4 || (offset & 3)) {
        return nikon_control_error(s, offset, size, true, value);
    }
    if (nikon_control_config(offset, &mask)) {
        if (value & ~mask) {
            return nikon_control_error(s, offset, size, true, value);
        }
        s->config[offset / 4] = value;
    } else if (offset == 0x2c) {
        s->pending &= ~(value & 0x401);
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
    NikonParameterEventsState *s = NIKON_PARAMETER_EVENTS(dev);

    memset(s->config, 0, sizeof(s->config));
    s->pending = 0;
    s->reported_error = false;
    /* Keep actual provider levels; reset is not a new external edge. */
    nikon_control_irq(s);
}

static void nikon_control_init(Object *obj)
{
    NikonParameterEventsState *s = NIKON_PARAMETER_EVENTS(obj);

    memory_region_init_io(&s->regs, obj, &nikon_control_ops, s,
                          "nikon.parameter-events", 0x100);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
    qdev_init_gpio_in_named(DEVICE(obj), nikon_control_event,
                           "external-event", 2);
}

static const VMStateDescription nikon_control_vmstate = {
    .name = "nikon-parameter-events",
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
    .name = TYPE_NIKON_PARAMETER_EVENTS,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonParameterEventsState),
    .instance_init = nikon_control_init,
    .class_init = nikon_control_class_init,
};

static void nikon_control_register_types(void)
{
    type_register_static(&nikon_control_type);
}
type_init(nikon_control_register_types)
