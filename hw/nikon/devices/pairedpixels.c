/*
 * Finite mono/CFA RAW12 operator. Filter thresholds, temporal weights,
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
    int32_t half, scale;

    if (!shift) {
        return value;
    }
    half = 1U << (shift - 1);
    scale = 1U << shift;

    /* Symmetric nearest rounding, with half-way values away from zero. */
    return value < 0 ? -((-value + half) / scale) : (value + half) / scale;
}

static uint32_t paired_multiply(uint32_t a, uint32_t b, uint32_t polynomial)
{
    uint32_t result = 0;

    while (b) {
        uint32_t carry = a >> 31;

        if (b & 1) {
            result ^= a;
        }
        a <<= 1;
        if (carry) {
            a ^= polynomial;
        }
        b >>= 1;
    }
    return result;
}

static uint32_t paired_power(uint32_t polynomial, uint32_t exponent)
{
    uint32_t result = 1, x = 2;

    while (exponent) {
        if (exponent & 1) {
            result = paired_multiply(result, x, polynomial);
        }
        x = paired_multiply(x, x, polynomial);
        exponent >>= 1;
    }
    return result;
}

static bool paired_primitive(uint32_t feedback)
{
    static const uint32_t factors[] = { 3, 5, 17, 257, 65537 };
    uint32_t polynomial = 0;

    if (!(feedback & (1U << 31))) {
        return false;
    }
    /* Reverse the Galois taps to the low32 terms of x^32 + p(x). */
    for (unsigned bit = 0; bit < 32; bit++) {
        polynomial |= ((feedback >> bit) & 1U) << (31 - bit);
    }
    if (paired_power(polynomial, UINT32_MAX) != 1) {
        return false;
    }
    for (unsigned i = 0; i < ARRAY_SIZE(factors); i++) {
        if (paired_power(polynomial, UINT32_MAX / factors[i]) == 1) {
            return false;
        }
    }
    return true;
}

bool nikon_paired_noise_valid(const NikonPairedPixelConfig *p)
{
    if (!p || p->noise_negative > 1023 || p->noise_positive > 1023) {
        return false;
    }
    for (unsigned i = 0; i < 3; i++) {
        const NikonPairedNoise *n = &p->noise[i];

        if (n->enabled > 1 || n->shift > 15 ||
            (n->enabled && !n->feedback) ||
            (n->feedback && !paired_primitive(n->feedback))) {
            return false;
        }
    }
    return true;
}

bool nikon_paired_filter_valid(const NikonPairedFilter *f)
{
    if (!f) {
        return false;
    }
    if (!f->enabled) {
        return !f->refine && !f->clipped && !f->retain && !f->base &&
               !f->radius && !f->slope;
    }
    return f->retain <= 16383 && f->base <= 16383 && f->radius <= 15 &&
           f->slope <= 16383;
}

bool nikon_paired_denoise_valid(const NikonPairedDenoise *n)
{
    return n && n->threshold >= 7 && n->threshold <= 103 &&
           n->low >= 1 && n->low <= 43 && n->high == n->low + 2 &&
           n->high <= n->threshold && n->axial >= 3 && n->axial <= 17 &&
           n->diagonal >= 1 && n->diagonal <= 10 &&
           n->axial >= n->diagonal && n->bias <= 12 &&
           n->retain >= 8 && n->retain <= 31;
}

bool nikon_paired_detail_valid(const NikonPairedDetail *d)
{
    return d && d->limit >= 4 && d->limit <= 31 && d->strength <= 32 &&
           d->lane >= 8 && d->lane <= 16;
}

bool nikon_paired_adaptive_valid(const NikonPairedAdaptive *a)
{
    return a && a->base >= 50 && a->base <= 300 &&
           a->slope >= 60 && a->slope <= 4095 &&
           a->cap >= 16 && a->cap <= 4095 &&
           a->weight >= 100 && a->weight <= 1024 &&
           a->precision >= 8 && a->precision <= 10 && a->dose <= 15;
}

bool nikon_paired_radial_valid(const NikonPairedRadial *r)
{
    return r && r->center_x >= -4095 && r->center_x <= 65535 &&
           r->center_y >= -4095 && r->center_y <= 65535 &&
           r->red >= -128 && r->red <= 127 &&
           r->blue >= -128 && r->blue <= 127;
}

