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

static void timing_program(TimingTest *t, unsigned channel, uint32_t divider)
{
    uint64_t first = FIRST + 0x100 * (channel + 1);
    uint64_t second = SECOND + 0x100 * (channel + 1);

    qtest_writel(t->qts, SECOND, 0);
    qtest_writel(t->qts, second, 1);
    qtest_writel(t->qts, second, 0);
    qtest_writel(t->qts, second + 4, divider);
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
    /* QEMU's fixed-point 3GHz period reaches compare at 500ns. */
    qtest_clock_step(t.qts, 498);
    g_assert_cmphex(qtest_readl(t.qts, second + 0x30), ==, 1497);
    g_assert_true(qtest_get_irq(t.qts, 3));
    qtest_clock_step(t.qts, 1);
    g_assert_cmphex(qtest_readl(t.qts, second + 0x30), ==, 1500);
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
    qtest_clock_step(t.qts, 50001);
    g_assert_true(qtest_get_irq(t.qts, 0));
    g_assert_cmphex(qtest_readl(t.qts, first + 0x10), ==, 0x10);
    qtest_writel(t.qts, first + 0x10, 0x10);
    g_assert_false(qtest_get_irq(t.qts, 0));
    qtest_clock_step(t.qts, 1000000);
    g_assert_cmphex(qtest_readl(t.qts, first + 0x10), ==, 0x10);
    timing_program(&t, 0, 1);
    qtest_writel(t.qts, SECOND, 1);
    qtest_clock_step(t.qts, 2001);
    g_assert_cmphex(qtest_readl(t.qts, first + 0x10), ==, 0x10);
    qtest_writel(t.qts, first + 0x10, 0x10);
    qtest_clock_step(t.qts, 2000);
    g_assert_cmphex(qtest_readl(t.qts, second + 0xc), <=, 2999);
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
        qtest_writel(t.qts, second + 0x38, 2);
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
    qtest_clock_step(t.qts, 2001);
    g_assert_cmphex(qtest_readl(t.qts, first + 0x10), ==, 0x10);
    qtest_writel(t.qts, first + 0x10, 0x10);
    qtest_writel(t.qts, second + 0x24, 0);
    qtest_clock_step(t.qts, 10000);
    g_assert_cmphex(qtest_readl(t.qts, first + 0x10), ==, 1);
    g_assert_true(qtest_get_irq(t.qts, 0));
    g_assert_cmphex(qtest_readl(t.qts, second + 0xc), <=, 2999);
    qtest_writel(t.qts, first + 0x10, 1);
    /* Reenable does not report periods elapsed while the feature was off. */
    qtest_writel(t.qts, second + 0x24, 1);
    qtest_clock_step(t.qts, 1);
    g_assert_cmphex(qtest_readl(t.qts, first + 0x10), ==, 0);
    qtest_clock_step(t.qts, 2000);
    g_assert_cmphex(qtest_readl(t.qts, first + 0x10), ==, 0x10);
    /* Reprogramming cancels old edges and rearms the basic threshold. */
    qtest_writel(t.qts, second + 0x24, 0);
    qtest_writel(t.qts, first + 0x10, 0x10);
    qtest_writel(t.qts, second + 8, 29999);
    qtest_clock_step(t.qts, 1000);
    g_assert_cmphex(qtest_readl(t.qts, first + 0x10), ==, 0);
    qtest_clock_step(t.qts, 19001);
    g_assert_cmphex(qtest_readl(t.qts, first + 0x10), ==, 1);
    timing_quit(&t);
}

static void timing_basic_program(TimingTest *t, unsigned channel,
                                 uint32_t divider, uint32_t period)
{
    uint64_t first = FIRST + 0x100 * (channel + 1);
    uint64_t second = SECOND + 0x100 * (channel + 1);

    qtest_writel(t->qts, SECOND, 0);
    qtest_writel(t->qts, second, 1);
    qtest_writel(t->qts, second, 0);
    qtest_writel(t->qts, second + 4, divider);
    qtest_writel(t->qts, second + 8, period);
    qtest_writel(t->qts, first, 1);
    qtest_writel(t->qts, FIRST + 4, 1);
}

static void test_preset_execution(void)
{
    TimingTest t = timing_init("sysbus-irq");
    uint64_t first = FIRST + 0xa00, second = SECOND + 0xa00;
    uint32_t frozen;

    timing_basic_program(&t, 9, 0, UINT32_MAX - 1);
    qtest_writel(t.qts, SECOND + 4, ~13000U);
    g_assert_cmphex(qtest_readl(t.qts, second + 12), ==, 0);
    qtest_clock_step(t.qts, 10000);
    g_assert_false(qtest_get_irq(t.qts, 9));
    qtest_writel(t.qts, SECOND + 8, 1 << 9);
    g_assert_cmphex(qtest_readl(t.qts, second + 12), ==, ~13000U);
    g_assert_cmphex(qtest_readl(t.qts, first + 16), ==, 0);
    g_assert_false(qtest_get_irq(t.qts, 9));
    qtest_writel(t.qts, SECOND, 1 << 9);
    qtest_clock_step(t.qts, 1000);
    g_assert_cmphex(qtest_readl(t.qts, second + 12), >=, ~13000U + 2999);
    g_assert_false(qtest_get_irq(t.qts, 9));
    qtest_writel(t.qts, SECOND, 0);
    frozen = qtest_readl(t.qts, second + 12);
    qtest_clock_step(t.qts, 1000000);
    g_assert_cmphex(qtest_readl(t.qts, second + 12), ==, frozen);
    qtest_writel(t.qts, SECOND, 1 << 9);
    qtest_clock_step(t.qts, 3000);
    g_assert_false(qtest_get_irq(t.qts, 9));
    qtest_clock_step(t.qts, 400);
    g_assert_true(qtest_get_irq(t.qts, 9));
    g_assert_cmphex(qtest_readl(t.qts, first + 16), ==, 1);
    qtest_writel(t.qts, first + 16, 1);
    qtest_clock_step(t.qts, 10000);
    /* A load is once-only; the next wrap uses the programmed full period. */
    g_assert_false(qtest_get_irq(t.qts, 9));
    qtest_writel(t.qts, FIRST, 1);
    qtest_clock_step(t.qts, 2000000000);
    g_assert_cmphex(qtest_readl(t.qts, first + 16), ==, 0);
    g_assert_false(qtest_get_irq(t.qts, 9));
    timing_quit(&t);
}

