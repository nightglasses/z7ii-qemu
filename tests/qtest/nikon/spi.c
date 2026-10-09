/* Native SPI FIFO/frame/cancellation contracts; no Nikon firmware.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "qemu/cutils.h"
#include "qemu/sockets.h"
#include "hw/nikon/peer.h"
#include "hw/nikon/wire.h"
#include "libqtest.h"

#define BASE 0x2c0a0000
#define FRAME_NS 10000
#define RAM 0xb1800000
#define HDMAC 0x2c020000

static const char *bios;

typedef struct SPIWireTest {
    QTestState *q;
    char *dir;
    int fd[2];
    unsigned count;
} SPIWireTest;

static SPIWireTest spi_wire_start_at(unsigned count, unsigned first)
{
    SPIWireTest t = { .fd = { -1, -1 }, .count = count };
    g_autoptr(GString) args = g_string_new(NULL);
    int listener[2];

    g_assert_cmpuint(count, <=, ARRAY_SIZE(t.fd));
    t.dir = g_dir_make_tmp("qtest-spi-XXXXXX", NULL);
    g_assert_nonnull(t.dir);
    g_string_append_printf(args, "-machine z7ii-mas-diagnostic -smp 1 "
                           "-bios %s -serial none -display none", bios);
    for (unsigned i = 0; i < count; i++) {
        struct sockaddr_un address = { .sun_family = AF_UNIX };
        g_autofree char *path = g_strdup_printf("%s/%u", t.dir, i);

        g_assert_cmpuint(strlen(path), <, sizeof(address.sun_path));
        pstrcpy(address.sun_path, sizeof(address.sun_path), path);
        listener[i] = qemu_socket(AF_UNIX, SOCK_STREAM, 0);
        g_assert_cmpint(listener[i], >=, 0);
        g_assert_cmpint(bind(listener[i], (struct sockaddr *)&address,
                             sizeof(address)), ==, 0);
        g_assert_cmpint(listen(listener[i], 1), ==, 0);
        g_string_append_printf(args,
            " -chardev socket,id=p%u,path=%s"
            " -device nikon-peer,id=wire%u,chardev=p%u"
            " -device nikon-spi-peer,peer=/machine/peripheral/wire%u,"
            "spi=/machine/spi%u", i, path, i, i, i, first + i);
    }
    t.q = qtest_init(args->str);
    for (unsigned i = 0; i < count; i++) {
        g_autofree char *path = g_strdup_printf("%s/%u", t.dir, i);
        GPollFD pfd = { .fd = listener[i], .events = G_IO_IN };

        g_assert_cmpint(g_poll(&pfd, 1, 1000), ==, 1);
        t.fd[i] = accept(listener[i], NULL, NULL);
        g_assert_cmpint(t.fd[i], >=, 0);
        close(listener[i]);
        unlink(path);
    }
    qtest_qmp_assert_success(t.q, "{'execute':'cont'}");
    return t;
}

static SPIWireTest spi_wire_start(unsigned count)
{
    return spi_wire_start_at(count, 0);
}

static void spi_wire_quit(SPIWireTest *t)
{
    qtest_quit(t->q);
    for (unsigned i = 0; i < t->count; i++) {
        close(t->fd[i]);
    }
    rmdir(t->dir);
    g_free(t->dir);
}

static void spi_wire_read(SPIWireTest *t, unsigned port, uint8_t *frame)
{
    GPollFD pfd = { .fd = t->fd[port], .events = G_IO_IN };
    unsigned received = 0;

    while (received < NIKON_WIRE_SIZE) {
        ssize_t count;

        g_assert_cmpint(g_poll(&pfd, 1, 1000), ==, 1);
        count = recv(pfd.fd, frame + received, NIKON_WIRE_SIZE - received, 0);
        g_assert_cmpint(count, >, 0);
        received += count;
    }
    g_assert_cmphex(ldl_le_p(frame), ==, NIKON_WIRE_MAGIC);
}

static void spi_wire_send(SPIWireTest *t, unsigned port, uint8_t *frame,
                          uint32_t value)
{
    frame[NIKON_WIRE_TYPE] = NIKON_WIRE_REPLY;
    stl_le_p(frame + NIKON_WIRE_WORD, value);
    /* Split inside token/word fields: transport must preserve partial input. */
    g_assert_cmpint(send(t->fd[port], frame, 11, 0), ==, 11);
    g_assert_cmpint(send(t->fd[port], frame + 11,
                         NIKON_WIRE_SIZE - 11, 0), ==, NIKON_WIRE_SIZE - 11);
}

