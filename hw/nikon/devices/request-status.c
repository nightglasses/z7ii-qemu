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
#include "nikon.h"

OBJECT_DECLARE_SIMPLE_TYPE(NikonRequestStatusState, NIKON_REQUEST_STATUS)

struct NikonRequestStatusState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    uint32_t config[0x34 / 4];
    bool reported_error;
};

static MemTxResult nikon_request_status_error(NikonRequestStatusState *s, hwaddr offset,
                                  unsigned size, bool write, uint64_t value)
{
    nikon_access_error(&s->reported_error, s->regs.addr + offset, size,
                      write, value,
                      "request-status-access-or-activation-not-supported");
    return MEMTX_ERROR;
}

static bool nikon_request_status_config(hwaddr offset, uint32_t *mask)
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

static MemTxResult nikon_request_status_read(void *opaque, hwaddr offset,
                                 uint64_t *value, unsigned size,
                                 MemTxAttrs attrs)
{
    NikonRequestStatusState *s = opaque;
    uint32_t mask;

    *value = 0;
    if (size != 4 || (offset & 3)) {
        return nikon_request_status_error(s, offset, size, false, 0);
    }
    if (nikon_request_status_config(offset, &mask) || offset == 0x20) {
        *value = s->config[offset / 4];
    } else if (offset != 0x24 && offset != 0x30) {
        return nikon_request_status_error(s, offset, size, false, 0);
    }
    /* 24/30 are explicitly unavailable, never read-triggered ready. */
    return MEMTX_OK;
}

static MemTxResult nikon_request_status_write(void *opaque, hwaddr offset,
                                  uint64_t value, unsigned size,
                                  MemTxAttrs attrs)
{
    NikonRequestStatusState *s = opaque;
    uint32_t mask;

    if (size != 4 || (offset & 3)) {
        return nikon_request_status_error(s, offset, size, true, value);
    }
    if (offset == 0x20) {
        /* Neither changing handshake bits nor other unbound bits is supported. */
        if (value != s->config[offset / 4]) {
            return nikon_request_status_error(s, offset, size, true, value);
        }
    } else if (nikon_request_status_config(offset, &mask) && !(value & ~mask)) {
        s->config[offset / 4] = value;
    } else {
        return nikon_request_status_error(s, offset, size, true, value);
    }
    return MEMTX_OK;
}

static const MemoryRegionOps nikon_request_status_ops = {
    .read_with_attrs = nikon_request_status_read,
    .write_with_attrs = nikon_request_status_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
};

static void nikon_request_status_reset(DeviceState *dev)
{
    NikonRequestStatusState *s = NIKON_REQUEST_STATUS(dev);

    memset(s->config, 0, sizeof(s->config));
    s->reported_error = false;
}

static void nikon_request_status_init(Object *obj)
{
    NikonRequestStatusState *s = NIKON_REQUEST_STATUS(obj);

    memory_region_init_io(&s->regs, obj, &nikon_request_status_ops, s,
                          "nikon.request-status", 0x34);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
}

static const VMStateDescription nikon_request_status_vmstate = {
    .name = "nikon-request-status",
    .unmigratable = true,
};

static void nikon_request_status_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &nikon_request_status_vmstate;
    dc->user_creatable = false;
    device_class_set_legacy_reset(dc, nikon_request_status_reset);
}

static const TypeInfo nikon_request_status_type = {
    .name = TYPE_NIKON_REQUEST_STATUS,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonRequestStatusState),
    .instance_init = nikon_request_status_init,
    .class_init = nikon_request_status_class_init,
};

static void nikon_request_status_register_types(void)
{
    type_register_static(&nikon_request_status_type);
}
type_init(nikon_request_status_register_types)
