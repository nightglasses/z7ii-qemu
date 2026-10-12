/* Stateful notification masks, not stored mailbox replies.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/rcu.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
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
                                  uint32_t value, MemOp size)
{
    MemoryRegion *regs = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);

    return memory_region_dispatch_write(regs, offset, value, size,
                                         MEMTXATTRS_UNSPECIFIED);
}

static void test_notification_masks(void)
{
    DeviceState *dev = qdev_new(TYPE_NIKON_BRIDGE);
    uint64_t value;

    object_property_add_child(object_get_objects_root(), "bridge-flags[*]",
                              OBJECT(dev));
    g_assert_true(sysbus_realize(SYS_BUS_DEVICE(dev), &error_abort));
    device_cold_reset(dev);

    g_assert_cmpint(read_register(dev, 0x6004, &value, MO_32), ==, MEMTX_OK);
    g_assert_cmphex(value, ==, 0);
    g_assert_cmpint(write_register(dev, 0x6008, 1, MO_32), ==, MEMTX_OK);
    g_assert_cmpint(write_register(dev, 0x6008, 4, MO_32), ==, MEMTX_OK);
    g_assert_cmpint(read_register(dev, 0x6004, &value, MO_32), ==, MEMTX_OK);
    g_assert_cmphex(value, ==, 5);
    /* Zero is a no-op in both write-one masks, not a stored zero reply. */
    g_assert_cmpint(write_register(dev, 0x6008, 0, MO_32), ==, MEMTX_OK);
    g_assert_cmpint(write_register(dev, 0x600c, 0, MO_32), ==, MEMTX_OK);
    g_assert_cmpint(write_register(dev, 0x600c, 1, MO_32), ==, MEMTX_OK);
    g_assert_cmpint(read_register(dev, 0x6004, &value, MO_32), ==, MEMTX_OK);
    g_assert_cmphex(value, ==, 4);
    /* The unqualified first getter word stays blocked in the default model. */
    g_assert_cmpint(read_register(dev, 0x6000, &value, MO_32), ==,
                    MEMTX_ERROR);
    /* Unknown directions, widths and neighboring words remain errors. */
    g_assert_cmpint(write_register(dev, 0x6004, 0, MO_32), ==, MEMTX_ERROR);
    g_assert_cmpint(write_register(dev, 0x6008, 0, MO_16), ==, MEMTX_ERROR);
    g_assert_cmpint(read_register(dev, 0x6008, &value, MO_32), ==,
                    MEMTX_ERROR);
    g_assert_cmpint(read_register(dev, 0x6010, &value, MO_32), ==,
                    MEMTX_ERROR);
    g_assert_cmpint(read_register(dev, 0xe004, &value, MO_32), ==,
                    MEMTX_ERROR);
    g_assert_cmpint(read_register(dev, 0x6004, &value, MO_32), ==, MEMTX_OK);
    g_assert_cmphex(value, ==, 4);
    g_assert_cmpint(write_register(dev, 0xc5e0, 0xf000f, MO_32), ==,
                    MEMTX_OK);
    g_assert_cmpint(read_register(dev, 0x6004, &value, MO_32), ==,
                    MEMTX_ERROR);
    g_assert_cmpint(write_register(dev, 0xc5e0, 0, MO_32), ==, MEMTX_OK);
    g_assert_cmpint(read_register(dev, 0x6004, &value, MO_32), ==, MEMTX_OK);
    g_assert_cmphex(value, ==, 0);

    qdev_unrealize(dev);
    drain_call_rcu();
    object_unparent(OBJECT(dev));
    object_unref(OBJECT(dev));
    drain_call_rcu();
}

static void irq_level(void *opaque, int n, int level)
{
    *(bool *)opaque = !!level;
}

static uint32_t read_word(DeviceState *dev, hwaddr offset)
{
    uint64_t value;

    g_assert_cmpint(read_register(dev, offset, &value, MO_32), ==, MEMTX_OK);
    return value;
}

static void write_word(DeviceState *dev, hwaddr offset, uint32_t value)
{
    g_assert_cmpint(write_register(dev, offset, value, MO_32), ==, MEMTX_OK);
}

