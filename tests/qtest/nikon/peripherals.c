/* Native qtest for VR register, clock and DMA protocols. No Nikon code. */
/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "hw/nikon/wire.h"
#include "libqtest.h"
#include "qobject/qdict.h"
#include "qobject/qlist.h"
#include "qobject/qjson.h"

static uint16_t package_crc(const uint8_t *data, size_t length)
{
    uint16_t crc = 0;
    unsigned bit;
    size_t i;

    for (i = 0; i < length; i++) {
        crc ^= data[i] << 8;
        for (bit = 0; bit < 8; bit++) {
            crc = (crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0);
        }
    }
    return crc;
}

static char *test_package(bool upload)
{
    static const char *const names[] = {
        "ex1840_013000.bi", "_tpj01_v140.bin", "eg1840_mas_01700",
        "vr1840_010100.bi", "eg1840_sla_01700",
    };
    const unsigned body_size = 0x1000;
    const unsigned records = body_size / 256 + 1;
    const unsigned vr_size = records * 262;
    const unsigned size = 208 + 4 * 3 + vr_size + 2 + 16;
    g_autofree uint8_t *data = g_malloc0(size);
    g_autofree uint8_t *body = g_malloc0(body_size);
    g_autoptr(GError) error = NULL;
    char *path = NULL;
    unsigned i, offset = 208;
    int fd;

    /* qtest executes no CPU; valid empty guest COPY intervals are required. */
    for (i = 0; i < 13; i++) {
        stl_le_p(body + 0xc00 + i * 12, 0xfff00000);
        stl_le_p(body + 0xc04 + i * 12, 0xfff00000);
    }
    if (upload) {
        /* The machine maps only declared guest intervals. These synthetic
         * ZERO entries cover the capture group's mode, consumed pointer and
         * both 276-byte RX buffers, not arbitrary board RAM or MMIO aliases.
         */
        stl_le_p(body + 0xc9c, 0x10f55);
        stl_le_p(body + 0xca0, 0x10f56);
        stl_le_p(body + 0xca4, 0x11904);
        stl_le_p(body + 0xca8, 0x11908);
        stl_le_p(body + 0xcac, 0x1587c);
        stl_le_p(body + 0xcb0, 0x15aa4);
    }
    memset(data, ' ', 32);
    stl_be_p(data + 32, ARRAY_SIZE(names));
    stl_be_p(data + 36, 208);
    for (i = 0; i < ARRAY_SIZE(names); i++) {
        unsigned payload = i == 3 ? vr_size : 1;
        uint8_t *directory = data + 48 + i * 32;

        memcpy(directory, names[i], strlen(names[i]));
        stl_be_p(directory + 16, offset);
        stl_be_p(directory + 20, payload + 2);
        if (i == 3) {
            unsigned record;

            memcpy(data + offset, "\x04\x01\x04\x00\x00\x01", 6);
            for (record = 1; record < records; record++) {
                uint8_t *r = data + offset + record * 262;

                r[0] = 4;
                r[1] = 1;
                stl_le_p(r + 2, 0xfff00000 + (record - 1) * 256);
                memcpy(r + 6, body + (record - 1) * 256, 256);
            }
        }
        stw_be_p(data + offset + payload, package_crc(data + offset, payload));
        offset += payload + 2;
    }
    stw_be_p(data + size - 16, package_crc(data, size - 16));
    fd = g_file_open_tmp("vrperipherals.XXXXXX", &path, &error);
    g_assert_no_error(error);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    g_assert_true(g_file_set_contents(path, (const char *)data, size, &error));
    g_assert_no_error(error);
    return path;
}

static void test_gpio_and_irq(void)
{
    g_autofree char *path = test_package(false);
    QTestState *qts = qtest_initf("-machine nikonvr -bios %s", path);

    /* Locked PFS cannot connect an external interrupt. */
    qtest_writeb(qts, 0x8c167, 0x40);
    g_assert_cmphex(qtest_readb(qts, 0x8c167), ==, 0);
    qtest_writeb(qts, 0x8c11f, 0);
    qtest_writeb(qts, 0x8c11f, 0x40);
    qtest_writeb(qts, 0x8c167, 0x40);
    qtest_writeb(qts, 0x8c11f, 0x80);
    g_assert_cmphex(qtest_readb(qts, 0x8c167), ==, 0x40);
    qtest_writeb(qts, 0x8750f, 12); /* Both edges, original SCI5 select. */
    qtest_writeb(qts, 0x8734f, 9);
    qtest_writeb(qts, 0x87209, 0x80);
    qtest_set_irq_in(qts, "/machine/vr-peripherals", "pad", 39, 1);
    g_assert_cmphex(qtest_readb(qts, 0x8c044), ==, 0x80);
    g_assert_cmphex(qtest_readb(qts, 0x8704f), ==, 1);
    qtest_writeb(qts, 0x8704f, 0);
    g_assert_cmphex(qtest_readb(qts, 0x8704f), ==, 0);
    qtest_set_irq_in(qts, "/machine/vr-peripherals", "pad", 39, 0);
    g_assert_cmphex(qtest_readb(qts, 0x8704f), ==, 1);
    qtest_set_irq_in(qts, "/machine/vr-peripherals", "ack", 0, 79);
    g_assert_cmphex(qtest_readb(qts, 0x8704f), ==, 0);

    qtest_writeb(qts, 0x8c11f, 0);
    qtest_writeb(qts, 0x8c11f, 0x40);
    qtest_writeb(qts, 0x8c14f, 0x4a); /* Source #74: P17 ISEL and SCI mux. */
    qtest_writeb(qts, 0x8c11f, 0x80);
    qtest_writeb(qts, 0x87507, 8);
    qtest_set_irq_in(qts, "/machine/vr-peripherals", "pad", 15, 1);
    g_assert_cmphex(qtest_readb(qts, 0x87047), ==, 1);

    /* Output direction and peripheral mux determine actual PIDR readback. */
    qtest_writeb(qts, 0x8c020, 0x55);
    qtest_writeb(qts, 0x8c000, 0xff);
    g_assert_cmphex(qtest_readb(qts, 0x8c040), ==, 0x55);
    qtest_writeb(qts, 0x8c060, 0x0f);
    g_assert_cmphex(qtest_readb(qts, 0x8c040), ==, 0x50);
    qtest_quit(qts);
    unlink(path);
}

static void test_cmt(void)
{
    g_autofree char *path = test_package(false);
    QTestState *qts = qtest_initf("-machine nikonvr -bios %s", path);

    qtest_writeb(qts, 0x87304, 3);
    qtest_writeb(qts, 0x87203, 0x10);
    qtest_writew(qts, 0x88002, 0xc0);
    qtest_writew(qts, 0x88006, 999);
    qtest_clock_step(qts, 1000000);
    g_assert_cmphex(qtest_readb(qts, 0x8701c), ==, 0);
    qtest_writew(qts, 0x88000, 1);
    qtest_clock_step(qts, 1000000);
    g_assert_cmphex(qtest_readb(qts, 0x8701c), ==, 1);
    qtest_writew(qts, 0x88000, 0);
    qtest_writeb(qts, 0x8701c, 0);
    qtest_clock_step(qts, 1000000);
    g_assert_cmphex(qtest_readb(qts, 0x8701c), ==, 0);
    qtest_quit(qts);
    unlink(path);
}

static void program_dma(QTestState *qts, unsigned channel, uint32_t source,
                        uint32_t dest, uint16_t address_mode, unsigned count)
{
    unsigned base = 0x82000 + channel * 0x40;

    qtest_writel(qts, base, source);
    qtest_writel(qts, base + 4, dest);
    qtest_writel(qts, base + 8, count);
    qtest_writew(qts, base + 16, 0x2101); /* Normal 16-bit, IRQ request. */
    qtest_writew(qts, base + 20, address_mode);
    qtest_writeb(qts, base + 28, 1);
}

static void test_counter_events(void)
{
    g_autofree char *path = test_package(false);
    QTestState *qts = qtest_initf("-machine nikonvr -bios %s", path);

    qtest_writeb(qts, 0x879d2, 8); /* MTU1A -> vector 210. */
    qtest_writeb(qts, 0x879d3, 9); /* MTU1B -> vector 211. */
    qtest_writeb(qts, 0x87a00, 1);
    qtest_writeb(qts, 0x879d3, 12);
    g_assert_cmphex(qtest_readb(qts, 0x879d3), ==, 9);
    qtest_writeb(qts, 0xc1380, 0x20); /* PCLK, clear on TGRA. */
    qtest_writeb(qts, 0xc1384, 3);
    qtest_writew(qts, 0xc1388, 599);
    qtest_writew(qts, 0xc138a, 299);
    qtest_clock_step(qts, 10000);
    g_assert_cmphex(qtest_readw(qts, 0xc1386), ==, 0);
    g_assert_cmphex(qtest_readb(qts, 0x870d2), ==, 0);
    qtest_writeb(qts, 0xc1280, 2);
    qtest_clock_step(qts, 10000);
    g_assert_cmphex(qtest_readw(qts, 0xc1386), ==, 1);
    g_assert_cmphex(qtest_readb(qts, 0x870d2), ==, 1);
    g_assert_cmphex(qtest_readb(qts, 0x870d3), ==, 1);
    qtest_writeb(qts, 0xc1280, 0);
    qtest_writeb(qts, 0x870d2, 0);
    qtest_clock_step(qts, 100000);
    g_assert_cmphex(qtest_readw(qts, 0xc1386), ==, 1);
    g_assert_cmphex(qtest_readb(qts, 0x870d2), ==, 0);

    /* Original TPU5 TPSC=7 counts only real external clock edges. */
    qtest_writeb(qts, 0x87a00, 0);
    qtest_writeb(qts, 0x87790, 37); /* TPU5A -> vector 144. */
    qtest_writeb(qts, 0x88160, 7);
    qtest_writeb(qts, 0x88164, 1);
    qtest_writew(qts, 0x88168, 2);
    qtest_writeb(qts, 0x88100, 32);
    qtest_clock_step(qts, 100000);
    g_assert_cmphex(qtest_readw(qts, 0x88166), ==, 0);
    qtest_set_irq_in(qts, "/machine/vr-peripherals", "timer-clock", 11, 1);
    qtest_set_irq_in(qts, "/machine/vr-peripherals", "timer-clock", 11, 0);
    qtest_set_irq_in(qts, "/machine/vr-peripherals", "timer-clock", 11, 1);
    g_assert_cmphex(qtest_readw(qts, 0x88166), ==, 2);
    g_assert_cmphex(qtest_readb(qts, 0x87090), ==, 1);
    qtest_quit(qts);
    unlink(path);
}

