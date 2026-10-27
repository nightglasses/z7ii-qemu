/*
 * Fixed numerical vectors for the functional monochrome RAW12 operator.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "hw/nikon/pairedpixels.h"

typedef struct PairedOutputs {
    uint16_t half[4], residual[4], grid[4];
    uint8_t y[16], uv[8];
} PairedOutputs;

static NikonPairedPixelConfig config(void)
{
    NikonPairedPixelConfig p = {
        .width = 4, .height = 4, .output_width = 4, .output_height = 4,
        .grid_width = 2, .grid_height = 2,
        .denoise = { .threshold = 7, .low = 1, .high = 3,
                     .axial = 3, .diagonal = 1, .retain = 31 },
        .a = { 1024, 0, 0, 0, 1024, 0, 0, 0, 1024 },
        .c = { 1024, 0, 0, 0, 1024, 0, 0, 0, 1024 },
        .b = { 153, 301, 58, -86, -170, 256, 256, -214, -42 },
    };

    return p;
}

static bool run(const NikonPairedPixelConfig *p, const uint16_t *raw,
                const uint16_t *prior, const uint16_t *residual,
                PairedOutputs *out)
{
    return nikon_paired_pixels(p, raw, prior, residual, out->half,
                               out->residual, out->grid, out->y, out->uv);
}

static void fill(uint16_t *data, unsigned count, uint16_t value)
{
    unsigned i;

    for (i = 0; i < count; i++) {
        data[i] = value;
    }
}

static void constant_output(const PairedOutputs *out, uint16_t luma,
                             uint16_t residual, uint8_t y, uint8_t uv)
{
    unsigned i;

    for (i = 0; i < 4; i++) {
        g_assert_cmpuint(out->half[i], ==, luma);
        g_assert_cmpuint(out->residual[i], ==, residual);
        g_assert_cmpuint(out->grid[i], ==, luma);
    }
    for (i = 0; i < 16; i++) {
        g_assert_cmpuint(out->y[i], ==, y);
    }
    for (i = 0; i < 8; i++) {
        g_assert_cmpuint(out->uv[i], ==, uv);
    }
}

static void constant(void)
{
    NikonPairedPixelConfig p = config();
    uint16_t raw[16];
    PairedOutputs out;

    fill(raw, ARRAY_SIZE(raw), 1000);
    g_assert_true(run(&p, raw, NULL, NULL, &out));
    constant_output(&out, 1000, 0, 63, 128);
    fill(raw, ARRAY_SIZE(raw), 4095);
    g_assert_true(run(&p, raw, NULL, NULL, &out));
    constant_output(&out, 4095, 0, 255, 128);
    fill(raw, ARRAY_SIZE(raw), 0);
    g_assert_true(run(&p, raw, NULL, NULL, &out));
    constant_output(&out, 0, 0, 0, 128);
}

static void history(void)
{
    NikonPairedPixelConfig p = config();
    uint16_t raw[16], prior[4], residual[4];
    const uint16_t varied_prior[] = { 900, 1000, 1100, 1257 };
    const uint16_t expected_half[] = { 975, 1000, 1025, 1000 };
    const uint16_t expected_residual[] = { 100, 0, 100, 257 };
    const uint8_t expected_y[] = {
        61, 61, 63, 63, 61, 61, 63, 63,
        64, 64, 63, 63, 64, 64, 63, 63,
    };
    PairedOutputs out;
    unsigned i;

    fill(raw, ARRAY_SIZE(raw), 1000);
    fill(prior, ARRAY_SIZE(prior), 900);
    fill(residual, ARRAY_SIZE(residual), 0);
    g_assert_true(run(&p, raw, prior, residual, &out));
    constant_output(&out, 975, 100, 61, 128);
    /* Missing residual history retains the maximum quarter history weight. */
    g_assert_true(run(&p, raw, prior, NULL, &out));
    constant_output(&out, 975, 100, 61, 128);
    fill(residual, ARRAY_SIZE(residual), 128);
    g_assert_true(run(&p, raw, prior, residual, &out));
    constant_output(&out, 988, 100, 62, 128);
    fill(residual, ARRAY_SIZE(residual), 256);
    g_assert_true(run(&p, raw, prior, residual, &out));
    constant_output(&out, 1000, 100, 63, 128);
    fill(residual, ARRAY_SIZE(residual), 4095);
    g_assert_true(run(&p, raw, prior, residual, &out));
    constant_output(&out, 1000, 100, 63, 128);
    g_assert_true(run(&p, raw, NULL, residual, &out));
    constant_output(&out, 1000, 0, 63, 128);
    fill(prior, ARRAY_SIZE(prior), 744);
    fill(residual, ARRAY_SIZE(residual), 0);
    g_assert_true(run(&p, raw, prior, residual, &out));
    constant_output(&out, 936, 256, 59, 128);
    fill(prior, ARRAY_SIZE(prior), 743);
    g_assert_true(run(&p, raw, prior, residual, &out));
    constant_output(&out, 1000, 257, 63, 128);
    g_assert_true(run(&p, raw, varied_prior, residual, &out));
    g_assert_cmpmem(out.half, sizeof(out.half),
                    expected_half, sizeof(expected_half));
    g_assert_cmpmem(out.grid, sizeof(out.grid),
                    expected_half, sizeof(expected_half));
    g_assert_cmpmem(out.residual, sizeof(out.residual),
                    expected_residual, sizeof(expected_residual));
    g_assert_cmpmem(out.y, sizeof(out.y), expected_y, sizeof(expected_y));
    for (i = 0; i < ARRAY_SIZE(out.uv); i++) {
        g_assert_cmpuint(out.uv[i], ==, 128);
    }
}

static void spatial(void)
{
    NikonPairedPixelConfig p = config();
    uint16_t raw[16];
    const uint16_t step[] = {
        1000, 1000, 2000, 2000, 1000, 1000, 2000, 2000,
        1000, 1000, 2000, 2000, 1000, 1000, 2000, 2000,
    };
    const uint16_t expected_half[] = { 1063, 1938, 1063, 1938 };
    const uint16_t threshold_half[] = { 1192, 1032, 1032, 1000 };
    const uint16_t expected_residual[4] = { 0 };
    const uint8_t expected_y[] = {
        63, 70, 117, 125, 63, 70, 117, 125,
        63, 70, 117, 125, 63, 70, 117, 125,
    };
    const uint8_t threshold_y[] = {
        63, 71, 63, 63, 71, 95, 71, 63,
        63, 71, 63, 63, 63, 63, 63, 63,
    };
    const uint8_t chroma[] = { 194, 62, 249, 7, 194, 62, 249, 7 };
    PairedOutputs out;
    unsigned i;

    fill(raw, ARRAY_SIZE(raw), 1000);
    raw[5] = 4095; /* An isolated interior impulse has coherent neighbors. */
    g_assert_true(run(&p, raw, NULL, NULL, &out));
    constant_output(&out, 1000, 0, 63, 128);
    raw[5] = 2024; /* The exact 1024 impulse threshold is retained. */
    g_assert_true(run(&p, raw, NULL, NULL, &out));
    g_assert_cmpmem(out.half, sizeof(out.half),
                    threshold_half, sizeof(threshold_half));
    g_assert_cmpmem(out.grid, sizeof(out.grid),
                    threshold_half, sizeof(threshold_half));
    g_assert_cmpmem(out.y, sizeof(out.y), threshold_y, sizeof(threshold_y));
    raw[5] = 2025;
    g_assert_true(run(&p, raw, NULL, NULL, &out));
    constant_output(&out, 1000, 0, 63, 128);
    g_assert_true(run(&p, step, NULL, NULL, &out));
    g_assert_cmpmem(out.half, sizeof(out.half),
                    expected_half, sizeof(expected_half));
    g_assert_cmpmem(out.grid, sizeof(out.grid),
                    expected_half, sizeof(expected_half));
    g_assert_cmpmem(out.residual, sizeof(out.residual),
                    expected_residual, sizeof(expected_residual));
    g_assert_cmpmem(out.y, sizeof(out.y), expected_y, sizeof(expected_y));
    for (i = 0; i < ARRAY_SIZE(out.uv); i++) {
        g_assert_cmpuint(out.uv[i], ==, 128);
    }
    memset(p.b + 3, 0, 6 * sizeof(p.b[0]));
    p.b[3] = 512;
    p.b[6] = -512;
    g_assert_true(run(&p, step, NULL, NULL, &out));
    g_assert_cmpmem(out.y, sizeof(out.y), expected_y, sizeof(expected_y));
    g_assert_cmpmem(out.uv, sizeof(out.uv), chroma, sizeof(chroma));
}

