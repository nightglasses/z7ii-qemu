/*
 * Synthetic independent vectors for the declared RGB-grid numerical choices.
 * No factory coefficients or calibration tables are embedded here.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "hw/nikon/gategrid.h"

static void gain(void)
{
    NikonGateGridGain p[3] = {
        { .gain = 1, .shift = 1, .upper = UINT16_MAX },
        { .gain = 2, .offset = -3, .upper = 100 },
        { .gain = 65535, .offset = 65535, .upper = 42 },
    };
    uint16_t input[] = { 1, 3, 2, 4, 5, 7 };
    const uint16_t expected[] = { 1, 2, 0, 2, 42, 42 };

    /* In-place output also proves the complete input is read before copying. */
    g_assert_true(nikon_gate_grid_gain3(p, input, 2, input, 6));
    g_assert_cmpmem(input, sizeof(input), expected, sizeof(expected));
    p[0].gain = 0;
    p[0].lower = 9;
    g_assert_true(nikon_gate_grid_gain3(p, input, 2, input, 6));
    g_assert_cmpuint(input[0], ==, 9);
    g_assert_cmpuint(input[1], ==, 9);
}

static void lut(void)
{
    static const uint16_t x[] = {
        0, 4, 7, 8, 12, 15, 16, 16375, 16376, 16383,
    };
    uint16_t bank[6][1024], input[3 * ARRAY_SIZE(x)], out[ARRAY_SIZE(input)];

    for (unsigned c = 0; c < 3; c++) {
        for (unsigned i = 0; i < 1024; i++) {
            bank[2 * c][i] = 2 * i + 2000 * c;
            bank[2 * c + 1][i] = 2 * i + 2000 * c + 1;
        }
        memcpy(input + c * ARRAY_SIZE(x), x, sizeof(x));
    }
    g_assert_true(nikon_gate_grid_lut3(bank, input, ARRAY_SIZE(x), out,
                                      ARRAY_SIZE(out)));
    for (unsigned c = 0; c < 3; c++) {
        for (unsigned i = 0; i < ARRAY_SIZE(x); i++) {
            unsigned expected = MIN((x[i] + 4) / 8, 2047) + 2000 * c;

            g_assert_cmpuint(out[c * ARRAY_SIZE(x) + i], ==, expected);
        }
    }
    bank[1][0] = 9;
    bank[0][1] = 17;
    bank[1][1023] = 11;
    g_assert_true(nikon_gate_grid_lut3(bank, input, ARRAY_SIZE(x), out,
                                      ARRAY_SIZE(out)));
    g_assert_cmpuint(out[1], ==, 5);  /* Half between0 and9, ties upward. */
    g_assert_cmpuint(out[3], ==, 9);  /* The odd knot is an actual input. */
    g_assert_cmpuint(out[4], ==, 13); /* Between odd and next even knot. */
    g_assert_cmpuint(out[6], ==, 17);
    g_assert_cmpuint(out[8], ==, 11);
    g_assert_cmpuint(out[9], ==, 11); /* No read beyond final odd knot. */
    g_assert_cmpuint(out[ARRAY_SIZE(x) + 3], ==, 2001);
}

static NikonGateGridMatrix matrix_config(void)
{
    NikonGateGridMatrix p = { .constant = 2 };

    for (unsigned r = 0; r < 4; r++) {
        p.row[r].lower = INT32_MIN;
        p.row[r].upper = INT32_MAX;
    }
    p.row[0].coefficient[0] = p.row[0].coefficient[3] = 1;
    p.row[1].coefficient[1] = -1;
    p.row[1].coefficient[3] = 1;
    p.row[1].shift = 1;
    p.row[2].coefficient[0] = 1;
    p.row[2].coefficient[1] = 2;
    p.row[2].coefficient[2] = 3;
    p.row[2].coefficient[3] = -1;
    p.row[2].lower = 0;
    p.row[2].upper = 30;
    p.row[3].coefficient[3] = -3;
    return p;
}

