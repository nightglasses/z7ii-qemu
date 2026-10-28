/*
 * Generated graph and independent integer pixel oracle, not factory data.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/bswap.h"
#include "system/address-spaces.h"
#include "system/system.h"
#include "hw/nikon/gatework.h"
#include "hw/nikon/gateinput.h"
#include "hw/nikon/devices.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "qemu/timer.h"
#include "system/cpu-timers.h"
#include "qemu/main-loop.h"
#include "qemu/thread.h"
#include "block/thread-pool.h"
#include "system/runstate.h"

int (*qemu_main)(void);

#define RAM_BASE 0x10000000
#define RAM_SIZE 0x200000
#define GRID 0x1000
#define TRANSFORM 0x2000

typedef struct Fixture {
    MemoryRegion ram, alias;
    uint8_t *bytes;
    uint32_t config[0x4080 / 4], slots[16][13];
    NikonGatePacket packets[10];
    NikonGateWorkCommand commands[10];
    uint8_t input[64 * 16 * 3 / 2];
    NikonGateWork work;
} Fixture;

static void set(NikonGatePacket *p, unsigned offset, uint32_t value)
{
    p->value[offset / 4] = value;
    p->present[offset / 4] = true;
}

static void zero(NikonGatePacket *p, unsigned offset, unsigned words)
{
    for (unsigned i = 0; i < words; i++) {
        set(p, offset + i * 4, 0);
    }
}

static void common_window(NikonGatePacket *p, bool terminal)
{
    zero(p, 0, 9);
    zero(p, 0x40, 7);
    zero(p, 0x80, 16);
    set(p, 0x18, terminal ? 0xff010000 : 0xff000000);
    set(p, 0x80, 6);
    set(p, 0x8c, 0xff);
    set(p, 0x94, 0xff);
    set(p, 0xa8, 0xff);
    set(p, 0xb0, 0xff);
}

static Fixture *fixture(void)
{
    static const unsigned offsets[] = {
        0xe60, 0x900, 0xde0, 0x8a0, 0x8c0, 0x8e0,
        0x9e0, 0x940, 0xba0, 0x920,
    };
    static const unsigned routes[] = { 0x40, 7, 0x46 };
    Fixture *f = g_new0(Fixture, 1);
    NikonGatePacket *p;

    memory_region_init_ram_flags_nomigrate(&f->ram, NULL, "gatework-ram",
                                           RAM_SIZE, 0, &error_abort);
    memory_region_add_subregion(get_system_memory(), RAM_BASE, &f->ram);
    memory_region_init_alias(&f->alias, NULL, "gatework-alias", &f->ram,
                              0, RAM_SIZE);
    memory_region_add_subregion(get_system_memory(), 0x20000000, &f->alias);
    f->bytes = memory_region_get_ram_ptr(&f->ram);
    memset(f->bytes, 0xa5, RAM_SIZE);
    f->work = (NikonGateWork) {
        .as = &address_space_memory, .config = f->config,
        .slots = (const uint32_t (*)[13])f->slots,
        .parameter_valid = 0x138, .commands = f->commands,
        .command_count = ARRAY_SIZE(f->commands), .input = f->input,
        .input_size = sizeof(f->input), .width = 64, .height = 16,
    };
    f->config[0x2100 / 4] = 0x121;
    f->config[0x2104 / 4] = 0xc2;
    f->config[0x2110 / 4] = 0x200087e0;
    f->config[0x2114 / 4] = 0x88000;
    f->config[0x2118 / 4] = 0x139;
    f->config[0x2600 / 4] = 0x305;
    f->config[0x2604 / 4] = 16;
    f->config[0x2608 / 4] = 1;
    f->config[0x2618 / 4] = 3;
    f->config[0x4050 / 4] = 2;
    f->config[0x4054 / 4] = 64 | (8 << 16);
    for (unsigned phase = 0; phase < 4; phase++) {
        f->config[(0x2200 + phase * 64 + 8 * 4) / 4] = 0x40000000;
    }
    f->slots[8][0] = 0x9000201;
    f->slots[8][1] = 0x15500;
    f->slots[8][4] = f->slots[8][5] = 64;
    f->slots[8][8] = 64 | (16 << 16);
    f->slots[8][10] = 0x8000;
    for (unsigned i = 0; i < ARRAY_SIZE(f->commands); i++) {
        f->commands[i].offset = offsets[i];
        f->commands[i].packet = &f->packets[i];
    }
    p = &f->packets[0];
    zero(p, 0, 3);
    set(p, 0, 8);
    set(p, 4, 0x30);
    set(p, 8, 0x202);
    zero(p, 0x200, 80);
    set(p, 0x400, 0x31);
    set(p, 0x404, 8);
    set(p, 0x408, 0xfe);
    p = &f->packets[1];
    zero(p, 0, 33);
    set(p, 0, 0x6a);
    for (unsigned i = 4; i <= 0xc; i += 4) {
        set(p, i, 0xff);
    }
    set(p, 0x10, 0x30);
    set(p, 0x14, 0x10000000);
    set(p, 0x18, 0x4080);
    set(p, 0x1c, 0x120);
    set(p, 0x28, 1);
    for (unsigned i = 0x30; i <= 0x3c; i += 4) {
        set(p, i, 0x100000);
    }
    for (unsigned i = 0x40; i <= 0x4c; i += 4) {
        set(p, i, 0xff);
    }
    set(p, 0x50, (2 << 16) | 1);
    set(p, 0x54, RAM_BASE + GRID);
    set(p, 0x64, 4);
    set(p, 0x68, 0x40302);
    set(p, 0x78, 0x31);
    set(p, 0x7c, 0x3000);
    set(p, 0x80, 0x10);
    for (unsigned row = 0; row < 2; row++) {
        uint8_t *r = f->bytes + GRID + row * 40;

        memset(r, 0, 40);
        for (unsigned corner = 0; corner < 4; corner++) {
            stl_le_p(r + corner * 8, (corner & 1 ? 64 : 0) << 8);
            stl_le_p(r + corner * 8 + 4,
                      (row * 8 + (corner & 2 ? 8 : 0)) << 8);
        }
        stl_le_p(r + 32, 0x306);
    }
    p = &f->packets[2];
    common_window(p, false);
    set(p, 0, 0x90000);
    set(p, 4, 0xff0000);
    set(p, 8, 0x10500);
    set(p, 0x40, 63 << 16);
    set(p, 0x44, 15 << 16);
    set(p, 0x48, 0x400040);
    set(p, 0x4c, 0x80008);
    for (unsigned i = 0; i < 3; i++) {
        unsigned width = i ? 32 : 64, height = i ? 8 : 16;
        unsigned scale = i ? 2 : 1;
        unsigned slot = i + 3;
        uint8_t *a = f->bytes + TRANSFORM + i * 32;

        p = &f->packets[3 + i];
        zero(p, 0, 27);
        zero(p, 0x78, 3);
        set(p, 0, i == 1 ? 0xa : 0xff);
        set(p, 4, i == 0 ? 0x14 : i == 2 ? 0x16 : 0xff);
        set(p, 8, 0xff);
        set(p, 0xc, 0xff);
        set(p, 0x10, i == 2 ? 0x10 : 0x13);
        set(p, 0x14, 0x10000000);
        set(p, 0x18, 0x5050);
        set(p, 0x1c, 0x300);
        set(p, 0x20, i == 0 ? 0x10 : 0);
        set(p, 0x40, 0xff);
        set(p, 0x44, 0xff);
        set(p, 0x48, 0xff);
        set(p, 0x50, 0x10001);
        set(p, 0x54, RAM_BASE + TRANSFORM + i * 32);
        set(p, 0x64, 0xd);
        set(p, 0x78, 1);
        memset(a, 0, 32);
        stl_le_p(a, i == 0 ? 0x80000000 : 0);
        stl_le_p(a + 4, i == 0 ? 0x80000000 : 0);
        stl_le_p(a + 8, scale << 16);
        stl_le_p(a + 20, scale << 16);
        stl_le_p(a + 24, width | (height << 16));
        p = &f->packets[6 + i];
        common_window(p, true);
        set(p, 0, routes[i] << 16);
        set(p, 8, 0x10000 | slot);
        set(p, 0x40, (width - 1) << 16);
        set(p, 0x44, (height - 1) << 16);
        set(p, 0x48, i == 2 ? 0x400040 : 0x1000100);
        set(p, 0x4c, 0x20002);
        f->commands[6 + i].auxiliary[0] = 0x200087e0;
        f->commands[6 + i].auxiliary[1] = 0x88000;
        f->slots[slot][0] = 0x9000201;
        f->slots[slot][1] = 0x15500;
        f->slots[slot][4] = f->slots[slot][5] = 64;
        f->slots[slot][8] = width | (height << 16);
        f->slots[slot][10] = 0x800;
        for (unsigned phase = 0; phase < 4; phase++) {
            f->config[(0x2200 + phase * 64 + slot * 4) / 4] =
                RAM_BASE + 0x10000 * (i + 1) + phase * 0x40000;
        }
    }
    p = &f->packets[9];
    zero(p, 0, 2);
    zero(p, 0x10, 6);
    zero(p, 0x40, 1);
    zero(p, 0x60, 8);
    set(p, 0x10, 0x20200);
    set(p, 0x14, 0x20201);
    set(p, 0x18, 0x20202);
    set(p, 0x1c, 0x203);
    for (unsigned y = 0; y < 16; y++) {
        for (unsigned x = 0; x < 64; x++) {
            f->input[y * 64 + x] = 3 * y + x;
        }
    }
    for (unsigned y = 0; y < 8; y++) {
        for (unsigned x = 0; x < 64; x += 2) {
            f->input[1024 + y * 64 + x] = 20 + 2 * y + x;
            f->input[1024 + y * 64 + x + 1] = 180 - 2 * y - x;
        }
    }
    return f;
}

static void cleanup(Fixture *f)
{
    memory_region_del_subregion(get_system_memory(), &f->alias);
    memory_region_del_subregion(get_system_memory(), &f->ram);
    object_unparent(OBJECT(&f->alias));
    object_unparent(OBJECT(&f->ram));
    g_free(f);
}

static void check_outputs(Fixture *f, unsigned phase, unsigned shift)
{
    for (unsigned i = 0; i < 3; i++) {
        unsigned width = i ? 32 : 64, height = i ? 8 : 16;
        unsigned scale = i ? 2 : 1;
        size_t base = 0x10000 * (i + 1) + phase * 0x40000;

        for (unsigned y = 0; y < height; y++) {
            for (unsigned x = 0; x < 64; x++) {
                g_assert_cmpuint(f->bytes[base + y * 64 + x], ==,
                    x < width ? 3 * y * scale +
                    MIN(x * scale + shift, 63) : 0xa5);
            }
        }
        for (unsigned y = 0; y < height / 2; y++) {
            for (unsigned x = 0; x < 64; x++) {
                unsigned v = 0xa5;

                if (x < width) {
                    unsigned sx = MIN((x & ~1U) * scale + shift, 62);
                    unsigned sy = y * scale;

                    v = x & 1 ? 180 - 2 * sy - sx : 20 + 2 * sy + sx;
                }
                g_assert_cmpuint(f->bytes[base + 0x800 + y * 64 + x], ==, v);
            }
        }
        g_assert_cmpuint(f->bytes[base - 1], ==, 0xa5);
        g_assert_cmpuint(f->bytes[base + height * 64], ==, 0xa5);
    }
}

static void test_pixels(void)
{
    Fixture *f = fixture();

    for (unsigned phase = 0; phase < 4; phase++) {
        f->work.phase = phase;
        g_assert_cmpint(nikon_gate_work_execute(&f->work), ==,
                         NIKON_GATE_WORK_OK);
        check_outputs(f, phase, 0);
        for (unsigned next = phase + 1; next < 4; next++) {
            g_assert_cmpuint(f->bytes[0x10000 + next * 0x40000], ==, 0xa5);
        }
    }
    cleanup(f);
}

static void test_grid_translation(void)
{
    Fixture *f = fixture();

    for (unsigned row = 0; row < 2; row++) {
        for (unsigned corner = 0; corner < 4; corner++) {
            uint8_t *x = f->bytes + GRID + row * 40 + corner * 8;

            stl_le_p(x, ldl_le_p(x) + (2 << 8));
        }
    }
    g_assert_cmpint(nikon_gate_work_execute(&f->work), ==, NIKON_GATE_WORK_OK);
    check_outputs(f, 0, 2);
    cleanup(f);
}

static void test_private_plan(void)
{
    Fixture *f = fixture();
    NikonGateWorkPlan *plan;

    g_assert_cmpint(nikon_gate_work_prepare(&f->work, &plan), ==,
                     NIKON_GATE_WORK_OK);
    /* Neither the old tight input nor grid records remain worker dependencies. */
    memset(f->input, 0, sizeof(f->input));
    memset(f->bytes + GRID, 0, 80);
    g_assert_cmpint(nikon_gate_work_compute(plan), ==, NIKON_GATE_WORK_OK);
    g_assert_cmpuint(f->bytes[0x10000], ==, 0xa5);
    g_assert_cmpint(nikon_gate_work_publish(plan, &f->work), ==,
                     NIKON_GATE_WORK_OK);
    check_outputs(f, 0, 0);
    nikon_gate_work_free(plan);
    cleanup(f);
}