static void test_divider_load(void)
{
    TimingTest t = timing_init("sysbus-irq");

    /*
     * 10371704 copies a raw byte; 102e7ce4 supplies divisor minus one.
     * Exercise the complete byte field and every source channel, not a list
     * of values taken from whichever startup path happened to run last.
     */
    for (unsigned divider = 0; divider <= UINT8_MAX; divider++) {
        unsigned channel = divider % 12;
        uint64_t first = FIRST + 0x100 * (channel + 1);
        uint64_t second = SECOND + 0x100 * (channel + 1);
        unsigned scale = divider + 1;

        timing_basic_program(&t, channel, divider, 2999);
        qtest_writel(t.qts, SECOND + 4, 2900);
        qtest_writel(t.qts, SECOND + 8, 1 << channel);
        g_assert_cmphex(qtest_readl(t.qts, second + 4), ==, divider);
        g_assert_cmphex(qtest_readl(t.qts, second + 12), ==, 2900);
        g_assert_false(qtest_get_irq(t.qts, channel));
        qtest_writel(t.qts, SECOND, 1 << channel);
        qtest_clock_step(t.qts, (100 * scale - 1) / 3);
        g_assert_false(qtest_get_irq(t.qts, channel));
        qtest_clock_step(t.qts, 2);
        g_assert_true(qtest_get_irq(t.qts, channel));
        g_assert_cmphex(qtest_readl(t.qts, first + 16), ==, 1);
        qtest_writel(t.qts, first + 16, 1);
        qtest_clock_step(t.qts, 1000 * scale + 1);
        g_assert_true(qtest_get_irq(t.qts, channel));
        qtest_writel(t.qts, SECOND, 0);
    }
    timing_quit(&t);
}

static void test_divider_phase(void)
{
    TimingTest t = timing_init("sysbus-irq");
    uint64_t second = SECOND + 0x800;

    timing_basic_program(&t, 7, 5, 2999); /* Six source ticks per count. */
    qtest_writel(t.qts, SECOND, 1 << 7);
    qtest_clock_step(t.qts, 1);
    qtest_writel(t.qts, SECOND, 0);
    g_assert_cmphex(qtest_readl(t.qts, second + 12), ==, 0);
    qtest_clock_step(t.qts, 1000000);
    qtest_writel(t.qts, SECOND, 1 << 7);
    qtest_clock_step(t.qts, 1);
    qtest_writel(t.qts, second + 4, 5); /* Same divider keeps elapsed phase. */
    g_assert_cmphex(qtest_readl(t.qts, second + 12), ==, 1);
    qtest_writel(t.qts, second + 4, 0); /* Running divider change rejects. */
    qtest_writel(t.qts, second + 4, 0x100); /* Beyond source byte. */
    g_assert_cmphex(qtest_readl(t.qts, second + 4), ==, 5);
    timing_quit(&t);
}

static void test_load_boundaries(void)
{
    TimingTest t = timing_init("sysbus-irq");
    const unsigned dividers[] = { 0, 1, 0x31, 0xf9, 0xff };
    const uint32_t periods[] = { 0, 1, UINT32_MAX };
    uint64_t first = FIRST + 0x800, second = SECOND + 0x800;

    for (unsigned i = 0; i < ARRAY_SIZE(dividers); i++) {
        for (unsigned j = 0; j < ARRAY_SIZE(periods); j++) {
            uint32_t period = periods[j];

            timing_basic_program(&t, 7, dividers[i], period);
            qtest_writel(t.qts, SECOND + 4, 0);
            qtest_writel(t.qts, SECOND + 8, 1 << 7);
            g_assert_cmphex(qtest_readl(t.qts, second + 12), ==, 0);
            qtest_writel(t.qts, SECOND + 4, period);
            qtest_writel(t.qts, SECOND + 8, 1 << 7);
            g_assert_cmphex(qtest_readl(t.qts, second + 12), ==, period);
            if (period != UINT32_MAX) {
                qtest_writel(t.qts, SECOND + 4, period + 1);
                qtest_writel(t.qts, SECOND + 8, 1 << 7);
                g_assert_cmphex(qtest_readl(t.qts, second + 12), ==, period);
                /* Invalid submission retains the operand for a valid retry. */
                qtest_writel(t.qts, second + 8, period + 1);
                qtest_writel(t.qts, SECOND + 8, 1 << 7);
                g_assert_cmphex(qtest_readl(t.qts, second + 12), ==,
                                period + 1);
            }
            g_assert_false(qtest_get_irq(t.qts, 7));
        }
        qtest_writel(t.qts, SECOND, 1 << 7);
        if (dividers[i] >= 3) {
            qtest_clock_step(t.qts, dividers[i] / 3);
        }
        g_assert_false(qtest_get_irq(t.qts, 7));
        qtest_clock_step(t.qts, 2);
        g_assert_true(qtest_get_irq(t.qts, 7));
        g_assert_cmphex(qtest_readl(t.qts, first + 16), ==, 1);
        g_assert_cmphex(qtest_readl(t.qts, second + 12), <=, 5);
    }
    timing_quit(&t);
}

