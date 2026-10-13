/* Nikon byte-stream graphics packet: real RAM writes and IRQ130 ACK.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "libqtest.h"
#include "fixture.h"

#define TRANSFER 0x20180000
#define RAM 0xb1800000
#define LUT_SETUP_BYTES 224U
#define LUT_DRAW_BYTES 1688U

static const char *image;

void nikon_transfer_add_tests(const char *bios);

static QTestState *start(void)
{
    g_autofree char *quoted = g_shell_quote(image);
    g_autofree char *args = g_strdup_printf(
        "-M z7ii-mas-diagnostic -smp 2 -bios %s -serial none", quoted);

    QTestState *q = qtest_init(args);

    nikon_intercept_gic(q);
    return q;
}

static void packet(QTestState *q, uint32_t source, uint32_t destination,
                   uint32_t count)
{
    qtest_writel(q, TRANSFER + 0xc1c, 0x8000);
    qtest_writel(q, TRANSFER + 0xc00, source);
    qtest_writel(q, TRANSFER + 0xc04, destination);
    qtest_writel(q, TRANSFER + 0xc34, 0);
    qtest_writel(q, TRANSFER + 0xc38, 0);
    qtest_writel(q, TRANSFER + 0xc10, 8);
    qtest_writel(q, TRANSFER + 0xc20, count);
    qtest_writel(q, TRANSFER + 0xc24, 0);
    qtest_writel(q, TRANSFER + 0xc28, 0);
}

static void build_lut_program(uint8_t setup[LUT_SETUP_BYTES],
                              uint8_t draw[LUT_DRAW_BYTES],
                              uint32_t source, uint32_t destination)
{
    uint32_t *setup_words = (uint32_t *)setup;
    uint32_t *draw_words = (uint32_t *)draw;

    memset(setup, 0, LUT_SETUP_BYTES);
    memset(draw, 0, LUT_DRAW_BYTES);
    stl_le_p(&setup_words[0], 0x00691004);
    stl_le_p(&setup_words[22], 0x00653004);
    stl_le_p(&setup_words[36], 0x0080f026);
    stl_le_p(&setup_words[37], source);
    stl_le_p(&setup_words[39], 16);
    stl_le_p(&setup_words[40], 0x00800080);
    stl_le_p(&setup_words[42], 0x0006f004);

    stl_le_p(&draw_words[0], 0x01408004);
    stl_le_p(&draw_words[10], 0x01a3f004);
    stl_le_p(&draw_words[11], 0x00200020);
    stl_le_p(&draw_words[87], 0x0188f7fc);
    for (unsigned i = 0; i < 256; i++) {
        stl_le_p(&draw_words[88 + i], i << 4);
    }
    stl_le_p(&draw_words[344], 0x0192f02e);
    stl_le_p(&draw_words[355], 0x0188f0fc);
    for (unsigned i = 0; i < 32; i++) {
        stl_le_p(&draw_words[356 + i], i << 7);
    }
    stl_le_p(&draw_words[396], 0x01a4f004);
    stl_le_p(&draw_words[397], destination);
    stl_le_p(&draw_words[403], 0x01000000);
    stl_le_p(&draw_words[408], 0x00a0f02e);
    stl_le_p(&draw_words[409], source);
    stl_le_p(&draw_words[414], 2);
    stl_le_p(&draw_words[415], 0x00a6f004);
}

static void submit_lut_program(QTestState *q,
                               const uint8_t setup[LUT_SETUP_BYTES],
                               const uint8_t *draw, uint32_t draw_bytes)
{
    qtest_memwrite(q, RAM, setup, LUT_SETUP_BYTES);
    qtest_memwrite(q, RAM + 0x1000, draw, draw_bytes);
    qtest_writel(q, TRANSFER + 0x1214, 0x0100ffff);
    qtest_writel(q, TRANSFER + 0x1298, 0);
    qtest_writel(q, TRANSFER + 0x10d4, RAM);
    qtest_writel(q, TRANSFER + 0x10dc, LUT_SETUP_BYTES / 8);
    qtest_writel(q, TRANSFER + 0x10d0, 1);
    qtest_writel(q, TRANSFER + 0x10e4, RAM + 0x1000);
    qtest_writel(q, TRANSFER + 0x10ec, draw_bytes / 8);
    qtest_writel(q, TRANSFER + 0x10e0, 1);
}

static void wait_completion(QTestState *q)
{
    int64_t now = qtest_clock_step(q, 1);
    int64_t deadline = now + 1000000;
    unsigned events = 0;

    /* Advance each scheduled deadline so multi-batch DMA can reschedule. */
    while (!qtest_get_irq(q, 130 - 32)) {
        int64_t next = qtest_clock_step_next(q);

        g_assert_cmpint(next, >, now);
        g_assert_cmpint(next, <=, deadline);
        g_assert_cmpuint(++events, <=, 64);
        now = next;
    }
}

