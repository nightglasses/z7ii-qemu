/*
 * Synthetic unit8 graph with an independent scalar confidence-map oracle.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/bswap.h"
#include "system/address-spaces.h"
#include "system/system.h"
#include "hw/nikon/gateplane.h"

int (*qemu_main)(void);

#include "gridfixture.h"

static int64_t rounded(int64_t value, unsigned shift)
{
    int64_t divisor = INT64_C(1) << shift;

    return value < 0 ? -((-value + divisor / 2) / divisor) :
                       (value + divisor / 2) / divisor;
}

static int sign(unsigned value, unsigned bits)
{
    unsigned high = 1U << (bits - 1);

    value &= (1U << bits) - 1;
    return (int)(value ^ high) - (int)high;
}

static void oracle_matrix(const NikonGatePacket *p, const int32_t *input,
                           int32_t *output)
{
    for (unsigned r = 0; r < 4; r++) {
        uint32_t bounds = get(p, 0x60 + r * 4);
        int lower = sign(bounds >> 16, r == 3 ? 9 : 15);
        int upper = r == 3 ? sign(bounds, 9) : bounds & 0x7fff;

        for (unsigned i = 0; i < SAMPLES; i++) {
            int64_t sum = 0;

            for (unsigned c = 0; c < 3; c++) {
                unsigned packed = get(p, 0x20 + 8 * r + (c / 2) * 4);

                sum += (int64_t)sign(packed >> ((c % 2) * 16), 15) *
                       input[c * SAMPLES + i];
            }
            output[r * SAMPLES + i] = CLAMP(rounded(sum, 12), lower, upper);
        }
    }
}

static void oracle_filter(const NikonGatePacket *p, const int32_t *input,
                           int32_t *output)
{
    /* Explicit coordinates independently expand the seventeen stored taps. */
    static const unsigned x[] = { 1, 2, 3, 1, 2, 3, 1, 2, 3,
                                  0, 1, 2, 3, 4, 0, 4, 0 };
    static const unsigned y[] = { 1, 1, 1, 2, 2, 2, 3, 3, 3,
                                  0, 0, 0, 0, 0, 1, 1, 2 };

    for (unsigned row = 0; row < HEIGHT; row++) {
        for (unsigned col = 0; col < WIDTH; col++) {
            int64_t sum = 0;

            for (unsigned t = 0; t < 17; t++) {
                unsigned tap = (get(p, 0x80 + (t / 2) * 4) >>
                                ((t % 2) * 16)) & 0xffff;

                for (unsigned mirror = 0; mirror < (t < 9 ? 1 : 2); mirror++) {
                    int dx = (int)(mirror ? 4 - x[t] : x[t]) - 2;
                    int dy = (int)(mirror ? 4 - y[t] : y[t]) - 2;
                    unsigned sx = CLAMP((int)col + dx, 0, WIDTH - 1);
                    unsigned sy = CLAMP((int)row + dy, 0, HEIGHT - 1);

                    sum += (int64_t)tap * input[sy * WIDTH + sx];
                }
            }
            output[row * WIDTH + col] = CLAMP(rounded(sum, get(p, 0x68)),
                                              0, 4095);
        }
    }
}

static void oracle(const Fixture *f, uint16_t output[SAMPLES])
{
    int32_t input[3 * SAMPLES], features[4 * SAMPLES];
    int32_t confidence[SAMPLES], filtered[SAMPLES], blend[SAMPLES];
    const NikonGatePacket *gain = &f->packet[1];

    for (unsigned c = 0; c < 3; c++) {
        unsigned multiplier = c == 2 ? get(gain, 0x1c) :
            (get(gain, 0x18) >> (16 * c)) & 0xffff;
        unsigned shift = (get(gain, 0x2c) >> (4 * c)) & 15;

        for (unsigned i = 0; i < SAMPLES; i++) {
            unsigned value = CLAMP(rounded((int64_t)f->rgb[c * SAMPLES + i] *
                                            multiplier, shift),
                                   0, get(gain, 0x30 + c * 4));
            unsigned knot = value / 8, fraction = value % 8;
            unsigned index = knot / 2, parity = knot % 2;
            unsigned a = f->table[c * 2 + parity][index];
            unsigned b = parity ? (index == 1023 ? a :
                f->table[c * 2][index + 1]) : f->table[c * 2 + 1][index];

            input[c * SAMPLES + i] =
                (a * (8 - fraction) + b * fraction + 4) / 8;
        }
    }
    oracle_matrix(&f->packet[3], input, features);
    memcpy(input, features, sizeof(input));
    oracle_matrix(&f->packet[4], input, features);
    for (unsigned i = 0; i < SAMPLES; i++) {
        unsigned sum = 0;

        for (unsigned r = 0; r < 4; r++) {
            unsigned bounds = get(&f->packet[4], 0x60 + r * 4);
            int lower = sign(bounds >> 16, r == 3 ? 9 : 15);
            int upper = r == 3 ? sign(bounds, 9) : bounds & 0x7fff;
            unsigned norm = MAX(abs(lower), abs(upper));

            sum += norm ? MIN((abs(features[r * SAMPLES + i]) * 4095U +
                               norm / 2) / norm, 4095U) : 0;
        }
        confidence[i] = (sum + 2) / 4;
    }
    oracle_filter(&f->packet[6], confidence, filtered);
    for (unsigned i = 0; i < SAMPLES; i++) {
        blend[i] = (confidence[i] + filtered[i] + 1) / 2;
    }
    oracle_filter(&f->packet[6], blend, filtered);
    for (unsigned i = 0; i < SAMPLES; i++) {
        output[i] = filtered[i];
    }
}