static void spi_wait_rx(QTestState *q, unsigned port)
{
    int64_t deadline = g_get_monotonic_time() + G_TIME_SPAN_SECOND;

    while (!qtest_readl(q, BASE + port * 0x100 + 0x30)) {
        g_assert_cmpint(g_get_monotonic_time(), <, deadline);
        g_usleep(1000);
    }
}

static QTestState *spi_start(void)
{
    QTestState *q = qtest_initf("-machine z7ii-mas-diagnostic -smp 1 "
                               "-bios %s -serial none -display none", bios);

    qtest_qmp_assert_success(q, "{'execute':'cont'}");
    return q;
}

static void spi_setup(QTestState *q, unsigned port, unsigned bits)
{
    uint64_t base = BASE + port * 0x100;

    qtest_writel(q, base + 0x60, 0);
    qtest_writel(q, base + 0xc, 0x20);
    qtest_writel(q, base + 0x10, (bits - 1) << 8);
    qtest_writel(q, base + 0x28, 0x1ff);
}

static void test_fifo(void)
{
    QTestState *q = spi_start();

    spi_setup(q, 0, 8);
    qtest_writel(q, BASE, 0x123456ab);
    qtest_writel(q, BASE + 0x3c, 1);
    g_assert_cmphex(qtest_readl(q, BASE + 0x2c), ==, 1);
    g_assert_cmphex(qtest_readl(q, BASE + 0x30), ==, 0);
    g_assert_cmphex(qtest_readl(q, BASE + 0x14) & 1, ==, 1);
    qtest_clock_step(q, FRAME_NS);
    g_assert_cmphex(qtest_readl(q, BASE + 0x2c), ==, 0);
    g_assert_cmphex(qtest_readl(q, BASE + 0x30), ==, 1);
    g_assert_cmphex(qtest_readl(q, BASE + 4), ==, 0);
    g_assert_cmphex(qtest_readl(q, BASE + 0x24) & 0x10, ==, 0x10);
    qtest_writel(q, BASE + 0x28, 0x10);
    g_assert_cmphex(qtest_readl(q, BASE + 0x24) & 0x10, ==, 0);
    qtest_quit(q);
}

static void test_cancel(void)
{
    QTestState *q = spi_start();

    spi_setup(q, 0, 32);
    qtest_writel(q, BASE, 0x98765432);
    qtest_writel(q, BASE + 0x3c, 1);
    qtest_writel(q, BASE + 0x3c, 0);
    qtest_clock_step(q, FRAME_NS);
    g_assert_cmphex(qtest_readl(q, BASE + 0x2c), ==, 1);
    g_assert_cmphex(qtest_readl(q, BASE + 0x30), ==, 0);
    g_assert_cmphex(qtest_readl(q, BASE + 0x24) & 0x10, ==, 0);
    qtest_writel(q, BASE + 0x3c, 1);
    qtest_clock_step(q, FRAME_NS);
    g_assert_cmphex(qtest_readl(q, BASE + 0x30), ==, 1);
    qtest_writel(q, BASE + 0x60, 1);
    qtest_clock_step(q, FRAME_NS);
    g_assert_cmphex(qtest_readl(q, BASE + 0x2c), ==, 0);
    g_assert_cmphex(qtest_readl(q, BASE + 0x30), ==, 0);
    g_assert_cmphex(qtest_readl(q, BASE + 0x24), ==, 0);
    qtest_quit(q);
}

