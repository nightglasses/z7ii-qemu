/* Synthetic NOR input declarations and independent source arithmetic.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "../../../tools/nikon/module.h"

static char *tool;

static void test_layout(void)
{
    g_autoptr(GBytes) bytes = nikon_module_cold_neutral();
    gsize size;
    const uint8_t *data = g_bytes_get_data(bytes, &size);

    g_assert_cmpuint(size, ==, 0x180000);
    g_assert_cmpmem(data + 0x17fffc, 4, "ENDM", 4);
    g_assert_cmphex(data[0xbe014], ==, 0x15);
    g_assert_cmphex(data[0xbe015], ==, 0);
}

static void test_readsets(void)
{
    const struct {
        size_t offset;
        size_t size;
    } zero[] = {
        { 0x20, 1 }, { 0x23, 1 },
        { 0x4e40, 64 }, { 0x64c0, 128 }, { 0x6540, 128 },
        { 0x2c00, 141 }, { 0x3230, 141 },
        { 0xbf080, 1 }, { 0xbf180, 2 },
        { 0xbf280, 1 }, { 0xbf2b0, 2 },
        { 0xc04b0, 1 }, { 0xc13b0, 2 },
        { 0xcda20, 1 }, { 0xcdae0, 2 },
    };
    g_autoptr(GBytes) bytes = nikon_module_cold_neutral();
    const uint8_t *data = g_bytes_get_data(bytes, NULL);

    for (unsigned i = 0; i < ARRAY_SIZE(zero); i++) {
        for (size_t j = 0; j < zero[i].size; j++) {
            g_assert_cmphex(data[zero[i].offset + j], ==, 0);
        }
    }
    for (unsigned entry = 0; entry < 2; entry++) {
        uint32_t control = 0;

        /* 10300660 expands fourteen u16 fields; its consumer shifts 13..0. */
        for (unsigned i = 0; i < 14; i++) {
            uint32_t field = lduw_le_p(data + 0x64c0 + entry * 128 + i * 2);

            control |= field << (13 - i);
        }
        g_assert_cmphex(control, ==, 0);
    }
    g_assert_cmphex(data[0xc04b7] & 0xe0, ==, 0);
}

static void test_timing(void)
{
    g_autoptr(GBytes) bytes = nikon_module_cold_neutral();
    const uint8_t *row = (const uint8_t *)g_bytes_get_data(bytes, NULL) +
                         0xbe2b0;
    uint32_t x = lduw_le_p(row + 2);
    uint32_t fallback = lduw_le_p(row + 6);
    uint32_t overhead = lduw_le_p(row + 12);
    uint32_t divisor, basecount, quantized, q;

    g_assert_cmpuint(x, ==, 4096);
    g_assert_cmpuint(fallback, ==, 35190);
    g_assert_cmpuint(overhead, ==, 72);
    g_assert_cmpuint(row[11], ==, 1);
    /* These are fixed valid declarations, not a parser for arbitrary rows. */
    g_assert_cmpuint(x, >, 0);
    g_assert_cmpuint((uint64_t)72000 * x + 71999, <=, UINT32_MAX);
    divisor = (72000 * x + 71999) / 72000; /* 103066be */
    basecount = (144144000 / divisor) & ~1u;
    quantized = 10 * ((1000 * overhead / 72 + 5) / 10);
    g_assert_cmpuint(divisor, ==, 4096);
    g_assert_cmpuint(basecount, ==, 35190);
    g_assert_cmpuint(fallback, ==, basecount);
    g_assert_cmpuint(quantized, ==, 1000);
    /* Selector3's 102ffc5a uses minus-one hardware loads, without q. */
    g_assert_cmpuint(divisor - 1, ==, 4095);
    g_assert_cmpuint(basecount - 1, ==, 35189);

    /* Independent selector4/102ffb14 example: duration is guest state,
     * not a module field or a value promised for every execution. */
    g_assert_cmpuint((uint64_t)72 * 7383 + divisor - 1, <=, UINT32_MAX);
    q = (72 * 7383 + divisor - 1) / divisor;
    g_assert_cmpuint(q, ==, 130);
    g_assert_cmpuint(basecount - 1 + q, ==, 35319);
    g_assert_cmpuint(q + 2, ==, 132);
    g_assert_cmpuint(q + 3, ==, 133);
    g_assert_cmpuint(row[11] + q, ==, 131);
}

