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

static void rtc(void)
{
    QTestState *qts = start();
    const uint32_t base = 0x40002800;
    qtest_writel(qts, 0x40007000, 1 << 8);
    qtest_writel(qts, base, 0x235959);
    g_assert_cmphex(qtest_readl(qts, base), ==, 0); /* locked */
    qtest_writel(qts, 0x40023874, 1);
    qtest_writel(qts, 0x40023870, (1 << 15) | (2 << 8));
    qtest_clock_step(qts, 100000);
    qtest_writel(qts, base + 0x24, 0xca);
    qtest_writel(qts, base + 0x24, 0x53);
    qtest_writel(qts, base + 0x0c, 0x80);
    g_assert_cmphex(qtest_readl(qts, base + 0x0c) & 0x40, ==, 0);
    qtest_clock_step(qts, 62500);
    g_assert_cmphex(qtest_readl(qts, base + 0x0c) & 0x40, ==, 0x40);
    qtest_writel(qts, base + 0x10, 0x007f00f9); /* 32 kHz /128 /250 */
    qtest_writel(qts, base, 0x235959);
    qtest_writel(qts, base + 4, 0x246228); /* 2024-02-28, Wednesday */
    qtest_writel(qts, base + 0x0c, 0);
    qtest_clock_step(qts, 62500);
    qtest_writel(qts, base + 0x14, 999);
    qtest_writel(qts, 0x40013c00, 1 << 22);
    qtest_writel(qts, base + 8, (1 << 10) | (1 << 14));
    qtest_clock_step(qts, 500000000);
    g_assert_true(qtest_get_irq(qts, 3));
    g_assert_cmphex(qtest_readl(qts, base + 0x0c) & (1 << 10), ==, 1 << 10);
    qtest_writel(qts, base + 0x0c,
                 qtest_readl(qts, base + 0x0c) & ~(1 << 10));
    qtest_writel(qts, 0x40013c14, 1 << 22);
    g_assert_false(qtest_get_irq(qts, 3));
    qtest_clock_step(qts, 500000000);
    g_assert_cmphex(qtest_readl(qts, base), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + 4), ==, 0x248229);
    qtest_quit(qts);
}

static void io_start(QTestState *qts, bool read)
{
    qtest_writel(qts, 0x40005400, 1 | (1 << 8) | (1 << 10));
    qtest_writel(qts, 0x40005410, 0x44 | read);
    g_assert_cmphex(qtest_readl(qts, 0x40005414) & 0x402, ==, 2);
    qtest_readl(qts, 0x40005418);
}

static void io_stop(QTestState *qts)
{
    qtest_writel(qts, 0x40005400, 1 | (1 << 9));
}

static void io_write(QTestState *qts, uint8_t reg, const uint8_t *data, size_t len)
{
    io_start(qts, false);
    qtest_writel(qts, 0x40005410, reg);
    for (size_t i = 0; i < len; i++) {
        qtest_writel(qts, 0x40005410, data[i]);
    }
    io_stop(qts);
}

static void io_read(QTestState *qts, uint8_t reg, uint8_t *data, size_t len)
{
    io_start(qts, false);
    qtest_writel(qts, 0x40005410, reg);
    io_start(qts, true);
    for (size_t i = 0; i < len; i++) {
        data[i] = qtest_readl(qts, 0x40005410);
    }
    io_stop(qts);
}