static void crop_grid(void)
{
    NikonPairedPixelConfig p = config();
    const uint16_t raw[] = { 0, 100, 200, 300, 400, 500, 600, 700, 800 };
    const uint16_t expected_half[] = { 225, 357, 619, 750 };
    const uint16_t expected_grid[] = { 50, 182, 444, 575 };
    const uint16_t expected_residual[4] = { 0 };
    const uint8_t expected_y[] = { 25, 31, 41, 47 };
    const uint8_t expected_uv[] = { 128, 128 };
    PairedOutputs out, saved;

    p.width = p.height = 3;
    p.output_width = p.output_height = 2;
    p.x = p.y = 1;
    memset(&out, 0xa5, sizeof(out));
    saved = out;
    g_assert_true(run(&p, raw, NULL, NULL, &out));
    g_assert_cmpmem(out.half, sizeof(out.half),
                    expected_half, sizeof(expected_half));
    g_assert_cmpmem(out.grid, sizeof(out.grid),
                    expected_grid, sizeof(expected_grid));
    g_assert_cmpmem(out.residual, sizeof(out.residual),
                    expected_residual, sizeof(expected_residual));
    g_assert_cmpmem(out.y, sizeof(expected_y), expected_y, sizeof(expected_y));
    g_assert_cmpmem(out.uv, sizeof(expected_uv),
                    expected_uv, sizeof(expected_uv));
    g_assert_cmpmem(out.y + 4, sizeof(out.y) - 4,
                    saved.y + 4, sizeof(saved.y) - 4);
    g_assert_cmpmem(out.uv + 2, sizeof(out.uv) - 2,
                    saved.uv + 2, sizeof(saved.uv) - 2);
}

static void matrices(void)
{
    NikonPairedPixelConfig p = config();
    uint16_t raw[16];
    PairedOutputs out;
    unsigned i;

    fill(raw, ARRAY_SIZE(raw), 1000);
    p.a[4] = 512;
    p.a[8] = 256;
    memset(p.c, 0, sizeof(p.c));
    p.c[1] = p.c[5] = p.c[6] = 1024;
    memset(p.b, 0, sizeof(p.b));
    p.b[0] = p.b[4] = p.b[8] = 512;
    /* A gives (1000,500,250), C gives (500,250,1000), then B adds UV bias. */
    g_assert_true(run(&p, raw, NULL, NULL, &out));
    for (i = 0; i < ARRAY_SIZE(out.y); i++) {
        g_assert_cmpuint(out.y[i], ==, 31);
    }
    for (i = 0; i < ARRAY_SIZE(out.uv); i += 2) {
        g_assert_cmpuint(out.uv[i], ==, 144);
        g_assert_cmpuint(out.uv[i + 1], ==, 191);
    }
    for (i = 0; i < ARRAY_SIZE(out.half); i++) {
        g_assert_cmpuint(out.half[i], ==, 1000);
        g_assert_cmpuint(out.grid[i], ==, 1000);
    }
    p = config();
    fill(raw, ARRAY_SIZE(raw), 4095);
    memset(p.b, 0, sizeof(p.b));
    p.b[0] = -512;
    p.b[3] = -2048;
    p.b[6] = 2047;
    g_assert_true(run(&p, raw, NULL, NULL, &out));
    for (i = 0; i < ARRAY_SIZE(out.y); i++) {
        g_assert_cmpuint(out.y[i], ==, 0);
    }
    for (i = 0; i < ARRAY_SIZE(out.uv); i += 2) {
        g_assert_cmpuint(out.uv[i], ==, 0);
        g_assert_cmpuint(out.uv[i + 1], ==, 255);
    }
    p = config();
    p.a[0] = p.a[4] = p.a[8] = -4096;
    g_assert_true(run(&p, raw, NULL, NULL, &out));
    constant_output(&out, 4095, 0, 0, 128);
    p = config();
    p.c[0] = p.c[4] = p.c[8] = -4096;
    g_assert_true(run(&p, raw, NULL, NULL, &out));
    constant_output(&out, 4095, 0, 0, 128);
}

static void gains(void)
{
    static const struct {
        uint16_t sample, gain[3];
        bool enabled[3];
        uint8_t y, u, v;
    } vectors[] = {
        /* Disabled coefficients have no effect, even zero and maximum. */
        { 1000, { 0, 65535, 128 }, { false, false, false }, 63, 191, 191 },
        { 1000, { 512, 128, 64 }, { true, true, true }, 125, 159, 144 },
        { 1000, { 0, 0, 0 }, { true, true, true }, 0, 128, 128 },
        { 1000, { 256, 256, 256 }, { true, true, true }, 63, 191, 191 },
        { 4095, { 65535, 65535, 65535 }, { true, true, true }, 255, 255, 255 },
        { 0, { 65535, 65535, 65535 }, { true, true, true }, 0, 128, 128 },
        /* 15*128/256 is7.5: rounding to8 crosses the byte rounding boundary. */
        { 15, { 128, 127, 129 }, { true, true, true }, 1, 128, 129 },
        { 1000, { 0, 0, 512 }, { true, false, true }, 0, 191, 253 },
    };
    NikonPairedPixelConfig p;
    uint16_t raw[16];
    PairedOutputs out;
    unsigned i, j;

    for (i = 0; i < ARRAY_SIZE(vectors); i++) {
        p = config();
        memcpy(p.gain, vectors[i].gain, sizeof(p.gain));
        memcpy(p.gain_enabled, vectors[i].enabled, sizeof(p.gain_enabled));
        memset(p.b, 0, sizeof(p.b));
        p.b[0] = p.b[4] = p.b[8] = 512;
        fill(raw, ARRAY_SIZE(raw), vectors[i].sample);
        g_test_message("gain vector%u", i);
        g_assert_true(run(&p, raw, NULL, NULL, &out));
        for (j = 0; j < ARRAY_SIZE(out.half); j++) {
            g_assert_cmpuint(out.half[j], ==, vectors[i].sample);
            g_assert_cmpuint(out.residual[j], ==, 0);
            g_assert_cmpuint(out.grid[j], ==, vectors[i].sample);
        }
        for (j = 0; j < ARRAY_SIZE(out.y); j++) {
            g_assert_cmpuint(out.y[j], ==, vectors[i].y);
        }
        for (j = 0; j < ARRAY_SIZE(out.uv); j += 2) {
            g_assert_cmpuint(out.uv[j], ==, vectors[i].u);
            g_assert_cmpuint(out.uv[j + 1], ==, vectors[i].v);
        }
    }

    p = config();
    p.gain[0] = 512;
    p.gain[1] = 128;
    p.gain[2] = 64;
    p.gain_enabled[0] = p.gain_enabled[1] = p.gain_enabled[2] = true;
    memset(p.a, 0, sizeof(p.a));
    p.a[1] = p.a[5] = p.a[6] = 1024;
    memset(p.c, 0, sizeof(p.c));
    p.c[0] = p.c[1] = 512;
    p.c[3] = p.c[8] = 1024;
    memset(p.b, 0, sizeof(p.b));
    p.b[0] = p.b[4] = p.b[8] = 512;
    fill(raw, ARRAY_SIZE(raw), 1000);
    /* Gain=(2000,500,250), A=(500,250,2000), C=(375,500,2000). */
    g_assert_true(run(&p, raw, NULL, NULL, &out));
    for (i = 0; i < ARRAY_SIZE(out.y); i++) {
        g_assert_cmpuint(out.y[i], ==, 23);
    }
    for (i = 0; i < ARRAY_SIZE(out.uv); i += 2) {
        g_assert_cmpuint(out.uv[i], ==, 159);
        g_assert_cmpuint(out.uv[i + 1], ==, 253);
    }

    p = config();
    p.gain[0] = 65535;
    p.gain_enabled[0] = true;
    p.a[0] = 512;
    memset(p.b, 0, sizeof(p.b));
    p.b[0] = p.b[4] = p.b[8] = 512;
    /* Clamp gained R to4095 before A halves it to2048; G/B stay1000. */
    g_assert_true(run(&p, raw, NULL, NULL, &out));
    for (i = 0; i < ARRAY_SIZE(out.y); i++) {
        g_assert_cmpuint(out.y[i], ==, 128);
    }
    for (i = 0; i < ARRAY_SIZE(out.uv); i++) {
        g_assert_cmpuint(out.uv[i], ==, 191);
    }
}