static void run_cli(char **argv, int expected_status, const char *message)
{
    g_autofree char *output = NULL;
    g_autofree char *errors = NULL;
    g_autoptr(GError) error = NULL;
    int status;

    g_assert_true(g_spawn_sync(NULL, argv, NULL, 0, NULL, NULL, &output,
                               &errors, &status, &error));
    g_assert_no_error(error);
    if (expected_status) {
        g_assert_false(g_spawn_check_exit_status(status, &error));
        g_assert_error(error, G_SPAWN_EXIT_ERROR, expected_status);
        g_assert_nonnull(strstr(errors, message));
    } else {
        g_assert_true(g_spawn_check_exit_status(status, &error));
        g_assert_no_error(error);
        g_assert_cmpstr(errors, ==, "");
    }
    g_assert_cmpstr(output, ==, "");
}

static void test_cli(void)
{
    g_autofree char *directory = g_dir_make_tmp("nikon-module-XXXXXX", NULL);
    g_autofree char *path = g_build_filename(directory, "module input.bin",
                                            NULL);
    g_autofree char *data = NULL;
    char profile[] = NIKON_MODULE_PROFILE;
    char unsupported[] = "factory";
    char excess[] = "extra";
    char blank[] = "";
    char *valid[] = { tool, profile, path, NULL };
    char *unknown[] = { tool, unsupported, path, NULL };
    char *missing[] = { tool, profile, NULL };
    char *extra[] = { tool, profile, path, excess, NULL };
    char *empty[] = { tool, profile, blank, NULL };
    char *unwritable[] = { tool, profile, directory, NULL };
    gsize size;

    g_assert_nonnull(directory);
    run_cli(valid, 0, NULL);
    run_cli(unknown, 2, "usage:");
    run_cli(missing, 2, "usage:");
    run_cli(extra, 2, "usage:");
    run_cli(empty, 2, "usage:");
    run_cli(unwritable, 3, "cannot write module input");
    g_assert_true(g_file_get_contents(path, &data, &size, NULL));
    g_assert_cmpuint(size, ==, 0x180000);
    g_assert_cmpmem(data + 0x17fffc, 4, "ENDM", 4);
    g_assert_cmphex((uint8_t)data[0xbe014], ==, 0x15);
    g_assert_cmpuint(lduw_le_p(data + 0xbe2b2), ==, 4096);
    g_assert_cmpuint(lduw_le_p(data + 0xbe2b6), ==, 35190);
    g_assert_cmpint(unlink(path), ==, 0);
    g_assert_cmpint(rmdir(directory), ==, 0);
}

