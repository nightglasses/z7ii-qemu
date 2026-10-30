/* Private SLA five-route signal controller and separate configuration inputs.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "libqtest.h"

void nikon_signal_test(QTestState *q, const char *path, uint64_t signal,
                       uint64_t configuration, uint64_t lanes);

void nikon_signal_test(QTestState *q, const char *path, uint64_t signal,
                       uint64_t configuration, uint64_t lanes)
{
    static const unsigned bits[] = { 0, 1, 4, 2, 3 };
    unsigned i, j;

    qtest_irq_intercept_out_named(q, path, "sysbus-irq");
    /* Presence is observable before capture release/gate configuration. */
    for (i = 0; i < 5; i++) {
        qtest_set_irq_in(q, path, "signal", i, 1);
    }
    g_assert_cmphex(qtest_readl(q, signal + 0x10), ==, 0x1f1f);
    g_assert_cmphex(qtest_readl(q, signal + 8), ==, 0);
    qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
    /* The board's absent storage provider republishes route 3 low at reset. */
    g_assert_cmphex(qtest_readl(q, signal + 0x10), ==, 0x1b1b);
    g_assert_cmphex(qtest_readl(q, signal + 8), ==, 0);
    for (i = 0; i < 5; i++) {
        qtest_set_irq_in(q, path, "signal", i, 0);
    }
    g_assert_cmphex(qtest_readl(q, signal + 0x10), ==, 0);
    /* Exact original initializer: neither input write is an event producer. */
    qtest_writel(q, configuration, 0xf);
    qtest_writel(q, signal + 4, 0x3c);
    g_assert_cmphex(qtest_readl(q, signal + 8), ==, 0);
    for (i = 0; i < 5; i++) {
        g_assert_false(qtest_get_irq(q, i));
    }

    /* Whole original89B06 active/disabled paths, both separate apertures. */
    for (i = 0; i < 2; i++) {
        uint64_t lane = lanes + i * 0x100;

        qtest_writel(q, lane + 8, 1);
        qtest_writel(q, lane + 4, 0);
        qtest_writel(q, lane + 0xc, 0x89abcdef ^ i);
        qtest_writel(q, lane + 0x10, 0x76543210 ^ i);
        qtest_writel(q, lane, 1);
        qtest_clock_step(q, 1000000);
        qtest_writel(q, lane, 0);
        qtest_writel(q, lane + 4, 1);
        g_assert_cmphex(qtest_readl(q, signal + 8), ==, 0);
        for (j = 0; j < 5; j++) {
            g_assert_false(qtest_get_irq(q, j));
        }
    }

    /* All physical pending pairs are independent of the software callback order. */
    qtest_writel(q, signal, 0x1f);
    qtest_writel(q, signal + 0xc, 0x3ff);
    for (i = 0; i < 5; i++) {
        qtest_set_irq_in(q, path, "signal", i, 1);
    }
    g_assert_cmphex(qtest_readl(q, signal + 8), ==, 0x155);
    for (i = 0; i < 5; i++) {
        g_assert_true(qtest_get_irq(q, i));
    }
    for (i = 0; i < 5; i++) {
        qtest_writel(q, signal + 8, 1U << (2 * bits[i]));
        g_assert_false(qtest_get_irq(q, i));
        for (j = i + 1; j < 5; j++) {
            g_assert_true(qtest_get_irq(q, j));
        }
    }
    for (i = 0; i < 5; i++) {
        qtest_set_irq_in(q, path, "signal", i, 0);
    }
    g_assert_cmphex(qtest_readl(q, signal + 8), ==, 0x2aa);
    /* Original ISR reads one word and writes the same pending word to ACK. */
    qtest_writel(q, signal + 8, qtest_readl(q, signal + 8));
    g_assert_cmphex(qtest_readl(q, signal + 8), ==, 0);
    for (i = 0; i < 5; i++) {
        g_assert_false(qtest_get_irq(q, i));
    }

    /* Gate/reset control edge capture, never the external status level. */
    for (i = 0; i < 5; i++) {
        qtest_set_irq_in(q, path, "signal", i, 1);
    }
    qtest_writel(q, signal + 4, 0);
    g_assert_cmphex(qtest_readl(q, signal + 0x10), ==, 0x1f1f);
    g_assert_cmphex(qtest_readl(q, signal + 8), ==, 0);
    qtest_writel(q, signal + 4, 0x3c);
    qtest_writel(q, signal, 0);
    g_assert_cmphex(qtest_readl(q, signal + 0x10), ==, 0x1f1f);
    g_assert_cmphex(qtest_readl(q, signal + 8), ==, 0);
    qtest_writel(q, signal + 4, 0);
    qtest_writel(q, signal + 0xc, 0x3ff);
    for (i = 0; i < 5; i++) {
        unsigned bit = bits[i];

        qtest_set_irq_in(q, path, "signal", i, 0);
        qtest_writel(q, signal + 0x14, (1U << bit) | (3U << (8 + 4 * bit)));
        qtest_set_irq_in(q, path, "signal", i, 1);
        g_assert_cmphex(qtest_readl(q, signal + 0x10) & (1U << (8 + bit)),
                        ==, 0);
        qtest_clock_step(q, 7999);
        g_assert_cmphex(qtest_readl(q, signal + 0x10) & (1U << (8 + bit)),
                        ==, 0);
        qtest_clock_step(q, 1);
        g_assert_cmphex(qtest_readl(q, signal + 0x10) & (1U << (8 + bit)), ==,
                        1U << (8 + bit));
        g_assert_cmphex(qtest_readl(q, signal + 8), ==, 0);
        g_assert_false(qtest_get_irq(q, i));
    }
    /* Reset cancels a pending filter deadline and rebases without an edge. */
    qtest_set_irq_in(q, path, "signal", 4, 0);
    qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
    qtest_clock_step(q, 10000);
    g_assert_cmphex(qtest_readl(q, signal + 0x10), ==, 0x1313);
    g_assert_cmphex(qtest_readl(q, signal + 8), ==, 0);
}