static void curves(void)
{
    NikonPairedPixelConfig p = config();
    uint16_t raw[16];
    PairedOutputs out;
    unsigned i;

    fill(raw, ARRAY_SIZE(raw), 1000);
    fill(p.curve[0], ARRAY_SIZE(p.curve[0]), 65535);
    fill(p.curve[1], ARRAY_SIZE(p.curve[1]), 65535);
    g_assert_true(run(&p, raw, NULL, NULL, &out));
    constant_output(&out, 1000, 0, 63, 128);

    p.curve_enabled[0] = true;
    fill(p.curve[0], ARRAY_SIZE(p.curve[0]), 512);
    g_assert_true(run(&p, raw, NULL, NULL, &out));
    constant_output(&out, 1000, 0, 31, 128);
    fill(p.curve[0], ARRAY_SIZE(p.curve[0]), 0);
    g_assert_true(run(&p, raw, NULL, NULL, &out));
    constant_output(&out, 1000, 0, 0, 128);

    fill(p.curve[0], ARRAY_SIZE(p.curve[0]), 1024);
    fill(p.curve[1], ARRAY_SIZE(p.curve[1]), 2048);
    p.curve_enabled[1] = true;
    g_assert_true(run(&p, raw, NULL, NULL, &out));
    constant_output(&out, 1000, 0, 125, 128);
    fill(p.curve[1], ARRAY_SIZE(p.curve[1]), 1024);
    g_assert_true(run(&p, raw, NULL, NULL, &out));
    constant_output(&out, 1000, 0, 63, 128);

    /* Sample1000 is midway between knots62/63: gain768 yields sample750. */
    p.curve_enabled[1] = false;
    fill(p.curve[0], ARRAY_SIZE(p.curve[0]), 0);
    p.curve[0][62] = 512;
    p.curve[0][63] = 1024;
    g_assert_true(run(&p, raw, NULL, NULL, &out));
    constant_output(&out, 1000, 0, 47, 128);

    /* RAW4095 uses knot256: gain960 yields3839, which rounds to byte240. */
    fill(raw, ARRAY_SIZE(raw), 4095);
    p.curve[0][255] = 0;
    p.curve[0][256] = 1024;
    g_assert_true(run(&p, raw, NULL, NULL, &out));
    constant_output(&out, 4095, 0, 240, 128);

    fill(raw, ARRAY_SIZE(raw), 1000);
    fill(p.curve[0], ARRAY_SIZE(p.curve[0]), 1536);
    fill(p.curve[1], ARRAY_SIZE(p.curve[1]), 512);
    p.curve[1][62] = p.curve[1][63] = 2048;
    p.curve_enabled[1] = true;
    /* Forward1000->1500->750; reversed order would yield3000. */
    g_assert_true(run(&p, raw, NULL, NULL, &out));
    constant_output(&out, 1000, 0, 47, 128);

    fill(p.curve[0], ARRAY_SIZE(p.curve[0]), 65535);
    fill(p.curve[1], ARRAY_SIZE(p.curve[1]), 512);
    /* Stage0 saturates to4095 before stage1 rounds half to2048. */
    g_assert_true(run(&p, raw, NULL, NULL, &out));
    constant_output(&out, 1000, 0, 128, 128);
    fill(raw, ARRAY_SIZE(raw), 0);
    g_assert_true(run(&p, raw, NULL, NULL, &out));
    constant_output(&out, 0, 0, 0, 128);

    p = config();
    p.curve_enabled[0] = true;
    fill(p.curve[0], ARRAY_SIZE(p.curve[0]), 512);
    p.gain_enabled[0] = true;
    p.gain[0] = 65535;
    p.a[0] = 512;
    memset(p.b, 0, sizeof(p.b));
    p.b[0] = p.b[4] = p.b[8] = 512;
    fill(raw, ARRAY_SIZE(raw), 1000);
    /* Curve500 -> gained/clamped R4095 -> A2048; G/B remain500. */
    g_assert_true(run(&p, raw, NULL, NULL, &out));
    for (i = 0; i < ARRAY_SIZE(out.y); i++) {
        g_assert_cmpuint(out.y[i], ==, 128);
    }
    for (i = 0; i < ARRAY_SIZE(out.uv); i++) {
        g_assert_cmpuint(out.uv[i], ==, 159);
    }
    for (i = 0; i < ARRAY_SIZE(out.half); i++) {
        g_assert_cmpuint(out.half[i], ==, 1000);
        g_assert_cmpuint(out.residual[i], ==, 0);
        g_assert_cmpuint(out.grid[i], ==, 1000);
    }
}

static void rejected(const NikonPairedPixelConfig *p, const uint16_t *raw,
                      const uint16_t *prior, const uint16_t *residual)
{
    PairedOutputs out, saved;

    memset(&out, 0xa5, sizeof(out));
    saved = out;
    g_assert_false(run(p, raw, prior, residual, &out));
    g_assert_cmpmem(&out, sizeof(out), &saved, sizeof(saved));
}

