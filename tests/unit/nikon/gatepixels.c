/*
 * Independent fixed vectors for the bounded NV12 grid warp.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "hw/nikon/gatepixels.h"

static void word(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4; i++) {
        p[i] = value >> (8 * i);
    }
}

static void cell(uint8_t *p, int32_t x, int32_t y, unsigned exponent)
{
    memset(p, 0, 40);
    for (unsigned corner = 0; corner < 4; corner++) {
        word(p + corner * 8, (x + (corner & 1 ? (1U << exponent) * 256 : 0)) &
                             0x7fffff);
        word(p + corner * 8 + 4, (y + (corner & 2 ? 8 * 256 : 0)) &
                                 0x7fffff);
    }
    word(p + 32, 0x300 | exponent);
}

static void record(uint8_t *p, int32_t x, int32_t y)
{
    cell(p, x, y, 3);
}

static NikonGatePixels config(const uint8_t *input, const uint8_t *records)
{
    NikonGatePixels p = {
        .input = input, .input_size = 24,
        .src_width = 4, .src_height = 4,
        .records = records, .records_size = 40,
        .columns = 1, .rows = 1, .dst_width = 4, .dst_height = 4,
    };

    return p;
}

static const uint8_t ramp[24] = {
    0, 10, 20, 30, 40, 50, 60, 70,
    80, 90, 100, 110, 120, 130, 140, 150,
    0, 200, 40, 160, 80, 120, 120, 80,
};

static void identity(void)
{
    uint8_t table[40], out[24];
    NikonGatePixels p = config(ramp, table);

    record(table, 0, 0);
    word(table + 36, 0xdeadbeef);
    g_assert_true(nikon_gate_pixels_nv12(&p, out, sizeof(out)));
    g_assert_cmpmem(out, sizeof(out), ramp, sizeof(ramp));
}

static void translation(void)
{
    static const uint8_t expected[24] = {
        100, 110, 110, 110, 140, 150, 150, 150,
        140, 150, 150, 150, 140, 150, 150, 150,
        120, 80, 120, 80, 120, 80, 120, 80,
    };
    uint8_t table[40], out[24];
    NikonGatePixels p = config(ramp, table);

    record(table, 512, 512);
    g_assert_true(nikon_gate_pixels_nv12(&p, out, sizeof(out)));
    g_assert_cmpmem(out, sizeof(out), expected, sizeof(expected));
}

static void fractional(void)
{
    static const uint8_t expected[24] = {
        15, 25, 35, 40, 55, 65, 75, 80,
        95, 105, 115, 120, 125, 135, 145, 150,
        20, 180, 50, 150, 90, 110, 120, 80,
    };
    uint8_t table[40], out[24];
    NikonGatePixels p = config(ramp, table);

    record(table, 128, 64);
    g_assert_true(nikon_gate_pixels_nv12(&p, out, sizeof(out)));
    g_assert_cmpmem(out, sizeof(out), expected, sizeof(expected));
}

static void nonaffine(void)
{
    uint8_t input[16 * 8 * 3 / 2], table[40], out[24];
    NikonGatePixels p = config(input, table);

    p.src_width = 16;
    p.src_height = 8;
    p.input_size = sizeof(input);
    for (unsigned y = 0; y < 8; y++) {
        for (unsigned x = 0; x < 16; x++) {
            input[y * 16 + x] = x + 16 * y;
        }
    }
    memset(input + 128, 128, 64);
    record(table, 0, 0);
    /* BR changes x to x*(1+y); the mixed derivative is nonzero. */
    word(table + 24, 72 * 256);
    g_assert_true(nikon_gate_pixels_nv12(&p, out, sizeof(out)));
    for (unsigned y = 0; y < 4; y++) {
        for (unsigned x = 0; x < 4; x++) {
            g_assert_cmpuint(out[y * 4 + x], ==, x * (1 + y) + 16 * y);
        }
    }
    g_assert_cmpuint(out[10], ==, 38);
    g_assert_cmpuint(out[15], ==, 60);
}

