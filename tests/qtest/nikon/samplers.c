/* Whole SLA sample/configuration families; each helper requires a fresh VM.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "libqtest.h"

void nikon_pairs_test(QTestState *q, const char *path, uint64_t first,
                      uint64_t second);
void nikon_sampler_test(QTestState *q, const char *path, uint64_t base);
void nikon_table_test(QTestState *q, const char *path, uint64_t base);
void nikon_shared_test(QTestState *q, const char *path, uint64_t base);
void nikon_parameters_test(QTestState *q, const char *path, uint64_t base);

static void pulse(QTestState *q, const char *path, const char *name, unsigned n)
{
    qtest_set_irq_in(q, path, name, n, 0);
    qtest_set_irq_in(q, path, name, n, 1);
}

static void retained(QTestState *q, uint64_t base, unsigned offset,
                     uint32_t value)
{
    qtest_writel(q, base + offset, value);
    g_assert_cmphex(qtest_readl(q, base + offset), ==, value);
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

static void pairs_requests(QTestState *q, uint64_t first, uint64_t second)
{
    const uint64_t base[] = { first, second };
    static const unsigned commands[] = { 8, 0x10 };
    static const unsigned stops[] = { 0x54, 0x60, 0x6c, 0x78 };
    unsigned bank, i;

    for (bank = 0; bank < ARRAY_SIZE(base); bank++) {
        g_autofree char *reply = qtest_hmp(q, "xp /1wx 0x%" PRIx64,
                                          base[bank] + 0x18);

        g_assert_null(strstr(reply, "Cannot access memory"));
        g_assert_cmphex(qtest_readl(q, base[bank] + 0x18), ==, 0);
        /* An unarmed start cannot acquire a request. */
        qtest_writel(q, base[bank] + 8, 1);
        for (i = 0; i < ARRAY_SIZE(commands); i++) {
            qtest_writel(q, base[bank] + commands[i], 0);
            qtest_writel(q, base[bank] + commands[i], 2);
            qtest_writeb(q, base[bank] + commands[i], 1);
            qtest_writew(q, base[bank] + commands[i], 1);
            qtest_writel(q, base[bank] + commands[i] + 1, 1);
        }
        qtest_writel(q, base[bank] + 8, 1);
        g_assert_cmphex(qtest_readl(q, base[bank] + 0x18), ==, 0);
    }
    qtest_writel(q, first + 0x10, 1);
    qtest_writel(q, first + 0x10, 1); /* Duplicate arm preserves the request. */
    qtest_writel(q, second + 8, 1);
    g_assert_cmphex(qtest_readl(q, first + 0x18), ==, 0);
    g_assert_cmphex(qtest_readl(q, second + 0x18), ==, 0);
    qtest_writel(q, first + 8, 1);
    g_assert_cmphex(qtest_readl(q, first + 0x18), ==, 1);
    g_assert_cmphex(qtest_readl(q, second + 0x18), ==, 0);
    qtest_writel(q, second + 0x10, 1);
    qtest_writel(q, second + 8, 1);
    for (bank = 0; bank < ARRAY_SIZE(base); bank++) {
        qtest_writel(q, base[bank] + 8, 1);
        qtest_writel(q, base[bank] + 0x10, 1);
        qtest_writel(q, base[bank] + 0x18, 0);
        retained(q, base[bank], 0x20, 0x12345678 + bank);
        g_assert_cmphex(qtest_readl(q, base[bank] + 0x18), ==, 1);
        retained(q, base[bank], 0xc, 1);
        retained(q, base[bank], 0xc, 1);
        qtest_writel(q, base[bank] + 0x14, 1);
        qtest_writel(q, base[bank] + 0x54, 1); /* Unarmed stop rejects. */
    }
    qtest_clock_step(q, 1000000000);
    for (bank = 0; bank < ARRAY_SIZE(base); bank++) {
        g_autofree char *reply = NULL;

        g_assert_cmphex(qtest_readl(q, base[bank] + 0x18), ==, 1);
        g_assert_cmphex(qtest_readl(q, base[bank] + 0x24), ==, 0);
        g_assert_false(qtest_get_irq(q, bank));
        g_assert_cmphex(qtest_readl(q, base[bank] + 0x118), ==, 0);
        reply = qtest_hmp(q, "xp /1wx 0x%" PRIx64, base[bank] + 0xb4);
        g_assert_nonnull(strstr(reply, "Cannot access memory"));
        qtest_writel(q, base[bank] + 0x84, 0);
        qtest_writel(q, base[bank] + 0xf0, 0xfc400);
        qtest_writel(q, base[bank] + 0x60, 1); /* Wrong stage rejects. */
        for (i = 0; i < ARRAY_SIZE(stops); i++) {
            g_assert_cmphex(qtest_readl(q, base[bank] + 0x24), ==, 0);
            qtest_writel(q, base[bank] + stops[i], 1);
        }
        g_assert_cmphex(qtest_readl(q, base[bank] + 0x18), ==, 0);
        g_assert_cmphex(qtest_readl(q, base[bank] + 0x24), ==, 0x014a0080);
        g_assert_true(qtest_get_irq(q, bank));
        qtest_writel(q, base[bank] + 0x24, 0x014a0080);
        g_assert_false(qtest_get_irq(q, bank));
    }
    qtest_system_reset(q);
    for (bank = 0; bank < ARRAY_SIZE(base); bank++) {
        g_assert_cmphex(qtest_readl(q, base[bank] + 0x18), ==, 0);
        g_assert_cmphex(qtest_readl(q, base[bank] + 0x24), ==, 0);
        g_assert_false(qtest_get_irq(q, bank));
        qtest_writel(q, base[bank] + 8, 1);
        g_assert_cmphex(qtest_readl(q, base[bank] + 0x18), ==, 0);
    }
    qtest_writel(q, first + 0x10, 1);
    qtest_system_reset(q); /* An armed request is cancelled too. */
    qtest_writel(q, first + 8, 1);
    g_assert_cmphex(qtest_readl(q, first + 0x18), ==, 0);
}

