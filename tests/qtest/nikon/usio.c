/*
 * Real USIO byte-stream/FIFO/IRQ lifecycle; no firmware or invented peer reply.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "libqtest.h"

#define BASE 0x2c090000
#define FRAME 10000 /* Nominal 100MHz / (BGR99 + 1), 8N1. */

typedef struct USIOTest {
    QTestState *qts;
    char *image;
    int socket;
} USIOTest;

static USIOTest usio_init(bool connected, unsigned channel)
{
    USIOTest t = { .socket = -1 };
    uint32_t loop = cpu_to_le32(0xeafffffe);
    g_autofree char *args = NULL;
    g_autofree char *path = NULL;
    int fd = g_file_open_tmp("nikon-usio-XXXXXX", &t.image, NULL);

    g_assert_cmpint(fd, >=, 0);
    g_assert_cmpint(write(fd, &loop, sizeof(loop)), ==, sizeof(loop));
    close(fd);
    args = g_strdup_printf("-machine z7ii-mas-diagnostic -smp 1 -bios %s "
                           "-display none", t.image);
    if (connected) {
        /* Upstream helper uses a short temporary Unix socket directory. */
        t.qts = qtest_init_with_serial(args, &t.socket);
    } else {
        t.qts = qtest_initf("%s -serial none", args);
    }
    path = g_strdup_printf("/machine/usio%u", channel);
    qtest_irq_intercept_out_named(t.qts, path, "sysbus-irq");
    /* Virtual timers must run; -S inherited from libqtest is not a deadline. */
    qtest_qmp_assert_success(t.qts, "{'execute':'cont'}");
    qtest_writeb(t.qts, BASE + 1, 0x80);
    qtest_writeb(t.qts, BASE, 1);
    qtest_writeb(t.qts, BASE + 2, 0);
    qtest_writew(t.qts, BASE + 6, 99);
    return t;
}

static void usio_quit(USIOTest *t)
{
    if (t->socket >= 0) {
        close(t->socket);
    }
    qtest_quit(t->qts);
    unlink(t->image);
    g_free(t->image);
}

static void usio_output(USIOTest *t, const char *expected)
{
    char bytes[32];
    size_t length = strlen(expected);
    size_t received = 0;
    GPollFD pfd = { .fd = t->socket, .events = G_IO_IN };

    g_assert_cmpuint(length, <=, sizeof(bytes));
    while (received < length) {
        ssize_t count;

        g_assert_cmpint(g_poll(&pfd, 1, 1000), ==, 1);
        count = recv(t->socket, bytes + received, length - received, 0);
        g_assert_cmpint(count, >, 0);
        received += count;
    }
    g_assert_cmpmem(bytes, length, expected, length);
}

static void usio_receive(USIOTest *t, const char *input, unsigned expected)
{
    int64_t deadline = g_get_monotonic_time() + G_TIME_SPAN_SECOND;

    g_assert_cmpint(send(t->socket, input, strlen(input), 0), ==, strlen(input));
    while ((qtest_readw(t->qts, BASE + 0xe) >> 8) != expected) {
        g_assert_cmpint(g_get_monotonic_time(), <, deadline);
        g_usleep(1000);
    }
}

