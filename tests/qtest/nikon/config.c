/* Complete MAS/SLA MXIC configuration producer shapes; synthetic field data.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "../libqtest.h"
#include "hw/nikon/devices.h"
#include "qobject/qdict.h"
#include "qobject/qlist.h"

#define CONFIG 0x20010000
#define SLOTS 0x24074000
#define MXIC 0x20016000

static const char *config_bios;

void nikon_config_add_tests(const char *bios);

/* Independent source order: MAS 5b2db90 / SLA 1076d278 snapshot table has
 * nineteen absolute pointers. These are register addresses, not instructions
 * or a copied firmware profile. Values below exercise the documented fields.
 */
static const uint32_t snapshot_offsets[] = {
    0x38, 0x3c, 0x100, 0x104, 0x108, 0x10c, 0x110, 0x114, 0x118, 0x11c,
    0x5004, 0x5020, 0x5024, 0x5028, 0x502c, 0x5030, 0x5034, 0x5038, 0x503c,
};

static QTestState *start(void)
{
    g_autofree char *quoted = g_shell_quote(config_bios);
    g_autofree char *args = g_strdup_printf(
        "-M z7ii-mas-diagnostic -smp 1 -bios %s -serial none", quoted);

    return qtest_init(args);
}

static uint32_t configuration_word(unsigned index)
{
    if (index == 0) {
        return 0xffffff01;
    }
    if (index == 1) {
        return 0xff;
    }
    if (index < 10) {
        return 0x04030201 + index;
    }
    return 0x5a000000 + index;
}

static uint32_t slot_word(unsigned index)
{
    return (index & 0x7f) * 0x01010101;
}

static void draft(QTestState *q, unsigned words, unsigned slots)
{
    qtest_writel(q, CONFIG + 0x60, 1);
    for (unsigned i = 0; i < words; i++) {
        qtest_writel(q, CONFIG + snapshot_offsets[i], configuration_word(i));
    }
    for (unsigned i = 0; i < slots; i++) {
        qtest_writel(q, SLOTS + i * 4, slot_word(i));
    }
}

static void selectors(QTestState *q)
{
    /* 100538b0: four 23-word and four 8-word banks, independently live. */
    for (unsigned bank = 0; bank < 4; bank++) {
        for (unsigned word = 0; word < 31; word++) {
            uint32_t offset = 0x3000 + bank * 0x100 +
                              (word < 23 ? word * 4 : 0xbc + (word - 23) * 4);
            uint32_t value = (bank & 3) | ((word & 3) << 4);

            qtest_writel(q, CONFIG + offset, value);
            g_assert_cmphex(qtest_readl(q, CONFIG + offset), ==, value);
        }
    }
    /* The boolean producer belongs to the separate, existing MXIC device. */
    for (unsigned word = 0; word < 32; word++) {
        qtest_writel(q, MXIC + 0xa00 + word * 4, word & 1);
        g_assert_cmphex(qtest_readl(q, MXIC + 0xa00 + word * 4), ==, word & 1);
    }
}

