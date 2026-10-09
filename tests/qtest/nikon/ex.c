/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "libqtest.h"

static QTestState *start(void)
{
    QTestState *qts = qtest_init("-machine nikonex -display none");
    qtest_irq_intercept_in(qts, "/machine/ex/armv7m");
    return qts;
}

static void oscillator(void)
{
    QTestState *qts = start();
    const uint32_t cr = 0x40023800, pll = cr + 4, cfgr = cr + 8;
    g_assert_cmphex(qtest_readl(qts, cr) & (1 << 17), ==, 0);
    qtest_writel(qts, cr, qtest_readl(qts, cr) | (1 << 16));
    g_assert_cmphex(qtest_readl(qts, cr) & (1 << 17), ==, 0);
    qtest_clock_step(qts, 100000);
    g_assert_cmphex(qtest_readl(qts, cr) & (1 << 17), ==, 1 << 17);
    qtest_writel(qts, pll, 10 | (100 << 6) | (1 << 22) | (2 << 24));
    qtest_writel(qts, cr, qtest_readl(qts, cr) | (1 << 24));
    qtest_writel(qts, cfgr, 2);
    g_assert_cmphex(qtest_readl(qts, cfgr) & 12, ==, 0);
    qtest_clock_step(qts, 100000);
    g_assert_cmphex(qtest_readl(qts, cr) & (1 << 25), ==, 1 << 25);
    g_assert_cmphex(qtest_readl(qts, cfgr) & 12, ==, 8);
    /* Cancelling an oscillator before its deadline cannot make it ready. */
    qtest_writel(qts, cr, 1);
    qtest_clock_step(qts, 100000);
    g_assert_cmphex(qtest_readl(qts, cr) & 0x02020000, ==, 0);
    qtest_quit(qts);
}

static void gpio_exti(void)
{
    QTestState *qts = start();
    uint32_t gpio = 0x40020800, exti = 0x40013c00;
    /* Port C pin 5 routed by guest EXTICR to external IRQ23. */
    qtest_writel(qts, 0x4001380c, 2 << 4);
    qtest_writel(qts, exti, 1 << 5);
    qtest_writel(qts, exti + 8, 1 << 5);
    qtest_writel(qts, gpio, 1 << 10);
    qtest_writel(qts, gpio + 0x18, 1 << 5);
    g_assert_cmphex(qtest_readl(qts, gpio + 0x10) & (1 << 5), ==, 1 << 5);
    g_assert_cmphex(qtest_readl(qts, exti + 0x14), ==, 1 << 5);
    g_assert_true(qtest_get_irq(qts, 23));
    qtest_writel(qts, exti + 0x14, 1 << 5);
    g_assert_false(qtest_get_irq(qts, 23));
    qtest_writel(qts, gpio + 0x18, 1 << 21);
    g_assert_cmphex(qtest_readl(qts, gpio + 0x10) & (1 << 5), ==, 0);
    qtest_quit(qts);
}

static void dma(void)
{
    QTestState *qts = start();
    uint32_t controller = 0x40026400, stream = controller + 0x10;
    const uint32_t source = 0x20001000, dest = 0x20002000;
    qtest_writel(qts, source, 0xabcdef01);
    qtest_writel(qts, source + 4, 0x12345678);
    qtest_writel(qts, stream + 4, 2);
    qtest_writel(qts, stream + 8, source);
    qtest_writel(qts, stream + 12, dest);
    g_assert_cmphex(qtest_readl(qts, dest), ==, 0);
    qtest_writel(qts, stream, 1 | (2 << 6) | (1 << 9) | (1 << 10) |
                              (2 << 11) | (2 << 13) | (1 << 4));
    g_assert_cmphex(qtest_readl(qts, dest), ==, 0xabcdef01);
    g_assert_cmphex(qtest_readl(qts, dest + 4), ==, 0x12345678);
    g_assert_cmphex(qtest_readl(qts, stream + 4), ==, 0);
    g_assert_cmphex(qtest_readl(qts, stream) & 1, ==, 0);
    g_assert_true(qtest_get_irq(qts, 56));
    qtest_writel(qts, controller + 8, 0x3d);
    g_assert_false(qtest_get_irq(qts, 56));
    qtest_quit(qts);
}

static void i2c_absent(void)
{
    QTestState *qts = start();
    uint32_t i2c = 0x40005400;
    qtest_writel(qts, i2c + 4, 1 << 8);
    qtest_writel(qts, i2c, 1 | (1 << 8));
    g_assert_cmphex(qtest_readl(qts, i2c + 0x14), ==, 1);
    qtest_writel(qts, i2c + 0x10, 0xa0);
    g_assert_cmphex(qtest_readl(qts, i2c + 0x14) & 0x402, ==, 0x400);
    g_assert_true(qtest_get_irq(qts, 32));
    qtest_writel(qts, i2c + 0x14, 0);
    g_assert_false(qtest_get_irq(qts, 32));
    qtest_writel(qts, i2c, 1 | (1 << 9));
    g_assert_cmphex(qtest_readl(qts, i2c + 0x18), ==, 0);
    qtest_quit(qts);
}

static void peripheral_reset(void)
{
    QTestState *qts = start();
    qtest_writel(qts, 0x40003800, 0x40);
    qtest_writel(qts, 0x4000380c, 0x42);
    g_assert_cmphex(qtest_readl(qts, 0x40003808) & 2, ==, 0);
    qtest_writel(qts, 0x40023820, 1 << 14);
    g_assert_cmphex(qtest_readl(qts, 0x40003800), ==, 0);
    g_assert_cmphex(qtest_readl(qts, 0x40003808), ==, 2);
    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/nikon/ex/oscillator", oscillator);
    qtest_add_func("/nikon/ex/gpio-exti", gpio_exti);
    qtest_add_func("/nikon/ex/dma", dma);
    qtest_add_func("/nikon/ex/i2c-absent", i2c_absent);
    qtest_add_func("/nikon/ex/peripheral-reset", peripheral_reset);
    return g_test_run();
}
