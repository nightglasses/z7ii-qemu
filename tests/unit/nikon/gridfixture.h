/* Private synthetic confidence-grid fixture shared by native tests.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef TEST_NIKON_GRID_FIXTURE_H
#define TEST_NIKON_GRID_FIXTURE_H

#include "qemu/rcu.h"

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
    set(&f->packet[5], 0xa8, 0xff);
    set(&f->packet[5], 0xb0, 0xff);
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
    /* FlatView retirement may still access these ownerless embedded regions. */
    drain_call_rcu();
    object_unparent(OBJECT(&f->readonly));
    object_unparent(OBJECT(&f->alias));
    object_unparent(OBJECT(&f->ram));
    g_free(f);
}

#endif
