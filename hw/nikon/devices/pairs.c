/*
 * Finite paired controls, RAW processing and result publication.
 * Cold zero, retained readback, mask1 inhibition and W1C are virtual laws.
 * Only completed RAW DMA can produce numerical work and output events.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "qemu/host-utils.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "qemu/timer.h"
#include "migration/vmstate.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/nikon/devices.h"
#include "hw/nikon/paired.h"
#include "hw/nikon/pairedpixels.h"
#include "hw/nikon/gateinput.h"
#include "system/address-spaces.h"
#include "system/dma.h"

#define BANK_SIZE 0x5e00
OBJECT_DECLARE_SIMPLE_TYPE(NikonPairedEventBanksState, NIKON_PAIRED_EVENT_BANKS)

typedef enum NikonPairedRequestState {
    PAIRED_QUIESCENT,
    PAIRED_ARMED,
    PAIRED_AWAIT_INPUT,
    PAIRED_DRAIN_PENDING,
} NikonPairedRequestState;

typedef struct PairedPlane {
    uint32_t address, stride, row_bytes, height;
} PairedPlane;

typedef struct PairedJob {
    NikonPairedPixelConfig pixels;
    NikonPairedRawFrame source;
    /* Main Y/UV, half-resolution history/residual, grid. */
    PairedPlane plane[5];
    uint16_t *raw, *history, *residual;
    uint64_t generation;
    uint64_t epoch;
    uint64_t gate_generation;
    unsigned phase, slot;
    bool work, stream;
} PairedJob;

typedef struct NikonPairedEventBanksBank {
    NikonPairedEventBanksState *parent;
    MemoryRegion regs;
    qemu_irq irq;
    uint32_t config[BANK_SIZE / 4];
    uint32_t mask, pending, inputs;
    NikonPairedRequestState request;
    QEMUTimer *timer;
    PairedJob job;
    uint64_t consumed[4], table_written[10];
    uint64_t curve_written[2][3];
    uint32_t filter_written, radial_table_written;
    uint8_t denoise_written, detail_written, adaptive_written, radial_written;
    uint32_t history_address[4][2], result;
    uint64_t history_epoch[4];
    NikonRawEncoding history_encoding[4];
    unsigned phase, seen, refill, stop;
    bool active, published, finish_pending;
    unsigned index;
} NikonPairedEventBanksBank;

struct NikonPairedEventBanksState {
    SysBusDevice parent_obj;
    NikonPairedEventBanksBank bank[2];
    NikonPairedRawFrame raw[4];
    uint64_t source_epoch;
    char *calibration_path;
    DeviceState *gate;
    uint32_t calibration[BANK_SIZE / 4];
    bool calibration_loaded;
    uint64_t completed, rejected, cancelled, blocked, bytes;
    bool reported_error;
};

static void paired_try(NikonPairedEventBanksBank *b, hwaddr origin,
                       uint32_t value);

void nikon_paired_frame(DeviceState *dev, const NikonPairedRawFrame *frame)
{
    NikonPairedEventBanksState *s = NIKON_PAIRED_EVENT_BANKS(dev);

    if (!frame) {
        memset(s->raw, 0, sizeof(s->raw));
        s->source_epoch++;
    } else if (frame->slot < ARRAY_SIZE(s->raw)) {
        s->raw[frame->slot] = *frame;
    }
    if (frame && frame->generation) {
        for (unsigned i = 0; i < 2; i++) {
            paired_try(&s->bank[i], 0, 0);
        }
    }
}

static bool nikon_paired_event_banks_config(hwaddr offset, unsigned bank)
{
    /* SOURCE11 96cba..994c2 setters and 994e8 word-table publication.
     * These are retained configuration, not fabricated results. Gaps containing
     * 994e2 status consumers deliberately remain inaccessible.
     */
    static const struct {
        unsigned first, last;
    } ranges[] = {
        { 0x40, 0x50 }, { 0x94, 0xa0 }, { 0xb8, 0xcc },
        { 0xd4, 0xe4 }, { 0xf8, 0x110 }, { 0x11c, 0x144 },
        { 0x200, 0x210 }, { 0x218, 0x234 }, { 0x24c, 0x258 },
        { 0x300, 0x34c },
        { 0x364, 0x384 }, { 0x394, 0x3b4 }, { 0x3c4, 0x3c8 },
        { 0x3d8, 0x3e0 }, { 0x3e8, 0x3f0 },
        { 0x400, 0x414 }, { 0x420, 0x470 },
        { 0x488, 0x4c8 }, { 0x500, 0x678 }, { 0x700, 0x778 },
        { 0x900, 0x9b0 }, { 0x9b4, 0x9d8 },
        { 0xb00, 0xb60 }, { 0xb64, 0xbbc },
        { 0xd00, 0xe18 }, { 0xf00, 0xf2c },
        { 0x1100, 0x113c }, { 0x1200, 0x1244 },
        { 0x1300, 0x1320 }, { 0x1328, 0x1348 },
        { 0x1350, 0x1370 }, { 0x1378, 0x1398 },
        /* MAS1036bf88/bfae: 22*64B and14*68B CPU-published tables. */
        { 0x1400, 0x1d34 },
        { 0x1d38, 0x1e34 }, { 0x1f00, 0x1f40 }, { 0x1f48, 0x1f88 },
        { 0x1f90, 0x1fb0 }, { 0x1fb8, 0x1fd8 },
        { 0x1fe0, 0x2000 }, { 0x2008, 0x2028 }, { 0x2030, 0x2050 },
        { 0x2058, 0x2078 }, { 0x2080, 0x20a0 }, { 0x20a8, 0x20c8 },
        { 0x20d0, 0x20dc }, { 0x2100, 0x22fc },
        { 0x2400, 0x288c }, { 0x2890, 0x2c94 },
        { 0x3900, 0x3a98 }, { 0x3aa0, 0x3c38 },
        { 0x3c40, 0x3dd8 }, { 0x3de0, 0x3f78 },
        { 0x3f80, 0x4118 }, { 0x4120, 0x42b8 },
        { 0x42c0, 0x44c0 }, { 0x44c8, 0x45c8 },
        { 0x45d0, 0x46d0 }, { 0x46d8, 0x47d8 },
        { 0x47e0, 0x48e0 }, { 0x48e8, 0x49e8 },
        { 0x49f0, 0x4a70 }, { 0x4a74, 0x4af4 },
        { 0x4afc, 0x4b7c }, { 0x4b80, 0x4c00 },
        { 0x4c08, 0x4c88 }, { 0x4c8c, 0x4d0c },
        { 0x4d14, 0x4d94 }, { 0x4d98, 0x4e18 },
        { 0x4e1c, 0x4f1c }, { 0x4f20, 0x4fa0 },
        { 0x4fa4, 0x5024 }, { 0x502c, 0x512c },
        { 0x5134, 0x5334 }, { 0x533c, 0x553c },
        { 0x5544, 0x5744 }, { 0x574c, 0x594c },
        { 0x5954, 0x5a54 }, { 0x5a5c, 0x5adc },
        { 0x5ae4, 0x5b64 },
        { 0x5c00, 0x5c7c }, { 0x5c80, 0x5cfc },
        { 0x5d00, 0x5d7c }, { 0x5d80, 0x5dfc },
    };
    unsigned i;

    for (i = 0; i < ARRAY_SIZE(ranges); i++) {
        if (offset >= ranges[i].first && offset <= ranges[i].last) {
            return true;
        }
    }
    switch (offset) {
    case 0: case 4: case 0xc: case 0x20: case 0x84: case 0x88: case 0x8c:
    /*
     * MAS1036a2ac / SLA10096d20 write the full eight-field packet;
     * a8 retains byte2 without a modeled completion side effect.
     */
    case 0xa8: case 0xac: case 0xf0: case 0x67c:
        return true;
    case 0xd0: case 0xb0:
        return bank == 0;
    default:
        return false;
    }
}

/* Only opaque command shapes remain constrained. Setters retain full words;
 * source packing is a producer convention, not a proved hardware legal mask.
 */
static bool nikon_paired_event_banks_value(hwaddr offset, uint32_t value)
{
    switch (offset) {
    case 0: case 4:
        return value == 3;
    case 0xc:
        return value == 1;
    case 0xd0:
        return value <= 1;
    case 0xf0:
        return value == 0x8e || value == 0xfc400;
    default:
        return true;
    }
}

