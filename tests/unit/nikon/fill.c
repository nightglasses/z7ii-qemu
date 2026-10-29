/* Source-shaped byte-row DMA through the public QOM/MMIO interface.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/rcu.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/nikon/devices.h"
#include "system/system.h"

int (*qemu_main)(void);

#define SOURCE 0x1000
#define DESTINATION 0x4000
#define ALIAS 0x7000
#define SIZE 0x1000

typedef struct Fixture {
    Object *owner;
    MemoryRegion root, source, destination, alias;
    DeviceState *dev;
    qemu_irq irq[2];
    bool level[2];
    uint8_t *src, *dst;
} Fixture;

static void irq_level(void *opaque, int n, int level)
{
    *(bool *)opaque = level;
}

static Fixture *fixture(void)
{
    Fixture *f = g_new0(Fixture, 1);

    f->owner = object_new(TYPE_CONTAINER);
    object_property_add_child(object_get_objects_root(), "fill-memory[*]",
                              f->owner);
    memory_region_init(&f->root, f->owner, "fill.root", 0x10000);
    memory_region_init_ram_flags_nomigrate(&f->source, f->owner, "fill.source",
                                          SIZE, 0, &error_abort);
    memory_region_init_ram_flags_nomigrate(&f->destination, f->owner,
                                          "fill.destination", SIZE, 0,
                                          &error_abort);
    memory_region_init_alias(&f->alias, f->owner, "fill.alias", &f->source,
                              0, SIZE);
    memory_region_add_subregion(&f->root, SOURCE, &f->source);
    memory_region_add_subregion(&f->root, DESTINATION, &f->destination);
    memory_region_add_subregion(&f->root, ALIAS, &f->alias);
    f->src = memory_region_get_ram_ptr(&f->source);
    f->dst = memory_region_get_ram_ptr(&f->destination);
    for (unsigned i = 0; i < SIZE; i++) {
        f->src[i] = i * 37 + 3;
    }
    memset(f->dst, 0xa5, SIZE);
    f->dev = qdev_new(TYPE_NIKON_FILL);
    object_property_set_link(OBJECT(f->dev), "memory", OBJECT(&f->root),
                             &error_abort);
    g_assert_true(sysbus_realize(SYS_BUS_DEVICE(f->dev), &error_abort));
    for (unsigned i = 0; i < 2; i++) {
        f->irq[i] = qemu_allocate_irq(irq_level, &f->level[i], i);
        sysbus_connect_irq(SYS_BUS_DEVICE(f->dev), i, f->irq[i]);
    }
    device_cold_reset(f->dev);
    return f;
}

static void cleanup(Fixture *f)
{
    qdev_unrealize(f->dev);
    object_unparent(OBJECT(f->dev));
    object_unref(OBJECT(f->dev));
    drain_call_rcu();
    for (unsigned i = 0; i < 2; i++) {
        qemu_free_irq(f->irq[i]);
    }
    memory_region_del_subregion(&f->root, &f->alias);
    memory_region_del_subregion(&f->root, &f->source);
    memory_region_del_subregion(&f->root, &f->destination);
    drain_call_rcu();
    object_unparent(f->owner);
    object_unref(f->owner);
    drain_call_rcu();
    g_free(f);
}

static MemTxResult write_port(Fixture *f, unsigned engine, unsigned offset,
                              uint32_t value, MemOp size)
{
    return memory_region_dispatch_write(
        sysbus_mmio_get_region(SYS_BUS_DEVICE(f->dev), 0),
        engine * 0x40 + offset, value, size, MEMTXATTRS_UNSPECIFIED);
}

static void write_word(Fixture *f, unsigned engine, unsigned offset,
                       uint32_t value)
{
    g_assert_cmpint(write_port(f, engine, offset, value, MO_32), ==, MEMTX_OK);
}

static uint32_t read_word(Fixture *f, unsigned engine, unsigned offset)
{
    uint64_t value;

    g_assert_cmpint(memory_region_dispatch_read(
        sysbus_mmio_get_region(SYS_BUS_DEVICE(f->dev), 0),
        engine * 0x40 + offset, &value, MO_32, MEMTXATTRS_UNSPECIFIED), ==,
        MEMTX_OK);
    return value;
}

static void configure(Fixture *f, unsigned engine, unsigned flags,
                       unsigned source_pitch, unsigned destination_pitch,
                       unsigned width, unsigned rows, uint32_t source,
                       uint32_t destination)
{
    write_word(f, engine, 0, 0);
    write_word(f, engine, 4, 0x11);
    write_word(f, engine, 0x14, flags);
    write_word(f, engine, 0x18, 0);
    write_word(f, engine, 0x1c, source_pitch);
    write_word(f, engine, 0x20, destination_pitch);
    write_word(f, engine, 0x24, width);
    write_word(f, engine, 0x28, rows);
    write_word(f, engine, 0x2c, source);
    write_word(f, engine, 0x30, destination);
}

static void acknowledge(Fixture *f, unsigned engine)
{
    write_word(f, engine, 8, 0x11);
    g_assert_cmphex(read_word(f, engine, 8), ==, 0);
    g_assert_false(f->level[engine]);
}

static void test_copy(void)
{
    static const struct {
        unsigned source_pitch, destination_pitch, width, rows, x, y;
    } shapes[] = {
        { 13, 17, 9, 4, 3, 5 }, { 512, 512, 512, 1, 128, 256 },
        { 1, 1, 1, 1, 1, 1 }, { 16, 19, 7, 3, SIZE - 39, SIZE - 45 },
    };
    Fixture *f = fixture();
    uint8_t expected[SIZE], source[SIZE];

    memcpy(source, f->src, SIZE);
    for (unsigned engine = 0; engine < 2; engine++) {
        for (unsigned flag = 0; flag < 2; flag++) {
            for (unsigned i = 0; i < ARRAY_SIZE(shapes); i++) {
                memset(f->dst, 0xa5, SIZE);
                memset(expected, 0xa5, SIZE);
                for (unsigned row = 0; row < shapes[i].rows; row++) {
                    memcpy(expected + shapes[i].y +
                           row * shapes[i].destination_pitch,
                           source + shapes[i].x + row * shapes[i].source_pitch,
                           shapes[i].width);
                }
                configure(f, engine, flag * 0x10, shapes[i].source_pitch,
                          shapes[i].destination_pitch, shapes[i].width,
                          shapes[i].rows, SOURCE + shapes[i].x,
                          DESTINATION + shapes[i].y);
                write_word(f, engine, 0x10, 1);
                g_assert_cmphex(read_word(f, engine, 8), ==, 1);
                g_assert_cmphex(read_word(f, engine, 0xc), ==, 0);
                g_assert_cmphex(read_word(f, engine, 0x10), ==, 0);
                g_assert_true(f->level[engine]);
                g_assert_cmpmem(f->dst, SIZE, expected, SIZE);
                g_assert_cmpmem(f->src, SIZE, source, SIZE);
                acknowledge(f, engine);
            }
        }
    }
    cleanup(f);
}

static void test_zero(void)
{
    Fixture *f = fixture();
    uint8_t expected[SIZE];

    for (unsigned engine = 0; engine < 2; engine++) {
        for (unsigned flag = 0; flag < 2; flag++) {
            memset(f->dst, 0xa5, SIZE);
            memset(expected, 0xa5, SIZE);
            for (unsigned row = 0; row < 3; row++) {
                memset(expected + row * 17, 0, 9);
            }
            configure(f, engine, 1 + flag * 0x10, 0, 17, 9, 3,
                      0, DESTINATION);
            write_word(f, engine, 0x10, 1);
            g_assert_cmphex(read_word(f, engine, 8), ==, 1);
            g_assert_cmpmem(f->dst, SIZE, expected, SIZE);
            acknowledge(f, engine);
        }
    }
    cleanup(f);
}

static void test_bus(void)
{
    static const struct {
        unsigned source_pitch, destination_pitch, rows;
        uint32_t source, destination;
    } holes[] = {
        { SIZE, 16, 2, SOURCE, DESTINATION },
        { 16, SIZE, 2, SOURCE, DESTINATION },
        { 16, 16, 1, SOURCE + SIZE - 4, DESTINATION },
        { 16, 16, 1, SOURCE, DESTINATION + SIZE - 4 },
    };
    Fixture *f = fixture();
    uint8_t expected[SIZE];

    memset(expected, 0xa5, SIZE);
    for (unsigned engine = 0; engine < 2; engine++) {
        /* Read-only source RAM is a legitimate DMA input. */
        memory_region_set_readonly(&f->source, true);
        configure(f, engine, 0x10, 16, 16, 8, 2, SOURCE, DESTINATION);
        write_word(f, engine, 0x10, 1);
        g_assert_cmphex(read_word(f, engine, 8), ==, 1);
        g_assert_cmpmem(f->dst, 8, f->src, 8);
        g_assert_cmpmem(f->dst + 16, 8, f->src + 16, 8);
        acknowledge(f, engine);
        memory_region_set_readonly(&f->source, false);
        memset(f->dst, 0xa5, SIZE);
        memory_region_set_readonly(&f->destination, true);
        configure(f, engine, 0, 16, 16, 8, 2, SOURCE, DESTINATION);
        write_word(f, engine, 0x10, 1);
        g_assert_cmphex(read_word(f, engine, 8), ==, 0x10);
        g_assert_true(f->level[engine]);
        g_assert_cmpmem(f->dst, SIZE, expected, SIZE);
        acknowledge(f, engine);
        memory_region_set_readonly(&f->destination, false);
        for (unsigned i = 0; i < ARRAY_SIZE(holes); i++) {
            configure(f, engine, 0x10, holes[i].source_pitch,
                      holes[i].destination_pitch, 8, holes[i].rows,
                      holes[i].source, holes[i].destination);
            write_word(f, engine, 0x10, 1);
            g_assert_cmphex(read_word(f, engine, 8), ==, 0x10);
            g_assert_true(f->level[engine]);
            g_assert_cmpmem(f->dst, SIZE, expected, SIZE);
            acknowledge(f, engine);
        }
    }
    cleanup(f);
}

