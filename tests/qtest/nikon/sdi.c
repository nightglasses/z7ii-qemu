/* Source-derived SDI init/training families, without camera instructions.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "../libqtest.h"

static const char *sdi_bios;

/* Independent MMIO configuration fixture from MAS 6a6b721 and the complete
 * SLA source 1076d278. Single-rank changes are applied below, not collected
 * from the device's register description.
 */
static const struct {
    uint32_t offset;
    uint32_t value[2];
} configuration[] = {
    { 0x100, { 0, 0 } }, { 0x10c, { 0x303, 0x303 } },
    { 0x1004, { 1, 1 } }, { 0x1008, { 1, 1 } },
    { 0x100c, { 1, 1 } }, { 0x1010, { 1, 1 } },
    { 0x1014, { 0, 0 } },
    { 0x1020, { 0x1010001, 0x1010001 } },
    { 0x1024, { 0x1010001, 0x1010001 } },
    { 0x1028, { 0x1010001, 0x1010001 } },
    { 0x102c, { 0x1010001, 0x1010001 } },
    { 0x1030, { 0x1010001, 0x1010001 } },
    { 0x1038, { 0x1010001, 0x1010001 } },
    { 0x2408, { 0x1f8d, 0x1f8d } }, { 0x2414, { 0x553b, 0x553b } },
    { 0x3404, { 0x43, 0x43 } }, { 0x3408, { 0x1a, 0x1a } },
    { 0x340c, { 2, 2 } },
    { 0x3500, { 0x2030101, 0x2030101 } },
    { 0x3504, { 0x2030101, 0x2030101 } },
    { 0x3508, { 0x30000000, 0x30000000 } },
    { 0x350c, { 0x30000000, 0x30000000 } },
    { 0x3510, { 0, 0 } }, { 0x3514, { 0x30000000, 0x30000000 } },
    { 0x3600, { 0xf9f, 0xf9f } }, { 0x3604, { 0x13881, 0x13881 } },
    { 0x360c, { 0x18f, 0x18f } }, { 0x3610, { 0x40404, 0x40404 } },
    { 0x3614, { 0x13031007, 0x13031007 } },
    { 0x3618, { 0x807, 0x807 } }, { 0x361c, { 0x20b0b07, 0x20b0b07 } },
    { 0x3620, { 0x30a0409, 0x30a0409 } }, { 0x3624, { 0x53, 0x53 } },
    { 0x3628, { 0x8f0023, 0x8f0023 } },
    { 0x362c, { 0x3000202, 0x3000202 } },
    { 0x3630, { 0x50059, 0x50059 } }, { 0x3634, { 0xa0a01, 0xa0a01 } },
    { 0x3700, { 0xd, 0xd } }, { 0x3708, { 0x90003, 0x90003 } },
    { 0x370c, { 1, 1 } }, { 0x3800, { 0x618, 0x618 } },
    { 0x3804, { 1, 1 } }, { 0x3808, { 0, 0 } },
    { 0x3900, { 0x9896800, 0x9896800 } }, { 0x3904, { 0, 0 } },
    { 0x3a00, { 0x30d4000, 0x30d4000 } }, { 0x3a04, { 0, 0 } },
    { 0x3b00, { 7, 7 } }, { 0x3b04, { 0, 0 } },
    { 0x4400, { 3, 3 } }, { 0x4404, { 1, 1 } },
    { 0x4408, { 0x44444, 0x44444 } },
    { 0x8040, { 0x40004, 0x40004 } },
    { 0x9040, { 0x40004, 0x40004 } },
    { 0xa040, { 0x40004, 0x40004 } },
    { 0xb040, { 0x40004, 0x40004 } },
    { 0xc040, { 0x40004, 0x40004 } },
    { 0xe400, { 2, 2 } }, { 0xe600, { 2, 2 } },
    { 0xe604, { 1, 1 } }, { 0xe608, { 0x40, 0x40 } },
    { 0xe700, { 2, 2 } }, { 0xe704, { 1, 1 } },
    { 0xe708, { 0x40, 0x40 } }, { 0xe70c, { 0, 0 } },
    { 0xe710, { 0x10000000, 0x10000000 } },
    { 0x10010, { 5, 5 } }, { 0x10014, { 8, 8 } },
    { 0x10018, { 9, 9 } }, { 0x1001c, { 7, 7 } },
    { 0x10020, { 3, 3 } }, { 0x10024, { 0x19061200, 0x19061200 } },
    { 0x10030, { 0x66606, 0x66606 } }, { 0x10034, { 0x3713, 0x3713 } },
    { 0x10040, { 0x30, 0x30 } }, { 0x10044, { 0x4664, 0x4664 } },
    { 0x10048, { 0x330026, 0x330026 } }, { 0x10050, { 0x26, 0x26 } },
    { 0x10054, { 0x9d084, 0xaa092 } },
    { 0x10058, { 0xa0086, 0xae094 } },
    { 0x1005c, { 0x9e086, 0xac092 } },
    { 0x10060, { 0x9f085, 0xb0096 } },
    { 0x10074, { 0x16030, 0x1a030 } },
    { 0x10078, { 0x14030, 0x17030 } },
    { 0x1007c, { 0x14030, 0x15030 } },
    { 0x10080, { 0x16030, 0x18030 } },
    { 0x10094, { 0x800, 0x800 } }, { 0x10098, { 0x800, 0x800 } },
    { 0x1009c, { 0x800, 0x800 } }, { 0x100a0, { 0x800, 0x800 } },
    { 0x100b8, { 3, 3 } },
    { 0x100e4, { 0x89019, 0x96026 } },
    { 0x100e8, { 0x89019, 0x98028 } },
    { 0x100ec, { 0x8c01c, 0x97027 } },
    { 0x100f0, { 0x88018, 0x9a02a } },
    { 0x10128, { 0x10016, 0x1001a } },
    { 0x1012c, { 0x10014, 0x10017 } },
    { 0x10130, { 0x10014, 0x10015 } },
    { 0x10134, { 0x10016, 0x10018 } },
    { 0x10148, { 0x10020, 0x10020 } },
    { 0x1014c, { 0x10020, 0x10020 } },
    { 0x10150, { 0x10020, 0x10020 } },
    { 0x10154, { 0x10020, 0x10020 } },
    { 0x10820, { 0, 0 } }, { 0x10824, { 1, 1 } },
    { 0x10828, { 0x43, 0x43 } }, { 0x1082c, { 0x1a, 0x1a } },
    { 0x10840, { 0x4b, 0x4b } }, { 0x10850, { 0x10050, 0x10050 } },
    { 0x10910, { 0, 0 } },
};

