/* Synthetic five-component container; no factory firmware is embedded. */
/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "hw/nikon/sla.h"
#include "libqtest-single.h"

static uint16_t fixture_crc(const uint8_t *data, size_t size)
{
    uint16_t crc = 0;
    size_t i;
    unsigned bit;

    for (i = 0; i < size; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (bit = 0; bit < 8; bit++) {
            crc = (crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0);
        }
    }
    return crc;
}

static char *fixture_container(void)
{
    static const char *const names[] = {
        "ex1840_013000.bi", "_tpj01_v140.bin", "eg1840_mas_01700",
        "vr1840_010100.bi", "eg1840_sla_01700",
    };
    const size_t header = 208;
    size_t total = header + 4 * 6 + NIKON_SLA_ROM_SIZE + 2 + 16;
    g_autofree uint8_t *data = g_malloc0(total);
    size_t offset = header;
    char *path = NULL;
    int fd;
    unsigned i;

    memset(data, ' ', 32);
    stl_be_p(data + 32, ARRAY_SIZE(names));
    stl_be_p(data + 36, header);
    for (i = 0; i < ARRAY_SIZE(names); i++) {
        size_t length = i == 4 ? NIKON_SLA_ROM_SIZE : 4;
        uint8_t *entry = data + 48 + i * 32;

        memcpy(entry, names[i], strlen(names[i]));
        stl_be_p(entry + 16, offset);
        stl_be_p(entry + 20, length + 2);
        /* Each component differs; selecting MAS instead of SLA fails. */
        stl_le_p(data + offset, 0x51a00000 | i);
        if (i == 4) {
            stl_le_p(data + offset + 0xbcd4, 0x12345678);
            stl_le_p(data + offset + 0x83c4d4, 0xabcdef01);
            stl_le_p(data + offset + 0x854240, 0x76543210);
            data[offset + length - 1] = 0x5a;
        }
        stw_be_p(data + offset + length, fixture_crc(data + offset, length));
        offset += length + 2;
    }
    stw_be_p(data + offset, fixture_crc(data, offset));
    fd = g_file_open_tmp("nikon-sla-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    g_assert_true(g_file_set_contents(path, (const char *)data, total, NULL));
    return path;
}

static void test_loaded_domain(void)
{
    g_autofree char *path = fixture_container();
    QTestState *qts = qtest_initf("-machine nikonsla -smp 2 -bios %s", path);

    g_assert_cmphex(qtest_readl(qts, NIKON_SLA_ROM_BASE), ==, 0x51a00004);
    g_assert_cmphex(qtest_readl(qts, NIKON_SLA_ENTRY), ==, 0x12345678);
    g_assert_cmphex(qtest_readb(qts, NIKON_SLA_ROM_BASE +
                               NIKON_SLA_ROM_SIZE - 1), ==, 0x5a);
    /* Source content is loaded, but neither COPY target nor release is seeded. */
    g_assert_cmphex(qtest_readl(qts, NIKON_SLA_ROM_BASE + 0x83c4d4),
                    ==, 0xabcdef01);

    /* The guest owns metadata at both ends of its source-declared arena. */
    g_assert_cmphex(qtest_readl(qts, 0xc1940000), ==, 0);
    g_assert_cmphex(qtest_readl(qts, 0xc4fffffc), ==, 0);
    qtest_writel(qts, 0xc1940000, 0x19400000);
    qtest_writel(qts, 0xc4fffffc, 0x4fffffc0);
    g_assert_cmphex(qtest_readl(qts, 0xc1940000), ==, 0x19400000);
    g_assert_cmphex(qtest_readl(qts, 0xc4fffffc), ==, 0x4fffffc0);

    /* The runtime counter is driven by virtual time and can be stopped. */
    qtest_writel(qts, 0x04266000, 0);
    qtest_writel(qts, 0x04266008, 0);
    qtest_writel(qts, 0x0426600c, 0);
    qtest_writel(qts, 0x04266010, 48000000);
    qtest_writel(qts, 0x04266000, 3);
    qtest_clock_step(qts, 1000000);
    g_assert_cmphex(qtest_readl(qts, 0x04266008), ==, 48000);
    qtest_writel(qts, 0x04266000, 0);
    qtest_clock_step(qts, 1000000);
    g_assert_cmphex(qtest_readl(qts, 0x04266008), ==, 48000);
    g_assert_cmphex(qtest_readb(qts, 0x2c090003) & 2, ==, 2);
    g_assert_cmphex(qtest_readl(qts, 0xf81020fc), ==, 0x0002043b);
    /* The peer seam follows private GPIO direction and masked latch writes. */
    qtest_irq_intercept_out_named(qts, "/machine/sla", "peer-ready");
    qtest_writel(qts, 0x04282020, 0x04000400);
    g_assert_false(qtest_get_irq(qts, 0));
    qtest_writel(qts, 0x04282120, 0x400);
    g_assert_true(qtest_get_irq(qts, 0));
    qtest_writel(qts, 0x04282020, 0x04000000);
    g_assert_false(qtest_get_irq(qts, 0));
    /* Complete source domain4 request applies state before its done flag. */
    qtest_writel(qts, 0x04287000, 0x4100);
    qtest_writel(qts, 0x04287010, 0xa1);
    qtest_clock_step(qts, 1);
    g_assert_cmphex(qtest_readl(qts, 0x04287004), ==, 1);
    g_assert_cmphex(qtest_readl(qts, 0x0428700c), ==, 0x10);
    qtest_writel(qts, 0x04287004, 1);
    qtest_writel(qts, 0x04287000, 0x4000);
    qtest_writel(qts, 0x04287010, 0xa1);
    qtest_clock_step(qts, 1);
    g_assert_cmphex(qtest_readl(qts, 0x04287004), ==, 1);
    g_assert_cmphex(qtest_readl(qts, 0x0428700c), ==, 0);
    qtest_writel(qts, 0x04287004, 1);
    g_assert_cmphex(qtest_readl(qts, 0xc60f0000), ==, 0);
    g_assert_cmphex(qtest_readl(qts, 0xc60fc614), ==, 0);
    qtest_writel(qts, 0xc60fc614, 0x12345678);
    g_assert_cmphex(qtest_readl(qts, 0xc60fc614), ==, 0x12345678);
    g_assert_cmphex(qtest_readl(qts, NIKON_SLA_ROM_BASE + 0x854240),
                    ==, 0x76543210);
    g_assert_cmphex(qtest_readl(qts, NIKON_SLA_COPY1_BASE), ==, 0);
    g_assert_cmphex(qtest_readl(qts, NIKON_SLA_COPY2_BASE), ==, 0);
    g_assert_cmphex(qtest_readl(qts, 0x04008ffc), ==, 0);
    qtest_writel(qts, NIKON_SLA_COPY1_BASE, 0xdeadbeef);
    g_assert_cmphex(qtest_readl(qts, NIKON_SLA_COPY1_BASE), ==, 0xdeadbeef);
    g_assert_cmphex(qtest_readl(qts, NIKON_SLA_ROM_BASE + 0x83c4d4),
                    ==, 0xabcdef01);

    /* Exercise the real fill DMA against SLA RAM; completion follows bus work. */
    qtest_writel(qts, 0xc1017d80, 0xcccccccc);
    qtest_writel(qts, 0x20070000, 0);
    qtest_writel(qts, 0x20070014, 1);
    qtest_writel(qts, 0x20070020, 0x4000);
    qtest_writel(qts, 0x20070024, 0x4000);
    qtest_writel(qts, 0x20070028, 1);
    qtest_writel(qts, 0x20070018, 0);
    qtest_writel(qts, 0x20070004, 0);
    qtest_writel(qts, 0x20070030, 0xc1017d80);
    qtest_writel(qts, 0x20070010, 1);
    g_assert_cmphex(qtest_readl(qts, 0x20070008), ==, 1);
    g_assert_cmphex(qtest_readl(qts, 0xc1017d80), ==, 0);
    qtest_writel(qts, 0x20070008, 1);
    g_assert_cmphex(qtest_readl(qts, 0x20070008), ==, 0);
    /* A destination beyond declared SLA RAM cannot yield successful DMA. */
    qtest_writel(qts, 0xc1017d80, 0xcccccccc);
    qtest_writel(qts, 0x20070030, 0xc5000000);
    qtest_writel(qts, 0x20070010, 1);
    g_assert_cmphex(qtest_readl(qts, 0x20070008), ==, 0x10);
    g_assert_cmphex(qtest_readl(qts, 0xc1017d80), ==, 0xcccccccc);
    qtest_quit(qts);
    g_assert_cmpint(unlink(path), ==, 0);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/nikon/sla/loaded-domain", test_loaded_domain);
    return g_test_run();
}
