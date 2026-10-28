/*
 * Fixed numerical vectors for bounded mono/CFA RAW12 processing.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "hw/nikon/pairedpixels.h"

typedef struct PairedOutputs {
    uint16_t half[4], residual[4], reference[4];
    uint8_t y[16], uv[8];
} PairedOutputs;

/* The reference plane below is test-owned scalar scratch, never a public
 * device output. Every successful adapter call checks the actual half planes
 * against this independent pipeline; existing MAIN byte oracles use the same
 * reference data, rather than observing implementation intermediates. */
static bool pixels_checked(const NikonPairedPixelConfig *p, const uint16_t *raw,
                           const uint16_t *prior, const uint16_t *old_residual,
                           uint16_t *half, uint16_t *residual,
                           uint16_t *reference, uint8_t *y, uint8_t *uv);

static int64_t filter_reference_round(int64_t value, int64_t divisor);
static void post_reference(const NikonPairedPost *p, unsigned channel,
                           uint16_t image[16]);

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
        .post = { .original = 256, .edge_strength = 3, .edge_limit = 5,
                  .negative_base = 10, .positive_base = 10,
                  .mix = 128, .radius = 1 },
        .main_gain = 256,
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
    for (unsigned i = 0; i < ARRAY_SIZE(p.saturation); i++) {
        p.saturation[i] = 256;
    }
    return p;
}