static void matrix(void)
{
    const int32_t input[] = { 1, -3, 5, -7, 9, -11 };
    const int32_t expected[] = { 3, -1, -2, 5, 30, 0, -6, -6 };
    NikonGateGridMatrix p = matrix_config();
    g_autofree void *storage = g_malloc(sizeof(expected));
    int32_t out[8];

    g_assert_true(nikon_gate_grid_matrix4(&p, input, 2, out, ARRAY_SIZE(out)));
    g_assert_cmpmem(out, sizeof(out), expected, sizeof(expected));
    memcpy(storage, input, sizeof(input));
    g_assert_true(nikon_gate_grid_matrix4(&p, storage, 2, storage, 8));
    g_assert_cmpmem(storage, sizeof(expected), expected, sizeof(expected));

    /* Bounds must permit a full wide sum before the signed output clamp. */
    int32_t maximum[] = { INT32_MAX, INT32_MAX, INT32_MAX };

    p.constant = 65535;
    for (unsigned r = 0; r < 4; r++) {
        p.row[r].shift = 0;
        p.row[r].lower = INT32_MIN;
        p.row[r].upper = INT32_MAX;
        for (unsigned c = 0; c < 4; c++) {
            p.row[r].coefficient[c] = r & 1 ? -32768 : 32767;
        }
    }
    g_assert_true(nikon_gate_grid_matrix4(&p, maximum, 1, out, 4));
    for (unsigned r = 0; r < 4; r++) {
        g_assert_cmpint(out[r], ==, r & 1 ? INT32_MIN : INT32_MAX);
    }
    for (unsigned c = 0; c < 3; c++) {
        maximum[c] = INT32_MIN;
    }
    g_assert_true(nikon_gate_grid_matrix4(&p, maximum, 1, out, 4));
    for (unsigned r = 0; r < 4; r++) {
        g_assert_cmpint(out[r], ==, r & 1 ? INT32_MAX : INT32_MIN);
    }
}

/* Direct coordinate enumeration and division/remainder, independent of taps. */
static int32_t convolution(const int32_t *input, unsigned width, unsigned height,
                            unsigned x, unsigned y, const uint16_t kernel[5][5],
                            unsigned shift)
{
    int64_t total = 0, divisor = INT64_C(1) << shift;

    for (int dy = -2; dy <= 2; dy++) {
        int sy = (int)y + dy;

        if (sy < 0) {
            sy = 0;
        } else if (sy >= (int)height) {
            sy = height - 1;
        }
        for (int dx = -2; dx <= 2; dx++) {
            int sx = (int)x + dx;

            if (sx < 0) {
                sx = 0;
            } else if (sx >= (int)width) {
                sx = width - 1;
            }
            total += (int64_t)input[sy * width + sx] * kernel[dy + 2][dx + 2];
        }
    }
    int64_t magnitude = total < 0 ? -total : total;
    int64_t rounded = magnitude / divisor;

    rounded += magnitude % divisor * 2 >= divisor;
    return total < 0 ? -rounded : rounded;
}

static void filter(void)
{
    static const uint16_t kernel[5][5] = {
        { 10, 11, 12, 13, 14 },
        { 15,  1,  2,  3, 16 },
        { 17,  4,  5,  6, 17 },
        { 16,  7,  8,  9, 15 },
        { 14, 13, 12, 11, 10 },
    };
    NikonGateGridFilter p = {
        .shift = 3, .lower = INT32_MIN, .upper = INT32_MAX,
    };
    int32_t input[4 * 25] = { 0 }, out[ARRAY_SIZE(input)];

    for (unsigned i = 0; i < 17; i++) {
        p.tap[i] = i + 1;
    }
    input[12] = 64;
    input[25] = -32;
    input[50 + 4] = 16;
    input[75 + 24] = -8;
    g_assert_true(nikon_gate_grid_filter(&p, input, 4, 5, 5, out,
                                        ARRAY_SIZE(out)));
    for (unsigned c = 0; c < 4; c++) {
        for (unsigned y = 0; y < 5; y++) {
            for (unsigned x = 0; x < 5; x++) {
                g_assert_cmpint(out[c * 25 + y * 5 + x], ==,
                    convolution(input + c * 25, 5, 5, x, y, kernel, p.shift));
            }
        }
    }
    /* Partial overlap is also safe because all reads precede public writes. */
    int32_t overlap[ARRAY_SIZE(input) + 1];

    memcpy(overlap, input, sizeof(input));
    g_assert_true(nikon_gate_grid_filter(&p, overlap, 4, 5, 5, overlap + 1,
                                        ARRAY_SIZE(input)));
    g_assert_cmpmem(overlap + 1, sizeof(out), out, sizeof(out));
    p.shift = 1;
    memset(p.tap, 0, sizeof(p.tap));
    p.tap[4] = 1;
    input[0] = -1;
    input[1] = 1;
    g_assert_true(nikon_gate_grid_filter(&p, input, 1, 2, 1, out, 2));
    g_assert_cmpint(out[0], ==, -1);
    g_assert_cmpint(out[1], ==, 1);
    int32_t extremes[] = { INT32_MIN, INT32_MAX };

    for (unsigned i = 0; i < 17; i++) {
        p.tap[i] = UINT16_MAX;
    }
    p.shift = 0;
    p.lower = -7;
    p.upper = 9;
    g_assert_true(nikon_gate_grid_filter(&p, extremes, 2, 1, 1, out, 2));
    g_assert_cmpint(out[0], ==, -7);
    g_assert_cmpint(out[1], ==, 9);
}

