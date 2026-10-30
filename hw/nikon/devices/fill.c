/*
 * Original two-engine byte-copy, zero-fill and runtime control profile.
 * Units, stop acknowledgement, IRQ masks and error detail are virtual laws.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "migration/vmstate.h"
#include "system/address-spaces.h"
#include "system/dma.h"
#include "hw/nikon/devices.h"

OBJECT_DECLARE_SIMPLE_TYPE(NikonFillState, NIKON_FILL)

typedef struct NikonFillEngine {
    uint32_t control, mask, flags, pattern, source, destination;
    uint32_t source_pitch, pitch, row_bytes, count;
    uint32_t status, error, command;
    unsigned configured;
    bool busy;
    qemu_irq irq;
} NikonFillEngine;

struct NikonFillState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    MemoryRegion *memory;
    AddressSpace *dma;
    NikonFillEngine engine[2];
    bool reported_error;
};

static void nikon_fill_irq(NikonFillEngine *e)
{
    /* Named mask bits0/4 follow corresponding latched result bits. */
    qemu_set_irq(e->irq, !!(e->status & e->mask & 0x11));
}

static MemTxResult nikon_fill_error(NikonFillState *s, hwaddr offset,
                                 unsigned size, bool write, uint64_t value,
                                 const char *reason)
{
    nikon_access_error(&s->reported_error, s->regs.addr + offset,
                      size, write, value, reason);
    return MEMTX_ERROR;
}

static MemTxResult nikon_fill_read(void *opaque, hwaddr offset,
                                uint64_t *value, unsigned size,
                                MemTxAttrs attrs)
{
    NikonFillState *s = opaque;
    NikonFillEngine *e = &s->engine[offset / 0x40];

    *value = 0;
    if (size != 4 || (offset & 3)) {
        return nikon_fill_error(s, offset, size, false, 0,
                              "fill-access-not-supported");
    }
    switch (offset & 0x3f) {
    case 0:
        *value = e->control;
        break;
    case 4:
        *value = e->mask;
        break;
    case 8:
        *value = e->status;
        break;
    case 0xc:
        *value = e->error;
        break;
    case 0x10:
        *value = e->command;
        break;
    case 0x14:
        *value = e->flags;
        break;
    case 0x18:
        *value = e->pattern;
        break;
    case 0x1c:
        *value = e->source_pitch;
        break;
    case 0x20:
        *value = e->pitch;
        break;
    case 0x24:
        *value = e->row_bytes;
        break;
    case 0x28:
        *value = e->count;
        break;
    case 0x2c:
        *value = e->source;
        break;
    case 0x30:
        *value = e->destination;
        break;
    default:
        return nikon_fill_error(s, offset, size, false, 0,
                              "fill-access-not-supported");
    }
    return MEMTX_OK;
}

static MemTxResult nikon_fill_zero(NikonFillState *s, hwaddr destination,
                                  uint64_t length, MemTxAttrs attrs)
{
    while (length) {
        MemoryRegionSection section = memory_region_find(s->dma->root,
                                                        destination, length);
        MemTxResult result;
        uint64_t span;

        if (!section.mr) {
            return MEMTX_ERROR;
        }
        span = int128_get64(section.size);
        if (!span || span > length ||
            section.offset_within_address_space != destination ||
            !memory_region_is_ram(section.mr) || section.readonly ||
            !memory_access_is_direct(section.mr, true, attrs)) {
            memory_region_unref(section.mr);
            return MEMTX_ERROR;
        }
        memory_region_unref(section.mr);
        result = dma_memory_set(s->dma, destination, 0, span, attrs);
        if (result != MEMTX_OK) {
            return result;
        }
        destination += span;
        length -= span;
    }
    return MEMTX_OK;
}

typedef struct NikonFillSpan {
    ram_addr_t address;
    uint64_t size;
} NikonFillSpan;

static bool nikon_fill_spans(NikonFillState *s, uint32_t address,
                              unsigned pitch, unsigned width, unsigned rows,
                              bool write, MemTxAttrs attrs, GArray *spans)
{
    for (unsigned row = 0; row < rows; row++) {
        hwaddr current = address + (uint64_t)row * pitch;
        uint64_t remaining = width;

        while (remaining) {
            MemoryRegionSection section = memory_region_find(s->dma->root,
                                                              current,
                                                              remaining);
            uint64_t size;
            NikonFillSpan span;

            if (!section.mr) {
                return false;
            }
            size = int128_get64(section.size);
            if (!size || size > remaining ||
                section.offset_within_address_space != current ||
                !memory_region_is_ram(section.mr) ||
                (write && section.readonly) ||
                !memory_access_is_direct(section.mr, write, attrs)) {
                memory_region_unref(section.mr);
                return false;
            }
            span = (NikonFillSpan) {
                memory_region_get_ram_addr(section.mr) +
                    section.offset_within_region, size
            };
            g_array_append_val(spans, span);
            memory_region_unref(section.mr);
            current += size;
            remaining -= size;
        }
    }
    return true;
}

static int nikon_fill_span_compare(const void *a, const void *b)
{
    const NikonFillSpan *x = a, *y = b;

    return (x->address > y->address) - (x->address < y->address);
}