static QTestState *start(void)
{
    g_autofree char *quoted = g_shell_quote(sdi_bios);
    g_autofree char *args = g_strdup_printf(
        "-M z7ii-mas-diagnostic -smp 1 -bios %s -serial none", quoted);

    return qtest_init(args);
}

static uint64_t bank_base(unsigned bank)
{
    return 0x20020000 + bank * 0x20000;
}

static void configure(QTestState *q, unsigned bank, bool single_rank)
{
    uint64_t base = bank_base(bank);

    qtest_writel(q, base, 1);
    for (unsigned i = 0; i < ARRAY_SIZE(configuration); i++) {
        uint32_t offset = configuration[i].offset;
        uint32_t value = configuration[i].value[bank];

        if (single_rank) {
            if (offset == 0x10c) {
                value = 0x101;
            } else if (offset == 0x100b8) {
                value = 1;
            } else if (offset == 0x3504) {
                value = 0x2030100;
            } else if (offset == 0x3508 || offset == 0x350c ||
                       offset == 0x3514) {
                value = 0x40000000;
            }
        }
        qtest_writel(q, base + offset, value);
        g_assert_cmphex(qtest_readl(q, base + offset), ==, value);
    }
    qtest_writel(q, base, 0);
}

static void complete(QTestState *q, uint64_t base, uint32_t request,
                     uint32_t status, uint32_t command)
{
    qtest_writel(q, base + request, command);
    g_assert_cmphex(qtest_readl(q, base + request), ==, command);
    g_assert_cmphex(qtest_readl(q, base + status), ==, 0);
    qtest_clock_step(q, 9999);
    g_assert_cmphex(qtest_readl(q, base + status), ==, 0);
    qtest_clock_step(q, 1);
    g_assert_cmphex(qtest_readl(q, base + status), ==, 1);
    qtest_writel(q, base + status, 1);
    g_assert_cmphex(qtest_readl(q, base + status), ==, 0);
}

