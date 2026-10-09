/* Native explicit event publication and original ISR acknowledgement.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "libqtest.h"

void nikon_events_test(QTestState *q, const char *path, uint64_t base);

void nikon_events_test(QTestState *q, const char *path, uint64_t base)
{
    unsigned i;

    qtest_irq_intercept_out_named(q, path, "sysbus-irq");
    g_assert_cmphex(qtest_readl(q, base + 8), ==, 0);
    g_assert_cmphex(qtest_readl(q, base + 0xc), ==, 0);
    qtest_writel(q, base + 8, 0x1c00);
    qtest_clock_step(q, 1000000);
    g_assert_false(qtest_get_irq(q, 0));
    g_assert_cmphex(qtest_readl(q, base + 0xc), ==, 0);
    /* Status writes cannot create events, even with every cause enabled. */
    qtest_writel(q, base + 0xc, 0xe);
    g_assert_false(qtest_get_irq(q, 0));

    for (i = 0; i < 3; i++) {
        uint32_t bit = 1U << (i + 1);

        qtest_writel(q, base + 8, 0);
        qtest_set_irq_in(q, path, "publish", 0, bit);
        g_assert_cmphex(qtest_readl(q, base + 0xc), ==, bit);
        g_assert_false(qtest_get_irq(q, 0));
        qtest_writel(q, base + 8, 1U << (i + 10));
        g_assert_true(qtest_get_irq(q, 0));
        /* Preserve does not acknowledge. The original ISR flips all 3 bits. */
        qtest_writel(q, base + 0xc, 0xe);
        g_assert_true(qtest_get_irq(q, 0));
        qtest_writel(q, base + 0xc, qtest_readl(q, base + 0xc) ^ 0xe);
        g_assert_false(qtest_get_irq(q, 0));
        g_assert_cmphex(qtest_readl(q, base + 0xc), ==, 0);
        qtest_set_irq_in(q, path, "publish", 0, 0);
        g_assert_false(qtest_get_irq(q, 0));
    }

    /* Several genuine events share one IRQ; a partial W0C preserves others. */
    qtest_writel(q, base + 8, 0x1c00);
    qtest_set_irq_in(q, path, "publish", 0, 0xe);
    g_assert_cmphex(qtest_readl(q, base + 0xc), ==, 0xe);
    qtest_set_irq_in(q, path, "publish", 0, 0);
    g_assert_cmphex(qtest_readl(q, base + 0xc), ==, 0xe);
    qtest_writel(q, base + 0xc, 0xc);
    g_assert_cmphex(qtest_readl(q, base + 0xc), ==, 0xc);
    g_assert_true(qtest_get_irq(q, 0));
    qtest_system_reset(q);
    g_assert_cmphex(qtest_readl(q, base + 8), ==, 0);
    g_assert_cmphex(qtest_readl(q, base + 0xc), ==, 0);
    qtest_writel(q, base + 8, 0x1c00);
    qtest_clock_step(q, 1000000);
    g_assert_false(qtest_get_irq(q, 0));
}
