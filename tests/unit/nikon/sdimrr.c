/* Native SDI mode-register read protocol; no camera instructions or RAM IDs.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/timer.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/nikon/devices.h"
#include "system/address-spaces.h"
#include "system/system.h"

int (*qemu_main)(void);

typedef struct Fixture {
    DeviceState *dev;
    MemoryRegion *regs;
    qemu_irq irq;
    bool level;
} Fixture;

static void observe(void *opaque, int n, int level)
{
    ((Fixture *)opaque)->level = !!level;
}

static Fixture *fixture(unsigned bank)
{
    Fixture *f = g_new0(Fixture, 1);

    f->dev = qdev_new(TYPE_NIKON_SDI);
    object_property_add_child(object_get_objects_root(), "sdi-mrr[*]",
                              OBJECT(f->dev));
    qdev_prop_set_uint32(f->dev, "bank", bank);
    g_assert_true(sysbus_realize(SYS_BUS_DEVICE(f->dev), &error_abort));
    f->irq = qemu_allocate_irq(observe, f, 0);
    sysbus_connect_irq(SYS_BUS_DEVICE(f->dev), 0, f->irq);
    device_cold_reset(f->dev);
    f->regs = sysbus_mmio_get_region(SYS_BUS_DEVICE(f->dev), 0);
    return f;
}

static void cleanup(Fixture *f)
{
    qdev_unrealize(f->dev);
    object_unparent(OBJECT(f->dev));
    object_unref(OBJECT(f->dev));
    qemu_free_irq(f->irq);
    g_free(f);
}

static MemTxResult write_result(Fixture *f, hwaddr offset, uint32_t value)
{
    return memory_region_dispatch_write(f->regs, offset, value, MO_32,
                                         MEMTXATTRS_UNSPECIFIED);
}

static void write_word(Fixture *f, hwaddr offset, uint32_t value)
{
    g_assert_cmpint(write_result(f, offset, value), ==, MEMTX_OK);
}

static MemTxResult read_result(Fixture *f, hwaddr offset, uint64_t *value)
{
    return memory_region_dispatch_read(f->regs, offset, value, MO_32,
                                        MEMTXATTRS_UNSPECIFIED);
}

static uint32_t read_word(Fixture *f, hwaddr offset)
{
    uint64_t value;

    g_assert_cmpint(read_result(f, offset, &value), ==, MEMTX_OK);
    return value;
}

static void advance(unsigned ns)
{
    qemu_clock_advance_virtual_time(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + ns);
}

static void prepare(Fixture *f)
{
    /* Minimal applied PHY/DDR configuration. Values are synthetic, not a
     * physical topology, and setup crosses the real MMIO/timer transitions.
     */
    static const struct {
        uint32_t base;
        unsigned count;
        uint32_t value;
    } groups[] = {
        { 0x100, 1, 0 }, { 0x10c, 1, 0x101 }, { 0x1004, 5, 1 },
        { 0x2408, 1, 1 }, { 0x2414, 1, 1 },
        { 0x3404, 3, 1 }, { 0x3500, 2, 1 },
        { 0x3508, 2, 0x10000000 }, { 0x3510, 1, 0 },
        { 0x3514, 1, 0x10000000 }, { 0x3600, 2, 1 },
        { 0x360c, 11, 1 }, { 0x3700, 1, 1 }, { 0x3708, 2, 1 },
        { 0x3800, 3, 1 }, { 0x3900, 2, 1 }, { 0x3a00, 2, 1 },
        { 0x3b00, 2, 1 }, { 0x10010, 6, 1 }, { 0x10030, 2, 1 },
        { 0x10040, 3, 1 }, { 0x10050, 1, 1 }, { 0x10054, 4, 1 },
        { 0x10074, 4, 1 }, { 0x10094, 4, 1 }, { 0x100b8, 1, 1 },
        { 0x100e4, 4, 0x1001 }, { 0x10128, 4, 0x10001 },
        { 0x10148, 4, 0x10001 }, { 0x10820, 4, 1 },
        { 0x10840, 1, 1 }, { 0x10850, 1, 1 }, { 0x10910, 1, 1 },
    };

    for (unsigned i = 0; i < ARRAY_SIZE(groups); i++) {
        for (unsigned word = 0; word < groups[i].count; word++) {
            write_word(f, groups[i].base + word * 4, groups[i].value);
        }
    }
    write_word(f, 0, 0);
    write_word(f, 0x2000, 0);
    write_word(f, 0x2100, 1);
    advance(10000);
    write_word(f, 0x2300, 1);
    write_word(f, 0x3000, 0);
    write_word(f, 0x3100, 0x10001);
    advance(10000);
    g_assert_true(f->level);
}

static void ack_isr(Fixture *f)
{
    /* Original MAS handlers 1020384f/102037fd read and W1C all eight words. */
    for (unsigned i = 0; i < 8; i++) {
        uint32_t status = read_word(f, 0x3300 + 4 * i);

        write_word(f, 0x3300 + 4 * i, status);
    }
}

