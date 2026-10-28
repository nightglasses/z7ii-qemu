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

GBytes *nikon_module_neutral_regions(GBytes *descriptor, GError **error)
{
    gsize size;
    const uint8_t *original = g_bytes_get_data(descriptor, &size);
    uint8_t *data, *record;

    if (size != NIKON_DESCRIPTOR17_SLOT_SIZE) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                    "descriptor17 input must be exactly %u bytes",
                    NIKON_DESCRIPTOR17_SLOT_SIZE);
        return NULL;
    }
    data = g_memdup2(original, size);
    record = data + 0xe400;
    /* Manifest 118d0b30 copies C00 bytes; the D00-byte directory span is not
     * the payload length. Other records and the outer footer stay opaque. */
    memset(record, 0, 0xc00);
    stl_le_p(record + 28, 8);
    stl_le_p(record + 32, 8);
    for (unsigned family = 0; family < 2; family++) {
        unsigned table = family ? 0x434 : 0x24;
        unsigned rectangle = family ? 0x834 : 0x424;

        /* 10218bca selects inclusive bounds in two 8x8 region families. */
        for (unsigned y = 0; y < 8; y++) {
            for (unsigned x = 0; x < 8; x++) {
                unsigned offset = table + 4 * (y * 8 + x);

                stl_le_p(record + offset, x * 30 / 8);
                stl_le_p(record + offset + 0x100, (x + 1) * 30 / 8 - 1);
                stl_le_p(record + offset + 0x200, y * 12 / 8);
                stl_le_p(record + offset + 0x300, (y + 1) * 12 / 8 - 1);
            }
        }
        /* 10218e7c obtains the corresponding whole-image rectangle. */
        stl_le_p(record + rectangle + 8, 29);
        stl_le_p(record + rectangle + 12, 11);
    }
    /* Source consumers bound pointers to 64/64/16 and use ten margin frames.
     * Thresholds/other parameters remain explicit zero-valued hypotheses. */
    stl_le_p(record + 0x848, 64);
    stl_le_p(record + 0x850, 24);
    stl_le_p(record + 0x860, 64);
    stl_le_p(record + 0x864, 16);
    return g_bytes_new_take(data, size);
}

GBytes *nikon_module_functional_group(GBytes *descriptor, GError **error)
{
    gsize size;
    const uint8_t *original = g_bytes_get_data(descriptor, &size);
    uint8_t *data, *record;

    if (size != NIKON_DESCRIPTOR17_SLOT_SIZE) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                    "descriptor17 input must be exactly %u bytes",
                    NIKON_DESCRIPTOR17_SLOT_SIZE);
        return NULL;
    }
    data = g_memdup2(original, size);
    record = data + 0x11500;
    /* Resource39: 102aa02a reads modes at1800, four24-word coefficient
     * variants at186c, the alternate at26b0, and a65-u16 curve at1a1c.
     * Only these consumed fields are virtual declarations. */
    for (unsigned i = 0; i < 27; i++) {
        stl_le_p(record + 0x1800 + i * 4, 0);
    }
    for (unsigned variant = 0; variant < 5; variant++) {
        unsigned base = variant == 4 ? 0x26b0 : 0x186c + variant * 96;

        for (unsigned filter = 0; filter < 3; filter++) {
            for (unsigned tap = 0; tap < 8; tap++) {
                unsigned coefficient = tap == 3 ? 1 :
                                       tap == 4 + filter ? 0x7f : 0;

                stl_le_p(record + base + (filter * 8 + tap) * 4, coefficient);
            }
        }
    }
    for (unsigned i = 0; i < 4; i++) {
        stl_le_p(record + 0x19ec + i * 4, 0);
    }
    stl_le_p(record + 0x2710, 0);
    for (unsigned i = 0; i < 65; i++) {
        stw_le_p(record + 0x1a1c + i * 2, MIN(i * 1024, 65535));
    }
    return g_bytes_new_take(data, size);
}
