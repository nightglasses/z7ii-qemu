/* DMA suspend/resume contracts through real bus and Audio DREQ.
 * Synthetic RAM only; no original firmware or physical timing claim.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "../libqtest.h"
#include "fixture.h"

#define RAM 0xb1800000
#define HDMAC 0x2c020000
#define AUDIO 0x2c080000
#define EB (1U << 31)
#define PB (1U << 30)
#define DH (1U << 24)

static char *image;

static QTestState *start(void)
{
    g_autofree char *quoted = g_shell_quote(image);
    g_autofree char *args = g_strdup_printf(
        "-M z7ii-mas-diagnostic -smp 1 -bios %s -serial none", quoted);

    QTestState *q = qtest_init(args);

    /* qtest executes no guest CPU. -S would disable virtual timer callbacks. */
    nikon_intercept_gic(q);
    return q;
}

static void setup(QTestState *q, uint64_t channel, uint32_t source,
                  uint32_t destination, uint32_t cb, uint32_t ca)
{
    qtest_writel(q, channel + 8, source);
    qtest_writel(q, channel + 12, destination);
    qtest_writel(q, channel + 4, cb);
    qtest_writel(q, channel, ca);
}

static void test_pause_geometry(void)
{
    QTestState *q = start();
    unsigned bank, index;

    for (bank = 0; bank < 2; bank++) {
        uint64_t base = HDMAC + bank * 0x10000;

        qtest_writel(q, base, 0x90000000);
        for (index = 0; index < 8; index++) {
            uint64_t channel = base + 0x10 + index * 0x10;
            uint32_t source = RAM + 0x1000 + (bank * 8 + index) * 0x20;
            uint32_t destination = RAM + 0x2000 + (bank * 8 + index) * 0x20;
            uint32_t value = 0xaabb0000 | bank << 8 | index;
            unsigned irq = (bank ? 142 : 150) + index;

            qtest_writel(q, source, value);
            qtest_writel(q, source + 4, value + 1);
            setup(q, channel, source, destination, 0x08180000, 0xa0000001);
            qtest_writel(q, channel, qtest_readl(q, channel) | PB);
            qtest_clock_step(q, 100000);
            g_assert_cmphex(qtest_readl(q, destination), ==, 0);
            g_assert_cmphex(qtest_readl(q, channel + 8), ==, source);
            g_assert_cmphex(qtest_readl(q, channel + 4), ==, 0x08180000);
            g_assert_false(qtest_get_irq(q, irq - 32));
            qtest_writel(q, channel, qtest_readl(q, channel) & ~PB);
            qtest_clock_step_next(q);
            g_assert_cmphex(qtest_readl(q, destination), ==, value);
            g_assert_cmphex(qtest_readl(q, destination + 4), ==, value + 1);
            g_assert_cmphex(qtest_readl(q, channel + 4), ==, 0x081d0000);
            g_assert_true(qtest_get_irq(q, irq - 32));
            qtest_writel(q, channel + 4, 0x08000000);
            g_assert_false(qtest_get_irq(q, irq - 32));
        }
    }
    qtest_quit(q);
}

static void test_halt_all(void)
{
    QTestState *q = start();
    unsigned bank, index;

    for (bank = 0; bank < 2; bank++) {
        uint64_t base = HDMAC + bank * 0x10000;

        qtest_writel(q, base, 0x90000000 | DH);
        for (index = 0; index < 8; index++) {
            uint32_t source = RAM + 0x1000 + (bank * 8 + index) * 4;
            uint32_t destination = RAM + 0x2000 + (bank * 8 + index) * 4;

            qtest_writel(q, source, 0x100 + bank * 8 + index);
            setup(q, base + 0x10 + index * 0x10, source, destination,
                  0x08000000, 0xa0000000);
        }
    }
    qtest_clock_step(q, 100000);
    for (index = 0; index < 16; index++) {
        g_assert_cmphex(qtest_readl(q, RAM + 0x2000 + index * 4), ==, 0);
    }
    /* Clearing PB cannot override a still asserted global halt. */
    qtest_writel(q, HDMAC + 0x10, 0xe0000000);
    qtest_writel(q, HDMAC + 0x10, 0xa0000000);
    qtest_clock_step(q, 100000);
    g_assert_cmphex(qtest_readl(q, RAM + 0x2000), ==, 0);
    for (bank = 0; bank < 2; bank++) {
        qtest_writel(q, HDMAC + bank * 0x10000, 0x90000000);
    }
    qtest_clock_step_next(q);
    for (index = 0; index < 16; index++) {
        g_assert_cmphex(qtest_readl(q, RAM + 0x2000 + index * 4), ==,
                        0x100 + index);
    }
    qtest_quit(q);
}

