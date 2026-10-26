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
    return g_test_run();
}
