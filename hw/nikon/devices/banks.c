/*
 * Source-bound dual-bank, two-port, ten-layer image transactions.
 * Pixel encoding, cadence and completion-bit meanings are functional laws;
 * source packets, real RAM accesses and guest IRQ handling remain separate.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "migration/vmstate.h" /* IWYU pragma: keep */
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/nikon/devices.h"
#include "system/address-spaces.h"
#include "system/dma.h"
#include "qemu/bswap.h"
#include "qemu/bitops.h"
#include "qemu/timer.h"
#include "ui/console.h"
#include "ui/qemu-pixman.h" /* IWYU pragma: keep */

#define BANK_SIZE 0x4000
#define FRAME_PIXELS (16 * 1024 * 1024)
#define FRAME_PENDING ((1U << 1) | (1U << 16))
OBJECT_DECLARE_SIMPLE_TYPE(NikonDualEventBanksState, NIKON_DUAL_EVENT_BANKS)

typedef struct NikonBanksPrimary {
    uint32_t packet[BANK_SIZE / 4];
    uint8_t *luma, *chroma;
    pixman_image_t *image;
} NikonBanksPrimary;

typedef struct NikonDualEventBanksRegion {
    NikonDualEventBanksState *parent;
    MemoryRegion regs;
    unsigned index;
    QEMUTimer *timer;
    QemuConsole *console;
    uint32_t packet[BANK_SIZE / 4];
    uint64_t period;
    bool active;
} NikonDualEventBanksRegion;

struct NikonDualEventBanksState {
    SysBusDevice parent_obj;
    NikonDualEventBanksRegion region[14];
    qemu_irq irq[3];
    uint32_t config[2][BANK_SIZE / 4];
    uint32_t mask[2], pending[2], inputs[2];
    uint32_t global[5];
    uint32_t c0[0xcc / 4];
    uint32_t tables[8][0x400 / 4];
    NikonBanksPrimary primary[2];
    uint64_t primary_conversions, primary_reuses;
    uint64_t irq_levels;
    bool reported_error;
};

static void nikon_banks_primary_clear(NikonBanksPrimary *primary)
{
    if (primary->image) {
        pixman_image_unref(primary->image);
    }
    g_free(primary->luma);
    g_free(primary->chroma);
    memset(primary, 0, sizeof(*primary));
}

/* Only settled generated configuration words, not physical legal masks. */
static bool nikon_dual_event_banks_config(hwaddr offset, uint32_t *mask)
{
    unsigned port = offset >> 12;
    unsigned word = offset & 0xfff;

    /* CFB6/D060: exactly 42 entries in each original overlay array. */
    if (port == 1 && word >= 0x800 && word <= 0xba4 &&
        (word & 0xff) <= 0xa4) {
        unsigned array = (word - 0x800) >> 8;
        *mask = array < 2 ? 0x3fffffff :
                array == 2 ? 0x003f003f : 0x00ffffff;
        return true;
    }

    /* Original 1046d48a/1046d568: complete ten-entry parallel arrays. */
    if (port == 2 || port == 3) {
        if ((word >= 0x400 && word <= 0x424) ||
            (word >= 0x47c && word <= 0x4a0) ||
            (word >= 0x4c0 && word <= 0x4e4) ||
            (word >= 0x500 && word <= 0x524) ||
            (word >= 0x540 && word <= 0x564) ||
            (word >= 0x580 && word <= 0x5a4) ||
            (word >= 0x440 && word <= 0x44c)) {
            *mask = UINT32_MAX;
            return true;
        }
        switch (word) {
        case 0x200: *mask = 0x137f0017; return true;
        case 0x204: case 0x208: *mask = 0x3fffffff; return true;
        case 0x210: *mask = 0x03030303; return true;
        case 0x214: *mask = UINT32_MAX; return true;
        case 0x218: *mask = 1; return true;
        case 0x5c0: case 0x5c4: case 0x600: *mask = 0x3ff; return true;
        /* 1046d6d0 requests an overlay update with bit0 after committing
         * dirty layers. A complete value of three is only observed as a
         * hardware-state comparison, not as a guest-produced value. */
        case 0x604: *mask = 1; return true;
        case 0x5e0: case 0x5e4: case 0x5e8: case 0x5ec: case 0x5f0:
            *mask = 0x3f3f3f3f; return true;
        case 0x840: case 0x844: case 0x848:
            *mask = 0xffffff; return true;
        case 0x810: *mask = 0xff; return true;
        case 0x814: *mask = 0x1f001f1f; return true;
        case 0x818: *mask = 0x1f1f; return true;
        }
    }
    switch (offset) {
    case 0: case 0x1000: case 0x2000: case 0x3000: case 0x1224:
        *mask = 1;
        break;
    case 0x100: case 0x2100: case 0x3100:
        *mask = 3;
        break;
    case 0x1100: case 0x400: case 0x40c: case 0x4f0: case 0x1304:
        *mask = 0xff;
        break;
    case 0x1200:
        *mask = 0x13;
        break;
    case 0x200:
        *mask = 0x37f0311f;
        break;
    case 0x240: case 0x250: case 0x424: case 0x1204:
    case 0x130c: case 0x1314: case 0x1318: case 0x131c:
    case 0x1328: case 0x132c:
        *mask = 0xffff;
        break;
    case 0x204: case 0x600:
        *mask = 0x3fffffff;
        break;
    case 0x604:
        *mask = 0x101;
        break;
    case 0x4c0: case 0x1510:
        *mask = 0x606;
        break;
    case 0x410: case 0x1208:
        *mask = 0x301;
        break;
    case 0x414:
        *mask = 0x00ff01ff;
        break;
    case 0x418:
        *mask = 0x00ff00ff;
        break;
    case 0x404:
        *mask = 0x1f001f1f;
        break;
    case 0x408:
        *mask = 0x1f1f;
        break;
    case 0x1400:
        *mask = 0x030100ff;
        break;
    case 0x1300:
        *mask = 0x11111;
        break;
    case 0x1308:
        *mask = 0xbfff3fff;
        break;
    case 0x1310:
        *mask = 0x103ff;
        break;
    case 0x1338:
        *mask = 1;
        break;
    case 0x110: case 0x2110: case 0x3110: case 0x1220:
    case 0x1420: case 0x1424:
    case 0x210: case 0x214: case 0x218: case 0x21c:
    case 0x220: case 0x224: case 0x228: case 0x22c:
    case 0x260:
    case 0x500: case 0x2880: case 0x3880: case 0x120c:
    case 0x41c: case 0x420: case 0x1c00: case 0x1c04: case 0x1c08:
    case 0x1320: case 0x1324: case 0x1330:
    case 0x1480: case 0x1620:
        *mask = UINT32_MAX;
        break;
    default:
        if ((offset >= 0x430 && offset <= 0x478 && offset != 0x454) ||
            (offset >= 0x4a0 && offset <= 0x4b4) ||
            (offset >= 0x4d0 && offset <= 0x4e4) ||
            (offset >= 0x1490 && offset <= 0x14a4) ||
            (offset >= 0x14f0 && offset <= 0x1504) ||
            (offset >= 0x14b0 && offset <= 0x14c8) ||
            (offset >= 0x14d0 && offset <= 0x14d8) ||
            (offset >= 0x14e0 && offset <= 0x14e4) ||
            (offset >= 0x1600 && offset <= 0x1618)) {
            /* Exact C6CA packet and inverse C8EA scalar words. */
            *mask = UINT32_MAX;
            return true;
        }
        return false;
    }
    return true;
}