static void invalid(void)
{
    NikonPairedPixelConfig good = config(), p;
    uint16_t raw[16], prior[4], residual[4];
    PairedOutputs out, saved;
    unsigned i;

    fill(raw, ARRAY_SIZE(raw), 1000);
    fill(prior, ARRAY_SIZE(prior), 900);
    fill(residual, ARRAY_SIZE(residual), 0);
    rejected(NULL, raw, prior, residual);
    rejected(&good, NULL, prior, residual);
    for (i = 0; i < 16; i++) {
        p = good;
        switch (i) {
        case 0:
            p.width = 0;
            break;
        case 1:
            p.height = 0;
            break;
        case 2:
            p.width = 4097;
            break;
        case 3:
            p.height = 4097;
            break;
        case 4:
            p.output_width = 0;
            break;
        case 5:
            p.output_height = 0;
            break;
        case 6:
            p.output_width = 3;
            break;
        case 7:
            p.output_height = 3;
            break;
        case 8:
            p.x = UINT_MAX;
            break;
        case 9:
            p.y = UINT_MAX;
            break;
        case 10:
            p.x = 1;
            break;
        case 11:
            p.y = 1;
            break;
        case 12:
            p.grid_width = 0;
            break;
        case 13:
            p.grid_height = 0;
            break;
        case 14:
            p.grid_width = 5;
            break;
        case 15:
            p.grid_height = 5;
            break;
        }
        rejected(&p, raw, prior, residual);
    }
    for (i = 0; i < 6; i++) {
        p = good;
        switch (i) {
        case 0:
            p.a[0] = -4097;
            break;
        case 1:
            p.a[8] = 4096;
            break;
        case 2:
            p.c[0] = -4097;
            break;
        case 3:
            p.c[8] = 4096;
            break;
        case 4:
            p.b[0] = -2049;
            break;
        case 5:
            p.b[8] = 2048;
            break;
        }
        rejected(&p, raw, prior, residual);
    }
    raw[15] = 4096;
    rejected(&good, raw, prior, residual);
    raw[15] = 1000;
    prior[3] = 4096;
    rejected(&good, raw, prior, residual);
    prior[3] = 900;
    residual[3] = 4096;
    rejected(&good, raw, prior, residual);
    rejected(&good, raw, NULL, residual);
    residual[3] = 0;
    for (i = 0; i < 5; i++) {
        memset(&out, 0xa5, sizeof(out));
        saved = out;
        g_assert_false(nikon_paired_pixels(&good, raw, prior, residual,
            i == 0 ? NULL : out.half, i == 1 ? NULL : out.residual,
            i == 2 ? NULL : out.grid, i == 3 ? NULL : out.y,
            i == 4 ? NULL : out.uv));
        g_assert_cmpmem(&out, sizeof(out), &saved, sizeof(saved));
    }
}

static void noise(void)
{
    static const uint8_t expected[][16] = {
        { 77, 62, 49, 48, 54, 50, 47, 47,
          47, 47, 47, 47, 47, 47, 47, 47 },
        { 63, 71, 74, 60, 59, 69, 69, 74,
          60, 69, 69, 74, 74, 60, 60, 69 },
    };
    /* The second primitive mask is outside the recovered64-row table. */
    static const uint32_t masks[] = { 0xf8200000, 0x80200003 };
    NikonPairedPixelConfig p = config();
    uint16_t raw[16];
    PairedOutputs out, again;

    memset(p.b, 0, sizeof(p.b));
    p.b[0] = 512;
    p.noise_negative = p.noise_positive = 1020;
    p.noise[0] = (NikonPairedNoise) { .enabled = 1, .gain = 255 };
    fill(raw, ARRAY_SIZE(raw), 1000);
    for (unsigned i = 0; i < ARRAY_SIZE(masks); i++) {
        p.noise[0].feedback = masks[i];
        g_assert_true(run(&p, raw, NULL, NULL, &out));
        g_assert_cmpmem(out.y, sizeof(out.y), expected[i], sizeof(expected[i]));
        for (unsigned j = 0; j < ARRAY_SIZE(out.uv); j++) {
            g_assert_cmpuint(out.uv[j], ==, 128);
        }
        for (unsigned j = 0; j < ARRAY_SIZE(out.half); j++) {
            g_assert_cmpuint(out.half[j], ==, 1000);
            g_assert_cmpuint(out.residual[j], ==, 0);
            g_assert_cmpuint(out.grid[j], ==, 1000);
        }
        /* Job-local state cannot leak between calls, even after other masks. */
        g_assert_true(run(&p, raw, NULL, NULL, &again));
        g_assert_cmpmem(&out, sizeof(out), &again, sizeof(again));
    }
    for (unsigned channel = 1; channel < 3; channel++) {
        NikonPairedNoise n = p.noise[0];

        memset(p.noise, 0, sizeof(p.noise));
        p.noise[channel] = n;
        p.b[0] = 0;
        p.b[channel] = 512;
        g_assert_true(run(&p, raw, NULL, NULL, &out));
        g_assert_cmpmem(out.y, sizeof(out.y), expected[1], sizeof(expected[1]));
        p.b[channel] = 0;
        p.noise[0] = n;
    }
    memset(p.noise + 1, 0, sizeof(p.noise) - sizeof(p.noise[0]));
    p.b[0] = 512;
    p.noise[0].enabled = 0;
    g_assert_true(run(&p, raw, NULL, NULL, &out));
    constant_output(&out, 1000, 0, 63, 128);
    p.noise[0].enabled = 1;
    p.noise_negative = p.noise_positive = 0;
    p.noise[0].offset = 128;
    p.noise[0].shift = 4;
    g_assert_true(run(&p, raw, NULL, NULL, &out));
    constant_output(&out, 1000, 0, 63, 128);
    p.noise[0].offset = 255;
    g_assert_true(run(&p, raw, NULL, NULL, &out));
    constant_output(&out, 1000, 0, 64, 128);
}

static void quad(void)
{
    NikonPairedPixelConfig p = config();
    uint16_t raw[16];
    PairedOutputs out;

    fill(raw, ARRAY_SIZE(raw), 1000);
    p.quad_enabled = true;
    p.quad_limit = 3000;
    p.quad_gain[0] = 768;
    p.quad_gain[1] = 512;
    p.quad_gain[2] = 256;
    p.quad_gain[3] = 1024;
    p.quad_offset[0] = p.quad_offset[2] = -16;
    p.quad_offset[1] = 16;
    p.quad_pedestal[0] = 1632;
    p.quad_pedestal[1] = 12;
    p.quad_pedestal[2] = 20;
    p.quad_pedestal[3] = 99;
    memset(p.b, 0, sizeof(p.b));
    p.b[0] = p.b[4] = p.b[8] = 512;
    g_assert_true(run(&p, raw, NULL, NULL, &out));
    for (unsigned i = 0; i < ARRAY_SIZE(out.y); i++) {
        g_assert_cmpuint(out.y[i], ==, 188); /* R clipped at3000. */
    }
    for (unsigned i = 0; i < ARRAY_SIZE(out.uv); i += 2) {
        g_assert_cmpuint(out.uv[i], ==, 176); /* G=(1028+512)/2=770. */
        g_assert_cmpuint(out.uv[i + 1], ==, 255); /* B=2099, plus2048. */
    }
    for (unsigned i = 0; i < ARRAY_SIZE(out.half); i++) {
        g_assert_cmpuint(out.half[i], ==, 1000);
        g_assert_cmpuint(out.grid[i], ==, 1000);
        g_assert_cmpuint(out.residual[i], ==, 0);
    }
    p.curve_enabled[0] = true;
    fill(p.curve[0], 257, 512);
    g_assert_true(run(&p, raw, NULL, NULL, &out));
    g_assert_cmpuint(out.y[0], ==, 94);
    g_assert_cmpuint(out.uv[0], ==, 152);
    g_assert_cmpuint(out.uv[1], ==, 194);
    p.curve_enabled[0] = false;
    p.quad_offset[0] = -1001;
    p.quad_gain[0] = 256;
    p.quad_pedestal[0] = 1;
    g_assert_true(run(&p, raw, NULL, NULL, &out));
    g_assert_cmpuint(out.y[0], ==, 0); /* Negative half rounds away from0. */
    p.quad_limit = 0;
    g_assert_true(run(&p, raw, NULL, NULL, &out));
    constant_output(&out, 1000, 0, 0, 128);
    p.quad_enabled = false;
    g_assert_true(run(&p, raw, NULL, NULL, &out));
    g_assert_cmpuint(out.y[0], ==, 63);
    p.quad_gain[3] = 16384;
    rejected(&p, raw, NULL, NULL);
    p.quad_gain[3] = 16383;
    p.quad_offset[2] = -4097;
    rejected(&p, raw, NULL, NULL);
    p.quad_offset[2] = 4096;
    rejected(&p, raw, NULL, NULL);
    p.quad_offset[2] = 4095;
    p.quad_limit = 32768;
    rejected(&p, raw, NULL, NULL);
}

