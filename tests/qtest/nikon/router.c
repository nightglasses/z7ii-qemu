/* SLA router source consumers and explicit pending provider.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "libqtest.h"

void nikon_router_test(QTestState *q, const char *path, uint64_t base);

void nikon_router_test(QTestState *q, const char *path, uint64_t base)
{
    static const unsigned masks[] = { 0x34, 0x40, 0x4c };
    static const unsigned raw[] = { 0x38, 0x44, 0x50 };
    static const uint32_t all[] = { 0x7ff, UINT32_MAX, UINT32_MAX };
    static const unsigned lane_words[] = {
        0xc8, 0xcc, 0xd0, 0xd4, 0xd8, 0xdc, 0xec,
    };
    static const unsigned words[] = { 0xa0, 0xa8, 0xac, 0x314 };
    unsigned i, j, domain;

    qtest_irq_intercept_out_named(q, path, "sysbus-irq");
    for (domain = 0; domain < 3; domain++) {
        g_assert_cmphex(qtest_readl(q, base + masks[domain]), ==, 0);
        g_assert_cmphex(qtest_readl(q, base + raw[domain]), ==, 0);
        g_assert_false(qtest_get_irq(q, domain));
    }

    /* Exact 9F030 packed-selector writes, for its two valid alternatives. */
    qtest_writel(q, base, 0x3f);
    qtest_writel(q, base + 8, 0x7fff);
    qtest_writel(q, base + 4, 0x21);
    qtest_writel(q, base + 0xc, 0x4567);
    g_assert_cmphex(qtest_readl(q, base), ==, 0x3f);
    g_assert_cmphex(qtest_readl(q, base + 8), ==, 0x7fff);
    g_assert_cmphex(qtest_readl(q, base + 4), ==, 0x21);
    g_assert_cmphex(qtest_readl(q, base + 0xc), ==, 0x4567);

    /* Inherited finite configuration contract, without activating requests. */
    for (i = 0x10; i <= 0x1c; i += 4) {
        qtest_writel(q, base + i, 1);
        g_assert_cmphex(qtest_readl(q, base + i), ==, 1);
    }
    qtest_writel(q, base + 0x20, 0x7ff);
    qtest_writel(q, base + 0x24, 0x321);
    g_assert_cmphex(qtest_readl(q, base + 0x20), ==, 0x7ff);
    g_assert_cmphex(qtest_readl(q, base + 0x24), ==, 0x321);
    for (i = 0x70; i <= 0x9c; i += 4) {
        qtest_writel(q, base + i, 3);
        g_assert_cmphex(qtest_readl(q, base + i), ==, 3);
    }
    qtest_writel(q, base + 0xa4, 0x1f);
    qtest_writel(q, base + 0xa4,
                 (qtest_readl(q, base + 0xa4) & ~0x1f) | 0xa);
    g_assert_cmphex(qtest_readl(q, base + 0xa4), ==, 0xa);
    for (i = 0; i < G_N_ELEMENTS(words); i++) {
        qtest_writel(q, base + words[i], 0xfedcba98 ^ i);
        g_assert_cmphex(qtest_readl(q, base + words[i]), ==, 0xfedcba98 ^ i);
    }
    qtest_writel(q, base + 0x328, 0x87);
    qtest_writel(q, base + 0x32c, 0x65);
    g_assert_cmphex(qtest_readl(q, base + 0x328), ==, 0x87);
    g_assert_cmphex(qtest_readl(q, base + 0x32c), ==, 0x65);
    for (i = 0xb4; i <= 0xc4; i += 4) {
        qtest_writel(q, base + i, 0xff - i);
        g_assert_cmphex(qtest_readl(q, base + i), ==, 0xff - i);
    }
    for (i = 0; i < 6; i++) {
        for (j = 0; j < G_N_ELEMENTS(lane_words); j++) {
            uint64_t address = base + lane_words[j] + i * 0x60;
            uint32_t value = 0x87654321 ^ (i << 8) ^ j;

            qtest_writel(q, address, value);
            g_assert_cmphex(qtest_readl(q, address), ==, value);
        }
        qtest_writel(q, base + 0xe8 + i * 0x60, 0xa0 + i);
        g_assert_cmphex(qtest_readl(q, base + 0xe8 + i * 0x60), ==,
                        0xa0 + i);
    }
    qtest_writel(q, base + 0xb0, 0x12345678);
    g_assert_cmphex(qtest_readl(q, base + 0xb0), ==, 0x12345678);
    qtest_clock_step(q, 1000000);
    for (domain = 0; domain < 3; domain++) {
        g_assert_cmphex(qtest_readl(q, base + raw[domain]), ==, 0);
        g_assert_false(qtest_get_irq(q, domain));
    }

    /* Providers are independent of configuration and initially masked. */
    for (i = 0; i < 75; i++) {
        qtest_set_irq_in(q, path, "external-pending", i, 1);
    }
    for (domain = 0; domain < 3; domain++) {
        g_assert_cmphex(qtest_readl(q, base + raw[domain]), ==, all[domain]);
        g_assert_false(qtest_get_irq(q, domain));
        qtest_writel(q, base + masks[domain], all[domain]);
        g_assert_true(qtest_get_irq(q, domain));
    }

    /* Original IRQ86 loop acknowledges each of the eleven bits separately. */
    for (i = 0; i < 11; i++) {
        qtest_writel(q, base + 0x38, 1u << i);
        g_assert_cmphex(qtest_readl(q, base + 0x38), ==,
                        0x7ff & ~((1u << (i + 1)) - 1));
        g_assert_cmpint(qtest_get_irq(q, 0), ==, i != 10);
        g_assert_true(qtest_get_irq(q, 1));
        g_assert_true(qtest_get_irq(q, 2));
    }
    /* IRQ87/88 each read one raw word and write the same word to ACK. */
    for (domain = 1; domain < 3; domain++) {
        qtest_writel(q, base + raw[domain],
                     qtest_readl(q, base + raw[domain]));
        g_assert_cmphex(qtest_readl(q, base + raw[domain]), ==, 0);
        g_assert_false(qtest_get_irq(q, domain));
    }
    qtest_set_irq_in(q, path, "external-pending", 42, 1);
    g_assert_cmphex(qtest_readl(q, base + 0x44), ==, 0);
    qtest_set_irq_in(q, path, "external-pending", 42, 0);
    qtest_set_irq_in(q, path, "external-pending", 42, 1);
    g_assert_cmphex(qtest_readl(q, base + 0x44), ==, 0x80000000);
    qtest_writel(q, base + 0x40, 0);
    g_assert_false(qtest_get_irq(q, 1));
    g_assert_cmphex(qtest_readl(q, base + 0x44), ==, 0x80000000);
    qtest_writel(q, base + 0x40, 0x80000000);
    g_assert_true(qtest_get_irq(q, 1));

    qtest_system_reset(q);
    g_assert_cmphex(qtest_readl(q, base + 0xb0), ==, 0);
    for (domain = 0; domain < 3; domain++) {
        g_assert_cmphex(qtest_readl(q, base + masks[domain]), ==, 0);
        g_assert_cmphex(qtest_readl(q, base + raw[domain]), ==, 0);
        g_assert_false(qtest_get_irq(q, domain));
    }
    qtest_set_irq_in(q, path, "external-pending", 42, 1);
    g_assert_cmphex(qtest_readl(q, base + 0x44), ==, 0);
    qtest_set_irq_in(q, path, "external-pending", 42, 0);
    qtest_set_irq_in(q, path, "external-pending", 42, 1);
    g_assert_cmphex(qtest_readl(q, base + 0x44), ==, 0x80000000);
    g_assert_false(qtest_get_irq(q, 1));
}