static void test_adc_input(void)
{
    g_autofree char *path = test_package(false);
    QTestState *qts = qtest_initf("-machine nikonvr -bios %s", path);
    unsigned i;

    qtest_writeb(qts, 0x87791, 64); /* S12ADI -> vector 145. */
    qtest_writew(qts, 0x89004, 7);
    qtest_writew(qts, 0x89008, 7);
    qtest_writeb(qts, 0x8900c, 5); /* Original sixteen-sample addition. */
    qtest_writew(qts, 0x89000, 0x9000);
    qtest_clock_step(qts, 100000);
    g_assert_cmphex(qtest_readw(qts, 0x89000) & 0x8000, ==, 0x8000);
    g_assert_cmphex(qtest_readb(qts, 0x87091), ==, 0);
    qtest_set_irq_in(qts, "/machine/vr-peripherals", "analog", 0, 123);
    qtest_set_irq_in(qts, "/machine/vr-peripherals", "analog", 1, 456);
    qtest_set_irq_in(qts, "/machine/vr-peripherals", "analog", 2, 789);
    for (i = 0; i < 15; i++) {
        qtest_clock_step(qts, 1000);
    }
    g_assert_cmphex(qtest_readw(qts, 0x89000) & 0x8000, ==, 0x8000);
    qtest_clock_step(qts, 1000);
    g_assert_cmphex(qtest_readw(qts, 0x89000) & 0x8000, ==, 0);
    g_assert_cmphex(qtest_readw(qts, 0x89020), ==, 123 * 16);
    g_assert_cmphex(qtest_readw(qts, 0x89022), ==, 456 * 16);
    g_assert_cmphex(qtest_readw(qts, 0x89024), ==, 789 * 16);
    g_assert_cmphex(qtest_readb(qts, 0x87091), ==, 1);
    qtest_quit(qts);
    unlink(path);
}

static void test_dtc_chain(void)
{
    g_autofree char *path = test_package(false);
    QTestState *qts = qtest_initf("-machine nikonvr -bios %s", path);

    qtest_writel(qts, 0x1348, 0x1100); /* DTCVBR1000, vector210 pointer. */
    qtest_writel(qts, 0x1100, 0x01000000); /* WBDIS, byte, normal. */
    qtest_writel(qts, 0x1104, 0x1200);
    qtest_writel(qts, 0x1108, 0x1204);
    qtest_writel(qts, 0x110c, 0x00010000);
    qtest_writeb(qts, 0x1200, 0xa5);
    qtest_writel(qts, 0x82404, 0x1000);
    qtest_writeb(qts, 0x871d2, 1);
    qtest_writeb(qts, 0x8721a, 4);
    qtest_writeb(qts, 0x8240c, 1);
    qtest_writeb(qts, 0x879d2, 8);
    qtest_writeb(qts, 0xc1380, 0x20);
    qtest_writeb(qts, 0xc1384, 1);
    qtest_writew(qts, 0xc1388, 59);
    qtest_writeb(qts, 0xc1280, 2);
    qtest_clock_step(qts, 1000);
    g_assert_cmphex(qtest_readb(qts, 0x1204), ==, 0xa5);
    g_assert_cmphex(qtest_readl(qts, 0x110c), ==, 0x00010000);
    g_assert_cmphex(qtest_readb(qts, 0x871d2), ==, 1);
    g_assert_cmphex(qtest_readb(qts, 0x870d2), ==, 0);
    qtest_writeb(qts, 0x1200, 0x5a);
    qtest_clock_step(qts, 1000);
    g_assert_cmphex(qtest_readb(qts, 0x1204), ==, 0x5a);

    qtest_writeb(qts, 0xc1280, 0);
    qtest_writel(qts, 0x1100, 0x60c80000); /* Original repeat+completion chain. */
    qtest_writel(qts, 0x1104, 0x1210);
    qtest_writel(qts, 0x1108, 0x1300);
    qtest_writel(qts, 0x110c, 0x02020000);
    qtest_writel(qts, 0x1110, 0x01200000); /* Original WBDIS+DISEL chain end. */
    qtest_writel(qts, 0x1114, 0x1220);
    qtest_writel(qts, 0x1118, 0x1221);
    qtest_writel(qts, 0x111c, 0x00010000);
    qtest_writel(qts, 0x1210, 0x12345678);
    qtest_writeb(qts, 0x1220, 0xcc);
    qtest_writeb(qts, 0xc1280, 2);
    qtest_clock_step(qts, 1000);
    g_assert_cmphex(qtest_readl(qts, 0x1300), ==, 0x12345678);
    g_assert_cmphex(qtest_readw(qts, 0x110e), ==, 0x0201);
    g_assert_cmphex(qtest_readb(qts, 0x870d2), ==, 0);
    qtest_clock_step(qts, 1000);
    g_assert_cmphex(qtest_readl(qts, 0x1304), ==, 0x12345678);
    g_assert_cmphex(qtest_readl(qts, 0x1108), ==, 0x1300);
    g_assert_cmphex(qtest_readw(qts, 0x110e), ==, 0x0202);
    g_assert_cmphex(qtest_readb(qts, 0x1221), ==, 0xcc);
    g_assert_cmphex(qtest_readb(qts, 0x870d2), ==, 1);
    qtest_quit(qts);
    unlink(path);
}

static void test_sci10_transmit(void)
{
    g_autofree char *path = test_package(false);
    QTestState *qts = qtest_initf("-machine nikonvr -bios %s", path);

    qtest_writeb(qts, 0x1100, 0x12);
    qtest_writeb(qts, 0x1101, 0x34);
    qtest_writeb(qts, 0x87418, 105);
    qtest_writeb(qts, 0x8741c, 104);
    program_dma(qts, 6, 0x1100, 0xd0043, 0x8000, 2);
    program_dma(qts, 7, 0xd0045, 0x1200, 0x0080, 2);
    qtest_writew(qts, 0x82190, 0x2001);
    qtest_writew(qts, 0x821d0, 0x2001);
    qtest_writeb(qts, 0x82200, 1);
    qtest_writeb(qts, 0xd0040, 0x80);
    qtest_writeb(qts, 0xd0041, 3);
    qtest_writeb(qts, 0xd0046, 0x7a);
    qtest_writeb(qts, 0xd005c, 4);
    g_assert_cmphex(qtest_readb(qts, 0xd005c), ==, 0);
    qtest_writeb(qts, 0xd0042, 0xf0);
    qtest_clock_step(qts, 1); /* Return to the main loop for the TX request. */
    g_assert_cmphex(qtest_readl(qts, 0x82188), ==, 1);
    qtest_clock_step(qts, 10000);
    qtest_clock_step(qts, 10000);
    g_assert_cmphex(qtest_readl(qts, 0x82188), ==, 0);
    g_assert_cmphex(qtest_readb(qts, 0x8219c), ==, 0);
    g_assert_cmphex(qtest_readb(qts, 0xd0044) & 4, ==, 4);
    /* No SCI10 peer is connected: actual TX clocks do not supply RX bytes. */
    g_assert_cmphex(qtest_readl(qts, 0x821c8), ==, 2);
    g_assert_cmphex(qtest_readb(qts, 0xd0044) & 0x40, ==, 0);
    qtest_quit(qts);
    unlink(path);
}

static void test_rspi2_frames(void)
{
    g_autofree char *path = test_package(false);
    QTestState *qts = qtest_initf("-machine nikonvr -bios %s", path);
    unsigned i;

    qtest_writeb(qts, 0xd030b, 0x23); /* SPLW, four buffered frames. */
    qtest_writew(qts, 0xd0310, 0x0600); /* 32-bit command. */
    qtest_writeb(qts, 0xd0300, 0x48);
    for (i = 0; i < 3; i++) {
        qtest_writel(qts, 0xd0304, i + 1);
    }
    qtest_clock_step(qts, 100000);
    g_assert_cmphex(qtest_readb(qts, 0xd0303) & 0x80, ==, 0);
    qtest_writel(qts, 0xd0304, 4);
    for (i = 0; i < 4; i++) {
        qtest_clock_step(qts, 100000);
    }
    g_assert_cmphex(qtest_readb(qts, 0xd0303) & 0xe0, ==, 0xe0);
    for (i = 0; i < 4; i++) {
        /* Upstream SSI idle wire, no claimed sensor receipt. */
        g_assert_cmphex(qtest_readl(qts, 0xd0304), ==, 0);
    }
    g_assert_cmphex(qtest_readb(qts, 0xd0303) & 0x80, ==, 0);
    qtest_quit(qts);
    unlink(path);
}

static void peer_send(int fd, unsigned type, NikonPeerKind kind,
                      uint64_t token, unsigned bits, unsigned line,
                      uint32_t word)
{
    uint8_t frame[NIKON_WIRE_SIZE] = { 0 };

    stl_le_p(frame, NIKON_WIRE_MAGIC);
    frame[NIKON_WIRE_TYPE] = type;
    frame[NIKON_WIRE_KIND] = kind;
    frame[NIKON_WIRE_BITS] = bits;
    stq_le_p(frame + NIKON_WIRE_TOKEN, token);
    stl_le_p(frame + NIKON_WIRE_LINE, line);
    stl_le_p(frame + NIKON_WIRE_WORD, word);
    g_assert_cmpint(send(fd, frame, sizeof(frame), 0), ==, sizeof(frame));
}

static void peer_receive(int fd, unsigned type, NikonPeerKind kind,
                         uint64_t token, unsigned bits, unsigned line,
                         uint32_t word)
{
    uint8_t frame[NIKON_WIRE_SIZE];
    size_t received = 0;
    GPollFD pfd = { .fd = fd, .events = G_IO_IN };
    int64_t deadline = g_get_monotonic_time() + G_TIME_SPAN_SECOND;

    g_test_message("peer receive: type=%u kind=%u token=%" PRIu64
                   " bits=%u line=%u", type, kind, token, bits, line);
    while (received < sizeof(frame)) {
        ssize_t count;
        int64_t remaining = deadline - g_get_monotonic_time();

        g_assert_cmpint(remaining, >, 0);
        g_assert_cmpint(g_poll(&pfd, 1, (remaining + 999) / 1000), ==, 1);
        count = recv(fd, frame + received, sizeof(frame) - received, 0);
        g_assert_cmpint(count, >, 0);
        received += count;
    }
    g_assert_cmphex(ldl_le_p(frame), ==, NIKON_WIRE_MAGIC);
    g_assert_cmphex(frame[NIKON_WIRE_TYPE], ==, type);
    g_assert_cmphex(frame[NIKON_WIRE_KIND], ==, kind);
    g_assert_cmphex(ldq_le_p(frame + NIKON_WIRE_TOKEN), ==, token);
    g_assert_cmphex(ldl_le_p(frame + NIKON_WIRE_LINE), ==, line);
    g_assert_cmphex(ldl_le_p(frame + NIKON_WIRE_WORD), ==, word);
    g_assert_cmphex(frame[NIKON_WIRE_BITS], ==, bits);
}

static void peer_no_reply(int fd)
{
    GPollFD pfd = { .fd = fd, .events = G_IO_IN };

    g_assert_cmpint(g_poll(&pfd, 1, 20), ==, 0);
}

