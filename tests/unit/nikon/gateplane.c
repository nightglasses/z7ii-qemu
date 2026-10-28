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

#define BASE 0x10000000
#define ALIAS 0x20000000
#define READONLY 0x30000000
#define RAM_SIZE 0x20000
#define WIDTH 8
#define HEIGHT 6
#define SAMPLES (WIDTH * HEIGHT)

typedef struct Fixture {
    MemoryRegion ram, alias, readonly;
    uint8_t *bytes;
    uint32_t config[0x4080 / 4], slots[16][13];
    NikonGatePacket packet[8];
    NikonGateWorkCommand command[8];
    uint16_t table[6][1024], rgb[3 * SAMPLES];
    NikonGatePlaneWork work;
} Fixture;

static void set(NikonGatePacket *p, unsigned offset, uint32_t value)
{
    p->present[offset / 4] = true;
    p->value[offset / 4] = value;
}

static void zero(NikonGatePacket *p, unsigned offset, unsigned count)
{
    for (unsigned i = 0; i < count; i++) {
        set(p, offset + 4 * i, 0);
    }
}

static uint32_t get(const NikonGatePacket *p, unsigned offset)
{
    return p->value[offset / 4];
}

static void coefficient(NikonGatePacket *p, unsigned row, unsigned column,
                         int value)
{
    unsigned offset = 0x20 + row * 8 + (column / 2) * 4;
    unsigned shift = (column % 2) * 16;

    set(p, offset, (get(p, offset) & ~(0x7fffU << shift)) |
                  ((value & 0x7fffU) << shift));
}

static void matrices(Fixture *f)
{
    for (unsigned n = 0; n < 2; n++) {
        NikonGatePacket *p = &f->packet[3 + n];

        zero(p, 0, 1);
        zero(p, 0x20, 14);
        zero(p, 0x60, 4);
        zero(p, 0x80, 6);
        set(p, 0, n ? 0x200068 : 0x220021);
        set(p, 0x40, n ? 7 : 1);
        for (unsigned r = 0; r < 3; r++) {
            coefficient(p, r, r, n ? 512 + r * 256 : 4096);
            set(p, 0x60 + r * 4, n ? 0x70000fff : 0x40003fff);
        }
        coefficient(p, 3, 0, 128);
        coefficient(p, 3, 1, 256);
        coefficient(p, 3, 2, 512);
        set(p, 0x6c, 0x10000ff);
        if (n) {
            set(p, 0x4c, 0xb);
            set(p, 0x80, 1);
            set(p, 0x84, 0x11111111);
            set(p, 0x88, 0x32103112);
            set(p, 0x8c, 0x16b);
            set(p, 0x90, 0x076e07ba);
            set(p, 0x94, 0x011f0000);
        }
    }
}

static void window(NikonGatePacket *p)
{
    zero(p, 0, 9);
    zero(p, 0x40, 7);
    zero(p, 0x80, 16);
    set(p, 0x18, 0xff000000);
    set(p, 0x40, (WIDTH - 1) << 16);
    set(p, 0x44, (HEIGHT - 1) << 16);
    set(p, 0x48, WIDTH | (WIDTH << 16));
    set(p, 0x4c, 0x20002);
    set(p, 0x8c, 0xfff);
    set(p, 0x94, 0xfff);
    set(p, 0xa8, 0xff);
    set(p, 0xb0, 0xff);
}

static void select_phase(Fixture *f, unsigned bank, unsigned phase)
{
    unsigned map = bank ? 0x2300 : 0x2200;
    uint32_t input = BASE + 0x1000 + phase * 0x200;

    f->work.phase = phase;
    f->config[0x2004 / 4] = 0xc2 | bank | (phase << 4);
    f->config[(map + phase * 64 + 4) / 4] = input;
    f->config[(map + phase * 64 + 8) / 4] =
        BASE + 0x8000 + phase * 0x1000 + bank * 0x4000;
    for (unsigned c = 0; c < 3; c++) {
        f->work.input_address[c] = input + c * 0x2000;
    }
}