static void nikon_dual_event_banks_irq(NikonDualEventBanksState *s)
{
    uint64_t levels = 0;
    unsigned route, bank;

    for (route = 0; route < 3; route++) {
        bool asserted = false;

        for (bank = 0; bank < 2; bank++) {
            asserted |= s->config[bank][0x1100 / 4] == route + 1 &&
                        !!(s->pending[bank] & ~s->mask[bank]);
        }
        levels |= (uint64_t)asserted << route;
        qemu_set_irq(s->irq[route], asserted);
    }
    s->irq_levels = levels;
}

static bool nikon_banks_ram(hwaddr base, uint64_t bytes)
{
    if (!base || !bytes || bytes > HWADDR_MAX - base) {
        return false;
    }
    while (bytes) {
        MemoryRegionSection section =
            memory_region_find(get_system_memory(), base, bytes);
        uint64_t span = int128_get64(section.size);
        bool valid = section.mr && span && span <= bytes &&
                     memory_region_is_ram(section.mr) &&
                     !memory_region_is_rom(section.mr);

        if (section.mr) {
            memory_region_unref(section.mr);
        }
        if (!valid) {
            return false;
        }
        base += span;
        bytes -= span;
    }
    return true;
}

static void nikon_banks_schedule(NikonDualEventBanksRegion *r)
{
    uint32_t *packet = r->parent->config[r->index];
    uint64_t pixels = (uint64_t)packet[0x132c / 4] * packet[0x1328 / 4];

    if (pixels && pixels <= FRAME_PIXELS) {
        r->period = MAX(1, (pixels * NANOSECONDS_PER_SECOND +
                           47999999) / 48000000);
    }
    if (r->active) {
        timer_mod(r->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + r->period);
    }
}

/* Functional encodings for the seven original numeric input layouts.
 * Width, row stride, plane addresses and lane selectors are source facts.
 * The packed 16-bit component profiles remain explicitly virtual.
 */
static uint32_t nikon_banks_pixel(const uint8_t *data, unsigned mode,
                                 uint32_t control, uint32_t lanes)
{
    unsigned red, green, blue, alpha = 255;
    uint16_t packed;

    if (!mode) {
        unsigned i, component[4];

        for (i = 0; i < 4; i++) {
            unsigned lane = (lanes >> (i * 8)) & 3;
            component[i] = data[(control & 0x10) ? 3 - lane : lane];
        }
        red = component[0];
        green = component[1];
        blue = component[2];
        if (control & 0x10000000) {
            alpha = component[3];
        }
    } else {
        packed = (control & 0x10) ? lduw_be_p(data) : lduw_le_p(data);
        if (mode == 2) {
            red = ((packed >> 10) & 31) * 255 / 31;
            green = ((packed >> 5) & 31) * 255 / 31;
            blue = (packed & 31) * 255 / 31;
            alpha = (packed & 0x8000) ? 255 : 0;
        } else if (mode == 3) {
            red = ((packed >> 8) & 15) * 17;
            green = ((packed >> 4) & 15) * 17;
            blue = (packed & 15) * 17;
            alpha = ((packed >> 12) & 15) * 17;
        } else {
            red = (packed >> 11) * 255 / 31;
            green = ((packed >> 5) & 63) * 255 / 63;
            blue = (packed & 31) * 255 / 31;
        }
    }
    /* Pixman OVER requires premultiplied source components. */
    return (alpha << 24) | ((red * alpha / 255) << 16) |
           ((green * alpha / 255) << 8) | (blue * alpha / 255);
}

/* Same limited-range BT.601 convention as the transfer107 producer. */
static uint32_t nikon_banks_yuva(const uint8_t *pair, unsigned column)
{
    int y = MAX(0, pair[column & 1] - 16);
    int u = pair[2] - 128, v = pair[3] - 128;
    unsigned red = CLAMP((298 * y + 409 * v + 128) >> 8, 0, 255);
    unsigned green = CLAMP((298 * y - 100 * u - 208 * v + 128) >> 8, 0, 255);
    unsigned blue = CLAMP((298 * y + 516 * u + 128) >> 8, 0, 255);

    return 0xff000000U | (red << 16) | (green << 8) | blue;
}

