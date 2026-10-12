/*
 * Nikon graphics packet transfer.  Register production and the IRQ130 ACK
 * are joined to C1.70 106ee148 and 106ee3d6 respectively.  The byte-stream
 * command is produced by 106eeb5c with control.bit3 and a byte extent.
 * Zero reset and a 48MHz transfer clock are functional-board assumptions.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-clock.h"
#include "migration/vmstate.h"
#include "system/address-spaces.h"
#include "system/dma.h"
#include "hw/nikon/transfer.h"

OBJECT_DECLARE_SIMPLE_TYPE(NikonTransferState, NIKON_TRANSFER)

enum {
    TRANSFER_SOURCE, TRANSFER_DESTINATION, TRANSFER_SOURCE_GEOMETRY,
    TRANSFER_DESTINATION_GEOMETRY, TRANSFER_CONTROL, TRANSFER_START,
    TRANSFER_EXTENT, TRANSFER_EXTENSION, TRANSFER_PADDING,
    TRANSFER_SECONDARY_SOURCE, TRANSFER_SECONDARY_DESTINATION,
    TRANSFER_REGISTER_COUNT,
};

static const hwaddr register_offsets[TRANSFER_REGISTER_COUNT] = {
    0xc00, 0xc04, 0xc08, 0xc0c, 0xc10, 0xc18,
    0xc20, 0xc24, 0xc28, 0xc34, 0xc38,
};

/* Whole 106edfd6 initialization and empty-table 106ee04a configuration,
 * not a permissive aperture or an implementation of the optional table jobs.
 */
static const hwaddr setup_offsets[] = {
    0x1c, 0x34, 0xc1c, 0x1020, 0x1248, 0x151c, 0x155c, 0x1214, 0x1298,
};
static const uint32_t setup_values[] = {
    0, 0, 0x8000, 0, 1, 1, 1, 0x0100ffff, 0,
};

typedef struct NikonTransferStream {
    uint32_t source, units64, written;
    uint64_t token;
    GBytes *bytes;
} NikonTransferStream;

struct NikonTransferState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    Clock *clock;
    QEMUTimer *timer;
    qemu_irq irq[3];
    uint32_t reg[TRANSFER_REGISTER_COUNT];
    uint32_t setup[ARRAY_SIZE(setup_offsets)];
    uint32_t configured;
    uint64_t source, destination, remaining;
    uint64_t plane_remaining, secondary_source, secondary_destination;
    bool running, completed, reported_error;
    NikonTransferStream stream[2];
    qemu_irq stream_request[2];
    uint32_t stream_setup;
    uint64_t stream_serial;
};

static void nikon_transfer_irq(NikonTransferState *s)
{
    /* IRQ129/131 belong to other producers; they are never invented here. */
    qemu_set_irq(s->irq[0], 0);
    /* Bit15 is the source-installed transfer completion enable convention. */
    qemu_set_irq(s->irq[1], s->completed && (s->setup[2] & 0x8000));
    qemu_set_irq(s->irq[2], 0);
}

static MemTxResult nikon_transfer_error(NikonTransferState *s, hwaddr offset,
                                       unsigned size, bool write,
                                       uint64_t value, const char *reason)
{
    nikon_access_error(&s->reported_error, s->regs.addr + offset, size,
                       write, value, reason);
    return MEMTX_ERROR;
}

static uint64_t nikon_transfer_span(hwaddr address, uint64_t length, bool write)
{
    MemoryRegionSection section;
    uint64_t span;
    bool direct;

    if (!length) {
        return 0;
    }
    section = memory_region_find(get_system_memory(), address, length);
    if (!section.mr) {
        return 0;
    }
    span = int128_get64(section.size);
    direct = span && section.offset_within_address_space == address &&
             memory_access_is_direct(section.mr, write,
                                     MEMTXATTRS_UNSPECIFIED) &&
             (!write || !section.readonly);
    memory_region_unref(section.mr);
    return direct ? span : 0;
}