static void test_transmit(void)
{
    USIOTest t = usio_init(true, 0);

    qtest_writew(t.qts, BASE + 0xc, 0x405);
    qtest_writeb(t.qts, BASE + 1, 5);
    g_assert_true(qtest_get_irq(t.qts, 1));
    qtest_writew(t.qts, BASE + 4, 'a');
    qtest_writew(t.qts, BASE + 4, 'b');
    qtest_writew(t.qts, BASE + 4, 'c');
    g_assert_cmphex(qtest_readb(t.qts, BASE + 0xe), ==, 2);
    g_assert_cmphex(qtest_readb(t.qts, BASE + 3) & 3, ==, 0);
    g_assert_false(qtest_get_irq(t.qts, 1));
    for (unsigned i = 0; i < 3; i++) {
        qtest_clock_step(t.qts, FRAME);
    }
    usio_output(&t, "abc");
    g_assert_cmphex(qtest_readw(t.qts, BASE + 0xe), ==, 0);
    g_assert_cmphex(qtest_readb(t.qts, BASE + 3) & 3, ==, 3);
    g_assert_true(qtest_get_irq(t.qts, 1));
    /* FIFO request is hardware-set and software zero-cleared. */
    qtest_writeb(t.qts, BASE + 1, 1);
    qtest_writeb(t.qts, BASE + 0xd, 2);
    g_assert_false(qtest_get_irq(t.qts, 1));
    qtest_writew(t.qts, BASE + 4, 'd');
    qtest_clock_step(t.qts, FRAME);
    usio_output(&t, "d");
    g_assert_true(qtest_get_irq(t.qts, 1));
    usio_quit(&t);
}

static void test_receive(void)
{
    USIOTest t = usio_init(true, 0);

    qtest_writew(t.qts, BASE + 0xc, 0xf);
    qtest_writeb(t.qts, BASE + 0xf, 2);
    qtest_writeb(t.qts, BASE + 1, 0x13);
    g_assert_cmphex(qtest_readw(t.qts, BASE + 0xe), ==, 0);
    g_assert_false(qtest_get_irq(t.qts, 0));
    usio_receive(&t, "x", 1);
    g_assert_false(qtest_get_irq(t.qts, 0));
    usio_receive(&t, "y", 2);
    g_assert_true(qtest_get_irq(t.qts, 0));
    g_assert_cmphex(qtest_readw(t.qts, BASE + 4), ==, 'x');
    g_assert_true(qtest_get_irq(t.qts, 0));
    g_assert_cmphex(qtest_readw(t.qts, BASE + 4), ==, 'y');
    g_assert_false(qtest_get_irq(t.qts, 0));
    /* FRIIE requires real queued input followed by eight baud clocks idle. */
    qtest_writeb(t.qts, BASE + 0xd, 8);
    usio_receive(&t, "z", 1);
    g_assert_false(qtest_get_irq(t.qts, 0));
    qtest_clock_step(t.qts, 8000);
    g_assert_true(qtest_get_irq(t.qts, 0));
    g_assert_cmphex(qtest_readw(t.qts, BASE + 4), ==, 'z');
    g_assert_false(qtest_get_irq(t.qts, 0));
    usio_receive(&t, "z", 1);
    qtest_writeb(t.qts, BASE + 0xc, 0xb);
    g_assert_cmphex(qtest_readw(t.qts, BASE + 0xe), ==, 0);
    qtest_clock_step(t.qts, 8000);
    g_assert_false(qtest_get_irq(t.qts, 0));
    usio_quit(&t);
}

static void test_retransmit(void)
{
    USIOTest t = usio_init(true, 0);

    qtest_writew(t.qts, BASE + 0xc, 0x15);
    qtest_writeb(t.qts, BASE + 1, 1);
    qtest_writew(t.qts, BASE + 4, 'r');
    qtest_clock_step(t.qts, FRAME);
    usio_output(&t, "r");
    qtest_writeb(t.qts, BASE + 1, 0);
    qtest_writew(t.qts, BASE + 0xc, 0x20);
    g_assert_cmphex(qtest_readw(t.qts, BASE + 0xe), ==, 1);
    g_assert_cmphex(qtest_readw(t.qts, BASE + 0xc) & 0x30, ==, 0);
    qtest_writew(t.qts, BASE + 0xc, 1);
    qtest_writeb(t.qts, BASE + 1, 1);
    qtest_clock_step(t.qts, FRAME);
    usio_output(&t, "r");
    /* Flush cancels a queued/in-flight byte and its old deadline. */
    qtest_writew(t.qts, BASE + 4, 'q');
    qtest_writeb(t.qts, BASE + 0xc, 5);
    qtest_clock_step(t.qts, FRAME);
    g_assert_cmphex(qtest_readb(t.qts, BASE + 3) & 3, ==, 3);
    {
        GPollFD pfd = { .fd = t.socket, .events = G_IO_IN };

        g_assert_cmpint(g_poll(&pfd, 1, 0), ==, 0);
    }
    usio_quit(&t);
}

