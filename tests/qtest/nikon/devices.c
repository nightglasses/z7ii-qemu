/* Nikon functional device and synthetic startup contracts; no camera firmware.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "qobject/qdict.h"
#include "qobject/qlist.h"
#include "hw/nikon/devices.h"
#include "../libqtest.h"
#include "fixture.h"
#include "bridge.h"

#define ROM 0x10020000
#define RAM 0xb1800000
#define MEMC 0x04264000
#define CLOCK 0x04281000
#define GPIO 0x04282000
#define EXIU 0x0426f000
#define FILL 0x20070000
#define COPY 0x04251000
#define HDMAC 0x2c020000

static char *image;

void nikon_test_completion(QTestState *q);
void nikon_test_bridge(QTestState *q);
void nikon_transfer_add_tests(const char *bios);
void nikon_sampling_add_tests(const char *bios);
void nikon_image_add_tests(const char *bios);
void nikon_sdi_add_tests(const char *bios);
void nikon_spi_add_tests(const char *bios);
void nikon_processing_add_tests(const char *bios);

static QTestState *start(bool execute, unsigned cpus)
{
    g_autofree char *quoted = g_shell_quote(image);
    g_autofree char *args = g_strdup_printf(
        "-M z7ii-mas-diagnostic -smp %u -bios %s -serial none %s",
        cpus, quoted, execute ? "-S -accel tcg,thread=single" : "");

    return qtest_init(args);
}

static void write_image(const uint32_t *words, size_t count)
{
    g_autofree uint32_t *little = g_new(uint32_t, count);
    size_t i;

    for (i = 0; i < count; i++) {
        little[i] = cpu_to_le32(words[i]);
    }
    g_assert_true(g_file_set_contents(image, (char *)little,
                                     count * sizeof(*little), NULL));
}

static uint32_t reg(QTestState *q, unsigned cpu, const char *name)
{
    QDict *response = qtest_qmp(q, "{'execute': 'human-monitor-command',"
                              " 'arguments': {'command-line': 'info registers',"
                              " 'cpu-index': %u}}", cpu);
    const char *text = qdict_get_str(response, "return");
    const char *value = strstr(text, name);
    uint32_t result;

    g_assert_nonnull(value);
    result = strtoul(value + strlen(name), NULL, 16);
    qobject_unref(response);
    return result;
}

static void wait_reg(QTestState *q, unsigned cpu, const char *name,
                     uint32_t expected)
{
    int64_t deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;

    while (reg(q, cpu, name) != expected) {
        g_assert_cmpint(g_get_monotonic_time(), <, deadline);
        g_usleep(1000);
    }
}

static void test_boot(void)
{
    /* Synthetic MOV r4,#0x5a; B . proves actual TCG execution, not just reset. */
    const uint32_t words[] = { 0xe3a0405a, 0xeafffffe };
    QTestState *q;

    write_image(words, ARRAY_SIZE(words));
    q = start(true, 1);
    g_assert_cmphex(reg(q, 0, "R15="), ==, ROM);
    g_assert_cmphex(reg(q, 0, "PSR=") & 0x3f, ==, 0x13);
    qtest_qmp_assert_success(q, "{'execute': 'cont'}");
    wait_reg(q, 0, "R04=", 0x5a);
    qtest_qmp_assert_success(q, "{'execute': 'stop'}");
    g_assert_cmphex(reg(q, 0, "R15="), ==, ROM + 4);
    g_assert_cmphex(qtest_readl(q, ROM), ==, words[0]);
    g_assert_cmphex(qtest_readl(q, ROM + 4), ==, words[1]);
    qtest_quit(q);
}

static void constant(GArray *code, unsigned reg, uint32_t value)
{
    uint32_t low = value & 0xffff, high = value >> 16;
    uint32_t words[] = {
        0xe3000000 | (low & 0xf000) << 4 | reg << 12 | (low & 0xfff),
        0xe3400000 | (high & 0xf000) << 4 | reg << 12 | (high & 0xfff),
    };

    g_array_append_vals(code, words, ARRAY_SIZE(words));
}

