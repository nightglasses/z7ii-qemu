/*
 * EX RNG external input and ready protocol; fixed words are not entropy.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "hw/nikon/devices.h"
#include "libqtest.h"

#define RNG 0x50060800
#define RNGEN (1U << 2)
#define IE (1U << 3)
#define DRDY 1U

typedef struct FixedInput {
    const char *text;
    uint32_t word;
} FixedInput;

static void assert_idle(QTestState *qts)
{
    g_assert_cmphex(qtest_readl(qts, RNG), ==, 0);
    g_assert_cmphex(qtest_readl(qts, RNG + 4), ==, 0);
    g_assert_cmphex(qtest_readl(qts, RNG + 8), ==, 0);
}

static void test_default(void)
{
    QTestState *qts = qtest_init("-machine nikonex -display none");

    assert_idle(qts);
    for (unsigned i = 0; i < 4; i++) {
        qtest_writel(qts, RNG, RNGEN);
        g_assert_cmphex(qtest_readl(qts, RNG + 4), ==, DRDY);
        /* Host data may be any 32-bit word, including zero and repeats. */
        qtest_readl(qts, RNG + 8);
        g_assert_cmphex(qtest_readl(qts, RNG + 4), ==, 0);
        g_assert_cmphex(qtest_readl(qts, RNG + 8), ==, 0);
    }
    qtest_writel(qts, RNG, 0);
    assert_idle(qts);
    qtest_system_reset(qts);
    assert_idle(qts);
    qtest_quit(qts);
}

static void test_fixed(const void *opaque)
{
    const FixedInput *input = opaque;
    QTestState *qts = qtest_initf("-machine nikonex -display none "
        "-global " TYPE_NIKON_EX_RNG ".input-word=%s", input->text);

    assert_idle(qts);
    qtest_writel(qts, RNG, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, RNG), ==, RNGEN | IE);
    g_assert_cmphex(qtest_readl(qts, RNG + 4), ==, DRDY);
    /* Status writes cannot invent error flags or consume the ready word. */
    qtest_writel(qts, RNG + 4, UINT32_MAX);
    qtest_writel(qts, RNG + 4, 0);
    g_assert_cmphex(qtest_readl(qts, RNG + 4), ==, DRDY);
    qtest_writel(qts, RNG + 8, ~input->word); /* Data remains read-only. */

    qtest_writeb(qts, RNG, 0);
    qtest_writew(qts, RNG + 4, 0);
    qtest_writel(qts, RNG + 1, 0);
    qtest_writel(qts, RNG + 12, 0);
    qtest_readb(qts, RNG + 8);
    qtest_readw(qts, RNG + 8);
    qtest_readl(qts, RNG + 9);
    qtest_readl(qts, RNG + 12);
    g_assert_cmphex(qtest_readl(qts, RNG), ==, RNGEN | IE);
    g_assert_cmphex(qtest_readl(qts, RNG + 4), ==, DRDY);
    g_assert_cmphex(qtest_readl(qts, RNG + 8), ==, input->word);
    g_assert_cmphex(qtest_readl(qts, RNG + 4), ==, 0);
    g_assert_cmphex(qtest_readl(qts, RNG + 8), ==, 0);
    qtest_clock_step(qts, 1000000);
    g_assert_cmphex(qtest_readl(qts, RNG + 4), ==, 0);

    /* Rewriting enable supplies another word through the same protocol. */
    qtest_writel(qts, RNG, RNGEN);
    g_assert_cmphex(qtest_readl(qts, RNG + 4), ==, DRDY);
    g_assert_cmphex(qtest_readl(qts, RNG + 8), ==, input->word);
    qtest_writel(qts, RNG, RNGEN);
    qtest_writel(qts, RNG, 0); /* Cancels an unread word. */
    qtest_clock_step(qts, 1000000);
    assert_idle(qts);

    qtest_writel(qts, RNG, RNGEN | IE);
    /* Reset cancels ready, retaining external input. */
    qtest_system_reset(qts);
    assert_idle(qts);
    qtest_clock_step(qts, 1000000);
    assert_idle(qts);
    qtest_writel(qts, RNG, RNGEN);
    g_assert_cmphex(qtest_readl(qts, RNG + 4), ==, DRDY);
    g_assert_cmphex(qtest_readl(qts, RNG + 8), ==, input->word);
    g_assert_cmphex(qtest_readl(qts, RNG + 4), ==, 0);
    qtest_quit(qts);
}

static void startup_rejected(const char *property, const char *reason)
{
    g_autofree char *global = g_strdup_printf(TYPE_NIKON_EX_RNG ".%s",
                                             property);
    g_autofree char *errors = NULL;
    g_autoptr(GError) error = NULL;
    const char *args[] = {
        qtest_qemu_binary(NULL), "-machine", "nikonex", "-global", global,
        "-qtest", "stdio", "-accel", "qtest",
        "-display", "none", "-monitor", "none", "-serial", "none", "-S",
        "-run-with", "exit-with-parent=on", NULL,
    };
    int status;

    g_assert_true(g_spawn_sync(NULL, (char **)args, NULL, 0, NULL, NULL,
                               NULL, &errors, &status, &error));
    g_assert_no_error(error);
    g_assert_true(WIFEXITED(status));
    g_assert_cmpint(WEXITSTATUS(status), ==, 1);
    g_assert_nonnull(strstr(errors, reason));
}

static void test_invalid_input(void)
{
    static const char *const invalid[] = {
        "", "-1", "0x", "1x", "0x100000000", "18446744073709551616",
    };

    if (!g_test_subprocess()) {
        /* A missed rejection must time out instead of hanging the suite. */
        g_test_trap_subprocess(NULL, 5 * G_TIME_SPAN_SECOND, 0);
        g_test_trap_assert_passed();
        return;
    }
    for (unsigned i = 0; i < ARRAY_SIZE(invalid); i++) {
        g_autofree char *property = g_strdup_printf("input-word=%s",
                                                  invalid[i]);

        startup_rejected(property,
                         "input-word must be an unsigned 32-bit integer");
    }
    startup_rejected("unknown-input=1", "not found");
}

int main(int argc, char **argv)
{
    static const FixedInput zero = { "0", 0 };
    static const FixedInput all_ones = { "4294967295", UINT32_MAX };
    static const FixedInput word = { "0x12345678", 0x12345678 };

    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/nikon/rng/default-protocol", test_default);
    qtest_add_data_func("/nikon/rng/fixed-zero", &zero, test_fixed);
    qtest_add_data_func("/nikon/rng/fixed-all-ones", &all_ones, test_fixed);
    qtest_add_data_func("/nikon/rng/fixed-word", &word, test_fixed);
    qtest_add_func("/nikon/rng/invalid-input", test_invalid_input);
    return g_test_run();
}
