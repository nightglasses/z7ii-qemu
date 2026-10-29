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
    qtest_writel(q, base + 0x1328, 8);
    qtest_writel(q, base + 0x132c, 8);
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
    int64_t deadline = now + 20000000;
    unsigned events = 0;

    while (!qtest_get_irq(q, 111 - 32)) {
        now = qtest_clock_step_next(q);
        g_assert_cmpint(now, <=, deadline);
        g_assert_cmpuint(++events, <=, 64);
    }
}

static char *pixels_extent(QTestState *q, unsigned head, unsigned width,
                           unsigned height)
{
    g_autofree char *header = g_strdup_printf("P6\n%u %u\n255\n", width, height);
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
    g_assert_cmpuint(length, ==, strlen(header) + width * height * 3);
    g_assert_true(g_str_has_prefix(contents, header));
    memmove(contents, contents + strlen(header), width * height * 3);
    unlink(path);
    return contents;
}

static char *pixels_head(QTestState *q, unsigned head)
{
    return pixels_extent(q, head, 8, 8);
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
    qtest_writel(q, BANK + 0x1328, 682);
    qtest_writel(q, BANK + 0x132c, 1024);
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
        qtest_writel(q, base + 0x1328, 8);
        qtest_writel(q, base + 0x132c, 8);
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
    qtest_writel(q, BANK + 0x2100, 1);
    g_assert_cmphex(qtest_readl(q, BANK + 0x4c0), ==, 0x606);
    g_assert_cmphex(qtest_readl(q, BANK + 0x2100), ==, 1);
    qtest_writel(q, BANK + 0x1200, 1);
    wait_completion(q);
    g_assert_cmphex(qtest_readl(q, BANK + 0x4c0), ==, 0x01010606);
    g_assert_cmphex(qtest_readl(q, BANK + 0x1510), ==, 0x01010606);
    g_assert_cmphex(qtest_readl(q, BANK + 0x2100), ==, 3);
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

/* Independent known limited-range BT.601 vectors, including unequal Ys. */
static const struct {
    uint8_t pair[4];
    uint8_t rgb[2][3];
} yuva_colors[] = {
    { { 16, 235, 128, 128 }, { { 0, 0, 0 }, { 255, 255, 255 } } },
    { { 81, 81, 90, 240 }, { { 255, 0, 0 }, { 255, 0, 0 } } },
    { { 145, 145, 54, 34 }, { { 0, 255, 1 }, { 0, 255, 1 } } },
    { { 41, 41, 240, 110 }, { { 0, 0, 255 }, { 0, 0, 255 } } },
};
static const uint8_t yuva_opacity[] = { 0, 64, 128, 255 };

static void yuva_data(QTestState *q, uint32_t primary, uint32_t secondary,
                      unsigned width, unsigned height, unsigned stride,
                      unsigned alpha_stride, bool white)
{
    g_autofree uint8_t *data = g_malloc(height * stride);
    g_autofree uint8_t *alpha = g_malloc(height * alpha_stride);

    memset(data, 0xa5, height * stride);
    memset(alpha, 0x5a, height * alpha_stride);
    for (unsigned y = 0; y < height; y++) {
        for (unsigned x = 0; x < width; x += 2) {
            unsigned color = (x / 2 + y) % ARRAY_SIZE(yuva_colors);

            memcpy(data + y * stride + x * 2, white ?
                   (uint8_t[]) { 235, 235, 128, 128 } :
                   yuva_colors[color].pair, 4);
            for (unsigned j = 0; j < 2; j++) {
                alpha[y * alpha_stride + x + j] = white ? 255 :
                    yuva_opacity[(x + j + y) % ARRAY_SIZE(yuva_opacity)];
            }
        }
    }
    qtest_memwrite(q, primary, data, height * stride);
    qtest_memwrite(q, secondary, alpha, height * alpha_stride);
}

static void yuva_packet(QTestState *q, unsigned unit, unsigned port,
                        unsigned layer, unsigned width, unsigned height,
                        uint32_t primary, uint32_t secondary,
                        unsigned stride, unsigned alpha_stride)
{
    uint32_t base = BANK + unit * 0x4000;
    uint32_t block = base + 0x2000 + port * 0x1000;
    uint32_t geometry = (height << 16) | width;

    qtest_writel(q, base + 0x1100, 1);
    qtest_writel(q, base + 0x1308, geometry);
    qtest_writel(q, base + 0x1328, height);
    qtest_writel(q, base + 0x132c, width);
    qtest_writel(q, block + 0x100, 1);
    qtest_writel(q, block + 0x200, 0x00030006);
    qtest_writel(q, block + 0x204, geometry);
    qtest_writel(q, block + 0x210, 0x02000301);
    qtest_writel(q, block + 0x400 + layer * 4, geometry);
    qtest_writel(q, block + (layer ? 0x47c + layer * 4 : 0x440), primary);
    qtest_writel(q, block + 0x4c0 + layer * 4, secondary);
    qtest_writel(q, block + 0x500 + layer * 4, stride);
    qtest_writel(q, block + 0x540 + layer * 4, alpha_stride);
    qtest_writel(q, block + 0x5c0, 1U << layer);
}

static void assert_yuva(const char *image, unsigned width, unsigned height)
{
    for (unsigned y = 0; y < height; y++) {
        for (unsigned x = 0; x < width; x++) {
            unsigned color = (x / 2 + y) % ARRAY_SIZE(yuva_colors);
            unsigned alpha = yuva_opacity[(x + y) % ARRAY_SIZE(yuva_opacity)];

            for (unsigned c = 0; c < 3; c++) {
                unsigned rgb = yuva_colors[color].rgb[x & 1][c];

                g_assert_cmpuint((uint8_t)image[(y * width + x) * 3 + c],
                                ==, rgb * alpha / 255);
            }
        }
    }
}

static void test_alpha_endpoints(void)
{
    static const uint16_t rgb565[] = { 0xf800, 0x07e0, 0x001f, 0xffe0 };
    static const uint8_t rgb[][3] = {
        { 255, 0, 0 }, { 0, 255, 0 }, { 0, 0, 255 }, { 255, 255, 0 },
    };
    static const uint8_t opacity4[] = { 0, 1, 7, 15 };
    static const uint8_t opacity8[] = { 0, 1, 128, 255 };
    QTestState *q = start();
    uint8_t data[8 * 24], alpha[8 * 12], after[sizeof(data)];

    /* White port0 exposes both transparent retention and OVER rounding. */
    qtest_memset(q, RAM + 0x8000, 0xff, 8 * 40);
    packet(q, 0, 0);
    qtest_writel(q, BANK + 0x2440, RAM + 0x8000);
    for (unsigned format = 0; format < 3; format++) {
        unsigned mode = format ? 6 : 5;

        memset(data, 0xa5, sizeof(data));
        for (unsigned y = 0; y < 8; y++) {
            for (unsigned x = 0; x < 8; x++) {
                if (format == 2) {
                    memcpy(data + y * 24 + (x & ~1U) * 2,
                           yuva_colors[(x / 2 + y) % 4].pair, 4);
                } else {
                    stw_le_p(data + y * 24 + x * 2, rgb565[(x + y) % 4]);
                }
            }
        }
        qtest_memwrite(q, RAM, data, sizeof(data));
        yuva_packet(q, 0, 1, 0, 8, 8, RAM, RAM + 0x4000, 24, 12);
        qtest_writel(q, BANK + 0x3200, format == 2 ? 0x30006 : mode);
        for (unsigned pattern = 0; pattern < 3; pattern++) {
            g_autofree char *image = NULL;

            memset(alpha, 0x5a, sizeof(alpha));
            for (unsigned y = 0; y < 8; y++) {
                for (unsigned x = 0; x < 8; x++) {
                    unsigned value = pattern == 0 ? 0 : pattern == 1 ?
                        (mode == 5 ? 15 : 255) : mode == 5 ?
                        opacity4[(x + y) % 4] : opacity8[(x + y) % 4];

                    if (mode == 5) {
                        unsigned shift = (x & 1) * 4;
                        uint8_t *byte = &alpha[y * 12 + x / 2];

                        *byte = (*byte & ~(15U << shift)) | (value << shift);
                    } else {
                        alpha[y * 12 + x] = value;
                    }
                }
            }
            qtest_memwrite(q, RAM + 0x4000, alpha, sizeof(alpha));
            qtest_writel(q, BANK + 0x1200, 1);
            wait_completion(q);
            image = pixels(q);
            for (unsigned y = 0; y < 8; y++) {
                for (unsigned x = 0; x < 8; x++) {
                    unsigned a = pattern == 0 ? 0 : pattern == 1 ? 255 :
                        mode == 5 ? opacity4[(x + y) % 4] * 17 :
                        opacity8[(x + y) % 4];
                    const uint8_t *color = format == 2 ?
                        yuva_colors[(x / 2 + y) % 4].rgb[x & 1] :
                        rgb[(x + y) % 4];

                    for (unsigned c = 0; c < 3; c++) {
                        g_assert_cmpuint((uint8_t)image[(y * 8 + x) * 3 + c],
                                        ==, color[c] * a / 255 + 255 - a);
                    }
                }
            }
            qtest_memread(q, RAM, after, sizeof(after));
            g_assert_cmpmem(after, sizeof(after), data, sizeof(data));
            qtest_memread(q, RAM + 0x4000, after, sizeof(alpha));
            g_assert_cmpmem(after, sizeof(alpha), alpha, sizeof(alpha));
            qtest_writel(q, BANK + 0x1214, FRAME_PENDING);
            if (!pattern) {
                /* Zero opacity must not bypass the actual DMA preflight. */
                qtest_writel(q, BANK + 0x3440, BANK);
                qtest_clock_step(q, 1000000);
                g_assert_false(qtest_get_irq(q, 111 - 32));
                qtest_writel(q, BANK + 0x3440, RAM);
            }
        }
    }
    qtest_quit(q);
}

static void test_yuva(void)
{
    static const unsigned providers[][3] = {
        { 0, 0, 0 }, { 0, 1, 9 }, { 1, 0, 1 }, { 1, 1, 4 },
    };

    for (unsigned i = 0; i < ARRAY_SIZE(providers); i++) {
        QTestState *q = start();
        unsigned unit = providers[i][0], port = providers[i][1];
        uint32_t base = BANK + unit * 0x4000;
        uint32_t block = base + 0x2000 + port * 0x1000;
        unsigned layer = providers[i][2];
        g_autofree char *image = NULL;

        yuva_data(q, RAM, RAM + 0x4000, 8, 8, 24, 12, false);
        yuva_packet(q, unit, port, layer, 8, 8, RAM, RAM + 0x4000, 24, 12);
        qtest_writel(q, base + 0x1200, 1);
        g_assert_false(qtest_get_irq(q, 111 - 32));
        wait_completion(q);
        image = pixels_head(q, unit);
        assert_yuva(image, 8, 8);
        qtest_writel(q, base + 0x1214, FRAME_PENDING);
        /* Live pointer changes use independent padded RAM without restart. */
        yuva_data(q, RAM + 0x8000, RAM + 0xc000, 8, 8, 24, 12, true);
        qtest_writel(q, block + (layer ? 0x47c + layer * 4 : 0x440),
                     RAM + 0x8000);
        qtest_writel(q, block + 0x4c0 + layer * 4, RAM + 0xc000);
        wait_completion(q);
        g_clear_pointer(&image, g_free);
        image = pixels_head(q, unit);
        assert_color(image, 255, 255, 255);
        qtest_writel(q, base + 0x1214, FRAME_PENDING);
        qtest_writel(q, base + 0x1200, 2);
        yuva_data(q, RAM + 0x8000, RAM + 0xc000, 8, 8, 24, 12, false);
        qtest_clock_step(q, 1000000);
        g_assert_false(qtest_get_irq(q, 111 - 32));
        g_clear_pointer(&image, g_free);
        image = pixels_head(q, unit);
        assert_color(image, 255, 255, 255);
        qtest_writel(q, base + 0x1200, 1);
        qtest_system_reset(q);
        qtest_clock_step(q, 1000000);
        g_assert_false(qtest_get_irq(q, 111 - 32));
        g_assert_cmphex(qtest_readl(q, base + 0x1214), ==, 0);
        qtest_quit(q);
    }
}

static void test_yuva_fault(void)
{
    enum { LANE, CONTROL, ODD_WIDTH, PRIMARY_ALIGN, STRIDE_ALIGN,
           SHORT_STRIDE, SHORT_ALPHA, PRIMARY_MMIO, SECONDARY_MMIO,
           LAST_LAYER, CASES };
    QTestState *q = start();
    g_autofree char *image = NULL;

    yuva_data(q, RAM, RAM + 0x4000, 8, 8, 24, 12, true);
    yuva_packet(q, 0, 0, 0, 8, 8, RAM, RAM + 0x4000, 24, 12);
    qtest_writel(q, BANK + 0x1200, 1);
    wait_completion(q);
    qtest_writel(q, BANK + 0x1214, FRAME_PENDING);
    for (unsigned test = LANE; test < CASES; test++) {
        yuva_packet(q, 0, 0, 0, 8, 8, RAM, RAM + 0x4000, 24, 12);
        switch (test) {
        case LANE: qtest_writel(q, BANK + 0x2210, 0x02000300); break;
        case CONTROL: qtest_writel(q, BANK + 0x2200, 0x00030016); break;
        case ODD_WIDTH: qtest_writel(q, BANK + 0x2400, (8U << 16) | 7); break;
        case PRIMARY_ALIGN: qtest_writel(q, BANK + 0x2440, RAM + 2); break;
        case STRIDE_ALIGN: qtest_writel(q, BANK + 0x2500, 18); break;
        case SHORT_STRIDE: qtest_writel(q, BANK + 0x2500, 12); break;
        case SHORT_ALPHA: qtest_writel(q, BANK + 0x2540, 7); break;
        case PRIMARY_MMIO: qtest_writel(q, BANK + 0x2440, BANK); break;
        case SECONDARY_MMIO: qtest_writel(q, BANK + 0x24c0, BANK); break;
        case LAST_LAYER:
            qtest_writel(q, BANK + 0x2404, (8U << 16) | 8);
            qtest_writel(q, BANK + 0x2480, BANK);
            qtest_writel(q, BANK + 0x24c4, RAM + 0x4000);
            qtest_writel(q, BANK + 0x2504, 24);
            qtest_writel(q, BANK + 0x2544, 12);
            qtest_writel(q, BANK + 0x25c0, 3);
            break;
        }
        qtest_clock_step(q, 1000000);
        g_assert_false(qtest_get_irq(q, 111 - 32));
        g_assert_cmphex(qtest_readl(q, BANK + 0x1214), ==, 0);
        g_clear_pointer(&image, g_free);
        image = pixels(q);
        assert_color(image, 255, 255, 255);
    }
    yuva_packet(q, 0, 0, 0, 8, 8, RAM, RAM + 0x4000, 24, 12);
    wait_completion(q);
    qtest_writel(q, BANK + 0x1214, FRAME_PENDING);
    qtest_quit(q);
}

static void test_yuva_frame(void)
{
    QTestState *q = start();
    const unsigned width = 1024, height = 682, stride = 2048, alpha_stride = 1024;
    const uint32_t primary = RAM + 0x20000, secondary = RAM + 0x200000;
    g_autofree char *image = NULL;

    yuva_data(q, primary, secondary, width, height, stride, alpha_stride, false);
    yuva_packet(q, 1, 0, 0, width, height, primary, secondary, stride, alpha_stride);
    qtest_writel(q, BANK + 0x4000 + 0x1200, 0x11);
    wait_completion(q);
    image = pixels_extent(q, 1, width, height);
    assert_yuva(image, width, height);
    qtest_quit(q);
}

static void test_visible_area(void)
{
    QTestState *q = start();
    const uint32_t base = BANK + 0x4000;
    const uint32_t primary = RAM + 0x20000, secondary = RAM + 0x200000;
    const uint32_t invalid[][2] = { { 0, 342 }, { 512, 0 }, { 4097, 4097 } };
    g_autofree char *image = NULL;
    g_autofree char *before = NULL;

    yuva_data(q, primary, secondary, 1024, 682, 2048, 1024, false);
    yuva_packet(q, 1, 0, 0, 1024, 682, primary, secondary, 2048, 1024);
    /* 10448d12's rear-display packet has separate timing and visible fields. */
    qtest_writel(q, base + 0x1308, 0x030c030c);
    qtest_writel(q, base + 0x130c, 1155);
    qtest_writel(q, base + 0x1310, 3);
    qtest_writel(q, base + 0x1314, 3);
    qtest_writel(q, base + 0x1318, 0x0707);
    qtest_writel(q, base + 0x131c, 96);
    qtest_writel(q, base + 0x1200, 0x11);
    wait_completion(q);
    image = pixels_extent(q, 1, 1024, 682);
    assert_yuva(image, 1024, 682);
    qtest_writel(q, base + 0x1214, FRAME_PENDING);
    /* The live visible output clips a larger port canvas without resizing it. */
    qtest_writel(q, base + 0x1328, 342);
    qtest_writel(q, base + 0x132c, 512);
    wait_completion(q);
    before = pixels_extent(q, 1, 512, 342);
    assert_yuva(before, 512, 342);
    qtest_writel(q, base + 0x1214, FRAME_PENDING);
    /* CA80 serializes these as complete unsigned halfwords. */
    qtest_writel(q, base + 0x132c, 0x10000);
    qtest_writel(q, base + 0x1328, 0x10000);
    g_assert_cmphex(qtest_readl(q, base + 0x132c), ==, 512);
    g_assert_cmphex(qtest_readl(q, base + 0x1328), ==, 342);
    for (unsigned i = 0; i < ARRAY_SIZE(invalid); i++) {
        qtest_writel(q, base + 0x132c, invalid[i][0]);
        qtest_writel(q, base + 0x1328, invalid[i][1]);
        qtest_clock_step(q, 20000000);
        g_assert_false(qtest_get_irq(q, 111 - 32));
        g_assert_cmphex(qtest_readl(q, base + 0x1214), ==, 0);
        g_clear_pointer(&image, g_free);
        image = pixels_extent(q, 1, 512, 342);
        g_assert_cmpmem(image, 512 * 342 * 3, before, 512 * 342 * 3);
    }
    qtest_writel(q, base + 0x132c, 512);
    qtest_writel(q, base + 0x1328, 342);
    wait_completion(q);
    qtest_writel(q, base + 0x1214, FRAME_PENDING);
    qtest_writel(q, base + 0x1200, 2);
    qtest_writel(q, base + 0x132c, 256);
    qtest_writel(q, base + 0x1328, 128);
    qtest_clock_step(q, 20000000);
    g_assert_false(qtest_get_irq(q, 111 - 32));
    g_clear_pointer(&image, g_free);
    image = pixels_extent(q, 1, 512, 342);
    g_assert_cmpmem(image, 512 * 342 * 3, before, 512 * 342 * 3);
    qtest_writel(q, base + 0x1200, 1);
    qtest_system_reset(q);
    qtest_clock_step(q, 20000000);
    g_assert_false(qtest_get_irq(q, 111 - 32));
    g_assert_cmphex(qtest_readl(q, base + 0x132c), ==, 0);
    g_assert_cmphex(qtest_readl(q, base + 0x1328), ==, 0);
    /* A cold timing packet alone cannot authorize a visible transaction. */
    yuva_packet(q, 1, 0, 0, 1024, 682, primary, secondary, 2048, 1024);
    qtest_writel(q, base + 0x1308, 0x030c030c);
    qtest_writel(q, base + 0x132c, 0);
    qtest_writel(q, base + 0x1328, 0);
    qtest_writel(q, base + 0x1200, 0x11);
    qtest_clock_step(q, 20000000);
    g_assert_false(qtest_get_irq(q, 111 - 32));
    g_assert_cmphex(qtest_readl(q, base + 0x1214), ==, 0);
    qtest_quit(q);
}

static void primary_packet(QTestState *q, unsigned mode)
{
    /*
     * Synthetic matrices: R=Y+V/2, G=Y-U/4-V/2, B=Y+U;
     * the second produces (B-R/2, G, R). Both signed domains matter.
     */
    const int first[3][3] = {
        { 16384, 0, 8192 }, { 16384, -4096, -8192 }, { 16384, 16384, 0 },
    };
    const int second[3][3] = {
        { -512, 0, 1024 }, { 0, 1024, 0 }, { 1024, 0, 0 },
    };
    unsigned row;

    qtest_writel(q, BANK + 0x1100, 1);
    qtest_writel(q, BANK + 0x1328, 8);
    qtest_writel(q, BANK + 0x132c, 8);
    qtest_writel(q, BANK + 0x100, 1);
    qtest_writel(q, BANK + 0x200, 0x00300000 | mode);
    qtest_writel(q, BANK + 0x204, 0x00080008);
    qtest_writel(q, BANK + 0x210, RAM + 2);
    qtest_writel(q, BANK + 0x220, RAM + 0x4002);
    qtest_writel(q, BANK + 0x240, 12);
    qtest_writel(q, BANK + 0x250, 14);
    qtest_writel(q, BANK + 0x500, 255);
    /* Disabled adjustment retains its source-produced inactive parameters. */
    qtest_writel(q, BANK + 0x410, 0x200);
    qtest_writel(q, BANK + 0x414, 0xc00100);
    qtest_writel(q, BANK + 0x418, 0xc0);
    qtest_writel(q, BANK + 0x4c0, 0x404);
    qtest_writel(q, BANK + 0x4f0, 1);
    for (unsigned bank = 0; bank < 2; bank++) {
        for (unsigned plane = 0; plane < 3; plane++) {
            for (unsigned i = 0; i <= 16; i++) {
                unsigned lo = MIN(i * 16, 255);
                unsigned hi = i == 16 ? 0 : i * 16 + 8;

                qtest_writel(q, 0x24045000 + bank * 0x200 + plane * 0x80 +
                             i * 4, lo | (hi << 16));
            }
        }
        for (unsigned curve = 0; curve < 2; curve++) {
            for (unsigned i = 0; i <= 16; i++) {
                unsigned lo = curve ? 64 : MIN(i * 16, 255);
                unsigned hi = i == 16 ? 0 : curve ? 64 : i * 16 + 8;

                qtest_writel(q, 0x24046000 + bank * 0x100 + curve * 0x80 +
                             i * 4, lo | (hi << 16));
            }
        }
    }
    for (row = 0; row < 3; row++) {
        qtest_writel(q, BANK + 0x4a0 + row * 8,
                     (uint16_t)first[row][0] |
                     ((uint32_t)(uint16_t)first[row][1] << 16));
        qtest_writel(q, BANK + 0x4a4 + row * 8,
                     (uint16_t)first[row][2]);
        qtest_writel(q, BANK + 0x4d0 + row * 8,
                     (second[row][0] & 0xfff) |
                     ((second[row][1] & 0xfff) << 16));
        qtest_writel(q, BANK + 0x4d4 + row * 8,
                     second[row][2] & 0xfff);
    }
}

static void primary_source(QTestState *q, unsigned mode,
                           uint8_t luma[8 * 12], uint8_t uv[8 * 14])
{
    unsigned x, y;

    memset(luma, 0xa5, 8 * 12);
    memset(uv, 0xa5, 8 * 14);
    for (y = 0; y < 8; y++) {
        for (x = 0; x < 8; x++) {
            luma[y * 12 + x + 2] = 48 + y * 4 + x * 2;
        }
    }
    for (y = 0; y < (mode == 4 ? 4 : 8); y++) {
        for (x = 0; x < 8; x += 2) {
            uv[y * 14 + x + 2] = 160 + y * 4;
            uv[y * 14 + x + 3] = 96;
        }
    }
    qtest_memwrite(q, RAM, luma, 8 * 12);
    qtest_memwrite(q, RAM + 0x4000, uv, 8 * 14);
}

static void test_primary(void)
{
    QTestState *q = start();
    uint8_t luma[8 * 12], uv[8 * 14], observed[8 * 14];
    unsigned mode, x, y;

    for (mode = 0; mode <= 4; mode += 4) {
        g_autofree char *image = NULL;

        primary_source(q, mode, luma, uv);
        primary_packet(q, mode);
        qtest_writel(q, BANK + 0x1200, 1);
        g_assert_false(qtest_get_irq(q, 111 - 32));
        wait_completion(q);
        image = pixels(q);
        for (y = 0; y < 8; y++) {
            unsigned u = 32 + (mode == 4 ? y / 2 : y) * 4;

            for (x = 0; x < 8; x++) {
                unsigned luminance = 48 + y * 4 + x * 2;
                const uint8_t *rgb = (uint8_t *)image + (y * 8 + x) * 3;

                g_assert_cmpuint(rgb[0], ==, luminance / 2 + u + 8);
                g_assert_cmpuint(rgb[1], ==, luminance - u / 4 + 16);
                g_assert_cmpuint(rgb[2], ==, luminance - 16);
            }
        }
        qtest_memread(q, RAM, observed, sizeof(luma));
        g_assert_cmpmem(observed, sizeof(luma), luma, sizeof(luma));
        qtest_memread(q, RAM + 0x4000, observed, sizeof(uv));
        g_assert_cmpmem(observed, sizeof(uv), uv, sizeof(uv));
        qtest_writel(q, BANK + 0x1214, FRAME_PENDING);
    }
    /* A new live RAM frame needs no second master start. */
    qtest_memset(q, RAM, 64, sizeof(luma));
    qtest_memset(q, RAM + 0x4000, 128, sizeof(uv));
    wait_completion(q);
    {
        g_autofree char *image = pixels(q);

        assert_color(image, 32, 64, 64);
    }
    qtest_writel(q, BANK + 0x1214, FRAME_PENDING);
    /* The special profile bypasses the nonidentity second matrix. */
    qtest_writel(q, BANK + 0x4f0, 0);
    wait_completion(q);
    {
        g_autofree char *image = pixels(q);

        assert_color(image, 64, 64, 64);
    }
    qtest_writel(q, BANK + 0x1214, FRAME_PENDING);
    qtest_writel(q, BANK + 0x500, 0);
    wait_completion(q);
    {
        g_autofree char *image = pixels(q);

        assert_color(image, 0, 0, 0);
    }
    qtest_quit(q);
}

static uint8_t primary_matrix_reference(const int coefficients[3],
                                        const int input[3], unsigned shift)
{
    int64_t value = 0, divisor = INT64_C(1) << shift;

    for (unsigned i = 0; i < 3; i++) {
        value += (int64_t)coefficients[i] * input[i];
    }
    value += divisor / 2;
    /* Explicit floor division, independent of the production signed shift. */
    value = value < 0 ? -((-value + divisor - 1) / divisor) : value / divisor;
    return CLAMP(value, 0, 255);
}

static void test_primary_matrix_bounds(void)
{
    static const int matrices[][2][3][3] = {
        {
            { { -32768, -32768, -32768 }, { 32767, 32767, 32767 },
              { 32767, -32768, 32767 } },
            { { -2048, -2048, -2048 }, { 2047, 2047, 2047 },
              { 2047, -2048, 2047 } },
        }, {
            { { -32768, 32767, 32767 }, { 32767, -32768, 32767 },
              { 32767, 32767, -32768 } },
            { { 2047, -2048, 1024 }, { -2048, 2047, 1024 },
              { 1024, 2047, -2048 } },
        }, {
            { { 16383, 8191, -8193 }, { 16385, -4097, 8193 },
              { 16384, 16383, -1 } },
            { { 1023, 511, -513 }, { -511, 1025, 513 },
              { 513, -1, 1024 } },
        }, {
            { { 16384, 0, 0 }, { 16384, 0, 0 }, { 16384, 0, 0 } },
            { { 2047, -2048, 1 }, { -2048, 2047, 2 },
              { 1024, -2048, 2047 } },
        },
    };
    static const uint8_t values[] = { 0, 1, 2, 127, 128, 129, 254, 255 };
    static const unsigned alphas[] = { 1, 128, 255 };
    QTestState *q = start();
    uint8_t luma[8 * 12], uv[8 * 14], observed[sizeof(uv)];

    for (unsigned mode = 0; mode <= 4; mode += 4) {
        primary_packet(q, mode);
        memset(luma, 0xa5, sizeof(luma));
        memset(uv, 0x5a, sizeof(uv));
        for (unsigned y = 0; y < 8; y++) {
            for (unsigned x = 0; x < 8; x++) {
                luma[y * 12 + x + 2] = values[(x + y) % 8];
            }
            for (unsigned x = 0; x < 8; x += 2) {
                uv[y * 14 + x + 2] = values[(x / 2 + y) % 8];
                uv[y * 14 + x + 3] = values[(3 * x / 2 + y + 4) % 8];
            }
        }
        qtest_memwrite(q, RAM, luma, sizeof(luma));
        qtest_memwrite(q, RAM + 0x4000, uv, sizeof(uv));
        for (unsigned k = 0; k < ARRAY_SIZE(matrices); k++) {
            for (unsigned row = 0; row < 3; row++) {
                const int *first = matrices[k][0][row];
                const int *second = matrices[k][1][row];

                qtest_writel(q, BANK + 0x4a0 + row * 8,
                             (uint16_t)first[0] |
                             ((uint32_t)(uint16_t)first[1] << 16));
                qtest_writel(q, BANK + 0x4a4 + row * 8, (uint16_t)first[2]);
                qtest_writel(q, BANK + 0x4d0 + row * 8,
                             (second[0] & 0xfff) |
                             ((second[1] & 0xfff) << 16));
                qtest_writel(q, BANK + 0x4d4 + row * 8, second[2] & 0xfff);
            }
            for (unsigned apply = 0; apply < 2; apply++) {
                qtest_writel(q, BANK + 0x4f0, apply);
                for (unsigned a = 0; a < ARRAY_SIZE(alphas); a++) {
                    g_autofree char *image = NULL;

                    qtest_writel(q, BANK + 0x500, alphas[a]);
                    qtest_writel(q, BANK + 0x1200, 1);
                    wait_completion(q);
                    image = pixels(q);
                    for (unsigned y = 0; y < 8; y++) {
                        for (unsigned x = 0; x < 8; x++) {
                            unsigned uv_row = mode == 4 ? y / 2 : y;
                            unsigned index = uv_row * 14 + (x & ~1U) + 2;
                            int input[3] = { luma[y * 12 + x + 2],
                                             uv[index] - 128,
                                             uv[index + 1] - 128 };
                            int rgb[3];

                            for (unsigned c = 0; c < 3; c++) {
                                rgb[c] = primary_matrix_reference(
                                    matrices[k][0][c], input, 14);
                            }
                            for (unsigned c = 0; c < 3; c++) {
                                unsigned value = apply ?
                                    primary_matrix_reference(
                                        matrices[k][1][c], rgb, 10) : rgb[c];

                                g_assert_cmpuint(
                                    (uint8_t)image[(y * 8 + x) * 3 + c], ==,
                                    value * alphas[a] / 255);
                            }
                        }
                    }
                    qtest_writel(q, BANK + 0x1214, FRAME_PENDING);
                }
            }
        }
        qtest_memread(q, RAM, observed, sizeof(luma));
        g_assert_cmpmem(observed, sizeof(luma), luma, sizeof(luma));
        qtest_memread(q, RAM + 0x4000, observed, sizeof(uv));
        g_assert_cmpmem(observed, sizeof(uv), uv, sizeof(uv));
    }
    qtest_quit(q);
}

static void test_primary_controls(void)
{
    const struct {
        unsigned offset;
        uint32_t value;
    } invalid[] = {
        { 0x200, 0x00300001 }, { 0x200, 0x00300104 },
        { 0x204, 0x00080007 }, { 0x204, 0x00070008 },
        { 0x204, 0x3ffe4000 }, { 0x240, 6 }, { 0x250, 7 },
        { 0x210, 0xfffffffc }, { 0x220, 0xfffffff8 },
        { 0x210, 0x10020000 }, { 0x220, 0x20140000 },
        { 0x214, RAM }, { 0x224, RAM }, { 0x260, 1 },
        { 0x604, 1 }, { 0x40c, 1 }, { 0x410, 0x201 },
        { 0x430, 1 }, { 0x4c0, 0x406 }, { 0x4c0, 0x604 },
        { 0x4f0, 2 }, { 0x4a4, 0x00010000 },
        { 0x4d0, 0x1000 }, { 0x4d4, 0x1000 },
        { 0x500, 256 }, { 0x400, 1 },
    };
    QTestState *q = start();
    uint8_t luma[8 * 12], uv[8 * 14];
    g_autofree char *before = NULL;
    unsigned i;

    primary_source(q, 4, luma, uv);
    primary_packet(q, 4);
    qtest_writel(q, BANK + 0x1200, 1);
    wait_completion(q);
    before = pixels(q);
    qtest_writel(q, BANK + 0x1214, FRAME_PENDING);
    for (i = 0; i < G_N_ELEMENTS(invalid); i++) {
        g_autofree char *after = NULL;

        primary_packet(q, 4);
        qtest_writel(q, BANK + invalid[i].offset, invalid[i].value);
        qtest_writel(q, BANK + 0x1200, 1);
        qtest_clock_step(q, 1000000);
        g_assert_cmphex(qtest_readl(q, BANK + 0x1214), ==, 0);
        g_assert_false(qtest_get_irq(q, 111 - 32));
        after = pixels(q);
        g_assert_cmpmem(after, 192, before, 192);
        qtest_writel(q, BANK + invalid[i].offset, 0);
    }
    /* Unknown active-bank selection cannot hide a nonneutral table point. */
    for (i = 0; i < 6; i++) {
        static const uint32_t addresses[] = {
            0x24040000, 0x24042000, 0x24045000,
            0x24045200, 0x24046000, 0x24046180,
        };
        g_autofree char *after = NULL;
        uint32_t prior;

        primary_packet(q, 4);
        prior = qtest_readl(q, addresses[i]);
        qtest_writel(q, addresses[i], prior ^ 1);
        qtest_clock_step(q, 1000000);
        g_assert_cmphex(qtest_readl(q, BANK + 0x1214), ==, 0);
        g_assert_false(qtest_get_irq(q, 111 - 32));
        after = pixels(q);
        g_assert_cmpmem(after, 192, before, 192);
        qtest_writel(q, addresses[i], prior);
    }
    primary_packet(q, 4);
    qtest_writel(q, BANK + 0x1200, 2);
    qtest_memset(q, RAM, 0xff, sizeof(luma));
    qtest_clock_step(q, 1000000);
    g_assert_cmphex(qtest_readl(q, BANK + 0x1214), ==, 0);
    {
        g_autofree char *paused = pixels(q);

        g_assert_cmpmem(paused, 192, before, 192);
    }
    qtest_writel(q, BANK + 0x1200, 1);
    qtest_system_reset(q);
    qtest_clock_step(q, 1000000);
    g_assert_cmphex(qtest_readl(q, BANK + 0x100), ==, 0);
    g_assert_cmphex(qtest_readl(q, BANK + 0x1214), ==, 0);
    g_assert_false(qtest_get_irq(q, 111 - 32));
    qtest_quit(q);
}

static uint64_t primary_count(QTestState *q, const char *property)
{
    QDict *reply = qtest_qmp(q, "{'execute':'qom-get','arguments':"
                               "{'path':'/machine/display','property':%s}}",
                            property);
    uint64_t count = qdict_get_int(reply, "return");

    qobject_unref(reply);
    return count;
}

static void primary_next(QTestState *q)
{
    qtest_writel(q, BANK + 0x1214, FRAME_PENDING);
    wait_completion(q);
}

static void primary_pixels(QTestState *q, const uint8_t expected[192])
{
    g_autofree char *image = pixels(q);

    g_assert_cmpmem(image, 192, expected, 192);
}

static void test_primary_cache(void)
{
    QTestState *q = start();
    uint8_t luma[8 * 12], uv[8 * 14], original[192], expected[192], scaled[192];
    uint64_t conversions, reuses;

    primary_source(q, 4, luma, uv);
    primary_packet(q, 4);
    qtest_writel(q, BANK + 0x1200, 1);
    wait_completion(q);
    /* The first publication changes start1 to state3 in the full packet. */
    primary_next(q);
    {
        g_autofree char *image = pixels(q);

        memcpy(expected, image, sizeof(expected));
        memcpy(original, image, sizeof(original));
    }
    conversions = primary_count(q, "primary-conversions");
    reuses = primary_count(q, "primary-reuses");
    g_assert_cmpuint(conversions, ==, 2);
    primary_next(q);
    primary_pixels(q, expected);
    g_assert_cmpuint(primary_count(q, "primary-conversions"), ==, conversions);
    g_assert_cmpuint(primary_count(q, "primary-reuses"), ==, ++reuses);
    /* Pitch padding is outside the owned pixel rows. */
    qtest_writeb(q, RAM, 0);
    primary_next(q);
    primary_pixels(q, expected);
    g_assert_cmpuint(primary_count(q, "primary-conversions"), ==, conversions);
    g_assert_cmpuint(primary_count(q, "primary-reuses"), ==, ++reuses);

    /* One owned Y byte changes one pixel, without a register write. */
    qtest_writeb(q, RAM + 2, luma[2] + 2);
    expected[0] += 1;
    expected[1] += 2;
    expected[2] += 2;
    primary_next(q);
    primary_pixels(q, expected);
    g_assert_cmpuint(primary_count(q, "primary-conversions"), ==, ++conversions);
    /* One U byte is shared by the actual two-by-two NV12 sample. */
    qtest_writeb(q, RAM + 0x4002, uv[2] + 4);
    for (unsigned y = 0; y < 2; y++) {
        for (unsigned x = 0; x < 2; x++) {
            expected[(y * 8 + x) * 3] += 4;
            expected[(y * 8 + x) * 3 + 1] -= 1;
        }
    }
    primary_next(q);
    primary_pixels(q, expected);
    g_assert_cmpuint(primary_count(q, "primary-conversions"), ==, ++conversions);

    /* Geometry and scale replace the pixman image and its transform together. */
    qtest_writel(q, BANK + 0x204, 0x00040004);
    qtest_writel(q, BANK + 0x400, 1);
    qtest_writel(q, BANK + 0x404, 0x102);
    qtest_writel(q, BANK + 0x408, 0x102);
    for (unsigned y = 0; y < 8; y++) {
        for (unsigned x = 0; x < 8; x++) {
            memcpy(scaled + (y * 8 + x) * 3,
                   expected + ((y / 2) * 8 + x / 2) * 3, 3);
        }
    }
    primary_next(q);
    primary_pixels(q, scaled);
    g_assert_cmpuint(primary_count(q, "primary-conversions"), ==, ++conversions);
    qtest_writel(q, BANK + 0x500, 128);
    for (unsigned i = 0; i < sizeof(scaled); i++) {
        scaled[i] = scaled[i] * 128 / 255;
    }
    primary_next(q);
    primary_pixels(q, scaled);
    g_assert_cmpuint(primary_count(q, "primary-conversions"), ==, ++conversions);

    qtest_writel(q, BANK + 0x4f0, 0);
    for (unsigned y = 0; y < 8; y++) {
        for (unsigned x = 0; x < 8; x++) {
            unsigned sy = y / 2, sx = x / 2;
            unsigned value = luma[sy * 12 + sx + 2] + (!sy && !sx ? 2 : 0);
            unsigned u = uv[(sy / 2) * 14 + (sx & ~1U) + 2] - 128;
            unsigned at = (y * 8 + x) * 3;

            u += sy < 2 && sx < 2 ? 4 : 0;
            scaled[at] = (value - 16) * 128 / 255;
            scaled[at + 1] = (value - u / 4 + 16) * 128 / 255;
            scaled[at + 2] = (value + u) * 128 / 255;
        }
    }
    primary_next(q);
    primary_pixels(q, scaled);
    g_assert_cmpuint(primary_count(q, "primary-conversions"), ==, ++conversions);

    qtest_writel(q, BANK + 0x4a0, 8192);
    for (unsigned y = 0; y < 8; y++) {
        for (unsigned x = 0; x < 8; x++) {
            unsigned sy = y / 2, sx = x / 2;
            unsigned value = luma[sy * 12 + sx + 2] + (!sy && !sx ? 2 : 0);

            scaled[(y * 8 + x) * 3] = (value / 2 - 16) * 128 / 255;
        }
    }
    primary_next(q);
    primary_pixels(q, scaled);
    g_assert_cmpuint(primary_count(q, "primary-conversions"), ==, ++conversions);

    /* A cached image cannot suppress validation of a changed curve table. */
    primary_next(q);
    g_assert_cmpuint(primary_count(q, "primary-reuses"), ==, ++reuses);
    qtest_writel(q, BANK + 0x1214, FRAME_PENDING);
    qtest_writel(q, 0x24045000, 0x00080001);
    qtest_clock_step(q, 1000000);
    g_assert_false(qtest_get_irq(q, 111 - 32));
    primary_pixels(q, scaled);
    qtest_writel(q, 0x24045000, 0x00080000);
    primary_next(q);
    primary_pixels(q, scaled);
    g_assert_cmpuint(primary_count(q, "primary-conversions"), ==, ++conversions);

    qtest_writel(q, BANK + 0x100, 2);
    primary_next(q);
    primary_pixels(q, scaled);
    qtest_writel(q, BANK + 0x100, 1);
    primary_next(q);
    primary_pixels(q, scaled);
    g_assert_cmpuint(primary_count(q, "primary-conversions"), ==, ++conversions);
    qtest_writel(q, BANK + 0x1200, 2);
    qtest_writel(q, BANK + 0x1200, 1);
    primary_next(q);
    primary_pixels(q, scaled);
    g_assert_cmpuint(primary_count(q, "primary-conversions"), ==, ++conversions);

    qtest_system_reset(q);
    g_assert_cmpuint(primary_count(q, "primary-conversions"), ==, 0);
    g_assert_cmpuint(primary_count(q, "primary-reuses"), ==, 0);
    primary_source(q, 4, luma, uv);
    primary_packet(q, 4);
    qtest_writel(q, BANK + 0x1200, 1);
    wait_completion(q);
    primary_pixels(q, original);
    g_assert_cmpuint(primary_count(q, "primary-conversions"), ==, 1);
    qtest_quit(q);
}

static void test_primary_composite(void)
{
    QTestState *q = start();
    uint8_t luma[8 * 12], uv[8 * 14];
    g_autofree char *image = NULL;
    unsigned x, y;

    primary_source(q, 4, luma, uv);
    primary_packet(q, 4);
    qtest_writel(q, BANK + 0x204, 0x00040004);
    qtest_writel(q, BANK + 0x400, 1);
    qtest_writel(q, BANK + 0x404, 0x102);
    qtest_writel(q, BANK + 0x408, 0x102);
    qtest_writel(q, BANK + 0x500, 128);
    qtest_writel(q, BANK + 0x600, 0x00020002);
    /* A genuine RGBA overlay occupies the upper-left two pixels. */
    packet(q, 0, 0);
    qtest_writel(q, BANK + 0x2400, 0x00020002);
    qtest_memset(q, RAM + 0x8000, 0, 8 * 40);
    qtest_writel(q, RAM + 0x8000, 0xff00ff00);
    qtest_writel(q, RAM + 0x8004, 0xff00ff00);
    qtest_writel(q, RAM + 0x8028, 0xff00ff00);
    qtest_writel(q, RAM + 0x802c, 0xff00ff00);
    qtest_writel(q, BANK + 0x2440, RAM + 0x8000);
    qtest_writel(q, BANK + 0x1200, 1);
    wait_completion(q);
    image = pixels(q);
    for (y = 0; y < 8; y++) {
        for (x = 0; x < 8; x++) {
            unsigned sy = y >= 2 ? (y - 2) / 2 : 0;
            unsigned sx = x >= 2 ? (x - 2) / 2 : 0;
            unsigned u = 32 + (sy / 2) * 4;
            unsigned value = 48 + sy * 4 + sx * 2;
            const uint8_t *rgb = (uint8_t *)image + (y * 8 + x) * 3;

            if (x < 2 && y < 2) {
                g_assert_cmpuint(rgb[0], ==, 0);
                g_assert_cmpuint(rgb[1], ==, 255);
                g_assert_cmpuint(rgb[2], ==, 0);
            } else if (x < 2 || y < 2) {
                g_assert_cmpuint(rgb[0], ==, 0);
                g_assert_cmpuint(rgb[1], ==, 0);
                g_assert_cmpuint(rgb[2], ==, 0);
            } else {
                g_assert_cmpuint(rgb[0], ==,
                                 (value / 2 + u + 8) * 128 / 255);
                g_assert_cmpuint(rgb[1], ==, (value - u / 4 + 16) * 128 / 255);
                g_assert_cmpuint(rgb[2], ==, (value - 16) * 128 / 255);
            }
        }
    }
    qtest_writel(q, BANK + 0x1214, FRAME_PENDING);
    /* Pausing only unit0 leaves the independent overlay transaction valid. */
    qtest_writel(q, BANK + 0x100, 2);
    qtest_clock_step(q, 1000000);
    g_assert_true(qtest_get_irq(q, 111 - 32));
    g_clear_pointer(&image, g_free);
    image = pixels(q);
    g_assert_cmpuint((uint8_t)image[3 * 63], ==, 0);
    g_assert_cmpuint((uint8_t)image[3 * 63 + 1], ==, 0);
    g_assert_cmpuint((uint8_t)image[3 * 63 + 2], ==, 0);
    qtest_writel(q, BANK + 0x1214, FRAME_PENDING);
    /* A disabled unit must not even inspect an invalid retained provider. */
    qtest_writel(q, BANK + 0x100, 0);
    qtest_writel(q, BANK + 0x210, 0xfffffffc);
    qtest_clock_step(q, 1000000);
    g_assert_true(qtest_get_irq(q, 111 - 32));
    g_clear_pointer(&image, g_free);
    image = pixels(q);
    g_assert_cmpuint((uint8_t)image[1], ==, 255);
    g_assert_cmpuint((uint8_t)image[3 * 63], ==, 0);
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
    qtest_add_func("/nikon/image/yuva", test_yuva);
    qtest_add_func("/nikon/image/alpha-endpoints", test_alpha_endpoints);
    qtest_add_func("/nikon/image/yuva-fault", test_yuva_fault);
    qtest_add_func("/nikon/image/yuva-frame", test_yuva_frame);
    qtest_add_func("/nikon/image/visible-area", test_visible_area);
    qtest_add_func("/nikon/image/primary", test_primary);
    qtest_add_func("/nikon/image/primary-matrix-bounds",
                   test_primary_matrix_bounds);
    qtest_add_func("/nikon/image/primary-controls", test_primary_controls);
    qtest_add_func("/nikon/image/primary-cache", test_primary_cache);
    qtest_add_func("/nikon/image/primary-composite", test_primary_composite);
}
