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
/* Four source-shaped post-color groups; numerical roles are hypotheses. */
typedef struct NikonPairedPost {
    uint16_t unsharp[2]; /* B58: Y and shared U/V Q8 residual strengths. */
    uint16_t original, transformed; /* BAC: complementary Q8 C blend. */
    uint8_t smooth[2]; /* D28: axial then diagonal Q5 mean blends. */
    uint8_t edge_strength, edge_limit; /* DD4/DD8: bounded final blend. */
    uint8_t negative_base, positive_base, slope, retain;
    uint16_t amount, modifier; /* DDC/DE0: hypothesized Q8/Q9 factors. */
    uint8_t mix, radius, diagonal; /* Qualified DE4/DE8/DEC family. */
} NikonPairedPost;

typedef struct NikonPairedPixelConfig {
    NikonRawEncoding encoding;
    unsigned width, height, x, y, output_width, output_height;
    unsigned grid_width, grid_height;
    bool grid_enabled;
    uint32_t grid_start_x, grid_start_y; /* Q22. */
    uint16_t grid_step_x, grid_step_y; /* Effective Q18, multiplied by16. */
    int32_t grid_origin_x, grid_origin_y; /* Relative to the RAW ROI. */
    uint16_t quad_gain[4], quad_pedestal[4], quad_limit;
    int16_t quad_offset[4];
    bool quad_enabled;
    uint16_t gain[3];
    bool gain_enabled[3];
    uint16_t curve[2][257];
    bool curve_enabled[2];
    uint16_t saturation[33]; /* Mandatory Q8 MAIN chroma gain, Y step128. */
    NikonPairedNoise noise[3];
    uint16_t noise_negative, noise_positive;
    NikonPairedFilter filter;
    NikonPairedDenoise denoise;
    NikonPairedDetail detail;
    NikonPairedAdaptive adaptive;
    NikonPairedRadial radial;
    NikonPairedColorFilter colorfilter;
    NikonPairedPost post;
    uint16_t main_gain; /* Latest complete uniform 32-knot Q8 publication. */
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
bool nikon_paired_post_valid(const NikonPairedPost *post);

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
 * A Q10 and B Q9, then C Q10 in centered Y/chroma space. C preserves Y
 * and has no luma/chroma cross terms. BAC blends the original and C output
 * with complementary Q8 weights before centering chroma at2048. Curves/gains/noise affect main color
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
 * or zero when prior luma is absent. Half luma uses the filtered result.
 * When grid_enabled, confidence_grid_nullable is a mandatory immutable input
 * containing grid_width*grid_height host-endian twelve-bit samples. The grid
 * may be larger than RAW. Coordinates are Q22 start +16*(RAW-origin)*u16 step,
 * relative to the RAW ROI, with replicated edges and one bilinear rounding.
 * Its confidence meaning and effective Q18 step scale are virtual hypotheses.
 * Main outputs are
 * cropped at x/y: tightly packed Y and interleaved UV (NV12), respectively
 * output_width*output_height and output_width*output_height/2 bytes.
 *
 * Dimensions are at most 4096; main output dimensions must be positive and
 * even, and its rectangle must fit the input. Enabled grid dimensions are
 * positive and at most4096. A/C coefficients are signed13-bit and B signed12-bit.
 * MAIN uses three actual Q12 intermediate planes. B58 adds a Q8 axial unsharp
 * residual (Y strength0, shared chroma strength1), multiplied by confidence/4095
 * before its single final rounding. A disabled grid uses4095; a full map exactly
 * preserves the unmodulated law, and zero map/strength is neutral. D28 blends toward the
 * axial and diagonal means with its two Q5 strengths. A complete DC0..DEC
 * kernel then bounds negative/positive neighbor differences by16*base plus
 * slope*center/16, mixes axial/diagonal means at the qualified radius, and
 * scales the residual by the Q5 retention complement, Q8 amount, Q9 modifier
 * and mix/(128+mix). Zero modifier is naturally neutral in this equation.
 * DD4/DD8 finally add the
 * axial mean residual d, weighted by max(0,1-abs(d)/(16*edge_limit)) and
 * edge_strength/32. Keep d rational until the final correction rounding.
 * Every pass reads an immutable plane, replicates borders, rounds one exact
 * signed rational correction with ties away from zero, and clamps to12-bit.
 * Spatial processing precedes the actual2x2 UV average and8-bit conversion.
 * These post-color roles, scales and ordering are finite hypotheses. They do
 * not change pre-color WORK/history. All groups are mandatory; the edge
 * strength3..18 and limit5 family has no invented wire bypass.
 * After these passes, MAIN Y alone is multiplied by main_gain/256, rounded
 * to nearest and clamped to12-bit before byte quantization. The unsigned16
 * gain includes zero; no omitted-table identity or chroma gain is inferred.
 * The role and placement of this uniform 0x1900/0x1940 double buffer are
 * functional hypotheses.
 * Every supplied 16-bit sample must be in [0,4095]. Output buffers must not
 * overlap one another or input/history buffers. The caller supplies their
 * complete extents. Failure leaves all output buffers unchanged.
 *
 * A mandatory33-knot unsigned16 Q8 saturation curve scales the signed mixed
 * chroma after B/C/BAC and before adding2048 and clipping. Its index is clamped
 * Y12/128, with linear interpolation and nearest rounding. Y itself is unchanged.
 * Chroma products use signed64-bit arithmetic, nearest ties away from zero.
 * All-zero knots explicitly desaturate; the neutral synthetic array is256 at
 * every knot. No enable or omitted-array identity is inferred. Q8 gain has
 * producer arithmetic support; the Y axis, spacing and placement are functional
 * hypotheses. The later post-color neighborhood passes and NV12 averaging follow.
 *
 * Noise uses primitive degree-32 right-shift Galois feedback masks. Each main
 * job starts three states at1; enabled channels step once per emitted pixel,
 * in 2x2 block, dy, dx order. The post-step top11 bits minus1024 are clamped
 * to [-noise_negative,noise_positive] (both <=1023), multiplied by gain and
 * rounded by10 bits. Add offset, round by shift (<=15), then add/clamp RGB12.
 * These seed, order, slice and scalar meanings are functional hypotheses.
 */
/* Fixed stages for one owned frame. Inputs remain immutable and live until
 * free. Distinct tasks of the current stage may run concurrently, each exactly
 * once; the caller joins every task before advance/free. No worker schedules
 * other work. Results are private and available only after DONE.
 * PREFIX repeats twelve pass barriers (prefix_pass0..11): CFA gather, DPC,
 * spatial, filter, optional vertical refinement, denoise, detail, adaptive,
 * two color blocks, color tail and CFA scatter. MONO omits gather/scatter.
 * CFA has up to three row bands per active lattice (12 tasks); MONO up to four.
 * Every band reads the complete immutable previous plane, including neighbors
 * across band boundaries. Neutral passes/lanes do not exchange buffers.
 * COLOR has up to12 bands of complete two-row blocks. PREPARE records each
 * band's three exact sequential LFSR start states; tasks keep the original
 * 2x2-block/dy/dx traversal and never share mutable noise state. TEMPORAL,
 * RADIAL and PACK retain at most four bands; POST retains its own bound below.
 * POST repeats five pass barriers (post_pass0..4). Each pass has up to12 tasks:
 * up to four disjoint row bands per nonneutral channel, reading the immutable
 * previous pass. A zero-task neutral pass still requires advance. Only advance
 * swaps plane pointers; PACK reads each channel's final plane without a copy.
 */
typedef enum NikonPairedPixelStage {
    NIKON_PAIRED_PIXEL_PREPARE,
    NIKON_PAIRED_PIXEL_PREFIX,
    NIKON_PAIRED_PIXEL_TEMPORAL,
    NIKON_PAIRED_PIXEL_RADIAL,
    NIKON_PAIRED_PIXEL_COLOR,
    NIKON_PAIRED_PIXEL_POST,
    NIKON_PAIRED_PIXEL_PACK,
    NIKON_PAIRED_PIXEL_DONE,
} NikonPairedPixelStage;

typedef struct NikonPairedPixels NikonPairedPixels;
typedef struct NikonPairedPixelResult {
    const uint16_t *half_luma, *half_residual;
    const uint8_t *y, *uv, *work;
} NikonPairedPixelResult;

NikonPairedPixels *nikon_paired_pixels_new(const NikonPairedPixelConfig *config,
    const uint16_t *raw, const uint16_t *prior_luma,
    const uint16_t *prior_residual, const uint16_t *grid);
NikonPairedPixelStage nikon_paired_pixels_stage(const NikonPairedPixels *pixels);
/* Zero before PREFIX; 0..11 during PREFIX; 12 after its last barrier. */
unsigned nikon_paired_pixels_prefix_pass(const NikonPairedPixels *pixels);
/* Zero before POST; 0..4 during POST; 5 after its last barrier. */
unsigned nikon_paired_pixels_post_pass(const NikonPairedPixels *pixels);
unsigned nikon_paired_pixels_tasks(const NikonPairedPixels *pixels);
bool nikon_paired_pixels_run(NikonPairedPixels *pixels, unsigned task);
bool nikon_paired_pixels_advance(NikonPairedPixels *pixels);
const NikonPairedPixelResult *nikon_paired_pixels_result(
    const NikonPairedPixels *pixels);
void nikon_paired_pixels_free(NikonPairedPixels *pixels);

/* Explicit synchronous execution of the same fixed stages. */
bool nikon_paired_pixels(const NikonPairedPixelConfig *config,
                         const uint16_t *raw_contiguous_ROI,
                         const uint16_t *prior_half_luma_nullable,
                         const uint16_t *prior_half_residual_nullable,
                         uint16_t *out_half_luma,
                         uint16_t *out_half_residual,
                         const uint16_t *confidence_grid_nullable,
                         uint8_t *out_y, uint8_t *out_uv);

#endif