static void initialize(QTestState *q, unsigned bank, bool single_rank)
{
    uint64_t base = bank_base(bank);

    configure(q, bank, single_rank);
    qtest_writel(q, base + 0x2000, 0);
    complete(q, base, 0x2100, 0x2300, 1);
    qtest_writel(q, base + 0x3000, 0);
    complete(q, base, 0x3100, 0x3300, 0x10001);
}

static void train(QTestState *q, unsigned bank, uint32_t wl_error)
{
    uint64_t base = bank_base(bank);

    for (unsigned mode = 0; mode < 5; mode++) {
        complete(q, base, 0x3118, 0x3318, 1 | mode << 16);
        g_assert_cmphex(qtest_readl(q, base + 0x1016c), ==,
                        mode == 0 ? wl_error : 0);
    }
}

static void ports(QTestState *q, unsigned bank)
{
    uint64_t base = bank_base(bank);

    qtest_writel(q, base + 0x4000, 0);
    complete(q, base, 0x4100, 0x4300, 1);
    for (unsigned port = 0; port < 5; port++) {
        for (unsigned sub = 0; sub < 3; sub++) {
            uint32_t offset = 0x8000 + port * 0x1000 +
                              (sub == 0 ? 0 : sub == 1 ? 0x800 : 0xc00);

            qtest_writel(q, base + offset, 0);
            complete(q, base, offset + 0x10, offset + 0x30, 1);
        }
    }
    qtest_writel(q, base + 0xe000, 0);
    qtest_writel(q, base + 0xe100, 1);
    qtest_clock_step(q, 10000);
    g_assert_cmphex(qtest_readl(q, base + 0xe300), ==, 1);
    /* Both SLA train families preserve this asymmetric source ACK. */
    qtest_writel(q, base + 0xcc30, 1);
    g_assert_cmphex(qtest_readl(q, base + 0xe300), ==, 1);
}

static void test_family(gconstpointer opaque)
{
    bool single_rank = GPOINTER_TO_UINT(opaque);
    QTestState *q = start();

    for (unsigned bank = 0; bank < 2; bank++) {
        initialize(q, bank, single_rank);
        train(q, bank, 0);
        ports(q, bank);
    }
    qtest_quit(q);
}

static void test_retry(void)
{
    QTestState *q = start();

    for (unsigned bank = 0; bank < 2; bank++) {
        uint64_t base = bank_base(bank);

        initialize(q, bank, true);
        qtest_writel(q, base + 0x100e4, 0x20010);
        train(q, bank, 4);
        qtest_writel(q, base + 0x4000, 0);
        qtest_writel(q, base + 0x4100, 1);
        qtest_clock_step(q, 10000);
        g_assert_cmphex(qtest_readl(q, base + 0x4300), ==, 0);
        /* Source's first retry family widens all four WL windows. */
        for (unsigned lane = 0; lane < 4; lane++) {
            static const uint32_t retry[2][4] = {
                { 0xd1061, 0xd1061, 0xd4064, 0xd0060 },
                { 0xde06e, 0xe0070, 0xdf06f, 0xe2072 },
            };

            qtest_writel(q, base + 0x100e4 + lane * 4, retry[bank][lane]);
        }
        train(q, bank, 0);
        ports(q, bank);
    }
    qtest_quit(q);
}

