/* Real independent MemoryRegion roots at equal guest physical addresses.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "hw/nikon/devices.h"
#include "fixture.h"

#define FILL 0x20070000
#define DESTINATION 0x04008000
#define SENTINEL 0x04008ea8
#define LENGTH 0x4000
#define BACKEND_SIZE 0x04010000

static char *image;

static char *backend_memory(QTestState *q)
{
    QDict *response = qtest_qmp(q, "{'execute':'qom-list',"
                              "'arguments':{'path':'/objects/private'}}");
    const QListEntry *entry;
    char *path = NULL;

    QLIST_FOREACH_ENTRY(qdict_get_qlist(response, "return"), entry) {
        const QDict *child = qobject_to(QDict, qlist_entry_obj(entry));

        if (!strcmp(qdict_get_str(child, "type"), "child<memory-region>")) {
            g_assert_null(path);
            path = g_strdup_printf("/objects/private/%s",
                                   qdict_get_str(child, "name"));
        }
    }
    qobject_unref(response);
    g_assert_nonnull(path);
    return path;
}

static QTestState *start(const char *file, bool readonly)
{
    g_autofree char *bios = g_shell_quote(image);
    g_autofree char *quoted = g_shell_quote(file);
    QTestState *q = qtest_initf(
        "-M z7ii-mas-diagnostic -bios %s -serial none "
        "-object memory-backend-file,id=private,size=%u,mem-path=%s,"
        "share=on,readonly=%s", bios, BACKEND_SIZE, quoted,
        readonly ? "on" : "off");
    g_autofree char *fill = nikon_device_path(q, TYPE_NIKON_FILL);
    g_autofree char *memory = backend_memory(q);

    /* Rebind a board-created device while paused and unrealized. No production
     * aliases or synthetic device protocol are needed to inspect private RAM.
     */
    qtest_qmp_assert_success(q, "{'execute':'qom-set',"
                            "'arguments':{'path':%s,'property':'realized',"
                            "'value':false}}", fill);
    qtest_qmp_assert_success(q, "{'execute':'qom-set',"
                            "'arguments':{'path':%s,'property':'memory',"
                            "'value':%s}}", fill, memory);
    qtest_qmp_assert_success(q, "{'execute':'qom-set',"
                            "'arguments':{'path':%s,'property':'realized',"
                            "'value':true}}", fill);
    return q;
}

static void configure_profile(QTestState *q, hwaddr base, uint32_t pitch,
                              uint32_t row_bytes, uint32_t count,
                              uint32_t destination)
{
    qtest_writel(q, base, 0);
    qtest_writel(q, base + 4, 0x11);
    qtest_writel(q, base + 0x14, 1);
    qtest_writel(q, base + 0x18, 0);
    qtest_writel(q, base + 0x1c, 0);
    qtest_writel(q, base + 0x20, pitch);
    qtest_writel(q, base + 0x24, row_bytes);
    qtest_writel(q, base + 0x28, count);
    qtest_writel(q, base + 0x2c, 0);
    qtest_writel(q, base + 0x30, destination);
}

static void request_profile(QTestState *q, uint32_t pitch, uint32_t row_bytes,
                            uint32_t count, uint32_t destination)
{
    configure_profile(q, FILL, pitch, row_bytes, count, destination);
    qtest_writel(q, FILL + 0x10, 1);
}

static void request(QTestState *q, uint32_t destination)
{
    request_profile(q, LENGTH, LENGTH, 1, destination);
}

