/*
 * Nikon packet transfer. C1.70 106ee148/106ee3d6 provide byte-copy register
 * production and IRQ130 ACK; 106ee04a submits the paired graphics streams.
 * The finite LUT and rectangle-clear profiles execute before raising IRQ129.
 * Zero reset and a 48MHz transfer clock are functional-board assumptions.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "qemu/timer.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-clock.h"
#include "migration/vmstate.h" /* IWYU pragma: keep */
#include "system/address-spaces.h"
#include "system/dma.h"
#include "hw/nikon/transfer.h"

#define TRANSFER_LUT_INPUT_WIDTH 128U
#define TRANSFER_LUT_INPUT_HEIGHT 128U
#define TRANSFER_LUT_INPUT_STRIDE 128U
#define TRANSFER_LUT_OUTPUT_WIDTH 32U
#define TRANSFER_LUT_OUTPUT_HEIGHT 32U
#define TRANSFER_LUT_OUTPUT_BYTES \
    (TRANSFER_LUT_OUTPUT_WIDTH * TRANSFER_LUT_OUTPUT_HEIGHT)
#define TRANSFER_LUT_PRIMARY_ENTRIES 256U
#define TRANSFER_LUT_SECONDARY_ENTRIES 32U
#define TRANSFER_CLEAR_WIDTH 1024U
#define TRANSFER_CLEAR_HEIGHT 682U
#define TRANSFER_CLEAR_PLANE_BYTES \
    (TRANSFER_CLEAR_WIDTH * TRANSFER_CLEAR_HEIGHT)
#define TRANSFER_CLEAR_YUVA_PRIMARY_BYTES \
    (2 * TRANSFER_CLEAR_PLANE_BYTES)
#define TRANSFER_RGBA_WIDTH 1024U
#define TRANSFER_RGBA_HEIGHT 688U
#define TRANSFER_RGBA_OUTPUT_BYTES \
    (4 * TRANSFER_RGBA_WIDTH * TRANSFER_RGBA_HEIGHT)
#define TRANSFER_RGBA_TARGET 0xbf309600U
#define TRANSFER_RGBA_PATH_SEGMENT_BYTES 5U
#define TRANSFER_RGBA_PATH_COORDINATE_BYTES 20U

typedef struct NikonTransferLutProgram {
    uint32_t source;
    uint32_t destination;
    uint16_t primary[TRANSFER_LUT_PRIMARY_ENTRIES];
    uint16_t secondary[TRANSFER_LUT_SECONDARY_ENTRIES];
} NikonTransferLutProgram;

typedef enum NikonTransferProgramKind {
    TRANSFER_PROGRAM_LUT,
    TRANSFER_PROGRAM_YUV_CLEAR,
    TRANSFER_PROGRAM_RGBA_CLEAR,
} NikonTransferProgramKind;

typedef struct NikonTransferProgram {
    NikonTransferProgramKind kind;
    NikonTransferLutProgram lut;
    uint32_t plane[2];
    uint32_t plane_bytes[2];
    uint32_t format;
} NikonTransferProgram;

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
    QEMUTimer *stream_timer;
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
    bool stream_running, stream_ready, stream_irq_pending;
};

