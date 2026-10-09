/*
 * Nikon decoded update container and auxiliary record inputs.
 * No wrapper decryption, CPU placement, peer success or boot state is implied.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "hw/nikon/bundle.h"

struct NikonBundle {
    GBytes *component[NIKON_COMPONENT_COUNT];
};

static const char *const component_names[NIKON_COMPONENT_COUNT] = {
    "ex1840_013000.bi", "_tpj01_v140.bin", "eg1840_mas_01700",
    "vr1840_010100.bi", "eg1840_sla_01700",
};

static uint16_t bundle_crc(const uint8_t *data, size_t size)
{
    uint16_t crc = 0;
    size_t i;
    unsigned bit;

    /* CRC-16/XMODEM, initial zero, non-reflected, polynomial 0x1021. */
    for (i = 0; i < size; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (bit = 0; bit < 8; bit++) {
            crc = (crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0);
        }
    }
    return crc;
}

NikonBundle *nikon_bundle_parse(GBytes *container, Error **errp)
{
    size_t size, expected = 48 + 32 * NIKON_COMPONENT_COUNT;
    const uint8_t *data = g_bytes_get_data(container, &size);
    uint32_t offsets[NIKON_COMPONENT_COUNT], lengths[NIKON_COMPONENT_COUNT];
    NikonBundle *bundle;
    unsigned i;

    if (size < expected + 16 || size > UINT32_MAX ||
        memcmp(data, "                                ", 32) ||
        ldl_be_p(data + 32) != NIKON_COMPONENT_COUNT ||
        ldl_be_p(data + 36) != expected ||
        ldl_be_p(data + 40) || ldl_be_p(data + 44)) {
        error_setg(errp, "invalid Nikon five-component decoded directory");
        return NULL;
    }
    for (i = 0; i < NIKON_COMPONENT_COUNT; i++) {
        const uint8_t *entry = data + 48 + i * 32;
        size_t name_len = strlen(component_names[i]);
        uint32_t offset = ldl_be_p(entry + 16);
        uint32_t length = ldl_be_p(entry + 20);
        unsigned j;

        if (memcmp(entry, component_names[i], name_len)) {
            error_setg(errp, "Nikon component %u has unexpected identity", i);
            return NULL;
        }
        for (j = name_len; j < 16; j++) {
            if (entry[j]) {
                error_setg(errp, "Nikon component %u name is not padded", i);
                return NULL;
            }
        }
        if (ldl_be_p(entry + 24) || ldl_be_p(entry + 28) ||
            offset != expected || length < 2 ||
            length > size - 16 - expected) {
            error_setg(errp, "Nikon component %u has invalid source bounds", i);
            return NULL;
        }
        if (bundle_crc(data + offset, length - 2) !=
            lduw_be_p(data + offset + length - 2)) {
            error_setg(errp, "Nikon component %u CRC mismatch", i);
            return NULL;
        }
        offsets[i] = offset;
        lengths[i] = length - 2;
        expected += length;
    }
    if (expected != size - 16 ||
        memcmp(data + size - 14, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 14) ||
        bundle_crc(data, size - 16) != lduw_be_p(data + size - 16)) {
        error_setg(errp, "invalid Nikon package footer or CRC");
        return NULL;
    }
    bundle = g_new0(NikonBundle, 1);
    for (i = 0; i < NIKON_COMPONENT_COUNT; i++) {
        bundle->component[i] = g_bytes_new_from_bytes(container, offsets[i],
                                                    lengths[i]);
    }
    return bundle;
}

NikonBundle *nikon_bundle_load(const char *path, Error **errp)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(GBytes) bytes = NULL;
    gchar *data;
    gsize size;

    if (!g_file_get_contents(path, &data, &size, &error)) {
        error_setg(errp, "cannot read Nikon bundle '%s': %s", path,
                   error->message);
        return NULL;
    }
    bytes = g_bytes_new_take(data, size);
    return nikon_bundle_parse(bytes, errp);
}

void nikon_bundle_free(NikonBundle *bundle)
{
    unsigned i;

    if (!bundle) {
        return;
    }
    for (i = 0; i < NIKON_COMPONENT_COUNT; i++) {
        g_bytes_unref(bundle->component[i]);
    }
    g_free(bundle);
}

GBytes *nikon_bundle_component(NikonBundle *bundle, NikonComponent component)
{
    assert(component >= 0 && component < NIKON_COMPONENT_COUNT);
    return g_bytes_ref(bundle->component[component]);
}

GBytes *nikon_vr_body(GBytes *records, GBytes **metadata, Error **errp)
{
    const unsigned record_size = 262, body_size = 256;
    const uint8_t *data;
    uint8_t *body;
    size_t size, count, i;
    uint32_t address;

    *metadata = NULL;
    data = g_bytes_get_data(records, &size);
    if (size < 2 * record_size || size % record_size ||
        memcmp(data, "\x04\x01\x04\x00\x00\x01", 6)) {
        error_setg(errp, "invalid Nikon VR record sequence or metadata");
        return NULL;
    }
    count = size / record_size - 1;
    address = ldl_le_p(data + record_size + 2);
    if (count > ((uint64_t)UINT32_MAX + 1 - address) / body_size) {
        error_setg(errp, "Nikon VR record address sequence overflows");
        return NULL;
    }
    for (i = 0; i < count; i++) {
        const uint8_t *record = data + (i + 1) * record_size;

        if (record[0] != 4 || record[1] != 1 ||
            (uint32_t)ldl_le_p(record + 2) != address + i * body_size) {
            error_setg(errp, "Nikon VR record %zu is discontinuous", i + 1);
            return NULL;
        }
    }
    body = g_malloc(count * body_size);
    for (i = 0; i < count; i++) {
        memcpy(body + i * body_size, data + (i + 1) * record_size + 6,
               body_size);
    }
    *metadata = g_bytes_new_from_bytes(records, 0, record_size);
    return g_bytes_new_take(body, count * body_size);
}