static void test_current_layout(void)
{
    for (unsigned variant = 0; variant < 6; variant++) {
        Fixture *f = fixture();
        NikonGateWorkPlan *plan;
        g_autofree uint8_t *saved = NULL;

        g_assert_cmpint(nikon_gate_work_prepare(&f->work, &plan), ==,
                         NIKON_GATE_WORK_OK);
        g_assert_cmpint(nikon_gate_work_compute(plan), ==, NIKON_GATE_WORK_OK);
        switch (variant) {
        case 0: /* Old destination is still RAM, but no longer selected. */
            f->config[(0x2200 + 3 * 4) / 4] += 0x40000;
            break;
        case 1: /* A legal current stride must still match the owned layout. */
            f->slots[3][4] = 128;
            break;
        case 2:
            memory_region_set_readonly(&f->ram, true);
            break;
        case 3: /* Distinct bus addresses can alias the same backing. */
            f->config[(0x2200 + 4 * 4) / 4] = 0x20010000;
            break;
        case 4:
            f->work.phase = 1;
            break;
        case 5:
            stl_le_p(f->bytes + TRANSFORM + 12, 1);
            break;
        }
        saved = g_memdup2(f->bytes, RAM_SIZE);
        g_assert_cmpint(nikon_gate_work_publish(plan, &f->work), !=,
                         NIKON_GATE_WORK_OK);
        g_assert_cmpmem(f->bytes, RAM_SIZE, saved, RAM_SIZE);
        memory_region_set_readonly(&f->ram, false);
        nikon_gate_work_free(plan);
        cleanup(f);
    }
}