static void test_commands(void)
{
    Fixture *bank[2] = { fixture(0), fixture(1) };

    for (unsigned i = 0; i < 2; i++) {
        Fixture *f = bank[i];

        prepare(f);
        ack_isr(f);
        write_word(f, 0x3204, 1);
        write_word(f, 0x3208, 1);
        write_word(f, 0x321c, 1);
        g_assert_cmpuint(read_word(f, 0x321c), ==, 1);
        g_assert_cmpuint(read_word(f, 0x3204), ==, 1);
        g_assert_cmpuint(read_word(f, 0x3208), ==, 1);
        for (unsigned mr = 5; mr <= 8; mr++) {
            uint32_t command = mr << 24 | 0x10001;
            uint64_t value;

            write_word(f, 0x311c, command);
            g_assert_cmphex(read_word(f, 0x311c), ==, command);
            g_assert_cmpint(read_result(f, 0x3a08, &value), ==, MEMTX_ERROR);
            advance(9999);
            g_assert_false(f->level);
            g_assert_cmpuint(read_word(f, 0x331c), ==, 0);
            advance(1);
            g_assert_true(f->level);
            g_assert_cmpuint(read_word(f, 0x331c), ==, 1);
            g_assert_cmpuint(read_word(f, 0x3a08), ==, 0);
            ack_isr(f);
            g_assert_false(f->level);
            /* The original read follows the ISR ACK. Zero is a model-invalid
             * reply; this test never asserts manufacturer or density identity.
             */
            g_assert_cmpuint(read_word(f, 0x3a08), ==, 0);
        }
    }
    cleanup(bank[0]);
    cleanup(bank[1]);
}

static void test_irq_or_mask(void)
{
    Fixture *f = fixture(0);

    prepare(f); /* Leave init done unacknowledged. */
    write_word(f, 0x321c, 1);
    write_word(f, 0x311c, 0x05010001);
    advance(10000);
    write_word(f, 0x331c, 1);
    g_assert_true(f->level); /* Init still owns the OR. */
    write_word(f, 0x3300, 1);
    g_assert_false(f->level);
    write_word(f, 0x3118, 1);
    write_word(f, 0x311c, 0x06010001);
    advance(10000);
    write_word(f, 0x3318, 1);
    g_assert_true(f->level); /* MRR still owns the OR. */
    write_word(f, 0x321c, 0);
    g_assert_cmpuint(read_word(f, 0x321c), ==, 0);
    g_assert_false(f->level);
    g_assert_cmpuint(read_word(f, 0x331c), ==, 1);
    write_word(f, 0x321c, 1);
    g_assert_cmpuint(read_word(f, 0x321c), ==, 1);
    g_assert_true(f->level);
    write_word(f, 0x331c, 1);
    g_assert_false(f->level);
    write_word(f, 0x321c, 0);
    write_word(f, 0x311c, 0x07010001);
    advance(10000);
    g_assert_false(f->level);
    g_assert_cmpuint(read_word(f, 0x331c), ==, 1);
    write_word(f, 0x321c, 1);
    g_assert_true(f->level);
    ack_isr(f);
    cleanup(f);
}

static void test_boundaries(void)
{
    Fixture *f = fixture(0);
    uint64_t value;
    const uint32_t bad[] = {
        0x04010001, 0x09010001, 0x05000001, 0x05010000, 0x05030001,
    };

    g_assert_cmpint(write_result(f, 0x311c, 0x05010001), ==, MEMTX_ERROR);
    g_assert_cmpint(read_result(f, 0x3a08, &value), ==, MEMTX_ERROR);
    g_assert_cmpuint(read_word(f, 0x321c), ==, 0);
    prepare(f);
    ack_isr(f);
    for (unsigned i = 0; i < ARRAY_SIZE(bad); i++) {
        g_assert_cmpint(write_result(f, 0x311c, bad[i]), ==, MEMTX_ERROR);
    }
    g_assert_cmpint(write_result(f, 0x321c, 2), ==, MEMTX_ERROR);
    g_assert_cmpint(memory_region_dispatch_write(f->regs, 0x311c, 1, MO_16,
                                                MEMTXATTRS_UNSPECIFIED),
                    ==, MEMTX_ERROR);
    write_word(f, 0x321c, 1);
    write_word(f, 0x311c, 0x08010001);
    g_assert_cmpint(write_result(f, 0x311c, 0x05010001), ==, MEMTX_ERROR);
    g_assert_cmpint(write_result(f, 0x331c, 1), ==, MEMTX_ERROR);
    write_word(f, 0x3000, 1); /* DDR reset cancels the pending MRR timer. */
    advance(10000);
    g_assert_false(f->level);
    g_assert_cmpuint(read_word(f, 0x331c), ==, 0);
    g_assert_cmpint(read_result(f, 0x3a08, &value), ==, MEMTX_ERROR);
    g_assert_cmpint(write_result(f, 0x311c, 0x05010001), ==, MEMTX_ERROR);
    device_cold_reset(f->dev);
    g_assert_cmpuint(read_word(f, 0x321c), ==, 0);
    g_assert_cmpuint(read_word(f, 0x3204), ==, 0);
    g_assert_cmpuint(read_word(f, 0x3208), ==, 0);
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
    g_test_add_func("/nikon/sdi/mrr/commands", test_commands);
    g_test_add_func("/nikon/sdi/mrr/irq-or-mask", test_irq_or_mask);
    g_test_add_func("/nikon/sdi/mrr/boundaries", test_boundaries);
    result = g_test_run();
    qemu_cleanup(result);
    unlink(socket);
    rmdir(directory);
    return result;
}
