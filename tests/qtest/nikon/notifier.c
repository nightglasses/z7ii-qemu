/* Native source-field / disconnected-provider / acknowledgement fixture.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "../libqtest.h"

#define BASE 0x2c0b0000
#define PATH "/machine/sla/notifier"

void nikon_test_notifier(QTestState *q);

void nikon_test_notifier(QTestState *q)
{
    qtest_irq_intercept_out_named(q, PATH, "sysbus-irq");
    g_assert_cmphex(qtest_readl(q, BASE + 4), ==, 7);
    g_assert_cmphex(qtest_readl(q, BASE + 0x20), ==, 0);

    /* Complete configuration setter FEF2, including both byte parameters
     * and full opaque words. These are field values, not firmware bytes. */
    qtest_writel(q, BASE, 0xc);
    qtest_writel(q, BASE + 8, 0xa5);
    qtest_writel(q, BASE + 0xc, 0x5a);
    qtest_writel(q, BASE + 0x10, 0x12345678);
    qtest_writel(q, BASE + 0x14, 0x87654321);
    qtest_writel(q, BASE + 0x18, 0xabcdef01);
    qtest_writel(q, BASE + 4, 0x5a17);
    g_assert_cmphex(qtest_readl(q, BASE), ==, 0xc);
    g_assert_cmphex(qtest_readl(q, BASE + 8), ==, 0xa5);
    g_assert_cmphex(qtest_readl(q, BASE + 0xc), ==, 0x5a);
    g_assert_cmphex(qtest_readl(q, BASE + 0x10), ==, 0x12345678);
    g_assert_cmphex(qtest_readl(q, BASE + 0x14), ==, 0x87654321);
    g_assert_cmphex(qtest_readl(q, BASE + 0x18), ==, 0xabcdef01);
    /* Every opaque control combination remains disconnected, including the
     * FF48 bit1 set/clear path. Neither read nor time produces completion. */
    for (unsigned value = 0; value < 16; value++) {
        qtest_writel(q, BASE, value);
        qtest_clock_step(q, 1000000);
        g_assert_cmphex(qtest_readl(q, BASE + 4), ==, 0x5a17);
        g_assert_false(qtest_get_irq(q, 0));
    }

    for (unsigned event = 0; event < 3; event++) {
        uint32_t status;

        qtest_set_irq_in(q, PATH, "event", event, 1);
        status = qtest_readl(q, BASE + 4);
        g_assert_cmphex(status, ==, 0x5a17 & ~(1U << event));
        g_assert_true(qtest_get_irq(q, 0));
        /* The original ISR replaces bits0..2 with their inverse, while
         * retaining all other fields; only the external event is cleared. */
        qtest_writel(q, BASE + 4, (status & ~7U) | (~status & 7));
        g_assert_cmphex(qtest_readl(q, BASE + 4), ==, 0x5a17);
        g_assert_false(qtest_get_irq(q, 0));
        qtest_set_irq_in(q, PATH, "event", event, 1);
        g_assert_false(qtest_get_irq(q, 0));
        qtest_set_irq_in(q, PATH, "event", event, 0);
    }

    qtest_writel(q, BASE + 8, 0x100); /* rejected field */
    qtest_writew(q, BASE + 8, 0x11); /* rejected width */
    qtest_writel(q, BASE + 0x1c, 0x55); /* actual hole */
    g_assert_cmphex(qtest_readl(q, BASE + 8), ==, 0xa5);
    qtest_set_irq_in(q, PATH, "busy", 0, 1);
    g_assert_cmphex(qtest_readl(q, BASE + 0x20), ==, 1);
    qtest_set_irq_in(q, PATH, "event", 0, 1);
    qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
    qtest_qmp_eventwait(q, "RESET");
    g_assert_cmphex(qtest_readl(q, BASE + 4), ==, 7);
    g_assert_cmphex(qtest_readl(q, BASE + 0x20), ==, 1);
    g_assert_false(qtest_get_irq(q, 0));
    qtest_set_irq_in(q, PATH, "event", 0, 1);
    g_assert_false(qtest_get_irq(q, 0));
    qtest_set_irq_in(q, PATH, "event", 0, 0);
    qtest_set_irq_in(q, PATH, "event", 0, 1);
    g_assert_true(qtest_get_irq(q, 0));
    qtest_writel(q, BASE + 4, 7);
    qtest_set_irq_in(q, PATH, "event", 0, 0);
    qtest_set_irq_in(q, PATH, "busy", 0, 0);
}
