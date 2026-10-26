/*
 * Finite monochrome RAW12 operator. Filter thresholds, temporal weights,
 * matrix rounding and grid averaging are explicit functional hypotheses,
 * not recovered silicon processing or physical-camera calibration.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "hw/nikon/pairedpixels.h"

#define PAIRED_MAX_DIMENSION 4096
#define PAIRED_MAX_SAMPLE 4095

static uint16_t paired_clamp(int32_t value)
{
    return CLAMP(value, 0, PAIRED_MAX_SAMPLE);
}

static int32_t paired_round(int32_t value, unsigned shift)
{
    int32_t half = 1U << (shift - 1);
    int32_t scale = 1U << shift;

    /* Symmetric nearest rounding, with half-way values away from zero. */
    return value < 0 ? -((-value + half) / scale) : (value + half) / scale;
}

static bool paired_samples_valid(const uint16_t *samples, size_t count)
{
    size_t i;

    if (samples) {
        for (i = 0; i < count; i++) {
            if (samples[i] > PAIRED_MAX_SAMPLE) {
                return false;
            }
        }
    }
    return true;
}

static bool paired_config_valid(const NikonPairedPixelConfig *p)
{
    unsigned i;

    if (!p || !p->width || !p->height ||
        p->width > PAIRED_MAX_DIMENSION || p->height > PAIRED_MAX_DIMENSION ||
        !p->output_width || !p->output_height ||
        (p->output_width & 1) || (p->output_height & 1) ||
        p->x >= p->width || p->y >= p->height ||
        p->output_width > p->width - p->x ||
        p->output_height > p->height - p->y ||
        !p->grid_width || !p->grid_height ||
        p->grid_width > p->width || p->grid_height > p->height) {
        return false;
    }
    for (i = 0; i < 9; i++) {
        if (p->a[i] < -4096 || p->a[i] > 4095 ||
            p->c[i] < -4096 || p->c[i] > 4095 ||
            p->b[i] < -2048 || p->b[i] > 2047) {
            return false;
        }
    }
    return true;
}

static uint16_t paired_dpc(const uint16_t *raw, unsigned width, unsigned height,
                           unsigned x, unsigned y)
{
    unsigned left = x ? x - 1 : x;
    unsigned right = x + 1 < width ? x + 1 : x;
    unsigned up = y ? y - 1 : y;
    unsigned down = y + 1 < height ? y + 1 : y;
    uint16_t cross[] = { raw[y * width + x], raw[y * width + left],
                         raw[y * width + right], raw[up * width + x],
                         raw[down * width + x] };
    uint16_t low = cross[1], high = cross[1], center = cross[0];
    unsigned i, j;

    /* Border neighbors replicate the edge; no samples outside the ROI exist. */
    for (i = 2; i < ARRAY_SIZE(cross); i++) {
        low = MIN(low, cross[i]);
        high = MAX(high, cross[i]);
    }
    if (high - low > 64) {
        return center;
    }
    for (i = 1; i < ARRAY_SIZE(cross); i++) {
        uint16_t sample = cross[i];

        for (j = i; j && cross[j - 1] > sample; j--) {
            cross[j] = cross[j - 1];
        }
        cross[j] = sample;
    }
    return abs((int)center - cross[2]) > 1024 ? cross[2] : center;
}

static void paired_spatial(const uint16_t *dpc, uint16_t *filtered,
                            unsigned width, unsigned height)
{
    unsigned x, y;

    for (y = 0; y < height; y++) {
        unsigned up = y ? y - 1 : y;
        unsigned down = y + 1 < height ? y + 1 : y;

        for (x = 0; x < width; x++) {
            unsigned left = x ? x - 1 : x;
            unsigned right = x + 1 < width ? x + 1 : x;
            unsigned sum = 4 * dpc[y * width + x] + dpc[y * width + left] +
                           dpc[y * width + right] + dpc[up * width + x] +
                           dpc[down * width + x];

            filtered[y * width + x] = (sum + 4) / 8;
        }
    }
}

