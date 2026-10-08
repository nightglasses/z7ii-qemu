/*
 * Original system-configuration object, distinct from the clock controller.
 * Functional reset inputs and implemented indexed bank bits are explicit.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "hw/core/sysbus.h"
#include "z7ii.h"

#define SYSCONFIG_BASE 0x04280000ULL
OBJECT_DECLARE_SIMPLE_TYPE(Z7IISysconfigState, Z7II_SYSCONFIG)

struct Z7IISysconfigState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    uint32_t config[8];
    uint32_t initialized;
    uint32_t banks[4];
    bool reported_error;
};

static const unsigned config_offsets[] = {
    4, 8, 0x24, 0x28, 0x30, 0x74, 0x34, 0x38,
};
/* Immediate original caller indices, not physical module names. */
static const uint32_t bank_masks[] = {
    0x01001e07, 0x01ff01c0, 0x1803dfc0, 0x00000444,
};

static MemTxResult z7ii_sysconfig_error(Z7IISysconfigState *s, hwaddr offset,
                                       unsigned size, bool write,
                                       uint64_t value, const char *reason)
{
    z7ii_access_error(&s->reported_error, SYSCONFIG_BASE + offset, size,
                       write, value, reason);
    return MEMTX_ERROR;
}

static MemTxResult z7ii_sysconfig_read(void *opaque, hwaddr offset,
                                      uint64_t *value, unsigned size,
                                      MemTxAttrs attrs)
{
    Z7IISysconfigState *s = opaque;
    unsigned i;

    *value = 0;
    if (size != 4 || (offset & 3)) {
        goto unsupported;
    }
    if (offset >= 0x7c && offset <= 0x88) {
        *value = s->banks[(offset - 0x7c) / 4];
        return MEMTX_OK;
    }
    /* Named virtual strap inputs; their physical interpretation is unknown. */
    if (offset == 0x20 || offset == 0x2c) {
        return MEMTX_OK;
    }
    for (i = 0; i < ARRAY_SIZE(config_offsets); i++) {
        if (offset == config_offsets[i]) {
            if (!(s->initialized & (1U << i))) {
                return z7ii_sysconfig_error(s, offset, size, false, 0,
                                            "sysconfig-before-guest-write");
            }
            *value = s->config[i];
            return MEMTX_OK;
        }
    }
unsupported:
    return z7ii_sysconfig_error(s, offset, size, false, 0,
                                "sysconfig-access-not-supported");
}

static MemTxResult z7ii_sysconfig_write(void *opaque, hwaddr offset,
                                       uint64_t value, unsigned size,
                                       MemTxAttrs attrs)
{
    Z7IISysconfigState *s = opaque;
    unsigned i;

    if (size != 4 || (offset & 3)) {
        goto unsupported;
    }
    if (offset >= 0x7c && offset <= 0x88) {
        i = (offset - 0x7c) / 4;
        if (value & ~bank_masks[i]) {
            return z7ii_sysconfig_error(s, offset, size, true, value,
                                        "sysconfig-bank-bits-not-supported");
        }
        s->banks[i] = value;
        return MEMTX_OK;
    }
    for (i = 0; i < ARRAY_SIZE(config_offsets); i++) {
        if (offset == config_offsets[i]) {
            if ((offset == 0x24 && value > 1) ||
                (offset == 0x74 && value > 0xff) ||
                (offset == 0x34 && (value & ~0xf0000000U)) ||
                (offset == 0x38 && (value & ~0x00ff0000U))) {
                return z7ii_sysconfig_error(s, offset, size, true, value,
                                            "sysconfig-field-not-supported");
            }
            s->config[i] = value;
            s->initialized |= 1U << i;
            return MEMTX_OK;
        }
    }
unsupported:
    return z7ii_sysconfig_error(s, offset, size, true, value,
                                "sysconfig-access-not-supported");
}

static const MemoryRegionOps z7ii_sysconfig_ops = {
    .read_with_attrs = z7ii_sysconfig_read,
    .write_with_attrs = z7ii_sysconfig_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
};

static void z7ii_sysconfig_reset(DeviceState *dev)
{
    Z7IISysconfigState *s = Z7II_SYSCONFIG(dev);

    /* Original boot RMW selectors use explicit virtual pinmux reset zero. */
    s->initialized = (1U << 6) | (1U << 7);
    memset(s->config, 0, sizeof(s->config));
    /* Functional control reset: clear all implemented indexed requests. */
    memset(s->banks, 0, sizeof(s->banks));
    s->reported_error = false;
}

static void z7ii_sysconfig_init(Object *obj)
{
    Z7IISysconfigState *s = Z7II_SYSCONFIG(obj);

    memory_region_init_io(&s->regs, obj, &z7ii_sysconfig_ops, s,
                          "z7ii.sysconfig", 0x8c);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
}

static const VMStateDescription z7ii_sysconfig_vmstate = {
    .name = "z7ii-sysconfig",
    .unmigratable = true,
};

static void z7ii_sysconfig_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &z7ii_sysconfig_vmstate;
    device_class_set_legacy_reset(dc, z7ii_sysconfig_reset);
    dc->user_creatable = false;
}

static const TypeInfo z7ii_sysconfig_type = {
    .name = TYPE_Z7II_SYSCONFIG,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Z7IISysconfigState),
    .instance_init = z7ii_sysconfig_init,
    .class_init = z7ii_sysconfig_class_init,
};

static void z7ii_sysconfig_register_types(void)
{
    type_register_static(&z7ii_sysconfig_type);
}
type_init(z7ii_sysconfig_register_types)