static void test_smp(void)
{
    g_autoptr(GArray) setup = g_array_new(false, false, sizeof(uint32_t));
    g_autoptr(GArray) code = g_array_new(false, false, sizeof(uint32_t));
    const uint32_t master[] = {
        0xe3a03000, 0xe5803000, 0xf57ff04f, 0xe320f004,
        0xe5914000, 0xe3540001, 0x1afffffc,
        0xe5802000, 0xf57ff04f, 0xe320f004,
        0xe5915004, 0xe3550001, 0x1afffffc, 0xeafffffe,
    };
    const uint32_t secondary[] = {
        0xe3a03001, 0xe5813000, 0xf57ff04f, 0xe320f004,
        0xe320f002, 0xe5904000, 0xe1540002, 0x1afffffb,
        0xe3a03000, 0xe5803000, 0xf57ff04f,
        0xe3a03001, 0xe5813004, 0xe320f004, 0xeafffffe,
    };
    uint32_t entry[] = { 0xee107fb0, 0xe207700f, 0xe3570000, 0 };
    unsigned secondary_index;
    QTestState *q;

    constant(setup, 0, 0x04008ffc);
    constant(setup, 1, RAM);
    constant(setup, 2, 0xbabebabe);
    secondary_index = 4 + setup->len + ARRAY_SIZE(master);
    entry[3] = 0x1a000000 | (secondary_index - 5);
    g_array_append_vals(code, entry, ARRAY_SIZE(entry));
    g_array_append_vals(code, setup->data, setup->len);
    g_array_append_vals(code, master, ARRAY_SIZE(master));
    g_array_append_vals(code, setup->data, setup->len);
    g_array_append_vals(code, secondary, ARRAY_SIZE(secondary));
    write_image((uint32_t *)code->data, code->len);
    q = start(true, 2);
    qtest_qmp_assert_success(q, "{'execute': 'cont'}");
    wait_reg(q, 0, "R15=", ROM + (secondary_index - 1) * 4);
    wait_reg(q, 1, "R15=", ROM + (code->len - 1) * 4);
    qtest_qmp_assert_success(q, "{'execute': 'stop'}");
    g_assert_cmphex(reg(q, 1, "R07="), ==, 1);
    g_assert_cmphex(qtest_readl(q, RAM), ==, 1);
    g_assert_cmphex(qtest_readl(q, RAM + 4), ==, 1);
    g_assert_cmphex(qtest_readl(q, 0x04008ffc), ==, 0);
    qtest_quit(q);
}

static void test_memc(void)
{
    QTestState *q = start(false, 1);

    qtest_writel(q, MEMC, 0x21);
    qtest_writel(q, MEMC + 0x20, 0x153b0902);
    g_assert_cmphex(qtest_readl(q, MEMC), ==, 0x21);
    g_assert_cmphex(qtest_readl(q, MEMC + 0x20), ==, 0x153b0902);
    qtest_writel(q, MEMC, 0x29);
    g_assert_cmphex(qtest_readl(q, MEMC), ==, 0x29);
    qtest_quit(q);
}

static void test_clock(void)
{
    QTestState *q = start(false, 1);
    unsigned bank;

    for (bank = 0; bank < 6; bank++) {
        uint64_t address = CLOCK + 0x38 + bank * 4;

        g_assert_cmphex(qtest_readl(q, address), ==, 0);
        qtest_writel(q, address + 0x100, 1);
        g_assert_cmphex(qtest_readl(q, address), ==, 1);
        qtest_writel(q, address + 0x200, 1);
        g_assert_cmphex(qtest_readl(q, address), ==, 0);
    }
    qtest_quit(q);
}

static void test_completion(void)
{
    QTestState *q = start(false, 1);

    nikon_intercept_gic(q);
    nikon_test_completion(q);
    qtest_quit(q);
}

static void test_bridge(void)
{
    QTestState *q = start(false, 1);

    nikon_test_bridge(q);
    qtest_quit(q);
}

