/* Native QEMU device protocol, real MMIO dispatch and producer isolation.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "hw/nikon/readout.h"
#include "system/system.h"

int (*qemu_main)(void);

typedef struct ReadoutFixture {
    DeviceState *dev;
    MemoryRegion *regs;
    qemu_irq irq, request;
    int pending, active;
} ReadoutFixture;

static void readout_line(void *opaque, int n, int level)
{
    int *line = opaque;

    *line = level;
}

static MemTxResult readout_get(ReadoutFixture *f, hwaddr offset,
                              uint64_t *value, MemOp size)
{
    return memory_region_dispatch_read(f->regs, offset, value, size,
                                       MEMTXATTRS_UNSPECIFIED);
}

static uint32_t readout_word(ReadoutFixture *f, hwaddr offset)
{
    uint64_t value;

    g_assert_cmpint(readout_get(f, offset, &value, MO_32), ==, MEMTX_OK);
    return value;
}

static void readout_put(ReadoutFixture *f, hwaddr offset, uint32_t value)
{
    g_assert_cmpint(memory_region_dispatch_write(f->regs, offset, value, MO_32,
                                                MEMTXATTRS_UNSPECIFIED), ==,
                    MEMTX_OK);
}

static void readout_reject(ReadoutFixture *f, hwaddr offset, MemOp size,
                           bool write, uint32_t value)
{
    uint64_t result;
    MemTxResult transaction;

    transaction = write ? memory_region_dispatch_write(
                          f->regs, offset, value, size, MEMTXATTRS_UNSPECIFIED) :
                          readout_get(f, offset, &result, size);
    g_assert_cmpint(transaction, !=, MEMTX_OK);
}

static uint64_t readout_submit(ReadoutFixture *f)
{
    readout_put(f, 0, 1);
    g_assert_cmpint(f->active, ==, 0);
    readout_put(f, 0x300, 1);
    g_assert_cmpint(f->active, ==, 1);
    g_assert_cmpuint(nikon_results_generation(f->dev), !=, 0);
    return nikon_results_generation(f->dev);
}

static void test_readout(void)
{
    /* All fields supplied by 9AE6C..9AF88 and the independent setters. */
    static const struct {
        hwaddr offset;
        uint32_t value;
    } config[] = {
        { 0x20, 0x01234567 }, { 0x24, 0x89abcdef }, { 0x28, 0xfedcba98 },
        { 0x40, 0x7fff }, { 0x44, 0x1773 },
        { 0x48, 0x1234 }, { 0x4c, 0x5678 }, { 0x50, 0xffff },
        { 0x54, 0x773 },
        { 0x58, 0xabcd }, { 0x5c, 0xef01 }, { 0x60, 0x2345 },
        { 0x64, 0x6789 }, { 0x68, 0xabcd }, { 0x6c, 0xef01 },
        { 0x70, 0x3fffbfd },
        { 0x74, 0x2345 }, { 0x78, 0x6789 }, { 0x7c, 0xabcd },
        { 0x80, 0x13579bdf }, { 0x84, 0x2468ace0 }, { 0x88, 0xfedcba98 },
        { 0x8c, 0x3fff }, { 0x90, 0x3fff }, { 0x94, 0x87654321 },
        { 0x98, 0x12345678 }, { 0x9c, 0x23456789 }, { 0xa0, 0x3456789a },
        { 0xa4, 0x456789ab }, { 0xa8, 0x56789abc }, { 0xac, 0x6789abcd },
        { 0xb0, 0x789abcde }, { 0xb4, 0x89abcdef }, { 0xb8, 0x9abcdef0 },
        { 0xbc, 0xff }, { 0x304, 1 }, { 0x308, 3 }, { 0x30c, 3 },
    };
    ReadoutFixture f = { 0 };
    uint32_t first[NIKON_READOUT_WORDS], second[NIKON_READOUT_WORDS];
    uint64_t generation, next;

    f.dev = qdev_new(TYPE_NIKON_READOUT);
    f.irq = qemu_allocate_irq(readout_line, &f.pending, 0);
    f.request = qemu_allocate_irq(readout_line, &f.active, 0);
    sysbus_connect_irq(SYS_BUS_DEVICE(f.dev), 0, f.irq);
    qdev_connect_gpio_out_named(f.dev, "request", 0, f.request);
    g_assert_true(sysbus_realize(SYS_BUS_DEVICE(f.dev), &error_abort));
    f.regs = sysbus_mmio_get_region(SYS_BUS_DEVICE(f.dev), 0);
    device_cold_reset(f.dev);

    for (unsigned i = 0; i < G_N_ELEMENTS(config); i++) {
        g_assert_cmphex(readout_word(&f, config[i].offset), ==, 0);
        readout_put(&f, config[i].offset, config[i].value);
        g_assert_cmphex(readout_word(&f, config[i].offset), ==, config[i].value);
    }
    g_assert_cmpint(f.pending, ==, 0);
    g_assert_cmpint(f.active, ==, 0);
    readout_reject(&f, 0x40, MO_32, true, 0x8000);
    readout_reject(&f, 0x44, MO_32, true, 4);
    readout_reject(&f, 0x54, MO_32, true, 8);
    readout_reject(&f, 0x70, MO_32, true, 2);
    readout_reject(&f, 0x8c, MO_32, true, 0x4000);
    readout_reject(&f, 0x304, MO_32, true, 2);
    readout_reject(&f, 0x308, MO_32, true, 4);
    readout_reject(&f, 0x304, MO_8, true, 1);
    readout_reject(&f, 0x40, MO_16, true, 1);
    readout_reject(&f, 0x41, MO_32, true, 1);
    readout_reject(&f, 8, MO_32, false, 0);
    readout_reject(&f, 0x1a0, MO_32, false, 0);
    readout_reject(&f, 0x2a0, MO_32, false, 0);
    readout_reject(&f, 4, MO_32, false, 0);
    readout_reject(&f, 0x30, MO_32, false, 0);
    readout_reject(&f, 0x30, MO_32, true, 1);
    readout_reject(&f, 0x100, MO_32, false, 0);
    readout_reject(&f, 0x200, MO_32, false, 0);
    g_assert_cmphex(readout_word(&f, 0x40), ==, 0x7fff);
    g_assert_cmphex(readout_word(&f, 0x304), ==, 1);

    nikon_results_state(f.dev, true);
    g_assert_cmpuint(readout_word(&f, 4), ==, 1);
    nikon_results_state(f.dev, false);
    g_assert_cmpuint(readout_word(&f, 4), ==, 0);
    g_assert_cmpint(f.pending, ==, 0);

    for (unsigned i = 0; i < NIKON_READOUT_WORDS; i++) {
        first[i] = 0xfedc0000 | (0x100 + i);
        second[i] = 0xab000000 | ((0x200 + i) << 8) | i;
    }
    generation = readout_submit(&f);
    g_assert_false(nikon_results_publish(f.dev, generation + 1, 1,
                                         first, second));
    g_assert_false(nikon_results_publish(f.dev, generation, 2, first, second));
    g_assert_false(nikon_results_publish(f.dev, generation, 1, NULL, second));
    g_assert_cmpint(f.pending, ==, 0);
    g_assert_true(nikon_results_publish(f.dev, generation, 0x15, first, second));
    g_assert_cmpint(f.active, ==, 0);
    g_assert_cmpint(f.pending, ==, 1);
    g_assert_cmphex(readout_word(&f, 0x30), ==, 0x15);
    for (unsigned i = 0; i < NIKON_READOUT_WORDS; i++) {
        uint32_t a = readout_word(&f, 0x100 + i * 4);
        uint32_t b = readout_word(&f, 0x200 + i * 4);

        g_assert_cmphex(a, ==, first[i]);
        g_assert_cmphex(b, ==, second[i]);
        /* Exactly the three halfword results emitted by 9AFA4's 8x5 loop. */
        g_assert_cmpuint((uint16_t)a, ==, 0x100 + i);
        g_assert_cmpuint((uint16_t)(b >> 8), ==, 0x200 + i);
        g_assert_cmpuint((uint8_t)b, ==, i);
    }
    readout_put(&f, 0x30, 4);
    g_assert_cmphex(readout_word(&f, 0x30), ==, 0x11);
    g_assert_cmpint(f.pending, ==, 1);
    readout_put(&f, 0x30, 0x11);
    g_assert_cmpuint(readout_word(&f, 0x30), ==, 0);
    g_assert_cmpint(f.pending, ==, 0);

    /* The original ISR resubmits before invoking its guest callback. */
    next = readout_submit(&f);
    g_assert_cmpuint(next, !=, generation);
    g_assert_cmphex(readout_word(&f, 0x19c), ==, first[39]);
    g_assert_cmphex(readout_word(&f, 0x29c), ==, second[39]);
    g_assert_false(nikon_results_publish(f.dev, generation, 1, first, second));
    readout_put(&f, 0, 0);
    readout_put(&f, 0x300, 0);
    g_assert_false(nikon_results_publish(f.dev, next, 1, first, second));
    g_assert_cmpuint(nikon_results_generation(f.dev), ==, 0);
    g_assert_cmpint(f.pending, ==, 0);
    g_assert_cmphex(readout_word(&f, 0x100), ==, first[0]);

    next = readout_submit(&f);
    readout_put(&f, 4, 1);
    g_assert_false(nikon_results_publish(f.dev, next, 1, first, second));
    g_assert_cmpint(f.active, ==, 0);
    readout_reject(&f, 4, MO_32, false, 0);
    readout_reject(&f, 0x30, MO_32, false, 0);
    readout_reject(&f, 0x100, MO_32, false, 0);
    nikon_results_state(f.dev, false);
    g_assert_cmpuint(readout_word(&f, 4), ==, 0);

    next = readout_submit(&f);
    device_cold_reset(f.dev);
    g_assert_false(nikon_results_publish(f.dev, next, 1, first, second));
    g_assert_cmpint(f.active, ==, 0);
    for (unsigned i = 0; i < G_N_ELEMENTS(config); i++) {
        g_assert_cmpuint(readout_word(&f, config[i].offset), ==, 0);
    }
    next = readout_submit(&f);
    qdev_unrealize(f.dev);
    g_assert_false(nikon_results_publish(f.dev, next, 1, first, second));
    g_assert_cmpint(f.active, ==, 0);
    g_assert_cmpint(f.pending, ==, 0);
    object_unparent(OBJECT(f.dev));
    object_unref(OBJECT(f.dev));
    qemu_free_irq(f.irq);
    qemu_free_irq(f.request);
}

int main(int argc, char **argv)
{
    g_autofree char *directory = g_dir_make_tmp("nikonXXXXXX", NULL);
    g_autofree char *socket = g_build_filename(directory, "qtest", NULL);
    g_autofree char *chardev = g_strdup_printf("unix:%s,server=on,wait=off",
                                             socket);
    char *qemu_argv[] = {
        argv[0], "-machine", "none", "-accel", "qtest", "-display", "none",
        "-nodefaults", "-qtest", chardev, NULL
    };
    int result;

    g_assert_nonnull(directory);
    g_test_init(&argc, &argv, NULL);
    qemu_init(G_N_ELEMENTS(qemu_argv) - 1, qemu_argv);
    g_test_add_func("/nikon/readout/protocol", test_readout);
    result = g_test_run();
    qemu_cleanup(result);
    unlink(socket);
    rmdir(directory);
    return result;
}
