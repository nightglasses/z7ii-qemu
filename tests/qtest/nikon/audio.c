/*
 * Audio configuration and opaque-word transport, without codec/PCM input.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "hw/nikon/devices.h"
#include "fixture.h"

#define AUDIO 0x2c080000
#define HDMAC 0x2c020000
#define RAM 0xb1801000
static char *image;

static QTestState *start(void)
{
    g_autofree char *bios = g_shell_quote(image);
    QTestState *q = qtest_initf("-M z7ii-mas-diagnostic -smp 1 -bios %s "
                              "-serial none", bios);
    g_autofree char *path = nikon_device_path(q, TYPE_NIKON_AUDIO);

    qtest_irq_intercept_out_named(q, path, "sysbus-irq");
    return q;
}

static void test_format(void)
{
    static const unsigned bits[] = { 16, 17, 18, 19, 23, 24, 25, 26, 27 };
    static const unsigned ports[] = { 0, 8, 16, 17, 24, 25, 28, 29 };
    QTestState *q = start();

    for (unsigned channel = 0; channel < 2; channel++) {
        uint64_t base = AUDIO + 0x100 + channel * 0x100;

        /* Exact original microphone record, before general field coverage. */
        qtest_writel(q, base + 8, 0xa0000);
        qtest_writel(q, base + 12, 0xd820000);
        g_assert_cmphex(qtest_readl(q, base + 12), ==, 0xd820000);
        for (unsigned n = 0; n < 512; n++) {
            uint32_t mode = 0;

            for (unsigned i = 0; i < ARRAY_SIZE(bits); i++) {
                mode |= ((n >> i) & 1) << bits[i];
            }
            qtest_writel(q, base + 12, mode);
            g_assert_cmphex(qtest_readl(q, base + 12), ==, mode);
        }
        for (unsigned n = 0; n < 256; n++) {
            uint32_t port = 0;

            for (unsigned i = 0; i < ARRAY_SIZE(ports); i++) {
                port |= ((n >> i) & 1) << ports[i];
            }
            qtest_writel(q, base + 24, port);
            g_assert_cmphex(qtest_readl(q, base + 24), ==, port);
        }
        for (unsigned format = 0; format < 128; format++) {
            uint32_t control = (format << 24) | ((format & 15) << 16);

            qtest_writel(q, base + 8, control);
            g_assert_cmphex(qtest_readl(q, base + 8), ==, control | 0x200);
        }
        g_assert_cmphex(qtest_readl(q, base + 28), ==, 0);
        g_assert_cmphex(qtest_readl(q, base + 32), ==, 0);
        g_assert_false(qtest_get_irq(q, channel));
    }
    qtest_quit(q);
}

static void test_clock(void)
{
    QTestState *q = start();

    for (unsigned mck = 0; mck < 32; mck++) {
        for (unsigned ck = 0; ck < 8; ck++) {
            for (unsigned lr = 0; lr < 4; lr++) {
                uint64_t ticks = (mck + 1) * 4 * (ck + 1) * 16 * (lr + 1);
                int64_t ns = ticks * 1000000000 / 48000000;
                uint32_t divider = 0x40000000 | mck << 24 | ck << 18 | lr << 16;

                qtest_writel(q, AUDIO, 1);
                qtest_writel(q, AUDIO, 0);
                qtest_writel(q, AUDIO + 0x114, divider);
                g_assert_cmphex(qtest_readl(q, AUDIO + 0x114), ==, divider);
                qtest_writel(q, AUDIO + 0x104, 0x12345678);
                qtest_writel(q, AUDIO + 0x108, 0x2000);
                /* QEMU Clock's fixed-point48MHz conversion can lose1ns. */
                qtest_clock_step(q, ns - 2);
                g_assert_cmphex(qtest_readl(q, AUDIO + 0x120), ==, 3);
                qtest_clock_step(q, 3);
                g_assert_cmphex(qtest_readl(q, AUDIO + 0x120), ==, 0);
            }
        }
    }
    qtest_quit(q);
}