static void test_capture_divider(void)
{
    TimingTest t = timing_init("sysbus-irq");
    uint64_t first = FIRST + 0x600, second = SECOND + 0x600;

    timing_basic_program(&t, 5, 1, 2999);
    qtest_writel(t.qts, second + 16, 1);
    qtest_writel(t.qts, first, 7);
    qtest_writel(t.qts, SECOND, 1 << 5);
    qtest_clock_step(t.qts, 200);
    qtest_set_irq_in(t.qts, t.device, "capture", 0, 1);
    g_assert_cmphex(qtest_readl(t.qts, second + 20), ==, 300);
    g_assert_cmphex(qtest_readl(t.qts, first + 16), ==, 2);
    g_assert_true(qtest_get_irq(t.qts, 5));
    qtest_writel(t.qts, first + 16, 2);
    qtest_writel(t.qts, SECOND, 0);
    qtest_clock_step(t.qts, 1000000);
    qtest_set_irq_in(t.qts, t.device, "capture", 0, 0);
    g_assert_false(qtest_get_irq(t.qts, 5));
    qtest_writel(t.qts, SECOND, 1 << 5);
    qtest_clock_step(t.qts, 200);
    qtest_set_irq_in(t.qts, t.device, "capture", 0, 1);
    g_assert_cmphex(qtest_readl(t.qts, second + 20), ==, 600);
    qtest_writel(t.qts, FIRST, 1);
    qtest_set_irq_in(t.qts, t.device, "capture", 0, 0);
    g_assert_false(qtest_get_irq(t.qts, 5));
    g_assert_cmphex(qtest_readl(t.qts, second + 20), ==, 0);
    timing_quit(&t);
}

static void test_preset_preflight(void)
{
    TimingTest t = timing_init("sysbus-irq");
    uint64_t second = SECOND + 0xa00;

    timing_basic_program(&t, 9, 0, 29999);
    qtest_writel(t.qts, SECOND + 8, 1 << 9);
    g_assert_cmphex(qtest_readl(t.qts, second + 12), ==, 0);
    qtest_writel(t.qts, SECOND + 4, 100);
    qtest_writel(t.qts, SECOND + 8, 0);
    qtest_writel(t.qts, SECOND + 8, 1 << 12);
    qtest_writel(t.qts, SECOND + 8, (1 << 8) | (1 << 9));
    qtest_writel(t.qts, SECOND + 8, 1 << 8); /* Not configured. */
    g_assert_cmphex(qtest_readl(t.qts, second + 12), ==, 0);
    qtest_writel(t.qts, SECOND + 8, 1 << 9);
    g_assert_cmphex(qtest_readl(t.qts, second + 12), ==, 100);
    qtest_writel(t.qts, second + 8, 29999);
    qtest_writel(t.qts, SECOND + 8, 1 << 9); /* Consumed operand. */
    g_assert_cmphex(qtest_readl(t.qts, second + 12), ==, 0);
    qtest_writel(t.qts, SECOND + 4, 200);
    qtest_writeb(t.qts, SECOND + 4, 0xff);
    qtest_writel(t.qts, SECOND + 8, 1 << 9);
    g_assert_cmphex(qtest_readl(t.qts, second + 12), ==, 200);

    timing_basic_program(&t, 5, 1, 29999);
    qtest_writel(t.qts, SECOND + 0x610, 1); /* Capture is already configured. */
    qtest_writel(t.qts, SECOND + 4, 100);
    qtest_writel(t.qts, SECOND + 8, 1 << 5);
    g_assert_cmphex(qtest_readl(t.qts, SECOND + 0x60c), ==, 0);
    timing_program(&t, 0, 0); /* Compare feature is configured. */
    qtest_writel(t.qts, SECOND + 4, 100);
    qtest_writel(t.qts, SECOND + 8, 1);
    g_assert_cmphex(qtest_readl(t.qts, SECOND + 0x10c), ==, 0);

    timing_basic_program(&t, 9, 0, 29999);
    qtest_writel(t.qts, SECOND + 4, 30000); /* Beyond last valid count. */
    qtest_writel(t.qts, SECOND + 8, 1 << 9);
    g_assert_cmphex(qtest_readl(t.qts, second + 12), ==, 0);
    qtest_writel(t.qts, SECOND + 4, 100);
    qtest_writel(t.qts, SECOND, 1 << 9);
    qtest_writel(t.qts, SECOND + 8, 1 << 9); /* Already running. */
    qtest_clock_step(t.qts, 100);
    g_assert_cmphex(qtest_readl(t.qts, second + 12), >=, 299);
    g_assert_cmphex(qtest_readl(t.qts, second + 12), <=, 301);
    qtest_writel(t.qts, SECOND, 0);
    qtest_writel(t.qts, SECOND + 8, 1 << 9); /* Valid retry while stopped. */
    g_assert_cmphex(qtest_readl(t.qts, second + 12), ==, 100);
    g_assert_false(qtest_get_irq(t.qts, 9));
    timing_quit(&t);
}