static void peer_wait_pad(QTestState *qts, uint32_t address,
                          unsigned mask, unsigned level)
{
    int64_t deadline = g_get_monotonic_time() + G_TIME_SPAN_SECOND;

    while ((qtest_readb(qts, address) & mask) != level) {
        g_assert_cmpint(g_get_monotonic_time(), <, deadline);
        g_usleep(1000);
    }
}

static void peer_arm(QTestState *qts, uint32_t output,
                     unsigned transmit, unsigned receive)
{
    qtest_writeb(qts, 0x8a0a2, 2); /* Program descriptors before TX is enabled. */
    qtest_writel(qts, 0x1100, output);
    qtest_writeb(qts, 0x87400, 84);
    qtest_writeb(qts, 0x87404, 85);
    program_dma(qts, 0, 0x8a0a5, 0x1200, 0x0080, receive);
    program_dma(qts, 1, 0x1100, 0x8a0a3, 0x8000, transmit);
    qtest_writew(qts, 0x82010, 0x2001);
    qtest_writew(qts, 0x82050, 0x2001);
    qtest_writeb(qts, 0x82200, 1);
    qtest_writeb(qts, 0x8a0a0, 0x80);
    qtest_writeb(qts, 0x8a0a2, 0xf2);
    qtest_clock_step(qts, 1);
}

static void peer_wait_disconnected(QTestState *qts)
{
    int64_t deadline = g_get_monotonic_time() + G_TIME_SPAN_SECOND;
    bool disconnected = false;

    while (!disconnected) {
        QDict *response = qtest_qmp(qts, "{'execute':'query-chardev'}");
        const QListEntry *entry;

        QLIST_FOREACH_ENTRY(qdict_get_qlist(response, "return"), entry) {
            QDict *info = qobject_to(QDict, qlist_entry_obj(entry));

            if (!strcmp(qdict_get_str(info, "label"), "s0")) {
                disconnected = g_str_has_prefix(qdict_get_str(info, "filename"),
                                                "disconnected:");
            }
        }
        qobject_unref(response);
        g_assert_cmpint(g_get_monotonic_time(), <, deadline);
        if (!disconnected) {
            g_usleep(1000);
        }
    }
}

static void test_sci5_peer(void)
{
    g_autofree char *path = test_package(false);
    g_autofree char *args = g_strdup_printf(
        "-machine nikonvr -bios %s -display none "
        "-device nikon-peer,id=wire,chardev=s0 "
        "-device nikon-vr-peer,id=vrpeer,peer=wire,"
        "peripherals=/machine/vr-peripherals", path);
    int fd;
    QTestState *qts = qtest_init_with_serial(args, &fd);

    qtest_irq_intercept_out_named(qts, "/machine/vr-peripherals", "sysbus-irq");

    /* Output direction produces physical GPIO wire levels, not clock replies. */
    qtest_writeb(qts, 0x8c020, 1);
    qtest_writeb(qts, 0x8c000, 1);
    peer_receive(fd, NIKON_WIRE_SIGNAL, NIKON_PEER_GPIO, 0, 0, 0, 1);
    qtest_writeb(qts, 0x8c020, 0);
    peer_receive(fd, NIKON_WIRE_SIGNAL, NIKON_PEER_GPIO, 0, 0, 0, 0);

    qtest_writeb(qts, 0x8c11f, 0);
    qtest_writeb(qts, 0x8c11f, 0x40);
    qtest_writeb(qts, 0x8c167, 0x40);
    qtest_writeb(qts, 0x8c11f, 0x80);
    qtest_writeb(qts, 0x8750f, 12);
    qtest_writeb(qts, 0x8734f, 9);
    qtest_writeb(qts, 0x87209, 0x80);
    peer_send(fd, NIKON_WIRE_SIGNAL, NIKON_PEER_SELECT, 0, 0, 0, 0);
    peer_wait_pad(qts, 0x8c044, 0x80, 0);
    g_assert_cmphex(qtest_readb(qts, 0x8704f), ==, 1);
    g_assert_true(qtest_get_irq(qts, 0));
    qtest_set_irq_in(qts, "/machine/vr-peripherals", "ack", 0, 79);
    g_assert_false(qtest_get_irq(qts, 0));
    peer_send(fd, NIKON_WIRE_REQUEST, NIKON_PEER_CLOCK, 1, 8, 0, 0x31);
    peer_no_reply(fd); /* CS and an unarmed guest cannot produce a byte. */

    peer_send(fd, NIKON_WIRE_CANCEL, NIKON_PEER_CLOCK, 1, 8, 0, 0);
    /* This physical input confirms earlier frames on the same stream ran. */
    peer_send(fd, NIKON_WIRE_SIGNAL, NIKON_PEER_GPIO, 0, 0, 8, 1);
    peer_wait_pad(qts, 0x8c041, 1, 1);
    peer_arm(qts, 0xa5, 1, 1);
    peer_no_reply(fd);
    g_assert_cmphex(qtest_readl(qts, 0x82008), ==, 1);
    g_assert_cmphex(qtest_readl(qts, 0x82048), ==, 0);

    peer_send(fd, NIKON_WIRE_REQUEST, NIKON_PEER_CLOCK, 2, 8, 0, 0x31);
    peer_receive(fd, NIKON_WIRE_REPLY, NIKON_PEER_CLOCK, 2, 8, 0, 0xa5);
    g_assert_cmphex(qtest_readb(qts, 0x1200), ==, 0x31);
    g_assert_cmphex(qtest_readl(qts, 0x82008), ==, 0);

    peer_send(fd, NIKON_WIRE_SIGNAL, NIKON_PEER_SELECT, 0, 0, 0, 1);
    peer_wait_pad(qts, 0x8c044, 0x80, 0x80);
    peer_arm(qts, 0x5a, 1, 1);
    peer_send(fd, NIKON_WIRE_REQUEST, NIKON_PEER_CLOCK, 3, 8, 0, 0x32);
    peer_no_reply(fd);
    g_assert_cmphex(qtest_readl(qts, 0x82008), ==, 1);
    peer_send(fd, NIKON_WIRE_SIGNAL, NIKON_PEER_SELECT, 0, 0, 0, 0);
    peer_receive(fd, NIKON_WIRE_REPLY, NIKON_PEER_CLOCK, 3, 8, 0, 0x5a);
    g_assert_cmphex(qtest_readb(qts, 0x1200), ==, 0x32);

    peer_arm(qts, 0x5aa5, 2, 2);
    peer_send(fd, NIKON_WIRE_REQUEST, NIKON_PEER_CLOCK, 4, 16, 0, 0x4321);
    peer_receive(fd, NIKON_WIRE_REPLY, NIKON_PEER_CLOCK, 4, 16, 0, 0x5aa5);
    g_assert_cmphex(qtest_readw(qts, 0x1200), ==, 0x4321);
    g_assert_cmphex(qtest_readl(qts, 0x82004), ==, 0x1202);
    g_assert_cmphex(qtest_readl(qts, 0x82040), ==, 0x1102);

    /* The second byte waits for TDR refill; cancel cannot reclock the first. */
    peer_arm(qts, 0x6c, 1, 2);
    peer_send(fd, NIKON_WIRE_REQUEST, NIKON_PEER_CLOCK, 5, 16, 0, 0x6543);
    peer_wait_pad(qts, 0x1200, 0xff, 0x43);
    g_assert_cmphex(qtest_readl(qts, 0x82008), ==, 1);
    peer_no_reply(fd);
    peer_send(fd, NIKON_WIRE_CANCEL, NIKON_PEER_CLOCK, 5, 16, 0, 0);
    peer_send(fd, NIKON_WIRE_SIGNAL, NIKON_PEER_GPIO, 0, 0, 8, 0);
    peer_wait_pad(qts, 0x8c041, 1, 0);
    peer_arm(qts, 0x55, 1, 1);
    peer_no_reply(fd);
    g_assert_cmphex(qtest_readl(qts, 0x82008), ==, 1);
    g_assert_cmphex(qtest_readb(qts, 0x1200), ==, 0x43);
    peer_send(fd, NIKON_WIRE_REQUEST, NIKON_PEER_CLOCK, 6, 8, 0, 0x76);
    peer_receive(fd, NIKON_WIRE_REPLY, NIKON_PEER_CLOCK, 6, 8, 0, 0x55);
    g_assert_cmphex(qtest_readb(qts, 0x1200), ==, 0x76);

    peer_send(fd, NIKON_WIRE_REQUEST, NIKON_PEER_CLOCK, 7, 8, 0, 0x33);
    peer_send(fd, NIKON_WIRE_SIGNAL, NIKON_PEER_GPIO, 0, 0, 8, 1);
    peer_wait_pad(qts, 0x8c041, 1, 1);
    peer_no_reply(fd);
    qtest_qmp_assert_success(qts, "{'execute':'system_reset'}");
    qtest_qmp_eventwait(qts, "RESET");
    peer_arm(qts, 0x77, 1, 1);
    peer_no_reply(fd); /* Reset cancelled token 7, no stale clock is retried. */
    peer_send(fd, NIKON_WIRE_REQUEST, NIKON_PEER_CLOCK, 8, 8, 0, 0x33);
    peer_receive(fd, NIKON_WIRE_REPLY, NIKON_PEER_CLOCK, 8, 8, 0, 0x77);
    g_assert_cmphex(qtest_readb(qts, 0x1200), ==, 0x33);

    peer_send(fd, NIKON_WIRE_REQUEST, NIKON_PEER_CLOCK, 9, 8, 0, 0x34);
    peer_send(fd, NIKON_WIRE_SIGNAL, NIKON_PEER_GPIO, 0, 0, 9, 1);
    peer_wait_pad(qts, 0x8c041, 2, 2);
    peer_no_reply(fd);
    close(fd);
    peer_wait_disconnected(qts);
    peer_arm(qts, 0x44, 1, 1);
    g_assert_cmphex(qtest_readl(qts, 0x82008), ==, 1);
    g_assert_cmphex(qtest_readb(qts, 0x1200), ==, 0x33);
    qtest_quit(qts);
    unlink(path);
}