static void test_blocks(void)
{
    QTestState *q = start();

    for (unsigned channel = 0; channel < 2; channel++) {
        uint64_t base = AUDIO + 0x100 + channel * 0x100;

        qtest_writel(q, base + 0x40, 0x101);
        qtest_writel(q, base + 0x44, 0xffff);
        qtest_writel(q, base + 0x48, 0x1234);
        g_assert_cmphex(qtest_readl(q, base + 0x40), ==, 0x101);
        g_assert_cmphex(qtest_readl(q, base + 0x44), ==, 0xffff);
        g_assert_cmphex(qtest_readl(q, base + 0x48), ==, 0x1234);
        qtest_writel(q, base + 0x40, 0x1000);
        qtest_writel(q, base + 0x44, 0x10000);
        qtest_writel(q, base + 0x48, 0x10000);
        g_assert_cmphex(qtest_readl(q, base + 0x40), ==, 0x101);
        g_assert_cmphex(qtest_readl(q, base + 0x44), ==, 0xffff);
        g_assert_cmphex(qtest_readl(q, base + 0x48), ==, 0x1234);
        /* Unsupported dual-DMA output cannot consume a word as single DMA. */
        qtest_writel(q, base + 0x3c, 0xfeedface);
        g_assert_cmphex(qtest_readl(q, base + 0x20), ==, 0);
        qtest_writel(q, base + 8, 0x2000);
        g_assert_cmphex(qtest_readl(q, base + 8), ==, 0x200);
        qtest_writel(q, base + 0x40, 1);
        qtest_writel(q, base + 8, 0x1000);
        qtest_clock_step(q, 1000000);
        g_assert_cmphex(qtest_readl(q, base + 28), ==, 0);
        g_assert_cmphex(qtest_readl(q, base + 16) & 0x1010000, ==, 0);
        g_assert_false(qtest_get_irq(q, channel));
        qtest_writel(q, base + 8, 0x2000);
        qtest_writel(q, base + 0x40, 0x101); /* Cannot enter during output. */
        g_assert_cmphex(qtest_readl(q, base + 0x40), ==, 1);
        g_assert_cmphex(qtest_readl(q, base + 8), ==, 0x2200);
    }
    qtest_system_reset(q);
    for (unsigned offset = 0x140; offset <= 0x148; offset += 4) {
        g_assert_cmphex(qtest_readl(q, AUDIO + offset), ==, 0);
        g_assert_cmphex(qtest_readl(q, AUDIO + offset + 0x100), ==, 0);
    }
    qtest_quit(q);
}

static void test_lifetime(void)
{
    QTestState *q = start();
    g_autofree char *path = nikon_device_path(q, TYPE_NIKON_AUDIO);

    qtest_writel(q, AUDIO + 0x10c, 0xf8f0000);
    qtest_writel(q, AUDIO + 0x104, 0x11223344);
    qtest_writel(q, AUDIO + 0x140, 0x100); /* Cannot reinterpret queued data. */
    g_assert_cmphex(qtest_readl(q, AUDIO + 0x140), ==, 0);
    /* Cannot shrink an occupied FIFO or switch occupied lane geometry. */
    qtest_writel(q, AUDIO + 0x10c, 0xd8f0000);
    qtest_writel(q, AUDIO + 0x108, 2);
    qtest_writel(q, AUDIO + 0x10c, 0x80000000); /* Loopback remains unbound. */
    qtest_writel(q, AUDIO + 0x118, 0x10); /* Reserved port bit. */
    qtest_writew(q, AUDIO + 0x10c, 0);
    qtest_writel(q, AUDIO + 0x10d, 0);
    g_assert_cmphex(qtest_readl(q, AUDIO + 0x10c), ==, 0xf8f0000);
    g_assert_cmphex(qtest_readl(q, AUDIO + 0x120), ==, 3);
    qtest_writel(q, AUDIO + 0x114, 0x40000000);
    qtest_writel(q, AUDIO + 0x108, 0x2800);
    qtest_qmp_assert_success(q, "{'execute':'qom-set','arguments':{"
                            "'path':%s,'property':'realized','value':false}}",
                            path);
    qtest_clock_step(q, 1000000);
    g_assert_cmphex(qtest_readl(q, AUDIO + 0x120), ==, 0);
    g_assert_false(qtest_get_irq(q, 0));
    qtest_qmp_assert_success(q, "{'execute':'qom-set','arguments':{"
                            "'path':%s,'property':'realized','value':true}}",
                            path);
    qtest_writel(q, AUDIO + 0x114, 0x40000000);
    qtest_writel(q, AUDIO + 0x108, 0x2000);
    qtest_clock_step(q, 1334);
    g_assert_cmphex(qtest_readl(q, AUDIO + 0x110) & 0x20000, ==, 0x20000);
    qtest_writel(q, AUDIO + 0x110, 0);
    g_assert_cmphex(qtest_readl(q, AUDIO + 0x110) & 0x20000, ==, 0);
    qtest_system_reset(q);
    qtest_clock_step(q, 100000);
    g_assert_cmphex(qtest_readl(q, AUDIO + 0x110) & 0x20000, ==, 0);
    qtest_quit(q);
}

