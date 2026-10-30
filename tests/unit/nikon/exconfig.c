/*
 * EX class-factory configuration composer fixtures.
 *
 * The composer targets the original firmware's own block validator: magic,
 * a size field at +4, the size echoed four bytes before the block end, and the
 * record-1 fields the cold-start consumers read.  These fixtures assert that
 * layout, not recovered factory data.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "hw/nikon/exconfig.h"

#define FLASH_SIZE 0x80000u
#define SENTINEL 0xa5u

static uint8_t flash[FLASH_SIZE];

static void reset_flash(void)
{
    memset(flash, SENTINEL, sizeof(flash));
}

static void check_block(uint32_t offset, const char *magic, uint32_t size,
                        const uint32_t *record_sizes, size_t record_count)
{
    uint32_t cursor = NIKON_EX_CONFIG_HEADER_SIZE;

    g_assert_cmpmem(flash + offset, 4, magic, 4);
    g_assert_cmphex(ldl_le_p(flash + offset + 4), ==, size);
    g_assert_cmphex(ldl_le_p(flash + offset + size - 4), ==, size);
    for (size_t i = 0; i < record_count; i++) {
        cursor += record_sizes[i];
    }
    g_assert_cmphex(cursor + 4, ==, size);
}

static void assert_zero(uint32_t start, uint32_t end)
{
    for (uint32_t i = start; i < end; i++) {
        g_assert_cmphex(flash[i], ==, 0);
    }
}

/* Block A's header tail and record 1 keep erased zero apart from the fields. */
static void assert_block_a_fill(void)
{
    static const uint32_t fields[] = {
        NIKON_EX_CONFIG_REC1_CHANNEL0,
        NIKON_EX_CONFIG_REC1_MODE,
        NIKON_EX_CONFIG_REC1_MODE + 1,
        NIKON_EX_CONFIG_REC1_GATE,
    };
    uint32_t base = NIKON_EX_CONFIG_A_OFFSET;

    assert_zero(base + 8, base + NIKON_EX_CONFIG_HEADER_SIZE);
    for (uint32_t i = 0; i < NIKON_EX_CONFIG_REC1_SIZE; i++) {
        bool written = false;

        for (size_t f = 0; f < ARRAY_SIZE(fields); f++) {
            written |= fields[f] == i;
        }
        if (!written) {
            g_assert_cmphex(flash[base + NIKON_EX_CONFIG_HEADER_SIZE + i], ==,
                            0);
        }
    }
}

/* Everything outside the three blocks keeps its previous contents. */
static void assert_outside_untouched(void)
{
    static const struct {
        uint32_t start;
        uint32_t end;
    } spans[] = {
        { 0, NIKON_EX_CONFIG_A_OFFSET },
        { NIKON_EX_CONFIG_A_OFFSET + NIKON_EX_CONFIG_A_SIZE,
          NIKON_EX_CONFIG_B_OFFSET },
        { NIKON_EX_CONFIG_B_OFFSET + NIKON_EX_CONFIG_B_SIZE,
          NIKON_EX_CONFIG_C_OFFSET },
        { NIKON_EX_CONFIG_C_OFFSET + NIKON_EX_CONFIG_C_SIZE, FLASH_SIZE },
    };

    for (size_t s = 0; s < ARRAY_SIZE(spans); s++) {
        for (uint32_t i = spans[s].start; i < spans[s].end; i++) {
            g_assert_cmphex(flash[i], ==, SENTINEL);
        }
    }
}

