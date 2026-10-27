/*
 * Bounded monochrome RAW12 processing, not physical sensor calibration.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef NIKON_PAIRED_PIXELS_H
#define NIKON_PAIRED_PIXELS_H

#include <stdbool.h>
#include <stdint.h>

typedef struct NikonPairedNoise {
    uint32_t feedback;
    uint8_t enabled, gain, shift, offset;
} NikonPairedNoise;

typedef struct NikonPairedFilter {
    bool enabled, refine, clipped;
    uint16_t retain, base, radius, slope;
} NikonPairedFilter;

/* Functional roles for the separate, source-interpolated scalar family. */
typedef struct NikonPairedDenoise {
    uint16_t threshold, low, high;
    uint8_t axial, diagonal, bias, retain;
} NikonPairedDenoise;

typedef struct NikonPairedPixelConfig {
    unsigned width, height, x, y, output_width, output_height;
    unsigned grid_width, grid_height;
    uint16_t quad_gain[4], quad_pedestal[4], quad_limit;
    int16_t quad_offset[4];
    bool quad_enabled;
    uint16_t gain[3];
    bool gain_enabled[3];
    uint16_t curve[2][257];
    bool curve_enabled[2];
    NikonPairedNoise noise[3];
    uint16_t noise_negative, noise_positive;
    NikonPairedFilter filter;
    NikonPairedDenoise denoise;
    int16_t a[9], b[9], c[9];
} NikonPairedPixelConfig;

/* Zero feedback requires a disabled channel; no seed is read from RAM. */
bool nikon_paired_noise_valid(const NikonPairedPixelConfig *config);

/* Disabled filters require zero fields; enabled scalars are unsigned14-bit. */
bool nikon_paired_filter_valid(const NikonPairedFilter *filter);

/* No enable/bypass bit is inferred; all fields must describe a finite family. */
bool nikon_paired_denoise_valid(const NikonPairedDenoise *denoise);

/*
 * Functional grayscale choices: coherent-neighbor impulse suppression, a
 * five-tap spatial filter, an optional adaptive filter, and bounded half-block
 * history blending. The adaptive filter uses immutable replicated-edge
 * neighborhoods at radius0..15 in a sample*4 intermediate domain. It moves
 * toward the four axial neighbors' mean by (16383-retain)/16383. Clipping
 * bounds that residual by base+round(slope*sample*4/16383), saturated at16383.
 * Refine selects a horizontal two-neighbor pass followed by a vertical pass.
 * Each pass rounds signed corrections to nearest, ties away from zero, then
 * rounds/clamps back to RAW12. Radius0 or retain16383 is identity. These
 * scales, control meanings and processing order are functional hypotheses.
 * A second, mandatory finite denoise stage uses immutable radius-one axial
 * and diagonal neighbors. With s=sample*4, L=threshold+low+bias and
 * U=threshold+high+bias, a neighbor residual d receives weight1 below L,
 * (U-abs(d))/(U-L) between L/U and0 above U. The axial/diagonal means are
 * weighted by their respective scalars and divided by retain+axial+diagonal.
 * Round the correction to nearest, signed ties away from zero, clamp to14-bit,
 * then round/clamp to RAW12. Replicate borders. Its role mapping and placement
 * before temporal blending are functional hypotheses, not silicon semantics.
 * Zero denoise fields are invalid. A synthetic neutral tuple is
 * {threshold7,low1,high3,axial3,diagonal1,bias0,retain31}; its correction
 * quantizes back to the original RAW12 through the same numerical operation.
 * The resulting mono sample optionally passes through four affine lanes:
 * clamp(round((mono + offset) * gain /512) + pedestal,0,min(limit,4095)).
 * Lanes0/3 become R/B; the rounded mean of lanes1/2 becomes G. Gains are
 * unsigned14-bit, offsets signed13-bit, pedestals unsigned16-bit and the
 * shared limit unsigned15-bit. Lane order, RAW12 units and color-only placement
 * are functional choices, not recovered CFA processing. RGB then passes through
 * up to two Q10 gain curves (257 knots at sample steps of16), optionally gained
 * in Q8, optionally perturbed by bounded LFSR noise, then transformed by
 * A/C Q10 and B Q9. Curves/gains/noise affect main color
 * output only. These choices do not establish the silicon algorithm.
 *
 * raw contains width*height contiguous host-endian RAW12 samples. Optional
 * history and mandatory half outputs contain ceil(width/2)*ceil(height/2)
 * host-endian RAW12 samples; absent history contributes no temporal weight.
 * Half residual is the pre-temporal spatial mean's deviation from prior luma,
 * or zero when prior luma is absent. Half luma/grid use the filtered result.
 * The grid contains grid_width*grid_height RAW12 samples. Main outputs are
 * cropped at x/y: tightly packed Y and interleaved UV (NV12), respectively
 * output_width*output_height and output_width*output_height/2 bytes.
 *
 * Dimensions are at most 4096; main output dimensions must be positive and
 * even, and its rectangle must fit the input. Grid dimensions must fit the
 * input. A/C coefficients are signed 13-bit and B coefficients signed 12-bit.
 * Every supplied 16-bit sample must be in [0,4095]. Output buffers must not
 * overlap one another or input/history buffers. The caller supplies their
 * complete extents. Failure leaves all output buffers unchanged.
 *
 * Noise uses primitive degree-32 right-shift Galois feedback masks. Each main
 * job starts three states at1; enabled channels step once per emitted pixel,
 * in 2x2 block, dy, dx order. The post-step top11 bits minus1024 are clamped
 * to [-noise_negative,noise_positive] (both <=1023), multiplied by gain and
 * rounded by10 bits. Add offset, round by shift (<=15), then add/clamp RGB12.
 * These seed, order, slice and scalar meanings are functional hypotheses.
 */
bool nikon_paired_pixels(const NikonPairedPixelConfig *config,
                         const uint16_t *raw_contiguous_ROI,
                         const uint16_t *prior_half_luma_nullable,
                         const uint16_t *prior_half_residual_nullable,
                         uint16_t *out_half_luma,
                         uint16_t *out_half_residual,
                         uint16_t *out_grid,
                         uint8_t *out_y, uint8_t *out_uv);

#endif
