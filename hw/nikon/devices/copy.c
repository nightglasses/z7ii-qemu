/*
 * Four-channel original default byte-copy interface.
 * Count/source/destination are joined to the original memcpy callers.
 * Zero reset, 48MHz throughput, residual count and bus-error status1 are
 * virtual-board conventions; success8 is the original software condition.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "qemu/units.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "migration/vmstate.h"
#include "qapi/visitor.h"
#include "system/address-spaces.h"
#include "system/dma.h"
#include "hw/nikon/devices.h"

OBJECT_DECLARE_SIMPLE_TYPE(NikonCopyState, NIKON_COPY)

/* Original setup BFI fields, separate from preserved trigger/mode bits. */
#define COPY_SETUP_MASK 0x00f09110U
#define COPY_EVENT_BYTES (64 * KiB)
#define COPY_DMA_BYTES (4 * KiB)

typedef struct NikonCopyChannel {
    NikonCopyState *owner;
    QEMUTimer *timer;
    qemu_irq irq;
    uint32_t reg[9];
    uint64_t remaining;
    uint64_t length;
    uint64_t started_ns;
    uint64_t source_writes;
    uint64_t destination_writes;
    uint32_t last_source_write;
    uint32_t last_destination_write;
    unsigned configured;
    bool running;
} NikonCopyChannel;

typedef struct NikonCopyRange {
    Int128 start;
    Int128 end;
    bool readable;
    bool writable;
} NikonCopyRange;

struct NikonCopyState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    MemoryRegion *memory;
    AddressSpace *dma;
    MemoryListener memory_listener;
    GArray *ranges;
    Clock *clock;
    NikonCopyChannel channel[4];
    uint32_t control;
    bool reported_error;
    bool first_fault;
    uint32_t first_fault_channel;
    uint32_t first_fault_status;
    uint64_t first_fault_source;
    uint64_t first_fault_destination;
    uint64_t first_fault_remaining;
    uint64_t first_fault_source_writes;
    uint64_t first_fault_destination_writes;
};

static void nikon_copy_irq(NikonCopyChannel *c)
{
    qemu_set_irq(c->irq, (c->reg[6] & (1U << 15)) && c->reg[8]);
}

static void nikon_copy_finish(NikonCopyChannel *c, uint32_t status)
{
    timer_del(c->timer);
    c->running = false;
    c->reg[6] &= ~3U;
    c->reg[8] = status;
    nikon_copy_irq(c);
}

static void nikon_copy_record_fault(NikonCopyChannel *c)
{
    NikonCopyState *s = c->owner;

    if (s->first_fault) {
        return;
    }
    s->first_fault = true;
    s->first_fault_channel = c - s->channel;
    s->first_fault_status = 1;
    s->first_fault_source = c->reg[1];
    s->first_fault_destination = c->reg[2];
    s->first_fault_remaining = c->remaining;
    s->first_fault_source_writes = c->source_writes;
    s->first_fault_destination_writes = c->destination_writes;
    error_report("nikon: COPY first DMA fault channel=%u source=0x%08"
                 PRIx64 " destination=0x%08" PRIx64 " remaining=0x%08"
                 PRIx64 " source-writes=%" PRIu64 " destination-writes=%"
                 PRIu64 " status=1",
                 s->first_fault_channel, s->first_fault_source,
                 s->first_fault_destination, s->first_fault_remaining,
                 s->first_fault_source_writes,
                 s->first_fault_destination_writes);
}

static void nikon_copy_schedule(NikonCopyChannel *c)
{
    uint64_t completed = c->length - c->remaining;
    uint64_t next = completed + MIN(c->remaining, COPY_EVENT_BYTES);

    timer_mod(c->timer, c->started_ns +
              clock_ticks_to_ns(c->owner->clock, next) + 1);
}

static uint64_t nikon_copy_memory_span(NikonCopyState *s, hwaddr addr,
                                       uint64_t length, bool write)
{
    RCU_READ_LOCK_GUARD();
    Int128 start = int128_make64(addr);

    /* Translate drops section bounds and only caps RAM, not ROMD. Preflight
     * and actual DMA must both use these effective flat-view intervals. */
    for (unsigned i = 0; i < s->ranges->len; i++) {
        NikonCopyRange *range = &g_array_index(s->ranges, NikonCopyRange, i);

        if (int128_le(range->start, start) && int128_lt(start, range->end)) {
            if (!(write ? range->writable : range->readable)) {
                return 0;
            }
            return int128_get64(int128_min(int128_sub(range->end, start),
                                           int128_make64(length)));
        }
    }
    return 0;
}