static void noise_invalid(void)
{
    static const uint32_t masks[] = { 0, 1, 0x80000000, 0x80000001 };
    NikonPairedPixelConfig p = config();
    uint16_t raw[16];

    fill(raw, ARRAY_SIZE(raw), 1000);
    p.noise[0].enabled = 1;
    for (unsigned i = 0; i < ARRAY_SIZE(masks); i++) {
        p.noise[0].feedback = masks[i];
        rejected(&p, raw, NULL, NULL);
    }
    p.noise[0].feedback = 0xf8040000;
    p.noise[0].shift = 16;
    rejected(&p, raw, NULL, NULL);
    p.noise[0].shift = 4;
    p.noise[0].enabled = 2;
    rejected(&p, raw, NULL, NULL);
    p.noise[0].enabled = 1;
    p.noise_negative = 1024;
    rejected(&p, raw, NULL, NULL);
    p.noise_negative = 1020;
    p.noise_positive = 1024;
    rejected(&p, raw, NULL, NULL);
}

typedef struct FilterOutputs {
    uint16_t half[4], residual[4], grid[16];
    uint8_t y[16], uv[8];
} FilterOutputs;

static int64_t filter_reference_round(int64_t value, unsigned divisor)
{
    lldiv_t q = lldiv(value, divisor);

    if (2 * llabs(q.rem) >= divisor) {
        q.quot += value < 0 ? -1 : 1;
    }
    return q.quot;
}

static void filter_reference(const NikonPairedFilter *f, uint16_t image[16])
{
    static const int delta[4][2] = { { -1, 0 }, { 1, 0 },
                                    { 0, -1 }, { 0, 1 } };

    for (unsigned pass = 0; pass < (f->refine ? 2 : 1); pass++) {
        uint16_t source[16];

        memcpy(source, image, sizeof(source));
        for (int y = 0; y < 4; y++) {
            for (int x = 0; x < 4; x++) {
                unsigned at = y * 4 + x, count = 0;
                int64_t sum = 0, s = source[at] * 4;

                for (unsigned direction = 0; direction < 4; direction++) {
                    int dx = delta[direction][0], dy = delta[direction][1];
                    int sx = CLAMP(x + dx * f->radius, 0, 3);
                    int sy = CLAMP(y + dy * f->radius, 0, 3);

                    if (f->refine && ((pass == 0) != (dy == 0))) {
                        continue;
                    }
                    sum += source[sy * 4 + sx] * 4;
                    count++;
                }
                if (f->enabled) {
                    int64_t residual = sum / count - s;
                    int64_t threshold = MIN(16383, f->base +
                        filter_reference_round(f->slope * s, 16383));

                    if (f->clipped) {
                        residual = CLAMP(residual, -threshold, threshold);
                    }
                    s += filter_reference_round(residual * (16383 - f->retain),
                                                16383);
                }
                image[at] = MIN(4095, filter_reference_round(
                    CLAMP(s, 0, 16383), 4));
            }
        }
    }
}

static FilterOutputs filter_check(NikonPairedPixelConfig p,
                                  const uint16_t raw[16], bool history)
{
    NikonPairedPixelConfig neutral = p;
    const uint16_t prior[] = { 600, 1800, 1300, 2600 };
    const uint16_t old_residual[] = { 0, 128, 256, 1 };
    FilterOutputs before = { 0 }, actual = { 0 }, expected = { 0 };

    p.grid_width = neutral.grid_width = 4;
    p.grid_height = neutral.grid_height = 4;
    neutral.filter = (NikonPairedFilter) { 0 };
    g_assert_true(nikon_paired_pixels(&neutral, raw, NULL, NULL, before.half,
        before.residual, before.grid, before.y, before.uv));
    memcpy(expected.grid, before.grid, sizeof(expected.grid));
    filter_reference(&p.filter, expected.grid);
    for (unsigned block = 0; block < 4; block++) {
        unsigned first = (block / 2) * 8 + (block % 2) * 2;
        unsigned sum = expected.grid[first] + expected.grid[first + 1] +
                       expected.grid[first + 4] + expected.grid[first + 5];
        unsigned current = filter_reference_round(sum, 4), weight = 0;

        if (history) {
            expected.residual[block] = abs((int)current - prior[block]);
            if (expected.residual[block] <= 256) {
                weight = filter_reference_round(64 *
                    (256 - MIN(old_residual[block], 256)), 256);
            }
        }
        sum = 0;
        for (unsigned i = 0; i < 4; i++) {
            unsigned at = first + (i / 2) * 4 + i % 2;

            expected.grid[at] = filter_reference_round(
                expected.grid[at] * (256 - weight) + prior[block] * weight,
                256);
            sum += expected.grid[at];
        }
        expected.half[block] = filter_reference_round(sum, 4);
    }
    for (unsigned i = 0; i < 16; i++) {
        expected.y[i] = MIN(255, filter_reference_round(expected.grid[i], 16));
    }
    memset(expected.uv, 128, sizeof(expected.uv));
    g_assert_true(nikon_paired_pixels(&p, raw, history ? prior : NULL,
        history ? old_residual : NULL, actual.half, actual.residual,
        actual.grid, actual.y, actual.uv));
    g_assert_cmpmem(&actual, sizeof(actual), &expected, sizeof(expected));
    return actual;
}