static void constants(void)
{
    static const unsigned binomial[] = { 1, 4, 6, 4, 1 };
    const int32_t values[] = { -10, 0, 17, INT32_MAX };
    NikonGateGridFilter p = {
        .shift = 8, .lower = INT32_MIN, .upper = INT32_MAX,
    };
    int32_t input[4 * 6];

    for (unsigned y = 0; y < 3; y++) {
        for (unsigned x = 0; x < 3; x++) {
            p.tap[y * 3 + x] = binomial[y + 1] * binomial[x + 1];
        }
    }
    for (unsigned i = 0; i < 5; i++) {
        p.tap[9 + i] = binomial[i];
    }
    p.tap[14] = p.tap[15] = 4;
    p.tap[16] = 6;
    for (unsigned c = 0; c < 4; c++) {
        for (unsigned i = 0; i < 6; i++) {
            input[c * 6 + i] = values[c];
        }
    }
    g_assert_true(nikon_gate_grid_filter(&p, input, 4, 3, 2, input,
                                        ARRAY_SIZE(input)));
    for (unsigned c = 0; c < 4; c++) {
        for (unsigned i = 0; i < 6; i++) {
            g_assert_cmpint(input[c * 6 + i], ==, values[c]);
        }
    }
}

static void invalid(void)
{
    uint16_t input[3] = { 1, 2, 16384 }, color[3], saved[3];
    uint16_t bank[6][1024] = { 0 };
    NikonGateGridGain g[3] = { { .gain = 1 }, { .gain = 1 }, { .gain = 1 } };
    NikonGateGridMatrix m = matrix_config();
    NikonGateGridFilter f = { .tap = { 1 }, .upper = 10 };
    int32_t feature[4], original[4], pixel = INT32_MIN;
    const int32_t matrix_input[] = { 1, 2, 16384 };

    memset(color, 0xa5, sizeof(color));
    memcpy(saved, color, sizeof(color));
    g_assert_false(nikon_gate_grid_lut3(bank, input, 1, color, 3));
    g[2].shift = 32;
    g_assert_false(nikon_gate_grid_gain3(g, input, 1, color, 3));
    g[2].shift = 0;
    g[2].offset = -65536;
    g_assert_false(nikon_gate_grid_gain3(g, input, 1, color, 3));
    g[2].offset = 0;
    g[2].gain = 65536;
    g_assert_false(nikon_gate_grid_gain3(g, input, 1, color, 3));
    g[2].gain = 1;
    g[2].lower = 1;
    g_assert_false(nikon_gate_grid_gain3(g, input, 1, color, 3));
    g_assert_false(nikon_gate_grid_lut3(bank, input, 1, color, 2));
    g_assert_false(nikon_gate_grid_gain3(g, input, 0, color, 0));
    g_assert_cmpmem(color, sizeof(color), saved, sizeof(saved));

    memset(feature, 0xa5, sizeof(feature));
    memcpy(original, feature, sizeof(original));
    m.row[3].coefficient[3] = 32768;
    g_assert_false(nikon_gate_grid_matrix4(&m, matrix_input, 1, feature, 4));
    m = matrix_config();
    m.row[3].shift = 32;
    g_assert_false(nikon_gate_grid_matrix4(&m, matrix_input, 1, feature, 4));
    m = matrix_config();
    m.constant = 65536;
    g_assert_false(nikon_gate_grid_matrix4(&m, matrix_input, 1, feature, 4));
    m = matrix_config();
    m.row[3].lower = 1;
    m.row[3].upper = 0;
    g_assert_false(nikon_gate_grid_matrix4(&m, matrix_input, 1, feature, 4));
    g_assert_false(nikon_gate_grid_filter(&f, &pixel, 5, 1, 1, feature, 4));
    g_assert_false(nikon_gate_grid_filter(&f, &pixel, 1, 0, 1, feature, 1));
    g_assert_false(nikon_gate_grid_filter(&f, &pixel, 1, 4097, 1, feature, 4));
    f.shift = 32;
    g_assert_false(nikon_gate_grid_filter(&f, &pixel, 1, 1, 1, feature, 1));
    f.shift = 0;
    f.lower = 11;
    g_assert_false(nikon_gate_grid_filter(&f, &pixel, 1, 1, 1, feature, 1));
    g_assert_cmpmem(feature, sizeof(feature), original, sizeof(original));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/nikon/gategrid/gain", gain);
    g_test_add_func("/nikon/gategrid/lut", lut);
    g_test_add_func("/nikon/gategrid/matrix", matrix);
    g_test_add_func("/nikon/gategrid/filter", filter);
    g_test_add_func("/nikon/gategrid/constants", constants);
    g_test_add_func("/nikon/gategrid/invalid", invalid);
    return g_test_run();
}