static void test_disconnected(void)
{
    USIOTest t = usio_init(false, 0);

    qtest_writeb(t.qts, BASE + 2, 4);
    qtest_writeb(t.qts, BASE + 7, 1);
    g_assert_cmphex(qtest_readw(t.qts, BASE + 6), ==, 0x163);
    qtest_writeb(t.qts, BASE + 5, 1);
    qtest_writeb(t.qts, BASE + 4, 0x23);
    g_assert_cmphex(qtest_readw(t.qts, BASE + 4), ==, 0x123);
    qtest_writeb(t.qts, BASE + 1, 0x13);
    qtest_clock_step(t.qts, 1000000);
    g_assert_cmphex(qtest_readb(t.qts, BASE + 3), ==, 3);
    g_assert_false(qtest_get_irq(t.qts, 0));
    g_assert_cmphex(qtest_readw(t.qts, BASE + 0xe), ==, 0);
    usio_quit(&t);
}

static void test_channels(void)
{
    for (unsigned observed = 0; observed < 3; observed++) {
        USIOTest t = usio_init(false, observed);
        uint64_t selected = BASE + 0x10 * observed;

        for (unsigned channel = 0; channel < 3; channel++) {
            uint64_t base = BASE + 0x10 * channel;

            qtest_writeb(t.qts, base + 1, 0x80);
            qtest_writeb(t.qts, base, 1);
            qtest_writeb(t.qts, base + 2, 0);
            qtest_writew(t.qts, base + 6, 99 + channel);
            qtest_writew(t.qts, base + 0xc, 0xf);
            qtest_writeb(t.qts, base + 0xf, 2 + channel);
            qtest_writew(t.qts, base + 4, 'A' + channel);
        }
        g_assert_false(qtest_get_irq(t.qts, 0));
        g_assert_false(qtest_get_irq(t.qts, 1));
        /* Only the selected transmitter is enabled and can drain its word. */
        qtest_writeb(t.qts, selected + 1, 5);
        g_assert_false(qtest_get_irq(t.qts, 1));
        qtest_clock_step(t.qts, FRAME + observed * 100);
        g_assert_true(qtest_get_irq(t.qts, 1));
        for (unsigned channel = 0; channel < 3; channel++) {
            uint64_t base = BASE + 0x10 * channel;

            g_assert_cmphex(qtest_readw(t.qts, base + 6), ==, 99 + channel);
            g_assert_cmphex(qtest_readb(t.qts, base + 0xe), ==,
                            channel == observed ? 0 : 1);
            g_assert_cmphex(qtest_readw(t.qts, base + 4), ==, 'A' + channel);
            g_assert_cmphex(qtest_readb(t.qts, base + 3) & 4, ==, 0);
        }
        /* Enabling receive has no input producer on any disconnected port. */
        qtest_writeb(t.qts, selected + 1, 0x13);
        g_assert_false(qtest_get_irq(t.qts, 0));
        g_assert_false(qtest_get_irq(t.qts, 1));
        qtest_writeb(t.qts, selected + 1, 0x80);
        for (unsigned channel = 0; channel < 3; channel++) {
            if (channel != observed) {
                g_assert_cmphex(qtest_readb(t.qts, BASE + 0x10 * channel +
                                           0xe), ==, 1);
            }
        }
        usio_quit(&t);
    }
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/nikon/usio/transmit-fifo-status-irq", test_transmit);
    qtest_add_func("/nikon/usio/receive-threshold-idle-flush", test_receive);
    qtest_add_func("/nikon/usio/retransmit-flush", test_retransmit);
    qtest_add_func("/nikon/usio/disconnected-ninebit-lanes", test_disconnected);
    qtest_add_func("/nikon/usio/three-channel-isolation", test_channels);
    return g_test_run();
}
