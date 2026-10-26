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

typedef struct NikonPairedPixelConfig {
    unsigned width, height, x, y, output_width, output_height;
    unsigned grid_width, grid_height;
    uint16_t gain[3];
    bool gain_enabled[3];
    uint16_t curve[2][257];
    bool curve_enabled[2];
    NikonPairedNoise noise[3];
    uint16_t noise_negative, noise_positive;
    int16_t a[9], b[9], c[9];
} NikonPairedPixelConfig;

/* Zero feedback requires a disabled channel; no seed is read from RAM. */
bool nikon_paired_noise_valid(const NikonPairedPixelConfig *config);

/*
 * Functional grayscale choices: coherent-neighbor impulse suppression, a
 * five-tap spatial filter, and bounded half-block history blending. Each
 * resulting mono sample passes through up to two Q10 gain curves (257 knots
 * at sample steps of16), then is replicated into RGB, optionally gained in
 * Q8, optionally perturbed by bounded LFSR noise, then transformed by A/C Q10
 * and B Q9. Curves/gains/noise affect main color
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