static void live_parameters(QTestState *q, bool sla)
{
    const uint32_t row[] = {
        0x00200002, 0x00070001, 0x00300002, 0,
        0x00100002, 0x00080002, 0x00300002, 0x00200002,
        0x00150002, 0x00300002,
    };
    /* Complete literal/operand targets from SLA 10053ab4..10053b12. Each
     * row parameter carries mode bits 1:0 and period bits 27:16. The source
     * duplicates parameter 1 and 5 to two independent consumers.
     */
    const uint32_t offsets[] = {
        0x2648, 0x2490, 0x24a0, 0x2790, 0x2798, 0x2628,
        0x2658, 0x2660, 0x2638, 0x2640, 0x27b0, 0x27a8,
    };
    const unsigned parameter[] = { 0, 1, 1, 2, 3, 4, 5, 5, 6, 7, 8, 9 };

    for (unsigned group = 0; group < 2; group++) {
        for (unsigned port = 0; port < 2; port++) {
            for (unsigned level = 0; level < 3; level++) {
                uint64_t base = CONFIG + 0x2000 + group * 0x100 +
                                port * 0x40 + level * 12;

                qtest_writel(q, base, 0x05060307);
                qtest_writel(q, base + 4, 0x00070801);
                qtest_writel(q, base + 8, 0x00090a02);
                g_assert_cmphex(qtest_readl(q, base), ==, 0x05060307);
                g_assert_cmphex(qtest_readl(q, base + 4), ==, 0x00070801);
                g_assert_cmphex(qtest_readl(q, base + 8), ==, 0x00090a02);
            }
        }
    }
    for (unsigned i = 0; i < ARRAY_SIZE(offsets); i++) {
        if (!sla && (offsets[i] == 0x2638 || offsets[i] == 0x2640 ||
                     offsets[i] == 0x27a8)) {
            continue;
        }
        qtest_writel(q, CONFIG + offsets[i], row[parameter[i]]);
        g_assert_cmphex(qtest_readl(q, CONFIG + offsets[i]), ==,
                        row[parameter[i]]);
    }
    if (!sla) {
        qtest_writel(q, CONFIG + 0x2618, 0x00120002);
        g_assert_cmphex(qtest_readl(q, CONFIG + 0x2618), ==, 0x00120002);
    }
}

static void test_family(gconstpointer opaque)
{
    bool sla = GPOINTER_TO_UINT(opaque);
    QTestState *q = start();

    /* Capture path 10053b18 selects active state before calling init. */
    qtest_writel(q, CONFIG + 0x44, 1);
    for (unsigned i = 0; i < ARRAY_SIZE(snapshot_offsets); i++) {
        g_assert_cmphex(qtest_readl(q, CONFIG + snapshot_offsets[i]), ==,
                        i == 0 ? 1 : 0);
    }
    for (unsigned i = 0; i < 512; i++) {
        g_assert_cmphex(qtest_readl(q, CONFIG + 0x4800 + i * 4), ==, 0);
    }
    qtest_writel(q, CONFIG + 0x44, 0);
    selectors(q);
    g_assert_cmphex(qtest_readl(q, CONFIG + 0x38) & 1, ==, 1);
    draft(q, ARRAY_SIZE(snapshot_offsets), 512);
    for (unsigned i = 0; i < 4; i++) {
        qtest_writel(q, CONFIG + 0x5010 + i * 4, 0x12340000 + i);
    }
    /* The active view is unchanged until the actual request completes. */
    qtest_writel(q, CONFIG + 0x44, 1);
    g_assert_cmphex(qtest_readl(q, CONFIG + 0x100), ==, 0);
    g_assert_cmphex(qtest_readl(q, CONFIG + 0x4804), ==, 0);
    qtest_writel(q, CONFIG + 0x44, 0);
    qtest_writel(q, CONFIG + 0x34, 1);
    for (unsigned read = 0; read < 16; read++) {
        g_assert_cmphex(qtest_readl(q, CONFIG + 0x38) & 1, ==, 0);
    }
    qtest_clock_step(q, 9999);
    g_assert_cmphex(qtest_readl(q, CONFIG + 0x38) & 1, ==, 0);
    qtest_clock_step(q, 1);
    g_assert_cmphex(qtest_readl(q, CONFIG + 0x38) & 1, ==, 1);
    qtest_writel(q, CONFIG + 0x60, 0);
    live_parameters(q, sla);
    qtest_writel(q, CONFIG + 0x44, 1);
    for (unsigned i = 0; i < ARRAY_SIZE(snapshot_offsets); i++) {
        g_assert_cmphex(qtest_readl(q, CONFIG + snapshot_offsets[i]), ==,
                        configuration_word(i));
    }
    for (unsigned i = 0; i < 512; i++) {
        g_assert_cmphex(qtest_readl(q, CONFIG + 0x4800 + i * 4), ==, slot_word(i));
    }
    for (unsigned i = 0; i < 4; i++) {
        g_assert_cmphex(qtest_readl(q, CONFIG + 0x5010 + i * 4), ==,
                        0x12340000 + i);
    }
    qtest_quit(q);
}