static void test_cancel_held(void)
{
    QTestState *q = start();

    qtest_writel(q, RAM + 0x1000, 0x12345678);
    qtest_writel(q, HDMAC, 0x90000000);
    setup(q, HDMAC + 0x10, RAM + 0x1000, RAM + 0x2000,
          0x08180000, 0xe0000000);
    qtest_writel(q, HDMAC + 0x10, qtest_readl(q, HDMAC + 0x10) & ~EB);
    qtest_writel(q, HDMAC + 0x10, qtest_readl(q, HDMAC + 0x10) & ~PB);
    qtest_clock_step(q, 100000);
    g_assert_cmphex(qtest_readl(q, HDMAC + 0x14), ==, 0x081a0000);
    g_assert_cmphex(qtest_readl(q, RAM + 0x2000), ==, 0);
    qtest_quit(q);
}

static void test_reset_held(void)
{
    QTestState *q = start();

    qtest_writel(q, RAM + 0x1000, 0x12345678);
    qtest_writel(q, HDMAC, 0x90000000 | DH);
    setup(q, HDMAC + 0x10, RAM + 0x1000, RAM + 0x2000,
          0x08180000, 0xe0000000);
    qtest_qmp_assert_success(q, "{'execute': 'system_reset'}");
    qtest_qmp_eventwait(q, "RESET");
    qtest_clock_step(q, 100000);
    g_assert_cmphex(qtest_readl(q, HDMAC), ==, 0);
    g_assert_cmphex(qtest_readl(q, HDMAC + 0x10), ==, 0);
    g_assert_cmphex(qtest_readl(q, HDMAC + 0x14), ==, 0);
    g_assert_cmphex(qtest_readl(q, RAM + 0x2000), ==, 0);
    qtest_quit(q);
}

static void test_reentrant_pause(void)
{
    QTestState *q = start();

    qtest_writel(q, HDMAC, 0x90000000);
    qtest_writel(q, RAM + 0x1000, 0xe0000001);
    qtest_writel(q, RAM + 0x1004, 0x08000000);
    setup(q, HDMAC + 0x10, RAM + 0x1000, HDMAC + 0x10,
          0x08180000, 0xa0000001);
    qtest_clock_step_next(q);
    qtest_clock_step(q, 100000);
    /* First accepted beat writes PB through the actual DMA bus. */
    g_assert_cmphex(qtest_readl(q, HDMAC + 0x10), ==, 0xe0000001);
    g_assert_cmphex(qtest_readl(q, HDMAC + 0x18), ==, RAM + 0x1004);
    g_assert_cmphex(qtest_readl(q, HDMAC + 0x1c), ==, HDMAC + 0x14);
    g_assert_cmphex(qtest_readl(q, HDMAC + 0x14), ==, 0x08180000);
    qtest_writel(q, HDMAC + 0x10, 0xa0000001);
    qtest_clock_step_next(q);
    /* The second beat clears CI/EI through CB, then really completes. */
    g_assert_cmphex(qtest_readl(q, HDMAC + 0x18), ==, RAM + 0x1008);
    g_assert_cmphex(qtest_readl(q, HDMAC + 0x14), ==, 0x08050000);
    qtest_quit(q);
}