bool nikon_paired_colorfilter_valid(const NikonPairedColorFilter *f)
{
    /* Convex interpolation bounds, not a table of accepted firmware packets. */
    static const uint16_t maximum[2][10] = {
        { 400, 300, 100, 500, 120, 300, 80, 800, 120, 300 },
        { 480, 600, 480, 800, 540, 500, 500, 650, 320, 620 },
    };
    static const uint8_t shift_max[2][2][4] = {
        { { 4, 3, 4, 4 }, { 4, 2, 2, 2 } },
        { { 4, 2, 4, 4 }, { 4, 2, 4, 4 } },
    };

    if (!f || f->precision > 1 || f->negative > 130 || f->positive > 200 ||
        f->strength > 31 || f->shift < 2 || f->shift > 4 ||
        f->weight < 8 || f->weight > 16) {
        return false;
    }
    for (unsigned i = 0; i < 2; i++) {
        const NikonPairedColorBlock *b = &f->block[i];

        if (b->base > maximum[i][0] || b->slope > maximum[i][1] ||
            b->negative_limit > b->positive_limit ||
            b->negative_limit > (i ? 100 : 4095) ||
            b->positive_limit > (i ? 150 : 4095) ||
            (i && (b->negative_limit < 5 || b->positive_limit < 9)) ||
            b->strength[0] > 31 || b->strength[1] > 31 ||
            b->strength[2] != b->strength[1]) {
            return false;
        }
        for (unsigned j = 0; j < 2; j++) {
            const NikonPairedColorBranch *n = &b->branch[j];
            unsigned k = 2 + 4 * j;

            if (n->negative_base > maximum[i][k] ||
                n->negative_slope > maximum[i][k + 1] ||
                n->positive_base > maximum[i][k + 2] ||
                n->positive_slope > maximum[i][k + 3] ||
                (j && n->positive_base > n->positive_slope) ||
                (n->radius != 0 && n->radius != 2) ||
                n->radius != (i && j ? 0 : f->block[0].branch[0].radius)) {
                return false;
            }
            for (unsigned d = 0; d < 4; d++) {
                unsigned minimum = i && !j && d == 2 ? 3 : 2;

                if (n->shift[d] < minimum || n->shift[d] > shift_max[i][j][d]) {
                    return false;
                }
            }
        }
    }
    return true;
}

