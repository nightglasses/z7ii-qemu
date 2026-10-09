/* Complete encoded completion domain cycle; synthetic qtest input only.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "../libqtest.h"

#define COMPLETION 0x04287000
#define COMPLETION_IRQ 250

void nikon_test_completion(QTestState *q);

static void completion_submit(QTestState *q, unsigned domain, bool enable)
{
    int64_t deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;

    qtest_writel(q, COMPLETION, (domain << 12) | 0x10 |
                 (enable ? 0x100 : 0));
    qtest_writel(q, COMPLETION + 0x10, 0xa1);
    /* BH execution is asynchronous: do not require an observable busy read. */
    while (qtest_readl(q, COMPLETION + 4) != 1) {
        g_assert_cmpint(g_get_monotonic_time(), <, deadline);
        g_usleep(1000);
    }
}

/* The suite owner intercepts the real GIC SPI inputs on a fresh machine.
 * qtest transactions are nonsecure and cannot initialize the secure CPU
 * interface used by the original guest, so they must not assert its IAR.
 * No image, firmware, build or test-registration route is introduced here. */
void nikon_test_completion(QTestState *q)
{
    uint32_t applied = 0;
    unsigned domain;

    g_assert_cmphex(qtest_readl(q, COMPLETION + 0xc), ==, 0);
    for (domain = 0; domain < 8; domain++) {
        completion_submit(q, domain, true);
        applied |= 1U << domain;
        g_assert_cmphex(qtest_readl(q, COMPLETION + 0xc), ==, applied);
        /* A completion with notification disabled does not raise an IRQ. */
        g_assert_false(qtest_get_irq(q, COMPLETION_IRQ - 32));
        qtest_writel(q, COMPLETION + 4, 1);
        g_assert_cmphex(qtest_readl(q, COMPLETION + 4), ==, 0);
    }

    qtest_writel(q, COMPLETION + 8, 1);
    for (domain = 8; domain-- > 0;) {
        completion_submit(q, domain, false);
        applied &= ~(1U << domain);
        g_assert_cmphex(qtest_readl(q, COMPLETION + 0xc), ==, applied);
        g_assert_true(qtest_get_irq(q, COMPLETION_IRQ - 32));
        qtest_writel(q, COMPLETION + 4, 1);
        g_assert_cmphex(qtest_readl(q, COMPLETION + 4), ==, 0);
        g_assert_false(qtest_get_irq(q, COMPLETION_IRQ - 32));
    }

    completion_submit(q, 2, true);
    g_assert_cmphex(qtest_readl(q, COMPLETION + 0xc), ==, 4);
    qtest_qmp_assert_success(q, "{'execute': 'system_reset'}");
    qtest_qmp_eventwait(q, "RESET");
    g_assert_cmphex(qtest_readl(q, COMPLETION), ==, 0);
    g_assert_cmphex(qtest_readl(q, COMPLETION + 4), ==, 0);
    g_assert_cmphex(qtest_readl(q, COMPLETION + 8), ==, 0);
    g_assert_cmphex(qtest_readl(q, COMPLETION + 0xc), ==, 0);
    /* A post-reset transaction must be produced again, independently. */
    completion_submit(q, 7, true);
    g_assert_cmphex(qtest_readl(q, COMPLETION + 0xc), ==, 0x80);
    qtest_writel(q, COMPLETION + 4, 1);
}
