/* Source-packet image composition, actual console pixels and causal IRQ111.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "fixture.h"

#define BANK 0x20140000
#define RAM 0xb1810000
#define TARGET (RAM + 0x10000)
#define FRAME_PENDING ((1U << 1) | (1U << 16))

static const char *bios_image;

void nikon_image_add_tests(const char *bios);

static QTestState *start(void)
{
    g_autofree char *quoted = g_shell_quote(bios_image);
    g_autofree char *args = g_strdup_printf(
        "-M z7ii-mas-diagnostic -smp 2 -bios %s -nodefaults", quoted);
    QTestState *q = qtest_init(args);

    nikon_intercept_gic(q);
    return q;
}

static void packet(QTestState *q, unsigned bank, unsigned mode)
{
    uint32_t base = BANK + bank * 0x4000;
    unsigned bytes = mode ? 2 : 4;

    qtest_writel(q, base + 0x1100, 1);
    qtest_writel(q, base + 0x1308, (8U << 16) | 8);
    qtest_writel(q, base + 0x1c00, 0);
    qtest_writel(q, base + 0x1c04, 0);
    qtest_writel(q, base + 0x2100, 1);
    qtest_writel(q, base + 0x2200, mode);
    qtest_writel(q, base + 0x2204, (8U << 16) | 8);
    qtest_writel(q, base + 0x2210, 0x03000102);
    qtest_writel(q, base + 0x2400, (8U << 16) | 8);
    qtest_writel(q, base + 0x2440, RAM);
    qtest_writel(q, base + 0x24c0, RAM + 0x4000);
    qtest_writel(q, base + 0x2500, 8 * bytes + 8);
    qtest_writel(q, base + 0x2540, mode == 5 ? 4 : 8);
    qtest_writel(q, base + 0x2580, 0);
    qtest_writel(q, base + 0x25c0, 1);
}

static void wait_completion(QTestState *q)
{
    int64_t now = qtest_clock_step(q, 1);
    int64_t deadline = now + 1000000;
    unsigned events = 0;

    while (!qtest_get_irq(q, 111 - 32)) {
        now = qtest_clock_step_next(q);
        g_assert_cmpint(now, <=, deadline);
        g_assert_cmpuint(++events, <=, 64);
    }
}

static char *pixels_head(QTestState *q, unsigned head)
{
    const char *header = "P6\n8 8\n255\n";
    g_autofree char *path = NULL;
    char *contents = NULL;
    gsize length;
    int fd = g_file_open_tmp("nikonXXXXXX", &path, NULL);
    QDict *reply;

    g_assert_cmpint(fd, >=, 0);
    close(fd);
    reply = qtest_qmp(q, "{'execute':'screendump','arguments':"
                        "{'filename':%s,'format':'ppm',"
                        "'device':'display','head':%d}}", path, (int)head);
    g_assert_false(qdict_haskey(reply, "error"));
    qobject_unref(reply);
    g_assert_true(g_file_get_contents(path, &contents, &length, NULL));
    g_assert_cmpuint(length, ==, strlen(header) + 64 * 3);
    g_assert_true(g_str_has_prefix(contents, header));
    memmove(contents, contents + strlen(header), 64 * 3);
    unlink(path);
    return contents;
}

static char *pixels(QTestState *q)
{
    return pixels_head(q, 0);
}

static void assert_color(const char *image, unsigned red, unsigned green,
                         unsigned blue)
{
    unsigned pixel;

    for (pixel = 0; pixel < 64; pixel++) {
        g_assert_cmpuint((uint8_t)image[pixel * 3], ==, red);
        g_assert_cmpuint((uint8_t)image[pixel * 3 + 1], ==, green);
        g_assert_cmpuint((uint8_t)image[pixel * 3 + 2], ==, blue);
    }
}

static void test_frame(void)
{
    QTestState *q = start();
    uint8_t source[8 * 40], target[8 * 8 * 4];
    g_autofree char *image = NULL;
    unsigned x, y;

    memset(source, 0xa5, sizeof(source));
    for (y = 0; y < 8; y++) {
        for (x = 0; x < 8; x++) {
            stl_le_p(source + y * 40 + x * 4, 0xff12e534);
        }
    }
    qtest_memwrite(q, RAM, source, sizeof(source));
    qtest_memset(q, TARGET, 0, sizeof(target));
    packet(q, 0, 0);
    qtest_writel(q, BANK + 0x1200, 1);
    g_assert_cmphex(qtest_readl(q, BANK + 0x1200) & 3, ==, 1);
    g_assert_false(qtest_get_irq(q, 111 - 32));
    qtest_memread(q, TARGET, target, sizeof(target));
    g_assert_cmpmem(target, sizeof(target), (uint8_t[256]) { 0 }, 256);
    wait_completion(q);
    qtest_memread(q, TARGET, target, sizeof(target));
    /* The original bitmap is not a guessed RAM output address. */
    g_assert_cmpmem(target, sizeof(target), (uint8_t[256]) { 0 }, 256);
    image = pixels(q);
    assert_color(image, 0x12, 0xe5, 0x34);
    g_assert_cmphex(qtest_readl(q, BANK + 0x1214), ==, FRAME_PENDING);
    g_assert_cmphex(qtest_readl(q, BANK + 0x1200) & 3, ==, 3);
    qtest_writel(q, BANK + 0x1214, FRAME_PENDING);
    g_assert_false(qtest_get_irq(q, 111 - 32));
    qtest_quit(q);
}