static bool nikon_transfer_range(hwaddr address, uint64_t length, bool write)
{
    while (length) {
        uint64_t span = nikon_transfer_span(address, length, write);

        if (!span) {
            return false;
        }
        address += span;
        length -= span;
    }
    return true;
}

static void nikon_transfer_schedule(NikonTransferState *s)
{
    timer_mod(s->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
              clock_ticks_to_ns(s->clock, MIN(s->plane_remaining, 4096)) + 1);
}

static void nikon_transfer_step(void *opaque)
{
    NikonTransferState *s = opaque;
    uint8_t buffer[4096];
    unsigned length = MIN(s->plane_remaining, sizeof(buffer));
    MemTxResult result;
    MemTxAttrs attrs = MEMTXATTRS_UNSPECIFIED;

    if (!s->running) {
        return;
    }
    /* Direct ROMD reads are not capped by QEMU at a short alias boundary.
     * Select each effective backing separately, including adjacent ROMD/RAM.
     * Recheck the current destination too: topology may change after START.
     */
    length = nikon_transfer_span(s->source, length, false);
    length = nikon_transfer_span(s->destination, length, true);
    if (!length) {
        s->running = false;
        nikon_transfer_error(s, 0xc18, 4, true, 1,
                             "transfer-memory-range-not-backed");
        return;
    }
    result = dma_memory_read(&address_space_memory, s->source, buffer,
                             length, attrs);
    if (result == MEMTX_OK) {
        attrs.memory = true;
        result = dma_memory_write(&address_space_memory, s->destination,
                                  buffer, length, attrs);
    }
    if (result != MEMTX_OK) {
        s->running = false;
        nikon_transfer_error(s, 0xc18, 4, true, 1, "transfer-dma-failed");
        return;
    }
    s->source += length;
    s->destination += length;
    s->remaining -= length;
    s->plane_remaining -= length;
    if (!s->plane_remaining && s->remaining) {
        s->source = s->secondary_source;
        s->destination = s->secondary_destination;
        s->plane_remaining = s->remaining;
    }
    if (s->remaining) {
        nikon_transfer_schedule(s);
    } else {
        s->running = false;
        s->completed = true;
        nikon_transfer_irq(s);
    }
}

static int nikon_transfer_register(hwaddr offset)
{
    unsigned i;

    for (i = 0; i < TRANSFER_REGISTER_COUNT; i++) {
        if (register_offsets[i] == offset) {
            return i;
        }
    }
    return -1;
}

static int nikon_transfer_setup(hwaddr offset)
{
    unsigned i;

    for (i = 0; i < ARRAY_SIZE(setup_offsets); i++) {
        if (setup_offsets[i] == offset) {
            return i;
        }
    }
    return -1;
}

static MemTxResult nikon_transfer_read(void *opaque, hwaddr offset,
                                      uint64_t *value, unsigned size,
                                      MemTxAttrs attrs)
{
    NikonTransferState *s = opaque;
    int index = nikon_transfer_register(offset);
    int setup = nikon_transfer_setup(offset);

    if (size != 4 || (index < 0 && setup < 0)) {
        return nikon_transfer_error(s, offset, size, false, 0,
                                    "transfer-read-not-supported");
    }
    *value = index >= 0 ? s->reg[index] : s->setup[setup];
    return MEMTX_OK;
}