static MemTxResult nikon_fill_copy(NikonFillState *s, NikonFillEngine *e,
                                   MemTxAttrs attrs, bool *overlap)
{
    g_autoptr(GArray) source = g_array_new(false, false, sizeof(NikonFillSpan));
    g_autoptr(GArray) destination = g_array_new(false, false,
                                              sizeof(NikonFillSpan));
    g_autofree uint8_t *row = NULL;
    unsigned i = 0, j = 0;

    /* Preflight every active byte; row padding need not have RAM backing. */
    if (!nikon_fill_spans(s, e->source, e->source_pitch, e->row_bytes,
                          e->count, false, attrs, source) ||
        !nikon_fill_spans(s, e->destination, e->pitch, e->row_bytes,
                          e->count, true, attrs, destination)) {
        return MEMTX_ERROR;
    }
    g_array_sort(source, nikon_fill_span_compare);
    g_array_sort(destination, nikon_fill_span_compare);
    while (i < source->len && j < destination->len) {
        NikonFillSpan *a = &g_array_index(source, NikonFillSpan, i);
        NikonFillSpan *b = &g_array_index(destination, NikonFillSpan, j);

        if (a->address < b->address + b->size &&
            b->address < a->address + a->size) {
            *overlap = true;
            return MEMTX_ERROR;
        }
        if (a->address + a->size <= b->address) {
            i++;
        } else {
            j++;
        }
    }
    row = g_malloc(e->row_bytes);
    for (unsigned y = 0; y < e->count; y++) {
        MemTxResult result = dma_memory_read(s->dma,
            e->source + (uint64_t)y * e->source_pitch,
            row, e->row_bytes, attrs);

        if (result == MEMTX_OK) {
            result = dma_memory_write(s->dma,
                e->destination + (uint64_t)y * e->pitch,
                row, e->row_bytes, attrs);
        }
        if (result != MEMTX_OK) {
            return result;
        }
    }
    return MEMTX_OK;
}

static MemTxResult nikon_fill_start(NikonFillState *s, NikonFillEngine *e,
                                 hwaddr offset, MemTxAttrs attrs)
{
    uint64_t bytes = (uint64_t)e->row_bytes * e->count;
    uint64_t extent = e->count ?
        (uint64_t)(e->count - 1) * e->pitch + e->row_bytes : 0;
    uint64_t source_extent = e->count ?
        (uint64_t)(e->count - 1) * e->source_pitch + e->row_bytes : 0;
    bool fill = e->flags & 1, overlap = false;
    unsigned required = fill ? 127 : 511;
    uint32_t command = e->command;
    MemTxResult result;

    /*
     * MAS10389e3a: destination=base+x+pitch*y. MAS102dba60 writes
     * pitch/row bytes/rows at20/24/28 for either engine. 1038a354/a442
     * select copy with bit0=0 and publish source pitch/address at1c/2c.
     * Bit4 preserves the caller's independent byte; its identical byte-level
     * effect in this finite operator is a hypothesis, not silicon semantics.
     */
    if ((e->configured & required) != required ||
        (e->flags & ~0x11U) || e->pattern ||
        !e->row_bytes || !e->count || e->pitch < e->row_bytes ||
        (uint64_t)e->destination + extent > (1ULL << 32) ||
        (fill ? (e->source_pitch || e->source || (e->destination & 3)) :
         (e->source_pitch < e->row_bytes ||
          (uint64_t)e->source + source_extent > (1ULL << 32)))) {
        return nikon_fill_error(s, offset, 4, true, 1,
                              "fill-request-profile-not-supported");
    }
    e->busy = true;
    e->command = 1;
    attrs.memory = true;
    if (!fill) {
        result = nikon_fill_copy(s, e, attrs, &overlap);
    } else if (e->pitch == e->row_bytes) {
        result = nikon_fill_zero(s, e->destination, bytes, attrs);
    } else {
        result = MEMTX_OK;
        for (unsigned row = 0; row < e->count; row++) {
            result = nikon_fill_zero(s,
                                    e->destination + (uint64_t)row * e->pitch,
                                    e->row_bytes, attrs);
            if (result != MEMTX_OK) {
                break;
            }
        }
    }
    e->busy = false;
    e->command = 0;
    if (overlap) {
        e->command = command;
        return nikon_fill_error(s, offset, 4, true, 1,
                              "fill-copy-overlap-not-supported");
    }
    e->status = result == MEMTX_OK ? 1 : 0x10;
    /* Named DMA-bus code1, not a Nikon physical error/direction encoding. */
    e->error = result == MEMTX_OK ? 0 : 1U << 16;
    nikon_fill_irq(e);
    qemu_log_mask(LOG_GUEST_ERROR,
                  "nikon-fill: engine=%u destination=0x%08x bytes=0x%" PRIx64
                  " bus-result=%u status=0x%x\n",
                  (unsigned)(e - s->engine), e->destination,
                  bytes,
                  result, e->status);
    return MEMTX_OK;
}