static void test_gpio_input(void)
{
    QTestState *q = start(false, 1);
    g_autofree char *path = nikon_device_path(q, TYPE_NIKON_GPIO);
    unsigned port;

    /* No producer: input follows the explicitly configured pull profile. */
    for (port = 0; port < 12; port++) {
        uint64_t offset = port * 4;

        qtest_writel(q, GPIO + 0x300 + offset, 0xffff);
        qtest_writel(q, GPIO + 0x400 + offset, 0xffff);
        g_assert_cmphex(qtest_readl(q, GPIO + offset), ==, 0xffff);
        for (unsigned pin = 0; pin < 16; pin++) {
            unsigned index = port * 16 + pin;
            unsigned bit = 1U << pin;

            qtest_set_irq_in(q, path, "external-level", index, 0);
            g_assert_cmphex(qtest_readl(q, GPIO + offset) & bit, ==, 0);
            qtest_set_irq_in(q, path, "external-level", index, 1);
            g_assert_cmphex(qtest_readl(q, GPIO + offset) & bit, ==, bit);
        }
        /* Output takes priority, and alternate function restores inputs. */
        qtest_writel(q, GPIO + 0x100 + offset, 0xffff);
        qtest_writel(q, GPIO + offset, 0xffff0000);
        g_assert_cmphex(qtest_readl(q, GPIO + offset), ==, 0);
        qtest_writel(q, GPIO + 0x200 + offset, 0xffff);
        g_assert_cmphex(qtest_readl(q, GPIO + offset), ==, 0xffff);
    }
    qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
    /* Configuration resets, while external producers remain connected. */
    for (port = 0; port < 12; port++) {
        g_assert_cmphex(qtest_readl(q, GPIO + port * 4), ==, 0xffff);
    }
    qtest_quit(q);
}

static void test_gpio_irq(void)
{
    QTestState *q = start(false, 1);

    nikon_intercept_gic(q);
    qtest_writel(q, EXIU, 0xfffe);
    qtest_writel(q, GPIO + 0x114, 0x80);
    qtest_writel(q, GPIO + 0x14, 0x00800080);
    g_assert_cmphex(qtest_readl(q, GPIO + 0x14), ==, 0x80);
    g_assert_cmphex(qtest_readl(q, EXIU + 0xc), ==, 1);
    g_assert_true(qtest_get_irq(q, 65 - 32));
    /* CLR is a command register: read zero, acknowledge only selected bits. */
    g_assert_cmphex(qtest_readl(q, EXIU + 0x10), ==, 0);
    qtest_writel(q, EXIU + 0x10, 1);
    g_assert_false(qtest_get_irq(q, 65 - 32));
    qtest_quit(q);
}

static void test_gpio_level(void)
{
    QTestState *q = start(false, 1);

    qtest_writel(q, EXIU + 0x18, 0xfffe);
    qtest_writel(q, EXIU, 0xfffe);
    qtest_writel(q, GPIO + 0x114, 0x80);
    qtest_writel(q, GPIO + 0x14, 0x00800080);
    qtest_writel(q, EXIU + 0x10, 1);
    g_assert_cmphex(qtest_readl(q, EXIU + 8), ==, 1);
    qtest_writel(q, GPIO + 0x14, 0x00800000);
    qtest_writel(q, EXIU + 0x10, 1);
    g_assert_cmphex(qtest_readl(q, EXIU + 8), ==, 0);
    qtest_quit(q);
}