static void io(void)
{
    QTestState *qts = start();
    uint8_t value[6], data[] = { 0x11, 0x22, 0x33, 0x44 };
    /* Offset5 cycles 5,6,4,5; AI walks across the reserved hole to polarity. */
    io_write(qts, 5, data, sizeof(data));
    io_read(qts, 4, value, 3);
    g_assert_cmphex(value[0], ==, 0x33);
    g_assert_cmphex(value[1], ==, 0x44);
    g_assert_cmphex(value[2], ==, 0x22);
    io_read(qts, 0x86, value, 2);
    g_assert_cmphex(value[0], ==, 0x22);
    g_assert_cmphex(value[1], ==, 0);
    /* Floating inputs resolve high; externally driven low is a real input. */
    io_read(qts, 0, value, 3);
    g_assert_cmphex(value[0], ==, 0xff);
    qtest_set_irq_in(qts, "/machine/ex/io", "pad", 9, 0);
    io_read(qts, 1, value, 1);
    g_assert_cmphex(value[0], ==, 0xfd);
    /* B1 falling edge stays pending across a later rise; live status does not
     * acknowledge it. Input-latch captures the first low value until read. */
    qtest_set_irq_in(qts, "/machine/ex/io", "pad", 9, 1);
    io_read(qts, 1, value, 1);
    data[0] = 2;
    io_write(qts, 0x49, data, 1);
    data[0] = 8;
    io_write(qts, 0x62, data, 1);
    data[0] = 0xfd;
    io_write(qts, 0x55, data, 1);
    qtest_writel(qts, 0x40013c00, 1);
    qtest_writel(qts, 0x40013c0c, 1); /* active-low INT -> A0 falling EXTI0 */
    qtest_set_irq_in(qts, "/machine/ex/io", "pad", 9, 0);
    qtest_set_irq_in(qts, "/machine/ex/io", "pad", 9, 1);
    io_read(qts, 0x6d, value, 1);
    g_assert_cmphex(value[0], ==, 0xff);
    io_read(qts, 0x59, value, 1);
    g_assert_cmphex(value[0], ==, 2);
    g_assert_true(qtest_get_irq(qts, 6));
    io_read(qts, 1, value, 1);
    g_assert_cmphex(value[0], ==, 0xfd);
    io_read(qts, 0x59, value, 1);
    g_assert_cmphex(value[0], ==, 0);
    qtest_writel(qts, 0x40013c14, 1);
    g_assert_false(qtest_get_irq(qts, 6));
    qtest_set_irq_in(qts, "/machine/ex/io", "pad", 9, 0);
    data[0] = 2;
    io_write(qts, 0x69, data, 1);
    io_read(qts, 0x59, value, 1);
    g_assert_cmphex(value[0], ==, 0);
    /* Released P2_4 follows its programmed pull-down, then push-pull output. */
    data[0] = 0xef;
    io_write(qts, 0x52, data, 1);
    data[0] = 0x10;
    io_write(qts, 0x4e, data, 1);
    io_read(qts, 0x6e, value, 1);
    g_assert_cmphex(value[0], ==, 0xef);
    data[0] = 0xef;
    io_write(qts, 0x0e, data, 1);
    data[0] = 0xff;
    io_write(qts, 6, data, 1);
    io_read(qts, 0x6e, value, 1);
    g_assert_cmphex(value[0], ==, 0xff);
    /* Debounce advances only on the external P0_0 time base. */
    data[0] = 3;
    io_write(qts, 0x74, data, 1);
    data[0] = 2;
    io_write(qts, 0x76, data, 1);
    qtest_set_irq_in(qts, "/machine/ex/io", "pad", 1, 0);
    for (unsigned i = 0; i < 9; i++) {
        qtest_set_irq_in(qts, "/machine/ex/io", "pad", 0, 0);
        qtest_set_irq_in(qts, "/machine/ex/io", "pad", 0, 1);
    }
    io_read(qts, 0x6c, value, 1);
    g_assert_cmphex(value[0], ==, 0xff);
    for (unsigned i = 0; i < 2; i++) {
        qtest_set_irq_in(qts, "/machine/ex/io", "pad", 0, 0);
        qtest_set_irq_in(qts, "/machine/ex/io", "pad", 0, 1);
    }
    io_read(qts, 0x6c, value, 1);
    g_assert_cmphex(value[0], ==, 0xfd);
    /* Global open-drain for port2, reversed for pin4 by IOCR. */
    data[0] = 4;
    io_write(qts, 0x5c, data, 1);
    io_read(qts, 0x6e, value, 1);
    g_assert_cmphex(value[0], ==, 0xef);
    data[0] = 0x10;
    io_write(qts, 0x72, data, 1);
    io_read(qts, 0x6e, value, 1);
    g_assert_cmphex(value[0], ==, 0xff);
    /* Physical reset holds the peer unaddressable, then restores registers. */
    qtest_set_irq_in(qts, "/machine/ex/io", "reset", 0, 0);
    qtest_writel(qts, 0x40005400, 1 | (1 << 8));
    qtest_writel(qts, 0x40005410, 0x44);
    g_assert_cmphex(qtest_readl(qts, 0x40005414) & 0x400, ==, 0x400);
    io_stop(qts);
    qtest_set_irq_in(qts, "/machine/ex/io", "reset", 0, 1);
    io_read(qts, 4, value, 3);
    g_assert_cmphex(value[0], ==, 0xff);
    /* Original HAL three-byte finish: BTF, clear ACK, read first, STOP,
     * then drain the two received bytes without another external clock. */
    io_start(qts, false);
    qtest_writel(qts, 0x40005410, 4);
    io_start(qts, true);
    g_assert_cmphex(qtest_readl(qts, 0x40005414) & 0x44, ==, 0x44);
    qtest_writel(qts, 0x40005400, 1);
    g_assert_cmphex(qtest_readl(qts, 0x40005410), ==, 0xff);
    io_stop(qts);
    g_assert_cmphex(qtest_readl(qts, 0x40005410), ==, 0xff);
    g_assert_cmphex(qtest_readl(qts, 0x40005410), ==, 0xff);
    g_assert_cmphex(qtest_readl(qts, 0x40005414) & 0x44, ==, 0);
    /* Reserved commands are rejected; no successful unknown-register ACK. */
    io_start(qts, false);
    qtest_writel(qts, 0x40005410, 3);
    g_assert_cmphex(qtest_readl(qts, 0x40005414) & 0x400, ==, 0x400);
    io_stop(qts);
    qtest_quit(qts);
}