static void test_preset_reset(void)
{
    TimingTest t = timing_init("sysbus-irq");
    uint64_t first = FIRST + 0xa00, second = SECOND + 0xa00;

    timing_basic_program(&t, 9, 0, 29999);
    qtest_writel(t.qts, SECOND + 4, 100);
    qtest_writel(t.qts, second, 1);
    qtest_writel(t.qts, second, 0);
    qtest_writel(t.qts, second + 8, 29999);
    qtest_writel(t.qts, SECOND + 8, 1 << 9);
    g_assert_cmphex(qtest_readl(t.qts, second + 12), ==, 0);
    qtest_writel(t.qts, SECOND + 4, 200);
    qtest_writel(t.qts, FIRST, 1);
    qtest_writel(t.qts, FIRST, 0);
    qtest_writel(t.qts, second + 8, 29999);
    qtest_writel(t.qts, SECOND + 8, 1 << 9);
    g_assert_cmphex(qtest_readl(t.qts, second + 12), ==, 0);
    qtest_writel(t.qts, SECOND + 4, 300);
    qtest_qmp_assert_success(t.qts, "{'execute':'system_reset'}");
    qtest_qmp_eventwait(t.qts, "RESET");
    qtest_writel(t.qts, second + 8, 29999);
    qtest_writel(t.qts, SECOND + 8, 1 << 9);
    g_assert_cmphex(qtest_readl(t.qts, second + 12), ==, 0);

    timing_basic_program(&t, 9, 0, UINT32_MAX - 1);
    qtest_writel(t.qts, SECOND + 4, ~13000U);
    qtest_writel(t.qts, SECOND + 8, 1 << 9);
    qtest_writel(t.qts, SECOND, 1 << 9);
    qtest_clock_step(t.qts, 1000);
    qtest_writel(t.qts, second, 1);
    qtest_clock_step(t.qts, 2000000000);
    g_assert_cmphex(qtest_readl(t.qts, first + 16), ==, 0);
    g_assert_false(qtest_get_irq(t.qts, 9));
    timing_quit(&t);
}

static void test_preset_pending(void)
{
    TimingTest t = timing_init("sysbus-irq");
    uint64_t first = FIRST + 0xc00, second = SECOND + 0xc00;

    timing_basic_program(&t, 11, 0, 2999);
    qtest_writel(t.qts, SECOND, 1 << 11);
    qtest_clock_step(t.qts, 1001);
    g_assert_true(qtest_get_irq(t.qts, 11));
    qtest_writel(t.qts, SECOND, 0);
    qtest_writel(t.qts, SECOND + 4, 0);
    qtest_writel(t.qts, SECOND + 8, 1 << 11);
    g_assert_cmphex(qtest_readl(t.qts, second + 12), ==, 0);
    g_assert_true(qtest_get_irq(t.qts, 11));
    g_assert_cmphex(qtest_readl(t.qts, first + 16), ==, 1);
    qtest_writel(t.qts, first + 16, 1);
    g_assert_false(qtest_get_irq(t.qts, 11));
    qtest_writel(t.qts, SECOND, 1 << 11);
    qtest_clock_step(t.qts, 500);
    g_assert_false(qtest_get_irq(t.qts, 11));
    qtest_clock_step(t.qts, 501);
    g_assert_true(qtest_get_irq(t.qts, 11));
    timing_quit(&t);
}

static void test_extra_budget(void)
{
    TimingTest t = timing_init("sysbus-irq");

    for (unsigned channel = 0; channel < 5; channel++) {
        uint64_t first = FIRST + 0x100 * (channel + 1);
        uint64_t second = SECOND + 0x100 * (channel + 1);

        for (unsigned budget = 1; budget <= 3; budget += 2) {
            uint32_t frozen;

            timing_basic_program(&t, channel, 0, 2999);
            qtest_writel(t.qts, second + 0x3c, budget);
            qtest_writel(t.qts, second + 0x38, 1);
            qtest_writel(t.qts, first, 9);
            g_assert_cmphex(qtest_readl(t.qts, second + 0x38), ==, 1);
            g_assert_cmphex(qtest_readl(t.qts, second + 0x40), ==, 0);
            g_assert_false(qtest_get_irq(t.qts, channel));
            qtest_writel(t.qts, SECOND, 1 << channel);
            qtest_clock_step(t.qts, 900);
            g_assert_cmphex(qtest_readl(t.qts, second + 0x40), ==, 0);
            g_assert_false(qtest_get_irq(t.qts, channel));
            for (unsigned done = 1; done <= budget; done++) {
                qtest_clock_step(t.qts, done == 1 ? 200 : 1000);
                g_assert_cmphex(qtest_readl(t.qts, second + 0x40), ==,
                                done);
                g_assert_cmphex(qtest_readl(t.qts, first + 0x10), ==,
                                done == budget ? 9 : 1);
                g_assert_true(qtest_get_irq(t.qts, channel));
                qtest_writel(t.qts, first + 0x10, 9);
                g_assert_false(qtest_get_irq(t.qts, channel));
            }
            frozen = qtest_readl(t.qts, second + 0xc);
            qtest_clock_step(t.qts, 10000);
            g_assert_cmphex(qtest_readl(t.qts, second + 0x40), ==, budget);
            g_assert_cmphex(qtest_readl(t.qts, second + 0xc), ==, frozen);
            g_assert_cmphex(qtest_readl(t.qts, first), ==, 9);
            g_assert_cmphex(qtest_readl(t.qts, second + 0x38), ==, 1);
            g_assert_false(qtest_get_irq(t.qts, channel));
        }
    }
    timing_quit(&t);
}

static void timing_extra_program(TimingTest *t, unsigned channel,
                                 uint32_t divider, uint32_t period,
                                 uint32_t budget, bool compare)
{
    uint64_t first = FIRST + 0x100 * (channel + 1);
    uint64_t second = SECOND + 0x100 * (channel + 1);

    timing_basic_program(t, channel, divider, period);
    if (compare) {
        qtest_writel(t->qts, second + 0x2c, 1500);
        qtest_writel(t->qts, second + 0x24, 1);
    }
    qtest_writel(t->qts, second + 0x3c, budget);
    qtest_writel(t->qts, second + 0x38, 1);
    qtest_writel(t->qts, first, compare ? 8 : 9);
}