static MemTxResult nikon_fill_write(void *opaque, hwaddr offset,
                                 uint64_t value, unsigned size,
                                 MemTxAttrs attrs)
{
    NikonFillState *s = opaque;
    NikonFillEngine *e = &s->engine[offset / 0x40];
    hwaddr reg = offset & 0x3f;

    if (size != 4 || (offset & 3)) {
        return nikon_fill_error(s, offset, size, true, value,
                              "fill-access-not-supported");
    }
    if (reg == 8) {
        /* Full raw ISR ACK clears only implemented real result bits. */
        e->status &= ~(value & 0x11);
        nikon_fill_irq(e);
        return MEMTX_OK;
    }
    if (reg == 0x10 && value == 0) {
        /* Supported DMA is synchronous: no deferred work can survive stop.
         * Stop remains a causal command transition independent of ACK. */
        e->command = 2;
        return MEMTX_OK;
    }
    if (e->busy) {
        return nikon_fill_error(s, offset, size, true, value,
                              "fill-operation-busy");
    }
    if (e->status && reg != 0 && reg != 4) {
        return nikon_fill_error(s, offset, size, true, value,
                              "fill-result-not-acknowledged");
    }
    switch (reg) {
    case 0:
        if (value > 1) {
            break;
        }
        e->control = value;
        return MEMTX_OK;
    case 4:
        e->mask = value;
        e->configured |= 1;
        nikon_fill_irq(e);
        return MEMTX_OK;
    case 0x14:
        if (value & ~0x11U) {
            break;
        }
        e->flags = value;
        e->configured |= 4;
        return MEMTX_OK;
    case 0x18:
        if (value & ~0x03ffffffU) {
            break;
        }
        e->pattern = value;
        e->configured |= 2;
        return MEMTX_OK;
    case 0x1c: case 0x20: case 0x24: case 0x28:
        if (value > 0xffff) {
            break;
        }
        if (reg == 0x1c) {
            e->source_pitch = value;
            e->configured |= 128;
        } else if (reg == 0x20) {
            e->pitch = value;
            e->configured |= 8;
        } else if (reg == 0x24) {
            e->row_bytes = value;
            e->configured |= 16;
        } else {
            e->count = value;
            e->configured |= 32;
        }
        return MEMTX_OK;
    case 0x2c:
        e->source = value;
        e->configured |= 256;
        return MEMTX_OK;
    case 0x30:
        e->destination = value;
        e->configured |= 64;
        return MEMTX_OK;
    case 0x10:
        if (value == 1) {
            return nikon_fill_start(s, e, offset, attrs);
        }
        break;
    default:
        return nikon_fill_error(s, offset, size, true, value,
                              "fill-access-not-supported");
    }
    return nikon_fill_error(s, offset, size, true, value,
                          "fill-value-profile-not-supported");
}

static const MemoryRegionOps nikon_fill_ops = {
    .read_with_attrs = nikon_fill_read,
    .write_with_attrs = nikon_fill_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
};

static void nikon_fill_reset(DeviceState *dev)
{
    NikonFillState *s = NIKON_FILL(dev);
    unsigned i;

    /* Explicit virtual inactive reset, not recovered silicon. */
    for (i = 0; i < 2; i++) {
        qemu_irq irq = s->engine[i].irq;
        memset(&s->engine[i], 0, sizeof(s->engine[i]));
        s->engine[i].irq = irq;
        nikon_fill_irq(&s->engine[i]);
    }
    s->reported_error = false;
}

static void nikon_fill_init(Object *obj)
{
    NikonFillState *s = NIKON_FILL(obj);

    memory_region_init_io(&s->regs, obj, &nikon_fill_ops, s, "nikon.fill", 0x80);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->engine[0].irq);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->engine[1].irq);
}

static const VMStateDescription nikon_fill_vmstate = {
    .name = "nikon-fill",
    .unmigratable = true,
};

static void nikon_fill_realize(DeviceState *dev, Error **errp)
{
    NikonFillState *s = NIKON_FILL(dev);

    s->dma = g_new0(AddressSpace, 1);
    address_space_init(s->dma, s->memory ? s->memory : get_system_memory(),
                       "nikon.fill");
}

static void nikon_fill_unrealize(DeviceState *dev)
{
    NikonFillState *s = NIKON_FILL(dev);

    address_space_destroy_free(g_steal_pointer(&s->dma));
}

static const Property nikon_fill_properties[] = {
    DEFINE_PROP_LINK("memory", NikonFillState, memory,
                     TYPE_MEMORY_REGION, MemoryRegion *),
};

static void nikon_fill_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &nikon_fill_vmstate;
    dc->realize = nikon_fill_realize;
    dc->unrealize = nikon_fill_unrealize;
    device_class_set_props(dc, nikon_fill_properties);
    device_class_set_legacy_reset(dc, nikon_fill_reset);
    dc->user_creatable = false;
}

static const TypeInfo nikon_fill_type = {
    .name = TYPE_NIKON_FILL,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonFillState),
    .instance_init = nikon_fill_init,
    .class_init = nikon_fill_class_init,
};

static void nikon_fill_register_types(void)
{
    type_register_static(&nikon_fill_type);
}
type_init(nikon_fill_register_types)