static void test_regions(void)
{
    g_autofree char *directory = g_dir_make_tmp("nikon-regions-XXXXXX", NULL);
    g_autofree char *input = g_build_filename(directory, "input.bin", NULL);
    g_autofree char *output = g_build_filename(directory, "output.bin", NULL);
    g_autofree uint8_t *before = g_malloc(0x20001);
    g_autofree char *after = NULL;
    g_autofree char *original = NULL;
    char profile[] = "neutral-regions-descriptor17";
    char *valid[] = { tool, profile, input, output, NULL };
    char *missing[] = { tool, profile, output, input, NULL };
    char *bad_output[] = { tool, profile, input, directory, NULL };
    char *bad_args[] = { tool, profile, input, NULL };
    const uint8_t *record;
    gsize size, original_size;

    for (unsigned i = 0; i < 0x20001; i++) {
        before[i] = i * 29 + (i >> 8) * 11;
    }
    memcpy(before + 0x1fffc, "ENDM", 4);
    run_cli(bad_args, 2, "usage:");
    run_cli(missing, 3, "cannot read");
    g_assert_true(g_file_set_contents(input, (char *)before, 0x20000, NULL));
    run_cli(valid, 0, NULL);
    g_assert_true(g_file_get_contents(output, &after, &size, NULL));
    g_assert_cmpuint(size, ==, 0x20000);
    g_assert_cmpmem(after, 0xe400, before, 0xe400);
    g_assert_cmpmem(after + 0xf000, size - 0xf000,
                    before + 0xf000, size - 0xf000);
    g_assert_true(g_file_get_contents(input, &original, &original_size, NULL));
    g_assert_cmpmem(original, original_size, before, 0x20000);
    record = (const uint8_t *)after + 0xe400;
    g_assert_cmpuint(ldl_le_p(record + 28), ==, 8);
    g_assert_cmpuint(ldl_le_p(record + 32), ==, 8);
    for (unsigned family = 0; family < 2; family++) {
        unsigned covered[12][30] = { 0 };
        unsigned table = family ? 0x434 : 0x24;
        unsigned rectangle = family ? 0x834 : 0x424;

        for (unsigned i = 0; i < 64; i++) {
            uint32_t x0 = ldl_le_p(record + table + 4 * i);
            uint32_t x1 = ldl_le_p(record + table + 0x100 + 4 * i);
            uint32_t y0 = ldl_le_p(record + table + 0x200 + 4 * i);
            uint32_t y1 = ldl_le_p(record + table + 0x300 + 4 * i);

            g_assert_cmpuint(x0, <=, x1);
            g_assert_cmpuint(y0, <=, y1);
            g_assert_cmpuint(x1, <, 30);
            g_assert_cmpuint(y1, <, 12);
            for (unsigned y = y0; y <= y1; y++) {
                for (unsigned x = x0; x <= x1; x++) {
                    g_assert_cmpuint((y * 32 + x) * 2 + 2, <=, 32 * 12 * 2);
                    covered[y][x]++;
                }
            }
        }
        for (unsigned y = 0; y < 12; y++) {
            for (unsigned x = 0; x < 30; x++) {
                g_assert_cmpuint(covered[y][x], ==, 1);
            }
        }
        g_assert_cmpuint(ldl_le_p(record + rectangle), ==, 0);
        g_assert_cmpuint(ldl_le_p(record + rectangle + 4), ==, 0);
        g_assert_cmpuint(ldl_le_p(record + rectangle + 8), ==, 29);
        g_assert_cmpuint(ldl_le_p(record + rectangle + 12), ==, 11);
    }
    /* The first two consumers have 64-frame arrays and a five-frame window
     * on either side; the third consumer has only 16 frame pointers. */
    g_assert_cmpuint(ldl_le_p(record + 0x848), ==, 64);
    g_assert_cmpuint(ldl_le_p(record + 0x848), >, 10);
    g_assert_cmpuint(ldl_le_p(record + 0x860), ==, 64);
    g_assert_cmpuint(ldl_le_p(record + 0x864), ==, 16);
    g_assert_cmpuint(ldl_le_p(record + 0x850), ==, 24);
    run_cli(bad_output, 3, "cannot write");
    for (unsigned length = 0x1ffff; length <= 0x20001; length += 2) {
        g_assert_true(g_file_set_contents(input, (char *)before, length, NULL));
        run_cli(valid, 3, "exactly");
    }
    g_assert_cmpint(unlink(input), ==, 0);
    g_assert_cmpint(unlink(output), ==, 0);
    g_assert_cmpint(rmdir(directory), ==, 0);
}