static void test_extra_compare(void)
{
    TimingTest t = timing_init("sysbus-irq");

    for (unsigned channel = 0; channel < 5; channel++) {
        uint64_t first = FIRST + 0x100 * (channel + 1);
        uint64_t second = SECOND + 0x100 * (channel + 1);

        timing_extra_program(&t, channel, 0, 2999, 3, true);
        qtest_writel(t.qts, SECOND, 1 << channel);
        for (unsigned done = 1; done <= 3; done++) {
            qtest_clock_step(t.qts, done == 1 ? 1100 : 1000);
            g_assert_cmphex(qtest_readl(t.qts, second + 0x40), ==, done);
            g_assert_cmphex(qtest_readl(t.qts, first + 0x10), ==,
                            done == 3 ? 0x18 : 0x10);
            g_assert_true(qtest_get_irq(t.qts, channel));
            qtest_writel(t.qts, first + 0x10, 0x18);
        }
        qtest_clock_step(t.qts, 10000);
        g_assert_cmphex(qtest_readl(t.qts, second + 0x40), ==, 3);
        g_assert_false(qtest_get_irq(t.qts, channel));
    }
    qtest_writel(t.qts, SECOND + 0x538, 1);
    qtest_clock_step(t.qts, 200);
    qtest_writel(t.qts, SECOND + 0x524, 0);
    {
        uint32_t frozen = qtest_readl(t.qts, SECOND + 0x50c);

        qtest_clock_step(t.qts, 10000);
        g_assert_cmphex(qtest_readl(t.qts, SECOND + 0x50c), ==, frozen);
        g_assert_cmphex(qtest_readl(t.qts, SECOND + 0x540), ==, 0);
        g_assert_false(qtest_get_irq(t.qts, 4));
    }
    qtest_writel(t.qts, SECOND + 0x524, 1);
    qtest_clock_step(t.qts, 1000);
    g_assert_cmphex(qtest_readl(t.qts, SECOND + 0x540), ==, 1);
    g_assert_cmphex(qtest_readl(t.qts, FIRST + 0x510), ==, 0x10);
    qtest_writel(t.qts, FIRST + 0x510, 0x18);
    qtest_writel(t.qts, SECOND + 0x538, 0);
    qtest_clock_step(t.qts, 5000);
    g_assert_cmphex(qtest_readl(t.qts, FIRST + 0x510), ==, 0x10);
    timing_quit(&t);

    t = timing_init("compare");
    timing_extra_program(&t, 4, 0, 2999, 1, true);
    qtest_writel(t.qts, SECOND, 1 << 4);
    qtest_clock_step(t.qts, 100);
    g_assert_true(qtest_get_irq(t.qts, 4));
    qtest_clock_step(t.qts, 500);
    g_assert_false(qtest_get_irq(t.qts, 4));
    qtest_clock_step(t.qts, 500);
    g_assert_cmphex(qtest_readl(t.qts, SECOND + 0x540), ==, 1);
    g_assert_false(qtest_get_irq(t.qts, 4));
    qtest_clock_step(t.qts, 10000);
    g_assert_false(qtest_get_irq(t.qts, 4));
    timing_quit(&t);
}

static void test_extra_gates(void)
{
    TimingTest t = timing_init("sysbus-irq");
    uint64_t first = FIRST + 0x500, second = SECOND + 0x500;
    const uint64_t gates[] = { SECOND, FIRST + 4, first };
    const uint32_t paused[] = { 0, 0, 8 };
    const uint32_t resumed[] = { 1 << 4, 1, 9 };

    for (unsigned i = 0; i < ARRAY_SIZE(gates); i++) {
        uint32_t frozen;

        timing_extra_program(&t, 4, 1, 2999, 3, false);
        qtest_writel(t.qts, SECOND, 1 << 4);
        qtest_clock_step(t.qts, 2500);
        qtest_writel(t.qts, gates[i], paused[i]);
        frozen = qtest_readl(t.qts, second + 0xc);
        g_assert_cmphex(frozen, >=, 700);
        g_assert_cmphex(frozen, <=, 800);
        g_assert_cmphex(qtest_readl(t.qts, second + 0x40), ==, 1);
        qtest_writel(t.qts, first + 0x10, 1);
        qtest_clock_step(t.qts, 10000);
        g_assert_cmphex(qtest_readl(t.qts, second + 0xc), ==, frozen);
        g_assert_cmphex(qtest_readl(t.qts, second + 0x40), ==, 1);
        g_assert_false(qtest_get_irq(t.qts, 4));
        qtest_writel(t.qts, gates[i], resumed[i]);
        qtest_clock_step(t.qts, 1000);
        g_assert_cmphex(qtest_readl(t.qts, second + 0x40), ==, 1);
        g_assert_false(qtest_get_irq(t.qts, 4));
        qtest_clock_step(t.qts, 600);
        g_assert_cmphex(qtest_readl(t.qts, second + 0x40), ==, 2);
        g_assert_cmphex(qtest_readl(t.qts, first + 0x10), ==, 1);
        qtest_writel(t.qts, first + 0x10, 1);
        qtest_clock_step(t.qts, 2000);
        g_assert_cmphex(qtest_readl(t.qts, second + 0x40), ==, 3);
        g_assert_cmphex(qtest_readl(t.qts, first + 0x10), ==, 9);
    }

    /* A pause preserves fractional divider ticks as well as the count. */
    timing_extra_program(&t, 4, 5, 2999, 1, false);
    qtest_writel(t.qts, SECOND, 1 << 4);
    qtest_clock_step(t.qts, 1);
    qtest_writel(t.qts, SECOND, 0);
    g_assert_cmphex(qtest_readl(t.qts, second + 0xc), ==, 0);
    qtest_clock_step(t.qts, 10000);
    qtest_writel(t.qts, SECOND, 1 << 4);
    qtest_clock_step(t.qts, 1);
    qtest_writel(t.qts, second + 4, 5);
    g_assert_cmphex(qtest_readl(t.qts, second + 0xc), ==, 1);
    g_assert_cmphex(qtest_readl(t.qts, second + 0x40), ==, 0);
    qtest_clock_step(t.qts, 6100);
    g_assert_cmphex(qtest_readl(t.qts, second + 0x40), ==, 1);
    timing_quit(&t);
}

