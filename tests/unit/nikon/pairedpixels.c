/*
 * Fixed numerical vectors for bounded mono/CFA RAW12 processing.
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
        .detail = { .limit = 4, .strength = 0, .lane = 8 },
        .adaptive = { .base = 50, .slope = 60, .cap = 16, .weight = 100,
                      .precision = 10, .dose = 0 },
        .radial = { .center_x = 2, .center_y = 2 },
        .colorfilter = { .shift = 2, .weight = 8 },
        .a = { 1024, 0, 0, 0, 1024, 0, 0, 0, 1024 },
        .c = { 1024, 0, 0, 0, 1024, 0, 0, 0, 1024 },
        .b = { 153, 301, 58, -86, -170, 256, 256, -214, -42 },
    };

    for (unsigned i = 0; i < 2; i++) {
        NikonPairedColorBlock *b = &p.colorfilter.block[i];

        b->negative_limit = i ? 5 : 0;
        b->positive_limit = i ? 9 : 0;
        for (unsigned j = 0; j < 2; j++) {
            for (unsigned d = 0; d < 4; d++) {
                b->branch[j].shift[d] = i && !j && d == 2 ? 3 : 2;
            }
        }
    }
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

static int detail_compare(const void *a, const void *b)
{
    return (int)*(const uint16_t *)a - *(const uint16_t *)b;
}

static void detail_reference(const NikonPairedDetail *d, uint16_t image[16])
{
    uint16_t source[16];

    memcpy(source, image, sizeof(source));
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            uint16_t neighbors[9];
            unsigned count = 0;
            int64_t center = 4 * source[y * 4 + x], residual;

            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    unsigned sx = CLAMP(x + dx, 0, 3);
                    unsigned sy = CLAMP(y + dy, 0, 3);

                    neighbors[count++] = source[sy * 4 + sx];
                }
            }
            qsort(neighbors, count, sizeof(neighbors[0]), detail_compare);
            residual = CLAMP(4 * neighbors[count / 2] - center,
                             -(int64_t)d->limit, d->limit);
            center += filter_reference_round(residual * d->strength * d->lane,
                                              64 * 16);
            image[y * 4 + x] = MIN(4095, filter_reference_round(
                CLAMP(center, 0, 16383), 4));
        }
    }
}

static FilterOutputs detail_check(NikonPairedPixelConfig p,
                                  const uint16_t raw[16], bool history)
{
    NikonPairedPixelConfig neutral = p;
    const uint16_t prior[] = { 600, 1800, 1300, 2600 };
    const uint16_t old_residual[] = { 0, 128, 256, 1 };
    FilterOutputs before = { 0 }, actual = { 0 }, expected = { 0 };

    p.grid_width = neutral.grid_width = 4;
    p.grid_height = neutral.grid_height = 4;
    neutral.detail = config().detail;
    g_assert_true(nikon_paired_pixels(&neutral, raw, NULL, NULL, before.half,
        before.residual, before.grid, before.y, before.uv));
    memcpy(expected.grid, before.grid, sizeof(expected.grid));
    detail_reference(&p.detail, expected.grid);
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

static void detail(void)
{
    NikonPairedPixelConfig p = config();
    uint16_t raw[16];
    FilterOutputs out;

    p.detail = (NikonPairedDetail) { .limit = 31, .strength = 32, .lane = 16 };
    for (unsigned sign = 0; sign < 2; sign++) {
        fill(raw, 16, sign ? 1044 : 1016);
        raw[5] = sign ? 1012 : 1048;
        out = detail_check(p, raw, false);
        /* +/-31*32*16/1024 are signed half ties, rounded to+/-16. */
        g_assert_cmpuint(out.grid[5], ==, sign ? 1032 : 1028);
        g_assert_cmpuint(out.grid[0], ==, sign ? 1042 : 1018);
        g_assert_cmpuint(out.grid[6], ==, sign ? 1042 : 1018);
        g_assert_cmpuint(out.y[5], ==, sign ? 65 : 64);
        detail_check(p, raw, true);
    }
    fill(raw, 16, 1016);
    raw[5] = 1048;
    FilterOutputs baseline = detail_check(p, raw, false);

    for (unsigned field = 0; field < 3; field++) {
        NikonPairedPixelConfig other = p;
        FilterOutputs changed;

        switch (field) {
        case 0: other.detail.limit = 4; break;
        case 1: other.detail.strength = 0; break;
        case 2: other.detail.lane = 8; break;
        }
        changed = detail_check(other, raw, false);
        g_assert_cmpint(memcmp(baseline.grid, changed.grid,
                               sizeof(baseline.grid)), !=, 0);
    }
    NikonPairedPixelConfig neutral = p;
    uint64_t before_energy = 0, after_energy = 0;

    neutral.detail = config().detail;
    FilterOutputs before = detail_check(neutral, raw, false);

    for (unsigned i = 0; i < 16; i++) {
        int a = (int)before.grid[i] - 1016;
        int b = (int)baseline.grid[i] - 1016;

        before_energy += a * a;
        after_energy += b * b;
    }
    g_assert_cmpuint(after_energy, <, before_energy);
    for (unsigned value = 0; value <= 4095; value += 1365) {
        fill(raw, 16, value);
        out = detail_check(p, raw, false);
        for (unsigned i = 0; i < 16; i++) {
            g_assert_cmpuint(out.grid[i], ==, value);
        }
    }
    for (unsigned i = 0; i < 16; i++) {
        raw[i] = i % 4 < 2 ? 0 : 4095;
    }
    out = detail_check(p, raw, false);
    before = detail_check(neutral, raw, false);
    g_assert_cmpmem(&out, sizeof(out), &before, sizeof(before));
    for (unsigned strength = 0; strength <= 32; strength++) {
        p.detail.strength = strength;
        p.detail.limit = 4 + strength % 28;
        p.detail.lane = 8 + strength % 9;
        for (unsigned i = 0; i < 16; i++) {
            raw[i] = 1000 + ((i * 29 + i / 4 * 13 + strength) % 81) - 40;
        }
        detail_check(p, raw, false);
        detail_check(p, raw, true);
    }
    p.detail = (NikonPairedDetail) { .limit = 31, .strength = 32, .lane = 16 };
    p.denoise = denoise_config();
    p.filter = (NikonPairedFilter) {
        .enabled = true, .refine = true, .clipped = true,
        .retain = 6087, .base = 50, .radius = 1, .slope = 50,
    };
    detail_check(p, raw, true);
}