static void filter(void)
{
    static const uint16_t step[] = {
        0, 0, 4095, 4095, 0, 0, 4095, 4095,
        0, 0, 4095, 4095, 0, 0, 4095, 4095,
    };
    static const uint16_t axial[] = { 128, 1152, 2943, 3967 };
    static const uint16_t refined[] = { 256, 1792, 2304, 3839 };
    static const uint16_t clipped[] = { 1, 513, 3582, 4094 };
    uint16_t patterns[4][16];
    NikonPairedPixelConfig p = config();
    FilterOutputs out;

    for (unsigned i = 0; i < 16; i++) {
        patterns[0][i] = 1000;
        patterns[1][i] = i == 5 ? 2024 : 1000;
        patterns[2][i] = step[i];
        patterns[3][i] = 1000 + ((i * 29 + i / 4 * 13) % 81) - 40;
    }
    for (unsigned mode = 0; mode < 4; mode++) {
        p.filter = (NikonPairedFilter) {
            .enabled = true, .refine = mode & 1, .clipped = mode & 2,
            .retain = 6087, .base = 50, .radius = 1, .slope = 50,
        };
        for (unsigned vector = 0; vector < 4; vector++) {
            filter_check(p, patterns[vector], false);
            filter_check(p, patterns[vector], true);
        }
    }
    p.filter = (NikonPairedFilter) { .enabled = true, .radius = 1 };
    out = filter_check(p, step, false);
    for (unsigned i = 0; i < 16; i++) {
        g_assert_cmpuint(out.grid[i], ==, axial[i % 4]);
    }
    p.filter.refine = true;
    out = filter_check(p, step, false);
    for (unsigned i = 0; i < 16; i++) {
        g_assert_cmpuint(out.grid[i], ==, refined[i % 4]);
    }
    p.filter.refine = false;
    p.filter.clipped = true;
    p.filter.base = 4;
    out = filter_check(p, step, false);
    for (unsigned i = 0; i < 16; i++) {
        g_assert_cmpuint(out.grid[i], ==, clipped[i % 4]);
    }
    p.filter.base = 2; /* Positive half-LSB rounds up at the left border. */
    out = filter_check(p, step, false);
    g_assert_cmpuint(out.grid[0], ==, 1);
    g_assert_cmpuint(out.grid[3], ==, 4095);
    for (unsigned field = 0; field < 4; field++) {
        FilterOutputs baseline, changed;

        p.filter = (NikonPairedFilter) {
            .enabled = true, .clipped = true, .retain = 4000,
            .base = 32, .radius = 1,
        };
        baseline = filter_check(p, patterns[1], false);
        switch (field) {
        case 0:
            p.filter.retain = 12000;
            break;
        case 1:
            p.filter.base = 512;
            break;
        case 2:
            p.filter.radius = 2;
            break;
        case 3:
            p.filter.slope = 12000;
            break;
        }
        changed = filter_check(p, patterns[1], false);
        g_assert_cmpint(memcmp(baseline.grid, changed.grid,
                               sizeof(baseline.grid)), !=, 0);
    }
    for (unsigned identity = 0; identity < 2; identity++) {
        p.filter = (NikonPairedFilter) {
            .enabled = true, .radius = identity ? 15 : 0,
            .retain = identity ? 16383 : 0,
        };
        filter_check(p, patterns[3], false);
    }
    for (unsigned value = 0; value <= 4095; value += 4095) {
        fill(patterns[0], 16, value);
        p.filter = (NikonPairedFilter) {
            .enabled = true, .refine = true, .clipped = true,
            .base = 16383, .radius = 15, .slope = 16383,
        };
        out = filter_check(p, patterns[0], false);
        for (unsigned i = 0; i < 16; i++) {
            g_assert_cmpuint(out.grid[i], ==, value);
        }
    }
    p.filter = (NikonPairedFilter) { 0 };
    FilterOutputs before = filter_check(p, patterns[3], false);
    uint64_t old_energy = 0, new_energy = 0;

    p.filter = (NikonPairedFilter) { .enabled = true, .radius = 1 };
    out = filter_check(p, patterns[3], false);
    for (unsigned i = 0; i < 16; i++) {
        old_energy += (int)(before.grid[i] - 1000) *
                      (int)(before.grid[i] - 1000);
        new_energy += (int)(out.grid[i] - 1000) * (int)(out.grid[i] - 1000);
    }
    g_assert_cmpuint(new_energy, <, old_energy);
}

static void filter_invalid(void)
{
    NikonPairedPixelConfig p = config();
    uint16_t raw[16];

    fill(raw, 16, 1000);
    g_assert_false(nikon_paired_filter_valid(NULL));
    g_assert_true(nikon_paired_filter_valid(&p.filter));
    for (unsigned test = 0; test < 10; test++) {
        p.filter = (NikonPairedFilter) { .enabled = test >= 6 };
        switch (test) {
        case 0:
            p.filter.refine = true;
            break;
        case 1:
            p.filter.clipped = true;
            break;
        case 2:
            p.filter.retain = 1;
            break;
        case 3:
            p.filter.base = 1;
            break;
        case 4:
            p.filter.radius = 1;
            break;
        case 5:
            p.filter.slope = 1;
            break;
        case 6:
            p.filter.retain = 16384;
            break;
        case 7:
            p.filter.base = 16384;
            break;
        case 8:
            p.filter.radius = 16;
            break;
        case 9:
            p.filter.slope = 16384;
            break;
        }
        g_assert_false(nikon_paired_filter_valid(&p.filter));
        rejected(&p, raw, NULL, NULL);
    }
}

static NikonPairedDenoise denoise_config(void)
{
    return (NikonPairedDenoise) {
        .threshold = 103, .low = 43, .high = 45,
        .axial = 17, .diagonal = 10, .bias = 12, .retain = 8,
    };
}

static void denoise_reference(const NikonPairedDenoise *n, uint16_t image[16])
{
    static const int neighbors[8][2] = {
        { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 },
        { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 },
    };
    uint16_t source[16];
    unsigned lo = n->threshold + n->low + n->bias;
    unsigned hi = n->threshold + n->high + n->bias;

    memcpy(source, image, sizeof(source));
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            int64_t sums[2] = { 0 }, center = 4 * source[y * 4 + x];

            for (unsigned k = 0; k < 8; k++) {
                unsigned sx = CLAMP(x + neighbors[k][0], 0, 3);
                unsigned sy = CLAMP(y + neighbors[k][1], 0, 3);
                int64_t residual = source[sy * 4 + sx] * 4 - center;
                unsigned distance = llabs(residual), numerator;

                if (distance <= lo) {
                    numerator = hi - lo;
                } else if (distance >= hi) {
                    numerator = 0;
                } else {
                    numerator = hi - distance;
                }
                sums[k / 4] += numerator * residual;
            }
            center += filter_reference_round(n->axial * sums[0] +
                n->diagonal * sums[1], 4 * (hi - lo) *
                (n->retain + n->axial + n->diagonal));
            image[y * 4 + x] = MIN(4095, filter_reference_round(
                CLAMP(center, 0, 16383), 4));
        }
    }
}

static FilterOutputs denoise_check(NikonPairedPixelConfig p,
                                   const uint16_t raw[16], bool history)
{
    NikonPairedPixelConfig neutral = p;
    const uint16_t prior[] = { 600, 1800, 1300, 2600 };
    const uint16_t old_residual[] = { 0, 128, 256, 1 };
    FilterOutputs before = { 0 }, actual = { 0 }, expected = { 0 };

    p.grid_width = neutral.grid_width = 4;
    p.grid_height = neutral.grid_height = 4;
    neutral.denoise = config().denoise;
    g_assert_true(nikon_paired_pixels(&neutral, raw, NULL, NULL, before.half,
        before.residual, before.grid, before.y, before.uv));
    memcpy(expected.grid, before.grid, sizeof(expected.grid));
    denoise_reference(&p.denoise, expected.grid);
    for (unsigned block = 0; block < 4; block++) {
        unsigned first = (block / 2) * 8 + (block % 2) * 2;
        unsigned sum = expected.grid[first] + expected.grid[first + 1] +
                       expected.grid[first + 4] + expected.grid[first + 5];
        unsigned current = filter_reference_round(sum, 4), weight = 0;

        if (history) {
            expected.residual[block] = abs((int)current - prior[block]);
            if (expected.residual[block] <= 256) {
                weight = filter_reference_round(64 *
                    (256 - MIN(old_residual[block], 256)), 256);
            }
        }
        sum = 0;
        for (unsigned i = 0; i < 4; i++) {
            unsigned at = first + (i / 2) * 4 + i % 2;

            expected.grid[at] = filter_reference_round(
                expected.grid[at] * (256 - weight) + prior[block] * weight,
                256);
            sum += expected.grid[at];
        }
        expected.half[block] = filter_reference_round(sum, 4);
    }
    for (unsigned i = 0; i < 16; i++) {
        expected.y[i] = MIN(255, filter_reference_round(expected.grid[i], 16));
    }
    memset(expected.uv, 128, sizeof(expected.uv));
    g_assert_true(nikon_paired_pixels(&p, raw, history ? prior : NULL,
        history ? old_residual : NULL, actual.half, actual.residual,
        actual.grid, actual.y, actual.uv));
    g_assert_cmpmem(&actual, sizeof(actual), &expected, sizeof(expected));
    return actual;
}