static void test_empty(void)
{
    QTestState *q = start();
    g_autofree char *path = NULL;
    QDict *reply;
    int fd = g_file_open_tmp("nikon-empty-XXXXXX", &path, NULL);

    g_assert_cmpint(fd, >=, 0);
    close(fd);
    qtest_writel(q, BANK + 0x1100, 1);
    qtest_writel(q, BANK + 0x1210, 0x0011f1f1);
    qtest_writel(q, BANK + 0x1308, (682U << 16) | 1024);
    qtest_writel(q, BANK + 0x2100, 0x11);
    qtest_writel(q, BANK + 0x2200, 0x00030006);
    qtest_writel(q, BANK + 0x2204, (682U << 16) | 1024);
    qtest_writel(q, BANK + 0x2210, 0x02000301);
    qtest_writel(q, BANK + 0x2400, (682U << 16) | 1024);
    qtest_writel(q, BANK + 0x2440, 0xcb4a7000);
    qtest_writel(q, BANK + 0x24c0, 0xcb5fc000);
    qtest_writel(q, BANK + 0x2500, 0x800);
    qtest_writel(q, BANK + 0x2540, 0x400);
    /* The firmware-created object is initially disabled. */
    g_assert_cmphex(qtest_readl(q, BANK + 0x25c0), ==, 0);
    qtest_writel(q, BANK + 0x1200, 0x11);
    qtest_clock_step(q, 20000000);
    g_assert_true(qtest_get_irq(q, 111 - 32));
    g_assert_cmphex(qtest_readl(q, BANK + 0x1200), ==, 0x13);
    g_assert_cmphex(qtest_readl(q, BANK + 0x1214), ==, FRAME_PENDING);
    reply = qtest_qmp(q, "{'execute':'qom-get','arguments':{"
                         "'path':'/machine/display','property':'irq-levels'}}");
    g_assert_cmpuint(qdict_get_uint(reply, "return"), ==, 1);
    qobject_unref(reply);

    /* The live mask inhibits semantic bit16 but leaves IRQ cause bit1. */
    g_assert_cmphex(FRAME_PENDING & ~qtest_readl(q, BANK + 0x1210), ==,
                    1U << 1);
    /* Empty completion does not fabricate or publish a visible frame. */
    reply = qtest_qmp(q, "{'execute':'screendump','arguments':"
                        "{'filename':%s,'format':'ppm',"
                        "'device':'display','head':0}}", path);
    g_assert_true(qdict_haskey(reply, "error"));
    qobject_unref(reply);
    unlink(path);
    qtest_writel(q, BANK + 0x1214, FRAME_PENDING);
    g_assert_false(qtest_get_irq(q, 111 - 32));
    reply = qtest_qmp(q, "{'execute':'qom-get','arguments':{"
                         "'path':'/machine/display','property':'irq-levels'}}");
    g_assert_cmpuint(qdict_get_uint(reply, "return"), ==, 0);
    qobject_unref(reply);
    qtest_quit(q);
}

