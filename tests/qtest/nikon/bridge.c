/* Explicit disconnected bridge inputs, not a synthetic camera peer.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qobject/qdict.h"
#include "qobject/qlist.h"
#include "hw/core/sysbus.h"
#include "hw/nikon/devices.h"
#include "../libqtest.h"
#include "fixture.h"

#define BRIDGE 0x20130000
#define RAW (BRIDGE + 0xd020)
#define MASK (BRIDGE + 0xd024)
#define ACK (BRIDGE + 0xd028)
#define RESET (BRIDGE + 0xc5e0)
#define READY (BRIDGE + 0xc908)

void nikon_test_bridge(QTestState *q);

static void event(QTestState *q, const char *path, unsigned input, int level)
{
    qtest_set_irq_in(q, path, "peer-event", input, level);
}

static void test_edges(QTestState *q, const char *path)
{
    static const unsigned bits[] = { 0, 2, 5 };
    unsigned i;

    g_assert_cmphex(qtest_readl(q, RAW), ==, 0);
    g_assert_cmphex(qtest_readl(q, READY), ==, 0);
    qtest_clock_step(q, 1000000);
    g_assert_cmphex(qtest_readl(q, RAW), ==, 0);
    for (i = 0; i < ARRAY_SIZE(bits); i++) {
        unsigned bit = 1U << bits[i];

        qtest_writel(q, MASK, 0);
        event(q, path, i, 1);
        g_assert_cmphex(qtest_readl(q, RAW), ==, bit);
        g_assert_false(qtest_get_irq(q, 0));
        qtest_writel(q, MASK, bit);
        g_assert_true(qtest_get_irq(q, 0));
        /* A different ACK neither consumes nor hides this pending edge. */
        qtest_writel(q, ACK, 0x40);
        g_assert_cmphex(qtest_readl(q, RAW), ==, bit);
        qtest_writel(q, ACK, bit);
        g_assert_false(qtest_get_irq(q, 0));
        event(q, path, i, 1);
        g_assert_cmphex(qtest_readl(q, RAW), ==, 0);
        event(q, path, i, 0);
        event(q, path, i, 1);
        g_assert_cmphex(qtest_readl(q, RAW), ==, bit);
        qtest_writel(q, ACK, bit);
        event(q, path, i, 0);
    }
}

static void test_ready(QTestState *q, const char *path)
{
    qtest_writel(q, MASK, 0xffff);
    qtest_set_irq_in(q, path, "peer-ready", 0, 1);
    g_assert_cmphex(qtest_readl(q, READY), ==, 4);
    g_assert_cmphex(qtest_readl(q, BRIDGE + 0x4908), ==, 0);
    g_assert_cmphex(qtest_readl(q, RAW), ==, 0);
    g_assert_false(qtest_get_irq(q, 0));
    event(q, path, 2, 1);
    qtest_writel(q, ACK, 0x20);
    g_assert_cmphex(qtest_readl(q, READY), ==, 4);
    /* The source-known bit2 acknowledgement is not a readiness producer. */
    qtest_writel(q, BRIDGE + 0x829c, 1);
    qtest_set_irq_in(q, path, "peer-ready", 0, 0);
    g_assert_cmphex(qtest_readl(q, READY), ==, 0);
    g_assert_cmphex(qtest_readl(q, RAW), ==, 0);
    event(q, path, 2, 0);
}

static void test_reset(QTestState *q, const char *path)
{
    qtest_writel(q, MASK, 0xffff);
    event(q, path, 0, 1);
    g_assert_true(qtest_get_irq(q, 0));
    qtest_writel(q, RESET, 0xf000f);
    g_assert_cmphex(qtest_readl(q, RAW), ==, 0);
    g_assert_false(qtest_get_irq(q, 0));
    event(q, path, 1, 1);
    qtest_writel(q, RESET, 0);
    event(q, path, 0, 1);
    event(q, path, 1, 1);
    g_assert_cmphex(qtest_readl(q, RAW), ==, 0);
    event(q, path, 1, 0);
    event(q, path, 1, 1);
    g_assert_cmphex(qtest_readl(q, RAW), ==, 4);
    qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
    g_assert_cmphex(qtest_readl(q, RAW), ==, 0);
    qtest_writel(q, MASK, 0xffff);
    event(q, path, 1, 1);
    g_assert_cmphex(qtest_readl(q, RAW), ==, 0);
    event(q, path, 1, 0);
    event(q, path, 1, 1);
    g_assert_cmphex(qtest_readl(q, RAW), ==, 4);
    event(q, path, 0, 0);
    event(q, path, 1, 0);
}

void nikon_test_bridge(QTestState *q)
{
    g_autofree char *path = nikon_device_path(q, TYPE_NIKON_BRIDGE);

    qtest_irq_intercept_out_named(q, path, SYSBUS_DEVICE_GPIO_IRQ);
    qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
    test_edges(q, path);
    qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
    test_ready(q, path);
    qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
    test_reset(q, path);
    qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
}