static void test_transaction(void)
{
    QTestState *q = start();

    draft(q, ARRAY_SIZE(snapshot_offsets), 512);
    qtest_writel(q, CONFIG + 0x34, 2); /* invalid command cannot publish */
    qtest_clock_step(q, 10000);
    qtest_writel(q, CONFIG + 0x44, 1);
    g_assert_cmphex(qtest_readl(q, CONFIG + 0x100), ==, 0);
    qtest_writel(q, CONFIG + 0x44, 0);
    qtest_writel(q, CONFIG + 0x34, 1);
    qtest_writel(q, CONFIG + 0x38, 1); /* snapshot cannot manufacture ready */
    qtest_writel(q, CONFIG + 0x100, 0);
    qtest_writel(q, CONFIG + 0x2638, 0x00300002);
    qtest_writel(q, SLOTS + 4, 0);
    qtest_writel(q, CONFIG + 0x60, 0);
    g_assert_cmphex(qtest_readl(q, CONFIG + 0x38) & 1, ==, 0);
    g_assert_cmphex(qtest_readl(q, CONFIG + 0x60), ==, 1);
    g_assert_cmphex(qtest_readl(q, CONFIG + 0x2638), ==, 0);
    qtest_clock_step(q, 10000);
    qtest_writel(q, CONFIG + 0x44, 1);
    g_assert_cmphex(qtest_readl(q, CONFIG + 0x100), ==, configuration_word(2));
    g_assert_cmphex(qtest_readl(q, CONFIG + 0x4804), ==, slot_word(1));
    qtest_writel(q, CONFIG + 0x100, 0); /* active view cannot mutate draft */
    g_assert_cmphex(qtest_readl(q, CONFIG + 0x100), ==, configuration_word(2));
    qtest_quit(q);
}

static void intercept(QTestState *q, const char *type)
{
    QDict *reply = qtest_qmp(q, "{'execute':'qom-list','arguments':"
                              "{'path':'/machine/unattached'}}");
    const QListEntry *entry;
    g_autofree char *wanted = g_strdup_printf("child<%s>", type);
    g_autofree char *path = NULL;

    QLIST_FOREACH_ENTRY(qdict_get_qlist(reply, "return"), entry) {
        const QDict *child = qobject_to(QDict, qlist_entry_obj(entry));
        if (!strcmp(qdict_get_str(child, "type"), wanted)) {
            g_assert_null(path);
            path = g_strdup_printf("/machine/unattached/%s",
                                   qdict_get_str(child, "name"));
        }
    }
    qobject_unref(reply);
    g_assert_nonnull(path);
    qtest_irq_intercept_out_named(q, path, "sysbus-irq");
}