static Fixture *fixture(void)
{
    static const unsigned ports[] = {
        0x800, 0xd00, 0xa40, 0xa00, 0xa20, 0xdc0, 0x880, 0x9c0,
    };
    static const unsigned taps[] = {
        16, 24, 16, 24, 36, 24, 16, 24, 16,
        1, 4, 6, 4, 1, 4, 4, 6,
    };
    Fixture *f = g_new0(Fixture, 1);
    NikonGatePacket *p;

    memory_region_init_ram_flags_nomigrate(&f->ram, NULL, "gateplane-ram",
                                           RAM_SIZE, 0, &error_abort);
    memory_region_add_subregion(get_system_memory(), BASE, &f->ram);
    memory_region_init_alias(&f->alias, NULL, "gateplane-alias", &f->ram,
                              0, RAM_SIZE);
    memory_region_add_subregion(get_system_memory(), ALIAS, &f->alias);
    memory_region_init_ram_flags_nomigrate(&f->readonly, NULL,
                                           "gateplane-readonly", 0x8000,
                                           0, &error_abort);
    memory_region_set_readonly(&f->readonly, true);
    memory_region_add_subregion(get_system_memory(), READONLY, &f->readonly);
    f->bytes = memory_region_get_ram_ptr(&f->ram);
    memset(f->bytes, 0xa5, RAM_SIZE);
    f->work = (NikonGatePlaneWork) {
        .as = &address_space_memory, .config = f->config,
        .slots = (const uint32_t (*)[13])f->slots,
        .parameter_valid = 6, .commands = f->command, .command_count = 8,
        .table = (const uint16_t (*)[1024])f->table,
        .width = WIDTH, .height = HEIGHT, .rgb = f->rgb,
    };
    f->config[0x2000 / 4] = 0x131;
    f->config[0x2010 / 4] = 0x74011;
    f->config[0x2014 / 4] = 0x4100;
    f->config[0x2018 / 4] = 7;
    f->config[0x2640 / 4] = 0x202;
    f->config[0x2644 / 4] = HEIGHT;
    f->config[0x2648 / 4] = 3;
    f->config[0x264c / 4] = 2;
    f->config[0x2658 / 4] = 3;
    f->config[0x4040 / 4] = 0x10;
    f->config[0x4044 / 4] = WIDTH | (HEIGHT << 16);
    f->slots[1][0] = 0x20121;
    f->slots[2][0] = 0x10621;
    f->slots[1][1] = f->slots[2][1] = 0x15500;
    f->slots[1][8] = f->slots[2][8] = WIDTH | (HEIGHT << 16);
    for (unsigned c = 0; c < 3; c++) {
        f->slots[1][4 + c] = f->work.input_stride[c] = 32;
        f->slots[1][9 + c] = c * 0x2000;
    }
    f->slots[2][4] = f->slots[2][5] = 32;
    for (unsigned i = 0; i < 8; i++) {
        f->command[i].offset = ports[i];
        f->command[i].packet = &f->packet[i];
    }
    p = &f->packet[0];
    zero(p, 4, 1);
    zero(p, 0x10, 15);
    set(p, 4, 0x5a);
    set(p, 0x10, 0x10);
    set(p, 0x14, 0x10000);
    set(p, 0x18, 0x30004);
    set(p, 0x20, (WIDTH - 1) << 16);
    set(p, 0x24, (HEIGHT + 3) << 16);
    set(p, 0x28, WIDTH | (WIDTH << 16));
    set(p, 0x2c, 0x20006);
    set(p, 0x30, 2);
    set(p, 0x38, 1);
    p = &f->packet[1];
    zero(p, 0, 2);
    zero(p, 0x10, 13);
    set(p, 0, 0xffff0022);
    set(p, 4, 0x770);
    set(p, 0x18, 0x2000200);
    set(p, 0x1c, 512);
    set(p, 0x2c, 0x999);
    for (unsigned i = 0; i < 3; i++) {
        set(p, 0x30 + 4 * i, 0x3fff);
    }
    p = &f->packet[2];
    zero(p, 0, 6);
    set(p, 0, 0x5a0020);
    set(p, 4, 0xff00ff);
    set(p, 8, 0xff00ff);
    set(p, 0xc, 0xff00ff);
    set(p, 0x10, 1);
    set(p, 0x20, 0xe4b101c);
    for (unsigned i = 0; i < 6; i++) {
        unsigned base = 0x30 + i * 0x20;

        zero(p, base, i == 5 ? 4 : 5);
        set(p, base + 4, 0x3fff3fff);
        set(p, base + 8, 0x40004000);
        if (i == 1 || i == 2) {
            set(p, base + 12, (i == 1 ? 1U : 2U) << 29);
        }
        if (i < 5) {
            set(p, base + 16, i < 2 ? 0xe4b101c : 0x1c);
        }
    }
    matrices(f);
    window(&f->packet[5]);
    set(&f->packet[5], 0, 0x210000);
    set(&f->packet[5], 4, 0x50000);
    set(&f->packet[5], 8, 0x10400);
    set(&f->packet[5], 0x1c, 0x101);
    window(&f->packet[7]);
    set(&f->packet[7], 0, 0x40000);
    set(&f->packet[7], 8, 0x10002);
    f->command[7].auxiliary[0] = 0x74011;
    f->command[7].auxiliary[1] = 0x4100;
    p = &f->packet[6];
    zero(p, 0, 21);
    zero(p, 0x64, 4);
    zero(p, 0x80, 9);
    zero(p, 0xc0, 9);
    zero(p, 0x100, 9);
    zero(p, 0x22c, 4);
    set(p, 0, 0x6912);
    set(p, 4, 4);
    set(p, 8, 0x440012);
    set(p, 0xc, 0x10000);
    set(p, 0x10, (WIDTH - 1) << 16);
    set(p, 0x14, (HEIGHT - 1) << 16);
    set(p, 0x18, WIDTH | (WIDTH << 16));
    set(p, 0x1c, 0x20002);
    set(p, 0x20, 0xfffff084);
    set(p, 0x24, 0x07770774);
    for (unsigned i = 0x28; i <= 0x3c; i += 4) {
        set(p, i, 0xfff);
    }
    for (unsigned i = 0; i < 3; i++) {
        set(p, 0x68 + i * 4, 8);
    }
    for (unsigned i = 0; i < 17; i++) {
        unsigned offset = 0x80 + (i / 2) * 4;

        set(p, offset, get(p, offset) | (taps[i] << ((i % 2) * 16)));
    }
    set(p, 0x22c, 0xc);
    set(p, 0x234, 1);
    for (unsigned c = 0; c < 3; c++) {
        for (unsigned i = 0; i < 1024; i++) {
            f->table[2 * c][i] = i * 3 + c * 17;
            f->table[2 * c + 1][i] = i * 3 + c * 17 + 2;
        }
        for (unsigned i = 0; i < SAMPLES; i++) {
            f->rgb[c * SAMPLES + i] = 700 + i * 57 + c * 900;
        }
    }
    /* Accept full u16 storage; the gain stage performs the 14-bit clamp. */
    f->rgb[0] = 51348;
    select_phase(f, 0, 0);
    return f;
}

static void cleanup(Fixture *f)
{
    memory_region_del_subregion(get_system_memory(), &f->readonly);
    memory_region_del_subregion(get_system_memory(), &f->alias);
    memory_region_del_subregion(get_system_memory(), &f->ram);
    object_unparent(OBJECT(&f->readonly));
    object_unparent(OBJECT(&f->alias));
    object_unparent(OBJECT(&f->ram));
    g_free(f);
}

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