static MemTxResult nikon_transfer_stream_write(NikonTransferState *s,
                                               hwaddr offset, uint64_t value)
{
    unsigned bank = offset >= 0x10e0;
    unsigned field = offset - (bank ? 0x10e0 : 0x10d0);
    NikonTransferStream *stream = &s->stream[bank];
    uint8_t *bytes;
    uint64_t address, remaining, length;

    if (stream->bytes) {
        goto unsupported;
    }
    if (field == 4 && !(value & 1)) {
        stream->source = value;
        stream->written |= 1;
        return MEMTX_OK;
    }
    if (field == 12 && value <= NIKON_TRANSFER_STREAM_BYTES / 8) {
        stream->units64 = value;
        stream->written |= 2;
        return MEMTX_OK;
    }
    if (field != 0 || value != 1 || s->stream_setup != 3 ||
        stream->written != 3 || !stream->units64) {
        goto unsupported;
    }
    /* 106ee04a halves the descriptor's 32-bit word extent for this port. */
    length = 8ULL * stream->units64;
    address = stream->source;
    if (address > UINT32_MAX - length ||
        !nikon_transfer_range(address, length, false)) {
        return nikon_transfer_error(s, offset, 4, true, value,
                                    "transfer-stream-memory-not-backed");
    }
    bytes = g_malloc(length);
    remaining = length;
    while (remaining) {
        uint64_t span = nikon_transfer_span(address, remaining, false);

        if (!span || dma_memory_read(&address_space_memory, address,
                bytes + length - remaining, span,
                MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
            g_free(bytes);
            return nikon_transfer_error(s, offset, 4, true, value,
                                        "transfer-stream-read-failed");
        }
        address += span;
        remaining -= span;
    }
    stream->bytes = g_bytes_new_take(bytes, length);
    if (++s->stream_serial == 0) {
        ++s->stream_serial;
    }
    stream->token = s->stream_serial;
    /* Actual submitted bytes, not processing, a fence or an IRQ129 cause. */
    qemu_set_irq(s->stream_request[bank], 1);
    return MEMTX_OK;

unsupported:
    return nikon_transfer_error(s, offset, 4, true, value,
                                "transfer-stream-not-supported");
}

static MemTxResult nikon_transfer_write(void *opaque, hwaddr offset,
                                       uint64_t value, unsigned size,
                                       MemTxAttrs attrs)
{
    NikonTransferState *s = opaque;
    int index = nikon_transfer_register(offset);
    static const unsigned byte_factors[] = { 4, 3, 2, 2, 2, 1 };
    uint32_t required = (1U << TRANSFER_SOURCE) |
                        (1U << TRANSFER_DESTINATION) |
                        (1U << TRANSFER_CONTROL);
    uint64_t extent;
    unsigned selector;
    bool secondary = false;
    int setup = nikon_transfer_setup(offset);

    if (size != 4) {
        goto unsupported;
    }
    if ((offset >= 0x10d0 && offset <= 0x10dc) ||
        (offset >= 0x10e0 && offset <= 0x10ec)) {
        return nikon_transfer_stream_write(s, offset, value);
    }
    if (setup >= 0) {
        if ((offset == 0x1214 || offset == 0x1298) &&
            (s->stream[0].bytes || s->stream[1].bytes)) {
            goto unsupported;
        }
        if (value != setup_values[setup] && !(setup == 2 && !value) &&
            !(offset == 0x1214 && value == 0xffff)) {
            goto unsupported;
        }
        s->setup[setup] = value;
        if (offset == 0x1214) {
            s->stream_setup |= 1;
        } else if (offset == 0x1298) {
            s->stream_setup |= 2;
        }
        nikon_transfer_irq(s);
        return MEMTX_OK;
    }
    if (index < 0) {
        goto unsupported;
    }
    if (index != TRANSFER_START) {
        if (s->running) {
            goto unsupported;
        }
        s->reg[index] = value;
        s->configured |= 1U << index;
        return MEMTX_OK;
    }
    if (!value) {
        timer_del(s->timer);
        s->running = s->completed = false;
        s->reg[index] = 0;
        nikon_transfer_irq(s);
        return MEMTX_OK;
    }
    if (value != 1 || s->running || s->completed ||
        (s->configured & required) != required) {
        goto unsupported;
    }
    if (s->reg[TRANSFER_CONTROL] == 8) {
        required = (1U << TRANSFER_EXTENT) |
                   (1U << TRANSFER_EXTENSION) |
                   (1U << TRANSFER_PADDING);
        if ((s->configured & required) != required ||
            s->reg[TRANSFER_EXTENSION] || s->reg[TRANSFER_PADDING]) {
            goto unsupported;
        }
        /* The original producer sends a second command for secondary data. */
        extent = s->reg[TRANSFER_EXTENT];
    } else {
        required = (1U << TRANSFER_SOURCE_GEOMETRY) |
                   (1U << TRANSFER_DESTINATION_GEOMETRY);
        selector = (s->reg[TRANSFER_CONTROL] >> 8) & 7;
        if ((s->configured & required) != required ||
            s->reg[TRANSFER_CONTROL] & ~0x00007700U ||
            selector >= ARRAY_SIZE(byte_factors) ||
            selector != ((s->reg[TRANSFER_CONTROL] >> 12) & 7) ||
            s->reg[TRANSFER_SOURCE_GEOMETRY] !=
            s->reg[TRANSFER_DESTINATION_GEOMETRY]) {
            goto unsupported;
        }
        if (s->reg[TRANSFER_SECONDARY_SOURCE] ||
            s->reg[TRANSFER_SECONDARY_DESTINATION]) {
            if (selector != 5 || !s->reg[TRANSFER_SECONDARY_SOURCE] ||
                !s->reg[TRANSFER_SECONDARY_DESTINATION]) {
                goto unsupported;
            }
            secondary = true;
        }
        extent = (uint64_t)(s->reg[TRANSFER_SOURCE_GEOMETRY] & 0xffff) *
                 (s->reg[TRANSFER_SOURCE_GEOMETRY] >> 16) *
                 byte_factors[selector];
    }
    if (!extent || extent > UINT32_MAX ||
        s->reg[TRANSFER_SOURCE] > UINT32_MAX - extent ||
        s->reg[TRANSFER_DESTINATION] > UINT32_MAX - extent) {
        goto unsupported;
    }
    if (secondary &&
        (s->reg[TRANSFER_SECONDARY_SOURCE] > UINT32_MAX - extent ||
         s->reg[TRANSFER_SECONDARY_DESTINATION] > UINT32_MAX - extent ||
         !nikon_transfer_range(s->reg[TRANSFER_SECONDARY_SOURCE], extent,
                               false) ||
         !nikon_transfer_range(s->reg[TRANSFER_SECONDARY_DESTINATION], extent,
                               true))) {
        return nikon_transfer_error(s, offset, size, true, value,
                                    "transfer-secondary-range-not-backed");
    }
    s->source = s->reg[TRANSFER_SOURCE];
    s->destination = s->reg[TRANSFER_DESTINATION];
    s->remaining = extent;
    if (!nikon_transfer_range(s->source, s->remaining, false) ||
        !nikon_transfer_range(s->destination, s->remaining, true)) {
        return nikon_transfer_error(s, offset, size, true, value,
                                    "transfer-memory-range-not-backed");
    }
    s->plane_remaining = extent;
    s->secondary_source = s->reg[TRANSFER_SECONDARY_SOURCE];
    s->secondary_destination = s->reg[TRANSFER_SECONDARY_DESTINATION];
    if (secondary) {
        s->remaining += extent;
    }
    s->reg[index] = value;
    s->running = true;
    nikon_transfer_schedule(s);
    return MEMTX_OK;
unsupported:
    return nikon_transfer_error(s, offset, size, true, value,
                                "transfer-packet-not-supported");
}

bool nikon_transfer_stream_request(DeviceState *dev, unsigned bank,
                                   NikonTransferStreamRequest *request)
{
    NikonTransferState *s = NIKON_TRANSFER(dev);
    NikonTransferStream *stream;

    if (bank >= ARRAY_SIZE(s->stream) || !request) {
        return false;
    }
    stream = &s->stream[bank];
    if (!stream->bytes) {
        return false;
    }
    request->token = stream->token;
    request->source = stream->source;
    request->units64 = stream->units64;
    request->bytes = g_bytes_ref(stream->bytes);
    return true;
}

static const MemoryRegionOps nikon_transfer_ops = {
    .read_with_attrs = nikon_transfer_read,
    .write_with_attrs = nikon_transfer_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl = { .min_access_size = 4, .max_access_size = 4 },
};

static bool nikon_transfer_stream0_pending(Object *obj, Error **errp)
{
    return NIKON_TRANSFER(obj)->stream[0].bytes != NULL;
}

static bool nikon_transfer_stream1_pending(Object *obj, Error **errp)
{
    return NIKON_TRANSFER(obj)->stream[1].bytes != NULL;
}

static char *nikon_transfer_stream0_hash(Object *obj, Error **errp)
{
    GBytes *bytes = NIKON_TRANSFER(obj)->stream[0].bytes;

    return bytes ? g_compute_checksum_for_bytes(G_CHECKSUM_SHA256, bytes) :
                   g_strdup("");
}

static char *nikon_transfer_stream1_hash(Object *obj, Error **errp)
{
    GBytes *bytes = NIKON_TRANSFER(obj)->stream[1].bytes;

    return bytes ? g_compute_checksum_for_bytes(G_CHECKSUM_SHA256, bytes) :
                   g_strdup("");
}

static void nikon_transfer_reset(DeviceState *dev)
{
    NikonTransferState *s = NIKON_TRANSFER(dev);

    timer_del(s->timer);
    for (unsigned i = 0; i < ARRAY_SIZE(s->stream); i++) {
        g_clear_pointer(&s->stream[i].bytes, g_bytes_unref);
        memset(&s->stream[i], 0, sizeof(s->stream[i]));
        qemu_set_irq(s->stream_request[i], 0);
    }
    s->stream_setup = 0;
    memset(s->reg, 0, sizeof(s->reg));
    memset(s->setup, 0, sizeof(s->setup));
    s->configured = 0;
    s->source = s->destination = s->remaining = 0;
    s->plane_remaining = s->secondary_source = s->secondary_destination = 0;
    s->running = s->completed = s->reported_error = false;
    clock_update_hz(s->clock, 48000000);
    nikon_transfer_irq(s);
}

static void nikon_transfer_init(Object *obj)
{
    NikonTransferState *s = NIKON_TRANSFER(obj);
    unsigned i;

    qdev_init_gpio_out_named(DEVICE(obj), s->stream_request,
                            "stream-request", ARRAY_SIZE(s->stream));
    object_property_add_bool(obj, "stream0-pending",
                             nikon_transfer_stream0_pending, NULL);
    object_property_add_bool(obj, "stream1-pending",
                             nikon_transfer_stream1_pending, NULL);
    object_property_add_str(obj, "stream0-sha256", nikon_transfer_stream0_hash,
                            NULL);
    object_property_add_str(obj, "stream1-sha256", nikon_transfer_stream1_hash,
                            NULL);
    object_property_add_uint64_ptr(obj, "stream0-token", &s->stream[0].token,
                                  OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(obj, "stream1-token", &s->stream[1].token,
                                  OBJ_PROP_FLAG_READ);
    s->clock = qdev_init_clock_out(DEVICE(obj), "transfer-clock");
    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, nikon_transfer_step, s);
    memory_region_init_io(&s->regs, obj, &nikon_transfer_ops, s,
                          "nikon.transfer", 0x2000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
    for (i = 0; i < 3; i++) {
        sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq[i]);
    }
}

static void nikon_transfer_finalize(Object *obj)
{
    NikonTransferState *s = NIKON_TRANSFER(obj);

    for (unsigned i = 0; i < ARRAY_SIZE(s->stream); i++) {
        g_clear_pointer(&s->stream[i].bytes, g_bytes_unref);
    }
    timer_free(s->timer);
}

static const VMStateDescription nikon_transfer_vmstate = {
    .name = "nikon-transfer", .unmigratable = true,
};

static void nikon_transfer_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &nikon_transfer_vmstate;
    dc->user_creatable = false;
    dc->unrealize = nikon_transfer_reset;
    device_class_set_legacy_reset(dc, nikon_transfer_reset);
}

static const TypeInfo nikon_transfer_type = {
    .name = TYPE_NIKON_TRANSFER,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonTransferState),
    .instance_init = nikon_transfer_init,
    .instance_finalize = nikon_transfer_finalize,
    .class_init = nikon_transfer_class_init,
};

static void nikon_transfer_register_types(void)
{
    type_register_static(&nikon_transfer_type);
}
type_init(nikon_transfer_register_types)
