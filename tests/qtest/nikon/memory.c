/*
 * Complete source-declared storage profile; no firmware or host objects.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "hw/nikon/devices.h"
#include "libqtest.h"

typedef struct MemoryTest {
    QTestState *qts;
    char *image;
} MemoryTest;

static MemoryTest memory_init(const char *options)
{
    MemoryTest t = { 0 };
    uint32_t loop = cpu_to_le32(0xeafffffe);
    int fd = g_file_open_tmp("nikon-memory-XXXXXX", &t.image, NULL);

    g_assert_cmpint(fd, >=, 0);
    g_assert_cmpint(write(fd, &loop, sizeof(loop)), ==, sizeof(loop));
    close(fd);
    t.qts = qtest_initf("-machine z7ii-mas-diagnostic -smp 1 -bios %s "
                        "-display none -serial none %s", t.image, options);
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
    MemoryTest t = memory_init("");
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
    MemoryTest t = memory_init("");
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
    MemoryTest t = memory_init("");
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
    MemoryTest t = memory_init("");
    unsigned i;

    /* An address classifier catch-all is not an unlimited physical RAM grant. */
    for (i = 0; i < ARRAY_SIZE(addresses); i++) {
        qtest_writel(t.qts, addresses[i], 0x12345678);
        g_assert_cmphex(qtest_readl(t.qts, addresses[i]), ==, 0);
    }
    memory_quit(&t);
}

static void prefix_copy(QTestState *qts, uint32_t source,
                        uint32_t destination, uint32_t length)
{
    qtest_writel(qts, 0x04251000, 0x10000000);
    qtest_writel(qts, 0x04251010, length - 1);
    qtest_writel(qts, 0x04251014, source);
    qtest_writel(qts, 0x04251018, destination);
    qtest_writel(qts, 0x0425101c, 0x30f00);
    qtest_writel(qts, 0x04251020, 0x30f00);
    qtest_writel(qts, 0x04251024, 0);
    qtest_writel(qts, 0x04251028, 0x9000);
    qtest_writel(qts, 0x04251028, 0x11109003);
    qtest_clock_step(qts, (uint64_t)length * 1000000000 / 48000000 + 1);
}

static void test_dynamic_load(void)
{
    const size_t length = 4099;
    const uint32_t source = 0xb1804000;
    const uint32_t destinations[] = {
        0xb1000000, 0xb1400003, 0xb1800000 - length,
    };
    g_autofree uint8_t *expected = g_malloc(ARRAY_SIZE(destinations) * length);
    g_autofree uint8_t *actual = g_malloc(length);
    MemoryTest t = memory_init("");

    /* Distinct patterns detect aliases between the first, middle and last
     * bytes of class20, including an unaligned transfer with a non-word-sized
     * tail. */
    for (unsigned i = 0; i < ARRAY_SIZE(destinations); i++) {
        uint8_t *pattern = expected + i * length;

        g_assert_cmphex(qtest_readl(t.qts, destinations[i]), ==, 0);
        for (size_t j = 0; j < length; j++) {
            pattern[j] = (j * 37 + (j >> 8) * 11 + i * 59 + 7) & 0xff;
        }
        qtest_memwrite(t.qts, source + i * 0x2000, pattern, length);
        if (i) {
            qtest_writel(t.qts, destinations[i] - 4, 0xabc40000 + i);
        }
        /* The last after-guard is the adjacent kernel class's first word. */
        qtest_writel(t.qts, destinations[i] + length, 0xabc50000 + i);
        prefix_copy(t.qts, source + i * 0x2000, destinations[i], length);
        g_assert_cmphex(qtest_readl(t.qts, 0x04251030), ==, 8);
    }

    /* Reject a whole transaction crossing the lower hole into valid class20;
     * neither its valid suffix nor any source byte may be committed. */
    prefix_copy(t.qts, source, 0xb0fffffc, 8);
    g_assert_cmphex(qtest_readl(t.qts, 0x04251030), ==, 1);
    g_assert_cmphex(qtest_readl(t.qts, 0x04251010), ==, 7);
    g_assert_cmphex(qtest_readl(t.qts, 0x04251014), ==, source);
    g_assert_cmphex(qtest_readl(t.qts, 0x04251018), ==, 0xb0fffffc);

    for (unsigned phase = 0; phase < 2; phase++) {
        if (phase) {
            qtest_system_reset(t.qts);
        }
        /* Later copies and reset preserve independent class20/kernel bytes. */
        for (unsigned i = 0; i < ARRAY_SIZE(destinations); i++) {
            qtest_memread(t.qts, destinations[i], actual, length);
            g_assert_cmpmem(actual, length, expected + i * length, length);
            qtest_memread(t.qts, source + i * 0x2000, actual, length);
            g_assert_cmpmem(actual, length, expected + i * length, length);
            if (i) {
                g_assert_cmphex(qtest_readl(t.qts, destinations[i] - 4), ==,
                                0xabc40000 + i);
            }
            g_assert_cmphex(qtest_readl(t.qts, destinations[i] + length), ==,
                            0xabc50000 + i);
        }
    }
    memory_quit(&t);
}