static void test_layouts(void)
{
    QTestState *q = start();
    unsigned mode;

    qtest_memset(q, RAM, 0xff, 1024);
    qtest_memset(q, RAM + 0x4000, 0xff, 64);
    for (mode = 0; mode <= 6; mode++) {
        g_autofree char *image = NULL;
        packet(q, 0, mode);
        qtest_writel(q, BANK + 0x1200, 1);
        wait_completion(q);
        image = pixels(q);
        assert_color(image, 255, 255, 255);
        qtest_writel(q, BANK + 0x1214, FRAME_PENDING);
    }
    qtest_quit(q);
}

static void test_cancel(void)
{
    QTestState *q = start();
    uint8_t target[256];
    g_autofree char *image = NULL;

    qtest_memset(q, RAM, 0xff, 1024);
    packet(q, 0, 0);
    qtest_writel(q, BANK + 0x1200, 1);
    wait_completion(q);
    qtest_writel(q, BANK + 0x1214, FRAME_PENDING);
    qtest_memset(q, TARGET, 0, sizeof(target));
    packet(q, 0, 0);
    qtest_writel(q, BANK + 0x4c0, 0x606);
    qtest_writel(q, BANK + 0x1200, 1);
    qtest_writel(q, BANK + 0x1200, 2);
    qtest_clock_step(q, 1000000);
    qtest_memread(q, TARGET, target, sizeof(target));
    g_assert_cmpmem(target, sizeof(target), (uint8_t[256]) { 0 }, 256);
    g_assert_false(qtest_get_irq(q, 111 - 32));
    image = pixels(q);
    assert_color(image, 255, 255, 255);
    g_assert_cmphex(qtest_readl(q, BANK + 0x1214), ==, 0);
    g_assert_cmphex(qtest_readl(q, BANK + 0x4c0), ==, 0x606);
    qtest_quit(q);
}

static void test_fault(void)
{
    QTestState *q = start();
    uint8_t target[256];
    g_autofree char *image = NULL;
    unsigned i;

    qtest_memset(q, RAM, 0xff, 1024);
    packet(q, 0, 0);
    qtest_writel(q, BANK + 0x1200, 1);
    wait_completion(q);
    qtest_writel(q, BANK + 0x1214, FRAME_PENDING);
    qtest_memset(q, TARGET, 0x5a, sizeof(target));
    packet(q, 0, 0);
    /* The last layer is invalid: an earlier valid layer cannot publish. */
    qtest_writel(q, BANK + 0x2404, (8U << 16) | 8);
    qtest_writel(q, BANK + 0x2480, 0x20180000);
    qtest_writel(q, BANK + 0x2504, 32);
    qtest_writel(q, BANK + 0x25c0, 3);
    qtest_writel(q, BANK + 0x1200, 1);
    qtest_clock_step(q, 1000000);
    qtest_memread(q, TARGET, target, sizeof(target));
    for (i = 0; i < sizeof(target); i++) {
        g_assert_cmphex(target[i], ==, 0x5a);
    }
    g_assert_false(qtest_get_irq(q, 111 - 32));
    image = pixels(q);
    assert_color(image, 255, 255, 255);
    qtest_quit(q);
}

