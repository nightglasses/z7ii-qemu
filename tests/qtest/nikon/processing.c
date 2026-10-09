/* Complete externally produced buffers through the original processing seam.
 * No original codec, sensor join, or firmware GUI execution is claimed here.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "fixture.h"
#include "hw/nikon/processing.h"

#define PROCESSING 0x24070000
#define OUTPUT 0xb1810000

static const char *image;
static char *result_file;

void nikon_processing_add_tests(const char *bios);

static void cleanup(void)
{
    unlink(result_file);
    g_free(result_file);
}

static void supplied(unsigned length)
{
    g_autofree uint8_t *bytes = g_malloc(length);
    unsigned i;

    for (i = 0; i < length; i++) {
        bytes[i] = i * 19 + 7;
    }
    g_assert_true(g_file_set_contents(result_file, (char *)bytes, length, NULL));
}

static QTestState *start(bool connected, unsigned slot)
{
    g_autofree char *bios = g_shell_quote(image);
    g_autofree char *file = g_shell_quote(result_file);
    g_autofree char *provider = connected ? g_strdup_printf(
        "-global %s.result=%s -global %s.result-slot=%u",
        TYPE_NIKON_PROCESSING, file, TYPE_NIKON_PROCESSING, slot) :
        g_strdup("");
    g_autofree char *args = g_strdup_printf(
        "-M z7ii-mas-diagnostic -smp 2 -bios %s -nodefaults %s", bios, provider);
    QTestState *q = qtest_init(args);

    nikon_intercept_gic(q);
    return q;
}

static void configure(QTestState *q, unsigned slot, uint32_t capacity,
                      uint32_t target)
{
    /* Original104b9bf4 writes the address, polls bit0, then the capacity. */
    qtest_writel(q, PROCESSING + 0x464, slot >= 2);
    qtest_writel(q, PROCESSING + 0x470 + slot * 4, target);
    g_assert_cmphex(qtest_readl(q, PROCESSING + 0x470 + slot * 4), ==, target);
    qtest_writel(q, PROCESSING + (slot & 1 ? 0x49c : 0x494), capacity);
    qtest_writel(q, PROCESSING + 0xc24, 1U << (9 + (slot & 1)));
}

static void verify(QTestState *q, uint32_t target, unsigned length)
{
    g_autofree uint8_t *bytes = g_malloc(length);
    unsigned i;

    qtest_memread(q, target, bytes, length);
    for (i = 0; i < length; i++) {
        g_assert_cmphex(bytes[i], ==, (uint8_t)(i * 19 + 7));
    }
}

static void untouched(QTestState *q, uint32_t target, unsigned length)
{
    g_autofree uint8_t *bytes = g_malloc(length);
    unsigned i;

    qtest_memread(q, target, bytes, length);
    for (i = 0; i < length; i++) {
        g_assert_cmphex(bytes[i], ==, 0xa5);
    }
}

static void test_complete(void)
{
    QTestState *q;

    supplied(2048);
    q = start(true, 0);
    configure(q, 0, 2048, OUTPUT);
    qtest_memset(q, OUTPUT, 0xa5, 2048);
    qtest_writel(q, PROCESSING + 0x454, 1);
    g_assert_cmphex(qtest_readl(q, PROCESSING + 0xfc0), ==, 0x80);
    g_assert_cmphex(qtest_readl(q, PROCESSING + 0xc28), ==, 0);
    g_assert_false(qtest_get_irq(q, 110 - 32));
    qtest_clock_step(q, 1000000);
    verify(q, OUTPUT, 2048);
    g_assert_cmphex(qtest_readl(q, PROCESSING + 0xfc0), ==, 0);
    g_assert_cmphex(qtest_readl(q, PROCESSING + 0xc28), ==, 1U << 9);
    g_assert_true(qtest_get_irq(q, 110 - 32));
    /* Original1051c4fc copies the raw status word to the adjacent ACK. */
    qtest_writel(q, PROCESSING + 0xc30,
                 qtest_readl(q, PROCESSING + 0xc28));
    g_assert_false(qtest_get_irq(q, 110 - 32));
    g_assert_cmphex(qtest_readl(q, PROCESSING + 0xc28), ==, 0);
    qtest_quit(q);
}

static void test_slots(void)
{
    unsigned slot;

    supplied(512);
    for (slot = 0; slot < 4; slot++) {
        QTestState *q = start(true, slot);

        configure(q, slot, 512, OUTPUT);
        qtest_memset(q, OUTPUT, 0xa5, 512);
        qtest_writel(q, PROCESSING + 0x454, 1);
        qtest_clock_step(q, 1000000);
        verify(q, OUTPUT, 512);
        g_assert_cmphex(qtest_readl(q, PROCESSING + 0xc28), ==,
                        1U << (9 + (slot & 1)));
        g_assert_true(qtest_get_irq(q, 110 - 32));
        qtest_quit(q);
    }
}