static void test_overlap(void)
{
    Fixture *f = fixture();
    uint8_t expected[SIZE];

    memcpy(expected, f->src, SIZE);
    for (unsigned engine = 0; engine < 2; engine++) {
        for (unsigned alias = 0; alias < 2; alias++) {
            configure(f, engine, 0x10, 16, 16, 12, 2, SOURCE,
                      (alias ? ALIAS : SOURCE) + 4);
            write_word(f, engine, 0x10, 0);
            g_assert_cmpint(write_port(f, engine, 0x10, 1, MO_32), ==,
                            MEMTX_ERROR);
            g_assert_cmphex(read_word(f, engine, 0x10), ==, 2);
            g_assert_cmphex(read_word(f, engine, 8), ==, 0);
            g_assert_false(f->level[engine]);
            g_assert_cmpmem(f->src, SIZE, expected, SIZE);
        }
        /* Aliasing a RAM block alone does not make disjoint extents illegal. */
        configure(f, engine, 0, 16, 16, 8, 2, SOURCE, ALIAS + 64);
        write_word(f, engine, 0x10, 1);
        g_assert_cmphex(read_word(f, engine, 8), ==, 1);
        g_assert_cmpmem(f->src + 64, 8, expected, 8);
        g_assert_cmpmem(f->src + 80, 8, expected + 16, 8);
        acknowledge(f, engine);
        memcpy(f->src, expected, SIZE);
    }
    cleanup(f);
}