static void nikon_paired_event_banks_irq(NikonPairedEventBanksBank *b)
{
    qemu_set_irq(b->irq, !!(b->pending & ~b->mask));
}

static void paired_release(NikonPairedEventBanksBank *b)
{
    g_clear_pointer(&b->job.raw, g_free);
    g_clear_pointer(&b->job.history, g_free);
    g_clear_pointer(&b->job.residual, g_free);
    b->active = false;
}

static void paired_cancel(NikonPairedEventBanksBank *b)
{
    if (b->active) {
        timer_del(b->timer);
        paired_release(b);
        b->parent->cancelled++;
    }
}

static uint64_t paired_span(const PairedPlane *p)
{
    return (uint64_t)p->stride * p->height;
}

static bool paired_ram(uint32_t address, uint64_t bytes, bool write)
{
    if (!address || !bytes || bytes > 0x100000000ULL - address) {
        return false;
    }
    while (bytes) {
        hwaddr offset, length = bytes;
        MemoryRegion *mr = address_space_translate(&address_space_memory,
            address, &offset, &length, write, MEMTXATTRS_UNSPECIFIED);

        if (!length || !memory_region_is_ram(mr) ||
            !memory_access_is_direct(mr, write, MEMTXATTRS_UNSPECIFIED)) {
            return false;
        }
        bytes -= length;
        address += length;
    }
    return true;
}

static bool paired_overlap(uint32_t a, uint64_t as, uint32_t b, uint64_t bs)
{
    if (a < (uint64_t)b + bs && b < (uint64_t)a + as) {
        return true;
    }
    /* PA separation is insufficient when decoder aliases share backing. */
    while (as) {
        hwaddr ao, an = as;
        MemoryRegion *am = address_space_translate(&address_space_memory, a,
            &ao, &an, false, MEMTXATTRS_UNSPECIFIED);
        uint64_t remaining = bs;
        uint32_t address = b;

        if (!an || !memory_region_is_ram(am)) {
            return true;
        }
        uintptr_t ap = (uintptr_t)memory_region_get_ram_ptr(am) + ao;

        while (remaining) {
            hwaddr bo, bn = remaining;
            MemoryRegion *bm = address_space_translate(&address_space_memory,
                address, &bo, &bn, false, MEMTXATTRS_UNSPECIFIED);

            if (!bn || !memory_region_is_ram(bm)) {
                return true;
            }
            uintptr_t bp = (uintptr_t)memory_region_get_ram_ptr(bm) + bo;

            if (ap < bp + bn && bp < ap + an) {
                return true;
            }
            address += bn;
            remaining -= bn;
        }
        a += an;
        as -= an;
    }
    return false;
}

static bool paired_matrix_word(unsigned offset)
{
    return (offset >= 0xb00 && offset <= 0xb10) ||
           (offset >= 0xb64 && offset <= 0xb74) ||
           (offset >= 0xb94 && offset <= 0xba4);
}

/* Fields interpreted below, rather than opaque calibration profile words. */
static int paired_filter_word(unsigned offset)
{
    if (offset == 0x594) {
        return 0;
    }
    if (offset >= 0x598 && offset <= 0x5dc) {
        return 1 + (offset - 0x598) / 4;
    }
    return offset == 0x608 ? 19 : -1;
}

static bool paired_live_word(unsigned offset)
{
    if ((offset >= 0x708 && offset <= 0x720) ||
        (offset >= 0x660 && offset <= 0x678) ||
        (offset >= 0x73c && offset <= 0x758) ||
        (offset >= 0x75c && offset <= 0x778) ||
        (offset >= 0x724 && offset <= 0x738) ||
        (offset >= 0x1200 && offset <= 0x1244) ||
        paired_filter_word(offset) >= 0) {
        return true;
    }
    if (paired_matrix_word(offset) ||
        (offset >= 0x30c && offset <= 0x31c) ||
        (offset >= 0x218 && offset <= 0x234) ||
        (offset >= 0x320 && offset <= 0x32c) ||
        (offset >= 0x394 && offset <= 0x3b4) ||
        (offset >= 0x400 && offset <= 0x410) ||
        (offset >= 0x428 && offset <= 0x470) ||
        (offset >= 0x488 && offset <= 0x4c8) ||
        (offset >= 0xdf4 && offset <= 0xe10) ||
        (offset >= 0x2890 && offset <= 0x2c94)) {
        return true;
    }
    switch (offset) {
    case 0xc: case 0x84: case 0x88: case 0x8c: case 0xf0:
    case 0x40: case 0x44: case 0x48: case 0x4c: case 0x50:
    case 0x94: case 0x98: case 0x9c: case 0xa0: case 0xa8:
    case 0xb8: case 0xbc: case 0xc0: case 0xc4: case 0xc8: case 0xcc:
    case 0xd4: case 0xd8: case 0xdc: case 0xe0: case 0xe4:
    case 0xf8: case 0xfc: case 0x100: case 0x104: case 0x108:
    case 0x10c: case 0x110: case 0x11c: case 0x120: case 0x124:
    case 0x128: case 0x12c: case 0x130: case 0x134: case 0x138:
    case 0x13c: case 0x140: case 0x144:
    case 0x204: case 0x300: case 0x3d8: case 0x420: case 0x424:
    case 0x9cc: case 0x9d0: case 0x9d4:
        return true;
    default:
        return false;
    }
}

static void paired_matrix(NikonPairedEventBanksBank *b, unsigned offset,
                          unsigned bits, bool low_first, int16_t matrix[9])
{
    matrix[0] = sextract32(b->config[offset / 4], 0, bits);
    for (unsigned i = 0; i < 8; i++) {
        unsigned shift = ((i & 1) == low_first) ? 16 : 0;

        matrix[i + 1] = sextract32(b->config[(offset + 4 + (i / 2) * 4) / 4],
                                  shift, bits);
    }
}

static bool paired_profile_reject(NikonPairedEventBanksBank *b, unsigned offset)
{
    qemu_log_mask(LOG_GUEST_ERROR,
                  "nikon-paired: bank=%u rejected profile offset=0x%x "
                  "value=0x%x\n", b->index, offset, b->config[offset / 4]);
    return false;
}

static bool paired_filter(NikonPairedEventBanksBank *b,
                           NikonPairedPixelConfig *pixels)
{
    uint16_t fields[6];
    uint32_t control = b->config[0x594 / 4];
    uint32_t clipped = b->config[0x608 / 4];

    if (!b->filter_written) {
        return true; /* Explicit unpopulated synthetic neutral input. */
    }
    if (b->filter_written != (1U << 20) - 1 ||
        (control != 0 && control != 6) || clipped > 1) {
        return paired_profile_reject(b, 0x594);
    }
    for (unsigned pair = 0; pair < ARRAY_SIZE(fields); pair++) {
        unsigned offset = 0x598 + 12 * pair;
        uint32_t value = b->config[offset / 4];

        if ((value >> 16) != (value & 0xffff) ||
            b->config[(offset + 4) / 4] != value ||
            b->config[(offset + 8) / 4] != value) {
            return paired_profile_reject(b, offset);
        }
        fields[pair] = value;
    }
    /* Inspected monochrome records have zero-offset, unity-strength tails. */
    if (fields[4] || fields[5] != 16) {
        return paired_profile_reject(b, 0x5c8);
    }
    pixels->filter = (NikonPairedFilter) {
        .enabled = true, .refine = control == 6, .clipped = clipped,
        .retain = fields[0], .base = fields[1], .radius = fields[2],
        .slope = fields[3],
    };
    if (!nikon_paired_filter_valid(&pixels->filter)) {
        return paired_profile_reject(b, 0x598);
    }
    return true;
}

static bool paired_denoise(NikonPairedEventBanksBank *b,
                            NikonPairedPixelConfig *pixels)
{
    const uint32_t *c = b->config;
    uint32_t head = c[0x660 / 4], limits = c[0x664 / 4];
    uint32_t weights = c[0x668 / 4], retain = c[0x66c / 4];
    uint8_t r = retain;

    /* 103620e6/1036adac: complete scalar packet, no inferred enable bit. */
    if (b->denoise_written != 0x7f || (head >> 16) ||
        (limits & 0xf000f000U) || (uint8_t)(weights >> 8) != (uint8_t)weights ||
        retain != r * 0x01010101U || c[0x670 / 4] ||
        c[0x674 / 4] != 0x03020001 || c[0x678 / 4]) {
        return paired_profile_reject(b, 0x660);
    }
    pixels->denoise = (NikonPairedDenoise) {
        .threshold = head,
        .low = limits >> 16, .high = limits,
        .axial = weights >> 24, .diagonal = weights >> 16,
        .bias = weights, .retain = r,
    };
    if (!nikon_paired_denoise_valid(&pixels->denoise)) {
        return paired_profile_reject(b, 0x660);
    }
    return true;
}

