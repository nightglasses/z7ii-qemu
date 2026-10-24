/* Explicit finite resource80 cold-path input, not recovered Nikon data.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "module.h"

GBytes *nikon_module_cold_neutral(void)
{
    uint8_t *data = g_malloc0(NIKON_MODULE_SLOT_SIZE);
    uint8_t *row = data + 0xbe2b0;

    /* Every other byte is a declared zero-valued functional parameter. */
    data[0xbe014] = 0x15;
    data[0xbe015] = 0x00;
    stw_le_p(row + 2, 4096);
    stw_le_p(row + 6, 35190);
    row[11] = 1;
    stw_le_p(row + 12, 72);
    memcpy(data + NIKON_MODULE_SLOT_SIZE - 4, "ENDM", 4);
    return g_bytes_new_take(data, NIKON_MODULE_SLOT_SIZE);
}
