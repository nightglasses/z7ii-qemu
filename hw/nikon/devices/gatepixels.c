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

static int32_t gate_pixels_coordinate(const uint8_t *record, unsigned x,
                                      unsigned y, unsigned axis)
{
    unsigned width = 1U << (ldl_le_p(record + 32) & 15);
    unsigned divisor = width * 8;
    int64_t sum = 0;

    for (unsigned corner = 0; corner < 4; corner++) {
        uint32_t word = ldl_le_p(record + 8 * corner + 4 * axis);
        int32_t value = (int32_t)(word ^ 0x400000) - 0x400000;
        unsigned weight = (corner & 1 ? x : width - x) *
                          (corner & 2 ? y : 8 - y);

        sum += (int64_t)value * weight;
    }
    return sum < 0 ? -((-sum + divisor / 2) / divisor) :
                    (sum + divisor / 2) / divisor;
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
    for (unsigned y = 0; y < p->dst_height; y++) {
        for (unsigned x = 0; x < p->dst_width; x++) {
            const uint8_t *record = p->records +
                40 * ((size_t)(y / 8) * p->columns + x / 64);
            int32_t sx = gate_pixels_coordinate(record, x % 64, y % 8, 0);
            int32_t sy = gate_pixels_coordinate(record, x % 64, y % 8, 1);

            output[(size_t)y * p->dst_width + x] = gate_pixels_sample(
                p->input, p->src_width, p->src_height, p->src_width,
                1, 0, sx, sy, 8);
            if (!((x | y) & 1)) {
                for (unsigned component = 0; component < 2; component++) {
                    output[output_y + (size_t)(y / 2) * p->dst_width +
                           x + component] = gate_pixels_sample(
                        p->input + input_y, p->src_width / 2,
                        p->src_height / 2, p->src_width, 2, component,
                        sx, sy, 9);
                }
            }
        }
    }
    return true;
}