static void test_dma_and_serial(void)
{
    g_autofree char *path = test_package(false);
    QTestState *qts = qtest_initf("-machine nikonvr -bios %s", path);

    qtest_writew(qts, 0x1100, 0x1234);
    qtest_writew(qts, 0x1102, 0xabcd);
    qtest_writew(qts, 0x1200, 0xffff);
    qtest_writew(qts, 0x1202, 0xffff);
    qtest_writeb(qts, 0x87400, 39);
    qtest_writeb(qts, 0x87404, 38);
    program_dma(qts, 0, 0x1100, 0xd0104, 0x8000, 2);
    program_dma(qts, 1, 0xd0104, 0x1200, 0x0080, 2);
    qtest_writeb(qts, 0x82200, 1);
    qtest_writew(qts, 0xd0110, 0x0f03);
    qtest_writeb(qts, 0xd010a, 0);
    qtest_writeb(qts, 0xd0100, 0xe8);
    qtest_clock_step(qts, 1); /* DMA runs after the configuration callback. */
    g_assert_cmphex(qtest_readl(qts, 0x82008), ==, 1); /* Initial empty TDR. */
    g_assert_cmphex(qtest_readl(qts, 0x82048), ==, 2); /* No RX before clock. */
    qtest_clock_step(qts, 100000);
    qtest_clock_step(qts, 100000);
    g_assert_cmphex(qtest_readl(qts, 0x82000), ==, 0x1104);
    g_assert_cmphex(qtest_readl(qts, 0x82044), ==, 0x1204);
    g_assert_cmphex(qtest_readb(qts, 0x8201c), ==, 0);
    g_assert_cmphex(qtest_readb(qts, 0x8205c), ==, 0);
    g_assert_cmphex(qtest_readb(qts, 0x8201e) & 0x11, ==, 0x10);
    g_assert_cmphex(qtest_readb(qts, 0x8205e) & 0x11, ==, 0x10);
    /* These zeros are the explicit SSI idle wire, not a sensor/VR response. */
    g_assert_cmphex(qtest_readw(qts, 0x1200), ==, 0);
    g_assert_cmphex(qtest_readw(qts, 0x1202), ==, 0);

    /* Failed source bus reads cannot complete or fabricate a DMA beat. */
    qtest_writeb(qts, 0xd0100, 8);
    qtest_writeb(qts, 0x8201e, 0);
    program_dma(qts, 0, 0x60000000, 0xd0104, 0x8000, 1);
    qtest_writeb(qts, 0xd0100, 0xe8);
    qtest_clock_step(qts, 1);
    g_assert_cmphex(qtest_readb(qts, 0x8201e) & 0x11, ==, 1);
    g_assert_cmphex(qtest_readl(qts, 0x82008), ==, 1);
    g_assert_cmphex(qtest_readb(qts, 0x8201c), ==, 0);
    qtest_quit(qts);
    unlink(path);
}

static char *test_input_file(const uint8_t *data, size_t size)
{
    g_autoptr(GError) error = NULL;
    char *path = NULL;
    int fd = g_file_open_tmp("vrinput.XXXXXX", &path, &error);

    g_assert_no_error(error);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    g_assert_true(g_file_set_contents(path, (const char *)data, size, &error));
    g_assert_no_error(error);
    return path;
}

static QTestState *test_input_start(const char *package, const char *input,
                                   bool flash)
{
    return qtest_initf("-machine nikonvr -bios %s -device nikon-vr-input,id=input "
                       "-device nikon-vr-span,input=/machine/peripheral/input,"
                       "file=%s,"
                       "guest-base=0x%08x,exact-size=%u,origin=synthetic-fixture,"
                       "qualification=%s,flash-block=%s",
                       package, input, flash ? 0xffff6000 : 0xfffc8008,
                       flash ? 8192 : 4, flash ? "data" : "original-code",
                       flash ? "true" : "false");
}

static void input_set_number(QTestState *qts, const char *property, uint64_t value)
{
    qtest_qmp_assert_success(qts,
        "{'execute':'qom-set','arguments':{'path':'/machine/peripheral/input',"
        "'property':%s,'value':%" PRIu64 "}}", property, value);
}

static bool input_get_bool(QTestState *qts, const char *property)
{
    QDict *reply = qtest_qmp(qts,
        "{'execute':'qom-get','arguments':{'path':'/machine/peripheral/input',"
        "'property':%s}}", property);
    bool value;

    /* assert_success_ref extracts a QDict; qom-get returns a scalar. */
    g_assert_true(qdict_haskey(reply, "return"));
    value = qdict_get_bool(reply, "return");

    qobject_unref(reply);
    return value;
}

static uint64_t input_get_number(QTestState *qts, const char *property)
{
    QDict *reply = qtest_qmp(qts,
        "{'execute':'qom-get','arguments':{'path':'/machine/peripheral/input',"
        "'property':%s}}", property);
    uint64_t value;

    g_assert_true(qdict_haskey(reply, "return"));
    value = qdict_get_int(reply, "return");

    qobject_unref(reply);
    return value;
}

static void input_operation(QTestState *qts, const char *operation, bool success)
{
    QDict *reply = qtest_qmp(qts,
        "{'execute':'qom-set','arguments':{'path':'/machine/peripheral/input',"
        "'property':'operation','value':%s}}", operation);

    if (!qdict_haskey(reply, success ? "return" : "error")) {
        g_autoptr(GString) json = qobject_to_json_pretty(QOBJECT(reply), true);

        g_test_message("VR operation '%s' expected %s; QMP response: %s",
                       operation, success ? "return" : "error", json->str);
    }
    g_assert_true(qdict_haskey(reply, success ? "return" : "error"));
    qobject_unref(reply);
}

static void upload_status_unavailable(QTestState *qts)
{
    QDict *reply = qtest_qmp(qts,
        "{'execute':'qom-get','arguments':{'path':'/machine/peripheral/input',"
        "'property':'upload-eligible-status'}}");

    g_assert_true(qdict_haskey(reply, "error"));
    qobject_unref(reply);
}

static void test_external_span(void)
{
    const uint8_t supplied[] = { 0x91, 0x23, 0x45, 0x67 };
    g_autofree char *package = test_package(false);
    g_autofree char *input = test_input_file(supplied, sizeof(supplied));
    QTestState *qts = test_input_start(package, input, false);

    g_assert_cmphex(qtest_readl(qts, 0xfffc8008), ==, 0x67452391);
    g_assert_false(input_get_bool(qts, "access-failed"));
    input_set_number(qts, "destination", 0xfffc8008);
    input_set_number(qts, "length", 4);
    g_assert_true(input_get_bool(qts, "original-code"));
    input_set_number(qts, "length", 5);
    g_assert_false(input_get_bool(qts, "original-code"));
    /* qtest discards MemTxResult; this confirms the error callback was reached. */
    qtest_readb(qts, 0xfffc8007);
    g_assert_true(input_get_bool(qts, "access-failed"));
    qtest_qmp_assert_success(qts, "{'execute':'system_reset'}");
    qtest_qmp_eventwait(qts, "RESET");
    g_assert_false(input_get_bool(qts, "access-failed"));
    qtest_readb(qts, 0xfffc800c);
    g_assert_true(input_get_bool(qts, "access-failed"));
    qtest_qmp_assert_success(qts, "{'execute':'system_reset'}");
    qtest_qmp_eventwait(qts, "RESET");
    qtest_writel(qts, 0xfffc8008, 0);
    g_assert_true(input_get_bool(qts, "access-failed"));
    g_assert_cmphex(qtest_readl(qts, 0xfffc8008), ==, 0x67452391);
    qtest_quit(qts);
    unlink(input);
    unlink(package);
}

static void test_flash_overlay(void)
{
    g_autofree uint8_t *snapshot = g_malloc(8192);
    uint8_t program[128], readback[128];
    g_autofree char *package = test_package(false);
    g_autofree char *input = NULL;
    QTestState *qts;
    unsigned i;

    for (i = 0; i < 8192; i++) {
        snapshot[i] = (i * 17 + 3) & 255; /* Entire synthetic page, no signature. */
    }
    for (i = 0; i < sizeof(program); i++) {
        program[i] = i ^ 0x96;
    }
    input = test_input_file(snapshot, 8192);
    qts = test_input_start(package, input, true);
    qtest_memread(qts, 0xffff6000, readback, sizeof(readback));
    g_assert_cmpmem(readback, sizeof(readback), snapshot, sizeof(readback));
    input_set_number(qts, "destination", 0xffff6000);
    input_set_number(qts, "length", 128);
    g_assert_false(input_get_bool(qts, "original-code"));
    input_set_number(qts, "token", 1);
    input_set_number(qts, "delay-ns", 1000);
    input_operation(qts, "erase", true);
    g_assert_true(input_get_bool(qts, "busy"));
    g_assert_cmphex(qtest_readb(qts, 0xffff6000), ==, 3);
    qtest_clock_step(qts, 999);
    g_assert_true(input_get_bool(qts, "busy"));
    qtest_clock_step(qts, 1);
    g_assert_false(input_get_bool(qts, "busy"));
    g_assert_cmpuint(input_get_number(qts, "completed-token"), ==, 1);
    g_assert_cmphex(qtest_readb(qts, 0xffff7fff), ==, 0xff);
    qtest_memwrite(qts, 0x1000, program, sizeof(program));
    input_set_number(qts, "token", 2);
    input_set_number(qts, "source-address", 0x1000);
    input_operation(qts, "program", true);
    g_assert_cmphex(qtest_readb(qts, 0xffff6000), ==, 0xff);
    qtest_clock_step(qts, 1000);
    qtest_memread(qts, 0xffff6000, readback, sizeof(readback));
    g_assert_cmpmem(readback, sizeof(readback), program, sizeof(program));
    g_assert_cmpuint(input_get_number(qts, "completed-token"), ==, 2);
    /* Real flash bit constraint and untouched neighbor; no automatic success. */
    memset(program, 0xff, sizeof(program));
    qtest_memwrite(qts, 0x1000, program, sizeof(program));
    input_set_number(qts, "token", 3);
    input_operation(qts, "program", false);
    g_assert_false(input_get_bool(qts, "busy"));
    g_assert_cmpuint(input_get_number(qts, "completed-token"), ==, 2);
    g_assert_cmphex(qtest_readb(qts, 0xffff6080), ==, 0xff);
    qtest_qmp_assert_success(qts, "{'execute':'system_reset'}");
    qtest_qmp_eventwait(qts, "RESET");
    g_assert_cmphex(qtest_readb(qts, 0xffff6000), ==, 0x96);
    qtest_quit(qts);
    /* New instance reads the original snapshot: mutations did not change input. */
    qts = test_input_start(package, input, true);
    qtest_memread(qts, 0xffff6000, readback, sizeof(readback));
    g_assert_cmpmem(readback, sizeof(readback), snapshot, sizeof(readback));
    qtest_quit(qts);
    unlink(input);
    unlink(package);
}