static void monitor_read(QTestState *q, uint64_t address, uint32_t expected)
{
    /* A qtest read alone returns zero even on MEMTX_ERROR. A real DMA beat
     * proves the monitor read succeeds, rather than hiding a source error. */
    qtest_writel(q, RAM, 0xfeedface);
    qtest_writel(q, HDMAC, 0x90000000);
    qtest_writel(q, HDMAC + 0x18, address);
    qtest_writel(q, HDMAC + 0x1c, RAM);
    qtest_writel(q, HDMAC + 0x14, 0x08000000);
    qtest_writel(q, HDMAC + 0x10, 0xa0000000);
    qtest_clock_step(q, 100);
    g_assert_cmphex(qtest_readl(q, HDMAC + 0x14), ==, 0x08050000);
    g_assert_cmphex(qtest_readl(q, RAM), ==, expected);
}

static void test_monitor_reset(void)
{
    QTestState *q = start();

    for (unsigned channel = 0; channel < 2; channel++) {
        uint64_t base = AUDIO + 0x100 + channel * 0x100;

        qtest_writel(q, base + 8, 0x1000);
        for (unsigned repeat = 0; repeat < 2; repeat++) {
            for (unsigned offset = 0x54; offset <= 0x60; offset += 4) {
                monitor_read(q, base + offset, 0);
            }
        }
        g_assert_cmphex(qtest_readl(q, base + 0x1c), ==, 0);
        g_assert_cmphex(qtest_readl(q, base + 0x10) & 0x1010000, ==, 0);
        g_assert_false(qtest_get_irq(q, channel));
    }
    qtest_quit(q);
}

static void test_monitor_output(void)
{
    QTestState *q = start();

    for (unsigned channel = 0; channel < 2; channel++) {
        uint64_t base = AUDIO + 0x100 + channel * 0x100;

        qtest_writel(q, base + 4, 0x89ab1234);
        g_assert_cmphex(qtest_readl(q, base + 0x5c), ==, 0);
        qtest_writel(q, base + 0x14, 0x40000000);
        qtest_writel(q, base + 8, 0x2000);
        qtest_clock_step(q, 1335);
        qtest_writel(q, base + 8, 0);
        monitor_read(q, base + 0x5c, 0x1234);
        monitor_read(q, base + 0x60, 0x89ab);
        monitor_read(q, base + 0x54, 0);
        monitor_read(q, base + 0x58, 0);
        /* Reads, guest writes, holes and wrong widths cannot change a latch. */
        qtest_writel(q, base + 0x5c, 0xffffffff);
        qtest_writel(q, base + 0x60, 0xffffffff);
        qtest_writel(q, base + 0x54, 0xffffffff);
        qtest_writel(q, base + 0x58, 0xffffffff);
        qtest_writew(q, base + 0x5c, 0);
        qtest_writel(q, base + 0x5d, 0);
        qtest_writel(q, base + 0x50, 0xffffffff);
        qtest_writel(q, base + 0x64, 0xffffffff);
        monitor_read(q, base + 0x5c, 0x1234);
        monitor_read(q, base + 0x60, 0x89ab);
        qtest_clock_step(q, 100000);
        g_assert_cmphex(qtest_readl(q, base + 0x5c), ==, 0x1234);
        /* A later quieter word replaces the prior value, not a peak max. */
        qtest_writel(q, base + 4, 0x00020001);
        qtest_writel(q, base + 8, 0x2000);
        qtest_clock_step(q, 1335);
        qtest_writel(q, base + 8, 0);
        monitor_read(q, base + 0x5c, 1);
        monitor_read(q, base + 0x60, 2);
        qtest_writel(q, AUDIO, 1U << channel);
        qtest_writel(q, AUDIO, 0);
        monitor_read(q, base + 0x5c, 0);
        monitor_read(q, base + 0x60, 0);
    }
    qtest_quit(q);
}