static void denoise(void)
{
    const uint16_t expected[] = { 1009, 1015, 1025, 1031 };
    const uint16_t gate_edges[][4] = {
        { 1002, 1005, 1048, 1051 }, /* Difference156 equals U. */
        { 1002, 1010, 1043, 1051 }, /* Difference156 is halfway in L..U. */
        { 1002, 1016, 1037, 1051 }, /* Difference156 equals L. */
    };
    const uint16_t signed_ties[] = { 1002, 1005, 1051, 1054 };
    NikonPairedPixelConfig p = config();
    uint16_t raw[16];
    FilterOutputs out;

    p.denoise = denoise_config();
    for (unsigned i = 0; i < 16; i++) {
        raw[i] = i % 4 < 2 ? 1008 : 1032;
    }
    out = denoise_check(p, raw, false);
    for (unsigned i = 0; i < 16; i++) {
        g_assert_cmpuint(out.grid[i], ==, expected[i % 4]);
    }
    g_assert_cmpuint(out.y[3], ==, 64); /* Spatial-only produces65. */
    denoise_check(p, raw, true);
    for (unsigned edge = 0; edge < 3; edge++) {
        p.denoise.threshold = 99 + edge;
        for (unsigned i = 0; i < 16; i++) {
            raw[i] = i % 4 < 2 ? 1000 : 1052;
        }
        out = denoise_check(p, raw, false);
        for (unsigned i = 0; i < 16; i++) {
            g_assert_cmpuint(out.grid[i], ==, gate_edges[edge][i % 4]);
        }
    }
    p.denoise = denoise_config();
    p.denoise.axial = 10;
    for (unsigned i = 0; i < 16; i++) {
        raw[i] = i % 4 < 2 ? 1000 : 1056;
    }
    out = denoise_check(p, raw, false);
    for (unsigned i = 0; i < 16; i++) {
        g_assert_cmpuint(out.grid[i], ==, signed_ties[i % 4]);
    }
    p.denoise = denoise_config();
    for (unsigned level = 0; level <= 4095; level += 1365) {
        fill(raw, 16, level);
        out = denoise_check(p, raw, false);
        for (unsigned i = 0; i < 16; i++) {
            g_assert_cmpuint(out.grid[i], ==, level);
        }
    }
    for (unsigned pattern = 0; pattern < 3; pattern++) {
        for (unsigned i = 0; i < 16; i++) {
            raw[i] = pattern == 0 ? (i % 4 < 2 ? 0 : 4095) :
                     pattern == 1 ? (i == 5 ? 2024 : 1000) :
                     1000 + ((i * 29 + i / 4 * 13) % 81) - 40;
        }
        out = denoise_check(p, raw, false);
        if (pattern == 0) {
            NikonPairedPixelConfig neutral = p;

            neutral.denoise = config().denoise;
            FilterOutputs before = denoise_check(neutral, raw, false);

            g_assert_cmpmem(out.grid, sizeof(out.grid), before.grid,
                            sizeof(before.grid));
        }
        denoise_check(p, raw, true);
    }
    for (unsigned field = 0; field < 6; field++) {
        bool changed = false;

        for (unsigned amplitude = 1; amplitude <= 96; amplitude++) {
            NikonPairedPixelConfig other = p;
            FilterOutputs a, b;

            p.denoise = (NikonPairedDenoise) {
                .threshold = 90, .low = 30, .high = 32,
                .axial = 17, .diagonal = 10, .bias = 6, .retain = 8,
            };
            other.denoise = p.denoise;
            switch (field) {
            case 0: other.denoise.threshold = 103; break;
            case 1:
                other.denoise.low = 43;
                other.denoise.high = 45;
                break;
            case 2: other.denoise.axial = 10; break;
            case 3: other.denoise.diagonal = 1; break;
            case 4: other.denoise.bias = 12; break;
            case 5: other.denoise.retain = 31; break;
            }
            for (unsigned i = 0; i < 16; i++) {
                raw[i] = i % 4 < 2 ? 1000 : 1000 + amplitude;
            }
            a = denoise_check(p, raw, false);
            b = denoise_check(other, raw, false);
            changed |= memcmp(a.grid, b.grid, sizeof(a.grid)) != 0;
        }
        g_assert_true(changed);
    }
    p.denoise = denoise_config();
    for (unsigned i = 0; i < 16; i++) {
        raw[i] = i % 4 < 2 ? 992 : 1008;
    }
    NikonPairedPixelConfig neutral = p;
    uint64_t old_energy = 0, new_energy = 0;

    neutral.denoise = config().denoise;
    FilterOutputs before = denoise_check(neutral, raw, false);

    out = denoise_check(p, raw, false);
    for (unsigned i = 0; i < 16; i++) {
        int a = (int)before.grid[i] - 1000, b = (int)out.grid[i] - 1000;

        old_energy += a * a;
        new_energy += b * b;
    }
    g_assert_cmpuint(new_energy, <, old_energy);
    p.filter = (NikonPairedFilter) {
        .enabled = true, .refine = true, .clipped = true,
        .retain = 6087, .base = 50, .radius = 1, .slope = 50,
    };
    denoise_check(p, raw, true);
}

static void denoise_invalid(void)
{
    NikonPairedPixelConfig p = config();
    uint16_t raw[16];

    fill(raw, 16, 1000);
    g_assert_false(nikon_paired_denoise_valid(NULL));
    g_assert_true(nikon_paired_denoise_valid(&p.denoise));
    for (unsigned test = 0; test < 14; test++) {
        p.denoise = denoise_config();
        switch (test) {
        case 0: p.denoise = (NikonPairedDenoise) { 0 }; break;
        case 1: p.denoise.threshold = 6; break;
        case 2: p.denoise.threshold = 104; break;
        case 3: p.denoise.low = 0; p.denoise.high = 2; break;
        case 4: p.denoise.low = 44; p.denoise.high = 46; break;
        case 5: p.denoise.high = 44; break;
        case 6: p.denoise.threshold = 44; break;
        case 7: p.denoise.axial = 2; break;
        case 8: p.denoise.axial = 18; break;
        case 9: p.denoise.diagonal = 0; break;
        case 10: p.denoise.diagonal = 11; break;
        case 11: p.denoise.axial = 9; break;
        case 12: p.denoise.bias = 13; break;
        case 13: p.denoise.retain = 32; break;
        }
        g_assert_false(nikon_paired_denoise_valid(&p.denoise));
        rejected(&p, raw, NULL, NULL);
    }
    p.denoise = denoise_config();
    p.denoise.retain = 7;
    rejected(&p, raw, NULL, NULL);
}

static void cfa_fill(uint16_t *raw, unsigned width, unsigned height,
                     NikonRawEncoding encoding, const uint16_t lanes[4])
{
    for (unsigned y = 0; y < height; y++) {
        for (unsigned x = 0; x < width; x++) {
            unsigned lane = (encoding - 1) ^ ((y & 1) * 2 + (x & 1));

            raw[y * width + x] = lanes[lane];
        }
    }
}

