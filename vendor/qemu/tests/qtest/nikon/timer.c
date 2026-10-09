/*
 * Nominal Nikon timing/compare lifecycle; no firmware or physical waveforms.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "hw/nikon/devices.h"
#include "libqtest.h"
#include "qobject/qdict.h"
#include "qobject/qlist.h"

#define FIRST 0x2c010000
#define SECOND 0x2c011000

typedef struct TimingTest {
    QTestState *qts;
    char *image;
    char *device;
} TimingTest;

static TimingTest timing_init(const char *outputs)
{
    TimingTest t = { 0 };
    uint32_t loop = cpu_to_le32(0xeafffffe);
    QDict *reply;
    const QListEntry *entry;
    int fd = g_file_open_tmp("nikon-timing-XXXXXX", &t.image, NULL);

    g_assert_cmpint(fd, >=, 0);
    g_assert_cmpint(write(fd, &loop, sizeof(loop)), ==, sizeof(loop));
    close(fd);
    t.qts = qtest_initf("-machine z7ii-mas-diagnostic -smp 1 -bios %s "
                        "-display none -serial none", t.image);
    reply = qtest_qmp(t.qts, "{'execute':'qom-list','arguments':"
                      "{'path':'/machine/unattached'}}");
    QLIST_FOREACH_ENTRY(qdict_get_qlist(reply, "return"), entry) {
        const QDict *child = qobject_to(QDict, qlist_entry_obj(entry));

        if (!strcmp(qdict_get_str(child, "type"),
                    "child<" TYPE_NIKON_TIMING ">")) {
            g_assert_null(t.device);
            t.device = g_strdup_printf("/machine/unattached/%s",
                                       qdict_get_str(child, "name"));
        }
    }
    qobject_unref(reply);
    g_assert_nonnull(t.device);
    qtest_irq_intercept_out_named(t.qts, t.device, outputs);
    return t;
}

static void timing_quit(TimingTest *t)
{
    qtest_quit(t->qts);
    unlink(t->image);
    g_free(t->image);
    g_free(t->device);
}

static void timing_program(TimingTest *t, unsigned channel, uint32_t opcode)
{
    uint64_t first = FIRST + 0x100 * (channel + 1);
    uint64_t second = SECOND + 0x100 * (channel + 1);

    qtest_writel(t->qts, SECOND, 0);
    qtest_writel(t->qts, second, 1);
    qtest_writel(t->qts, second, 0);
    qtest_writel(t->qts, second + 4, opcode);
    qtest_writel(t->qts, second + 8, 2999);
    qtest_writel(t->qts, second + 0x24, 1);
    qtest_writel(t->qts, second + 0x28, 0);
    qtest_writel(t->qts, second + 0x2c, 1500);
    /* Original E876 ends with basic control zero; feature has its own gate. */
    qtest_writel(t->qts, first, 0);
    qtest_writel(t->qts, FIRST + 4, 1);
}

static void test_waveform(void)
{
    TimingTest t = timing_init("compare");
    uint64_t first = FIRST + 0x400, second = SECOND + 0x400;
    uint32_t frozen;

    timing_program(&t, 3, 0);
    qtest_clock_step(t.qts, 10000);
    g_assert_cmphex(qtest_readl(t.qts, first + 0x10), ==, 0);
    g_assert_false(qtest_get_irq(t.qts, 3));
    qtest_writel(t.qts, SECOND, 1 << 3);
    g_assert_false(qtest_get_irq(t.qts, 3));
    qtest_clock_step(t.qts, 1);
    g_assert_true(qtest_get_irq(t.qts, 3));
    qtest_clock_step(t.qts, 499);
    g_assert_true(qtest_get_irq(t.qts, 3));
    qtest_clock_step(t.qts, 1);
    g_assert_false(qtest_get_irq(t.qts, 3));
    g_assert_cmphex(qtest_readl(t.qts, first + 0x10), ==, 0);
    qtest_clock_step(t.qts, 500);
    g_assert_true(qtest_get_irq(t.qts, 3));
    g_assert_cmphex(qtest_readl(t.qts, first + 0x10), ==, 0x10);
    qtest_writel(t.qts, SECOND, 0);
    frozen = qtest_readl(t.qts, second + 0x30);
    qtest_clock_step(t.qts, 1000000);
    g_assert_cmphex(qtest_readl(t.qts, second + 0x30), ==, frozen);
    g_assert_false(qtest_get_irq(t.qts, 3));
    qtest_writel(t.qts, first + 0x10, 0x10);
    qtest_writel(t.qts, second + 0x28, 1);
    qtest_writel(t.qts, SECOND, 1 << 3);
    qtest_clock_step(t.qts, 1);
    g_assert_false(qtest_get_irq(t.qts, 3));
    qtest_writel(t.qts, second + 0x24, 0);
    qtest_clock_step(t.qts, 1000000);
    g_assert_cmphex(qtest_readl(t.qts, first + 0x10), ==, 0);
    g_assert_false(qtest_get_irq(t.qts, 3));
    timing_quit(&t);
}

