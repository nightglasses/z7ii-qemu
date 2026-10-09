/* Native six-bank source ACK / external-provider / reset fixture.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "../libqtest.h"

#define BASE 0x201a0300
#define PATH "/machine/sla/flags"

static const unsigned offsets[] = { 0, 4, 8, 0xc, 0x10, 0x1c };

void nikon_test_flags(QTestState *q);

void nikon_test_flags(QTestState *q)
{
    uint32_t snapshot[G_N_ELEMENTS(offsets)];
    unsigned bank, bit;

    qtest_irq_intercept_out_named(q, PATH, "sysbus-irq");
    for (bank = 0; bank < G_N_ELEMENTS(offsets); bank++) {
        g_assert_cmphex(qtest_readl(q, BASE + offsets[bank]), ==, 0);
        qtest_writel(q, BASE + offsets[bank], UINT32_MAX);
    }
    qtest_clock_step(q, 1000000);
    g_assert_false(qtest_get_irq(q, 0));

    /* All six full words reach the original callback, not only the two bit0
     * flags separately consumed by the ISR. Exercise every bank and bit. */
    for (bank = 0; bank < G_N_ELEMENTS(offsets); bank++) {
        for (bit = 0; bit < 32; bit++) {
            qtest_set_irq_in(q, PATH, "event", bank * 32 + bit, 1);
        }
        g_assert_cmphex(qtest_readl(q, BASE + offsets[bank]), ==, UINT32_MAX);
    }
    g_assert_true(qtest_get_irq(q, 0));
    /* Snapshot all words, then perform the exact six writebacks of 59904. */
    for (bank = 0; bank < G_N_ELEMENTS(offsets); bank++) {
        snapshot[bank] = qtest_readl(q, BASE + offsets[bank]);
    }
    for (bank = 0; bank < G_N_ELEMENTS(offsets); bank++) {
        qtest_writel(q, BASE + offsets[bank], snapshot[bank]);
        g_assert_cmphex(qtest_readl(q, BASE + offsets[bank]), ==, 0);
        g_assert_cmpint(qtest_get_irq(q, 0), ==,
                        bank + 1 < G_N_ELEMENTS(offsets));
        qtest_set_irq_in(q, PATH, "event", bank * 32, 1);
        g_assert_cmphex(qtest_readl(q, BASE + offsets[bank]), ==, 0);
        for (bit = 0; bit < 32; bit++) {
            qtest_set_irq_in(q, PATH, "event", bank * 32 + bit, 0);
        }
    }

    /* A new bit after a snapshot survives that snapshot's ACK. Zero and
     * another bank's ACK cannot acknowledge it or manufacture a new event. */
    qtest_set_irq_in(q, PATH, "event", 0, 1);
    snapshot[0] = qtest_readl(q, BASE);
    qtest_set_irq_in(q, PATH, "event", 31, 1);
    qtest_writel(q, BASE, snapshot[0]);
    qtest_writel(q, BASE, 0);
    qtest_writel(q, BASE + 0x1c, UINT32_MAX);
    g_assert_cmphex(qtest_readl(q, BASE), ==, 0x80000000);
    g_assert_true(qtest_get_irq(q, 0));
    qtest_writew(q, BASE, UINT16_MAX); /* unsupported width */
    qtest_writeb(q, BASE + 3, UINT8_MAX);
    qtest_writel(q, BASE + 1, UINT32_MAX); /* unaligned */
    qtest_writel(q, BASE + 0x14, UINT32_MAX); /* sparse holes */
    qtest_writel(q, BASE + 0x18, UINT32_MAX);
    g_assert_cmphex(qtest_readl(q, BASE), ==, 0x80000000);
    g_assert_true(qtest_get_irq(q, 0));

    qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
    qtest_qmp_eventwait(q, "RESET");
    for (bank = 0; bank < G_N_ELEMENTS(offsets); bank++) {
        g_assert_cmphex(qtest_readl(q, BASE + offsets[bank]), ==, 0);
    }
    g_assert_false(qtest_get_irq(q, 0));
    qtest_set_irq_in(q, PATH, "event", 31, 1); /* input remains held */
    g_assert_false(qtest_get_irq(q, 0));
    qtest_set_irq_in(q, PATH, "event", 31, 0);
    qtest_set_irq_in(q, PATH, "event", 31, 1);
    g_assert_cmphex(qtest_readl(q, BASE), ==, 0x80000000);
    g_assert_true(qtest_get_irq(q, 0));
    qtest_writel(q, BASE, 0x80000000);
    qtest_set_irq_in(q, PATH, "event", 0, 0);
    qtest_set_irq_in(q, PATH, "event", 31, 0);
    g_assert_false(qtest_get_irq(q, 0));
}
