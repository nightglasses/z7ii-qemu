/*
 * Bounded mono/CFA RAW12 processing, not physical sensor calibration.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef NIKON_PAIRED_PIXELS_H
#define NIKON_PAIRED_PIXELS_H

#include <stdbool.h>
#include <stdint.h>
#include "hw/nikon/raw.h"

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

/* Hypothesized bounded median-residual correction, with no wire enable bit. */
typedef struct NikonPairedDetail {
    uint8_t limit, strength, lane;
} NikonPairedDetail;

/* Finite axial residual correction for the eight-word 75c..778 packet. */
typedef struct NikonPairedAdaptive {
    uint16_t base, slope, cap, weight;
    uint8_t precision, dose;
} NikonPairedAdaptive;
/* Functional coefficient-only radial magnification, not physical lens data. */
typedef struct NikonPairedRadial {
    int32_t center_x, center_y; /* Wire center minus the RAW ROI origin. */
    int16_t red, blue;         /* Signed Q16 corrections in [-128,127]. */
} NikonPairedRadial;

/* Functional roles for the complete source-interpolated 900..9c4 family. */
typedef struct NikonPairedColorBranch {
    uint16_t negative_base, negative_slope, positive_base, positive_slope;
    uint8_t shift[4], radius;
} NikonPairedColorBranch;

typedef struct NikonPairedColorBlock {
    uint16_t base, slope, negative_limit, positive_limit;
    NikonPairedColorBranch branch[2];
    uint8_t strength[3];
} NikonPairedColorBlock;

typedef struct NikonPairedColorFilter {
    NikonPairedColorBlock block[2];
    uint16_t negative, positive;
    uint8_t precision, strength, shift, weight;
} NikonPairedColorFilter;

typedef struct NikonPairedPixelConfig {
    NikonRawEncoding encoding;
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
    NikonPairedDetail detail;
    NikonPairedAdaptive adaptive;
    NikonPairedRadial radial;
    NikonPairedColorFilter colorfilter;
    int16_t a[9], b[9], c[9];
} NikonPairedPixelConfig;

/* Zero feedback requires a disabled channel; no seed is read from RAM. */
bool nikon_paired_noise_valid(const NikonPairedPixelConfig *config);

/* Disabled filters require zero fields; enabled scalars are unsigned14-bit. */
bool nikon_paired_filter_valid(const NikonPairedFilter *filter);

/* No enable/bypass bit is inferred; all fields must describe a finite family. */
bool nikon_paired_denoise_valid(const NikonPairedDenoise *denoise);

/* Zero strength is valid; an omitted/zero parameter group is not valid. */
bool nikon_paired_detail_valid(const NikonPairedDetail *detail);

/* Dose zero is algebraically neutral; all other scalars remain mandatory. */
bool nikon_paired_adaptive_valid(const NikonPairedAdaptive *adaptive);
bool nikon_paired_radial_valid(const NikonPairedRadial *radial);
bool nikon_paired_colorfilter_valid(const NikonPairedColorFilter *filter);

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
 * A mandatory detail stage follows denoise, before temporal history. Read an
 * immutable replicated-edge3x3 neighborhood including the center. In the same
 * sample*4 domain, clamp its median-minus-center residual to +/-limit, then
 * multiply by strength*lane/1024. Round the signed correction to nearest,
 * ties away from zero, clamp14-bit and round/clamp to RAW12. Limits4..31,
 * strengths0..32 and common lanes8..16 are finite source-family bounds;
 * their roles, scales and stage order are functional hypotheses. The explicit
 * neutral tuple {limit4,strength0,lane8} runs the same arithmetic and produces
 * zero correction. No wire bypass/enable or omitted-group identity is inferred.
 * The mandatory 75c..778 suffix follows detail. With immutable axial neighbors
 * and replicated borders, n=sum(neighbors)-4*center remains an exact rational
 * mean residual n/4. Let t=min(cap,base+round(slope*center/4095)), w=weight*dose
 * and r=16*(1<<precision). Add round(clamp(n,-4*t,4*t)*w/(4*(r+w))) to center
 * and clamp to RAW12. Signed ties round away from zero. Dose0 is algebraically
 * neutral, not a recovered enable bit. All scalar roles, units and ordering are
 * functional hypotheses. CFA filtering runs this stage on each phase plane.
 * Two mandatory color-filter blocks and a median tail then run before temporal
 * blending. They use immutable same-phase neighborhoods and sample*4 arithmetic.
 * A block gates signed neighbor residuals by positive/negative thresholds formed
 * from common/branch base+slope*sample/16383. Four directional weights are powers
 * of two; the branch radius is1+radius, with radius0 or2, not an enable. Average
 * axial/diagonal corrections, scale by lane strength/31 and clamp to the signed
 * limits. R/Gr/Gb/B use strengths R/G/G/B; MONO uses round((R+G+B)/3). The second
 * block consumes the first block's completed image. The tail clamps the3x3
 * median residual to [-negative,positive] and scales it by
 * strength*weight/(31*4*2^shift*2^precision). Signed ties round away from zero;
 * each pass clamps14-bit and rounds/clamps back to RAW12. All tap/field roles,
 * scales and ordering are explicit functional hypotheses. Full parameter-family
 * bounds and duplicate relationships are mandatory, including zero strength.
 * No omitted-group bypass, guessed enable bit or nonzero kernel is supported.
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
 * CFA encodings require even input dimensions. Lane R/Gr/Gb/B is
 * (encoding-1) XOR ((y&1)*2+(x&1)); the caller adjusts encoding for the RAW
 * ROI origin. Filtering runs separately on each half-resolution phase plane.
 * Temporal history contributes one common signed correction per 2x2 block,
 * using the existing mean/residual qualification and weight. It never blends
 * individual color samples toward a scalar mean. WORK and grid values use this
 * pre-color mosaic. The quad affine is then applied to each actual lane.
 * Bilinear demosaicing replicates the nearest same-phase border, preserves
 * either measured green at green sites, and averages the two interpolated
 * greens at R/B sites. Existing curves/gains/noise/matrices follow. These CFA
 * geometry, arithmetic and history choices are explicit functional hypotheses.
 * MAIN crop x/y may be odd; interpolation uses the complete RAW ROI.
 * A coefficient-only radial hypothesis modifies MAIN R/B coordinates before
 * quad correction:
 * source = destination + (destination-center)*coefficient/65536.
 * G is the reference. Centers are wire unsigned16 values minus the RAW ROI
 * origin, independent of the later MAIN crop. Immutable same-phase bilinear
 * sampling replicates borders and rounds once with unsigned64-bit weights.
 * MONO similarly samples R/B independently before quad correction. Coefficient0
 * follows the same arithmetic and exactly preserves integer sampling. This
 * bounded path has no published radial LUT; it does not interpret a nonzero,
 * partially published or even explicitly zero published radial LUT.
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