static void test_reject(void)
{
    for (unsigned variant = 0; variant < 20; variant++) {
        Fixture *f = fixture();
        NikonGateWorkResult expected;
        g_autofree uint8_t *saved = NULL;

        switch (variant) {
        case 0:
            f->commands[9].packet = NULL;
            expected = NIKON_GATE_WORK_TOPOLOGY;
            break;
        case 1:
            f->packets[0].value[0x208 / 4] = 1;
            expected = NIKON_GATE_WORK_FORMAT;
            break;
        case 2:
            f->packets[8].value[0x84 / 4] = 1;
            expected = NIKON_GATE_WORK_FORMAT;
            break;
        case 3:
            f->commands[8].auxiliary[1] ^= 1;
            expected = NIKON_GATE_WORK_FORMAT;
            break;
        case 4:
            stl_le_p(f->bytes + GRID + 40 + 32, 0x305);
            expected = NIKON_GATE_WORK_PIXELS;
            break;
        case 5:
            stl_le_p(f->bytes + TRANSFORM + 64 + 12, 1);
            expected = NIKON_GATE_WORK_AFFINE;
            break;
        case 6:
            f->config[(0x2200 + 5 * 4) / 4] = RAM_BASE + RAM_SIZE - 8;
            expected = NIKON_GATE_WORK_MEMORY;
            break;
        case 7:
            f->slots[5][10] = 0xfffffff0;
            expected = NIKON_GATE_WORK_FORMAT;
            break;
        case 8:
            f->config[(0x2200 + 5 * 4) / 4] =
                0x20000000 + 0x10000;
            expected = NIKON_GATE_WORK_ALIAS;
            break;
        case 9:
            f->config[(0x2200 + 5 * 4) / 4] = RAM_BASE + GRID;
            expected = NIKON_GATE_WORK_ALIAS;
            break;
        case 10:
            f->work.parameter_valid &= ~(1U << 5);
            expected = NIKON_GATE_WORK_FORMAT;
            break;
        case 11:
            f->config[0x2608 / 4] = 0;
            expected = NIKON_GATE_WORK_CONFIG;
            break;
        case 12:
            memory_region_set_readonly(&f->ram, true);
            expected = NIKON_GATE_WORK_MEMORY;
            break;
        case 13:
            f->work.input = f->bytes + 0x10000;
            expected = NIKON_GATE_WORK_ALIAS;
            break;
        case 14:
            set(&f->packets[1], 0x54, RAM_BASE + RAM_SIZE - 40);
            expected = NIKON_GATE_WORK_MEMORY;
            break;
        case 15:
            set(&f->packets[0], 0x100, 0);
            expected = NIKON_GATE_WORK_FORMAT;
            break;
        case 16:
            f->commands[9].offset = f->commands[0].offset;
            expected = NIKON_GATE_WORK_TOPOLOGY;
            break;
        case 17:
            f->slots[5][10] = 16;
            expected = NIKON_GATE_WORK_ALIAS;
            break;
        case 18:
            f->slots[8][8] ^= 2;
            expected = NIKON_GATE_WORK_FORMAT;
            break;
        default:
            f->config[(0x2200 + 2 * 64 + 8 * 4) / 4] = RAM_BASE;
            expected = NIKON_GATE_WORK_FORMAT;
            break;
        }
        saved = g_memdup2(f->bytes, RAM_SIZE);
        g_assert_cmpint(nikon_gate_work_execute(&f->work), ==, expected);
        g_assert_cmpmem(f->bytes, RAM_SIZE, saved, RAM_SIZE);
        cleanup(f);
    }
}