static void domain(bool readonly)
{
    uint8_t expected[LENGTH], actual[LENGTH];
    g_autofree char *path = NULL;
    int fd = g_file_open_tmp("nikonXXXXXX", &path, NULL);
    QTestState *q;

    g_assert_cmpint(fd, >=, 0);
    g_assert_cmpint(ftruncate(fd, BACKEND_SIZE), ==, 0);
    memset(expected, 0xa5, sizeof(expected));
    g_assert_cmpint(pwrite(fd, expected, sizeof(expected), DESTINATION), ==,
                    sizeof(expected));
    q = start(path, readonly);
    /* Same PA, distinct backing: global MAS temporary stack vs private RAM. */
    qtest_writel(q, SENTINEL, 0x87654321);
    g_assert_cmphex(qtest_readl(q, SENTINEL), ==, 0x87654321);
    request(q, DESTINATION);
    g_assert_cmphex(qtest_readl(q, FILL + 8), ==, readonly ? 0x10 : 1);
    g_assert_cmphex(qtest_readl(q, FILL + 0xc), ==,
                    readonly ? 1U << 16 : 0);
    g_assert_cmphex(qtest_readl(q, SENTINEL), ==, 0x87654321);
    g_assert_cmpint(pread(fd, actual, sizeof(actual), DESTINATION), ==,
                    sizeof(actual));
    if (!readonly) {
        memset(expected, 0, sizeof(expected));
    }
    g_assert_cmpmem(actual, sizeof(actual), expected, sizeof(expected));
    qtest_writel(q, FILL + 8, 0x11);
    memset(expected, 0xa5, sizeof(expected));
    g_assert_cmpint(pwrite(fd, expected, LENGTH / 2,
                          BACKEND_SIZE - LENGTH / 2), ==, LENGTH / 2);
    request(q, BACKEND_SIZE - LENGTH / 2);
    g_assert_cmphex(qtest_readl(q, FILL + 8), ==, 0x10);
    g_assert_cmpint(pread(fd, actual, LENGTH / 2,
                         BACKEND_SIZE - LENGTH / 2), ==, LENGTH / 2);
    if (!readonly) {
        memset(expected, 0, LENGTH / 2);
    }
    g_assert_cmpmem(actual, LENGTH / 2, expected, LENGTH / 2);
    qtest_writel(q, FILL + 8, 0x11);

    /* GUI geometry reaches the selected domain and fails on missing RAM. */
    request_profile(q, 0x0b40, 0x0b40, 0x01e0, 0xbec5f200);
    g_assert_cmphex(qtest_readl(q, FILL + 8), ==, 0x10);
    qtest_writel(q, FILL + 8, 0x11);
    request_profile(q, 0x0b40, 0x0b40, 0x01e0, 0xbf309600);
    g_assert_cmphex(qtest_readl(q, FILL + 8), ==, 0x10);
    qtest_writel(q, FILL + 8, 0x11);
    request(q, BACKEND_SIZE);
    g_assert_cmphex(qtest_readl(q, FILL + 8), ==, 0x10);
    g_assert_cmphex(qtest_readl(q, SENTINEL), ==, 0x87654321);
    qtest_writel(q, FILL + 8, 0x11);
    g_assert_cmphex(qtest_readl(q, FILL + 8), ==, 0);
    qtest_quit(q);
    close(fd);
    g_assert_cmpint(unlink(path), ==, 0);
}

static void test_private(void)
{
    domain(false);
}

static void test_readonly(void)
{
    domain(true);
}

