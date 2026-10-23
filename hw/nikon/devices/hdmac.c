/*
 * Eight-channel HDMAC-compatible functional DMA interface.
 * Register/GIC geometry comes from original MAS and matching author sources.
 * Zero reset and nominal bus timing are virtual board assumptions.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "migration/vmstate.h"
#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "system/address-spaces.h"
#include "system/dma.h"
#include "hw/nikon/devices.h"

#define HDMAC_DE (1U << 31)
#define HDMAC_EB (1U << 31)
#define HDMAC_ST (1U << 29)
#define HDMAC_PB (1U << 30)
#define HDMAC_DH (1U << 24)
#define HDMAC_SS (7U << 16)
#define HDMAC_CI (1U << 19)
#define HDMAC_EI (1U << 20)
#define HDMAC_RD (1U << 21)
#define HDMAC_RS (1U << 22)
#define HDMAC_RC (1U << 23)
#define HDMAC_COUNT 0xfffffU
OBJECT_DECLARE_SIMPLE_TYPE(NikonHDMACState, NIKON_HDMAC)

typedef struct NikonHDMACChannel {
    NikonHDMACState *owner;
    uint32_t ca, cb, source, destination;
    QEMUTimer *timer;
    qemu_irq irq;
    bool running;
    int64_t ready_at;
    int64_t wait_ns;
    uint64_t generation;
    uint32_t remaining;
    uint32_t initial_count, initial_source, initial_destination;
} NikonHDMACChannel;

typedef struct NikonHDMACReadonlyRange {
    Int128 start;
    Int128 end;
} NikonHDMACReadonlyRange;

struct NikonHDMACState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    MemoryRegion *memory;
    AddressSpace *dma;
    MemoryListener memory_listener;
    GArray *readonly;
    Clock *clock;
    uint32_t bank;
    uint32_t control;
    unsigned cursor;
    NikonHDMACChannel channel[8];
    bool reported_error;
    bool request[32];
};

static bool nikon_hdmac_requested(NikonHDMACChannel *c)
{
    unsigned selection = (c->ca >> 24) & 31;

    return !(c->ca & HDMAC_PB) && !(c->owner->control & HDMAC_DH) &&
           (!selection || c->owner->request[selection]);
}

static void nikon_hdmac_update(NikonHDMACChannel *c)
{
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    if (!c->running) {
        return;
    }
    if (!nikon_hdmac_requested(c)) {
        if (timer_pending(c->timer)) {
            c->wait_ns = MAX(1, c->ready_at - now);
            timer_del(c->timer);
        }
    } else if (!timer_pending(c->timer)) {
        c->ready_at = now + MAX(1, c->wait_ns);
        timer_mod(c->timer, c->ready_at);
    }
}

static void nikon_hdmac_irq(NikonHDMACChannel *c)
{
    unsigned status = (c->cb >> 16) & 7;

    qemu_set_irq(c->irq, status == 5 ? !!(c->cb & HDMAC_CI) :
                 (status >= 1 && status <= 4 && (c->cb & HDMAC_EI)));
}

static void nikon_hdmac_finish(NikonHDMACChannel *c, unsigned status)
{
    timer_del(c->timer);
    c->generation++;
    c->running = false;
    c->ca &= ~HDMAC_EB;
    c->cb = (c->cb & ~HDMAC_SS) | (status << 16);
    nikon_hdmac_irq(c);
}

static MemTxResult nikon_hdmac_write_memory(NikonHDMACState *s,
                                           hwaddr address, const void *data,
                                           unsigned width)
{
    hwaddr current = address;
    unsigned remaining = width;
    RCU_READ_LOCK_GUARD();

    /* The listener reports the selected flat view's effective permissions,
     * including readonly aliases/containers. Translation exposes only a leaf.
     * Do not use memory_region_find(): it can ascend into another root's AS. */
    for (unsigned i = 0; i < s->readonly->len; i++) {
        NikonHDMACReadonlyRange *range = &g_array_index(
            s->readonly, NikonHDMACReadonlyRange, i);

        if (int128_lt(int128_make64(address), range->end) &&
            int128_lt(range->start, int128_make64(address + width))) {
            return MEMTX_ERROR;
        }
    }

    /* QEMU may intentionally ignore ROM writes. DMA must report their failure,
     * while actual MMIO sinks retain their causal device transactions.
     */
    while (remaining) {
        hwaddr offset, length = remaining;
        MemoryRegion *mr = address_space_translate(s->dma, current, &offset,
                                                  &length, true,
                                                  MEMTXATTRS_UNSPECIFIED);

        if (memory_region_is_rom(mr) || memory_region_is_romd(mr) || !length) {
            return MEMTX_ERROR;
        }
        /* Translation bounds RAM spans. MMIO lengths need not be bounded by
         * their mapping, so inspect every covered byte without bus accesses.
         * The actual write below still keeps the original complete beat width.
         */
        if (!memory_region_is_ram(mr)) {
            length = 1;
        }
        current += length;
        remaining -= length;
    }
    return dma_memory_write(s->dma, address, data, width,
                            MEMTXATTRS_UNSPECIFIED);
}