static void test_fill(void)
{
    uint8_t bytes[0x4000];
    QTestState *q = start(false, 1);
    unsigned engine;

    for (engine = 0; engine < 2; engine++) {
        uint64_t base = FILL + engine * 0x40;
        uint64_t destination = 0xb18aedc0 + engine * sizeof(bytes);

        memset(bytes, 0xa5, sizeof(bytes));
        qtest_memwrite(q, destination, bytes, sizeof(bytes));
        qtest_writel(q, base, 0);
        qtest_writel(q, base + 4, 0x11);
        qtest_writel(q, base + 0x14, 1);
        qtest_writel(q, base + 0x18, 0);
        qtest_writel(q, base + 0x1c, 0);
        qtest_writel(q, base + 0x20, 0x4000);
        qtest_writel(q, base + 0x24, 0x4000);
        qtest_writel(q, base + 0x28, 1);
        qtest_writel(q, base + 0x2c, 0);
        qtest_writel(q, base + 0x30, destination);
        qtest_writel(q, base + 0x10, 1);
        g_assert_cmphex(qtest_readl(q, base + 8), ==, 1);
        qtest_memread(q, destination, bytes, sizeof(bytes));
        for (unsigned i = 0; i < sizeof(bytes); i++) {
            g_assert_cmphex(bytes[i], ==, 0);
        }
        qtest_writel(q, base + 8, 1);
        g_assert_cmphex(qtest_readl(q, base + 8), ==, 0);
    }
    qtest_quit(q);
}

static void copy_setup(QTestState *q, unsigned channel, unsigned length)
{
    uint64_t base = COPY + 0x10 + channel * 0x30;

    qtest_writel(q, COPY, 0x10000000);
    qtest_writel(q, base, length - 1);
    qtest_writel(q, base + 4, RAM + 0x100);
    qtest_writel(q, base + 8, RAM + 0x40000);
    qtest_writel(q, base + 12, 0x30f00);
    qtest_writel(q, base + 16, 0x30f00);
    qtest_writel(q, base + 20, 0);
    qtest_writel(q, base + 24,
                 (qtest_readl(q, base + 24) & ~0x00f09110U) | 0x9000);
}

static void copy_complete(QTestState *q, unsigned length)
{
    /* One late callback must commit every byte whose absolute time is due. */
    qtest_clock_step(q, (uint64_t)length * 1000000000 / 48000000 + 1);
}

static void copy_request(QTestState *q, unsigned channel, uint32_t source,
                         uint32_t destination, unsigned length)
{
    uint64_t base = COPY + 0x10 + channel * 0x30;

    copy_setup(q, channel, length);
    qtest_writel(q, base + 4, source);
    qtest_writel(q, base + 8, destination);
    qtest_writel(q, base + 24, 0x11109003);
}

static void copy_pattern(uint8_t *data, unsigned length, unsigned seed)
{
    for (unsigned i = 0; i < length; i++) {
        data[i] = seed ^ i ^ (i >> 8) ^ (i >> 16);
    }
}

static void test_copy_large_jump(void)
{
    const unsigned sizes[] = { 3 * 1024 * 1024, 8 * 1024 * 1024 };
    g_autofree uint8_t *expected = g_malloc(sizes[1]);
    g_autofree uint8_t *actual = g_malloc(sizes[1]);
    QTestState *q = start(false, 1);

    nikon_intercept_gic(q);
    for (unsigned i = 0; i < ARRAY_SIZE(sizes); i++) {
        unsigned length = sizes[i];
        uint32_t source = RAM + 0x100000;
        uint32_t destination = RAM + 0x2000000;

        copy_pattern(expected, length, i + 0x35);
        qtest_memwrite(q, source, expected, length);
        qtest_memset(q, destination, 0, length);
        copy_request(q, 0, source, destination, length);
        g_assert_cmphex(qtest_readl(q, COPY + 0x30), ==, 0);
        g_assert_false(qtest_get_irq(q, 45 - 32));
        g_assert_cmphex(qtest_readb(q, destination), ==, 0);
        copy_complete(q, length);
        qtest_memread(q, destination, actual, length);
        g_assert_cmpmem(actual, length, expected, length);
        g_assert_cmphex(qtest_readl(q, COPY + 0x14), ==, source + length);
        g_assert_cmphex(qtest_readl(q, COPY + 0x18), ==, destination + length);
        g_assert_cmphex(qtest_readl(q, COPY + 0x10), ==, 0);
        g_assert_cmphex(qtest_readl(q, COPY + 0x30), ==, 8);
        g_assert_true(qtest_get_irq(q, 45 - 32));
        /* Writing the existing bit preserves it; zero acknowledges it. */
        qtest_writel(q, COPY + 0x30, 8);
        g_assert_true(qtest_get_irq(q, 45 - 32));
        qtest_writel(q, COPY + 0x30, 0);
        g_assert_false(qtest_get_irq(q, 45 - 32));
    }
    qtest_quit(q);
}