static void test_ports(void)
{
    QTestState *q = start();
    uint8_t source[256];
    g_autofree char *image = NULL;
    unsigned pixel;

    for (pixel = 0; pixel < 64; pixel++) {
        stl_le_p(source + pixel * 4, 0xff0000ff);
    }
    qtest_memset(q, RAM, 0, 1024);
    qtest_memwrite(q, RAM + 0x8000, source, sizeof(source));
    packet(q, 0, 0);
    qtest_writel(q, BANK + 0x3100, 1);
    qtest_writel(q, BANK + 0x3200, 0);
    qtest_writel(q, BANK + 0x3204, (8U << 16) | 8);
    qtest_writel(q, BANK + 0x3210, 0x03000102);
    qtest_writel(q, BANK + 0x3400, (8U << 16) | 8);
    qtest_writel(q, BANK + 0x3440, RAM + 0x8000);
    qtest_writel(q, BANK + 0x3500, 32);
    qtest_writel(q, BANK + 0x3580, 0);
    qtest_writel(q, BANK + 0x35c0, 1);
    qtest_writel(q, BANK + 0x1200, 1);
    wait_completion(q);
    image = pixels(q);
    assert_color(image, 0, 0, 255);
    /* Both banks feed one route: first ACK must preserve the other bank. */
    packet(q, 1, 0);
    qtest_writel(q, BANK + 0x4000 + 0x1200, 1);
    qtest_clock_step(q, 1000000);
    g_assert_cmphex(qtest_readl(q, BANK + 0x4000 + 0x1214), ==,
                    FRAME_PENDING);
    g_clear_pointer(&image, g_free);
    image = pixels_head(q, 1);
    assert_color(image, 0, 0, 0);
    qtest_writel(q, BANK + 0x1214, FRAME_PENDING);
    g_assert_true(qtest_get_irq(q, 111 - 32));
    qtest_writel(q, BANK + 0x4000 + 0x1214, FRAME_PENDING);
    g_assert_false(qtest_get_irq(q, 111 - 32));
    qtest_quit(q);
}

static void test_domains(void)
{
    QTestState *q = start();
    unsigned bank, offset;

    for (bank = 0; bank < 2; bank++) {
        uint32_t base = BANK + bank * 0x4000;

        /* C3C2 configuration is in unit0, not the BF0C main command. */
        qtest_writel(q, base + 0x200, 1);
        qtest_writel(q, base + 0x204, (8U << 16) | 8);
        qtest_writel(q, base + 0x1204, 11);
        qtest_writel(q, base + 0x1308, (8U << 16) | 8);
        g_assert_cmphex(qtest_readl(q, base + 0x200), ==, 1);
        g_assert_cmphex(qtest_readl(q, base + 0x1200), ==, 0);
        g_assert_cmphex(qtest_readl(q, base + 0x1204), ==, 11);
        g_assert_cmphex(qtest_readl(q, base + 0x1308), ==, (8U << 16) | 8);
        /* C6CA packet and C1DA coefficient serialization retain their domains. */
        for (offset = 0x430; offset <= 0x478; offset += 4) {
            if (offset != 0x454) {
                qtest_writel(q, base + offset, offset + bank);
                g_assert_cmphex(qtest_readl(q, base + offset), ==, offset + bank);
            }
        }
        for (offset = 0x4a0; offset <= 0x4b4; offset += 4) {
            qtest_writel(q, base + offset, 0x00110022 + offset);
            g_assert_cmphex(qtest_readl(q, base + offset), ==,
                            0x00110022 + offset);
        }
        for (offset = 0x2440; offset <= 0x244c; offset += 4) {
            qtest_writel(q, base + offset, RAM + offset);
            g_assert_cmphex(qtest_readl(q, base + offset), ==, RAM + offset);
        }
        qtest_clock_step(q, 1000000);
        g_assert_cmphex(qtest_readl(q, base + 0x1214), ==, 0);
        g_assert_false(qtest_get_irq(q, 111 - 32));
    }
    qtest_quit(q);
}

static void test_overlay(void)
{
    QTestState *q = start();
    g_autofree char *image = NULL;
    unsigned x, y;

    qtest_memset(q, RAM, 0xff, 1024);
    packet(q, 0, 0);
    qtest_writel(q, BANK + 0x1804, (2U << 16) | 2);
    qtest_writel(q, BANK + 0x1904, (4U << 16) | 4);
    qtest_writel(q, BANK + 0x1b04, 0xff);
    qtest_writel(q, BANK + 0x1c00, 2);
    qtest_writel(q, BANK + 0x4c0, 0x606);
    qtest_writel(q, BANK + 0x1510, 0x606);
    qtest_writel(q, BANK + 0x100, 1);
    g_assert_cmphex(qtest_readl(q, BANK + 0x4c0), ==, 0x606);
    g_assert_cmphex(qtest_readl(q, BANK + 0x100), ==, 1);
    qtest_writel(q, BANK + 0x1200, 1);
    wait_completion(q);
    g_assert_cmphex(qtest_readl(q, BANK + 0x4c0), ==, 0x01010606);
    g_assert_cmphex(qtest_readl(q, BANK + 0x1510), ==, 0x01010606);
    g_assert_cmphex(qtest_readl(q, BANK + 0x100), ==, 3);
    image = pixels(q);
    for (y = 0; y < 8; y++) {
        for (x = 0; x < 8; x++) {
            bool red = x >= 2 && x < 6 && y >= 2 && y < 6;
            unsigned pixel = (y * 8 + x) * 3;
            g_assert_cmpuint((uint8_t)image[pixel], ==, 255);
            g_assert_cmpuint((uint8_t)image[pixel + 1], ==, red ? 0 : 255);
            g_assert_cmpuint((uint8_t)image[pixel + 2], ==, red ? 0 : 255);
        }
    }
    qtest_quit(q);
}