static void test_reentrant_halt(void)
{
    QTestState *q = start();

    qtest_writel(q, HDMAC, 0x90000000);
    qtest_writel(q, RAM + 0x1000, 0x91000000);
    qtest_writel(q, RAM + 0x1004, 0x11223344);
    setup(q, HDMAC + 0x10, RAM + 0x1000, HDMAC, 0x08180000, 0xa0000001);
    qtest_clock_step_next(q);
    qtest_clock_step(q, 100000);
    /* Do not run the second beat into the unimplemented global+4 hole. */
    g_assert_cmphex(qtest_readl(q, HDMAC), ==, 0x91000000);
    g_assert_cmphex(qtest_readl(q, HDMAC + 0x18), ==, RAM + 0x1004);
    g_assert_cmphex(qtest_readl(q, HDMAC + 0x14), ==, 0x08180000);
    qtest_writel(q, HDMAC + 0x10, 0x20000001);
    qtest_clock_step(q, 100000);
    g_assert_cmphex(qtest_readl(q, HDMAC + 0x14), ==, 0x081a0000);
    qtest_quit(q);
}

static void test_audio_pause(void)
{
    QTestState *q = start();
    uint64_t channel = HDMAC + 0x10010;
    unsigned i;

    for (i = 0; i < 32; i++) {
        qtest_writel(q, RAM + 0x1000 + i * 4, i + 1);
    }
    qtest_writel(q, AUDIO + 0x10c, 1U << 25);
    qtest_writel(q, AUDIO + 0x108, (1U << 13) | (1U << 15));
    qtest_writel(q, HDMAC + 0x10000, 0x90000000);
    setup(q, channel, RAM + 0x1000, AUDIO + 0x104, 0x29180000, 0x8300001f);
    qtest_clock_step(q, 100000);
    g_assert_cmphex(qtest_readl(q, channel + 8), ==, RAM + 0x1040);
    g_assert_cmphex(qtest_readl(q, channel + 4), ==, 0x29180000);
    g_assert_cmphex(qtest_readl(q, AUDIO + 0x120), ==, 0xffffffff);
    qtest_writel(q, channel, qtest_readl(q, channel) | PB);
    qtest_writel(q, AUDIO + 0x114, 1U << 30);
    qtest_clock_step_next(q);
    /* A real sink freed a FIFO stage/DREQ, but PB still forbids a beat. */
    g_assert_cmphex(qtest_readl(q, AUDIO + 0x120), ==, 0x3fffffff);
    g_assert_cmphex(qtest_readl(q, channel + 8), ==, RAM + 0x1040);
    qtest_writel(q, channel, qtest_readl(q, channel) & ~PB);
    qtest_clock_step(q, 100000);
    g_assert_cmphex(qtest_readl(q, channel + 8), ==, RAM + 0x1080);
    g_assert_cmphex(qtest_readl(q, channel + 4), ==, 0x291d0000);
    qtest_quit(q);
}

static void test_reload_matrix(void)
{
    QTestState *q = start();
    uint64_t channel = HDMAC + 0x10;

    qtest_writel(q, HDMAC, 0x90000000);
    for (unsigned tw = 0; tw < 3; tw++) {
        unsigned width = 1U << tw;

        for (unsigned fixed = 0; fixed < 4; fixed++) {
            for (unsigned reload = 0; reload < 8; reload++) {
                uint32_t cb = tw << 26 | fixed << 24 | reload << 21 |
                              0x180000;
                uint32_t source = RAM + 0x1000;
                uint32_t destination = RAM + 0x2000;

                for (unsigned i = 0; i < 32; i++) {
                    qtest_writeb(q, source + i, 0x40 + i);
                    qtest_writeb(q, destination + i, 0);
                }
                setup(q, channel, source, destination, cb, 0xa0010001);
                g_assert_cmphex(qtest_readl(q, channel + 4), ==, cb);
                qtest_clock_step_next(q);
                /* BC=TC=1 means four beats. Fixed endpoints still perform
                 * every bus transaction; reload changes only final readback. */
                for (unsigned beat = 0; beat < 4; beat++) {
                    for (unsigned byte = 0; byte < width; byte++) {
                        unsigned offset = byte +
                                          ((fixed & 1) ? 0 : beat * width);
                        unsigned expected = 0x40 + byte;

                        if (!(fixed & 2)) {
                            expected += ((fixed & 1) ? 3 : beat) * width;
                        }
                        g_assert_cmphex(qtest_readb(q, destination + offset),
                                        ==, expected);
                    }
                }
                g_assert_cmphex(qtest_readl(q, channel + 8), ==,
                                source + ((fixed & 2) || (reload & 2) ? 0 :
                                          4 * width));
                g_assert_cmphex(qtest_readl(q, channel + 12), ==,
                                destination + ((fixed & 1) || (reload & 1) ? 0 :
                                               4 * width));
                g_assert_cmphex(qtest_readl(q, channel), ==,
                                0x20000000 | ((reload & 4) ? 0x10001 : 0));
                g_assert_cmphex(qtest_readl(q, channel + 4), ==, cb | 0x50000);
                g_assert_true(qtest_get_irq(q, 150 - 32));
                qtest_writel(q, channel + 4, cb);
                g_assert_false(qtest_get_irq(q, 150 - 32));
                /* Acknowledgement and elapsed time alone cannot repeat DMA. */
                qtest_writeb(q, destination, 0xaa);
                qtest_clock_step(q, 100000);
                g_assert_cmphex(qtest_readb(q, destination), ==, 0xaa);
            }
        }
    }
    qtest_quit(q);
}