static void test_copy_partial_cancel(void)
{
    const unsigned length = 3 * 1024 * 1024, progress = 65536;
    g_autofree uint8_t *expected = g_malloc(length);
    g_autofree uint8_t *actual = g_malloc(length);
    QTestState *q = start(false, 1);

    copy_pattern(expected, length, 0x75);
    qtest_memwrite(q, RAM + 0x100000, expected, length);
    qtest_memset(q, RAM + 0x2000000, 0, length);
    nikon_intercept_gic(q);
    copy_request(q, 0, RAM + 0x100000, RAM + 0x2000000, length);
    copy_complete(q, progress);
    g_assert_cmphex(qtest_readl(q, COPY + 0x10), ==, length - progress - 1);
    g_assert_cmphex(qtest_readl(q, COPY + 0x14), ==,
                   RAM + 0x100000 + progress);
    g_assert_cmphex(qtest_readl(q, COPY + 0x18), ==,
                   RAM + 0x2000000 + progress);
    g_assert_cmphex(qtest_readl(q, COPY + 0x30), ==, 0);
    g_assert_false(qtest_get_irq(q, 45 - 32));
    qtest_memread(q, RAM + 0x2000000, actual, length);
    g_assert_cmpmem(actual, progress, expected, progress);
    for (unsigned i = progress; i < length; i++) {
        g_assert_cmphex(actual[i], ==, 0);
    }
    qtest_writel(q, COPY + 0x28, 0x11109000);
    copy_complete(q, length);
    g_assert_cmphex(qtest_readl(q, COPY + 0x30), ==, 1);
    g_assert_cmphex(qtest_readl(q, COPY + 0x10), ==, length - progress - 1);
    g_assert_true(qtest_get_irq(q, 45 - 32));
    qtest_memread(q, RAM + 0x2000000, actual, length);
    memset(expected + progress, 0, length - progress);
    g_assert_cmpmem(actual, length, expected, length);
    qtest_writel(q, COPY + 0x30, 0);
    g_assert_false(qtest_get_irq(q, 45 - 32));
    qtest_quit(q);
}

static void test_copy_concurrent(void)
{
    const unsigned progress = 65536;
    g_autofree uint8_t *expected = g_malloc(4 * progress);
    g_autofree uint8_t *actual = g_malloc(4 * progress);
    QTestState *q = start(false, 1);

    nikon_intercept_gic(q);
    for (unsigned channel = 0; channel < 4; channel++) {
        unsigned length = (channel + 1) * progress;
        uint32_t source = RAM + 0x100000 + channel * 0x100000;
        uint32_t destination = RAM + 0x2000000 + channel * 0x100000;

        copy_pattern(expected, length, channel + 0x51);
        qtest_memwrite(q, source, expected, length);
        qtest_memset(q, destination, 0, length);
        copy_request(q, channel, source, destination, length);
    }
    copy_complete(q, progress);
    for (unsigned channel = 0; channel < 4; channel++) {
        unsigned length = (channel + 1) * progress;
        uint64_t base = COPY + 0x10 + channel * 0x30;

        g_assert_cmphex(qtest_readl(q, base), ==,
                       channel ? length - progress - 1 : 0);
        g_assert_cmphex(qtest_readl(q, base + 32), ==, channel ? 0 : 8);
        g_assert_cmpint(qtest_get_irq(q, 45 + channel - 32), ==, !channel);
        qtest_memread(q, RAM + 0x2000000 + channel * 0x100000,
                      actual, length);
        copy_pattern(expected, length, channel + 0x51);
        g_assert_cmpmem(actual, progress, expected, progress);
        for (unsigned i = progress; i < length; i++) {
            g_assert_cmphex(actual[i], ==, 0);
        }
    }
    copy_complete(q, 3 * progress);
    for (unsigned channel = 0; channel < 4; channel++) {
        unsigned length = (channel + 1) * progress;
        uint64_t status = COPY + 0x30 + channel * 0x30;

        copy_pattern(expected, length, channel + 0x51);
        qtest_memread(q, RAM + 0x2000000 + channel * 0x100000,
                      actual, length);
        g_assert_cmpmem(actual, length, expected, length);
        g_assert_cmphex(qtest_readl(q, status), ==, 8);
        g_assert_true(qtest_get_irq(q, 45 + channel - 32));
        qtest_writel(q, status, 0);
        g_assert_false(qtest_get_irq(q, 45 + channel - 32));
    }
    qtest_quit(q);
}