/* Neutral curves qualify the matrix-only law without ignoring table changes. */
static bool nikon_banks_primary_curves(NikonDualEventBanksRegion *r)
{
    const uint32_t (*tables)[0x400 / 4] = r->parent->tables + r->index * 4;

    for (unsigned family = 0; family < 2; family++) {
        for (unsigned i = 0; i < 0x400 / 4; i++) {
            if (tables[family][i]) {
                return false;
            }
        }
    }
    for (unsigned bank = 0; bank < 2; bank++) {
        for (unsigned plane = 0; plane < 3; plane++) {
            for (unsigned i = 0; i <= 16; i++) {
                unsigned lo = MIN(i * 16, 255);
                unsigned hi = i == 16 ? 0 : i * 16 + 8;
                unsigned word = (bank * 0x200 + plane * 0x80) / 4 + i;

                if (tables[2][word] != (lo | (hi << 16))) {
                    return false;
                }
            }
        }
        for (unsigned curve = 0; curve < 2; curve++) {
            for (unsigned i = 0; i <= 16; i++) {
                unsigned lo = curve ? 64 : MIN(i * 16, 255);
                unsigned hi = i == 16 ? 0 : curve ? 64 : i * 16 + 8;
                unsigned word = (bank * 0x100 + curve * 0x80) / 4 + i;

                if (tables[3][word] != (lo | (hi << 16))) {
                    return false;
                }
            }
        }
    }
    return true;
}

/*
 * Unit0 receives the separate Y/UV view, before the two overlay ports.
 * C3C2/C354 preserve its source rectangle, pitches and already cropped PAs.
 * Q14/Q10 signed matrices, nearest scaling and global alpha are explicit
 * functional color laws; table-update requests and effects stay unsupported.
 */