static void test_copy(void)
{
    QTestState *q = start();
    uint8_t source[8192], destination[sizeof(source)];
    unsigned i;

    for (i = 0; i < sizeof(source); i++) {
        source[i] = i ^ (i >> 8);
    }
    qtest_memwrite(q, RAM, source, sizeof(source));
    qtest_memset(q, RAM + 0x4000, 0, sizeof(source));
    packet(q, RAM, RAM + 0x4000, sizeof(source));
    qtest_writel(q, TRANSFER + 0xc18, 1);
    qtest_memread(q, RAM + 0x4000, destination, sizeof(destination));
    g_assert_cmpmem(destination, sizeof(destination),
                    (uint8_t[8192]) { 0 }, sizeof(destination));
    wait_completion(q);
    qtest_memread(q, RAM + 0x4000, destination, sizeof(destination));
    g_assert_cmpmem(destination, sizeof(destination), source, sizeof(source));
    g_assert_cmphex(qtest_readl(q, TRANSFER + 0xc00), ==, RAM);
    g_assert_cmphex(qtest_readl(q, TRANSFER + 0xc04), ==, RAM + 0x4000);
    g_assert_true(qtest_get_irq(q, 130 - 32));
    qtest_writel(q, TRANSFER + 0xc18, 0);
    g_assert_cmphex(qtest_readl(q, TRANSFER + 0xc18), ==, 0);
    g_assert_false(qtest_get_irq(q, 130 - 32));
    qtest_quit(q);
}

static void test_cancel(void)
{
    QTestState *q = start();
    uint8_t destination[64];

    qtest_memset(q, RAM, 0x5a, sizeof(destination));
    qtest_memset(q, RAM + 0x4000, 0, sizeof(destination));
    packet(q, RAM, RAM + 0x4000, sizeof(destination));
    qtest_writel(q, TRANSFER + 0xc18, 1);
    qtest_writel(q, TRANSFER + 0xc18, 0);
    qtest_clock_step(q, 1000000);
    qtest_memread(q, RAM + 0x4000, destination, sizeof(destination));
    g_assert_cmpmem(destination, sizeof(destination),
                    (uint8_t[64]) { 0 }, sizeof(destination));
    g_assert_false(qtest_get_irq(q, 130 - 32));
    qtest_quit(q);
}

static void test_geometry(void)
{
    QTestState *q = start();
    uint8_t destination[1024];
    unsigned i;

    qtest_memset(q, RAM, 0xa5, sizeof(destination));
    qtest_memset(q, RAM + 0x4000, 0, sizeof(destination));
    packet(q, RAM, RAM + 0x4000, 0);
    qtest_writel(q, TRANSFER + 0xc10, 0x2200);
    qtest_writel(q, TRANSFER + 0xc08, (8U << 16) | 64);
    qtest_writel(q, TRANSFER + 0xc0c, (8U << 16) | 64);
    qtest_writel(q, TRANSFER + 0xc18, 1);
    wait_completion(q);
    qtest_memread(q, RAM + 0x4000, destination, sizeof(destination));
    for (i = 0; i < sizeof(destination); i++) {
        g_assert_cmphex(destination[i], ==, 0xa5);
    }
    g_assert_true(qtest_get_irq(q, 130 - 32));
    qtest_quit(q);
}

static void test_lut_program(void)
{
    QTestState *q = start();
    uint8_t setup[LUT_SETUP_BYTES], draw[LUT_DRAW_BYTES];
    uint8_t input[128 * 128], output[32 * 32];

    for (unsigned i = 0; i < sizeof(input); i++) {
        input[i] = i ^ (i >> 7) ^ 0x5a;
    }
    build_lut_program(setup, draw, RAM + 0x4000, RAM + 0x9000);
    qtest_memwrite(q, RAM + 0x4000, input, sizeof(input));
    qtest_memset(q, RAM + 0x9000, 0xa5, sizeof(output));
    submit_lut_program(q, setup, draw, sizeof(draw));
    qtest_memread(q, RAM + 0x9000, output, sizeof(output));
    g_assert_cmpmem(output, sizeof(output),
                    (uint8_t[32 * 32]) { [0 ... 32 * 32 - 1] = 0xa5 },
                    sizeof(output));
    qtest_clock_step_next(q);
    qtest_memread(q, RAM + 0x9000, output, sizeof(output));
    for (unsigned y = 0; y < 32; y++) {
        for (unsigned x = 0; x < 32; x++) {
            g_assert_cmphex(output[y * 32 + x], ==,
                            input[y * 4 * 128 + x * 4] & 0xf8);
        }
    }
    g_assert_true(qtest_get_irq(q, 129 - 32));
    qtest_writel(q, TRANSFER + 0x1020, 0);
    g_assert_false(qtest_get_irq(q, 129 - 32));
    qtest_quit(q);
}