void nikon_pairs_test(QTestState *q, const char *path, uint64_t first,
                      uint64_t second)
{
    /* Actual scalar/table method roots, including the final 99238 table. */
    static const unsigned fields[] = {
        0x20, 0x40, 0x44, 0x48, 0x84, 0xd4, 0x120, 0x134, 0x144,
        0x200, 0x234, 0x258, 0x304, 0x34c, 0x364, 0x3b4, 0x3c4,
        0x408, 0x42c, 0x470, 0x488, 0x4c8, 0x500, 0x630, 0x634,
        0x678, 0x700, 0x778, 0x900, 0x9d8, 0xb00, 0xbbc, 0xd00,
        0xe18, 0xf00, 0xf2c, 0x1100, 0x113c, 0x1200, 0x1244,
        0x1300, 0x1348, 0x1350, 0x1398, 0x1d38, 0x1e34,
        0x1f00, 0x1f88, 0x1f90, 0x2050, 0x20c8, 0x20dc,
        0x2100, 0x22fc, 0x2400, 0x2c94, 0x3900, 0x42b8,
        0x42c0, 0x49e8, 0x49f0, 0x4d0c, 0x4d14, 0x512c,
        0x5134, 0x5a54, 0x5a5c, 0x5b64, 0x5c00, 0x5dfc,
    };
    /* 1036a2ac / 10096d20 setter order, including the complete byte2 domain. */
    static const unsigned packet[] = {
        0x88, 0x94, 0x98, 0x9c, 0xa0, 0xa8, 0x8c, 0x84,
    };
    static const unsigned holes[] = {
        0x80, 0x90, 0xa4, 0xb4, 0x139c, 0x13fc, 0x1e38,
    };
    unsigned i;

    qtest_irq_intercept_out_named(q, path, "sysbus-irq");
    {
        g_autofree char *reply = qtest_hmp(q, "xp /1wx 0x%" PRIx64,
                                          first + 0x1400);

        g_assert_null(strstr(reply, "Cannot access memory"));
    }
    {
        g_autofree char *reply = qtest_hmp(q, "xp /1wx 0x%" PRIx64,
                                          first + 0xa8);

        g_assert_null(strstr(reply, "Cannot access memory"));
    }
    for (i = 0; i < ARRAY_SIZE(packet); i++) {
        retained(q, first, packet[i], 0xa5000000 | packet[i]);
        retained(q, second, packet[i], 0x5a000000 | packet[i]);
    }
    for (i = 0; i <= UINT8_MAX; i++) {
        retained(q, first, 0xa8, i);
        retained(q, second, 0xa8, UINT8_MAX - i);
        g_assert_cmphex(qtest_readl(q, first + 0xa8), ==, i);
    }
    /* Bad widths/alignment and adjacent holes cannot change the packet. */
    for (i = 0; i < ARRAY_SIZE(packet); i++) {
        retained(q, first, packet[i], 0xa5000000 | packet[i]);
        qtest_writeb(q, first + packet[i], 0);
        qtest_writew(q, first + packet[i], 0);
        qtest_writel(q, first + packet[i] + 1, 0);
        g_assert_cmphex(qtest_readl(q, first + packet[i]), ==,
                        0xa5000000 | packet[i]);
    }
    for (i = 0; i < ARRAY_SIZE(holes); i++) {
        g_autofree char *reply = qtest_hmp(q, "xp /1wx 0x%" PRIx64,
                                          first + holes[i]);

        g_assert_nonnull(strstr(reply, "Cannot access memory"));
    }
    retained(q, first, 0xc, 1);
    retained(q, second, 0xc, 1);
    retained(q, first, 0, 3);
    retained(q, first, 4, 3);
    retained(q, second, 0, 3);
    retained(q, second, 4, 3);
    for (i = 0; i < G_N_ELEMENTS(fields); i++) {
        retained(q, first, fields[i], 0xa5000000 | fields[i]);
        retained(q, second, fields[i], 0x5a000000 | fields[i]);
        g_assert_cmphex(qtest_readl(q, first + fields[i]), ==,
                        0xa5000000 | fields[i]);
    }
    /* Both source-selected table families, including their unequal strides. */
    for (i = 0x1400; i < 0x1d38; i += 4) {
        retained(q, first, i, 0xa5000000 | i);
        g_assert_cmphex(qtest_readl(q, second + i), ==, 0);
        retained(q, second, i, 0x5a000000 | i);
        g_assert_cmphex(qtest_readl(q, first + i), ==, 0xa5000000 | i);
    }
    qtest_writeb(q, first + 0x1400, 0);
    qtest_writew(q, first + 0x1400, 0);
    qtest_writel(q, first + 0x1401, 0);
    g_assert_cmphex(qtest_readl(q, first + 0x1400), ==, 0xa5001400);
    qtest_clock_step(q, 1000000);
    g_assert_cmphex(qtest_readl(q, first + 0x24), ==, 0);
    g_assert_cmphex(qtest_readl(q, second + 0x24), ==, 0);
    g_assert_false(qtest_get_irq(q, 0));
    g_assert_false(qtest_get_irq(q, 1));
    retained(q, first, 0xe8, 8);
    pulse(q, path, "pending-bit", 3);
    pulse(q, path, "pending-bit", 63);
    g_assert_cmphex(qtest_readl(q, first + 0x24), ==, 8);
    g_assert_false(qtest_get_irq(q, 0));
    g_assert_true(qtest_get_irq(q, 1));
    /* Updating either packet does not acknowledge an existing event. */
    for (i = 0; i < ARRAY_SIZE(packet); i++) {
        retained(q, first, packet[i], i);
        retained(q, second, packet[i], UINT32_MAX - i);
    }
    for (i = 0x1400; i < 0x1d38; i += 4) {
        retained(q, first, i, 0x12340000 | i);
        retained(q, second, i, 0xedcb0000 | i);
    }
    qtest_clock_step(q, 1000000);
    g_assert_cmphex(qtest_readl(q, first + 0xe8), ==, 8);
    g_assert_cmphex(qtest_readl(q, second + 0xe8), ==, 0);
    g_assert_false(qtest_get_irq(q, 0));
    g_assert_true(qtest_get_irq(q, 1));
    g_assert_cmphex(qtest_readl(q, first + 0x24), ==, 8);
    g_assert_cmphex(qtest_readl(q, second + 0x24), ==, 0x80000000);
    retained(q, first, 0xe8, 0);
    g_assert_true(qtest_get_irq(q, 0));
    qtest_writel(q, first + 0x24, 8);
    g_assert_false(qtest_get_irq(q, 0));
    g_assert_true(qtest_get_irq(q, 1));
    qtest_writel(q, second + 0x24, 0x80000000);
    g_assert_false(qtest_get_irq(q, 1));
    qtest_set_irq_in(q, path, "pending-bit", 3, 1);
    g_assert_cmphex(qtest_readl(q, first + 0x24), ==, 0);
    qtest_system_reset(q);
    for (i = 0; i < ARRAY_SIZE(packet); i++) {
        g_assert_cmphex(qtest_readl(q, first + packet[i]), ==, 0);
        g_assert_cmphex(qtest_readl(q, second + packet[i]), ==, 0);
    }
    for (i = 0x1400; i < 0x1d38; i += 4) {
        g_assert_cmphex(qtest_readl(q, first + i), ==, 0);
        g_assert_cmphex(qtest_readl(q, second + i), ==, 0);
    }
    g_assert_cmphex(qtest_readl(q, first + 0x5dfc), ==, 0);
    g_assert_cmphex(qtest_readl(q, second + 0x5dfc), ==, 0);
    g_assert_cmphex(qtest_readl(q, first + 0x24), ==, 0);
    pairs_requests(q, first, second);
}

