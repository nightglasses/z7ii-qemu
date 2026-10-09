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
#include "z7ii.h"

#define CONTROL_BASE 0x20203000ULL
#define CONFIG_WORDS (0x100 / 4)
OBJECT_DECLARE_SIMPLE_TYPE(Z7IIControl20203State, Z7II_CONTROL20203)

struct Z7IIControl20203State {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    qemu_irq irq;
    uint32_t config[CONFIG_WORDS], pending;
    uint8_t events;
    bool reported_error;
};

/* Only actual MMIO setters, never software-shadow BFI offsets or masks. */
static bool z7ii_control_config(hwaddr offset, uint32_t *mask)
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

static void z7ii_control_irq(Z7IIControl20203State *s)
{
    /* Offset20 is a full guest parameter, without proven mask semantics. */
    qemu_set_irq(s->irq, !!s->pending);
}

static void z7ii_control_event(void *opaque, int index, int level)
{
    Z7IIControl20203State *s = opaque;
    static const uint32_t pending[] = { 1, 0x400 };
    bool rising = level && !(s->events & (1 << index));

    s->events = (s->events & ~(1 << index)) | (!!level << index);
    if (rising) {
        s->pending |= pending[index];
        z7ii_control_irq(s);
    }
}

static MemTxResult z7ii_control_error(Z7IIControl20203State *s,
                                    hwaddr offset, unsigned size,
                                    bool write, uint64_t value)
{
    z7ii_access_error(&s->reported_error, CONTROL_BASE + offset, size,
                      write, value,
                      "control20203-access-or-activation-not-supported");
    return MEMTX_ERROR;
}

static MemTxResult z7ii_control_read(void *opaque, hwaddr offset,
                                   uint64_t *value, unsigned size,
                                   MemTxAttrs attrs)
{
    Z7IIControl20203State *s = opaque;
    uint32_t mask;

    *value = 0;
    if (size != 4 || (offset & 3)) {
        return z7ii_control_error(s, offset, size, false, 0);
    }
    if (z7ii_control_config(offset, &mask)) {
        *value = s->config[offset / 4];
    } else if (offset == 0x2c) {
        *value = s->pending;
    } else {
        return z7ii_control_error(s, offset, size, false, 0);
    }
    return MEMTX_OK;
}

static MemTxResult z7ii_control_write(void *opaque, hwaddr offset,
                                    uint64_t value, unsigned size,
                                    MemTxAttrs attrs)
{
    Z7IIControl20203State *s = opaque;
    uint32_t mask;

    if (size != 4 || (offset & 3)) {
        return z7ii_control_error(s, offset, size, true, value);
    }
    if (z7ii_control_config(offset, &mask)) {
        if (value & ~mask) {
            return z7ii_control_error(s, offset, size, true, value);
        }
        s->config[offset / 4] = value;
    } else if (offset == 0x2c) {
        s->pending &= ~(value & 0x401);
        z7ii_control_irq(s);
    } else {
        return z7ii_control_error(s, offset, size, true, value);
    }
    return MEMTX_OK;
}

static const MemoryRegionOps z7ii_control_ops = {
    .read_with_attrs = z7ii_control_read,
    .write_with_attrs = z7ii_control_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
};

static void z7ii_control_reset(DeviceState *dev)
{
    Z7IIControl20203State *s = Z7II_CONTROL20203(dev);

    memset(s->config, 0, sizeof(s->config));
    s->pending = 0;
    s->reported_error = false;
    /* Keep actual provider levels; reset is not a new external edge. */
    z7ii_control_irq(s);
}

static void z7ii_control_init(Object *obj)
{
    Z7IIControl20203State *s = Z7II_CONTROL20203(obj);

    memory_region_init_io(&s->regs, obj, &z7ii_control_ops, s,
                          "z7ii.control20203", 0x100);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
    qdev_init_gpio_in_named(DEVICE(obj), z7ii_control_event,
                           "external-event", 2);
}

static const VMStateDescription z7ii_control_vmstate = {
    .name = "z7ii-control20203",
    .unmigratable = true,
};

static void z7ii_control_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &z7ii_control_vmstate;
    dc->user_creatable = false;
    device_class_set_legacy_reset(dc, z7ii_control_reset);
}

static const TypeInfo z7ii_control_type = {
    .name = TYPE_Z7II_CONTROL20203,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Z7IIControl20203State),
    .instance_init = z7ii_control_init,
    .class_init = z7ii_control_class_init,
};

static void z7ii_control_register_types(void)
{
    type_register_static(&z7ii_control_type);
}
type_init(z7ii_control_register_types)