static void test_monitor_lifetime(void)
{
    QTestState *q = start();
    g_autofree char *path = nikon_device_path(q, TYPE_NIKON_AUDIO);

    /* Separate words must both be accepted by the actual virtual sink. */
    qtest_writel(q, AUDIO + 0x108, 2);
    qtest_writel(q, AUDIO + 0x130, 0xabcdfedc);
    qtest_writel(q, AUDIO + 0x114, 0x40000000);
    qtest_writel(q, AUDIO + 0x108, 0x2002);
    qtest_clock_step(q, 1335);
    g_assert_cmphex(qtest_readl(q, AUDIO + 0x15c), ==, 0);
    qtest_writel(q, AUDIO + 0x134, 0x12345678);
    qtest_clock_step(q, 1335);
    qtest_writel(q, AUDIO + 0x108, 2);
    monitor_read(q, AUDIO + 0x15c, 0xfedc);
    monitor_read(q, AUDIO + 0x160, 0x5678);
    /* An underflow must retain the previous observation. */
    qtest_writel(q, AUDIO + 0x108, 0x2002);
    qtest_clock_step(q, 1335);
    qtest_writel(q, AUDIO + 0x108, 2);
    monitor_read(q, AUDIO + 0x15c, 0xfedc);
    monitor_read(q, AUDIO + 0x160, 0x5678);
    qtest_writel(q, AUDIO + 0x130, 0x11112222);
    qtest_writel(q, AUDIO + 0x134, 0x33334444);
    qtest_writel(q, AUDIO + 0x108, 0x2002);
    qtest_system_reset(q);
    qtest_clock_step(q, 100000);
    monitor_read(q, AUDIO + 0x15c, 0);
    monitor_read(q, AUDIO + 0x160, 0);
    qtest_writel(q, AUDIO + 0x104, 0xaabbccdd);
    qtest_writel(q, AUDIO + 0x114, 0x40000000);
    qtest_writel(q, AUDIO + 0x108, 0x2000);
    qtest_qmp_assert_success(q, "{'execute':'qom-set','arguments':{"
                            "'path':%s,'property':'realized','value':false}}",
                            path);
    qtest_clock_step(q, 100000);
    monitor_read(q, AUDIO + 0x15c, 0);
    monitor_read(q, AUDIO + 0x160, 0);
    qtest_quit(q);
}

int main(int argc, char **argv)
{
    uint32_t idle = cpu_to_le32(0xeafffffe);
    int fd, result;

    g_test_init(&argc, &argv, NULL);
    fd = g_file_open_tmp("nikonaudioXXXXXX", &image, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    g_assert_true(g_file_set_contents(image, (char *)&idle,
                                     sizeof(idle), NULL));
    qtest_add_func("/nikon/audio/format", test_format);
    qtest_add_func("/nikon/audio/clock", test_clock);
    qtest_add_func("/nikon/audio/blocks", test_blocks);
    qtest_add_func("/nikon/audio/lifetime", test_lifetime);
    qtest_add_func("/nikon/audio/monitor/reset", test_monitor_reset);
    qtest_add_func("/nikon/audio/monitor/output", test_monitor_output);
    qtest_add_func("/nikon/audio/monitor/lifetime", test_monitor_lifetime);
    result = g_test_run();
    g_assert_cmpint(unlink(image), ==, 0);
    g_free(image);
    return result;
}