#define GATE_BASE 0x30000000

typedef struct GateFixture {
    Fixture *f;
    DeviceState *dev;
    qemu_irq irq;
    bool level;
    bool reset_on_irq, ack_on_irq;
    unsigned capacity_pulses;
    qemu_irq capacity;
} GateFixture;

static void gate_write(unsigned offset, uint32_t value);

static void gate_irq(void *opaque, int index, int level)
{
    GateFixture *g = opaque;

    g->level = level;
    if (level && g->reset_on_irq) {
        g->reset_on_irq = false;
        device_cold_reset(g->dev);
    } else if (level && g->ack_on_irq) {
        g->ack_on_irq = false;
        gate_write(0x1c, 0x61);
        gate_write(0x20, 8);
    }
}

static void gate_capacity(void *opaque, int index, int level)
{
    GateFixture *g = opaque;

    g->capacity_pulses += !!level;
}

static void gate_write(unsigned offset, uint32_t value)
{
    uint8_t data[4];

    stl_le_p(data, value);
    g_assert_cmpint(address_space_write(&address_space_memory,
        GATE_BASE + offset, MEMTXATTRS_UNSPECIFIED, data, 4), ==, MEMTX_OK);
}

static uint32_t gate_read(unsigned offset)
{
    uint8_t data[4];

    g_assert_cmpint(address_space_read(&address_space_memory,
        GATE_BASE + offset, MEMTXATTRS_UNSPECIFIED, data, 4), ==, MEMTX_OK);
    return ldl_le_p(data);
}