static void test_irq_reset(void)
{
    TimingTest t = timing_init("sysbus-irq");
    uint64_t first = FIRST + 0x100, second = SECOND + 0x100;

    timing_program(&t, 0, 0x31);
    qtest_writel(t.qts, SECOND, 1);
    qtest_clock_step(t.qts, 1001);
    g_assert_true(qtest_get_irq(t.qts, 0));
    g_assert_cmphex(qtest_readl(t.qts, first + 0x10), ==, 0x10);
    qtest_writel(t.qts, first + 0x10, 0x10);
    g_assert_false(qtest_get_irq(t.qts, 0));
    qtest_clock_step(t.qts, 1000000);
    g_assert_cmphex(qtest_readl(t.qts, first + 0x10), ==, 0);
    timing_program(&t, 0, 1);
    qtest_writel(t.qts, SECOND, 1);
    qtest_clock_step(t.qts, 1001);
    g_assert_cmphex(qtest_readl(t.qts, first + 0x10), ==, 0x10);
    qtest_writel(t.qts, first + 0x10, 0x10);
    qtest_clock_step(t.qts, 1000);
    g_assert_cmphex(qtest_readl(t.qts, second + 0xc), >=, 6000);
    g_assert_cmphex(qtest_readl(t.qts, first + 0x10), ==, 0x10);
    qtest_writel(t.qts, FIRST, 1);
    g_assert_false(qtest_get_irq(t.qts, 0));
    g_assert_cmphex(qtest_readl(t.qts, second + 0x24), ==, 0);
    qtest_clock_step(t.qts, 1000000);
    g_assert_cmphex(qtest_readl(t.qts, first + 0x10), ==, 0);
    timing_quit(&t);
}

static void test_preflight(void)
{
    TimingTest t = timing_init("sysbus-irq");
    unsigned channel;

    for (channel = 0; channel < 5; channel++) {
        uint64_t second = SECOND + 0x100 * (channel + 1);

        timing_program(&t, channel, 0);
        qtest_writel(t.qts, second + 0x2c, UINT32_MAX);
        qtest_writel(t.qts, second + 0x3c, 0x12345678);
        qtest_writel(t.qts, second + 0x38, 0);
        qtest_writel(t.qts, second + 0x38, 1);
        qtest_writel(t.qts, second + 0x28, 2);
        qtest_writel(t.qts, second + 0x24, 2);
        qtest_writeb(t.qts, second + 0x24, 0);
        g_assert_cmphex(qtest_readl(t.qts, second + 0x24), ==, 1);
        g_assert_cmphex(qtest_readl(t.qts, second + 0x28), ==, 0);
        g_assert_cmphex(qtest_readl(t.qts, second + 0x2c), ==, UINT32_MAX);
        g_assert_cmphex(qtest_readl(t.qts, second + 0x38), ==, 0);
        g_assert_cmphex(qtest_readl(t.qts, second + 0x3c), ==, 0x12345678);
    }
    qtest_clock_step(t.qts, 1000000);
    for (channel = 0; channel < 12; channel++) {
        g_assert_false(qtest_get_irq(t.qts, channel));
    }
    timing_quit(&t);
}

static void test_transition(void)
{
    TimingTest t = timing_init("sysbus-irq");
    uint64_t first = FIRST + 0x100, second = SECOND + 0x100;

    timing_program(&t, 0, 1);
    qtest_writel(t.qts, first, 1);
    qtest_writel(t.qts, SECOND, 1);
    qtest_clock_step(t.qts, 1001);
    g_assert_cmphex(qtest_readl(t.qts, first + 0x10), ==, 0x10);
    qtest_writel(t.qts, first + 0x10, 0x10);
    qtest_writel(t.qts, second + 0x24, 0);
    qtest_clock_step(t.qts, 10000);
    g_assert_cmphex(qtest_readl(t.qts, first + 0x10), ==, 0);
    g_assert_false(qtest_get_irq(t.qts, 0));
    g_assert_cmphex(qtest_readl(t.qts, second + 0xc), >=, 33003);
    /* Reenable does not report periods elapsed while the feature was off. */
    qtest_writel(t.qts, second + 0x24, 1);
    qtest_clock_step(t.qts, 1);
    g_assert_cmphex(qtest_readl(t.qts, first + 0x10), ==, 0);
    qtest_clock_step(t.qts, 1000);
    g_assert_cmphex(qtest_readl(t.qts, first + 0x10), ==, 0x10);
    /* Reprogramming cancels old edges and rearms the basic threshold. */
    qtest_writel(t.qts, second + 0x24, 0);
    qtest_writel(t.qts, first + 0x10, 0x10);
    qtest_writel(t.qts, second + 8, 29999);
    qtest_clock_step(t.qts, 1000);
    g_assert_cmphex(qtest_readl(t.qts, first + 0x10), ==, 0);
    qtest_clock_step(t.qts, 9001);
    g_assert_cmphex(qtest_readl(t.qts, first + 0x10), ==, 1);
    timing_quit(&t);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/nikon/timing-compare/waveform-pause-disable",
                   test_waveform);
    qtest_add_func("/nikon/timing-compare/irq-one-shot-source-reset",
                   test_irq_reset);
    qtest_add_func("/nikon/timing-compare/geometry-preflight", test_preflight);
    qtest_add_func("/nikon/timing-compare/basic-feature-transition",
                   test_transition);
    return g_test_run();
}