static void test_flash_cancel_and_bounds(void)
{
    uint8_t snapshot[8192], source[128];
    g_autofree char *package = test_package(false);
    g_autofree char *input = NULL;
    QTestState *qts;

    memset(snapshot, 0x24, sizeof(snapshot));
    memset(source, 0, sizeof(source));
    input = test_input_file(snapshot, sizeof(snapshot));
    qts = test_input_start(package, input, true);
    input_set_number(qts, "destination", 0xffff6000);
    input_set_number(qts, "token", 7);
    input_set_number(qts, "delay-ns", 1000);
    input_operation(qts, "erase", true);
    input_set_number(qts, "token", 8);
    input_operation(qts, "cancel", false);
    input_operation(qts, "erase", false); /* Busy producer cannot be replaced. */
    input_set_number(qts, "token", 7);
    input_operation(qts, "cancel", true);
    qtest_clock_step(qts, 1000);
    g_assert_cmphex(qtest_readb(qts, 0xffff6000), ==, 0x24);
    g_assert_cmpuint(input_get_number(qts, "completed-token"), ==, 0);
    input_operation(qts, "erase", true);
    qtest_qmp_assert_success(qts, "{'execute':'system_reset'}");
    qtest_qmp_eventwait(qts, "RESET");
    qtest_clock_step(qts, 1000);
    g_assert_false(input_get_bool(qts, "busy"));
    g_assert_cmphex(qtest_readb(qts, 0xffff6000), ==, 0x24);
    input_set_number(qts, "destination", 0xffff6080);
    input_operation(qts, "erase", false);
    qtest_memwrite(qts, 0x1000, source, sizeof(source));
    input_set_number(qts, "destination", 0xffff7f80);
    input_set_number(qts, "source-address", 0x1000);
    input_set_number(qts, "length", 256); /* Crosses the exact supplied block. */
    input_operation(qts, "program", false);
    input_set_number(qts, "length", 128);
    input_set_number(qts, "source-address", 0xfffc8008); /* Real source bus error. */
    input_operation(qts, "program", false);
    g_assert_true(input_get_bool(qts, "access-failed"));
    g_assert_false(input_get_bool(qts, "busy"));
    input_set_number(qts, "source-address", 0x1000);
    input_operation(qts, "program", true); /* Last valid unit commits real data. */
    qtest_clock_step(qts, 1000);
    g_assert_cmphex(qtest_readb(qts, 0xffff7fff), ==, 0);
    g_assert_cmphex(qtest_readb(qts, 0xffff7f7f), ==, 0x24);
    qtest_quit(qts);
    unlink(input);
    unlink(package);
}

static void upload_capture_bytes(QTestState *qts, uint8_t command,
                                 const uint8_t *record, unsigned size,
                                 bool success)
{
    uint8_t packet[268] = { command, 0, 0x10, 0 };
    unsigned i;

    g_assert_cmpuint(size, <=, sizeof(packet));
    if (record) {
        packet[2] = 8;
        packet[3] = 1;
        memcpy(packet + 4, record, 262);
        for (i = 4; i < 266; i++) {
            packet[267] -= packet[i];
        }
    }
    qtest_memwrite(qts, 0x1587c, packet, size);
    qtest_writel(qts, 0x11904, 0x1587c);
    qtest_writel(qts, 0x82008, 276 - size);
    input_operation(qts, "upload-capture", success);
}

static void upload_capture(QTestState *qts, uint8_t command,
                           const uint8_t *record, bool success)
{
    upload_capture_bytes(qts, command, record,
                         command == 0xf7 ? 268 : 152, success);
}

static void test_upload_staging(void)
{
    const uint8_t supplied[] = { 0x91, 0x23, 0x45, 0x67 };
    uint8_t record[262] = { 4, 1, 4, 0, 0, 1 };
    g_autofree char *package = test_package(true);
    g_autofree char *input = test_input_file(supplied, sizeof(supplied));
    QTestState *qts = test_input_start(package, input, false);

    input_set_number(qts, "length", 3 * 262);
    input_operation(qts, "upload-begin", false); /* Explicit origin required. */
    qtest_qmp_assert_success(qts,
        "{'execute':'qom-set','arguments':{'path':'/machine/peripheral/input',"
        "'property':'upload-origin','value':'synthetic-upload'}}");
    input_operation(qts, "upload-begin", true);
    upload_capture(qts, 0xf5, NULL, false); /* No guest update mode/high CS. */
    qtest_set_irq_in(qts, "/machine/vr-peripherals", "pad", 39, 1);
    qtest_writeb(qts, 0x10f55, 1);
    upload_capture(qts, 0xf5, NULL, true);
    upload_capture(qts, 0xf7, record, false); /* Mode 1 is not resident mode 2. */
    qtest_writeb(qts, 0x10f55, 2);
    upload_capture(qts, 0xfa, NULL, false); /* Incomplete source cannot seal. */
    upload_capture(qts, 0xf7, record, true);
    g_assert_cmpuint(input_get_number(qts, "upload-received"), ==, 262);
    qtest_writeb(qts, 0x1587c + 267,
                 qtest_readb(qts, 0x1587c + 267) ^ 1);
    input_operation(qts, "upload-capture", false); /* Real checksum rejection. */
    g_assert_cmpuint(input_get_number(qts, "upload-received"), ==, 262);
    upload_capture(qts, 0xf7, record, false); /* Metadata is not a body record. */
    stl_le_p(record + 2, 0xffff6000);
    memset(record + 6, 0xa5, 256);
    upload_capture(qts, 0xf7, record, true);
    upload_capture(qts, 0xf7, record, false); /* Repeated address is explicit. */
    stl_le_p(record + 2, 0xffff6200);
    upload_capture(qts, 0xf7, record, false); /* Discontinuous address. */
    stl_le_p(record + 2, 0xffff6100);
    upload_capture(qts, 0xf7, record, true);
    g_assert_cmpuint(input_get_number(qts, "upload-received"), ==, 786);
    upload_capture(qts, 0xf8, NULL, true);
    g_assert_false(input_get_bool(qts, "upload-sealed"));
    upload_capture(qts, 0xfa, NULL, true);
    g_assert_true(input_get_bool(qts, "upload-sealed"));
    /* Staging changes neither external bytes nor DMA/SCI response state. */
    g_assert_cmphex(qtest_readl(qts, 0xfffc8008), ==, 0x67452391);
    g_assert_cmphex(qtest_readb(qts, 0x8201c), ==, 0);
    g_assert_cmphex(qtest_readb(qts, 0x8a0a2), ==, 0);
    input_operation(qts, "upload-cancel", true);
    g_assert_false(input_get_bool(qts, "upload-sealed"));
    g_assert_cmpuint(input_get_number(qts, "upload-received"), ==, 0);
    upload_capture(qts, 0xf7, record, false);
    upload_capture(qts, 0xf5, NULL, true);
    qtest_qmp_assert_success(qts, "{'execute':'system_reset'}");
    qtest_qmp_eventwait(qts, "RESET");
    g_assert_false(input_get_bool(qts, "upload-sealed"));
    g_assert_cmpuint(input_get_number(qts, "upload-received"), ==, 0);
    qtest_quit(qts);
    unlink(input);
    unlink(package);
}

static QTestState *upload_store_start(const char *package, const char *body,
                                     const char *metadata)
{
    return qtest_initf("-machine nikonvr -bios %s "
        "-drive if=none,id=uploadbody,format=raw,file=%s "
        "-drive if=none,id=uploadmetadata,format=raw,file=%s "
        "-device nikon-vr-input,id=input,upload-body-drive=uploadbody,"
        "upload-metadata-drive=uploadmetadata,upload-storage-base=0xfff00000,"
        "upload-storage-size=819200", package, body, metadata);
}

static void upload_store_stage(QTestState *qts, uint32_t destination)
{
    uint8_t record[262] = { 4, 1, 4, 0, 0, 1 };

    input_set_number(qts, "length", 3 * 262);
    qtest_qmp_assert_success(qts,
        "{'execute':'qom-set','arguments':{'path':'/machine/peripheral/input',"
        "'property':'upload-origin','value':'synthetic-persistence'}}");
    input_operation(qts, "upload-begin", true);
    qtest_set_irq_in(qts, "/machine/vr-peripherals", "pad", 39, 1);
    qtest_writeb(qts, 0x10f55, 2);
    upload_capture(qts, 0xf5, NULL, true);
    upload_capture(qts, 0xf7, record, true);
    stl_le_p(record + 2, destination);
    memset(record + 6, 0xa5, 256);
    upload_capture(qts, 0xf7, record, true);
    stl_le_p(record + 2, destination + 256);
    memset(record + 6, 0x3c, 256);
    upload_capture(qts, 0xf7, record, true);
    upload_capture(qts, 0xfa, NULL, true);
}

static void test_upload_persistence(void)
{
    g_autofree uint8_t *expected = g_malloc(819200);
    uint8_t metadata_seed[512], metadata_expected[512] = { 4, 1, 4, 0, 0, 1 };
    g_autofree char *package = test_package(true);
    g_autofree char *body_path = NULL;
    g_autofree char *metadata_path = NULL;
    g_autofree char *body_read = NULL;
    g_autofree char *metadata_read = NULL;
    g_autoptr(GError) error = NULL;
    gsize body_size, metadata_size;
    QTestState *qts;
    int64_t deadline;

    memset(expected, 0x5a, 819200);
    memset(metadata_seed, 0xee, sizeof(metadata_seed));
    memset(metadata_expected + 262, 0xee, sizeof(metadata_expected) - 262);
    body_path = test_input_file(expected, 819200);
    metadata_path = test_input_file(metadata_seed, sizeof(metadata_seed));
    qts = upload_store_start(package, body_path, metadata_path);
    input_set_number(qts, "token", 1);
    input_operation(qts, "upload-persist", false); /* No sealed source. */
    upload_store_stage(qts, 0xfff00000);
    input_operation(qts, "upload-persist", true);
    /* Native file I/O has a host deadline, independent of guest virtual time. */
    deadline = g_get_monotonic_time() + 5 * G_USEC_PER_SEC;
    while (input_get_bool(qts, "busy") && g_get_monotonic_time() < deadline) {
        qtest_clock_step(qts, 1);
        g_usleep(1000);
    }
    g_assert_false(input_get_bool(qts, "busy"));
    g_assert_cmpuint(input_get_number(qts, "persist-completed-token"), ==, 1);
    g_assert_cmpint(input_get_number(qts, "persist-error"), ==, 0);
    qtest_quit(qts);
    memset(expected, 0xa5, 256);
    memset(expected + 256, 0x3c, 256);
    g_assert_true(g_file_get_contents(body_path, &body_read, &body_size, &error));
    g_assert_no_error(error);
    g_assert_cmpmem(body_read, body_size, expected, 819200);
    g_assert_true(g_file_get_contents(metadata_path, &metadata_read,
                                      &metadata_size, &error));
    g_assert_no_error(error);
    g_assert_cmpmem(metadata_read, metadata_size, metadata_expected, 512);

    /* A new instance sees durable bytes. A stream crossing the supplied body
     * boundary is rejected before either metadata or body write is submitted.
     */
    qts = upload_store_start(package, body_path, metadata_path);
    upload_store_stage(qts, 0xfffc7f00);
    input_set_number(qts, "token", 2);
    input_operation(qts, "upload-persist", false);
    g_assert_false(input_get_bool(qts, "busy"));
    g_assert_cmpuint(input_get_number(qts, "persist-completed-token"), ==, 0);
    qtest_quit(qts);
    g_clear_pointer(&body_read, g_free);
    g_clear_pointer(&metadata_read, g_free);
    g_assert_true(g_file_get_contents(body_path, &body_read, &body_size, &error));
    g_assert_no_error(error);
    g_assert_cmpmem(body_read, body_size, expected, 819200);
    g_assert_true(g_file_get_contents(metadata_path, &metadata_read,
                                      &metadata_size, &error));
    g_assert_no_error(error);
    g_assert_cmpmem(metadata_read, metadata_size, metadata_expected, 512);
    unlink(body_path);
    unlink(metadata_path);
    unlink(package);
}

