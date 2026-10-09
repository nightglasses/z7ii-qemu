/*
 * Complete source-declared storage profile; no firmware or host objects.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "libqtest.h"

typedef struct MemoryTest {
    QTestState *qts;
    char *image;
} MemoryTest;

static MemoryTest memory_init(void)
{
    MemoryTest t = { 0 };
    uint32_t loop = cpu_to_le32(0xeafffffe);
    int fd = g_file_open_tmp("nikon-memory-XXXXXX", &t.image, NULL);

    g_assert_cmpint(fd, >=, 0);
    g_assert_cmpint(write(fd, &loop, sizeof(loop)), ==, sizeof(loop));
    close(fd);
    t.qts = qtest_initf("-machine z7ii-mas-diagnostic -smp 1 -bios %s "
                        "-display none -serial none", t.image);
    return t;
}

static void memory_quit(MemoryTest *t)
{
    qtest_quit(t->qts);
    unlink(t->image);
    g_free(t->image);
}

static const uint64_t class_bounds[][2] = {
    { 0xb1800000, 0xb5f00000 },
    { 0xb5f00000, 0xb7d00000 },
    { 0xb7d00000, 0xbaa00000 },
    { 0xbaa00000, 0xbb300000 },
    { 0xbb300000, 0xbc100000 },
    { 0xbc100000, 0xbd200000 },
};

static void test_storage_profile(void)
{
    static const uint64_t addresses[] = {
        0xb18aedc0, 0xb2b74000, 0xb2b88000, 0xb2bb0000,
        0xb2c00000, 0xb7cf0000, 0xb80dc3c0, 0xb8108580,
        0xbc121690, 0xbd1dfffc, 0xbd1e0000,
    };
    MemoryTest t = memory_init();
    unsigned i;

    /* Each class owns both its first and last words, with distinct values. */
    for (i = 0; i < ARRAY_SIZE(class_bounds); i++) {
        qtest_writel(t.qts, class_bounds[i][0], 0xabc10000 + i);
        qtest_writel(t.qts, class_bounds[i][1] - 4, 0xabc20000 + i);
    }
    for (i = 0; i < ARRAY_SIZE(class_bounds); i++) {
        g_assert_cmphex(qtest_readl(t.qts, class_bounds[i][0]), ==,
                        0xabc10000 + i);
        g_assert_cmphex(qtest_readl(t.qts, class_bounds[i][1] - 4), ==,
                        0xabc20000 + i);
    }

    /* Copies, stacks and storage beyond individual arena limits share RAM. */
    for (i = 0; i < ARRAY_SIZE(addresses); i++) {
        g_assert_cmphex(qtest_readl(t.qts, addresses[i]), ==, 0);
        qtest_writel(t.qts, addresses[i], 0xabc30000 + i);
    }
    for (i = 0; i < ARRAY_SIZE(addresses); i++) {
        g_assert_cmphex(qtest_readl(t.qts, addresses[i]), ==, 0xabc30000 + i);
    }
    memory_quit(&t);
}

static void test_no_bus_mirror(void)
{
    static const uint64_t views[][2] = {
        { 0x14000000, 0xb5f00000 },
        { 0x15dffffc, 0xb7cffffc },
        { 0x15e00000, 0xb7d00000 },
        { 0x18affffc, 0xba9ffffc },
        { 0x18b00000, 0xbc100000 },
        { 0x19bffffc, 0xbd1ffffc },
    };
    MemoryTest t = memory_init();
    unsigned i;

    for (i = 0; i < ARRAY_SIZE(views); i++) {
        qtest_writel(t.qts, views[i][1], 0xdef20000 + i);
        /* Logical CPU views require guest page tables, not a bus alias. */
        qtest_writel(t.qts, views[i][0], 0x12340000 + i);
        g_assert_cmphex(qtest_readl(t.qts, views[i][1]), ==, 0xdef20000 + i);
    }
    memory_quit(&t);
}

static void set_decode(MemoryTest *t, uint32_t high_length)
{
    qtest_writel(t->qts, 0x20016d50, 0x60000000);
    qtest_writel(t->qts, 0x20016d54, 0x50000000);
    qtest_writel(t->qts, 0x20016d58, high_length);
    qtest_writel(t->qts, 0x20016d5c, 0xb0000000);
    qtest_writel(t->qts, 0x20016d40, 1);
}

static void test_decode_retention(void)
{
    MemoryTest t = memory_init();
    unsigned i;

    for (i = 0; i < ARRAY_SIZE(class_bounds); i++) {
        qtest_writel(t.qts, class_bounds[i][0], 0xdec10000 + i);
    }
    set_decode(&t, 0);
    for (i = 0; i < ARRAY_SIZE(class_bounds); i++) {
        g_assert_cmphex(qtest_readl(t.qts, class_bounds[i][0]), ==, 0);
        qtest_writel(t.qts, class_bounds[i][0], 0xdead0000 + i);
    }
    set_decode(&t, 0x50000000);
    for (i = 0; i < ARRAY_SIZE(class_bounds); i++) {
        g_assert_cmphex(qtest_readl(t.qts, class_bounds[i][0]), ==,
                        0xdec10000 + i);
    }
    memory_quit(&t);
}

static void test_outer_holes(void)
{
    static const uint64_t addresses[] = { 0xb17ffffc, 0xbd200000 };
    MemoryTest t = memory_init();
    unsigned i;

    /* An address classifier catch-all is not an unlimited physical RAM grant. */
    for (i = 0; i < ARRAY_SIZE(addresses); i++) {
        qtest_writel(t.qts, addresses[i], 0x12345678);
        g_assert_cmphex(qtest_readl(t.qts, addresses[i]), ==, 0);
    }
    memory_quit(&t);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/nikon/memory/storage-profile", test_storage_profile);
    qtest_add_func("/nikon/memory/no-physical-logical-mirror",
                   test_no_bus_mirror);
    qtest_add_func("/nikon/memory/decode-retention", test_decode_retention);
    qtest_add_func("/nikon/memory/outer-holes", test_outer_holes);
    return g_test_run();
}
