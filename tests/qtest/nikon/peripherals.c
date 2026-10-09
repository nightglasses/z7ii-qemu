/* Native qtest for VR register, clock and DMA protocols. No Nikon code. */
/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "libqtest.h"

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

static char *test_package(void)
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
    g_autofree char *path = test_package();
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
    g_autofree char *path = test_package();
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
    g_autofree char *path = test_package();
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
    g_autofree char *path = test_package();
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

static void test_dma_and_serial(void)
{
    g_autofree char *path = test_package();
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
    g_assert_cmphex(qtest_readb(qts, 0x8201e) & 0x11, ==, 1);
    g_assert_cmphex(qtest_readl(qts, 0x82008), ==, 1);
    g_assert_cmphex(qtest_readb(qts, 0x8201c), ==, 0);
    qtest_quit(qts);
    unlink(path);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/nikon/vr/gpio-irq", test_gpio_and_irq);
    qtest_add_func("/nikon/vr/cmt", test_cmt);
    qtest_add_func("/nikon/vr/dma-serial", test_dma_and_serial);
    qtest_add_func("/nikon/vr/counter-events", test_counter_events);
    qtest_add_func("/nikon/vr/adc-input", test_adc_input);
    return g_test_run();
}