static bool nikon_copy_memory_range(NikonCopyState *s, hwaddr addr,
                                    uint64_t length, bool write)
{
    while (length) {
        uint64_t span = nikon_copy_memory_span(s, addr, length, write);

        if (!span) {
            return false;
        }
        addr += span;
        length -= span;
    }
    return true;
}

static void nikon_copy_step(void *opaque)
{
    NikonCopyChannel *c = opaque;
    uint8_t buffer[COPY_DMA_BYTES];
    uint64_t completed = c->length - c->remaining;
    uint64_t due;
    uint64_t elapsed = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) - c->started_ns;
    MemTxAttrs attrs = MEMTXATTRS_UNSPECIFIED;

    if (!c->running) {
        return;
    }
    /* ROMD is a direct source, but QEMU's memory attribute only admits RAM. */
    if (!nikon_copy_memory_range(c->owner, c->reg[1], c->remaining, false) ||
        !nikon_copy_memory_range(c->owner, c->reg[2], c->remaining, true)) {
        nikon_copy_record_fault(c);
        nikon_copy_finish(c, 1);
        return;
    }
    due = MIN(c->length, clock_ns_to_ticks(c->owner->clock, elapsed));
    /* The deadline's extra ns also applies to an exactly integral tick. */
    if (due && clock_ticks_to_ns(c->owner->clock, due) + 1 > elapsed) {
        due--;
    }
    while (completed < due) {
        unsigned length = MIN(due - completed, sizeof(buffer));
        MemTxResult result;

        /* A direct ROMD transaction otherwise reads through a short alias's
         * hidden backing instead of translating the next effective section. */
        length = nikon_copy_memory_span(c->owner, c->reg[1], length, false);
        length = nikon_copy_memory_span(c->owner, c->reg[2], length, true);
        if (!length) {
            nikon_copy_record_fault(c);
            nikon_copy_finish(c, 1);
            return;
        }
        /* ROMD sources use ordinary read attributes; RAM writes are strict. */
        attrs.memory = false;
        result = dma_memory_read(c->owner->dma, c->reg[1], buffer,
                                 length, attrs);
        if (result == MEMTX_OK) {
            attrs.memory = true;
            result = dma_memory_write(c->owner->dma, c->reg[2], buffer,
                                      length, attrs);
        }
        if (result != MEMTX_OK) {
            nikon_copy_record_fault(c);
            nikon_copy_finish(c, 1);
            return;
        }
        c->reg[1] += length;
        c->reg[2] += length;
        c->remaining -= length;
        completed += length;
        /* Functional residual, not recovered silicon progress encoding. */
        c->reg[0] = c->remaining ? c->remaining - 1 : 0;
    }
    if (!c->remaining) {
        nikon_copy_finish(c, 8);
    } else {
        nikon_copy_schedule(c);
    }
}

static MemTxResult nikon_copy_error(NikonCopyState *s, hwaddr offset,
                                  unsigned size, bool write, uint64_t value)
{
    nikon_access_error(&s->reported_error, s->regs.addr + offset, size, write,
                      value, "copy-access-or-profile-not-supported");
    return MEMTX_ERROR;
}

static NikonCopyChannel *nikon_copy_channel(NikonCopyState *s, hwaddr offset,
                                        unsigned *index)
{
    if (offset < 0x10 || offset >= 0xd0) {
        return NULL;
    }
    offset -= 0x10;
    *index = (offset % 0x30) / 4;
    return *index <= 8 ? &s->channel[offset / 0x30] : NULL;
}

static MemTxResult nikon_copy_read(void *opaque, hwaddr offset,
                                 uint64_t *value, unsigned size,
                                 MemTxAttrs attrs)
{
    NikonCopyState *s = opaque;
    NikonCopyChannel *c;
    unsigned index;

    *value = 0;
    if (size == 4 && !(offset & 3)) {
        if (!offset) {
            *value = s->control;
            return MEMTX_OK;
        }
        c = nikon_copy_channel(s, offset, &index);
        if (c && index != 7) {
            *value = c->reg[index];
            return MEMTX_OK;
        }
    }
    return nikon_copy_error(s, offset, size, false, 0);
}