static void paired_temporal(uint16_t *filtered, unsigned width, unsigned height,
                             const uint16_t *prior_luma,
                             const uint16_t *prior_residual,
                             uint16_t *out_luma, uint16_t *out_residual)
{
    unsigned half_width = (width + 1) / 2;
    unsigned hx, hy, x, y;

    for (hy = 0; hy < (height + 1) / 2; hy++) {
        for (hx = 0; hx < half_width; hx++) {
            unsigned last_x = MIN(2 * hx + 2, width);
            unsigned last_y = MIN(2 * hy + 2, height);
            unsigned count = (last_x - 2 * hx) * (last_y - 2 * hy);
            unsigned index = hy * half_width + hx;
            unsigned current, deviation = 0, old_weight = 0, sum = 0;

            for (y = 2 * hy; y < last_y; y++) {
                for (x = 2 * hx; x < last_x; x++) {
                    sum += filtered[y * width + x];
                }
            }
            current = (sum + count / 2) / count;
            if (prior_luma) {
                unsigned residual = prior_residual ? prior_residual[index] : 0;

                deviation = abs((int)current - prior_luma[index]);
                if (deviation <= 256) {
                    /* Q8 weight: at most one quarter, zero at residual 256. */
                    old_weight = (64 * (256 - MIN(residual, 256U)) + 128) / 256;
                }
            }
            sum = 0;
            for (y = 2 * hy; y < last_y; y++) {
                for (x = 2 * hx; x < last_x; x++) {
                    unsigned value = filtered[y * width + x];

                    if (old_weight) {
                        value = (value * (256 - old_weight) +
                                 prior_luma[index] * old_weight + 128) / 256;
                        filtered[y * width + x] = value;
                    }
                    sum += value;
                }
            }
            out_luma[index] = (sum + count / 2) / count;
            out_residual[index] = deviation;
        }
    }
}

static void paired_grid(const uint16_t *filtered,
                         const NikonPairedPixelConfig *p, uint16_t *grid)
{
    unsigned gx, gy, x, y;

    for (gy = 0; gy < p->grid_height; gy++) {
        unsigned first_y = gy * p->height / p->grid_height;
        unsigned last_y = (gy + 1) * p->height / p->grid_height;

        for (gx = 0; gx < p->grid_width; gx++) {
            unsigned first_x = gx * p->width / p->grid_width;
            unsigned last_x = (gx + 1) * p->width / p->grid_width;
            unsigned count = (last_x - first_x) * (last_y - first_y);
            uint64_t sum = 0;

            for (y = first_y; y < last_y; y++) {
                for (x = first_x; x < last_x; x++) {
                    sum += filtered[y * p->width + x];
                }
            }
            grid[gy * p->grid_width + gx] = (sum + count / 2) / count;
        }
    }
}

static void paired_matrix(const int16_t matrix[9], const uint16_t input[3],
                           uint16_t output[3])
{
    unsigned row, column;

    for (row = 0; row < 3; row++) {
        int32_t sum = 0;

        for (column = 0; column < 3; column++) {
            sum += matrix[3 * row + column] * input[column];
        }
        output[row] = paired_clamp(paired_round(sum, 10));
    }
}