static bool paired_detail(NikonPairedEventBanksBank *b,
                          NikonPairedPixelConfig *pixels)
{
    const uint32_t *c = b->config;
    uint32_t control = c[0x744 / 4], lanes = c[0x748 / 4];
    uint8_t lane = lanes;

    /* 102f734e/1036b49c..b554: q6..28 of the interpolated57-field family. */
    if (b->detail_written != 0xff || c[0x73c / 4] != 0x00060301 ||
        c[0x740 / 4] || (control & 0xff00ff00U) != 0x40000600 ||
        (lanes >> 24) != 4 || (lanes & 0xffffff) != lane * 0x010101U ||
        c[0x74c / 4] != 0x00040010 || c[0x750 / 4] ||
        c[0x754 / 4] != 0x001f0000 || c[0x758 / 4] != 0x1f100804) {
        return paired_profile_reject(b, 0x744);
    }
    pixels->detail = (NikonPairedDetail) {
        .limit = control >> 16, .strength = control, .lane = lane,
    };
    if (!nikon_paired_detail_valid(&pixels->detail)) {
        return paired_profile_reject(b, 0x744);
    }
    return true;
}

static bool paired_adaptive(NikonPairedEventBanksBank *b,
                            NikonPairedPixelConfig *pixels)
{
    const uint32_t *c = b->config;

    /* q29..50: whole source-shaped packet, including fixed/reserved fields. */
    if (b->adaptive_written != 0xff || (c[0x75c / 4] & 0xffff) != 30 ||
        (c[0x764 / 4] & 0xffff) != 4095 ||
        (c[0x768 / 4] & 0xffffff) != 0x1f0c0e ||
        (c[0x76c / 4] & 0xff00ffffU) != 0x04000000 ||
        c[0x770 / 4] != 0x08080808 || c[0x774 / 4] != 0x00000808 ||
        c[0x778 / 4] != 0x0af00020) {
        return paired_profile_reject(b, 0x75c);
    }
    pixels->adaptive = (NikonPairedAdaptive) {
        .base = c[0x75c / 4] >> 16,
        .slope = c[0x760 / 4] >> 16,
        .cap = c[0x760 / 4],
        .weight = c[0x764 / 4] >> 16,
        .precision = c[0x768 / 4] >> 24,
        .dose = c[0x76c / 4] >> 16,
    };
    if (!nikon_paired_adaptive_valid(&pixels->adaptive)) {
        return paired_profile_reject(b, 0x75c);
    }
    return true;
}

static bool paired_radial(NikonPairedEventBanksBank *b,
                           NikonPairedPixelConfig *pixels)
{
    const uint32_t *c = b->config;
    uint32_t coefficient = c[0x730 / 4], scale = c[0x734 / 4];
    uint32_t lanes = c[0x738 / 4];
    int red = sextract32(coefficient, 16, 10);
    int blue = sextract32(coefficient, 0, 10);

    /* 102bf33c/102e4bbe omit1200/1224 for NULL config[14]. This bounded
     * coefficient-only model requires that omission, not a zero LUT fallback.
     */
    if (b->radial_written != 0x3f || c[0x724 / 4] != 270) {
        return paired_profile_reject(b, 0x724);
    }
    if (c[0x72c / 4] != 0x00030205) {
        return paired_profile_reject(b, 0x72c);
    }
    if ((coefficient & 0xfc00fc00U) || red < -128 || red > 127 ||
        blue < -128 || blue > 127) {
        return paired_profile_reject(b, 0x730);
    }
    if ((scale >> 16) ||
        (scale >> 8) < 5 || (scale >> 8) > 8 ||
        (scale & 0xff) < 32 || (scale & 0xff) > 62) {
        return paired_profile_reject(b, 0x734);
    }
    if (lanes < 0x101 || lanes > 0x303 ||
        (lanes >> 8) != (lanes & 0xff)) {
        return paired_profile_reject(b, 0x738);
    }
    for (unsigned offset = 0x1200; offset <= 0x1244; offset += 4) {
        uint32_t bit = 1U << ((offset - 0x1200) / 4);

        if ((b->radial_table_written & bit) || c[offset / 4]) {
            return paired_profile_reject(b, offset);
        }
    }
    pixels->radial = (NikonPairedRadial) {
        .center_x = c[0x728 / 4] & 0xffff,
        .center_y = c[0x728 / 4] >> 16,
        .red = red, .blue = blue,
    };
    return true;
}

static bool paired_mode(NikonPairedEventBanksBank *b, PairedJob *job)
{
    uint32_t *c = b->config;
    bool chunked = c[0x88 / 4] == 2;
    unsigned lines = c[0x13c / 4] >> 16, count = c[0x140 / 4] >> 16;

    if ((c[0x88 / 4] != 0 && !chunked) ||
        c[0x8c / 4] != (chunked ? 0 : 3) ||
        c[0xa8 / 4] != (chunked ? 2 : 4) ||
        c[0x10c / 4] != chunked || c[0x138 / 4] != chunked) {
        return paired_profile_reject(b, 0x88);
    }
    if (chunked) {
        /* 102c0304: scheduling counts, not RAW height minus one. */
        if (!lines || !count || (c[0x13c / 4] & 0xffff) ||
            (c[0x140 / 4] & 0xffff) ||
            (c[0x94 / 4] != 4 * lines && c[0x94 / 4] != 4 * (lines + 1)) ||
            c[0x9c / 4] != (uint64_t)lines * count - 1) {
            return paired_profile_reject(b, 0x94);
        }
    } else if (c[0x94 / 4] || c[0x13c / 4] || c[0x140 / 4]) {
        return paired_profile_reject(b, 0x94);
    }
    /* 102be8a0 has independent work and main enable pairs. */
    if (c[0x408 / 4] > 1 || c[0x410 / 4] != c[0x408 / 4] ||
        c[0x124 / 4] != c[0x408 / 4] ||
        c[0x420 / 4] != 1 || c[0x428 / 4] != 1 ||
        (c[0x424 / 4] != (chunked ? 2 : 0) &&
         !(chunked && c[0x424 / 4] == 3 && !c[0x408 / 4])) ||
        c[0x3d8 / 4] != (c[0x408 / 4] ? 2 : 0)) {
        return paired_profile_reject(b, 0x424);
    }
    job->work = c[0x408 / 4];
    job->stream = c[0x424 / 4] == 3;
    if (c[0xe0 / 4] != (chunked ? 0x156 : 0x158)) {
        return paired_profile_reject(b, 0xe0);
    }
    return true;
}

static bool paired_aliases(NikonPairedEventBanksBank *b, unsigned primary,
                           unsigned mirror, unsigned words)
{
    for (unsigned i = 0; i < words; i++) {
        if (b->config[primary / 4 + i] != b->config[mirror / 4 + i]) {
            return paired_profile_reject(b, mirror + 4 * i);
        }
    }
    return true;
}

static bool paired_inactive(NikonPairedEventBanksBank *b, unsigned offset)
{
    unsigned selected = b->config[0xfc / 4] == 1;

    if (offset >= 0x430 && offset <= 0x470 && !b->config[0x408 / 4]) {
        return true;
    }
    if (offset >= 0x218 && offset <= 0x234) {
        return (offset - 0x218) / 16 != selected;
    }
    if (offset >= 0x310 && offset <= 0x32c) {
        return (offset - 0x310) / 16 != selected;
    }
    if (offset >= 0x398 && offset <= 0x3b4) {
        return (offset - 0x398) / 16 != selected;
    }
    if (offset >= 0x488 && offset <= 0x4c4) {
        return (offset - 0x488) / 32 != selected;
    }
    return false;
}

static uint32_t paired_source(NikonPairedEventBanksBank *b, unsigned phase)
{
    unsigned selected = b->config[0xfc / 4] == 1;

    return b->config[0x310 / 4 + 4 * selected + phase];
}

