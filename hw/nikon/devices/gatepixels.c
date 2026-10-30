/*
 * Functional type9 four-corner NV12 warp, not recovered physical calibration.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "hw/nikon/gatepixels.h"

static bool gate_pixels_overlap(const void *a, size_t an,
                                const void *b, size_t bn)
{
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;

    return x > UINTPTR_MAX - an || y > UINTPTR_MAX - bn ||
           (x < y + bn && y < x + an);
}

static int32_t gate_pixels_round(int64_t sum, unsigned shift)
{
    uint64_t magnitude = sum < 0 ? 0U - (uint64_t)sum : (uint64_t)sum;
    int32_t rounded = (magnitude + (UINT64_C(1) << (shift - 1))) >> shift;

    /* The validated signed23 corners keep the rounded result in signed23. */
    return sum < 0 ? -rounded : rounded;
}

static uint8_t gate_pixels_sample(const uint8_t *plane, unsigned width,
                                  unsigned height, unsigned stride,
                                  unsigned step, unsigned component,
                                  int32_t x, int32_t y, unsigned bits)
{
    unsigned unit = 1U << bits;
    unsigned ix, iy, fx, fy, nx, ny;
    uint64_t sum;

    x = CLAMP(x, 0, (int32_t)((width - 1) * unit));
    y = CLAMP(y, 0, (int32_t)((height - 1) * unit));
    ix = x >> bits;
    iy = y >> bits;
    fx = x & (unit - 1);
    fy = y & (unit - 1);
    nx = MIN(ix + 1, width - 1);
    ny = MIN(iy + 1, height - 1);
    sum = (uint64_t)plane[iy * stride + ix * step + component] *
          (unit - fx) * (unit - fy) +
          (uint64_t)plane[iy * stride + nx * step + component] *
          fx * (unit - fy) +
          (uint64_t)plane[ny * stride + ix * step + component] *
          (unit - fx) * fy +
          (uint64_t)plane[ny * stride + nx * step + component] * fx * fy;
    return (sum + (UINT64_C(1) << (2 * bits - 1))) >> (2 * bits);
}

bool nikon_gate_pixels_nv12(const NikonGatePixels *p, uint8_t *output,
                            size_t output_size)
{
    size_t input_y, output_y, input_bytes, output_bytes, grid_bytes;
    unsigned tail_exponent = 3;

    if (!p || !p->input || !p->records || !output ||
        !p->src_width || !p->src_height ||
        !p->dst_width || !p->dst_height ||
        ((p->src_width | p->src_height | p->dst_width | p->dst_height) & 1) ||
        p->src_width > 4096 || p->src_height > 4096 ||
        p->dst_width > 4096 || p->dst_height > 4096 ||
        !p->columns || p->columns > 64 || !p->rows || p->rows > 512 ||
        p->columns != DIV_ROUND_UP(p->dst_width, 64) ||
        p->dst_height > p->rows * 8) {
        return false;
    }
    input_y = (size_t)p->src_width * p->src_height;
    output_y = (size_t)p->dst_width * p->dst_height;
    input_bytes = input_y + input_y / 2;
    output_bytes = output_y + output_y / 2;
    grid_bytes = (size_t)p->columns * p->rows * 40;
    while ((1U << tail_exponent) < p->dst_width - (p->columns - 1) * 64) {
        tail_exponent++;
    }
    if (p->input_size != input_bytes || output_size != output_bytes ||
        p->records_size < grid_bytes ||
        gate_pixels_overlap(output, output_bytes, p->input, input_bytes) ||
        gate_pixels_overlap(output, output_bytes, p->records, grid_bytes)) {
        return false;
    }
    for (size_t offset = 0; offset < grid_bytes; offset += 40) {
        unsigned column = (offset / 40) % p->columns;
        unsigned exponent = column + 1 == p->columns ? tail_exponent : 6;

        if (ldl_le_p(p->records + offset + 32) != (0x300 | exponent)) {
            return false;
        }
        for (unsigned i = 0; i < 8; i++) {
            if (ldl_le_p(p->records + offset + 4 * i) & 0xff800000U) {
                return false;
            }
        }
    }
    for (unsigned top = 0; top < p->dst_height; top += 8) {
        for (unsigned left = 0; left < p->dst_width; left += 64) {
            const uint8_t *record = p->records +
                40 * ((size_t)(top / 8) * p->columns + left / 64);
            unsigned exponent = ldl_le_p(record + 32) & 15;
            unsigned width = 1U << exponent;
            unsigned rows = MIN(8, p->dst_height - top);
            unsigned columns = MIN(64, p->dst_width - left);
            int64_t base[2], step[2], base_dy[2], step_dy[2];

            for (unsigned axis = 0; axis < 2; axis++) {
                int32_t c[4];

                for (unsigned corner = 0; corner < 4; corner++) {
                    uint32_t word = ldl_le_p(record + corner * 8 + axis * 4);

                    c[corner] = (int32_t)(word ^ 0x400000) - 0x400000;
                }
                /*
                 * The original four-corner numerator is affine in x per row:
                 * W*(c0*(8-y)+c2*y) + x*((c1-c0)*(8-y)+(c3-c2)*y).
                 * Recur only this exact integer numerator; round once at each
                 * sample, with the same half-away rule and divisor W*8.
                 * Advancing y adds W*(c2-c0) to its base and
                 * (c3-c2)-(c1-c0) to its x increment, without any rounding.
                 * Nonnegative weights sum to W*8: signed23 corners and W<=64
                 * bound the numerator magnitude by 2^31. Base and increment
                 * differences are below 2^29 and 2^24; all use int64_t.
                 */
                base[axis] = (int64_t)width * c[0] * 8;
                step[axis] = (int64_t)(c[1] - c[0]) * 8;
                base_dy[axis] = (int64_t)width * (c[2] - c[0]);
                step_dy[axis] = (c[3] - c[2]) - (c[1] - c[0]);
            }
            for (unsigned row = 0; row < rows; row++) {
                unsigned y = top + row;
                int64_t nx = base[0], ny = base[1];

                for (unsigned column = 0; column < columns; column++) {
                    unsigned x = left + column;
                    int32_t sx = gate_pixels_round(nx, exponent + 3);
                    int32_t sy = gate_pixels_round(ny, exponent + 3);

                    output[(size_t)y * p->dst_width + x] = gate_pixels_sample(
                        p->input, p->src_width, p->src_height, p->src_width,
                        1, 0, sx, sy, 8);
                    if (!((x | y) & 1)) {
                        for (unsigned component = 0; component < 2;
                             component++) {
                            output[output_y + (size_t)(y / 2) * p->dst_width +
                                   x + component] = gate_pixels_sample(
                                p->input + input_y, p->src_width / 2,
                                p->src_height / 2, p->src_width, 2, component,
                                sx, sy, 9);
                        }
                    }
                    nx += step[0];
                    ny += step[1];
                }
                for (unsigned axis = 0; axis < 2; axis++) {
                    base[axis] += base_dy[axis];
                    step[axis] += step_dy[axis];
                }
            }
        }
    }
    return true;
}