static void test_partial(void)
{
    QTestState *q = start();

    intercept(q, TYPE_NIKON_MXIC_CONFIG);
    qtest_writel(q, CONFIG + 0xc, 0x1e); /* no completion enable */
    g_assert_false(qtest_get_irq(q, 0));
    qtest_writel(q, CONFIG + 0x60, 1);
    /* Exact source5A2EE sparse shape: eight banks, three ports, four
     * 16-byte rows with two word stores in each row (192 distinct words). */
    for (unsigned bank = 0; bank < 8; bank++) {
        for (unsigned port = 0; port < 3; port++) {
            for (unsigned row = 0; row < 4; row++) {
                for (unsigned word = 0; word < 2; word++) {
                    unsigned offset = bank * 0x100 + port * 0x40 +
                                      row * 0x10 + word * 4;
                    qtest_writel(q, SLOTS + offset, 0x01020304);
                }
            }
        }
    }
    qtest_writel(q, CONFIG + 0x60, 0);
    qtest_writel(q, CONFIG + 0x38, 0xffffff00);
    qtest_writel(q, CONFIG + 0x3c, 0xff);
    qtest_writel(q, CONFIG + 0x100, 0x01020304);
    qtest_writel(q, CONFIG + 0x34, 1);
    g_assert_cmphex(qtest_readl(q, CONFIG + 0x10), ==, 0);
    qtest_clock_step(q, 10000);
    g_assert_cmphex(qtest_readl(q, CONFIG + 0x10), ==, 1);
    g_assert_false(qtest_get_irq(q, 0));
    qtest_writel(q, CONFIG + 0xc, 0x1f);
    g_assert_true(qtest_get_irq(q, 0));
    qtest_writel(q, CONFIG + 0x10, 2); /* W1C unrelated cause */
    g_assert_true(qtest_get_irq(q, 0));
    qtest_writel(q, CONFIG + 0x10, 1);
    g_assert_false(qtest_get_irq(q, 0));
    qtest_writel(q, CONFIG + 0x60, 1);
    qtest_writel(q, SLOTS, 0x05060708);
    qtest_writel(q, SLOTS + 0x40, 0x090a0b0c);
    qtest_writel(q, CONFIG + 0x60, 0);
    qtest_writel(q, CONFIG + 0x38, 0x200); /* only arbiter0 port1 */
    qtest_writel(q, CONFIG + 0x3c, 0);
    qtest_writel(q, CONFIG + 0x34, 1);
    qtest_clock_step(q, 10000);
    qtest_writel(q, CONFIG + 0x44, 1);
    qtest_writel(q, CONFIG + 0x60, 1);
    g_assert_cmphex(qtest_readl(q, SLOTS), ==, 0x01020304);
    g_assert_cmphex(qtest_readl(q, SLOTS + 0x40), ==, 0x090a0b0c);
    g_assert_cmphex(qtest_readl(q, SLOTS + 8), ==, 0);
    qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
    g_assert_cmphex(qtest_readl(q, CONFIG + 0x10), ==, 0);
    g_assert_false(qtest_get_irq(q, 0));
    qtest_quit(q);
}

static void test_second(void)
{
    QTestState *q = start();

    intercept(q, TYPE_NIKON_MXIC);
    qtest_writel(q, MXIC + 0x808, 7);
    qtest_writel(q, MXIC + 0x80c, 7);
    g_assert_false(qtest_get_irq(q, 0));
    qtest_writel(q, MXIC + 0x830, 0x33330);
    qtest_writel(q, MXIC + 0x840, 0x01020304);
    qtest_writel(q, MXIC + 0x844, 0x05060708);
    qtest_writel(q, MXIC + 0x838, 1);
    g_assert_cmphex(qtest_readl(q, MXIC + 0x840), ==, 0);
    qtest_writel(q, MXIC + 0x838, 0);
    qtest_writel(q, MXIC + 0x82c, 1);
    qtest_writel(q, MXIC + 0x830, 1); /* cannot manufacture ready */
    g_assert_cmphex(qtest_readl(q, MXIC + 0x830) & 1, ==, 0);
    qtest_clock_step(q, 9999);
    g_assert_false(qtest_get_irq(q, 0));
    qtest_clock_step(q, 1);
    g_assert_cmphex(qtest_readl(q, MXIC + 0x80c), ==, 1);
    g_assert_true(qtest_get_irq(q, 0));
    qtest_writel(q, MXIC + 0x838, 1);
    g_assert_cmphex(qtest_readl(q, MXIC + 0x840), ==, 0x01020304);
    qtest_writel(q, MXIC + 0x80c, 1);
    g_assert_false(qtest_get_irq(q, 0));
    qtest_writel(q, MXIC + 0x838, 0);
    qtest_writel(q, MXIC + 0x840, 0x20); /* reserved field */
    g_assert_cmphex(qtest_readl(q, MXIC + 0x840), ==, 0x01020304);
    qtest_writel(q, MXIC + 0x82c, 1);
    qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
    qtest_clock_step(q, 10000);
    g_assert_cmphex(qtest_readl(q, MXIC + 0x830), ==, 1);
    g_assert_cmphex(qtest_readl(q, MXIC + 0x80c), ==, 0);
    g_assert_false(qtest_get_irq(q, 0));
    qtest_quit(q);
}

