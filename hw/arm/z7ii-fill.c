/*
 * Original startup zero-fill protocol, with declared byte-block semantics.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/core/sysbus.h"
#include "migration/vmstate.h"
#include "system/address-spaces.h"
#include "system/dma.h"
#include "z7ii.h"

OBJECT_DECLARE_SIMPLE_TYPE(Z7IIFillState, Z7II_FILL)

struct Z7IIFillState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    uint32_t control;
    uint32_t flags;
    uint32_t width;
    uint32_t stride;
    uint32_t count;
    uint32_t destination;
    uint32_t status;
    unsigned configured;
    bool reported_error;
};

static MemTxResult z7ii_fill_error(Z7IIFillState *s, hwaddr offset,
                                 unsigned size, bool write, uint64_t value,
                                 const char *reason)
{
    z7ii_access_error(&s->reported_error, 0x20070000ULL + offset,
                       size, write, value, reason);
    return MEMTX_ERROR;
}

static MemTxResult z7ii_fill_read(void *opaque, hwaddr offset,
                                uint64_t *value, unsigned size,
                                MemTxAttrs attrs)
{
    Z7IIFillState *s = opaque;

    *value = 0;
    if (size == 4 && !(offset & 3)) {
        switch (offset) {
        case 0x08:
            *value = s->status;
            return MEMTX_OK;
        case 0x14:
            *value = s->flags;
            return MEMTX_OK;
        case 0x20:
            *value = s->width;
            return MEMTX_OK;
        case 0x24:
            *value = s->stride;
            return MEMTX_OK;
        }
    }
    return z7ii_fill_error(s, offset, size, false, 0,
                          "fill-access-not-supported");
}

static MemTxResult z7ii_fill_write(void *opaque, hwaddr offset,
                                 uint64_t value, unsigned size,
                                 MemTxAttrs attrs)
{
    Z7IIFillState *s = opaque;

    if (size != 4 || (offset & 3)) {
        return z7ii_fill_error(s, offset, size, true, value,
                              "fill-access-not-supported");
    }
    if (offset == 0x08) {
        if (value != 1 && value != 0x10) {
            return z7ii_fill_error(s, offset, size, true, value,
                                  "fill-ack-not-supported");
        }
        s->status &= ~value;
        return MEMTX_OK;
    }
    if (s->status) {
        return z7ii_fill_error(s, offset, size, true, value,
                              "fill-result-not-acknowledged");
    }
    switch (offset) {
    case 0:
        if (value <= 1) {
            /* Observed boolean control; not an all-register reset command. */
            s->control = value;
            return MEMTX_OK;
        }
        break;
    case 0x04:
    case 0x18:
        if (value == 0) {
            s->configured |= offset == 4 ? 1 : 2;
            return MEMTX_OK;
        }
        break;
    case 0x14:
        if (value == 1) {
            s->flags = value;
            s->configured |= 4;
            return MEMTX_OK;
        }
        break;
    case 0x20:
    case 0x24:
        if (value == 0x4000) {
            if (offset == 0x20) {
                s->width = value;
            } else {
                s->stride = value;
            }
            s->configured |= offset == 0x20 ? 8 : 16;
            return MEMTX_OK;
        }
        break;
    case 0x28:
        if (value && value < 0x4000) {
            s->count = value;
            s->configured |= 32;
            return MEMTX_OK;
        }
        break;
    case 0x30:
        if (!(value & 3)) {
            s->destination = value;
            s->configured |= 64;
            return MEMTX_OK;
        }
        break;
    case 0x10:
        if (value == 1) {
            uint64_t length = (uint64_t)s->width * s->count;
            MemTxResult result;

            if (s->control || s->configured != 127 ||
                (uint64_t)s->destination + length > (1ULL << 32)) {
                return z7ii_fill_error(s, offset, size, true, value,
                                      "fill-request-profile-not-supported");
            }
            /* Nominal direct count, byte width == stride, zero pattern.
             * Normal-memory-only bus transactions cannot target device MMIO.
             * Completion is produced only by the real bus operation result. */
            attrs.memory = true;
            result = dma_memory_set(&address_space_memory, s->destination,
                                    0, length, attrs);
            s->status = result == MEMTX_OK ? 1 : 0x10;
            qemu_log_mask(LOG_GUEST_ERROR,
                          "z7ii-fill: destination=0x%08x bytes=0x%" PRIx64
                          " bus-result=%u status=0x%x\n",
                          s->destination, length, result, s->status);
            return MEMTX_OK;
        }
        break;
    default:
        return z7ii_fill_error(s, offset, size, true, value,
                              "fill-access-not-supported");
    }
    return z7ii_fill_error(s, offset, size, true, value,
                          "fill-value-profile-not-supported");
}

static const MemoryRegionOps z7ii_fill_ops = {
    .read_with_attrs = z7ii_fill_read,
    .write_with_attrs = z7ii_fill_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
};

static void z7ii_fill_reset(DeviceState *dev)
{
    Z7IIFillState *s = Z7II_FILL(dev);

    /* Explicit virtual idle/zero-upper-bit profile, not recovered silicon. */
    s->control = s->flags = s->width = s->stride = 0;
    s->count = s->destination = s->status = 0;
    s->configured = 0;
    s->reported_error = false;
}

static void z7ii_fill_init(Object *obj)
{
    Z7IIFillState *s = Z7II_FILL(obj);

    memory_region_init_io(&s->regs, obj, &z7ii_fill_ops, s,
                          "z7ii.fill", 0x34);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
}

static const VMStateDescription z7ii_fill_vmstate = {
    .name = "z7ii-fill",
    .unmigratable = true,
};

static void z7ii_fill_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &z7ii_fill_vmstate;
    device_class_set_legacy_reset(dc, z7ii_fill_reset);
    dc->user_creatable = false;
}

static const TypeInfo z7ii_fill_type = {
    .name = TYPE_Z7II_FILL,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Z7IIFillState),
    .instance_init = z7ii_fill_init,
    .class_init = z7ii_fill_class_init,
};

static void z7ii_fill_register_types(void)
{
    type_register_static(&z7ii_fill_type);
}
type_init(z7ii_fill_register_types)