static void compose(void)
{
    static const uint32_t block_a_records[] = { NIKON_EX_CONFIG_REC1_SIZE };
    static const uint32_t block_b_records[] = {
        NIKON_EX_CONFIG_REC3_SIZE, NIKON_EX_CONFIG_REC4_SIZE,
        NIKON_EX_CONFIG_REC5_SIZE, NIKON_EX_CONFIG_REC6_SIZE,
    };
    static const uint32_t block_c_records[] = { NIKON_EX_CONFIG_REC8_SIZE };
    uint8_t first[FLASH_SIZE];

    reset_flash();
    g_assert_true(nikon_ex_config_compose(flash, sizeof(flash)));

    check_block(NIKON_EX_CONFIG_A_OFFSET, "MAKE", NIKON_EX_CONFIG_A_SIZE,
                block_a_records, ARRAY_SIZE(block_a_records));
    check_block(NIKON_EX_CONFIG_B_OFFSET, "MODE", NIKON_EX_CONFIG_B_SIZE,
                block_b_records, ARRAY_SIZE(block_b_records));
    check_block(NIKON_EX_CONFIG_C_OFFSET, "INDI", NIKON_EX_CONFIG_C_SIZE,
                block_c_records, ARRAY_SIZE(block_c_records));

    /* Record 1 carries the four fields the cold-start consumers read. */
    g_assert_cmphex(flash[NIKON_EX_CONFIG_A_OFFSET +
                          NIKON_EX_CONFIG_REC1_OFFSET +
                          NIKON_EX_CONFIG_REC1_CHANNEL0], ==, 0);
    g_assert_cmphex(flash[NIKON_EX_CONFIG_A_OFFSET +
                          NIKON_EX_CONFIG_REC1_OFFSET +
                          NIKON_EX_CONFIG_REC1_MODE], ==, 5);
    g_assert_cmphex(flash[NIKON_EX_CONFIG_A_OFFSET +
                          NIKON_EX_CONFIG_REC1_OFFSET +
                          NIKON_EX_CONFIG_REC1_MODE + 1], ==, 5);
    g_assert_cmphex(flash[NIKON_EX_CONFIG_A_OFFSET +
                          NIKON_EX_CONFIG_REC1_OFFSET +
                          NIKON_EX_CONFIG_REC1_GATE], ==, 1);

    assert_block_a_fill();
    assert_zero(NIKON_EX_CONFIG_B_OFFSET + 8,
                NIKON_EX_CONFIG_B_OFFSET + NIKON_EX_CONFIG_B_SIZE - 4);
    assert_zero(NIKON_EX_CONFIG_C_OFFSET + 8,
                NIKON_EX_CONFIG_C_OFFSET + NIKON_EX_CONFIG_C_SIZE - 4);
    assert_outside_untouched();

    /* Deterministic: the same input composes the same bytes. */
    memcpy(first, flash, sizeof(first));
    g_assert_true(nikon_ex_config_compose(flash, sizeof(flash)));
    g_assert_cmpmem(first, sizeof(first), flash, sizeof(flash));
}

/* A flash too small for the complete span is rejected without any write. */
static void compose_too_small(void)
{
    uint8_t exact[NIKON_EX_CONFIG_SPAN];
    uint8_t short_flash[NIKON_EX_CONFIG_SPAN - 4];

    memset(short_flash, SENTINEL, sizeof(short_flash));
    g_assert_false(nikon_ex_config_compose(short_flash, sizeof(short_flash)));
    for (size_t i = 0; i < sizeof(short_flash); i++) {
        g_assert_cmphex(short_flash[i], ==, SENTINEL);
    }
    g_assert_false(nikon_ex_config_compose(NULL, sizeof(exact)));

    memset(exact, SENTINEL, sizeof(exact));
    g_assert_true(nikon_ex_config_compose(exact, sizeof(exact)));
    g_assert_cmphex(ldl_le_p(exact + NIKON_EX_CONFIG_C_OFFSET + 4), ==,
                    NIKON_EX_CONFIG_C_SIZE);
    g_assert_cmphex(ldl_le_p(exact + NIKON_EX_CONFIG_C_OFFSET +
                             NIKON_EX_CONFIG_C_SIZE - 4), ==,
                    NIKON_EX_CONFIG_C_SIZE);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/ex/config/compose", compose);
    g_test_add_func("/ex/config/too-small", compose_too_small);
    return g_test_run();
}