static void test_rows(void)
{
    static const struct {
        uint32_t pitch, row_bytes, count;
    } shapes[] = {
        { 48, 31, 3 }, { 64, 64, 5 }, { 1, 1, 1 },
    };
    static const struct {
        uint32_t pitch, row_bytes, count, destination;
    } invalid[] = {
        { 0, 0, 1, DESTINATION },
        { 48, 0, 3, DESTINATION },
        { 48, 31, 0, DESTINATION },
        { 30, 31, 3, DESTINATION },
        { 48, 31, 3, DESTINATION + 1 },
        { 48, 31, 3, 0xfffffff0 },
    };
    static const struct {
        unsigned offset;
        uint32_t value;
    } unsupported[] = {
        { 0, 1 }, { 0x14, 0 }, { 0x14, 0x11 }, { 0x18, 1 },
        { 0x1c, 1 }, { 0x2c, 1 },
    };
    uint8_t expected[512], actual[512];
    g_autofree char *path = NULL;
    int fd = g_file_open_tmp("nikonXXXXXX", &path, NULL);
    QTestState *q;

    g_assert_cmpint(fd, >=, 0);
    g_assert_cmpint(ftruncate(fd, BACKEND_SIZE), ==, 0);
    q = start(path, false);
    g_autofree char *fill = nikon_device_path(q, TYPE_NIKON_FILL);
    qtest_irq_intercept_out_named(q, fill, "sysbus-irq");
    for (unsigned engine = 0; engine < 2; engine++) {
        hwaddr base = FILL + engine * 0x40;

        for (unsigned shape = 0; shape < ARRAY_SIZE(shapes); shape++) {
            uint32_t pitch = shapes[shape].pitch;
            uint32_t width = shapes[shape].row_bytes;
            uint32_t count = shapes[shape].count;

            memset(expected, 0xa5, sizeof(expected));
            g_assert_cmpint(pwrite(fd, expected, sizeof(expected),
                                  DESTINATION - 4), ==, sizeof(expected));
            configure_profile(q, base, pitch, width, count, DESTINATION);
            qtest_writel(q, base + 0x10, 1);
            g_assert_cmphex(qtest_readl(q, base + 8), ==, 1);
            g_assert_cmphex(qtest_readl(q, base + 0xc), ==, 0);
            g_assert_cmphex(qtest_readl(q, base + 0x10), ==, 0);
            g_assert_true(qtest_get_irq(q, engine));
            for (unsigned row = 0; row < count; row++) {
                memset(expected + 4 + row * pitch, 0, width);
            }
            g_assert_cmpint(pread(fd, actual, sizeof(actual),
                                 DESTINATION - 4), ==, sizeof(actual));
            g_assert_cmpmem(actual, sizeof(actual), expected, sizeof(expected));
            qtest_writel(q, base + 8, 1);
            g_assert_false(qtest_get_irq(q, engine));
        }
        memset(expected, 0xa5, sizeof(expected));
        g_assert_cmpint(pwrite(fd, expected, sizeof(expected),
                              DESTINATION - 4), ==, sizeof(expected));
        for (unsigned i = 0; i < ARRAY_SIZE(invalid); i++) {
            configure_profile(q, base, invalid[i].pitch, invalid[i].row_bytes,
                              invalid[i].count, invalid[i].destination);
            qtest_writel(q, base + 0x10, 1);
            g_assert_cmphex(qtest_readl(q, base + 8), ==, 0);
            g_assert_false(qtest_get_irq(q, engine));
        }
        for (unsigned i = 0; i < ARRAY_SIZE(unsupported); i++) {
            configure_profile(q, base, 48, 31, 3, DESTINATION);
            qtest_writel(q, base + unsupported[i].offset, unsupported[i].value);
            qtest_writel(q, base + 0x10, 1);
            g_assert_cmphex(qtest_readl(q, base + 8), ==, 0);
            g_assert_false(qtest_get_irq(q, engine));
        }
        g_assert_cmpint(pread(fd, actual, sizeof(actual), DESTINATION - 4), ==,
                        sizeof(actual));
        g_assert_cmpmem(actual, sizeof(actual), expected, sizeof(expected));

        /* A bus error after two rows keeps their real writes and padding. */
        g_assert_cmpint(pwrite(fd, expected, 128, BACKEND_SIZE - 128), ==, 128);
        configure_profile(q, base, 48, 31, 3, BACKEND_SIZE - 80);
        qtest_writel(q, base + 0x10, 1);
        g_assert_cmphex(qtest_readl(q, base + 8), ==, 0x10);
        g_assert_cmphex(qtest_readl(q, base + 0xc), ==, 1U << 16);
        g_assert_true(qtest_get_irq(q, engine));
        memset(expected + 48, 0, 31);
        memset(expected + 96, 0, 31);
        g_assert_cmpint(pread(fd, actual, 128, BACKEND_SIZE - 128), ==, 128);
        g_assert_cmpmem(actual, 128, expected, 128);
        qtest_writel(q, base + 8, 0x11);
        g_assert_false(qtest_get_irq(q, engine));
    }
    qtest_quit(q);
    close(fd);
    g_assert_cmpint(unlink(path), ==, 0);
}