static void test_copy_romd(void)
{
    const unsigned length = 3 * 1024 * 1024;
    const uint32_t idle[] = { 0xeafffffe };
    g_autofree uint8_t *expected = g_malloc(length);
    g_autofree uint8_t *actual = g_malloc(length);
    QTestState *q;

    copy_pattern(expected, length, 0x93);
    g_assert_true(g_file_set_contents(image, (char *)expected, length, NULL));
    q = start(false, 1);
    nikon_intercept_gic(q);
    copy_request(q, 0, ROM, RAM + 0x2000000, length);
    copy_complete(q, length);
    qtest_memread(q, RAM + 0x2000000, actual, length);
    g_assert_cmpmem(actual, length, expected, length);
    qtest_memread(q, ROM, actual, length);
    g_assert_cmpmem(actual, length, expected, length);
    g_assert_cmphex(qtest_readl(q, COPY + 0x30), ==, 8);
    g_assert_true(qtest_get_irq(q, 45 - 32));
    qtest_writel(q, COPY + 0x30, 0);
    /* ROMD is readable source backing, never an accepted destination. */
    copy_request(q, 0, RAM + 0x2000000, ROM, length);
    copy_complete(q, length);
    g_assert_cmphex(qtest_readl(q, COPY + 0x30), ==, 1);
    g_assert_true(qtest_get_irq(q, 45 - 32));
    qtest_memread(q, ROM, actual, length);
    g_assert_cmpmem(actual, length, expected, length);
    qtest_writel(q, COPY + 0x30, 0);
    copy_request(q, 0, 0xdead0000, RAM + 0x2000000, 7);
    copy_complete(q, 7);
    g_assert_cmphex(qtest_readl(q, COPY + 0x30), ==, 1);
    g_assert_cmphex(qtest_readl(q, COPY + 0x10), ==, 6);
    qtest_quit(q);
    write_image(idle, ARRAY_SIZE(idle));
}

static uint64_t copy_property(QTestState *q, const char *name)
{
    QDict *response = qtest_qmp(q, "{'execute':'qom-get',"
                              " 'arguments':{'path':'/machine/copy',"
                              " 'property':%s}}", name);
    uint64_t value;

    g_assert_true(qdict_haskey(response, "return"));
    value = qdict_get_uint(response, "return");
    qobject_unref(response);
    return value;
}