static void upload_wait_store(QTestState *qts)
{
    int64_t deadline = g_get_monotonic_time() + 5 * G_USEC_PER_SEC;

    while (input_get_bool(qts, "busy") && g_get_monotonic_time() < deadline) {
        qtest_clock_step(qts, 1);
        g_usleep(1000);
    }
    g_assert_false(input_get_bool(qts, "busy"));
    g_assert_cmpint(input_get_number(qts, "persist-error"), ==, 0);
}

static void test_upload_record_progress(void)
{
    uint8_t record[262] = { 4, 1, 4, 0, 0, 1 };
    g_autofree uint8_t *expected = g_malloc(819200);
    uint8_t metadata_seed[512], metadata_expected[512] = { 4, 1, 4, 0, 0, 1 };
    g_autofree char *package = test_package(true);
    g_autofree char *body_path = NULL, *metadata_path = NULL;
    g_autofree char *readback = NULL;
    g_autoptr(GError) error = NULL;
    gsize size;
    QTestState *qts;

    memset(expected, 0x5a, 819200);
    memset(metadata_seed, 0xee, sizeof(metadata_seed));
    memset(metadata_expected + 262, 0xee, sizeof(metadata_expected) - 262);
    body_path = test_input_file(expected, 819200);
    metadata_path = test_input_file(metadata_seed, sizeof(metadata_seed));
    qts = qtest_initf("-machine nikonvr -bios %s "
        "-drive if=none,id=uploadbody,format=raw,file=%s "
        "-drive if=none,id=uploadmetadata,format=raw,file=%s "
        "-device nikon-vr-input,id=input,upload-body-drive=uploadbody,"
        "upload-metadata-drive=uploadmetadata,upload-storage-base=0xfff00000,"
        "upload-storage-size=819200,upload-record-persistence=true",
        package, body_path, metadata_path);
    input_set_number(qts, "length", 3 * 262);
    qtest_qmp_assert_success(qts,
        "{'execute':'qom-set','arguments':{'path':'/machine/peripheral/input',"
        "'property':'upload-origin','value':'synthetic-record-progress'}}");
    input_operation(qts, "upload-begin", true);
    upload_status_unavailable(qts);
    qtest_set_irq_in(qts, "/machine/vr-peripherals", "pad", 39, 1);
    qtest_writeb(qts, 0x10f55, 2);
    upload_capture(qts, 0xf5, NULL, true);
    upload_capture(qts, 0xf8, NULL, true); /* Poll while preparing is allowed. */
    upload_wait_store(qts);
    g_assert_true(input_get_bool(qts, "upload-prepared"));
    g_assert_cmpuint(input_get_number(qts, "upload-eligible-status"), ==, 2);
    g_assert_cmpuint(input_get_number(qts, "upload-durable-received"), ==, 0);
    g_assert_true(g_file_get_contents(metadata_path, &readback, &size, &error));
    g_assert_no_error(error);
    g_assert_cmpmem(readback, size, metadata_seed, 512); /* Prepare did not write. */
    g_clear_pointer(&readback, g_free);

    upload_capture(qts, 0xfa, NULL, false); /* Incomplete stream cannot finish. */
    upload_capture(qts, 0xf7, record, true);
    upload_capture(qts, 0xf8, NULL, true);
    upload_wait_store(qts);
    g_assert_cmpuint(input_get_number(qts, "upload-durable-received"), ==, 262);
    g_assert_cmpuint(input_get_number(qts, "upload-eligible-status"), ==, 2);
    g_assert_true(g_file_get_contents(metadata_path, &readback, &size, &error));
    g_assert_no_error(error);
    g_assert_cmpmem(readback, size, metadata_expected, 512);
    g_clear_pointer(&readback, g_free);

    stl_le_p(record + 2, 0xfff00000);
    memset(record + 6, 0xa5, 256);
    upload_capture(qts, 0xf7, record, true);
    upload_capture(qts, 0xf8, NULL, true);
    upload_wait_store(qts);
    g_assert_cmpuint(input_get_number(qts, "upload-durable-received"), ==, 524);
    memset(expected, 0xa5, 256);
    g_assert_true(g_file_get_contents(body_path, &readback, &size, &error));
    g_assert_no_error(error);
    g_assert_cmpmem(readback, size, expected, 819200);
    g_clear_pointer(&readback, g_free);

    stl_le_p(record + 2, 0xfff00100);
    memset(record + 6, 0x3c, 256);
    upload_capture(qts, 0xf7, record, true);
    upload_wait_store(qts);
    g_assert_cmpuint(input_get_number(qts, "upload-durable-received"), ==, 786);
    g_assert_false(input_get_bool(qts, "upload-durable-final"));
    upload_capture(qts, 0xfa, NULL, true);
    upload_capture(qts, 0xf8, NULL, true); /* Final polls need a sealed stream. */
    upload_wait_store(qts);
    g_assert_true(input_get_bool(qts, "upload-durable-final"));
    g_assert_cmpuint(input_get_number(qts, "upload-eligible-status"), ==, 3);
    upload_capture(qts, 0xf8, NULL, true);
    g_assert_cmphex(qtest_readb(qts, 0x8a0a2), ==, 0);
    g_assert_cmphex(qtest_readb(qts, 0x8201c), ==, 0);
    input_operation(qts, "upload-cancel", true);
    g_assert_false(input_get_bool(qts, "upload-prepared"));
    g_assert_false(input_get_bool(qts, "upload-durable-final"));
    g_assert_cmpuint(input_get_number(qts, "upload-durable-received"), ==, 0);
    upload_status_unavailable(qts);
    upload_capture(qts, 0xf5, NULL, true);
    upload_wait_store(qts);
    record[0] = 3;
    upload_capture(qts, 0xf7, record, false); /* Invalid record cannot reuse 2. */
    upload_status_unavailable(qts);
    g_assert_false(input_get_bool(qts, "upload-prepared"));
    upload_capture(qts, 0xf5, NULL, true);
    qtest_qmp_assert_success(qts, "{'execute':'system_reset'}");
    qtest_qmp_eventwait(qts, "RESET");
    g_assert_false(input_get_bool(qts, "upload-prepared"));
    g_assert_false(input_get_bool(qts, "busy"));
    qtest_quit(qts);
    memset(expected + 256, 0x3c, 256);
    g_assert_true(g_file_get_contents(body_path, &readback, &size, &error));
    g_assert_no_error(error);
    g_assert_cmpmem(readback, size, expected, 819200); /* Cancel is not rollback. */
    unlink(body_path);
    unlink(metadata_path);
    unlink(package);
}

static void test_upload_capture_rejection(void)
{
    uint8_t record[262] = { 4, 1, 4, 0, 0, 1 };
    uint8_t metadata_seed[512] = { 0 }, metadata_expected[512] = { 4, 1, 4, 0, 0, 1 };
    g_autofree uint8_t *body = g_malloc0(819200);
    g_autofree char *package = test_package(true);
    g_autofree char *body_path = test_input_file(body, 819200);
    g_autofree char *metadata_path = test_input_file(metadata_seed, 512);
    g_autofree char *readback = NULL;
    g_autoptr(GError) error = NULL;
    gsize size;
    unsigned attempt;
    QTestState *qts = qtest_initf("-machine nikonvr -bios %s "
        "-drive if=none,id=uploadbody,format=raw,file=%s "
        "-drive if=none,id=uploadmetadata,format=raw,file=%s "
        "-device nikon-vr-input,id=input,upload-body-drive=uploadbody,"
        "upload-metadata-drive=uploadmetadata,upload-storage-base=0xfff00000,"
        "upload-storage-size=819200,upload-record-persistence=true",
        package, body_path, metadata_path);

    input_set_number(qts, "length", 3 * 262);
    qtest_qmp_assert_success(qts,
        "{'execute':'qom-set','arguments':{'path':'/machine/peripheral/input',"
        "'property':'upload-origin','value':'synthetic-capture-rejection'}}");
    input_operation(qts, "upload-begin", true);
    for (attempt = 0; attempt < 4; attempt++) {
        record[0] = 4;
        record[1] = 1;
        record[2] = 4;
        record[3] = record[4] = 0;
        record[5] = 1;
        memset(record + 6, 0, 256);
        qtest_set_irq_in(qts, "/machine/vr-peripherals", "pad", 39, 1);
        qtest_writeb(qts, 0x10f55, 2);
        upload_capture(qts, 0xf5, NULL, true);
        upload_wait_store(qts);
        upload_capture(qts, 0xf7, record, true);
        upload_wait_store(qts);
        g_assert_cmpuint(input_get_number(qts, "upload-eligible-status"), ==, 2);
        /* A rejected short poll, or an empty ring containing a stale F7,
         * is not a new chunk and must leave the previous eligibility alone.
         */
        upload_capture_bytes(qts, 0xf8, NULL, 151, false);
        g_assert_cmpuint(input_get_number(qts, "upload-eligible-status"), ==, 2);
        qtest_writeb(qts, 0x1587c, 0xf7);
        qtest_writel(qts, 0x82008, 276);
        input_operation(qts, "upload-capture", false);
        g_assert_cmpuint(input_get_number(qts, "upload-eligible-status"), ==, 2);

        stl_le_p(record + 2, 0xfff00000);
        memset(record + 6, 0xa5, 256);
        if (attempt == 2) {
            qtest_writeb(qts, 0x10f55, 1);
        } else if (attempt == 3) {
            qtest_set_irq_in(qts, "/machine/vr-peripherals", "pad", 39, 0);
        }
        upload_capture_bytes(qts, 0xf7, record,
                             attempt == 0 ? 267 : attempt == 1 ? 1 : 268,
                             false);
        g_assert_false(input_get_bool(qts, "upload-prepared"));
        g_assert_cmpuint(input_get_number(qts, "upload-received"), ==, 262);
        g_assert_cmpuint(input_get_number(qts, "upload-durable-received"), ==, 262);
        qtest_writeb(qts, 0x10f55, 2);
        qtest_set_irq_in(qts, "/machine/vr-peripherals", "pad", 39, 1);
        upload_capture(qts, 0xf8, NULL, true);
        upload_status_unavailable(qts);
    }
    qtest_quit(qts);
    g_assert_true(g_file_get_contents(body_path, &readback, &size, &error));
    g_assert_no_error(error);
    g_assert_cmpmem(readback, size, body, 819200);
    g_clear_pointer(&readback, g_free);
    g_assert_true(g_file_get_contents(metadata_path, &readback, &size, &error));
    g_assert_no_error(error);
    g_assert_cmpmem(readback, size, metadata_expected, 512);
    unlink(body_path);
    unlink(metadata_path);
    unlink(package);
}