static unsigned paired_refill_mask(NikonPairedEventBanksBank *b)
{
    return 0x0f0fU << (4 * (b->config[0xfc / 4] == 1));
}

static bool paired_profile(NikonPairedEventBanksBank *b, PairedJob *job,
                            unsigned p)
{
    static const struct { unsigned offset; uint32_t value; } controls[] = {
        { 0x40, 5 }, { 0x4c, 1 }, { 0x50, 0 }, { 0x84, 1 },
        { 0x98, 0x400 }, { 0xa0, 3 },
        { 0xb8, 1 }, { 0xbc, 1 }, { 0xc0, 0 }, { 0xc8, 0 }, { 0xcc, 0 },
        { 0xd4, 0x0c060202 }, { 0xd8, 9 }, { 0xdc, 0x30300 },
        { 0xe4, 0 }, { 0xf0, 0x8e },
        { 0xf8, 1 },
        { 0x100, 1 }, { 0x104, 0 }, { 0x108, 3 },
        { 0x110, 0 }, { 0x11c, 1 }, { 0x120, 1 },
        { 0x128, 0 }, { 0x12c, 1 }, { 0x130, 0 }, { 0x134, 0 },
        { 0x144, 0 },
        { 0x300, 0 }, { 0x404, 1 }, { 0x42c, 3 },
        { 0xb14, 0x80000000 }, { 0xb60, 0x80000000 },
        { 0xb90, 0x80000000 },
    };
    uint32_t *c = b->config;
    NikonPairedPixelConfig *g = &job->pixels;
    unsigned hw, hh, selected = c[0xfc / 4] == 1;

    if (!b->parent->calibration_loaded) {
        return paired_profile_reject(b, 0);
    }
    /*
     * 102e4ce4/4d06 skip NULL table pointers. A wholly omitted table is
     * accepted only as an explicitly zero profile; a touched table must be
     * complete, including its zero words. Partial publication never qualifies.
     */
    for (unsigned table = 0, first = 0; table < 36; table++) {
        unsigned words = table < 22 ? 16 : 17;
        unsigned written = 0;
        bool nonzero = false;

        for (unsigned i = first; i < first + words; i++) {
            written += !!(b->table_written[i / 64] &
                           (UINT64_C(1) << (i % 64)));
            nonzero |= b->parent->calibration[0x1400 / 4 + i] != 0;
        }
        if ((written && written != words) || (!written && nonzero)) {
            return paired_profile_reject(b, 0x1400 + 4 * first);
        }
        first += words;
    }
    /*
     * This explicit virtual-camera profile does not claim arbitrary silicon
     * calibration algorithms. Every opaque field must match the input tuple;
     * the matrices excluded here actually participate in pixel computation.
     */
    for (unsigned offset = 0; offset < BANK_SIZE; offset += 4) {
        if (nikon_paired_event_banks_config(offset, b->index) &&
            !paired_live_word(offset) &&
            c[offset / 4] != b->parent->calibration[offset / 4]) {
            return paired_profile_reject(b, offset);
        }
    }
    for (unsigned i = 0; i < ARRAY_SIZE(controls); i++) {
        if (c[controls[i].offset / 4] != controls[i].value) {
            return paired_profile_reject(b, controls[i].offset);
        }
    }
    if (c[0xfc / 4] > 1) {
        return paired_profile_reject(b, 0xfc);
    }
    if (!paired_mode(b, job) || !paired_filter(b, g) || !paired_denoise(b, g) ||
        !paired_detail(b, g) || !paired_adaptive(b, g) || !paired_radial(b, g)) {
        return false;
    }
    for (unsigned i = 0x330; i <= 0x34c; i += 4) {
        if (c[i / 4]) {
            return paired_profile_reject(b, i);
        }
    }
    /* The selected front/back RAW views and shared work ports must agree. */
    if (!paired_aliases(b, 0x310 + 16 * selected, 0x218 + 16 * selected, 4) ||
        (job->work && !paired_aliases(b, 0x430, 0x450, 8))) {
        return false;
    }
    g->width = c[0x48 / 4] >> 16;
    g->height = c[0x48 / 4] & 0xffff;
    g->x = c[0x40c / 4] & 0xffff;
    g->y = c[0x400 / 4] >> 16;
    g->output_width = c[0x40c / 4] >> 16;
    g->output_height = c[0x400 / 4] & 0xffff;
    g->grid_width = 60;
    g->grid_height = 34;
    /* 103646fe/10363bc0 construct four gain/offset/pedestal lanes and a cap. */
    if (c[0x704 / 4] || (c[0x700 / 4] && c[0x700 / 4] != 0x101)) {
        return paired_profile_reject(b, 0x700);
    }
    g->quad_enabled = c[0x700 / 4] == 0x101;
    for (unsigned i = 0; i < 4; i++) {
        unsigned shift = (i % 2) ? 0 : 16;
        uint32_t offset = c[0x710 / 4 + i / 2] >> shift;

        g->quad_gain[i] = c[0x708 / 4 + i / 2] >> shift;
        g->quad_offset[i] = sextract32(offset, 0, 13);
        g->quad_pedestal[i] = c[0x718 / 4 + i / 2] >> shift;
        if (g->quad_gain[i] > 16383 || (offset & 0xe000) ||
            (!g->quad_enabled && (g->quad_gain[i] || offset ||
                                  g->quad_pedestal[i]))) {
            return paired_profile_reject(b, 0x708 + 4 * (i / 2));
        }
    }
    if (c[0x720 / 4] > 32767 || (!g->quad_enabled && c[0x720 / 4])) {
        return paired_profile_reject(b, 0x720);
    }
    g->quad_limit = c[0x720 / 4];
    if (!g->width || g->width > 4096 || g->height < 34 ||
        g->height > 4096 || g->width < 60 ||
        !g->output_width || !g->output_height ||
        ((g->output_width | g->output_height) & 1) ||
        g->x + g->output_width > g->width ||
        g->y + g->output_height > g->height ||
        (c[0x88 / 4] == 0 && c[0x9c / 4] != g->height - 1) ||
        c[0xc4 / 4] != g->width ||
        c[0x204 / 4] != ((g->height << 16) | g->width)) {
        return paired_profile_reject(b, 0x48);
    }
    hw = DIV_ROUND_UP(g->width, 2);
    hh = DIV_ROUND_UP(g->height, 2);
    job->plane[0] = (PairedPlane) {
        c[0x488 / 4 + 8 * selected + p], c[0x4c8 / 4],
        g->output_width, g->output_height
    };
    job->plane[1] = (PairedPlane) {
        c[0x498 / 4 + 8 * selected + p], c[0x4c8 / 4],
        g->output_width, g->output_height / 2
    };
    if (job->work) {
        job->plane[2] = (PairedPlane) {
            c[0x430 / 4 + 8 * selected + p], c[0x470 / 4], hw * 2, hh
        };
        job->plane[3] = (PairedPlane) {
            c[0x440 / 4 + 8 * selected + p], c[0x470 / 4], hw * 2, hh
        };
    }
    job->plane[4] = (PairedPlane) { c[0x398 / 4 + 4 * selected + p],
                                   c[0x394 / 4], 90, 34 };
    if ((job->stream ?
         (job->plane[0].address != 0x40000000 ||
          job->plane[1].address != 0x40008000 ||
          job->plane[0].stride < g->output_width ||
          (job->plane[0].stride & 63)) :
         (uint64_t)job->plane[0].address + paired_span(&job->plane[0]) !=
             job->plane[1].address) ||
        (job->work &&
         (uint64_t)job->plane[2].address + paired_span(&job->plane[2]) !=
             job->plane[3].address)) {
        return paired_profile_reject(b, 0x400);
    }
    paired_matrix(b, 0xb00, 13, false, g->a);
    paired_matrix(b, 0xb64, 12, false, g->b);
    paired_matrix(b, 0xb94, 13, true, g->c);
    /* 1036bb04 packs three u16 gains and four independent control bytes. */
    if ((c[0x9cc / 4] & ~0x01010101U) || (c[0x9d4 / 4] & 0xffff0000U)) {
        return paired_profile_reject(b, 0x9cc);
    }
    g->gain[0] = c[0x9d0 / 4] >> 16;
    g->gain[1] = c[0x9d0 / 4];
    g->gain[2] = c[0x9d4 / 4];
    for (unsigned i = 0; i < 3; i++) {
        g->gain_enabled[i] = (c[0x9cc / 4] & (1U << 24)) &&
                            (c[0x9cc / 4] & (1U << (16 - 8 * i)));
    }
    for (unsigned table = 0; table < 2; table++) {
        unsigned written = ctpop64(b->curve_written[table][0]) +
                           ctpop64(b->curve_written[table][1]) +
                           ctpop64(b->curve_written[table][2]);
        unsigned base = 0x2890 + 516 * table;

        if (written && (written != 129 || c[(base + 512) / 4] >> 16)) {
            return paired_profile_reject(b, base);
        }
        g->curve_enabled[table] = written != 0;
        for (unsigned i = 0; i < 257; i++) {
            g->curve[table][i] = c[base / 4 + i / 2] >> (16 * (i % 2));
        }
    }
    /* 1036c862 publishes three full LFSR records and two packed limits. */
    if ((c[0xdf4 / 4] | c[0xe04 / 4] | c[0xe08 / 4] |
         c[0xe0c / 4]) & 0xff000000U) {
        return paired_profile_reject(b, 0xdf4);
    }
    g->noise_negative = c[0xe10 / 4] >> 16;
    g->noise_positive = c[0xe10 / 4];
    for (unsigned i = 0; i < 3; i++) {
        unsigned shift = 16 - 8 * i;

        g->noise[i] = (NikonPairedNoise) {
            .feedback = c[0xdf8 / 4 + i],
            .enabled = c[0xdf4 / 4] >> shift,
            .gain = c[0xe0c / 4] >> shift,
            .shift = c[0xe04 / 4] >> shift,
            .offset = c[0xe08 / 4] >> shift,
        };
    }
    if (!nikon_paired_noise_valid(g)) {
        return paired_profile_reject(b, 0xdf4);
    }
    /* Reject controls/reserved bits instead of silently truncating matrices. */
    for (unsigned m = 0; m < 3; m++) {
        unsigned base = m == 0 ? 0xb00 : m == 1 ? 0xb64 : 0xb94;
        unsigned bits = m == 1 ? 12 : 13;
        uint32_t mask = (1U << bits) - 1;

        if (m != 0) {
            int16_t first = m == 1 ? g->b[0] : g->c[0];

            if (c[base / 4] != (uint32_t)(int32_t)first) {
                return paired_profile_reject(b, base);
            }
        } else if (c[base / 4] & ~mask) {
            return paired_profile_reject(b, base);
        }
        for (unsigned i = 1; i < 5; i++) {
            if (c[base / 4 + i] & ~(mask | (mask << 16))) {
                return paired_profile_reject(b, base + 4 * i);
            }
        }
    }
    return true;
}