static void test_lut_rejection(void)
{
    static const enum {
        UNKNOWN, TRUNCATED, RANGE, OVERLAP, FORMAT,
    } cases[] = { UNKNOWN, TRUNCATED, RANGE, OVERLAP, FORMAT };

    for (unsigned i = 0; i < ARRAY_SIZE(cases); i++) {
        QTestState *q = start();
        uint8_t setup[LUT_SETUP_BYTES], draw[LUT_DRAW_BYTES];
        uint8_t destination[32 * 32];
        uint32_t draw_bytes = sizeof(draw);

        build_lut_program(setup, draw, RAM + 0x4000, RAM + 0x9000);
        switch (cases[i]) {
        case UNKNOWN:
            stl_le_p(draw, 0xdeadbeef);
            break;
        case TRUNCATED:
            draw_bytes -= 8;
            break;
        case RANGE:
            stl_le_p(draw + 397 * 4, 0xfffffc00);
            break;
        case OVERLAP:
            stl_le_p(draw + 397 * 4, RAM + 0x4000);
            break;
        case FORMAT:
            stl_le_p(draw + 414 * 4, 3);
            break;
        }
        qtest_memset(q, RAM + 0x4000, 0x3c, 128 * 128);
        qtest_memset(q, RAM + 0x9000, 0xa5, sizeof(destination));
        submit_lut_program(q, setup, draw, draw_bytes);
        qtest_clock_step(q, 1000);
        qtest_memread(q, RAM + 0x9000, destination, sizeof(destination));
        for (unsigned j = 0; j < sizeof(destination); j++) {
            g_assert_cmphex(destination[j], ==, 0xa5);
        }
        g_assert_false(qtest_get_irq(q, 129 - 32));
        qtest_quit(q);
    }
}

static void test_lut_reset_cancel(void)
{
    QTestState *q = start();
    uint8_t setup[LUT_SETUP_BYTES], draw[LUT_DRAW_BYTES];
    uint8_t destination[32 * 32];

    build_lut_program(setup, draw, RAM + 0x4000, RAM + 0x9000);
    qtest_memset(q, RAM + 0x4000, 0x7c, 128 * 128);
    qtest_memset(q, RAM + 0x9000, 0xa5, sizeof(destination));
    submit_lut_program(q, setup, draw, sizeof(draw));
    qtest_system_reset(q);
    qtest_clock_step(q, 1000);
    qtest_memread(q, RAM + 0x9000, destination, sizeof(destination));
    for (unsigned i = 0; i < sizeof(destination); i++) {
        g_assert_cmphex(destination[i], ==, 0xa5);
    }
    g_assert_false(qtest_get_irq(q, 129 - 32));
    qtest_quit(q);
}

static void test_planes(void)
{
    QTestState *q = start();
    uint8_t destination[1024];
    unsigned i;

    qtest_memset(q, RAM, 0x5a, sizeof(destination));
    qtest_memset(q, RAM + 0x8000, 0xa5, sizeof(destination));
    qtest_memset(q, RAM + 0x4000, 0, sizeof(destination));
    qtest_memset(q, RAM + 0xc000, 0, sizeof(destination));
    packet(q, RAM, RAM + 0x4000, 0);
    qtest_writel(q, TRANSFER + 0xc10, 0x5500);
    qtest_writel(q, TRANSFER + 0xc08, (16U << 16) | 64);
    qtest_writel(q, TRANSFER + 0xc0c, (16U << 16) | 64);
    qtest_writel(q, TRANSFER + 0xc34, RAM + 0x8000);
    qtest_writel(q, TRANSFER + 0xc38, RAM + 0xc000);
    qtest_writel(q, TRANSFER + 0xc18, 1);
    qtest_clock_step_next(q);
    g_assert_false(qtest_get_irq(q, 130 - 32));
    qtest_memread(q, RAM + 0xc000, destination, sizeof(destination));
    g_assert_cmpmem(destination, sizeof(destination),
                    (uint8_t[1024]) { 0 }, sizeof(destination));
    wait_completion(q);
    qtest_memread(q, RAM + 0x4000, destination, sizeof(destination));
    for (i = 0; i < sizeof(destination); i++) {
        g_assert_cmphex(destination[i], ==, 0x5a);
    }
    qtest_memread(q, RAM + 0xc000, destination, sizeof(destination));
    for (i = 0; i < sizeof(destination); i++) {
        g_assert_cmphex(destination[i], ==, 0xa5);
    }
    qtest_writel(q, TRANSFER + 0xc18, 0);
    g_assert_false(qtest_get_irq(q, 130 - 32));
    qtest_quit(q);
}

void nikon_transfer_add_tests(const char *bios)
{
    image = bios;
    qtest_add_func("/nikon/transfer/copy", test_copy);
    qtest_add_func("/nikon/transfer/cancel", test_cancel);
    qtest_add_func("/nikon/transfer/geometry", test_geometry);
    qtest_add_func("/nikon/transfer/lut-program", test_lut_program);
    qtest_add_func("/nikon/transfer/lut-rejection", test_lut_rejection);
    qtest_add_func("/nikon/transfer/lut-reset-cancel", test_lut_reset_cancel);
    qtest_add_func("/nikon/transfer/planes", test_planes);
}