static bool run(const NikonPairedPixelConfig *p, const uint16_t *raw,
                const uint16_t *prior, const uint16_t *residual,
                PairedOutputs *out)
{
    return pixels_checked(p, raw, prior, residual, out->half,
                           out->residual, out->reference, out->y, out->uv);
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
        g_assert_cmpuint(out->reference[i], ==, luma);
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
    uint16_t expected_main[16];
    uint8_t expected_y[16];
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
    for (unsigned at = 0; at < 16; at++) {
        expected_main[at] = expected_half[(at / 8) * 2 + (at % 4) / 2];
    }
    post_reference(&p.post, 0, expected_main);
    for (unsigned at = 0; at < 16; at++) {
        expected_y[at] = MIN(255, (expected_main[at] + 8) / 16);
    }
    g_assert_cmpmem(out.half, sizeof(out.half),
                    expected_half, sizeof(expected_half));
    g_assert_cmpmem(out.reference, sizeof(out.reference),
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
    const uint16_t step_main[] = {
        1000, 1125, 1875, 2000, 1000, 1125, 1875, 2000,
        1000, 1125, 1875, 2000, 1000, 1125, 1875, 2000,
    };
    uint16_t threshold_main[] = {
        1000, 1128, 1000, 1000, 1128, 1512, 1128, 1000,
        1000, 1128, 1000, 1000, 1000, 1000, 1000, 1000,
    };
    uint16_t expected_main[16], uv_main[2][16];
    uint8_t expected_y[16], threshold_y[16], chroma[8];
    PairedOutputs out;
    unsigned i;

    fill(raw, ARRAY_SIZE(raw), 1000);
    raw[5] = 4095; /* An isolated interior impulse has coherent neighbors. */
    g_assert_true(run(&p, raw, NULL, NULL, &out));
    constant_output(&out, 1000, 0, 63, 128);
    memcpy(expected_main, step_main, sizeof(expected_main));
    post_reference(&p.post, 0, expected_main);
    post_reference(&p.post, 0, threshold_main);
    for (unsigned at = 0; at < 16; at++) {
        expected_y[at] = MIN(255, (expected_main[at] + 8) / 16);
        threshold_y[at] = MIN(255, (threshold_main[at] + 8) / 16);
        uv_main[0][at] = 2048 + step_main[at];
        uv_main[1][at] = 2048 - step_main[at];
    }
    for (unsigned channel = 0; channel < 2; channel++) {
        post_reference(&p.post, channel + 1, uv_main[channel]);
        for (unsigned y = 0; y < 2; y++) {
            for (unsigned x = 0; x < 2; x++) {
                unsigned sum = 0;
                for (unsigned yy = 0; yy < 2; yy++) {
                    for (unsigned xx = 0; xx < 2; xx++) {
                        sum += uv_main[channel][(2*y+yy)*4+2*x+xx];
                    }
                }
                chroma[4*y+2*x+channel] = MIN(255, ((sum+2)/4+8)/16);
            }
        }
    }
    raw[5] = 2024; /* The exact 1024 impulse threshold is retained. */
    g_assert_true(run(&p, raw, NULL, NULL, &out));
    g_assert_cmpmem(out.half, sizeof(out.half),
                    threshold_half, sizeof(threshold_half));
    g_assert_cmpmem(out.reference, sizeof(out.reference),
                    threshold_half, sizeof(threshold_half));
    g_assert_cmpmem(out.y, sizeof(out.y), threshold_y, sizeof(threshold_y));
    raw[5] = 2025;
    g_assert_true(run(&p, raw, NULL, NULL, &out));
    constant_output(&out, 1000, 0, 63, 128);
    g_assert_true(run(&p, step, NULL, NULL, &out));
    g_assert_cmpmem(out.half, sizeof(out.half),
                    expected_half, sizeof(expected_half));
    g_assert_cmpmem(out.reference, sizeof(out.reference),
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
    g_assert_cmpmem(out.reference, sizeof(out.reference),
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
    PairedOutputs out, saved;
    unsigned i;

    fill(raw, ARRAY_SIZE(raw), 1000);
    p.a[4] = 512;
    p.a[8] = 256;
    memset(p.c, 0, sizeof(p.c));
    p.c[0] = p.c[5] = p.c[7] = 1024;
    p.post.original = 0;
    p.post.transformed = 256;
    memset(p.b, 0, sizeof(p.b));
    p.b[0] = p.b[4] = p.b[8] = 512;
    /* A/B give (1000,500,250). C swaps centered U/V, preserving Y. */
    g_assert_true(run(&p, raw, NULL, NULL, &out));
    for (i = 0; i < ARRAY_SIZE(out.y); i++) {
        g_assert_cmpuint(out.y[i], ==, 63);
    }
    for (i = 0; i < ARRAY_SIZE(out.uv); i += 2) {
        g_assert_cmpuint(out.uv[i], ==, 144);
        g_assert_cmpuint(out.uv[i + 1], ==, 159);
    }
    for (i = 0; i < ARRAY_SIZE(out.half); i++) {
        g_assert_cmpuint(out.half[i], ==, 1000);
        g_assert_cmpuint(out.reference[i], ==, 1000);
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
    saved = out;
    g_assert_false(run(&p, raw, NULL, NULL, &out));
    g_assert_cmpmem(&out, sizeof(out), &saved, sizeof(saved));
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
            g_assert_cmpuint(out.reference[j], ==, vectors[i].sample);
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
    p.c[0] = p.c[7] = 1024;
    p.c[4] = p.c[5] = 512;
    p.post.original = 0;
    p.post.transformed = 256;
    memset(p.b, 0, sizeof(p.b));
    p.b[0] = p.b[4] = p.b[8] = 512;
    fill(raw, ARRAY_SIZE(raw), 1000);
    /* Gain/A/B=(500,250,2000); C=(500,1125,250), then UV bias. */
    g_assert_true(run(&p, raw, NULL, NULL, &out));
    for (i = 0; i < ARRAY_SIZE(out.y); i++) {
        g_assert_cmpuint(out.y[i], ==, 31);
    }
    for (i = 0; i < ARRAY_SIZE(out.uv); i += 2) {
        g_assert_cmpuint(out.uv[i], ==, 198);
        g_assert_cmpuint(out.uv[i + 1], ==, 144);
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
        g_assert_cmpuint(out.reference[i], ==, 1000);
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
            p.grid_enabled = true;
            p.grid_width = 0;
            break;
        case 13:
            p.grid_enabled = true;
            p.grid_height = 0;
            break;
        case 14:
            p.grid_enabled = true;
            p.grid_width = 4097;
            break;
        case 15:
            p.grid_enabled = true;
            p.grid_height = 4097;
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
    for (i = 0; i < 4; i++) {
        memset(&out, 0xa5, sizeof(out));
        saved = out;
        g_assert_false(pixels_checked(&good, raw, prior, residual,
            i == 0 ? NULL : out.half, i == 1 ? NULL : out.residual,
            out.reference, i == 2 ? NULL : out.y,
            i == 3 ? NULL : out.uv));
        g_assert_cmpmem(&out, sizeof(out), &saved, sizeof(saved));
    }
}

static void noise(void)
{
    static const uint8_t before_post[][16] = {
        { 77, 62, 49, 48, 54, 50, 47, 47,
          47, 47, 47, 47, 47, 47, 47, 47 },
        { 63, 71, 74, 60, 59, 69, 69, 74,
          60, 69, 69, 74, 74, 60, 60, 69 },
    };
    /* The second primitive mask is outside the recovered64-row table. */
    static const uint32_t masks[] = { 0xf8200000, 0x80200003 };
    NikonPairedPixelConfig p = config();
    uint16_t raw[16];
    uint8_t expected[2][16];
    PairedOutputs out, again;

    memset(p.b, 0, sizeof(p.b));
    p.b[0] = 512;
    p.noise_negative = p.noise_positive = 1020;
    p.noise[0] = (NikonPairedNoise) { .enabled = 1, .gain = 255 };
    fill(raw, ARRAY_SIZE(raw), 1000);
    for (unsigned i = 0; i < ARRAY_SIZE(masks); i++) {
        uint32_t state = 1;
        uint16_t plane[16];

        for (unsigned y = 0; y < 4; y += 2) {
            for (unsigned x = 0; x < 4; x += 2) {
                for (unsigned dy = 0; dy < 2; dy++) {
                    for (unsigned dx = 0; dx < 2; dx++) {
                        unsigned at = (y + dy) * 4 + x + dx;
                        bool outgoing = state & 1;
                        int noise;

                        state /= 2;
                        if (outgoing) {
                            state ^= masks[i];
                        }
                        noise = CLAMP((int)(state >> 21) - 1024, -1020, 1020);

                        plane[at] = CLAMP(1000 +
                            filter_reference_round(noise * 255, 1024), 0, 4095);
                        g_assert_cmpuint(MIN(255,
                            filter_reference_round(plane[at], 16)), ==,
                            before_post[i][at]);
                    }
                }
            }
        }
        post_reference(&p.post, 0, plane);
        for (unsigned at = 0; at < 16; at++) {
            expected[i][at] = MIN(255, filter_reference_round(plane[at], 16));
        }
        p.noise[0].feedback = masks[i];
        g_assert_true(run(&p, raw, NULL, NULL, &out));
        g_assert_cmpmem(out.y, sizeof(out.y), expected[i], sizeof(expected[i]));
        for (unsigned j = 0; j < ARRAY_SIZE(out.uv); j++) {
            g_assert_cmpuint(out.uv[j], ==, 128);
        }
        for (unsigned j = 0; j < ARRAY_SIZE(out.half); j++) {
            g_assert_cmpuint(out.half[j], ==, 1000);
            g_assert_cmpuint(out.residual[j], ==, 0);
            g_assert_cmpuint(out.reference[j], ==, 1000);
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
        g_assert_cmpuint(out.reference[i], ==, 1000);
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
    uint16_t half[4], residual[4], reference[16];
    uint8_t y[16], uv[8];
} FilterOutputs;

static int64_t filter_reference_round(int64_t value, int64_t divisor)
{
    lldiv_t q = lldiv(value, divisor);

    if (2 * llabs(q.rem) >= divisor) {
        q.quot += value < 0 ? -1 : 1;
    }
    return q.quot;
}

/* Exact rational oracle with explicit neighbor-coordinate enumeration. It uses
 * division/remainder rounding rather than the implementation's half-add rule.
 */
static void post_reference_size(const NikonPairedPost *p, unsigned channel,
                                uint16_t *image, unsigned width, unsigned height)
{
    static const int cross[][2] = { {-1, 0}, {1, 0}, {0, -1}, {0, 1} };
    static const int diagonal[][2] = { {-1, -1}, {1, -1}, {-1, 1}, {1, 1} };

    size_t bytes = (size_t)width * height * sizeof(*image);
    g_autofree uint16_t *previous = g_malloc(bytes);

    for (unsigned stage = 0; stage < 5; stage++) {
        const int (*offset)[2] = stage == 2 ? diagonal : cross;

        memcpy(previous, image, bytes);
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int at = width * y + x;
                int64_t numerator = 0, divisor;

                for (unsigned neighbor = 0; neighbor < 4; neighbor++) {
                    int yy = CLAMP(y + offset[neighbor][1], 0, (int)height - 1);
                    int xx = CLAMP(x + offset[neighbor][0], 0, (int)width - 1);

                    numerator += previous[width * yy + xx] - previous[at];
                }
                if (stage == 3) {
                    int c = previous[at];
                    int low = 16 * p->negative_base +
                        filter_reference_round((int64_t)p->slope * c, 16);
                    int high = 16 * p->positive_base +
                        filter_reference_round((int64_t)p->slope * c, 16);

                    numerator = 0;
                    for (unsigned direction = 0; direction < 8; direction++) {
                        const int *delta = direction < 4 ? cross[direction] :
                                                           diagonal[direction - 4];
                        int xx = CLAMP(x + delta[0] * p->radius,
                                       0, (int)width - 1);
                        int yy = CLAMP(y + delta[1] * p->radius,
                                       0, (int)height - 1);
                        int difference = CLAMP(previous[yy * width + xx] - c,
                                               -low, high);

                        numerator += (direction < 4 ? 16 - p->diagonal :
                                                       p->diagonal) * difference;
                    }
                    numerator *= (31 - p->retain) * (int64_t)p->amount *
                                  p->modifier * p->mix;
                    divisor = INT64_C(64) * 31 * 256 * 512 * (128 + p->mix);
                } else if (stage == 0) {
                    numerator *= -(int64_t)p->unsharp[!!channel];
                    divisor = 1024;
                } else if (stage < 3) {
                    numerator *= p->smooth[stage - 1];
                    divisor = 128;
                } else {
                    int64_t t = p->edge_limit * 16;

                    numerator *= MAX(4 * t - llabs(numerator), 0) *
                                 p->edge_strength;
                    divisor = 512 * t;
                }
                image[at] = CLAMP(previous[at] +
                    filter_reference_round(numerator, divisor), 0, 4095);
            }
        }
    }
}

static void post_reference(const NikonPairedPost *p, unsigned channel,
                           uint16_t image[16])
{
    post_reference_size(p, channel, image, 4, 4);
}

static void post_expected_y(const NikonPairedPost *p, const uint16_t grid[16],
                            uint8_t y[16])
{
    uint16_t luma[16];

    memcpy(luma, grid, sizeof(luma));
    post_reference(p, 0, luma);
    for (unsigned i = 0; i < 16; i++) {
        y[i] = MIN(255, filter_reference_round(luma[i], 16));
    }
}

static void filter_reference_size(const NikonPairedFilter *f, uint16_t *image,
                                   unsigned width, unsigned height)
{
    static const int delta[4][2] = { { -1, 0 }, { 1, 0 },
                                    { 0, -1 }, { 0, 1 } };

    for (unsigned pass = 0; pass < (f->refine ? 2 : 1); pass++) {
        g_autofree uint16_t *source = g_new(uint16_t, width * height);

        memcpy(source, image, width * height * sizeof(*source));
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                unsigned at = y * width + x, count = 0;
                int64_t sum = 0, s = source[at] * 4;

                for (unsigned direction = 0; direction < 4; direction++) {
                    int dx = delta[direction][0], dy = delta[direction][1];
                    int sx = CLAMP(x + dx * f->radius, 0, (int)width - 1);
                    int sy = CLAMP(y + dy * f->radius, 0, (int)height - 1);

                    if (f->refine && ((pass == 0) != (dy == 0))) {
                        continue;
                    }
                    sum += source[sy * width + sx] * 4;
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

static void filter_reference(const NikonPairedFilter *p,
                             uint16_t image[16])
{
    filter_reference_size(p, image, 4, 4);
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
    g_assert_true(pixels_checked(&neutral, raw, NULL, NULL, before.half,
        before.residual, before.reference, before.y, before.uv));
    memcpy(expected.reference, before.reference, sizeof(expected.reference));
    filter_reference(&p.filter, expected.reference);
    for (unsigned block = 0; block < 4; block++) {
        unsigned first = (block / 2) * 8 + (block % 2) * 2;
        unsigned sum = expected.reference[first] + expected.reference[first + 1] +
                       expected.reference[first + 4] + expected.reference[first + 5];
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

            expected.reference[at] = filter_reference_round(
                expected.reference[at] * (256 - weight) + prior[block] * weight,
                256);
            sum += expected.reference[at];
        }
        expected.half[block] = filter_reference_round(sum, 4);
    }
    post_expected_y(&p.post, expected.reference, expected.y);
    memset(expected.uv, 128, sizeof(expected.uv));
    g_assert_true(pixels_checked(&p, raw, history ? prior : NULL,
        history ? old_residual : NULL, actual.half, actual.residual,
        actual.reference, actual.y, actual.uv));
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
        g_assert_cmpuint(out.reference[i], ==, axial[i % 4]);
    }
    p.filter.refine = true;
    out = filter_check(p, step, false);
    for (unsigned i = 0; i < 16; i++) {
        g_assert_cmpuint(out.reference[i], ==, refined[i % 4]);
    }
    p.filter.refine = false;
    p.filter.clipped = true;
    p.filter.base = 4;
    out = filter_check(p, step, false);
    for (unsigned i = 0; i < 16; i++) {
        g_assert_cmpuint(out.reference[i], ==, clipped[i % 4]);
    }
    p.filter.base = 2; /* Positive half-LSB rounds up at the left border. */
    out = filter_check(p, step, false);
    g_assert_cmpuint(out.reference[0], ==, 1);
    g_assert_cmpuint(out.reference[3], ==, 4095);
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
        g_assert_cmpint(memcmp(baseline.reference, changed.reference,
                               sizeof(baseline.reference)), !=, 0);
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
            g_assert_cmpuint(out.reference[i], ==, value);
        }
    }
    p.filter = (NikonPairedFilter) { 0 };
    FilterOutputs before = filter_check(p, patterns[3], false);
    uint64_t old_energy = 0, new_energy = 0;

    p.filter = (NikonPairedFilter) { .enabled = true, .radius = 1 };
    out = filter_check(p, patterns[3], false);
    for (unsigned i = 0; i < 16; i++) {
        old_energy += (int)(before.reference[i] - 1000) *
                      (int)(before.reference[i] - 1000);
        new_energy += (int)(out.reference[i] - 1000) * (int)(out.reference[i] - 1000);
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

static void denoise_reference_size(const NikonPairedDenoise *n, uint16_t *image,
                                    unsigned width, unsigned height)
{
    static const int neighbors[8][2] = {
        { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 },
        { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 },
    };
    g_autofree uint16_t *source = g_new(uint16_t, width * height);
    unsigned lo = n->threshold + n->low + n->bias;
    unsigned hi = n->threshold + n->high + n->bias;

    memcpy(source, image, width * height * sizeof(*source));
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int64_t sums[2] = { 0 }, center = 4 * source[y * width + x];

            for (unsigned k = 0; k < 8; k++) {
                unsigned sx = CLAMP(x + neighbors[k][0], 0, (int)width - 1);
                unsigned sy = CLAMP(y + neighbors[k][1], 0, (int)height - 1);
                int64_t residual = source[sy * width + sx] * 4 - center;
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
            image[y * width + x] = MIN(4095, filter_reference_round(
                CLAMP(center, 0, 16383), 4));
        }
    }
}

static void denoise_reference(const NikonPairedDenoise *p,
                              uint16_t image[16])
{
    denoise_reference_size(p, image, 4, 4);
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
    g_assert_true(pixels_checked(&neutral, raw, NULL, NULL, before.half,
        before.residual, before.reference, before.y, before.uv));
    memcpy(expected.reference, before.reference, sizeof(expected.reference));
    denoise_reference(&p.denoise, expected.reference);
    for (unsigned block = 0; block < 4; block++) {
        unsigned first = (block / 2) * 8 + (block % 2) * 2;
        unsigned sum = expected.reference[first] + expected.reference[first + 1] +
                       expected.reference[first + 4] + expected.reference[first + 5];
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

            expected.reference[at] = filter_reference_round(
                expected.reference[at] * (256 - weight) + prior[block] * weight,
                256);
            sum += expected.reference[at];
        }
        expected.half[block] = filter_reference_round(sum, 4);
    }
    post_expected_y(&p.post, expected.reference, expected.y);
    memset(expected.uv, 128, sizeof(expected.uv));
    g_assert_true(pixels_checked(&p, raw, history ? prior : NULL,
        history ? old_residual : NULL, actual.half, actual.residual,
        actual.reference, actual.y, actual.uv));
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
        g_assert_cmpuint(out.reference[i], ==, expected[i % 4]);
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
            g_assert_cmpuint(out.reference[i], ==, gate_edges[edge][i % 4]);
        }
    }
    p.denoise = denoise_config();
    p.denoise.axial = 10;
    for (unsigned i = 0; i < 16; i++) {
        raw[i] = i % 4 < 2 ? 1000 : 1056;
    }
    out = denoise_check(p, raw, false);
    for (unsigned i = 0; i < 16; i++) {
        g_assert_cmpuint(out.reference[i], ==, signed_ties[i % 4]);
    }
    p.denoise = denoise_config();
    for (unsigned level = 0; level <= 4095; level += 1365) {
        fill(raw, 16, level);
        out = denoise_check(p, raw, false);
        for (unsigned i = 0; i < 16; i++) {
            g_assert_cmpuint(out.reference[i], ==, level);
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

            g_assert_cmpmem(out.reference, sizeof(out.reference), before.reference,
                            sizeof(before.reference));
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
            changed |= memcmp(a.reference, b.reference, sizeof(a.reference)) != 0;
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
        int a = (int)before.reference[i] - 1000, b = (int)out.reference[i] - 1000;

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
                g_assert_cmpuint(out.reference[i], ==, 1152);
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
                g_assert_cmpuint(out.reference[0], ==, 1152);
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
                g_assert_cmpuint(out.reference[i], ==, history ? 1433 : 1408);
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
        g_assert_true(pixels_checked(&p, raw, NULL, NULL, half,
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

static void detail_reference_size(const NikonPairedDetail *d, uint16_t *image,
                                   unsigned width, unsigned height)
{
    g_autofree uint16_t *source = g_new(uint16_t, width * height);

    memcpy(source, image, width * height * sizeof(*source));
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint16_t neighbors[9];
            unsigned count = 0;
            int64_t center = 4 * source[y * width + x], residual;

            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    unsigned sx = CLAMP(x + dx, 0, (int)width - 1);
                    unsigned sy = CLAMP(y + dy, 0, (int)height - 1);

                    neighbors[count++] = source[sy * width + sx];
                }
            }
            qsort(neighbors, count, sizeof(neighbors[0]), detail_compare);
            residual = CLAMP(4 * neighbors[count / 2] - center,
                             -(int64_t)d->limit, d->limit);
            center += filter_reference_round(residual * d->strength * d->lane,
                                              64 * 16);
            image[y * width + x] = MIN(4095, filter_reference_round(
                CLAMP(center, 0, 16383), 4));
        }
    }
}

static void detail_reference(const NikonPairedDetail *p,
                             uint16_t image[16])
{
    detail_reference_size(p, image, 4, 4);
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
    g_assert_true(pixels_checked(&neutral, raw, NULL, NULL, before.half,
        before.residual, before.reference, before.y, before.uv));
    memcpy(expected.reference, before.reference, sizeof(expected.reference));
    detail_reference(&p.detail, expected.reference);
    for (unsigned block = 0; block < 4; block++) {
        unsigned first = (block / 2) * 8 + (block % 2) * 2;
        unsigned sum = expected.reference[first] + expected.reference[first + 1] +
                       expected.reference[first + 4] + expected.reference[first + 5];
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

            expected.reference[at] = filter_reference_round(
                expected.reference[at] * (256 - weight) + prior[block] * weight,
                256);
            sum += expected.reference[at];
        }
        expected.half[block] = filter_reference_round(sum, 4);
    }
    post_expected_y(&p.post, expected.reference, expected.y);
    memset(expected.uv, 128, sizeof(expected.uv));
    g_assert_true(pixels_checked(&p, raw, history ? prior : NULL,
        history ? old_residual : NULL, actual.half, actual.residual,
        actual.reference, actual.y, actual.uv));
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
        g_assert_cmpuint(out.reference[5], ==, sign ? 1032 : 1028);
        g_assert_cmpuint(out.reference[0], ==, sign ? 1042 : 1018);
        g_assert_cmpuint(out.reference[6], ==, sign ? 1042 : 1018);
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
        g_assert_cmpint(memcmp(baseline.reference, changed.reference,
                               sizeof(baseline.reference)), !=, 0);
    }
    NikonPairedPixelConfig neutral = p;
    uint64_t before_energy = 0, after_energy = 0;

    neutral.detail = config().detail;
    FilterOutputs before = detail_check(neutral, raw, false);

    for (unsigned i = 0; i < 16; i++) {
        int a = (int)before.reference[i] - 1016;
        int b = (int)baseline.reference[i] - 1016;

        before_energy += a * a;
        after_energy += b * b;
    }
    g_assert_cmpuint(after_energy, <, before_energy);
    for (unsigned value = 0; value <= 4095; value += 1365) {
        fill(raw, 16, value);
        out = detail_check(p, raw, false);
        for (unsigned i = 0; i < 16; i++) {
            g_assert_cmpuint(out.reference[i], ==, value);
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

static void adaptive_reference_size(const NikonPairedAdaptive *a, uint16_t *image,
                                     unsigned width, unsigned height)
{
    g_autofree uint16_t *source = g_new(uint16_t, width * height);

    memcpy(source, image, width * height * sizeof(*source));
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int at = y * width + x;
            int64_t c = source[at];
            int64_t limit = MIN(a->cap, a->base +
                filter_reference_round((int64_t)a->slope * c, 4095));
            int64_t sum = source[MAX(y - 1, 0) * width + x] +
                source[MIN(y + 1, (int)height - 1) * width + x] +
                source[y * width + MAX(x - 1, 0)] +
                source[y * width + MIN(x + 1, (int)width - 1)];
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

static void adaptive_reference(const NikonPairedAdaptive *p,
                               uint16_t image[16])
{
    adaptive_reference_size(p, image, 4, 4);
}

static FilterOutputs adaptive_check(NikonPairedPixelConfig p,
                                    const uint16_t raw[16])
{
    NikonPairedPixelConfig neutral = p;
    FilterOutputs before = { 0 }, actual = { 0 }, expected = { 0 };

    p.grid_width = neutral.grid_width = 4;
    p.grid_height = neutral.grid_height = 4;
    neutral.adaptive.dose = 0;
    g_assert_true(pixels_checked(&neutral, raw, NULL, NULL, before.half,
        before.residual, before.reference, before.y, before.uv));
    memcpy(expected.reference, before.reference, sizeof(expected.reference));
    adaptive_reference(&p.adaptive, expected.reference);
    for (unsigned block = 0; block < 4; block++) {
        unsigned at = (block / 2) * 8 + (block % 2) * 2;
        unsigned sum = expected.reference[at] + expected.reference[at + 1] +
                       expected.reference[at + 4] + expected.reference[at + 5];

        expected.half[block] = filter_reference_round(sum, 4);
    }
    post_expected_y(&p.post, expected.reference, expected.y);
    memset(expected.uv, 128, sizeof(expected.uv));
    g_assert_true(pixels_checked(&p, raw, NULL, NULL, actual.half,
        actual.residual, actual.reference, actual.y, actual.uv));
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
        g_assert_cmpuint(out.reference[1], ==, sign ? 1039 : 1021);
        g_assert_cmpuint(out.reference[5], ==, sign ? 1034 : 1026);
        g_assert_cmpuint(out.reference[0], ==, sign ? 1043 : 1017);
    }
    p.adaptive.dose = 15;
    fill(raw, 16, 1016);
    raw[5] = 1040;
    out = adaptive_check(p, raw);
    /* Neighbor residual3/4 survives to the final rational division. */
    g_assert_cmpuint(out.reference[1], ==, 1020);
    g_assert_cmpuint(out.reference[5], ==, 1021);
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
        g_assert_cmpint(memcmp(baseline.reference, out.reference, sizeof(out.reference)),
                        !=, 0);
    }
    p.adaptive.base = p.adaptive.cap = 300;
    unsigned previous = 0;

    for (unsigned dose = 0; dose <= 15; dose++) {
        p.adaptive.dose = dose;
        out = adaptive_check(p, raw);
        /* RAW edge becomes{0,512,3583,4095}; its inner residual exceeds300. */
        unsigned expected = 512 + (300 * dose + (4 + dose) / 2) / (4 + dose);

        g_assert_cmpuint(out.reference[1], ==, expected);
        if (dose) {
            g_assert_cmpuint(out.reference[1], >, previous);
        }
        previous = out.reference[1];
    }
    for (unsigned value = 0; value <= 4095; value += 1365) {
        fill(raw, 16, value);
        out = adaptive_check(p, raw);
        for (unsigned i = 0; i < 16; i++) {
            g_assert_cmpuint(out.reference[i], ==, value);
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
        int a = (int)before.reference[i] - 1000, b = (int)out.reference[i] - 1000;

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
            expected[at] = out.reference[i];
        }
    }
    p.encoding = NIKON_RAW_RGGB;
    p.width = p.height = p.output_width = p.output_height = 8;
    p.grid_width = p.grid_height = 8;
    g_assert_true(pixels_checked(&p, raw, NULL, NULL, half, residual,
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
    for (unsigned test = 0; test < 14; test++) {
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
        case 13: p.adaptive.weight = 0; break;
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
                        uint16_t corrected[32] = { 0 }, expected_main[32];
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
                            g_assert_true(pixels_checked(&p, raw, NULL,
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
                                    expected_main[y * p.output_width + x] = sample;
                                }
                            }
                            post_reference_size(&p.post, 0, expected_main,
                                                p.output_width, p.output_height);
                            for (unsigned i = 0;
                                 i < p.output_width * p.output_height; i++) {
                                g_assert_cmpuint(yplane[i], ==,
                                    MIN(255, (expected_main[i] + 8) / 16));
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

static int reference_compare(const void *a, const void *b)
{
    return (int)*(const uint16_t *)a - *(const uint16_t *)b;
}

static void reference_plane(const NikonPairedPixelConfig *p, uint16_t *image,
                             unsigned width, unsigned height, unsigned lane)
{
    static const int cross[][2] = { {-1, 0}, {1, 0}, {0, -1}, {0, 1} };
    size_t bytes = width * height * sizeof(*image);
    g_autofree uint16_t *source = g_memdup2(image, bytes);

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint16_t samples[5] = { source[y * width + x] };
            unsigned low = 4095, high = 0;

            for (unsigned n = 0; n < 4; n++) {
                int xx = CLAMP(x + cross[n][0], 0, (int)width - 1);
                int yy = CLAMP(y + cross[n][1], 0, (int)height - 1);

                samples[n + 1] = source[yy * width + xx];
                low = MIN(low, samples[n + 1]);
                high = MAX(high, samples[n + 1]);
            }
            qsort(samples, 5, sizeof(*samples), reference_compare);
            if (high - low <= 64 &&
                abs((int)source[y * width + x] - samples[2]) > 1024) {
                image[y * width + x] = samples[2];
            }
        }
    }
    memcpy(source, image, bytes);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            unsigned sum = 4 * source[y * width + x];

            for (unsigned n = 0; n < 4; n++) {
                int xx = CLAMP(x + cross[n][0], 0, (int)width - 1);
                int yy = CLAMP(y + cross[n][1], 0, (int)height - 1);

                sum += source[yy * width + xx];
            }
            image[y * width + x] = filter_reference_round(sum, 8);
        }
    }
    filter_reference_size(&p->filter, image, width, height);
    denoise_reference_size(&p->denoise, image, width, height);
    detail_reference_size(&p->detail, image, width, height);
    adaptive_reference_size(&p->adaptive, image, width, height);
    color_reference(&p->colorfilter, image, width, height, lane);
}

static bool pixels_checked(const NikonPairedPixelConfig *p, const uint16_t *raw,
                           const uint16_t *prior, const uint16_t *old_residual,
                           uint16_t *half, uint16_t *residual,
                           uint16_t *reference, uint8_t *y, uint8_t *uv)
{
    g_autofree uint16_t *expected = NULL;
    bool cfa = p && p->encoding != NIKON_RAW_MONO;

    if (!nikon_paired_pixels(p, raw, prior, old_residual, half, residual,
                             NULL, y, uv)) {
        return false;
    }
    expected = g_memdup2(raw, p->width * p->height * sizeof(*raw));
    if (cfa) {
        unsigned width = p->width / 2, height = p->height / 2;
        g_autofree uint16_t *plane = g_new(uint16_t, width * height);

        for (unsigned phase = 0; phase < 4; phase++) {
            for (unsigned row = 0; row < height; row++) {
                for (unsigned x = 0; x < width; x++) {
                    plane[row * width + x] = raw[(2 * row + phase / 2) *
                        p->width + 2 * x + phase % 2];
                }
            }
            reference_plane(p, plane, width, height, phase ^ (p->encoding - 1));
            for (unsigned row = 0; row < height; row++) {
                for (unsigned x = 0; x < width; x++) {
                    expected[(2 * row + phase / 2) * p->width +
                        2 * x + phase % 2] = plane[row * width + x];
                }
            }
        }
    } else {
        reference_plane(p, expected, p->width, p->height, 4);
    }
    for (unsigned hy = 0; hy < (p->height + 1) / 2; hy++) {
        for (unsigned hx = 0; hx < (p->width + 1) / 2; hx++) {
            unsigned at = hy * ((p->width + 1) / 2) + hx;
            unsigned sum = 0, count = 0, weight = 0, deviation = 0, current;

            for (unsigned yy = 2 * hy; yy < MIN(2 * hy + 2, p->height); yy++) {
                for (unsigned xx = 2 * hx; xx < MIN(2 * hx + 2, p->width); xx++) {
                    sum += expected[yy * p->width + xx];
                    count++;
                }
            }
            current = filter_reference_round(sum, count);
            if (prior) {
                deviation = abs((int)current - prior[at]);
                if (deviation <= 256) {
                    weight = filter_reference_round(64 *
                        (256 - MIN(old_residual ? old_residual[at] : 0, 256)),
                        256);
                }
            }
            sum = 0;
            for (unsigned yy = 2 * hy; yy < MIN(2 * hy + 2, p->height); yy++) {
                for (unsigned xx = 2 * hx; xx < MIN(2 * hx + 2, p->width); xx++) {
                    unsigned i = yy * p->width + xx;
                    int value = expected[i];

                    if (weight) {
                        value = cfa ? CLAMP(value + filter_reference_round(
                            ((int64_t)prior[at] - current) * weight, 256),
                            0, 4095) : filter_reference_round(
                            value * (256 - weight) + prior[at] * weight, 256);
                    }
                    expected[i] = value;
                    sum += value;
                }
            }
            g_assert_cmpuint(half[at], ==, filter_reference_round(sum, count));
            g_assert_cmpuint(residual[at], ==, deviation);
        }
    }
    /* Old pre-color observations become independent reference scratch only. */
    for (unsigned gy = 0; gy < p->grid_height; gy++) {
        for (unsigned gx = 0; gx < p->grid_width; gx++) {
            unsigned sum = 0, count = 0;

            for (unsigned yy = gy * p->height / p->grid_height;
                 yy < (gy + 1) * p->height / p->grid_height; yy++) {
                for (unsigned xx = gx * p->width / p->grid_width;
                     xx < (gx + 1) * p->width / p->grid_width; xx++) {
                    sum += expected[yy * p->width + xx];
                    count++;
                }
            }
            reference[gy * p->grid_width + gx] =
                filter_reference_round(sum, count);
        }
    }
    return true;
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
    g_assert_true(pixels_checked(&neutral, raw, NULL, NULL, half,
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
    g_assert_true(pixels_checked(&p, raw, NULL, NULL, half, residual,
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
        post_reference_size(&p.post, 0, expected, p.width, p.height);
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

static void refine_neutral(void)
{
    for (unsigned encoding = NIKON_RAW_MONO;
         encoding <= NIKON_RAW_BGGR; encoding++) {
        for (unsigned geometry = 0; geometry < 4; geometry++) {
            for (unsigned pattern = 0; pattern < 4; pattern++) {
                for (unsigned variant = 0; variant < 41; variant++) {
                    NikonPairedPixelConfig p = config();
                    uint16_t raw[64], half[16], residual[16], reference[64];
                    uint16_t prior[16], old_residual[16], main[3][64];
                    uint8_t y[64], uv[32];
                    unsigned mask = variant < 32 ? variant :
                                    3 | ((variant - 32) << 2);
                    bool history = variant & 1;

                    p.encoding = encoding;
                    p.width = p.height = 8;
                    p.output_width = p.output_height = 8;
                    if (geometry == 1) {
                        p.x = p.y = 1;
                        p.output_width = 4;
                        p.output_height = 6;
                    } else if (geometry == 2) {
                        p.x = p.y = 6;
                        p.output_width = p.output_height = 2;
                    } else if (geometry == 3) {
                        p.width = p.height = 2;
                        p.output_width = p.output_height = 2;
                    }
                    p.grid_width = p.width;
                    p.grid_height = p.height;
                    p.detail = (NikonPairedDetail) {
                        .limit = 31, .strength = mask & 1 ? 32 : 0, .lane = 16,
                    };
                    p.adaptive = (NikonPairedAdaptive) {
                        .base = 300, .slope = 4095, .cap = 4095, .weight = 1024,
                        .precision = 8, .dose = mask & 2 ? 15 : 0,
                    };
                    p.colorfilter = color_active();
                    for (unsigned block = 0; block < 2; block++) {
                        NikonPairedColorBlock *b = &p.colorfilter.block[block];
                        unsigned strength = mask & (4 << block) ? 31 : 0;

                        b->strength[0] = b->strength[1] = b->strength[2] = strength;
                        if (variant >= 32) {
                            /* Opposite red/green-blue choices in the two blocks
                             * exercise channel-dependent buffer parity. The
                             * final variant also rounds mono's {1,0,0} to 0.
                             */
                            b->strength[block ? 1 : 0] = 0;
                            b->strength[2] = b->strength[1];
                            if (variant == 40) {
                                b->strength[0] = 1;
                                b->strength[1] = b->strength[2] = 0;
                            }
                        }
                    }
                    p.colorfilter.strength = mask & 16 ? 31 : 0;
                    p.colorfilter.weight = 16;
                    /* Observe all three colors through distinct Y/U/V rows.
                     * Full scalar refinement still visits every stage,
                     * including zero-strength neighborhoods and clamps. The
                     * independent floating-coordinate sampler retains the
                     * generic bilinear law, including one rounding per sample.
                     */
                    const int16_t matrix[] = {
                        128, 256, 128, 256, -256, 0, 0, -256, 256,
                    };

                    memcpy(p.b, matrix, sizeof(p.b));
                    for (unsigned i = 0; i < p.width * p.height; i++) {
                        raw[i] = pattern == 0 ?
                            1900 + (i * 73 + i * i * 19) % 240 :
                            pattern == 1 ? (i % p.width < p.width / 2 ? 0 : 4095) :
                            pattern == 2 ? 0 : 4095;
                    }
                    for (unsigned i = 0; i < ARRAY_SIZE(prior); i++) {
                        prior[i] = 2000 + 11 * i;
                        old_residual[i] = 85 * (i % 4);
                    }
                    memset(y, 0xa5, sizeof(y));
                    memset(uv, 0xa5, sizeof(uv));
                    g_assert_true(pixels_checked(&p, raw,
                        history ? prior : NULL, history ? old_residual : NULL,
                        half, residual, reference, y, uv));
                    for (unsigned row = 0; row < p.output_height; row++) {
                        for (unsigned x = 0; x < p.output_width; x++) {
                            unsigned xx = p.x + x, yy = p.y + row;
                            unsigned at = row * p.output_width + x;
                            int r, g, b;

                            if (encoding == NIKON_RAW_MONO) {
                                r = g = b = reference[yy * p.width + xx];
                            } else {
                                unsigned lane = (encoding - 1) ^
                                                ((yy & 1) * 2 + (xx & 1));

                                r = radial_reference(reference, p.width, p.height,
                                    encoding - 1, true, xx, yy);
                                b = radial_reference(reference, p.width, p.height,
                                    3 ^ (encoding - 1), true, xx, yy);
                                if (lane == 1 || lane == 2) {
                                    g = reference[yy * p.width + xx];
                                } else {
                                    unsigned gr = radial_reference(reference,
                                        p.width, p.height, 1 ^ (encoding - 1),
                                        true, xx, yy);
                                    unsigned gb = radial_reference(reference,
                                        p.width, p.height, 2 ^ (encoding - 1),
                                        true, xx, yy);

                                    g = filter_reference_round(gr + gb, 2);
                                }
                            }
                            main[0][at] = filter_reference_round(r + 2 * g + b, 4);
                            main[1][at] = 2048 + CLAMP(
                                filter_reference_round(r - g, 2), -2048, 2047);
                            main[2][at] = 2048 + CLAMP(
                                filter_reference_round(b - g, 2), -2048, 2047);
                        }
                    }
                    for (unsigned channel = 0; channel < 3; channel++) {
                        post_reference_size(&p.post, channel, main[channel],
                                             p.output_width, p.output_height);
                    }
                    for (unsigned i = 0; i < sizeof(y); i++) {
                        if (i < p.output_width * p.output_height) {
                            g_assert_cmpuint(y[i], ==, MIN(255,
                                filter_reference_round(main[0][i], 16)));
                        } else {
                            g_assert_cmphex(y[i], ==, 0xa5);
                        }
                    }
                    for (unsigned row = 0; row < p.output_height; row += 2) {
                        for (unsigned x = 0; x < p.output_width; x += 2) {
                            unsigned at = row * p.output_width + x;

                            for (unsigned channel = 1; channel < 3; channel++) {
                                uint16_t *plane = main[channel];
                                unsigned mean = filter_reference_round(
                                    plane[at] + plane[at + 1] +
                                    plane[at + p.output_width] +
                                    plane[at + p.output_width + 1], 4);

                                g_assert_cmpuint(uv[row / 2 * p.output_width +
                                    x + channel - 1], ==, MIN(255,
                                    filter_reference_round(mean, 16)));
                            }
                        }
                    }
                    for (unsigned i = p.output_width * p.output_height / 2;
                         i < sizeof(uv); i++) {
                        g_assert_cmphex(uv[i], ==, 0xa5);
                    }
                }
            }
        }
    }
}

/* Deliberately evaluate every enabled color stage, including identities. Keep
 * division/remainder rounding and the conditional LFSR recurrence independent
 * of the implementation's shifts, masks and job-level identity decisions.
 */
static void color_pixel_reference(const NikonPairedPixelConfig *p,
                                  uint16_t rgb[3], uint32_t state[3],
                                  uint16_t output[3])
{
    int after_a[3], yuv[3];

    for (unsigned channel = 0; channel < 3; channel++) {
        for (unsigned table = 0; table < 2; table++) {
            if (p->curve_enabled[table]) {
                unsigned index = rgb[channel] / 16, fraction = rgb[channel] % 16;
                unsigned gain = filter_reference_round(
                    p->curve[table][index] * (16 - fraction) +
                    p->curve[table][index + 1] * fraction, 16);

                rgb[channel] = CLAMP(filter_reference_round(
                    (int64_t)rgb[channel] * gain, 1024), 0, 4095);
            }
        }
        if (p->gain_enabled[channel]) {
            rgb[channel] = CLAMP(filter_reference_round(
                (int64_t)rgb[channel] * p->gain[channel], 256), 0, 4095);
        }
        if (p->noise[channel].enabled) {
            const NikonPairedNoise *n = &p->noise[channel];
            bool carry = state[channel] % 2;
            int noise;

            state[channel] /= 2;
            if (carry) {
                state[channel] ^= n->feedback;
            }
            noise = CLAMP((int)(state[channel] / (1U << 21)) - 1024,
                          -(int)p->noise_negative, p->noise_positive);
            noise = filter_reference_round(noise * n->gain, 1024);
            noise = filter_reference_round(noise + n->offset, 1U << n->shift);
            rgb[channel] = CLAMP((int)rgb[channel] + noise, 0, 4095);
        }
    }
    for (unsigned row = 0; row < 3; row++) {
        int64_t sum = 0;

        for (unsigned channel = 0; channel < 3; channel++) {
            sum += (int64_t)p->a[row * 3 + channel] * rgb[channel];
        }
        after_a[row] = CLAMP(filter_reference_round(sum, 1024), 0, 4095);
    }
    for (unsigned row = 0; row < 3; row++) {
        int64_t sum = 0;

        for (unsigned channel = 0; channel < 3; channel++) {
            sum += (int64_t)p->b[row * 3 + channel] * after_a[channel];
        }
        yuv[row] = CLAMP(filter_reference_round(sum, 512), row ? -2048 : 0,
                         row ? 2047 : 4095);
    }
    output[0] = yuv[0];
    unsigned index = yuv[0] / 128, fraction = yuv[0] % 128;
    unsigned gain = filter_reference_round(
        p->saturation[index] * (128 - fraction) +
        p->saturation[index + 1] * fraction, 128);

    for (unsigned row = 1; row < 3; row++) {
        int transformed = filter_reference_round(
            (int64_t)p->c[row * 3 + 1] * yuv[1] +
            (int64_t)p->c[row * 3 + 2] * yuv[2], 1024);
        int mixed = filter_reference_round(
            (int64_t)p->post.original * yuv[row] +
            (int64_t)p->post.transformed * transformed, 256);

        output[row] = CLAMP(filter_reference_round((int64_t)mixed * gain, 256) +
                            2048, 0, 4095);
    }
}

static void color_identities(void)
{
    static const int16_t matrix[] = {
        198, 309, 5, -65, -497, 562, 477, -478, 1,
    };

    for (unsigned encoding = NIKON_RAW_MONO;
         encoding <= NIKON_RAW_BGGR; encoding++) {
        for (unsigned crop = 0; crop < 2; crop++) {
            for (unsigned pattern = 0; pattern < 4; pattern++) {
                for (unsigned variant = 0; variant < 16; variant++) {
                    NikonPairedPixelConfig p = config();
                    uint16_t raw[16], half[4], residual[4], reference[16];
                    uint16_t planes[3][16];
                    uint32_t state[] = { 1, 1, 1 };
                    uint8_t y[16], uv[8];

                    p.encoding = encoding;
                    p.grid_width = p.grid_height = 4;
                    if (crop) {
                        p.x = p.y = 1;
                        p.output_width = p.output_height = 2;
                    }
                    fill(p.curve[0], 257, 1024);
                    fill(p.curve[1], 257, 1024);
                    p.curve_enabled[0] = p.curve_enabled[1] = variant != 0;
                    p.post.original = 147;
                    p.post.transformed = 109;
                    memcpy(p.b, matrix, sizeof(p.b));
                    for (unsigned i = 0; i < 33; i++) {
                        p.saturation[i] = 110 + (17 * i) % 148;
                    }
                    for (unsigned channel = 0; channel < 3; channel++) {
                        p.gain[channel] = 256;
                        p.gain_enabled[channel] = variant != 0;
                        p.noise[channel] = (NikonPairedNoise) {
                            .feedback = channel ? 0x80200003 : 0xf8200000,
                            .enabled = variant != 0,
                            .gain = channel ? 21 : 255,
                            .shift = channel ? 4 : 0,
                            .offset = channel == 2 ? 255 : 0,
                        };
                    }
                    p.noise_negative = p.noise_positive = 1020;
                    switch (variant) {
                    case 0:
                        fill(p.curve[0], 257, 65535);
                        fill(p.curve[1], 257, 0);
                        p.gain[0] = 0;
                        p.gain[1] = p.gain[2] = 65535;
                        break;
                    case 2: fill(p.curve[0], 257, 512); break;
                    case 3: fill(p.curve[1], 257, 2048); break;
                    case 4:
                        fill(p.curve[0], 257, 65535);
                        fill(p.curve[1], 257, 512);
                        break;
                    case 5:
                        p.gain[0] = 0;
                        p.gain[1] = 65535;
                        p.gain[2] = 128;
                        break;
                    case 6:
                        p.curve_enabled[0] = false;
                        fill(p.curve[0], 257, 65535);
                        p.gain_enabled[0] = false;
                        p.gain[0] = 65535;
                        p.noise[0].enabled = false;
                        break;
                    case 7: p.a[0] = 1023; break;
                    case 8:
                        p.a[0] = -4096;
                        p.a[2] = p.a[4] = 4095;
                        p.a[3] = -4096;
                        p.a[8] = 2048;
                        break;
                    case 9:
                        p.c[4] = p.c[8] = -4096;
                        p.c[5] = p.c[7] = 4095;
                        break;
                    case 10: p.c[4] = 1023; break;
                    case 11:
                        fill(p.curve[0], 257, 65535);
                        fill(p.curve[1], 257, 512);
                        p.gain[0] = 512;
                        p.a[0] = -4096;
                        p.c[4] = p.c[8] = 0;
                        p.c[5] = -1024;
                        p.c[7] = 1024;
                        break;
                    case 12: p.curve[0][256] = 2048; break;
                    case 13: p.curve[1][1] = 2048; break;
                    case 14:
                        p.post.original = 0;
                        p.post.transformed = 256;
                        break;
                    case 15:
                        p.post.original = 256;
                        p.post.transformed = 0;
                        break;
                    }
                    for (unsigned i = 0; i < 16; i++) {
                        raw[i] = pattern == 0 ? 0 : pattern == 1 ? 4095 :
                            pattern == 2 ? 15 : 300 + (i * 197) % 3200;
                    }
                    memset(y, 0xa5, sizeof(y));
                    memset(uv, 0xa5, sizeof(uv));
                    g_assert_true(pixels_checked(&p, raw, NULL, NULL,
                        half, residual, reference, y, uv));
                    for (unsigned row = 0; row < p.output_height; row += 2) {
                        for (unsigned x = 0; x < p.output_width; x += 2) {
                            for (unsigned dy = 0; dy < 2; dy++) {
                                for (unsigned dx = 0; dx < 2; dx++) {
                                    unsigned xx = p.x + x + dx;
                                    unsigned yy = p.y + row + dy;
                                    unsigned at = (row + dy) * p.output_width +
                                                  x + dx;
                                    uint16_t rgb[3], output[3];

                                    if (encoding == NIKON_RAW_MONO) {
                                        rgb[0] = rgb[1] = rgb[2] =
                                            reference[yy * p.width + xx];
                                    } else {
                                        unsigned lane = (encoding - 1) ^
                                            ((yy & 1) * 2 + (xx & 1));

                                        rgb[0] = radial_reference(reference, 4, 4,
                                            encoding - 1, true, xx, yy);
                                        rgb[2] = radial_reference(reference, 4, 4,
                                            3 ^ (encoding - 1), true, xx, yy);
                                        rgb[1] = lane == 1 || lane == 2 ?
                                            reference[yy * 4 + xx] :
                                            filter_reference_round(
                                                radial_reference(reference, 4, 4,
                                                    1 ^ (encoding - 1), true,
                                                    xx, yy) +
                                                radial_reference(reference, 4, 4,
                                                    2 ^ (encoding - 1), true,
                                                    xx, yy), 2);
                                    }
                                    color_pixel_reference(&p, rgb, state, output);
                                    for (unsigned channel = 0; channel < 3;
                                         channel++) {
                                        planes[channel][at] = output[channel];
                                    }
                                }
                            }
                        }
                    }
                    for (unsigned channel = 0; channel < 3; channel++) {
                        post_reference_size(&p.post, channel, planes[channel],
                                             p.output_width, p.output_height);
                    }
                    unsigned pixels = p.output_width * p.output_height;

                    for (unsigned i = 0; i < sizeof(y); i++) {
                        g_assert_cmpuint(y[i], ==, i < pixels ? MIN(255,
                            filter_reference_round(planes[0][i], 16)) : 0xa5);
                    }
                    for (unsigned row = 0; row < p.output_height; row += 2) {
                        for (unsigned x = 0; x < p.output_width; x += 2) {
                            unsigned at = row * p.output_width + x;

                            for (unsigned channel = 1; channel < 3; channel++) {
                                uint16_t *plane = planes[channel];
                                unsigned mean = filter_reference_round(
                                    plane[at] + plane[at + 1] +
                                    plane[at + p.output_width] +
                                    plane[at + p.output_width + 1], 4);

                                g_assert_cmpuint(uv[row / 2 * p.output_width +
                                    x + channel - 1], ==, MIN(255,
                                    filter_reference_round(mean, 16)));
                            }
                        }
                    }
                    for (unsigned i = pixels / 2; i < sizeof(uv); i++) {
                        g_assert_cmphex(uv[i], ==, 0xa5);
                    }
                }
            }
        }
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

static void post(void)
{
    static const uint16_t patterns[][16] = {
        { 1000, 1017, 1041, 1087, 1031, 1003, 1099, 1055,
          1079, 1121, 1023, 1101, 1113, 1081, 1147, 1009 },
        { 0, 0, 4095, 4095, 0, 0, 4095, 4095,
          0, 0, 4095, 4095, 0, 0, 4095, 4095 },
        { 900, 1400, 1100, 2300, 1700, 1200, 1900, 1000,
          2400, 800, 1600, 2800, 1500, 1800, 1000, 1300 },
    };
    bool differentiated[7] = { 0 };

    for (unsigned pattern = 0; pattern < ARRAY_SIZE(patterns); pattern++) {
        FilterOutputs baseline = { 0 };

        for (unsigned variant = 0; variant < 8; variant++) {
            NikonPairedPixelConfig p = config();
            FilterOutputs actual = { 0 };
            uint16_t planes[3][16];

            p.grid_width = p.grid_height = 4;
            p.a[4] = 512;
            p.a[8] = 256;
            memset(p.b, 0, sizeof(p.b));
            p.b[0] = 512;
            p.b[4] = 256;
            p.b[5] = -256;
            p.b[6] = -256;
            p.b[7] = 256;
            p.c[4] = p.c[8] = 0;
            p.c[5] = -1024;
            p.c[7] = 1024;
            switch (variant) {
            case 1: p.post.unsharp[0] = 256; break;
            case 2: p.post.unsharp[1] = 256; break;
            case 3: p.post.smooth[0] = 31; break;
            case 4: p.post.smooth[1] = 31; break;
            case 5: p.post.edge_strength = 18; break;
            case 6: p.post.original = 0; p.post.transformed = 256; break;
            case 7:
                p.post = (NikonPairedPost) {
                    .unsharp = { 192, 96 }, .original = 147,
                    .transformed = 109, .smooth = { 17, 9 },
                    .edge_strength = 12, .edge_limit = 5,
                    .negative_base = 10, .positive_base = 10,
                    .mix = 128, .radius = 1,
                };
                break;
            }
            g_assert_true(pixels_checked(&p, patterns[pattern], NULL, NULL,
                actual.half, actual.residual, actual.reference, actual.y, actual.uv));
            /* Independently computed pre-color reference samples feed the
             * A/B, signed C and complementary-blend byte oracle.
             */
            for (unsigned i = 0; i < 16; i++) {
                int s = actual.reference[i];
                int g = filter_reference_round(s, 2);
                int b = filter_reference_round(s, 4);
                int u = filter_reference_round(g - b, 2);
                int v = filter_reference_round(g - s, 2);
                int cu = filter_reference_round(p.post.original * u -
                    (int)p.post.transformed * v, 256);
                int cv = filter_reference_round(p.post.original * v +
                    (int)p.post.transformed * u, 256);

                planes[0][i] = s;
                planes[1][i] = CLAMP(cu + 2048, 0, 4095);
                planes[2][i] = CLAMP(cv + 2048, 0, 4095);
            }
            for (unsigned channel = 0; channel < 3; channel++) {
                post_reference(&p.post, channel, planes[channel]);
            }
            for (unsigned i = 0; i < 16; i++) {
                g_assert_cmpuint(actual.y[i], ==,
                    MIN(255, filter_reference_round(planes[0][i], 16)));
            }
            for (unsigned block = 0; block < 4; block++) {
                unsigned at = block / 2 * 8 + block % 2 * 2;

                for (unsigned channel = 1; channel < 3; channel++) {
                    unsigned mean = (planes[channel][at] +
                        planes[channel][at + 1] + planes[channel][at + 4] +
                        planes[channel][at + 5] + 2) / 4;

                    g_assert_cmpuint(actual.uv[2 * block + channel - 1], ==,
                        MIN(255, filter_reference_round(mean, 16)));
                }
            }
            if (!variant) {
                baseline = actual;
            } else {
                g_assert_cmpmem(actual.reference, sizeof(actual.reference),
                                baseline.reference, sizeof(baseline.reference));
                g_assert_cmpmem(actual.half, sizeof(actual.half),
                                baseline.half, sizeof(baseline.half));
                differentiated[variant - 1] |=
                    memcmp(actual.y, baseline.y, sizeof(actual.y)) ||
                    memcmp(actual.uv, baseline.uv, sizeof(actual.uv));
            }
        }
    }
    for (unsigned i = 0; i < ARRAY_SIZE(differentiated); i++) {
        g_assert_true(differentiated[i]);
    }
}

static void post_packet(void)
{
    static const uint16_t texture[16] = {
        300, 2400, 800, 3500, 1700, 1100, 3100, 700,
        3800, 600, 2000, 1300, 1000, 3300, 400, 2800,
    };
    bool changed[13] = { 0 };

    for (unsigned pattern = 0; pattern < 7; pattern++) {
        FilterOutputs baseline = { 0 };
        uint16_t raw[16];

        for (unsigned i = 0; i < 16; i++) {
            raw[i] = pattern == 0 ? texture[i] : pattern == 1 ?
                1000 + texture[i] / 4 : pattern == 2 ? 1600 :
                pattern == 3 ? (i % 4 < 2 ? 500 : 3500) :
                pattern == 4 ? (i % 4 < 2 ? 0 : 4095) :
                pattern == 5 ? 0 : 4095;
        }
        for (unsigned variant = 0; variant < 30; variant++) {
            NikonPairedPixelConfig p = config();
            FilterOutputs actual = { 0 };
            uint16_t planes[3][16];

            p.grid_width = p.grid_height = 4;
            p.a[4] = 512;
            p.a[8] = 256;
            memset(p.b, 0, sizeof(p.b));
            p.b[0] = 512;
            p.b[4] = 256;
            p.b[5] = -256;
            p.b[6] = -256;
            p.b[7] = 256;
            p.post.negative_base = 30;
            p.post.positive_base = 60;
            p.post.slope = 2;
            p.post.retain = 4;
            p.post.amount = 180;
            p.post.modifier = 400;
            switch (variant) {
            case 1: p.post.negative_base = 10; break;
            case 2: p.post.positive_base = 200; break;
            case 3: p.post.slope = 0; break;
            case 4: p.post.retain = 25; break;
            case 5: p.post.amount = 80; break;
            case 6: p.post.modifier = 100; break;
            case 7: p.post.mix = 64; break;
            case 8:
                p.post.radius = 3;
                p.post.diagonal = 16;
                p.post.amount = 80;
                break;
            case 9: p.post.amount = 0; break;
            case 10: p.post.modifier = 0; break;
            case 11: p.post.retain = 31; break;
            case 12:
                p.post.amount = 360;
                p.post.mix = 64;
                p.post.modifier = 511;
                break;
            case 13:
                p.post.unsharp[0] = 192;
                p.post.unsharp[1] = 96;
                p.post.smooth[0] = 17;
                p.post.smooth[1] = 9;
                p.post.edge_strength = 18;
                break;
            }
            if (variant >= 14) {
                unsigned mask = variant - 14;

                /* All 16 active/neutral combinations of the first four
                 * passes, with opposite luma/chroma unsharp choices. Keep
                 * the full five-pass scalar oracle below: it still computes
                 * zero corrections, clamps and edge neighborhoods. Exercise
                 * each neutral kernel factor with the remaining multipliers
                 * maximal for this mix, as well as both sample endpoints.
                 */
                p.post.unsharp[0] = mask & 1 ? 256 : 0;
                p.post.unsharp[1] = mask & 1 ? 0 : 256;
                p.post.smooth[0] = mask & 2 ? 31 : 0;
                p.post.smooth[1] = mask & 4 ? 31 : 0;
                p.post.negative_base = p.post.positive_base = 200;
                p.post.slope = 4;
                p.post.retain = 0;
                p.post.amount = 360;
                p.post.modifier = 511;
                p.post.mix = 64;
                p.post.edge_strength = 18;
                if (!(mask & 8)) {
                    switch (mask % 3) {
                    case 0: p.post.amount = 0; break;
                    case 1: p.post.modifier = 0; break;
                    case 2: p.post.retain = 31; break;
                    }
                }
            }
            g_assert_true(nikon_paired_post_valid(&p.post));
            g_assert_true(pixels_checked(&p, raw, NULL, NULL, actual.half,
                actual.residual, actual.reference, actual.y, actual.uv));
            for (unsigned i = 0; i < 16; i++) {
                int s = actual.reference[i];
                int green = filter_reference_round(s, 2);
                int blue = filter_reference_round(s, 4);

                planes[0][i] = s;
                planes[1][i] = 2048 + filter_reference_round(green - blue, 2);
                planes[2][i] = 2048 + filter_reference_round(green - s, 2);
            }
            for (unsigned channel = 0; channel < 3; channel++) {
                post_reference(&p.post, channel, planes[channel]);
            }
            for (unsigned i = 0; i < 16; i++) {
                g_assert_cmpuint(actual.y[i], ==,
                    MIN(255, filter_reference_round(planes[0][i], 16)));
            }
            for (unsigned block = 0; block < 4; block++) {
                unsigned at = block / 2 * 8 + block % 2 * 2;

                for (unsigned channel = 1; channel < 3; channel++) {
                    unsigned mean = filter_reference_round(planes[channel][at] +
                        planes[channel][at + 1] + planes[channel][at + 4] +
                        planes[channel][at + 5], 4);

                    g_assert_cmpuint(actual.uv[2 * block + channel - 1], ==,
                        MIN(255, filter_reference_round(mean, 16)));
                }
            }
            if (!variant) {
                baseline = actual;
            } else {
                g_assert_cmpmem(actual.half, sizeof(actual.half),
                                baseline.half, sizeof(baseline.half));
                g_assert_cmpmem(actual.reference, sizeof(actual.reference),
                                baseline.reference, sizeof(baseline.reference));
                if (variant <= ARRAY_SIZE(changed)) {
                    changed[variant - 1] |= memcmp(actual.y, baseline.y,
                        sizeof(actual.y)) || memcmp(actual.uv, baseline.uv,
                        sizeof(actual.uv));
                }
            }
        }
    }
    for (unsigned i = 0; i < ARRAY_SIZE(changed); i++) {
        g_assert_true(changed[i]);
    }
    for (unsigned level = 0; level <= 4095; level += 1365) {
        NikonPairedPixelConfig p = config();
        PairedOutputs actual;
        uint16_t raw[16];

        p.post.negative_base = 30;
        p.post.positive_base = 60;
        p.post.slope = 2;
        p.post.amount = 180;
        p.post.modifier = 511;
        fill(raw, 16, level);
        g_assert_true(run(&p, raw, NULL, NULL, &actual));
        for (unsigned i = 0; i < 16; i++) {
            g_assert_cmpuint(actual.y[i], ==, MIN(255, (level + 8) / 16));
        }
        for (unsigned i = 0; i < 8; i++) {
            g_assert_cmpuint(actual.uv[i], ==, 128);
        }
    }
}

static void post_invalid(void)
{
    uint16_t raw[16];

    fill(raw, 16, 1000);
    g_assert_false(nikon_paired_post_valid(NULL));
    for (unsigned test = 0; test < 25; test++) {
        NikonPairedPixelConfig p = config();

        switch (test) {
        case 0: p.post.unsharp[0] = 257; break;
        case 1: p.post.unsharp[1] = 257; break;
        case 2: p.post.original = 257; break;
        case 3: p.post.transformed = 1; break;
        case 4: p.post.smooth[0] = 32; break;
        case 5: p.post.smooth[1] = 32; break;
        case 6: p.post.edge_strength = 2; break;
        case 7: p.post.edge_strength = 19; break;
        case 8: p.post.edge_limit = 4; break;
        case 9: p.c[0] = 512; break;
        case 10: p.c[1] = 1; break;
        case 11: p.c[3] = 1; break;
        case 12: p.c[6] = 1; break;
        case 13: p.post.negative_base = 9; break;
        case 14: p.post.positive_base = 201; break;
        case 15: p.post.slope = 5; break;
        case 16: p.post.retain = 32; break;
        case 17: p.post.amount = 236; break;
        case 18: p.post.modifier = 512; break;
        case 19: p.post.mix = 63; break;
        case 20: p.post.radius = 0; break;
        case 21: p.post.diagonal = 1; break;
        case 22:
            p.post.radius = 3; p.post.diagonal = 16; p.post.amount = 3;
            break;
        case 23:
            p.post.radius = 3; p.post.diagonal = 16; p.post.amount = 101;
            break;
        case 24:
            p.post.radius = 3; p.post.diagonal = 16;
            p.post.amount = 80; p.post.mix = 64;
            break;
        }
        rejected(&p, raw, NULL, NULL);
    }
}

static void main_gain(void)
{
    static const uint16_t gains[] = { 0, 128, 256, 512, 65535 };
    static const uint16_t levels[] = { 0, 1, 17, 1024, 2056, 4095 };
    static const int16_t a[] = { 1024, 0, 0, 0, 512, 0, 0, 0, 256 };
    static const int16_t b[] = { 512, 0, 0, 0, 256, -256, -256, 256, 0 };

    for (unsigned sample = 0; sample < ARRAY_SIZE(levels); sample++) {
        NikonPairedPixelConfig p = config();
        PairedOutputs reference;
        uint16_t raw[16];

        memcpy(p.a, a, sizeof(a));
        memcpy(p.b, b, sizeof(b));
        fill(raw, ARRAY_SIZE(raw), levels[sample]);
        g_assert_true(run(&p, raw, NULL, NULL, &reference));
        for (unsigned i = 0; i < ARRAY_SIZE(gains); i++) {
            PairedOutputs actual;
            uint64_t scaled = ((uint64_t)levels[sample] * gains[i] + 128) / 256;
            unsigned expected = MIN(255, (MIN(scaled, 4095) + 8) / 16);

            p.main_gain = gains[i];
            g_assert_true(run(&p, raw, NULL, NULL, &actual));
            for (unsigned at = 0; at < ARRAY_SIZE(actual.y); at++) {
                g_assert_cmpuint(actual.y[at], ==, expected);
            }
            g_assert_cmpmem(actual.uv, sizeof(actual.uv),
                            reference.uv, sizeof(reference.uv));
            g_assert_cmpmem(actual.half, sizeof(actual.half),
                            reference.half, sizeof(reference.half));
            g_assert_cmpmem(actual.residual, sizeof(actual.residual),
                            reference.residual, sizeof(reference.residual));
            g_assert_cmpmem(actual.reference, sizeof(actual.reference),
                            reference.reference, sizeof(reference.reference));
        }
    }
}

static void saturation(void)
{
    static const uint16_t levels[] = { 0, 127, 128, 129, 2047, 4095 };
    NikonPairedPixelConfig p = config();
    uint16_t raw[16];
    bool changed[7] = { 0 };

    p.grid_width = p.grid_height = 4;
    p.a[4] = 512;
    p.a[8] = 256;
    memset(p.b, 0, sizeof(p.b));
    p.b[0] = 512;
    p.b[4] = 256;
    p.b[5] = -256;
    p.b[6] = -256;
    p.b[7] = 256;
    p.c[4] = p.c[8] = 0;
    p.c[5] = -1024;
    p.c[7] = 1024;
    p.post.original = 147;
    p.post.transformed = 109;
    for (unsigned pattern = 0; pattern <= ARRAY_SIZE(levels); pattern++) {
        FilterOutputs baseline = { 0 };

        for (unsigned i = 0; i < 16; i++) {
            raw[i] = pattern < ARRAY_SIZE(levels) ? levels[pattern] :
                     (i * 701 + i * i * 13) % 4096;
        }
        for (unsigned curve = 0; curve < 8; curve++) {
            FilterOutputs actual = { 0 };
            uint16_t planes[3][16];

            for (unsigned i = 0; i < 33; i++) {
                switch (curve) {
                case 0: p.saturation[i] = 256; break;
                case 1: p.saturation[i] = 0; break;
                case 2: p.saturation[i] = 128; break;
                case 3: p.saturation[i] = 384; break;
                case 4: p.saturation[i] = i * 2047; break;
                case 5: p.saturation[i] = i & 1 ? 512 : 0; break;
                case 6: p.saturation[i] = 96 + MIN(160, 8 * i); break;
                case 7: p.saturation[i] = UINT16_MAX; break;
                }
            }
            g_assert_true(pixels_checked(&p, raw, NULL, NULL, actual.half,
                actual.residual, actual.reference, actual.y, actual.uv));
            for (unsigned i = 0; i < 16; i++) {
                int s = actual.reference[i];
                int g = filter_reference_round(s, 2);
                int blue = filter_reference_round(s, 4);
                int u = filter_reference_round(g - blue, 2);
                int v = filter_reference_round(g - s, 2);
                int cu = filter_reference_round(p.post.original * u -
                    (int)p.post.transformed * v, 256);
                int cv = filter_reference_round(p.post.original * v +
                    (int)p.post.transformed * u, 256);
                unsigned first = s / 128, remainder = s % 128;
                unsigned gain = filter_reference_round(
                    p.saturation[first] * (128 - remainder) +
                    p.saturation[first + 1] * remainder, 128);

                planes[0][i] = s;
                planes[1][i] = CLAMP(filter_reference_round(
                    (int64_t)cu * gain, 256) + 2048, 0, 4095);
                planes[2][i] = CLAMP(filter_reference_round(
                    (int64_t)cv * gain, 256) + 2048, 0, 4095);
            }
            for (unsigned channel = 0; channel < 3; channel++) {
                post_reference(&p.post, channel, planes[channel]);
            }
            for (unsigned i = 0; i < 16; i++) {
                g_assert_cmpuint(actual.y[i], ==,
                    MIN(255, filter_reference_round(planes[0][i], 16)));
            }
            for (unsigned block = 0; block < 4; block++) {
                unsigned at = block / 2 * 8 + block % 2 * 2;

                for (unsigned channel = 1; channel < 3; channel++) {
                    unsigned mean = filter_reference_round(planes[channel][at] +
                        planes[channel][at + 1] + planes[channel][at + 4] +
                        planes[channel][at + 5], 4);

                    g_assert_cmpuint(actual.uv[2 * block + channel - 1], ==,
                        MIN(255, filter_reference_round(mean, 16)));
                }
            }
            if (!curve) {
                baseline = actual;
            } else {
                g_assert_cmpmem(actual.y, sizeof(actual.y),
                                baseline.y, sizeof(baseline.y));
                g_assert_cmpmem(actual.reference, sizeof(actual.reference),
                                baseline.reference, sizeof(baseline.reference));
                g_assert_cmpmem(actual.half, sizeof(actual.half),
                                baseline.half, sizeof(baseline.half));
                g_assert_cmpmem(actual.residual, sizeof(actual.residual),
                                baseline.residual, sizeof(baseline.residual));
                changed[curve - 1] |= memcmp(actual.uv, baseline.uv,
                                             sizeof(actual.uv)) != 0;
            }
        }
    }
    for (unsigned i = 0; i < ARRAY_SIZE(changed); i++) {
        g_assert_true(changed[i]);
    }
    /* Every knot is observable, including the4096 endpoint via Y4095. */
    p.grid_width = p.grid_height = 2;
    for (unsigned knot = 0; knot < 33; knot++) {
        PairedOutputs before, actual;

        for (unsigned i = 0; i < 33; i++) {
            p.saturation[i] = 256;
        }
        fill(raw, 16, MIN(4095, knot * 128 + (knot ? 0 : 64)));
        g_assert_true(run(&p, raw, NULL, NULL, &before));
        p.saturation[knot] = 512;
        g_assert_true(run(&p, raw, NULL, NULL, &actual));
        g_assert_cmpint(memcmp(before.uv, actual.uv, sizeof(actual.uv)), !=, 0);
    }
}

static void saturation_cfa(void)
{
    NikonPairedPixelConfig p = config();
    uint16_t raw[16];
    PairedOutputs out;

    memset(p.b, 0, sizeof(p.b));
    p.b[0] = 512;
    p.b[4] = 256;
    p.b[5] = -256;
    p.b[6] = -256;
    p.b[7] = 256;
    for (unsigned i = 0; i < 33; i++) {
        p.saturation[i] = 64 + 16 * i;
    }
    for (p.encoding = NIKON_RAW_RGGB;
         p.encoding <= NIKON_RAW_BGGR; p.encoding++) {
        for (unsigned i = 0; i < 16; i++) {
            unsigned lane = (p.encoding - 1) ^ ((i / 4 % 2) * 2 + i % 2);

            raw[i] = lane == 0 ? 3200 : lane == 3 ? 400 : 1200;
        }
        g_assert_true(run(&p, raw, NULL, NULL, &out));
        for (unsigned i = 0; i < 16; i++) {
            g_assert_cmpuint(out.y[i], ==, 200);
        }
        for (unsigned i = 0; i < 8; i += 2) {
            g_assert_cmpuint(out.uv[i], ==, 173);
            g_assert_cmpuint(out.uv[i + 1], ==, 15);
        }
        fill(raw, 16, 1000);
        for (unsigned gain = 0; gain <= 1; gain++) {
            fill(p.saturation, 33, gain ? UINT16_MAX : 0);
            g_assert_true(run(&p, raw, NULL, NULL, &out));
            for (unsigned i = 0; i < 8; i++) {
                g_assert_cmpuint(out.uv[i], ==, 128);
            }
        }
        for (unsigned i = 0; i < 33; i++) {
            p.saturation[i] = 64 + 16 * i;
        }
    }
}

static unsigned confidence_reference(const NikonPairedPixelConfig *p,
                                      const uint16_t *map, int x, int y)
{
    int64_t coordinate[2] = {
        p->grid_start_x + 16 * ((int64_t)x - p->grid_origin_x) * p->grid_step_x,
        p->grid_start_y + 16 * ((int64_t)y - p->grid_origin_y) * p->grid_step_y,
    };
    const int64_t unit = INT64_C(1) << 22;
    unsigned bounds[] = { p->grid_width, p->grid_height };
    int64_t base[2], fraction[2], total = 0;

    for (unsigned axis = 0; axis < 2; axis++) {
        coordinate[axis] = CLAMP(coordinate[axis], 0,
                                (bounds[axis] - 1) * unit);
        base[axis] = coordinate[axis] / unit;
        fraction[axis] = coordinate[axis] % unit;
    }
    for (unsigned corner = 0; corner < 4; corner++) {
        unsigned xx = MIN(base[0] + (corner & 1), bounds[0] - 1);
        unsigned yy = MIN(base[1] + (corner >> 1), bounds[1] - 1);
        int64_t wx = corner & 1 ? fraction[0] : unit - fraction[0];
        int64_t wy = corner & 2 ? fraction[1] : unit - fraction[1];

        total += map[yy * p->grid_width + xx] * wx * wy;
    }
    return filter_reference_round(total, unit * unit);
}

static void confidence(void)
{
    static const uint16_t maps[][4] = {
        { 0, 0, 0, 0 }, { 4095, 4095, 4095, 4095 },
        { 0, 1001, 3002, 4095 }, { 4095, 2047, 2048, 0 },
    };
    uint16_t raw[16] = { 300, 2400, 800, 3500, 1700, 1100, 3100, 700,
                         3800, 600, 2000, 1300, 1000, 3300, 400, 2800 };
    bool varied = false;

    for (unsigned geometry = 0; geometry < 5; geometry++) {
        for (unsigned m = 0; m < ARRAY_SIZE(maps); m++) {
            NikonPairedPixelConfig p = config();
            uint16_t half[4], residual[4], before[16], expected[3][16];
            uint8_t y[16], uv[8], no_map_y[16], no_map_uv[8];
            NikonPairedPost rest;

            p.grid_enabled = true;
            p.grid_start_x = (1U << 21) + 3;
            p.grid_start_y = (1U << 20) + 11;
            p.grid_step_x = 49153;
            p.grid_step_y = 32767;
            p.grid_origin_x = geometry == 0 ? 1 : geometry == 1 ? -2 : 0;
            p.grid_origin_y = geometry == 0 ? 2 : geometry == 1 ? -1 : 0;
            if (geometry == 2) {
                p.grid_start_x = UINT32_MAX;
                p.grid_start_y = UINT32_MAX;
            } else if (geometry == 3) {
                p.grid_step_x = p.grid_step_y = 0;
            } else if (geometry == 4) {
                p.x = p.y = 1;
                p.output_width = p.output_height = 2;
            }
            p.post.unsharp[0] = 173;
            p.post.unsharp[1] = 91;
            p.a[4] = 512;
            p.a[8] = 256;
            memset(p.b, 0, sizeof(p.b));
            p.b[0] = 512;
            p.b[4] = p.b[7] = 256;
            p.b[5] = p.b[6] = -256;
            memcpy(before, raw, sizeof(before));
            reference_plane(&p, before, 4, 4, 4);
            if (geometry == 4) {
                uint16_t source[16];

                memcpy(source, before, sizeof(source));
                for (unsigned yy = 0; yy < 2; yy++) {
                    for (unsigned xx = 0; xx < 2; xx++) {
                        before[yy * 2 + xx] = source[(yy + 1) * 4 + xx + 1];
                    }
                }
            }
            int width = p.output_width, height = p.output_height;
            unsigned count = width * height;

            for (unsigned channel = 0; channel < 3; channel++) {
                uint16_t source[16];

                for (unsigned at = 0; at < count; at++) {
                    int s = before[at];
                    int green = filter_reference_round(s, 2);
                    int blue = filter_reference_round(s, 4);

                    source[at] = channel == 0 ? s : 2048 +
                        filter_reference_round(green - (channel == 1 ? blue : s),
                                                2);
                }
                for (int yy = 0; yy < height; yy++) {
                    for (int xx = 0; xx < width; xx++) {
                        unsigned at = yy * width + xx;
                        int sum = source[yy * width + MAX(xx - 1, 0)] +
                            source[yy * width + MIN(xx + 1, width - 1)] +
                            source[MAX(yy - 1, 0) * width + xx] +
                            source[MIN(yy + 1, height - 1) * width + xx];
                        unsigned mask = confidence_reference(&p, maps[m],
                                                              p.x + xx, p.y + yy);
                        int64_t numerator = (4 * (int)source[at] - sum) *
                            (int64_t)p.post.unsharp[!!channel] * mask;

                        expected[channel][at] = CLAMP(source[at] +
                            filter_reference_round(numerator,
                                                    INT64_C(1024) * 4095),
                            0, 4095);
                    }
                }
                rest = p.post;
                rest.unsharp[0] = rest.unsharp[1] = 0;
                post_reference_size(&rest, channel, expected[channel],
                                     width, height);
            }
            g_assert_true(nikon_paired_pixels(&p, raw, NULL, NULL, half,
                                              residual, maps[m], y, uv));
            for (unsigned at = 0; at < count; at++) {
                g_assert_cmpuint(y[at], ==,
                    MIN(255, filter_reference_round(expected[0][at], 16)));
            }
            for (unsigned block = 0; block < count / 4; block++) {
                unsigned at = (block / (width / 2)) * 2 * width +
                              2 * (block % (width / 2));

                for (unsigned channel = 1; channel < 3; channel++) {
                    unsigned mean = filter_reference_round(
                        expected[channel][at] + expected[channel][at + 1] +
                        expected[channel][at + width] +
                        expected[channel][at + width + 1], 4);

                    g_assert_cmpuint(uv[2 * block + channel - 1], ==,
                        MIN(255, filter_reference_round(mean, 16)));
                }
            }
            p.grid_enabled = false;
            g_assert_true(nikon_paired_pixels(&p, raw, NULL, NULL, half,
                                              residual, NULL,
                                              no_map_y, no_map_uv));
            if (m == 1) {
                g_assert_cmpmem(y, count, no_map_y, count);
                g_assert_cmpmem(uv, count / 2, no_map_uv, count / 2);
            } else if (m == 2) {
                varied |= memcmp(y, no_map_y, count) != 0;
            }
        }
    }
    g_assert_true(varied);
}

static void confidence_invalid(void)
{
    NikonPairedPixelConfig p = config();
    uint16_t raw[16], map[4] = { 1, 2, 3, 4096 };
    PairedOutputs out, saved;

    fill(raw, 16, 1000);
    p.grid_enabled = true;
    for (unsigned bad = 0; bad < 9; bad++) {
        NikonPairedPixelConfig invalid = p;
        const uint16_t *input = map;

        memset(&out, 0xa5, sizeof(out));
        saved = out;
        switch (bad) {
        case 0: input = NULL; break;
        case 1: break; /* Out-of-domain actual sample. */
        case 2: invalid.grid_width = 0; break;
        case 3: invalid.grid_height = 4097; break;
        case 4: invalid.grid_origin_x = -4096; break;
        case 5: invalid.grid_origin_y = 65536; break;
        case 6: input = out.half; break;
        case 7: input = (uint16_t *)out.y; break;
        case 8: map[3] = 4095; break;
        }
        g_assert_false(nikon_paired_pixels(&invalid, raw, NULL, NULL, out.half,
            bad == 8 ? out.half : out.residual, input, out.y, out.uv));
        g_assert_cmpmem(&out, sizeof(out), &saved, sizeof(saved));
    }
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/nikon/pairedpixels/post-packet", post_packet);
    g_test_add_func("/nikon/pairedpixels/saturation", saturation);
    g_test_add_func("/nikon/pairedpixels/saturation-cfa", saturation_cfa);
    g_test_add_func("/nikon/pairedpixels/colorfilter", colorfilter);
    g_test_add_func("/nikon/pairedpixels/colorfilter-fields",
                     colorfilter_fields);
    g_test_add_func("/nikon/pairedpixels/colorfilter-cfa", colorfilter_cfa);
    g_test_add_func("/nikon/pairedpixels/refine-neutral", refine_neutral);
    g_test_add_func("/nikon/pairedpixels/color-identities", color_identities);
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
    g_test_add_func("/nikon/pairedpixels/post", post);
    g_test_add_func("/nikon/pairedpixels/post-invalid", post_invalid);
    g_test_add_func("/nikon/pairedpixels/main-gain", main_gain);
    g_test_add_func("/nikon/pairedpixels/confidence", confidence);
    g_test_add_func("/nikon/pairedpixels/confidence-invalid", confidence_invalid);
    return g_test_run();
}
