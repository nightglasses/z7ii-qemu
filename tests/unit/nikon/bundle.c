/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "hw/nikon/bundle.h"

static uint16_t test_crc(const uint8_t *data, size_t size)
{
    uint16_t crc = 0;
    size_t i;
    unsigned bit;

    for (i = 0; i < size; i++) {
        crc ^= data[i] << 8;
        for (bit = 0; bit < 8; bit++) {
            crc = (crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0);
        }
    }
    return crc;
}

static GBytes *test_container(void)
{
    static const char *const names[] = {
        "ex1840_013000.bi", "_tpj01_v140.bin", "eg1840_mas_01700",
        "vr1840_010100.bi", "eg1840_sla_01700",
    };
    const unsigned header = 208, payload = 9, span = payload + 2;
    unsigned size = header + span * ARRAY_SIZE(names) + 16;
    uint8_t *data = g_malloc0(size);
    unsigned i;

    memset(data, ' ', 32);
    stl_be_p(data + 32, ARRAY_SIZE(names));
    stl_be_p(data + 36, header);
    for (i = 0; i < ARRAY_SIZE(names); i++) {
        unsigned offset = header + i * span;
        uint8_t *entry = data + 48 + i * 32;

        memcpy(entry, names[i], strlen(names[i]));
        stl_be_p(entry + 16, offset);
        stl_be_p(entry + 20, span);
        memcpy(data + offset, "123456789", payload);
        /* Published XMODEM known-answer vector; independent of test_crc. */
        stw_be_p(data + offset + payload, 0x31c3);
    }
    stw_be_p(data + size - 16, test_crc(data, size - 16));
    return g_bytes_new_take(data, size);
}

static void test_valid(void)
{
    g_autoptr(GBytes) input = test_container();
    NikonBundle *bundle = nikon_bundle_parse(input, &error_abort);
    unsigned i;

    g_assert_nonnull(bundle);
    for (i = 0; i < NIKON_COMPONENT_COUNT; i++) {
        g_autoptr(GBytes) part = nikon_bundle_component(bundle, i);
        size_t size;
        const void *data = g_bytes_get_data(part, &size);

        g_assert_cmpmem(data, size, "123456789", 9);
    }
    nikon_bundle_free(bundle);
}

static void test_invalid(void)
{
    /* Bounds/identity/CRC/footer changes must all fail before publication. */
    static const size_t positions[] = {
        0, 35, 39, 40, 48, 63, 67, 71, 72, 208, 217, 263, 265,
    };
    g_autoptr(GBytes) input = test_container();
    size_t size;
    const void *source = g_bytes_get_data(input, &size);
    unsigned i;

    for (i = 0; i < ARRAY_SIZE(positions); i++) {
        g_autoptr(GBytes) changed = NULL;
        Error *error = NULL;
        uint8_t *data = g_memdup2(source, size);

        data[positions[i]] ^= 1;
        changed = g_bytes_new_take(data, size);
        g_assert_null(nikon_bundle_parse(changed, &error));
        g_assert_nonnull(error);
        error_free(error);
    }
    for (i = 0; i < size; i++) {
        g_autoptr(GBytes) short_input = g_bytes_new_from_bytes(input, 0, i);
        Error *error = NULL;

        g_assert_null(nikon_bundle_parse(short_input, &error));
        g_assert_nonnull(error);
        error_free(error);
    }
}

static void test_records(void)
{
    uint8_t data[3 * 262] = { 4, 1, 4, 0, 0, 1 };
    g_autoptr(GBytes) records = NULL;
    g_autoptr(GBytes) metadata = NULL;
    g_autoptr(GBytes) body = NULL;
    const uint8_t *output;
    size_t size;
    unsigned i;

    for (i = 1; i < 3; i++) {
        data[i * 262] = 4;
        data[i * 262 + 1] = 1;
        stl_le_p(data + i * 262 + 2, 0xfff00000 + (i - 1) * 256);
        memset(data + i * 262 + 6, i, 256);
    }
    records = g_bytes_new(data, sizeof(data));
    body = nikon_vr_body(records, &metadata, &error_abort);
    output = g_bytes_get_data(body, &size);
    g_assert_cmpuint(size, ==, 512);
    g_assert_cmpuint(g_bytes_get_size(metadata), ==, 262);
    g_assert_cmpmem(g_bytes_get_data(metadata, NULL), 262, data, 262);
    for (i = 0; i < size; i++) {
        g_assert_cmpuint(output[i], ==, i < 256 ? 1 : 2);
    }
    data[2 * 262 + 2] ^= 1;
    {
        g_autoptr(GBytes) invalid = g_bytes_new(data, sizeof(data));
        GBytes *missing = NULL;
        Error *error = NULL;

        g_assert_null(nikon_vr_body(invalid, &missing, &error));
        g_assert_null(missing);
        g_assert_nonnull(error);
        error_free(error);
    }
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/nikon/bundle/complete", test_valid);
    g_test_add_func("/nikon/bundle/rejection", test_invalid);
    g_test_add_func("/nikon/bundle/records", test_records);
    return g_test_run();
}
