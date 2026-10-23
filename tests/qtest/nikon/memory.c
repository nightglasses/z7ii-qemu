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

static MemoryTest memory_init_machine(const char *machine_options,
                                      const char *options)
{
    MemoryTest t = { 0 };
    uint32_t loop = cpu_to_le32(0xeafffffe);
    int fd = g_file_open_tmp("nikon-memory-XXXXXX", &t.image, NULL);

    g_assert_cmpint(fd, >=, 0);
    g_assert_cmpint(write(fd, &loop, sizeof(loop)), ==, sizeof(loop));
    close(fd);
    t.qts = qtest_initf("-machine z7ii-mas-diagnostic%s -smp 1 -bios %s "
                        "-display none -serial none %s", machine_options,
                        t.image, options);
    return t;
}

static MemoryTest memory_init(const char *options)
{
    return memory_init_machine("", options);
}

static MemoryTest selector_one_memory_init(void)
{
    MemoryTest t = { 0 };
    const size_t image_size = 0x8a0000;
    g_autofree uint8_t *image = g_malloc(image_size);
    int fd = g_file_open_tmp("nikon-selector-one-XXXXXX", &t.image, NULL);

    g_assert_cmpint(fd, >=, 0);
    for (size_t i = 0; i < image_size; i++) {
        image[i] = (i * 29 + (i >> 8) * 11 + 7) & 0xff;
    }
    g_assert_cmpint(write(fd, image, image_size), ==, image_size);
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

static void memory_constant(GArray *code, unsigned reg, uint32_t value)
{
    uint32_t low = value & 0xffff, high = value >> 16;
    uint32_t words[] = {
        0xe3000000 | (low & 0xf000) << 4 | reg << 12 | (low & 0xfff),
        0xe3400000 | (high & 0xf000) << 4 | reg << 12 | (high & 0xfff),
    };

    g_array_append_vals(code, words, ARRAY_SIZE(words));
}

static void test_cpu_ram(void)
{
    /*
     * Partial pages, interior pages and exact last words of every RAM slot.
     * BED661C8 is the original GUI's failing physical access.
     */
    static const uint32_t addresses[] = {
        0xb1800000, 0xb5effffc, 0xb5f00000, 0xb7cffffc,
        0xb7d00000, 0xba9ffffc, 0xbaa00000, 0xbb2ffffc,
        0xbb300000, 0xbc0ffffc, 0xbc100000, 0xbd1ffffc,
        0xbec5f200, 0xbec5f400, 0xbed661c8, 0xbf10f1fc,
        0xbf10f200, 0xbf10f400, 0xbf3095fc,
        0xbf309600, 0xbf309800, 0xbf7b95fc,
        0xc175d600, 0xc175d800, 0xc1c0d5fc,
        0xcb4a7000, 0xcc63affc, 0x62059800, 0x62059c00, 0x622217fc,
        0x50300000, 0x5063fffc, 0xb0d00000, 0xb10ffffc,
        0xb0900000, 0xb0cffffc, 0x04008ea8, 0x04008efc, 0x04008ffc,
    };
    const uint32_t observations = 0xb1801000;
    const uint32_t done = observations + 8 * ARRAY_SIZE(addresses);
    g_autoptr(GArray) code = g_array_new(false, false, sizeof(uint32_t));
    g_autofree uint32_t *little = NULL;
    MemoryTest t = { 0 };
    int fd;

    memory_constant(code, 3, observations);
    for (unsigned i = 0; i < ARRAY_SIZE(addresses); i++) {
        uint32_t words[] = {
            0xe5902000,                       /* ldr r2, [r0] */
            0xe5832000 | (i * 8),             /* record bus-produced value */
            0xe5801000,                       /* str r1, [r0] */
            0xe5902000,                       /* ldr r2, [r0] */
            0xe5832000 | (i * 8 + 4),         /* record CPU-produced value */
        };

        memory_constant(code, 0, addresses[i]);
        memory_constant(code, 1, 0x5a000000 + i);
        g_array_append_vals(code, words, ARRAY_SIZE(words));
    }
    memory_constant(code, 0, done);
    {
        const uint32_t finish[] = { 0xe3a01001, 0xe5801000, 0xeafffffe };

        g_array_append_vals(code, finish, ARRAY_SIZE(finish));
    }
    little = g_new(uint32_t, code->len);
    for (unsigned i = 0; i < code->len; i++) {
        little[i] = cpu_to_le32(g_array_index(code, uint32_t, i));
    }
    fd = g_file_open_tmp("nikon-memory-cpu-XXXXXX", &t.image, NULL);
    g_assert_cmpint(fd, >=, 0);
    g_assert_cmpint(write(fd, little, code->len * 4), ==, code->len * 4);
    close(fd);
    t.qts = qtest_initf("-machine z7ii-mas-diagnostic -smp 1 -bios %s "
                        "-display none -serial none -accel tcg,thread=single -S",
                        t.image);
    for (unsigned phase = 0; phase < 2; phase++) {
        int64_t deadline;

        if (phase) {
            qtest_system_reset(t.qts);
        }
        qtest_writel(t.qts, done, 0);
        for (unsigned i = 0; i < ARRAY_SIZE(addresses); i++) {
            qtest_writel(t.qts, addresses[i], 0xa5000000 + phase * 0x100 + i);
        }
        qtest_qmp_assert_success(t.qts, "{'execute':'cont'}");
        deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
        while (!qtest_readl(t.qts, done)) {
            g_assert_cmpint(g_get_monotonic_time(), <, deadline);
            g_usleep(1000);
        }
        qtest_qmp_assert_success(t.qts, "{'execute':'stop'}");
        for (unsigned i = 0; i < ARRAY_SIZE(addresses); i++) {
            g_assert_cmphex(qtest_readl(t.qts, observations + i * 8), ==,
                            0xa5000000 + phase * 0x100 + i);
            g_assert_cmphex(qtest_readl(t.qts, observations + i * 8 + 4), ==,
                            0x5a000000 + i);
            g_assert_cmphex(qtest_readl(t.qts, addresses[i]), ==, 0x5a000000 + i);
        }
    }
    memory_quit(&t);
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

static void test_kind40_buffer(void)
{
    const uint64_t base = 0x62059800, end = 0x62221800;
    const uint64_t addresses[] = { base, base + 0xe4000, end - 4 };
    MemoryTest t = memory_init("");

    for (unsigned i = 0; i < ARRAY_SIZE(addresses); i++) {
        g_assert_cmphex(qtest_readl(t.qts, addresses[i]), ==, 0);
        qtest_writel(t.qts, addresses[i], 0x40280000 + i);
    }
    /* The logical view requires the guest MMU; adjacent bytes stay holes. */
    qtest_writel(t.qts, 0x36d59800, 0x12345678);
    qtest_writel(t.qts, base - 4, 0x12345678);
    qtest_writel(t.qts, end, 0x12345678);
    g_assert_cmphex(qtest_readl(t.qts, 0x36d59800), ==, 0);
    g_assert_cmphex(qtest_readl(t.qts, base - 4), ==, 0);
    g_assert_cmphex(qtest_readl(t.qts, end), ==, 0);
    for (unsigned phase = 0; phase < 2; phase++) {
        if (phase) {
            qtest_system_reset(t.qts);
        }
        for (unsigned i = 0; i < ARRAY_SIZE(addresses); i++) {
            g_assert_cmphex(qtest_readl(t.qts, addresses[i]), ==,
                            0x40280000 + i);
        }
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

static void test_row_storage(void)
{
    const uint32_t cells[] = {
        0x50300000, 0x5055fffc, 0x50560000, 0x5063fffc,
    };
    MemoryTest t = memory_init("");

    for (unsigned i = 0; i < ARRAY_SIZE(cells); i++) {
        g_assert_cmphex(qtest_readl(t.qts, cells[i]), ==, 0);
        qtest_writel(t.qts, cells[i], 0x120b0000 + i);
    }
    qtest_writel(t.qts, 0x502ffffc, 0x12345678);
    qtest_writel(t.qts, 0x50640000, 0x12345678);
    qtest_writel(t.qts, 0x30260000, 0x12345678);
    g_assert_cmphex(qtest_readl(t.qts, 0x502ffffc), ==, 0);
    g_assert_cmphex(qtest_readl(t.qts, 0x50640000), ==, 0);
    g_assert_cmphex(qtest_readl(t.qts, 0x30260000), ==, 0);
    for (unsigned phase = 0; phase < 2; phase++) {
        if (phase) {
            qtest_system_reset(t.qts);
        }
        for (unsigned i = 0; i < ARRAY_SIZE(cells); i++) {
            g_assert_cmphex(qtest_readl(t.qts, cells[i]), ==, 0x120b0000 + i);
        }
    }
    memory_quit(&t);
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

static void test_display_buffers(void)
{
    static const uint64_t bases[] = {
        0xcb4a7000, 0xcb82b000, 0xcbbaf000,
        0xcbf33000, 0xcc18b000, 0xcc3e3000,
    };
    const uint64_t end = 0xcc63b000;
    MemoryTest t = memory_init("");
    unsigned i;

    for (i = 0; i < ARRAY_SIZE(bases); i++) {
        g_assert_cmphex(qtest_readl(t.qts, bases[i]), ==, 0);
        qtest_writel(t.qts, bases[i], 0xd1500000 + i);
    }
    qtest_writel(t.qts, end - 4, 0xd15fffff);

    /* Adjacent addresses remain uncovered; the arena does not grant a broad
     * C0000000-class RAM aperture. */
    qtest_writel(t.qts, bases[0] - 4, 0x12345678);
    qtest_writel(t.qts, end, 0x87654321);
    g_assert_cmphex(qtest_readl(t.qts, bases[0] - 4), ==, 0);
    g_assert_cmphex(qtest_readl(t.qts, end), ==, 0);

    for (unsigned phase = 0; phase < 2; phase++) {
        if (phase) {
            qtest_system_reset(t.qts);
        }
        for (i = 0; i < ARRAY_SIZE(bases); i++) {
            g_assert_cmphex(qtest_readl(t.qts, bases[i]), ==,
                            0xd1500000 + i);
        }
        g_assert_cmphex(qtest_readl(t.qts, end - 4), ==, 0xd15fffff);
    }
    memory_quit(&t);
}

static void test_lcd_composition(void)
{
    const uint64_t base = 0xbf309600;
    const uint64_t draw_end = 0xbf5b9600;
    const uint64_t end = 0xbf7b9600;
    MemoryTest t = memory_init("");

    g_assert_cmphex(qtest_readl(t.qts, base), ==, 0);
    g_assert_cmphex(qtest_readl(t.qts, draw_end), ==, 0);
    g_assert_cmphex(qtest_readl(t.qts, end - 4), ==, 0);
    qtest_writel(t.qts, base, 0x1cd00000);
    qtest_writel(t.qts, draw_end, 0x1cd00001);
    qtest_writel(t.qts, end - 4, 0x1cd00002);

    /* Record1 ends at index5's exact base; the index5 right edge is a hole. */
    qtest_writel(t.qts, base - 4, 0x12345678);
    qtest_writel(t.qts, end, 0x87654321);
    g_assert_cmphex(qtest_readl(t.qts, base - 4), ==, 0x12345678);
    g_assert_cmphex(qtest_readl(t.qts, end), ==, 0);

    qtest_system_reset(t.qts);
    g_assert_cmphex(qtest_readl(t.qts, base), ==, 0x1cd00000);
    g_assert_cmphex(qtest_readl(t.qts, draw_end), ==, 0x1cd00001);
    g_assert_cmphex(qtest_readl(t.qts, end - 4), ==, 0x1cd00002);
    g_assert_cmphex(qtest_readl(t.qts, base - 4), ==, 0x12345678);
    memory_quit(&t);
}

static void test_resource57_record0(void)
{
    const uint64_t base = 0xbec5f200;
    const uint64_t fill_end = 0xbedb0a00;
    const uint64_t end = 0xbf10f200;
    MemoryTest t = memory_init("");

    g_assert_cmphex(qtest_readl(t.qts, base), ==, 0);
    g_assert_cmphex(qtest_readl(t.qts, fill_end), ==, 0);
    g_assert_cmphex(qtest_readl(t.qts, end - 4), ==, 0);
    qtest_writel(t.qts, base, 0x57000000);
    qtest_writel(t.qts, fill_end, 0x57000001);
    qtest_writel(t.qts, end - 4, 0x57000002);

    /* The left edge is exact; record1 begins at the record0 right edge. */
    qtest_writel(t.qts, base - 4, 0x12345678);
    qtest_writel(t.qts, end, 0x87654321);
    g_assert_cmphex(qtest_readl(t.qts, base - 4), ==, 0);
    g_assert_cmphex(qtest_readl(t.qts, end), ==, 0x87654321);

    qtest_system_reset(t.qts);
    g_assert_cmphex(qtest_readl(t.qts, base), ==, 0x57000000);
    g_assert_cmphex(qtest_readl(t.qts, fill_end), ==, 0x57000001);
    g_assert_cmphex(qtest_readl(t.qts, end - 4), ==, 0x57000002);
    g_assert_cmphex(qtest_readl(t.qts, end), ==, 0x87654321);
    memory_quit(&t);
}

static void test_resource57_record1(void)
{
    const uint64_t base = 0xbf10f200;
    const uint64_t end = 0xbf309600;
    MemoryTest t = memory_init("");

    g_assert_cmphex(qtest_readl(t.qts, base), ==, 0);
    g_assert_cmphex(qtest_readl(t.qts, end - 4), ==, 0);
    qtest_writel(t.qts, base - 4, 0x5700ffff);
    qtest_writel(t.qts, base, 0x57010000);
    qtest_writel(t.qts, end - 4, 0x5701ffff);
    qtest_writel(t.qts, end, 0x57050000);

    /* Record0 and record5 are mapped neighbors, not record1 padding. */
    g_assert_cmphex(qtest_readl(t.qts, base - 4), ==, 0x5700ffff);
    g_assert_cmphex(qtest_readl(t.qts, end), ==, 0x57050000);

    qtest_system_reset(t.qts);
    g_assert_cmphex(qtest_readl(t.qts, base - 4), ==, 0x5700ffff);
    g_assert_cmphex(qtest_readl(t.qts, base), ==, 0x57010000);
    g_assert_cmphex(qtest_readl(t.qts, end - 4), ==, 0x5701ffff);
    g_assert_cmphex(qtest_readl(t.qts, end), ==, 0x57050000);
    memory_quit(&t);
}

static void test_resource57_record7(void)
{
    const uint64_t base = 0xc175d600;
    const uint64_t fill_end = 0xc18aee00;
    const uint64_t end = 0xc1c0d600;
    MemoryTest t = memory_init("");

    g_assert_cmphex(qtest_readl(t.qts, base), ==, 0);
    g_assert_cmphex(qtest_readl(t.qts, fill_end), ==, 0);
    g_assert_cmphex(qtest_readl(t.qts, end - 4), ==, 0);
    qtest_writel(t.qts, base, 0x57070000);
    qtest_writel(t.qts, fill_end, 0x57070001);
    qtest_writel(t.qts, end - 4, 0x57070002);

    /* Record7 is exact; record8 begins at its end and remains a hole. */
    qtest_writel(t.qts, base - 4, 0x12345678);
    qtest_writel(t.qts, end, 0x87654321);
    g_assert_cmphex(qtest_readl(t.qts, base - 4), ==, 0);
    g_assert_cmphex(qtest_readl(t.qts, end), ==, 0);

    qtest_system_reset(t.qts);
    g_assert_cmphex(qtest_readl(t.qts, base), ==, 0x57070000);
    g_assert_cmphex(qtest_readl(t.qts, fill_end), ==, 0x57070001);
    g_assert_cmphex(qtest_readl(t.qts, end - 4), ==, 0x57070002);
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

static void G_GNUC_UNUSED test_dynamic_load(void)
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

static void test_selector_one_download(void)
{
    static const struct {
        uint32_t source;
        uint32_t destination;
        uint32_t length;
    } transfers[] = {
        { 0x107e00f4, 0xb0905000, 0x34238 },
        { 0x1081432c, 0xb093a000, 0x33300 },
        { 0x1087a92c, 0xb096e000, 0x2cbc8 },
    };
    MemoryTest t = selector_one_memory_init();

    /* The selector-one producer uses one fixed four-MiB arena and partitions
     * it into these three page-aligned destinations. The source bytes remain
     * the guest ROM image; QEMU does not seed or synthesize their contents. */
    for (unsigned i = 0; i < ARRAY_SIZE(transfers); i++) {
        const uint32_t source = transfers[i].source;
        const uint32_t destination = transfers[i].destination;
        const uint32_t length = transfers[i].length;
        g_autofree uint8_t *expected = g_malloc(length);
        g_autofree uint8_t *actual = g_malloc(length);

        qtest_memread(t.qts, source, expected, length);
        qtest_memread(t.qts, destination, actual, length);
        for (uint32_t j = 0; j < length; j++) {
            g_assert_cmpuint(actual[j], ==, 0);
        }
        qtest_writel(t.qts, 0x04251030, 0);
        prefix_copy(t.qts, source, destination, length);
        g_assert_cmphex(qtest_readl(t.qts, 0x04251030), ==, 8);
        qtest_memread(t.qts, destination, actual, length);
        g_assert_cmpmem(actual, length, expected, length);
    }

    /* Exact arena bound; the adjacent selector-zero arena remains a separate
     * owner at the immediately following address. */
    qtest_writel(t.qts, 0xb0cffffc, 0x5a5aa5a5);
    g_assert_cmphex(qtest_readl(t.qts, 0xb0cffffc), ==, 0x5a5aa5a5);
    qtest_writel(t.qts, 0xb0d00000, 0x0a0b0c0d);
    g_assert_cmphex(qtest_readl(t.qts, 0xb0d00000), ==, 0x0a0b0c0d);
    g_assert_cmphex(qtest_readl(t.qts, 0xb0cffffc), ==, 0x5a5aa5a5);
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

static void nor_unlock(QTestState *qts, uint16_t command)
{
    qtest_writew(qts, 0x10000aaa, 0xaa);
    qtest_writew(qts, 0x10000554, 0x55);
    qtest_writew(qts, 0x10000aaa, command);
}

static void test_nor_protocol(void)
{
    MemoryTest t = memory_init_machine(
        ",mas-boot-prefix=erased", "");
    QTestState *qts = t.qts;

    qtest_writel(qts, 0x04264000, 0x21);
    qtest_writel(qts, 0x04264020, 0x153b0902);

    nor_unlock(qts, 0x90);
    g_assert_cmphex(qtest_readw(qts, 0x10000000), ==, 0xffff);
    g_assert_cmphex(qtest_readw(qts, 0x10000002), ==, 0xffff);
    g_assert_cmphex(qtest_readw(qts, 0x1000001c), ==, 0xffff);
    g_assert_cmphex(qtest_readw(qts, 0x1000001e), ==, 0xffff);
    qtest_writew(qts, 0x10000aaa, 0xf0);

    qtest_writew(qts, 0x100000aa, 0x98);
    g_assert_cmphex(qtest_readw(qts, 0x1000004e), ==, 26);
    g_assert_cmphex(qtest_readw(qts, 0x10000054), ==, 7);
    g_assert_cmphex(qtest_readw(qts, 0x1000005e), ==, 0);
    g_assert_cmphex(qtest_readw(qts, 0x10000060), ==, 2);
    qtest_writew(qts, 0x10000aaa, 0xf0);

    nor_unlock(qts, 0xa0);
    qtest_writew(qts, 0x12960000, 0x55aa);
    g_assert_cmphex(qtest_readw(qts, 0x12960000), ==, 0x55aa);
    nor_unlock(qts, 0xa0);
    qtest_writew(qts, 0x12960000, 0xf0f0);
    g_assert_cmphex(qtest_readw(qts, 0x12960000), ==, 0x50a0);

    nor_unlock(qts, 0x80);
    qtest_writew(qts, 0x10000aaa, 0xaa);
    qtest_writew(qts, 0x10000554, 0x55);
    qtest_writew(qts, 0x12960000, 0x30);
    g_assert_cmphex(qtest_readw(qts, 0x12960000), ==, 0xffff);

    qtest_writew(qts, 0x10000aaa, 0xaa);
    qtest_writew(qts, 0x10000554, 0x55);
    qtest_writew(qts, 0x12960000, 0x25);
    qtest_writew(qts, 0x12960000, 3);
    for (unsigned i = 0; i < 4; i++) {
        qtest_writew(qts, 0x12960020 + i * 2, 0xa500 + i);
    }
    qtest_writew(qts, 0x12960000, 0x29);
    for (unsigned i = 0; i < 4; i++) {
        g_assert_cmphex(qtest_readw(qts, 0x12960020 + i * 2), ==,
                        0xa500 + i);
    }

    nor_unlock(qts, 0xa0);
    qtest_writew(qts, 0x12960000, 0x1234);
    qtest_system_reset(qts);
    g_assert_cmphex(qtest_readw(qts, 0x12960000), ==, 0x1234);
    memory_quit(&t);
}

static void test_descriptor17_input(void)
{
    const size_t size = 0x20000;
    g_autofree uint8_t *input = g_malloc(size);
    g_autofree char *path = NULL;
    g_autofree char *machine = NULL;
    MemoryTest t;
    int fd;

    for (size_t i = 0; i < size; i++) {
        input[i] = (i * 23 + (i >> 8) * 7 + 5) & 0xff;
    }
    memcpy(input + size - 4, "ENDM", 4);
    fd = g_file_open_tmp("nikon-descriptor17-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    g_assert_true(g_file_set_contents(path, (char *)input, size, NULL));
    machine = g_strdup_printf(
            ",mas-boot-prefix=erased,"
        "descriptor17-input=%s", path);
    t = memory_init_machine(machine, "");

    qtest_writel(t.qts, 0x04264000, 0x21);
    qtest_writel(t.qts, 0x04264020, 0x153b0902);
    g_assert_cmphex(qtest_readw(t.qts, 0x132c0000), ==,
                    lduw_le_p(input));
    g_assert_cmphex(qtest_readw(t.qts, 0x132caa00), ==,
                    lduw_le_p(input + 0xaa00));
    g_assert_cmphex(qtest_readw(t.qts, 0x132dfffc), ==,
                    lduw_le_p(input + size - 4));
    g_assert_cmphex(qtest_readw(t.qts, 0x132dfffe), ==,
                    lduw_le_p(input + size - 2));
    g_assert_cmphex(qtest_readw(t.qts, 0x132e0000), ==, 0xffff);

    nor_unlock(t.qts, 0xa0);
    qtest_writew(t.qts, 0x132c0000, 0x0000);
    qtest_system_reset(t.qts);
    g_assert_cmphex(qtest_readw(t.qts, 0x132c0000), ==, 0);
    g_assert_cmphex(qtest_readw(t.qts, 0x132caa00), ==,
                    lduw_le_p(input + 0xaa00));

    memory_quit(&t);
    unlink(path);
}

static void test_row_input(gconstpointer opaque)
{
    const unsigned id = GPOINTER_TO_UINT(opaque);
    const uint32_t base = id == 11 ? 0x12a80000 : 0x12ce0000;
    const size_t size = id == 11 ? 0x260000 : 0xe0000;
    g_autofree uint8_t *input = g_malloc(size);
    g_autofree char *path = NULL;
    g_autofree char *machine = NULL;
    MemoryTest t;
    int fd;

    for (size_t i = 0; i < size; i++) {
        input[i] = (i * 13 + (i >> 12) * 19 + id) & 0xff;
    }
    fd = g_file_open_tmp("nikon-row-input-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    g_assert_true(g_file_set_contents(path, (char *)input, size, NULL));
    machine = g_strdup_printf(",mas-boot-prefix=erased,descriptor%u-input=%s",
                              id, path);
    t = memory_init_machine(machine, "");
    qtest_writel(t.qts, 0x04264000, 0x21);
    qtest_writel(t.qts, 0x04264020, 0x153b0902);
    for (unsigned i = 0; i < 3; i++) {
        size_t offset = i == 0 ? 0 : i == 1 ? size / 2 : size - 2;

        g_assert_cmphex(qtest_readw(t.qts, base + offset), ==,
                        lduw_le_p(input + offset));
    }
    g_assert_cmphex(qtest_readw(t.qts, base - 2), ==, 0xffff);
    g_assert_cmphex(qtest_readw(t.qts, base + size), ==, 0xffff);
    g_assert_cmphex(qtest_readw(t.qts, base + 0x3e0000), ==, 0xffff);
    /* File input populates NOR only, never its destination RAM or an event. */
    g_assert_cmphex(qtest_readl(t.qts, id == 11 ? 0x50300000 : 0x50560000),
                    ==, 0);
    nor_unlock(t.qts, 0xa0);
    qtest_writew(t.qts, base, 0);
    qtest_system_reset(t.qts);
    g_assert_cmphex(qtest_readw(t.qts, base), ==, 0);
    g_assert_cmphex(qtest_readw(t.qts, base + size - 2), ==,
                    lduw_le_p(input + size - 2));
    memory_quit(&t);
    unlink(path);
}

static void test_row_input_rejection(gconstpointer opaque)
{
    const unsigned id = GPOINTER_TO_UINT(opaque);
    const size_t size = id == 11 ? 0x260000 : 0xe0000;
    g_autofree uint8_t *input = g_malloc0(size + 1);
    g_autofree char *path = NULL;
    g_autofree char *expected = NULL;
    int fd;

    if (g_test_subprocess()) {
        g_autofree char *machine = g_strdup_printf(",descriptor%u-input=%s",
            id, g_getenv("NIKON_ROW_FIXTURE"));
        MemoryTest t = memory_init_machine(machine, "");

        memory_quit(&t);
        return;
    }
    fd = g_file_open_tmp("nikon-row-invalid-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    g_setenv("NIKON_ROW_FIXTURE", path, true);
    expected = g_strdup_printf(
        "*descriptor%u input must contain exactly %zu KiB*", id, size / 1024);
    for (unsigned i = 0; i < 2; i++) {
        g_assert_true(g_file_set_contents(path, (char *)input,
                                          i ? size + 1 : size - 1, NULL));
        g_test_trap_subprocess(NULL, 5 * G_TIME_SPAN_SECOND, 0);
        g_test_trap_assert_failed();
        g_test_trap_assert_stderr(expected);
    }
    unlink(path);
    g_test_trap_subprocess(NULL, 5 * G_TIME_SPAN_SECOND, 0);
    g_test_trap_assert_failed();
    g_test_trap_assert_stderr("*cannot load descriptor*input*");
    g_unsetenv("NIKON_ROW_FIXTURE");
}

static void test_descriptor17_rejection(void)
{
    if (g_test_subprocess()) {
        g_autofree char *machine = g_strdup_printf(
            ",descriptor17-input=%s",
            g_getenv("NIKON_DESCRIPTOR17_FIXTURE"));
        MemoryTest t = memory_init_machine(machine, "");

        memory_quit(&t);
        return;
    }
    {
        const uint32_t short_input = 0x4d444e45;
        g_autofree char *path = NULL;
        int fd = g_file_open_tmp("nikon-descriptor17-short-XXXXXX", &path,
                                 NULL);

        g_assert_cmpint(fd, >=, 0);
        g_assert_cmpint(write(fd, &short_input, sizeof(short_input)), ==,
                        sizeof(short_input));
        close(fd);
        g_setenv("NIKON_DESCRIPTOR17_FIXTURE", path, true);
        g_test_trap_subprocess(NULL, 5 * G_TIME_SPAN_SECOND, 0);
        g_test_trap_assert_failed();
        g_test_trap_assert_stderr(
            "*descriptor17 input must contain exactly 128 KiB*");
        g_unsetenv("NIKON_DESCRIPTOR17_FIXTURE");
        unlink(path);
    }
}

static void test_descriptor18_input(void)
{
    const size_t size = 0xa0000;
    g_autofree uint8_t *input = g_malloc(size);
    g_autofree char *path17 = NULL;
    g_autofree char *path18 = NULL;
    g_autofree char *machine = NULL;
    MemoryTest t;
    int fd;

    for (size_t i = 0; i < size; i++) {
        input[i] = (i * 31 + (i >> 9) * 13 + 9) & 0xff;
    }
    memcpy(input + size - 4, "ENDM", 4);
    fd = g_file_open_tmp("nikon-descriptor18-XXXXXX", &path18, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    g_assert_true(g_file_set_contents(path18, (char *)input, size, NULL));
    fd = g_file_open_tmp("nikon-descriptor17-pair-XXXXXX", &path17, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    g_assert_true(g_file_set_contents(path17, (char *)input, 0x20000, NULL));
    machine = g_strdup_printf(",mas-boot-prefix=erased,"
                              "descriptor17-input=%s,descriptor18-input=%s",
                              path17, path18);
    t = memory_init_machine(machine, "");
    qtest_writel(t.qts, 0x04264000, 0x21);
    qtest_writel(t.qts, 0x04264020, 0x153b0902);
    g_assert_cmphex(qtest_readw(t.qts, 0x132dfffe), ==,
                    lduw_le_p(input + 0x1fffe));
    g_assert_cmphex(qtest_readw(t.qts, 0x132e0000), ==, lduw_le_p(input));
    g_assert_cmphex(qtest_readw(t.qts, 0x132e0100), ==,
                    lduw_le_p(input + 0x100));
    g_assert_cmphex(qtest_readw(t.qts, 0x13370000), ==,
                    lduw_le_p(input + 0x90000));
    g_assert_cmphex(qtest_readw(t.qts, 0x1337fffc), ==,
                    lduw_le_p(input + size - 4));
    g_assert_cmphex(qtest_readw(t.qts, 0x1337fffe), ==,
                    lduw_le_p(input + size - 2));
    g_assert_cmphex(qtest_readw(t.qts, 0x133a0000), ==, 0xffff);
    nor_unlock(t.qts, 0xa0);
    qtest_writew(t.qts, 0x132e0000, 0);
    qtest_system_reset(t.qts);
    g_assert_cmphex(qtest_readw(t.qts, 0x132e0000), ==, 0);
    g_assert_cmphex(qtest_readw(t.qts, 0x132e0100), ==,
                    lduw_le_p(input + 0x100));
    g_assert_cmphex(qtest_readw(t.qts, 0x132c0000), ==, lduw_le_p(input));
    memory_quit(&t);
    unlink(path17);
    unlink(path18);
}

static void test_descriptor18_rejection(void)
{
    g_autofree char *path = NULL;
    g_autofree uint8_t *input = g_malloc0(0xa0001);
    int fd;

    if (g_test_subprocess()) {
        g_autofree char *machine = g_strdup_printf(
            ",descriptor18-input=%s",
            g_getenv("NIKON_DESCRIPTOR18_FIXTURE"));
        MemoryTest t = memory_init_machine(machine, "");

        memory_quit(&t);
        return;
    }
    fd = g_file_open_tmp("nikon-descriptor18-invalid-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    g_setenv("NIKON_DESCRIPTOR18_FIXTURE", path, true);
    for (unsigned i = 0; i < 2; i++) {
        g_assert_true(g_file_set_contents(path, (char *)input,
                                          i ? 0xa0001 : 4, NULL));
        g_test_trap_subprocess(NULL, 5 * G_TIME_SPAN_SECOND, 0);
        g_test_trap_assert_failed();
        g_test_trap_assert_stderr(
            "*descriptor18 input must contain exactly 640 KiB*");
    }
    unlink(path);
    g_test_trap_subprocess(NULL, 5 * G_TIME_SPAN_SECOND, 0);
    g_test_trap_assert_failed();
    g_test_trap_assert_stderr("*cannot load descriptor18 input*");
    g_unsetenv("NIKON_DESCRIPTOR18_FIXTURE");
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

static void test_sysconfig_controls(void)
{
    const uint32_t base = 0x04280000;
    const unsigned offsets[] = { 0x64, 0x68, 0x6c };
    MemoryTest t = memory_init("");

    for (unsigned phase = 0; phase < 2; phase++) {
        if (phase) {
            qtest_system_reset(t.qts);
        }
        for (unsigned i = 0; i < ARRAY_SIZE(offsets); i++) {
            uint32_t addr = base + offsets[i];

            g_assert_cmphex(qtest_readl(t.qts, addr), ==, 0);
            for (unsigned bit = 0; bit < 32; bit++) {
                uint32_t value = 1U << bit;

                qtest_writel(t.qts, addr, value);
                g_assert_cmphex(qtest_readl(t.qts, addr), ==, value);
            }
            qtest_writel(t.qts, addr, 0xffffffff);
            g_assert_cmphex(qtest_readl(t.qts, addr), ==, 0xffffffff);
            qtest_writeb(t.qts, addr, 0);
            qtest_writew(t.qts, addr, 0);
            qtest_writel(t.qts, addr + 1, 0);
            g_assert_cmphex(qtest_readl(t.qts, addr), ==, 0xffffffff);
        }
        /* Whole-word replacement of independent groups, without strobes. */
        qtest_writel(t.qts, base + 0x64, 0x10);
        qtest_writel(t.qts, base + 0x68, 0xa55a5aa5);
        qtest_writel(t.qts, base + 0x6c, 0xffff0);
        g_assert_cmphex(qtest_readl(t.qts, base + 0x64), ==, 0x10);
        g_assert_cmphex(qtest_readl(t.qts, base + 0x6c), ==, 0xffff0);
        qtest_writel(t.qts, base + 0x64, 0x3c0);
        qtest_writel(t.qts, base + 0x6c, 0x1e000000);
        g_assert_cmphex(qtest_readl(t.qts, base + 0x64), ==, 0x3c0);
        g_assert_cmphex(qtest_readl(t.qts, base + 0x6c), ==, 0x1e000000);
        for (unsigned hole = 0x60; hole <= 0x70; hole += 0x10) {
            qtest_writel(t.qts, base + hole, 0xffffffff);
            g_assert_cmphex(qtest_readl(t.qts, base + hole), ==, 0);
        }
        g_assert_cmphex(qtest_readl(t.qts, base + 0x64), ==, 0x3c0);
        g_assert_cmphex(qtest_readl(t.qts, base + 0x68), ==, 0xa55a5aa5);
        g_assert_cmphex(qtest_readl(t.qts, base + 0x6c), ==, 0x1e000000);
    }
    memory_quit(&t);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/nikon/memory/storage-profile", test_storage_profile);
    qtest_add_func("/nikon/memory/cpu-ram", test_cpu_ram);
    qtest_add_func("/nikon/memory/kind40-buffer", test_kind40_buffer);
    qtest_add_func("/nikon/memory/row-storage", test_row_storage);
    qtest_add_data_func("/nikon/memory/descriptor11-input",
                        GUINT_TO_POINTER(11), test_row_input);
    qtest_add_data_func("/nikon/memory/descriptor12-input",
                        GUINT_TO_POINTER(12), test_row_input);
    qtest_add_data_func("/nikon/memory/descriptor11-rejection",
                        GUINT_TO_POINTER(11), test_row_input_rejection);
    qtest_add_data_func("/nikon/memory/descriptor12-rejection",
                        GUINT_TO_POINTER(12), test_row_input_rejection);
    qtest_add_func("/nikon/memory/no-physical-logical-mirror",
                   test_no_bus_mirror);
    qtest_add_func("/nikon/memory/decode-retention", test_decode_retention);
    qtest_add_func("/nikon/memory/outer-holes", test_outer_holes);
    qtest_add_func("/nikon/memory/display-buffers", test_display_buffers);
    qtest_add_func("/nikon/memory/lcd-composition", test_lcd_composition);
    qtest_add_func("/nikon/memory/resource57-record0",
                   test_resource57_record0);
    qtest_add_func("/nikon/memory/resource57-record1",
                   test_resource57_record1);
    qtest_add_func("/nikon/memory/resource57-record7",
                   test_resource57_record7);
    qtest_add_func("/nikon/memory/download-scratch", test_download_scratch);
    qtest_add_func("/nikon/memory/selector-one-download",
                   test_selector_one_download);
    qtest_add_func("/nikon/memory/external-input", test_external_input);
    qtest_add_func("/nikon/memory/nor-protocol", test_nor_protocol);
    qtest_add_func("/nikon/memory/descriptor17-input",
                   test_descriptor17_input);
    qtest_add_func("/nikon/memory/descriptor17-rejection",
                   test_descriptor17_rejection);
    qtest_add_func("/nikon/memory/descriptor18-input", test_descriptor18_input);
    qtest_add_func("/nikon/memory/descriptor18-rejection",
                   test_descriptor18_rejection);
    qtest_add_func("/nikon/memory/sysconfig", test_sysconfig_profile);
    qtest_add_func("/nikon/memory/sysconfig-controls", test_sysconfig_controls);
    return g_test_run();
}