static void test_ports(void)
{
    QTestState *q = spi_start();

    for (unsigned i = 0; i < 11; i++) {
        uint64_t base = BASE + i * 0x100;

        spi_setup(q, i, i + 1);
        qtest_writel(q, base, i + 1);
        if (i & 1) {
            qtest_writel(q, base + 0x3c, 1);
        }
    }
    qtest_clock_step(q, FRAME_NS);
    for (unsigned i = 0; i < 11; i++) {
        uint64_t base = BASE + i * 0x100;

        g_assert_cmphex(qtest_readl(q, base + 0x2c), ==, !(i & 1));
        g_assert_cmphex(qtest_readl(q, base + 0x30), ==, i & 1);
    }
    qtest_quit(q);
}

static void test_response(void)
{
    SPIWireTest t = spi_wire_start(1);
    uint8_t frame[NIKON_WIRE_SIZE];
    GPollFD pfd = { .fd = t.fd[0], .events = G_IO_IN };

    qtest_irq_intercept_out_named(t.q, "/machine/spi0", "sysbus-irq");
    spi_setup(t.q, 0, 8);
    qtest_writel(t.q, BASE + 0x20, 0x10);
    qtest_writel(t.q, BASE, 0xab);
    qtest_writel(t.q, BASE + 0x3c, 1);
    qtest_clock_step(t.q, FRAME_NS);
    spi_wire_read(&t, 0, frame);
    g_assert_cmpuint(frame[NIKON_WIRE_TYPE], ==, NIKON_WIRE_REQUEST);
    g_assert_cmpuint(frame[NIKON_WIRE_KIND], ==, NIKON_PEER_CLOCK);
    g_assert_cmpuint(frame[NIKON_WIRE_BITS], ==, 8);
    g_assert_cmphex(ldl_le_p(frame + NIKON_WIRE_WORD), ==, 0xab);
    g_assert_cmpuint(ldq_le_p(frame + NIKON_WIRE_TOKEN), >, 0);
    qtest_clock_step(t.q, FRAME_NS * 100);
    g_assert_cmpint(g_poll(&pfd, 1, 0), ==, 0);
    g_assert_cmphex(qtest_readl(t.q, BASE + 0x2c), ==, 1);
    g_assert_cmphex(qtest_readl(t.q, BASE + 0x30), ==, 0);
    g_assert_cmphex(qtest_readl(t.q, BASE + 0x24) & 0x10, ==, 0);
    g_assert_false(qtest_get_irq(t.q, 0));
    spi_wire_send(&t, 0, frame, 0x5c);
    spi_wait_rx(t.q, 0);
    g_assert_cmphex(qtest_readl(t.q, BASE + 4), ==, 0x5c);
    g_assert_cmphex(qtest_readl(t.q, BASE + 0x2c), ==, 0);
    g_assert_cmphex(qtest_readl(t.q, BASE + 0x24) & 0x10, ==, 0x10);
    g_assert_true(qtest_get_irq(t.q, 0));
    qtest_writel(t.q, BASE + 0x28, 0x10);
    g_assert_false(qtest_get_irq(t.q, 0));
    spi_wire_quit(&t);
}