static void test_sci5_peer_attach(void)
{
    g_autofree char *path = test_package(false);
    g_autofree char *args = g_strdup_printf(
        "-machine nikonvr -bios %s -display none "
        "-device nikon-peer,id=wire,chardev=s0 "
        "-device nikon-vr-peer,id=vrpeer,peer=wire,"
        "peripherals=/machine/vr-peripherals", path);
    int fd;
    QTestState *qts = qtest_init_with_serial(args, &fd);
    QDict *reply, *failure;

    qtest_qmp_assert_success(qts,
        "{'execute':'qom-set','arguments':{'path':'/machine/peripheral/vrpeer',"
        "'property':'realized','value':false}}");
    peer_send(fd, NIKON_WIRE_REQUEST, NIKON_PEER_CLOCK, 1, 8, 0, 0x31);
    peer_no_reply(fd);
    reply = qtest_qmp(qts,
        "{'execute':'qom-set','arguments':{'path':'/machine/peripheral/vrpeer',"
        "'property':'realized','value':true}}");
    failure = qdict_get_qdict(reply, "error");
    g_assert_nonnull(failure);
    g_assert_cmpstr(qdict_get_str(failure, "desc"), ==,
                    "VR SCI endpoint cannot attach during an incoming request");
    qobject_unref(reply);
    peer_send(fd, NIKON_WIRE_CANCEL, NIKON_PEER_CLOCK, 1, 8, 0, 0);
    peer_no_reply(fd);
    qtest_qmp_assert_success(qts,
        "{'execute':'qom-set','arguments':{'path':'/machine/peripheral/vrpeer',"
        "'property':'realized','value':true}}");
    /* Successful re-attachment proves the failed realize released both the
     * transport state hook and the peripheral GPIO observer.
     */
    qtest_writeb(qts, 0x8c020, 1);
    qtest_writeb(qts, 0x8c000, 1);
    peer_receive(fd, NIKON_WIRE_SIGNAL, NIKON_PEER_GPIO, 0, 0, 0, 1);
    peer_send(fd, NIKON_WIRE_SIGNAL, NIKON_PEER_SELECT, 0, 0, 0, 0);
    peer_wait_pad(qts, 0x8c044, 0x80, 0);
    peer_arm(qts, 0xa5, 1, 1);
    peer_no_reply(fd); /* The rejected raw token was never re-clocked. */
    peer_send(fd, NIKON_WIRE_REQUEST, NIKON_PEER_CLOCK, 2, 8, 0, 0x32);
    peer_receive(fd, NIKON_WIRE_REPLY, NIKON_PEER_CLOCK, 2, 8, 0, 0xa5);
    g_assert_cmphex(qtest_readb(qts, 0x1200), ==, 0x32);
    close(fd);
    qtest_quit(qts);
    unlink(path);
}

static uint64_t update_frames(QTestState *qts)
{
    QDict *reply = qtest_qmp(qts,
        "{'execute':'qom-get','arguments':{'path':'/machine/peripheral/update',"
        "'property':'frames'}}");
    uint64_t frames;

    g_assert_true(qdict_haskey(reply, "return"));
    frames = qdict_get_int(reply, "return");
    qobject_unref(reply);
    return frames;
}

static void update_transfer(QTestState *qts, int fd, uint64_t *token,
                            uint64_t *frames, uint8_t command,
                            const uint8_t *record, bool corrupt,
                            unsigned bits, uint8_t status)
{
    uint8_t packet[268] = { command, 0, command == 0xf7 ? 8 : 0x10,
                           command == 0xf7 ? 1 : 0 };
    unsigned length = command == 0xf7 ? 268 : 152;
    unsigned i, bytes = bits / 8;
    int64_t deadline;

    if (command == 0xf7) {
        uint8_t sum = 0;

        memcpy(packet + 4, record, 262);
        for (i = 0; i < 262; i++) {
            sum += record[i];
        }
        packet[267] = -sum;
        if (corrupt) {
            packet[267] ^= 1;
        }
    }
    g_assert_true(bits == 8 || bits == 16);
    peer_send(fd, NIKON_WIRE_SIGNAL, NIKON_PEER_SELECT, 0, 0, 0, 0);
    for (i = 0; i < length; i += bytes) {
        uint32_t word = bits == 16 ? lduw_le_p(packet + i) : packet[i];
        uint32_t reply = bits == 16 ? 0xffff : 0xff;

        if (!i) {
            reply = bits == 16 ? 0xff00 | status : status;
        }
        peer_send(fd, NIKON_WIRE_REQUEST, NIKON_PEER_CLOCK, ++*token,
                  bits, 0, word);
        peer_receive(fd, NIKON_WIRE_REPLY, NIKON_PEER_CLOCK, *token,
                     bits, 0, reply);
    }
    peer_send(fd, NIKON_WIRE_SIGNAL, NIKON_PEER_SELECT, 0, 0, 0, 1);
    ++*frames;
    deadline = g_get_monotonic_time() + G_TIME_SPAN_SECOND;
    while (update_frames(qts) != *frames) {
        g_assert_cmpint(g_get_monotonic_time(), <, deadline);
        g_usleep(1000);
    }
}

static void test_update_peer(void)
{
    uint8_t record[262] = { 4, 1, 4, 0, 0, 1 };
    uint8_t metadata_seed[512], metadata_expected[512] = { 4, 1, 4, 0, 0, 1 };
    g_autofree uint8_t *expected = g_malloc(819200);
    g_autofree char *package = NULL;
    g_autofree char *body_path = NULL, *metadata_path = NULL, *args = NULL;
    g_autofree char *readback = NULL;
    g_autoptr(GError) error = NULL;
    QTestState *qts;
    uint64_t token = UINT64_C(0x100000000), frames = 0;
    gsize size;
    int fd;

    if (g_test_subprocess()) {
        QTestState *child = qtest_initf("-machine nikonvr -bios %s "
            "-drive if=none,id=uploadbody,format=raw,file=%s "
            "-drive if=none,id=uploadmetadata,format=raw,file=%s "
            "-chardev null,id=s0 -chardev null,id=s1 "
            "-device nikon-peer,id=wire,chardev=s0 "
            "-device nikon-peer,id=wire2,chardev=s1 "
            "-device nikon-vr-input,id=input,upload-body-drive=uploadbody,"
            "upload-metadata-drive=uploadmetadata,upload-storage-base=0xfff00000,"
            "upload-storage-size=819200,upload-record-persistence=true "
            "-device nikon-update-peer,id=update,peer=/machine/peripheral/wire,"
            "input=/machine/peripheral/input,stream-size=786,origin=first-owner "
            "-device nikon-update-peer,id=other,peer=/machine/peripheral/wire2,"
            "input=/machine/peripheral/input,stream-size=786,origin=second-owner",
            g_getenv("NIKON_UPDATE_PACKAGE"), g_getenv("NIKON_UPDATE_BODY"),
            g_getenv("NIKON_UPDATE_METADATA"));

        qtest_quit(child);
        return;
    }

    package = test_package(false);
    memset(expected, 0x5a, 819200);
    memset(metadata_seed, 0xee, sizeof(metadata_seed));
    memset(metadata_expected + 262, 0xee, sizeof(metadata_expected) - 262);
    body_path = test_input_file(expected, 819200);
    metadata_path = test_input_file(metadata_seed, sizeof(metadata_seed));
    args = g_strdup_printf("-machine nikonvr -bios %s "
        "-drive if=none,id=uploadbody,format=raw,file=%s "
        "-drive if=none,id=uploadmetadata,format=raw,file=%s "
        "-device nikon-peer,id=wire,chardev=s0 "
        "-device nikon-vr-input,id=input,upload-body-drive=uploadbody,"
        "upload-metadata-drive=uploadmetadata,upload-storage-base=0xfff00000,"
        "upload-storage-size=819200,upload-record-persistence=true "
        "-device nikon-update-peer,id=update,peer=/machine/peripheral/wire,"
        "input=/machine/peripheral/input,"
        "stream-size=786,origin=synthetic-update-wire",
        package, body_path, metadata_path);
    qts = qtest_init_with_serial(args, &fd);

    /* This exclusive native owner cannot be replaced by guest-capture/QMP. */
    input_operation(qts, "upload-begin", false);
    input_operation(qts, "upload-capture", false);
    input_operation(qts, "upload-cancel", false);
    /* Ordinary QOM lifecycle: a bare peer may already have an incoming token.
     * Failed attachment must release both claims, without consuming the token.
     */
    qtest_qmp_assert_success(qts,
        "{'execute':'qom-set','arguments':{'path':'/machine/peripheral/update',"
        "'property':'realized','value':false}}");
    peer_send(fd, NIKON_WIRE_REQUEST, NIKON_PEER_CLOCK, ++token, 16, 0, 0x00f5);
    peer_no_reply(fd);
    {
        QDict *reply = qtest_qmp(qts,
            "{'execute':'qom-set','arguments':{'path':'/machine/peripheral/update',"
            "'property':'realized','value':true}}");
        QDict *failure = qdict_get_qdict(reply, "error");

        g_assert_nonnull(failure);
        g_assert_cmpstr(qdict_get_str(failure, "desc"), ==,
                        "Update endpoint cannot attach during an incoming request");
        qobject_unref(reply);
    }
    input_operation(qts, "upload-cancel", true); /* No leaked input owner. */
    peer_send(fd, NIKON_WIRE_CANCEL, NIKON_PEER_CLOCK, token, 16, 0, 0);
    peer_no_reply(fd);
    qtest_qmp_assert_success(qts,
        "{'execute':'qom-set','arguments':{'path':'/machine/peripheral/update',"
        "'property':'realized','value':true}}");
    input_operation(qts, "upload-cancel", false); /* Re-attached owner is live. */
    update_transfer(qts, fd, &token, &frames, 0xf5, NULL, false, 16, 1);
    upload_wait_store(qts);
    g_assert_true(input_get_bool(qts, "upload-prepared"));
    update_transfer(qts, fd, &token, &frames, 0xf8, NULL, false, 8, 2);
    g_assert_cmpuint(input_get_number(qts, "upload-durable-received"), ==, 0);

    update_transfer(qts, fd, &token, &frames, 0xf7, record, false, 16, 2);
    upload_wait_store(qts);
    g_assert_cmpuint(input_get_number(qts, "upload-durable-received"), ==, 262);
    g_assert_true(g_file_get_contents(metadata_path, &readback, &size, &error));
    g_assert_no_error(error);
    g_assert_cmpmem(readback, size, metadata_expected, sizeof(metadata_expected));
    g_clear_pointer(&readback, g_free);
    update_transfer(qts, fd, &token, &frames, 0xf8, NULL, false, 16, 2);

    stl_le_p(record + 2, 0xfff00000);
    memset(record + 6, 0xa5, 256);
    update_transfer(qts, fd, &token, &frames, 0xf7, record, false, 16, 2);
    upload_wait_store(qts);
    g_assert_cmpuint(input_get_number(qts, "upload-durable-received"), ==, 524);
    memset(expected, 0xa5, 256);
    g_assert_true(g_file_get_contents(body_path, &readback, &size, &error));
    g_assert_no_error(error);
    g_assert_cmpmem(readback, size, expected, 819200);
    g_clear_pointer(&readback, g_free);
    update_transfer(qts, fd, &token, &frames, 0xf8, NULL, false, 16, 2);

    stl_le_p(record + 2, 0xfff00100);
    memset(record + 6, 0x3c, 256);
    update_transfer(qts, fd, &token, &frames, 0xf7, record, false, 16, 2);
    upload_wait_store(qts);
    g_assert_cmpuint(input_get_number(qts, "upload-durable-received"), ==, 786);
    update_transfer(qts, fd, &token, &frames, 0xf8, NULL, false, 16, 2);
    update_transfer(qts, fd, &token, &frames, 0xfa, NULL, false, 16, 2);
    upload_wait_store(qts);
    g_assert_true(input_get_bool(qts, "upload-durable-final"));
    update_transfer(qts, fd, &token, &frames, 0xf8, NULL, false, 16, 3);

    /* Restart, then reject an invalid F7 instead of reusing old eligible 2. */
    update_transfer(qts, fd, &token, &frames, 0xf5, NULL, false, 16, 3);
    upload_wait_store(qts);
    update_transfer(qts, fd, &token, &frames, 0xf7, record, true, 16, 2);
    g_assert_false(input_get_bool(qts, "upload-prepared"));
    upload_status_unavailable(qts);
    update_transfer(qts, fd, &token, &frames, 0xf8, NULL, false, 16, 1);

    update_transfer(qts, fd, &token, &frames, 0xf5, NULL, false, 16, 1);
    upload_wait_store(qts);
    /* A clock with inactive physical CS remains uncompleted. Cancel clears
     * the stage and cannot deliver a late byte into the next CS interval.
     */
    peer_send(fd, NIKON_WIRE_REQUEST, NIKON_PEER_CLOCK, ++token, 16, 0, 0x00f7);
    peer_no_reply(fd);
    peer_send(fd, NIKON_WIRE_CANCEL, NIKON_PEER_CLOCK, token, 16, 0, 0);
    peer_no_reply(fd);
    g_assert_false(input_get_bool(qts, "upload-prepared"));
    upload_status_unavailable(qts);

    update_transfer(qts, fd, &token, &frames, 0xf5, NULL, false, 16, 1);
    upload_wait_store(qts);
    peer_send(fd, NIKON_WIRE_SIGNAL, NIKON_PEER_SELECT, 0, 0, 0, 0);
    peer_send(fd, NIKON_WIRE_REQUEST, NIKON_PEER_CLOCK, ++token, 16, 0, 0x00f7);
    peer_receive(fd, NIKON_WIRE_REPLY, NIKON_PEER_CLOCK, token, 16, 0, 0xff02);
    qtest_qmp_assert_success(qts, "{'execute':'system_reset'}");
    qtest_qmp_eventwait(qts, "RESET");
    frames = 0;
    peer_no_reply(fd);
    g_assert_false(input_get_bool(qts, "upload-prepared"));
    g_assert_false(input_get_bool(qts, "busy"));
    update_transfer(qts, fd, &token, &frames, 0xf5, NULL, false, 16, 1);
    upload_wait_store(qts);
    peer_send(fd, NIKON_WIRE_SIGNAL, NIKON_PEER_SELECT, 0, 0, 0, 0);
    peer_send(fd, NIKON_WIRE_REQUEST, NIKON_PEER_CLOCK, ++token, 16, 0, 0x00f7);
    peer_receive(fd, NIKON_WIRE_REPLY, NIKON_PEER_CLOCK, token, 16, 0, 0xff02);
    close(fd);
    peer_wait_disconnected(qts);
    g_assert_false(input_get_bool(qts, "upload-prepared"));
    g_assert_false(input_get_bool(qts, "busy"));
    g_assert_cmphex(qtest_readb(qts, 0x8a0a2), ==, 0);
    g_assert_cmphex(qtest_readb(qts, 0x8201c), ==, 0);
    qtest_quit(qts);

    memset(expected + 256, 0x3c, 256);
    g_assert_true(g_file_get_contents(body_path, &readback, &size, &error));
    g_assert_no_error(error);
    g_assert_cmpmem(readback, size, expected, 819200);
    g_clear_pointer(&readback, g_free);
    g_assert_true(g_file_get_contents(metadata_path, &readback, &size, &error));
    g_assert_no_error(error);
    g_assert_cmpmem(readback, size, metadata_expected, sizeof(metadata_expected));
    g_setenv("NIKON_UPDATE_PACKAGE", package, true);
    g_setenv("NIKON_UPDATE_BODY", body_path, true);
    g_setenv("NIKON_UPDATE_METADATA", metadata_path, true);
    g_test_trap_subprocess(NULL, 5 * G_USEC_PER_SEC, 0);
    g_test_trap_assert_failed();
    g_test_trap_assert_stderr("*unused realized per-record input*");
    g_test_trap_assert_stderr_unmatched("*Assertion*");
    g_unsetenv("NIKON_UPDATE_PACKAGE");
    g_unsetenv("NIKON_UPDATE_BODY");
    g_unsetenv("NIKON_UPDATE_METADATA");
    unlink(package);
    unlink(body_path);
    unlink(metadata_path);
}