void nikon_sampler_test(QTestState *q, const char *path, uint64_t base)
{
    const uint32_t word = 0x89abcdef;
    unsigned i;

    qtest_irq_intercept_out_named(q, path, "sysbus-irq");
    retained(q, base, 0x44, 2);
    retained(q, base, 0x48, 0);
    retained(q, base, 0, 0);
    retained(q, base, 0x20, 0x12345678);
    retained(q, base, 0x60, 0xa5);
    retained(q, base, 0xb8, 0x7ff);
    retained(q, base, 0x104, 0x87654321);
    retained(q, base, 0x118, 0xfedcba98);
    retained(q, base, 0x160, 0x76543210);
    for (i = 0; i < 4; i++) {
        retained(q, base, 0x130 + i * 4, word ^ i);
        retained(q, base, 0x140 + i * 4, word + i);
        retained(q, base, 0x178 + i * 4, word - i);
        retained(q, base, 0x188 + i * 4, word | i);
    }
    publish(q, path, &word, 1);
    pulse(q, path, "external-event", 0);
    g_assert_cmphex(qtest_readl(q, base + 0x2c), ==, 0);
    retained(q, base, 0x44, 3);
    retained(q, base, 0x48, 7);
    qtest_writel(q, base + 0x14, 1);
    pulse(q, path, "external-event", 0);
    qtest_clock_step(q, 1000000);
    g_assert_cmphex(qtest_readl(q, base + 0x2c), ==, 0);
    publish(q, path, &word, 1);
    pulse(q, path, "external-event", 0);
    pulse(q, path, "external-event", 1);
    pulse(q, path, "external-event", 2);
    g_assert_cmphex(qtest_readl(q, base + 0x2c), ==, 0x43);
    g_assert_true(qtest_get_irq(q, 0));
    qtest_writel(q, base + 0x2c, 1);
    g_assert_cmphex(qtest_readl(q, base + 0x1a0), ==, word);
    g_assert_true(qtest_get_irq(q, 0));
    qtest_writel(q, base + 0x2c, 0x42);
    g_assert_false(qtest_get_irq(q, 0));
    retained(q, base, 0, 1);
    retained(q, base, 0, 0);
    g_assert_cmphex(qtest_readl(q, base + 0x1a0), ==, word);
    qtest_system_reset(q);
    g_assert_cmphex(qtest_readl(q, base + 0x104), ==, 0);
    g_assert_cmphex(qtest_readl(q, base + 0x2c), ==, 0);
}

