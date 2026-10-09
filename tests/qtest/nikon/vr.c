/* Synthetic RX guest tests. No Nikon firmware is included. */
/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "libqtest.h"

static uint16_t vr_test_crc(const uint8_t *data, size_t size)
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

static char *vr_test_bundle(void)
{
    static const char *const names[] = {
        "ex1840_013000.bi", "_tpj01_v140.bin", "eg1840_mas_01700",
        "vr1840_010100.bi", "eg1840_sla_01700",
    };
    /* Existing upstream insns.decode encodings: MOV/MVFC/MVTC, illegal, BRA. */
    static const uint8_t entry[] = {
        0xfb, 0x12, 0x00, 0x11, 0x00, 0x00, /* mov.l #1100,r1 */
        0xef, 0x02,                         /* mov.l r0,r2 */
        0xe3, 0x12,                         /* mov.l r2,[r1] */
        0xfd, 0x6a, 0xd2,                   /* mvfc extb,r2 */
        0xfb, 0x12, 0x04, 0x11, 0x00, 0x00,
        0xe3, 0x12,
        0xfb, 0xe2, 0x00, 0x12, 0x00, 0x00, /* mov.l #1200,r14 */
        0xfd, 0x68, 0xed,                   /* mvtc r14,extb */
        0xfd, 0x6a, 0xd2,
        0xfb, 0x12, 0x08, 0x11, 0x00, 0x00,
        0xe3, 0x12,
        0xff, 0xff, 0xff,                   /* undefined => exception 23 */
    };
    static const uint8_t handler[] = {
        0xfb, 0x12, 0x10, 0x11, 0x00, 0x00, /* mov.l #1110,r1 */
        0x66, 0x12,                         /* mov.l #1,r2 */
        0xe3, 0x12,
        0x2e, 0x00,                         /* bra.b current PC */
    };
    const unsigned body_size = 0x1000, record_count = body_size / 256 + 1;
    const unsigned vr_size = record_count * 262;
    unsigned size = 208 + 4 * 3 + vr_size + 2 + 16;
    g_autofree uint8_t *data = g_malloc0(size);
    g_autofree uint8_t *body = g_malloc0(body_size);
    g_autofree uint8_t *records = g_malloc0(vr_size);
    g_autoptr(GError) error = NULL;
    char *path = NULL;
    unsigned i, offset = 208;
    int fd;

    memcpy(body + 0x10, entry, sizeof(entry));
    memcpy(body + 0x100, handler, sizeof(handler));
    /* Zero-length COPY records still have a valid original source interval. */
    for (i = 0; i < 13; i++) {
        stl_le_p(body + 0xc00 + i * 12, 0xfff00000);
        stl_le_p(body + 0xc04 + i * 12, 0xfff00000);
    }
    memcpy(records, "\x04\x01\x04\x00\x00\x01", 6);
    for (i = 1; i < record_count; i++) {
        records[i * 262] = 4;
        records[i * 262 + 1] = 1;
        stl_le_p(records + i * 262 + 2, 0xfff00000 + (i - 1) * 256);
        memcpy(records + i * 262 + 6, body + (i - 1) * 256, 256);
    }
    memset(data, ' ', 32);
    stl_be_p(data + 32, ARRAY_SIZE(names));
    stl_be_p(data + 36, 208);
    for (i = 0; i < ARRAY_SIZE(names); i++) {
        uint8_t *directory = data + 48 + i * 32;
        unsigned payload = i == 3 ? vr_size : 1;

        memcpy(directory, names[i], strlen(names[i]));
        stl_be_p(directory + 16, offset);
        stl_be_p(directory + 20, payload + 2);
        if (i == 3) {
            memcpy(data + offset, records, payload);
        }
        stw_be_p(data + offset + payload, vr_test_crc(data + offset, payload));
        offset += payload + 2;
    }
    stw_be_p(data + size - 16, vr_test_crc(data, size - 16));
    fd = g_file_open_tmp("nikonvr.XXXXXX", &path, &error);
    g_assert_no_error(error);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    g_assert_true(g_file_set_contents(path, (const char *)data, size, &error));
    g_assert_no_error(error);
    return path;
}

