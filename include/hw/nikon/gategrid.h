/*
 * Pure bounded RGB-grid reference primitives; no device or event ownership.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef NIKON_GATE_GRID_H
#define NIKON_GATE_GRID_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct NikonGateGridGain {
    uint32_t gain;                     /* 0..65535 */
    int32_t offset;                    /* -65535..65535, before gain */
    unsigned shift;                    /* 0..31 */
    uint16_t lower, upper;
} NikonGateGridGain;

typedef struct NikonGateGridRow {
    int32_t coefficient[4];            /* Each -32768..32767 */
    unsigned shift;                    /* 0..31 */
    int32_t lower, upper;
} NikonGateGridRow;

typedef struct NikonGateGridMatrix {
    NikonGateGridRow row[4];
    int32_t constant;                  /* -65535..65535 */
} NikonGateGridMatrix;

typedef struct NikonGateGridFilter {
    uint16_t tap[17];
    unsigned shift;                    /* 0..31; normalized Gaussian uses 11 */
    int32_t lower, upper;
} NikonGateGridFilter;

/*
 * All arrays are tight, plane-major, host-endian numerical samples. The caller
 * decodes guest LE16 and handles guest pitches. samples is one plane's length,
 * in [1,4096*4096]; output_count is in elements and must match exactly.
 * Gain, LUT and matrix inputs have three planes; the matrix returns four.
 * Every public output is copied from private scratch only after all validation
 * and arithmetic succeeds. Input, parameters, tables and output may overlap.
 * A false return leaves every output byte unchanged.
 *
 * These gain/offset order, rounding, LUT interpolation, filter tap order and
 * border rules are explicit functional hypotheses, not recovered DSP laws.
 */

/* Three channels: clamp(round_away((input + offset) * gain / 2^shift)). */
bool nikon_gate_grid_gain3(const NikonGateGridGain gain[3],
                           const uint16_t *input, size_t samples,
                           uint16_t *output, size_t output_count);

/*
 * Three channels, six 1024-entry banks in channel/parity order. Input is 14-bit.
 * A[i], B[i] are knots at 16*i and 16*i+8. Interpolate with denominator 8,
 * nearest rounding with ties upward. Beyond the final B knot, clamp to it.
 */
bool nikon_gate_grid_lut3(const uint16_t bank[6][1024],
                          const uint16_t *input, size_t samples,
                          uint16_t *output, size_t output_count);

/* Four signed rows applied to [R,G,B,constant]; signed ties round away. */
bool nikon_gate_grid_matrix4(const NikonGateGridMatrix *matrix,
                             const uint16_t *input, size_t samples,
                             int32_t *output, size_t output_count);

/*
 * One to four signed planes, width/height in [1,4096]. First nine taps are the
 * inner 3x3 in row order; next five are the outer top row; last three are row1
 * left, row1 right, row2 left. Reflect these outer eight through 180 degrees.
 * Replicate edges, accumulate signed products, round ties away, then clamp.
 */
bool nikon_gate_grid_filter(const NikonGateGridFilter *filter,
                            const int32_t *input, unsigned channels,
                            unsigned width, unsigned height,
                            int32_t *output, size_t output_count);

#endif
