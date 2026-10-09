/*
 * Finite 2406 opaque configuration / unavailable status profile.
 * Source 54f43cb3d5a661467e343c9d4a6939723da6aef1. Cold/readback zero
 * is a virtual convention, not physical reset or valid running status.
 * Request handshake, service154 and IRQ229 producers remain unbound.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "hw/core/sysbus.h"
#include "z7ii.h"

OBJECT_DECLARE_SIMPLE_TYPE(Z7IIControl2406State, Z7II_CONTROL2406)

struct Z7IIControl2406State {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    uint32_t config[0x34 / 4];
    bool reported_error;
};

static MemTxResult z7ii_2406_error(Z7IIControl2406State *s, hwaddr offset,
                                  unsigned size, bool write, uint64_t value)
{
    z7ii_access_error(&s->reported_error, 0x24060000 + offset, size,
                      write, value,
                      "control2406-access-or-activation-not-supported");
    return MEMTX_ERROR;
}

static bool z7ii_2406_config(hwaddr offset, uint32_t *mask)
{
    switch (offset) {
    case 0:
        *mask = 1;
        return true;
    case 8: case 0x10: case 0x14:
        *mask = UINT32_MAX;
        return true;
    case 0xc:
        *mask = 0xff;
        return true;
    default:
        return false;
    }
}

static MemTxResult z7ii_2406_read(void *opaque, hwaddr offset,
                                 uint64_t *value, unsigned size,
                                 MemTxAttrs attrs)
{
    Z7IIControl2406State *s = opaque;
    uint32_t mask;

    *value = 0;
    if (size != 4 || (offset & 3)) {
        return z7ii_2406_error(s, offset, size, false, 0);
    }
    if (z7ii_2406_config(offset, &mask) || offset == 0x20) {
        *value = s->config[offset / 4];
    } else if (offset != 0x24 && offset != 0x30) {
        return z7ii_2406_error(s, offset, size, false, 0);
    }
    /* 24/30 are explicitly unavailable, never read-triggered ready. */
    return MEMTX_OK;
}

static MemTxResult z7ii_2406_write(void *opaque, hwaddr offset,
                                  uint64_t value, unsigned size,
                                  MemTxAttrs attrs)
{
    Z7IIControl2406State *s = opaque;
    uint32_t mask;

    if (size != 4 || (offset & 3)) {
        return z7ii_2406_error(s, offset, size, true, value);
    }
    if (offset == 0x20) {
        /* Neither changing handshake bits nor other unbound bits is supported. */
        if (value != s->config[offset / 4]) {
            return z7ii_2406_error(s, offset, size, true, value);
        }
    } else if (z7ii_2406_config(offset, &mask) && !(value & ~mask)) {
        s->config[offset / 4] = value;
    } else {
        return z7ii_2406_error(s, offset, size, true, value);
    }
    return MEMTX_OK;
}

static const MemoryRegionOps z7ii_2406_ops = {
    .read_with_attrs = z7ii_2406_read,
    .write_with_attrs = z7ii_2406_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
};

static void z7ii_2406_reset(DeviceState *dev)
{
    Z7IIControl2406State *s = Z7II_CONTROL2406(dev);

    memset(s->config, 0, sizeof(s->config));
    s->reported_error = false;
}

static void z7ii_2406_init(Object *obj)
{
    Z7IIControl2406State *s = Z7II_CONTROL2406(obj);

    memory_region_init_io(&s->regs, obj, &z7ii_2406_ops, s,
                          "z7ii.control2406", 0x34);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
}

static const VMStateDescription z7ii_2406_vmstate = {
    .name = "z7ii-control2406",
    .unmigratable = true,
};

static void z7ii_2406_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &z7ii_2406_vmstate;
    dc->user_creatable = false;
    device_class_set_legacy_reset(dc, z7ii_2406_reset);
}

static const TypeInfo z7ii_2406_type = {
    .name = TYPE_Z7II_CONTROL2406,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Z7IIControl2406State),
    .instance_init = z7ii_2406_init,
    .class_init = z7ii_2406_class_init,
};

static void z7ii_2406_register_types(void)
{
    type_register_static(&z7ii_2406_type);
}
type_init(z7ii_2406_register_types)
