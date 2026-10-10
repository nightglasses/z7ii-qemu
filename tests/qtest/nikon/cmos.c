/* Guest-configured CMOS transport fixture; values are not Nikon mode facts.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "libqtest.h"
#include "hw/nikon/cmos.h"

#define CMOS 0x24080000
#define RAM 0xb1810000

static char *image;

static QTestState *start(void)
{
    g_autofree char *bios = g_shell_quote(image);

    QTestState *q = qtest_initf("-M z7ii-mas-diagnostic -smp 1 -bios %s "
                                "-nodefaults -display none", bios);

    qtest_irq_intercept_out_named(q, "/machine/cmos", "sysbus-irq");
    return q;
}

static void configure(QTestState *q, unsigned width, unsigned height,
                      unsigned bytes_per_pixel, unsigned stride)
{
    qtest_writel(q, CMOS + 0x004, width);
    qtest_writel(q, CMOS + 0x008, height);
    qtest_writel(q, CMOS + 0x00c, 0);
    qtest_writel(q, CMOS + 0x010, 0);
    qtest_writel(q, CMOS + 0x014, width);
    qtest_writel(q, CMOS + 0x018, height);
    qtest_writel(q, CMOS + 0x01c, 1U << 16);
    qtest_writel(q, CMOS + 0x020, 1U << 16);
    qtest_writel(q, CMOS + 0x024, 0x12345678); /* opaque format word */
    qtest_writel(q, CMOS + 0x094, 0x1badb002); /* transport seed */
    qtest_writel(q, CMOS + 0x028, bytes_per_pixel);
    qtest_writel(q, CMOS + 0x02c, stride);
    qtest_writel(q, CMOS + 0x030, 2);
    qtest_writel(q, CMOS + 0x038, RAM);
    qtest_writel(q, CMOS + 0x03c, RAM + 0x10000);
    qtest_writel(q, CMOS + 0x048, 10);
    qtest_writel(q, CMOS + 0x058, 10);
}

static void test_configured_frames(void)
{
    static const struct {
        unsigned width, height, bytes_per_pixel, stride;
    } modes[] = {
        { 8, 8, 4, 48 }, /* transport regression fixture */
        { 11, 5, 2, 32 },
        { 13, 3, 1, 16 },
    };
    QTestState *q = start();
    uint8_t first[8 * 4], second[8 * 4];
    unsigned i;

    for (i = 0; i < ARRAY_SIZE(modes); i++) {
        unsigned bytes = modes[i].width * modes[i].bytes_per_pixel;
        unsigned active = i & 1;
        uint32_t destination = RAM + active * 0x10000;

        configure(q, modes[i].width, modes[i].height,
                  modes[i].bytes_per_pixel, modes[i].stride);
        qtest_memset(q, destination, 0xa5,
                     modes[i].stride * modes[i].height);
        qtest_writel(q, CMOS + 0x060, 1);
        g_assert_cmphex(qtest_readl(q, CMOS + 0x068), ==, NIKON_CMOS_EXPOSING);
        g_assert_cmphex(qtest_readb(q, destination), ==, 0xa5);
        qtest_clock_step(q, 10000);
        g_assert_cmphex(qtest_readl(q, CMOS + 0x068), ==, NIKON_CMOS_DONE);
        g_assert_cmphex(qtest_readl(q, CMOS + 0x078), ==, i + 1);
        g_assert_cmphex(qtest_readl(q, CMOS + 0x07c), ==, (i + 1) & 1);
        g_assert_cmphex(qtest_readb(q, destination + bytes), ==, 0xa5);
        g_assert_cmphex(qtest_readb(q, destination), !=, 0xa5);
        if (i == 0) {
            qtest_memread(q, destination, first, sizeof(first));
        } else if (i == 1) {
            qtest_memread(q, destination, second, sizeof(second));
        }
    }
    g_assert_cmpint(memcmp(first, second, sizeof(second)), !=, 0);
    g_assert_cmphex(qtest_readl(q, CMOS + 0x070), ==, NIKON_CMOS_EVENT_DONE);
    qtest_writel(q, CMOS + 0x06c, NIKON_CMOS_EVENT_DONE);
    g_assert_true(qtest_get_irq(q, 0));
    qtest_writel(q, CMOS + 0x074, NIKON_CMOS_EVENT_DONE);
    g_assert_false(qtest_get_irq(q, 0));
    g_assert_cmphex(qtest_readl(q, CMOS + 0x070), ==, 0);
    qtest_quit(q);
}

