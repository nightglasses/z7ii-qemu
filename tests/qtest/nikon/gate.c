/* SLA event gate finite source profile and explicit pending inputs.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "libqtest.h"

void nikon_gate_test(QTestState *q, const char *path, uint64_t base);

void nikon_gate_test(QTestState *q, const char *path, uint64_t base)
{
    static const struct {
        unsigned first, last;
    } maps[] = {
        { 0x2200, 0x22fc }, { 0x2300, 0x23fc },
        { 0x2880, 0x28f4 }, { 0x2900, 0x2924 },
    };
    static const unsigned packed_words[] = {
        0xf8, 0xfc, 0x100, 0x104, 0x120, 0x124, 0x128, 0x12c,
    };
    unsigned i, j, group;

    qtest_irq_intercept_out_named(q, path, "sysbus-irq");
    /* Actual 944D2 initializer write does not reset or invent pending. */
    qtest_writel(q, base, 0);
    qtest_writel(q, base + 8, 0);
    g_assert_cmphex(qtest_readl(q, base + 4), ==, 0);
    for (i = 0; i < 3; i++) {
        g_assert_cmphex(qtest_readl(q, base + 0xc + i * 4), ==, 0);
        g_assert_cmphex(qtest_readl(q, base + 0x18 + i * 4), ==, 0);
    }
    qtest_clock_step(q, 1000000);
    g_assert_false(qtest_get_irq(q, 0));
    g_assert_cmphex(qtest_readl(q, base + 0x18), ==, 0);

    for (group = 0; group < 2; group++) {
        uint64_t input = base + 0x2000 + group * 0x100;
        uint64_t parameter = base + 0x2600 + group * 0x40;
        uint32_t packed = 0x12340;

        /* Complete 945B6 packet paths and 9458A/945A2 low-nibble RMW. */
        qtest_writel(q, base + 0x20a4, 0);
        qtest_writel(q, input, packed);
        qtest_writel(q, input + 4, 0x3fff);
        qtest_writel(q, input + 8, 0xa5);
        qtest_writel(q, input + 0x10, 0x89abcdef);
        qtest_writel(q, input + 0x14, 0x76543210);
        qtest_writel(q, input + 0x18, 0x9876);
        qtest_writel(q, input, (qtest_readl(q, input) & ~0xf) | 1);
        g_assert_cmphex(qtest_readl(q, input), ==, packed | 1);
        qtest_writel(q, input, qtest_readl(q, input) & ~0xf);
        g_assert_cmphex(qtest_readl(q, input), ==, packed);
        g_assert_cmphex(qtest_readl(q, input + 8), ==, 0xa5);
        g_assert_cmphex(qtest_readl(q, input + 0x10), ==, 0x89abcdef);
        g_assert_cmphex(qtest_readl(q, input + 0x14), ==, 0x76543210);
        g_assert_cmphex(qtest_readl(q, input + 0x18), ==, 0x9876);

        /* Regression: 94640 has an unclamped OR operand, for both inputs. */
        qtest_writel(q, input + 4,
                     (qtest_readl(q, input + 4) & ~1) | 0x80010001);
        g_assert_cmphex(qtest_readl(q, input + 4), ==, 0x80013fff);
        qtest_writel(q, input + 4, qtest_readl(q, input + 4) & ~1);
        g_assert_cmphex(qtest_readl(q, input + 4), ==, 0x80013ffe);

        /* 946E0 packet selects independent 2600/2640 parameter groups. */
        qtest_writel(q, parameter + 0x20, 0);
        qtest_writel(q, parameter, 0xabcd);
        qtest_writel(q, parameter + 4, 0x12345678);
        qtest_writel(q, parameter + 8, 0xfedcba98);
        qtest_writel(q, parameter + 0xc, 0x87);
        qtest_writel(q, parameter + 0x10, 0x98760054);
        qtest_writel(q, parameter + 0x18, 0x32);
        g_assert_cmphex(qtest_readl(q, parameter), ==, 0xabcd);
        g_assert_cmphex(qtest_readl(q, parameter + 4), ==, 0x12345678);
        g_assert_cmphex(qtest_readl(q, parameter + 8), ==, 0xfedcba98);
        g_assert_cmphex(qtest_readl(q, parameter + 0xc), ==, 0x87);
        g_assert_cmphex(qtest_readl(q, parameter + 0x10), ==, 0x98760054);
        g_assert_cmphex(qtest_readl(q, parameter + 0x18), ==, 0x32);
    }
    for (i = 0; i < G_N_ELEMENTS(maps); i++) {
        for (j = maps[i].first; j <= maps[i].last; j += 4) {
            uint32_t value = 0x87654321 ^ j;

            qtest_writel(q, base + j, value);
            g_assert_cmphex(qtest_readl(q, base + j), ==, value);
        }
    }
    /* Full-byte packed 94836 values; getter extraction does not narrow writes. */
    for (i = 0; i < 6; i++) {
        qtest_writel(q, base + 0x2820 + i * 4, 0xfedcba98 ^ i);
        g_assert_cmphex(qtest_readl(q, base + 0x2820 + i * 4), ==,
                        0xfedcba98 ^ i);
    }
    qtest_writel(q, base + 0x2838, 0xfedc);
    qtest_writel(q, base + 0x2684, 0xfedcba98);
    qtest_writel(q, base + 0x2690, 0xabcd);
    g_assert_cmphex(qtest_readl(q, base + 0x2838), ==, 0xfedc);
    g_assert_cmphex(qtest_readl(q, base + 0x2684), ==, 0xfedcba98);
    g_assert_cmphex(qtest_readl(q, base + 0x2690), ==, 0xabcd);
    qtest_writel(q, base + 0xf4, 0xfff00);
    qtest_writel(q, base + 0x108, 0xabc00);
    qtest_writel(q, base + 0x10c, 0xab0fff00);
    g_assert_cmphex(qtest_readl(q, base + 0xf4), ==, 0xfff00);
    g_assert_cmphex(qtest_readl(q, base + 0x108), ==, 0xabc00);
    g_assert_cmphex(qtest_readl(q, base + 0x10c), ==, 0xab0fff00);
    for (i = 0; i < G_N_ELEMENTS(packed_words); i++) {
        qtest_writel(q, base + packed_words[i], 0x87654320 ^ (i << 4));
        g_assert_cmphex(qtest_readl(q, base + packed_words[i]), ==,
                        0x87654320 ^ (i << 4));
    }
    for (i = 0x30; i <= 0x34; i += 4) {
        qtest_writel(q, base + i, qtest_readl(q, base + i) | 0x80000000);
        g_assert_cmphex(qtest_readl(q, base + i), ==, 0x80000000);
    }
    qtest_clock_step(q, 1000000);
    g_assert_cmphex(qtest_readl(q, base + 0x18), ==, 0);
    g_assert_false(qtest_get_irq(q, 0));

    /* Gate-open RMW 9453A, full-word masks 949DA, three separate raw words. */
    qtest_writel(q, base + 4, (qtest_readl(q, base + 4) & ~3) | 1);
    for (i = 0; i < 3; i++) {
        qtest_writel(q, base + 0xc + i * 4, 0x80000000);
        qtest_set_irq_in(q, path, "pending-bit", i * 32 + 31, 1);
        g_assert_cmphex(qtest_readl(q, base + 0x18 + i * 4), ==, 0x80000000);
    }
    g_assert_true(qtest_get_irq(q, 0));
    for (i = 0; i < 3; i++) {
        qtest_writel(q, base + 0x18 + i * 4,
                     qtest_readl(q, base + 0x18 + i * 4));
        g_assert_cmphex(qtest_readl(q, base + 0x18 + i * 4), ==, 0);
        g_assert_cmpint(qtest_get_irq(q, 0), ==, i != 2);
    }
    qtest_set_irq_in(q, path, "pending-bit", 95, 1);
    g_assert_cmphex(qtest_readl(q, base + 0x20), ==, 0);
    qtest_set_irq_in(q, path, "pending-bit", 95, 0);
    qtest_set_irq_in(q, path, "pending-bit", 95, 1);
    qtest_writel(q, base + 0x14, 0);
    g_assert_cmphex(qtest_readl(q, base + 0x20), ==, 0x80000000);
    g_assert_false(qtest_get_irq(q, 0));
    qtest_writel(q, base + 0x14, 0x80000000);
    g_assert_true(qtest_get_irq(q, 0));

    /* Retained baseline virtual stop: deferred once, never a data producer. */
    qtest_writel(q, base + 0xc, 1);
    qtest_writel(q, base + 0x20, 0x80000000);
    qtest_writel(q, base + 4, 0);
    qtest_clock_step(q, 10000);
    qtest_writel(q, base + 4, 1);
    qtest_clock_step(q, 100000);
    g_assert_cmphex(qtest_readl(q, base + 0x18), ==, 0);
    qtest_writel(q, base + 4, 0);
    g_assert_cmphex(qtest_readl(q, base + 0x18), ==, 0);
    qtest_clock_step(q, 19999);
    g_assert_cmphex(qtest_readl(q, base + 0x18), ==, 0);
    qtest_clock_step(q, 1);
    g_assert_cmphex(qtest_readl(q, base + 0x18), ==, 1);
    g_assert_true(qtest_get_irq(q, 0));
    qtest_writel(q, base + 0x18, 1);
    g_assert_false(qtest_get_irq(q, 0));
    qtest_clock_step(q, 1000000);
    g_assert_cmphex(qtest_readl(q, base + 0x18), ==, 0);

    qtest_system_reset(q);
    g_assert_cmphex(qtest_readl(q, base + 0x2004), ==, 0);
    g_assert_cmphex(qtest_readl(q, base + 0x2924), ==, 0);
    qtest_writel(q, base + 4, 1);
    qtest_set_irq_in(q, path, "pending-bit", 95, 1);
    g_assert_cmphex(qtest_readl(q, base + 0x20), ==, 0);
    g_assert_false(qtest_get_irq(q, 0));
}