static void detail_invalid(void)
{
    NikonPairedPixelConfig p = config();
    uint16_t raw[16];

    fill(raw, 16, 1000);
    g_assert_false(nikon_paired_detail_valid(NULL));
    g_assert_true(nikon_paired_detail_valid(&p.detail));
    for (unsigned test = 0; test < 6; test++) {
        p.detail = (NikonPairedDetail) {
            .limit = 31, .strength = 32, .lane = 16,
        };
        switch (test) {
        case 0: p.detail = (NikonPairedDetail) { 0 }; break;
        case 1: p.detail.limit = 3; break;
        case 2: p.detail.limit = 32; break;
        case 3: p.detail.strength = 33; break;
        case 4: p.detail.lane = 7; break;
        case 5: p.detail.lane = 17; break;
        }
        g_assert_false(nikon_paired_detail_valid(&p.detail));
        rejected(&p, raw, NULL, NULL);
    }
}

static void adaptive_reference(const NikonPairedAdaptive *a, uint16_t image[16])
{
    uint16_t source[16];

    memcpy(source, image, sizeof(source));
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            int at = y * 4 + x;
            int64_t c = source[at];
            int64_t limit = MIN(a->cap, a->base +
                filter_reference_round((int64_t)a->slope * c, 4095));
            int64_t sum = source[MAX(y - 1, 0) * 4 + x] +
                source[MIN(y + 1, 3) * 4 + x] +
                source[y * 4 + MAX(x - 1, 0)] +
                source[y * 4 + MIN(x + 1, 3)];
            int64_t n = sum - 4 * c;
            int64_t w = (int64_t)a->weight * a->dose;
            int64_t divisor = 4 * (16 * (INT64_C(1) << a->precision) + w);

            /* Compare rationals before division; never round the axial mean. */
            if (n > 4 * limit) {
                n = 4 * limit;
            } else if (n < -4 * limit) {
                n = -4 * limit;
            }
            image[at] = CLAMP(c + filter_reference_round(n * w, divisor),
                               0, 4095);
        }
    }
}

static FilterOutputs adaptive_check(NikonPairedPixelConfig p,
                                    const uint16_t raw[16])
{
    NikonPairedPixelConfig neutral = p;
    FilterOutputs before = { 0 }, actual = { 0 }, expected = { 0 };

    p.grid_width = neutral.grid_width = 4;
    p.grid_height = neutral.grid_height = 4;
    neutral.adaptive.dose = 0;
    g_assert_true(nikon_paired_pixels(&neutral, raw, NULL, NULL, before.half,
        before.residual, before.grid, before.y, before.uv));
    memcpy(expected.grid, before.grid, sizeof(expected.grid));
    adaptive_reference(&p.adaptive, expected.grid);
    for (unsigned block = 0; block < 4; block++) {
        unsigned at = (block / 2) * 8 + (block % 2) * 2;
        unsigned sum = expected.grid[at] + expected.grid[at + 1] +
                       expected.grid[at + 4] + expected.grid[at + 5];

        expected.half[block] = filter_reference_round(sum, 4);
    }
    for (unsigned i = 0; i < 16; i++) {
        expected.y[i] = MIN(255, filter_reference_round(expected.grid[i], 16));
    }
    memset(expected.uv, 128, sizeof(expected.uv));
    g_assert_true(nikon_paired_pixels(&p, raw, NULL, NULL, actual.half,
        actual.residual, actual.grid, actual.y, actual.uv));
    g_assert_cmpmem(&actual, sizeof(actual), &expected, sizeof(expected));
    return actual;
}