static void test_reload_lifetime(void)
{
    QTestState *q = start();
    uint64_t channel = HDMAC + 0x10;
    const uint32_t invalid[] = {
        0x08f80001, 0x08f80100, 0x08f81000, 0x48f80000,
        0x0cf80000, 0x38f80000,
    };

    qtest_writel(q, RAM + 0x1000, 0x12345678);
    qtest_writel(q, RAM + 0x1004, 0x90abcdef);
    qtest_writel(q, HDMAC, 0x90000000);
    setup(q, channel, RAM + 0x1000, RAM + 0x2000, 0x08f80000, 0xe0000001);
    /* Reload flags do not relax reserved/protection/TT/TW/MS boundaries,
     * nor permit configuration changes to an armed operation. */
    for (unsigned i = 0; i < G_N_ELEMENTS(invalid); i++) {
        qtest_writel(q, channel + 4, invalid[i]);
        g_assert_cmphex(qtest_readl(q, channel + 4), ==, 0x08f80000);
    }
    qtest_writel(q, channel + 4, 0x08d80000);
    qtest_writel(q, channel + 8, RAM + 0x1004);
    qtest_writel(q, channel + 12, RAM + 0x2004);
    qtest_writel(q, channel, 0xe0010001);
    g_assert_cmphex(qtest_readl(q, channel + 4), ==, 0x08f80000);
    g_assert_cmphex(qtest_readl(q, channel + 8), ==, RAM + 0x1000);
    g_assert_cmphex(qtest_readl(q, channel + 12), ==, RAM + 0x2000);
    g_assert_cmphex(qtest_readl(q, channel), ==, 0xe0000001);
    qtest_clock_step(q, 100000);
    g_assert_cmphex(qtest_readl(q, RAM + 0x2000), ==, 0);
    qtest_writel(q, channel, 0xa0000001);
    qtest_clock_step_next(q);
    for (unsigned i = 0; i < 2; i++) {
        g_assert_cmphex(qtest_readl(q, channel + 8), ==, RAM + 0x1000);
        g_assert_cmphex(qtest_readl(q, channel + 12), ==, RAM + 0x2000);
        g_assert_cmphex(qtest_readl(q, channel), ==, 0x20000001);
        g_assert_cmphex(qtest_readl(q, channel + 4), ==, 0x08fd0000);
        g_assert_cmphex(qtest_readl(q, RAM + 0x2004), ==, 0x90abcdef);
        qtest_writel(q, channel + 4, 0x08f80000);
        qtest_writel(q, RAM + 0x2004, 0);
        if (!i) {
            qtest_writel(q, channel, 0xa0000001);
            qtest_clock_step_next(q);
        }
    }
    /* New programming must replace the prior operation's reload snapshot. */
    setup(q, channel, RAM + 0x1004, RAM + 0x2004, 0x08f80000, 0xa0000000);
    qtest_clock_step_next(q);
    g_assert_cmphex(qtest_readl(q, channel + 8), ==, RAM + 0x1004);
    g_assert_cmphex(qtest_readl(q, channel + 12), ==, RAM + 0x2004);
    g_assert_cmphex(qtest_readl(q, RAM + 0x2004), ==, 0x90abcdef);

    qtest_writel(q, channel + 4, 0x08f80000);
    qtest_writel(q, channel, 0xe0000000);
    qtest_writel(q, channel, 0x60000000);
    qtest_clock_step(q, 100000);
    g_assert_cmphex(qtest_readl(q, channel + 4), ==, 0x08fa0000);
    g_assert_false(qtest_readl(q, channel) & EB);
    qtest_writel(q, channel + 4, 0x08f80000);
    qtest_writel(q, channel, 0xe0000000);
    qtest_qmp_assert_success(q, "{'execute': 'system_reset'}");
    qtest_qmp_eventwait(q, "RESET");
    qtest_clock_step(q, 100000);
    g_assert_cmphex(qtest_readl(q, channel), ==, 0);
    g_assert_cmphex(qtest_readl(q, channel + 4), ==, 0);
    g_assert_cmphex(qtest_readl(q, channel + 8), ==, 0);
    g_assert_false(qtest_get_irq(q, 150 - 32));
    qtest_quit(q);
}

