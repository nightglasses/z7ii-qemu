/* Nikon requested sample publication and source ISR acknowledgement.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "fixture.h"

#define SAMPLER 0x20201000
#define TABLE 0x20204000

static const char *image;

void nikon_sampling_add_tests(const char *bios);

static QTestState *start(bool observe_gic)
{
    g_autofree char *quoted = g_shell_quote(image);
    g_autofree char *args = g_strdup_printf(
        "-M z7ii-mas-diagnostic -smp 2 -bios %s -serial none", quoted);
    QTestState *q = qtest_init(args);

    if (observe_gic) {
        nikon_intercept_gic(q);
    }
    return q;
}

static void publish(QTestState *q, const char *path, const uint32_t *words,
                    unsigned count)
{
    unsigned i;

    for (i = 0; i < count * 32; i++) {
        qtest_set_irq_in(q, path, "data-bit", i,
                         !!(words[i / 32] & (1U << (i % 32))));
    }
}

static void event(QTestState *q, const char *path, unsigned index)
{
    qtest_set_irq_in(q, path, "external-event", index, 0);
    qtest_set_irq_in(q, path, "external-event", index, 1);
}

static void test_sampler(void)
{
    QTestState *q = start(true);
    g_autofree char *path = nikon_device_path(q,
                                            "nikon-data-sampler-functional");
    const uint32_t first = 0x87654321, second = 0xabcdef00;

    /* An unsolicited publication is not a requested transaction. */
    publish(q, path, &first, 1);
    event(q, path, 0);
    g_assert_cmphex(qtest_readl(q, SAMPLER + 0x2c), ==, 0);
    g_assert_false(qtest_get_irq(q, 116 - 32));
    qtest_writel(q, SAMPLER + 0x44, 3);
    qtest_writel(q, SAMPLER + 0x48, 7);
    qtest_writel(q, SAMPLER + 0x14, 1);
    event(q, path, 0);
    g_assert_false(qtest_get_irq(q, 116 - 32));
    publish(q, path, &first, 1);
    event(q, path, 0);
    g_assert_true(qtest_get_irq(q, 116 - 32));
    g_assert_cmphex(qtest_readl(q, SAMPLER + 0x2c), ==, 1);
    /* Original10294364 acknowledges before fetching its retained word. */
    qtest_writel(q, SAMPLER + 0x2c, 1);
    g_assert_false(qtest_get_irq(q, 116 - 32));
    g_assert_cmphex(qtest_readl(q, SAMPLER + 0x1a0), ==, first);
    qtest_writel(q, SAMPLER, 1);
    qtest_writel(q, SAMPLER, 0);
    g_assert_cmphex(qtest_readl(q, SAMPLER + 0x1a0), ==, first);
    qtest_writel(q, SAMPLER + 0x14, 1);
    event(q, path, 0);
    g_assert_false(qtest_get_irq(q, 116 - 32));
    publish(q, path, &second, 1);
    event(q, path, 0);
    g_assert_cmphex(qtest_readl(q, SAMPLER + 0x1a0), ==, second);
    qtest_quit(q);
}

static void test_table(void)
{
    QTestState *q = start(true);
    g_autofree char *path = nikon_device_path(q,
                                           "nikon-table-sampler-functional");
    const uint32_t words[] = { 0x01234567, 0x89abcdef, 0xfedcba98 };
    unsigned i;

    qtest_writel(q, TABLE + 0x44, 0x10000);
    qtest_writel(q, TABLE + 0x14, 1);
    publish(q, path, words, 2);
    event(q, path, 0);
    g_assert_cmphex(qtest_readl(q, TABLE + 0x2c), ==, 0);
    g_assert_false(qtest_get_irq(q, 121 - 32));
    publish(q, path, words, 3);
    event(q, path, 0);
    g_assert_cmphex(qtest_readl(q, TABLE + 0x2c), ==, 0x100);
    g_assert_true(qtest_get_irq(q, 121 - 32));
    /* Original103b0020 ACKs, reads all words, then updates retained controls. */
    qtest_writel(q, TABLE + 0x2c, 0x100);
    for (i = 0; i < 3; i++) {
        g_assert_cmphex(qtest_readl(q, TABLE + 0x194 + i * 4), ==, words[i]);
    }
    qtest_writel(q, TABLE, qtest_readl(q, TABLE) | 1);
    qtest_writel(q, TABLE + 4, qtest_readl(q, TABLE + 4) | 1);
    qtest_writel(q, TABLE + 0x44,
                 qtest_readl(q, TABLE + 0x44) & ~0x70000);
    g_assert_false(qtest_get_irq(q, 121 - 32));
    for (i = 0; i < 3; i++) {
        g_assert_cmphex(qtest_readl(q, TABLE + 0x194 + i * 4), ==, words[i]);
    }
    qtest_writel(q, TABLE + 0x44, 0x10000);
    qtest_writel(q, TABLE + 0x14, 1);
    event(q, path, 0);
    g_assert_false(qtest_get_irq(q, 121 - 32));
    qtest_quit(q);
}

static void test_request(const void *opaque)
{
    bool table = GPOINTER_TO_UINT(opaque);
    QTestState *q = start(false);
    const char *type = table ? "nikon-table-sampler-functional" :
                              "nikon-data-sampler-functional";
    g_autofree char *path = nikon_device_path(q, type);
    uint64_t base = table ? TABLE : SAMPLER;
    const uint32_t words[] = { 0x12345678, 0x23456789, 0x34567890 };

    qtest_irq_intercept_out_named(q, path, "sample-request");
    g_assert_false(qtest_get_irq(q, 0));
    qtest_writel(q, base + 0x44, table ? 0x10000 : 3);
    if (!table) {
        qtest_writel(q, base + 0x48, 7);
    }
    qtest_writel(q, base + 0x14, 1);
    g_assert_true(qtest_get_irq(q, 0));
    qtest_clock_step(q, 1000000);
    g_assert_true(qtest_get_irq(q, 0));
    g_assert_cmphex(qtest_readl(q, base + 0x2c), ==, 0);
    publish(q, path, words, table ? 3 : 1);
    event(q, path, 0);
    g_assert_false(qtest_get_irq(q, 0));
    g_assert_cmphex(qtest_readl(q, base + 0x2c), ==, table ? 0x100 : 1);
    qtest_quit(q);
}

void nikon_sampling_add_tests(const char *bios)
{
    image = bios;
    qtest_add_func("/nikon/sampling/word", test_sampler);
    qtest_add_func("/nikon/sampling/table", test_table);
    qtest_add_data_func("/nikon/sampling/wordrequest", GUINT_TO_POINTER(0),
                        test_request);
    qtest_add_data_func("/nikon/sampling/tablerequest", GUINT_TO_POINTER(1),
                        test_request);
}
