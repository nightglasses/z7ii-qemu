/* Real private RAM, peripheral FIFO and request-wire DMA contracts.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "hw/nikon/devices.h"
#include "fixture.h"

#define HDMAC 0x2c020000
#define AUDIO 0x2c080000
#define SOURCE 0x04008000
#define DESTINATION 0x04008ea8
#define BACKEND_SIZE 0x04010000
#define CB 0x08180000

static char *image;

static char *memory_child(QTestState *q, const char *owner)
{
    QDict *response = qtest_qmp(q, "{'execute':'qom-list',"
                              "'arguments':{'path':%s}}", owner);
    const QListEntry *entry;
    char *path = NULL;

    QLIST_FOREACH_ENTRY(qdict_get_qlist(response, "return"), entry) {
        const QDict *child = qobject_to(QDict, qlist_entry_obj(entry));

        if (!strcmp(qdict_get_str(child, "type"), "child<memory-region>")) {
            g_assert_null(path);
            path = g_strdup_printf("%s/%s", owner,
                                   qdict_get_str(child, "name"));
        }
    }
    qobject_unref(response);
    g_assert_nonnull(path);
    return path;
}

static char *controller(QTestState *q)
{
    QDict *response = qtest_qmp(q, "{'execute':'qom-list',"
                              "'arguments':{'path':'/machine/unattached'}}");
    const QListEntry *entry;
    char *path = NULL;

    QLIST_FOREACH_ENTRY(qdict_get_qlist(response, "return"), entry) {
        const QDict *child = qobject_to(QDict, qlist_entry_obj(entry));

        if (!strcmp(qdict_get_str(child, "type"),
                    "child<" TYPE_NIKON_HDMAC ">")) {
            g_autofree char *candidate = g_strdup_printf(
                "/machine/unattached/%s", qdict_get_str(child, "name"));
            QDict *bank = qtest_qmp(q, "{'execute':'qom-get',"
                                  "'arguments':{'path':%s,'property':'bank'}}",
                                  candidate);

            if (!qdict_get_uint(bank, "return")) {
                g_assert_null(path);
                path = g_strdup(candidate);
            }
            qobject_unref(bank);
        }
    }
    qobject_unref(response);
    g_assert_nonnull(path);
    return path;
}

static void select_memory(QTestState *q, const char *owner)
{
    g_autofree char *device = controller(q);
    g_autofree char *memory = memory_child(q, owner);

    qtest_qmp_assert_success(q, "{'execute':'qom-set',"
                            "'arguments':{'path':%s,'property':'realized',"
                            "'value':false}}", device);
    qtest_qmp_assert_success(q, "{'execute':'qom-set',"
                            "'arguments':{'path':%s,'property':'memory',"
                            "'value':%s}}", device, memory);
    qtest_qmp_assert_success(q, "{'execute':'qom-set',"
                            "'arguments':{'path':%s,'property':'realized',"
                            "'value':true}}", device);
}

static QTestState *start(const char *file, bool readonly)
{
    g_autofree char *bios = g_shell_quote(image);
    g_autofree char *quoted = file ? g_shell_quote(file) : NULL;
    g_autofree char *backend = file ? g_strdup_printf(
        "-object memory-backend-file,id=private,size=%u,mem-path=%s,"
        "share=on,readonly=%s", BACKEND_SIZE, quoted,
        readonly ? "on" : "off") : g_strdup("");
    QTestState *q = qtest_initf("-M z7ii-mas-diagnostic -smp 1 -bios %s "
                              "-serial none %s", bios, backend);

    nikon_intercept_gic(q);
    if (file) {
        select_memory(q, "/objects/private");
    }
    return q;
}

static void request(QTestState *q, uint32_t source, uint32_t destination,
                    uint32_t cb, uint32_t ca)
{
    qtest_writel(q, HDMAC, 0x90000000);
    qtest_writel(q, HDMAC + 0x18, source);
    qtest_writel(q, HDMAC + 0x1c, destination);
    qtest_writel(q, HDMAC + 0x14, cb);
    qtest_writel(q, HDMAC + 0x10, ca);
}

static int backing(char **path)
{
    int fd = g_file_open_tmp("nikonXXXXXX", path, NULL);
    uint8_t source[] = { 0x78, 0x56, 0x34, 0x12, 0xef, 0xcd, 0xab, 0x90 };

    g_assert_cmpint(fd, >=, 0);
    g_assert_cmpint(ftruncate(fd, BACKEND_SIZE), ==, 0);
    g_assert_cmpint(pwrite(fd, source, sizeof(source), SOURCE), ==,
                    sizeof(source));
    return fd;
}

static void result(QTestState *q, unsigned status)
{
    g_assert_cmphex(qtest_readl(q, HDMAC + 0x14), ==, CB | status << 16);
    g_assert_true(qtest_get_irq(q, 150 - 32));
    qtest_writel(q, HDMAC + 0x14, 0x08000000);
    g_assert_false(qtest_get_irq(q, 150 - 32));
}

static void test_private(void)
{
    g_autofree char *path = NULL;
    uint8_t actual[8], expected[] = { 0x78, 0x56, 0x34, 0x12,
                                     0xef, 0xcd, 0xab, 0x90 };
    int fd = backing(&path);
    QTestState *q = start(path, false);

    qtest_writel(q, DESTINATION, 0x87654321);
    g_assert_cmphex(qtest_readl(q, DESTINATION), ==, 0x87654321);
    request(q, SOURCE, DESTINATION, CB, 0xa0000001);
    qtest_clock_step_next(q);
    result(q, 5);
    g_assert_cmpint(pread(fd, actual, sizeof(actual), DESTINATION), ==,
                    sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), expected, sizeof(expected));
    g_assert_cmphex(qtest_readl(q, DESTINATION), ==, 0x87654321);
    request(q, BACKEND_SIZE, DESTINATION, CB, 0xa0000000);
    qtest_clock_step_next(q);
    result(q, 3);
    request(q, SOURCE, BACKEND_SIZE, CB, 0xa0000000);
    qtest_clock_step_next(q);
    result(q, 4);
    g_assert_cmphex(qtest_readl(q, DESTINATION), ==, 0x87654321);
    qtest_quit(q);
    close(fd);
    g_assert_cmpint(unlink(path), ==, 0);
}

static void test_readonly(void)
{
    g_autofree char *path = NULL;
    uint32_t actual = 1;
    int fd = backing(&path);
    QTestState *q = start(path, true);

    request(q, SOURCE, DESTINATION, CB, 0xa0000000);
    qtest_clock_step_next(q);
    result(q, 4);
    g_assert_cmpint(pread(fd, &actual, sizeof(actual), DESTINATION), ==,
                    sizeof(actual));
    g_assert_cmphex(actual, ==, 0);
    qtest_quit(q);
    close(fd);
    g_assert_cmpint(unlink(path), ==, 0);
}

static void test_fifo(void)
{
    QTestState *q = start(NULL, false);
    g_autofree char *audio = nikon_device_path(q, TYPE_NIKON_AUDIO);

    select_memory(q, audio);
    /* Reset selects one stage. Configure the established 16-stage profile
     * before submitting two beats; no virtual sink is started or data seeded.
     */
    qtest_writel(q, AUDIO + 0x10c, 1U << 25);
    g_assert_cmphex(qtest_readl(q, AUDIO + 0x120), ==, 0);
    /* The actual Audio aperture retains its mapped address when used as an
     * AddressSpace root. Both fixed beats really read occupancy then write
     * the FIFO; the private view contains no RAM or other device mappings.
     */
    request(q, AUDIO + 0x120, AUDIO + 0x104, CB | (3U << 24), 0xa0000001);
    qtest_clock_step_next(q);
    g_assert_cmphex(qtest_readl(q, HDMAC + 0x14), ==,
                    (CB | (3U << 24) | (5U << 16)));
    g_assert_cmphex(qtest_readl(q, AUDIO + 0x120), ==, 0xf);
    qtest_quit(q);
}