static void test_notification_profile(void)
{
    DeviceState *dev[2];
    qemu_irq irq[2];
    bool level[2] = { false, false };
    uint64_t value;

    for (unsigned i = 0; i < 2; i++) {
        dev[i] = qdev_new(TYPE_NIKON_BRIDGE);
        object_property_add_child(object_get_objects_root(), "notify[*]",
                                  OBJECT(dev[i]));
        qdev_prop_set_bit(dev[i], "notification-v1", true);
        irq[i] = qemu_allocate_irq(irq_level, &level[i], 0);
        sysbus_connect_irq(SYS_BUS_DEVICE(dev[i]), 0, irq[i]);
        g_assert_true(sysbus_realize(SYS_BUS_DEVICE(dev[i]), &error_abort));
        device_cold_reset(dev[i]);
    }
    nikon_bridge_connect_notifications(dev[0], dev[1]);
    for (unsigned i = 0; i < 2; i++) {
        g_assert_cmphex(read_word(dev[i], 0x6004), ==, 0);
        g_assert_cmphex(read_word(dev[i], 0xe004), ==, 0);
        g_assert_cmphex(read_word(dev[i], 0xd020), ==, 0);
        g_assert_false(level[i]);
    }
    /* Only an actual set-mask write creates the experimental receiver edge. */
    write_word(dev[0], 0x6008, 1);
    g_assert_cmphex(read_word(dev[0], 0x6004), ==, 1);
    g_assert_cmphex(read_word(dev[1], 0xe004), ==, 1);
    g_assert_cmphex(read_word(dev[1], 0xd020), ==, 0x8000);
    g_assert_cmphex(read_word(dev[0], 0xd020), ==, 0);
    g_assert_false(level[1]);
    write_word(dev[1], 0xd024, 0x8000);
    g_assert_true(level[1]);
    write_word(dev[1], 0xd028, 0x8000);
    g_assert_false(level[1]);
    g_assert_cmphex(read_word(dev[1], 0xe004), ==, 1);
    write_word(dev[0], 0x6008, 1);
    g_assert_false(level[1]);
    write_word(dev[0], 0x6008, 4);
    g_assert_true(level[1]);
    g_assert_cmphex(read_word(dev[1], 0xe004), ==, 5);
    /* RX acknowledgment changes the real sender's flag word, not a reply. */
    write_word(dev[1], 0xe00c, 1);
    g_assert_cmphex(read_word(dev[0], 0x6004), ==, 4);
    g_assert_cmphex(read_word(dev[1], 0xe004), ==, 4);
    write_word(dev[1], 0xe00c, 4);
    write_word(dev[1], 0xd028, 0x8000);
    g_assert_false(level[1]);
    write_word(dev[0], 0x6008, 0);
    write_word(dev[1], 0xe00c, 0);
    g_assert_false(level[1]);
    g_assert_cmphex(read_word(dev[0], 0x6004), ==, 0);

    /* Opposite direction is independent; experimental raw mirror is live. */
    write_word(dev[1], 0x6008, 2);
    g_assert_cmphex(read_word(dev[0], 0xe004), ==, 2);
    g_assert_cmphex(read_word(dev[0], 0xe000), ==, 2);
    g_assert_cmphex(read_word(dev[1], 0x6000), ==, 2);
    g_assert_cmphex(read_word(dev[0], 0xd020), ==, 0x8000);
    g_assert_cmpint(write_register(dev[0], 0xe004, 0, MO_32), ==,
                    MEMTX_ERROR);
    g_assert_cmpint(write_register(dev[0], 0xe00c, 2, MO_16), ==,
                    MEMTX_ERROR);
    g_assert_cmphex(read_word(dev[0], 0xe004), ==, 2);

    /* Reset cannot consume the other guest's outgoing state or invent edges. */
    write_word(dev[0], 0x6008, 1);
    write_word(dev[0], 0xc5e0, 0xf000f);
    g_assert_cmpint(read_register(dev[0], 0xe004, &value, MO_32), ==,
                    MEMTX_ERROR);
    g_assert_cmpint(write_register(dev[1], 0x6008, 4, MO_32), ==,
                    MEMTX_ERROR);
    g_assert_cmphex(read_word(dev[1], 0x6004), ==, 2);
    write_word(dev[0], 0xc5e0, 0);
    g_assert_cmphex(read_word(dev[0], 0xe004), ==, 2);
    g_assert_cmphex(read_word(dev[1], 0xe004), ==, 0);
    g_assert_cmphex(read_word(dev[0], 0xd020), ==, 0);
    nikon_bridge_connect_notifications(dev[0], NULL);
    g_assert_cmpint(read_register(dev[0], 0xe004, &value, MO_32), ==,
                    MEMTX_ERROR);
    write_word(dev[1], 0x6008, 4);
    g_assert_cmphex(read_word(dev[1], 0x6004), ==, 6);
    g_assert_cmphex(read_word(dev[0], 0xd020), ==, 0);

    for (unsigned i = 0; i < 2; i++) {
        qdev_unrealize(dev[i]);
        drain_call_rcu();
        object_unparent(OBJECT(dev[i]));
        object_unref(OBJECT(dev[i]));
        qemu_free_irq(irq[i]);
    }
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
    g_test_add_func("/nikon/bridge/notification-masks", test_notification_masks);
    g_test_add_func("/nikon/bridge/notification-profile",
                    test_notification_profile);
    result = g_test_run();
    qemu_cleanup(result);
    unlink(socket);
    rmdir(directory);
    return result;
}