static void test_chunks(void)
{
    QTestState *q;

    supplied(8192);
    q = start(true, 0);
    configure(q, 0, 8192, OUTPUT);
    qtest_writel(q, PROCESSING + 0xc24, 0);
    qtest_memset(q, OUTPUT, 0xa5, 8192);
    qtest_writel(q, PROCESSING + 0x454, 1);
    qtest_clock_step(q, 90000);
    verify(q, OUTPUT, 4096);
    g_assert_cmphex(qtest_readl(q, OUTPUT + 8192 - 4), ==, 0xa5a5a5a5);
    g_assert_cmphex(qtest_readl(q, PROCESSING + 0xc28), ==, 0);
    g_assert_false(qtest_get_irq(q, 110 - 32));
    qtest_clock_step(q, 90000);
    verify(q, OUTPUT, 8192);
    g_assert_cmphex(qtest_readl(q, PROCESSING + 0xc28), ==, 1U << 9);
    g_assert_false(qtest_get_irq(q, 110 - 32));
    qtest_writel(q, PROCESSING + 0xc24, 1U << 9);
    g_assert_true(qtest_get_irq(q, 110 - 32));
    qtest_writel(q, PROCESSING + 0xc30, 1U << 10);
    g_assert_true(qtest_get_irq(q, 110 - 32));
    qtest_writel(q, PROCESSING + 0xc30, 1U << 9);
    g_assert_false(qtest_get_irq(q, 110 - 32));
    qtest_quit(q);
}

static void test_cancel(void)
{
    static const uint32_t offset[] = { 0x454, 0xc00, 0xc00, 0 };
    static const uint32_t command[] = { 2, 0x80, 0x800, 0 };
    unsigned i;

    supplied(8192);
    for (i = 0; i < ARRAY_SIZE(command); i++) {
        QTestState *q = start(true, 0);

        configure(q, 0, 8192, OUTPUT);
        qtest_memset(q, OUTPUT, 0xa5, 8192);
        qtest_writel(q, PROCESSING + 0x454, 1);
        qtest_clock_step(q, 90000);
        verify(q, OUTPUT, 4096);
        if (i == 3) {
            qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
            qtest_qmp_eventwait(q, "RESET");
        } else {
            qtest_writel(q, PROCESSING + offset[i], command[i]);
        }
        qtest_clock_step(q, 1000000);
        verify(q, OUTPUT, 4096);
        untouched(q, OUTPUT + 4096, 4096);
        g_assert_cmphex(qtest_readl(q, PROCESSING + 0xfc0), ==, 0);
        g_assert_cmphex(qtest_readl(q, PROCESSING + 0xc28), ==, 0);
        g_assert_false(qtest_get_irq(q, 110 - 32));
        qtest_quit(q);
    }
}

static void test_bytes(void)
{
    QTestState *q;

    /* Exact legal byte extents do not acquire a sector alignment requirement. */
    supplied(51);
    q = start(true, 0);
    configure(q, 0, 51, OUTPUT);
    qtest_memset(q, OUTPUT, 0xa5, 512);
    qtest_writel(q, PROCESSING + 0x454, 1);
    qtest_clock_step(q, 1000000);
    verify(q, OUTPUT, 51);
    g_assert_cmphex(qtest_readb(q, OUTPUT + 51), ==, 0xa5);
    g_assert_cmphex(qtest_readl(q, PROCESSING + 0xc28), ==, 1U << 9);
    g_assert_true(qtest_get_irq(q, 110 - 32));
    qtest_quit(q);

    /* A 511-byte file is not a full 512-byte buffer with an invented zero. */
    supplied(511);
    q = start(true, 0);
    configure(q, 0, 512, OUTPUT);
    qtest_memset(q, OUTPUT, 0xa5, 512);
    qtest_writel(q, PROCESSING + 0x454, 1);
    qtest_clock_step(q, 1000000);
    untouched(q, OUTPUT, 512);
    g_assert_cmphex(qtest_readl(q, PROCESSING + 0xc28), ==, 0);
    g_assert_false(qtest_get_irq(q, 110 - 32));
    qtest_quit(q);
}

static void test_eof(void)
{
    QTestState *q;

    supplied(8192);
    q = start(true, 0);
    configure(q, 0, 8192, OUTPUT);
    qtest_memset(q, OUTPUT, 0xa5, 8192);
    qtest_writel(q, PROCESSING + 0x454, 1);
    qtest_clock_step(q, 90000);
    verify(q, OUTPUT, 4096);
    g_assert_cmpint(truncate(result_file, 4096), ==, 0);
    qtest_clock_step(q, 1000000);
    verify(q, OUTPUT, 4096);
    untouched(q, OUTPUT + 4096, 4096);
    g_assert_cmphex(qtest_readl(q, PROCESSING + 0xc28), ==, 0);
    g_assert_false(qtest_get_irq(q, 110 - 32));
    qtest_quit(q);
}

