/* Native three-mode fields / real provider events / W0C fixture.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "../libqtest.h"

#define BASE 0x2c015000
#define PATH "/machine/sla/counter"

void nikon_test_counter(QTestState *q);

void nikon_test_counter(QTestState *q)
{
    static const uint32_t modes[] = { 0x9050, 0xa730, 0xa830 };

    qtest_irq_intercept_out_named(q, PATH, "sysbus-irq");
    for (unsigned channel = 0; channel < 3; channel++) {
        uint64_t base = BASE + channel * 0x20;

        for (unsigned mode = 0; mode < ARRAY_SIZE(modes); mode++) {
            qtest_writel(q, base + 4, 0x1357 + channel + mode);
            qtest_writel(q, base + 0xc, mode ? 0x60 : 0x20);
            qtest_writel(q, base + 0x14, modes[mode]);
            g_assert_cmphex(qtest_readl(q, base + 4), ==,
                           0x1357 + channel + mode);
            g_assert_cmphex(qtest_readl(q, base + 0x14), ==, modes[mode]);
            qtest_clock_step(q, 1000000);
            g_assert_cmphex(qtest_readl(q, base + 0xc) & 0x1c, ==, 0);
            g_assert_cmphex(qtest_readl(q, base + 0x14) & 0x4000, ==, 0);
            g_assert_false(qtest_get_irq(q, channel));
        }
        qtest_set_irq_in(q, PATH, "state", channel * 2, 1);
        qtest_set_irq_in(q, PATH, "state", channel * 2 + 1, 1);
        g_assert_cmphex(qtest_readl(q, base + 0xc), ==, 0x63);
        g_assert_false(qtest_get_irq(q, channel));
        for (unsigned event = 0; event < 4; event++) {
            qtest_set_irq_in(q, PATH, "event", channel * 4 + event, 1);
        }
        g_assert_cmphex(qtest_readl(q, base + 0xc), ==, 0x7f);
        g_assert_cmphex(qtest_readl(q, base + 0x14), ==, modes[2] | 0x4000);
        g_assert_true(qtest_get_irq(q, channel));
        /* Complete source ACK6076C: W0C flags2/3/4 and flag14, retain fields
         * and the provider's actual state, clear7/set15 on the mode word. */
        qtest_writel(q, base + 0xc, 0x63);
        g_assert_cmphex(qtest_readl(q, base + 0xc), ==, 0x63);
        g_assert_true(qtest_get_irq(q, channel)); /* flag14 still pending */
        qtest_writel(q, base + 0x14, modes[2]);
        g_assert_false(qtest_get_irq(q, channel));
        for (unsigned event = 0; event < 4; event++) {
            qtest_set_irq_in(q, PATH, "event", channel * 4 + event, 1);
        }
        g_assert_false(qtest_get_irq(q, channel));
        qtest_writel(q, base + 4, 0x10000);
        qtest_writew(q, base + 4, 1);
        qtest_writel(q, base + 8, 1);
        g_assert_cmphex(qtest_readl(q, base + 4), ==, 0x1359 + channel);
    }

    qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
    qtest_qmp_eventwait(q, "RESET");
    for (unsigned channel = 0; channel < 3; channel++) {
        uint64_t base = BASE + channel * 0x20;

        g_assert_cmphex(qtest_readl(q, base + 4), ==, 0);
        g_assert_cmphex(qtest_readl(q, base + 0xc), ==, 3);
        g_assert_cmphex(qtest_readl(q, base + 0x14), ==, 0);
        g_assert_false(qtest_get_irq(q, channel));
        qtest_set_irq_in(q, PATH, "event", channel * 4, 1);
        g_assert_false(qtest_get_irq(q, channel));
        for (unsigned event = 0; event < 4; event++) {
            qtest_set_irq_in(q, PATH, "event", channel * 4 + event, 0);
        }
        qtest_set_irq_in(q, PATH, "event", channel * 4, 1);
        g_assert_cmphex(qtest_readl(q, base + 0xc), ==, 7);
        g_assert_true(qtest_get_irq(q, channel));
        qtest_writel(q, base + 0xc, 0);
        g_assert_cmphex(qtest_readl(q, base + 0xc), ==, 3);
        g_assert_false(qtest_get_irq(q, channel));
        qtest_set_irq_in(q, PATH, "event", channel * 4, 0);
        qtest_set_irq_in(q, PATH, "state", channel * 2, 0);
        qtest_set_irq_in(q, PATH, "state", channel * 2 + 1, 0);
    }
}