static void test_download_scratch(void)
{
    const uint32_t source = 0x10020000;
    const uint32_t destination = 0xb0d00000;
    uint8_t expected[4];
    uint8_t actual[4];
    MemoryTest t = memory_init("");

    /* The selector-zero loader's source is the four-byte diagnostic image in
     * this fixture; the destination is the dedicated 4 MiB download buffer.
     * This does not alias any declared DDR storage class. */
    qtest_memread(t.qts, source, expected, sizeof(expected));
    g_assert_cmphex(qtest_readl(t.qts, destination), ==, 0);
    prefix_copy(t.qts, source, destination, sizeof(expected));
    g_assert_cmphex(qtest_readl(t.qts, 0x04251030), ==, 8);
    qtest_memread(t.qts, destination, actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), expected, sizeof(expected));

    /* The explicit 4 MiB bound is part of the source contract. */
    qtest_writel(t.qts, destination + 0x400000 - 4, 0xa5a55a5a);
    g_assert_cmphex(qtest_readl(t.qts, destination + 0x400000 - 4), ==,
                    0xa5a55a5a);
    qtest_writel(t.qts, destination + 0x400000, 0x5aa5a55a);
    g_assert_cmphex(qtest_readl(t.qts, destination + 0x400000), ==, 0);
    memory_quit(&t);
}

static void test_external_input(void)
{
    const size_t prefix_size = 0x20000, length = 0x100000;
    g_autofree uint8_t *image = g_malloc(length - prefix_size);
    g_autofree uint8_t *prefix = g_malloc(prefix_size);
    g_autofree uint8_t *expected = g_malloc(length);
    g_autofree uint8_t *actual = g_malloc(length);
    g_autofree char *image_path = NULL, *prefix_path = NULL;
    int image_fd, prefix_fd;

    for (size_t i = 0; i < length - prefix_size; i++) {
        image[i] = (i * 29 + 7) & 0xff;
    }
    stl_le_p(image, 0xeafffffe);
    for (size_t i = 0; i < prefix_size; i++) {
        prefix[i] = (i * 17 + 3) & 0xff;
    }
    image_fd = g_file_open_tmp("nikon-mas-image-XXXXXX", &image_path, NULL);
    prefix_fd = g_file_open_tmp("nikon-mas-prefix-XXXXXX", &prefix_path, NULL);
    g_assert_cmpint(image_fd, >=, 0);
    g_assert_cmpint(prefix_fd, >=, 0);
    close(image_fd);
    close(prefix_fd);
    g_assert_true(g_file_set_contents(image_path, (char *)image,
                                     length - prefix_size, NULL));
    g_assert_true(g_file_set_contents(prefix_path, (char *)prefix,
                                     prefix_size, NULL));

    for (unsigned profile = 0; profile < 3; profile++) {
        g_autofree char *option = profile ?
            g_strdup_printf(",mas-boot-prefix=%s",
                            profile == 1 ? "erased" : prefix_path) :
            g_strdup("");
        QTestState *qts = qtest_initf("-nodefaults -machine "
            "z7ii-mas-diagnostic%s -smp 1 -bios %s -display none "
            "-serial none", option, image_path);

        memset(expected, 0, length);
        prefix_copy(qts, 0x10000000, 0xbaa00000, length);
        if (!profile) {
            /* An absent input fails before copying the known package tail. */
            g_assert_cmphex(qtest_readl(qts, 0x04251030), ==, 1);
            g_assert_cmphex(qtest_readl(qts, 0x04251010), ==, length - 1);
        } else {
            if (profile == 1) {
                memset(expected, 0xff, prefix_size);
            } else {
                memcpy(expected, prefix, prefix_size);
            }
            memcpy(expected + prefix_size, image, length - prefix_size);
            g_assert_cmphex(qtest_readl(qts, 0x04251030), ==, 8);
        }
        qtest_memread(qts, 0xbaa00000, actual, length);
        g_assert_cmpmem(actual, length, expected, length);

        if (profile) {
            /* The explicit input remains ROMD for CPU and selected-AS DMA. */
            qtest_writel(qts, 0x04264000, 0x29);
            qtest_writel(qts, 0x04264020, 0x153b0902);
            qtest_writel(qts, 0x10000000, 0x12345678);
            g_assert_cmphex(qtest_readl(qts, 0x10000000), ==,
                            (uint32_t)ldl_le_p(expected));
            qtest_writel(qts, 0x04251030, 0);
            prefix_copy(qts, 0xbaa00000, 0x10000000, 16);
            g_assert_cmphex(qtest_readl(qts, 0x04251030), ==, 1);
            g_assert_cmphex(qtest_readl(qts, 0x04251010), ==, 15);
            qtest_memread(qts, 0x10000000, actual, prefix_size);
            g_assert_cmpmem(actual, prefix_size, expected, prefix_size);
        }
        qtest_quit(qts);
    }
    unlink(image_path);
    unlink(prefix_path);
}