static void crop(void)
{
    uint8_t input[54], table[40], out[24];
    NikonGatePixels p = config(input, table);

    p.src_width = 6;
    p.src_height = 6;
    p.input_size = sizeof(input);
    for (unsigned i = 0; i < sizeof(input); i++) {
        input[i] = i;
    }
    record(table, 256, 256);
    g_assert_true(nikon_gate_pixels_nv12(&p, out, sizeof(out)));
    for (unsigned y = 0; y < 4; y++) {
        for (unsigned x = 0; x < 4; x++) {
            g_assert_cmpuint(out[y * 4 + x], ==, (y + 1) * 6 + x + 1);
        }
    }
    /* Independent half-grid UV average: U(0.5,0.5)=(36+38+42+44)/4. */
    g_assert_cmpuint(out[16], ==, 40);
    g_assert_cmpuint(out[17], ==, 41);
}

static void cell_boundaries(void)
{
    uint8_t input[66 * 10 * 3 / 2], table[160], out[sizeof(input)];
    NikonGatePixels p = config(input, table);

    p.src_width = p.dst_width = 66;
    p.src_height = p.dst_height = 10;
    p.input_size = sizeof(input);
    p.records_size = sizeof(table);
    p.columns = p.rows = 2;
    for (unsigned i = 0; i < sizeof(input); i++) {
        input[i] = (i * 17 + 9) & 255;
    }
    for (unsigned y = 0; y < 2; y++) {
        for (unsigned x = 0; x < 2; x++) {
            cell(table + 40 * (2 * y + x), x * 64 * 256, y * 8 * 256,
                 x ? 3 : 6);
        }
    }
    g_assert_true(nikon_gate_pixels_nv12(&p, out, sizeof(out)));
    g_assert_cmpmem(out, sizeof(out), input, sizeof(input));
    word(table + 32, 0x303); /* A non-final column must retain extent64. */
    memset(out, 0xa5, sizeof(out));
    g_assert_false(nikon_gate_pixels_nv12(&p, out, sizeof(out)));
    for (unsigned i = 0; i < sizeof(out); i++) {
        g_assert_cmpuint(out[i], ==, 0xa5);
    }
}

static void signed_edges(void)
{
    uint8_t table[40], out[24];
    NikonGatePixels p = config(ramp, table);

    record(table, -128, -64);
    g_assert_true(nikon_gate_pixels_nv12(&p, out, sizeof(out)));
    g_assert_cmpuint(out[0], ==, 0);
    g_assert_cmpuint(out[5], ==, 35);
    for (unsigned i = 0; i < 8; i++) {
        word(table + 4 * i, 0x400000); /* signed23 minimum */
    }
    g_assert_true(nikon_gate_pixels_nv12(&p, out, sizeof(out)));
    for (unsigned i = 0; i < 16; i++) {
        g_assert_cmpuint(out[i], ==, 0);
    }
    for (unsigned i = 0; i < 8; i++) {
        word(table + 4 * i, 0x3fffff); /* signed23 maximum */
    }
    g_assert_true(nikon_gate_pixels_nv12(&p, out, sizeof(out)));
    for (unsigned i = 0; i < 16; i++) {
        g_assert_cmpuint(out[i], ==, 150);
    }
}

static void tail_widths(void)
{
    static const unsigned widths[] = { 8, 10, 16, 18, 32, 34, 64 };
    uint8_t input[64 * 8 * 3 / 2], table[40], out[64 * 2 * 3 / 2];
    NikonGatePixels p = config(input, table);

    p.src_width = 64;
    p.src_height = 8;
    p.input_size = sizeof(input);
    p.dst_height = 2;
    for (unsigned y = 0; y < 8; y++) {
        for (unsigned x = 0; x < 64; x++) {
            input[y * 64 + x] = x + 16 * y;
        }
    }
    memset(input + 512, 128, 256);
    for (unsigned i = 0; i < G_N_ELEMENTS(widths); i++) {
        unsigned width = widths[i], exponent = 3;

        while ((1U << exponent) < width) {
            exponent++;
        }
        p.dst_width = width;
        cell(table, 0, 0, exponent);
        g_assert_true(nikon_gate_pixels_nv12(&p, out, width * 3));
        /* Fixed ramp oracle also distinguishes /16 or /32 from hardcoded /64. */
        for (unsigned x = 0; x < width; x++) {
            g_assert_cmpuint(out[x], ==, x);
            g_assert_cmpuint(out[width + x], ==, x + 16);
        }
        word(table + 32, 0x300 | (exponent == 6 ? 5 : exponent + 1));
        memset(out, 0xa5, sizeof(out));
        g_assert_false(nikon_gate_pixels_nv12(&p, out, width * 3));
        for (unsigned x = 0; x < sizeof(out); x++) {
            g_assert_cmpuint(out[x], ==, 0xa5);
        }
    }
}

