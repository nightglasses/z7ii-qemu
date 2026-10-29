/*
 * Explicit descriptor inputs; captured bytes remain caller-owned files.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "module.h"

GBytes *nikon_module_cold_neutral(void)
{
    static const unsigned timing_groups[] = { 7, 10 };
    uint8_t *data = g_malloc0(NIKON_MODULE_SLOT_SIZE);

    /* Every other byte is a declared zero-valued functional parameter. */
    data[0xbe014] = 0x15;
    data[0xbe015] = 0x00;
    /* Uniform virtual rows for the two source groups' four subtype selectors. */
    for (unsigned i = 0; i < G_N_ELEMENTS(timing_groups); i++) {
        for (unsigned subtype = 0; subtype < 4; subtype++) {
            uint8_t *row = data + 0xbe0c0 + timing_groups[i] * 64 + subtype * 16;

            stw_le_p(row + 2, 4096);
            stw_le_p(row + 6, 35190);
            row[11] = 1;
            stw_le_p(row + 12, 72);
        }
    }
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

GBytes *nikon_module_acceleration(GBytes *descriptor, GBytes *capture,
                                 gboolean raw12, GError **error)
{
    static const char marker[16] = "ACCELE";
    static const unsigned fields[] = { 32, 36, 40, 56, 60, 64 };
    gsize size, capture_size;
    const uint8_t *original = g_bytes_get_data(descriptor, &size);
    const uint8_t *input = g_bytes_get_data(capture, &capture_size);
    uint8_t record[192], *data;

    if (size != NIKON_DESCRIPTOR18_SLOT_SIZE || capture_size != 4096) {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                            "descriptor18 must be 640 KiB and F025 4096 bytes");
        return NULL;
    }
    if (memcmp(input + 0x100, marker, sizeof(marker))) {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                            "F025 offset 0x100 lacks the padded ACCELE marker");
        return NULL;
    }
    /*
     * C1.70 manifest 118d0be0 copies exactly C0 bytes from offset100.
     * Setter103d5366 checks only the marker; preserve type and other fields.
     */
    memcpy(record, input + 0x100, sizeof(record));
    if (raw12) {
        for (unsigned i = 0; i < ARRAY_SIZE(fields); i++) {
            int32_t value = (int32_t)ldl_le_p(record + fields[i]);

            if (value % 16 || (i >= 3 && value <= 0)) {
                g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                            "ACCELE field %u must divide exactly by 16; "
                            "gains must be positive", fields[i]);
                return NULL;
            }
            stl_le_p(record + fields[i], value / 16);
        }
    }
    data = g_memdup2(original, size);
    memcpy(data + 0x100, record, sizeof(record));
    return g_bytes_new_take(data, size);
}