static void cfa_neutral(void)
{
    const uint16_t lanes[] = { 1024, 2048, 1024, 512 };

    for (unsigned encoding = NIKON_RAW_RGGB;
         encoding <= NIKON_RAW_BGGR; encoding++) {
        for (unsigned crop = 0; crop < 9; crop++) {
            NikonPairedPixelConfig p = config();
            uint16_t raw[16];
            PairedOutputs out;

            p.encoding = encoding;
            p.quad_enabled = true;
            p.quad_limit = 4095;
            p.quad_gain[0] = 1024;
            p.quad_gain[1] = 512;
            p.quad_gain[2] = 1024;
            p.quad_gain[3] = 2048;
            p.x = crop % 3;
            p.y = crop / 3;
            p.output_width = p.output_height = 2;
            cfa_fill(raw, 4, 4, p.encoding, lanes);
            memset(&out, 0xa5, sizeof(out));
            g_assert_true(run(&p, raw, NULL, NULL, &out));
            for (unsigned i = 0; i < 4; i++) {
                g_assert_cmpuint(out.half[i], ==, 1152);
                g_assert_cmpuint(out.grid[i], ==, 1152);
                g_assert_cmpuint(out.residual[i], ==, 0);
                g_assert_cmpuint(out.y[i], ==, 128);
            }
            g_assert_cmpuint(out.uv[0], ==, 128);
            g_assert_cmpuint(out.uv[1], ==, 128);
            for (unsigned i = 4; i < sizeof(out.y); i++) {
                g_assert_cmphex(out.y[i], ==, 0xa5);
            }
            for (unsigned i = 2; i < sizeof(out.uv); i++) {
                g_assert_cmphex(out.uv[i], ==, 0xa5);
            }
            if (!crop) {
                p.width = p.height = 2;
                p.grid_width = p.grid_height = 1;
                cfa_fill(raw, 2, 2, p.encoding, lanes);
                memset(&out, 0xa5, sizeof(out));
                g_assert_true(run(&p, raw, NULL, NULL, &out));
                g_assert_cmpuint(out.half[0], ==, 1152);
                g_assert_cmpuint(out.grid[0], ==, 1152);
                for (unsigned i = 0; i < 4; i++) {
                    g_assert_cmpuint(out.y[i], ==, 128);
                }
                g_assert_cmpuint(out.uv[0], ==, 128);
                g_assert_cmpuint(out.uv[1], ==, 128);
            }
        }
    }
}

static void cfa_color_history(void)
{
    const uint16_t lanes[] = { 3072, 1024, 1024, 512 };

    for (unsigned encoding = NIKON_RAW_RGGB;
         encoding <= NIKON_RAW_BGGR; encoding++) {
        NikonPairedPixelConfig p = config();
        uint16_t raw[16], prior[4], residual[4] = { 0 };
        PairedOutputs out;

        p.encoding = encoding;
        cfa_fill(raw, 4, 4, p.encoding, lanes);
        for (unsigned history = 0; history < 2; history++) {
            fill(prior, 4, 1508);
            g_assert_true(run(&p, raw, history ? prior : NULL,
                               residual, &out));
            for (unsigned i = 0; i < 4; i++) {
                g_assert_cmpuint(out.half[i], ==, history ? 1433 : 1408);
                g_assert_cmpuint(out.grid[i], ==, history ? 1433 : 1408);
                g_assert_cmpuint(out.residual[i], ==, history ? 100 : 0);
            }
            /* RGB=(3072,1024,512), then a common +25 for history. B's
             * chroma row sums are zero: history must not desaturate it.
             */
            for (unsigned i = 0; i < 16; i++) {
                g_assert_cmpuint(out.y[i], ==, history ? 100 : 99);
            }
            for (unsigned i = 0; i < 8; i += 2) {
                g_assert_cmpuint(out.uv[i], ==, 91);
                g_assert_cmpuint(out.uv[i + 1], ==, 195);
            }
        }
    }
}

static void cfa_edge(void)
{
    static const uint8_t expected[2][8] = {
        { 0, 8, 16, 64, 112, 120, 128, 128 },
        { 0, 0, 8, 16, 64, 112, 120, 128 },
    };

    for (unsigned encoding = NIKON_RAW_RGGB;
         encoding <= NIKON_RAW_BGGR; encoding++) {
        NikonPairedPixelConfig p = config();
        uint16_t raw[32], half[8], residual[8], grid[4];
        uint8_t yplane[32], uv[16];

        p.encoding = encoding;
        p.width = p.output_width = 8;
        memset(p.b, 0, sizeof(p.b));
        p.b[0] = 512; /* Observe R independently of RGB-to-Y coefficients. */
        for (unsigned y = 0; y < 4; y++) {
            for (unsigned x = 0; x < 8; x++) {
                unsigned lane = (encoding - 1) ^ ((y & 1) * 2 + (x & 1));

                raw[y * 8 + x] = lane == 0 && x >= 4 ? 2048 : 0;
            }
        }
        g_assert_true(nikon_paired_pixels(&p, raw, NULL, NULL, half,
                                          residual, grid, yplane, uv));
        /* The same-color spatial plane is {0,256,1792,2048}; bilinear
         * interpolation inserts midpoints and replicates same-phase borders.
         */
        for (unsigned y = 0; y < 4; y++) {
            g_assert_cmpmem(yplane + y * 8, 8,
                            expected[(encoding - 1) & 1], 8);
        }
        for (unsigned i = 0; i < sizeof(uv); i++) {
            g_assert_cmpuint(uv[i], ==, 128);
        }
    }
}

static void cfa_invalid(void)
{
    NikonPairedPixelConfig p = config();
    uint16_t raw[16];

    fill(raw, 16, 1000);
    p.encoding = NIKON_RAW_BGGR + 1;
    rejected(&p, raw, NULL, NULL);
    p.encoding = (NikonRawEncoding)-1;
    rejected(&p, raw, NULL, NULL);
    p.encoding = NIKON_RAW_RGGB;
    p.width = 5;
    rejected(&p, raw, NULL, NULL);
    p.width = 4;
    p.height = 5;
    rejected(&p, raw, NULL, NULL);
    p.height = 4;
    raw[3] = 4096;
    rejected(&p, raw, NULL, NULL);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/nikon/pairedpixels/constant", constant);
    g_test_add_func("/nikon/pairedpixels/history", history);
    g_test_add_func("/nikon/pairedpixels/spatial", spatial);
    g_test_add_func("/nikon/pairedpixels/crop-grid", crop_grid);
    g_test_add_func("/nikon/pairedpixels/matrices", matrices);
    g_test_add_func("/nikon/pairedpixels/gains", gains);
    g_test_add_func("/nikon/pairedpixels/curves", curves);
    g_test_add_func("/nikon/pairedpixels/invalid", invalid);
    g_test_add_func("/nikon/pairedpixels/noise", noise);
    g_test_add_func("/nikon/pairedpixels/noise-invalid", noise_invalid);
    g_test_add_func("/nikon/pairedpixels/quad", quad);
    g_test_add_func("/nikon/pairedpixels/filter", filter);
    g_test_add_func("/nikon/pairedpixels/filter-invalid", filter_invalid);
    g_test_add_func("/nikon/pairedpixels/denoise", denoise);
    g_test_add_func("/nikon/pairedpixels/denoise-invalid", denoise_invalid);
    g_test_add_func("/nikon/pairedpixels/cfa-neutral", cfa_neutral);
    g_test_add_func("/nikon/pairedpixels/cfa-color-history", cfa_color_history);
    g_test_add_func("/nikon/pairedpixels/cfa-edge", cfa_edge);
    g_test_add_func("/nikon/pairedpixels/cfa-invalid", cfa_invalid);
    return g_test_run();
}
