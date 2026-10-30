/*
 * Capture endpoint parameters and externally published global results.
 * Configuration retention alone produces no command completion, DMA or IRQ.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "hw/core/sysbus.h"
#include "hw/nikon/devices.h"
#include "hw/nikon/statistics.h"

#define ENDPOINTS_SIZE 0xb84
#define ENDPOINTS_WORDS (ENDPOINTS_SIZE / 4)
OBJECT_DECLARE_SIMPLE_TYPE(NikonCaptureEndpointsState, NIKON_CAPTURE_ENDPOINTS)

struct NikonCaptureEndpointsState {
    SysBusDevice parent_obj;
    MemoryRegion memory;
    uint32_t configuration[ENDPOINTS_WORDS];
    bool reported_error;
    uint32_t results[4];
    bool results_valid;
    uint32_t consumed;
};

void nikon_capture_endpoints_snapshot(
    DeviceState *dev, uint32_t words[NIKON_CAPTURE_ENDPOINT_WORDS])
{
    memcpy(words, NIKON_CAPTURE_ENDPOINTS(dev)->configuration,
           sizeof(NIKON_CAPTURE_ENDPOINTS(dev)->configuration));
}

void nikon_capture_endpoints_results(DeviceState *dev, const uint32_t words[4])
{
    NikonCaptureEndpointsState *s = NIKON_CAPTURE_ENDPOINTS(dev);

    memcpy(s->results, words, sizeof(s->results));
    s->results_valid = true;
    s->consumed = false;
}

/* Offsets in this helper match the original 2006a000 parameter domain. */
static bool nikon_endpoints_global(unsigned offset, uint32_t *mask)
{
    if (offset < 0x200 || offset > 0x370) {
        return false;
    }
    *mask = 0xffff;
    if (offset <= 0x278) {
        if ((offset >= 0x214 && offset <= 0x21c) ||
            (offset >= 0x224 && offset <= 0x258) ||
            offset == 0x270 || offset == 0x274) {
            *mask = 0x0fffffff;
        } else if (offset == 0x200) {
            *mask = 0xffff00ff;
        } else if (offset == 0x204) {
            *mask = UINT32_MAX;
        } else if (offset == 0x25c) {
            *mask = 0xffffff;
        }
    } else if (offset <= 0x2b8) {
        switch (offset) {
        case 0x27c:
            *mask = UINT32_MAX;
            break;
        case 0x280:
            *mask = 0xff;
            break;
        case 0x28c:
            *mask = 0x7fffffff;
            break;
        case 0x288: case 0x290: case 0x2a4: case 0x2b8:
            break;
        default:
            *mask = 0x03ffffff;
            break;
        }
    } else if (offset <= 0x318) {
        /* Four repeated B056/AED6 records, each six output words. */
        switch ((offset - 0x2bc) % 0x18) {
        case 0:
            *mask = UINT32_MAX;
            break;
        case 4:
            *mask = 0xffffff;
            break;
        case 0x10:
            *mask = 0x03ffffff;
            break;
        }
    } else if (offset == 0x31c) {
        *mask = 0xff;
    } else {
        /* B094's second row packs its last halfword with shift11, not10. */
        switch ((offset - 0x320) % 0x1c) {
        case 0:
            *mask = UINT32_MAX;
            break;
        case 0xc:
            *mask = 0x03ffffff;
            break;
        case 0x18:
            *mask = offset == 0x354 ? 0x07ffffff : 0x03ffffff;
            break;
        }
    }
    return true;
}

static bool nikon_endpoints_config(hwaddr offset, uint32_t *mask)
{
    unsigned endpoint, word;

    if (offset < 0x174) {
        return nikon_endpoints_global(offset + 0x200, mask);
    }
    if (offset < 0x200 || offset >= ENDPOINTS_SIZE) {
        return false;
    }
    offset -= 0x200;
    endpoint = offset >> 8;
    word = offset & 0xff;
    *mask = UINT32_MAX;
    switch (word) {
    case 0x14: case 0x40: case 0x50: case 0x60: case 0x80:
        return true;
    case 0x18: case 0x24: case 0x44: case 0x48: case 0x4c:
        *mask = 0xff;
        return true;
    case 0x3c:
        *mask = 7;
        return true;
    case 0x54: case 0x58: case 0x5c: case 0x64: case 0x68: case 0x6c:
        /* Original pointer-table entries are NULL at the other six indices. */
        return endpoint == 2 || endpoint == 3 || endpoint == 4 || endpoint == 9;
    default:
        return false;
    }
}

