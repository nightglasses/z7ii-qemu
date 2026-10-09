/* Real command words, external event causality and cancellation.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/timer.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/nikon/command.h"
#include "system/system.h"

int (*qemu_main)(void);

typedef struct CommandFixture {
    DeviceState *dev;
    qemu_irq irq[20], request[24];
    bool levels[20], requests[24];
} CommandFixture;

static void line(void *opaque, int n, int level)
{
    bool *levels = opaque;

    levels[n] = !!level;
}

static void write_port(CommandFixture *f, unsigned bank, hwaddr port,
                       uint32_t value, MemTxResult result)
{
    MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(f->dev), bank);

    g_assert_cmpint(memory_region_dispatch_write(mr, port, value, MO_32,
                                                MEMTXATTRS_UNSPECIFIED), ==,
                    result);
}

static uint32_t read_port(CommandFixture *f, unsigned bank, hwaddr port)
{
    MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(f->dev), bank);
    uint64_t value;

    g_assert_cmpint(memory_region_dispatch_read(mr, port, &value, MO_32,
                                               MEMTXATTRS_UNSPECIFIED), ==,
                    MEMTX_OK);
    return value;
}

static void test_command(void)
{
    CommandFixture f = { .dev = qdev_new(TYPE_NIKON_COMMAND) };
    NikonCommandRequest request, second;
    uint32_t words[9];
    uint64_t value;
    unsigned i;

    for (i = 0; i < G_N_ELEMENTS(f.irq); i++) {
        f.irq[i] = qemu_allocate_irq(line, f.levels, i);
        sysbus_connect_irq(SYS_BUS_DEVICE(f.dev), i, f.irq[i]);
    }
    for (i = 0; i < G_N_ELEMENTS(f.request); i++) {
        f.request[i] = qemu_allocate_irq(line, f.requests, i);
        qdev_connect_gpio_out_named(f.dev, "request", i, f.request[i]);
    }
    g_assert_true(sysbus_realize(SYS_BUS_DEVICE(f.dev), &error_abort));
    device_cold_reset(f.dev);
    g_assert_cmpint(memory_region_dispatch_write(
                        sysbus_mmio_get_region(SYS_BUS_DEVICE(f.dev), 1),
                        0x100, 1, MO_16, MEMTXATTRS_UNSPECIFIED), ==,
                    MEMTX_ERROR);
    g_assert_cmphex(read_port(&f, 1, 0x900), ==, 0);
    g_assert_cmpint(memory_region_dispatch_read(
                        sysbus_mmio_get_region(SYS_BUS_DEVICE(f.dev), 1),
                        0x88, &value, MO_32, MEMTXATTRS_UNSPECIFIED), ==,
                    MEMTX_ERROR);
    write_port(&f, 1, 0x100, 4, MEMTX_ERROR);
    g_assert_false(nikon_command_receive(f.dev, 1, 0, 2, words, 9));

    /* Original bank1 slot0 request uses control3, parameter4 and real words. */
    write_port(&f, 1, 0x280, 1, MEMTX_OK);
    write_port(&f, 1, 0x280, 2, MEMTX_OK);
    g_assert_cmphex(read_port(&f, 1, 0x280), ==, 1);
    g_assert_cmphex(read_port(&f, 1, 0x900), ==, 1U << 3);
    write_port(&f, 1, 0x284, 3, MEMTX_OK);
    write_port(&f, 1, 0x290, 4, MEMTX_OK);
    for (i = 0; i < G_N_ELEMENTS(words); i++) {
        words[i] = 0xa5003100 ^ (i * 0x10203);
        write_port(&f, 1, 0x2c0 + i * 4, words[i], MEMTX_OK);
    }
    write_port(&f, 1, 0x288, 1, MEMTX_OK);
    g_assert_true(f.requests[11]);
    g_assert_false(f.levels[16]);
    g_assert_true(nikon_command_request(f.dev, 1, 3, &request));
    g_assert_cmphex(request.slots, ==, 1);
    g_assert_cmphex(request.control, ==, 3);
    g_assert_cmphex(request.parameter, ==, 4);
    g_assert_cmphex(request.written, ==, 0x1ff);
    g_assert_cmpmem(request.words, sizeof(words), words, sizeof(words));
    qemu_clock_advance_virtual_time(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                                    1000000000);
    g_assert_true(f.requests[11]);
    g_assert_false(f.levels[16]);
    write_port(&f, 1, 0x2c0, 0, MEMTX_ERROR);
    g_assert_true(nikon_command_complete(f.dev, 1, 3, request.token, 0x55));
    g_assert_false(f.requests[11]);
    g_assert_true(f.levels[16]);
    g_assert_cmphex(read_port(&f, 1, 0x80), ==, 5U << 7);
    g_assert_cmphex(read_port(&f, 1, 0x2b8), ==, 0x55);
    write_port(&f, 1, 0x2b4, 0x54, MEMTX_ERROR);
    g_assert_true(f.levels[16]);
    write_port(&f, 1, 0x2b4, 0x55, MEMTX_OK);
    g_assert_false(f.levels[16]);
    write_port(&f, 1, 0x280, 0, MEMTX_OK);
    g_assert_cmphex(read_port(&f, 1, 0x900), ==, 0);

    /* Actual incoming nine-word frame owns engine7 until the slot1 ACK. */
    g_assert_true(nikon_command_receive(f.dev, 2, 7, 1, words, 9));
    g_assert_true(f.levels[19]);
    g_assert_false(f.levels[18]);
    g_assert_cmphex(read_port(&f, 2, 0x84), ==, 9U << 7);
    g_assert_cmphex(read_port(&f, 2, 0x900), ==, 0x80);
    for (i = 0; i < G_N_ELEMENTS(words); i++) {
        g_assert_cmphex(read_port(&f, 2, 0x4c0 + i * 4), ==, words[i]);
    }
    g_assert_false(nikon_command_receive(f.dev, 2, 6, 1, words, 9));
    write_port(&f, 2, 0x494, 1, MEMTX_ERROR);
    g_assert_true(f.levels[19]);
    write_port(&f, 2, 0x494, 2, MEMTX_OK);
    g_assert_false(f.levels[19]);
    g_assert_cmphex(read_port(&f, 2, 0x900), ==, 0);

    /* All20 actual slot outputs are independent, including bank0 slot15. */
    for (i = 0; i < 20; i++) {
        unsigned bank = i < 16 ? 0 : i < 18 ? 1 : 2;
        unsigned slot = i < 16 ? i : i < 18 ? i - 16 : i - 18;

        g_assert_true(nikon_command_receive(f.dev, bank, 0, slot, words, 9));
        g_assert_true(f.levels[i]);
        write_port(&f, bank, 0x114, 1U << slot, MEMTX_OK);
        g_assert_false(f.levels[i]);
    }

    /* Reset/cancel retires the token; an old completion cannot finish a reuse. */
    write_port(&f, 0, 0x100, 1, MEMTX_OK);
    write_port(&f, 0, 0x140, words[0], MEMTX_OK);
    write_port(&f, 0, 0x108, 1, MEMTX_OK);
    g_assert_true(nikon_command_request(f.dev, 0, 0, &request));
    device_cold_reset(f.dev);
    g_assert_false(nikon_command_complete(f.dev, 0, 0, request.token, 1));
    write_port(&f, 0, 0x100, 1, MEMTX_OK);
    write_port(&f, 0, 0x140, words[0], MEMTX_OK);
    write_port(&f, 0, 0x108, 1, MEMTX_OK);
    g_assert_true(nikon_command_request(f.dev, 0, 0, &second));
    g_assert_cmpuint(second.token, !=, request.token);
    g_assert_false(nikon_command_complete(f.dev, 0, 0, request.token, 1));
    g_assert_false(f.levels[0]);
    write_port(&f, 0, 0x100, 0, MEMTX_OK);
    g_assert_false(f.requests[0]);
    g_assert_false(nikon_command_complete(f.dev, 0, 0, second.token, 1));

    qdev_unrealize(f.dev);
    object_unparent(OBJECT(f.dev));
    object_unref(OBJECT(f.dev));
    for (i = 0; i < G_N_ELEMENTS(f.irq); i++) {
        qemu_free_irq(f.irq[i]);
    }
    for (i = 0; i < G_N_ELEMENTS(f.request); i++) {
        qemu_free_irq(f.request[i]);
    }
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
    g_test_add_func("/nikon/command/causality", test_command);
    result = g_test_run();
    qemu_cleanup(result);
    unlink(socket);
    rmdir(directory);
    return result;
}