static void adaptive(void)
{
    NikonPairedPixelConfig p = config();
    uint16_t raw[16];
    FilterOutputs out;

    p.adaptive = (NikonPairedAdaptive) {
        .base = 50, .slope = 60, .cap = 4095, .weight = 1024,
        .precision = 8, .dose = 4,
    };
    for (unsigned sign = 0; sign < 2; sign++) {
        fill(raw, 16, sign ? 1044 : 1016);
        raw[5] = sign ? 1012 : 1048;
        out = adaptive_check(p, raw);
        /* W equals retention: axial +/-1 residuals are signed half ties. */
        g_assert_cmpuint(out.grid[1], ==, sign ? 1039 : 1021);
        g_assert_cmpuint(out.grid[5], ==, sign ? 1034 : 1026);
        g_assert_cmpuint(out.grid[0], ==, sign ? 1043 : 1017);
    }
    p.adaptive.dose = 15;
    fill(raw, 16, 1016);
    raw[5] = 1040;
    out = adaptive_check(p, raw);
    /* Neighbor residual3/4 survives to the final rational division. */
    g_assert_cmpuint(out.grid[1], ==, 1020);
    g_assert_cmpuint(out.grid[5], ==, 1021);
    for (unsigned i = 0; i < 16; i++) {
        raw[i] = i % 4 < 2 ? 0 : 4095;
    }
    FilterOutputs baseline = adaptive_check(p, raw);

    for (unsigned field = 0; field < 6; field++) {
        NikonPairedPixelConfig other = p;

        switch (field) {
        case 0: other.adaptive.base = 300; break;
        case 1: other.adaptive.slope = 4095; break;
        case 2: other.adaptive.cap = 16; break;
        case 3: other.adaptive.weight = 100; break;
        case 4: other.adaptive.precision = 10; break;
        case 5: other.adaptive.dose = 1; break;
        }
        out = adaptive_check(other, raw);
        g_assert_cmpint(memcmp(baseline.grid, out.grid, sizeof(out.grid)),
                        !=, 0);
    }
    p.adaptive.base = p.adaptive.cap = 300;
    unsigned previous = 0;

    for (unsigned dose = 0; dose <= 15; dose++) {
        p.adaptive.dose = dose;
        out = adaptive_check(p, raw);
        /* RAW edge becomes{0,512,3583,4095}; its inner residual exceeds300. */
        unsigned expected = 512 + (300 * dose + (4 + dose) / 2) / (4 + dose);

        g_assert_cmpuint(out.grid[1], ==, expected);
        if (dose) {
            g_assert_cmpuint(out.grid[1], >, previous);
        }
        previous = out.grid[1];
    }
    for (unsigned value = 0; value <= 4095; value += 1365) {
        fill(raw, 16, value);
        out = adaptive_check(p, raw);
        for (unsigned i = 0; i < 16; i++) {
            g_assert_cmpuint(out.grid[i], ==, value);
        }
    }
    for (unsigned seed = 0; seed < 24; seed++) {
        p.adaptive = (NikonPairedAdaptive) {
            .base = 50 + seed * 10, .slope = 60 + seed * 170,
            .cap = 16 + seed * 170, .weight = 100 + seed * 40,
            .precision = 8 + seed % 3, .dose = seed % 16,
        };
        for (unsigned i = 0; i < 16; i++) {
            raw[i] = 1000 + ((i * 47 + i / 4 * 23 + seed) % 121) - 60;
        }
        adaptive_check(p, raw);
    }
    p.adaptive = (NikonPairedAdaptive) {
        .base = 300, .slope = 60, .cap = 300, .weight = 1024,
        .precision = 8, .dose = 15,
    };
    NikonPairedPixelConfig neutral = p;
    uint64_t before_energy = 0, after_energy = 0;

    neutral.adaptive.dose = 0;
    FilterOutputs before = adaptive_check(neutral, raw);

    out = adaptive_check(p, raw);
    for (unsigned i = 0; i < 16; i++) {
        int a = (int)before.grid[i] - 1000, b = (int)out.grid[i] - 1000;

        before_energy += a * a;
        after_energy += b * b;
    }
    g_assert_cmpuint(after_energy, <, before_energy);
}

static void adaptive_cfa(void)
{
    NikonPairedPixelConfig p = config();
    uint16_t raw[64], half[16], residual[16], grid[64], expected[64];
    uint8_t y[64], uv[32];

    p.adaptive = (NikonPairedAdaptive) {
        .base = 300, .slope = 60, .cap = 300, .weight = 1024,
        .precision = 8, .dose = 15,
    };
    for (unsigned phase = 0; phase < 4; phase++) {
        uint16_t plane[16];

        for (unsigned i = 0; i < 16; i++) {
            plane[i] = phase * 700 + (i % 4 < 2 ? 0 : 1000);
        }
        FilterOutputs out = adaptive_check(p, plane);

        for (unsigned i = 0; i < 16; i++) {
            unsigned at = (2 * (i / 4) + phase / 2) * 8 +
                          2 * (i % 4) + phase % 2;

            raw[at] = plane[i];
            expected[at] = out.grid[i];
        }
    }
    p.encoding = NIKON_RAW_RGGB;
    p.width = p.height = p.output_width = p.output_height = 8;
    p.grid_width = p.grid_height = 8;
    g_assert_true(nikon_paired_pixels(&p, raw, NULL, NULL, half, residual,
                                      grid, y, uv));
    g_assert_cmpmem(grid, sizeof(grid), expected, sizeof(expected));
    for (unsigned block = 0; block < 16; block++) {
        unsigned at = 2 * (block / 4) * 8 + 2 * (block % 4);
        unsigned sum = expected[at] + expected[at + 1] +
                       expected[at + 8] + expected[at + 9];

        g_assert_cmpuint(half[block], ==, filter_reference_round(sum, 4));
        g_assert_cmpuint(residual[block], ==, 0);
    }
}

