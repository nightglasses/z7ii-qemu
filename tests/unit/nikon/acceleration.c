/*
 * Finite acceleration input through the upstream I2C bus and reset APIs.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/sysbus.h"
#include "hw/nikon/devices.h"
#include "system/system.h"

int (*qemu_main)(void);

#define ACCELERATION "nikon-acceleration-functional"
#define ADDRESS 0x1d

typedef struct Fixture {
    DeviceState *controller;
    I2CSlave *peer;
    I2CBus *bus;
} Fixture;

static Fixture fixture(bool present, int x, int y, int z)
{
    Fixture f = { .controller = qdev_new(TYPE_NIKON_I2C) };

    object_property_add_child(object_get_objects_root(), "acceleration[*]",
                              OBJECT(f.controller));
    g_assert_true(sysbus_realize(SYS_BUS_DEVICE(f.controller), &error_abort));
    f.bus = nikon_i2c_bus(f.controller);
    f.peer = i2c_slave_new(ACCELERATION, ADDRESS);
    qdev_prop_set_bit(DEVICE(f.peer), "sample-present", present);
    qdev_prop_set_int32(DEVICE(f.peer), "sample-x", x);
    qdev_prop_set_int32(DEVICE(f.peer), "sample-y", y);
    qdev_prop_set_int32(DEVICE(f.peer), "sample-z", z);
    g_assert_true(i2c_slave_realize_and_unref(f.peer, f.bus, &error_abort));
    device_cold_reset(f.controller);
    return f;
}

static void cleanup(Fixture *f)
{
    i2c_end_transfer(f->bus);
    qdev_unrealize(f->controller);
    object_unparent(OBJECT(f->controller));
    object_unref(OBJECT(f->controller));
}

static void write_bytes(Fixture *f, const uint8_t *bytes, unsigned n)
{
    g_assert_cmpint(i2c_start_send(f->bus, ADDRESS), ==, 0);
    for (unsigned i = 0; i < n; i++) {
        g_assert_cmpint(i2c_send(f->bus, bytes[i]), ==, 0);
    }
    i2c_end_transfer(f->bus);
}

static void control(Fixture *f, uint8_t value)
{
    const uint8_t bytes[] = { 0x2a, value };

    write_bytes(f, bytes, sizeof(bytes));
}

static void select_reg(Fixture *f, uint8_t reg)
{
    write_bytes(f, &reg, 1);
}

static uint8_t status(Fixture *f)
{
    uint8_t result;

    select_reg(f, 0);
    g_assert_cmpint(i2c_start_recv(f->bus, ADDRESS), ==, 0);
    result = i2c_recv(f->bus);
    i2c_nack(f->bus);
    i2c_end_transfer(f->bus);
    return result;
}

static void test_sequence(void)
{
    Fixture f = fixture(true, -2048, 2047, -1);
    const uint8_t expected[] = { 0x80, 0, 0x7f, 0xf0, 0xff, 0xf0 };
    const uint8_t bursts[][16] = {
        { 0x0e, 0, 0 }, { 0x11, 0 }, { 0x15, 0 },
        { 0x17, 0, 0 }, { 0x1d, 0, 0, 0, 0 },
        { 0x23, 0, 0, 0, 0, 0, 0, 0, 0x2c, 2, 0, 0, 0, 0, 0, 0 },
    };
    const unsigned lengths[] = { 3, 2, 2, 3, 5, 16 };

    g_assert_cmphex(status(&f), ==, 0);
    control(&f, 1);
    g_assert_cmphex(status(&f), ==, 8);
    /* Repeated START after the selector is the original combined read. */
    g_assert_cmpint(i2c_start_send(f.bus, ADDRESS), ==, 0);
    g_assert_cmpint(i2c_send(f.bus, 1), ==, 0);
    g_assert_cmpint(i2c_start_recv(f.bus, ADDRESS), ==, 0);
    for (unsigned i = 0; i < sizeof(expected); i++) {
        g_assert_cmphex(i2c_recv(f.bus), ==, expected[i]);
    }
    i2c_nack(f.bus);
    i2c_end_transfer(f.bus);
    control(&f, 0);
    g_assert_cmphex(status(&f), ==, 0);
    for (unsigned i = 0; i < ARRAY_SIZE(bursts); i++) {
        write_bytes(&f, bursts[i], lengths[i]);
    }
    control(&f, 0x2d);
    g_assert_cmphex(status(&f), ==, 8);
    device_cold_reset(f.controller);
    g_assert_cmphex(status(&f), ==, 0);
    control(&f, 1);
    g_assert_cmphex(status(&f), ==, 8);
    cleanup(&f);
}

static void test_missing(void)
{
    Fixture f = fixture(false, 0, 0, 0);

    control(&f, 1);
    g_assert_cmphex(status(&f), ==, 0);
    select_reg(&f, 1);
    g_assert_cmpint(i2c_start_recv(f.bus, ADDRESS), !=, 0);
    cleanup(&f);
}