static void coordinate_rounding(void)
{
    uint8_t input[24] = { 0, 254 }, table[40], out[24];
    NikonGatePixels p = config(input, table);

    record(table, 128, 0);
    word(table + 8, 132);
    word(table + 24, 132);
    /* x=1 gives Q8 coordinate128.5, rounded to129 before pixel sampling. */
    g_assert_true(nikon_gate_pixels_nv12(&p, out, sizeof(out)));
    g_assert_cmpuint(out[0], ==, 127);
    g_assert_cmpuint(out[1], ==, 128);
}

static void reject(const NikonGatePixels *p, uint8_t *out, size_t len)
{
    uint8_t before[25];

    g_assert_cmpuint(len, <=, sizeof(before));
    memcpy(before, out, len);
    g_assert_false(nikon_gate_pixels_nv12(p, out, len));
    g_assert_cmpmem(out, len, before, len);
}

static void invalid(void)
{
    uint8_t input[24], table[80], out[25];
    NikonGatePixels good = config(input, table), p;

    memcpy(input, ramp, sizeof(input));
    memset(out, 0xa5, sizeof(out));
    record(table, 0, 0);
    record(table + 40, 64 * 256, 0);
    reject(NULL, out, 24);
    for (unsigned field = 0; field < 14; field++) {
        p = good;
        switch (field) {
        case 0:
            p.input = NULL;
            break;
        case 1:
            p.records = NULL;
            break;
        case 2:
            p.src_width = 0;
            break;
        case 3:
            p.src_height = 3;
            break;
        case 4:
            p.dst_width = 4098;
            break;
        case 5:
            p.dst_height = 0;
            break;
        case 6:
            p.columns = 0;
            break;
        case 7:
            p.rows = 513;
            break;
        case 8:
            p.dst_width = 66;
            break;
        case 9:
            p.dst_height = 10;
            break;
        case 10:
            p.input_size--;
            break;
        case 11:
            p.input_size++;
            break;
        case 12:
            p.records_size--;
            break;
        case 13:
            p.columns = 65;
            break;
        }
        reject(&p, out, 24);
    }
    reject(&good, out, 23);
    reject(&good, out, 25);
    g_assert_false(nikon_gate_pixels_nv12(&good, NULL, 24));
    word(table + 32, 0);
    word(table + 36, 0x303); /* padding must not substitute for the tag */
    reject(&good, out, 24);
    record(table, 0, 0);
    for (unsigned i = 0; i < 8; i++) {
        uint8_t saved = table[i * 4 + 3];

        table[i * 4 + 3] = 0x80;
        reject(&good, out, 24);
        table[i * 4 + 3] = saved;
    }
    /* Even an unused cell outside the output crop must pass validation. */
    p = good;
    p.rows = 2;
    p.records_size = sizeof(table);
    word(table + 72, 0x305);
    reject(&p, out, 24);
    p = good;
    p.input = out;
    reject(&p, out, 24);
    p = good;
    p.records = out;
    reject(&p, out, 24);
    p = good;
    p.input = (const uint8_t *)(UINTPTR_MAX - 4);
    reject(&p, out, 24);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/nikon/gatepixels/identity", identity);
    g_test_add_func("/nikon/gatepixels/translation", translation);
    g_test_add_func("/nikon/gatepixels/fractional", fractional);
    g_test_add_func("/nikon/gatepixels/nonaffine", nonaffine);
    g_test_add_func("/nikon/gatepixels/crop", crop);
    g_test_add_func("/nikon/gatepixels/cells", cell_boundaries);
    g_test_add_func("/nikon/gatepixels/signed", signed_edges);
    g_test_add_func("/nikon/gatepixels/tails", tail_widths);
    g_test_add_func("/nikon/gatepixels/rounding", coordinate_rounding);
    g_test_add_func("/nikon/gatepixels/invalid", invalid);
    return g_test_run();
}