static void test_negative(void)
{
    static const struct { unsigned offset; uint32_t value; } invalid[] = {
        { 0x18, 1 }, { 0x1c, 0 }, { 0x1c, 7 },
        { 0x20, 7 }, { 0x24, 0 }, { 0x28, 0 },
        { 0x2c, 0xfffffffc }, { 0x30, 0xfffffffc },
    };
    static const unsigned flags[] = { 2, 4, 0x20, 0x12, 0xffffffff };
    Fixture *f = fixture();
    uint8_t expected[SIZE];

    memset(expected, 0xa5, SIZE);
    for (unsigned engine = 0; engine < 2; engine++) {
        /* Offset 0 is engine enable: 0 and 1 are accepted; > 1 is rejected. */
        g_assert_cmpint(write_port(f, engine, 0, 1, MO_32), ==, MEMTX_OK);
        g_assert_cmpint(write_port(f, engine, 0, 2, MO_32), ==, MEMTX_ERROR);
        configure(f, engine, 0x10, 16, 16, 8, 2, SOURCE, DESTINATION);
        write_word(f, engine, 0, 1);
        g_assert_cmpint(write_port(f, engine, 0x10, 1, MO_32), ==, MEMTX_OK);
        g_assert_cmphex(read_word(f, engine, 8), ==, 1);
        acknowledge(f, engine);
        memset(f->dst, 0xa5, SIZE);

        for (unsigned i = 0; i < ARRAY_SIZE(invalid); i++) {
            device_cold_reset(f->dev);
            configure(f, engine, 0x10, 16, 16, 8, 2, SOURCE, DESTINATION);
            write_word(f, engine, invalid[i].offset, invalid[i].value);
            g_assert_cmpint(write_port(f, engine, 0x10, 1, MO_32), ==,
                            MEMTX_ERROR);
            g_assert_cmphex(read_word(f, engine, 8), ==, 0);
            g_assert_false(f->level[engine]);
            g_assert_cmpmem(f->dst, SIZE, expected, SIZE);
        }
        for (unsigned i = 0; i < ARRAY_SIZE(flags); i++) {
            g_assert_cmpint(write_port(f, engine, 0x14, flags[i], MO_32), ==,
                            MEMTX_ERROR);
        }
        configure(f, engine, 0x11, 0, 16, 8, 2, 0, DESTINATION);
        write_word(f, engine, 0x18, 0x01000000);
        g_assert_cmpint(write_port(f, engine, 0x10, 1, MO_32), ==,
                        MEMTX_ERROR);
        g_assert_cmpmem(f->dst, SIZE, expected, SIZE);
        g_assert_cmpint(write_port(f, engine, 0x1c, 0x10000, MO_32), ==,
                        MEMTX_ERROR);
        g_assert_cmpint(write_port(f, engine, 0x2c, SOURCE, MO_16), ==,
                        MEMTX_ERROR);
        g_assert_cmpint(write_port(f, engine, 0x34, 0, MO_32), ==,
                        MEMTX_ERROR);
    }
    cleanup(f);
}