static void test_crop_scale_and_unknown(void)
{
    QTestState *q = start();
    uint8_t cropped[6], full[12];

    configure(q, 6, 4, 1, 16);
    qtest_writel(q, CMOS + 0x100, 0xdeadbeef);
    qtest_writel(q, CMOS + 0x104, 0x0000abcd);
    qtest_writel(q, CMOS + 0x080, 0x13); /* unknown Bayer selector */
    qtest_writel(q, CMOS + 0x084, 0x19); /* unknown bit-depth word */
    qtest_writel(q, CMOS + 0x088, 0x37); /* unknown packing word */
    qtest_writel(q, CMOS + 0x08c, 0xb1817654); /* opaque DMA source */
    qtest_writel(q, CMOS + 0x090, 0xb1814321); /* opaque DMA destination */
    g_assert_cmphex(qtest_readl(q, CMOS + 0x100), ==, 0xdeadbeef);
    g_assert_cmphex(qtest_readl(q, CMOS + 0x104), ==, 0x0000abcd);
    g_assert_cmphex(qtest_readl(q, CMOS + 0x080), ==, 0x13);
    g_assert_cmphex(qtest_readl(q, CMOS + 0x084), ==, 0x19);
    g_assert_cmphex(qtest_readl(q, CMOS + 0x088), ==, 0x37);
    g_assert_cmphex(qtest_readl(q, CMOS + 0x08c), ==, 0xb1817654);
    g_assert_cmphex(qtest_readl(q, CMOS + 0x090), ==, 0xb1814321);
    qtest_memset(q, RAM, 0xa5, 64);
    qtest_writel(q, CMOS + 0x060, 1);
    qtest_clock_step(q, 10000);
    qtest_memread(q, RAM, full, sizeof(full));

    qtest_writel(q, CMOS + 0x00c, 2);
    qtest_writel(q, CMOS + 0x010, 1);
    qtest_writel(q, CMOS + 0x014, 3);
    qtest_writel(q, CMOS + 0x018, 2);
    qtest_writel(q, CMOS + 0x01c, 2U << 16);
    qtest_writel(q, CMOS + 0x020, 1U << 16);
    qtest_writel(q, CMOS + 0x03c, RAM + 0x10000);
    qtest_memset(q, RAM + 0x10000, 0xa5, 64);
    qtest_writel(q, CMOS + 0x060, 1);
    qtest_clock_step(q, 10000);
    qtest_memread(q, RAM + 0x10000, cropped, sizeof(cropped));
    g_assert_cmphex(qtest_readl(q, CMOS + 0x068), ==, NIKON_CMOS_DONE);
    g_assert_cmphex(qtest_readb(q, RAM + 0x10000 + 6), ==, 0xa5);
    g_assert_cmpint(memcmp(full, cropped, sizeof(cropped)), !=, 0);
    qtest_quit(q);
}

static void test_invalid_and_cancel(void)
{
    QTestState *q = start();

    qtest_writel(q, CMOS + 0x060, 1);
    g_assert_cmphex(qtest_readl(q, CMOS + 0x068), ==, NIKON_CMOS_ERROR);
    g_assert_cmphex(qtest_readl(q, CMOS + 0x070), ==, NIKON_CMOS_EVENT_ERROR);
    qtest_writel(q, CMOS + 0x074, NIKON_CMOS_EVENT_ERROR);
    configure(q, 8, 8, 4, 48);
    qtest_memset(q, RAM, 0xa5, 48 * 8);
    qtest_writel(q, CMOS + 0x060, 1);
    qtest_writel(q, CMOS + 0x064, 1);
    qtest_clock_step(q, 10000);
    g_assert_cmphex(qtest_readl(q, CMOS + 0x068), ==, NIKON_CMOS_IDLE);
    g_assert_cmphex(qtest_readl(q, CMOS + 0x070), ==, NIKON_CMOS_EVENT_CANCEL);
    g_assert_cmphex(qtest_readl(q, RAM), ==, 0xa5a5a5a5);
    qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
    qtest_qmp_eventwait(q, "RESET");
    g_assert_cmphex(qtest_readl(q, CMOS + 0x100), ==, 0);
    g_assert_cmphex(qtest_readl(q, CMOS + 0x070), ==, 0);
    qtest_quit(q);
}

static void test_period(void)
{
    QTestState *q = start();

    configure(q, 4, 2, 1, 8);
    qtest_writel(q, CMOS + 0x050, 100000);
    qtest_writel(q, CMOS + 0x060, 1);
    qtest_clock_step(q, 10000);
    g_assert_cmphex(qtest_readl(q, CMOS + 0x068), ==, NIKON_CMOS_DONE);
    qtest_writel(q, CMOS + 0x060, 1);
    g_assert_cmphex(qtest_readl(q, CMOS + 0x068), ==, NIKON_CMOS_ERROR);
    g_assert_cmphex(qtest_readl(q, CMOS + 0x078), ==, 1);
    qtest_clock_step(q, 100001);
    qtest_writel(q, CMOS + 0x060, 1);
    qtest_clock_step(q, 10000);
    g_assert_cmphex(qtest_readl(q, CMOS + 0x068), ==, NIKON_CMOS_DONE);
    g_assert_cmphex(qtest_readl(q, CMOS + 0x078), ==, 2);
    qtest_quit(q);
}

int main(int argc, char **argv)
{
    uint32_t idle = cpu_to_le32(0xeafffffe);
    int fd, result;

    g_test_init(&argc, &argv, NULL);
    fd = g_file_open_tmp("nikoncmosbiosXXXXXX", &image, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    g_assert_true(g_file_set_contents(image, (char *)&idle,
                                      sizeof(idle), NULL));
    qtest_add_func("/nikon/cmos/configured-frames", test_configured_frames);
    qtest_add_func("/nikon/cmos/crop-scale-unknown", test_crop_scale_and_unknown);
    qtest_add_func("/nikon/cmos/invalid-cancel", test_invalid_and_cancel);
    qtest_add_func("/nikon/cmos/period", test_period);
    result = g_test_run();
    unlink(image);
    g_free(image);
    return result;
}