static void test_tables(void)
{
    QTestState *q = start();
    const uint32_t family[] = { 0x24040000, 0x24042000,
                               0x24045000, 0x24046000 };
    unsigned unit, type, bank, plane, word;

    for (unit = 0; unit < 2; unit++) {
        for (type = 0; type < ARRAY_SIZE(family); type++) {
            for (bank = 0; bank < 2; bank++) {
                for (plane = 0; plane < (type == 3 ? 2 : 3); plane++) {
                    uint32_t base = family[type] + unit * 0x10000 +
                        bank * (type == 2 ? 0x200 : 0x100) +
                        plane * (type < 2 ? 0x40 : 0x80);
                    for (word = 0; word < (type < 2 ? 16 : 17); word++) {
                        uint32_t value = type < 2 ? 0x01230045 :
                            word < 16 ? 0x00ab00cd : type == 2 ? 0xef : 0x12ef;
                        qtest_writel(q, base + word * 4, value);
                        g_assert_cmphex(qtest_readl(q, base + word * 4), ==,
                                        value);
                    }
                }
            }
        }
    }
    g_assert_false(qtest_get_irq(q, 111 - 32));
    qtest_quit(q);
}

static void test_refresh(void)
{
    QTestState *q = start();
    uint8_t source[16 * 72];
    g_autofree char *image = NULL;
    unsigned x, y;

    memset(source, 0xa5, sizeof(source));
    for (y = 0; y < 16; y++) {
        for (x = 0; x < 16; x++) {
            stl_le_p(source + y * 72 + x * 4,
                     (x / 2) & 1 ? 0xff0000ff : 0xffff0000);
        }
    }
    qtest_memwrite(q, RAM, source, sizeof(source));
    packet(q, 0, 0);
    qtest_writel(q, BANK + 0x2204, (16U << 16) | 16);
    qtest_writel(q, BANK + 0x2400, (16U << 16) | 16);
    qtest_writel(q, BANK + 0x2500, 72);
    qtest_writel(q, BANK + 0x2810, 1);
    qtest_writel(q, BANK + 0x2814, 0x201);
    qtest_writel(q, BANK + 0x2818, 0x201);
    qtest_writel(q, BANK + 0x1200, 1);
    wait_completion(q);
    image = pixels(q);
    for (y = 0; y < 8; y++) {
        for (x = 0; x < 8; x++) {
            unsigned pixel = (y * 8 + x) * 3;
            g_assert_cmpuint((uint8_t)image[pixel], ==, (x & 1) ? 0 : 255);
            g_assert_cmpuint((uint8_t)image[pixel + 1], ==, 0);
            g_assert_cmpuint((uint8_t)image[pixel + 2], ==, (x & 1) ? 255 : 0);
        }
    }
    qtest_writel(q, BANK + 0x1214, FRAME_PENDING);
    /* A live buffer update requires no second main start. */
    for (y = 0; y < 16; y++) {
        for (x = 0; x < 16; x++) {
            stl_le_p(source + y * 72 + x * 4, 0xff00ff00);
        }
    }
    qtest_memwrite(q, RAM + 0x8000, source, sizeof(source));
    qtest_writel(q, BANK + 0x2440, RAM + 0x8000);
    wait_completion(q);
    g_clear_pointer(&image, g_free);
    image = pixels(q);
    assert_color(image, 0, 255, 0);
    g_assert_cmphex(qtest_readl(q, BANK + 0x1200) & 3, ==, 3);
    qtest_writel(q, BANK + 0x1214, FRAME_PENDING);
    /* Invalid live ratios retain the last complete surface and no IRQ. */
    qtest_writel(q, BANK + 0x2818, 0x200);
    qtest_clock_step(q, 1000000);
    g_assert_false(qtest_get_irq(q, 111 - 32));
    g_clear_pointer(&image, g_free);
    image = pixels(q);
    assert_color(image, 0, 255, 0);
    qtest_writel(q, BANK + 0x2818, 0x201);
    wait_completion(q);
    qtest_writel(q, BANK + 0x1214, FRAME_PENDING);
    qtest_writel(q, BANK + 0x1200, 2);
    qtest_memset(q, RAM + 0x8000, 0xff, sizeof(source));
    qtest_clock_step(q, 1000000);
    g_assert_false(qtest_get_irq(q, 111 - 32));
    g_clear_pointer(&image, g_free);
    image = pixels(q);
    assert_color(image, 0, 255, 0);
    qtest_quit(q);
}