static void nikon_hdmac_transfer(void *opaque)
{
    NikonHDMACChannel *trigger = opaque;
    NikonHDMACState *s = trigger->owner;
    NikonHDMACChannel *c = NULL;
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    unsigned start = (s->control & (1U << 28)) ? s->cursor : 0;
    unsigned candidate;
    unsigned width, i;
    uint32_t beats;
    bool fixed_source, fixed_destination;
    uint8_t data[4];
    uint64_t generation;

    /* Fixed low-index or rotating arbitration among ready virtual requests. */
    for (candidate = 0; candidate < 8; candidate++) {
        unsigned index = (start + candidate) % 8;

        if (s->channel[index].running && s->channel[index].ready_at <= now &&
            nikon_hdmac_requested(&s->channel[index])) {
            c = &s->channel[index];
            s->cursor = (index + 1) % 8;
            break;
        }
    }
    if (!c) {
        return;
    }
    if (c != trigger && trigger->running) {
        timer_mod(trigger->timer, now);
    }
    /* Arbitration can select a channel with its own due timer still queued. */
    timer_del(c->timer);
    width = 1U << ((c->cb >> 26) & 3);
    /* Demand requests accept one beat, then re-evaluate the actual wire. */
    beats = ((c->ca >> 24) & 31) ? 1 : c->remaining;
    fixed_source = c->cb & (1U << 25);
    fixed_destination = c->cb & (1U << 24);
    generation = c->generation;
    c->wait_ns = MAX(1, clock_ticks_to_ns(s->clock, 1));

    for (i = 0; i < beats; i++) {
        MemTxResult result;

        if ((uint64_t)c->source + width > 1ULL << 32 ||
            (uint64_t)c->destination + width > 1ULL << 32) {
            nikon_hdmac_finish(c, 1);
            return;
        }
        result = dma_memory_read(s->dma, c->source, data, width,
                                  MEMTXATTRS_UNSPECIFIED);
        if (!c->running || c->generation != generation) {
            return;
        }
        if (result != MEMTX_OK) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "nikon.hdmac: bank%u source bus error "
                          "at 0x%08x; SS3\n", s->bank, c->source);
            nikon_hdmac_finish(c, 3);
            return;
        }
        result = nikon_hdmac_write_memory(s, c->destination, data, width);
        if (!c->running || c->generation != generation) {
            return;
        }
        if (result != MEMTX_OK) {
            qemu_log_mask(LOG_GUEST_ERROR, "nikon.hdmac: bank%u destination bus "
                          "error at 0x%08x; SS4\n", s->bank, c->destination);
            nikon_hdmac_finish(c, 4);
            return;
        }
        if (!fixed_source) {
            if ((uint64_t)c->source + width == 1ULL << 32 && c->remaining > 1) {
                nikon_hdmac_finish(c, 1);
                return;
            }
            c->source += width;
        }
        if (!fixed_destination) {
            if ((uint64_t)c->destination + width == 1ULL << 32 &&
                c->remaining > 1) {
                nikon_hdmac_finish(c, 1);
                return;
            }
            c->destination += width;
        }
        c->remaining--;
        /* PB/DH/DREQ can change through a genuine reentrant bus operation.
         * Commit the accepted beat once, then suspend before the next one. */
        if (c->remaining && !nikon_hdmac_requested(c)) {
            return;
        }
    }
    if (c->remaining) {
        if (nikon_hdmac_requested(c)) {
            nikon_hdmac_update(c);
        }
        return;
    }
    /* Matching IP reloads selected fields only after normal completion.
     * EB still clears: reload does not create another peripheral request.
     * Non-reload end counters remain a functional readback convention. */
    c->ca = (c->ca & ~HDMAC_COUNT) |
            ((c->cb & HDMAC_RC) ? c->initial_count : 0);
    if (c->cb & HDMAC_RS) {
        c->source = c->initial_source;
    }
    if (c->cb & HDMAC_RD) {
        c->destination = c->initial_destination;
    }
    nikon_hdmac_finish(c, 5);
}