void nikon_table_test(QTestState *q, const char *path, uint64_t base)
{
    const uint32_t words[] = { 0x12345678, 0x89abcdef, 0xfedcba98 };
    unsigned i;

    qtest_irq_intercept_out_named(q, path, "sysbus-irq");
    retained(q, base, 0, 1);
    retained(q, base, 4, 1);
    retained(q, base, 0x20, 0x12345678);
    retained(q, base, 0x40, 0x7003f);
    for (i = 0; i < 15; i++) {
        retained(q, base, 0x5c + 4 * i, 0x3fff - i);
        retained(q, base, 0x98 + 4 * i, i & 15);
        retained(q, base, 0xd8 + 4 * i, 0x3fff - i);
        retained(q, base, 0x114 + 4 * i, i & 15);
    }
    for (i = 0; i < 128; i++) {
        retained(q, base, 0x200 + 4 * i, 0x12340000 | i);
    }
    retained(q, base, 0x44, 0x10000);
    qtest_writel(q, base + 0x14, 1);
    publish(q, path, words, 2);
    pulse(q, path, "external-event", 0);
    qtest_clock_step(q, 1000000);
    g_assert_cmphex(qtest_readl(q, base + 0x2c), ==, 0);
    publish(q, path, words, 3);
    pulse(q, path, "external-event", 0);
    pulse(q, path, "external-event", 1);
    pulse(q, path, "external-event", 2);
    g_assert_cmphex(qtest_readl(q, base + 0x2c), ==, 0x2020100);
    qtest_writel(q, base + 0x2c, 0x100);
    for (i = 0; i < 3; i++) {
        g_assert_cmphex(qtest_readl(q, base + 0x194 + 4 * i), ==, words[i]);
    }
    retained(q, base, 0x44, 0);
    g_assert_true(qtest_get_irq(q, 0));
    qtest_writel(q, base + 0x2c, 0x2020000);
    g_assert_false(qtest_get_irq(q, 0));
    qtest_system_reset(q);
    g_assert_cmphex(qtest_readl(q, base + 0x3fc), ==, 0);
    g_assert_cmphex(qtest_readl(q, base + 0x2c), ==, 0);
}

