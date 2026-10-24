/*
 * Finite capture-group parameters and opaque address retention.
 * Configuration alone produces no status, DMA, IRQ or completion.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "hw/core/sysbus.h"
#include "hw/nikon/devices.h"

#define CAPTURE_GROUP_SIZE 0x584
#define CAPTURE_GROUP_WORDS (CAPTURE_GROUP_SIZE / 4)
OBJECT_DECLARE_SIMPLE_TYPE(NikonCaptureGroupState, NIKON_CAPTURE_GROUP)

struct NikonCaptureGroupState {
    SysBusDevice parent_obj;
    MemoryRegion memory;
    uint32_t configuration[CAPTURE_GROUP_WORDS];
    bool reported_error;
};

static bool nikon_capture_group_mask(hwaddr offset, uint32_t *mask)
{
    if (offset <= 0x1c) {
        *mask = offset <= 4 || offset == 0x1c ? 0xff : 0xffff;
    } else if (offset >= 0x20 && offset <= 0x7c) {
        /* Three CB0 rows: a complete byte followed by seven masked bytes. */
        *mask = (offset - 0x20) % 0x20 == 0 ? 0xff : 0x7f;
    } else if ((offset >= 0x80 && offset <= 0x180) ||
               (offset >= 0x200 && offset <= 0x20c) ||
               (offset >= 0x234 && offset <= 0x250) ||
               (offset >= 0x2a4 && offset <= 0x2ac)) {
        *mask = 0xffff;
    } else if ((offset >= 0x214 && offset <= 0x228) ||
               (offset >= 0x2b0 && offset <= 0x2b8)) {
        *mask = 0xff;
    } else if (offset >= 0x350 && offset <= 0x580 &&
               ((offset & 0xff) == 0x50 || (offset & 0xff) == 0x60 ||
                (offset & 0xff) == 0x80)) {
        /* Three original provider slots, two address sets and one extra word. */
        *mask = UINT32_MAX;
    } else {
        switch (offset) {
        case 0x184: case 0x210: case 0x22c: case 0x2a0: case 0x33c:
            *mask = UINT32_MAX;
            break;
        case 0x230:
            /* Only the source-provided count0/1 packet footprint is known. */
            *mask = 1;
            break;
        case 0x294:
            *mask = 0xffff;
            break;
        case 0x298: case 0x29c:
            *mask = 0x7fffff;
            break;
        case 0x318: case 0x324: case 0x344: case 0x348: case 0x34c:
            *mask = 0xff;
            break;
        default:
            return false;
        }
    }
    return true;
}

static MemTxResult nikon_capture_group_error(NikonCaptureGroupState *s,
                                            hwaddr offset, unsigned size,
                                            bool write, uint64_t value)
{
    nikon_access_error(&s->reported_error, s->memory.addr + offset,
                       size, write, value,
                       "capture-group-configuration-not-supported");
    return MEMTX_ERROR;
}

static MemTxResult nikon_capture_group_read(void *opaque, hwaddr offset,
                                           uint64_t *value, unsigned size,
                                           MemTxAttrs attrs)
{
    NikonCaptureGroupState *s = opaque;
    uint32_t mask;

    *value = 0;
    if (size != 4 || (offset & 3) ||
        !nikon_capture_group_mask(offset, &mask)) {
        return nikon_capture_group_error(s, offset, size, false, 0);
    }
    *value = s->configuration[offset / 4];
    return MEMTX_OK;
}

static MemTxResult nikon_capture_group_write(void *opaque, hwaddr offset,
                                            uint64_t value, unsigned size,
                                            MemTxAttrs attrs)
{
    NikonCaptureGroupState *s = opaque;
    uint32_t mask;

    if (size != 4 || (offset & 3) ||
        !nikon_capture_group_mask(offset, &mask) || (value & ~mask)) {
        return nikon_capture_group_error(s, offset, size, true, value);
    }
    s->configuration[offset / 4] = value;
    return MEMTX_OK;
}

static const MemoryRegionOps nikon_capture_group_ops = {
    .read_with_attrs = nikon_capture_group_read,
    .write_with_attrs = nikon_capture_group_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4,
               .unaligned = false },
};

static void nikon_capture_group_reset(DeviceState *dev)
{
    NikonCaptureGroupState *s = NIKON_CAPTURE_GROUP(dev);

    memset(s->configuration, 0, sizeof(s->configuration));
    s->reported_error = false;
}

static void nikon_capture_group_init(Object *obj)
{
    NikonCaptureGroupState *s = NIKON_CAPTURE_GROUP(obj);

    memory_region_init_io(&s->memory, obj, &nikon_capture_group_ops, s,
                          "nikon.capture-group", CAPTURE_GROUP_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->memory);
}

static const VMStateDescription nikon_capture_group_vmstate = {
    .name = "nikon-capture-group",
    .unmigratable = true,
};

static void nikon_capture_group_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &nikon_capture_group_vmstate;
    dc->user_creatable = false;
    dc->unrealize = nikon_capture_group_reset;
    device_class_set_legacy_reset(dc, nikon_capture_group_reset);
}

static const TypeInfo nikon_capture_group_type = {
    .name = TYPE_NIKON_CAPTURE_GROUP,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonCaptureGroupState),
    .instance_init = nikon_capture_group_init,
    .class_init = nikon_capture_group_class_init,
};

static void nikon_capture_group_register_types(void)
{
    type_register_static(&nikon_capture_group_type);
}

type_init(nikon_capture_group_register_types)