static void adaptive_invalid(void)
{
    NikonPairedPixelConfig p = config();
    uint16_t raw[16];

    fill(raw, 16, 1000);
    g_assert_false(nikon_paired_adaptive_valid(NULL));
    g_assert_true(nikon_paired_adaptive_valid(&p.adaptive));
    for (unsigned test = 0; test < 13; test++) {
        p.adaptive = config().adaptive;
        switch (test) {
        case 0: p.adaptive = (NikonPairedAdaptive) { 0 }; break;
        case 1: p.adaptive.base = 49; break;
        case 2: p.adaptive.base = 301; break;
        case 3: p.adaptive.slope = 59; break;
        case 4: p.adaptive.slope = 4096; break;
        case 5: p.adaptive.cap = 15; break;
        case 6: p.adaptive.cap = 4096; break;
        case 7: p.adaptive.weight = 99; break;
        case 8: p.adaptive.weight = 1025; break;
        case 9: p.adaptive.precision = 7; break;
        case 10: p.adaptive.precision = 11; break;
        case 11: p.adaptive.dose = 16; break;
        case 12: p.adaptive.precision = 255; break;
        }
        g_assert_false(nikon_paired_adaptive_valid(&p.adaptive));
        rejected(&p, raw, NULL, NULL);
    }
}

/* Independent floating-coordinate oracle: it does not share the production
 * integer weights or Q16 clamping code. The source is a known filtered step.
 */
static uint16_t radial_reference(const uint16_t *image, unsigned width,
                                  unsigned height, unsigned phase, bool cfa,
                                  long double sx, long double sy)
{
    unsigned step = cfa ? 2 : 1, px = phase & 1, py = phase >> 1;
    unsigned cols = width / step, rows = height / step;
    long double x = (sx - px) / step, y = (sy - py) / step;
    unsigned x0, x1, y0, y1;
    long double fx, fy, sum;

    x = CLAMP(x, 0, cols - 1);
    y = CLAMP(y, 0, rows - 1);
    x0 = x;
    y0 = y;
    x1 = MIN(x0 + 1, cols - 1);
    y1 = MIN(y0 + 1, rows - 1);
    fx = x - x0;
    fy = y - y0;
    sum = image[(step * y0 + py) * width + step * x0 + px] *
          (1 - fx) * (1 - fy) +
          image[(step * y0 + py) * width + step * x1 + px] * fx * (1 - fy) +
          image[(step * y1 + py) * width + step * x0 + px] * (1 - fx) * fy +
          image[(step * y1 + py) * width + step * x1 + px] * fx * fy;
    return sum + 0.5L;
}

static uint16_t radial_reference_quad(uint16_t sample, bool quad)
{
    return quad ? CLAMP(filter_reference_round(((int)sample - 25) * 768,
                                               512) + 10, 0, 2200) : sample;
}