static bool nikon_hdmac_start_supported(uint32_t ca, uint32_t cb,
                                       uint32_t source, uint32_t destination)
{
    unsigned beat = (ca >> 20) & 15;
    unsigned width = (cb >> 26) & 3;

    /* No wrapped bursts, transfer transforms or protection transactions. */
    return width < 3 &&
           (beat == 0 || beat == 8 || beat == 9 || beat == 11 ||
            beat == 13 || beat == 15) &&
           !(cb & 0xc000ff00U) && ((cb >> 28) & 3) < 3 &&
           !(source & ((1U << width) - 1)) &&
           !(destination & ((1U << width) - 1));
}

static void nikon_hdmac_schedule(NikonHDMACChannel *c)
{
    NikonHDMACState *s = c->owner;
    uint32_t beats = (((c->ca >> 16) & 15) + 1) * ((c->ca & 0xffff) + 1);

    unsigned selection = (c->ca >> 24) & 31;

    if (c->running || !(s->control & HDMAC_DE) || !(c->ca & HDMAC_EB) ||
        (!selection && !(c->ca & HDMAC_ST))) {
        return;
    }
    c->running = true;
    c->generation++;
    c->remaining = beats;
    c->initial_count = c->ca & HDMAC_COUNT;
    c->initial_source = c->source;
    c->initial_destination = c->destination;
    c->wait_ns = MAX(1, clock_ticks_to_ns(s->clock, selection ? 1 : beats));
    nikon_hdmac_update(c);
}

static void nikon_hdmac_request(void *opaque, int selection, int level)
{
    NikonHDMACState *s = opaque;
    unsigned i;

    s->request[selection] = level;
    for (i = 0; i < 8; i++) {
        NikonHDMACChannel *c = &s->channel[i];

        if (!c->running || ((c->ca >> 24) & 31) != selection) {
            continue;
        }
        nikon_hdmac_update(c);
    }
}

static MemTxResult nikon_hdmac_error(NikonHDMACState *s, hwaddr offset,
                                   unsigned size, bool write, uint64_t value)
{
    nikon_access_error(&s->reported_error,
                      s->regs.addr + offset,
                      size, write, value,
                      "hdmac-access-or-command-not-supported");
    return MEMTX_ERROR;
}

static MemTxResult nikon_hdmac_read(void *opaque, hwaddr offset, uint64_t *value,
                                  unsigned size, MemTxAttrs attrs)
{
    NikonHDMACState *s = opaque;
    NikonHDMACChannel *c;

    *value = 0;
    if (size != 4 || (offset & 3) || (offset && offset < 0x10) ||
        offset >= 0x90) {
        return nikon_hdmac_error(s, offset, size, false, 0);
    }
    if (!offset) {
        *value = s->control;
        return MEMTX_OK;
    }
    c = &s->channel[(offset - 0x10) >> 4];
    switch (offset & 15) {
    case 0:
        *value = c->ca;
        break;
    case 4:
        *value = c->cb;
        break;
    case 8:
        *value = c->source;
        break;
    case 12:
        *value = c->destination;
        break;
    }
    return MEMTX_OK;
}

