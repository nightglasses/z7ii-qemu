/* External byte-status inputs consumed by the original dispatcher.
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

static MemTxResult read_register(DeviceState *dev, hwaddr offset,
                                 uint64_t *value, MemOp size)
{
    MemoryRegion *regs = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);

    return memory_region_dispatch_read(regs, offset, value, size,
                                       MEMTXATTRS_UNSPECIFIED);
}

static MemTxResult write_register(DeviceState *dev, hwaddr offset,
                                  uint64_t value, MemOp size)
{
    MemoryRegion *regs = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);

    return memory_region_dispatch_write(regs, offset, value, size,
                                        MEMTXATTRS_UNSPECIFIED);
}

static void test_external_status(void)
{
    DeviceState *dev = qdev_new(TYPE_NIKON_BYTE_EVENT_ROUTER);
    static const uint8_t expected[] = { 0x02, 0x12, 0x32, 0x72, 0xf2 };
    uint64_t value;

    object_property_add_child(object_get_objects_root(), "dispatcher-status[*]",
                              OBJECT(dev));
    g_assert_true(sysbus_realize(SYS_BUS_DEVICE(dev), &error_abort));
    device_cold_reset(dev);

    g_assert_cmpint(read_register(dev, 0xc010, &value, MO_8), ==, MEMTX_OK);
    g_assert_cmphex(value, ==, 0);
    for (unsigned i = 0; i < G_N_ELEMENTS(expected); i++) {
        qemu_set_irq(qdev_get_gpio_in_named(dev, "external-status", i), 1);
        g_assert_cmpint(read_register(dev, 0xc010, &value, MO_8), ==,
                        MEMTX_OK);
        g_assert_cmphex(value, ==, expected[i]);
    }
    g_assert_cmpint(read_register(dev, 0xc010, &value, MO_16), ==,
                    MEMTX_ERROR);
    g_assert_cmpint(write_register(dev, 0xc010, 0, MO_8), ==, MEMTX_ERROR);

    /* Reset does not manufacture edges or change external pin levels. */
    device_cold_reset(dev);
    g_assert_cmpint(read_register(dev, 0xc010, &value, MO_8), ==, MEMTX_OK);
    g_assert_cmphex(value, ==, 0xf2);
    for (unsigned i = 0; i < G_N_ELEMENTS(expected); i++) {
        qemu_set_irq(qdev_get_gpio_in_named(dev, "external-status", i), 0);
    }
    g_assert_cmpint(read_register(dev, 0xc010, &value, MO_8), ==, MEMTX_OK);
    g_assert_cmphex(value, ==, 0);

    qdev_unrealize(dev);
    drain_call_rcu();
    object_unparent(OBJECT(dev));
    object_unref(OBJECT(dev));
    drain_call_rcu();
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
    g_test_add_func("/nikon/dispatcher/external-status", test_external_status);
    result = g_test_run();
    qemu_cleanup(result);
    unlink(socket);
    rmdir(directory);
    return result;
}