static void test_group(void)
{
    g_autofree char *directory = g_dir_make_tmp("nikon-group-XXXXXX", NULL);
    g_autofree char *input = g_build_filename(directory, "input.bin", NULL);
    g_autofree char *output = g_build_filename(directory, "output.bin", NULL);
    g_autofree uint8_t *before = g_malloc0(0x20001);
    g_autofree char *after = NULL;
    g_autoptr(GBytes) source_bytes = NULL;
    g_autoptr(GBytes) encoded = NULL;
    g_autoptr(GError) error = NULL;
    uint8_t changed[0x20000] = { 0 };
    char profile[] = NIKON_MODULE_GROUP_PROFILE;
    char *valid[] = { tool, profile, input, output, NULL };
    char *missing[] = { tool, profile, input, NULL };
    gsize size;

    for (unsigned i = 0; i < 0x20001; i++) {
        before[i] = i * 31 + (i >> 8) * 13;
    }
    source_bytes = g_bytes_new(before, 0x20000);
    encoded = nikon_module_functional_group(source_bytes, &error);
    g_assert_no_error(error);
    g_assert_nonnull(encoded);
    g_assert_cmpmem(g_bytes_get_data(source_bytes, NULL), 0x20000,
                    before, 0x20000);
    memset(changed + 0x11500 + 0x1800, 1, 27 * 4);
    memset(changed + 0x11500 + 0x186c, 1, 4 * 96);
    memset(changed + 0x11500 + 0x19ec, 1, 16);
    memset(changed + 0x11500 + 0x1a1c, 1, 130);
    memset(changed + 0x11500 + 0x26b0, 1, 100);
    const uint8_t *data = g_bytes_get_data(encoded, &size);

    g_assert_cmpuint(size, ==, 0x20000);
    for (unsigned i = 0; i < size; i++) {
        if (!changed[i]) {
            g_assert_cmphex(data[i], ==, before[i]);
        }
    }
    for (unsigned variant = 0; variant < 5; variant++) {
        unsigned base = 0x11500 + (variant == 4 ? 0x26b0 : 0x186c + variant * 96);

        for (unsigned filter = 0; filter < 3; filter++) {
            for (unsigned tap = 0; tap < 8; tap++) {
                g_assert_cmpuint(ldl_le_p(data + base + (filter * 8 + tap) * 4),
                                 ==, tap == 3 ? 1 : tap == 4 + filter ? 127 : 0);
            }
        }
    }
    for (unsigned i = 0; i < 65; i++) {
        g_assert_cmpuint(lduw_le_p(data + 0x12f1c + i * 2), ==,
                         MIN(i * 1024, 65535));
    }
    g_assert_true(g_file_set_contents(input, (const char *)before, 0x20000, NULL));
    run_cli(valid, 0, NULL);
    g_assert_true(g_file_get_contents(output, &after, &size, NULL));
    g_assert_cmpmem(after, size, data, 0x20000);
    run_cli(missing, 2, "usage:");
    for (unsigned length = 0x1ffff; length <= 0x20001; length += 2) {
        g_autoptr(GBytes) invalid = g_bytes_new(before, length);
        g_autoptr(GBytes) result = nikon_module_functional_group(invalid, &error);

        g_assert_null(result);
        g_assert_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL);
        g_clear_error(&error);
        g_assert_true(g_file_set_contents(input, (const char *)before, length, NULL));
        run_cli(valid, 3, "exactly");
    }
    unlink(input);
    unlink(output);
    rmdir(directory);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_assert_cmpint(argc, ==, 2);
    tool = argv[1];
    g_test_add_func("/nikon/module/layout", test_layout);
    g_test_add_func("/nikon/module/readsets", test_readsets);
    g_test_add_func("/nikon/module/timing", test_timing);
    g_test_add_func("/nikon/module/cli", test_cli);
    g_test_add_func("/nikon/module/regions", test_regions);
    g_test_add_func("/nikon/module/group", test_group);
    return g_test_run();
}