static void radial(void)
{
    static const int coefficients[] = { -128, 0, 127 };
    static const uint16_t step[] = { 0, 256, 1792, 2048 };
    static const int centers[] = { -4095, 256, 65535 };

    for (unsigned encoding = NIKON_RAW_MONO;
         encoding <= NIKON_RAW_BGGR; encoding++) {
        bool cfa = encoding != NIKON_RAW_MONO;
        unsigned width = cfa ? 8 : 4;

        for (unsigned lane = 0; lane <= 3; lane += 3) {
            unsigned phase = cfa ? lane ^ (encoding - 1) : 0;
            unsigned px = phase & 1;

            for (unsigned center = 0; center < ARRAY_SIZE(centers); center++) {
                for (unsigned coefficient = 0;
                     coefficient < ARRAY_SIZE(coefficients); coefficient++) {
                    for (unsigned quad = 0; quad < 2; quad++) {
                        NikonPairedPixelConfig p = config();
                        uint16_t raw[32] = { 0 }, spatial[32] = { 0 };
                        uint16_t corrected[32] = { 0 };
                        uint16_t half[8], residual[8], grid[4];
                        uint8_t yplane[32], uv[16];
                        int k = coefficients[coefficient];

                        p.encoding = encoding;
                        p.width = p.output_width = width;
                        p.radial.center_x = centers[center];
                        p.radial.center_y = 2;
                        p.radial.red = lane == 0 ? k : 0;
                        p.radial.blue = lane == 3 ? k : 0;
                        p.quad_enabled = quad;
                        p.quad_limit = 2200;
                        for (unsigned i = 0; i < 4; i++) {
                            p.quad_gain[i] = 768;
                            p.quad_offset[i] = -25;
                            p.quad_pedestal[i] = 10;
                        }
                        memset(p.b, 0, sizeof(p.b));
                        p.b[lane == 0 ? 0 : 2] = 512;
                        for (unsigned y = 0; y < 4; y++) {
                            for (unsigned x = 0; x < width; x++) {
                                unsigned at = y * width + x;

                                if (!cfa || ((y & 1) * 2 + (x & 1)) == phase) {
                                    raw[at] = x >= width / 2 ? 2048 : 0;
                                    spatial[at] = step[x / (cfa ? 2 : 1)];
                                }
                            }
                        }
                        /* CFA's radial sampling precedes per-site quad and the
                         * existing demosaic; mono's sampling precedes quad.
                         */
                        if (cfa) {
                            for (unsigned y = phase >> 1; y < 4; y += 2) {
                                for (unsigned x = px; x < width; x += 2) {
                                    long double sx = x + ((long double)x -
                                        centers[center]) * k / 65536;
                                    long double sy = y +
                                        ((long double)y - 2) * k / 65536;
                                    uint16_t sample = radial_reference(spatial,
                                        width, 4, phase, true, sx, sy);

                                    corrected[y * width + x] =
                                        radial_reference_quad(sample, quad);
                                }
                            }
                        }
                        for (unsigned crop = 0; crop < 2; crop++) {
                            p.x = p.y = crop;
                            p.output_width = width - 2 * crop;
                            p.output_height = 4 - 2 * crop;
                            g_assert_true(nikon_paired_pixels(&p, raw, NULL,
                                NULL, half, residual, grid, yplane, uv));
                            for (unsigned i = 0; i < width; i++) {
                                unsigned value = cfa ? step[i % 4] / 4 :
                                                 (i % 2 ? 1920 : 128);

                                g_assert_cmpuint(half[i], ==, value);
                                g_assert_cmpuint(residual[i], ==, 0);
                            }
                            for (unsigned i = 0; i < 4; i++) {
                                unsigned value = i % 2 ? 1920 : 128;

                                g_assert_cmpuint(grid[i], ==,
                                                 cfa ? value / 4 : value);
                            }
                            for (unsigned y = 0; y < p.output_height; y++) {
                                for (unsigned x = 0; x < p.output_width; x++) {
                                    unsigned xx = p.x + x, yy = p.y + y;
                                    uint16_t sample;

                                    if (cfa) {
                                        sample = radial_reference(corrected,
                                            width, 4, phase, true, xx, yy);
                                    } else {
                                        long double sx = xx + ((long double)xx -
                                            centers[center]) * k / 65536;
                                        long double sy = yy +
                                            ((long double)yy - 2) * k / 65536;

                                        sample = radial_reference(spatial,
                                            width, 4, 0, false, sx, sy);
                                        sample = radial_reference_quad(sample,
                                                                        quad);
                                    }
                                    g_assert_cmpuint(
                                        yplane[y * p.output_width + x], ==,
                                        MIN(255, (sample + 8) / 16));
                                }
                            }
                            unsigned bytes =
                                p.output_width * p.output_height / 2;

                            for (unsigned i = 0; i < bytes; i++) {
                                g_assert_cmpuint(uv[i], ==, 128);
                            }
                            if (cfa && center == 1 && !quad && !crop) {
                                /* At the high-side measured site, +/- residuals
                                 * actually change112 to89/116, with fractional
                                 * sampling; coefficient0 stays112.
                                 */
                                g_assert_cmpuint(yplane[4 + px], ==,
                                    k == -128 ? 116 : k == 127 ? 89 : 112);
                            }
                        }
                    }
                }
            }
        }
    }
}

static void radial_invalid(void)
{
    static const NikonPairedRadial invalid[] = {
        { .red = -129 }, { .red = 128 }, { .blue = -129 }, { .blue = 128 },
        { .center_x = -4096 }, { .center_y = -4096 },
        { .center_x = 65536 }, { .center_y = 65536 },
    };
    NikonPairedPixelConfig p = config();
    uint16_t raw[16];

    fill(raw, 16, 1000);
    g_assert_false(nikon_paired_radial_valid(NULL));
    for (unsigned i = 0; i < ARRAY_SIZE(invalid); i++) {
        p.radial = invalid[i];
        g_assert_false(nikon_paired_radial_valid(&p.radial));
        rejected(&p, raw, NULL, NULL);
    }
}

