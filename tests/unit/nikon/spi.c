/* Continuous SPI master cadence under a late virtual timer callback.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/main-loop.h"
#include "qemu/timer.h"
#include "hw/core/sysbus.h"
#include "hw/nikon/devices.h"
#include "system/cpu-timers.h"
#include "system/runstate.h"
#include "system/system.h"

int (*qemu_main)(void);

typedef struct SPIFixture {
    DeviceState *dev;
    MemoryRegion *regs;
} SPIFixture;

static SPIFixture *fixture(void)
{
    SPIFixture *f = g_new0(SPIFixture, 1);

    f->dev = qdev_new(TYPE_NIKON_SPI);
    object_property_add_child(object_get_objects_root(), "spi[*]",
                              OBJECT(f->dev));
    g_assert_true(sysbus_realize(SYS_BUS_DEVICE(f->dev), &error_abort));
    device_cold_reset(f->dev);
    f->regs = sysbus_mmio_get_region(SYS_BUS_DEVICE(f->dev), 0);
    return f;
}

static void cleanup(SPIFixture *f)
{
    qdev_unrealize(f->dev);
    object_unparent(OBJECT(f->dev));
    object_unref(OBJECT(f->dev));
    g_free(f);
}

static void write_word(SPIFixture *f, hwaddr offset, uint32_t value)
{
    g_assert_cmpint(memory_region_dispatch_write(
                        f->regs, offset, value, MO_32,
                        MEMTXATTRS_UNSPECIFIED),
                    ==, MEMTX_OK);
}

static uint32_t read_word(SPIFixture *f, hwaddr offset)
{
    uint64_t value;

    g_assert_cmpint(memory_region_dispatch_read(
                        f->regs, offset, &value, MO_32,
                        MEMTXATTRS_UNSPECIFIED),
                    ==, MEMTX_OK);
    return value;
}

static void prepare(SPIFixture *f)
{
    write_word(f, 0x60, 0);       /* Open the original active-low port gate. */
    write_word(f, 0x08, 0);       /* Divide by two when DIVENABLE is set. */
    write_word(f, 0x0c, 0x22);    /* Synchronous master with DIVENABLE. */
    write_word(f, 0x10, 0x0f00);  /* Sixteen bits per frame. */
}

static void enqueue(SPIFixture *f)
{
    for (unsigned i = 0; i < 4; i++) {
        write_word(f, 0, 0x1200 + i);
    }
}

static void test_late_callback(void)
{
    SPIFixture *f = fixture();
    int64_t start;

    prepare(f);
    enqueue(f);
    start = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    write_word(f, 0x3c, 1);

    /*
     * Model a busy main loop without stepping each intermediate deadline.
     * The independent serial clock has completed all four frames by now.
     */
    cpus_set_virtual_clock(start + 100000);
    qemu_clock_run_timers(QEMU_CLOCK_VIRTUAL);
    g_assert_cmphex(read_word(f, 0x2c), ==, 0);
    g_assert_cmphex(read_word(f, 0x30), ==, 4);
    g_assert_cmphex(read_word(f, 0x24) & 0x10, ==, 0x10);

    /* An idle FIFO starts a new cadence instead of inheriting the old one. */
    write_word(f, 0, 0x5678);
    g_assert_cmphex(read_word(f, 0x2c), ==, 1);
    qemu_clock_run_timers(QEMU_CLOCK_VIRTUAL);
    g_assert_cmphex(read_word(f, 0x2c), ==, 1);
    cpus_set_virtual_clock(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 100000);
    qemu_clock_run_timers(QEMU_CLOCK_VIRTUAL);
    g_assert_cmphex(read_word(f, 0x2c), ==, 0);

    /* Disable and reset both discard an outstanding cadence deadline. */
    enqueue(f);
    write_word(f, 0x3c, 0);
    cpus_set_virtual_clock(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 100000);
    qemu_clock_run_timers(QEMU_CLOCK_VIRTUAL);
    g_assert_cmphex(read_word(f, 0x2c), ==, 4);
    device_cold_reset(f->dev);
    prepare(f);
    enqueue(f);
    write_word(f, 0x3c, 1);
    cpus_set_virtual_clock(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 100000);
    qemu_clock_run_timers(QEMU_CLOCK_VIRTUAL);
    g_assert_cmphex(read_word(f, 0x2c), ==, 0);
    g_assert_cmphex(read_word(f, 0x30), ==, 4);
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
    vm_start();
    g_test_add_func("/nikon/spi/late-callback-cadence", test_late_callback);
    result = g_test_run();
    qemu_cleanup(result);
    unlink(socket);
    rmdir(directory);
    return result;
}
