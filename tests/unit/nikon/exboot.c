/*
 * EX cold-boot scatter loader fixtures.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "hw/nikon/exboot.h"

#define FLASH_BASE 0x08000000u
#define FLASH_SIZE 0x1000u
#define RAM_BASE 0x20000000u
#define RAM_SIZE 0x400u
#define TABLE_OFFSET 0x0c0u

static uint8_t flash[FLASH_SIZE];
static uint8_t ram[RAM_SIZE];

static void put32(size_t off, uint32_t value)
{
    stl_le_p(flash + off, value);
}

static void put_record(unsigned index, uint32_t src, uint32_t dst,
                       uint32_t len, uint32_t fn)
{
    size_t off = TABLE_OFFSET + index * 16;

    put32(off, src);
    put32(off + 4, dst);
    put32(off + 8, len);
    put32(off + 12, fn);
}

static bool run(void)
{
    return nikon_ex_boot_image(ram, RAM_BASE, RAM_SIZE, flash, FLASH_BASE,
                               FLASH_SIZE, FLASH_BASE + TABLE_OFFSET);
}

/* Literals "WXYZ", then a back reference of four bytes at distance four. */
static void lz_stream(void)
{
    size_t off = 0x100;

    flash[off++] = 0x05;      /* five literals (four bytes), zero run */
    flash[off++] = 0x00;      /* extended run length: no zero bytes */
    flash[off++] = 'W';
    flash[off++] = 'X';
    flash[off++] = 'Y';
    flash[off++] = 'Z';
    flash[off++] = 0x28;      /* bit3 match, run 2 */
    flash[off++] = 0x01;      /* extended literal count: nothing copied */
    flash[off++] = 0x04;      /* distance four */
    put32(0x0e0, 8);          /* unused tail keeps the table readable */
}

static void boot_application(void)
{
    memset(flash, 0, sizeof(flash));
    memset(ram, 0xaa, sizeof(ram));
    lz_stream();
    put_record(0, FLASH_BASE + 0x100, RAM_BASE + 0x40, 8,
               NIKON_EX_SCATTER_LZ);
    put_record(1, FLASH_BASE + 0x100, RAM_BASE + 0x80, 4,
               NIKON_EX_SCATTER_ZERO);

    g_assert_true(run());
    g_assert_cmpmem(ram + 0x40, 8, "WXYZWXYZ", 8);
    g_assert_cmpmem(ram + 0x80, 4, "\0\0\0\0", 4);
    /* The rest of the region keeps its reset value. */
    g_assert_cmphex(ram[0x8c], ==, 0xaa);
}

static void unsupported_loader_is_skipped(void)
{
    memset(flash, 0, sizeof(flash));
    memset(ram, 0xaa, sizeof(ram));
    put_record(0, FLASH_BASE + 0x100, RAM_BASE + 0x40, 16,
               NIKON_EX_SCATTER_UNSUPPORTED);

    g_assert_true(run());
    g_assert_cmphex(ram[0x40], ==, 0xaa);
}

static void bad_distance_is_rejected(void)
{
    memset(flash, 0, sizeof(flash));
    memset(ram, 0, sizeof(ram));
    flash[0x100] = 0x08;      /* match with no literal history */
    flash[0x101] = 0x01;
    flash[0x102] = 0x04;
    put_record(0, FLASH_BASE + 0x100, RAM_BASE + 0x40, 8,
               NIKON_EX_SCATTER_LZ);

    g_assert_false(run());
}

static void out_of_range_is_rejected(void)
{
    memset(flash, 0, sizeof(flash));
    memset(ram, 0, sizeof(ram));
    put_record(0, FLASH_BASE + 0x100, RAM_BASE + RAM_SIZE - 4, 8,
               NIKON_EX_SCATTER_ZERO);

    g_assert_false(run());
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/nikon/exboot/application", boot_application);
    g_test_add_func("/nikon/exboot/unsupported-loader",
                    unsupported_loader_is_skipped);
    g_test_add_func("/nikon/exboot/bad-distance", bad_distance_is_rejected);
    g_test_add_func("/nikon/exboot/out-of-range", out_of_range_is_rejected);
    return g_test_run();
}