/* Gather/sort rational oracle, independent of the production traversal. */
static void color_reference(const NikonPairedColorFilter *f, uint16_t *image,
                            unsigned width, unsigned height, unsigned lane)
{
    size_t count = (size_t)width * height;
    g_autofree uint16_t *source = g_new(uint16_t, count);

    for (unsigned stage = 0; stage < 3; stage++) {
        memcpy(source, image, count * sizeof(*image));
        for (int y = 0; y < (int)height; y++) {
            for (int x = 0; x < (int)width; x++) {
                int at = y * width + x, center = source[at] * 4;
                int64_t correction;

                if (stage < 2) {
                    const NikonPairedColorBlock *b = &f->block[stage];
                    int common = b->base + filter_reference_round(
                        (int64_t)b->slope * center, 16383);
                    int means[2];
                    unsigned strength = lane == 4 ?
                        filter_reference_round(b->strength[0] +
                            b->strength[1] + b->strength[2], 3) :
                        b->strength[lane == 3 ? 2 : lane != 0];

                    for (unsigned j = 0; j < 2; j++) {
                        const NikonPairedColorBranch *n = &b->branch[j];
                        int radius = n->radius + 1;
                        int positions[2][4][2] = {
                            { { x - radius, y }, { x + radius, y },
                              { x, y - radius }, { x, y + radius } },
                            { { x - radius, y - radius },
                              { x + radius, y - radius },
                              { x - radius, y + radius },
                              { x + radius, y + radius } },
                        };
                        unsigned weights = 0;
                        int64_t sum = 0;

                        for (unsigned d = 0; d < 4; d++) {
                            int nx = CLAMP(positions[j][d][0], 0, (int)width - 1);
                            int ny = CLAMP(positions[j][d][1], 0, (int)height - 1);
                            int delta = 4 * source[ny * width + nx] - center;
                            unsigned base = delta < 0 ? n->negative_base :
                                                        n->positive_base;
                            int limit = common + base;
                            unsigned slope = delta < 0 ? n->negative_slope :
                                                         n->positive_slope;
                            unsigned weight = 1U << n->shift[d];

                            limit += filter_reference_round(
                                (int64_t)slope * center, 16383);
                            if (limit > abs(delta)) {
                                int64_t numerator =
                                    (int64_t)delta * (limit - abs(delta));

                                sum += weight * filter_reference_round(numerator,
                                                                         limit);
                            }
                            weights += weight;
                        }
                        means[j] = filter_reference_round(sum, weights);
                    }
                    correction = filter_reference_round(
                        filter_reference_round(means[0] + means[1], 2) *
                        strength, 31);
                    correction = CLAMP(correction, -(int)b->negative_limit,
                                        b->positive_limit);
                } else {
                    uint16_t values[9];
                    unsigned i = 0;

                    for (int ny = y - 1; ny <= y + 1; ny++) {
                        for (int nx = x - 1; nx <= x + 1; nx++) {
                            unsigned at = CLAMP(ny, 0, (int)height - 1) * width +
                                          CLAMP(nx, 0, (int)width - 1);

                            values[i++] = source[at];
                        }
                    }
                    for (unsigned a = 0; a < 9; a++) {
                        for (unsigned b = a + 1; b < 9; b++) {
                            if (values[a] > values[b]) {
                                uint16_t swap = values[a];

                                values[a] = values[b];
                                values[b] = swap;
                            }
                        }
                    }
                    int delta = CLAMP(values[4] * 4 - center,
                                      -(int)f->negative, f->positive);

                    correction = filter_reference_round(
                        (int64_t)delta * f->strength * f->weight,
                        31 * 4 * (1U << f->shift) * (1U << f->precision));
                }
                image[at] = MIN(4095, filter_reference_round(
                    CLAMP(center + correction, 0, 16383), 4));
            }
        }
    }
}

static NikonPairedColorFilter color_active(void)
{
    NikonPairedColorFilter f = config().colorfilter;

    for (unsigned i = 0; i < 2; i++) {
        NikonPairedColorBlock *b = &f.block[i];

        b->base = 200;
        b->slope = 200;
        b->negative_limit = i ? 100 : 4095;
        b->positive_limit = i ? 150 : 4095;
        for (unsigned lane = 0; lane < 3; lane++) {
            b->strength[lane] = 31;
        }
        for (unsigned j = 0; j < 2; j++) {
            b->branch[j].negative_base = 50;
            b->branch[j].negative_slope = 200;
            b->branch[j].positive_base = 80;
            b->branch[j].positive_slope = 200;
        }
    }
    f.negative = 130;
    f.positive = 200;
    f.strength = 16;
    f.weight = 12;
    return f;
}

static void color_check(NikonPairedPixelConfig p, const uint16_t *raw,
                        uint16_t *result)
{
    NikonPairedPixelConfig neutral = p;
    size_t count = (size_t)p.width * p.height;
    size_t halves = (size_t)((p.width + 1) / 2) * ((p.height + 1) / 2);
    g_autofree uint16_t *before = g_new(uint16_t, count);
    g_autofree uint16_t *half = g_new(uint16_t, halves);
    g_autofree uint16_t *residual = g_new(uint16_t, halves);
    g_autofree uint16_t *expected = g_new(uint16_t, count);
    g_autofree uint8_t *y = g_new(uint8_t, count);
    g_autofree uint8_t *uv = g_new(uint8_t, count / 2);

    neutral.colorfilter = config().colorfilter;
    p.grid_width = neutral.grid_width = p.width;
    p.grid_height = neutral.grid_height = p.height;
    p.b[0] = neutral.b[0] = 512;
    memset(p.b + 1, 0, sizeof(p.b) - sizeof(p.b[0]));
    memset(neutral.b + 1, 0, sizeof(neutral.b) - sizeof(neutral.b[0]));
    g_assert_true(nikon_paired_pixels(&neutral, raw, NULL, NULL, half,
        residual, before, y, uv));
    memcpy(expected, before, count * sizeof(*expected));
    if (p.encoding == NIKON_RAW_MONO) {
        color_reference(&p.colorfilter, expected, p.width, p.height, 4);
    } else {
        g_autofree uint16_t *plane = g_new(uint16_t, count / 4);

        for (unsigned phase = 0; phase < 4; phase++) {
            for (unsigned i = 0; i < count / 4; i++) {
                unsigned at = (2 * (i / (p.width / 2)) + phase / 2) * p.width +
                              2 * (i % (p.width / 2)) + phase % 2;

                plane[i] = before[at];
            }
            color_reference(&p.colorfilter, plane, p.width / 2, p.height / 2,
                             phase ^ (p.encoding - 1));
            for (unsigned i = 0; i < count / 4; i++) {
                expected[(2 * (i / (p.width / 2)) + phase / 2) * p.width +
                         2 * (i % (p.width / 2)) + phase % 2] = plane[i];
            }
        }
    }
    g_assert_true(nikon_paired_pixels(&p, raw, NULL, NULL, half, residual,
                                      result, y, uv));
    g_assert_cmpmem(result, count * sizeof(*result), expected,
                    count * sizeof(*expected));
    for (unsigned hy = 0; hy < (p.height + 1) / 2; hy++) {
        for (unsigned hx = 0; hx < (p.width + 1) / 2; hx++) {
            unsigned sum = 0, n = 0;

            for (unsigned row = 2 * hy;
                 row < MIN(2 * hy + 2, p.height); row++) {
                for (unsigned x = 2 * hx; x < MIN(2 * hx + 2, p.width); x++) {
                    sum += expected[row * p.width + x];
                    n++;
                }
            }
            g_assert_cmpuint(half[hy * ((p.width + 1) / 2) + hx], ==,
                             filter_reference_round(sum, n));
        }
    }
    if (p.encoding == NIKON_RAW_MONO) {
        for (unsigned i = 0; i < count; i++) {
            g_assert_cmpuint(y[i], ==,
                             MIN(255, filter_reference_round(expected[i], 16)));
        }
    }
}