static MemTxResult nikon_copy_write(void *opaque, hwaddr offset,
                                  uint64_t value, unsigned size,
                                  MemTxAttrs attrs)
{
    NikonCopyState *s = opaque;
    NikonCopyChannel *c;
    unsigned index, i;

    if (size != 4 || (offset & 3)) {
        goto error;
    }
    if (!offset) {
        /*
         * The physical roles/effects of all three global bits are unknown.
         * This virtual profile uses bit28 as enable/cancel and retains20/24
         * as configuration inputs, without submitting work or inventing IRQ.
         */
        if (value & ~0x11100000U) {
            goto error;
        }
        s->control = value;
        if (!(value & (1U << 28))) {
            for (i = 0; i < 4; i++) {
                c = &s->channel[i];
                if (c->running) {
                    nikon_copy_finish(c, 1);
                }
            }
        }
        return MEMTX_OK;
    }
    c = nikon_copy_channel(s, offset, &index);
    if (!c || index == 7) {
        goto error;
    }
    if (index == 8) {
        /* Original ISR clears low4 by RMW: zero clears, one preserves. */
        if (value & ~c->reg[8]) {
            goto error;
        }
        c->reg[8] &= value;
        nikon_copy_irq(c);
        return MEMTX_OK;
    }
    if (c->running) {
        if (index != 6 || (value & 3) ||
            ((value ^ c->reg[6]) & ~3U)) {
            goto error;
        }
        c->reg[6] = value;
        nikon_copy_finish(c, 1);
        return MEMTX_OK;
    }
    if (index == 6 && (value & 3)) {
        uint64_t length = (uint64_t)c->reg[0] + 1;

        if (value != 0x11109003 || !(s->control & (1U << 28)) ||
            c->configured != 0x3f || c->reg[3] != 0x30f00 ||
            c->reg[4] != 0x30f00 || c->reg[5] || c->reg[8] ||
            (uint64_t)c->reg[1] + length > (1ULL << 32) ||
            (uint64_t)c->reg[2] + length > (1ULL << 32)) {
            goto error;
        }
        c->reg[6] = value;
        c->remaining = length;
        c->length = length;
        c->started_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        c->running = true;
        nikon_copy_schedule(c);
        return MEMTX_OK;
    }
    if ((index == 3 || index == 4) && value != 0x30f00) {
        goto error;
    }
    if (index == 5 && value) {
        goto error;
    }
    if (index == 6) {
        bool setup = (value & COPY_SETUP_MASK) == 0x9000 &&
                     !((value ^ c->reg[6]) & ~COPY_SETUP_MASK);

        if (!setup && value != (c->reg[6] & ~3U)) {
            goto error;
        }
    }
    c->reg[index] = value;
    if (index == 1) {
        c->source_writes++;
        c->last_source_write = value;
    } else if (index == 2) {
        c->destination_writes++;
        c->last_destination_write = value;
    }
    if (index < 6) {
        c->configured |= 1U << index;
    }
    nikon_copy_irq(c);
    return MEMTX_OK;
error:
    return nikon_copy_error(s, offset, size, true, value);
}

static const MemoryRegionOps nikon_copy_ops = {
    .read_with_attrs = nikon_copy_read,
    .write_with_attrs = nikon_copy_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
};

static void nikon_copy_reset(DeviceState *dev)
{
    NikonCopyState *s = NIKON_COPY(dev);
    unsigned i;

    s->control = 0;
    s->reported_error = false;
    s->first_fault = false;
    s->first_fault_channel = 0;
    s->first_fault_status = 0;
    s->first_fault_source = 0;
    s->first_fault_destination = 0;
    s->first_fault_remaining = 0;
    s->first_fault_source_writes = 0;
    s->first_fault_destination_writes = 0;
    clock_update_hz(s->clock, 48000000);
    for (i = 0; i < 4; i++) {
        NikonCopyChannel *c = &s->channel[i];

        timer_del(c->timer);
        memset(c->reg, 0, sizeof(c->reg));
        c->remaining = c->configured = 0;
        c->length = c->started_ns = 0;
        c->source_writes = c->destination_writes = 0;
        c->last_source_write = c->last_destination_write = 0;
        c->running = false;
        nikon_copy_irq(c);
    }
}

