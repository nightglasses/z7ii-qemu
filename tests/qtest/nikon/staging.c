/* SOURCE11 93DC0..94320 staged configuration and explicit event provider.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "libqtest.h"

void nikon_staging_test(QTestState *q, const char *path, uint64_t base);

static void event(QTestState *q, const char *path, unsigned index)
{
    qtest_set_irq_in(q, path, "external-event", index, 0);
    qtest_set_irq_in(q, path, "external-event", index, 1);
}

void nikon_staging_test(QTestState *q, const char *path, uint64_t base)
{
    /* Values assembled by the complete 93F8C/9400C packet setters. */
    static const struct {
        unsigned offset;
        uint32_t value;
    } packet[] = {
        { 0x40, 0x83 }, { 0x44, 0x1234 }, { 0x48, 0x5678 },
        { 0x4c, 0x91 }, { 0x50, 0x12345678 }, { 0x54, 0x89abcdef },
        { 0x58, 0x76543210 }, { 0x5c, 0xfedcba98 },
        { 0x60, 0x001b0005 }, { 0x64, 0x07050301 },
        { 0x68, 0xa5 }, { 0x6c, 0x5a },
        { 0x80, 0x21 }, { 0x84, 0x43 }, { 0x88, 0x65 },
        { 0x8c, 0x12345678 }, { 0x90, 0x1fff7fff },
        { 0x94, 0x03214567 }, { 0x98, 0xd }, { 0x9c, 0x00352113 },
        { 0xa0, 0x2468 }, { 0xa4, 0x1357 },
        { 0xa8, 0x87 }, { 0xac, 0x69 }, { 0xb0, 0x009a0bcd },
        { 0xb4, 0xabcd }, { 0xb8, 0x1234 },
        { 0xbc, 0xef }, { 0xc0, 0x98 }, { 0xc4, 0x00760abc },
        { 0xc8, 0x54 }, { 0xcc, 0x23451234 },
        { 0xd0, 0x000a0021 }, { 0xd4, 0x65321232 },
    };
    unsigned i;

    qtest_irq_intercept_out_named(q, path, "sysbus-irq");
    /* Exact 93E3C stop write at original PC93E48 is legal while cold. */
    qtest_writel(q, base + 0x18, 0);
    g_assert_cmphex(qtest_readl(q, base + 8), ==, 0);
    g_assert_cmphex(qtest_readl(q, base + 0x2c), ==, 0);
    g_assert_false(qtest_get_irq(q, 0));

    qtest_writel(q, base + 0x10, 0);
    for (i = 0; i < G_N_ELEMENTS(packet); i++) {
        qtest_writel(q, base + packet[i].offset, packet[i].value);
        g_assert_cmphex(qtest_readl(q, base + packet[i].offset), ==, 0);
    }
    qtest_writel(q, base + 0x10, 1);
    for (i = 0; i < G_N_ELEMENTS(packet); i++) {
        g_assert_cmphex(qtest_readl(q, base + packet[i].offset), ==,
                        packet[i].value);
    }

    /* Whole 94170 coefficient loops, including both signed final values. */
    qtest_writel(q, base + 0x10, 0);
    for (i = 0; i < 8; i++) {
        qtest_writel(q, base + 0x100 + i * 4, 0x03ff0000 | (i + 1));
        qtest_writel(q, base + 0x124 + i * 4, ((i + 1) << 16) | 0x3ff);
    }
    qtest_writel(q, base + 0x120, 0xffff8000);
    qtest_writel(q, base + 0x144, 0x7fff);
    qtest_writel(q, base + 0x10, 1);
    for (i = 0; i < 8; i++) {
        g_assert_cmphex(qtest_readl(q, base + 0x100 + i * 4), ==,
                        0x03ff0000 | (i + 1));
        g_assert_cmphex(qtest_readl(q, base + 0x124 + i * 4), ==,
                        ((i + 1) << 16) | 0x3ff);
    }
    g_assert_cmphex(qtest_readl(q, base + 0x120), ==, 0xffff8000);
    g_assert_cmphex(qtest_readl(q, base + 0x144), ==, 0x7fff);
    qtest_writel(q, base + 0x10, 0);
    qtest_writel(q, base + 0x148, 0x01230345);
    qtest_writel(q, base + 0x14c, 0x02340156);
    qtest_writel(q, base + 0x10, 1);
    g_assert_cmphex(qtest_readl(q, base + 0x148), ==, 0x01230345);
    g_assert_cmphex(qtest_readl(q, base + 0x14c), ==, 0x02340156);

    /* 93F36 writes its independent subblock without the +10 bracket. */
    qtest_writel(q, base, 1);
    qtest_writel(q, base + 4, 1);
    qtest_writel(q, base + 0x200, 1);
    qtest_writel(q, base + 0x208, 0x12);
    qtest_writel(q, base + 0x20c, 0x34);
    qtest_writel(q, base + 0x210, 0x5678);
    qtest_writel(q, base + 0x218, 0x9a);
    qtest_writel(q, base + 0x22c, 0xbc);
    for (i = 0; i < 8; i++) {
        qtest_writel(q, base + 0x234 + i * 4, 0x12345678 ^ i);
        g_assert_cmphex(qtest_readl(q, base + 0x234 + i * 4), ==,
                        0x12345678 ^ i);
    }
    qtest_writel(q, base + 0x200, 0);
    qtest_writel(q, base + 0x208, 0);
    g_assert_cmphex(qtest_readl(q, base + 0x200), ==, 0);
    g_assert_cmphex(qtest_readl(q, base + 0x208), ==, 0);
    g_assert_cmphex(qtest_readl(q, base + 0x20c), ==, 0x34);
    g_assert_cmphex(qtest_readl(q, base + 0x210), ==, 0x5678);
    g_assert_cmphex(qtest_readl(q, base + 0x218), ==, 0x9a);
    g_assert_cmphex(qtest_readl(q, base + 0x22c), ==, 0xbc);

    /* All independent 9422C..94290 setter paths. */
    qtest_writel(q, base + 0x10, 0);
    qtest_writel(q, base + 0x204, 0x89abcdef);
    qtest_writel(q, base + 0x214, 0x76543210);
    qtest_writel(q, base + 0x220, 0xfedcba98);
    qtest_writel(q, base + 0x224, 1);
    qtest_writel(q, base + 0x228, 0x12345678);
    qtest_writel(q, base + 0x230, 0xabcdef01);
    qtest_writel(q, base + 0x10, 1);
    g_assert_cmphex(qtest_readl(q, base + 0x204), ==, 0x89abcdef);
    g_assert_cmphex(qtest_readl(q, base + 0x214), ==, 0x76543210);
    g_assert_cmphex(qtest_readl(q, base + 0x220), ==, 0xfedcba98);
    g_assert_cmphex(qtest_readl(q, base + 0x224), ==, 1);
    g_assert_cmphex(qtest_readl(q, base + 0x228), ==, 0x12345678);
    g_assert_cmphex(qtest_readl(q, base + 0x230), ==, 0xabcdef01);
    qtest_writel(q, base + 0x10, 0);
    qtest_writel(q, base + 0x224, 0);
    qtest_writel(q, base + 0x10, 1);
    g_assert_cmphex(qtest_readl(q, base + 0x224), ==, 0);
    g_assert_cmphex(qtest_readl(q, base + 0x228), ==, 0x12345678);

    qtest_writel(q, base + 0x18, 1);
    qtest_writel(q, base + 0x20, 0x10006);
    qtest_writel(q, base + 8, 1);
    qtest_writel(q, base + 0xc, 1);
    qtest_writel(q, base + 0x14, 1);
    qtest_clock_step(q, 1000000);
    g_assert_cmphex(qtest_readl(q, base + 8), ==, 1);
    g_assert_cmphex(qtest_readl(q, base + 0x2c), ==, 0);
    g_assert_false(qtest_get_irq(q, 0));
    event(q, path, 0);
    event(q, path, 1);
    event(q, path, 2);
    g_assert_cmphex(qtest_readl(q, base + 8), ==, 0);
    g_assert_cmphex(qtest_readl(q, base + 0x2c), ==, 0x10006);
    g_assert_true(qtest_get_irq(q, 0));
    qtest_writel(q, base + 0x2c, 2);
    g_assert_cmphex(qtest_readl(q, base + 0x2c), ==, 0x10004);
    g_assert_true(qtest_get_irq(q, 0));
    qtest_writel(q, base + 0x20, 0);
    g_assert_false(qtest_get_irq(q, 0));
    qtest_writel(q, base + 0x20, 0x10006);
    g_assert_true(qtest_get_irq(q, 0));
    qtest_writel(q, base + 0x2c, qtest_readl(q, base + 0x2c));
    g_assert_false(qtest_get_irq(q, 0));
    for (i = 0; i < 3; i++) {
        event(q, path, i);
    }
    g_assert_cmphex(qtest_readl(q, base + 0x2c), ==, 0);

    /* Original ISR cancellation does not acknowledge a different cause. */
    qtest_writel(q, base + 8, 1);
    event(q, path, 0);
    qtest_writel(q, base + 0xc, 1);
    qtest_writel(q, base, 1);
    qtest_writel(q, base + 4, 1);
    event(q, path, 2);
    g_assert_cmphex(qtest_readl(q, base + 0x2c), ==, 2);
    qtest_writel(q, base + 0x18, 0);
    g_assert_false(qtest_get_irq(q, 0));
    g_assert_cmphex(qtest_readl(q, base + 0x2c), ==, 2);

    /* Reset is a functional convention; held input is not a new event. */
    qtest_system_reset(q);
    g_assert_cmphex(qtest_readl(q, base + 0x40), ==, 0);
    g_assert_cmphex(qtest_readl(q, base + 0x250), ==, 0);
    g_assert_cmphex(qtest_readl(q, base + 0x20), ==, 0);
    g_assert_cmphex(qtest_readl(q, base + 0x2c), ==, 0);
    qtest_writel(q, base + 0x18, 1);
    qtest_writel(q, base + 8, 1);
    qtest_set_irq_in(q, path, "external-event", 0, 1);
    g_assert_cmphex(qtest_readl(q, base + 8), ==, 1);
    g_assert_cmphex(qtest_readl(q, base + 0x2c), ==, 0);
    event(q, path, 0);
    g_assert_cmphex(qtest_readl(q, base + 0x2c), ==, 2);
}