static void test_extra_rearm(void)
{
    TimingTest t = timing_init("sysbus-irq");
    uint64_t first = FIRST + 0x100, second = SECOND + 0x100;

    timing_extra_program(&t, 0, 0, 2999, 0, false);
    qtest_writel(t.qts, SECOND, 1);
    qtest_clock_step(t.qts, 10000);
    g_assert_cmphex(qtest_readl(t.qts, second + 0x40), ==, 0);
    g_assert_cmphex(qtest_readl(t.qts, first + 0x10), ==, 0);
    g_assert_false(qtest_get_irq(t.qts, 0));
    qtest_writel(t.qts, second + 0x3c, 1);
    g_assert_false(qtest_get_irq(t.qts, 0));
    qtest_clock_step(t.qts, 1100);
    g_assert_cmphex(qtest_readl(t.qts, second + 0x40), ==, 1);
    g_assert_cmphex(qtest_readl(t.qts, first + 0x10), ==, 9);

    /* Rearming and result reads do not acknowledge an earlier interrupt. */
    qtest_writel(t.qts, second + 0x38, 1);
    for (unsigned i = 0; i < 3; i++) {
        g_assert_cmphex(qtest_readl(t.qts, second + 0x40), ==, 0);
        g_assert_cmphex(qtest_readl(t.qts, first + 0x10), ==, 9);
        g_assert_true(qtest_get_irq(t.qts, 0));
    }
    qtest_writel(t.qts, first + 0x10, 9);
    qtest_clock_step(t.qts, 1100);
    g_assert_cmphex(qtest_readl(t.qts, second + 0x40), ==, 1);
    qtest_writel(t.qts, first + 0x10, 9);
    qtest_writel(t.qts, second + 0x3c, 3);
    g_assert_cmphex(qtest_readl(t.qts, second + 0x40), ==, 0);
    qtest_clock_step(t.qts, 1100);
    g_assert_cmphex(qtest_readl(t.qts, second + 0x40), ==, 1);
    qtest_writel(t.qts, first + 0x10, 9);
    qtest_writel(t.qts, second + 8, 5999);
    g_assert_cmphex(qtest_readl(t.qts, second + 0xc), ==, 0);
    g_assert_cmphex(qtest_readl(t.qts, second + 0x40), ==, 0);
    qtest_clock_step(t.qts, 1800);
    g_assert_cmphex(qtest_readl(t.qts, second + 0x40), ==, 0);
    g_assert_false(qtest_get_irq(t.qts, 0));
    qtest_clock_step(t.qts, 400);
    g_assert_cmphex(qtest_readl(t.qts, second + 0x40), ==, 1);

    /* Disabling the budget resumes the underlying periodic basic timer. */
    qtest_writel(t.qts, first + 0x10, 9);
    qtest_writel(t.qts, second + 0x38, 0);
    g_assert_false(qtest_get_irq(t.qts, 0));
    qtest_clock_step(t.qts, 10000);
    g_assert_cmphex(qtest_readl(t.qts, first + 0x10), ==, 1);
    qtest_writel(t.qts, first + 0x10, 1);
    qtest_clock_step(t.qts, 2100);
    g_assert_cmphex(qtest_readl(t.qts, first + 0x10), ==, 1);

    qtest_writel(t.qts, second + 0x38, 1);
    qtest_clock_step(t.qts, 200);
    qtest_writel(t.qts, second + 0x3c, 0);
    qtest_writel(t.qts, first + 0x10, 9);
    qtest_clock_step(t.qts, 10000);
    g_assert_cmphex(qtest_readl(t.qts, second + 0x40), ==, 0);
    g_assert_false(qtest_get_irq(t.qts, 0));
    qtest_writel(t.qts, second + 0x3c, 1);
    g_assert_false(qtest_get_irq(t.qts, 0));
    qtest_clock_step(t.qts, 2100);
    g_assert_cmphex(qtest_readl(t.qts, second + 0x40), ==, 1);
    g_assert_cmphex(qtest_readl(t.qts, first + 0x10), ==, 9);
    timing_quit(&t);
}

static void test_extra_reset(void)
{
    TimingTest t = timing_init("sysbus-irq");
    uint64_t first = FIRST + 0x100, second = SECOND + 0x100;

    for (unsigned reset = 0; reset < 3; reset++) {
        timing_extra_program(&t, 0, 0, 2999, 3, false);
        qtest_writel(t.qts, SECOND, 1);
        qtest_clock_step(t.qts, 1100);
        g_assert_cmphex(qtest_readl(t.qts, second + 0x40), ==, 1);
        g_assert_true(qtest_get_irq(t.qts, 0));
        if (reset < 2) {
            qtest_writel(t.qts, reset ? FIRST : second, 1);
            qtest_writel(t.qts, reset ? FIRST : second, 0);
        } else {
            qtest_qmp_assert_success(t.qts, "{'execute':'system_reset'}");
            qtest_qmp_eventwait(t.qts, "RESET");
        }
        qtest_clock_step(t.qts, 10000);
        g_assert_cmphex(qtest_readl(t.qts, second + 0x38), ==, 0);
        g_assert_cmphex(qtest_readl(t.qts, second + 0x3c), ==, 0);
        g_assert_cmphex(qtest_readl(t.qts, second + 0x40), ==, 0);
        g_assert_cmphex(qtest_readl(t.qts, first + 0x10), ==, 0);
        g_assert_false(qtest_get_irq(t.qts, 0));
    }
    timing_quit(&t);
}

