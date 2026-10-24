/*
 * Source-identified paired capture parameters and packed table retention.
 * Configuration produces no processing result, DMA, IRQ or completion.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "hw/core/sysbus.h"
#include "hw/nikon/devices.h"

#define CAPTURE_PARAMETERS_SIZE 0x300
OBJECT_DECLARE_SIMPLE_TYPE(NikonCaptureParametersState,
                           NIKON_CAPTURE_PARAMETERS)

struct NikonCaptureParametersState {
    SysBusDevice parent_obj;
    MemoryRegion memory;
    uint32_t configuration[CAPTURE_PARAMETERS_SIZE / 4];
    bool reported_error;
};

static bool nikon_capture_parameters_mask(hwaddr offset, uint32_t *mask)
{
    if (offset >= 0x100 && offset <= 0x2fc) {
        /* 1020916c copies all128 packed words through LE byte loads. */
        *mask = UINT32_MAX;
        return true;
    }
    switch (offset) {
    case 0: case 4: case 8: case 0x14: case 0x18: case 0x1c: case 0x24:
        *mask = UINT32_MAX;
        break;
    case 0xc: case 0x20:
        *mask = 0xff;
        break;
    case 0x10:
        *mask = 0xffff;
        break;
    case 0x28:
        *mask = 0xffffff;
        break;
    case 0x2c:
        *mask = 3;
        break;
    case 0x30:
        *mask = 0x03ff03ff;
        break;
    default:
        return false;
    }
    return true;
}

static MemTxResult nikon_capture_parameters_error(
    NikonCaptureParametersState *s, hwaddr offset, unsigned size,
    bool write, uint64_t value)
{
    nikon_access_error(&s->reported_error, s->memory.addr + offset,
                       size, write, value,
                       "capture-parameters-access-not-supported");
    return MEMTX_ERROR;
}

static MemTxResult nikon_capture_parameters_read(void *opaque, hwaddr offset,
                                                uint64_t *value, unsigned size,
                                                MemTxAttrs attrs)
{
    NikonCaptureParametersState *s = opaque;
    uint32_t mask;

    *value = 0;
    if (size != 4 || (offset & 3) ||
        !nikon_capture_parameters_mask(offset, &mask)) {
        return nikon_capture_parameters_error(s, offset, size, false, 0);
    }
    *value = s->configuration[offset / 4];
    return MEMTX_OK;
}

static MemTxResult nikon_capture_parameters_write(void *opaque, hwaddr offset,
                                                 uint64_t value, unsigned size,
                                                 MemTxAttrs attrs)
{
    NikonCaptureParametersState *s = opaque;
    uint32_t mask;

    if (size != 4 || (offset & 3) ||
        !nikon_capture_parameters_mask(offset, &mask) || (value & ~mask)) {
        return nikon_capture_parameters_error(s, offset, size, true, value);
    }
    s->configuration[offset / 4] = value;
    return MEMTX_OK;
}

static const MemoryRegionOps nikon_capture_parameters_ops = {
    .read_with_attrs = nikon_capture_parameters_read,
    .write_with_attrs = nikon_capture_parameters_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4,
               .unaligned = false },
};

static void nikon_capture_parameters_reset(DeviceState *dev)
{
    NikonCaptureParametersState *s = NIKON_CAPTURE_PARAMETERS(dev);

    memset(s->configuration, 0, sizeof(s->configuration));
    s->reported_error = false;
}

static void nikon_capture_parameters_init(Object *obj)
{
    NikonCaptureParametersState *s = NIKON_CAPTURE_PARAMETERS(obj);

    memory_region_init_io(&s->memory, obj, &nikon_capture_parameters_ops, s,
                          "nikon.capture-parameters", CAPTURE_PARAMETERS_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->memory);
}

static const VMStateDescription nikon_capture_parameters_vmstate = {
    .name = "nikon-capture-parameters",
    .unmigratable = true,
};

static void nikon_capture_parameters_class_init(ObjectClass *oc,
                                                const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &nikon_capture_parameters_vmstate;
    dc->user_creatable = false;
    dc->unrealize = nikon_capture_parameters_reset;
    device_class_set_legacy_reset(dc, nikon_capture_parameters_reset);
}

static const TypeInfo nikon_capture_parameters_type = {
    .name = TYPE_NIKON_CAPTURE_PARAMETERS,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonCaptureParametersState),
    .instance_init = nikon_capture_parameters_init,
    .class_init = nikon_capture_parameters_class_init,
};

static void nikon_capture_parameters_register_types(void)
{
    type_register_static(&nikon_capture_parameters_type);
}

type_init(nikon_capture_parameters_register_types)