void nikon_shared_test(QTestState *q, const char *path, uint64_t base)
{
    unsigned i;

    qtest_irq_intercept_out_named(q, path, "sysbus-irq");
    retained(q, base, 0x308, 0);
    retained(q, base, 0x308, 3);
    retained(q, base, 0x30c, 0);
    retained(q, base, 0x310, 0);
    retained(q, base, 0x314, 0);
    retained(q, base, 4, 1);
    retained(q, base, 0x20, 0x12345678);
    retained(q, base, 0x58, 0x89abcdef);
    retained(q, base, 0x5c, 0x17ff0);
    retained(q, base, 0x60, 0xfffffff0);
    retained(q, base, 0x11c, 0xa5);
    for (i = 0; i < 8; i++) {
        retained(q, base, 0x94 + i * 4, i);
        retained(q, base, 0x128 + i * 4, 15 - i);
    }
    for (i = 0; i < 10; i++) {
        retained(q, base, 0xb4 + i * 4, 0x7f - i);
        retained(q, base, 0x148 + i * 4, i);
    }
    qtest_clock_step(q, 1000000);
    g_assert_cmphex(qtest_readl(q, base + 0x2c), ==, 0);
    pulse(q, path, "external-event", 0);
    pulse(q, path, "external-event", 1);
    g_assert_cmphex(qtest_readl(q, base + 0x2c), ==, 5);
    qtest_writel(q, base + 0x2c, 1);
    g_assert_true(qtest_get_irq(q, 0));
    retained(q, base, 0, 1);
    retained(q, base, 0x300, 1);
    g_assert_cmphex(qtest_readl(q, base + 0x2c), ==, 4);
    qtest_writel(q, base + 0x2c, 4);
    g_assert_false(qtest_get_irq(q, 0));
    qtest_system_reset(q);
    g_assert_cmphex(qtest_readl(q, base + 0x308), ==, 0);
    g_assert_cmphex(qtest_readl(q, base + 0x2c), ==, 0);
}

void nikon_parameters_test(QTestState *q, const char *path, uint64_t base)
{
    unsigned i;

    qtest_irq_intercept_out_named(q, path, "sysbus-irq");
    retained(q, base, 0x10, 0);
    retained(q, base, 0x14, 0);
    retained(q, base, 0x10, 3);
    retained(q, base, 0x14, 3);
    retained(q, base, 0x20, 0x12345678);
    retained(q, base, 0x24, 0x89abcdef);
    retained(q, base, 0x40, 0xa5);
    retained(q, base, 0x48, 0xfedcba98);
    retained(q, base, 0x5c, 0x5a);
    retained(q, base, 0x64, 1);
    retained(q, base, 0x68, 0x87654321);
    retained(q, base, 0x84, 1);
    retained(q, base, 0x90, 0x76543210);
    for (i = 0; i < 24; i++) {
        retained(q, base, 0xa0 + i * 4, 0x12345678 ^ i);
    }
    qtest_clock_step(q, 1000000);
    g_assert_cmphex(qtest_readl(q, base + 0x2c), ==, 0);
    pulse(q, path, "external-event", 0);
    pulse(q, path, "external-event", 1);
    g_assert_cmphex(qtest_readl(q, base + 0x2c), ==, 0x401);
    qtest_writel(q, base + 0x2c, 0x400);
    g_assert_true(qtest_get_irq(q, 0));
    retained(q, base, 0, 1);
    retained(q, base, 0, 0);
    g_assert_cmphex(qtest_readl(q, base + 0x2c), ==, 1);
    qtest_writel(q, base + 0x2c, 1);
    g_assert_false(qtest_get_irq(q, 0));
    qtest_system_reset(q);
    g_assert_cmphex(qtest_readl(q, base + 0xfc), ==, 0);
    g_assert_cmphex(qtest_readl(q, base + 0x2c), ==, 0);
}
