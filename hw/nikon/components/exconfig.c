/*
 * EX class-factory configuration composer.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "hw/nikon/exconfig.h"

typedef struct NikonEXConfigBlock {
    uint32_t magic;
    uint32_t offset;
    uint32_t size;
} NikonEXConfigBlock;

/* Little-endian four-character magics "MAKE", "MODE" and "INDI". */
#define MAGIC_MAKE 0x454b414du
#define MAGIC_MODE 0x45444f4du
#define MAGIC_INDI 0x49444e49u

static const NikonEXConfigBlock blocks[] = {
    { MAGIC_MAKE, NIKON_EX_CONFIG_A_OFFSET, NIKON_EX_CONFIG_A_SIZE },
    { MAGIC_MODE, NIKON_EX_CONFIG_B_OFFSET, NIKON_EX_CONFIG_B_SIZE },
    { MAGIC_INDI, NIKON_EX_CONFIG_C_OFFSET, NIKON_EX_CONFIG_C_SIZE },
};

/* The firmware accepts the primary view when its first word is "MAKE". */
static const uint8_t z7ii_ex_config_record1[] = {
    [NIKON_EX_CONFIG_REC1_CHANNEL0] = 0,
    [NIKON_EX_CONFIG_REC1_MODE] = 5,
    [NIKON_EX_CONFIG_REC1_MODE + 1] = 5,
    [NIKON_EX_CONFIG_REC1_GATE] = 1,
};

bool nikon_ex_config_compose(uint8_t *flash, size_t flash_size)
{
    uint8_t *record1;

    for (size_t i = 0; i < ARRAY_SIZE(blocks); i++) {
        if ((uint64_t)blocks[i].offset + blocks[i].size > flash_size) {
            return false;
        }
    }
    if (!flash) {
        return false;
    }

    /*
     * Unknown factory fields keep erased zero.  The size field is echoed four
     * bytes before the end of the block, which is the firmware's own integrity
     * check; zeroing the block first means every record this model does not
     * populate stays erased.
     */
    for (size_t i = 0; i < ARRAY_SIZE(blocks); i++) {
        uint8_t *block = flash + blocks[i].offset;

        memset(block, 0, blocks[i].size);
        stl_le_p(block, blocks[i].magic);
        stl_le_p(block + 4, blocks[i].size);
        stl_le_p(block + blocks[i].size - 4, blocks[i].size);
    }

    record1 = flash + NIKON_EX_CONFIG_A_OFFSET + NIKON_EX_CONFIG_REC1_OFFSET;
    memset(record1, 0, NIKON_EX_CONFIG_REC1_SIZE);
    memcpy(record1, z7ii_ex_config_record1,
           sizeof(z7ii_ex_config_record1));
    return true;
}