static void test_composition(void)
{
    const uint32_t destination = 0xbf309600, length = 720 * 480 * 4;
    const uint32_t slot_end = 0xbf7b9600;
    g_autofree uint8_t *bytes = g_malloc(length);
    g_autofree uint8_t *zero = g_malloc0(length);
    g_autofree char *bios = g_shell_quote(image);
    QTestState *q = qtest_initf("-M z7ii-mas-diagnostic -bios %s -serial none",
                               bios);
    g_autofree char *fill = nikon_device_path(q, TYPE_NIKON_FILL);

    qtest_irq_intercept_out_named(q, fill, "sysbus-irq");
    memset(bytes, 0xa5, length);
    qtest_memwrite(q, destination, bytes, length);
    qtest_writel(q, destination - 4, 0x57050001);
    qtest_writel(q, destination + length, 0x57050002);
    qtest_writel(q, slot_end - 4, 0x57050003);
    request_profile(q, 0xb40, 0xb40, 0, destination);
    g_assert_cmphex(qtest_readl(q, FILL + 8), ==, 0);
    request_profile(q, 0xb40, 0xb40, 0x1e0, destination + 1);
    g_assert_cmphex(qtest_readl(q, FILL + 8), ==, 0);
    qtest_memread(q, destination, zero, length);
    g_assert_cmpmem(bytes, length, zero, length);
    memset(zero, 0, length);
    g_assert_false(qtest_get_irq(q, 0));
    request_profile(q, 0xb40, 0xb40, 0x1e0, destination);
    g_assert_cmphex(qtest_readl(q, FILL + 8), ==, 1);
    g_assert_cmphex(qtest_readl(q, FILL + 0xc), ==, 0);
    g_assert_true(qtest_get_irq(q, 0));
    qtest_memread(q, destination, bytes, length);
    g_assert_cmpmem(bytes, length, zero, length);
    g_assert_cmphex(qtest_readl(q, destination - 4), ==, 0x57050001);
    g_assert_cmphex(qtest_readl(q, destination + length), ==, 0x57050002);
    g_assert_cmphex(qtest_readl(q, slot_end - 4), ==, 0x57050003);
    qtest_writel(q, FILL + 8, 1);
    g_assert_false(qtest_get_irq(q, 0));
    request_profile(q, 0xb40, 0xb40, 0x1e0, destination);
    g_assert_true(qtest_get_irq(q, 0));
    qtest_system_reset(q);
    g_assert_cmphex(qtest_readl(q, FILL + 8), ==, 0);
    g_assert_false(qtest_get_irq(q, 0));
    g_assert_cmphex(qtest_readl(q, destination + length), ==, 0x57050002);
    qtest_quit(q);
}

int main(int argc, char **argv)
{
    const uint8_t idle[] = { 0xfe, 0xff, 0xff, 0xea };
    int fd, result;

    g_test_init(&argc, &argv, NULL);
    fd = g_file_open_tmp("nikonXXXXXX", &image, NULL);
    g_assert_cmpint(fd, >=, 0);
    g_assert_cmpint(write(fd, idle, sizeof(idle)), ==, sizeof(idle));
    close(fd);
    qtest_add_func("/nikon/fill/private", test_private);
    qtest_add_func("/nikon/fill/readonly", test_readonly);
    qtest_add_func("/nikon/fill/rows", test_rows);
    qtest_add_func("/nikon/fill/composition", test_composition);
    result = g_test_run();
    unlink(image);
    g_free(image);
    return result;
}