static void test_late(void)
{
    SPIWireTest t = spi_wire_start(1);
    uint8_t first[NIKON_WIRE_SIZE], second[NIKON_WIRE_SIZE];
    uint8_t cancelled[NIKON_WIRE_SIZE];

    spi_setup(t.q, 0, 8);
    qtest_writel(t.q, BASE, 0x11);
    qtest_writel(t.q, BASE + 0x3c, 1);
    qtest_clock_step(t.q, FRAME_NS);
    spi_wire_read(&t, 0, first);
    qtest_writel(t.q, BASE + 0x3c, 0);
    spi_wire_read(&t, 0, cancelled);
    g_assert_cmpuint(cancelled[NIKON_WIRE_TYPE], ==, NIKON_WIRE_CANCEL);
    g_assert_cmphex(ldq_le_p(cancelled + NIKON_WIRE_TOKEN), ==,
                    ldq_le_p(first + NIKON_WIRE_TOKEN));
    spi_wire_send(&t, 0, first, 0xee);
    qtest_clock_step(t.q, FRAME_NS);
    g_assert_cmphex(qtest_readl(t.q, BASE + 0x30), ==, 0);
    g_assert_cmphex(qtest_readl(t.q, BASE + 0x2c), ==, 1);
    qtest_writel(t.q, BASE + 0x3c, 1);
    qtest_clock_step(t.q, FRAME_NS);
    spi_wire_read(&t, 0, second);
    g_assert_cmphex(ldq_le_p(second + NIKON_WIRE_TOKEN), !=,
                    ldq_le_p(first + NIKON_WIRE_TOKEN));
    spi_wire_send(&t, 0, first, 0xee);
    spi_wire_send(&t, 0, second, 0x22);
    spi_wait_rx(t.q, 0);
    g_assert_cmphex(qtest_readl(t.q, BASE + 4), ==, 0x22);
    g_assert_cmphex(qtest_readl(t.q, BASE + 0x30), ==, 0);
    qtest_writel(t.q, BASE, 0x33);
    qtest_clock_step(t.q, FRAME_NS);
    spi_wire_read(&t, 0, second);
    qtest_writel(t.q, BASE + 0x60, 1);
    spi_wire_read(&t, 0, cancelled);
    spi_wire_send(&t, 0, second, 0xff);
    qtest_clock_step(t.q, FRAME_NS);
    g_assert_cmphex(qtest_readl(t.q, BASE + 0x30), ==, 0);
    g_assert_cmphex(qtest_readl(t.q, BASE + 0x2c), ==, 0);
    g_assert_cmphex(qtest_readl(t.q, BASE + 0x24), ==, 0);
    spi_wire_quit(&t);
}

static void test_async_ports(void)
{
    SPIWireTest t = spi_wire_start(2);
    uint8_t frame[2][NIKON_WIRE_SIZE];

    for (unsigned i = 0; i < 2; i++) {
        spi_setup(t.q, i, 8);
        qtest_writel(t.q, BASE + i * 0x100, i + 0x70);
        qtest_writel(t.q, BASE + i * 0x100 + 0x3c, 1);
    }
    qtest_clock_step(t.q, FRAME_NS);
    for (unsigned i = 0; i < 2; i++) {
        spi_wire_read(&t, i, frame[i]);
        g_assert_cmphex(ldl_le_p(frame[i] + NIKON_WIRE_WORD), ==, i + 0x70);
    }
    spi_wire_send(&t, 1, frame[1], 0x42);
    spi_wait_rx(t.q, 1);
    g_assert_cmphex(qtest_readl(t.q, BASE + 0x30), ==, 0);
    g_assert_cmphex(qtest_readl(t.q, BASE + 0x2c), ==, 1);
    g_assert_cmphex(qtest_readl(t.q, BASE + 0x104), ==, 0x42);
    spi_wire_send(&t, 0, frame[0], 0x24);
    spi_wait_rx(t.q, 0);
    g_assert_cmphex(qtest_readl(t.q, BASE + 4), ==, 0x24);
    spi_wire_quit(&t);
}

static void test_offline(void)
{
    QTestState *q = qtest_initf("-machine z7ii-mas-diagnostic -smp 1 "
        "-bios %s -serial none -display none -device nikon-peer,id=wire0 "
        "-device nikon-spi-peer,peer=/machine/peripheral/wire0,"
        "spi=/machine/spi0", bios);

    qtest_qmp_assert_success(q, "{'execute':'cont'}");
    spi_setup(q, 0, 8);
    qtest_writel(q, BASE, 0x55);
    qtest_writel(q, BASE + 0x3c, 1);
    qtest_clock_step(q, FRAME_NS * 100);
    g_assert_cmphex(qtest_readl(q, BASE + 0x2c), ==, 1);
    g_assert_cmphex(qtest_readl(q, BASE + 0x30), ==, 0);
    g_assert_cmphex(qtest_readl(q, BASE + 0x14) & 1, ==, 1);
    g_assert_cmphex(qtest_readl(q, BASE + 0x24) & 0x10, ==, 0);
    qtest_writel(q, BASE + 0x60, 1);
    g_assert_cmphex(qtest_readl(q, BASE + 0x2c), ==, 0);
    qtest_quit(q);
}

