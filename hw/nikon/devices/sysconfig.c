/*
 * Original system-configuration object, distinct from the clock controller.
 * Full-word configuration retention and cold zero are functional assumptions;
 * physical writable bits and effects of these indexed words are unknown.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "hw/core/sysbus.h"
#include "hw/core/qdev-properties.h"
#include "hw/nikon/devices.h"

OBJECT_DECLARE_SIMPLE_TYPE(NikonSysconfigState, NIKON_SYSCONFIG)

struct NikonSysconfigState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    uint32_t config[11];
    uint32_t initialized;
    uint32_t banks[5];
    uint8_t identity, variant;
    uint16_t strapwide;
    uint8_t strapnarrow;
    bool reported_error;
};

static const unsigned config_offsets[] = {
    4, 8, 0x24, 0x28, 0x30, 0x74, 0x34, 0x38,
    /* 103c5d68 with source wrapper indices0/1/2: full-word replacement. */
    0x64, 0x68, 0x6c,
};

static MemTxResult nikon_sysconfig_error(NikonSysconfigState *s, hwaddr offset,
                                       unsigned size, bool write,
                                       uint64_t value, const char *reason)
{
    nikon_access_error(&s->reported_error, s->regs.addr + offset, size,
                       write, value, reason);
    return MEMTX_ERROR;
}

static MemTxResult nikon_sysconfig_read(void *opaque, hwaddr offset,
                                      uint64_t *value, unsigned size,
                                      MemTxAttrs attrs)
{
    NikonSysconfigState *s = opaque;
    unsigned i;

    *value = 0;
    if (size != 4 || (offset & 3)) {
        goto unsupported;
    }
    if (offset >= 0x78 && offset <= 0x88) {
        *value = s->banks[(offset - 0x78) / 4];
        return MEMTX_OK;
    }
    /* Original getter returns low six bits and the top nibble separately.
     * These stable board inputs survive guest configuration and reset. */
    if (offset == 0) {
        *value = s->identity | ((uint32_t)s->variant << 28);
        return MEMTX_OK;
    }
    /* Original getters consume ten and five bits respectively. */
    if (offset == 0x20 || offset == 0x2c) {
        *value = offset == 0x20 ? s->strapwide : s->strapnarrow;
        return MEMTX_OK;
    }
    for (i = 0; i < ARRAY_SIZE(config_offsets); i++) {
        if (offset == config_offsets[i]) {
            if (!(s->initialized & (1U << i))) {
                return nikon_sysconfig_error(s, offset, size, false, 0,
                                            "sysconfig-before-guest-write");
            }
            *value = s->config[i];
            return MEMTX_OK;
        }
    }
unsupported:
    return nikon_sysconfig_error(s, offset, size, false, 0,
                                "sysconfig-access-not-supported");
}

static MemTxResult nikon_sysconfig_write(void *opaque, hwaddr offset,
                                       uint64_t value, unsigned size,
                                       MemTxAttrs attrs)
{
    NikonSysconfigState *s = opaque;
    unsigned i;

    if (size != 4 || (offset & 3)) {
        goto unsupported;
    }
    if (offset >= 0x78 && offset <= 0x88) {
        i = (offset - 0x78) / 4;
        /* Bootstrap and dynamic bit RMW share full guest-written words. */
        s->banks[i] = value;
        return MEMTX_OK;
    }
    for (i = 0; i < ARRAY_SIZE(config_offsets); i++) {
        if (offset == config_offsets[i]) {
            if ((offset == 0x24 && value > 1) ||
                (offset == 0x74 && value > 0xff) ||
                (offset == 0x34 && (value & ~0xf0000000U)) ||
                (offset == 0x38 && (value & ~0x00ff0000U))) {
                return nikon_sysconfig_error(s, offset, size, true, value,
                                            "sysconfig-field-not-supported");
            }
            s->config[i] = value;
            s->initialized |= 1U << i;
            return MEMTX_OK;
        }
    }
unsupported:
    return nikon_sysconfig_error(s, offset, size, true, value,
                                "sysconfig-access-not-supported");
}

static const MemoryRegionOps nikon_sysconfig_ops = {
    .read_with_attrs = nikon_sysconfig_read,
    .write_with_attrs = nikon_sysconfig_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
};

static void nikon_sysconfig_reset(DeviceState *dev)
{
    NikonSysconfigState *s = NIKON_SYSCONFIG(dev);

    /* Original boot RMW selectors use explicit virtual pinmux reset zero. */
    s->initialized = (1U << 6) | (1U << 7);
    memset(s->config, 0, sizeof(s->config));
    /* Named cold configuration profile; no peripheral reset or IRQ effect. */
    memset(s->banks, 0, sizeof(s->banks));
    s->reported_error = false;
}

static void nikon_sysconfig_init(Object *obj)
{
    NikonSysconfigState *s = NIKON_SYSCONFIG(obj);

    memory_region_init_io(&s->regs, obj, &nikon_sysconfig_ops, s,
                          "nikon.sysconfig", 0x8c);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
}

static const VMStateDescription nikon_sysconfig_vmstate = {
    .name = "nikon-sysconfig",
    .unmigratable = true,
};

static const Property nikon_sysconfig_properties[] = {
    DEFINE_PROP_UINT8("identity", NikonSysconfigState, identity, 0),
    DEFINE_PROP_UINT8("variant", NikonSysconfigState, variant, 0),
    DEFINE_PROP_UINT16("strapwide", NikonSysconfigState, strapwide, 0),
    DEFINE_PROP_UINT8("strapnarrow", NikonSysconfigState, strapnarrow, 0),
};

static void nikon_sysconfig_realize(DeviceState *dev, Error **errp)
{
    NikonSysconfigState *s = NIKON_SYSCONFIG(dev);

    if (s->identity > 0x3f || s->variant > 0xf ||
        s->strapwide > 0x3ff || s->strapnarrow > 0x1f) {
        error_setg(errp, "sysconfig input exceeds recovered field width");
    }
}

static void nikon_sysconfig_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &nikon_sysconfig_vmstate;
    dc->realize = nikon_sysconfig_realize;
    device_class_set_props(dc, nikon_sysconfig_properties);
    device_class_set_legacy_reset(dc, nikon_sysconfig_reset);
    dc->user_creatable = false;
}

static const TypeInfo nikon_sysconfig_type = {
    .name = TYPE_NIKON_SYSCONFIG,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonSysconfigState),
    .instance_init = nikon_sysconfig_init,
    .class_init = nikon_sysconfig_class_init,
};

static void nikon_sysconfig_register_types(void)
{
    type_register_static(&nikon_sysconfig_type);
}
type_init(nikon_sysconfig_register_types)