static void test_extra_unrealize(void)
{
    const char *outputs[] = { "sysbus-irq", "compare" };
    const unsigned channels[] = { 0, 4 };

    for (unsigned output = 0; output < ARRAY_SIZE(outputs); output++) {
        TimingTest t = timing_init(outputs[output]);

        timing_extra_program(&t, 0, 0, 2999, 3, false);
        timing_extra_program(&t, 4, 0, 2999, 3, true);
        qtest_writel(t.qts, SECOND, (1 << 0) | (1 << 4));
        qtest_clock_step(t.qts, 1100);
        g_assert_cmphex(qtest_readl(t.qts, SECOND + 0x140), ==, 1);
        g_assert_cmphex(qtest_readl(t.qts, SECOND + 0x540), ==, 1);
        g_assert_cmphex(qtest_readl(t.qts, FIRST + 0x110), ==, 1);
        g_assert_cmphex(qtest_readl(t.qts, FIRST + 0x510), ==, 0x10);
        g_assert_cmpint(qtest_get_irq(t.qts, 0), ==, output == 0);
        g_assert_true(qtest_get_irq(t.qts, 4));
        qtest_qmp_assert_success(t.qts,
            "{'execute':'qom-set','arguments':{'path':%s,"
            "'property':'realized','value':false}}", t.device);
        g_assert_false(qtest_get_irq(t.qts, 0));
        g_assert_false(qtest_get_irq(t.qts, 4));
        for (unsigned phase = 0; phase < 2; phase++) {
            qtest_clock_step(t.qts, 10000);
            for (unsigned i = 0; i < ARRAY_SIZE(channels); i++) {
                uint64_t first = FIRST + 0x100 * (channels[i] + 1);
                uint64_t second = SECOND + 0x100 * (channels[i] + 1);

                g_assert_cmphex(qtest_readl(t.qts, second + 0x38), ==, 0);
                g_assert_cmphex(qtest_readl(t.qts, second + 0x3c), ==, 0);
                g_assert_cmphex(qtest_readl(t.qts, second + 0x40), ==, 0);
                g_assert_cmphex(qtest_readl(t.qts, first + 0x10), ==, 0);
                g_assert_false(qtest_get_irq(t.qts, channels[i]));
            }
            if (phase == 0) {
                qtest_qmp_assert_success(t.qts,
                    "{'execute':'qom-set','arguments':{'path':%s,"
                    "'property':'realized','value':true}}", t.device);
            }
        }
        /* The retained object accepts a fresh budget after re-realization. */
        timing_extra_program(&t, 0, 0, 2999, 1, false);
        timing_extra_program(&t, 4, 0, 2999, 1, true);
        qtest_writel(t.qts, SECOND, (1 << 0) | (1 << 4));
        qtest_clock_step(t.qts, 1100);
        g_assert_cmphex(qtest_readl(t.qts, SECOND + 0x140), ==, 1);
        g_assert_cmphex(qtest_readl(t.qts, SECOND + 0x540), ==, 1);
        g_assert_cmphex(qtest_readl(t.qts, FIRST + 0x110), ==, 9);
        g_assert_cmphex(qtest_readl(t.qts, FIRST + 0x510), ==, 0x18);
        g_assert_cmpint(qtest_get_irq(t.qts, 0), ==, output == 0);
        g_assert_cmpint(qtest_get_irq(t.qts, 4), ==, output == 0);
        timing_quit(&t);
    }
}

static void test_extra_preflight(void)
{
    TimingTest t = timing_init("sysbus-irq");
    uint64_t first = FIRST + 0x100, second = SECOND + 0x100;
    uint32_t frozen;

    timing_extra_program(&t, 0, 0, 2999, 3, false);
    qtest_writel(t.qts, SECOND, 1);
    qtest_clock_step(t.qts, 1100);
    qtest_writel(t.qts, SECOND, 0);
    frozen = qtest_readl(t.qts, second + 0xc);
    qtest_writel(t.qts, second + 0x38, 2);
    qtest_writeb(t.qts, second + 0x38, 0);
    qtest_writew(t.qts, second + 0x38, 0);
    qtest_writel(t.qts, second + 0x39, 0);
    qtest_writeb(t.qts, second + 0x3c, 0xff);
    qtest_writew(t.qts, second + 0x3c, 0xffff);
    qtest_writel(t.qts, second + 0x40, 0);
    qtest_writeb(t.qts, second + 0x40, 0);
    (void)qtest_readb(t.qts, second + 0x40);
    (void)qtest_readw(t.qts, second + 0x40);
    qtest_writel(t.qts, first, 0x19);
    qtest_writel(t.qts, second + 4, 0x100);
    qtest_writel(t.qts, SECOND + 4, 200);
    qtest_writel(t.qts, SECOND + 8, 1); /* Extra excludes initial-count load. */
    g_assert_cmphex(qtest_readl(t.qts, second + 0xc), ==, frozen);
    g_assert_cmphex(qtest_readl(t.qts, second + 0x38), ==, 1);
    g_assert_cmphex(qtest_readl(t.qts, second + 0x3c), ==, 3);
    g_assert_cmphex(qtest_readl(t.qts, second + 0x40), ==, 1);
    g_assert_cmphex(qtest_readl(t.qts, first), ==, 9);
    g_assert_cmphex(qtest_readl(t.qts, first + 0x10), ==, 1);
    g_assert_true(qtest_get_irq(t.qts, 0));
    qtest_writel(t.qts, first + 0x10, 1);
    qtest_clock_step(t.qts, 10000);
    g_assert_cmphex(qtest_readl(t.qts, second + 0x40), ==, 1);
    g_assert_false(qtest_get_irq(t.qts, 0));
    /* A rejected load retains the operand, but cannot bypass extra mode. */
    qtest_writel(t.qts, first, 1);
    qtest_writel(t.qts, second + 0x38, 0);
    qtest_writel(t.qts, SECOND + 8, 1);
    g_assert_cmphex(qtest_readl(t.qts, second + 0xc), ==, 200);

    for (unsigned channel = 5; channel < 12; channel++) {
        uint64_t other_a = FIRST + 0x100 * (channel + 1);
        uint64_t other_b = SECOND + 0x100 * (channel + 1);

        timing_basic_program(&t, channel, 1, 2999);
        if (channel < 7) {
            qtest_writel(t.qts, other_b + 0x10, 1);
        }
        qtest_writel(t.qts, other_b + 0x38, 1);
        qtest_writel(t.qts, other_b + 0x3c, 1);
        qtest_writel(t.qts, other_b + 0x40, 1);
        qtest_writel(t.qts, other_a, 9);
        g_assert_cmphex(qtest_readl(t.qts, other_a), ==, 1);
        g_assert_cmphex(qtest_readl(t.qts, other_b + 0xc), ==, 0);
        g_assert_cmphex(qtest_readl(t.qts, other_a + 0x10), ==, 0);
        g_assert_false(qtest_get_irq(t.qts, channel));
    }
    timing_quit(&t);
}