static void compare(Fixture *f)
{
    NikonGatePlaneOutput out = { 0 };
    uint16_t expected[SAMPLES];

    oracle(f, expected);
    g_assert_cmpint(nikon_gate_plane_execute(&f->work, &out), ==,
                    NIKON_GATE_WORK_OK);
    g_assert_cmpuint(out.width, ==, WIDTH);
    g_assert_cmpuint(out.height, ==, HEIGHT);
    g_assert_cmpuint(out.stride, ==, 32);
    for (unsigned y = 0; y < HEIGHT; y++) {
        const uint8_t *row = f->bytes + out.address - BASE + y * out.stride;

        for (unsigned x = 0; x < WIDTH; x++) {
            unsigned offset = (x / 2) * 3;
            unsigned actual = x & 1 ? (row[offset + 1] >> 4) |
                (row[offset + 2] << 4) : row[offset] |
                ((row[offset + 1] & 15) << 8);

            g_assert_cmpuint(actual, ==, expected[y * WIDTH + x]);
        }
        for (unsigned x = WIDTH * 3 / 2; x < out.stride; x++) {
            g_assert_cmpuint(row[x], ==, 0xa5);
        }
    }
}

static void test_phases(void)
{
    Fixture *f = fixture();

    for (unsigned bank = 0; bank < 2; bank++) {
        for (unsigned phase = 0; phase < 4; phase++) {
            select_phase(f, bank, phase);
            compare(f);
        }
    }
    cleanup(f);
}

static void test_changes(void)
{
    Fixture *f = fixture();
    uint8_t previous[HEIGHT * 32];

    compare(f);
    memcpy(previous, f->bytes + 0x8000, sizeof(previous));
    for (unsigned bank = 0; bank < 6; bank++) {
        for (unsigned i = 0; i < 1024; i++) {
            f->table[bank][i] /= 2;
        }
    }
    compare(f);
    g_assert_cmpint(memcmp(previous, f->bytes + 0x8000, sizeof(previous)),
                    !=, 0);
    memcpy(previous, f->bytes + 0x8000, sizeof(previous));
    coefficient(&f->packet[4], 3, 2, -512);
    compare(f);
    g_assert_cmpint(memcmp(previous, f->bytes + 0x8000, sizeof(previous)),
                    !=, 0);
    memcpy(previous, f->bytes + 0x8000, sizeof(previous));
    set(&f->packet[6], 0x90, get(&f->packet[6], 0x90) + 50);
    compare(f);
    g_assert_cmpint(memcmp(previous, f->bytes + 0x8000, sizeof(previous)),
                    !=, 0);
    cleanup(f);
}

static void rejected(Fixture *f, NikonGateWorkResult reason)
{
    g_autofree uint8_t *before = g_memdup2(f->bytes, RAM_SIZE);
    NikonGatePlaneOutput out = { .address = 0xfeed1234 };

    g_assert_cmpint(nikon_gate_plane_execute(&f->work, &out), ==, reason);
    g_assert_cmpmem(before, RAM_SIZE, f->bytes, RAM_SIZE);
    g_assert_cmpuint(out.address, ==, 0xfeed1234);
}