static void colorfilter(void)
{
    NikonPairedPixelConfig p = config();
    uint16_t raw[16], grid[16];

    p.colorfilter.negative = 130;
    p.colorfilter.positive = 200;
    p.colorfilter.strength = 31;
    p.colorfilter.weight = 16;
    for (unsigned sign = 0; sign < 2; sign++) {
        fill(raw, 16, 1000);
        raw[5] = sign ? 976 : 1024;
        color_check(p, raw, grid);
        /* Spatial1012/988 becomes median1003/997 through the tail formula. */
        g_assert_cmpuint(grid[5], ==, sign ? 997 : 1003);
    }
    p.colorfilter = color_active();
    for (unsigned value = 0; value <= 4095; value += 4095) {
        fill(raw, 16, value);
        color_check(p, raw, grid);
        for (unsigned i = 0; i < 16; i++) {
            g_assert_cmpuint(grid[i], ==, value);
        }
    }
    for (unsigned sign = 0; sign < 2; sign++) {
        uint64_t prior_energy = 0, energy = 0;
        NikonPairedPixelConfig neutral = p;
        uint16_t before[16];

        for (unsigned i = 0; i < 16; i++) {
            raw[i] = 1000 + (i % 3 == 0 ? 48 : -24);
            if (sign) {
                raw[i] = 4095 - raw[i];
            }
        }
        neutral.colorfilter = config().colorfilter;
        color_check(neutral, raw, before);
        color_check(p, raw, grid);
        for (unsigned i = 0; i < 16; i++) {
            int center = sign ? 3095 : 1000;
            int a = (int)before[i] - center, b = (int)grid[i] - center;

            prior_energy += a * a;
            energy += b * b;
        }
        g_assert_cmpuint(energy, <, prior_energy);
    }
    /* Large edges are outside both gates; the explicitly neutral tail is0. */
    p.colorfilter.strength = 0;
    uint16_t before[16];
    NikonPairedPixelConfig neutral = p;

    neutral.colorfilter = config().colorfilter;
    for (unsigned i = 0; i < 16; i++) {
        raw[i] = i % 4 < 2 ? 0 : 4095;
    }
    color_check(neutral, raw, before);
    color_check(p, raw, grid);
    g_assert_cmpmem(grid, sizeof(grid), before, sizeof(before));
}