static void test_requests(void)
{
    g_autofree char *path = NULL;
    int fd = backing(&path);
    QTestState *q = start(path, false);
    g_autofree char *device = controller(q);

    for (unsigned selector = 1; selector < 32; selector++) {
        uint8_t actual[8] = { 0 };
        uint32_t ca = 0x80000001 | selector << 24;

        g_assert_cmpint(pwrite(fd, actual, sizeof(actual), DESTINATION), ==,
                        sizeof(actual));
        qtest_set_irq_in(q, device, NULL, selector, 0);
        request(q, SOURCE, DESTINATION, CB, ca);
        qtest_clock_step(q, 100000);
        g_assert_cmphex(qtest_readl(q, HDMAC + 0x18), ==, SOURCE);
        g_assert_false(qtest_get_irq(q, 150 - 32));
        qtest_set_irq_in(q, device, NULL, selector, 1);
        qtest_clock_step_next(q);
        qtest_set_irq_in(q, device, NULL, selector, 0);
        /* A continuously available real request accepts both beats. */
        g_assert_cmphex(qtest_readl(q, HDMAC + 0x18), ==, SOURCE + 8);
        qtest_clock_step(q, 100000);
        g_assert_cmphex(qtest_readl(q, HDMAC + 0x18), ==, SOURCE + 8);
        result(q, 5);
        qtest_set_irq_in(q, device, NULL, selector, 0);
    }
    {
        uint8_t actual[8] = { 0 }, zero[8] = { 0 };

        g_assert_cmpint(pwrite(fd, zero, sizeof(zero), DESTINATION), ==,
                        sizeof(zero));
        request(q, SOURCE, DESTINATION, CB, 0xa0000000);
        qtest_writel(q, HDMAC + 0x10, 0x20000000);
        qtest_clock_step(q, 100000);
        result(q, 2);
        g_assert_cmpint(pread(fd, actual, sizeof(actual), DESTINATION), ==,
                        sizeof(actual));
        g_assert_cmpmem(actual, sizeof(actual), zero, sizeof(zero));
        request(q, SOURCE, DESTINATION, CB, 0xa0000000);
        qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
        qtest_qmp_eventwait(q, "RESET");
        qtest_clock_step(q, 100000);
        g_assert_cmphex(qtest_readl(q, HDMAC + 0x14), ==, 0);
        g_assert_false(qtest_get_irq(q, 150 - 32));
        g_assert_cmpint(pread(fd, actual, sizeof(actual), DESTINATION), ==,
                        sizeof(actual));
        g_assert_cmpmem(actual, sizeof(actual), zero, sizeof(zero));
    }
    qtest_quit(q);
    close(fd);
    g_assert_cmpint(unlink(path), ==, 0);
}