static void test_parameters(void)
{
    QTestState *q = start();
    const unsigned bases[] = { 0x2400, 0x2600, 0x5400, 0x5600 };

    for (unsigned bank = 0; bank < 4; bank++) {
        for (unsigned row = 0; row < (bank < 2 ? 63 : 32); row++) {
            uint64_t address = CONFIG + bases[bank] + row * 8;
            uint32_t value = 2 | ((row & 0x3ff) << 18);
            qtest_writel(q, address, value);
            qtest_writel(q, address + 4, 0x002a0b15);
            g_assert_cmphex(qtest_readl(q, address), ==, value);
            g_assert_cmphex(qtest_readl(q, address + 4), ==, 0x002a0b15);
            qtest_writel(q, address + 4, 0x00400000);
            g_assert_cmphex(qtest_readl(q, address + 4), ==, 0x002a0b15);
        }
    }
    for (unsigned group = 0; group < 2; group++) {
        for (unsigned port = 0; port < 4; port++) {
            for (unsigned level = 0; level < 3; level++) {
                uint64_t address = CONFIG + 0x2000 + group * 0x100 +
                                   port * 0x40 + level * 12;
                qtest_writel(q, address, 0xb);
                g_assert_cmphex(qtest_readl(q, address), ==, 0xb);
            }
        }
    }
    qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
    for (unsigned bank = 0; bank < 4; bank++) {
        g_assert_cmphex(qtest_readl(q, CONFIG + bases[bank]), ==, 0);
        g_assert_cmphex(qtest_readl(q, CONFIG + bases[bank] + 4), ==, 0);
    }
    qtest_quit(q);
}

static void test_reset_fields(void)
{
    QTestState *q = start();

    qtest_writel(q, CONFIG + 0x2638, 0x00300002);
    qtest_writel(q, CONFIG + 0x2640, 0x00200002);
    qtest_writel(q, CONFIG + 0x27a8, 0x00100002);
    qtest_writel(q, CONFIG + 0x2638, 4); /* mode reserved bit */
    qtest_writel(q, CONFIG + 0x2640, 0x10000000); /* period reserved bit */
    qtest_writew(q, CONFIG + 0x27a8, 1); /* word interface */
    g_assert_cmphex(qtest_readl(q, CONFIG + 0x2638), ==, 0x00300002);
    g_assert_cmphex(qtest_readl(q, CONFIG + 0x2640), ==, 0x00200002);
    g_assert_cmphex(qtest_readl(q, CONFIG + 0x27a8), ==, 0x00100002);
    draft(q, ARRAY_SIZE(snapshot_offsets), 512);
    qtest_writel(q, CONFIG + 0x34, 1);
    qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
    qtest_clock_step(q, 10000);
    g_assert_cmphex(qtest_readl(q, CONFIG + 0x38), ==, 1);
    g_assert_cmphex(qtest_readl(q, CONFIG + 0x60), ==, 0);
    g_assert_cmphex(qtest_readl(q, CONFIG + 0x2638), ==, 0);
    g_assert_cmphex(qtest_readl(q, CONFIG + 0x2640), ==, 0);
    g_assert_cmphex(qtest_readl(q, CONFIG + 0x27a8), ==, 0);
    qtest_writel(q, CONFIG + 0x44, 1);
    g_assert_cmphex(qtest_readl(q, CONFIG + 0x100), ==, 0);
    g_assert_cmphex(qtest_readl(q, CONFIG + 0x4804), ==, 0);
    qtest_quit(q);
}

void nikon_config_add_tests(const char *bios)
{
    config_bios = bios;
    qtest_add_data_func("/nikon/config/mas", GUINT_TO_POINTER(0), test_family);
    qtest_add_data_func("/nikon/config/sla", GUINT_TO_POINTER(1), test_family);
    qtest_add_func("/nikon/config/transaction", test_transaction);
    qtest_add_func("/nikon/config/resetfields", test_reset_fields);
    qtest_add_func("/nikon/config/partial", test_partial);
    qtest_add_func("/nikon/config/second", test_second);
    qtest_add_func("/nikon/config/parameters", test_parameters);
}