static void test_copy_telemetry(void)
{
    const unsigned length = 3 * 1024 * 1024, progress = 65536;
    QTestState *q = start(false, 1);
    uint64_t start_ns = copy_property(q, "virtual-clock-ns");
    uint64_t deadline;
    QDict *response;

    qtest_memset(q, RAM + 0x100000, 0x5a, length);
    qtest_memset(q, RAM + 0x2000000, 0, length);
    copy_request(q, 3, RAM + 0x100000, RAM + 0x2000000, length);
    g_assert_cmpuint(copy_property(q, "channel3-start-ns"), ==, start_ns);
    g_assert_cmpuint(copy_property(q, "channel3-total-bytes"), ==, length);
    g_assert_true(qtest_qom_get_bool(q, "/machine/copy", "channel3-running"));
    deadline = copy_property(q, "channel3-next-deadline-ns");
    g_assert_cmpuint(deadline, >, start_ns);
    qtest_clock_step(q, deadline - start_ns);
    g_assert_cmpuint(copy_property(q, "virtual-clock-ns"), ==, deadline);
    g_assert_cmpuint(copy_property(q, "channel3-remaining-bytes"), ==,
                     length - progress);
    g_assert_cmpuint(copy_property(q, "channel3-next-deadline-ns"), >, deadline);
    g_assert_cmpuint(copy_property(q, "channel0-total-bytes"), ==, 0);
    g_assert_cmphex(qtest_readb(q, RAM + 0x2000000 + progress - 1), ==, 0x5a);
    g_assert_cmphex(qtest_readb(q, RAM + 0x2000000 + progress), ==, 0);
    response = qtest_qmp(q, "{'execute':'qom-set',"
                        " 'arguments':{'path':'/machine/copy',"
                        " 'property':'channel3-remaining-bytes','value':0}}");
    g_assert_true(qdict_haskey(response, "error"));
    qobject_unref(response);
    qtest_writel(q, COPY, 0);
    g_assert_cmpuint(copy_property(q, "channel3-remaining-bytes"), ==,
                     length - progress);
    g_assert_cmpuint(copy_property(q, "channel3-next-deadline-ns"), ==, 0);
    g_assert_false(qtest_qom_get_bool(q, "/machine/copy", "channel3-running"));
    qtest_quit(q);
}

static void test_copy(void)
{
    uint8_t expected[513], actual[513];
    QTestState *q = start(false, 1);
    unsigned channel;

    for (unsigned i = 0; i < sizeof(expected); i++) {
        expected[i] = i;
    }
    qtest_memwrite(q, RAM + 0x100, expected, sizeof(expected));
    nikon_intercept_gic(q);
    for (channel = 0; channel < 4; channel++) {
        unsigned irq = 45 + channel;
        uint64_t status = COPY + 0x30 + channel * 0x30;

        copy_setup(q, channel, sizeof(expected));
        qtest_writel(q, COPY + 0x28 + channel * 0x30, 0x11109003);
        copy_complete(q, sizeof(expected));
        g_assert_cmphex(qtest_readl(q, status), ==, 8);
        qtest_memread(q, RAM + 0x40000, actual, sizeof(actual));
        g_assert_cmpmem(actual, sizeof(actual), expected, sizeof(expected));
        g_assert_true(qtest_get_irq(q, irq - 32));
        qtest_writel(q, status, 0);
        g_assert_false(qtest_get_irq(q, irq - 32));
    }
    qtest_quit(q);
}

static void test_copy_cancel(void)
{
    QTestState *q = start(false, 1);

    qtest_writel(q, RAM + 0x100, 0x12345678);
    copy_setup(q, 0, 513);
    qtest_writel(q, COPY + 0x28, 0x11109003);
    qtest_writel(q, COPY, 0);
    qtest_clock_step(q, 100000);
    g_assert_cmphex(qtest_readl(q, COPY + 0x30), ==, 1);
    g_assert_cmphex(qtest_readl(q, RAM + 0x40000), ==, 0);
    qtest_quit(q);
}

static void test_hdmac(void)
{
    QTestState *q = start(false, 1);

    nikon_intercept_gic(q);
    qtest_writel(q, RAM + 0x100, 0x11223344);
    qtest_writel(q, RAM + 0x104, 0x55667788);
    qtest_writel(q, HDMAC, 0x90000000);
    qtest_writel(q, HDMAC + 0x18, RAM + 0x100);
    qtest_writel(q, HDMAC + 0x1c, RAM + 0x200);
    qtest_writel(q, HDMAC + 0x14, 0x08180000);
    qtest_writel(q, HDMAC + 0x10, 0xa0000001);
    qtest_clock_step(q, 100000);
    g_assert_cmphex(qtest_readl(q, RAM + 0x200), ==, 0x11223344);
    g_assert_cmphex(qtest_readl(q, RAM + 0x204), ==, 0x55667788);
    g_assert_cmphex(qtest_readl(q, HDMAC + 0x14), ==, 0x081d0000);
    g_assert_true(qtest_get_irq(q, 150 - 32));
    qtest_writel(q, HDMAC + 0x14, 0x08000000);
    g_assert_false(qtest_get_irq(q, 150 - 32));
    qtest_quit(q);
}

