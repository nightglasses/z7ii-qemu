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

static bool power_start(QTestState *qts, uint8_t wire, bool read)
{
    qtest_writel(qts, 0x40005400, 1 | (1 << 8) | (1 << 10));
    qtest_writel(qts, 0x40005410, wire | read);
    uint32_t status = qtest_readl(qts, 0x40005414);

    if (status & 0x400) {
        return false;
    }
    g_assert_cmphex(status & 2, ==, 2);
    qtest_readl(qts, 0x40005418);
    return true;
}

static void power_pointer(QTestState *qts, uint8_t wire, uint16_t reg)
{
    g_assert_true(power_start(qts, wire, false));
    if (wire == 0x20) {
        qtest_writel(qts, 0x40005410, reg >> 8);
    }
    qtest_writel(qts, 0x40005410, reg & 0xff);
}

static void power_readback(QTestState *qts, uint8_t wire, uint16_t reg,
                           uint8_t value)
{
    for (unsigned i = 0; i < 3; i++) {
        power_pointer(qts, wire, reg);
        g_assert_true(power_start(qts, wire, true));
        g_assert_cmphex(qtest_readl(qts, 0x40005410), ==, value);
        io_stop(qts);
    }
}

static void power_profile(void)
{
    QTestState *qts = start();

    /* No register value is invented before the first actual guest write. */
    power_pointer(qts, 0x9e, 0x23);
    g_assert_false(power_start(qts, 0x9e, true));
    io_stop(qts);
    power_pointer(qts, 0x9e, 0x23);
    qtest_writel(qts, 0x40005410, 0x88);
    io_stop(qts);
    power_readback(qts, 0x9e, 0x23, 0x88);

    /* The optional second peer uses a big-endian 16-bit command pointer. */
    power_pointer(qts, 0x20, 0x1033);
    g_assert_false(power_start(qts, 0x20, true));
    io_stop(qts);
    power_pointer(qts, 0x20, 0x1033);
    qtest_writel(qts, 0x40005410, 0x78);
    io_stop(qts);
    power_readback(qts, 0x20, 0x1033, 0x78);

    power_pointer(qts, 0x20, 0x3310);
    g_assert_cmphex(qtest_readl(qts, 0x40005414) & 0x400, ==, 0x400);
    g_assert_false(power_start(qts, 0x20, true));
    io_stop(qts);
    power_pointer(qts, 0x9e, 0x24);
    g_assert_cmphex(qtest_readl(qts, 0x40005414) & 0x400, ==, 0x400);
    io_stop(qts);
    power_pointer(qts, 0x9e, 0x23);
    qtest_writel(qts, 0x40005410, 0x42);
    qtest_writel(qts, 0x40005410, 0x11);
    g_assert_cmphex(qtest_readl(qts, 0x40005414) & 0x400, ==, 0x400);
    io_stop(qts);
    power_readback(qts, 0x9e, 0x23, 0x42);

    /* MCU/controller reset preserves the independently powered peer state. */
    qtest_writel(qts, 0x40023820, 1 << 21);
    qtest_writel(qts, 0x40023820, 0);
    power_readback(qts, 0x9e, 0x23, 0x42);
    qtest_writel(qts, 0xe000ed0c, 0x05fa0004);
    qtest_clock_step(qts, 1);
    power_readback(qts, 0x9e, 0x23, 0x42);
    power_readback(qts, 0x20, 0x1033, 0x78);
    qtest_system_reset(qts);
    power_pointer(qts, 0x9e, 0x23);
    g_assert_false(power_start(qts, 0x9e, true));
    io_stop(qts);
    power_pointer(qts, 0x20, 0x1033);
    g_assert_false(power_start(qts, 0x20, true));
    io_stop(qts);
    qtest_quit(qts);
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
    /* Original late mode-8 profile: C8 pull-up, falling EXTI8, NVIC23. */
    g_assert_cmphex(qtest_readl(qts, 0x40020810) & 0x100, ==, 0x100);
    qtest_writel(qts, 0x4002080c, 0x10000);
    qtest_writel(qts, 0x40013810, 2);
    qtest_writel(qts, 0x40013c00, 0x100);
    qtest_writel(qts, 0x40013c0c, 0x100);
    g_assert_false(qtest_get_irq(qts, 23));
    qtest_set_irq_in(qts, "/machine/ex/io", "pad", 9, 0);
    qtest_set_irq_in(qts, "/machine/ex/io", "pad", 9, 1);
    io_read(qts, 0x6d, value, 1);
    g_assert_cmphex(value[0], ==, 0xff);
    io_read(qts, 0x59, value, 1);
    g_assert_cmphex(value[0], ==, 2);
    g_assert_true(qtest_get_irq(qts, 23));
    io_read(qts, 1, value, 1);
    g_assert_cmphex(value[0], ==, 0xfd);
    io_read(qts, 0x59, value, 1);
    g_assert_cmphex(value[0], ==, 0);
    qtest_writel(qts, 0x40013c14, 0x100);
    g_assert_false(qtest_get_irq(qts, 23));
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
    /* Actual generic initializers read/write CR2 and clear SMCR selector,
     * even on TIM11. The software profile keeps state for both registers. */
    qtest_writel(qts, tim11 + 4, 8);
    g_assert_cmphex(qtest_readl(qts, tim11 + 4), ==, 8);
    qtest_writel(qts, tim11 + 4, 0);
    qtest_writel(qts, tim11 + 8, 0x4004);
    g_assert_cmphex(qtest_readl(qts, tim11 + 8), ==, 0x4004);
    qtest_writel(qts, tim11 + 8,
                 qtest_readl(qts, tim11 + 8) & ~0xff77U);
    g_assert_cmphex(qtest_readl(qts, tim11 + 8), ==, 0);
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

static void input_reset(void)
{
    QTestState *qts = start();
    const uint32_t gpio = 0x40020800, exti = 0x40013c00;
    const uint32_t bit = 1 << 5;
    qtest_set_irq_in(qts, "/machine/ex", "input", 2 * 16 + 5, 1);
    g_assert_cmphex(qtest_readl(qts, gpio + 0x10) & bit, ==, bit);
    qtest_writel(qts, 0x4001380c, 2 << 4);
    qtest_writel(qts, exti, bit);
    qtest_writel(qts, exti + 8, bit);
    qtest_writel(qts, exti + 12, bit);
    qtest_writel(qts, exti + 20, bit);
    /* Routing a driven pad establishes the baseline without an input edge. */
    g_assert_cmphex(qtest_readl(qts, exti + 20), ==, 0);
    /* A held external input does not acquire a fictitious reset edge. */
    qtest_writel(qts, 0x40023810, 1 << 2);
    g_assert_cmphex(qtest_readl(qts, gpio + 0x10) & bit, ==, bit);
    g_assert_cmphex(qtest_readl(qts, exti + 20), ==, 0);
    g_assert_false(qtest_get_irq(qts, 23));
    qtest_writel(qts, 0x40023810, 0);
    qtest_set_irq_in(qts, "/machine/ex", "input", 2 * 16 + 5, 0);
    g_assert_true(qtest_get_irq(qts, 23));
    qtest_writel(qts, exti + 20, bit);
    qtest_writel(qts, 0x40023810, 1 << 2);
    g_assert_cmphex(qtest_readl(qts, gpio + 0x10) & bit, ==, 0);
    g_assert_false(qtest_get_irq(qts, 23));
    qtest_writel(qts, 0x40023810, 0);
    qtest_set_irq_in(qts, "/machine/ex", "input", 2 * 16 + 5, 1);
    qtest_system_reset(qts);
    g_assert_cmphex(qtest_readl(qts, gpio + 0x10) & bit, ==, bit);
    g_assert_cmphex(qtest_readl(qts, exti + 20), ==, 0);
    /* AIRCR resets only EX; the independently powered driver's level stays. */
    qtest_writel(qts, 0xe000ed0c, 0x05fa0004);
    qtest_clock_step(qts, 1);
    g_assert_cmphex(qtest_readl(qts, gpio + 0x10) & bit, ==, bit);
    /* Release disconnects that source and lets the real pull determine IDR. */
    qtest_set_irq_in(qts, "/machine/ex", "input", 2 * 16 + 5, -1);
    qtest_writel(qts, gpio + 12, 2 << 10);
    g_assert_cmphex(qtest_readl(qts, gpio + 0x10) & bit, ==, 0);
    qtest_writel(qts, gpio + 12, 1 << 10);
    g_assert_cmphex(qtest_readl(qts, gpio + 0x10) & bit, ==, bit);
    qtest_quit(qts);
}

static void spi_dma_arm(QTestState *qts, uint32_t stream, uint32_t peripheral,
                        uint32_t memory, uint32_t count, uint32_t cr)
{
    qtest_writel(qts, stream, 0);
    qtest_writel(qts, stream + 4, count);
    qtest_writel(qts, stream + 8, peripheral);
    qtest_writel(qts, stream + 12, memory);
    qtest_writel(qts, stream, cr);
}

static uint16_t spi_master_clock(QTestState *qts, uint32_t master, uint16_t word)
{
    qtest_writel(qts, master + 12, word);
    g_assert_cmphex(qtest_readl(qts, master + 8) & 1, ==, 1);
    return qtest_readl(qts, master + 12);
}

static void spi_word_profile(void)
{
    /* Ordinary QOM SSI endpoints on two real EX master buses. These masters
     * supply explicit 16-bit, LSB-first clocks to the original 8-bit slaves.
     */
    QTestState *qts = qtest_init(
        "-machine nikonex -display none "
        "-device nikon-ex-spi-slave,processor=/machine/ex,channel=0,bus=spi2,"
        "master-word-bits=16,master-lsb-first=on "
        "-device nikon-ex-spi-slave,processor=/machine/ex,channel=3,bus=spi3,"
        "master-word-bits=16,master-lsb-first=on");
    const uint32_t rx = 0x40013000, tx = 0x40013400;
    const uint32_t rx_master = 0x40003800, tx_master = 0x40003c00;
    const uint32_t dma_base = 0x40026400;
    const uint32_t rx_stream = dma_base + 0x10, tx_stream = dma_base + 0x28;
    const uint32_t rx_memory = 0x20001000, tx_memory = 0x20002000;
    qtest_irq_intercept_in(qts, "/machine/ex/armv7m");
    for (unsigned i = 0; i < 256; i++) {
        qtest_writeb(qts, rx_memory + i, 0xa5);
        qtest_writeb(qts, tx_memory + i, (i * 29 + 7) & 0xff);
    }
    qtest_writeb(qts, rx_memory + 256, 0x5a);
    qtest_writel(qts, rx, 0x2c9);
    qtest_writel(qts, rx + 4, 0x21);
    qtest_writel(qts, tx, 0x2c9);
    qtest_writel(qts, tx + 4, 0x22);
    qtest_writel(qts, rx_master, 0xacd);
    qtest_writel(qts, tx_master, 0xacd);
    spi_dma_arm(qts, rx_stream, rx + 12, rx_memory, 256, 0x0602041f);
    spi_dma_arm(qts, tx_stream, tx + 12, tx_memory, 256, 0x0800045f);
    for (unsigned i = 0; i < 128; i++) {
        unsigned low = (2 * i * 17 + 3) & 0xff;
        unsigned high = ((2 * i + 1) * 17 + 3) & 0xff;
        uint16_t expected = ((2 * i * 29 + 7) & 0xff) |
                            ((((2 * i + 1) * 29 + 7) & 0xff) << 8);
        g_assert_cmphex(spi_master_clock(qts, rx_master, low | (high << 8)),
                        ==, 0xffff);
        g_assert_cmphex(spi_master_clock(qts, tx_master, 0xffff), ==, expected);
        g_assert_cmpuint(qtest_readl(qts, rx_stream + 4), ==, 254 - 2 * i);
        g_assert_cmpuint(qtest_readl(qts, tx_stream + 4), ==, 254 - 2 * i);
        g_assert_cmphex(qtest_readb(qts, rx_memory + 2 * i), ==, low);
        g_assert_cmphex(qtest_readb(qts, rx_memory + 2 * i + 1), ==, high);
        if (i == 63) {
            g_assert_cmphex(qtest_readl(qts, dma_base) & 0x410, ==, 0x410);
            g_assert_true(qtest_get_irq(qts, 56));
            g_assert_true(qtest_get_irq(qts, 57));
        }
    }
    g_assert_cmphex(qtest_readl(qts, dma_base) & 0xc30, ==, 0xc30);
    g_assert_cmphex(qtest_readl(qts, rx_stream) & 1, ==, 0);
    g_assert_cmphex(qtest_readl(qts, tx_stream) & 1, ==, 0);
    g_assert_cmphex(qtest_readb(qts, rx_memory + 256), ==, 0x5a);
    g_assert_cmphex(spi_master_clock(qts, tx_master, 0xffff), ==, 0xffff);
    g_assert_cmpuint(qtest_readl(qts, tx_stream + 4), ==, 0);
    qtest_writel(qts, dma_base + 8, 0xf7d);
    g_assert_false(qtest_get_irq(qts, 56));
    g_assert_false(qtest_get_irq(qts, 57));

    /* No frame buffering: cancel DMA between master words, then disable SPE.
     * Real receive clocks still set RXNE, but cannot consume cancelled memory.
     */
    spi_dma_arm(qts, rx_stream, rx + 12, rx_memory, 4, 0x0602041f);
    spi_master_clock(qts, rx_master, 0x963c);
    g_assert_cmpuint(qtest_readl(qts, rx_stream + 4), ==, 2);
    qtest_writel(qts, rx_stream, qtest_readl(qts, rx_stream) & ~1u);
    spi_master_clock(qts, rx_master, 0x1122);
    g_assert_cmpuint(qtest_readl(qts, rx_stream + 4), ==, 2);
    qtest_writel(qts, rx, 0x289);
    g_assert_cmphex(spi_master_clock(qts, rx_master, 0x3344), ==, 0xffff);
    g_assert_cmpuint(qtest_readl(qts, rx_stream + 4), ==, 2);
    qtest_system_reset(qts);
    g_assert_cmpuint(qtest_readl(qts, rx_stream + 4), ==, 0);
    g_assert_cmpuint(qtest_readl(qts, tx_stream + 4), ==, 0);
    g_assert_false(qtest_get_irq(qts, 56));
    g_assert_false(qtest_get_irq(qts, 57));

    /* Opposite slave bit order receives actual reversed wire bits per byte. */
    qtest_writel(qts, rx_master, 0xacd);
    qtest_writel(qts, rx, 0x249);
    qtest_writel(qts, rx + 4, 1);
    spi_dma_arm(qts, rx_stream, rx + 12, rx_memory, 2, 0x0602041f);
    spi_master_clock(qts, rx_master, 0x963c);
    g_assert_cmphex(qtest_readb(qts, rx_memory), ==, 0x3c);
    g_assert_cmphex(qtest_readb(qts, rx_memory + 1), ==, 0x69);
    g_assert_cmpuint(qtest_readl(qts, rx_stream + 4), ==, 0);
    /* DFF=16 consumes one EX word, not two DMA halfwords. */
    qtest_writel(qts, rx, 0xac9);
    spi_dma_arm(qts, rx_stream, rx + 12, rx_memory, 1,
                0x0602041f | (1 << 11) | (1 << 13));
    spi_master_clock(qts, rx_master, 0x963c);
    g_assert_cmphex(qtest_readw(qts, rx_memory), ==, 0x963c);
    g_assert_cmpuint(qtest_readl(qts, rx_stream + 4), ==, 0);
    qtest_quit(qts);
}

static void adc_request(QTestState *qts, uint32_t base, unsigned channel,
                        uint32_t cr1, uint32_t cr2)
{
    qtest_writel(qts, base + 0x34, channel);
    qtest_writel(qts, base + 4, cr1);
    qtest_writel(qts, base + 8, cr2 | (1U << 30) | 1);
    qtest_clock_step(qts, 1);
}

static void adc_profile(void)
{
    QTestState *qts = start();
    const uint32_t adc = 0x40012000;
    const uint32_t eocie = 1U << 5, ovrie = 1U << 26;

    /* Original poll family: a requested channel without a source waits. */
    adc_request(qts, adc, 1, 0, 0);
    g_assert_cmphex(qtest_readl(qts, adc), ==, 0x10);
    g_assert_false(qtest_get_irq(qts, 18));
    qtest_set_irq_in(qts, "/machine/ex", "adc", 0, 0x135);
    qtest_clock_step(qts, 1);
    g_assert_cmphex(qtest_readl(qts, adc), ==, 0x10);
    qtest_set_irq_in(qts, "/machine/ex", "adc", 1, 0xabc);
    qtest_clock_step(qts, 1);
    g_assert_cmphex(qtest_readl(qts, adc) & 0x12, ==, 0x12);
    g_assert_cmphex(qtest_readl(qts, adc + 8) & (1U << 30), ==, 0);
    g_assert_false(qtest_get_irq(qts, 18));
    g_assert_cmphex(qtest_readl(qts, adc + 0x4c), ==, 0xabc);
    g_assert_cmphex(qtest_readl(qts, adc) & 2, ==, 0);

    /* Source IRQ family: EOCIE/OVRIE, DR consumption and SR W0C. */
    adc_request(qts, adc, 1, eocie | ovrie, 0);
    g_assert_true(qtest_get_irq(qts, 18));
    adc_request(qts, adc, 1, eocie | ovrie, 0);
    g_assert_cmphex(qtest_readl(qts, adc) & 0x22, ==, 0x22);
    g_assert_cmphex(qtest_readl(qts, adc + 0x4c), ==, 0xabc);
    g_assert_true(qtest_get_irq(qts, 18));
    qtest_writel(qts, adc, ~0x20U);
    g_assert_false(qtest_get_irq(qts, 18));

    /* Disconnecting an external source does not reuse its previous count. */
    qtest_set_irq_in(qts, "/machine/ex", "adc", 1, -1);
    adc_request(qts, adc, 1, eocie, 0);
    g_assert_cmphex(qtest_readl(qts, adc) & 2, ==, 0);
    g_assert_false(qtest_get_irq(qts, 18));
    qtest_writel(qts, adc + 8, 0);
    qtest_set_irq_in(qts, "/machine/ex", "adc", 1, 0x987);
    qtest_clock_step(qts, 1);
    g_assert_cmphex(qtest_readl(qts, adc) & 2, ==, 0);
    g_assert_false(qtest_get_irq(qts, 18));

    /* All ADCs share IRQ18 while retaining independent conversion state. */
    for (unsigned i = 0; i < 3; i++) {
        qtest_set_irq_in(qts, "/machine/ex", "adc", i * 19 + 3,
                        0x321 + i * 0x123);
        adc_request(qts, adc + i * 0x100, 3, eocie, 0);
    }
    for (unsigned i = 0; i < 3; i++) {
        g_assert_true(qtest_get_irq(qts, 18));
        g_assert_cmphex(qtest_readl(qts, adc + i * 0x100 + 0x4c), ==,
                        0x321 + i * 0x123);
    }
    g_assert_false(qtest_get_irq(qts, 18));
    for (unsigned resolution = 0; resolution < 4; resolution++) {
        for (unsigned left = 0; left < 2; left++) {
            uint32_t expected = 0x987 >> (resolution * 2);
            if (left) {
                expected <<= 4 + resolution * 2;
            }
            adc_request(qts, adc, 1, resolution << 24, left << 11);
            g_assert_cmphex(qtest_readl(qts, adc + 0x4c), ==, expected);
        }
    }

    /* Reset cancels a pending conversion but preserves the external source. */
    qtest_set_irq_in(qts, "/machine/ex", "adc", 2, -1);
    adc_request(qts, adc, 2, eocie, 0);
    qtest_writel(qts, 0xe000ed0c, 0x05fa0004);
    qtest_clock_step(qts, 1);
    g_assert_cmphex(qtest_readl(qts, adc), ==, 0);
    g_assert_cmphex(qtest_readl(qts, adc + 8), ==, 0);
    g_assert_cmphex(qtest_readl(qts, adc + 0x24), ==, 0xfff);
    g_assert_false(qtest_get_irq(qts, 18));
    qtest_set_irq_in(qts, "/machine/ex", "adc", 2, 0x246);
    qtest_clock_step(qts, 1);
    g_assert_cmphex(qtest_readl(qts, adc), ==, 0);
    adc_request(qts, adc, 1, eocie, 0);
    g_assert_cmphex(qtest_readl(qts, adc + 0x4c), ==, 0x987);
    qtest_system_reset(qts);
    for (unsigned i = 0; i < 3; i++) {
        g_assert_cmphex(qtest_readl(qts, adc + i * 0x100), ==, 0);
        adc_request(qts, adc + i * 0x100, 3, eocie, 0);
        g_assert_cmphex(qtest_readl(qts, adc + i * 0x100 + 0x4c), ==,
                        0x321 + i * 0x123);
    }
    qtest_set_irq_in(qts, "/machine/ex", "adc", 1, -1);
    qtest_system_reset(qts);
    adc_request(qts, adc, 1, eocie, 0);
    g_assert_cmphex(qtest_readl(qts, adc) & 2, ==, 0);
    g_assert_false(qtest_get_irq(qts, 18));
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
    qtest_add_func("/nikon/ex/input", input_reset);
    qtest_add_func("/nikon/ex/power", power_profile);
    qtest_add_func("/nikon/ex/word", spi_word_profile);
    qtest_add_func("/nikon/ex/adc", adc_profile);
    return g_test_run();
}