static MemTxResult nikon_endpoints_error(NikonCaptureEndpointsState *s,
                                         hwaddr offset, unsigned size,
                                         bool write, uint64_t value)
{
    nikon_access_error(&s->reported_error, s->memory.addr + offset,
                       size, write, value,
                       "capture-endpoint-configuration-not-supported");
    return MEMTX_ERROR;
}

static MemTxResult nikon_endpoints_read(void *opaque, hwaddr offset,
                                       uint64_t *value, unsigned size,
                                       MemTxAttrs attrs)
{
    NikonCaptureEndpointsState *s = opaque;
    uint32_t mask;

    *value = 0;
    if (size == 4 && !(offset & 3) && offset >= 0x1e0 &&
        offset <= 0x1ec && s->results_valid) {
        *value = s->results[(offset - 0x1e0) / 4];
        /* Current original consumer10238a62 always reads selector0. */
        s->consumed |= offset == 0x1e0;
        return MEMTX_OK;
    }
    if (size != 4 || (offset & 3) || !nikon_endpoints_config(offset, &mask)) {
        return nikon_endpoints_error(s, offset, size, false, 0);
    }
    *value = s->configuration[offset / 4];
    return MEMTX_OK;
}

static MemTxResult nikon_endpoints_write(void *opaque, hwaddr offset,
                                        uint64_t value, unsigned size,
                                        MemTxAttrs attrs)
{
    NikonCaptureEndpointsState *s = opaque;
    uint32_t mask;

    if (size != 4 || (offset & 3) || !nikon_endpoints_config(offset, &mask) ||
        (value & ~mask)) {
        return nikon_endpoints_error(s, offset, size, true, value);
    }
    s->configuration[offset / 4] = value;
    return MEMTX_OK;
}

static const MemoryRegionOps nikon_endpoints_ops = {
    .read_with_attrs = nikon_endpoints_read,
    .write_with_attrs = nikon_endpoints_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4,
               .unaligned = false },
};

static void nikon_endpoints_reset(DeviceState *dev)
{
    NikonCaptureEndpointsState *s = NIKON_CAPTURE_ENDPOINTS(dev);

    memset(s->configuration, 0, sizeof(s->configuration));
    s->reported_error = false;
    s->results_valid = s->consumed = false;
    memset(s->results, 0, sizeof(s->results));
}

static void nikon_endpoints_init(Object *obj)
{
    NikonCaptureEndpointsState *s = NIKON_CAPTURE_ENDPOINTS(obj);

    object_property_add_uint32_ptr(obj, "scalar-consumed", &s->consumed,
                                   OBJ_PROP_FLAG_READ);
    memory_region_init_io(&s->memory, obj, &nikon_endpoints_ops, s,
                          "nikon.capture-endpoints", ENDPOINTS_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->memory);
}

static const VMStateDescription nikon_endpoints_vmstate = {
    .name = "nikon-capture-endpoints",
    .unmigratable = true,
};

static void nikon_endpoints_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &nikon_endpoints_vmstate;
    dc->user_creatable = false;
    dc->unrealize = nikon_endpoints_reset;
    device_class_set_legacy_reset(dc, nikon_endpoints_reset);
}

static const TypeInfo nikon_endpoints_type = {
    .name = TYPE_NIKON_CAPTURE_ENDPOINTS,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonCaptureEndpointsState),
    .instance_init = nikon_endpoints_init,
    .class_init = nikon_endpoints_class_init,
};

static void nikon_endpoints_register_types(void)
{
    type_register_static(&nikon_endpoints_type);
}

type_init(nikon_endpoints_register_types)