static bool paired_targets(PairedJob *job, const NikonPairedRawFrame *input)
{
    for (unsigned i = 0; i < 5; i++) {
        PairedPlane *p = &job->plane[i];
        uint64_t span = paired_span(p);

        if ((job->stream && i < 2) || (!job->work && (i == 2 || i == 3))) {
            continue;
        }
        if ((p->address & 3) || p->stride < p->row_bytes ||
            !paired_ram(p->address, span, true) ||
            paired_overlap(p->address, span, input->address,
                           (uint64_t)input->stride * input->height)) {
            return false;
        }
        for (unsigned j = 0; j < i; j++) {
            if ((job->stream && j < 2) || !paired_span(&job->plane[j])) {
                continue;
            }
            if (paired_overlap(p->address, span, job->plane[j].address,
                               paired_span(&job->plane[j]))) {
                return false;
            }
        }
    }
    return true;
}

static bool paired_snapshot(NikonPairedEventBanksBank *b,
                             const NikonPairedRawFrame *frame)
{
    PairedJob *j = &b->job;
    NikonPairedPixelConfig *g = &j->pixels;
    unsigned x0 = b->config[0x44 / 4] >> 16;
    unsigned y0 = b->config[0x44 / 4] & 0xffff;
    size_t pixels = (size_t)g->width * g->height;
    unsigned row_bytes;
    g_autofree uint8_t *row = NULL;

    if (frame->bits != 12 || (frame->width & 1) ||
        (unsigned)frame->encoding > NIKON_RAW_BGGR ||
        (frame->encoding != NIKON_RAW_MONO && ((g->width | g->height) & 1)) ||
        frame->width > 4096 || frame->height > 4096 ||
        x0 + g->width > frame->width || y0 + g->height > frame->height ||
        frame->stride != b->config[0x30c / 4] ||
        frame->stride < (uint64_t)frame->width * 12 / 8 ||
        !paired_ram(frame->address, (uint64_t)frame->stride * frame->height,
                    false) || !paired_targets(j, frame)) {
        return false;
    }
    row_bytes = frame->width * 12 / 8;
    g->encoding = frame->encoding == NIKON_RAW_MONO ? NIKON_RAW_MONO :
        1 + ((frame->encoding - 1) ^ (x0 & 1) ^ ((y0 & 1) << 1));
    g->radial.center_x -= (int32_t)x0;
    g->radial.center_y -= (int32_t)y0;
    row = g_malloc(MAX(row_bytes, j->plane[2].row_bytes));
    j->raw = g_new(uint16_t, pixels);
    for (unsigned y = 0; y < g->height; y++) {
        if (dma_memory_read(&address_space_memory,
            (uint64_t)frame->address + (y + y0) * frame->stride,
            row, row_bytes, MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
            return false;
        }
        for (unsigned x = 0; x < g->width; x++) {
            unsigned bit = (x + x0) * 12, byte = bit / 8;
            uint32_t value = row[byte] | row[byte + 1] << 8;

            j->raw[y * g->width + x] = (value >> (bit % 8)) & 4095;
        }
    }
    /* Only successful publication qualifies RAM as numerical history. */
    for (unsigned h = 0; j->work && h < 4; h++) {
        if (b->history_epoch[h] != b->parent->source_epoch ||
            b->history_encoding[h] != g->encoding ||
            b->history_address[h][0] != j->plane[2].address ||
            b->history_address[h][1] != j->plane[3].address) {
            continue;
        }
        unsigned width = DIV_ROUND_UP(g->width, 2);
        unsigned height = DIV_ROUND_UP(g->height, 2);

        j->history = g_new(uint16_t, (size_t)width * height);
        j->residual = g_new(uint16_t, (size_t)width * height);
        for (unsigned plane = 0; plane < 2; plane++) {
            uint16_t *out = plane ? j->residual : j->history;
            PairedPlane *p = &j->plane[2 + plane];

            for (unsigned y = 0; y < height; y++) {
                if (dma_memory_read(&address_space_memory,
                    (uint64_t)p->address + y * p->stride, row, width * 2,
                    MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
                    return false;
                }
                for (unsigned x = 0; x < width; x++) {
                    out[y * width + x] = lduw_le_p(row + 2 * x);
                }
            }
        }
        break;
    }
    j->generation = frame->generation;
    j->source = *frame;
    j->epoch = b->parent->source_epoch;
    j->slot = frame->slot;
    j->phase = b->phase;
    return true;
}

static bool paired_batch(NikonPairedEventBanksBank *b,
                          const NikonPairedRawFrame *frame)
{
    PairedJob batch[4] = { 0 };
    uint64_t input_span = (uint64_t)frame->stride * frame->height;

    /* Validate the whole enabled batch, including phases not consumed yet. */
    for (unsigned p = 0; p < 4; p++) {
        uint32_t address = paired_source(b, p);

        if (!paired_profile(b, &batch[p], p) ||
            !paired_targets(&batch[p], frame) ||
            !paired_ram(address, input_span, false)) {
            return false;
        }
        for (unsigned q = 0; q < p; q++) {
            if (paired_overlap(address, input_span,
                               paired_source(b, q), input_span)) {
                return false;
            }
        }
        for (unsigned i = 0; i < 5; i++) {
            PairedPlane *a = &batch[p].plane[i];

            if ((batch[p].stream && i < 2) || !paired_span(a)) {
                continue;
            }
            for (unsigned q = 0; q < 4; q++) {
                if (paired_overlap(a->address, paired_span(a),
                    paired_source(b, q), input_span)) {
                    return false;
                }
            }
            for (unsigned q = 0; q < p; q++) {
                for (unsigned j = 0; j < 5; j++) {
                    PairedPlane *other = &batch[q].plane[j];

                    if ((batch[q].stream && j < 2) || !paired_span(other)) {
                        continue;
                    }
                    if (paired_overlap(a->address, paired_span(a),
                                       other->address, paired_span(other)) &&
                        (i != j || i == 4 ||
                         memcmp(a, other, sizeof(*a)))) {
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

static bool paired_peer_busy(NikonPairedEventBanksBank *b)
{
    NikonPairedEventBanksBank *other = &b->parent->bank[1 - b->index];

    if (!other->active && !(other->pending & 0x1000)) {
        return false;
    }
    if (b->job.stream && other->job.stream) {
        return true;
    }
    for (unsigned i = 0; i < 5; i++) {
        for (unsigned j = 0; j < 5; j++) {
            if ((b->job.stream && i < 2) || (other->job.stream && j < 2) ||
                !paired_span(&b->job.plane[i]) ||
                !paired_span(&other->job.plane[j])) {
                continue;
            }
            if (paired_overlap(b->job.plane[i].address,
                paired_span(&b->job.plane[i]), other->job.plane[j].address,
                paired_span(&other->job.plane[j]))) {
                return true;
            }
        }
    }
    return false;
}

static void paired_finish(NikonPairedEventBanksBank *b)
{
    if (b->request == PAIRED_DRAIN_PENDING && !b->active && b->published &&
        !b->pending && !b->finish_pending && !b->stop) {
        /* Real admitted work has drained; this does not count another frame. */
        b->request = PAIRED_QUIESCENT;
        b->finish_pending = true;
        b->pending |= 2;
        nikon_paired_event_banks_irq(b);
    }
}

static void paired_try(NikonPairedEventBanksBank *b, hwaddr origin,
                       uint32_t value)
{
    NikonPairedEventBanksState *s = b->parent;
    NikonPairedRawFrame *frame = NULL;
    uint32_t address = paired_source(b, b->phase);

    if (b->active || b->stop || (b->refill & paired_refill_mask(b)) || b->pending ||
        b->request != PAIRED_AWAIT_INPUT) {
        return;
    }
    for (unsigned i = 0; i < ARRAY_SIZE(s->raw); i++) {
        if (s->raw[i].generation > b->consumed[i] &&
            s->raw[i].address == address) {
            frame = &s->raw[i];
            break;
        }
    }
    if (!frame) {
        return;
    }
    memset(&b->job, 0, sizeof(b->job));
    if (!paired_profile(b, &b->job, b->phase) || !paired_batch(b, frame)) {
        goto reject;
    }
    if (b->job.stream) {
        NikonGateInputShape shape = {
            b->job.pixels.output_width, b->job.pixels.output_height,
            b->job.plane[0].stride
        };
        NikonGateInputStatus status = s->gate ?
            nikon_gate_input_status(s->gate, &shape, &b->job.gate_generation) :
            NIKON_GATE_INPUT_UNSUPPORTED;

        if (status == NIKON_GATE_INPUT_UNSUPPORTED) {
            goto reject;
        }
        if (status == NIKON_GATE_INPUT_BUSY) {
            s->blocked++;
            return;
        }
    }
    if (paired_peer_busy(b)) {
        s->blocked++;
        return;
    }
    if (!paired_snapshot(b, frame)) {
        goto reject;
    }
    b->active = true;
    timer_mod(b->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000000);
    return;

reject:
    paired_release(b);
    s->rejected++;
    qemu_log_mask(LOG_GUEST_ERROR, "nikon-paired: bank=%u rejected RAW job\n",
                  b->index);
    if (origin) {
        nikon_access_error(&s->reported_error, origin, 4, true, value,
                           "paired-frame-profile-or-memory-not-supported");
    }
}

static bool paired_write_rows(const PairedPlane *p, const uint8_t *data)
{
    for (unsigned y = 0; y < p->height; y++) {
        if (dma_memory_write(&address_space_memory,
            (uint64_t)p->address + y * p->stride, data + y * p->row_bytes,
            p->row_bytes, MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
            return false;
        }
    }
    return true;
}

static void paired_complete(void *opaque)
{
    NikonPairedEventBanksBank *b = opaque;
    NikonPairedEventBanksState *s = b->parent;
    PairedJob *j = &b->job;
    NikonPairedPixelConfig *g = &j->pixels;
    size_t half = DIV_ROUND_UP(g->width, 2) * DIV_ROUND_UP(g->height, 2);
    size_t main = (size_t)g->output_width * g->output_height;
    g_autofree uint16_t *luma = g_new(uint16_t, half);
    g_autofree uint16_t *residual = g_new(uint16_t, half);
    g_autofree uint16_t *grid = g_new(uint16_t, 60 * 34);
    g_autofree uint8_t *y = g_malloc(main), *uv = g_malloc(main / 2);
    g_autofree uint8_t *work = g_malloc(half * 4);
    g_autofree uint8_t *packed = g_malloc(90 * 34);
    bool success = nikon_paired_pixels(g, j->raw, j->history, j->residual,
                                        luma, residual, grid, y, uv);

    /* A decoder change may introduce an alias after admission. */
    success &= paired_batch(b, &j->source) && !paired_peer_busy(b);
    if (success && j->stream) {
        NikonGateInputShape shape = {
            g->output_width, g->output_height, j->plane[0].stride
        };
        uint64_t generation;

        success = nikon_gate_input_status(s->gate, &shape, &generation) ==
                      NIKON_GATE_INPUT_READY &&
                  generation == j->gate_generation;
    }

    for (unsigned i = 0; i < 5 && success; i++) {
        if ((j->stream && i < 2) || !paired_span(&j->plane[i])) {
            continue;
        }
        success = paired_ram(j->plane[i].address, paired_span(&j->plane[i]),
                              true);
    }
    if (success) {
        for (unsigned i = 0; i < half; i++) {
            stw_le_p(work + 2 * i, luma[i]);
            stw_le_p(work + 2 * half + 2 * i, residual[i]);
        }
        for (unsigned i = 0; i < 60 * 34; i += 2) {
            unsigned byte = 3 * (i / 2);

            packed[byte] = grid[i];
            packed[byte + 1] = (grid[i] >> 8) | (grid[i + 1] << 4);
            packed[byte + 2] = grid[i + 1] >> 4;
        }
        success = (j->stream ||
                   (paired_write_rows(&j->plane[0], y) &&
                    paired_write_rows(&j->plane[1], uv))) &&
                  (!j->work ||
                   (paired_write_rows(&j->plane[2], work) &&
                    paired_write_rows(&j->plane[3], work + 2 * half))) &&
                  paired_write_rows(&j->plane[4], packed);
        if (success && j->stream) {
            NikonGateInputShape shape = {
                g->output_width, g->output_height, j->plane[0].stride
            };

            success = nikon_gate_input_publish(s->gate, j->gate_generation,
                                                &shape, y, main, uv, main / 2);
        }
    }
    if (success) {
        b->consumed[j->slot] = j->generation;
        if (j->work && j->epoch == s->source_epoch) {
            b->history_address[j->phase][0] = j->plane[2].address;
            b->history_address[j->phase][1] = j->plane[3].address;
            b->history_epoch[j->phase] = j->epoch;
            b->history_encoding[j->phase] = g->encoding;
        }
        b->result += main;
        b->seen |= 1U << j->phase;
        b->phase = (j->phase + 1) % 4;
        b->published = true;
        s->completed++;
        s->bytes += main + main / 2 + (j->work ? half * 4 : 0) + 90 * 34;
        b->pending |= 0x1002;
        if (b->seen == 15) {
            /* Four actual reads mark a ring traversal, not a stop request. */
            b->pending |= 0x400;
            b->seen = 0;
        }
    } else {
        s->rejected++;
    }
    paired_release(b);
    if (!success) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "nikon-paired: bank=%u asynchronous pixel job failed\n",
                      b->index);
        paired_try(&s->bank[1 - b->index], 0, 0);
    }
    nikon_paired_event_banks_irq(b);
}

static void nikon_paired_event_banks_input(void *opaque, int index, int level)
{
    NikonPairedEventBanksState *s = opaque;
    NikonPairedEventBanksBank *b = &s->bank[index / 32];
    uint32_t bit = 1U << (index % 32);
    bool rising = level && !(b->inputs & bit);

    b->inputs = (b->inputs & ~bit) | (level ? bit : 0);
    if (rising) {
        b->pending |= bit;
        nikon_paired_event_banks_irq(b);
    }
}

static MemTxResult nikon_paired_event_banks_error(NikonPairedEventBanksBank *b, hwaddr offset,
                                 unsigned size, bool write, uint64_t value)
{
    nikon_access_error(&b->parent->reported_error,
                      b->regs.addr + offset,
                      size, write, value,
                      "paired-event-banks-access-or-command-not-supported");
    return MEMTX_ERROR;
}

static MemTxResult nikon_paired_event_banks_read(void *opaque, hwaddr offset,
                                uint64_t *value, unsigned size,
                                MemTxAttrs attrs)
{
    NikonPairedEventBanksBank *b = opaque;

    *value = 0;
    if (size != 4 || (offset & 3)) {
        return nikon_paired_event_banks_error(b, offset, size, false, 0);
    }
    if (offset == 0x24) {
        *value = b->pending;
    } else if (offset == 0xe8) {
        *value = b->mask;
    } else if (offset == 0x18) {
        /* The original consumer distinguishes only zero from nonzero. */
        *value = b->request == PAIRED_AWAIT_INPUT ||
                 b->request == PAIRED_DRAIN_PENDING;
    } else if (offset == 0x118) {
        /* Additive work count is a finite unit, not a physical statistic. */
        *value = b->result;
    } else if (nikon_paired_event_banks_config(offset, b->index)) {
        *value = b->config[offset / 4];
    } else {
        return nikon_paired_event_banks_error(b, offset, size, false, 0);
    }
    return MEMTX_OK;
}

static MemTxResult nikon_paired_event_banks_write(void *opaque, hwaddr offset,
                                 uint64_t value, unsigned size,
                                 MemTxAttrs attrs)
{
    NikonPairedEventBanksBank *b = opaque;

    if (size != 4 || (offset & 3)) {
        return nikon_paired_event_banks_error(b, offset, size, true, value);
    }
    if (offset == 0x10 || offset == 8) {
        NikonPairedRequestState required, next;
        unsigned mask = paired_refill_mask(b);
        unsigned refill = b->refill & mask;

        /*
         * 1036a49a kicks independently of descriptor replacement. Retain a
         * complete unchanged ring, or commit both views of four newly published
         * addresses. A partial update blocks even when values are unchanged.
         */
        if (offset == 0x10 && value == 1 &&
            b->request == PAIRED_AWAIT_INPUT) {
            if (refill == mask && !b->active && !b->stop) {
                b->refill &= ~mask;
            }
            paired_try(b, b->regs.addr + offset, value);
            return MEMTX_OK;
        }
        /*
         * MAS10369faa arms with 10=1 before 08=1. Admission returns to the
         * original software notification path; it is not a completion IRQ.
         */
        if (offset == 0x10) {
            required = PAIRED_QUIESCENT;
            next = PAIRED_ARMED;
        } else {
            required = PAIRED_ARMED;
            next = PAIRED_AWAIT_INPUT;
        }
        if (value != 1 || b->request != required || b->stop) {
            qemu_log_mask(LOG_GUEST_ERROR,
                "nikon-paired: bank=%u control=0x%" HWADDR_PRIx
                " request=%u phase=%u seen=0x%x refill=0x%x "
                "active=%u pending=0x%x\n", b->index, offset, b->request,
                b->phase, b->seen, b->refill, b->active,
                b->pending);
            return nikon_paired_event_banks_error(b, offset, size, true, value);
        }
        b->request = next;
        if (offset == 8) {
            b->phase = b->seen = b->refill = 0;
            b->result = 0;
            b->published = b->finish_pending = false;
            paired_try(b, b->regs.addr + offset, value);
        }
        return MEMTX_OK;
    }
    if (offset == 0x54 || offset == 0x60 || offset == 0x6c || offset == 0x78) {
        static const unsigned stop[] = { 0x54, 0x60, 0x6c, 0x78 };

        if (value != 1 || b->stop < 2 || b->stop > 5 ||
            offset != stop[b->stop - 2]) {
            return nikon_paired_event_banks_error(b, offset, size, true, value);
        }
        if (++b->stop == 6) {
            /*
             * All real owned work was cancelled before admitting these stops.
             * Bit7 is the finite summary of the aggregate stop-detail packet.
             */
            b->stop = 0;
            b->request = PAIRED_QUIESCENT;
            b->refill = 0;
            b->pending |= 0x014a0080;
            nikon_paired_event_banks_irq(b);
        }
        return MEMTX_OK;
    }
    if (offset == 0x24) {
        b->pending &= ~value;
        paired_finish(b);
        for (unsigned i = 0; i < 2; i++) {
            paired_try(&b->parent->bank[i], b->regs.addr + offset, value);
        }
    } else if (offset == 0xe8) {
        b->mask = value;
    } else if (nikon_paired_event_banks_config(offset, b->index) &&
               nikon_paired_event_banks_value(offset, value)) {
        bool changed = b->config[offset / 4] != value;
        int filter = paired_filter_word(offset);

        if (offset == 0xfc && changed &&
            b->request == PAIRED_AWAIT_INPUT && b->seen) {
            return nikon_paired_event_banks_error(b, offset, size, true, value);
        }
        if (offset >= 0x1400 && offset < 0x1d38) {
            unsigned word = (offset - 0x1400) / 4;
            uint64_t bit = UINT64_C(1) << (word % 64);

            changed |= !(b->table_written[word / 64] & bit);
            b->table_written[word / 64] |= bit;
        }
        if (offset >= 0x2890 && offset <= 0x2c94) {
            unsigned table = (offset - 0x2890) / 516;
            unsigned word = ((offset - 0x2890) % 516) / 4;
            uint64_t bit = UINT64_C(1) << (word % 64);

            changed |= !(b->curve_written[table][word / 64] & bit);
            b->curve_written[table][word / 64] |= bit;
        }
        if (filter >= 0) {
            uint32_t bit = 1U << filter;

            changed |= !(b->filter_written & bit);
            b->filter_written |= bit;
        }
        if (b->request == PAIRED_AWAIT_INPUT &&
            ((offset >= 0x218 && offset <= 0x234) ||
             (offset >= 0x310 && offset <= 0x32c))) {
            unsigned bit = offset < 0x310 ? (offset - 0x218) / 4 :
                                           8 + (offset - 0x310) / 4;

            b->refill |= 1U << bit;
            /* Publication starts on a write, including a same-value write. */
            changed = true;
        }
        if (offset >= 0x660 && offset <= 0x678) {
            uint8_t bit = 1U << ((offset - 0x660) / 4);

            changed |= !(b->denoise_written & bit);
            b->denoise_written |= bit;
        }
        if (offset >= 0x73c && offset <= 0x758) {
            uint8_t bit = 1U << ((offset - 0x73c) / 4);

            changed |= !(b->detail_written & bit);
            b->detail_written |= bit;
        }
        if (offset >= 0x75c && offset <= 0x778) {
            uint8_t bit = 1U << ((offset - 0x75c) / 4);

            changed |= !(b->adaptive_written & bit);
            b->adaptive_written |= bit;
        }
        if (offset >= 0x724 && offset <= 0x738) {
            uint8_t bit = 1U << ((offset - 0x724) / 4);

            changed |= !(b->radial_written & bit);
            b->radial_written |= bit;
        }
        if (offset >= 0x1200 && offset <= 0x1244) {
            uint32_t bit = 1U << ((offset - 0x1200) / 4);

            changed |= !(b->radial_table_written & bit);
            b->radial_table_written |= bit;
        }
        if (changed && offset != 0xc && offset != 0xf0 && offset != 0x84 &&
            !paired_inactive(b, offset)) {
            paired_cancel(b);
            /* RAW addresses and main-only color controls preserve history. */
            if (offset != 0xfc &&
                !(offset >= 0x708 && offset <= 0x720) &&
                !(offset >= 0x724 && offset <= 0x738) &&
                !(offset >= 0x1200 && offset <= 0x1244) &&
                !(offset >= 0xdf4 && offset <= 0xe10) &&
                !(offset >= 0x218 && offset <= 0x234) &&
                !(offset >= 0x310 && offset <= 0x32c) &&
                !(offset >= 0x398 && offset <= 0x3b4) &&
                !(offset >= 0x488 && offset <= 0x4c4)) {
                memset(b->history_address, 0, sizeof(b->history_address));
            }
        }
        if (offset == 0x84 && !value &&
            b->request != PAIRED_QUIESCENT) {
            paired_cancel(b);
            b->stop = 1;
        }
        if (offset == 0xf0 && value == 0xfc400 && b->stop == 1) {
            b->stop = 2;
        }
        if (offset == 0xc && b->request == PAIRED_AWAIT_INPUT) {
            /* 10369fc4: literal203800ec minus e0 is the finish request. */
            b->request = PAIRED_DRAIN_PENDING;
        }
        /* Opaque control inputs/configuration never clear pending or fields. */
        b->config[offset / 4] = value;
        paired_finish(b);
        /* Configuration/cancel may have released shared output ownership. */
        paired_try(&b->parent->bank[1 - b->index],
                   b->regs.addr + offset, value);
    } else {
        return nikon_paired_event_banks_error(b, offset, size, true, value);
    }
    nikon_paired_event_banks_irq(b);
    return MEMTX_OK;
}

static const MemoryRegionOps nikon_paired_event_banks_ops = {
    .read_with_attrs = nikon_paired_event_banks_read,
    .write_with_attrs = nikon_paired_event_banks_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
};

static void nikon_paired_event_banks_reset(DeviceState *dev)
{
    NikonPairedEventBanksState *s = NIKON_PAIRED_EVENT_BANKS(dev);
    unsigned i;

    for (i = 0; i < 2; i++) {
        paired_cancel(&s->bank[i]);
        memset(s->bank[i].config, 0, sizeof(s->bank[i].config));
        memset(s->bank[i].consumed, 0, sizeof(s->bank[i].consumed));
        memset(s->bank[i].table_written, 0, sizeof(s->bank[i].table_written));
        memset(s->bank[i].curve_written, 0, sizeof(s->bank[i].curve_written));
        s->bank[i].filter_written = 0;
        s->bank[i].denoise_written = 0;
        s->bank[i].detail_written = 0;
        s->bank[i].adaptive_written = 0;
        s->bank[i].radial_written = 0;
        s->bank[i].radial_table_written = 0;
        memset(s->bank[i].history_address, 0,
               sizeof(s->bank[i].history_address));
        memset(s->bank[i].history_epoch, 0, sizeof(s->bank[i].history_epoch));
        memset(s->bank[i].history_encoding, 0,
               sizeof(s->bank[i].history_encoding));
        s->bank[i].mask = s->bank[i].pending = 0;
        s->bank[i].request = PAIRED_QUIESCENT;
        s->bank[i].phase = s->bank[i].seen = s->bank[i].refill = 0;
        s->bank[i].stop = s->bank[i].result = 0;
        s->bank[i].published = false;
        s->bank[i].finish_pending = false;
        /* Keep external levels; reset must not generate a new rising edge. */
        nikon_paired_event_banks_irq(&s->bank[i]);
    }
    s->reported_error = false;
    memset(s->raw, 0, sizeof(s->raw));
    s->source_epoch++;
}

static void nikon_paired_event_banks_realize(DeviceState *dev, Error **errp)
{
    NikonPairedEventBanksState *s = NIKON_PAIRED_EVENT_BANKS(dev);
    g_autofree char *data = NULL;
    g_autoptr(GError) error = NULL;
    gsize bytes;

    if (!s->calibration_path) {
        return;
    }
    if (!g_file_get_contents(s->calibration_path, &data, &bytes, &error)) {
        error_setg(errp, "paired calibration: %s", error->message);
        return;
    }
    if (bytes != sizeof(s->calibration)) {
        error_setg(errp, "paired calibration requires exactly %zu bytes",
                   sizeof(s->calibration));
        return;
    }
    for (unsigned i = 0; i < ARRAY_SIZE(s->calibration); i++) {
        s->calibration[i] = ldl_le_p(data + 4 * i);
        if (paired_live_word(4 * i) &&
            s->calibration[i]) {
            error_setg(errp, "paired calibration must leave interpreted "
                       "configuration slots zero");
            return;
        }
        if (!nikon_paired_event_banks_config(4 * i, 0) &&
            s->calibration[i]) {
            error_setg(errp, "paired calibration reserved slots must be zero");
            return;
        }
    }
    s->calibration_loaded = true;
}

static char *paired_runtime(Object *obj, Error **errp)
{
    NikonPairedEventBanksState *s = NIKON_PAIRED_EVENT_BANKS(obj);
    GString *state = g_string_new(NULL);

    g_string_append_printf(state, "source-epoch=%" PRIu64 "\n",
                           s->source_epoch);
    for (unsigned i = 0; i < ARRAY_SIZE(s->bank); i++) {
        NikonPairedEventBanksBank *b = &s->bank[i];

        g_string_append_printf(state,
            "bank%u request=%u active=%u phase=%u seen=%x refill=%x "
            "published=%u finish-pending=%u consumed=",
            i, b->request, b->active, b->phase, b->seen, b->refill,
            b->published, b->finish_pending);
        for (unsigned slot = 0; slot < ARRAY_SIZE(b->consumed); slot++) {
            g_string_append_printf(state, "%s%" PRIu64,
                                   slot ? "," : "", b->consumed[slot]);
        }
        g_string_append_c(state, '\n');
    }
    for (unsigned i = 0; i < ARRAY_SIZE(s->raw); i++) {
        g_string_append_printf(state,
                               "raw%u address=%x generation=%" PRIu64 "\n",
                               i, s->raw[i].address, s->raw[i].generation);
    }
    return g_string_free(state, false);
}

static void nikon_paired_event_banks_init(Object *obj)
{
    NikonPairedEventBanksState *s = NIKON_PAIRED_EVENT_BANKS(obj);
    unsigned i;

    for (i = 0; i < 2; i++) {
        NikonPairedEventBanksBank *b = &s->bank[i];

        b->parent = s;
        b->index = i;
        b->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, paired_complete, b);
        memory_region_init_io(&b->regs, obj, &nikon_paired_event_banks_ops, b,
                              i ? "nikon.control203c" : "nikon.paired-event-banks",
                              BANK_SIZE);
        sysbus_init_mmio(SYS_BUS_DEVICE(obj), &b->regs);
        sysbus_init_irq(SYS_BUS_DEVICE(obj), &b->irq);
    }
    qdev_init_gpio_in_named(DEVICE(obj), nikon_paired_event_banks_input, "pending-bit", 64);
    object_property_add_uint64_ptr(obj, "completed", &s->completed,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(obj, "rejected", &s->rejected,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(obj, "cancelled", &s->cancelled,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(obj, "blocked", &s->blocked,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(obj, "bytes", &s->bytes, OBJ_PROP_FLAG_READ);
    object_property_add_str(obj, "runtime", paired_runtime, NULL);
}

static void nikon_paired_event_banks_finalize(Object *obj)
{
    NikonPairedEventBanksState *s = NIKON_PAIRED_EVENT_BANKS(obj);

    for (unsigned i = 0; i < 2; i++) {
        timer_free(s->bank[i].timer);
        paired_release(&s->bank[i]);
    }
}

static const VMStateDescription nikon_paired_event_banks_vmstate = {
    .name = "nikon-paired-event-banks",
    .unmigratable = true,
};

static const Property paired_properties[] = {
    DEFINE_PROP_STRING("calibration", NikonPairedEventBanksState,
                       calibration_path),
    DEFINE_PROP_LINK("gate", NikonPairedEventBanksState, gate,
                     TYPE_NIKON_EVENT_GATE, DeviceState *),
};

static void nikon_paired_event_banks_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &nikon_paired_event_banks_vmstate;
    dc->realize = nikon_paired_event_banks_realize;
    device_class_set_props(dc, paired_properties);
    dc->unrealize = nikon_paired_event_banks_reset;
    dc->user_creatable = false;
    device_class_set_legacy_reset(dc, nikon_paired_event_banks_reset);
}

static const TypeInfo nikon_paired_event_banks_type = {
    .name = TYPE_NIKON_PAIRED_EVENT_BANKS,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonPairedEventBanksState),
    .instance_init = nikon_paired_event_banks_init,
    .instance_finalize = nikon_paired_event_banks_finalize,
    .class_init = nikon_paired_event_banks_class_init,
};

static void nikon_paired_event_banks_register_types(void)
{
    type_register_static(&nikon_paired_event_banks_type);
}
type_init(nikon_paired_event_banks_register_types)