static void vr_guest_complete(QTestState *qts)
{
    int64_t deadline = g_get_monotonic_time() + G_TIME_SPAN_SECOND;

    qtest_qmp_assert_success(qts, "{'execute':'cont'}");
    while (qtest_readl(qts, 0x1110) != 1 &&
           g_get_monotonic_time() < deadline) {
        g_usleep(1000); /* Host budget, never used as a hardware tick model. */
    }
    g_assert_cmphex(qtest_readl(qts, 0x1110), ==, 1);
    qtest_qmp_assert_success(qts, "{'execute':'stop'}");
    g_assert_cmphex(qtest_readl(qts, 0x1100), ==, 0x2000);
    g_assert_cmphex(qtest_readl(qts, 0x1104), ==, 0xffffff80);
    g_assert_cmphex(qtest_readl(qts, 0x1108), ==, 0x1200);
}

static void vr_reset_and_exception(void)
{
    g_autofree char *path = vr_test_bundle();
    QTestState *qts = qtest_initf("-machine nikonvr -bios %s -accel tcg -S", path);

    /* A real guest exception selects this vector through its written EXTB. */
    qtest_writel(qts, 0x1200 + 23 * 4, 0xfff00100);
    vr_guest_complete(qts);
    qtest_qmp_assert_success(qts, "{'execute':'system_reset'}");
    qtest_writel(qts, 0x1110, 0);
    vr_guest_complete(qts);
    qtest_quit(qts);
    unlink(path);
}

static void vr_protection_and_clocks(void)
{
    g_autofree char *path = vr_test_bundle();
    QTestState *qts = qtest_initf("-machine nikonvr -bios %s -S", path);

    /* A locked or incorrectly keyed write cannot change protected clocks. */
    qtest_writel(qts, 0x80020, 0x10c10166);
    g_assert_cmphex(qtest_readl(qts, 0x80020), ==, 0);
    qtest_writew(qts, 0x803fe, 0xa40b);
    g_assert_cmphex(qtest_readw(qts, 0x803fe), ==, 0);
    qtest_writew(qts, 0x803fe, 0xa50b);
    g_assert_cmphex(qtest_readw(qts, 0x803fe), ==, 0xb);
    qtest_writel(qts, 0x80020, 0x10c10166);
    g_assert_cmphex(qtest_readl(qts, 0x80020), ==, 0x10c10166);

    /* Status follows a running input and PLL enable, after virtual settling. */
    qtest_writeb(qts, 0x800a2, 120);
    qtest_writeb(qts, 0x80032, 0);
    qtest_writew(qts, 0x80028, 0x1301);
    qtest_writeb(qts, 0x8002a, 0);
    g_assert_cmphex(qtest_readb(qts, 0x8003c), ==, 0);
    qtest_clock_step(qts, 1000);
    g_assert_cmphex(qtest_readb(qts, 0x8003c), ==, 5);
    qtest_writeb(qts, 0x8002a, 1);
    g_assert_cmphex(qtest_readb(qts, 0x8003c), ==, 1);
    qtest_writeb(qts, 0x80032, 1);
    g_assert_cmphex(qtest_readb(qts, 0x8003c), ==, 0);

    /* RTC reset is an operation that self-clears, never a fixed ready value. */
    qtest_writeb(qts, 0x8c428, 1);
    qtest_writeb(qts, 0x8c424, 2);
    g_assert_cmphex(qtest_readb(qts, 0x8c424), ==, 2);
    qtest_clock_step(qts, 1000);
    g_assert_cmphex(qtest_readb(qts, 0x8c424), ==, 0);
    qtest_writeb(qts, 0x8c426, 2);
    g_assert_cmphex(qtest_readb(qts, 0x8c426), ==, 2);
    g_assert_cmphex(qtest_readb(qts, 0x8c428), ==, 1);

    qtest_writew(qts, 0x803fe, 0xa500);
    qtest_writel(qts, 0x80020, 0);
    g_assert_cmphex(qtest_readl(qts, 0x80020), ==, 0x10c10166);
    qtest_quit(qts);
    unlink(path);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/nikon/vr/reset-exception", vr_reset_and_exception);
    qtest_add_func("/nikon/vr/protection-clocks", vr_protection_and_clocks);
    return g_test_run();
}