static void nikon_copy_get_clock(Object *obj, Visitor *v, const char *name,
                                 void *opaque, Error **errp)
{
    uint64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    visit_type_uint64(v, name, &now, errp);
}

static void nikon_copy_get_deadline(Object *obj, Visitor *v, const char *name,
                                    void *opaque, Error **errp)
{
    NikonCopyChannel *c = opaque;
    uint64_t deadline = timer_pending(c->timer) ?
                        timer_expire_time_ns(c->timer) : 0;

    visit_type_uint64(v, name, &deadline, errp);
}

static void nikon_copy_get_running(Object *obj, Visitor *v, const char *name,
                                   void *opaque, Error **errp)
{
    NikonCopyChannel *c = opaque;
    bool running = c->running;

    visit_type_bool(v, name, &running, errp);
}

static bool nikon_copy_get_first_fault(Object *obj, Error **errp)
{
    return NIKON_COPY(obj)->first_fault;
}

static void nikon_copy_init(Object *obj)
{
    NikonCopyState *s = NIKON_COPY(obj);
    unsigned i;

    s->clock = qdev_init_clock_out(DEVICE(obj), "nominal-copy-clock");
    memory_region_init_io(&s->regs, obj, &nikon_copy_ops, s, "nikon.copy", 0xd0);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
    object_property_add(obj, "virtual-clock-ns", "uint64",
                        nikon_copy_get_clock, NULL, NULL, NULL);
    object_property_add_bool(obj, "first-fault", nikon_copy_get_first_fault,
                             NULL);
    object_property_add_uint32_ptr(obj, "first-fault-channel",
                                   &s->first_fault_channel, OBJ_PROP_FLAG_READ);
    object_property_add_uint32_ptr(obj, "first-fault-status",
                                   &s->first_fault_status, OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(obj, "first-fault-source",
                                   &s->first_fault_source, OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(obj, "first-fault-destination",
                                   &s->first_fault_destination,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(obj, "first-fault-remaining",
                                   &s->first_fault_remaining,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(obj, "first-fault-source-writes",
                                   &s->first_fault_source_writes,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(obj, "first-fault-destination-writes",
                                   &s->first_fault_destination_writes,
                                   OBJ_PROP_FLAG_READ);
    for (i = 0; i < 4; i++) {
        NikonCopyChannel *c = &s->channel[i];
        g_autofree char *start = g_strdup_printf("channel%u-start-ns", i);
        g_autofree char *total = g_strdup_printf("channel%u-total-bytes", i);
        g_autofree char *remaining = g_strdup_printf("channel%u-remaining-bytes",
                                                    i);
        g_autofree char *deadline = g_strdup_printf("channel%u-next-deadline-ns",
                                                   i);
        g_autofree char *running = g_strdup_printf("channel%u-running", i);
        g_autofree char *source_writes =
            g_strdup_printf("channel%u-source-write-count", i);
        g_autofree char *destination_writes =
            g_strdup_printf("channel%u-destination-write-count", i);
        g_autofree char *last_source_write =
            g_strdup_printf("channel%u-last-source-write", i);
        g_autofree char *last_destination_write =
            g_strdup_printf("channel%u-last-destination-write", i);

        c->owner = s;
        c->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, nikon_copy_step, c);
        sysbus_init_irq(SYS_BUS_DEVICE(obj), &c->irq);
        object_property_add_uint64_ptr(obj, start, &c->started_ns,
                                       OBJ_PROP_FLAG_READ);
        object_property_add_uint64_ptr(obj, total, &c->length,
                                       OBJ_PROP_FLAG_READ);
        object_property_add_uint64_ptr(obj, remaining, &c->remaining,
                                       OBJ_PROP_FLAG_READ);
        object_property_add(obj, deadline, "uint64", nikon_copy_get_deadline,
                            NULL, NULL, c);
        object_property_add(obj, running, "bool", nikon_copy_get_running,
                            NULL, NULL, c);
        object_property_add_uint64_ptr(obj, source_writes,
                                       &c->source_writes,
                                       OBJ_PROP_FLAG_READ);
        object_property_add_uint64_ptr(obj, destination_writes,
                                       &c->destination_writes,
                                       OBJ_PROP_FLAG_READ);
        object_property_add_uint32_ptr(obj, last_source_write,
                                       &c->last_source_write,
                                       OBJ_PROP_FLAG_READ);
        object_property_add_uint32_ptr(obj, last_destination_write,
                                       &c->last_destination_write,
                                       OBJ_PROP_FLAG_READ);
    }
}

static void nikon_copy_finalize(Object *obj)
{
    NikonCopyState *s = NIKON_COPY(obj);
    unsigned i;

    for (i = 0; i < 4; i++) {
        timer_free(s->channel[i].timer);
    }
}

static const VMStateDescription nikon_copy_vmstate = {
    .name = "nikon-copy", .unmigratable = true,
};

static void nikon_copy_region_add(MemoryListener *listener,
                                  MemoryRegionSection *section)
{
    NikonCopyState *s = container_of(listener, NikonCopyState, memory_listener);

    NikonCopyRange range = {
        .start = int128_make64(section->offset_within_address_space),
        .end = int128_add(int128_make64(section->offset_within_address_space),
                          section->size),
        .readable = memory_access_is_direct(section->mr, false,
                                            MEMTXATTRS_UNSPECIFIED),
        .writable = !section->readonly && memory_region_is_ram(section->mr) &&
                    !memory_region_is_rom(section->mr) &&
                    !memory_region_is_romd(section->mr) &&
                    memory_access_is_direct(section->mr, true,
                                            MEMTXATTRS_UNSPECIFIED),
    };

    g_array_append_val(s->ranges, range);
}

static void nikon_copy_region_del(MemoryListener *listener,
                                  MemoryRegionSection *section)
{
    NikonCopyState *s = container_of(listener, NikonCopyState, memory_listener);
    Int128 start = int128_make64(section->offset_within_address_space);
    Int128 end = int128_add(start, section->size);

    /* Match the old interval, not the MR's current ROMD/permission mode. */
    for (unsigned i = 0; i < s->ranges->len; i++) {
        NikonCopyRange *range = &g_array_index(s->ranges, NikonCopyRange, i);

        if (int128_eq(range->start, start) && int128_eq(range->end, end)) {
            g_array_remove_index_fast(s->ranges, i);
            return;
        }
    }
    g_assert_not_reached();
}

static void nikon_copy_realize(DeviceState *dev, Error **errp)
{
    NikonCopyState *s = NIKON_COPY(dev);

    s->dma = g_new0(AddressSpace, 1);
    address_space_init(s->dma, s->memory ? s->memory : get_system_memory(),
                       "nikon.copy");
    s->ranges = g_array_new(false, false, sizeof(NikonCopyRange));
    s->memory_listener = (MemoryListener) {
        .name = "nikon.copy",
        .region_add = nikon_copy_region_add,
        .region_del = nikon_copy_region_del,
    };
    memory_listener_register(&s->memory_listener, s->dma);
}

static void nikon_copy_unrealize(DeviceState *dev)
{
    NikonCopyState *s = NIKON_COPY(dev);

    /* No callback may access an address space after its owner disconnects. */
    nikon_copy_reset(dev);
    memory_listener_unregister(&s->memory_listener);
    g_clear_pointer(&s->ranges, g_array_unref);
    address_space_destroy_free(g_steal_pointer(&s->dma));
}

static const Property nikon_copy_properties[] = {
    DEFINE_PROP_LINK("memory", NikonCopyState, memory,
                     TYPE_MEMORY_REGION, MemoryRegion *),
};

static void nikon_copy_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &nikon_copy_vmstate;
    dc->realize = nikon_copy_realize;
    dc->unrealize = nikon_copy_unrealize;
    device_class_set_props(dc, nikon_copy_properties);
    dc->user_creatable = false;
    device_class_set_legacy_reset(dc, nikon_copy_reset);
}

static const TypeInfo nikon_copy_type = {
    .name = TYPE_NIKON_COPY,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonCopyState),
    .instance_init = nikon_copy_init,
    .instance_finalize = nikon_copy_finalize,
    .class_init = nikon_copy_class_init,
};

static void nikon_copy_register_types(void)
{
    type_register_static(&nikon_copy_type);
}
type_init(nikon_copy_register_types)