static void test_crop(void)
{
    QTestState *q = start();
    uint32_t base = BANK + 0x4000;
    uint8_t source[10 * 48];
    g_autofree char *image = NULL;
    unsigned x, y;

    /* The original producer already adjusts a cropped pointer. The renderer
     * must preserve its full row stride and not subtract viewport origin.
     */
    for (y = 0; y < 10; y++) {
        for (x = 0; x < 12; x++) {
            bool crop = y >= 1 && y < 9 && x >= 2 && x < 10;
            stl_le_p(source + y * 48 + x * 4,
                     crop ? 0xff00ff00 : 0xff0000ff);
        }
    }
    qtest_memwrite(q, RAM, source, sizeof(source));
    packet(q, 1, 0);
    /* V=(-2,0,8,8), D=(0,0,8,8): pre-canvas10x8, relative layer x2. */
    qtest_writel(q, base + 0x2204, (8U << 16) | 10);
    qtest_writel(q, base + 0x2208, 0xfffe);
    qtest_writel(q, base + 0x2440, RAM + 48 + 2 * 4);
    qtest_writel(q, base + 0x2500, 48);
    qtest_writel(q, base + 0x2580, 2);
    qtest_writel(q, base + 0x1200, 1);
    wait_completion(q);
    image = pixels_head(q, 1);
    assert_color(image, 0, 255, 0);
    qtest_writel(q, base + 0x1214, FRAME_PENDING);
    /* Moving the live viewport also clips its trailing edge at the output. */
    qtest_writel(q, base + 0x2208, 2);
    wait_completion(q);
    g_clear_pointer(&image, g_free);
    image = pixels_head(q, 1);
    for (y = 0; y < 8; y++) {
        for (x = 0; x < 8; x++) {
            unsigned pixel = (y * 8 + x) * 3;
            g_assert_cmpuint((uint8_t)image[pixel], ==, 0);
            g_assert_cmpuint((uint8_t)image[pixel + 1], ==, x < 4 ? 0 : 255);
            g_assert_cmpuint((uint8_t)image[pixel + 2], ==, 0);
        }
    }
    qtest_quit(q);
}

void nikon_image_add_tests(const char *bios)
{
    bios_image = bios;
    qtest_add_func("/nikon/image/frame", test_frame);
    qtest_add_func("/nikon/image/empty", test_empty);
    qtest_add_func("/nikon/image/layouts", test_layouts);
    qtest_add_func("/nikon/image/cancel", test_cancel);
    qtest_add_func("/nikon/image/fault", test_fault);
    qtest_add_func("/nikon/image/ports", test_ports);
    qtest_add_func("/nikon/image/domains", test_domains);
    qtest_add_func("/nikon/image/overlay", test_overlay);
    qtest_add_func("/nikon/image/tables", test_tables);
    qtest_add_func("/nikon/image/refresh", test_refresh);
    qtest_add_func("/nikon/image/crop", test_crop);
}