bool nikon_paired_post_valid(const NikonPairedPost *p)
{
    return p && p->unsharp[0] <= 256 && p->unsharp[1] <= 256 &&
           p->original <= 256 && p->transformed <= 256 &&
           p->original + p->transformed == 256 &&
           p->smooth[0] <= 31 && p->smooth[1] <= 31 &&
           p->edge_strength >= 3 && p->edge_strength <= 18 &&
           p->edge_limit == 5;
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
        (unsigned)p->encoding > NIKON_RAW_BGGR ||
        (p->encoding != NIKON_RAW_MONO && ((p->width | p->height) & 1)) ||
        p->width > PAIRED_MAX_DIMENSION || p->height > PAIRED_MAX_DIMENSION ||
        !p->output_width || !p->output_height ||
        (p->output_width & 1) || (p->output_height & 1) ||
        p->x >= p->width || p->y >= p->height ||
        p->output_width > p->width - p->x ||
        p->output_height > p->height - p->y ||
        !p->grid_width || !p->grid_height ||
        p->grid_width > p->width || p->grid_height > p->height ||
        !nikon_paired_noise_valid(p) ||
        !nikon_paired_filter_valid(&p->filter) ||
        !nikon_paired_denoise_valid(&p->denoise) ||
        !nikon_paired_detail_valid(&p->detail) ||
        !nikon_paired_adaptive_valid(&p->adaptive) ||
        !nikon_paired_radial_valid(&p->radial) ||
        !nikon_paired_colorfilter_valid(&p->colorfilter) ||
        !nikon_paired_post_valid(&p->post)) {
        return false;
    }
    if (p->c[0] != 1024 || p->c[1] || p->c[2] || p->c[3] || p->c[6]) {
        return false;
    }
    for (i = 0; i < 9; i++) {
        if (p->a[i] < -4096 || p->a[i] > 4095 ||
            p->c[i] < -4096 || p->c[i] > 4095 ||
            p->b[i] < -2048 || p->b[i] > 2047) {
            return false;
        }
    }
    if (p->quad_limit > 32767) {
        return false;
    }
    for (i = 0; i < 4; i++) {
        if (p->quad_gain[i] > 16383 || p->quad_offset[i] < -4096 ||
            p->quad_offset[i] > 4095) {
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

static void paired_filter_pass(const uint16_t *input, uint16_t *output,
                                unsigned width, unsigned height,
                                const NikonPairedFilter *f, unsigned axis)
{
    for (unsigned y = 0; y < height; y++) {
        unsigned up = y >= f->radius ? y - f->radius : 0;
        unsigned down = MIN(y + f->radius, height - 1);

        for (unsigned x = 0; x < width; x++) {
            unsigned left = x >= f->radius ? x - f->radius : 0;
            unsigned right = MIN(x + f->radius, width - 1);
            unsigned at = y * width + x;
            int32_t center = input[at] * 4;
            int32_t sum = 0, count = 0, residual, correction, threshold;

            if (axis != 2) {
                sum += 4 * (input[y * width + left] +
                            input[y * width + right]);
                count += 2;
            }
            if (axis != 1) {
                sum += 4 * (input[up * width + x] + input[down * width + x]);
                count += 2;
            }
            residual = sum / count - center;
            if (f->clipped) {
                threshold = MIN(16383, f->base +
                    (f->slope * center + 8191) / 16383);
                residual = CLAMP(residual, -threshold, threshold);
            }
            /* The largest product is 16380*16383, within signed32. */
            correction = residual * (16383 - f->retain);
            correction = correction < 0 ? -((-correction + 8191) / 16383) :
                                           (correction + 8191) / 16383;
            output[at] = paired_clamp((CLAMP(center + correction, 0, 16383) +
                                       2) / 4);
        }
    }
}

static bool paired_filter(uint16_t *filtered, unsigned width, unsigned height,
                           const NikonPairedFilter *f)
{
    g_autofree uint16_t *scratch = NULL;
    size_t count = (size_t)width * height;

    if (!f->enabled || !f->radius || f->retain == 16383) {
        return true;
    }
    scratch = g_try_new(uint16_t, count);
    if (!scratch) {
        return false;
    }
    paired_filter_pass(filtered, scratch, width, height, f, f->refine ? 1 : 0);
    if (f->refine) {
        paired_filter_pass(scratch, filtered, width, height, f, 2);
    } else {
        memcpy(filtered, scratch, count * sizeof(*filtered));
    }
    return true;
}

static void paired_denoise(const uint16_t *input, uint16_t *output,
                           unsigned width, unsigned height,
                           const NikonPairedDenoise *n)
{
    int low = n->threshold + n->low + n->bias;
    int high = n->threshold + n->high + n->bias;
    int scale = high - low;
    int divisor = 4 * scale * (n->retain + n->axial + n->diagonal);

    for (unsigned y = 0; y < height; y++) {
        unsigned ys[] = { y ? y - 1 : y, y, MIN(y + 1, height - 1) };

        for (unsigned x = 0; x < width; x++) {
            unsigned xs[] = { x ? x - 1 : x, x, MIN(x + 1, width - 1) };
            unsigned at = y * width + x;
            int center = input[at] * 4, sum = 0, correction;

            for (unsigned row = 0; row < 3; row++) {
                for (unsigned column = 0; column < 3; column++) {
                    int difference, gate, weight;

                    if (row == 1 && column == 1) {
                        continue;
                    }
                    difference = 4 * (int)input[ys[row] * width + xs[column]] -
                                 center;
                    gate = CLAMP(high - abs(difference), 0, scale);
                    weight = row == 1 || column == 1 ? n->axial : n->diagonal;
                    sum += weight * gate * difference;
                }
            }
            /* Keep the two neighborhood means exact until the final divide. */
            correction = sum < 0 ? -((-sum + divisor / 2) / divisor) :
                                    (sum + divisor / 2) / divisor;
            output[at] = paired_clamp((CLAMP(center + correction, 0, 16383) +
                                       2) / 4);
        }
    }
}

static void paired_detail(const uint16_t *input, uint16_t *output,
                          unsigned width, unsigned height,
                          const NikonPairedDetail *d)
{
    for (unsigned y = 0; y < height; y++) {
        unsigned ys[] = { y ? y - 1 : y, y, MIN(y + 1, height - 1) };

        for (unsigned x = 0; x < width; x++) {
            unsigned xs[] = { x ? x - 1 : x, x, MIN(x + 1, width - 1) };
            unsigned at = y * width + x;
            uint16_t neighborhood[9];
            int center = input[at] * 4, residual, correction;

            for (unsigned i = 0; i < 9; i++) {
                unsigned j = i;
                uint16_t value = input[ys[i / 3] * width + xs[i % 3]];

                while (j && neighborhood[j - 1] > value) {
                    neighborhood[j] = neighborhood[j - 1];
                    j--;
                }
                neighborhood[j] = value;
            }
            residual = CLAMP(neighborhood[4] * 4 - center,
                             -(int)d->limit, d->limit);
            correction = paired_round(residual * d->strength * d->lane, 10);
            output[at] = paired_clamp((CLAMP(center + correction, 0, 16383) +
                                       2) / 4);
        }
    }
}

static void paired_adaptive(const uint16_t *input, uint16_t *output,
                            unsigned width, unsigned height,
                            const NikonPairedAdaptive *a)
{
    int64_t weight = (int64_t)a->weight * a->dose;
    int64_t denominator = 4 * ((16 << a->precision) + weight);

    for (unsigned y = 0; y < height; y++) {
        unsigned up = y ? y - 1 : y;
        unsigned down = MIN(y + 1, height - 1);

        for (unsigned x = 0; x < width; x++) {
            unsigned left = x ? x - 1 : x;
            unsigned right = MIN(x + 1, width - 1);
            int64_t center = input[y * width + x];
            int64_t tolerance = MIN(a->cap, a->base +
                ((int64_t)a->slope * center + 2047) / 4095);
            int64_t residual = input[up * width + x] +
                input[down * width + x] + input[y * width + left] +
                input[y * width + right] - 4 * center;
            int64_t numerator = CLAMP(residual, -4 * tolerance,
                                      4 * tolerance) * weight;
            int64_t correction = numerator < 0 ?
                -((-numerator + denominator / 2) / denominator) :
                (numerator + denominator / 2) / denominator;

            output[y * width + x] = paired_clamp(center + correction);
        }
    }
}

static int64_t paired_divide(int64_t value, unsigned divisor)
{
    return value < 0 ? -((-value + divisor / 2) / divisor) :
                        (value + divisor / 2) / divisor;
}

static void paired_color_block(const uint16_t *input, uint16_t *output,
                               unsigned width, unsigned height,
                               const NikonPairedColorBlock *b, unsigned lane)
{
    static const int dx[2][4] = { { -1, 1, 0, 0 }, { -1, 1, -1, 1 } };
    static const int dy[2][4] = { { 0, 0, -1, 1 }, { -1, -1, 1, 1 } };
    unsigned strength = lane == 4 ?
        (b->strength[0] + b->strength[1] + b->strength[2] + 1) / 3 :
        b->strength[lane == 3 ? 2 : lane != 0];

    for (unsigned y = 0; y < height; y++) {
        for (unsigned x = 0; x < width; x++) {
            unsigned at = y * width + x;
            int center = 4 * input[at];
            int common = b->base + paired_divide((int64_t)b->slope * center,
                                                16383);
            int mean[2];

            for (unsigned branch = 0; branch < 2; branch++) {
                const NikonPairedColorBranch *n = &b->branch[branch];
                unsigned radius = 1 + n->radius, weights = 0;
                int64_t sum = 0;
                int negative = common + n->negative_base +
                    paired_divide((int64_t)n->negative_slope * center, 16383);
                int positive = common + n->positive_base +
                    paired_divide((int64_t)n->positive_slope * center, 16383);

                for (unsigned d = 0; d < 4; d++) {
                    unsigned nx = CLAMP((int)x + dx[branch][d] * (int)radius,
                                        0, (int)width - 1);
                    unsigned ny = CLAMP((int)y + dy[branch][d] * (int)radius,
                                        0, (int)height - 1);
                    int difference = 4 * input[ny * width + nx] - center;
                    int threshold = difference < 0 ? negative : positive;
                    int gate = MAX(0, threshold - abs(difference));
                    unsigned weight = 1U << n->shift[d];

                    if (threshold) {
                        sum += weight * paired_divide(
                            (int64_t)difference * gate, threshold);
                    }
                    weights += weight;
                }
                mean[branch] = paired_divide(sum, weights);
            }
            int correction = paired_divide(
                paired_divide(mean[0] + mean[1], 2) * strength, 31);

            correction = CLAMP(correction, -(int)b->negative_limit,
                                b->positive_limit);
            output[at] = paired_clamp((CLAMP(center + correction, 0, 16383) +
                                       2) / 4);
        }
    }
}

static void paired_color_tail(const uint16_t *input, uint16_t *output,
                              unsigned width, unsigned height,
                              const NikonPairedColorFilter *f)
{
    unsigned divisor = 31 * 4 * (1U << f->shift) * (1U << f->precision);

    for (unsigned y = 0; y < height; y++) {
        for (unsigned x = 0; x < width; x++) {
            uint16_t neighbors[9];
            int center = 4 * input[y * width + x];

            for (unsigned i = 0; i < 9; i++) {
                unsigned nx = CLAMP((int)x + (int)(i % 3) - 1,
                                    0, (int)width - 1);
                unsigned ny = CLAMP((int)y + (int)(i / 3) - 1,
                                    0, (int)height - 1);
                unsigned j = i;
                uint16_t sample = input[ny * width + nx];

                while (j && neighbors[j - 1] > sample) {
                    neighbors[j] = neighbors[j - 1];
                    j--;
                }
                neighbors[j] = sample;
            }
            int residual = CLAMP(4 * neighbors[4] - center,
                                 -(int)f->negative, f->positive);
            int correction = paired_divide(
                (int64_t)residual * f->strength * f->weight, divisor);

            output[y * width + x] = paired_clamp(
                (CLAMP(center + correction, 0, 16383) + 2) / 4);
        }
    }
}

/* Each completed pass is immutable input to the next; no lane mixing. */
static void paired_colorfilter(uint16_t *image, uint16_t *scratch,
                               unsigned width, unsigned height,
                               const NikonPairedColorFilter *f, unsigned lane)
{
    paired_color_block(image, scratch, width, height, &f->block[0], lane);
    paired_color_block(scratch, image, width, height, &f->block[1], lane);
    paired_color_tail(image, scratch, width, height, f);
    memcpy(image, scratch, (size_t)width * height * sizeof(*image));
}

static void paired_temporal(uint16_t *filtered, unsigned width, unsigned height,
                             const uint16_t *prior_luma,
                             const uint16_t *prior_residual,
                             uint16_t *out_luma, uint16_t *out_residual,
                             bool cfa)
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
                        if (cfa) {
                            int delta = paired_round(
                                ((int)prior_luma[index] - (int)current) *
                                (int)old_weight, 8);

                            value = paired_clamp((int)value + delta);
                        } else {
                            value = (value * (256 - old_weight) +
                                     prior_luma[index] * old_weight + 128) / 256;
                        }
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

static bool paired_cfa_filter(const uint16_t *raw, uint16_t *output,
                              const NikonPairedPixelConfig *p)
{
    unsigned width = p->width / 2, height = p->height / 2;
    size_t count = (size_t)width * height;
    g_autofree uint16_t *plane = g_try_new(uint16_t, count);
    g_autofree uint16_t *dpc = g_try_new(uint16_t, count);
    g_autofree uint16_t *filtered = g_try_new(uint16_t, count);

    if (!plane || !dpc || !filtered) {
        return false;
    }
    for (unsigned phase = 0; phase < 4; phase++) {
        unsigned px = phase & 1, py = phase >> 1;

        for (unsigned y = 0; y < height; y++) {
            for (unsigned x = 0; x < width; x++) {
                plane[y * width + x] =
                    raw[(2 * y + py) * p->width + 2 * x + px];
            }
        }
        for (unsigned y = 0; y < height; y++) {
            for (unsigned x = 0; x < width; x++) {
                dpc[y * width + x] = paired_dpc(plane, width, height, x, y);
            }
        }
        paired_spatial(dpc, filtered, width, height);
        if (!paired_filter(filtered, width, height, &p->filter)) {
            return false;
        }
        paired_denoise(filtered, dpc, width, height, &p->denoise);
        paired_detail(dpc, filtered, width, height, &p->detail);
        paired_adaptive(filtered, dpc, width, height, &p->adaptive);
        paired_colorfilter(dpc, filtered, width, height, &p->colorfilter,
                            phase ^ (p->encoding - 1));
        for (unsigned y = 0; y < height; y++) {
            for (unsigned x = 0; x < width; x++) {
                output[(2 * y + py) * p->width + 2 * x + px] =
                    dpc[y * width + x];
            }
        }
    }
    return true;
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

static uint16_t paired_quad(const NikonPairedPixelConfig *p, uint16_t sample,
                            unsigned lane)
{
    int32_t value;

    if (!p->quad_enabled) {
        return sample;
    }
    value = paired_round(((int32_t)sample + p->quad_offset[lane]) *
                         p->quad_gain[lane], 9);
    return CLAMP(value + p->quad_pedestal[lane], 0,
                 MIN(p->quad_limit, PAIRED_MAX_SAMPLE));
}

static void paired_rgb_yuv(const NikonPairedPixelConfig *p, uint16_t rgb[3],
                            uint32_t noise_state[3], uint16_t output[3])
{
    uint16_t after_a[3];
    int32_t yuv[3];
    unsigned row, column;

    for (column = 0; column < 3; column++) {
        const NikonPairedNoise *n = &p->noise[column];

        /* Two source-published curves: Q10 gain knots at sample steps of16. */
        for (unsigned table = 0; table < 2; table++) {
            if (p->curve_enabled[table]) {
                unsigned sample = rgb[column];
                unsigned index = sample >> 4, fraction = sample & 15;
                unsigned gain =
                    (p->curve[table][index] * (16 - fraction) +
                     p->curve[table][index + 1] * fraction + 8) / 16;

                rgb[column] = paired_clamp(paired_round(sample * gain, 10));
            }
        }
        if (p->gain_enabled[column]) {
            rgb[column] = paired_clamp(paired_round(rgb[column] *
                                                   p->gain[column], 8));
        }
        if (n->enabled) {
            uint32_t *state = &noise_state[column];
            int32_t noise;

            *state = (*state >> 1) ^ ((0U - (*state & 1U)) & n->feedback);
            noise = (int32_t)(*state >> 21) - 1024;
            noise = CLAMP(noise, -(int32_t)p->noise_negative,
                          (int32_t)p->noise_positive);
            noise = paired_round(noise * n->gain, 10);
            noise = paired_round(noise + n->offset, n->shift);
            rgb[column] = paired_clamp((int32_t)rgb[column] + noise);
        }
    }
    paired_matrix(p->a, rgb, after_a);
    for (row = 0; row < 3; row++) {
        int32_t sum = 0;

        for (column = 0; column < 3; column++) {
            sum += p->b[3 * row + column] * after_a[column];
        }
        yuv[row] = CLAMP(paired_round(sum, 9), row ? -2048 : 0,
                        row ? 2047 : 4095);
    }
    output[0] = yuv[0];
    unsigned index = yuv[0] >> 7, fraction = yuv[0] & 127;
    unsigned gain = (p->saturation[index] * (128 - fraction) +
                     p->saturation[index + 1] * fraction + 64) / 128;

    for (row = 1; row < 3; row++) {
        int32_t transformed = paired_round(p->c[3 * row + 1] * yuv[1] +
                                           p->c[3 * row + 2] * yuv[2], 10);
        int32_t mixed = paired_round(p->post.original * yuv[row] +
                                     p->post.transformed * transformed, 8);

        /* One source Q8 curve scales signed chroma before bias/clipping. */
        mixed = paired_divide((int64_t)mixed * gain, 256);
        output[row] = paired_clamp(mixed + 2048);
    }
}

/* Coordinates are full-RAW pixel Q16 values. CFA interpolation stays on the
 * requested half-resolution color lattice. Clamping precedes unsigned shifts;
 * at most 4095 * 2^34 contributes to the sum, within unsigned64.
 */
static uint16_t paired_lattice_sample(const uint16_t *raw,
                                      const NikonPairedPixelConfig *p,
                                      unsigned lane, int64_t x, int64_t y)
{
    unsigned phase = p->encoding == NIKON_RAW_MONO ? 0 :
                     lane ^ (p->encoding - 1);
    unsigned px = phase & 1, py = phase >> 1;
    unsigned step = p->encoding == NIKON_RAW_MONO ? 1 : 2;
    unsigned bits = step == 1 ? 16 : 17;
    uint64_t unit = UINT64_C(1) << bits;
    uint64_t xx = CLAMP(x - ((int64_t)px << 16), 0,
                        (int64_t)(p->width - step) << 16);
    uint64_t yy = CLAMP(y - ((int64_t)py << 16), 0,
                        (int64_t)(p->height - step) << 16);
    unsigned x0 = px + step * (xx >> bits), y0 = py + step * (yy >> bits);
    unsigned x1 = MIN(x0 + step, p->width - step + px);
    unsigned y1 = MIN(y0 + step, p->height - step + py);
    uint64_t fx = xx & (unit - 1), fy = yy & (unit - 1);
    uint64_t sum = raw[y0 * p->width + x0] * (unit - fx) * (unit - fy) +
                   raw[y0 * p->width + x1] * fx * (unit - fy) +
                   raw[y1 * p->width + x0] * (unit - fx) * fy +
                   raw[y1 * p->width + x1] * fx * fy;

    return (sum + (UINT64_C(1) << (2 * bits - 1))) >> (2 * bits);
}

static uint16_t paired_radial_sample(const uint16_t *raw,
                                     const NikonPairedPixelConfig *p,
                                     unsigned lane, unsigned x, unsigned y)
{
    int coefficient = lane == 0 ? p->radial.red :
                      lane == 3 ? p->radial.blue : 0;
    int64_t sx = ((int64_t)x << 16) +
                 ((int64_t)x - p->radial.center_x) * coefficient;
    int64_t sy = ((int64_t)y << 16) +
                 ((int64_t)y - p->radial.center_y) * coefficient;

    return paired_lattice_sample(raw, p, lane, sx, sy);
}

static void paired_yuv(const NikonPairedPixelConfig *p, const uint16_t *raw,
                        unsigned x, unsigned y,
                        uint32_t noise_state[3], uint16_t output[3])
{
    uint16_t mono = raw[y * p->width + x];
    uint16_t rgb[] = {
        paired_quad(p, paired_radial_sample(raw, p, 0, x, y), 0),
        (paired_quad(p, mono, 1) + paired_quad(p, mono, 2) + 1) / 2,
        paired_quad(p, paired_radial_sample(raw, p, 3, x, y), 3),
    };

    paired_rgb_yuv(p, rgb, noise_state, output);
}

/* Interpolate one lane on its half-resolution lattice. Edge samples are
 * replicated on that lattice, never substituted from another color phase.
 */
static uint16_t paired_cfa_sample(const uint16_t *raw,
                                 const NikonPairedPixelConfig *p,
                                 unsigned lane, unsigned x, unsigned y)
{
    return paired_lattice_sample(raw, p, lane, (int64_t)x << 16,
                                 (int64_t)y << 16);
}

static void paired_cfa_yuv(const uint16_t *raw, const NikonPairedPixelConfig *p,
                            unsigned x, unsigned y, uint32_t noise_state[3],
                            uint16_t output[3])
{
    unsigned lane = (p->encoding - 1) ^ ((y & 1) * 2 + (x & 1));
    uint16_t rgb[] = {
        paired_cfa_sample(raw, p, 0, x, y),
        lane == 1 || lane == 2 ? raw[y * p->width + x] :
            (paired_cfa_sample(raw, p, 1, x, y) +
             paired_cfa_sample(raw, p, 2, x, y) + 1) / 2,
        paired_cfa_sample(raw, p, 3, x, y),
    };

    paired_rgb_yuv(p, rgb, noise_state, output);
}

static uint8_t paired_byte(unsigned value)
{
    return MIN((value + 8) / 16, 255U);
}

static int32_t paired_ratio(int64_t numerator, int64_t denominator)
{
    return numerator < 0 ? -((-numerator + denominator / 2) / denominator) :
                           (numerator + denominator / 2) / denominator;
}

static void paired_post_pass(const uint16_t *input, uint16_t *output,
                              unsigned width, unsigned height,
                              const NikonPairedPost *p, unsigned channel,
                              unsigned pass)
{
    for (unsigned y = 0; y < height; y++) {
        unsigned up = y ? y - 1 : y;
        unsigned down = MIN(y + 1, height - 1);

        for (unsigned x = 0; x < width; x++) {
            unsigned left = x ? x - 1 : x;
            unsigned right = MIN(x + 1, width - 1);
            int center = input[y * width + x];
            int n;
            int32_t correction;

            if (pass == 2) {
                n = input[up * width + left] + input[up * width + right] +
                    input[down * width + left] + input[down * width + right];
            } else {
                n = input[y * width + left] + input[y * width + right] +
                    input[up * width + x] + input[down * width + x];
            }
            n -= 4 * center;
            if (pass == 0) {
                correction = paired_ratio(-(int64_t)n *
                                           p->unsharp[!!channel], 4 * 256);
            } else if (pass < 3) {
                correction = paired_ratio((int64_t)n * p->smooth[pass - 1],
                                           4 * 32);
            } else {
                int threshold = 16 * p->edge_limit;
                int gate = MAX(4 * threshold - abs(n), 0);

                correction = paired_ratio((int64_t)n * gate *
                                           p->edge_strength,
                                           16 * threshold * 32);
            }
            output[y * width + x] = paired_clamp(center + correction);
        }
    }
}

static void paired_main(const uint16_t *filtered,
                         const NikonPairedPixelConfig *p,
                         uint16_t *planes, uint16_t *scratch,
                         uint8_t *out_y, uint8_t *out_uv)
{
    unsigned x, y, dx, dy;
    size_t pixels = (size_t)p->output_width * p->output_height;
    uint32_t noise_state[] = { 1, 1, 1 };

    /* Preserve the original 2x2 noise-state traversal while materializing only
     * actual computed YUV. No RGB side buffer supplies any sample.
     */
    for (y = 0; y < p->output_height; y += 2) {
        for (x = 0; x < p->output_width; x += 2) {
            for (dy = 0; dy < 2; dy++) {
                for (dx = 0; dx < 2; dx++) {
                    uint16_t yuv[3];
                    size_t at = (size_t)(y + dy) * p->output_width + x + dx;

                    if (p->encoding == NIKON_RAW_MONO) {
                        paired_yuv(p, filtered, p->x + x + dx,
                                   p->y + y + dy, noise_state, yuv);
                    } else {
                        paired_cfa_yuv(filtered, p, p->x + x + dx,
                                       p->y + y + dy, noise_state, yuv);
                    }
                    for (unsigned channel = 0; channel < 3; channel++) {
                        planes[channel * pixels + at] = yuv[channel];
                    }
                }
            }
        }
    }
    /* The RAW plane is no longer read: reuse its existing allocation as the
     * immutable-pass scratch, at least as large as the cropped MAIN plane.
     */
    for (unsigned channel = 0; channel < 3; channel++) {
        uint16_t *plane = planes + channel * pixels;

        paired_post_pass(plane, scratch, p->output_width, p->output_height,
                          &p->post, channel, 0);
        paired_post_pass(scratch, plane, p->output_width, p->output_height,
                          &p->post, channel, 1);
        paired_post_pass(plane, scratch, p->output_width, p->output_height,
                          &p->post, channel, 2);
        paired_post_pass(scratch, plane, p->output_width, p->output_height,
                          &p->post, channel, 3);
    }
    for (y = 0; y < p->output_height; y += 2) {
        for (x = 0; x < p->output_width; x += 2) {
            unsigned u = 0, v = 0;

            for (dy = 0; dy < 2; dy++) {
                for (dx = 0; dx < 2; dx++) {
                    size_t at = (size_t)(y + dy) * p->output_width + x + dx;

                    unsigned luma = ((uint64_t)planes[at] * p->main_gain +
                                     128) / 256;

                    out_y[at] = paired_byte(MIN(luma, 4095));
                    u += planes[pixels + at];
                    v += planes[2 * pixels + at];
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
    g_autofree uint16_t *planes = NULL;
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
    planes = g_try_new(uint16_t, (size_t)3 * config->output_width *
                                config->output_height);
    if (!dpc || !filtered || !planes) {
        return false;
    }
    if (config->encoding == NIKON_RAW_MONO) {
        for (y = 0; y < config->height; y++) {
            for (x = 0; x < config->width; x++) {
                dpc[y * config->width + x] = paired_dpc(raw_contiguous_ROI,
                    config->width, config->height, x, y);
            }
        }
        paired_spatial(dpc, filtered, config->width, config->height);
        if (!paired_filter(filtered, config->width, config->height,
                            &config->filter)) {
            return false;
        }
        paired_denoise(filtered, dpc, config->width, config->height,
                        &config->denoise);
        paired_detail(dpc, filtered, config->width, config->height,
                       &config->detail);
        paired_adaptive(filtered, dpc, config->width, config->height,
                         &config->adaptive);
        paired_colorfilter(dpc, filtered, config->width, config->height,
                            &config->colorfilter, 4);
    } else if (!paired_cfa_filter(raw_contiguous_ROI, dpc, config)) {
        return false;
    }
    paired_temporal(dpc, config->width, config->height,
                    prior_half_luma_nullable, prior_half_residual_nullable,
                    out_half_luma, out_half_residual,
                    config->encoding != NIKON_RAW_MONO);
    paired_grid(dpc, config, out_grid);
    if (config->encoding != NIKON_RAW_MONO) {
        for (y = 0; y < config->height; y++) {
            for (x = 0; x < config->width; x++) {
                unsigned lane = (config->encoding - 1) ^
                                ((y & 1) * 2 + (x & 1));
                unsigned at = y * config->width + x;

                filtered[at] = paired_quad(config,
                    paired_radial_sample(dpc, config, lane, x, y), lane);
            }
        }
    }
    paired_main(config->encoding == NIKON_RAW_MONO ? dpc : filtered,
                 config, planes, dpc, out_y, out_uv);
    return true;
}