static void test_rejection(void)
{
    Fixture f = fixture(true, 0, 0, 1024);

    g_assert_cmpint(i2c_start_send(f.bus, 0x68), !=, 0);
    i2c_end_transfer(f.bus);
    g_assert_cmpint(i2c_start_recv(f.bus, ADDRESS), !=, 0);
    i2c_end_transfer(f.bus);
    g_assert_cmpint(i2c_start_send(f.bus, ADDRESS), ==, 0);
    g_assert_cmpint(i2c_send(f.bus, 0xff), !=, 0);
    i2c_end_transfer(f.bus);
    g_assert_cmpint(i2c_start_send(f.bus, ADDRESS), ==, 0);
    g_assert_cmpint(i2c_send(f.bus, 0x2a), ==, 0);
    g_assert_cmpint(i2c_send(f.bus, 0xff), !=, 0);
    i2c_end_transfer(f.bus);
    g_assert_cmphex(status(&f), ==, 0);
    select_reg(&f, 1);
    g_assert_cmpint(i2c_start_recv(f.bus, ADDRESS), !=, 0);
    i2c_end_transfer(f.bus);
    cleanup(&f);
}

static void test_partial(void)
{
    Fixture f = fixture(true, 0, 0, 1024);

    /* An extra byte invalidates the whole staged control write. */
    g_assert_cmpint(i2c_start_send(f.bus, ADDRESS), ==, 0);
    g_assert_cmpint(i2c_send(f.bus, 0x2a), ==, 0);
    g_assert_cmpint(i2c_send(f.bus, 1), ==, 0);
    g_assert_cmpint(i2c_send(f.bus, 0), !=, 0);
    i2c_end_transfer(f.bus);
    g_assert_cmphex(status(&f), ==, 0);
    /* Incomplete writes and repeated START never activate the input. */
    select_reg(&f, 0x2a);
    g_assert_cmphex(status(&f), ==, 0);
    g_assert_cmpint(i2c_start_send(f.bus, ADDRESS), ==, 0);
    g_assert_cmpint(i2c_send(f.bus, 0x2a), ==, 0);
    g_assert_cmpint(i2c_send(f.bus, 1), ==, 0);
    g_assert_cmpint(i2c_start_send(f.bus, ADDRESS), ==, 0);
    i2c_end_transfer(f.bus);
    g_assert_cmphex(status(&f), ==, 0);
    /* Reset cancels an accepted-but-uncommitted write. */
    g_assert_cmpint(i2c_start_send(f.bus, ADDRESS), ==, 0);
    g_assert_cmpint(i2c_send(f.bus, 0x2a), ==, 0);
    g_assert_cmpint(i2c_send(f.bus, 1), ==, 0);
    device_cold_reset(f.controller);
    i2c_end_transfer(f.bus);
    g_assert_cmphex(status(&f), ==, 0);
    control(&f, 1);
    select_reg(&f, 1);
    g_assert_cmpint(i2c_start_recv(f.bus, ADDRESS), ==, 0);
    g_assert_cmphex(i2c_recv(f.bus), ==, 0);
    i2c_nack(f.bus);
    i2c_end_transfer(f.bus);
    /* A short sample read cannot leak its offset into a new selection. */
    select_reg(&f, 1);
    g_assert_cmpint(i2c_start_recv(f.bus, ADDRESS), ==, 0);
    g_assert_cmphex(i2c_recv(f.bus), ==, 0);
    i2c_end_transfer(f.bus);
    cleanup(&f);
}

static void test_invalid_sample(void)
{
    Fixture f = fixture(false, 0, 0, 0);
    const char *properties[] = { "sample-x", "sample-y", "sample-z" };
    const int32_t values[] = { -2049, 2048 };

    for (unsigned axis = 0; axis < ARRAY_SIZE(properties); axis++) {
        for (unsigned i = 0; i < ARRAY_SIZE(values); i++) {
            I2CSlave *peer = i2c_slave_new(ACCELERATION, 0x1e);
            Error *error = NULL;

            qdev_prop_set_int32(DEVICE(peer), properties[axis], values[i]);
            g_assert_false(i2c_slave_realize_and_unref(peer, f.bus, &error));
            g_assert_nonnull(error);
            g_assert_nonnull(strstr(error_get_pretty(error), "signed 12 bits"));
            error_free(error);
        }
    }
    cleanup(&f);
}

int main(int argc, char **argv)
{
    g_autofree char *directory = g_dir_make_tmp("nikon-acceleration-XXXXXX",
                                              NULL);
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
    g_test_add_func("/nikon/acceleration/sequence", test_sequence);
    g_test_add_func("/nikon/acceleration/missing", test_missing);
    g_test_add_func("/nikon/acceleration/rejection", test_rejection);
    g_test_add_func("/nikon/acceleration/partial", test_partial);
    g_test_add_func("/nikon/acceleration/invalid-sample", test_invalid_sample);
    result = g_test_run();
    qemu_cleanup(result);
    unlink(socket);
    rmdir(directory);
    return result;
}
