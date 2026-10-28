/*
 * Finite mono/CFA RAW12 operator. Filter thresholds, temporal weights,
 * matrix rounding and confidence modulation are explicit functional hypotheses,
 * not recovered silicon processing or physical-camera calibration.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "hw/nikon/pairedpixels.h"

#define PAIRED_MAX_DIMENSION 4096
#define PAIRED_MAX_SAMPLE 4095

static uint16_t paired_clamp(int32_t value)
{
    return CLAMP(value, 0, PAIRED_MAX_SAMPLE);
}

static int32_t paired_round(int32_t value, unsigned shift)
{
    uint32_t magnitude, quotient;

    if (!shift) {
        return value;
    }
    /* All callers use shifts 1..15 (or fixed 8/9/10). Form the magnitude in
     * unsigned arithmetic before negation, preserving symmetric nearest ties
     * away from zero even for INT32_MIN and any shift 1..31.
     */
    magnitude = value < 0 ? 0U - (uint32_t)value : (uint32_t)value;
    quotient = (magnitude + (1U << (shift - 1))) >> shift;
    return value < 0 ? -(int32_t)quotient : (int32_t)quotient;
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
           p->edge_limit == 5 && p->negative_base >= 10 &&
           p->negative_base <= 200 && p->positive_base >= 10 &&
           p->positive_base <= 200 && p->slope <= 4 && p->retain <= 31 &&
           p->modifier <= 511 &&
           ((p->mix == 64 && p->radius == 1 && !p->diagonal &&
             p->amount <= 360) ||
            (p->mix == 128 && p->radius == 1 && !p->diagonal &&
             p->amount <= 235) ||
            (p->mix == 128 && p->radius == 3 && p->diagonal == 16 &&
             p->amount >= 4 && p->amount <= 100));
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
        (p->grid_enabled &&
         (!p->grid_width || !p->grid_height ||
          p->grid_width > PAIRED_MAX_DIMENSION ||
          p->grid_height > PAIRED_MAX_DIMENSION ||
          p->grid_origin_x < -4095 || p->grid_origin_x > 65535 ||
          p->grid_origin_y < -4095 || p->grid_origin_y > 65535)) ||
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
    /* The five-sample median is within the four neighbors' [low, high].
     * This sufficient signed bound proves the replacement test is false;
     * differences exactly 1024 keep the center as in the scalar sort.
     */
    if (high - low > 64 ||
        ((int)center >= (int)high - 1024 &&
         (int)center <= (int)low + 1024)) {
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
                            unsigned width, unsigned height,
                            unsigned first, unsigned last)
{
    unsigned x, y;

    for (y = first; y < last; y++) {
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
                                const NikonPairedFilter *f, unsigned axis,
                                unsigned first, unsigned last)
{
    for (unsigned y = first; y < last; y++) {
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

static void paired_denoise(const uint16_t *input, uint16_t *output,
                           unsigned width, unsigned height,
                           const NikonPairedDenoise *n,
                           unsigned first, unsigned last)
{
    int low = n->threshold + n->low + n->bias;
    int high = n->threshold + n->high + n->bias;
    int scale = high - low;
    int divisor = 4 * scale * (n->retain + n->axial + n->diagonal);

    for (unsigned y = first; y < last; y++) {
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
                          const NikonPairedDetail *d,
                          unsigned first, unsigned last)
{
    for (unsigned y = first; y < last; y++) {
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
                            const NikonPairedAdaptive *a,
                            unsigned first, unsigned last)
{
    int64_t weight = (int64_t)a->weight * a->dose;
    int64_t denominator = 4 * ((16 << a->precision) + weight);

    for (unsigned y = first; y < last; y++) {
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

/* For 0 <= n < 2^32, ceil(2^32/d) overestimates floor(n/d) by at most one.
 * Correct that one possible excess before restoring the signed rounded result.
 * A contributing color tap has |value| <= threshold^2/4, threshold <= 2360;
 * the rounding numerator fits 21 bits. Keep d=1's multiplier (2^32) in u64.
 */
static int paired_color_divide(int value, unsigned divisor, uint64_t multiplier)
{
    uint64_t magnitude = (uint64_t)abs(value) + divisor / 2;
    uint64_t quotient = (magnitude * multiplier) >> 32;

    quotient -= quotient * divisor > magnitude;
    return value < 0 ? -(int)quotient : quotient;
}

typedef struct PairedColorBlockActive {
    unsigned weights[2];
    uint64_t weight_multiplier[2];
    uint64_t *multipliers;
} PairedColorBlockActive;

static bool paired_color_prepare(const NikonPairedColorBlock *b,
                                  PairedColorBlockActive *active)
{
    unsigned limit = 0;

    for (unsigned branch = 0; branch < 2; branch++) {
        const NikonPairedColorBranch *n = &b->branch[branch];
        unsigned bound = b->base + b->slope +
            MAX(n->negative_base + n->negative_slope,
                n->positive_base + n->positive_slope);

        limit = MAX(limit, bound);
        for (unsigned d = 0; d < 4; d++) {
            active->weights[branch] += 1U << n->shift[d];
        }
        active->weight_multiplier[branch] = ((UINT64_C(1) << 32) +
            active->weights[branch] - 1) / active->weights[branch];
    }
    /* All thresholds are nonnegative and bounded by the validated packet.
     * At most 2361 u64 entries; constant-threshold packets need far fewer.
     */
    active->multipliers = g_try_new(uint64_t, limit + 1);
    if (!active->multipliers) {
        return false;
    }
    active->multipliers[0] = 0;
    for (unsigned d = 1; d <= limit; d++) {
        active->multipliers[d] = ((UINT64_C(1) << 32) + d - 1) / d;
    }
    return true;
}

static void paired_color_block(const uint16_t *input, uint16_t *output,
    unsigned width, unsigned height, const NikonPairedColorBlock *b,
    const PairedColorBlockActive *active, unsigned strength,
    unsigned first, unsigned last)
{
    static const int dx[2][4] = { { -1, 1, 0, 0 }, { -1, 1, -1, 1 } };
    static const int dy[2][4] = { { 0, 0, -1, 1 }, { -1, -1, 1, 1 } };

    for (unsigned y = first; y < last; y++) {
        for (unsigned x = 0; x < width; x++) {
            unsigned at = y * width + x;
            int center = 4 * input[at];
            int common = b->base + paired_divide((int64_t)b->slope * center,
                                                16383);
            int mean[2];

            for (unsigned branch = 0; branch < 2; branch++) {
                const NikonPairedColorBranch *n = &b->branch[branch];
                unsigned radius = 1 + n->radius;
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

                    if (difference && gate) {
                        sum += (int64_t)weight * paired_color_divide(
                            difference * gate, threshold,
                            active->multipliers[threshold]);
                    }
                }
                /* T <= 2360 bounds each rounded tap by ceil(T/4) <= 590.
                 * Validated weights sum to at most 56, so |sum| <= 33040.
                 * This signed32 reciprocal is exact after the same one-step
                 * correction. Its divisor is independent of the T table.
                 */
                mean[branch] = paired_color_divide((int)sum,
                    active->weights[branch], active->weight_multiplier[branch]);
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
                              const NikonPairedColorFilter *f,
                              unsigned first, unsigned last)
{
    unsigned divisor = 31 * 4 * (1U << f->shift) * (1U << f->precision);

    for (unsigned y = first; y < last; y++) {
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

static unsigned paired_color_strength(const NikonPairedColorBlock *b,
                                      unsigned lane)
{
    return lane == 4 ?
        (b->strength[0] + b->strength[1] + b->strength[2] + 1) / 3 :
        b->strength[lane == 3 ? 2 : lane != 0];
}

static void paired_temporal(uint16_t *filtered, unsigned width, unsigned height,
                             const uint16_t *prior_luma,
                             const uint16_t *prior_residual,
                             uint16_t *out_luma, uint16_t *out_residual,
                             bool cfa, unsigned first, unsigned last)
{
    unsigned half_width = (width + 1) / 2;
    unsigned hx, hy, x, y;

    for (hy = first; hy < last; hy++) {
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

typedef struct PairedColorActive {
    bool curve[2], gain[3], a, c;
} PairedColorActive;

static bool paired_matrix_identity(const int16_t matrix[9])
{
    for (unsigned i = 0; i < 9; i++) {
        if (matrix[i] != (i % 4 ? 0 : 1024)) {
            return false;
        }
    }
    return true;
}

static PairedColorActive paired_color_active(const NikonPairedPixelConfig *p)
{
    PairedColorActive active = { 0 };

    for (unsigned table = 0; table < 2; table++) {
        if (p->curve_enabled[table]) {
            for (unsigned i = 0; i < ARRAY_SIZE(p->curve[table]); i++) {
                if (p->curve[table][i] != 1024) {
                    active.curve[table] = true;
                    break;
                }
            }
        }
    }
    for (unsigned channel = 0; channel < 3; channel++) {
        active.gain[channel] = p->gain_enabled[channel] && p->gain[channel] != 256;
    }
    active.a = !paired_matrix_identity(p->a);
    active.c = !paired_matrix_identity(p->c);
    return active;
}

static void paired_rgb_yuv(const NikonPairedPixelConfig *p,
                            const PairedColorActive *active, uint16_t rgb[3],
                            uint32_t noise_state[3], uint16_t output[3])
{
    uint16_t after_a[3];
    const uint16_t *color = rgb;
    int32_t yuv[3];
    unsigned row, column;

    for (column = 0; column < 3; column++) {
        const NikonPairedNoise *n = &p->noise[column];

        /* Two source-published curves: Q10 gain knots at sample steps of16. */
        for (unsigned table = 0; table < 2; table++) {
            if (active->curve[table]) {
                unsigned sample = rgb[column];
                unsigned index = sample >> 4, fraction = sample & 15;
                unsigned gain =
                    (p->curve[table][index] * (16 - fraction) +
                     p->curve[table][index + 1] * fraction + 8) / 16;

                rgb[column] = paired_clamp(paired_round(sample * gain, 10));
            }
        }
        if (active->gain[column]) {
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
    /* Q10 identity preserves already clamped RGB, including after noise. */
    if (active->a) {
        paired_matrix(p->a, rgb, after_a);
        color = after_a;
    }
    for (row = 0; row < 3; row++) {
        int32_t sum = 0;

        for (column = 0; column < 3; column++) {
            sum += p->b[3 * row + column] * color[column];
        }
        yuv[row] = CLAMP(paired_round(sum, 9), row ? -2048 : 0,
                        row ? 2047 : 4095);
    }
    output[0] = yuv[0];
    unsigned index = yuv[0] >> 7, fraction = yuv[0] & 127;
    unsigned gain = (p->saturation[index] * (128 - fraction) +
                     p->saturation[index + 1] * fraction + 64) / 128;

    for (row = 1; row < 3; row++) {
        int32_t mixed = yuv[row];

        /* For identity C, its signed Q10 round is exact. The validated
         * complementary Q8 weights then sum to 256, so that round is exact too.
         */
        if (active->c) {
            int32_t transformed = paired_round(p->c[3 * row + 1] * yuv[1] +
                                               p->c[3 * row + 2] * yuv[2], 10);

            mixed = paired_round(p->post.original * yuv[row] +
                                  p->post.transformed * transformed, 8);
        }

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
    int64_t sx, sy;

    /* Zero displacement is one exact tap on MONO's Q16 unit lattice or
     * this same CFA phase's Q17 lattice: both fractions are zero, including
     * replicated edges. Cross-lane coordinates still require interpolation.
     */
    if (!coefficient && (p->encoding == NIKON_RAW_MONO ||
        (lane ^ (p->encoding - 1)) == ((y & 1) * 2 + (x & 1)))) {
        return raw[y * p->width + x];
    }
    sx = ((int64_t)x << 16) +
         ((int64_t)x - p->radial.center_x) * coefficient;
    sy = ((int64_t)y << 16) +
         ((int64_t)y - p->radial.center_y) * coefficient;
    return paired_lattice_sample(raw, p, lane, sx, sy);
}

static void paired_yuv(const NikonPairedPixelConfig *p, const uint16_t *raw,
                        const PairedColorActive *active,
                        unsigned x, unsigned y,
                        uint32_t noise_state[3], uint16_t output[3])
{
    uint16_t mono = raw[y * p->width + x];
    uint16_t rgb[] = {
        paired_quad(p, paired_radial_sample(raw, p, 0, x, y), 0),
        (paired_quad(p, mono, 1) + paired_quad(p, mono, 2) + 1) / 2,
        paired_quad(p, paired_radial_sample(raw, p, 3, x, y), 3),
    };

    paired_rgb_yuv(p, active, rgb, noise_state, output);
}

/* Interpolate one lane on its half-resolution lattice. Edge samples are
 * replicated on that lattice, never substituted from another color phase.
 */
static uint16_t paired_cfa_sample(const uint16_t *raw,
                                 const NikonPairedPixelConfig *p,
                                 unsigned lane, unsigned x, unsigned y)
{
    unsigned phase = lane ^ (p->encoding - 1);
    unsigned px = phase & 1, py = phase >> 1;
    unsigned x0 = (x & 1) == px ? x : x ? x - 1 : px;
    unsigned y0 = (y & 1) == py ? y : y ? y - 1 : py;
    unsigned x1 = (x & 1) == px ? x : MIN(x + 1, p->width - 2 + px);
    unsigned y1 = (y & 1) == py ? y : MIN(y + 1, p->height - 2 + py);

    /* Integer full-RAW coordinates have only 0 or half-phase fractions in
     * the Q17 lattice. Replicated edges collapse to one sample. Reduce its
     * exact weighted sum before the single rounding, never round two axes
     * separately. Four RAW12 taps sum to at most 16380.
     */
    if (y0 == y1) {
        if (x0 == x1) {
            return raw[y0 * p->width + x0];
        }
        return (raw[y0 * p->width + x0] + raw[y0 * p->width + x1] + 1) / 2;
    }
    if (x0 == x1) {
        return (raw[y0 * p->width + x0] + raw[y1 * p->width + x0] + 1) / 2;
    }
    return (raw[y0 * p->width + x0] + raw[y0 * p->width + x1] +
            raw[y1 * p->width + x0] + raw[y1 * p->width + x1] + 2) / 4;
}

static void paired_cfa_yuv(const uint16_t *raw, const NikonPairedPixelConfig *p,
                            const PairedColorActive *active,
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

    paired_rgb_yuv(p, active, rgb, noise_state, output);
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

static unsigned paired_confidence(const NikonPairedPixelConfig *p,
                                   const uint16_t *grid, unsigned x, unsigned y)
{
    const uint64_t one = UINT64_C(1) << 22;
    int64_t xx = (int64_t)p->grid_start_x +
        16 * ((int64_t)x - p->grid_origin_x) * p->grid_step_x;
    int64_t yy = (int64_t)p->grid_start_y +
        16 * ((int64_t)y - p->grid_origin_y) * p->grid_step_y;
    unsigned x0, y0, x1, y1;
    uint64_t fx, fy, sum;

    if (!p->grid_enabled) {
        return 4095;
    }
    xx = CLAMP(xx, 0, (int64_t)((p->grid_width - 1) * one));
    yy = CLAMP(yy, 0, (int64_t)((p->grid_height - 1) * one));
    x0 = xx / one;
    y0 = yy / one;
    x1 = MIN(x0 + 1, p->grid_width - 1);
    y1 = MIN(y0 + 1, p->grid_height - 1);
    fx = xx % one;
    fy = yy % one;
    /* Four products sum below2^56; round once after both coordinates. */
    sum = grid[y0 * p->grid_width + x0] * (one - fx) * (one - fy) +
          grid[y0 * p->grid_width + x1] * fx * (one - fy) +
          grid[y1 * p->grid_width + x0] * (one - fx) * fy +
          grid[y1 * p->grid_width + x1] * fx * fy;
    return (sum + one * one / 2) / (one * one);
}

static int32_t paired_post_kernel(const uint16_t *input, unsigned width,
                                  unsigned height, unsigned x, unsigned y,
                                  const NikonPairedPost *p)
{
    static const int dx[2][4] = { { -1, 1, 0, 0 }, { -1, 1, -1, 1 } };
    static const int dy[2][4] = { { 0, 0, -1, 1 }, { -1, -1, 1, 1 } };
    int center = input[y * width + x];
    int slope = paired_ratio((int64_t)p->slope * center, 16);
    int negative = 16 * p->negative_base + slope;
    int positive = 16 * p->positive_base + slope;
    int64_t residual = 0;

    for (unsigned branch = 0; branch < 2; branch++) {
        int sum = 0;

        for (unsigned n = 0; n < 4; n++) {
            unsigned xx = CLAMP((int)x + dx[branch][n] * p->radius,
                                0, (int)width - 1);
            unsigned yy = CLAMP((int)y + dy[branch][n] * p->radius,
                                0, (int)height - 1);
            int difference = (int)input[yy * width + xx] - center;

            sum += CLAMP(difference, -negative, positive);
        }
        residual += (branch ? p->diagonal : 16 - p->diagonal) * sum;
    }
    /* Keep both neighborhood means and the complementary blend rational.
     * The signed numerator stays below2^49, denominator below2^36.
     */
    return paired_ratio(residual * (31 - p->retain) * p->amount *
                         p->modifier * p->mix,
                         INT64_C(64) * 31 * 256 * 512 * (128 + p->mix));
}

static void paired_post_pass(const uint16_t *input, uint16_t *output,
                              unsigned width, unsigned height,
                              const NikonPairedPixelConfig *config,
                              const uint16_t *grid, unsigned channel,
                              unsigned pass, unsigned first, unsigned last)
{
    const NikonPairedPost *p = &config->post;

    for (unsigned y = first; y < last; y++) {
        unsigned up = y ? y - 1 : y;
        unsigned down = MIN(y + 1, height - 1);

        for (unsigned x = 0; x < width; x++) {
            unsigned left = x ? x - 1 : x;
            unsigned right = MIN(x + 1, width - 1);
            int center = input[y * width + x];
            int n;
            int32_t correction;

            if (pass == 3) {
                correction = paired_post_kernel(input, width, height, x, y, p);
                output[y * width + x] = paired_clamp(center + correction);
                continue;
            } else if (pass == 2) {
                n = input[up * width + left] + input[up * width + right] +
                    input[down * width + left] + input[down * width + right];
            } else {
                n = input[y * width + left] + input[y * width + right] +
                    input[up * width + x] + input[down * width + x];
            }
            n -= 4 * center;
            if (!n) {
                output[y * width + x] = center;
                continue;
            }
            if (pass == 0) {
                correction = paired_ratio(-(int64_t)n *
                    p->unsharp[!!channel] * paired_confidence(config, grid,
                        config->x + x, config->y + y), 4 * 256 * 4095);
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

static void paired_main_color(const uint16_t *filtered,
    const NikonPairedPixelConfig *p, const PairedColorActive *active,
    uint16_t *planes, uint32_t noise_state[3], unsigned first, unsigned last)
{
    unsigned x, y, dx, dy;
    size_t pixels = (size_t)p->output_width * p->output_height;

    /* Preserve the original 2x2 noise-state traversal while materializing only
     * actual computed YUV. No RGB side buffer supplies any sample.
     */
    for (y = first; y < last; y += 2) {
        for (x = 0; x < p->output_width; x += 2) {
            for (dy = 0; dy < 2; dy++) {
                for (dx = 0; dx < 2; dx++) {
                    uint16_t yuv[3];
                    size_t at = (size_t)(y + dy) * p->output_width + x + dx;

                    if (p->encoding == NIKON_RAW_MONO) {
                        paired_yuv(p, filtered, active, p->x + x + dx,
                                   p->y + y + dy, noise_state, yuv);
                    } else {
                        paired_cfa_yuv(filtered, p, active, p->x + x + dx,
                                       p->y + y + dy, noise_state, yuv);
                    }
                    for (unsigned channel = 0; channel < 3; channel++) {
                        planes[channel * pixels + at] = yuv[channel];
                    }
                }
            }
        }
    }
}

static bool paired_post_neutral(const NikonPairedPost *p, unsigned channel,
                                unsigned pass)
{
    const bool neutral[] = {
        !p->unsharp[!!channel],
        !p->smooth[0],
        !p->smooth[1],
        !p->amount || !p->modifier || p->retain == 31,
        false,
    };

    return neutral[pass];
}

static void paired_main_pack(const NikonPairedPixelConfig *p,
    uint16_t *const planes[3], uint8_t *out_y, uint8_t *out_uv,
    unsigned first, unsigned last)
{
    unsigned x, y, dx, dy;

    for (y = first; y < last; y += 2) {
        for (x = 0; x < p->output_width; x += 2) {
            unsigned u = 0, v = 0;

            for (dy = 0; dy < 2; dy++) {
                for (dx = 0; dx < 2; dx++) {
                    size_t at = (size_t)(y + dy) * p->output_width + x + dx;

                    unsigned luma = ((uint64_t)planes[0][at] * p->main_gain +
                                     128) / 256;

                    out_y[at] = paired_byte(MIN(luma, 4095));
                    u += planes[1][at];
                    v += planes[2][at];
                }
            }
            out_uv[(y / 2) * p->output_width + x] = paired_byte((u + 2) / 4);
            out_uv[(y / 2) * p->output_width + x + 1] =
                paired_byte((v + 2) / 4);
        }
    }
}

static bool paired_buffers_disjoint(const void *const *inputs,
                                    const size_t *input_sizes,
                                    void *const outputs[4],
                                    const size_t output_sizes[4])
{
    for (unsigned i = 0; i < 4; i++) {
        uintptr_t start = (uintptr_t)outputs[i];

        if (output_sizes[i] > UINTPTR_MAX - start) {
            return false;
        }
        for (unsigned j = 0; j < 4; j++) {
            uintptr_t other = (uintptr_t)inputs[j];

            if (inputs[j] &&
                (input_sizes[j] > UINTPTR_MAX - other ||
                 (start < other + input_sizes[j] &&
                  other < start + output_sizes[i]))) {
                return false;
            }
        }
        for (unsigned j = 0; j < i; j++) {
            uintptr_t other = (uintptr_t)outputs[j];

            if (start < other + output_sizes[j] &&
                other < start + output_sizes[i]) {
                return false;
            }
        }
    }
    return true;
}

/* A stage owns disjoint writes; advance is a caller-provided join. Config and
 * input references never change while a task runs. Scratch is reused only after
 * the last reader's barrier, including the RAW allocations reused by POST.
 */
enum {
    PAIRED_PREFIX_GATHER,
    PAIRED_PREFIX_DPC,
    PAIRED_PREFIX_SPATIAL,
    PAIRED_PREFIX_FILTER,
    PAIRED_PREFIX_VERTICAL,
    PAIRED_PREFIX_DENOISE,
    PAIRED_PREFIX_DETAIL,
    PAIRED_PREFIX_ADAPTIVE,
    PAIRED_PREFIX_COLOR0,
    PAIRED_PREFIX_COLOR1,
    PAIRED_PREFIX_TAIL,
    PAIRED_PREFIX_SCATTER,
    PAIRED_PREFIX_DONE,
};

struct NikonPairedPixels {
    NikonPairedPixelConfig p;
    const uint16_t *raw, *prior, *residual, *grid;
    uint16_t *dpc, *filtered, *prefix_dpc, *prefix_filtered;
    uint16_t *prefix_input[4], *prefix_output[4];
    PairedColorBlockActive prefix_color[2];
    uint16_t *planes, *post_scratch, *half_luma, *half_residual;
    uint16_t *post_input[3], *post_output[3];
    uint8_t *y, *uv, *work;
    PairedColorActive active;
    NikonPairedPixelResult result;
    NikonPairedPixelStage stage;
    uint32_t seed[4][3];
    unsigned prefix_pass, post_pass;
    bool complete[12], success[12];
};

static unsigned paired_bands(unsigned rows)
{
    return MIN(rows, 4U);
}

static unsigned paired_band(unsigned rows, unsigned task, unsigned tasks)
{
    return rows * task / tasks;
}

static unsigned paired_prefix_bands(const NikonPairedPixels *c)
{
    return c->p.encoding == NIKON_RAW_MONO ? paired_bands(c->p.height) :
                                             MIN(c->p.height / 2, 3U);
}

static bool paired_prefix_active(const NikonPairedPixels *c, unsigned phase)
{
    const NikonPairedPixelConfig *p = &c->p;
    bool mono = p->encoding == NIKON_RAW_MONO;
    bool filter = p->filter.enabled && p->filter.radius &&
                  p->filter.retain != 16383;

    switch (c->prefix_pass) {
    case PAIRED_PREFIX_GATHER:
    case PAIRED_PREFIX_SCATTER:
        return !mono;
    case PAIRED_PREFIX_DPC:
    case PAIRED_PREFIX_SPATIAL:
    case PAIRED_PREFIX_DENOISE:
        return true;
    case PAIRED_PREFIX_FILTER:
        return filter;
    case PAIRED_PREFIX_VERTICAL:
        return filter && p->filter.refine;
    case PAIRED_PREFIX_DETAIL:
        return p->detail.strength != 0;
    case PAIRED_PREFIX_ADAPTIVE:
        return p->adaptive.weight && p->adaptive.dose;
    case PAIRED_PREFIX_COLOR0:
    case PAIRED_PREFIX_COLOR1:
        return paired_color_strength(
            &p->colorfilter.block[c->prefix_pass - PAIRED_PREFIX_COLOR0],
            mono ? 4 : phase ^ (p->encoding - 1)) != 0;
    case PAIRED_PREFIX_TAIL:
        return p->colorfilter.strength != 0;
    default:
        g_assert_not_reached();
    }
}

NikonPairedPixels *nikon_paired_pixels_new(const NikonPairedPixelConfig *config,
    const uint16_t *raw, const uint16_t *prior_luma,
    const uint16_t *prior_residual, const uint16_t *grid)
{
    NikonPairedPixels *c;

    if (!config || !raw) {
        return NULL;
    }
    c = g_try_new0(NikonPairedPixels, 1);
    if (c) {
        c->p = *config;
        c->raw = raw;
        c->prior = prior_luma;
        c->residual = prior_residual;
        c->grid = grid;
    }
    return c;
}

NikonPairedPixelStage nikon_paired_pixels_stage(const NikonPairedPixels *c)
{
    return c->stage;
}

unsigned nikon_paired_pixels_post_pass(const NikonPairedPixels *c)
{
    return c->post_pass;
}

unsigned nikon_paired_pixels_prefix_pass(const NikonPairedPixels *c)
{
    return c->prefix_pass;
}

unsigned nikon_paired_pixels_tasks(const NikonPairedPixels *c)
{
    switch (c->stage) {
    case NIKON_PAIRED_PIXEL_PREPARE:
        return 1;
    case NIKON_PAIRED_PIXEL_PREFIX: {
        unsigned phases = c->p.encoding == NIKON_RAW_MONO ? 1 : 4;
        unsigned active = 0;

        for (unsigned phase = 0; phase < phases; phase++) {
            active += paired_prefix_active(c, phase);
        }
        return active * paired_prefix_bands(c);
    }
    case NIKON_PAIRED_PIXEL_TEMPORAL:
        return paired_bands((c->p.height + 1) / 2);
    case NIKON_PAIRED_PIXEL_RADIAL:
        return c->p.encoding == NIKON_RAW_MONO ? 0 : paired_bands(c->p.height);
    case NIKON_PAIRED_PIXEL_COLOR:
    case NIKON_PAIRED_PIXEL_PACK:
        return paired_bands(c->p.output_height / 2);
    case NIKON_PAIRED_PIXEL_POST: {
        unsigned channels = 0;

        for (unsigned i = 0; i < 3; i++) {
            channels += !paired_post_neutral(&c->p.post, i, c->post_pass);
        }
        return channels * paired_bands(c->p.output_height);
    }
    case NIKON_PAIRED_PIXEL_DONE:
        return 0;
    }
    g_assert_not_reached();
}

static bool paired_prepare(NikonPairedPixels *c)
{
    const NikonPairedPixelConfig *p = &c->p;
    size_t pixels, half, main;
    unsigned bands;
    size_t visited = 0;
    bool noise;
    uint32_t state[3] = { 1, 1, 1 };

    if (!paired_config_valid(p) || (p->grid_enabled && !c->grid)) {
        return false;
    }
    pixels = (size_t)p->width * p->height;
    half = (size_t)((p->width + 1) / 2) * ((p->height + 1) / 2);
    main = (size_t)p->output_width * p->output_height;
    if (!paired_samples_valid(c->raw, pixels) ||
        !paired_samples_valid(c->prior, half) ||
        !paired_samples_valid(c->residual, half) ||
        (p->grid_enabled && !paired_samples_valid(c->grid,
            (size_t)p->grid_width * p->grid_height))) {
        return false;
    }
    c->dpc = g_try_new(uint16_t, pixels);
    c->filtered = g_try_new(uint16_t, pixels);
    if (p->encoding != NIKON_RAW_MONO) {
        c->prefix_dpc = g_try_new(uint16_t, pixels);
        c->prefix_filtered = g_try_new(uint16_t, pixels);
        if (!c->prefix_dpc || !c->prefix_filtered) {
            return false;
        }
    }
    c->planes = g_try_new(uint16_t, 3 * main);
    c->post_scratch = g_try_new(uint16_t, main);
    c->half_luma = g_try_new(uint16_t, half);
    c->half_residual = g_try_new(uint16_t, half);
    c->y = g_try_malloc(main);
    c->uv = g_try_malloc(main / 2);
    c->work = g_try_malloc(half * 4);
    if (!c->dpc || !c->filtered || !c->planes || !c->post_scratch ||
        !c->half_luma || !c->half_residual || !c->y || !c->uv || !c->work) {
        return false;
    }
    for (unsigned block = 0; block < 2; block++) {
        const NikonPairedColorBlock *b = &p->colorfilter.block[block];
        bool active = false;

        for (unsigned phase = 0; phase <
             (p->encoding == NIKON_RAW_MONO ? 1 : 4); phase++) {
            unsigned lane = p->encoding == NIKON_RAW_MONO ? 4 :
                            phase ^ (p->encoding - 1);

            active |= paired_color_strength(b, lane) != 0;
        }
        if (active && !paired_color_prepare(b, &c->prefix_color[block])) {
            return false;
        }
    }
    if (p->encoding == NIKON_RAW_MONO) {
        c->prefix_output[0] = c->dpc;
    } else {
        for (unsigned phase = 0; phase < 4; phase++) {
            c->prefix_output[phase] = c->filtered +
                                    phase * (pixels / 4);
        }
    }
    c->active = paired_color_active(p);
    bands = paired_bands(p->output_height / 2);
    noise = p->noise[0].enabled || p->noise[1].enabled || p->noise[2].enabled;
    for (unsigned band = 0; band < bands; band++) {
        size_t first = 2 * paired_band(p->output_height / 2, band, bands) *
                       (size_t)p->output_width;

        /* Every 2-row band starts between complete original 2x2 blocks. Each
         * enabled channel has exactly one recurrence per preceding pixel.
         * This scan stores only 4x3 states and performs no noise arithmetic.
         */
        if (!noise) {
            visited = first;
        }
        for (; visited < first; visited++) {
            for (unsigned channel = 0; channel < 3; channel++) {
                if (p->noise[channel].enabled) {
                    state[channel] = (state[channel] >> 1) ^
                        ((0U - (state[channel] & 1U)) &
                         p->noise[channel].feedback);
                }
            }
        }
        memcpy(c->seed[band], state, sizeof(state));
    }
    return true;
}

static void paired_prefix(NikonPairedPixels *c, unsigned task)
{
    const NikonPairedPixelConfig *p = &c->p;
    bool mono = p->encoding == NIKON_RAW_MONO;
    unsigned width = mono ? p->width : p->width / 2;
    unsigned height = mono ? p->height : p->height / 2;
    unsigned bands = paired_prefix_bands(c);
    unsigned band = task % bands, phase_task = task / bands, phase;
    unsigned first = paired_band(height, band, bands);
    unsigned last = paired_band(height, band + 1, bands);
    const uint16_t *input;
    uint16_t *output;

    for (phase = 0; phase < (mono ? 1 : 4); phase++) {
        if (paired_prefix_active(c, phase)) {
            if (!phase_task) {
                break;
            }
            phase_task--;
        }
    }
    input = c->prefix_input[phase];
    output = c->prefix_output[phase];
    switch (c->prefix_pass) {
    case PAIRED_PREFIX_GATHER:
        for (unsigned y = first; y < last; y++) {
            for (unsigned x = 0; x < width; x++) {
                output[y * width + x] =
                    c->raw[(2 * y + phase / 2) * p->width + 2 * x + phase % 2];
            }
        }
        break;
    case PAIRED_PREFIX_DPC:
        for (unsigned y = first; y < last; y++) {
            for (unsigned x = 0; x < width; x++) {
                output[y * width + x] = paired_dpc(mono ? c->raw : input,
                                                   width, height, x, y);
            }
        }
        break;
    case PAIRED_PREFIX_SPATIAL:
        paired_spatial(input, output, width, height, first, last);
        break;
    case PAIRED_PREFIX_FILTER:
    case PAIRED_PREFIX_VERTICAL:
        paired_filter_pass(input, output, width, height, &p->filter,
            c->prefix_pass == PAIRED_PREFIX_VERTICAL ? 2 : p->filter.refine,
            first, last);
        break;
    case PAIRED_PREFIX_DENOISE:
        paired_denoise(input, output, width, height, &p->denoise, first, last);
        break;
    case PAIRED_PREFIX_DETAIL:
        paired_detail(input, output, width, height, &p->detail, first, last);
        break;
    case PAIRED_PREFIX_ADAPTIVE:
        paired_adaptive(input, output, width, height, &p->adaptive, first, last);
        break;
    case PAIRED_PREFIX_COLOR0:
    case PAIRED_PREFIX_COLOR1: {
        unsigned block = c->prefix_pass - PAIRED_PREFIX_COLOR0;
        const NikonPairedColorBlock *b = &p->colorfilter.block[block];

        paired_color_block(input, output, width, height, b,
            &c->prefix_color[block], paired_color_strength(b,
                mono ? 4 : phase ^ (p->encoding - 1)), first, last);
        break;
    }
    case PAIRED_PREFIX_TAIL:
        paired_color_tail(input, output, width, height, &p->colorfilter,
                           first, last);
        break;
    case PAIRED_PREFIX_SCATTER:
        for (unsigned y = first; y < last; y++) {
            for (unsigned x = 0; x < width; x++) {
                c->dpc[(2 * y + phase / 2) * p->width + 2 * x + phase % 2] =
                    input[y * width + x];
            }
        }
        break;
    default:
        g_assert_not_reached();
    }
}

bool nikon_paired_pixels_run(NikonPairedPixels *c, unsigned task)
{
    const NikonPairedPixelConfig *p = &c->p;
    unsigned tasks = nikon_paired_pixels_tasks(c);
    bool success = true;

    if (task >= tasks || c->complete[task]) {
        return false;
    }
    switch (c->stage) {
    case NIKON_PAIRED_PIXEL_PREPARE:
        success = paired_prepare(c);
        break;
    case NIKON_PAIRED_PIXEL_PREFIX:
        paired_prefix(c, task);
        break;
    case NIKON_PAIRED_PIXEL_TEMPORAL:
        paired_temporal(c->dpc, p->width, p->height, c->prior, c->residual,
            c->half_luma, c->half_residual, p->encoding != NIKON_RAW_MONO,
            paired_band((p->height + 1) / 2, task, tasks),
            paired_band((p->height + 1) / 2, task + 1, tasks));
        break;
    case NIKON_PAIRED_PIXEL_RADIAL:
        for (unsigned y = paired_band(p->height, task, tasks);
             y < paired_band(p->height, task + 1, tasks); y++) {
            for (unsigned x = 0; x < p->width; x++) {
                unsigned lane = (p->encoding - 1) ^
                                ((y & 1) * 2 + (x & 1));

                c->filtered[y * p->width + x] = paired_quad(p,
                    paired_radial_sample(c->dpc, p, lane, x, y), lane);
            }
        }
        break;
    case NIKON_PAIRED_PIXEL_COLOR: {
        uint32_t state[3];

        memcpy(state, c->seed[task], sizeof(state));
        paired_main_color(p->encoding == NIKON_RAW_MONO ? c->dpc : c->filtered,
            p, &c->active, c->planes, state,
            2 * paired_band(p->output_height / 2, task, tasks),
            2 * paired_band(p->output_height / 2, task + 1, tasks));
        break;
    }
    case NIKON_PAIRED_PIXEL_POST: {
        unsigned bands = paired_bands(p->output_height);
        unsigned band = task % bands, channel_task = task / bands;
        unsigned channel;

        for (channel = 0; channel < 3; channel++) {
            if (!paired_post_neutral(&p->post, channel, c->post_pass)) {
                if (!channel_task) {
                    break;
                }
                channel_task--;
            }
        }
        paired_post_pass(c->post_input[channel], c->post_output[channel],
            p->output_width, p->output_height, p, c->grid, channel, c->post_pass,
            paired_band(p->output_height, band, bands),
            paired_band(p->output_height, band + 1, bands));
        break;
    }
    case NIKON_PAIRED_PIXEL_PACK: {
        size_t half = (size_t)((p->width + 1) / 2) * ((p->height + 1) / 2);

        paired_main_pack(p, c->post_input, c->y, c->uv,
            2 * paired_band(p->output_height / 2, task, tasks),
            2 * paired_band(p->output_height / 2, task + 1, tasks));
        for (size_t i = half * task / tasks; i < half * (task + 1) / tasks; i++) {
            stw_le_p(c->work + 2 * i, c->half_luma[i]);
            stw_le_p(c->work + 2 * half + 2 * i, c->half_residual[i]);
        }
        break;
    }
    case NIKON_PAIRED_PIXEL_DONE:
        g_assert_not_reached();
    }
    c->success[task] = success;
    c->complete[task] = true;
    return success;
}

bool nikon_paired_pixels_advance(NikonPairedPixels *c)
{
    if (c->stage == NIKON_PAIRED_PIXEL_DONE) {
        return false;
    }
    for (unsigned i = 0; i < nikon_paired_pixels_tasks(c); i++) {
        if (!c->complete[i] || !c->success[i]) {
            return false;
        }
    }
    if (c->stage == NIKON_PAIRED_PIXEL_PREFIX) {
        bool mono = c->p.encoding == NIKON_RAW_MONO;
        size_t plane = (size_t)c->p.width * c->p.height / 4;

        for (unsigned phase = 0; phase < (mono ? 1 : 4); phase++) {
            uint16_t *input = c->prefix_input[phase];

            if (!paired_prefix_active(c, phase) ||
                c->prefix_pass == PAIRED_PREFIX_SCATTER) {
                continue;
            }
            c->prefix_input[phase] = c->prefix_output[phase];
            if (c->prefix_pass == PAIRED_PREFIX_GATHER) {
                c->prefix_output[phase] = c->prefix_dpc + phase * plane;
            } else if (c->prefix_pass == PAIRED_PREFIX_DPC) {
                c->prefix_output[phase] = mono ? c->filtered :
                    c->prefix_filtered + phase * plane;
            } else {
                /* A zero correction preserves RAW12 exactly. Swap only the
                 * active lanes after every reader of this pass has joined.
                 */
                c->prefix_output[phase] = input;
            }
        }
        if (++c->prefix_pass == PAIRED_PREFIX_DONE) {
            g_clear_pointer(&c->prefix_dpc, g_free);
            g_clear_pointer(&c->prefix_filtered, g_free);
            for (unsigned block = 0; block < 2; block++) {
                g_clear_pointer(&c->prefix_color[block].multipliers, g_free);
            }
            if (mono && c->prefix_input[0] == c->filtered) {
                uint16_t *scratch = c->dpc;

                c->dpc = c->filtered;
                c->filtered = scratch;
            }
        }
    }
    if (c->stage == NIKON_PAIRED_PIXEL_COLOR) {
        size_t main = (size_t)c->p.output_width * c->p.output_height;

        /* The crop fits RAW, so the two dead RAW buffers and one MAIN scratch
         * hold three independent alternate planes after the COLOR barrier.
         */
        for (unsigned channel = 0; channel < 3; channel++) {
            c->post_input[channel] = c->planes + channel * main;
        }
        c->post_output[0] = c->dpc;
        c->post_output[1] = c->filtered;
        c->post_output[2] = c->post_scratch;
    }
    if (c->stage == NIKON_PAIRED_PIXEL_POST) {
        /* Neutral channels retain their initialized input, regardless of the
         * other channels' pass parity or uninitialized alternate storage.
         */
        for (unsigned channel = 0; channel < 3; channel++) {
            if (!paired_post_neutral(&c->p.post, channel, c->post_pass)) {
                uint16_t *input = c->post_input[channel];

                c->post_input[channel] = c->post_output[channel];
                c->post_output[channel] = input;
            }
        }
        c->post_pass++;
    }
    if ((c->stage != NIKON_PAIRED_PIXEL_POST || c->post_pass == 5) &&
        (c->stage != NIKON_PAIRED_PIXEL_PREFIX ||
         c->prefix_pass == PAIRED_PREFIX_DONE)) {
        c->stage++;
    }
    memset(c->complete, 0, sizeof(c->complete));
    memset(c->success, 0, sizeof(c->success));
    if (c->stage == NIKON_PAIRED_PIXEL_DONE) {
        c->result = (NikonPairedPixelResult) {
            c->half_luma, c->half_residual, c->y, c->uv, c->work,
        };
    }
    return true;
}

const NikonPairedPixelResult *nikon_paired_pixels_result(const NikonPairedPixels *c)
{
    return c->stage == NIKON_PAIRED_PIXEL_DONE ? &c->result : NULL;
}

void nikon_paired_pixels_free(NikonPairedPixels *c)
{
    if (!c) {
        return;
    }
    g_free(c->dpc);
    g_free(c->filtered);
    g_free(c->prefix_dpc);
    g_free(c->prefix_filtered);
    for (unsigned block = 0; block < 2; block++) {
        g_free(c->prefix_color[block].multipliers);
    }
    g_free(c->planes);
    g_free(c->post_scratch);
    g_free(c->half_luma);
    g_free(c->half_residual);
    g_free(c->y);
    g_free(c->uv);
    g_free(c->work);
    g_free(c);
}

bool nikon_paired_pixels(const NikonPairedPixelConfig *config,
                         const uint16_t *raw_contiguous_ROI,
                         const uint16_t *prior_half_luma_nullable,
                         const uint16_t *prior_half_residual_nullable,
                         uint16_t *out_half_luma,
                         uint16_t *out_half_residual,
                         const uint16_t *confidence_grid_nullable,
                         uint8_t *out_y, uint8_t *out_uv)
{
    NikonPairedPixels *c;
    const NikonPairedPixelResult *result;
    bool success = false;
    size_t pixels, half, main;

    if (!paired_config_valid(config) || !raw_contiguous_ROI ||
        !out_half_luma || !out_half_residual || !out_y || !out_uv) {
        return false;
    }
    pixels = (size_t)config->width * config->height;
    half = (size_t)((config->width + 1) / 2) * ((config->height + 1) / 2);
    main = (size_t)config->output_width * config->output_height;
    {
        const void *inputs[] = { raw_contiguous_ROI, prior_half_luma_nullable,
            prior_half_residual_nullable,
            config->grid_enabled ? confidence_grid_nullable : NULL };
        size_t input_sizes[] = { pixels * 2, half * 2, half * 2,
            (size_t)config->grid_width * config->grid_height * 2 };
        void *outputs[] = { out_half_luma, out_half_residual, out_y, out_uv };
        size_t output_sizes[] = { half * 2, half * 2, main, main / 2 };

        if (!paired_buffers_disjoint(inputs, input_sizes, outputs, output_sizes)) {
            return false;
        }
    }
    c = nikon_paired_pixels_new(config, raw_contiguous_ROI,
        prior_half_luma_nullable, prior_half_residual_nullable,
        confidence_grid_nullable);
    if (!c) {
        return false;
    }
    while (nikon_paired_pixels_stage(c) != NIKON_PAIRED_PIXEL_DONE) {
        for (unsigned i = 0; i < nikon_paired_pixels_tasks(c); i++) {
            if (!nikon_paired_pixels_run(c, i)) {
                goto out;
            }
        }
        if (!nikon_paired_pixels_advance(c)) {
            goto out;
        }
    }
    result = nikon_paired_pixels_result(c);
    memcpy(out_half_luma, result->half_luma, half * 2);
    memcpy(out_half_residual, result->half_residual, half * 2);
    memcpy(out_y, result->y, main);
    memcpy(out_uv, result->uv, main / 2);
    success = true;
out:
    nikon_paired_pixels_free(c);
    return success;
}