static void test_lifecycle(void)
{
    Fixture *f = fixture();
    uint8_t expected[SIZE];

    for (unsigned engine = 0; engine < 2; engine++) {
        g_assert_cmpint(write_port(f, engine, 0x10, 1, MO_32), ==,
                        MEMTX_ERROR);
        configure(f, engine, 0x10, 16, 16, 8, 2, SOURCE,
                  DESTINATION + engine * 64);
        write_word(f, engine, 0x10, 1);
        g_assert_cmphex(read_word(f, engine, 8), ==, 1);
        g_assert_true(f->level[engine]);
        g_assert_cmpint(write_port(f, engine, 0x10, 1, MO_32), ==,
                        MEMTX_ERROR);
        g_assert_cmpint(write_port(f, engine, 0x30, DESTINATION, MO_32), ==,
                        MEMTX_ERROR);
    }
    for (unsigned engine = 0; engine < 2; engine++) {
        write_word(f, engine, 4, 0);
        g_assert_false(f->level[engine]);
        write_word(f, engine, 4, 0x11);
        g_assert_true(f->level[engine]);
        acknowledge(f, engine);
        memcpy(expected, f->dst, SIZE);
        write_word(f, engine, 0x10, 0);
        g_assert_cmphex(read_word(f, engine, 0x10), ==, 2);
        g_assert_cmphex(read_word(f, engine, 8), ==, 0);
        g_assert_false(f->level[engine]);
        g_assert_cmpmem(f->dst, SIZE, expected, SIZE);
    }
    device_cold_reset(f->dev);
    for (unsigned engine = 0; engine < 2; engine++) {
        g_assert_cmphex(read_word(f, engine, 8), ==, 0);
        g_assert_cmphex(read_word(f, engine, 0x2c), ==, 0);
        g_assert_cmphex(read_word(f, engine, 0x10), ==, 0);
        g_assert_false(f->level[engine]);
        g_assert_cmpint(write_port(f, engine, 0x10, 1, MO_32), ==,
                        MEMTX_ERROR);
        g_assert_cmpmem(f->dst, SIZE, expected, SIZE);
    }
    cleanup(f);
}

int main(int argc, char **argv)
{
    g_autofree char *directory = g_dir_make_tmp("nikonXXXXXX", NULL);
    g_autofree char *socket = g_build_filename(directory, "qtest", NULL);
    g_autofree char *chardev = g_strdup_printf("unix:%s,server=on,wait=off",
                                             socket);
    const char *const arguments[] = {
        argv[0], "-machine", "none", "-accel", "qtest", "-display", "none",
        "-nodefaults", "-qtest", chardev, NULL
    };
    g_auto(GStrv) qemu_argv = g_new0(char *, G_N_ELEMENTS(arguments));
    int result;

    g_assert_nonnull(directory);
    g_test_init(&argc, &argv, NULL);
    for (unsigned i = 0; i < G_N_ELEMENTS(arguments) - 1; i++) {
        qemu_argv[i] = g_strdup(arguments[i]);
    }
    qemu_init(G_N_ELEMENTS(arguments) - 1, qemu_argv);
    g_test_add_func("/nikon/fill/copy", test_copy);
    g_test_add_func("/nikon/fill/zero", test_zero);
    g_test_add_func("/nikon/fill/bus", test_bus);
    g_test_add_func("/nikon/fill/overlap", test_overlap);
    g_test_add_func("/nikon/fill/negative", test_negative);
    g_test_add_func("/nikon/fill/lifecycle", test_lifecycle);
    result = g_test_run();
    qemu_cleanup(result);
    unlink(socket);
    rmdir(directory);
    return result;
}