static unsigned packet_block(uint8_t *data, const NikonGatePacket *p,
                              unsigned offset, unsigned count, bool last)
{
    stl_le_p(data, offset | (count << 16) |
                   (last ? 0x01000000 : 0x11000000));
    for (unsigned i = 1; i < count; i++) {
        stl_le_p(data + 4 * i, p->value[offset / 4 + i - 1]);
    }
    return count * 4;
}

static void gate_load(GateFixture *g, unsigned index)
{
    const NikonGatePacket *p = &g->f->packets[index];
    const NikonGateWorkCommand *c = &g->f->commands[index];
    uint8_t *data = g->f->bytes + 0x3000;
    unsigned n = 0;

    if (!index) {
        n += packet_block(data + n, p, 0, 4, false);
        n += packet_block(data + n, p, 0x200, 81, false);
        packet_block(data + n, p, 0x400, 4, true);
    } else if (index == 1) {
        packet_block(data, p, 0, 34, true);
    } else if (index >= 3 && index <= 5) {
        n += packet_block(data + n, p, 0, 28, false);
        packet_block(data + n, p, 0x78, 4, true);
    } else if (index == 9) {
        n += packet_block(data + n, p, 0, 3, false);
        n += packet_block(data + n, p, 0x10, 7, false);
        n += packet_block(data + n, p, 0x40, 2, false);
        packet_block(data + n, p, 0x60, 9, true);
    } else {
        n += packet_block(data + n, p, 0, 10, false);
        n += packet_block(data + n, p, 0x40, 8, false);
        packet_block(data + n, p, 0x80, 17, true);
    }
    gate_write(c->offset + 0x14, 1);
    gate_write(c->offset + 0x14, 0);
    if (index >= 6 && index <= 8) {
        gate_write(c->offset + 8, c->auxiliary[0]);
        gate_write(c->offset + 0xc, c->auxiliary[1]);
    }
    gate_write(c->offset + 0x10, RAM_BASE + 0x3000);
}

static GateFixture *gate_fixture(void)
{
    GateFixture *g = g_new0(GateFixture, 1);

    g->f = fixture();
    g->dev = qdev_new(TYPE_NIKON_EVENT_GATE);
    object_property_add_child(object_get_objects_root(), "gate[*]",
                               OBJECT(g->dev));
    g_assert_true(sysbus_realize(SYS_BUS_DEVICE(g->dev), &error_abort));
    sysbus_mmio_map(SYS_BUS_DEVICE(g->dev), 0, GATE_BASE);
    g->irq = qemu_allocate_irq(gate_irq, g, 0);
    sysbus_connect_irq(SYS_BUS_DEVICE(g->dev), 0, g->irq);
    g->capacity = qemu_allocate_irq(gate_capacity, g, 0);
    qdev_connect_gpio_out_named(g->dev, "input-capacity", 0, g->capacity);
    device_cold_reset(g->dev);
    for (unsigned i = 0; i < 16; i++) {
        if (g->f->work.parameter_valid & (1U << i)) {
            for (unsigned j = 0; j < 13; j++) {
                gate_write(0x400 + i * 64 + j * 4, g->f->slots[i][j]);
            }
            gate_write(0x2c, i);
        }
    }
    for (unsigned i = 0; i < 10; i++) {
        gate_load(g, i);
    }
    for (unsigned i = 0; i < ARRAY_SIZE(g->f->config); i++) {
        if (g->f->config[i]) {
            gate_write(i * 4, g->f->config[i]);
        }
    }
    gate_write(0x10, 0x61);
    gate_write(0x14, 8);
    gate_write(4, 1);
    vm_start();
    return g;
}

static void gate_cleanup(GateFixture *g)
{
    if (qdev_is_realized(g->dev)) {
        qdev_unrealize(g->dev);
    }
    object_unparent(OBJECT(g->dev));
    object_unref(OBJECT(g->dev));
    qemu_free_irq(g->irq);
    qemu_free_irq(g->capacity);
    cleanup(g->f);
    g_free(g);
}

typedef struct PoolBarrier {
    QemuSemaphore entered, release;
    bool block, done;
} PoolBarrier;

static int barrier_worker(void *opaque)
{
    PoolBarrier *b = opaque;

    qemu_sem_post(&b->entered);
    if (b->block) {
        qemu_sem_wait(&b->release);
    }
    return 0;
}

static void barrier_completed(void *opaque, int ret)
{
    PoolBarrier *b = opaque;

    g_assert_cmpint(ret, ==, 0);
    b->done = true;
}

