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

static void request_profile(QTestState *q, uint32_t width, uint32_t stride,
                            uint32_t count, uint32_t destination)
{
    qtest_writel(q, FILL, 0);
    qtest_writel(q, FILL + 4, 0x11);
    qtest_writel(q, FILL + 0x14, 1);
    qtest_writel(q, FILL + 0x18, 0);
    qtest_writel(q, FILL + 0x1c, 0);
    qtest_writel(q, FILL + 0x20, width);
    qtest_writel(q, FILL + 0x24, stride);
    qtest_writel(q, FILL + 0x28, count);
    qtest_writel(q, FILL + 0x2c, 0);
    qtest_writel(q, FILL + 0x30, destination);
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

    /* The exact GUI tuple reaches the selected address space and fails when
     * its fixed record0 destination is not mapped in this private domain. */
    request_profile(q, 0x0b40, 0x0b40, 0x01e0, 0xbec5f200);
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
    result = g_test_run();
    unlink(image);
    g_free(image);
    return result;
}
