/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "libqtest.h"

#define USART0 0x40011000

static void assert_ring(QTestState *qts, const char *id, const char *expected)
{
    QDict *reply = qtest_qmp(qts, "{'execute':'ringbuf-read',"
                            "'arguments':{'device':%s,'size':64}}", id);

    g_assert_false(qdict_haskey(reply, "error"));
    g_assert_cmpstr(qdict_get_str(reply, "return"), ==, expected);
    qobject_unref(reply);
}

static void test_route(const void *opaque)
{
    bool dedicated = GPOINTER_TO_INT(opaque);
    QTestState *qts = qtest_initf(
        "-M nikonex -chardev ringbuf,id=legacy -serial chardev:legacy "
        "-chardev ringbuf,id=battery %s",
        dedicated ? "-global nikon-ex.uart0-chardev=/chardevs/battery" : "");
    const char *selected = dedicated ? "battery" : "legacy";
    const char *unused = dedicated ? "legacy" : "battery";

    /* Includes QEMU mux's escape byte: a raw endpoint preserves it exactly. */
    qtest_writel(qts, USART0 + 4, 1);
    qtest_writel(qts, USART0 + 4, 2);
    qtest_writel(qts, USART0 + 4, 'A');
    assert_ring(qts, selected, "\1\2A");
    assert_ring(qts, unused, "");
    qtest_system_reset(qts);
    qtest_writel(qts, USART0 + 4, 'R');
    assert_ring(qts, selected, "R");
    assert_ring(qts, unused, "");
    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_data_func("/nikon/uart/default", GINT_TO_POINTER(false),
                        test_route);
    qtest_add_data_func("/nikon/uart/dedicated", GINT_TO_POINTER(true),
                        test_route);
    return g_test_run();
}