static MemTxResult nikon_hdmac_write(void *opaque, hwaddr offset, uint64_t value,
                                   unsigned size, MemTxAttrs attrs)
{
    NikonHDMACState *s = opaque;
    NikonHDMACChannel *c;
    uint32_t old;
    unsigned i;

    if (size != 4 || (offset & 3) || (offset && offset < 0x10) ||
        offset >= 0x90) {
        return nikon_hdmac_error(s, offset, size, true, value);
    }
    if (!offset) {
        /* Reference DH has only the bound halt1/resume0 encodings. */
        if (value & ~0x91000000U) {
            return nikon_hdmac_error(s, offset, size, true, value);
        }
        /* Preflight every armed request before changing DE or any timer. */
        if (value & HDMAC_DE) {
            for (i = 0; i < 8; i++) {
                c = &s->channel[i];
                if ((c->ca & HDMAC_EB) &&
                    !nikon_hdmac_start_supported(c->ca, c->cb,
                                                c->source, c->destination)) {
                    return nikon_hdmac_error(s, offset, size, true, value);
                }
            }
        }
        s->control = value;
        for (i = 0; i < 8; i++) {
            c = &s->channel[i];
            if (!(value & HDMAC_DE) && c->running) {
                nikon_hdmac_finish(c, 2);
            } else {
                nikon_hdmac_schedule(c);
                nikon_hdmac_update(c);
            }
        }
        return MEMTX_OK;
    }
    c = &s->channel[(offset - 0x10) >> 4];
    switch (offset & 15) {
    case 0:
        old = c->ca;
        if (c->running && ((value ^ old) & ~(HDMAC_EB | HDMAC_PB))) {
            return nikon_hdmac_error(s, offset, size, true, value);
        }
        if ((value & HDMAC_EB) &&
            !nikon_hdmac_start_supported(value, c->cb,
                                        c->source, c->destination)) {
            return nikon_hdmac_error(s, offset, size, true, value);
        }
        c->ca = value;
        if (!(value & HDMAC_EB) && (old & HDMAC_EB)) {
            nikon_hdmac_finish(c, 2);
        } else {
            nikon_hdmac_schedule(c);
            nikon_hdmac_update(c);
        }
        break;
    case 4:
        if ((value & 0xc000ffffU) || ((value >> 26) & 3) == 3 ||
            ((value >> 28) & 3) == 3 || ((value & HDMAC_SS) &&
                             (value & HDMAC_SS) != (c->cb & HDMAC_SS)) ||
            (c->running &&
             ((value ^ c->cb) & ~(HDMAC_SS | HDMAC_CI | HDMAC_EI)))) {
            return nikon_hdmac_error(s, offset, size, true, value);
        }
        if ((c->ca & HDMAC_EB) &&
            !nikon_hdmac_start_supported(c->ca, value,
                                        c->source, c->destination)) {
            return nikon_hdmac_error(s, offset, size, true, value);
        }
        c->cb = value;
        nikon_hdmac_irq(c);
        break;
    case 8:
    case 12:
        if (c->running) {
            return nikon_hdmac_error(s, offset, size, true, value);
        }
        if ((c->ca & HDMAC_EB) &&
            !nikon_hdmac_start_supported(c->ca, c->cb,
                                        (offset & 15) == 8 ? value : c->source,
                                        (offset & 15) == 12 ? value :
                                                            c->destination)) {
            return nikon_hdmac_error(s, offset, size, true, value);
        }
        if ((offset & 15) == 8) {
            c->source = value;
        } else {
            c->destination = value;
        }
        break;
    }
    return MEMTX_OK;
}

static const MemoryRegionOps nikon_hdmac_ops = {
    .read_with_attrs = nikon_hdmac_read,
    .write_with_attrs = nikon_hdmac_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
};

static void nikon_hdmac_reset(DeviceState *dev)
{
    NikonHDMACState *s = NIKON_HDMAC(dev);
    unsigned i;

    s->control = 0;
    s->cursor = 0;
    s->reported_error = false;
    clock_update_hz(s->clock, 48000000);
    for (i = 0; i < 8; i++) {
        NikonHDMACChannel *c = &s->channel[i];

        timer_del(c->timer);
        c->generation++;
        c->ca = c->cb = c->source = c->destination = 0;
        c->running = false;
        c->remaining = 0;
        c->initial_count = c->initial_source = c->initial_destination = 0;
        c->ready_at = c->wait_ns = 0;
        nikon_hdmac_irq(c);
    }
}