static void colorfilter_fields(void)
{
    NikonPairedPixelConfig p = config();
    uint16_t raw[1024], baseline[1024], actual[1024];

    p.width = p.height = p.output_width = p.output_height = 32;
    p.colorfilter = color_active();
    for (unsigned i = 0; i < ARRAY_SIZE(raw); i++) {
        raw[i] = 2000 + (i * 73 + i * i * 19) % 400;
    }
    color_check(p, raw, baseline);
    /* Source-variable fields affect the oracle and the actual pixel path. */
    for (unsigned block = 0; block < 2; block++) {
        for (unsigned field = 0; field < 22; field++) {
            NikonPairedPixelConfig q = p;
            NikonPairedColorBlock *b = &q.colorfilter.block[block];
            bool variable = true;

            switch (field) {
            case 0: b->base = 0; break;
            case 1: b->slope = 0; break;
            case 2: b->branch[0].negative_base = 0; break;
            case 3: b->branch[0].negative_slope = 0; break;
            case 4: b->branch[0].positive_base = 0; break;
            case 5: b->branch[0].positive_slope = 0; break;
            case 6: b->branch[1].negative_base = 0; break;
            case 7: b->branch[1].negative_slope = 0; break;
            case 8: b->branch[1].positive_base = 0; break;
            case 9: b->branch[1].positive_slope = 80; break;
            case 10: b->negative_limit = block ? 5 : 0; break;
            case 11: b->negative_limit = block ? 5 : 0;
                     b->positive_limit = block ? 9 : 0;
                     break;
            case 12: b->strength[0] = 0; break;
            case 13: b->strength[1] = b->strength[2] = 0; break;
            default: {
                unsigned branch = (field - 14) / 4, d = (field - 14) % 4;
                static const uint8_t maximum[2][2][4] = {
                    { { 4, 3, 4, 4 }, { 4, 2, 2, 2 } },
                    { { 4, 2, 4, 4 }, { 4, 2, 4, 4 } },
                };

                variable = maximum[block][branch][d] !=
                           b->branch[branch].shift[d];
                b->branch[branch].shift[d] = maximum[block][branch][d];
                break;
            }
            }
            if (!variable) {
                continue;
            }
            g_assert_true(nikon_paired_colorfilter_valid(&q.colorfilter));
            color_check(q, raw, actual);
            g_assert_cmpint(memcmp(baseline, actual, sizeof(actual)), !=, 0);
        }
    }
    for (unsigned field = 0; field < 7; field++) {
        NikonPairedPixelConfig q = p;

        switch (field) {
        case 0: q.colorfilter.precision = 1; break;
        case 1: q.colorfilter.negative = 0; break;
        case 2: q.colorfilter.positive = 0; break;
        case 3: q.colorfilter.strength = 0; break;
        case 4: q.colorfilter.shift = 4; break;
        case 5: q.colorfilter.weight = 16; break;
        case 6:
            q.colorfilter.block[0].branch[0].radius = 2;
            q.colorfilter.block[0].branch[1].radius = 2;
            q.colorfilter.block[1].branch[0].radius = 2;
            break;
        }
        color_check(q, raw, actual);
        g_assert_cmpint(memcmp(baseline, actual, sizeof(actual)), !=, 0);
    }
}

static void colorfilter_cfa(void)
{
    NikonPairedPixelConfig p = config();
    uint16_t raw[256], actual[256];

    p.width = p.height = p.output_width = p.output_height = 16;
    p.colorfilter = color_active();
    p.colorfilter.block[0].strength[0] = 8;
    p.colorfilter.block[0].strength[1] = 24;
    p.colorfilter.block[0].strength[2] = 24;
    for (unsigned i = 0; i < ARRAY_SIZE(raw); i++) {
        unsigned phase = (i / 16 % 2) * 2 + i % 2;

        raw[i] = 400 + phase * 700 + (i * 37) % 240;
    }
    for (p.encoding = NIKON_RAW_RGGB;
         p.encoding <= NIKON_RAW_BGGR; p.encoding++) {
        color_check(p, raw, actual);
    }
}

static void colorfilter_invalid(void)
{
    NikonPairedPixelConfig p = config();
    uint16_t raw[16];

    fill(raw, 16, 1000);
    g_assert_false(nikon_paired_colorfilter_valid(NULL));
    for (unsigned test = 0; test < 22; test++) {
        p.colorfilter = config().colorfilter;
        NikonPairedColorFilter *f = &p.colorfilter;

        switch (test) {
        case 0: memset(f, 0, sizeof(*f)); break;
        case 1: f->block[0].base = 401; break;
        case 2: f->block[1].slope = 601; break;
        case 3: f->block[0].branch[0].negative_base = 101; break;
        case 4: f->block[0].branch[1].positive_base = 1; break;
        case 5: f->block[0].branch[1].shift[1] = 3; break;
        case 6: f->block[1].branch[0].shift[2] = 2; break;
        case 7: f->block[0].branch[0].radius = 1; break;
        case 8: f->block[0].branch[1].radius = 2; break;
        case 9: f->block[1].branch[1].radius = 2; break;
        case 10: f->block[0].strength[0] = 32; break;
        case 11: f->block[1].strength[1] = 1; break;
        case 12: f->block[0].negative_limit = 1; break;
        case 13: f->block[1].negative_limit = 4; break;
        case 14: f->block[1].positive_limit = 151; break;
        case 15: f->precision = 2; break;
        case 16: f->negative = 131; break;
        case 17: f->positive = 201; break;
        case 18: f->strength = 32; break;
        case 19: f->shift = 1; break;
        case 20: f->weight = 7; break;
        case 21: f->weight = 17; break;
        }
        g_assert_false(nikon_paired_colorfilter_valid(f));
        rejected(&p, raw, NULL, NULL);
    }
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/nikon/pairedpixels/colorfilter", colorfilter);
    g_test_add_func("/nikon/pairedpixels/colorfilter-fields",
                     colorfilter_fields);
    g_test_add_func("/nikon/pairedpixels/colorfilter-cfa", colorfilter_cfa);
    g_test_add_func("/nikon/pairedpixels/colorfilter-invalid",
                     colorfilter_invalid);
    g_test_add_func("/nikon/pairedpixels/radial", radial);
    g_test_add_func("/nikon/pairedpixels/radial-invalid", radial_invalid);
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
    g_test_add_func("/nikon/pairedpixels/detail", detail);
    g_test_add_func("/nikon/pairedpixels/detail-invalid", detail_invalid);
    g_test_add_func("/nikon/pairedpixels/adaptive", adaptive);
    g_test_add_func("/nikon/pairedpixels/adaptive-cfa", adaptive_cfa);
    g_test_add_func("/nikon/pairedpixels/adaptive-invalid", adaptive_invalid);
    return g_test_run();
}