static void test_reload_requests(void)
{
    g_autofree char *path = NULL;
    int fd = backing(&path);
    QTestState *q = start(path, false);
    g_autofree char *device = controller(q);
    uint32_t cb = CB | 0xe00000;
    uint8_t actual[8], expected[] = { 0x78, 0x56, 0x34, 0x12,
                                     0xef, 0xcd, 0xab, 0x90 };

    request(q, SOURCE, DESTINATION, cb, 0x85000001);
    qtest_clock_step(q, 100000);
    g_assert_cmphex(qtest_readl(q, HDMAC + 0x14), ==, cb);
    qtest_set_irq_in(q, device, NULL, 5, 1);
    qtest_clock_step_next(q);
    g_assert_cmphex(qtest_readl(q, HDMAC + 0x18), ==, SOURCE);
    g_assert_cmphex(qtest_readl(q, HDMAC + 0x1c), ==, DESTINATION);
    g_assert_cmphex(qtest_readl(q, HDMAC + 0x10), ==, 0x05000001);
    g_assert_cmphex(qtest_readl(q, HDMAC + 0x14), ==, cb | 0x50000);
    g_assert_true(qtest_get_irq(q, 150 - 32));
    g_assert_cmpint(pread(fd, actual, sizeof(actual), DESTINATION), ==,
                    sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), expected, sizeof(expected));
    /* A still-high request cannot re-enable a normally completed channel. */
    qtest_writel(q, HDMAC + 0x14, cb);
    qtest_clock_step(q, 100000);
    g_assert_cmphex(qtest_readl(q, HDMAC + 0x18), ==, SOURCE);
    g_assert_false(qtest_get_irq(q, 150 - 32));

    /* First beat succeeds; second genuinely fails in the private root.
     * Errors retain accepted progress and never restore the initial address. */
    qtest_set_irq_in(q, device, NULL, 5, 0);
    request(q, SOURCE, BACKEND_SIZE - 4, cb, 0xa0000001);
    qtest_clock_step_next(q);
    g_assert_cmphex(qtest_readl(q, HDMAC + 0x14), ==, cb | 0x40000);
    g_assert_cmphex(qtest_readl(q, HDMAC + 0x18), ==, SOURCE + 4);
    g_assert_cmphex(qtest_readl(q, HDMAC + 0x1c), ==, BACKEND_SIZE);
    g_assert_false(qtest_readl(q, HDMAC + 0x10) & 0x80000000);
    request(q, BACKEND_SIZE - 4, DESTINATION, cb, 0xa0000001);
    qtest_clock_step_next(q);
    g_assert_cmphex(qtest_readl(q, HDMAC + 0x14), ==, cb | 0x30000);
    g_assert_cmphex(qtest_readl(q, HDMAC + 0x18), ==, BACKEND_SIZE);
    g_assert_cmphex(qtest_readl(q, HDMAC + 0x1c), ==, DESTINATION + 4);
    qtest_quit(q);
    close(fd);
    g_assert_cmpint(unlink(path), ==, 0);
}

int main(int argc, char **argv)
{
    const uint8_t idle[] = { 0xfe, 0xff, 0xff, 0xea };
    int fd, status;

    g_test_init(&argc, &argv, NULL);
    fd = g_file_open_tmp("nikonXXXXXX", &image, NULL);
    g_assert_cmpint(fd, >=, 0);
    g_assert_cmpint(write(fd, idle, sizeof(idle)), ==, sizeof(idle));
    close(fd);
    qtest_add_func("/nikon/hdmac/private", test_private);
    qtest_add_func("/nikon/hdmac/readonly", test_readonly);
    qtest_add_func("/nikon/hdmac/fifo", test_fifo);
    qtest_add_func("/nikon/hdmac/requests", test_requests);
    qtest_add_func("/nikon/hdmac/reload", test_reload_requests);
    status = g_test_run();
    unlink(image);
    g_free(image);
    return status;
}
