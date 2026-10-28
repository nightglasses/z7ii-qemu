/*
 * Functional RGB-grid gain, staggered LUT, linear features and spatial filter.
 * Packet decoding, RAM, scheduling and completion remain the caller's concern.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "hw/nikon/gategrid.h"

static bool grid_size(size_t samples, unsigned channels, size_t count)
{
    return samples && samples <= 4096u * 4096u &&
           count == samples * channels;
}

static int64_t grid_round(int64_t value, unsigned shift)
{
    int64_t half;

    if (!shift) {
        return value;
    }
    half = INT64_C(1) << (shift - 1);
    return value < 0 ? -((-value + half) >> shift) :
                       (value + half) >> shift;
}

bool nikon_gate_grid_gain3(const NikonGateGridGain gain[3],
                           const uint16_t *input, size_t samples,
                           uint16_t *output, size_t output_count)
{
    g_autofree uint16_t *data = NULL;

    if (!gain || !input || !output || !grid_size(samples, 3, output_count)) {
        return false;
    }
    for (unsigned c = 0; c < 3; c++) {
        if (gain[c].gain > 65535 || gain[c].offset < -65535 ||
            gain[c].offset > 65535 || gain[c].shift > 31 ||
            gain[c].lower > gain[c].upper) {
            return false;
        }
    }
    data = g_new(uint16_t, output_count);
    for (unsigned c = 0; c < 3; c++) {
        for (size_t i = 0; i < samples; i++) {
            int64_t value = (int64_t)input[c * samples + i] + gain[c].offset;

            value = grid_round(value * gain[c].gain, gain[c].shift);
            data[c * samples + i] = CLAMP(value, gain[c].lower, gain[c].upper);
        }
    }
    memcpy(output, data, output_count * sizeof(*output));
    return true;
}

bool nikon_gate_grid_lut3(const uint16_t bank[6][1024],
                          const uint16_t *input, size_t samples,
                          uint16_t *output, size_t output_count)
{
    g_autofree uint16_t *data = NULL;

    if (!bank || !input || !output || !grid_size(samples, 3, output_count)) {
        return false;
    }
    for (size_t i = 0; i < output_count; i++) {
        if (input[i] > 16383) {
            return false;
        }
    }
    data = g_new(uint16_t, output_count);
    for (unsigned c = 0; c < 3; c++) {
        for (size_t i = 0; i < samples; i++) {
            unsigned value = input[c * samples + i];
            unsigned index = value >> 4, fraction = value & 7;
            unsigned a, b;

            if (value & 8) {
                a = bank[2 * c + 1][index];
                b = index == 1023 ? a : bank[2 * c][index + 1];
            } else {
                a = bank[2 * c][index];
                b = bank[2 * c + 1][index];
            }
            data[c * samples + i] = (a * (8 - fraction) + b * fraction + 4) / 8;
        }
    }
    memcpy(output, data, output_count * sizeof(*output));
    return true;
}

bool nikon_gate_grid_matrix4(const NikonGateGridMatrix *matrix,
                             const uint16_t *input, size_t samples,
                             int32_t *output, size_t output_count)
{
    g_autofree int32_t *data = NULL;

    if (!matrix || !input || !output ||
        !grid_size(samples, 4, output_count) ||
        matrix->constant < -65535 || matrix->constant > 65535) {
        return false;
    }
    for (unsigned r = 0; r < 4; r++) {
        const NikonGateGridRow *row = &matrix->row[r];

        if (row->shift > 31 || row->lower > row->upper) {
            return false;
        }
        for (unsigned c = 0; c < 4; c++) {
            if (row->coefficient[c] < -32768 || row->coefficient[c] > 32767) {
                return false;
            }
        }
    }
    /* Four bounded products sum to less than 2^34, including the constant. */
    data = g_new(int32_t, output_count);
    for (unsigned r = 0; r < 4; r++) {
        const NikonGateGridRow *row = &matrix->row[r];

        for (size_t i = 0; i < samples; i++) {
            int64_t sum = (int64_t)row->coefficient[3] * matrix->constant;

            for (unsigned c = 0; c < 3; c++) {
                sum += (int64_t)row->coefficient[c] * input[c * samples + i];
            }
            sum = grid_round(sum, row->shift);
            data[r * samples + i] = CLAMP(sum, row->lower, row->upper);
        }
    }
    memcpy(output, data, output_count * sizeof(*output));
    return true;
}

bool nikon_gate_grid_filter(const NikonGateGridFilter *filter,
                            const int32_t *input, unsigned channels,
                            unsigned width, unsigned height,
                            int32_t *output, size_t output_count)
{
    uint16_t kernel[5][5] = { 0 };
    g_autofree int32_t *data = NULL;
    size_t samples = (size_t)width * height;

    if (!filter || !input || !output || !channels || channels > 4 ||
        !width || width > 4096 || !height || height > 4096 ||
        !grid_size(samples, channels, output_count) ||
        filter->shift > 31 || filter->lower > filter->upper) {
        return false;
    }
    for (unsigned y = 0; y < 3; y++) {
        for (unsigned x = 0; x < 3; x++) {
            kernel[y + 1][x + 1] = filter->tap[y * 3 + x];
        }
    }
    for (unsigned i = 0; i < 8; i++) {
        unsigned x = i < 5 ? i : i == 6 ? 4 : 0;
        unsigned y = i < 5 ? 0 : i < 7 ? 1 : 2;

        kernel[y][x] = kernel[4 - y][4 - x] = filter->tap[9 + i];
    }
    /* Even 25 * UINT16_MAX * INT32_MIN fits safely in signed 64-bit arithmetic. */
    data = g_new(int32_t, output_count);
    for (unsigned c = 0; c < channels; c++) {
        for (unsigned y = 0; y < height; y++) {
            for (unsigned x = 0; x < width; x++) {
                int64_t sum = 0;

                for (unsigned ky = 0; ky < 5; ky++) {
                    unsigned sy = CLAMP((int)y + (int)ky - 2,
                                        0, (int)height - 1);

                    for (unsigned kx = 0; kx < 5; kx++) {
                        unsigned sx = CLAMP((int)x + (int)kx - 2,
                                            0, (int)width - 1);

                        sum += (int64_t)kernel[ky][kx] *
                               input[c * samples + sy * width + sx];
                    }
                }
                sum = grid_round(sum, filter->shift);
                data[c * samples + y * width + x] =
                    CLAMP(sum, filter->lower, filter->upper);
            }
        }
    }
    memcpy(output, data, output_count * sizeof(*output));
    return true;
}
