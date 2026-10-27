/*
 * Nikon packet transfer. C1.70 106ee148/106ee3d6 provide byte-copy register
 * production and IRQ130 ACK; 106ee04a submits the paired graphics streams.
 * The finite LUT and rectangle-clear profiles execute before raising IRQ129.
 * Zero reset and a 48MHz transfer clock are functional-board assumptions.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "qemu/log.h"
#include "qemu/timer.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-clock.h"
#include "migration/vmstate.h" /* IWYU pragma: keep */
#include "system/address-spaces.h"
#include "system/dma.h"
#include "hw/nikon/transfer.h"
#include "hw/nikon/path.h"

#define TRANSFER_LUT_INPUT_WIDTH 128U
#define TRANSFER_LUT_INPUT_HEIGHT 128U
#define TRANSFER_LUT_INPUT_STRIDE 128U
#define TRANSFER_LUT_OUTPUT_WIDTH 32U
#define TRANSFER_LUT_OUTPUT_HEIGHT 32U

/* sub_106ee1f8 case 8/10 writes bit24 to 1214 while both streams are pending:
 * a caller fence request, not a configuration change. */
#define TRANSFER_STREAM_KICK 0x01000000U
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
#define TRANSFER_RGBA_TARGET_BYTES \
    (4 * TRANSFER_RGBA_WIDTH * TRANSFER_RGBA_HEIGHT)
#define TRANSFER_RGBA_ACTIVE_Y 6U
#define TRANSFER_RGBA_ACTIVE_HEIGHT 682U
#define TRANSFER_RGBA_ACTIVE_OFFSET \
    (4 * TRANSFER_RGBA_WIDTH * TRANSFER_RGBA_ACTIVE_Y)
#define TRANSFER_RGBA_ACTIVE_BYTES \
    (4 * TRANSFER_RGBA_WIDTH * TRANSFER_RGBA_ACTIVE_HEIGHT)
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
    TRANSFER_PROGRAM_RGBA_FILL,
    TRANSFER_PROGRAM_VECTORS,
    TRANSFER_PROGRAM_IMAGE_FILL,
    TRANSFER_PROGRAM_RGBA_YUV,
} NikonTransferProgramKind;