static void test_extra_limits(void)
{
    TimingTest t = timing_init("sysbus-irq");
    uint64_t first = FIRST + 0x500, second = SECOND + 0x500;

    timing_extra_program(&t, 4, UINT8_MAX, UINT32_MAX, UINT32_MAX, false);
    qtest_writel(t.qts, SECOND, 1 << 4);
    qtest_clock_step(t.qts, 10000);
    g_assert_cmphex(qtest_readl(t.qts, second + 8), ==, UINT32_MAX);
    g_assert_cmphex(qtest_readl(t.qts, second + 0x3c), ==, UINT32_MAX);
    g_assert_cmphex(qtest_readl(t.qts, second + 0x40), ==, 0);
    g_assert_cmphex(qtest_readl(t.qts, second + 0xc), >=, 100);
    g_assert_cmphex(qtest_readl(t.qts, second + 0xc), <=, 120);
    g_assert_false(qtest_get_irq(t.qts, 4));

    /* One full UINT32_MAX+1 period, without billions of short callbacks. */
    timing_extra_program(&t, 4, 0, UINT32_MAX, 1, false);
    qtest_writel(t.qts, SECOND, 1 << 4);
    qtest_clock_step(t.qts, 1400000000);
    g_assert_cmphex(qtest_readl(t.qts, second + 0x40), ==, 0);
    g_assert_false(qtest_get_irq(t.qts, 4));
    qtest_clock_step(t.qts, 100000000);
    g_assert_cmphex(qtest_readl(t.qts, second + 0x40), ==, 1);
    g_assert_cmphex(qtest_readl(t.qts, first + 0x10), ==, 9);
    qtest_writel(t.qts, first + 0x10, 9);
    qtest_clock_step(t.qts, 1500000000);
    g_assert_false(qtest_get_irq(t.qts, 4));

    timing_extra_program(&t, 4, 0, 2999, UINT32_MAX, false);
    qtest_writel(t.qts, SECOND, 1 << 4);
    qtest_clock_step(t.qts, 1100);
    g_assert_cmphex(qtest_readl(t.qts, second + 0x40), ==, 1);
    g_assert_cmphex(qtest_readl(t.qts, first + 0x10), ==, 1);
    qtest_writel(t.qts, SECOND, 0);

    for (uint32_t period = 0; period <= 1; period++) {
        timing_extra_program(&t, 4, 0, period, 3, false);
        qtest_writel(t.qts, SECOND, 1 << 4);
        qtest_clock_step(t.qts, 10);
        g_assert_cmphex(qtest_readl(t.qts, second + 0x40), ==, 3);
        g_assert_cmphex(qtest_readl(t.qts, first + 0x10), ==, 9);
    }
    timing_quit(&t);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/nikon/timing-compare/waveform-pause-disable",
                   test_waveform);
    qtest_add_func("/nikon/timing-compare/irq-dividers-reset",
                   test_irq_reset);
    qtest_add_func("/nikon/timing-compare/geometry-preflight", test_preflight);
    qtest_add_func("/nikon/timing-compare/basic-feature-transition",
                   test_transition);
    qtest_add_func("/nikon/timing-preset/execution", test_preset_execution);
    qtest_add_func("/nikon/timing-preset/divider-load", test_divider_load);
    qtest_add_func("/nikon/timing-preset/divider-phase", test_divider_phase);
    qtest_add_func("/nikon/timing-preset/load-boundaries",
                   test_load_boundaries);
    qtest_add_func("/nikon/timing-capture/divider", test_capture_divider);
    qtest_add_func("/nikon/timing-preset/preflight", test_preset_preflight);
    qtest_add_func("/nikon/timing-preset/reset-cancel", test_preset_reset);
    qtest_add_func("/nikon/timing-preset/pending", test_preset_pending);
    qtest_add_func("/nikon/timing-extra/budget", test_extra_budget);
    qtest_add_func("/nikon/timing-extra/compare", test_extra_compare);
    qtest_add_func("/nikon/timing-extra/gates-phase", test_extra_gates);
    qtest_add_func("/nikon/timing-extra/rearm-pending", test_extra_rearm);
    qtest_add_func("/nikon/timing-extra/reset-cancel", test_extra_reset);
    qtest_add_func("/nikon/timing-extra/unrealize", test_extra_unrealize);
    qtest_add_func("/nikon/timing-extra/preflight", test_extra_preflight);
    qtest_add_func("/nikon/timing-extra/limits", test_extra_limits);
    return g_test_run();
}