static void test_hdmac_pending(void)
{
    QTestState *q = start(false, 1);

    qtest_writel(q, HDMAC, 0x90000000);
    qtest_writel(q, HDMAC + 0x18, RAM + 0x100);
    qtest_writel(q, HDMAC + 0x1c, RAM + 0x200);
    qtest_writel(q, HDMAC + 0x14, 0x28180000);
    qtest_writel(q, HDMAC + 0x10, 0x81000001);
    qtest_clock_step(q, 100000);
    g_assert_cmphex(qtest_readl(q, HDMAC + 0x14), ==, 0x28180000);
    g_assert_cmphex(qtest_readl(q, RAM + 0x200), ==, 0);
    qtest_quit(q);
}

static void test_dma_bench(void)
{
    const unsigned length = 0x20000;
    g_autofree uint8_t *data = g_malloc(length);
    g_autofree uint8_t *actual = g_malloc(length);
    unsigned rounds = g_test_perf() ? 32 : 1;
    QTestState *q = start(false, 1);

    memset(data, 0x5a, length);
    qtest_memwrite(q, RAM + 0x100, data, length);
    g_test_timer_start();
    for (unsigned i = 0; i < rounds; i++) {
        copy_setup(q, 0, length);
        qtest_writel(q, COPY + 0x28, 0x11109003);
        copy_complete(q, length);
        g_assert_cmphex(qtest_readl(q, COPY + 0x30), ==, 8);
        qtest_writel(q, COPY + 0x30, 0);
    }
    if (g_test_perf()) {
        g_test_maximized_result((double)length * rounds / g_test_timer_elapsed(),
                                "byte-copy throughput (bytes/host-second)");
    }
    qtest_memread(q, RAM + 0x40000, actual, length);
    g_assert_cmpmem(actual, length, data, length);
    qtest_quit(q);
}

int main(int argc, char **argv)
{
    const uint32_t idle[] = { 0xeafffffe };
    int fd, result;

    g_test_init(&argc, &argv, NULL);
    fd = g_file_open_tmp("nikonXXXXXX", &image, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    write_image(idle, ARRAY_SIZE(idle));
    qtest_add_func("/nikon/boot", test_boot);
    qtest_add_func("/nikon/smp", test_smp);
    qtest_add_func("/nikon/memc", test_memc);
    qtest_add_func("/nikon/clock", test_clock);
    qtest_add_func("/nikon/gpio/irq", test_gpio_irq);
    qtest_add_func("/nikon/gpio/level", test_gpio_level);
    qtest_add_func("/nikon/gpio/input", test_gpio_input);
    qtest_add_func("/nikon/completion", test_completion);
    qtest_add_func("/nikon/bridge", test_bridge);
    nikon_transfer_add_tests(image);
    nikon_sampling_add_tests(image);
    nikon_image_add_tests(image);
    nikon_sdi_add_tests(image);
    nikon_spi_add_tests(image);
    nikon_processing_add_tests(image);
    nikon_bridge_add_tests();
    qtest_add_func("/nikon/fill", test_fill);
    qtest_add_func("/nikon/copy/transfer", test_copy);
    qtest_add_func("/nikon/copy/cancel", test_copy_cancel);
    qtest_add_func("/nikon/copy/large-jump", test_copy_large_jump);
    qtest_add_func("/nikon/copy/partial-cancel", test_copy_partial_cancel);
    qtest_add_func("/nikon/copy/concurrent", test_copy_concurrent);
    qtest_add_func("/nikon/copy/romd", test_copy_romd);
    qtest_add_func("/nikon/copy/telemetry", test_copy_telemetry);
    qtest_add_func("/nikon/hdmac/transfer", test_hdmac);
    qtest_add_func("/nikon/hdmac/pending", test_hdmac_pending);
    qtest_add_func("/nikon/bench/dma", test_dma_bench);
    result = g_test_run();
    unlink(image);
    g_free(image);
    return result;
}