typedef struct NikonTransferProgram {
    NikonTransferProgramKind kind;
    NikonTransferLutProgram lut;
    uint32_t plane[2];
    uint32_t plane_bytes[2];
    uint32_t format;
    bool over;
    uint32_t image_source;
    uint32_t rgba_width, rgba_height, rgba_stride, rgba_paint;
    uint32_t rgba_coordinates[5];
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
    uint64_t rgba720_target; /* UINT64_MAX means no initialized profile. */
    bool yuva107;
    bool wait_reported;
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
    s->rgba720_target = UINT64_MAX;
    s->yuva107 = false;
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
 * per-path operands repeated at draw words80/91. Draw word33 is the target.
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
    { 32, 0x01a4f004 },
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

/* Initialized 176/368-byte program: one rectangle in an explicit RGBA target. */
static const NikonTransferField rgba_single_setup_fields[] = {
    { 0, 0x00691004 }, { 1, 0x80 },
    { 2, 0x005ef00e }, { 3, 0x3f800000 },
    { 5, 0x0063f004 }, { 6, 5 },
    { 7, 0x0040f004 }, { 8, 0x07800b40 },
    { 9, 0x0040f004 }, { 10, 0x07800b40 },
    { 11, 0x0041f046 }, { 12, 0x3f800000 },
    { 16, 0x3f800000 }, { 20, 0x3f800000 },
    { 21, 0x0004f004 },
    { 23, 0x0005f004 }, { 24, 0xd0000005 },
    { 25, 0x0002f004 },
    { 27, 0x0003f004 }, { 28, 0x10000005 },
    { 29, 0x00653004 }, { 30, 0x00002100 },
    { 31, 0x0066f004 }, { 32, 0x00006004 },
    { 33, 0x0080f026 }, { 36, 0x200 }, { 37, 0x07800b40 },
    { 39, 0x0006f004 }, { 40, 1 },
    { 41, 0x0085f004 }, { 42, 0x00010001 },
};

static const NikonTransferField rgba_single_draw_fields[] = {
    { 0, 0x01408004 }, { 2, 0x01901004 },
    { 4, 0x014ee004 }, { 5, 0x001b0000 },
    { 6, 0x0156e004 }, { 7, 0x001b0000 },
    { 8, 0x01a2f004 },
    { 10, 0x01a3f004 }, { 11, 0x01e002d0 },
    { 12, 0x01844004 }, { 14, 0x01a7f004 }, { 15, 0x01000101 },
    { 16, 0x011cf004 }, { 17, 0x01e00000 },
    { 18, 0x01a08004 }, { 20, 0x01aa1004 },
    { 22, 0x01a04004 }, { 23, 0x00010000 },
    { 24, 0x01841004 }, { 26, 0x01842004 },
    { 28, 0x01a02004 }, { 30, 0x01a29004 },
    { 32, 0x01a4f004 },
    { 34, 0x01a51004 }, { 35, 1 }, { 36, 0x01a74004 },
    { 38, 0x01401004 }, { 40, 0x014df004 }, { 42, 0x014df004 },
    { 44, 0x00c0f00e }, { 45, 0x02cf0000 }, { 46, 0x01df0000 },
    { 47, 0x01001004 }, { 48, 1 },
    { 49, 0x01151004 }, { 50, 1 },
    { 51, 0x01b01004 }, { 52, 0xd },
    { 53, 0x01b31004 }, { 54, 1 },
    { 55, 0x01848004 }, { 57, 0x01a11004 },
    { 59, 0x01841004 }, { 60, 8 },
    { 61, 0x01401004 }, { 63, 0x014df004 }, { 65, 0x014df004 },
    { 67, 0x01402004 }, { 69, 0x014df004 }, { 71, 0x014df004 },
    { 73, 0x0115c004 },
    { 75, 0x0185f004 }, { 76, 0xff000000 },
    { 77, 0x01863004 }, { 78, 0x108 },
    { 79, 0x00a0f02e }, { 82, 0x200 }, { 83, 0x07800b40 },
    { 85, 0x00040002 }, { 86, 0x00a6f004 }, { 87, 0x00010001 },
    { 88, 0x01a61004 }, { 89, 1 },
    { 90, 0x00081004 }, { 91, 1 },
};

/* Finite continuation in the retained720x480 context. */
static const NikonTransferField path_setup_fields[] = {
    { 0, 0x005ef00e }, { 1, 0x3f800000 },
    { 3, 0x0063f004 }, { 4, 5 },
    { 5, 0x0040f004 }, { 6, 0x07800b40 },
    { 7, 0x0040f004 }, { 8, 0x07800b40 },
    { 9, 0x0041f046 }, { 10, 0x3f800000 },
    { 14, 0x3f800000 }, { 18, 0x3f800000 },
    { 19, 0x0004f004 }, { 21, 0x0005f004 }, { 22, 0xd0000005 },
    { 23, 0x0002f004 }, { 25, 0x0003f004 }, { 26, 0x10000005 },
    { 27, 0x00653004 }, { 28, 0x2100 },
    { 29, 0x0066f004 }, { 30, 0x6004 },
    { 31, 0x0080f026 }, { 34, 0x200 }, { 35, 0x07800b40 },
    { 37, 0x0006f004 }, { 38, 1 },
    { 39, 0x0085f004 }, { 40, 0x10002 },
};

/* Repeated55-word opaque SRC_OVER draw; rega5 bit8 selects nonzero fill. */
static const NikonTransferField path_draw_fields[] = {
    { 0, 0x01aa1004 }, { 2, 0x01a04004 }, { 3, 0x10000 },
    { 4, 0x01841004 }, { 5, 8 }, { 6, 0x01842004 },
    { 8, 0x01a02004 }, { 10, 0x01a29004 },
    { 12, 0x01a4f004 }, { 13, 0xc175d600 },
    { 14, 0x01a51004 }, { 15, 1 }, { 16, 0x01a74004 },
    { 18, 0x01401004 }, { 20, 0x014df004 }, { 22, 0x014df004 },
    { 24, 0x01841004 }, { 25, 8 },
    { 26, 0x01401004 }, { 28, 0x014df004 }, { 30, 0x014df004 },
    { 32, 0x01402004 }, { 34, 0x014df004 }, { 36, 0x014df004 },
    { 38, 0x0115c004 }, { 40, 0x0185f004 }, { 41, 0xff000000 },
    { 42, 0x01863004 }, { 43, 0x108 },
    { 44, 0x00a0f02e }, { 47, 0x200 }, { 48, 0x07800b40 },
    { 50, 0x00040102 }, { 51, 0x00a6f004 },
    { 53, 0x01a61004 }, { 54, 1 },
};

/* 1068c50c: solid stroke state followed by inherited-matrix path execution. */
static const NikonTransferField stroke_setup_fields[] = {
    {0, 0x0004f004}, {2, 0x0005f004}, {4, 0x0002f004}, {6, 0x0003f004},
    {8, 0x005af01e}, {13, 0x00654004}, {15, 0x00653004}, {16, 0x2000},
    {17, 0x0066f004}, {18, 0x6004}, {19, 0x0080f026}, {22, 0x200},
    {23, 0x07800b40}, {25, 0x0006f004}, {26, 1}, {27, 0x0085f004},
};

static const NikonTransferField stroke_draw_fields[] = {
    {0, 0x01841004}, {1, 8}, {2, 0x01401004}, {4, 0x014df004},
    {6, 0x014df004}, {8, 0x01402004}, {10, 0x014df004}, {12, 0x014df004},
    {14, 0x0115c004}, {16, 0x0185f004}, {18, 0x01863004}, {19, 0x108},
    {20, 0x00a0f02e}, {23, 0x200}, {24, 0x07800b40}, {27, 0x00a6f004},
    {29, 0x01a61004}, {30, 1},
};

static bool nikon_transfer_path_fields(const uint32_t *words, bool setup,
                                       bool stroke)
{
    const NikonTransferField *fields = stroke ?
        (setup ? stroke_setup_fields : stroke_draw_fields) :
        (setup ? path_setup_fields : path_draw_fields);
    unsigned count = stroke ? (setup ? 29 : 31) : (setup ? 41 : 55);
    unsigned field_count = stroke ?
        (setup ? ARRAY_SIZE(stroke_setup_fields) : ARRAY_SIZE(stroke_draw_fields)) :
        (setup ? ARRAY_SIZE(path_setup_fields) : ARRAY_SIZE(path_draw_fields));
    unsigned field = 0;

    for (unsigned i = 0; i < count; i++) {
        uint32_t expected = 0;

        if (field < field_count && fields[field].word == i) {
            expected = fields[field++].value;
        }
        if (stroke ?
            (setup ? (i == 1 || i == 3 || i == 5 || i == 7 || i == 11 ||
                      i == 12 || i == 14 || i == 20 || i == 28) :
                     (i == 17 || i == 21 || i == 26 || i == 28)) :
            ((setup && (i == 4 || (i >= 10 && i <= 18) || i == 20 ||
                       i == 22 || i == 24 || i == 26 || i == 32 || i == 40)) ||
             (!setup && (i == 13 || i == 41 || i == 45 || i == 50 || i == 52)))) {
            continue;
        }
        if (le32_to_cpu(words[i]) != expected) {
            return false;
        }
    }
    return field == field_count;
}

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
        /* The next complete 41..49 write replaces this cached matrix. */
        if ((!draw && ((i >= 3 && i <= 11) || i == 37)) ||
            (draw && (i == 9 || i == 379 || i == 381 || i == 383 ||
                      i == 399))) {
            continue;
        }
        /* 1068bd90 preserves solid at66, then replaces it with8 at389. */
        if (draw && i == 66 && (value == 0 || value == 8)) {
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
                                            unsigned field_count, bool draw,
                                            bool single)
{
    unsigned field = 0;

    for (unsigned i = 0; i < count; i++) {
        uint32_t expected = 0, value = le32_to_cpu(words[i]);

        if (field < field_count && fields[field].word == i) {
            expected = fields[field++].value;
        }
        if ((!draw && (i == 22 || i == 26 || i == 34 || i == 48)) ||
            (draw && (i == 33 || i == 80 || (!single && i == 91) ||
                      (single && i == 76)))) {
            continue;
        }
        /*
         * 10645c86 preserves shadow bit3; 10646062 sets it at word60,
         * before either draw trigger. Other initial state bits stay invalid.
         */
        if (draw && i == 25 && (value == 0 || value == 8)) {
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

static bool nikon_transfer_alias(uint64_t source, uint64_t length,
                                  uint64_t address, uint64_t target_bytes)
{
    MemoryRegionSection target = memory_region_find(get_system_memory(),
                                                   address, target_bytes);
    uintptr_t destination;
    bool alias = true;

    if (!target.mr) {
        return true;
    }
    if (int128_get64(target.size) < target_bytes ||
        !memory_access_is_direct(target.mr, true, MEMTXATTRS_UNSPECIFIED)) {
        goto out;
    }
    destination = (uintptr_t)memory_region_get_ram_ptr(target.mr) +
                  target.offset_within_region;
    while (length) {
        MemoryRegionSection input = memory_region_find(get_system_memory(),
                                                       source, length);
        uint64_t span;
        uintptr_t backing;

        if (!input.mr) {
            goto out;
        }
        span = int128_get64(input.size);
        if (!span || input.offset_within_address_space != source ||
            !memory_access_is_direct(input.mr, false, MEMTXATTRS_UNSPECIFIED)) {
            memory_region_unref(input.mr);
            goto out;
        }
        backing = (uintptr_t)memory_region_get_ram_ptr(input.mr) +
                  input.offset_within_region;
        memory_region_unref(input.mr);
        if (backing < destination + target_bytes && destination < backing + span) {
            goto out;
        }
        source += span;
        length -= span;
    }
    alias = false;
out:
    memory_region_unref(target.mr);
    return alias;
}

static bool nikon_transfer_path_initial(const uint32_t *draw)
{
    unsigned field = 0;

    /*
     * 106454b2/106b55a2 split global initialization around the first paint.
     * Reuse the established initialized RGBA state, without constraining
     * that first path's operands or its later draw state.
     */
    for (unsigned i = 0; i < 59; i++) {
        uint32_t expected = 0;

        if (field < ARRAY_SIZE(rgba_single_draw_fields) &&
            rgba_single_draw_fields[field].word == i) {
            expected = rgba_single_draw_fields[field++].value;
        }
        if ((i < 20 || i >= 44) && le32_to_cpu(draw[i]) != expected) {
            return false;
        }
    }
    return true;
}

typedef struct NikonTransferRectangle {
    uint32_t x, y, width, height, paint, target, key;
} NikonTransferRectangle;

static bool nikon_transfer_image_initial(const uint32_t *setup,
                                         const uint32_t *draw);

static bool nikon_transfer_rectangle_item(const uint32_t *setup, unsigned na,
    const uint32_t *draw, unsigned nb,
    NikonTransferRectangle *rectangle, unsigned *aw, unsigned *bw,
    const char **reason);

static void nikon_transfer_rgba_over(uint8_t *destination,
    const uint8_t *source, unsigned coverage, bool premultiplied);

static bool nikon_transfer_path_prefix(const uint32_t *words, bool setup)
{
    const NikonTransferField *fields = setup ? path_setup_fields :
                                              path_draw_fields;
    unsigned count = setup ? 19 : 24, field = 0;

    for (unsigned i = 0; i < count; i++) {
        uint32_t expected = 0;

        if (fields[field].word == i) {
            expected = fields[field++].value;
        }
        if (setup ? (i == 4 || (i >= 10 && i <= 18)) : i == 13) {
            continue;
        }
        if (le32_to_cpu(words[i]) != expected) {
            return false;
        }
    }
    return true;
}

static bool nikon_transfer_vectors(NikonTransferState *s, uint8_t *pixels,
                                   uint32_t *output, const char **reason)
{
    const uint32_t *setup, *draw;
    size_t setup_bytes, draw_bytes;
    unsigned sa = 0, da = 0, count = 0, segment_count = 0;
    uint32_t first_key = 0, target, matrix[9];
    bool matrix_valid = false, path_initial = false, rectangle_initial = false;
    const uint32_t target_bytes = 4 * NIKON_PATH_MASK_BYTES;
    g_autofree uint8_t *mask = pixels ? g_malloc(NIKON_PATH_MASK_BYTES) : NULL;

    setup = g_bytes_get_data(s->stream[0].bytes, &setup_bytes);
    draw = g_bytes_get_data(s->stream[1].bytes, &draw_bytes);
    if (setup_bytes >= 8 && le32_to_cpu(setup[0]) == 0x00691004) {
        if (setup_bytes < 12 || le32_to_cpu(setup[1]) != 0x80) {
            *reason = "transfer-vector-initial-state-not-supported";
            return false;
        }
        sa = 2;
        if (draw_bytes >= 35 * 4 &&
            le32_to_cpu(draw[0]) == 0x01408004 &&
            le32_to_cpu(setup[sa]) == 0x0041f046) {
            rectangle_initial = true;
            da = 35;
            if (!nikon_transfer_image_initial(setup, draw)) {
                *reason = "transfer-vector-initial-state-not-supported";
                return false;
            }
        } else {
            path_initial = true;
            da = 20;
            if (draw_bytes < 92 * 4 ||
                !nikon_transfer_path_initial(draw)) {
                *reason = "transfer-vector-initial-state-not-supported";
                return false;
            }
        }
    }
    if (setup_bytes / 4 < sa + 41 || draw_bytes / 4 < da + 55) {
        *reason = "transfer-vector-count-not-supported";
        return false;
    }
    bool first_rectangle = le32_to_cpu(setup[sa]) == 0x0041f046;
    unsigned first_draw = da;

    if (first_rectangle && le32_to_cpu(draw[first_draw]) == 0x01848004) {
        first_draw += 2;
    }
    target = le32_to_cpu(draw[first_draw + (first_rectangle ? 31 : 13)]);
    if ((!path_initial && !rectangle_initial &&
         (s->rgba720_target == UINT64_MAX ||
          (!first_rectangle && s->rgba720_target != target))) ||
        (target & 3) || (uint64_t)target + target_bytes > (1ULL << 32) ||
        nikon_transfer_span(target, target_bytes, true) < target_bytes) {
        *reason = "transfer-vector-target-or-context-not-supported";
        return false;
    }
    for (unsigned i = 0; i < 2; i++) {
        if (nikon_transfer_overlap(target, target_bytes, s->stream[i].source,
                                   8ULL * s->stream[i].units64) ||
            nikon_transfer_alias(s->stream[i].source,
                                  8ULL * s->stream[i].units64,
                                  target, target_bytes) ||
            (i && nikon_transfer_overlap(s->stream[0].source, setup_bytes,
                                         s->stream[1].source, draw_bytes))) {
            *reason = "transfer-stream-operands-overlap";
            return false;
        }
    }
    while (sa < setup_bytes / 4) {
        const uint32_t *a = setup + sa, *b = draw + da;
        unsigned na = setup_bytes / 4 - sa, nb = draw_bytes / 4 - da;
        unsigned aw, bw;
        uint32_t key, expected_key;

        if (na == 1 && !a[0]) {
            break;
        }
        if (++count > NIKON_PATH_MAX_BATCH || !nb) {
            *reason = "transfer-vector-count-not-supported";
            return false;
        }
        if (le32_to_cpu(a[0]) == 0x0041f046) {
            NikonTransferRectangle rectangle;

            if (!nikon_transfer_rectangle_item(a, na, b, nb,
                    &rectangle, &aw, &bw, reason) ||
                rectangle.target != target) {
                *reason = "transfer-rectangle-program-not-supported";
                return false;
            }
            key = rectangle.key;
            for (unsigned i = 0; i < 9; i++) {
                matrix[i] = cpu_to_le32(i % 4 ? 0 : 0x3f800000);
            }
            matrix_valid = true;
            segment_count = 5;
            if (pixels) {
                for (unsigned y = rectangle.y;
                     y < rectangle.y + rectangle.height; y++) {
                    for (unsigned x = rectangle.x;
                         x < rectangle.x + rectangle.width; x++) {
                        stl_le_p(pixels + 4 * (y * NIKON_PATH_WIDTH + x),
                                 rectangle.paint);
                    }
                }
            }
        } else {
            bool full = le32_to_cpu(a[0]) == 0x005ef00e;
            bool stroked;
            unsigned prefix = 0, paint_prefix = 0;
            uint32_t draw_state[55];
            NikonPathStroke stroke;

            if (full && na < 41) {
                *reason = "transfer-path-batch-count-not-supported";
                return false;
            }
            stroked = !full || le32_to_cpu(a[27]) == 0x005af01e;
            if (full) {
                if (!nikon_transfer_path_prefix(a, true)) {
                    *reason = "transfer-path-program-not-supported";
                    return false;
                }
                memcpy(matrix, a + 10, sizeof(matrix));
                matrix_valid = true;
                segment_count = le32_to_cpu(a[4]);
                prefix = stroked ? 19 : 0;
            }
            if (stroked && nb >= 24 &&
                le32_to_cpu(b[0]) == 0x01aa1004) {
                if (!nikon_transfer_path_prefix(b, false) ||
                    le32_to_cpu(b[13]) != target) {
                    *reason = "transfer-path-program-not-supported";
                    return false;
                }
                paint_prefix = 24;
            }
            aw = prefix + (stroked ? 29 : 41);
            bw = paint_prefix + (stroked ? 31 : 55);
            if (na < aw || nb < bw || !matrix_valid ||
                (stroked && path_initial && count == 1)) {
                *reason = "transfer-path-batch-count-not-supported";
                return false;
            }
            a += prefix;
            b += paint_prefix;
            if (path_initial && count == 1) {
                if (nb < bw + 15) {
                    *reason = "transfer-path-initial-state-not-supported";
                    return false;
                }
                memcpy(draw_state, b, 24 * sizeof(*b));
                memcpy(draw_state + 24, b + 39, 31 * sizeof(*b));
                b = draw_state;
                bw += 15;
            } else if ((!stroked || paint_prefix) &&
                       le32_to_cpu(b[stroked ? 0 : 24]) == 0x01848004) {
                unsigned at = stroked ? 0 : 24;

                if (nb < bw + 2 || b[at + 1]) {
                    *reason = "transfer-path-color-transform-not-supported";
                    return false;
                }
                memcpy(draw_state, b, at * sizeof(*b));
                memcpy(draw_state + at, b + at + 2,
                       ((stroked ? 31 : 55) - at) * sizeof(*b));
                b = draw_state;
                bw += 2;
            }
            uint32_t cd = le32_to_cpu(a[stroked ? 3 : 22]);
            uint32_t sd = le32_to_cpu(a[stroked ? 7 : 26]);
            uint32_t nc = cd & 0x0fffffff, ns = sd & 0x0fffffff;
            uint32_t source[2] = {
                le32_to_cpu(a[stroked ? 1 : 20]),
                le32_to_cpu(a[stroked ? 5 : 24]),
            };
            uint32_t length[2] = { nc * 4, ns };
            uint32_t coordinates[NIKON_PATH_MAX_COORDINATES];
            uint8_t segments[NIKON_PATH_MAX_SEGMENTS];
            uint32_t paint = le32_to_cpu(b[stroked ? 17 : 41]);
            uint32_t rule = le32_to_cpu(b[stroked ? 26 : 50]);

            key = le32_to_cpu(a[stroked ? 28 : 40]);
            if (!nikon_transfer_path_fields(a, true, stroked) ||
                !nikon_transfer_path_fields(b, false, stroked) ||
                (cd & 0xf0000000) != 0xd0000000 ||
                (sd & 0xf0000000) != 0x10000000 ||
                !nc || nc > NIKON_PATH_MAX_COORDINATES ||
                !ns || ns > NIKON_PATH_MAX_SEGMENTS || segment_count != ns ||
                !le32_to_cpu(a[stroked ? 20 : 32]) ||
                a[stroked ? 20 : 32] != b[stroked ? 21 : 45] ||
                (rule != 0x40002 && rule != 0x40102) ||
                le32_to_cpu(b[stroked ? 28 : 52]) != key ||
                (!stroked && le32_to_cpu(b[13]) != target) ||
                (source[0] & 3)) {
                *reason = "transfer-path-program-not-supported";
                return false;
            }
            if (stroked) {
                uint32_t radius = le32_to_cpu(a[11]);
                uint32_t extent = le32_to_cpu(a[12]);
                uint32_t style = le32_to_cpu(a[14]);

                memcpy(&stroke.radius, &radius, sizeof(radius));
                memcpy(&stroke.extent, &extent, sizeof(extent));
                stroke.cap = (style >> 16) & 3;
                stroke.join = (style >> 20) & 3;
                if (style & ~0x00330000U) {
                    *reason = "transfer-path-stroke-not-supported";
                    return false;
                }
            }
            for (unsigned j = 0; j < 2; j++) {
                if (!nikon_transfer_range(source[j], length[j], false) ||
                    nikon_transfer_overlap(target, target_bytes,
                                           source[j], length[j]) ||
                    nikon_transfer_alias(source[j], length[j], target,
                                          target_bytes) ||
                    nikon_transfer_overlap(s->stream[0].source, setup_bytes,
                                           source[j], length[j]) ||
                    nikon_transfer_overlap(s->stream[1].source, draw_bytes,
                                           source[j], length[j]) ||
                    (j && nikon_transfer_overlap(source[0], length[0],
                                                 source[1], length[1]))) {
                    *reason = "transfer-path-memory-not-supported";
                    return false;
                }
            }
            if (dma_memory_read(&address_space_memory, source[0], coordinates,
                                length[0],
                                MEMTXATTRS_UNSPECIFIED) != MEMTX_OK ||
                dma_memory_read(&address_space_memory, source[1], segments,
                                length[1],
                                MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
                *reason = "transfer-path-read-failed";
                return false;
            }
            if (mask) {
                memset(mask, 0, NIKON_PATH_MASK_BYTES);
            }
            if (!nikon_path_mask(mask, NIKON_PATH_WIDTH, NIKON_PATH_HEIGHT,
                                 segments, ns, coordinates, nc, matrix,
                                 rule == 0x40102,
                                 stroked ? &stroke : NULL, reason)) {
                return false;
            }
            if (pixels) {
                uint8_t color[4];

                stl_le_p(color, paint);
                for (unsigned p = 0; p < NIKON_PATH_MASK_BYTES; p++) {
                    nikon_transfer_rgba_over(pixels + 4 * p, color,
                                             mask[p], false);
                }
            }
        }
        if (count == 1) {
            first_key = key;
        }
        expected_key = 0x10000 |
            ((first_key - 0x10001 + count - 1) % 0xfffe + 1);
        if ((path_initial && first_key != 0x10001) ||
            (first_key >> 16) != 1 || !(first_key & 0xffff) ||
            (first_key & 0xffff) == 0xffff || key != expected_key) {
            *reason = "transfer-path-key-not-supported";
            return false;
        }
        sa += aw;
        da += bw;
    }
    if (!count || setup_bytes != ROUND_UP(sa, 2) * 4 ||
        draw_bytes != ROUND_UP(da + 2, 2) * 4 ||
        le32_to_cpu(draw[da]) != 0x00081004 ||
        le32_to_cpu(draw[da + 1]) != 1 ||
        ((sa & 1) && le32_to_cpu(setup[sa])) ||
        ((da & 1) && le32_to_cpu(draw[da + 2]))) {
        *reason = "transfer-path-ending-not-supported";
        return false;
    }
    if (output) {
        *output = target;
    }
    return true;
}

/* C1.70 inline signed32 rectangle and A8/straight RGBA image modulation. */
static const NikonTransferField image_setup_fields[] = {
    { 0, 0x0041f046 }, { 10, 0x00653004 }, { 11, 0x2100 },
    { 12, 0x0003f004 }, { 13, 5 }, { 14, 0x0005f004 }, { 15, 0x80000005 },
    { 16, 0x0063f004 }, { 17, 5 }, { 18, 0x005ef004 }, { 19, 0x3f800000 },
    { 20, 0x005ff004 }, { 22, 0x0040f004 }, { 23, 0x07800b40 },
    { 24, 0x0080f026 }, { 27, 0x10 }, { 28, 0x07800b40 },
    { 30, 0x0006f004 }, { 31, 1 }, { 32, 0x0a }, { 33, 0x07090702 },
    { 35, 0x00010022 }, { 41, 0x0085f004 },
};

static const NikonTransferField image_draw_fields[] = {
    { 0, 0x010c7046 }, { 10, 0x0158f004 }, { 12, 0x01567004 },
    { 14, 0x0152f004 }, { 16, 0x01401004 }, { 17, 2 },
    { 18, 0x014df004 }, { 20, 0x014df004 }, { 22, 0x01404004 },
    { 23, 0x00210000 }, { 24, 0x0155f004 },
    { 26, 0x01a04004 }, { 27, 0x10000 }, { 28, 0x01841004 }, { 29, 8 },
    { 30, 0x01401004 }, { 31, 2 }, { 32, 0x014df004 }, { 34, 0x014df004 },
    { 36, 0x01402004 }, { 38, 0x014df004 }, { 40, 0x014df004 },
    { 42, 0x0115c004 }, { 43, 0x01000000 }, { 44, 0x0185f004 },
    { 46, 0x01aa1004 }, { 48, 0x01841004 }, { 49, 14 },
    { 50, 0x01863004 }, { 51, 0x108 }, { 52, 0x01842004 },
    { 54, 0x01a02004 }, { 56, 0x01a29004 },
    { 58, 0x01a4f004 }, { 60, 0x01a51004 }, { 61, 1 },
    { 62, 0x01a74004 }, { 64, 0x01158004 }, { 65, 0x01000000 },
    { 66, 0x01531004 }, { 67, 1 }, { 68, 0x01431004 }, { 69, 1 },
    { 70, 0x00a0f02e }, { 73, 0x10 }, { 74, 0x07800b40 }, { 76, 2 },
    { 77, 0x00a6f004 }, { 79, 0x01a61004 }, { 80, 1 },
    { 81, 0x00081004 }, { 82, 1 },
};

static bool nikon_transfer_image_fields(const uint32_t *words, bool setup,
                                        unsigned count)
{
    const NikonTransferField *fields = setup ? image_setup_fields :
                                              image_draw_fields;
    unsigned field_count = setup ? ARRAY_SIZE(image_setup_fields) :
                                   ARRAY_SIZE(image_draw_fields);
    unsigned field = 0;

    for (unsigned i = 0; i < count; i++) {
        uint32_t expected = 0;

        if (field < field_count && fields[field].word == i) {
            expected = fields[field++].value;
        }
        if ((setup && ((i >= 1 && i <= 9) || i == 25 ||
                       (i >= 36 && i <= 40) || i == 42)) ||
            (!setup && ((i >= 1 && i <= 9) || i == 11 || i == 13 || i == 15 ||
                        i == 25 || i == 29 || i == 45 || i == 59 ||
                        i == 71 || i == 78))) {
            continue;
        }
        if (le32_to_cpu(words[i]) != expected) {
            return false;
        }
    }
    return field == field_count;
}

static bool nikon_transfer_image_parse(NikonTransferState *s, uint8_t *overlay,
                                       uint32_t *output, const char **reason)
{
    static const uint8_t segments[] = { 2, 7, 9, 7, 0 };
    const uint32_t target_bytes = 4 * NIKON_PATH_MASK_BYTES;
    const uint32_t *setup, *draw;
    uint32_t coordinates[5], extent, width, height, paint, key, target;
    NikonPathImageFormat format;
    uint64_t source[3], length[3];
    size_t setup_bytes, draw_bytes;
    bool initial;
    g_autofree uint8_t *mask = NULL;
    g_autofree uint8_t *image = NULL;

    setup = g_bytes_get_data(s->stream[0].bytes, &setup_bytes);
    draw = g_bytes_get_data(s->stream[1].bytes, &draw_bytes);
    initial = setup_bytes == 224 && draw_bytes == 472;
    if (initial) {
        if (!nikon_transfer_image_initial(setup, draw)) {
            *reason = "transfer-image-initial-state-not-supported";
            return false;
        }
        setup += 12;
        draw += 35;
    } else {
        if (setup_bytes == 216 && le32_to_cpu(setup[0]) == 0x0041f046) {
            /* The image body replaces all nine cached values before latching. */
            setup += 10;
        } else if (setup_bytes != 176) {
            *reason = "transfer-image-prefix-not-supported";
            return false;
        }
        if (draw_bytes == 344 && le32_to_cpu(draw[0]) == 0x01848004 &&
            !draw[1]) {
            draw += 2;
        } else if (draw_bytes != 336) {
            *reason = "transfer-image-prefix-not-supported";
            return false;
        }
    }
    if (!nikon_transfer_image_fields(setup, true, 44) ||
        !nikon_transfer_image_fields(draw, false, initial ? 83 : 84)) {
        *reason = "transfer-image-program-not-supported";
        return false;
    }
    switch (le32_to_cpu(draw[13])) {
    case 0x001b0018:
        format = NIKON_PATH_IMAGE_A8;
        break;
    case 0x001b0100:
        format = NIKON_PATH_IMAGE_RGBA;
        break;
    default:
        *reason = "transfer-image-format-not-supported";
        return false;
    }
    target = le32_to_cpu(draw[59]);
    if (!initial && s->rgba720_target != target) {
        *reason = "transfer-stream-context-not-established";
        return false;
    }
    extent = le32_to_cpu(draw[11]);
    width = extent >> 16;
    height = extent & 65535;
    paint = le32_to_cpu(draw[45]);
    key = le32_to_cpu(setup[42]);
    /* 10646062 preserves the prior paint mode before 10645c86 replaces it. */
    if ((le32_to_cpu(draw[29]) & ~6U) != 8 ||
        le32_to_cpu(draw[15]) != extent || (key >> 16) != 1 ||
        !(key & 65535) || key != le32_to_cpu(draw[78]) ||
        !le32_to_cpu(setup[25]) ||
        le32_to_cpu(setup[25]) != le32_to_cpu(draw[71]) ||
        !nikon_path_image(NULL, NULL, NULL, format, width, height, draw + 1,
                          paint, reason)) {
        *reason = "transfer-image-operands-not-supported";
        return false;
    }
    for (unsigned i = 0; i < 5; i++) {
        int32_t value = le32_to_cpu(setup[36 + i]);
        float converted;
        uint32_t bits;

        if (value < -4096 || value > 4096) {
            *reason = "transfer-path-coordinate-not-supported";
            return false;
        }
        converted = value;
        memcpy(&bits, &converted, 4);
        coordinates[i] = cpu_to_le32(bits);
    }
    source[0] = s->stream[0].source;
    source[1] = s->stream[1].source;
    source[2] = le32_to_cpu(draw[25]);
    length[0] = setup_bytes;
    length[1] = draw_bytes;
    length[2] = (uint64_t)width * height *
                (format == NIKON_PATH_IMAGE_RGBA ? 4 : 1);
    if ((target & 3) || (uint64_t)target + target_bytes > (1ULL << 32) ||
        nikon_transfer_span(target, target_bytes, true) < target_bytes) {
        *reason = "transfer-stream-memory-range-not-backed";
        return false;
    }
    for (unsigned i = 0; i < 3; i++) {
        if (!nikon_transfer_range(source[i], length[i], false) ||
            nikon_transfer_overlap(target, target_bytes, source[i], length[i]) ||
            nikon_transfer_alias(source[i], length[i], target, target_bytes)) {
            *reason = "transfer-image-memory-not-supported";
            return false;
        }
        for (unsigned j = 0; j < i; j++) {
            if (nikon_transfer_overlap(source[i], length[i],
                                       source[j], length[j])) {
                *reason = "transfer-stream-operands-overlap";
                return false;
            }
        }
    }
    if (overlay) {
        mask = g_malloc0(NIKON_PATH_MASK_BYTES);
        image = g_malloc(length[2]);
        if (dma_memory_read(&address_space_memory, source[2], image,
                            length[2], MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
            *reason = "transfer-image-read-failed";
            return false;
        }
    }
    if (!nikon_path_mask(mask, NIKON_PATH_WIDTH, NIKON_PATH_HEIGHT,
                         segments, 5, coordinates, 5, setup + 1,
                         false, NULL, reason) ||
        !nikon_path_image(overlay, mask, image, format, width, height, draw + 1,
                          paint, reason)) {
        return false;
    }
    if (output) {
        *output = target;
    }
    return true;
}

static bool nikon_transfer_rgba_parse(NikonTransferState *s,
                                      NikonTransferProgram *program,
                                      const char **reason)
{
    static const uint8_t expected_segments[TRANSFER_RGBA_PATH_SEGMENT_BYTES] = {
        0x02, 0x07, 0x09, 0x07, 0x00,
    };
    static const uint32_t expected_coordinates[5] = {
        0, 0x40c00000, 0x44800000, 0x442a8000, 0xc4800000,
    };
    const uint32_t *setup, *draw;
    uint8_t segments[TRANSFER_RGBA_PATH_SEGMENT_BYTES];
    uint32_t coordinates[ARRAY_SIZE(expected_coordinates)];
    size_t setup_bytes, draw_bytes;
    uint64_t input[4], input_bytes[4];
    bool single;
    uint32_t target, target_bytes, active_offset;
    const NikonTransferField *setup_fields, *draw_fields;
    unsigned setup_count, draw_count, setup_extent, draw_extent;

    setup = g_bytes_get_data(s->stream[0].bytes, &setup_bytes);
    draw = g_bytes_get_data(s->stream[1].bytes, &draw_bytes);
    single = draw_bytes == NIKON_TRANSFER_RGBA_SINGLE_DRAW_BYTES;
    setup_fields = single ? rgba_single_setup_fields : rgba_setup_fields;
    draw_fields = single ? rgba_single_draw_fields : rgba_draw_fields;
    setup_count = single ? ARRAY_SIZE(rgba_single_setup_fields) :
                           ARRAY_SIZE(rgba_setup_fields);
    draw_count = single ? ARRAY_SIZE(rgba_single_draw_fields) :
                          ARRAY_SIZE(rgba_draw_fields);
    setup_extent = single ? NIKON_TRANSFER_RGBA_SINGLE_SETUP_BYTES :
                            NIKON_TRANSFER_RGBA_SETUP_BYTES;
    draw_extent = single ? NIKON_TRANSFER_RGBA_SINGLE_DRAW_BYTES :
                           NIKON_TRANSFER_RGBA_DRAW_BYTES;
    if (setup_bytes != setup_extent || draw_bytes != draw_extent) {
        *reason = "transfer-stream-truncated";
        return false;
    }
    if (le32_to_cpu(draw[9]) != 0) {
        *reason = "transfer-stream-format-not-supported";
        return false;
    }
    if (le32_to_cpu(draw[11]) != (single ? 0x01e002d0 :
            (TRANSFER_RGBA_HEIGHT << 16 | TRANSFER_RGBA_WIDTH))) {
        *reason = "transfer-stream-geometry-not-supported";
        return false;
    }
    if (!nikon_transfer_rgba_fingerprint(setup, setup_bytes / 4,
            setup_fields, setup_count, false, single) ||
        !nikon_transfer_rgba_fingerprint(draw, draw_bytes / 4,
            draw_fields, draw_count, true, single) ||
        le32_to_cpu(draw[80]) != le32_to_cpu(setup[34]) ||
        !le32_to_cpu(setup[34]) ||
        (!single &&
                    (le32_to_cpu(draw[91]) != le32_to_cpu(setup[48]) ||
                     !le32_to_cpu(setup[48]) ||
                     le32_to_cpu(setup[34]) == le32_to_cpu(setup[48])))) {
        *reason = "transfer-stream-program-not-supported";
        return false;
    }

    /* 1064609a emits 1a4 from the current context's allocation object. */
    target = le32_to_cpu(draw[33]);
    target_bytes = single ? 4 * 720 * 480 : TRANSFER_RGBA_TARGET_BYTES;
    active_offset = single ? 0 : TRANSFER_RGBA_ACTIVE_OFFSET;
    if ((uint64_t)target + target_bytes > (1ULL << 32)) {
        *reason = "transfer-stream-range-overflow";
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
        if (nikon_transfer_overlap(target, target_bytes,
                                   input[i], input_bytes[i])) {
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
    if ((target & 3) ||
        nikon_transfer_span(target, target_bytes, true) < target_bytes ||
        (input[3] & 3) ||
        !nikon_transfer_range(input[2], input_bytes[2], false) ||
        !nikon_transfer_range(input[3], input_bytes[3], false)) {
        *reason = "transfer-stream-memory-range-not-backed";
        return false;
    }
    for (unsigned i = 0; i < ARRAY_SIZE(input); i++) {
        if (nikon_transfer_alias(input[i], input_bytes[i], target, target_bytes)) {
            *reason = "transfer-stream-operands-overlap";
            return false;
        }
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
    if (single) {
        float values[5];

        for (unsigned i = 0; i < ARRAY_SIZE(values); i++) {
            uint32_t bits = le32_to_cpu(coordinates[i]);

            memcpy(&values[i], &bits, sizeof(bits));
        }
        if (values[2] <= 0 || values[3] <= 0 || values[4] != -values[2] ||
            !nikon_path_mask(NULL, 720, 480, segments, 5, coordinates, 5,
                             setup + 12, false, NULL, reason)) {
            *reason = "transfer-stream-path-not-supported";
            return false;
        }
    } else {
        for (unsigned i = 0; i < ARRAY_SIZE(coordinates); i++) {
            if (le32_to_cpu(coordinates[i]) != expected_coordinates[i]) {
                *reason = "transfer-stream-path-not-supported";
                return false;
            }
        }
    }
    program->kind = TRANSFER_PROGRAM_RGBA_FILL;
    /* Format0 paths use their explicit rows without a YUV alignment bias. */
    program->plane[0] = target + active_offset;
    program->rgba_stride = single ? 720 : TRANSFER_RGBA_WIDTH;
    program->rgba_width = program->rgba_stride;
    program->rgba_height = single ? 480 : TRANSFER_RGBA_ACTIVE_HEIGHT;
    program->rgba_paint = le32_to_cpu(draw[76]);
    memcpy(program->rgba_coordinates, coordinates, sizeof(coordinates));
    program->plane_bytes[0] = 4 * ((program->rgba_height - 1) *
                                  program->rgba_stride + program->rgba_width);
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

/* Finite RGBA-input/YUV105 or YUVA107 packet; only captured table state. */
static const NikonTransferField convert_setup_fields[] = {
    { 0, 0x00691004 }, { 1, 0x00000080 },
    { 2, 0x0041f046 }, { 3, 0x3f800000 },
    { 7, 0x3f800000 }, { 11, 0x3f800000 },
    { 12, 0x0041f046 }, { 13, 0x3f800000 },
    { 17, 0xbf800000 }, { 20, 0x442c0000 },
    { 21, 0x3f800000 }, { 22, 0x00653004 },
    { 23, 0x00002100 }, { 24, 0x0003f004 },
    { 25, 0x00000005 }, { 26, 0x0005f004 },
    { 27, 0x80000005 }, { 28, 0x0063f004 },
    { 29, 0x00000005 }, { 30, 0x005ef004 },
    { 31, 0x3f800000 }, { 32, 0x005ff004 },
    { 34, 0x0040f004 }, { 35, 0x0ac01000 },
    { 36, 0x0080f026 }, { 39, 0x00000010 },
    { 40, 0x0ac01000 }, { 42, 0x0006f004 },
    { 43, 0x00000001 }, { 44, 0x0000000a },
    { 45, 0x07090702 }, { 47, 0x00010022 },
    { 50, 0x00000400 }, { 51, 0x000002aa },
    { 52, 0xfffffc00 }, { 53, 0x0085f004 },
};

static const NikonTransferField convert_draw_fields[] = {
    { 0, 0x01408004 }, { 2, 0x01901004 },
    { 4, 0x014ee004 }, { 5, 0x001b0000 },
    { 6, 0x0156e004 }, { 7, 0x001b0000 },
    { 8, 0x01a2f004 }, { 9, 0x00000105 },
    { 10, 0x01a3f004 }, { 11, 0x02b00400 },
    { 12, 0x01844004 }, { 14, 0x01a7f004 },
    { 15, 0x00000101 }, { 16, 0x011cf004 },
    { 17, 0x02b00000 }, { 18, 0x01a08004 },
    { 20, 0x00c0f00e }, { 21, 0x03ff0000 },
    { 22, 0x02af0000 }, { 23, 0x01001004 },
    { 24, 0x00000001 }, { 25, 0x01151004 },
    { 26, 0x00000001 }, { 27, 0x01b01004 },
    { 28, 0x0000000d }, { 29, 0x01b31004 },
    { 30, 0x00000001 }, { 31, 0x01c1f046 },
    { 32, 0x007a8000 }, { 33, 0x001671c6 },
    { 34, 0x00000749 }, { 35, 0x00000100 },
    { 36, 0x006ad259 }, { 37, 0x00075653 },
    { 38, 0x007ad200 }, { 39, 0x00753132 },
    { 40, 0x00000200 }, { 41, 0x01848004 },
    { 43, 0x01a11004 }, { 45, 0x010c7046 },
    { 46, 0x00350000 }, { 50, 0x00b58064 },
    { 51, 0x003f0241 }, { 54, 0x003f0000 },
    { 55, 0x0158f004 }, { 56, 0x040002aa },
    { 57, 0x01567004 }, { 58, 0x001b0100 },
    { 59, 0x0152f004 }, { 60, 0x040002b0 },
    { 61, 0x01401004 }, { 62, 0x00000002 },
    { 63, 0x014df004 }, { 65, 0x014df004 },
    { 67, 0x01404004 }, { 68, 0x00210000 },
    { 69, 0x0155f004 }, { 71, 0x01a04004 },
    { 73, 0x01841004 }, { 75, 0x01401004 },
    { 76, 0x00000002 }, { 77, 0x014df004 },
    { 79, 0x014df004 }, { 81, 0x01402004 },
    { 83, 0x014df004 }, { 85, 0x014df004 },
    { 87, 0x0115c004 }, { 88, 0x01000000 },
    { 89, 0x01aa1004 }, { 91, 0x01841004 },
    { 92, 0x00000002 }, { 93, 0x01868004 },
    { 94, 0x07000000 }, { 95, 0x0187f004 },
    { 97, 0x0188f7fc }, { 354, 0x0192f02e },
    { 355, 0x00000400 }, { 357, 0x00000400 },
    { 359, 0x00000400 }, { 361, 0x01868004 },
    { 362, 0x38000000 }, { 363, 0x0187f004 },
    { 365, 0x0188f0fc }, { 398, 0x01863004 },
    { 399, 0x0000010f }, { 400, 0x01842004 },
    { 402, 0x01a02004 }, { 404, 0x01a29004 },
    { 405, 0x00000105 }, { 406, 0x01a4f004 },
    { 408, 0x01b5f004 }, { 410, 0x01a51004 },
    { 412, 0x01a74004 }, { 414, 0x01158004 },
    { 415, 0x01000000 }, { 416, 0x01531004 },
    { 417, 0x00000001 }, { 418, 0x01431004 },
    { 419, 0x00000001 }, { 420, 0x00a0f02e },
    { 423, 0x00000010 }, { 424, 0x0ac01000 },
    { 426, 0x00000002 }, { 427, 0x00a6f004 },
    { 429, 0x01a61004 }, { 430, 0x00000001 },
    { 431, 0x00081004 }, { 432, 0x00000001 },
};

static bool nikon_transfer_convert_fields(const uint32_t *words, bool setup,
                                          bool scaled, bool over)
{
    static const uint16_t secondary[] = {
        0, 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128, 192,
        256, 384, 512, 768, 1024, 1280, 1536, 1792, 2048, 2304,
        2560, 2816, 3072, 3328, 3584, 3840,
    };
    const NikonTransferField *fields = setup ? convert_setup_fields :
                                              convert_draw_fields;
    unsigned count = setup ? 56 : 434;
    unsigned start = over ? (setup ? 12 : 45) : 0;
    unsigned field_count = setup ? ARRAY_SIZE(convert_setup_fields) :
                                   ARRAY_SIZE(convert_draw_fields);
    unsigned field = 0;

    if (over && !setup) {
        count--;
    }
    for (unsigned i = 0; i < count; i++) {
        uint32_t expected = 0;

        if (field < field_count && fields[field].word == i) {
            expected = fields[field++].value;
        }
        if (i < start) {
            continue;
        }
        /* Initialized packets replace all cached 41..49 values before latch. */
        if (setup && !over && i >= 3 && i <= 11) {
            continue;
        }
        /* 10646062 preserves mode 0..3 before 10645c86 replaces it. */
        if (!setup && i == 74) {
            if (le32_to_cpu(words[i - start]) & ~6U) {
                return false;
            }
            continue;
        }
        if ((setup && (i == 37 || i == 54)) ||
            (!setup && (i == 70 || i == 407 || i == 409 || i == 421 ||
                        i == 428))) {
            continue;
        }
        if (!setup && i >= 98 && i < 354) {
            expected = (i - 98) * 16;
        } else if (!setup && i >= 366 && i < 398) {
            expected = secondary[i - 366];
        }
        if (scaled && setup) {
            switch (i) {
            case 13: expected = 0x3fb60b00; break;
            case 17: expected = 0xbfb5dd80; break;
            case 20: expected = 0x442bffff; break;
            case 50: expected = 720; break;
            case 51: expected = 480; break;
            case 52: expected = (uint32_t)-720; break;
            }
        } else if (scaled) {
            switch (i) {
            case 9: case 405: expected = 0x107; break;
            case 46: expected = 0x34fff8; break;
            case 54: expected = 0x3efffe; break;
            case 56: case 60: expected = 0x02d001e0; break;
            case 417: case 419: expected = 7; break;
            }
        }
        if (over && !setup) {
            switch (i) {
            case 72: expected = 0x10000; break;
            case 74: expected = 2; break;
            case 354: expected = 0x0192f01e; break;
            case 359: expected = 0x01963004; break;
            case 360: expected = 0x400; break;
            case 411: expected = 1; break;
            }
        }
        if (le32_to_cpu(words[i - start]) != expected) {
            return false;
        }
    }
    return field == field_count;
}

static bool nikon_transfer_convert_parse(NikonTransferState *s,
                                         NikonTransferProgram *program,
                                         const char **reason)
{
    const uint32_t *setup, *draw;
    size_t setup_bytes, draw_bytes;
    uint32_t key;
    uint64_t address[5], length[5];
    bool scaled, over;
    unsigned setup_start, draw_start;

    setup = g_bytes_get_data(s->stream[0].bytes, &setup_bytes);
    draw = g_bytes_get_data(s->stream[1].bytes, &draw_bytes);
    over = setup_bytes == 176 && draw_bytes == 1552;
    if (!over && (setup_bytes != 224 || draw_bytes != 1736)) {
        *reason = "transfer-convert-program-not-supported";
        return false;
    }
    if (over && !s->yuva107) {
        *reason = "transfer-stream-context-not-established";
        return false;
    }
    setup_start = over ? 12 : 0;
    draw_start = over ? 45 : 0;
    scaled = over || le32_to_cpu(draw[9]) == 0x107;
    if (!nikon_transfer_convert_fields(setup, true, scaled, over) ||
        !nikon_transfer_convert_fields(draw, false, scaled, over)) {
        *reason = "transfer-convert-program-not-supported";
        return false;
    }
    key = le32_to_cpu(setup[54 - setup_start]);
    if ((key >> 16) != 1 || !(key & 65535) ||
        le32_to_cpu(draw[428 - draw_start]) != key ||
        !le32_to_cpu(setup[37 - setup_start]) ||
        le32_to_cpu(setup[37 - setup_start]) !=
        le32_to_cpu(draw[421 - draw_start])) {
        *reason = "transfer-convert-operands-not-supported";
        return false;
    }
    address[0] = s->stream[0].source;
    address[1] = s->stream[1].source;
    address[2] = le32_to_cpu(draw[70 - draw_start]);
    address[3] = le32_to_cpu(draw[407 - draw_start]);
    address[4] = le32_to_cpu(draw[409 - draw_start]);
    length[0] = setup_bytes;
    length[1] = draw_bytes;
    length[2] = scaled ? NIKON_PATH_MASK_BYTES * 4 : TRANSFER_RGBA_TARGET_BYTES;
    length[3] = TRANSFER_CLEAR_PLANE_BYTES * (scaled ? 2 : 1);
    length[4] = TRANSFER_CLEAR_PLANE_BYTES;
    if (address[4] != address[3] + length[3]) {
        *reason = "transfer-convert-plane-layout-not-supported";
        return false;
    }
    for (unsigned i = 0; i < 5; i++) {
        if (!nikon_transfer_range(address[i], length[i], i >= 3) ||
            nikon_transfer_span(address[i], length[i], i >= 3) < length[i]) {
            *reason = "transfer-stream-memory-range-not-backed";
            return false;
        }
        for (unsigned j = 0; j < i; j++) {
            if (nikon_transfer_overlap(address[j], length[j],
                                       address[i], length[i]) ||
                (i >= 3 && nikon_transfer_alias(address[j], length[j],
                                                address[i], length[i]))) {
                *reason = "transfer-stream-operands-overlap";
                return false;
            }
        }
    }
    program->image_source = address[2];
    program->format = scaled ? 0x107 : 0x105;
    program->over = over;
    for (unsigned i = 0; i < 2; i++) {
        program->plane[i] = address[3 + i];
        program->plane_bytes[i] = length[3 + i];
    }
    return true;
}

/*
 * 1068bd90/10646324: signed32, identity rectangle clears. 106454b2 emits
 * initialization only when its global dirty flag is set; ACK retains state.
 * Fields are the emitted register protocol, not an opaque firmware fixture.
 */
static const NikonTransferField rectangle_setup_fields[] = {
    {0, 0x0041f046}, {1, 0x3f800000}, {5, 0x3f800000}, {9, 0x3f800000},
    {10, 0x0041f046}, {11, 0x3f800000}, {15, 0x3f800000}, {19, 0x3f800000},
    {20, 0x00653004}, {21, 0x100}, {22, 0x0040f004}, {23, 0x01e002d0},
    {24, 0x0003f004}, {25, 5}, {26, 0x0005f004}, {27, 0x80000005},
    {28, 0x0063f004}, {29, 5}, {30, 0x005ef004}, {31, 0x3f800000},
    {32, 0x005ff004}, {34, 0x0080f026}, {37, 16}, {38, 0x01e002d0},
    {40, 0x0006f004}, {41, 1}, {42, 0x0a}, {43, 0x07090702},
    {45, 0x00010022}, {51, 0x0085f004},
};

static const NikonTransferField rectangle_prefix_fields[] = {
    {0, 0x01408004}, {2, 0x01901004},
    {4, 0x014ee004}, {5, 0x1b0000}, {6, 0x0156e004}, {7, 0x1b0000},
    {8, 0x01a2f004}, {10, 0x01a3f004}, {11, 0x01e002d0},
    {12, 0x01844004}, {14, 0x01a7f004}, {15, 0x01000101},
    {16, 0x011cf004}, {17, 0x01e00000}, {18, 0x01a08004},
    {20, 0x00c0f00e}, {21, 0x02cf0000}, {22, 0x01df0000},
    {23, 0x01001004}, {24, 1}, {25, 0x01151004}, {26, 1},
    {27, 0x01b01004}, {28, 13}, {29, 0x01b31004}, {30, 1},
    {31, 0x01848004}, {33, 0x01a11004},
};

static const NikonTransferField rectangle_draw_fields[] = {
    {0, 0x0115c004}, {2, 0x01401004}, {4, 0x014df004}, {6, 0x014df004},
    {8, 0x01401004}, {10, 0x014df004}, {12, 0x014df004},
    {14, 0x01402004}, {16, 0x014df004}, {18, 0x014df004},
    {20, 0x01841004}, {21, 8}, {22, 0x01863004}, {23, 0x108},
    {24, 0x01842004}, {26, 0x01a02004}, {28, 0x01a29004},
    {30, 0x01a4f004}, {32, 0x01a74004}, {34, 0x0185f004},
    {36, 0x01841004}, {37, 8}, {38, 0x01848004},
    {40, 0x01a04004}, /* SRC: alpha zero clears the covered destination. */
    {42, 0x01a51004}, {44, 0x01aa1004}, {46, 0x00a0f02e},
    {49, 16}, {50, 0x01e002d0}, {53, 0x00a6f004},
    {55, 0x01a61004}, {56, 1},
};

static bool nikon_transfer_rectangle_fields(const uint32_t *words,
                                            unsigned count,
                                            const NikonTransferField *fields,
                                            unsigned field_count,
                                            uint64_t operands)
{
    unsigned field = 0;

    for (unsigned i = 0; i < count; i++) {
        uint32_t expected = 0;

        if (field < field_count && fields[field].word == i) {
            expected = fields[field++].value;
        }
        if (!(operands & (1ULL << i)) && le32_to_cpu(words[i]) != expected) {
            return false;
        }
    }
    return field == field_count;
}

static bool nikon_transfer_image_initial(const uint32_t *setup,
                                         const uint32_t *draw)
{
    /* The body fully overwrites this matrix before its path latch. */
    return le32_to_cpu(setup[0]) == 0x00691004 &&
           le32_to_cpu(setup[1]) == 0x80 &&
           le32_to_cpu(setup[2]) == 0x0041f046 &&
           nikon_transfer_rectangle_fields(draw, 35, rectangle_prefix_fields,
               ARRAY_SIZE(rectangle_prefix_fields), 0);
}

static bool nikon_transfer_rectangle_item(const uint32_t *setup, unsigned na,
    const uint32_t *draw, unsigned nb,
    NikonTransferRectangle *rectangle, unsigned *aw, unsigned *bw,
    const char **reason)
{
    uint32_t normalized[53] = { cpu_to_le32(0x0041f046) };
    const uint32_t *a = setup, *b = draw;
    unsigned prefix = 0;
    int32_t x, y, width, height;

    if (na < 43 || nb < 57) {
        *reason = "transfer-rectangle-count-not-supported";
        return false;
    }
    *aw = le32_to_cpu(a[10]) == 0x0041f046 ? 53 : 43;
    if (na < *aw) {
        *reason = "transfer-rectangle-count-not-supported";
        return false;
    }
    if (*aw == 43) {
        memcpy(normalized + 10, a, 43 * sizeof(*a));
        a = normalized;
    }
    if (le32_to_cpu(b[0]) == 0x01848004) {
        if (b[1] || nb < 59) {
            *reason = "transfer-rectangle-context-not-supported";
            return false;
        }
        prefix = 2;
        b += prefix;
    }
    *bw = prefix + 57;
    x = le32_to_cpu(a[46]);
    y = le32_to_cpu(a[47]);
    width = le32_to_cpu(a[48]);
    height = le32_to_cpu(a[49]);
    if (!nikon_transfer_rectangle_fields(a, 53, rectangle_setup_fields,
            ARRAY_SIZE(rectangle_setup_fields),
            (0x1ffULL << 1) | (1ULL << 35) |
            (31ULL << 46) | (1ULL << 52)) ||
        !nikon_transfer_rectangle_fields(b, 57, rectangle_draw_fields,
            ARRAY_SIZE(rectangle_draw_fields),
            (1ULL << 31) | (1ULL << 35) | (1ULL << 47) | (1ULL << 54)) ||
        !a[35] || a[35] != b[47] || a[52] != b[54] ||
        x < 0 || y < 0 || width <= 0 || height <= 0 ||
        (uint64_t)x + width > NIKON_PATH_WIDTH ||
        (uint64_t)y + height > NIKON_PATH_HEIGHT ||
        le32_to_cpu(a[50]) != -(uint32_t)width) {
        *reason = "transfer-rectangle-program-not-supported";
        return false;
    }
    *rectangle = (NikonTransferRectangle) {
        x, y, width, height, le32_to_cpu(b[35]), le32_to_cpu(b[31]),
        le32_to_cpu(a[52]),
    };
    return true;
}

static bool nikon_transfer_stream_parse(NikonTransferState *s,
                                        NikonTransferProgram *program,
                                        const char **reason)
{
    size_t setup_bytes, draw_bytes;
    const uint32_t *draw = g_bytes_get_data(s->stream[1].bytes, &draw_bytes);
    const uint32_t *setup = g_bytes_get_data(s->stream[0].bytes, &setup_bytes);

    if (g_bytes_get_size(s->stream[0].bytes) >= 43 * 4 &&
        le32_to_cpu(setup[0]) == 0x00691004 &&
        le32_to_cpu(setup[2]) == 0x005ef00e) {
        /*
         * Keep the established single/two-path initialization profiles.
         * Other batches are identified by commands, before size-based LUT
         * or conversion dispatch can misclassify them.
         */
        if (draw_bytes == NIKON_TRANSFER_RGBA_DRAW_BYTES ||
            draw_bytes == NIKON_TRANSFER_RGBA_SINGLE_DRAW_BYTES) {
            return nikon_transfer_rgba_parse(s, program, reason);
        }
        program->kind = TRANSFER_PROGRAM_VECTORS;
        return nikon_transfer_vectors(s, NULL, NULL, reason);
    }
    /* Command grammar takes precedence over coincident conversion lengths. */
    if ((setup_bytes >= 43 * 4 &&
         ((draw_bytes >= 37 * 4 &&
           le32_to_cpu(draw[11]) == 0x01e002d0 &&
           le32_to_cpu(draw[33]) == 0x01a11004 &&
           le32_to_cpu(draw[35]) == 0x0115c004) ||
          (le32_to_cpu(setup[0]) == 0x0041f046 && draw_bytes >= 57 * 4 &&
           (le32_to_cpu(draw[0]) == 0x0115c004 ||
            (le32_to_cpu(draw[0]) == 0x01848004 &&
             le32_to_cpu(draw[2]) == 0x0115c004))))) ||
        (setup_bytes >= 41 * 4 && draw_bytes >= 55 * 4 &&
         le32_to_cpu(setup[0]) == 0x005ef00e)) {
        program->kind = TRANSFER_PROGRAM_VECTORS;
        return nikon_transfer_vectors(s, NULL, NULL, reason);
    }
    /* Retained conversions also begin with an f24 sampling matrix. */
    if (draw_bytes == 1736 || draw_bytes == 1552) {
        program->kind = TRANSFER_PROGRAM_RGBA_YUV;
        return nikon_transfer_convert_parse(s, program, reason);
    }
    /* Image and SRC-clear initialization share the same global prefix. */
    if (draw_bytes == 472 ||
        (draw_bytes <= (84 + 2) * sizeof(*draw) &&
         (le32_to_cpu(draw[0]) == 0x010c7046 ||
          (draw_bytes >= 12 && le32_to_cpu(draw[0]) == 0x01848004 &&
           le32_to_cpu(draw[2]) == 0x010c7046)))) {
        program->kind = TRANSFER_PROGRAM_IMAGE_FILL;
        return nikon_transfer_image_parse(s, NULL, NULL, reason);
    }
    program->kind = TRANSFER_PROGRAM_LUT;
    if (g_bytes_get_size(s->stream[1].bytes) == NIKON_TRANSFER_RGBA_DRAW_BYTES ||
        g_bytes_get_size(s->stream[1].bytes) ==
        NIKON_TRANSFER_RGBA_SINGLE_DRAW_BYTES) {
        return nikon_transfer_rgba_parse(s, program, reason);
    }
    if (g_bytes_get_size(s->stream[1].bytes) ==
        NIKON_TRANSFER_CLEAR_DRAW_BYTES) {
        return nikon_transfer_clear_parse(s, program, reason);
    }
    return nikon_transfer_lut_parse(s, &program->lut, reason);
}

/*
 * Straight RGBA output; path/image overlays already have associated RGB.
 * Retain 255^3 weights until division, as in the YUVA107 over convention.
 */
static void nikon_transfer_rgba_over(uint8_t *destination, const uint8_t *source,
                                    unsigned coverage, bool premultiplied)
{
    unsigned alpha = source[3] * coverage;
    unsigned sa = alpha * 255;
    unsigned da = destination[3] * (65025 - alpha);
    unsigned sum = sa + da;
    unsigned scale = premultiplied ? coverage * 65025 : sa;

    if (!alpha) {
        return;
    }
    for (unsigned c = 0; c < 3; c++) {
        uint64_t color = (uint64_t)source[c] * scale + destination[c] * da;

        destination[c] = (color + sum / 2) / sum;
    }
    destination[3] = (sum + 32512) / 65025;
}

static bool nikon_transfer_vectors_execute(NikonTransferState *s,
                                           uint64_t *target_out,
                                           const char **reason)
{
    const uint32_t extent = 4 * NIKON_PATH_MASK_BYTES;
    g_autofree uint8_t *pixels = g_malloc(extent);
    hwaddr bytes = extent;
    uint8_t *destination;
    uint32_t target;
    MemTxAttrs attrs = MEMTXATTRS_UNSPECIFIED;

    if (!nikon_transfer_vectors(s, NULL, &target, reason)) {
        return false;
    }
    if (dma_memory_read(&address_space_memory, target, pixels, extent,
                        attrs) != MEMTX_OK) {
        *reason = "transfer-vector-target-read-failed";
        return false;
    }
    /* SRC clears and OVER paths share one private, ordered destination. */
    if (!nikon_transfer_vectors(s, pixels, &target, reason)) {
        return false;
    }
    attrs.memory = true;
    destination = address_space_map(&address_space_memory, target,
                                    &bytes, true, attrs);
    if (!destination || bytes != extent) {
        if (destination) {
            address_space_unmap(&address_space_memory, destination, bytes,
                                true, 0);
        }
        *reason = "transfer-vector-output-map-failed";
        return false;
    }
    memcpy(destination, pixels, extent);
    address_space_unmap(&address_space_memory, destination, bytes, true, bytes);
    *target_out = target;
    return true;
}

static bool nikon_transfer_image_execute(NikonTransferState *s,
                                         uint64_t *target_out,
                                         const char **reason)
{
    g_autofree uint8_t *overlay = g_malloc0(4 * NIKON_PATH_MASK_BYTES);
    hwaddr bytes = 4 * NIKON_PATH_MASK_BYTES;
    uint8_t *destination;
    MemTxAttrs attrs = MEMTXATTRS_UNSPECIFIED;
    uint32_t target = 0xc175d600;

    if (!nikon_transfer_image_parse(s, overlay, &target, reason)) {
        return false;
    }
    attrs.memory = true;
    destination = address_space_map(&address_space_memory, target,
                                    &bytes, true, attrs);
    if (!destination || bytes != 4 * NIKON_PATH_MASK_BYTES) {
        if (destination) {
            address_space_unmap(&address_space_memory, destination, bytes,
                                true, 0);
        }
        *reason = "transfer-path-output-map-failed";
        return false;
    }
    for (unsigned i = 0; i < NIKON_PATH_MASK_BYTES; i++) {
        nikon_transfer_rgba_over(destination + 4 * i, overlay + 4 * i, 255, true);
    }
    address_space_unmap(&address_space_memory, destination, bytes, true, bytes);
    *target_out = target;
    return true;
}

static bool nikon_transfer_rgba_execute(NikonTransferProgram *program,
                                        const char **reason)
{
    static const uint8_t segments[] = { 2, 7, 9, 7, 0 };
    const uint32_t identity[] = {
        cpu_to_le32(0x3f800000), 0, 0, 0, cpu_to_le32(0x3f800000),
        0, 0, 0, cpu_to_le32(0x3f800000),
    };
    unsigned bias = program->rgba_stride == 720 ? 0 : TRANSFER_RGBA_ACTIVE_Y;
    g_autofree uint8_t *mask = g_malloc0(program->rgba_stride *
                                       (program->rgba_height + bias));
    hwaddr mapped = program->plane_bytes[0];
    MemTxAttrs attrs = MEMTXATTRS_UNSPECIFIED;
    uint8_t *destination;
    uint8_t source[4];

    if (!nikon_path_mask(mask, program->rgba_stride, program->rgba_height + bias,
                         segments, 5, program->rgba_coordinates, 5,
                         identity, false, NULL, reason)) {
        return false;
    }
    stl_le_p(source, program->rgba_paint);
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
    for (unsigned y = 0; y < program->rgba_height; y++) {
        for (unsigned x = 0; x < program->rgba_width; x++) {
            nikon_transfer_rgba_over(destination +
                                    4 * (y * program->rgba_stride + x), source,
                                    mask[(y + bias) * program->rgba_stride + x],
                                    false);
        }
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

/* Same functional limited-range BT.601 convention as the YUV clear/reference. */
static void nikon_transfer_rgb_yuv(const uint8_t *pixel, uint8_t yuv[3])
{
    int r = pixel[0], g = pixel[1], b = pixel[2];

    yuv[0] = CLAMP(((66 * r + 129 * g + 25 * b + 128) >> 8) + 16, 0, 255);
    yuv[1] = CLAMP(((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128, 0, 255);
    yuv[2] = CLAMP(((112 * r - 94 * g - 18 * b + 128) >> 8) + 128, 0, 255);
}

static void nikon_transfer_yuv_rgb(const uint8_t *pair, unsigned x,
                                  uint8_t rgb[3])
{
    int c = MAX(0, pair[x & 1] - 16);
    int d = pair[2] - 128, e = pair[3] - 128;

    rgb[0] = CLAMP((298 * c + 409 * e + 128) >> 8, 0, 255);
    rgb[1] = CLAMP((298 * c - 100 * d - 208 * e + 128) >> 8, 0, 255);
    rgb[2] = CLAMP((298 * c + 516 * d + 128) >> 8, 0, 255);
}

static bool nikon_transfer_scaled_yuva(const uint8_t *input, uint8_t *output,
                                      bool over,
                                      const char **reason)
{
    static const uint8_t segments[] = { 2, 7, 9, 7, 0 };
    uint32_t coordinates[] = { 0, 0, 0x44340000, 0x43f00000, 0xc4340000 };
    uint32_t matrix[] = { 0x3fb60b00, 0, 0, 0, 0xbfb5dd80, 0,
                          0, 0x442bffff, 0x3f800000 };
    g_autofree uint8_t *mask = g_malloc0(TRANSFER_RGBA_WIDTH *
                                       TRANSFER_RGBA_HEIGHT);
    uint8_t *alpha = output + 2 * TRANSFER_CLEAR_PLANE_BYTES;

    for (unsigned i = 0; i < ARRAY_SIZE(coordinates); i++) {
        coordinates[i] = cpu_to_le32(coordinates[i]);
    }
    for (unsigned i = 0; i < ARRAY_SIZE(matrix); i++) {
        matrix[i] = cpu_to_le32(matrix[i]);
    }
    if (!nikon_path_mask(mask, TRANSFER_RGBA_WIDTH, TRANSFER_RGBA_HEIGHT,
                         segments, 5, coordinates, 5, matrix, false, NULL, reason)) {
        return false;
    }
    for (unsigned y = 0; y < TRANSFER_CLEAR_HEIGHT; y++) {
        unsigned sy = (2ULL * 66113 * 1024 - 98404ULL * (2 * y + 13)) *
                       480 / (1024ULL * 131070);

        for (unsigned x = 0; x < TRANSFER_CLEAR_WIDTH; x += 2) {
            unsigned at = y * TRANSFER_CLEAR_WIDTH + x;
            uint8_t *pair = output + 2 * at;
            uint8_t yuv[2][3];

            for (unsigned j = 0; j < 2; j++) {
                unsigned sx = 131064ULL * (2 * (x + j) + 1) * 720 /
                               (2048ULL * 131070);
                unsigned coverage = mask[(y + TRANSFER_RGBA_ACTIVE_Y) *
                                          TRANSFER_RGBA_WIDTH + x + j];
                const uint8_t *src = input + 4 * (sy * NIKON_PATH_WIDTH + sx);
                /* Weights use 255^3 to retain fractional alpha until division. */
                unsigned sa = src[3] * coverage * 255;
                unsigned da = alpha[at + j] * (over ?
                    65025 - src[3] * coverage : 255 * (255 - coverage));
                unsigned sum = sa + da;
                uint8_t rgb[3];

                nikon_transfer_yuv_rgb(pair, j, rgb);
                for (unsigned c = 0; c < 3; c++) {
                    rgb[c] = sum ? (src[c] * sa + rgb[c] * da + sum / 2) /
                                    sum : 0;
                }
                alpha[at + j] = (sum + 32512) / 65025;
                nikon_transfer_rgb_yuv(rgb, yuv[j]);
            }
            pair[0] = yuv[0][0];
            pair[1] = yuv[1][0];
            pair[2] = (yuv[0][1] + yuv[1][1] + 1) / 2;
            pair[3] = (yuv[0][2] + yuv[1][2] + 1) / 2;
        }
    }
    return true;
}

static bool nikon_transfer_convert_execute(NikonTransferProgram *program,
                                           const char **reason)
{
    bool scaled = program->format == 0x107;
    unsigned input_bytes = scaled ? NIKON_PATH_MASK_BYTES * 4 :
                                    TRANSFER_RGBA_TARGET_BYTES;
    unsigned first_bytes = program->plane_bytes[0];
    g_autofree uint8_t *input = g_malloc(input_bytes);
    g_autofree uint8_t *output = g_malloc(first_bytes + program->plane_bytes[1]);
    void *plane[2] = { NULL, NULL };
    hwaddr mapped[2] = { first_bytes, program->plane_bytes[1] };
    MemTxAttrs attrs = MEMTXATTRS_UNSPECIFIED;
    bool success = false;

    if (dma_memory_read(&address_space_memory, program->image_source, input,
                        input_bytes, attrs) != MEMTX_OK) {
        *reason = "transfer-stream-input-read-failed";
        return false;
    }
    if (scaled) {
        for (unsigned i = 0; i < 2; i++) {
            if (dma_memory_read(&address_space_memory, program->plane[i],
                                output + (i ? first_bytes : 0),
                                program->plane_bytes[i], attrs) != MEMTX_OK) {
                *reason = "transfer-stream-input-read-failed";
                return false;
            }
        }
        if (!nikon_transfer_scaled_yuva(input, output, program->over, reason)) {
            return false;
        }
    } else {
        for (unsigned p = 0; p < input_bytes; p += 4) {
            if (input[p + 3] != 0 && input[p + 3] != 255) {
                *reason = "transfer-convert-alpha-not-supported";
                return false;
            }
            if (!input[p + 3]) {
                memset(input + p, 0, 3);
            }
        }
        for (unsigned y = 0; y < TRANSFER_CLEAR_HEIGHT; y++) {
            /* Captured f24 sampler at logical y+6.5, evaluated exactly. */
            uint64_t numerator = (2ULL * 66113 * 1024 -
                                  98404ULL * (2 * y + 13)) * 688;
            unsigned sy = numerator / (2 * 65536 * 1024);

            for (unsigned x = 0; x < TRANSFER_CLEAR_WIDTH; x += 2) {
                uint8_t a[3], b[3];
                unsigned at = y * TRANSFER_CLEAR_WIDTH + x;

                nikon_transfer_rgb_yuv(
                    input + 4 * (sy * TRANSFER_RGBA_WIDTH + x), a);
                nikon_transfer_rgb_yuv(
                    input + 4 * (sy * TRANSFER_RGBA_WIDTH + x + 1), b);
                output[at] = a[0];
                output[at + 1] = b[0];
                output[TRANSFER_CLEAR_PLANE_BYTES + at] = (a[1] + b[1] + 1) / 2;
                output[TRANSFER_CLEAR_PLANE_BYTES + at + 1] =
                    (a[2] + b[2] + 1) / 2;
            }
        }
    }
    attrs.memory = true;
    for (unsigned i = 0; i < 2; i++) {
        plane[i] = address_space_map(&address_space_memory, program->plane[i],
                                     &mapped[i], true, attrs);
        if (!plane[i] || mapped[i] != program->plane_bytes[i]) {
            *reason = "transfer-stream-output-map-failed";
            goto out;
        }
    }
    if (nikon_transfer_overlap((uintptr_t)plane[0], mapped[0],
                               (uintptr_t)plane[1], mapped[1])) {
        *reason = "transfer-stream-operands-overlap";
        goto out;
    }
    for (unsigned i = 0; i < 2; i++) {
        memcpy(plane[i], output + (i ? first_bytes : 0), mapped[i]);
    }
    success = true;
out:
    for (unsigned i = 0; i < 2; i++) {
        if (plane[i]) {
            address_space_unmap(&address_space_memory, plane[i], mapped[i], true,
                                success ? mapped[i] : 0);
        }
    }
    return success;
}

static bool nikon_transfer_stream_execute(NikonTransferState *s,
                                          uint64_t *rgba720_target,
                                          bool *yuva107,
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
    if (parsed.kind == TRANSFER_PROGRAM_VECTORS) {
        return nikon_transfer_vectors_execute(s, rgba720_target, reason);
    }
    if (parsed.kind == TRANSFER_PROGRAM_RGBA_YUV) {
        if (!nikon_transfer_convert_execute(&parsed, reason)) {
            return false;
        }
        *yuva107 = parsed.format == 0x107;
        return true;
    }
    if (parsed.kind == TRANSFER_PROGRAM_IMAGE_FILL) {
        if (!nikon_transfer_image_execute(s, rgba720_target, reason)) {
            return false;
        }
        return true;
    }
    if (parsed.kind == TRANSFER_PROGRAM_YUV_CLEAR) {
        return nikon_transfer_clear_execute(&parsed, reason);
    }
    if (parsed.kind == TRANSFER_PROGRAM_RGBA_FILL) {
        if (!nikon_transfer_rgba_execute(&parsed, reason)) {
            return false;
        }
        *rgba720_target = parsed.rgba_stride == 720 ? parsed.plane[0] : UINT64_MAX;
        return true;
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

static void nikon_transfer_stream_reject_log(NikonTransferState *s)
{
    qemu_log_mask(LOG_GUEST_ERROR,
                  "nikon-transfer: rejected streams source0=0x%08x "
                  "bytes0=%u source1=0x%08x bytes1=%u\n",
                  s->stream[0].source, 8 * s->stream[0].units64,
                  s->stream[1].source, 8 * s->stream[1].units64);
}

static void nikon_transfer_stream_complete(void *opaque)
{
    NikonTransferState *s = opaque;
    const char *reason = NULL;
    bool success, yuva107 = false;
    uint64_t rgba720_target = UINT64_MAX;

    if (!s->stream_running) {
        return;
    }
    success = nikon_transfer_stream_execute(s, &rgba720_target, &yuva107, &reason);
    if (!success) {
        nikon_transfer_stream_reject_log(s);
    }
    nikon_transfer_stream_clear(s);
    if (!success) {
        nikon_transfer_error(s, 0x10e0, 4, true, 1, reason);
        return;
    }
    /* Publish context only after successful execution; ACK retains it. */
    s->rgba720_target = rgba720_target;
    s->yuva107 = yuva107;
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
        s->wait_reported = false;
        return MEMTX_OK;
    }
    if (size != 4 || (index < 0 && setup < 0)) {
        return nikon_transfer_error(s, offset, size, false, 0,
                                    "transfer-read-not-supported");
    }
    *value = index >= 0 ? s->reg[index] : s->setup[setup];
    if (size == 4 && offset == 0x58 &&
        (s->stream_running || s->stream[0].bytes || s->stream[1].bytes) &&
        !s->stream_ready) {
        /* The original wait polls this bit until the stream completes; a
         * modelled completion that never arrives must report, not spin. */
        nikon_wait_stall(&s->wait_reported, offset, size, *value, 20000,
                         "transfer-stream-completion");
    }
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
            nikon_transfer_stream_reject_log(s);
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
        bool kick = offset == 0x1214 && value == TRANSFER_STREAM_KICK;

        if ((offset == 0x1214 || offset == 0x1298) &&
            (s->stream[0].bytes || s->stream[1].bytes) && !kick) {
            goto unsupported;
        }
        if (!kick && value != setup_values[setup] && !(setup == 2 && !value) &&
            !(offset == 0x1214 && value == 0xffff)) {
            goto unsupported;
        }
        if (kick) {
            /* Accepted while the streams are pending; the retained
             * configuration for 1214 must not be overwritten by the kick. */
            return MEMTX_OK;
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

    s->rgba720_target = UINT64_MAX;
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