static void test_preflight(void)
{
    Fixture *f = fixture();

    set(&f->packet[4], 0x94, 0);
    rejected(f, NIKON_GATE_WORK_GRID);
    set(&f->packet[4], 0x94, 0x11f0000);
    f->table[5][1023] = 0x4000;
    rejected(f, NIKON_GATE_WORK_GRID);
    f->table[5][1023] = 100;
    f->packet[7].present[0xbc / 4] = false;
    rejected(f, NIKON_GATE_WORK_GRID);
    f->packet[7].present[0xbc / 4] = true;
    f->config[(0x2200 + 8) / 4] = BASE + RAM_SIZE - 32;
    rejected(f, NIKON_GATE_WORK_MEMORY);
    f->config[(0x2200 + 8) / 4] = ALIAS + 0x1000;
    rejected(f, NIKON_GATE_WORK_ALIAS);
    f->config[(0x2200 + 8) / 4] = BASE + 0x8000;
    f->work.rgb = (uint16_t *)(f->bytes + 0x8000);
    rejected(f, NIKON_GATE_WORK_ALIAS);
    f->work.rgb = f->rgb;
    f->work.input_address[2]++;
    rejected(f, NIKON_GATE_WORK_FORMAT);
    f->work.input_address[2]--;
    f->config[(0x2200 + 8) / 4] = READONLY;
    rejected(f, NIKON_GATE_WORK_MEMORY);
    f->config[(0x2200 + 8) / 4] = BASE + 0x8000;
    f->config[(0x2200 + 4) / 4] = BASE + RAM_SIZE - 32;
    for (unsigned c = 0; c < 3; c++) {
        f->work.input_address[c] = BASE + RAM_SIZE - 32 + c * 0x2000;
    }
    rejected(f, NIKON_GATE_WORK_MEMORY);
    select_phase(f, 0, 0);
    f->command[7].offset = 0x800;
    rejected(f, NIKON_GATE_WORK_TOPOLOGY);
    cleanup(f);
}

static void test_readonly(void)
{
    Fixture *f = fixture();

    f->config[(0x2200 + 4) / 4] = READONLY;
    for (unsigned c = 0; c < 3; c++) {
        f->work.input_address[c] = READONLY + c * 0x2000;
    }
    compare(f);
    cleanup(f);
}

static bool refuse(void *opaque, const NikonGatePlaneOutput *output)
{
    unsigned *calls = opaque;

    (*calls)++;
    g_assert_cmpuint(output->address, ==, BASE + 0x8000);
    g_assert_cmpuint(output->width, ==, WIDTH);
    g_assert_cmpuint(output->height, ==, HEIGHT);
    return false;
}

static void test_publication(void)
{
    Fixture *f = fixture();
    unsigned calls = 0;

    f->work.begin = refuse;
    f->work.opaque = &calls;
    set(&f->packet[4], 0x94, 0);
    rejected(f, NIKON_GATE_WORK_GRID);
    g_assert_cmpuint(calls, ==, 0);
    set(&f->packet[4], 0x94, 0x11f0000);
    rejected(f, NIKON_GATE_WORK_CONFIG);
    g_assert_cmpuint(calls, ==, 1);
    f->work.begin = NULL;
    compare(f);
    cleanup(f);
}

int main(int argc, char **argv)
{
    g_autofree char *directory = g_dir_make_tmp("nikonXXXXXX", NULL);
    g_autofree char *socket = g_build_filename(directory, "qtest", NULL);
    g_autofree char *chardev =
        g_strdup_printf("unix:%s,server=on,wait=off", socket);
    const char *const arguments[] = {
        argv[0], "-machine", "none", "-accel", "qtest", "-display", "none",
        "-nodefaults", "-qtest", chardev, NULL,
    };
    g_auto(GStrv) qemu_argv = g_new0(char *, G_N_ELEMENTS(arguments));
    int result;

    g_assert_nonnull(directory);
    g_test_init(&argc, &argv, NULL);
    for (unsigned i = 0; i < G_N_ELEMENTS(arguments) - 1; i++) {
        qemu_argv[i] = g_strdup(arguments[i]);
    }
    qemu_init(G_N_ELEMENTS(arguments) - 1, qemu_argv);
    g_test_add_func("/nikon/gateplane/phases-padding", test_phases);
    g_test_add_func("/nikon/gateplane/numerical-inputs", test_changes);
    g_test_add_func("/nikon/gateplane/preflight", test_preflight);
    g_test_add_func("/nikon/gateplane/readonly-input", test_readonly);
    g_test_add_func("/nikon/gateplane/publication-guard", test_publication);
    result = g_test_run();
    qemu_cleanup(result);
    unlink(socket);
    rmdir(directory);
    return result;
}
