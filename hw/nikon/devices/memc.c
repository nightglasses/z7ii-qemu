/* Nikon finite functional device.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "migration/vmstate.h"
#include "hw/core/sysbus.h"
#include "hw/nikon/devices.h"

OBJECT_DECLARE_SIMPLE_TYPE(NikonMemcState, NIKON_MEMC)

struct NikonMemcState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    uint32_t mode;
    uint32_t timing;
    bool mode_initialized;
    bool timing_initialized;
    bool reported_error;
};

static MemTxResult nikon_memc_error(NikonMemcState *s, hwaddr addr,
                                  unsigned size, bool write, uint64_t value,
                                  const char *reason)
{
    nikon_access_error(&s->reported_error, addr, size, write, value, reason);
    return MEMTX_ERROR;
}

static MemTxResult nikon_memc_read(void *opaque, hwaddr offset,
                                 uint64_t *value, unsigned size,
                                 MemTxAttrs attrs)
{
    NikonMemcState *s = opaque;

    *value = 0; /* Invalid unless the transaction below succeeds. */
    if (size != 4 || (offset != 0 && offset != 0x20)) {
        return nikon_memc_error(s, s->regs.addr + offset, size, false, 0,
                               "memc-unsupported-access");
    }
    if ((offset == 0 && !s->mode_initialized) ||
        (offset == 0x20 && !s->timing_initialized)) {
        return nikon_memc_error(s, s->regs.addr + offset, size, false, 0,
                               "memc-uninitialized-read");
    }
    *value = offset == 0 ? s->mode : s->timing;
    return MEMTX_OK;
}

static MemTxResult nikon_memc_write(void *opaque, hwaddr offset,
                                  uint64_t value, unsigned size,
                                  MemTxAttrs attrs)
{
    NikonMemcState *s = opaque;

    if (size != 4 || (offset != 0 && offset != 0x20)) {
        return nikon_memc_error(s, s->regs.addr + offset, size, true, value,
                               "memc-unsupported-access");
    }
    /* Supported functional profiles, not the complete IP's legal values. */
    if (offset == 0) {
        if (value != 0x21 && value != 0x29) {
            return nikon_memc_error(s, s->regs.addr, size, true, value,
                                   "memc-mode-profile-not-supported");
        }
        s->mode = value; /* WDT=1 (x16), PAGE=1, WEOFF=bit3. */
        s->mode_initialized = true;
    } else {
        if (value != 0x153b0902) {
            return nikon_memc_error(s, s->regs.addr + offset, size, true, value,
                                   "memc-timing-profile-not-supported");
        }
        s->timing = value;
        s->timing_initialized = true;
    }
    qemu_log_mask(LOG_GUEST_ERROR, "nikon-memc: write offset=0x%" HWADDR_PRIx
                  " value=0x%" PRIx64 " width=4 accepted-profile\n",
                  offset, value);
    return MEMTX_OK;
}

static const MemoryRegionOps nikon_memc_ops = {
    .read_with_attrs = nikon_memc_read,
    .write_with_attrs = nikon_memc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    /* Dispatch bad widths/offsets too, so rejection records the guest PC. */
    .valid = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
};

static void nikon_memc_reset(DeviceState *dev)
{
    NikonMemcState *s = NIKON_MEMC(dev);

    s->mode_initialized = false;
    s->timing_initialized = false;
    s->reported_error = false;
    /* No hardware reset value is supplied for either register. */
}

static void nikon_memc_init(Object *obj)
{
    NikonMemcState *s = NIKON_MEMC(obj);

    memory_region_init_io(&s->regs, obj, &nikon_memc_ops, s,
                          "nikon.memc", 0x100);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
}

static const VMStateDescription nikon_memc_vmstate = {
    .name = "nikon-memc",
    .unmigratable = true,
};

static void nikon_memc_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &nikon_memc_vmstate;
    device_class_set_legacy_reset(dc, nikon_memc_reset);
    dc->user_creatable = false;
}

MemTxResult nikon_memc_nor_access(DeviceState *dev, hwaddr address,
                                 unsigned size, bool write, uint64_t value)
{
    NikonMemcState *s = NIKON_MEMC(dev);
    const char *reason;

    if (!write) {
        reason = "nor-indirect-read-not-supported";
    } else if (!s->mode_initialized || !s->timing_initialized) {
        reason = "nor-memc-not-initialized";
    } else if (s->mode & 8) {
        reason = "nor-write-protected";
    } else if (size != 2 || (address & 1)) {
        reason = "nor-x16-access-not-supported";
    } else {
        reason = "nor-backend-not-supported";
    }
    return nikon_memc_error(s, address, size, write, value, reason);
}

static const TypeInfo nikon_memc_type = {
    .name = TYPE_NIKON_MEMC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonMemcState),
    .instance_init = nikon_memc_init,
    .class_init = nikon_memc_class_init,
};

static void nikon_memc_register_types(void)
{
    type_register_static(&nikon_memc_type);
}
type_init(nikon_memc_register_types)