static void test_disconnected(void)
{
    QTestState *q = start(false, 0);

    configure(q, 0, 512, OUTPUT);
    qtest_memset(q, OUTPUT, 0xa5, 512);
    qtest_writel(q, PROCESSING + 0x454, 1);
    qtest_clock_step(q, 1000000000);
    g_assert_cmphex(qtest_readl(q, OUTPUT), ==, 0xa5a5a5a5);
    g_assert_cmphex(qtest_readl(q, PROCESSING + 0xfc0), ==, 0x80);
    g_assert_cmphex(qtest_readl(q, PROCESSING + 0xc28), ==, 0);
    g_assert_false(qtest_get_irq(q, 110 - 32));
    qtest_writel(q, PROCESSING + 0x454, 2);
    qtest_quit(q);
}

static void test_bounds(void)
{
    static const uint32_t target[] = {
        OUTPUT, 0x24074000, 0x90000000, 0, 0x10020000,
    };
    unsigned i;

    supplied(512);
    for (i = 0; i < ARRAY_SIZE(target); i++) {
        QTestState *q = start(true, 0);

        /* Case0 is a short supplied file: it cannot announce a full buffer. */
        configure(q, 0, i ? 512 : 1024, target[i]);
        qtest_memset(q, OUTPUT, 0xa5, 1024);
        qtest_writel(q, PROCESSING + 0x454, 1);
        qtest_clock_step(q, 1000000);
        g_assert_cmphex(qtest_readl(q, OUTPUT), ==, 0xa5a5a5a5);
        g_assert_cmphex(qtest_readl(q, PROCESSING + 0xc28), ==, 0);
        g_assert_false(qtest_get_irq(q, 110 - 32));
        qtest_quit(q);
    }
}

static void test_snapshot(void)
{
    QTestState *q;

    supplied(8192);
    q = start(true, 0);
    configure(q, 0, 8192, OUTPUT);
    qtest_memset(q, OUTPUT, 0xa5, 16384);
    qtest_writel(q, PROCESSING + 0x454, 1);
    qtest_writel(q, PROCESSING + 0x470, OUTPUT + 8192);
    qtest_writel(q, PROCESSING + 0x494, 4096);
    qtest_clock_step(q, 1000000);
    verify(q, OUTPUT, 8192);
    g_assert_cmphex(qtest_readl(q, OUTPUT + 8192), ==, 0xa5a5a5a5);
    g_assert_cmphex(qtest_readl(q, PROCESSING + 0xc28), ==, 1U << 9);
    qtest_quit(q);
}

static void test_inputs(void)
{
    static const unsigned selector[] = { 0, 1, 2, 3, 0x20, 0x21, 0x40, 0x41,
                                         0x60, 0x61, 0x6a, 0x6b };
    QTestState *q = start(false, 0);
    unsigned i;

    /* Actual104b9b7a has independent role/channel/field/index address pairs. */
    for (i = 0; i < ARRAY_SIZE(selector); i++) {
        qtest_writel(q, PROCESSING + 0x438, selector[i]);
        qtest_writel(q, PROCESSING + 0x43c, OUTPUT + i * 0x1000);
    }
    for (i = 0; i < ARRAY_SIZE(selector); i++) {
        qtest_writel(q, PROCESSING + 0x438, selector[i]);
        g_assert_cmphex(qtest_readl(q, PROCESSING + 0x43c), ==,
                        OUTPUT + i * 0x1000);
    }
    qtest_writel(q, PROCESSING + 0xe00, 0x8100);
    qtest_writel(q, PROCESSING + 0xe04, 0x2040);
    qtest_writel(q, PROCESSING + 0x480, 0x60000040);
    qtest_writel(q, PROCESSING + 0xefc, 200);
    qtest_clock_step(q, 1000000);
    g_assert_cmphex(qtest_readl(q, PROCESSING + 0xe00), ==, 0x8100);
    g_assert_cmphex(qtest_readl(q, PROCESSING + 0xe04), ==, 0x2040);
    g_assert_cmphex(qtest_readl(q, PROCESSING + 0x480), ==, 0x60000040);
    g_assert_cmphex(qtest_readl(q, PROCESSING + 0xefc), ==, 200);
    g_assert_cmphex(qtest_readl(q, PROCESSING + 0xc28), ==, 0);
    g_assert_false(qtest_get_irq(q, 110 - 32));
    qtest_quit(q);
}

void nikon_processing_add_tests(const char *bios)
{
    int fd = g_file_open_tmp("nikonprocessingXXXXXX", &result_file, NULL);

    g_assert_cmpint(fd, >=, 0);
    close(fd);
    atexit(cleanup);
    image = bios;
    qtest_add_func("/nikon/processing/complete", test_complete);
    qtest_add_func("/nikon/processing/slots", test_slots);
    qtest_add_func("/nikon/processing/chunks", test_chunks);
    qtest_add_func("/nikon/processing/cancel", test_cancel);
    qtest_add_func("/nikon/processing/disconnected", test_disconnected);
    qtest_add_func("/nikon/processing/bounds", test_bounds);
    qtest_add_func("/nikon/processing/snapshot", test_snapshot);
    qtest_add_func("/nikon/processing/inputs", test_inputs);
    qtest_add_func("/nikon/processing/bytes", test_bytes);
    qtest_add_func("/nikon/processing/eof", test_eof);
}