static void test_external_conflict(void)
{
    if (g_test_subprocess()) {
        const char *package = g_getenv("NIKON_VR_FIXTURE_PACKAGE");
        const char *input = g_getenv("NIKON_VR_FIXTURE_INPUT");
        QTestState *qts = qtest_initf(
            "-machine nikonvr -bios %s -device nikon-vr-input,id=input "
            "-device nikon-vr-span,input=/machine/peripheral/input,"
            "file=%s,guest-base=0xfffc8008,"
            "exact-size=4,origin=fixture-one,qualification=data "
            "-device nikon-vr-span,input=/machine/peripheral/input,"
            "file=%s,guest-base=0xfffc800a,"
            "exact-size=4,origin=fixture-two,qualification=data",
            package, input, input);

        qtest_quit(qts);
        return;
    }
    {
        const uint8_t data[] = { 0xa1, 0xb2, 0xc3, 0xd4 };
        g_autofree char *package = test_package(false);
        g_autofree char *input = test_input_file(data, sizeof(data));

        g_setenv("NIKON_VR_FIXTURE_PACKAGE", package, true);
        g_setenv("NIKON_VR_FIXTURE_INPUT", input, true);
        g_test_trap_subprocess(NULL, 5 * G_USEC_PER_SEC, 0);
        g_test_trap_assert_failed();
        g_test_trap_assert_stderr("*VR external span conflicts with source 'fixture-one'*");
        g_unsetenv("NIKON_VR_FIXTURE_PACKAGE");
        g_unsetenv("NIKON_VR_FIXTURE_INPUT");
        unlink(input);
        unlink(package);
    }
}

static void test_external_rejection(void)
{
    static const struct {
        const char *properties, *error;
    } cases[] = {
        { "guest-base=0xfffc8008,exact-size=5,origin=fixture,qualification=data",
          "*has 4 bytes, expected 5*" },
        { "guest-base=0xfffffffe,exact-size=4,origin=fixture,qualification=data",
          "*VR span exceeds the external 32-bit address domain*" },
        { "guest-base=0xfffc8008,exact-size=4,origin=,qualification=data",
          "*source origin and explicit qualification*" },
        { "guest-base=0xfffc8008,exact-size=4,origin=fixture,qualification=unknown",
          "*VR span qualification must be data or original-code*" },
        { "guest-base=0xffff6000,exact-size=4,origin=fixture,qualification=data,flash-block=true",
          "*complete source-observed 8 KiB block snapshot*" },
    };
    unsigned i;

    if (g_test_subprocess()) {
        QTestState *qts = qtest_initf(
            "-machine nikonvr -bios %s -device nikon-vr-input,id=input "
            "-device nikon-vr-span,input=/machine/peripheral/input,"
            "file=%s,%s",
            g_getenv("NIKON_VR_FIXTURE_PACKAGE"),
            g_getenv("NIKON_VR_FIXTURE_INPUT"),
            g_getenv("NIKON_VR_FIXTURE_PROPERTIES"));

        qtest_quit(qts);
        return;
    }
    {
        const uint8_t data[] = { 1, 2, 3, 4 };
        g_autofree char *package = test_package(false);
        g_autofree char *input = test_input_file(data, sizeof(data));

        g_setenv("NIKON_VR_FIXTURE_PACKAGE", package, true);
        g_setenv("NIKON_VR_FIXTURE_INPUT", input, true);
        for (i = 0; i < ARRAY_SIZE(cases); i++) {
            g_setenv("NIKON_VR_FIXTURE_PROPERTIES", cases[i].properties, true);
            g_test_trap_subprocess(NULL, 5 * G_USEC_PER_SEC, 0);
            g_test_trap_assert_failed();
            g_test_trap_assert_stderr(cases[i].error);
        }
        g_unsetenv("NIKON_VR_FIXTURE_PACKAGE");
        g_unsetenv("NIKON_VR_FIXTURE_INPUT");
        g_unsetenv("NIKON_VR_FIXTURE_PROPERTIES");
        unlink(input);
        unlink(package);
    }
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/nikon/vr/update-peer", test_update_peer);
    qtest_add_func("/nikon/vr/sci5-peer-attach", test_sci5_peer_attach);
    qtest_add_func("/nikon/vr/upload-staging", test_upload_staging);
    qtest_add_func("/nikon/vr/upload-persistence", test_upload_persistence);
    qtest_add_func("/nikon/vr/upload-record-progress", test_upload_record_progress);
    qtest_add_func("/nikon/vr/upload-capture-rejection", test_upload_capture_rejection);
    qtest_add_func("/nikon/vr/external-span", test_external_span);
    qtest_add_func("/nikon/vr/flash-overlay", test_flash_overlay);
    qtest_add_func("/nikon/vr/flash-cancel-bounds", test_flash_cancel_and_bounds);
    qtest_add_func("/nikon/vr/external-conflict", test_external_conflict);
    qtest_add_func("/nikon/vr/external-rejection", test_external_rejection);
    qtest_add_func("/nikon/vr/gpio-irq", test_gpio_and_irq);
    qtest_add_func("/nikon/vr/cmt", test_cmt);
    qtest_add_func("/nikon/vr/dma-serial", test_dma_and_serial);
    qtest_add_func("/nikon/vr/counter-events", test_counter_events);
    qtest_add_func("/nikon/vr/adc-input", test_adc_input);
    qtest_add_func("/nikon/vr/dtc-chain", test_dtc_chain);
    qtest_add_func("/nikon/vr/sci10-transmit", test_sci10_transmit);
    qtest_add_func("/nikon/vr/rspi2-frames", test_rspi2_frames);
    qtest_add_func("/nikon/vr/sci5-peer", test_sci5_peer);
    return g_test_run();
}