static void test_reload_reentrant(void)
{
    QTestState *q = start();
    uint64_t channel = HDMAC + 0x10;

    qtest_writel(q, HDMAC, 0x90000000);
    qtest_writel(q, RAM + 0x1000, 0xe0000001);
    qtest_writel(q, RAM + 0x1004, 0x08e00000);
    setup(q, channel, RAM + 0x1000, channel, 0x08f80000, 0xa0000001);
    qtest_clock_step_next(q);
    g_assert_cmphex(qtest_readl(q, channel + 8), ==, RAM + 0x1004);
    g_assert_cmphex(qtest_readl(q, channel + 12), ==, channel + 4);
    qtest_writel(q, channel, 0xa0000001);
    qtest_clock_step_next(q);
    g_assert_cmphex(qtest_readl(q, channel + 8), ==, RAM + 0x1000);
    g_assert_cmphex(qtest_readl(q, channel + 12), ==, channel);
    g_assert_cmphex(qtest_readl(q, channel + 4), ==, 0x08e50000);
    g_assert_false(qtest_get_irq(q, 150 - 32));

    qtest_writel(q, channel + 4, 0x08f80000);
    qtest_writel(q, RAM + 0x1000, 0x20000001);
    qtest_writel(q, channel, 0xa0000001);
    qtest_clock_step_next(q);
    /* A real bus cancel invalidates the operation before any reload/CI. */
    g_assert_cmphex(qtest_readl(q, channel + 4), ==, 0x08fa0000);
    g_assert_cmphex(qtest_readl(q, channel + 8), ==, RAM + 0x1000);
    g_assert_cmphex(qtest_readl(q, channel), ==, 0x20000001);
    qtest_quit(q);
}

int main(int argc, char **argv)
{
    uint32_t idle = cpu_to_le32(0xeafffffe);
    int fd, result;

    g_test_init(&argc, &argv, NULL);
    fd = g_file_open_tmp("nikondmaXXXXXX", &image, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    g_assert_true(g_file_set_contents(image, (char *)&idle, sizeof(idle), NULL));
    qtest_add_func("/nikon/dma/pause/geometry", test_pause_geometry);
    qtest_add_func("/nikon/dma/halt/all", test_halt_all);
    qtest_add_func("/nikon/dma/held/cancel", test_cancel_held);
    qtest_add_func("/nikon/dma/held/reset", test_reset_held);
    qtest_add_func("/nikon/dma/reentrant/pause", test_reentrant_pause);
    qtest_add_func("/nikon/dma/reentrant/halt", test_reentrant_halt);
    qtest_add_func("/nikon/dma/audio/pause", test_audio_pause);
    qtest_add_func("/nikon/dma/reload/matrix", test_reload_matrix);
    qtest_add_func("/nikon/dma/reload/lifetime", test_reload_lifetime);
    qtest_add_func("/nikon/dma/reload/reentrant", test_reload_reentrant);
    result = g_test_run();
    unlink(image);
    g_free(image);
    return result;
}