static void nikon_hdmac_init(Object *obj)
{
    NikonHDMACState *s = NIKON_HDMAC(obj);
    unsigned i;

    s->clock = qdev_init_clock_out(DEVICE(obj), "nominal-hdmac-clock");
    qdev_init_gpio_in(DEVICE(obj), nikon_hdmac_request, 32);
    memory_region_init_io(&s->regs, obj, &nikon_hdmac_ops, s,
                          "nikon.hdmac", 0x90);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
    for (i = 0; i < 8; i++) {
        s->channel[i].owner = s;
        s->channel[i].timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                           nikon_hdmac_transfer, &s->channel[i]);
        sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->channel[i].irq);
    }
}

static void nikon_hdmac_finalize(Object *obj)
{
    NikonHDMACState *s = NIKON_HDMAC(obj);
    unsigned i;

    for (i = 0; i < 8; i++) {
        timer_free(s->channel[i].timer);
    }
}

static const Property nikon_hdmac_properties[] = {
    DEFINE_PROP_UINT32("bank", NikonHDMACState, bank, 0),
    DEFINE_PROP_LINK("memory", NikonHDMACState, memory,
                     TYPE_MEMORY_REGION, MemoryRegion *),
};

static void nikon_hdmac_region_add(MemoryListener *listener,
                                   MemoryRegionSection *section)
{
    NikonHDMACState *s = container_of(listener, NikonHDMACState,
                                     memory_listener);

    if (section->readonly) {
        NikonHDMACReadonlyRange range = {
            .start = int128_make64(section->offset_within_address_space),
            .end = int128_add(int128_make64(section->offset_within_address_space),
                              section->size),
        };

        g_array_append_val(s->readonly, range);
    }
}

static void nikon_hdmac_region_del(MemoryListener *listener,
                                   MemoryRegionSection *section)
{
    NikonHDMACState *s = container_of(listener, NikonHDMACState,
                                     memory_listener);
    Int128 start = int128_make64(section->offset_within_address_space);
    Int128 end = int128_add(start, section->size);

    if (!section->readonly) {
        return;
    }
    for (unsigned i = 0; i < s->readonly->len; i++) {
        NikonHDMACReadonlyRange *range = &g_array_index(
            s->readonly, NikonHDMACReadonlyRange, i);

        if (int128_eq(range->start, start) && int128_eq(range->end, end)) {
            g_array_remove_index_fast(s->readonly, i);
            return;
        }
    }
    g_assert_not_reached();
}

static void nikon_hdmac_realize(DeviceState *dev, Error **errp)
{
    NikonHDMACState *s = NIKON_HDMAC(dev);

    s->dma = g_new0(AddressSpace, 1);
    address_space_init(s->dma, s->memory ? s->memory : get_system_memory(),
                       "nikon.hdmac");
    s->readonly = g_array_new(false, false, sizeof(NikonHDMACReadonlyRange));
    s->memory_listener = (MemoryListener) {
        .name = "nikon.hdmac",
        .region_add = nikon_hdmac_region_add,
        .region_del = nikon_hdmac_region_del,
    };
    memory_listener_register(&s->memory_listener, s->dma);
}

static void nikon_hdmac_unrealize(DeviceState *dev)
{
    NikonHDMACState *s = NIKON_HDMAC(dev);

    nikon_hdmac_reset(dev);
    memory_listener_unregister(&s->memory_listener);
    g_clear_pointer(&s->readonly, g_array_unref);
    address_space_destroy_free(g_steal_pointer(&s->dma));
}
static const VMStateDescription nikon_hdmac_vmstate = {
    .name = "nikon-hdmac", .unmigratable = true,
};

static void nikon_hdmac_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &nikon_hdmac_vmstate;
    dc->realize = nikon_hdmac_realize;
    dc->unrealize = nikon_hdmac_unrealize;
    dc->user_creatable = false;
    device_class_set_props(dc, nikon_hdmac_properties);
    device_class_set_legacy_reset(dc, nikon_hdmac_reset);
}

static const TypeInfo nikon_hdmac_type = {
    .name = TYPE_NIKON_HDMAC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonHDMACState),
    .instance_init = nikon_hdmac_init,
    .instance_finalize = nikon_hdmac_finalize,
    .class_init = nikon_hdmac_class_init,
};

static void nikon_hdmac_register_types(void)
{
    type_register_static(&nikon_hdmac_type);
}
type_init(nikon_hdmac_register_types)