static void barrier_submit(PoolBarrier *b, bool block)
{
    *b = (PoolBarrier) { .block = block };
    qemu_sem_init(&b->entered, 0);
    qemu_sem_init(&b->release, 0);
    thread_pool_submit_aio(barrier_worker, b, barrier_completed, b);
}

static void barrier_wait(PoolBarrier *b)
{
    int64_t deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;

    while (!b->done) {
        g_assert_cmpint(g_get_monotonic_time(), <, deadline);
        aio_poll(qemu_get_aio_context(), false);
        g_thread_yield();
    }
    qemu_sem_destroy(&b->entered);
    qemu_sem_destroy(&b->release);
}

static void settle(void)
{
    PoolBarrier marker;

    barrier_submit(&marker, false);
    barrier_wait(&marker);
    qemu_clock_run_timers(QEMU_CLOCK_VIRTUAL);
}

static void advance_clock(int64_t ns)
{
    cpus_set_virtual_clock(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + ns);
    qemu_clock_run_timers(QEMU_CLOCK_VIRTUAL);
}

static void advance(int64_t ns)
{
    advance_clock(ns);
    settle();
}

static void publish(GateFixture *g)
{
    NikonGateInputShape shape = { .width = 64, .height = 16, .stride = 64 };
    NikonGateInputVersion version;

    g_assert_cmpint(nikon_gate_input_status(g->dev, &shape, &version), ==,
                     NIKON_GATE_INPUT_READY);
    g_assert_true(nikon_gate_input_publish(g->dev, version.generation, &shape,
        g->f->input, 1024, g->f->input + 1024, 512));
}

static void test_device_frames(void)
{
    GateFixture *g = gate_fixture();

    publish(g);
    g_assert_cmpuint(gate_read(0x2060), ==, 1);
    g_assert_false(g->level);
    advance(999999);
    g_assert_cmpuint(g->f->bytes[0x10000], ==, 0xa5);
    advance(1);
    check_outputs(g->f, 0, 0);
    g_assert_cmpuint(gate_read(0x2060), ==, 0);
    g_assert_cmpuint(gate_read(0x1c), ==, 0x61);
    g_assert_cmpuint(gate_read(0x20), ==, 8);
    g_assert_true(g->level);
    publish(g);
    advance(2000000);
    g_assert_cmpuint(g->f->bytes[0x50000], ==, 0xa5);
    gate_write(0x1c, 0x61);
    advance(2000000);
    g_assert_cmpuint(g->f->bytes[0x50000], ==, 0xa5);
    gate_write(0x20, 8);
    advance(1000000);
    check_outputs(g->f, 1, 0);
    g_assert_cmpuint(object_property_get_uint(OBJECT(g->dev), "processed",
                                             &error_abort), ==, 2);
    gate_cleanup(g);
}

static void test_device_cancel(void)
{
    for (unsigned variant = 0; variant < 4; variant++) {
        GateFixture *g = gate_fixture();

        publish(g);
        advance(500000);
        switch (variant) {
        case 0:
            gate_write(0x2100, 0x120);
            break;
        case 1:
            gate_write(4, 0);
            break;
        case 2:
            device_cold_reset(g->dev);
            break;
        case 3:
            qdev_unrealize(g->dev);
            break;
        }
        advance(2000000);
        g_assert_cmpuint(g->f->bytes[0x10000], ==, 0xa5);
        g_assert_false(g->level);
        if (!variant) {
            g_assert_cmpuint(gate_read(0x2060), ==, 1);
            gate_write(0x2100, 0x121);
            advance(1000000);
            check_outputs(g->f, 0, 0);
        }
        gate_cleanup(g);
    }
}

static void test_device_reject(void)
{
    GateFixture *g = gate_fixture();

    stl_le_p(g->f->bytes + GRID + 40 + 32, 0x305);
    publish(g);
    advance(1000000);
    g_assert_cmpuint(gate_read(0x2060), ==, 1);
    g_assert_false(g->level);
    g_assert_cmpuint(g->f->bytes[0x10000], ==, 0xa5);
    stl_le_p(g->f->bytes + GRID + 40 + 32, 0x306);
    gate_load(g, 1);
    advance(1000000);
    check_outputs(g->f, 0, 0);
    gate_cleanup(g);
}

static void test_device_started(void)
{
    GateFixture *g = gate_fixture();

    for (unsigned i = 0; i < 10; i++) {
        gate_write(g->f->commands[i].offset, 1);
    }
    publish(g);
    advance(1000000);
    check_outputs(g->f, 0, 0);
    for (unsigned i = 0; i < 10; i++) {
        g_assert_cmpuint(gate_read(g->f->commands[i].offset), ==, 0);
    }
    /* Completed work must not leave a stale request blocking parameters. */
    for (unsigned j = 0; j < 13; j++) {
        gate_write(0x400 + 3 * 64 + j * 4, g->f->slots[3][j]);
    }
    gate_write(0x2c, 3);
    gate_cleanup(g);
}