static void timer_profile(void)
{
    QTestState *qts = start();
    const uint32_t tim11 = 0x40014800, tim1 = 0x40010000;
    const uint32_t enr = 0x40023844, cfgr = 0x40023808;
    /* Original IO polling mode: CC1 only, not update IRQ or GPIO INT. */
    qtest_writel(qts, enr, 1 << 18);
    qtest_writel(qts, tim11 + 0x28, 15);
    qtest_writel(qts, tim11 + 0x2c, 31);
    qtest_writel(qts, tim11 + 0x34, 7);
    qtest_writel(qts, tim11 + 0x0c, 2);
    qtest_writel(qts, tim11 + 0x14, 1);
    qtest_writel(qts, tim11 + 0x10, 0);
    qtest_writel(qts, tim11, 1);
    qtest_clock_step(qts, 6999);
    g_assert_cmphex(qtest_readl(qts, tim11 + 0x24), ==, 6);
    g_assert_false(qtest_get_irq(qts, 26));
    qtest_clock_step(qts, 1);
    g_assert_cmphex(qtest_readl(qts, tim11 + 0x10), ==, 2);
    g_assert_true(qtest_get_irq(qts, 26));
    qtest_writel(qts, tim11 + 0x10, ~2U);
    g_assert_false(qtest_get_irq(qts, 26));
    qtest_clock_step(qts, 25000);
    g_assert_cmphex(qtest_readl(qts, tim11 + 0x24), ==, 0);
    g_assert_cmphex(qtest_readl(qts, tim11 + 0x10), ==, 1);
    /* Gate retains counter phase; PSC writes take effect on update. */
    qtest_clock_step(qts, 3000);
    qtest_writel(qts, enr, 0);
    qtest_clock_step(qts, 20000);
    g_assert_cmphex(qtest_readl(qts, tim11 + 0x24), ==, 3);
    qtest_writel(qts, tim11 + 0x28, 31);
    qtest_writel(qts, enr, 1 << 18);
    qtest_clock_step(qts, 4000);
    g_assert_true(qtest_get_irq(qts, 26));
    qtest_clock_step(qts, 25000);
    g_assert_cmphex(qtest_readl(qts, tim11 + 0x24), ==, 0);
    qtest_writel(qts, tim11 + 0x10, 0);
    qtest_clock_step(qts, 13999);
    g_assert_false(qtest_get_irq(qts, 26));
    qtest_clock_step(qts, 1);
    g_assert_true(qtest_get_irq(qts, 26));
    qtest_writel(qts, tim11, 0);
    qtest_writel(qts, tim11 + 0x10, 0);
    /* APB2 /4 supplies doubled timer clock, hence HCLK /2. */
    qtest_writel(qts, cfgr, 5 << 13);
    qtest_clock_step(qts, 100000);
    qtest_writel(qts, tim11 + 0x28, 7);
    qtest_writel(qts, tim11 + 0x14, 1);
    qtest_writel(qts, tim11 + 0x10, 0);
    qtest_writel(qts, tim11, 1);
    qtest_clock_step(qts, 7000);
    g_assert_cmphex(qtest_readl(qts, tim11 + 0x24), ==, 7);
    qtest_writel(qts, tim11, 0);
    qtest_writel(qts, tim11 + 0x10, 0);
    qtest_writel(qts, cfgr, 0);
    qtest_clock_step(qts, 100000);
    /* TIM1 split compare/update IRQs, repetition and one-pulse stop. */
    qtest_writel(qts, enr, (1 << 18) | 1 | (1 << 17));
    qtest_writel(qts, tim1 + 0x28, 15);
    qtest_writel(qts, tim1 + 0x2c, 3);
    qtest_writel(qts, tim1 + 0x30, 1);
    qtest_writel(qts, tim1 + 0x34, 2);
    qtest_writel(qts, tim1 + 0x0c, 3);
    qtest_writel(qts, tim1 + 0x14, 1);
    qtest_writel(qts, tim1 + 0x10, 0);
    qtest_writel(qts, tim1, 9);
    qtest_clock_step(qts, 2000);
    g_assert_true(qtest_get_irq(qts, 27));
    g_assert_false(qtest_get_irq(qts, 25));
    qtest_writel(qts, tim1 + 0x10, 0);
    qtest_clock_step(qts, 2000);
    g_assert_false(qtest_get_irq(qts, 25));
    qtest_clock_step(qts, 4000);
    g_assert_true(qtest_get_irq(qts, 25));
    g_assert_cmphex(qtest_readl(qts, tim1) & 1, ==, 0);
    /* Clearing one shared source cannot clear the other source. */
    qtest_writel(qts, 0x4001440c, 1);
    qtest_writel(qts, 0x40014414, 1);
    qtest_writel(qts, tim1 + 0x10, 0);
    g_assert_true(qtest_get_irq(qts, 25));
    qtest_writel(qts, 0x40014410, 0);
    g_assert_false(qtest_get_irq(qts, 25));
    const uint32_t advanced[] = { tim1, 0x40010400 };
    const unsigned irq[][4] = { { 25, 27, 26, 24 }, { 44, 46, 45, 43 } };
    for (unsigned i = 0; i < 2; i++) {
        qtest_writel(qts, advanced[i] + 0x0c, 0xff);
        qtest_writel(qts, advanced[i] + 0x14, 0xff);
        for (unsigned j = 0; j < 4; j++) {
            g_assert_true(qtest_get_irq(qts, irq[i][j]));
        }
        qtest_writel(qts, advanced[i] + 0x10, 0);
        for (unsigned j = 0; j < 4; j++) {
            g_assert_false(qtest_get_irq(qts, irq[i][j]));
        }
    }
    /* TIM2 retains 32-bit ARR/CNT and external capture sets real CC1IF. */
    const uint32_t tim2 = 0x40000000;
    qtest_writel(qts, 0x40023840, 1);
    qtest_writel(qts, tim2 + 0x28, 15);
    qtest_writel(qts, tim2 + 0x2c, 0x10001);
    qtest_writel(qts, tim2 + 0x14, 1);
    qtest_writel(qts, tim2 + 0x10, 0);
    qtest_writel(qts, tim2, 1);
    qtest_clock_step(qts, 0x10000 * INT64_C(1000));
    g_assert_cmphex(qtest_readl(qts, tim2 + 0x24), ==, 0x10000);
    qtest_writel(qts, tim2 + 0x18, 1);
    qtest_writel(qts, tim2 + 0x20, 1);
    qtest_writel(qts, tim2 + 0x0c, 2);
    qtest_set_irq_in(qts, "/machine/ex/timer0", "capture", 0, 1);
    g_assert_true(qtest_get_irq(qts, 28));
    g_assert_cmphex(qtest_readl(qts, tim2 + 0x34), ==, 0x10000);
    g_assert_false(qtest_get_irq(qts, 28));
    qtest_writel(qts, tim2 + 0x10, 0);
    qtest_clock_step(qts, 2000);
    g_assert_cmphex(qtest_readl(qts, tim2 + 0x24), ==, 0);
    /* Full 32-bit period with maximum PSC exercises overflow-safe deadline. */
    qtest_writel(qts, tim2 + 0x2c, UINT32_MAX);
    qtest_writel(qts, tim2 + 0x28, UINT16_MAX);
    qtest_writel(qts, tim2 + 0x14, 1);
    qtest_clock_step(qts, 4096000);
    g_assert_cmphex(qtest_readl(qts, tim2 + 0x24), ==, 1);
    qtest_writel(qts, 0x40023820, 1);
    g_assert_cmphex(qtest_readl(qts, tim2), ==, 0);
    g_assert_false(qtest_get_irq(qts, 28));
    /* All four compare flags; down and center counting select real edges. */
    const uint32_t tim3 = 0x40000400;
    qtest_writel(qts, 0x40023840, 3);
    qtest_writel(qts, tim3 + 0x28, 15);
    qtest_writel(qts, tim3 + 0x2c, 9);
    for (unsigned i = 0; i < 4; i++) {
        qtest_writel(qts, tim3 + 0x34 + 4 * i, 2 * (i + 1));
    }
    qtest_writel(qts, tim3 + 0x0c, 0x1e);
    qtest_writel(qts, tim3 + 0x14, 1);
    qtest_writel(qts, tim3 + 0x10, 0);
    qtest_writel(qts, tim3, 1);
    for (unsigned i = 0; i < 4; i++) {
        qtest_clock_step(qts, 2000);
        g_assert_cmphex(qtest_readl(qts, tim3 + 0x10), ==,
                        (2U << (i + 1)) - 2);
    }
    qtest_writel(qts, tim3, 0x11);
    qtest_writel(qts, tim3 + 0x14, 1);
    qtest_writel(qts, tim3 + 0x10, 0);
    qtest_clock_step(qts, 1000);
    g_assert_cmphex(qtest_readl(qts, tim3 + 0x24), ==, 8);
    g_assert_cmphex(qtest_readl(qts, tim3 + 0x10), ==, 0x10);
    qtest_writel(qts, tim3, 0);
    qtest_writel(qts, tim3, 0x21);
    qtest_writel(qts, tim3 + 0x14, 1);
    qtest_writel(qts, tim3 + 0x10, 0);
    qtest_clock_step(qts, 9000);
    g_assert_cmphex(qtest_readl(qts, tim3 + 0x24), ==, 9);
    g_assert_false(qtest_get_irq(qts, 29));
    qtest_clock_step(qts, 1000);
    g_assert_cmphex(qtest_readl(qts, tim3 + 0x24), ==, 8);
    g_assert_cmphex(qtest_readl(qts, tim3 + 0x10) & 0x1e, ==, 0x10);
    g_assert_true(qtest_get_irq(qts, 29));
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
    qtest_add_func("/nikon/ex/rtc", rtc);
    qtest_add_func("/nikon/ex/io", io);
    qtest_add_func("/nikon/ex/timer", timer_profile);
    return g_test_run();
}