static void paired_yuv(const NikonPairedPixelConfig *p, uint16_t mono,
                        uint16_t output[3])
{
    uint16_t rgb[3];
    uint16_t after_a[3], after_c[3];
    unsigned row, column;

    /* Two source-published curves: Q10 gain knots at sample steps of16. */
    for (unsigned table = 0; table < 2; table++) {
        if (p->curve_enabled[table]) {
            unsigned index = mono >> 4, fraction = mono & 15;
            unsigned gain = (p->curve[table][index] * (16 - fraction) +
                             p->curve[table][index + 1] * fraction + 8) / 16;

            mono = paired_clamp(paired_round(mono * gain, 10));
        }
    }
    rgb[0] = rgb[1] = rgb[2] = mono;
    for (column = 0; column < 3; column++) {
        if (p->gain_enabled[column]) {
            rgb[column] = paired_clamp(paired_round(mono * p->gain[column], 8));
        }
    }
    paired_matrix(p->a, rgb, after_a);
    paired_matrix(p->c, after_a, after_c);
    for (row = 0; row < 3; row++) {
        int32_t sum = 0;

        for (column = 0; column < 3; column++) {
            sum += p->b[3 * row + column] * after_c[column];
        }
        output[row] = paired_clamp(paired_round(sum, 9) + (row ? 2048 : 0));
    }
}

static uint8_t paired_byte(unsigned value)
{
    return MIN((value + 8) / 16, 255U);
}

static void paired_main(const uint16_t *filtered,
                         const NikonPairedPixelConfig *p,
                         uint8_t *out_y, uint8_t *out_uv)
{
    unsigned x, y, dx, dy;

    for (y = 0; y < p->output_height; y += 2) {
        for (x = 0; x < p->output_width; x += 2) {
            unsigned u = 0, v = 0;

            for (dy = 0; dy < 2; dy++) {
                for (dx = 0; dx < 2; dx++) {
                    unsigned source = (p->y + y + dy) * p->width +
                                      p->x + x + dx;
                    uint16_t yuv[3];

                    paired_yuv(p, filtered[source], yuv);
                    out_y[(y + dy) * p->output_width + x + dx] =
                        paired_byte(yuv[0]);
                    u += yuv[1];
                    v += yuv[2];
                }
            }
            out_uv[(y / 2) * p->output_width + x] = paired_byte((u + 2) / 4);
            out_uv[(y / 2) * p->output_width + x + 1] =
                paired_byte((v + 2) / 4);
        }
    }
}

bool nikon_paired_pixels(const NikonPairedPixelConfig *config,
                         const uint16_t *raw_contiguous_ROI,
                         const uint16_t *prior_half_luma_nullable,
                         const uint16_t *prior_half_residual_nullable,
                         uint16_t *out_half_luma,
                         uint16_t *out_half_residual,
                         uint16_t *out_grid,
                         uint8_t *out_y, uint8_t *out_uv)
{
    g_autofree uint16_t *dpc = NULL;
    g_autofree uint16_t *filtered = NULL;
    size_t pixels, half_pixels;
    unsigned x, y;

    if (!paired_config_valid(config) || !raw_contiguous_ROI ||
        !out_half_luma || !out_half_residual || !out_grid || !out_y ||
        !out_uv) {
        return false;
    }
    pixels = (size_t)config->width * config->height;
    half_pixels = (size_t)((config->width + 1) / 2) *
                  ((config->height + 1) / 2);
    if (!paired_samples_valid(raw_contiguous_ROI, pixels) ||
        !paired_samples_valid(prior_half_luma_nullable, half_pixels) ||
        !paired_samples_valid(prior_half_residual_nullable, half_pixels)) {
        return false;
    }
    dpc = g_try_new(uint16_t, pixels);
    filtered = g_try_new(uint16_t, pixels);
    if (!dpc || !filtered) {
        return false;
    }
    for (y = 0; y < config->height; y++) {
        for (x = 0; x < config->width; x++) {
            dpc[y * config->width + x] = paired_dpc(raw_contiguous_ROI,
                config->width, config->height, x, y);
        }
    }
    paired_spatial(dpc, filtered, config->width, config->height);
    paired_temporal(filtered, config->width, config->height,
                    prior_half_luma_nullable, prior_half_residual_nullable,
                    out_half_luma, out_half_residual);
    paired_grid(filtered, config, out_grid);
    paired_main(filtered, config, out_y, out_uv);
    return true;
}