static bool nikon_banks_primary(NikonDualEventBanksRegion *r,
                                pixman_image_t *output, unsigned out_width,
                                unsigned out_height, bool *supplied)
{
    const uint32_t *p = r->packet;
    NikonBanksPrimary *primary = &r->parent->primary[r->index];
    unsigned command = p[0x100 / 4] & 3;
    unsigned mode = p[0x200 / 4] & 31;
    unsigned width = p[0x204 / 4] & 0xffff;
    unsigned height = p[0x204 / 4] >> 16;
    unsigned pitch_y = p[0x240 / 4], pitch_uv = p[0x250 / 4];
    unsigned x = p[0x600 / 4] & 0xffff, y = p[0x600 / 4] >> 16;
    unsigned alpha = p[0x500 / 4];
    unsigned a = 1, b = 1, e = 1, f = 1, scaled_w, scaled_h;
    unsigned uv_height, row, col, i, j;
    hwaddr base_y = p[0x210 / 4], base_uv = p[0x220 / 4];
    int matrix[2][3][3];
    g_autofree uint8_t *luma = NULL;
    g_autofree uint8_t *chroma = NULL;
    pixman_image_t *image;
    pixman_transform_t transform;
    uint32_t *pixels;
    unsigned stride;

    if (command != 1 && command != 3) {
        nikon_banks_primary_clear(primary);
        return true;
    }
    if ((mode != 0 && mode != 4) ||
        p[0x200 / 4] != (0x00300000U | mode) ||
        !width || !height || (width & 1) || (height & 1) ||
        (uint64_t)width * height > FRAME_PIXELS ||
        pitch_y < width || pitch_uv < width || (pitch_uv & 1) ||
        (base_uv & 1) || alpha > 255 || p[0x260 / 4] ||
        p[0x604 / 4] || p[0x40c / 4] ||
        (p[0x410 / 4] & 1) || (p[0x4c0 / 4] & 0x202) ||
        p[0x4f0 / 4] > 1 || !nikon_banks_primary_curves(r)) {
        return false;
    }
    /* Only the first PA in each four-entry array has a known live selector. */
    for (i = 1; i < 4; i++) {
        if (p[0x210 / 4 + i] || p[0x220 / 4 + i]) {
            return false;
        }
    }
    for (i = 0x430; i <= 0x478; i += 4) {
        if (i != 0x454 && p[i / 4]) {
            return false;
        }
    }
    for (i = 0; i < 3; i++) {
        uint32_t first = p[(0x4a0 + i * 8) / 4];
        uint32_t last = p[(0x4a4 + i * 8) / 4];
        uint32_t second = p[(0x4d0 + i * 8) / 4];
        uint32_t tail = p[(0x4d4 + i * 8) / 4];

        if ((last & 0xffff0000U) || (second & 0xf000f000U) ||
            (tail & 0xfffff000U)) {
            return false;
        }
        matrix[0][i][0] = (int16_t)first;
        matrix[0][i][1] = (int16_t)(first >> 16);
        matrix[0][i][2] = (int16_t)last;
        matrix[1][i][0] = sextract32(second, 0, 12);
        matrix[1][i][1] = sextract32(second, 16, 12);
        matrix[1][i][2] = sextract32(tail, 0, 12);
    }
    if (p[0x400 / 4]) {
        a = p[0x404 / 4] & 31;
        b = (p[0x404 / 4] >> 8) & 31;
        e = p[0x408 / 4] & 31;
        f = (p[0x408 / 4] >> 8) & 31;
        if (p[0x400 / 4] != 1 || !a || !b || !e || !f ||
            (p[0x404 / 4] >> 24)) {
            return false;
        }
    }
    scaled_w = (uint64_t)width * a / b;
    scaled_h = (uint64_t)height * e / f;
    if (!scaled_w || !scaled_h ||
        (uint64_t)scaled_w * scaled_h > FRAME_PIXELS) {
        return false;
    }
    uv_height = mode == 4 ? height / 2 : height;
    for (row = 0; row < height; row++) {
        uint64_t address = (uint64_t)base_y + (uint64_t)row * pitch_y;

        if (address + width > (1ULL << 32) ||
            !nikon_banks_ram(address, width)) {
            return false;
        }
    }
    for (row = 0; row < uv_height; row++) {
        uint64_t address = (uint64_t)base_uv + (uint64_t)row * pitch_uv;

        if (address + width > (1ULL << 32) ||
            !nikon_banks_ram(address, width)) {
            return false;
        }
    }
    luma = g_malloc((size_t)width * height);
    chroma = g_malloc((size_t)width * uv_height);
    for (row = 0; row < height; row++) {
        if (dma_memory_read(&address_space_memory,
                            base_y + (uint64_t)row * pitch_y,
                            luma + (size_t)row * width, width,
                            MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
            return false;
        }
    }
    for (row = 0; row < uv_height; row++) {
        if (dma_memory_read(&address_space_memory,
                            base_uv + (uint64_t)row * pitch_uv,
                            chroma + (size_t)row * width, width,
                            MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
            return false;
        }
    }
    /* Validate and read current RAM even when its exact conversion is cached. */
    if (primary->image && !memcmp(primary->packet, p, sizeof(primary->packet)) &&
        !memcmp(primary->luma, luma, (size_t)width * height) &&
        !memcmp(primary->chroma, chroma, (size_t)width * uv_height)) {
        image = primary->image;
        r->parent->primary_reuses++;
        goto composite;
    }
    nikon_banks_primary_clear(primary);
    image = pixman_image_create_bits(PIXMAN_a8r8g8b8, width, height, NULL, 0);
    if (!image) {
        return false;
    }
    pixels = pixman_image_get_data(image);
    stride = pixman_image_get_stride(image) / 4;
    for (row = 0; row < height; row++) {
        unsigned uv_row = mode == 4 ? row / 2 : row;

        for (col = 0; col < width; col++) {
            const uint8_t *uv = chroma + (size_t)uv_row * width + (col & ~1U);
            int input[3] = { luma[(size_t)row * width + col],
                             uv[0] - 128, uv[1] - 128 };
            int rgb[3], final[3];

            for (i = 0; i < 3; i++) {
                int64_t value = 0;

                for (j = 0; j < 3; j++) {
                    value += (int64_t)matrix[0][i][j] * input[j];
                }
                rgb[i] = CLAMP((value + 8192) >> 14, 0, 255);
            }
            for (i = 0; i < 3; i++) {
                int64_t value = (int64_t)rgb[i] * 1024;

                /* Profile byte22: finite bypass/apply interpretation. */
                if (p[0x4f0 / 4]) {
                    value = 0;
                    for (j = 0; j < 3; j++) {
                        value += (int64_t)matrix[1][i][j] * rgb[j];
                    }
                }
                final[i] = CLAMP((value + 512) >> 10, 0, 255) * alpha / 255;
            }
            pixels[row * stride + col] = (alpha << 24) |
                (final[0] << 16) | (final[1] << 8) | final[2];
        }
    }
    pixman_transform_init_scale(&transform, ((int64_t)b << 16) / a,
                                ((int64_t)f << 16) / e);
    if (!pixman_image_set_transform(image, &transform) ||
        !pixman_image_set_filter(image, PIXMAN_FILTER_NEAREST, NULL, 0)) {
        pixman_image_unref(image);
        return false;
    }
    primary->image = image;
    primary->luma = g_steal_pointer(&luma);
    primary->chroma = g_steal_pointer(&chroma);
    memcpy(primary->packet, p, sizeof(primary->packet));
    r->parent->primary_conversions++;
composite:
    if (x < out_width && y < out_height) {
        pixman_image_composite32(PIXMAN_OP_OVER, image, NULL, output,
                                 0, 0, 0, 0, x, y,
                                 MIN(scaled_w, out_width - x),
                                 MIN(scaled_h, out_height - y));
    }
    *supplied = true;
    return true;
}

static bool nikon_banks_layer(NikonDualEventBanksRegion *r,
                              pixman_image_t *output, unsigned port,
                              unsigned layer, unsigned out_width,
                              unsigned out_height)
{
    uint32_t *packet = r->packet;
    unsigned block = 0x2000 + port * 0x1000;
    uint32_t geometry = packet[(block + 0x400 + layer * 4) / 4];
    unsigned width = geometry & 0xffff, height = (geometry >> 16) & 0x3fff;
    uint32_t position = packet[(block + 0x580 + layer * 4) / 4];
    unsigned x = position & 0xffff, y = (position >> 16) & 0x3fff;
    uint32_t control = packet[(block + 0x200) / 4];
    unsigned mode = control & 7, bytes = mode ? 2 : 4;
    uint32_t lanes = packet[(block + 0x210) / 4];
    hwaddr primary = packet[(block + (layer ? 0x47c + layer * 4 : 0x440)) / 4];
    hwaddr secondary = packet[(block + 0x4c0 + layer * 4) / 4];
    unsigned stride = packet[(block + 0x500 + layer * 4) / 4];
    unsigned alpha_stride = packet[(block + 0x540 + layer * 4) / 4];
    unsigned row_bytes = width * bytes;
    unsigned alpha_bytes = mode == 5 ? (width + 1) / 2 : width;
    g_autofree uint8_t *row = NULL;
    g_autofree uint8_t *alpha = NULL;
    pixman_image_t *image;
    uint32_t *pixels;
    unsigned row_index, column, pitch;
    bool success = false;
    bool yuva = mode == 6 && (control & 0x30000);

    if ((yuva && (control != 0x00030006 || lanes != 0x02000301 ||
                  (width & 1) || (primary & 3) || (stride & 3))) ||
        mode > 6 || !width || !height ||
        (uint64_t)width * height > FRAME_PIXELS ||
        x >= out_width || y >= out_height ||
        width > out_width - x || height > out_height - y ||
        stride < row_bytes ||
        !nikon_banks_ram(primary, (uint64_t)(height - 1) * stride + row_bytes) ||
        (mode >= 5 && (alpha_stride < alpha_bytes ||
         !nikon_banks_ram(secondary, (uint64_t)(height - 1) * alpha_stride +
                          alpha_bytes)))) {
        return false;
    }
    image = pixman_image_create_bits(PIXMAN_a8r8g8b8, width, height, NULL, 0);
    if (!image) {
        return false;
    }
    pixels = pixman_image_get_data(image);
    pitch = pixman_image_get_stride(image) / 4;
    row = g_malloc(row_bytes);
    if (mode >= 5) {
        alpha = g_malloc(alpha_bytes);
    }
    for (row_index = 0; row_index < height; row_index++) {
        if (dma_memory_read(&address_space_memory,
                            primary + (uint64_t)row_index * stride,
                            row, row_bytes, MEMTXATTRS_UNSPECIFIED) != MEMTX_OK ||
            (mode >= 5 && dma_memory_read(&address_space_memory,
                secondary + (uint64_t)row_index * alpha_stride, alpha,
                alpha_bytes, MEMTXATTRS_UNSPECIFIED) != MEMTX_OK)) {
            goto out;
        }
        for (column = 0; column < width; column++) {
            unsigned opacity = 255;
            uint32_t pixel;

            if (mode >= 5) {
                opacity = mode == 5 ?
                    ((alpha[column / 2] >> ((column & 1) * 4)) & 15) * 17 :
                    alpha[column];
                if (!opacity) {
                    pixels[row_index * pitch + column] = 0;
                    continue;
                }
            }
            pixel = yuva ?
                nikon_banks_yuva(row + (column & ~1U) * 2, column) :
                nikon_banks_pixel(row + column * bytes, mode, control, lanes);
            if (opacity != 255) {
                pixel = (opacity << 24) |
                    ((((pixel >> 16) & 255) * opacity / 255) << 16) |
                    ((((pixel >> 8) & 255) * opacity / 255) << 8) |
                    ((pixel & 255) * opacity / 255);
            }
            pixels[row_index * pitch + column] = pixel;
        }
    }
    pixman_image_composite32(PIXMAN_OP_OVER, image, NULL, output,
                             0, 0, 0, 0, x, y, width, height);
    success = true;
out:
    pixman_image_unref(image);
    return success;
}

static const GraphicHwOps nikon_banks_graphics;

/* Source ratios map pre-scale port coordinates into viewport coordinates.
 * Nearest sampling and the frame boundary are named functional conventions.
 */
static bool nikon_banks_port(NikonDualEventBanksRegion *r,
                             pixman_image_t *output, unsigned port,
                             unsigned out_width, unsigned out_height,
                             bool *supplied)
{
    unsigned block = 0x2000 + port * 0x1000;
    uint32_t *packet = r->packet;
    uint32_t enabled = packet[(block + 0x5c0) / 4];
    uint32_t command = packet[(block + 0x100) / 4] & 3;
    uint32_t geometry = packet[(block + 0x204) / 4];
    uint32_t origin = packet[(block + 0x208) / 4];
    unsigned width = geometry & 0xffff, height = (geometry >> 16) & 0x3fff;
    int x = (int16_t)origin;
    unsigned y = (origin >> 16) & 0x3fff;
    unsigned a = 1, b = 1, e = 1, f = 1, scaled_width, scaled_height, layer;
    pixman_transform_t transform;
    pixman_image_t *canvas;

    if (!enabled || (command != 1 && command != 3)) {
        return true;
    }
    if (packet[(block + 0x810) / 4]) {
        uint32_t horizontal = packet[(block + 0x814) / 4];
        uint32_t vertical = packet[(block + 0x818) / 4];
        a = horizontal & 31;
        b = (horizontal >> 8) & 31;
        e = vertical & 31;
        f = (vertical >> 8) & 31;
        if (!a || !b || !e || !f) {
            return false;
        }
    }
    scaled_width = (uint64_t)width * a / b;
    scaled_height = (uint64_t)height * e / f;
    if (!width || !height || !scaled_width || !scaled_height ||
        (uint64_t)width * height > FRAME_PIXELS) {
        return false;
    }
    canvas = pixman_image_create_bits(PIXMAN_a8r8g8b8, width, height, NULL, 0);
    if (!canvas) {
        return false;
    }
    for (layer = 0; layer < 10; layer++) {
        if ((enabled & (1U << layer)) &&
            !nikon_banks_layer(r, canvas, port, layer, width, height)) {
            pixman_image_unref(canvas);
            return false;
        }
    }
    pixman_transform_init_scale(&transform, ((int64_t)b << 16) / a,
                                ((int64_t)f << 16) / e);
    if (!pixman_image_set_transform(canvas, &transform) ||
        !pixman_image_set_filter(canvas, PIXMAN_FILTER_NEAREST, NULL, 0)) {
        pixman_image_unref(canvas);
        return false;
    }
    /* DD354 derives this origin from signed viewport vx0. D96D4 bounds vx1,
     * not vx0: a negative viewport can place a positive relative layer at
     * global x0. Pixman clips the signed destination and advances the source
     * through the same inverse transform, including the cropped leading edge.
     */
    if (x < (int)out_width && (int64_t)x + scaled_width > 0 && y < out_height) {
        pixman_image_composite32(PIXMAN_OP_OVER, canvas, NULL, output,
                                 0, 0, 0, 0, x, y,
                                 scaled_width,
                                 MIN(scaled_height, out_height - y));
    }
    *supplied = true;
    pixman_image_unref(canvas);
    return true;
}

static bool nikon_banks_overlays(NikonDualEventBanksRegion *r,
                                pixman_image_t *output,
                                unsigned width, unsigned height)
{
    unsigned entry;
    bool supplied = false;

    for (entry = 0; entry < 42; entry++) {
        unsigned bitmap = entry < 32 ? 0x1c00 : 0x1c04;
        uint32_t position, extent, color;
        unsigned x, y, w, h;
        pixman_color_t rgb;
        pixman_rectangle16_t rect;

        if (!(r->packet[bitmap / 4] & (1U << (entry % 32)))) {
            continue;
        }
        position = r->packet[(0x1800 + entry * 4) / 4];
        extent = r->packet[(0x1900 + entry * 4) / 4];
        color = r->packet[(0x1b00 + entry * 4) / 4];
        x = position & 0xffff;
        y = (position >> 16) & 0x3fff;
        w = extent & 0xffff;
        h = (extent >> 16) & 0x3fff;
        if (!w || !h || x >= width || y >= height) {
            continue;
        }
        /* DD230 supplies three bytes; RGB byte order is a virtual profile.
         * The two 6-bit fields at1a00 remain retained, not guessed alpha.
         */
        rgb = (pixman_color_t) {
            .red = (color & 255) * 257,
            .green = ((color >> 8) & 255) * 257,
            .blue = ((color >> 16) & 255) * 257,
            .alpha = 65535,
        };
        rect = (pixman_rectangle16_t) {
            x, y, MIN(w, width - x), MIN(h, height - y),
        };
        pixman_image_fill_rectangles(PIXMAN_OP_OVER, output, &rgb, 1, &rect);
        supplied = true;
    }
    return supplied;
}

static void nikon_banks_frame(void *opaque)
{
    NikonDualEventBanksRegion *r = opaque;
    NikonDualEventBanksState *s = r->parent;
    uint32_t *packet = r->packet;
    unsigned width, height;
    pixman_image_t *output;
    unsigned port;
    bool supplied = false;

    if (!r->active) {
        return;
    }
    /* Live setters have no master restart. Snapshot at each frame boundary. */
    memcpy(packet, s->config[r->index], sizeof(r->packet));
    /* CA80 writes visible height/width separately from the 1308 timing pair. */
    width = packet[0x132c / 4];
    height = packet[0x1328 / 4];
    if (!width || !height || (uint64_t)width * height > FRAME_PIXELS) {
        goto error;
    }
    output = pixman_image_create_bits(PIXMAN_a8r8g8b8, width, height, NULL, 0);
    if (!output) {
        goto error;
    }
    if (!nikon_banks_primary(r, output, width, height, &supplied)) {
        pixman_image_unref(output);
        goto error;
    }
    for (port = 0; port < 2; port++) {
        if (!nikon_banks_port(r, output, port, width, height, &supplied)) {
            pixman_image_unref(output);
            goto error;
        }
    }
    supplied |= nikon_banks_overlays(r, output, width, height);
    /* A start with no enabled layer is a valid empty transaction. It advances
     * completion without creating a console or publishing host pixels. */
    if (supplied) {
        if (!r->console) {
            r->console = qemu_graphic_console_create(DEVICE(s), r->index,
                                                      &nikon_banks_graphics,
                                                      r);
        }
        qemu_console_resize(r->console, width, height);
        {
            DisplaySurface *surface = qemu_console_surface(r->console);
            pixman_image_composite32(PIXMAN_OP_SRC, output, NULL,
                                     surface->image, 0, 0, 0, 0, 0, 0,
                                     width, height);
        }
        qemu_console_update_full(r->console);
    }
    pixman_image_unref(output);
    s->config[r->index][0x1200 / 4] =
        (s->config[r->index][0x1200 / 4] & ~3U) | 3;
    {
        unsigned field;
        const unsigned selection[] = { 0x4c0, 0x1510 };
        const unsigned command[] = { 0x100, 0x2100, 0x3100 };

        /* E004 requests bits2/10; E0BA observes effective banks16/24.
         * Publication is the named functional bank-switch boundary.
         * It never commits a cancelled request or a failed data producer.
         */
        for (field = 0; field < ARRAY_SIZE(selection); field++) {
            unsigned word = selection[field] / 4;
            uint32_t request = packet[word];
            uint32_t current = s->config[r->index][word];

            if (request & 2) {
                current = (current & ~(1U << 16)) |
                          ((request & 4) << 14);
            }
            if (request & 0x200) {
                current = (current & ~(1U << 24)) |
                          ((request & 0x400) << 14);
            }
            s->config[r->index][word] = current;
        }
        for (field = 0; field < ARRAY_SIZE(command); field++) {
            unsigned word = command[field] / 4;
            if ((packet[word] & 3) == 1 &&
                (s->config[r->index][word] & 3) == 1) {
                s->config[r->index][word] = 3;
            }
        }
    }
    /* Low bit1 is the unmasked IRQ cause; retained bit16 is the semantic
     * frame event consumed by the original callback packet. */
    s->pending[r->index] |= FRAME_PENDING;
    nikon_dual_event_banks_irq(s);
    nikon_banks_schedule(r);
    return;
error:
    nikon_banks_primary_clear(&s->primary[r->index]);
    nikon_access_error(&s->reported_error, r->regs.addr + 0x1200, 4,
                       true, packet[0x1200 / 4], "image-packet-or-dma-failed");
    nikon_banks_schedule(r);
}

static bool nikon_banks_update(void *opaque)
{
    (void)opaque;
    /* The surface contains only the last completed guest transaction. */
    return true;
}

static const GraphicHwOps nikon_banks_graphics = {
    .gfx_update = nikon_banks_update,
};

static void nikon_dual_event_banks_input(void *opaque, int index, int level)
{
    NikonDualEventBanksState *s = opaque;
    unsigned bank = index / 32;
    uint32_t bit = 1U << (index % 32);
    bool rising = level && !(s->inputs[bank] & bit);

    s->inputs[bank] = (s->inputs[bank] & ~bit) | (level ? bit : 0);
    if (rising) {
        s->pending[bank] |= bit;
        nikon_dual_event_banks_irq(s);
    }
}

static MemTxResult nikon_dual_event_banks_error(NikonDualEventBanksRegion *r, hwaddr offset,
                                 unsigned size, bool write, uint64_t value)
{
    nikon_access_error(&r->parent->reported_error, r->regs.addr + offset,
                      size, write, value,
                      "dual-event-banks-access-or-activation-not-supported");
    return MEMTX_ERROR;
}

static bool nikon_dual_event_banks_global(unsigned index, hwaddr offset, uint32_t *mask,
                              unsigned *slot)
{
    if (offset == 0 && index >= 2 && index <= 4) {
        *slot = index == 4 ? 3 : index - 2;
        *mask = index == 2 ? 0x111 : index == 3 ? 0x11 : 1;
        return true;
    }
    if (index == 3 && offset == 0x100) {
        *slot = 2; /* D100 needs independent storage from E000. */
        *mask = 1;
        return true;
    }
    if (index == 3 && offset == 0x200) {
        /* The sole direct C1.70 caller supplies zero. Unknown values fail. */
        *slot = 4;
        *mask = 0;
        return true;
    }
    return false;
}

static bool nikon_banks_c0(hwaddr offset, uint32_t *mask)
{
    switch (offset) {
    case 4:   *mask = 1; return true;
    case 8:   *mask = 0xffff; return true;
    case 0xc: *mask = 0xff; return true;
    case 0x10: *mask = 0x10f; return true;
    case 0x14: *mask = 0x1f; return true;
    case 0x18: *mask = 0x00ff00ff; return true;
    case 0x2c: case 0x34: *mask = 1; return true;
    case 0x90: *mask = 0x0001003f; return true;
    case 0xa0: *mask = 1; return true;
    case 0xa4: *mask = 0xff; return true;
    case 0xc8: *mask = UINT32_MAX; return true;
    default: return false;
    }
}

static bool nikon_banks_table(unsigned index, hwaddr offset, uint32_t *mask)
{
    unsigned family = (index - 5) % 4;
    unsigned word;

    if (family < 2) {
        /* Two banks of three32-entry12-bit planes, packed in16 words. */
        if ((offset & 0xff) >= 0xc0) {
            return false;
        }
        *mask = 0x0fff0fff;
    } else if (family == 2) {
        /* Three33-entry8-bit planes per bank; each plane stride is0x80. */
        unsigned plane = (offset & 0x1ff) >> 7;
        word = offset & 0x7f;
        if (plane >= 3 || word > 0x40) {
            return false;
        }
        *mask = word == 0x40 ? 0xff : 0x00ff00ff;
    } else {
        /* Two single33-entry tables per bank. Their last setter uses LDRH. */
        word = offset & 0x7f;
        if (word > 0x40) {
            return false;
        }
        *mask = word == 0x40 ? 0xffff : 0x00ff00ff;
    }
    return true;
}

static MemTxResult nikon_dual_event_banks_read(void *opaque, hwaddr offset,
                                uint64_t *value, unsigned size,
                                MemTxAttrs attrs)
{
    NikonDualEventBanksRegion *r = opaque;
    NikonDualEventBanksState *s = r->parent;
    uint32_t mask;
    unsigned slot;

    *value = 0;
    if (size != 4 || (offset & 3)) {
        return nikon_dual_event_banks_error(r, offset, size, false, 0);
    }
    if (r->index < 2) {
        if (offset == 0x1214) {
            *value = s->pending[r->index];
        } else if (offset == 0x1210) {
            *value = s->mask[r->index];
        } else if (nikon_dual_event_banks_config(offset, &mask)) {
            *value = s->config[r->index][offset / 4];
        } else {
            return nikon_dual_event_banks_error(r, offset, size, false, 0);
        }
    } else if (r->index == 13 && nikon_banks_c0(offset, &mask)) {
        *value = s->c0[offset / 4];
    } else if (nikon_dual_event_banks_global(r->index, offset, &mask, &slot)) {
        *value = s->global[slot];
    } else if (r->index >= 5 && r->index < 13 &&
               nikon_banks_table(r->index, offset, &mask)) {
        *value = s->tables[r->index - 5][offset / 4];
    } else {
        return nikon_dual_event_banks_error(r, offset, size, false, 0);
    }
    return MEMTX_OK;
}

static MemTxResult nikon_dual_event_banks_write(void *opaque, hwaddr offset,
                                 uint64_t value, unsigned size,
                                 MemTxAttrs attrs)
{
    NikonDualEventBanksRegion *r = opaque;
    NikonDualEventBanksState *s = r->parent;
    uint32_t mask;
    unsigned slot;

    if (size != 4 || (offset & 3)) {
        return nikon_dual_event_banks_error(r, offset, size, true, value);
    }
    if (r->index < 2) {
        if (offset == 0x1214) {
            s->pending[r->index] &= ~value;
        } else if (offset == 0x1210) {
            s->mask[r->index] = value;
        } else if (nikon_dual_event_banks_config(offset, &mask) &&
                   !(value & ~mask)) {
            if (offset == 0x1200) {
                unsigned command = value & 3;
                if (command == 1) {
                    uint32_t *packet = s->config[r->index];
                    uint64_t pixels = (uint64_t)packet[0x132c / 4] *
                                      packet[0x1328 / 4];
                    bool running = r->active &&
                        (s->config[r->index][offset / 4] & 3) == 3;

                    if (!pixels || pixels > FRAME_PIXELS) {
                        return nikon_dual_event_banks_error(r, offset,
                                                            size, true, value);
                    }
                    r->active = true;
                    nikon_banks_schedule(r);
                    value = (value & ~3U) | (running ? 3 : 1);
                } else if (command == 3) {
                    return nikon_dual_event_banks_error(r, offset,
                                                        size, true, value);
                } else {
                    timer_del(r->timer);
                    r->active = false;
                    nikon_banks_primary_clear(&s->primary[r->index]);
                }
            } else if (offset == 0x100 || offset == 0x2100 ||
                       offset == 0x3100) {
                if (value == 3) {
                    return nikon_dual_event_banks_error(r, offset,
                                                        size, true, value);
                }
                if (offset == 0x100 && value != 1) {
                    nikon_banks_primary_clear(&s->primary[r->index]);
                }
            }
            if (offset == 0x4c0 || offset == 0x1510) {
                /* Effective-bank bits are hardware state, not request bits. */
                value |= s->config[r->index][offset / 4] & 0x01010000;
            }
            s->config[r->index][offset / 4] = value;
        } else {
            return nikon_dual_event_banks_error(r, offset, size, true, value);
        }
    } else if (r->index == 13 && nikon_banks_c0(offset, &mask) &&
               !(value & ~mask)) {
        s->c0[offset / 4] = value;
    } else if (nikon_dual_event_banks_global(r->index, offset, &mask, &slot) &&
               !(value & ~mask)) {
        s->global[slot] = value;
    } else if (r->index >= 5 && r->index < 13 &&
               nikon_banks_table(r->index, offset, &mask) &&
               !(value & ~mask)) {
        s->tables[r->index - 5][offset / 4] = value;
    } else {
        return nikon_dual_event_banks_error(r, offset, size, true, value);
    }
    nikon_dual_event_banks_irq(s);
    return MEMTX_OK;
}

static const MemoryRegionOps nikon_dual_event_banks_ops = {
    .read_with_attrs = nikon_dual_event_banks_read,
    .write_with_attrs = nikon_dual_event_banks_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
};

static void nikon_dual_event_banks_reset(DeviceState *dev)
{
    NikonDualEventBanksState *s = NIKON_DUAL_EVENT_BANKS(dev);
    unsigned i;

    memset(s->config, 0, sizeof(s->config));
    memset(s->mask, 0, sizeof(s->mask));
    memset(s->pending, 0, sizeof(s->pending));
    memset(s->global, 0, sizeof(s->global));
    memset(s->c0, 0, sizeof(s->c0));
    memset(s->tables, 0, sizeof(s->tables));
    s->primary_conversions = 0;
    s->primary_reuses = 0;
    for (i = 0; i < 2; i++) {
        nikon_banks_primary_clear(&s->primary[i]);
        timer_del(s->region[i].timer);
        s->region[i].active = false;
        s->region[i].period = 0;
        memset(s->region[i].packet, 0, sizeof(s->region[i].packet));
    }
    s->reported_error = false;
    /* Actual provider levels survive reset; no edge is manufactured. */
    nikon_dual_event_banks_irq(s);
}

static void nikon_dual_event_banks_init(Object *obj)
{
    NikonDualEventBanksState *s = NIKON_DUAL_EVENT_BANKS(obj);
    unsigned i;

    for (i = 0; i < ARRAY_SIZE(s->region); i++) {
        NikonDualEventBanksRegion *r = &s->region[i];
        uint64_t size = i < 2 ? BANK_SIZE :
                        i < 5 ? (i == 3 ? 0x204 : 4) :
                        i == 13 ? 0xcc :
                        (i - 5) % 4 == 2 ? 0x400 : 0x200;

        r->parent = s;
        r->index = i;
        memory_region_init_io(&r->regs, obj, &nikon_dual_event_banks_ops, r,
                              "nikon.dual-event-banks", size);
        sysbus_init_mmio(SYS_BUS_DEVICE(obj), &r->regs);
        if (i < 2) {
            r->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, nikon_banks_frame, r);
        }
    }
    for (i = 0; i < 3; i++) {
        sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq[i]);
    }
    qdev_init_gpio_in_named(DEVICE(obj), nikon_dual_event_banks_input,
                            "pending-bit", 64);
    object_property_add_uint64_ptr(obj, "irq-levels", &s->irq_levels,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(obj, "primary-conversions",
                                   &s->primary_conversions, OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(obj, "primary-reuses",
                                   &s->primary_reuses, OBJ_PROP_FLAG_READ);
}

static void nikon_dual_event_banks_unrealize(DeviceState *dev)
{
    NikonDualEventBanksState *s = NIKON_DUAL_EVENT_BANKS(dev);

    for (unsigned i = 0; i < 2; i++) {
        timer_del(s->region[i].timer);
        s->region[i].active = false;
        nikon_banks_primary_clear(&s->primary[i]);
    }
}

static void nikon_dual_event_banks_finalize(Object *obj)
{
    NikonDualEventBanksState *s = NIKON_DUAL_EVENT_BANKS(obj);
    unsigned i;

    for (i = 0; i < 2; i++) {
        nikon_banks_primary_clear(&s->primary[i]);
        timer_free(s->region[i].timer);
        if (s->region[i].console) {
            qemu_graphic_console_close(s->region[i].console);
        }
    }
}

static const VMStateDescription nikon_dual_event_banks_vmstate = {
    .name = "nikon-dual-event-banks",
    .unmigratable = true,
};

static void nikon_dual_event_banks_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &nikon_dual_event_banks_vmstate;
    dc->user_creatable = false;
    dc->unrealize = nikon_dual_event_banks_unrealize;
    device_class_set_legacy_reset(dc, nikon_dual_event_banks_reset);
}

static const TypeInfo nikon_dual_event_banks_type = {
    .name = TYPE_NIKON_DUAL_EVENT_BANKS,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonDualEventBanksState),
    .instance_init = nikon_dual_event_banks_init,
    .instance_finalize = nikon_dual_event_banks_finalize,
    .class_init = nikon_dual_event_banks_class_init,
};

static void nikon_dual_event_banks_register_types(void)
{
    type_register_static(&nikon_dual_event_banks_type);
}
type_init(nikon_dual_event_banks_register_types)
