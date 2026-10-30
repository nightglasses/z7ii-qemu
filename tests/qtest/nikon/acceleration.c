/*
 * Source-shaped acceleration transactions through the native timed controller.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "hw/nikon/devices.h"
#include "libqtest.h"
#include "fixture.h"

#define I2C 0x2c01a000

static const char *bios;

static QTestState *start(void)
{
    QTestState *q = qtest_initf("-machine z7ii-mas-diagnostic -smp 1 "
        "-bios %s -display none -serial none -device "
        TYPE_NIKON_ACCELERATION ",bus=nikon-i2c2,address=29,"
        "sample-present=on,sample-x=-1,sample-z=1024", bios);

    qtest_irq_intercept_out_named(q, "/machine/i2c2", "sysbus-irq");
    qtest_writel(q, I2C + 0x24, 1);
    return q;
}

static void complete(QTestState *q)
{
    qtest_clock_step(q, 10000000);
    g_assert_cmphex(qtest_readl(q, I2C + 0x10) & 5, ==, 1);
    g_assert_true(qtest_get_irq(q, 0));
    qtest_writel(q, I2C + 0x10, 0x2ff);
    g_assert_false(qtest_get_irq(q, 0));
}

static void write_bytes(QTestState *q, const uint8_t *bytes, unsigned length)
{
    qtest_writel(q, I2C, 6);
    for (unsigned i = 0; i < length; i++) {
        qtest_writel(q, I2C + 0xc, bytes[i]);
    }
    qtest_writel(q, I2C + 8, 0x1d);
    complete(q);
}

static void read_bytes(QTestState *q, uint8_t reg,
                       const uint8_t *expected, unsigned length)
{
    write_bytes(q, &reg, 1);
    qtest_writel(q, I2C, 7);
    qtest_writel(q, I2C + 0x14, length);
    qtest_writel(q, I2C + 8, 0x1d);
    complete(q);
    for (unsigned i = 0; i < length; i++) {
        g_assert_cmphex(qtest_readl(q, I2C + 0xc), ==, expected[i]);
    }
}

static void test_controller(void)
{
    QTestState *q = start();
    const uint8_t activate[] = { 0x2a, 1 };
    const uint8_t ready[] = { 8 }, idle[] = { 0 };
    const uint8_t sample[] = { 0xff, 0xf0, 0, 0, 0x40, 0 };

    read_bytes(q, 0, idle, sizeof(idle));
    write_bytes(q, activate, sizeof(activate));
    read_bytes(q, 0, ready, sizeof(ready));
    read_bytes(q, 1, sample, sizeof(sample));
    qtest_system_reset(q);
    qtest_writel(q, I2C + 0x24, 1);
    read_bytes(q, 0, idle, sizeof(idle));
    qtest_quit(q);
}

static void test_held_selector(void)
{
    QTestState *q = start();
    const uint8_t activate[] = { 0x2a, 1 };

    write_bytes(q, activate, sizeof(activate));
    /*
     * The original combined status read waits for TX COMP before RX,
     * while HOLD keeps the endpoint selected across the repeated START.
     */
    qtest_writel(q, I2C, 0xb3e);
    qtest_writel(q, I2C + 0xc, 0);
    qtest_writel(q, I2C + 8, 0x1d);
    complete(q);
    g_assert_cmphex(qtest_readl(q, I2C + 4) & 0x100, ==, 0x100);
    qtest_writel(q, I2C, 0xb3f);
    qtest_writel(q, I2C + 0x14, 1);
    qtest_writel(q, I2C + 8, 0x1d);
    qtest_writel(q, I2C, 0xb2f);
    complete(q);
    g_assert_cmphex(qtest_readl(q, I2C + 0xc), ==, 8);
    qtest_quit(q);
}

static void test_uncovered_read(void)
{
    QTestState *q = start();
    const uint8_t activate[] = { 0x2a, 1 };
    const uint8_t selector = 1;
    QDict *response;

    write_bytes(q, activate, sizeof(activate));
    write_bytes(q, &selector, 1);
    qtest_qmp_assert_success(q, "{'execute':'cont'}");
    qtest_writel(q, I2C, 7);
    qtest_writel(q, I2C + 0x14, 7);
    qtest_writel(q, I2C + 8, 0x1d);
    qtest_clock_step(q, 10000000);
    response = qtest_qmp(q, "{'execute':'query-status'}");
    g_assert_cmpstr(qdict_get_str(qdict_get_qdict(response, "return"),
                                "status"), ==, "debug");
    qobject_unref(response);
    qtest_quit(q);
}

void nikon_acceleration_add_tests(const char *image)
{
    bios = image;
    qtest_add_func("/nikon/acceleration/controller", test_controller);
    qtest_add_func("/nikon/acceleration/held-selector", test_held_selector);
    qtest_add_func("/nikon/acceleration/uncovered-read", test_uncovered_read);
}