static void test_device_phase(void)
{
    GateFixture *g = gate_fixture();

    gate_write(0x2104, 0xe2);
    publish(g);
    advance(1000000);
    check_outputs(g->f, 2, 0);
    gate_write(0x1c, 0x61);
    gate_write(0x20, 8);
    gate_write(0x2104, 0xd2);
    publish(g);
    advance(1000000);
    check_outputs(g->f, 1, 0);
    gate_cleanup(g);
}

static void blocker_start(PoolBarrier *b)
{
    int64_t deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;

    barrier_submit(b, true);
    while (qemu_sem_timedwait(&b->entered, 0)) {
        g_assert_cmpint(g_get_monotonic_time(), <, deadline);
        aio_poll(qemu_get_aio_context(), false);
        g_thread_yield();
    }
}

static void worker_returned(PoolBarrier *blocker, PoolBarrier *marker)
{
    /* One worker makes the marker a barrier after the actual queued math. */
    barrier_submit(marker, false);
    qemu_sem_post(&blocker->release);
    g_assert_cmpint(qemu_sem_timedwait(&marker->entered, 5000), ==, 0);
    g_assert_false(marker->done);
}

static void unpublished(GateFixture *g)
{
    g_assert_cmpuint(g->f->bytes[0x10000], ==, 0xa5);
    g_assert_false(g->level);
    g_assert_cmpuint(gate_read(0x1c), ==, 0);
    g_assert_cmpuint(gate_read(0x20), ==, 0);
}

static uint64_t gate_counter(GateFixture *g, const char *name)
{
    return object_property_get_uint(OBJECT(g->dev), name, &error_abort);
}

static void test_async_pause(void)
{
    GateFixture *g = gate_fixture();
    PoolBarrier blocker, marker;

    blocker_start(&blocker);
    publish(g);
    advance_clock(1000000);
    g_assert_cmpuint(gate_counter(g, "processing-compute"), ==, 1);
    g_assert_cmpuint(gate_counter(g, "processing-inflight"), ==, 1);
    /* A submitted task owns the real input and records, not guest live RAM. */
    memset(g->f->bytes + GRID, 0, 80);
    g_assert_cmpint(vm_stop(RUN_STATE_PAUSED), ==, 0);
    worker_returned(&blocker, &marker);
    unpublished(g); /* Host return itself cannot touch guest RAM or IRQ. */
    barrier_wait(&blocker);
    barrier_wait(&marker);
    qemu_clock_run_timers(QEMU_CLOCK_VIRTUAL);
    unpublished(g);
    g_assert_cmpuint(gate_counter(g, "processing-compute"), ==, 2);
    g_assert_cmpuint(gate_counter(g, "processing-inflight"), ==, 1);
    g_assert_cmpuint(gate_read(0x2060), ==, 1);
    vm_start();
    advance(0);
    check_outputs(g->f, 0, 0);
    g_assert_cmpuint(gate_counter(g, "processing-inflight"), ==, 0);
    g_assert_cmpuint(gate_read(0x2060), ==, 0);
    gate_cleanup(g);
}

static void test_async_cancel(void)
{
    for (unsigned variant = 0; variant < 5; variant++) {
        GateFixture *g = gate_fixture();
        PoolBarrier blocker, marker;

        blocker_start(&blocker);
        publish(g);
        advance_clock(1000000);
        if (variant == 2) {
            /* Host math and Aio callback finish, virtual publication is paused. */
            g_assert_cmpint(vm_stop(RUN_STATE_PAUSED), ==, 0);
            worker_returned(&blocker, &marker);
            barrier_wait(&blocker);
            barrier_wait(&marker);
            g_assert_cmpuint(gate_counter(g, "processing-compute"), ==, 2);
            device_cold_reset(g->dev);
        } else {
            if (variant == 0) {
                gate_write(0x2100, 0x120);
            } else if (variant == 1) {
                gate_write(4, 0);
            } else if (variant == 3) {
                device_cold_reset(g->dev);
            }
            g_assert_cmpuint(gate_counter(g, "processing-inflight"), ==, 1);
            worker_returned(&blocker, &marker);
            if (variant == 4) {
                qdev_unrealize(g->dev); /* Must drain the actual host owner. */
            }
            barrier_wait(&blocker);
            barrier_wait(&marker);
        }
        vm_start();
        advance(2000000);
        g_assert_cmpuint(gate_counter(g, "processing-inflight"), ==, 0);
        if (variant == 4) {
            g_assert_cmpuint(g->f->bytes[0x10000], ==, 0xa5);
            g_assert_false(g->level);
        } else {
            unpublished(g);
        }
        if (!variant) {
            /* Retained input can be re-qualified, but revoked work cannot DMA. */
            g_assert_cmpuint(gate_read(0x2060), ==, 1);
            gate_write(0x2100, 0x121);
            advance(1000000);
            check_outputs(g->f, 0, 0);
        }
        gate_cleanup(g);
    }
}