static void nikon_transfer_irq(NikonTransferState *s)
{
    /* IRQ129 is the bounded dual-stream completion inference below. */
    qemu_set_irq(s->irq[0], s->stream_irq_pending);
    /* Bit15 is the source-installed transfer completion enable convention. */
    qemu_set_irq(s->irq[1], s->completed && (s->setup[2] & 0x8000));
    /* The IRQ131 producer remains unknown. */
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
    if (address > UINT32_MAX || length > UINT32_MAX - address + 1ULL) {
        return false;
    }
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

static void nikon_transfer_stream_clear(NikonTransferState *s)
{
    s->stream_running = false;
    for (unsigned i = 0; i < ARRAY_SIZE(s->stream); i++) {
        g_clear_pointer(&s->stream[i].bytes, g_bytes_unref);
        memset(&s->stream[i], 0, sizeof(s->stream[i]));
        qemu_set_irq(s->stream_request[i], 0);
    }
}

static bool nikon_transfer_stream_word(const uint32_t *words, size_t count,
                                       unsigned index, uint32_t value)
{
    return index < count && le32_to_cpu(words[index]) == value;
}

static bool nikon_transfer_lut_parse(NikonTransferState *s,
                                        NikonTransferLutProgram *program,
                                        const char **reason)
{
    const uint32_t *setup, *draw;
    size_t setup_bytes, draw_bytes, setup_words, draw_words;
    uint32_t source, destination;

    setup = g_bytes_get_data(s->stream[0].bytes, &setup_bytes);
    draw = g_bytes_get_data(s->stream[1].bytes, &draw_bytes);
    if (setup_bytes != NIKON_TRANSFER_LUT_SETUP_BYTES ||
        draw_bytes != NIKON_TRANSFER_LUT_DRAW_BYTES) {
        *reason = "transfer-stream-truncated";
        return false;
    }
    setup_words = setup_bytes / sizeof(*setup);
    draw_words = draw_bytes / sizeof(*draw);

    /*
     * These command headers are the finite C1.70 LUT draw fingerprint.  The
     * tables and descriptor operands remain data; accepting a different
     * packet family would require another explicit profile.
     */
    if (!nikon_transfer_stream_word(setup, setup_words, 0, 0x00691004) ||
        !nikon_transfer_stream_word(setup, setup_words, 22, 0x00653004) ||
        !nikon_transfer_stream_word(setup, setup_words, 36, 0x0080f026) ||
        !nikon_transfer_stream_word(setup, setup_words, 42, 0x0006f004) ||
        !nikon_transfer_stream_word(draw, draw_words, 0, 0x01408004) ||
        !nikon_transfer_stream_word(draw, draw_words, 10, 0x01a3f004) ||
        !nikon_transfer_stream_word(draw, draw_words, 87, 0x0188f7fc) ||
        !nikon_transfer_stream_word(draw, draw_words, 344, 0x0192f02e) ||
        !nikon_transfer_stream_word(draw, draw_words, 355, 0x0188f0fc) ||
        !nikon_transfer_stream_word(draw, draw_words, 396, 0x01a4f004) ||
        !nikon_transfer_stream_word(draw, draw_words, 408, 0x00a0f02e) ||
        !nikon_transfer_stream_word(draw, draw_words, 415, 0x00a6f004)) {
        *reason = "transfer-stream-program-not-supported";
        return false;
    }

    source = le32_to_cpu(setup[37]);
    destination = le32_to_cpu(draw[397]);
    if (source != le32_to_cpu(draw[409]) ||
        le32_to_cpu(setup[39]) != TRANSFER_LUT_INPUT_STRIDE / 8 ||
        le32_to_cpu(setup[40]) !=
            (TRANSFER_LUT_INPUT_HEIGHT << 16 | TRANSFER_LUT_INPUT_WIDTH) ||
        le32_to_cpu(draw[11]) !=
            (TRANSFER_LUT_OUTPUT_HEIGHT << 16 | TRANSFER_LUT_OUTPUT_WIDTH)) {
        *reason = "transfer-stream-geometry-not-supported";
        return false;
    }
    if (le32_to_cpu(draw[403]) != 0x01000000 ||
        le32_to_cpu(draw[414]) != 2) {
        *reason = "transfer-stream-format-not-supported";
        return false;
    }
    for (unsigned i = 0; i < TRANSFER_LUT_PRIMARY_ENTRIES; i++) {
        uint32_t entry = le32_to_cpu(draw[88 + i]);

        if (entry > 0xfff) {
            *reason = "transfer-stream-lut-not-supported";
            return false;
        }
        program->primary[i] = entry;
    }
    for (unsigned i = 0; i < TRANSFER_LUT_SECONDARY_ENTRIES; i++) {
        uint32_t entry = le32_to_cpu(draw[356 + i]);

        if (entry > 0xfff) {
            *reason = "transfer-stream-lut-not-supported";
            return false;
        }
        program->secondary[i] = entry;
    }
    if (source > UINT32_MAX -
                 TRANSFER_LUT_INPUT_STRIDE * TRANSFER_LUT_INPUT_HEIGHT ||
        destination > UINT32_MAX - TRANSFER_LUT_OUTPUT_BYTES) {
        *reason = "transfer-stream-range-overflow";
        return false;
    }
    if (source < destination + TRANSFER_LUT_OUTPUT_BYTES &&
        destination < source +
                      TRANSFER_LUT_INPUT_STRIDE * TRANSFER_LUT_INPUT_HEIGHT) {
        *reason = "transfer-stream-operands-overlap";
        return false;
    }
    if (nikon_transfer_span(source,
            TRANSFER_LUT_INPUT_STRIDE * TRANSFER_LUT_INPUT_HEIGHT, false) <
            TRANSFER_LUT_INPUT_STRIDE * TRANSFER_LUT_INPUT_HEIGHT ||
        nikon_transfer_span(destination, TRANSFER_LUT_OUTPUT_BYTES, true) <
            TRANSFER_LUT_OUTPUT_BYTES) {
        *reason = "transfer-stream-memory-range-not-backed";
        return false;
    }
    program->source = source;
    program->destination = destination;
    return true;
}

typedef struct NikonTransferField {
    unsigned word;
    uint32_t value;
} NikonTransferField;

/*
 * Protocol fields, not a firmware-byte fixture. All unspecified words must
 * be zero; only the path key, target addresses and LUT entries are operands.
 * Headers encode op/count/register/byte mask as corroborated by the Z7
 * implementation. This is a finite fingerprint, not a generic command VM.
 */
static const NikonTransferField clear_setup_fields[] = {
    { 0, 0x00691004 }, { 1, 0x80 },
    { 2, 0x0041f046 }, { 3, 0x3f800000 }, { 7, 0x3f800000 },
    { 11, 0x3f800000 }, { 12, 0x0041f046 }, { 13, 0x3f800000 },
    { 17, 0x3f800000 }, { 21, 0x3f800000 },
    { 22, 0x00653004 }, { 23, 0x100 }, /* fill */
    { 24, 0x0040f004 }, { 25, 0x02b00400 },
    { 26, 0x0003f004 }, { 27, 5 },
    { 28, 0x0005f004 }, { 29, 0x80000005 },
    { 30, 0x0063f004 }, { 31, 5 },
    { 32, 0x005ef004 }, { 33, 0x3f800000 }, { 34, 0x005ff004 },
    { 36, 0x0080f026 }, { 39, 16 }, { 40, 0x02b00400 },
    { 42, 0x0006f004 }, { 43, 1 },
    { 44, 0xa }, { 45, 0x07090702 }, /* five path segments */
    { 47, 0x00010022 }, /* five signed 32-bit coordinates */
    { 49, 6 }, { 50, 1024 }, { 51, 682 }, { 52, 0xfffffc00 },
    { 53, 0x0085f004 }, { 54, 0x00010001 },
};

static const NikonTransferField clear_draw_fields[] = {
    { 0, 0x01408004 }, { 2, 0x01901004 },
    { 4, 0x014ee004 }, { 5, 0x001b0000 },
    { 6, 0x0156e004 }, { 7, 0x001b0000 },
    { 8, 0x01a2f004 }, { 9, 0x105 },
    { 10, 0x01a3f004 }, { 11, 0x02b00400 },
    { 12, 0x01844004 }, { 14, 0x01a7f004 }, { 15, 0x101 },
    { 16, 0x011cf004 }, { 17, 0x02b00000 }, { 18, 0x01a08004 },
    { 20, 0x00c0f00e }, { 21, 0x03ff0000 }, { 22, 0x02af0000 },
    { 23, 0x01001004 }, { 24, 1 }, { 25, 0x01151004 }, { 26, 1 },
    { 27, 0x01b01004 }, { 28, 0xd }, { 29, 0x01b31004 }, { 30, 1 },
    /* The observed conversion register block is fixed for this profile. */
    { 31, 0x01c1f046 }, { 32, 0x007a8000 }, { 33, 0x001671c6 },
    { 34, 0x749 }, { 35, 0x100 }, { 36, 0x006ad259 },
    { 37, 0x00075653 }, { 38, 0x007ad200 }, { 39, 0x00753132 },
    { 40, 0x200 },
    { 41, 0x01848004 }, { 43, 0x01a11004 }, { 45, 0x0115c004 },
    { 47, 0x01401004 }, { 49, 0x014df004 }, { 51, 0x014df004 },
    { 53, 0x01401004 }, { 55, 0x014df004 }, { 57, 0x014df004 },
    { 59, 0x01402004 }, { 61, 0x014df004 }, { 63, 0x014df004 },
    { 65, 0x01841004 }, { 66, 8 }, /* clear */
    { 67, 0x01868004 }, { 68, 0x07000000 }, { 69, 0x0187f004 },
    { 71, 0x0188f7fc }, /* 256 entries, repeated register188 */
    { 328, 0x0192f02e }, { 329, 0x400 }, { 331, 0x400 }, { 333, 0x400 },
    { 335, 0x01868004 }, { 336, 0x38000000 }, { 337, 0x0187f004 },
    { 339, 0x0188f0fc }, /* 32 entries */
    { 372, 0x01863004 }, { 373, 0x10f }, { 374, 0x01842004 },
    { 376, 0x01a02004 }, { 378, 0x01a29004 }, { 379, 0x105 },
    { 380, 0x01a4f004 }, { 382, 0x01b5f004 },
    { 384, 0x01a74004 }, { 386, 0x0185f004 }, /* color zero */
    { 388, 0x01841004 }, { 389, 8 }, { 390, 0x01848004 },
    { 392, 0x01a04004 }, { 394, 0x01a51004 }, { 396, 0x01aa1004 },
    { 398, 0x00a0f02e }, { 401, 16 }, { 402, 0x02b00400 },
    { 405, 0x00a6f004 }, { 406, 0x00010001 },
    { 407, 0x01a61004 }, { 408, 1 }, /* draw trigger */
    { 409, 0x00081004 }, { 410, 1 },
};

/*
 * The original sixth pair contains two indirect filled paths. Dynamic words
 * 22/26 select their shared coordinate/segment arrays; words34/48 are opaque
 * per-path operands repeated at draw words80/91. Everything else is fixed.
 */
static const NikonTransferField rgba_setup_fields[] = {
    { 0, 0x00691004 }, { 1, 0x80 },
    { 2, 0x005ef00e }, { 3, 0x3f800000 },
    { 5, 0x0063f004 }, { 6, 5 },
    { 7, 0x0040f004 }, { 8, 0x0ac01000 },
    { 9, 0x0040f004 }, { 10, 0x0ac01000 },
    { 11, 0x0041f046 }, { 12, 0x3f800000 },
    { 16, 0x3f800000 }, { 20, 0x3f800000 },
    { 21, 0x0004f004 },
    { 23, 0x0005f004 }, { 24, 0xd0000005 },
    { 25, 0x0002f004 },
    { 27, 0x0003f004 }, { 28, 0x10000005 },
    { 29, 0x00653004 }, { 30, 0x00002100 },
    { 31, 0x0066f004 }, { 32, 0x00006004 },
    { 33, 0x0080f026 }, { 36, 0x200 }, { 37, 0x08701000 },
    { 39, 0x0006f004 }, { 40, 1 },
    { 41, 0x0085f004 }, { 42, 0x00010001 },
    { 43, 0x0005f004 }, { 44, 0xd0000005 },
    { 45, 0x0003f004 }, { 46, 0x10000005 },
    { 47, 0x0080f026 }, { 50, 0x200 }, { 51, 0x02501000 },
    { 52, 0x0001f790 }, { 53, 0x0006f004 }, { 54, 1 },
    { 55, 0x0085f004 }, { 56, 0x00010002 },
};

static const NikonTransferField rgba_draw_fields[] = {
    { 0, 0x01408004 }, { 2, 0x01901004 },
    { 4, 0x014ee004 }, { 5, 0x001b0000 },
    { 6, 0x0156e004 }, { 7, 0x001b0000 },
    { 8, 0x01a2f004 }, /* format0 RGBA8888 */
    { 10, 0x01a3f004 }, { 11, 0x02b00400 },
    { 12, 0x01844004 }, { 14, 0x01a7f004 }, { 15, 0x01000101 },
    { 16, 0x011cf004 }, { 17, 0x02b00000 },
    { 18, 0x01a08004 }, { 20, 0x01aa1004 },
    { 22, 0x01a04004 }, { 23, 0x00010000 },
    { 24, 0x01841004 }, { 26, 0x01842004 },
    { 28, 0x01a02004 }, { 30, 0x01a29004 },
    { 32, 0x01a4f004 }, { 33, TRANSFER_RGBA_TARGET },
    { 34, 0x01a51004 }, { 35, 1 }, { 36, 0x01a74004 },
    { 38, 0x01401004 }, { 40, 0x014df004 }, { 42, 0x014df004 },
    { 44, 0x00c0f00e }, { 45, 0x03ff0000 }, { 46, 0x02af0000 },
    { 47, 0x01001004 }, { 48, 1 },
    { 49, 0x01151004 }, { 50, 1 },
    { 51, 0x01b01004 }, { 52, 0xd },
    { 53, 0x01b31004 }, { 54, 1 },
    { 55, 0x01848004 }, { 57, 0x01a11004 },
    { 59, 0x01841004 }, { 60, 8 }, /* clear */
    { 61, 0x01401004 }, { 63, 0x014df004 }, { 65, 0x014df004 },
    { 67, 0x01402004 }, { 69, 0x014df004 }, { 71, 0x014df004 },
    { 73, 0x0115c004 },
    { 75, 0x0185f004 }, { 76, 0xff000000 },
    { 77, 0x01863004 }, { 78, 0x108 },
    { 79, 0x00a0f02e }, { 82, 0x200 }, { 83, 0x08701000 },
    { 85, 0x00040002 }, { 86, 0x00a6f004 }, { 87, 0x00010001 },
    { 88, 0x01a61004 }, { 89, 1 },
    { 90, 0x00a0f02e }, { 93, 0x200 }, { 94, 0x02501000 },
    { 95, 0x21c }, { 96, 0x00040002 },
    { 97, 0x00a6f004 }, { 98, 0x00010002 },
    { 99, 0x01a61004 }, { 100, 1 },
    { 101, 0x00081004 }, { 102, 1 },
};

static bool nikon_transfer_clear_fingerprint(const uint32_t *words,
                                            unsigned count,
                                            const NikonTransferField *fields,
                                            unsigned field_count, bool draw)
{
    unsigned field = 0;

    for (unsigned i = 0; i < count; i++) {
        uint32_t expected = 0, value = le32_to_cpu(words[i]);

        if (field < field_count && fields[field].word == i) {
            expected = fields[field++].value;
        }
        if ((!draw && i == 37) ||
            (draw && (i == 9 || i == 379 || i == 381 || i == 383 ||
                      i == 399))) {
            continue;
        }
        if (draw && ((i >= 72 && i < 328) || (i >= 340 && i < 372))) {
            if (value > 0xfff) {
                return false;
            }
        } else if (value != expected) {
            return false;
        }
    }
    return field == field_count;
}

static bool nikon_transfer_rgba_fingerprint(const uint32_t *words,
                                            unsigned count,
                                            const NikonTransferField *fields,
                                            unsigned field_count, bool draw)
{
    unsigned field = 0;

    for (unsigned i = 0; i < count; i++) {
        uint32_t expected = 0, value = le32_to_cpu(words[i]);

        if (field < field_count && fields[field].word == i) {
            expected = fields[field++].value;
        }
        if ((!draw && (i == 22 || i == 26 || i == 34 || i == 48)) ||
            (draw && (i == 80 || i == 91))) {
            continue;
        }
        if (value != expected) {
            return false;
        }
    }
    return field == field_count;
}

static bool nikon_transfer_overlap(uint64_t first, uint64_t first_bytes,
                                   uint64_t second, uint64_t second_bytes)
{
    return first < second + second_bytes && second < first + first_bytes;
}

static bool nikon_transfer_rgba_parse(NikonTransferState *s,
                                      NikonTransferProgram *program,
                                      const char **reason)
{
    static const uint8_t expected_segments[TRANSFER_RGBA_PATH_SEGMENT_BYTES] = {
        0x02, 0x07, 0x09, 0x07, 0x00,
    };
    static const uint32_t expected_coordinates[] = {
        0x00000000, 0x40c00000, 0x44800000, 0x442a8000, 0xc4800000,
    };
    const uint32_t *setup, *draw;
    uint8_t segments[TRANSFER_RGBA_PATH_SEGMENT_BYTES];
    uint32_t coordinates[ARRAY_SIZE(expected_coordinates)];
    size_t setup_bytes, draw_bytes;
    uint64_t input[4], input_bytes[4];

    setup = g_bytes_get_data(s->stream[0].bytes, &setup_bytes);
    draw = g_bytes_get_data(s->stream[1].bytes, &draw_bytes);
    if (setup_bytes != NIKON_TRANSFER_RGBA_SETUP_BYTES ||
        draw_bytes != NIKON_TRANSFER_RGBA_DRAW_BYTES) {
        *reason = "transfer-stream-truncated";
        return false;
    }
    if (le32_to_cpu(draw[9]) != 0) {
        *reason = "transfer-stream-format-not-supported";
        return false;
    }
    if (le32_to_cpu(draw[11]) !=
            (TRANSFER_RGBA_HEIGHT << 16 | TRANSFER_RGBA_WIDTH)) {
        *reason = "transfer-stream-geometry-not-supported";
        return false;
    }
    if (!nikon_transfer_rgba_fingerprint(setup, setup_bytes / 4,
            rgba_setup_fields, ARRAY_SIZE(rgba_setup_fields), false) ||
        !nikon_transfer_rgba_fingerprint(draw, draw_bytes / 4,
            rgba_draw_fields, ARRAY_SIZE(rgba_draw_fields), true) ||
        le32_to_cpu(draw[80]) != le32_to_cpu(setup[34]) ||
        le32_to_cpu(draw[91]) != le32_to_cpu(setup[48]) ||
        !le32_to_cpu(setup[34]) || !le32_to_cpu(setup[48]) ||
        le32_to_cpu(setup[34]) == le32_to_cpu(setup[48])) {
        *reason = "transfer-stream-program-not-supported";
        return false;
    }

    input[0] = s->stream[0].source;
    input_bytes[0] = setup_bytes;
    input[1] = s->stream[1].source;
    input_bytes[1] = draw_bytes;
    input[2] = le32_to_cpu(setup[26]);
    input_bytes[2] = sizeof(segments);
    input[3] = le32_to_cpu(setup[22]);
    input_bytes[3] = sizeof(coordinates);
    for (unsigned i = 0; i < ARRAY_SIZE(input); i++) {
        if (input[i] > UINT32_MAX - input_bytes[i] + 1ULL) {
            *reason = "transfer-stream-range-overflow";
            return false;
        }
        if (nikon_transfer_overlap(TRANSFER_RGBA_TARGET,
                TRANSFER_RGBA_OUTPUT_BYTES, input[i], input_bytes[i])) {
            *reason = "transfer-stream-operands-overlap";
            return false;
        }
        for (unsigned j = 0; j < i; j++) {
            if (nikon_transfer_overlap(input[i], input_bytes[i],
                                       input[j], input_bytes[j])) {
                *reason = "transfer-stream-operands-overlap";
                return false;
            }
        }
    }
    if (nikon_transfer_span(TRANSFER_RGBA_TARGET,
            TRANSFER_RGBA_OUTPUT_BYTES, true) < TRANSFER_RGBA_OUTPUT_BYTES ||
        !nikon_transfer_range(input[2], input_bytes[2], false) ||
        !nikon_transfer_range(input[3], input_bytes[3], false)) {
        *reason = "transfer-stream-memory-range-not-backed";
        return false;
    }
    if (dma_memory_read(&address_space_memory, input[2], segments,
            sizeof(segments), MEMTXATTRS_UNSPECIFIED) != MEMTX_OK ||
        dma_memory_read(&address_space_memory, input[3], coordinates,
            sizeof(coordinates), MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
        *reason = "transfer-stream-path-read-failed";
        return false;
    }
    if (memcmp(segments, expected_segments, sizeof(segments))) {
        *reason = "transfer-stream-path-not-supported";
        return false;
    }
    for (unsigned i = 0; i < ARRAY_SIZE(coordinates); i++) {
        if (le32_to_cpu(coordinates[i]) != expected_coordinates[i]) {
            *reason = "transfer-stream-path-not-supported";
            return false;
        }
    }
    program->kind = TRANSFER_PROGRAM_RGBA_CLEAR;
    program->plane[0] = TRANSFER_RGBA_TARGET;
    program->plane_bytes[0] = TRANSFER_RGBA_OUTPUT_BYTES;
    program->format = 0;
    return true;
}

static bool nikon_transfer_clear_parse(NikonTransferState *s,
                                       NikonTransferProgram *program,
                                       const char **reason)
{
    const uint32_t *setup, *draw;
    size_t setup_bytes, draw_bytes;
    uint64_t first, second;
    uint32_t format;

    setup = g_bytes_get_data(s->stream[0].bytes, &setup_bytes);
    draw = g_bytes_get_data(s->stream[1].bytes, &draw_bytes);
    if (setup_bytes != NIKON_TRANSFER_CLEAR_SETUP_BYTES ||
        draw_bytes != NIKON_TRANSFER_CLEAR_DRAW_BYTES) {
        *reason = "transfer-stream-truncated";
        return false;
    }
    if (le32_to_cpu(setup[25]) != 0x02b00400 ||
        le32_to_cpu(setup[39]) != 16 ||
        le32_to_cpu(setup[40]) != 0x02b00400 ||
        le32_to_cpu(setup[48]) != 0 || le32_to_cpu(setup[49]) != 6 ||
        le32_to_cpu(setup[50]) != 1024 || le32_to_cpu(setup[51]) != 682 ||
        le32_to_cpu(setup[52]) != 0xfffffc00 ||
        le32_to_cpu(draw[11]) != 0x02b00400 ||
        le32_to_cpu(draw[21]) != 0x03ff0000 ||
        le32_to_cpu(draw[22]) != 0x02af0000 ||
        le32_to_cpu(draw[401]) != 16 ||
        le32_to_cpu(draw[402]) != 0x02b00400) {
        *reason = "transfer-stream-geometry-not-supported";
        return false;
    }
    format = le32_to_cpu(draw[9]);
    if ((format != 0x105 && format != 0x107) ||
        le32_to_cpu(draw[379]) != format) {
        *reason = "transfer-stream-format-not-supported";
        return false;
    }
    if (le32_to_cpu(setup[37]) != le32_to_cpu(draw[399]) ||
        !nikon_transfer_clear_fingerprint(setup, setup_bytes / 4,
            clear_setup_fields, ARRAY_SIZE(clear_setup_fields), false) ||
        !nikon_transfer_clear_fingerprint(draw, draw_bytes / 4,
            clear_draw_fields, ARRAY_SIZE(clear_draw_fields), true)) {
        *reason = "transfer-stream-program-not-supported";
        return false;
    }
    first = le32_to_cpu(draw[381]);
    second = le32_to_cpu(draw[383]);
    program->plane_bytes[0] = format == 0x107 ?
        TRANSFER_CLEAR_YUVA_PRIMARY_BYTES : TRANSFER_CLEAR_PLANE_BYTES;
    program->plane_bytes[1] = TRANSFER_CLEAR_PLANE_BYTES;
    if (first + program->plane_bytes[0] > UINT32_MAX + 1ULL ||
        second + program->plane_bytes[1] > UINT32_MAX + 1ULL) {
        *reason = "transfer-stream-range-overflow";
        return false;
    }
    if (first < second + program->plane_bytes[1] &&
        second < first + program->plane_bytes[0]) {
        *reason = "transfer-stream-operands-overlap";
        return false;
    }
    if (second != first + program->plane_bytes[0]) {
        *reason = "transfer-stream-geometry-not-supported";
        return false;
    }
    for (unsigned i = 0; i < 2; i++) {
        uint64_t destination = i ? second : first;

        for (unsigned j = 0; j < 2; j++) {
            uint64_t source = s->stream[j].source;
            uint64_t bytes = 8ULL * s->stream[j].units64;

            if (destination < source + bytes &&
                source < destination + program->plane_bytes[i]) {
                *reason = "transfer-stream-operands-overlap";
                return false;
            }
        }
        if (nikon_transfer_span(destination, program->plane_bytes[i], true) <
            program->plane_bytes[i]) {
            *reason = "transfer-stream-memory-range-not-backed";
            return false;
        }
        program->plane[i] = destination;
    }
    program->format = format;
    program->kind = TRANSFER_PROGRAM_YUV_CLEAR;
    return true;
}

static bool nikon_transfer_stream_parse(NikonTransferState *s,
                                        NikonTransferProgram *program,
                                        const char **reason)
{
    program->kind = TRANSFER_PROGRAM_LUT;
    if (g_bytes_get_size(s->stream[1].bytes) ==
        NIKON_TRANSFER_RGBA_DRAW_BYTES) {
        return nikon_transfer_rgba_parse(s, program, reason);
    }
    if (g_bytes_get_size(s->stream[1].bytes) ==
        NIKON_TRANSFER_CLEAR_DRAW_BYTES) {
        return nikon_transfer_clear_parse(s, program, reason);
    }
    return nikon_transfer_lut_parse(s, &program->lut, reason);
}

static bool nikon_transfer_rgba_execute(NikonTransferProgram *program,
                                        const char **reason)
{
    hwaddr mapped = program->plane_bytes[0];
    MemTxAttrs attrs = MEMTXATTRS_UNSPECIFIED;
    uint32_t *destination;

    attrs.memory = true;
    destination = address_space_map(&address_space_memory, program->plane[0],
                                    &mapped, true, attrs);
    if (!destination || mapped != program->plane_bytes[0]) {
        if (destination) {
            address_space_unmap(&address_space_memory, destination, mapped,
                                true, 0);
        }
        *reason = "transfer-stream-output-map-failed";
        return false;
    }
    for (unsigned i = 0; i < program->plane_bytes[0] / sizeof(*destination);
         i++) {
        stl_le_p(&destination[i], 0xff000000);
    }
    address_space_unmap(&address_space_memory, destination, mapped, true,
                        mapped);
    return true;
}

static bool nikon_transfer_clear_execute(NikonTransferProgram *program,
                                         const char **reason)
{
    void *plane[2] = { NULL, NULL };
    hwaddr mapped[2] = {
        program->plane_bytes[0], program->plane_bytes[1],
    };
    uintptr_t first, second;
    bool success = false;
    MemTxAttrs attrs = MEMTXATTRS_UNSPECIFIED;

    attrs.memory = true;
    /* Map both complete writable planes before modifying either one. */
    for (unsigned i = 0; i < 2; i++) {
        plane[i] = address_space_map(&address_space_memory, program->plane[i],
                                     &mapped[i], true, attrs);
        if (!plane[i] || mapped[i] != program->plane_bytes[i]) {
            *reason = "transfer-stream-output-map-failed";
            goto out;
        }
    }
    first = (uintptr_t)plane[0];
    second = (uintptr_t)plane[1];
    if (first < second + program->plane_bytes[1] &&
        second < first + program->plane_bytes[0]) {
        *reason = "transfer-stream-operands-overlap";
        goto out;
    }
    /* Functional limited-range BT.601 black, with transparent 107 alpha. */
    if (program->format == 0x105) {
        memset(plane[0], 16, program->plane_bytes[0]);
        memset(plane[1], 128, program->plane_bytes[1]);
    } else {
        uint8_t *packed = plane[0];

        for (uint32_t i = 0; i < program->plane_bytes[0]; i += 4) {
            packed[i] = packed[i + 1] = 16;
            packed[i + 2] = packed[i + 3] = 128;
        }
        memset(plane[1], 0, program->plane_bytes[1]);
    }
    success = true;
out:
    for (unsigned i = 0; i < 2; i++) {
        if (plane[i]) {
            address_space_unmap(&address_space_memory, plane[i], mapped[i],
                                true, success ? mapped[i] : 0);
        }
    }
    return success;
}

static bool nikon_transfer_stream_execute(NikonTransferState *s,
                                          const char **reason)
{
    NikonTransferProgram parsed;
    NikonTransferLutProgram program;
    g_autofree uint8_t *input = NULL;
    uint8_t output[TRANSFER_LUT_OUTPUT_BYTES];
    MemTxAttrs attrs = MEMTXATTRS_UNSPECIFIED;
    MemTxResult result;

    if (!nikon_transfer_stream_parse(s, &parsed, reason)) {
        return false;
    }
    if (parsed.kind == TRANSFER_PROGRAM_YUV_CLEAR) {
        return nikon_transfer_clear_execute(&parsed, reason);
    }
    if (parsed.kind == TRANSFER_PROGRAM_RGBA_CLEAR) {
        return nikon_transfer_rgba_execute(&parsed, reason);
    }
    program = parsed.lut;
    input = g_malloc(TRANSFER_LUT_INPUT_STRIDE * TRANSFER_LUT_INPUT_HEIGHT);
    result = dma_memory_read(&address_space_memory, program.source, input,
                             TRANSFER_LUT_INPUT_STRIDE *
                             TRANSFER_LUT_INPUT_HEIGHT, attrs);
    if (result != MEMTX_OK) {
        *reason = "transfer-stream-input-read-failed";
        return false;
    }
    for (unsigned y = 0; y < TRANSFER_LUT_OUTPUT_HEIGHT; y++) {
        for (unsigned x = 0; x < TRANSFER_LUT_OUTPUT_WIDTH; x++) {
            uint8_t sample = input[(y * 4) * TRANSFER_LUT_INPUT_STRIDE + x * 4];
            unsigned primary = program.primary[sample];
            unsigned secondary = program.secondary[primary >> 7];

            output[y * TRANSFER_LUT_OUTPUT_WIDTH + x] =
                MIN((secondary + 8) >> 4, UINT8_MAX);
        }
    }
    {
        hwaddr mapped = sizeof(output);
        void *destination;

        attrs.memory = true;
        destination = address_space_map(&address_space_memory,
                                        program.destination, &mapped, true,
                                        attrs);
        if (!destination || mapped != sizeof(output)) {
            if (destination) {
                address_space_unmap(&address_space_memory, destination,
                                    mapped, true, 0);
            }
            *reason = "transfer-stream-output-map-failed";
            return false;
        }
        memcpy(destination, output, sizeof(output));
        address_space_unmap(&address_space_memory, destination, mapped, true,
                            mapped);
    }
    return true;
}

static void nikon_transfer_stream_complete(void *opaque)
{
    NikonTransferState *s = opaque;
    const char *reason = NULL;
    bool success;

    if (!s->stream_running) {
        return;
    }
    success = nikon_transfer_stream_execute(s, &reason);
    nikon_transfer_stream_clear(s);
    if (!success) {
        nikon_transfer_error(s, 0x10e0, 4, true, 1, reason);
        return;
    }
    s->stream_ready = true;
    s->stream_irq_pending = true;
    nikon_transfer_irq(s);
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

    if (size == 4 && offset == 0x58 && s->stream_ready) {
        /* Only the observed post-completion bit26-clear path is modeled. */
        *value = 0;
        return MEMTX_OK;
    }
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

    if (stream->bytes || s->stream_running || s->stream_irq_pending) {
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
    s->stream_ready = false;
    qemu_set_irq(s->stream_request[bank], 1);
    if (s->stream[0].bytes && s->stream[1].bytes) {
        NikonTransferProgram program;
        const char *reason = NULL;

        if (!nikon_transfer_stream_parse(s, &program, &reason)) {
            nikon_transfer_stream_clear(s);
            return nikon_transfer_error(s, offset, 4, true, value, reason);
        }
        s->stream_running = true;
        /* Defer memory execution beyond the second START. */
        timer_mod(s->stream_timer,
                  qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1);
    }
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
        } else if (offset == 0x1020) {
            /* Inferred from IRQ129 handler ordering; not a silicon W0C claim. */
            s->stream_irq_pending = false;
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
    timer_del(s->stream_timer);
    nikon_transfer_stream_clear(s);
    s->stream_setup = 0;
    memset(s->reg, 0, sizeof(s->reg));
    memset(s->setup, 0, sizeof(s->setup));
    s->configured = 0;
    s->source = s->destination = s->remaining = 0;
    s->plane_remaining = s->secondary_source = s->secondary_destination = 0;
    s->running = s->completed = s->reported_error = false;
    s->stream_running = s->stream_ready = s->stream_irq_pending = false;
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
    s->stream_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                   nikon_transfer_stream_complete, s);
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
    timer_free(s->stream_timer);
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
