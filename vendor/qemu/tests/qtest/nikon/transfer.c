/* Nikon byte-stream graphics packet: real RAM writes and IRQ130 ACK.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "../libqtest.h"
#include "fixture.h"

#define TRANSFER 0x20180000
#define RAM 0xb1800000

static const char *image;

void nikon_transfer_add_tests(const char *bios);

static QTestState *start(void)
{
    g_autofree char *quoted = g_shell_quote(image);
    g_autofree char *args = g_strdup_printf(
        "-M z7ii-mas-diagnostic -smp 2 -bios %s -serial none -S", quoted);

    QTestState *q = qtest_init(args);

    nikon_intercept_gic(q);
    return q;
}

static void packet(QTestState *q, uint32_t source, uint32_t destination,
                   uint32_t count)
{
    qtest_writel(q, TRANSFER + 0xc00, source);
    qtest_writel(q, TRANSFER + 0xc04, destination);
    qtest_writel(q, TRANSFER + 0xc34, 0);
    qtest_writel(q, TRANSFER + 0xc38, 0);
    qtest_writel(q, TRANSFER + 0xc10, 8);
    qtest_writel(q, TRANSFER + 0xc20, count);
    qtest_writel(q, TRANSFER + 0xc24, 0);
    qtest_writel(q, TRANSFER + 0xc28, 0);
}

static void test_copy(void)
{
    QTestState *q = start();
    uint8_t source[8192], destination[sizeof(source)];
    unsigned i;

    for (i = 0; i < sizeof(source); i++) {
        source[i] = i ^ (i >> 8);
    }
    qtest_memwrite(q, RAM, source, sizeof(source));
    qtest_memset(q, RAM + 0x4000, 0, sizeof(source));
    packet(q, RAM, RAM + 0x4000, sizeof(source));
    qtest_writel(q, TRANSFER + 0xc18, 1);
    qtest_memread(q, RAM + 0x4000, destination, sizeof(destination));
    g_assert_cmpmem(destination, sizeof(destination),
                    (uint8_t[8192]) { 0 }, sizeof(destination));
    qtest_clock_step(q, 1000000);
    qtest_memread(q, RAM + 0x4000, destination, sizeof(destination));
    g_assert_cmpmem(destination, sizeof(destination), source, sizeof(source));
    g_assert_cmphex(qtest_readl(q, TRANSFER + 0xc00), ==, RAM);
    g_assert_cmphex(qtest_readl(q, TRANSFER + 0xc04), ==, RAM + 0x4000);
    g_assert_true(qtest_get_irq(q, 130 - 32));
    qtest_writel(q, TRANSFER + 0xc18, 0);
    g_assert_cmphex(qtest_readl(q, TRANSFER + 0xc18), ==, 0);
    g_assert_false(qtest_get_irq(q, 130 - 32));
    qtest_quit(q);
}

static void test_cancel(void)
{
    QTestState *q = start();
    uint8_t destination[64];

    qtest_memset(q, RAM, 0x5a, sizeof(destination));
    qtest_memset(q, RAM + 0x4000, 0, sizeof(destination));
    packet(q, RAM, RAM + 0x4000, sizeof(destination));
    qtest_writel(q, TRANSFER + 0xc18, 1);
    qtest_writel(q, TRANSFER + 0xc18, 0);
    qtest_clock_step(q, 1000000);
    qtest_memread(q, RAM + 0x4000, destination, sizeof(destination));
    g_assert_cmpmem(destination, sizeof(destination),
                    (uint8_t[64]) { 0 }, sizeof(destination));
    g_assert_false(qtest_get_irq(q, 130 - 32));
    qtest_quit(q);
}

static void test_geometry(void)
{
    QTestState *q = start();
    uint8_t destination[1024];
    unsigned i;

    qtest_memset(q, RAM, 0xa5, sizeof(destination));
    qtest_memset(q, RAM + 0x4000, 0, sizeof(destination));
    packet(q, RAM, RAM + 0x4000, 0);
    qtest_writel(q, TRANSFER + 0xc10, 0x2200);
    qtest_writel(q, TRANSFER + 0xc08, (8U << 16) | 64);
    qtest_writel(q, TRANSFER + 0xc0c, (8U << 16) | 64);
    qtest_writel(q, TRANSFER + 0xc18, 1);
    qtest_clock_step(q, 1000000);
    qtest_memread(q, RAM + 0x4000, destination, sizeof(destination));
    for (i = 0; i < sizeof(destination); i++) {
        g_assert_cmphex(destination[i], ==, 0xa5);
    }
    g_assert_true(qtest_get_irq(q, 130 - 32));
    qtest_quit(q);
}

void nikon_transfer_add_tests(const char *bios)
{
    image = bios;
    qtest_add_func("/nikon/transfer/copy", test_copy);
    qtest_add_func("/nikon/transfer/cancel", test_cancel);
    qtest_add_func("/nikon/transfer/geometry", test_geometry);
}