static void test_sysconfig_profile(void)
{
    const uint32_t base = 0x04280000;
    const unsigned profiles[][4] = {
        { 0x15, 0, 0x155, 0xa }, { 0x2a, 1, 0x2aa, 0x15 },
        { 0x3f, 2, 0x3ff, 0x1f }, { 0, 0xf, 0, 0 },
    };
    const uint32_t offsets[] = { 4, 8, 0x24, 0x28, 0x30, 0x74, 0x34, 0x38 };
    const uint32_t values[] = {
        0x12345678, 0x87654321, 1, 0x13579bdf, 0x2468ace0, 0xab,
        0xa0000000, 0x00550000,
    };
    MemoryTest cold = memory_init("");

    /* The named default is a functional input, not measured silicon ID. */
    g_assert_cmphex(qtest_readl(cold.qts, base), ==, 0);
    g_assert_cmphex(qtest_readl(cold.qts, base + 0x20), ==, 0);
    g_assert_cmphex(qtest_readl(cold.qts, base + 0x2c), ==, 0);
    memory_quit(&cold);
    for (unsigned profile = 0; profile < ARRAY_SIZE(profiles); profile++) {
        const unsigned *p = profiles[profile];
        g_autofree char *options = g_strdup_printf(
            "-global " TYPE_NIKON_SYSCONFIG ".identity=%u "
            "-global " TYPE_NIKON_SYSCONFIG ".variant=%u "
            "-global " TYPE_NIKON_SYSCONFIG ".strapwide=%u "
            "-global " TYPE_NIKON_SYSCONFIG ".strapnarrow=%u",
            p[0], p[1], p[2], p[3]);
        MemoryTest t = memory_init(options);
        uint32_t identity = p[0] | (p[1] << 28);

        for (unsigned phase = 0; phase < 2; phase++) {
            if (phase) {
                qtest_system_reset(t.qts);
            }
            g_assert_cmphex(qtest_readl(t.qts, base), ==, identity);
            g_assert_cmphex(qtest_readl(t.qts, base + 0x20), ==, p[2]);
            g_assert_cmphex(qtest_readl(t.qts, base + 0x2c), ==, p[3]);
            /* Guest writes cannot select a different variant or strap. */
            qtest_writel(t.qts, base, ~identity);
            qtest_writel(t.qts, base + 0x20, 0x3ff ^ p[2]);
            qtest_writel(t.qts, base + 0x2c, 0x1f ^ p[3]);
            qtest_writew(t.qts, base, 0xffff);
            g_assert_cmphex(qtest_readl(t.qts, base), ==, identity);
            g_assert_cmphex(qtest_readl(t.qts, base + 0x20), ==, p[2]);
            g_assert_cmphex(qtest_readl(t.qts, base + 0x2c), ==, p[3]);
            g_assert_cmphex(qtest_readl(t.qts, base + 0x34), ==, 0);
            g_assert_cmphex(qtest_readl(t.qts, base + 0x38), ==, 0);
            for (unsigned bank = 0; bank < 5; bank++) {
                g_assert_cmphex(qtest_readl(t.qts, base + 0x78 + bank * 4),
                                ==, 0);
                qtest_writel(t.qts, base + 0x78 + bank * 4,
                             0x13579bdf ^ bank);
                g_assert_cmphex(qtest_readl(t.qts, base + 0x78 + bank * 4),
                                ==, 0x13579bdf ^ bank);
            }
            for (unsigned i = 0; i < ARRAY_SIZE(offsets); i++) {
                qtest_writel(t.qts, base + offsets[i], values[i]);
                g_assert_cmphex(qtest_readl(t.qts, base + offsets[i]), ==,
                                values[i]);
            }
        }
        memory_quit(&t);
    }
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/nikon/memory/storage-profile", test_storage_profile);
    qtest_add_func("/nikon/memory/no-physical-logical-mirror",
                   test_no_bus_mirror);
    qtest_add_func("/nikon/memory/decode-retention", test_decode_retention);
    qtest_add_func("/nikon/memory/outer-holes", test_outer_holes);
    qtest_add_func("/nikon/memory/download-scratch", test_download_scratch);
    qtest_add_func("/nikon/memory/external-input", test_external_input);
    qtest_add_func("/nikon/memory/sysconfig", test_sysconfig_profile);
    return g_test_run();
}