static void test_capacity_epoch(void)
{
    GateFixture *g = gate_fixture();
    NikonGateInputShape shape = { 64, 16, 64 };
    NikonGateInputVersion before, busy, after;
    unsigned pulses = g->capacity_pulses;

    g_assert_cmpint(nikon_gate_input_status(g->dev, &shape, &before), ==,
                     NIKON_GATE_INPUT_READY);
    publish(g);
    g_assert_cmpint(nikon_gate_input_status(g->dev, &shape, &busy), ==,
                     NIKON_GATE_INPUT_BUSY);
    g_assert_cmpuint(busy.epoch, ==, before.epoch);
    g_assert_cmpuint(busy.generation, ==, before.generation);
    g_assert_cmpuint(g->capacity_pulses, ==, pulses);
    advance(1000000);
    g_assert_cmpint(nikon_gate_input_status(g->dev, &shape, &after), ==,
                     NIKON_GATE_INPUT_READY);
    g_assert_cmpuint(after.epoch, ==, before.epoch);
    g_assert_cmpuint(after.generation, !=, before.generation);
    g_assert_cmpuint(g->capacity_pulses, ==, pulses + 1);
    device_cold_reset(g->dev);
    g_assert_cmpint(nikon_gate_input_status(g->dev, &shape, &busy), ==,
                     NIKON_GATE_INPUT_UNSUPPORTED);
    g_assert_cmpuint(busy.epoch, !=, after.epoch);
    g_assert_cmpuint(busy.generation, !=, after.generation);
    g_assert_cmpuint(g->capacity_pulses, ==, pulses + 2);
    gate_cleanup(g);
}

static void test_grid_scope(void)
{
    GateFixture *g = gate_fixture();
    PoolBarrier blocker, marker;

    blocker_start(&blocker);
    publish(g);
    advance_clock(1000000);
    gate_write(0x2644, 17); /* The independent context8 height is not MAIN. */
    g_assert_cmpuint(gate_counter(g, "processing-cancelled"), ==, 0);
    worker_returned(&blocker, &marker);
    barrier_wait(&blocker);
    barrier_wait(&marker);
    advance(0);
    check_outputs(g->f, 0, 0);
    g_assert_cmpuint(gate_counter(g, "processed"), ==, 1);
    gate_cleanup(g);
}

static void test_irq_reentry(void)
{
    for (unsigned reset = 0; reset < 2; reset++) {
        GateFixture *g = gate_fixture();

        g->reset_on_irq = reset;
        g->ack_on_irq = !reset;
        publish(g);
        advance(1000000);
        check_outputs(g->f, 0, 0);
        g_assert_false(g->level);
        g_assert_cmpuint(gate_read(0x1c), ==, 0);
        g_assert_cmpuint(gate_read(0x20), ==, 0);
        g_assert_cmpuint(object_property_get_uint(OBJECT(g->dev), "processed",
                         &error_abort), ==, reset ? 0 : 1);
        g_assert_cmpuint(object_property_get_uint(OBJECT(g->dev),
                         "processing-phase", &error_abort), ==, reset ? 0 : 1);
        advance(2000000);
        g_assert_cmpuint(g->f->bytes[0x50000], ==, 0xa5);
        gate_cleanup(g);
    }
}

int main(int argc, char **argv)
{
    g_autofree char *directory = g_dir_make_tmp("nikonXXXXXX", NULL);
    g_autofree char *socket = g_build_filename(directory, "qtest", NULL);
    g_autofree char *chardev = g_strdup_printf("unix:%s,server=on,wait=off",
                                               socket);
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
    aio_context_set_thread_pool_params(qemu_get_aio_context(), 0, 1,
                                       &error_abort);
    g_test_add_func("/nikon/gatework/pixels-phases", test_pixels);
    g_test_add_func("/nikon/gatework/grid-translation", test_grid_translation);
    g_test_add_func("/nikon/gatework/private-plan", test_private_plan);
    g_test_add_func("/nikon/gatework/current-layout", test_current_layout);
    g_test_add_func("/nikon/gatework/preflight", test_reject);
    g_test_add_func("/nikon/gatework/device-frames", test_device_frames);
    g_test_add_func("/nikon/gatework/device-cancel", test_device_cancel);
    g_test_add_func("/nikon/gatework/device-reject", test_device_reject);
    g_test_add_func("/nikon/gatework/device-started", test_device_started);
    g_test_add_func("/nikon/gatework/device-phase", test_device_phase);
    g_test_add_func("/nikon/gatework/async-pause", test_async_pause);
    g_test_add_func("/nikon/gatework/async-cancel", test_async_cancel);
    g_test_add_func("/nikon/gatework/capacity-epoch", test_capacity_epoch);
    g_test_add_func("/nikon/gatework/grid-scope", test_grid_scope);
    g_test_add_func("/nikon/gatework/irq-reentry", test_irq_reentry);
    result = g_test_run();
    qemu_cleanup(result);
    unlink(socket);
    rmdir(directory);
    return result;
}
