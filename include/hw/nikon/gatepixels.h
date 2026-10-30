/*
 * Bounded type9 NV12 grid warp; numerical choices are functional hypotheses.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef NIKON_GATE_PIXELS_H
#define NIKON_GATE_PIXELS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct NikonGatePixels {
    const uint8_t *input;
    size_t input_size;
    unsigned src_width, src_height;
    const uint8_t *records;
    size_t records_size;
    unsigned columns, rows;
    unsigned dst_width, dst_height;
} NikonGatePixels;

/*
 * Input/output are tight NV12 with even dimensions in [2,4096]. Each 40-byte
 * LE record contains TL/TR/BL/BR signed23 Q8 coordinate pairs, a tag at
 * byte32, and an uninterpreted final word. Columns start every64 pixels; rows
 * cover8 pixels. Non-final columns use tag0x306. The final column uses tag
 * 0x300|e, e=ceil(log2(max(8,remaining_width))), with e in[3,6]. Its coordinate
 * interpolation denominator is2^e, including extrapolation past a partial tail.
 * columns must equal ceil(dst_width/64); rows must cover dst_height/8.
 * The complete grid is validated before any output store. Output cannot alias
 * input or records. Image lengths must match exactly; false leaves output
 * untouched. The record buffer may contain additional unreferenced bytes.
 * Corner and pixel interpolation round to nearest, ties away from zero. Edges
 * replicate. UV is co-sited at even destination coordinates and uses Q9 source
 * coordinates, retaining the half-pixel fraction when dividing source by two.
 * These sampling, rounding and edge rules are explicit numerical hypotheses.
 */
bool nikon_gate_pixels_nv12(const NikonGatePixels *p, uint8_t *output,
                            size_t output_size);

#endif