static void test_cancel(void)
{
    QTestState *q = start();
    uint64_t base = bank_base(0);

    initialize(q, 0, false);
    qtest_writel(q, base + 0x3118, 1);
    qtest_writel(q, base + 0x3300, 1); /* another flag cannot complete training */
    qtest_writel(q, base + 0x3318, 1); /* pending ACK cannot create completion */
    qtest_writel(q, base + 0x100e4, 0x20010); /* pending config is rejected */
    g_assert_cmphex(qtest_readl(q, base + 0x100e4), ==, 0x89019);
    qtest_writel(q, base + 0x3000, 1);
    qtest_clock_step(q, 10000);
    g_assert_cmphex(qtest_readl(q, base + 0x3318), ==, 0);
    qtest_writel(q, base + 0x3000, 0);
    qtest_writel(q, base + 0x3118, 1);
    qtest_clock_step(q, 10000);
    g_assert_cmphex(qtest_readl(q, base + 0x3318), ==, 0);
    complete(q, base, 0x3100, 0x3300, 0x10001);
    train(q, 0, 0);
    qtest_writel(q, base + 0x100b8, 4); /* reserved rank bit */
    g_assert_cmphex(qtest_readl(q, base + 0x100b8), ==, 3);
    qtest_quit(q);
}

static void test_preconditions(void)
{
    QTestState *q = start();
    uint64_t base = bank_base(0);

    configure(q, 0, true);
    qtest_writel(q, base + 0x2000, 0);
    qtest_writel(q, base + 0x10c, 0x303); /* rank mismatch */
    qtest_writel(q, base + 0x2100, 1);
    qtest_clock_step(q, 10000);
    g_assert_cmphex(qtest_readl(q, base + 0x2300), ==, 0);
    qtest_writel(q, base + 0x10c, 0x101);
    complete(q, base, 0x2100, 0x2300, 1);
    qtest_writel(q, base + 0x3000, 0);
    qtest_writel(q, base + 0x350c, 0x30000000); /* inconsistent boundary */
    qtest_writel(q, base + 0x3100, 0x10001);
    qtest_clock_step(q, 10000);
    g_assert_cmphex(qtest_readl(q, base + 0x3300), ==, 0);
    qtest_writel(q, base + 0x350c, 0x40000000);
    complete(q, base, 0x3100, 0x3300, 0x10001);
    qtest_writel(q, base + 0x3118, 0x40001); /* cannot skip the other modes */
    qtest_clock_step(q, 10000);
    g_assert_cmphex(qtest_readl(q, base + 0x3318), ==, 0);
    qtest_writew(q, base + 0x100b8, 3); /* exact word access contract */
    g_assert_cmphex(qtest_readl(q, base + 0x100b8), ==, 1);
    train(q, 0, 0);
    qtest_writel(q, base + 0x4000, 0);
    complete(q, base, 0x4100, 0x4300, 1);
    qtest_writel(q, base + 0x8000, 0);
    qtest_writel(q, base + 0x8010, 1);
    qtest_writel(q, base + 0x4000, 1); /* reset cancels dependent port */
    qtest_clock_step(q, 10000);
    g_assert_cmphex(qtest_readl(q, base + 0x8030), ==, 0);
    g_assert_cmphex(qtest_readl(q, base + 0x8000), ==, 1);
    qtest_quit(q);
}

void nikon_sdi_add_tests(const char *bios)
{
    sdi_bios = bios;
    qtest_add_data_func("/nikon/sdi/dualrank", GUINT_TO_POINTER(0), test_family);
    qtest_add_data_func("/nikon/sdi/singlerank", GUINT_TO_POINTER(1), test_family);
    qtest_add_func("/nikon/sdi/retry", test_retry);
    qtest_add_func("/nikon/sdi/cancel", test_cancel);
    qtest_add_func("/nikon/sdi/preconditions", test_preconditions);
}