static void spi_dma_setup(QTestState *q, unsigned channel, uint32_t source,
                          uint32_t destination, uint32_t cb, uint32_t ca)
{
    uint64_t base = HDMAC + 0x10 + channel * 0x10;

    qtest_writel(q, base + 8, source);
    qtest_writel(q, base + 12, destination);
    qtest_writel(q, base + 4, cb);
    qtest_writel(q, base, ca);
}

static void test_dma(void)
{
    SPIWireTest t = spi_wire_start_at(1, 4);
    uint64_t base = BASE + 0x400;
    uint8_t frame[NIKON_WIRE_SIZE];

    spi_setup(t.q, 4, 16);
    qtest_writel(t.q, base + 0xc, 0x420);
    qtest_writew(t.q, RAM + 0x1000, 0xa1b2);
    qtest_writew(t.q, RAM + 0x1002, 0xc3d4);
    qtest_writel(t.q, RAM + 0x2000, 0);
    qtest_writel(t.q, HDMAC, 0x90000000);
    /* Source-bound VR channels4/5, demands11/12, two 16-bit beats. */
    spi_dma_setup(t.q, 4, RAM + 0x1000, base, 0x05180000, 0x8b000001);
    spi_dma_setup(t.q, 5, base + 4, RAM + 0x2000, 0x06180000, 0x8c000001);
    qtest_clock_step(t.q, FRAME_NS);
    g_assert_cmphex(qtest_readl(t.q, base + 0x2c), ==, 0);
    g_assert_cmphex(qtest_readl(t.q, RAM + 0x2000), ==, 0);
    qtest_writel(t.q, base + 0x3c, 1);
    qtest_clock_step(t.q, FRAME_NS);
    qtest_clock_step(t.q, FRAME_NS);
    spi_wire_read(&t, 0, frame);
    g_assert_cmpuint(frame[NIKON_WIRE_BITS], ==, 16);
    g_assert_cmphex(ldl_le_p(frame + NIKON_WIRE_WORD), ==, 0xa1b2);
    g_assert_cmphex(qtest_readl(t.q, RAM + 0x2000), ==, 0);
    spi_wire_send(&t, 0, frame, 0x1234);
    spi_wait_rx(t.q, 4);
    qtest_clock_step(t.q, FRAME_NS);
    spi_wire_read(&t, 0, frame);
    g_assert_cmphex(ldl_le_p(frame + NIKON_WIRE_WORD), ==, 0xc3d4);
    g_assert_cmphex(qtest_readw(t.q, RAM + 0x2000), ==, 0x1234);
    spi_wire_send(&t, 0, frame, 0x5678);
    spi_wait_rx(t.q, 4);
    qtest_clock_step(t.q, FRAME_NS);
    g_assert_cmphex(qtest_readw(t.q, RAM + 0x2002), ==, 0x5678);
    g_assert_cmphex(qtest_readl(t.q, base + 0x2c), ==, 0);
    g_assert_cmphex(qtest_readl(t.q, base + 0x30), ==, 0);
    g_assert_cmphex(qtest_readl(t.q, HDMAC + 0x54) & 0x70000, ==, 0x50000);
    g_assert_cmphex(qtest_readl(t.q, HDMAC + 0x64) & 0x70000, ==, 0x50000);
    spi_wire_quit(&t);
}

void nikon_spi_add_tests(const char *image)
{
    bios = image;
    qtest_add_func("/nikon/spi/fifo", test_fifo);
    qtest_add_func("/nikon/spi/cancel", test_cancel);
    qtest_add_func("/nikon/spi/ports", test_ports);
    qtest_add_func("/nikon/spi/response", test_response);
    qtest_add_func("/nikon/spi/late", test_late);
    qtest_add_func("/nikon/spi/asynchronous", test_async_ports);
    qtest_add_func("/nikon/spi/offline", test_offline);
    qtest_add_func("/nikon/spi/dma", test_dma);
}
