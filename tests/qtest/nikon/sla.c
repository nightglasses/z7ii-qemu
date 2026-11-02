/* Synthetic five-component container; no factory firmware is embedded. */
/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "hw/nikon/sla.h"
#include "libqtest-single.h"
#include "fixture.h"
#include "router.h"

void nikon_events_test(QTestState *qts, const char *path, uint64_t base);
void nikon_test_notifier(QTestState *qts);
void nikon_test_counter(QTestState *qts);
void nikon_signal_test(QTestState *qts, const char *path, uint64_t signal,
                       uint64_t configuration, uint64_t lanes);
void nikon_staging_test(QTestState *qts, const char *path, uint64_t base);
void nikon_gate_test(QTestState *qts, const char *path, uint64_t base);
void nikon_status_test(QTestState *qts, uint64_t status, uint64_t system);
void nikon_test_flags(QTestState *qts);
void nikon_pairs_test(QTestState *qts, const char *path,
                      uint64_t first, uint64_t second);

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

static char *fixture_container_size(size_t sla_size)
{
    static const char *const names[] = {
        "ex1840_013000.bi", "_tpj01_v140.bin", "eg1840_mas_01700",
        "vr1840_010100.bi", "eg1840_sla_01700",
    };
    const size_t header = 208;
    size_t total = header + 4 * 6 + sla_size + 2 + 16;
    g_autofree uint8_t *data = g_malloc0(total);
    size_t offset = header;
    char *path = NULL;
    int fd;
    unsigned i;

    memset(data, ' ', 32);
    stl_be_p(data + 32, ARRAY_SIZE(names));
    stl_be_p(data + 36, header);
    for (i = 0; i < ARRAY_SIZE(names); i++) {
        size_t length = i == 4 ? sla_size : 4;
        uint8_t *entry = data + 48 + i * 32;

        memcpy(entry, names[i], strlen(names[i]));
        stl_be_p(entry + 16, offset);
        stl_be_p(entry + 20, length + 2);
        /* Each component differs; selecting MAS instead of SLA fails. */
        stl_le_p(data + offset, 0x51a00000 | i);
        if (i == 4) {
            stl_le_p(data + offset + 0xbcd4, 0x12345678);
            /* Keep the real vector-to-handler source window explicit.  The
             * runtime abort at VA 0x10035074 must be distinguished from a
             * missing loaded ROM byte: this is payload O=0x15074 under the
             * established 0x10020000 image projection. */
            stl_le_p(data + offset + 0x14100, 0xea0003da);
            stl_le_p(data + offset + 0x15074, 0xe59fd004);
            /* Startup copies this 16 KiB ROMD window to the guest-owned
             * level-one table at 0xc18e4000.  Keep a nonzero pattern here so
             * a qtest can prove the copy/read contract without a host alias. */
            for (unsigned word = 0; word < 0x4000 / sizeof(uint32_t); word++) {
                stl_le_p(data + offset + 0x10000 + word * sizeof(uint32_t),
                         0xa1000000 | word);
            }
            stl_le_p(data + offset + 0x83c4d4, 0xabcdef01);
            stl_le_p(data + offset + 0x854240, 0x76543210);
            if (length >= NIKON_SLA_ROM_SIZE) {
                data[offset + NIKON_SLA_ROM_SIZE - 1] = 0x5a;
            }
            for (size_t byte = NIKON_SLA_ROM_SIZE; byte < length; byte++) {
                data[offset + byte] = byte < NIKON_SLA_RESEARCH_APPEND_OFFSET ?
                                      0xff : 0x39 ^ byte ^ (byte >> 8);
            }
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

static char *fixture_container(void)
{
    return fixture_container_size(NIKON_SLA_ROM_SIZE);
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
    /* The handler source is a direct ROMD read at its physical image
     * address.  No alias or clone RAM is involved in this contract. */
    g_assert_cmphex(qtest_readl(qts, NIKON_SLA_ROM_BASE + 0x14100),
                    ==, 0xea0003da);
    g_assert_cmphex(qtest_readl(qts, 0x10035074), ==, 0xe59fd004);

    /* Model the firmware's memcpy(0x10030000, 0xc18e4000, 0x4000) with
     * guest bus transactions.  The destination must be independent writable
     * RAM; it must not be an alias of the ROMD source. */
    {
        g_autofree uint8_t *source = g_malloc(0x4000);
        g_autofree uint8_t *table = g_malloc0(0x4000);

        qtest_memread(qts, 0x10030000, source, 0x4000);
        g_assert_cmphex((uint32_t)ldl_le_p(source), ==, 0xa1000000);
        g_assert_cmphex((uint32_t)ldl_le_p(source + 0x3ffc), ==, 0xa1000fff);
        qtest_memwrite(qts, 0xc18e4000, source, 0x4000);
        qtest_memread(qts, 0xc18e4000, table, 0x4000);
        g_assert_cmpmem(table, 0x4000, source, 0x4000);
        /* A subsequent write proves the owner remains writable and is not
         * backed by the immutable source bytes. */
        stl_le_p(table, 0xfeedc18e);
        qtest_memwrite(qts, 0xc18e4000, table, sizeof(uint32_t));
        g_assert_cmphex(qtest_readl(qts, 0xc18e4000), ==, 0xfeedc18e);
        g_assert_cmphex(qtest_readl(qts, 0x10030000), ==, 0xa1000000);
    }

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
    g_assert_cmphex(qtest_readl(qts, 0xf81020fc), ==, 0x0001043b);
    /* The peer seam follows private GPIO direction and masked latch writes. */
    qtest_irq_intercept_out_named(qts, "/machine/sla", "peer-ready");
    qtest_writel(qts, 0x04282020, 0x04000400);
    g_assert_false(qtest_get_irq(qts, 0));
    qtest_writel(qts, 0x04282120, 0x400);
    g_assert_true(qtest_get_irq(qts, 0));
    qtest_writel(qts, 0x04282020, 0x04000000);
    g_assert_false(qtest_get_irq(qts, 0));
    /* The entire source registry has independent reset and capacity windows. */
    for (unsigned i = 0; i < 11; i++) {
        uint64_t base = 0x2c0a0000 + i * 0x100;
        uint64_t dma = 0x2c020000 + (i % 2) * 0x10000;

        qtest_writel(qts, base + 0x60, 1);
        g_assert_cmpuint(qtest_readl(qts, base + 0x48) & 0x1ff, >, 0);
        qtest_writel(qts, base + 0x60, 0);
        /* Each source TX selector reaches an actual private DMA/FIFO endpoint.
         * EN=0 holds the request; EN=1 permits the single halfword beat. */
        qtest_writew(qts, 0xc1940100, 0x1357 + i);
        qtest_writew(qts, 0xc1940102, 0xaaaa);
        qtest_writel(qts, base + 0xc, 0x420);
        qtest_writel(qts, base + 0x10, 15 << 8);
        qtest_writel(qts, dma, 0x90000000);
        qtest_writel(qts, dma + 0x18, 0xc1940100);
        qtest_writel(qts, dma + 0x1c, base);
        qtest_writel(qts, dma + 0x14, 0x05180000);
        qtest_writel(qts, dma + 0x10, 0x80000000 | ((3 + 2 * i) << 24));
        qtest_writel(qts, dma + 0x28, base + 4);
        qtest_writel(qts, dma + 0x2c, 0xc1940102);
        qtest_writel(qts, dma + 0x24, 0x06180000);
        qtest_writel(qts, dma + 0x20, 0x80000000 | ((4 + 2 * i) << 24));
        qtest_clock_step(qts, 1000);
        g_assert_cmphex(qtest_readl(qts, dma + 0x14) & 0x70000, ==, 0);
        g_assert_cmphex(qtest_readl(qts, base + 0x2c), ==, 0);
        g_assert_cmphex(qtest_readw(qts, 0xc1940102), ==, 0xaaaa);
        qtest_writel(qts, base + 0x3c, 1);
        qtest_clock_step(qts, 1000);
        g_assert_cmphex(qtest_readl(qts, dma + 0x14) & 0x70000, ==, 0x50000);
        /* Ordinary disconnected upstream SSI produces MISO-low after the
         * actual master frame; RX demand then drains its genuine FIFO. */
        qtest_clock_step(qts, 1000);
        qtest_clock_step(qts, 1000);
        g_assert_cmphex(qtest_readl(qts, dma + 0x24) & 0x70000, ==, 0x50000);
        g_assert_cmphex(qtest_readw(qts, 0xc1940102), ==, 0);
        qtest_writel(qts, base + 0x60, 1);
        qtest_writel(qts, dma + 0x10, 0);
        qtest_writel(qts, dma + 0x20, 0);
    }
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
    /* The source-qualified peer-memory class begins at c5000000 and is a
     * valid destination; the following word must be cleared by the transfer. */
    qtest_writel(qts, 0xc1017d80, 0xcccccccc);
    qtest_writel(qts, 0x20070030, 0xc5000000);
    qtest_writel(qts, 0x20070010, 1);
    g_assert_cmphex(qtest_readl(qts, 0x20070008), ==, 1);
    g_assert_cmphex(qtest_readl(qts, 0xc5000000), ==, 0);
    qtest_quit(qts);
    g_assert_cmpint(unlink(path), ==, 0);
}

static uint64_t sla_qom_uint(QTestState *qts, const char *path,
                             const char *property)
{
    QDict *reply = qtest_qmp(qts, "{'execute':'qom-get','arguments':{"
                              "'path':%s,'property':%s}}", path,
                              property);
    uint64_t value = qdict_get_uint(reply, "return");

    qobject_unref(reply);
    return value;
}

static void test_startup_observation(void)
{
    g_autofree char *path = fixture_container();
    QTestState *qts = qtest_initf("-machine nikonsla,boot-prefix=erased "
                                  "-smp 1 -bios %s -accel qtest", path);

    /* The prefix is a declared external input; no bytes are copied into RAM. */
    g_assert_true(qtest_qom_get_bool(qts, "/machine/sla",
                                     "boot-prefix-present"));
    g_assert_cmpuint(sla_qom_uint(qts, "/machine/sla",
                                  "boot-prefix-size"), ==,
                     NIKON_SLA_BOOT_PREFIX_SIZE);

    /* An invalid COPY destination records the first failed transaction with
     * source, destination and residual bytes, rather than hiding the fault. */
    qtest_writel(qts, 0x04251000, 0x10000000);
    qtest_writel(qts, 0x04251010, 3);
    qtest_writel(qts, 0x04251014, NIKON_SLA_ROM_BASE);
    qtest_writel(qts, 0x04251018, 0xc8100000);
    qtest_writel(qts, 0x0425101c, 0x30f00);
    qtest_writel(qts, 0x04251020, 0x30f00);
    qtest_writel(qts, 0x04251024, 0);
    qtest_writel(qts, 0x04251028, 0x11109003);
    qtest_clock_step(qts, 1000);
    g_assert_true(qtest_qom_get_bool(qts, "/machine/sla/copy",
                                     "first-fault"));
    g_assert_cmpuint(sla_qom_uint(qts, "/machine/sla/copy",
                                  "first-fault-channel"), ==, 0);
    g_assert_cmpuint(sla_qom_uint(qts, "/machine/sla/copy",
                                  "first-fault-status"), ==, 1);
    g_assert_cmpuint(sla_qom_uint(qts, "/machine/sla/copy",
                                  "first-fault-source"), ==,
                     NIKON_SLA_ROM_BASE);
    g_assert_cmpuint(sla_qom_uint(qts, "/machine/sla/copy",
                                  "first-fault-destination"), ==,
                     0xc8100000);
    g_assert_cmpuint(sla_qom_uint(qts, "/machine/sla/copy",
                                  "first-fault-remaining"), ==, 4);
    qtest_quit(qts);
    unlink(path);
}

static void test_external_interrupts(void)
{
    g_autofree char *path = fixture_container();
    QTestState *qts = qtest_initf("-machine nikonsla -smp 2 -bios %s", path);

    /* One interception per VM, observing the actual upstream GIC inputs. */
    qtest_irq_intercept_in(qts, "/machine/sla/gic");
    qtest_writel(qts, 0x0426f004, 0);
    qtest_writel(qts, 0x0426f010, 0xff);
    qtest_writel(qts, 0x0426f000, 0xff);
    qtest_writel(qts, 0x0426f014, 0xffff);
    qtest_writel(qts, 0x0426f018, 0xffff);
    qtest_writel(qts, 0x04282114, 0x7f80);
    for (unsigned i = 0; i < 8; i++) {
        unsigned pad = 1U << (7 + i);
        unsigned input = 65 + i - 32;

        qtest_writel(qts, 0x04282014, (pad << 16) | pad);
        /* +0xc is raw pending; +8 excludes the currently masked line. */
        g_assert_cmphex(qtest_readl(qts, 0x0426f00c), ==, 1U << i);
        g_assert_cmphex(qtest_readl(qts, 0x0426f008), ==, 0);
        g_assert_false(qtest_get_irq(qts, input));
        qtest_writel(qts, 0x0426f000, 0xff ^ (1U << i));
        g_assert_cmphex(qtest_readl(qts, 0x0426f008), ==, 1U << i);
        g_assert_true(qtest_get_irq(qts, input));
        qtest_writel(qts, 0x0426f010, 1U << i);
        g_assert_false(qtest_get_irq(qts, input));
        qtest_writel(qts, 0x0426f000, 0xff);
        qtest_writel(qts, 0x04282014, pad << 16);
    }
    qtest_quit(qts);
    g_assert_cmpint(unlink(path), ==, 0);
}

static void test_events(void)
{
    g_autofree char *path = fixture_container();
    QTestState *qts = qtest_initf("-machine nikonsla -smp 2 -bios %s", path);

    nikon_events_test(qts, "/machine/sla/events", 0x2c012000);
    qtest_quit(qts);
    g_assert_cmpint(unlink(path), ==, 0);
}

static void test_notifier(void)
{
    g_autofree char *path = fixture_container();
    QTestState *qts = qtest_initf("-machine nikonsla -smp 2 -bios %s", path);

    nikon_test_notifier(qts);
    qtest_quit(qts);
    g_assert_cmpint(unlink(path), ==, 0);
}

static void test_counter(void)
{
    g_autofree char *path = fixture_container();
    QTestState *qts = qtest_initf("-machine nikonsla -smp 2 -bios %s", path);

    nikon_test_counter(qts);
    qtest_quit(qts);
    g_assert_cmpint(unlink(path), ==, 0);
}

static void test_copy_private(void)
{
    const uint32_t copy = 0x04251000;
    const unsigned length = 131075, backend_size = 16384;
    const unsigned private_length = 4099, source = 0x11, destination = 0x2000;
    g_autofree char *path = fixture_container();
    g_autofree char *backend_path = NULL;
    g_autofree char *memory_path = NULL;
    g_autofree char *contents = NULL;
    g_autofree uint8_t *expected = g_malloc(length);
    g_autofree uint8_t *actual = g_malloc(length);
    g_autofree uint8_t *backend = g_malloc0(backend_size);
    QTestState *qts;
    QDict *response;
    const QListEntry *entry;
    size_t size;
    int fd = g_file_open_tmp("nikon-copy-private-XXXXXX", &backend_path, NULL);

    g_assert_cmpint(fd, >=, 0);
    close(fd);
    for (unsigned i = 0; i < private_length; i++) {
        backend[source + i] = 0x71 ^ i ^ (i >> 8);
    }
    g_assert_true(g_file_set_contents(backend_path, (char *)backend,
                                     backend_size, NULL));
    qts = qtest_initf("-machine nikonsla -smp 2 -bios %s "
                      "-object memory-backend-file,id=private,size=%u,"
                      "mem-path=%s,share=on", path, backend_size, backend_path);
    qtest_irq_intercept_in(qts, "/machine/sla/gic");

    /* Private SLA ROMD and writable RAM retain their actual byte contents. */
    g_assert_cmphex(qtest_readl(qts, NIKON_SLA_ROM_BASE + 0x83c4d4),
                    ==, 0xabcdef01);
    qtest_memread(qts, NIKON_SLA_ROM_BASE + 0x83c4d4, expected, length);
    g_assert_cmphex((uint32_t)ldl_le_p(expected), ==, 0xabcdef01);
    qtest_memset(qts, 0xc1940001, 0, length);
    qtest_writel(qts, copy, 0x10000000);
    qtest_writel(qts, copy + 0x10, length - 1);
    qtest_writel(qts, copy + 0x14, NIKON_SLA_ROM_BASE + 0x83c4d4);
    qtest_writel(qts, copy + 0x18, 0xc1940001);
    qtest_writel(qts, copy + 0x1c, 0x30f00);
    qtest_writel(qts, copy + 0x20, 0x30f00);
    qtest_writel(qts, copy + 0x24, 0);
    qtest_writel(qts, copy + 0x28, 0x9000);
    qtest_writel(qts, copy + 0x28, 0x11109003);
    g_assert_false(qtest_get_irq(qts, 45 - 32));
    g_assert_cmphex(qtest_readl(qts, 0xc1940001), ==, 0);
    qtest_clock_step(qts, (uint64_t)length * 1000000000 / 48000000 + 1);
    qtest_memread(qts, 0xc1940001, actual, length);
    g_assert_cmpmem(actual, length, expected, length);
    g_assert_cmphex(qtest_readl(qts, copy + 0x30), ==, 8);
    g_assert_true(qtest_get_irq(qts, 45 - 32));
    qtest_writel(qts, copy + 0x30, 0);
    g_assert_false(qtest_get_irq(qts, 45 - 32));

    /* A ROMD target fails before any write, preserving source and residual. */
    qtest_writel(qts, copy + 0x10, length - 1);
    qtest_writel(qts, copy + 0x14, 0xc1940001);
    qtest_writel(qts, copy + 0x18, NIKON_SLA_ROM_BASE + 0x83c4d4);
    qtest_writel(qts, copy + 0x28, 0x11109003);
    qtest_clock_step(qts, (uint64_t)length * 1000000000 / 48000000 + 1);
    g_assert_cmphex(qtest_readl(qts, copy + 0x30), ==, 1);
    g_assert_cmphex(qtest_readl(qts, copy + 0x10), ==, length - 1);
    qtest_memread(qts, NIKON_SLA_ROM_BASE + 0x83c4d4, actual, length);
    g_assert_cmpmem(actual, length, expected, length);

    /* Both these DMA addresses are absent from the board's address space.
     * The linked backend must supply the source and receive the real writes. */
    response = qtest_qmp(qts, "{'execute':'qom-list',"
                              " 'arguments':{'path':'/objects/private'}}");
    QLIST_FOREACH_ENTRY(qdict_get_qlist(response, "return"), entry) {
        const QDict *child = qobject_to(QDict, qlist_entry_obj(entry));

        if (!strcmp(qdict_get_str(child, "type"), "child<memory-region>")) {
            g_assert_null(memory_path);
            memory_path = g_strdup_printf("/objects/private/%s",
                                          qdict_get_str(child, "name"));
        }
    }
    qobject_unref(response);
    g_assert_nonnull(memory_path);
    qtest_qmp_assert_success(qts, "{'execute':'qom-set',"
                            " 'arguments':{'path':'/machine/sla/copy',"
                            " 'property':'realized','value':false}}");
    qtest_qmp_assert_success(qts, "{'execute':'qom-set',"
                            " 'arguments':{'path':'/machine/sla/copy',"
                            " 'property':'memory','value':%s}}", memory_path);
    qtest_qmp_assert_success(qts, "{'execute':'qom-set',"
                            " 'arguments':{'path':'/machine/sla/copy',"
                            " 'property':'realized','value':true}}");
    qtest_writel(qts, copy, 0x10000000);
    qtest_writel(qts, copy + 0x10, private_length - 1);
    qtest_writel(qts, copy + 0x14, source);
    qtest_writel(qts, copy + 0x18, destination);
    qtest_writel(qts, copy + 0x1c, 0x30f00);
    qtest_writel(qts, copy + 0x20, 0x30f00);
    qtest_writel(qts, copy + 0x24, 0);
    qtest_writel(qts, copy + 0x28, 0x9000);
    qtest_writel(qts, copy + 0x28, 0x11109003);
    qtest_clock_step(qts, (uint64_t)private_length * 1000000000 / 48000000 + 1);
    g_assert_cmphex(qtest_readl(qts, copy + 0x30), ==, 8);
    g_assert_true(qtest_get_irq(qts, 45 - 32));
    g_assert_true(g_file_get_contents(backend_path, &contents, &size, NULL));
    g_assert_cmpuint(size, ==, backend_size);
    g_assert_cmpmem(contents + destination, private_length,
                    backend + source, private_length);
    g_assert_cmpmem(contents + source, private_length,
                    backend + source, private_length);
    qtest_writel(qts, copy + 0x30, 0);

    /* Unrealize cancels the timer before freeing its private address space. */
    qtest_writel(qts, copy + 0x10, 4095);
    qtest_writel(qts, copy + 0x14, source);
    qtest_writel(qts, copy + 0x18, 0x1000);
    qtest_writel(qts, copy + 0x28, 0x11109003);
    qtest_qmp_assert_success(qts, "{'execute':'qom-set',"
                            " 'arguments':{'path':'/machine/sla/copy',"
                            " 'property':'realized','value':false}}");
    qtest_clock_step(qts, 1000000);
    g_clear_pointer(&contents, g_free);
    g_assert_true(g_file_get_contents(backend_path, &contents, &size, NULL));
    g_assert_cmpmem(contents + 0x1000, 4096, backend + 0x1000, 4096);
    g_assert_false(qtest_qom_get_bool(qts, "/machine/sla/copy",
                                    "channel0-running"));
    response = qtest_qmp(qts, "{'execute':'qom-get',"
                              " 'arguments':{'path':'/machine/sla/copy',"
                              " 'property':'channel0-next-deadline-ns'}}");
    g_assert_cmpuint(qdict_get_uint(response, "return"), ==, 0);
    qobject_unref(response);
    g_assert_false(qtest_get_irq(qts, 45 - 32));
    qtest_quit(qts);
    g_assert_cmpint(unlink(path), ==, 0);
    g_assert_cmpint(unlink(backend_path), ==, 0);
}

static void test_local_code(void)
{
    g_autofree char *path = fixture_container();
    QTestState *qts = qtest_initf("-machine nikonsla -smp 2 -bios %s", path);
    uint64_t last = NIKON_SLA_LOCAL_CODE_BASE + NIKON_SLA_LOCAL_CODE_SIZE - 4;

    /* Allocation has no host clone; both exact end words start cold zero. */
    g_assert_cmphex(qtest_readl(qts, NIKON_SLA_LOCAL_CODE_BASE), ==, 0);
    g_assert_cmphex(qtest_readl(qts, last), ==, 0);
    qtest_writel(qts, NIKON_SLA_LOCAL_CODE_BASE, 0x13579bdf);
    qtest_writel(qts, last, 0x2468ace0);
    qtest_system_reset(qts);
    g_assert_cmphex(qtest_readl(qts, NIKON_SLA_LOCAL_CODE_BASE), ==, 0x13579bdf);
    g_assert_cmphex(qtest_readl(qts, last), ==, 0x2468ace0);
    qtest_writel(qts, NIKON_SLA_LOCAL_CODE_BASE - 4, 0xdeadbeef);
    qtest_writel(qts, last + 4, 0xdeadbeef);
    g_assert_cmphex(qtest_readl(qts, NIKON_SLA_LOCAL_CODE_BASE - 4), ==, 0);
    g_assert_cmphex(qtest_readl(qts, last + 4), ==, 0);
    g_assert_cmphex(qtest_readl(qts, NIKON_SLA_LOCAL_CODE_BASE), ==, 0x13579bdf);
    g_assert_cmphex(qtest_readl(qts, last), ==, 0x2468ace0);
    qtest_quit(qts);
    g_assert_cmpint(unlink(path), ==, 0);
}

static void prefix_copy(QTestState *qts, uint32_t source, uint32_t destination,
                        unsigned length)
{
    const uint32_t copy = 0x04251000;

    qtest_writel(qts, copy, 0x10000000);
    qtest_writel(qts, copy + 0x10, length - 1);
    qtest_writel(qts, copy + 0x14, source);
    qtest_writel(qts, copy + 0x18, destination);
    qtest_writel(qts, copy + 0x1c, 0x30f00);
    qtest_writel(qts, copy + 0x20, 0x30f00);
    qtest_writel(qts, copy + 0x24, 0);
    qtest_writel(qts, copy + 0x28, 0x9000);
    qtest_writel(qts, copy + 0x28, 0x11109003);
    qtest_clock_step(qts, (uint64_t)length * 1000000000 / 48000000 + 1);
}

static void test_research_image_extent(void)
{
    const size_t lengths[] = {
        NIKON_SLA_ROM_SIZE,
        8836412, /* Current C1.70 bench image, including supplied FF gap. */
        NIKON_SLA_RESEARCH_IMAGE_MAX,
    };
    const size_t payload_offset = 208 + 4 * 6;
    const uint32_t destination = NIKON_SLA_LOCAL_CODE_BASE + 0x1000;

    for (unsigned i = 0; i < ARRAY_SIZE(lengths); i++) {
        g_autofree char *path = fixture_container_size(lengths[i]);
        g_autofree char *data = NULL;
        gsize size;
        const uint8_t *payload;
        const uint32_t end = NIKON_SLA_ROM_BASE + lengths[i];
        QTestState *qts = qtest_initf("-machine nikonsla -smp 2 -bios %s", path);

        g_assert_true(g_file_get_contents(path, &data, &size, NULL));
        payload = (const uint8_t *)data + payload_offset;
        g_assert_cmphex(qtest_readl(qts, NIKON_SLA_ROM_BASE), ==, 0x51a00004);
        g_assert_cmphex(qtest_readb(qts, NIKON_SLA_ROM_BASE +
                                   NIKON_SLA_ROM_SIZE - 1), ==, 0x5a);
        g_assert_cmphex(qtest_readb(qts, end - 1), ==, 0x5a);
        if (lengths[i] > NIKON_SLA_ROM_SIZE) {
            const size_t extra = lengths[i] - NIKON_SLA_ROM_SIZE;
            g_autofree uint8_t *actual = g_malloc(extra);

            /* The alignment gap and append are file bytes, not a zero-filled
             * extension or a host-provided ready/program image. */
            qtest_memread(qts, NIKON_SLA_ROM_BASE + NIKON_SLA_ROM_SIZE,
                          actual, extra);
            g_assert_cmpmem(actual, extra, payload + NIKON_SLA_ROM_SIZE, extra);
        }

        prefix_copy(qts, end - 4, destination, 4);
        g_assert_cmphex(qtest_readl(qts, 0x04251030), ==, 8);
        g_assert_cmphex(qtest_readl(qts, destination), ==,
                        (uint32_t)ldl_le_p(payload + lengths[i] - 4));
        qtest_writel(qts, 0x04251030, 0);
        qtest_writel(qts, destination, 0x13579bdf);
        /* The component CRC and page-rounded ROM backing are not visible.
         * A real private-AS DMA read at the first missing byte must fail. */
        prefix_copy(qts, end, destination, 4);
        g_assert_cmphex(qtest_readl(qts, 0x04251030), ==, 1);
        g_assert_cmphex(qtest_readl(qts, 0x04251010), ==, 3);
        g_assert_cmphex(qtest_readl(qts, destination), ==, 0x13579bdf);
        qtest_quit(qts);
        g_assert_cmpint(unlink(path), ==, 0);
    }
}

static void test_research_image_rejected_extent(void)
{
    const size_t lengths[] = {
        NIKON_SLA_ROM_SIZE - 1,
        NIKON_SLA_RESEARCH_IMAGE_MAX + 1,
    };

    for (unsigned i = 0; i < ARRAY_SIZE(lengths); i++) {
        g_autofree char *path = fixture_container_size(lengths[i]);
        g_autofree char *error_output = NULL;
        const char *argv[] = { qtest_qemu_binary(NULL), "-machine", "nikonsla",
                         "-bios", path, "-display", "none", "-monitor", "none",
                         "-serial", "none", "-S", NULL };
        int status;

        /* These are CRC-valid containers: rejection must come from the
         * component extent contract, not a truncated/corrupt bundle. */
        g_assert_true(g_spawn_sync(NULL, (char **)argv, NULL, G_SPAWN_DEFAULT, NULL, NULL,
                                   NULL, &error_output, &status, NULL));
        g_assert_false(g_spawn_check_exit_status(status, NULL));
        g_assert_nonnull(strstr(error_output, "bounded research image extent"));
        g_assert_cmpint(unlink(path), ==, 0);
    }
}

static void test_external_input(void)
{
    const unsigned length = NIKON_SLA_BOOT_PREFIX_SIZE + 32;
    g_autofree char *path = fixture_container();
    g_autofree char *prefix_path = NULL;
    g_autofree uint8_t *prefix = g_malloc(NIKON_SLA_BOOT_PREFIX_SIZE + 1);
    g_autofree uint8_t *expected = g_malloc0(length);
    g_autofree uint8_t *actual = g_malloc(length);
    g_autofree char *file_option = NULL;
    const char *options[3];
    int fd = g_file_open_tmp("nikon-external-prefix-XXXXXX", &prefix_path, NULL);

    g_assert_cmpint(fd, >=, 0);
    close(fd);
    for (unsigned i = 0; i <= NIKON_SLA_BOOT_PREFIX_SIZE; i++) {
        prefix[i] = 0x73 ^ i ^ (i >> 8);
    }
    g_assert_true(g_file_set_contents(prefix_path, (char *)prefix,
                                     NIKON_SLA_BOOT_PREFIX_SIZE, NULL));
    file_option = g_strdup_printf(",boot-prefix=%s", prefix_path);
    options[0] = ""; /* Strict original-package input remains the default. */
    options[1] = ",boot-prefix=erased";
    options[2] = file_option;
    for (unsigned profile = 0; profile < ARRAY_SIZE(options); profile++) {
        QTestState *qts = qtest_initf("-machine nikonsla%s -smp 2 -bios %s",
                                      options[profile], path);

        /* Creating an external source never installs destination clone bytes. */
        qtest_memread(qts, NIKON_SLA_LOCAL_CODE_BASE, actual, length);
        memset(expected, 0, length);
        g_assert_cmpmem(actual, length, expected, length);
        qtest_irq_intercept_in(qts, "/machine/sla/gic");
        prefix_copy(qts, NIKON_SLA_BOOT_PREFIX_BASE,
                    NIKON_SLA_LOCAL_CODE_BASE, length);
        if (!profile) {
            g_assert_cmphex(qtest_readl(qts, 0x04251030), ==, 1);
            g_assert_cmphex(qtest_readl(qts, 0x04251010), ==, length - 1);
            qtest_memread(qts, NIKON_SLA_LOCAL_CODE_BASE, actual, length);
            g_assert_cmpmem(actual, length, expected, length);
        } else {
            if (profile == 1) {
                memset(expected, 0xff, NIKON_SLA_BOOT_PREFIX_SIZE);
            } else {
                memcpy(expected, prefix, NIKON_SLA_BOOT_PREFIX_SIZE);
            }
            stl_le_p(expected + NIKON_SLA_BOOT_PREFIX_SIZE, 0x51a00004);
            qtest_memread(qts, NIKON_SLA_LOCAL_CODE_BASE, actual, length);
            g_assert_cmpmem(actual, length, expected, length);
            g_assert_cmphex(qtest_readl(qts, 0x04251030), ==, 8);
            g_assert_true(qtest_get_irq(qts, 45 - 32));
            qtest_writel(qts, 0x04251030, 0);
            g_assert_false(qtest_get_irq(qts, 45 - 32));

            /* CPU writes and selected-AS DMA writes cannot alter the input. */
            qtest_writel(qts, 0x04264000, 0x29);
            qtest_writel(qts, 0x04264020, 0x153b0902);
            qtest_writel(qts, NIKON_SLA_BOOT_PREFIX_BASE, 0x12345678);
            g_assert_cmphex(qtest_readl(qts, NIKON_SLA_BOOT_PREFIX_BASE), ==,
                            (uint32_t)ldl_le_p(expected));
            qtest_writel(qts, NIKON_SLA_LOCAL_CODE_BASE, 0x12345678);
            prefix_copy(qts, NIKON_SLA_LOCAL_CODE_BASE,
                        NIKON_SLA_BOOT_PREFIX_BASE, 16);
            g_assert_cmphex(qtest_readl(qts, 0x04251030), ==, 1);
            g_assert_cmphex(qtest_readl(qts, 0x04251010), ==, 15);
            qtest_memread(qts, NIKON_SLA_BOOT_PREFIX_BASE, actual,
                          NIKON_SLA_BOOT_PREFIX_SIZE);
            g_assert_cmpmem(actual, NIKON_SLA_BOOT_PREFIX_SIZE,
                            expected, NIKON_SLA_BOOT_PREFIX_SIZE);
            qtest_system_reset(qts);
            g_assert_cmphex(qtest_readl(qts, NIKON_SLA_BOOT_PREFIX_BASE), ==,
                            (uint32_t)ldl_le_p(expected));
        }
        qtest_quit(qts);
    }
    /* Explicit files must have the whole exact extent, with no truncation. */
    for (int delta = -1; delta <= 1; delta += 2) {
        g_autofree char *machine = g_strdup_printf("nikonsla,boot-prefix=%s",
                                                  prefix_path);
        g_autofree char *error_output = NULL;
        char *argv[] = { (char *)qtest_qemu_binary(NULL), "-machine", machine,
                         "-bios", path, "-display", "none", "-monitor", "none",
                         "-serial", "none", "-S", NULL };
        int status;

        g_assert_true(g_file_set_contents(prefix_path, (char *)prefix,
                                         NIKON_SLA_BOOT_PREFIX_SIZE + delta,
                                         NULL));
        g_assert_true(g_spawn_sync(NULL, argv, NULL, G_SPAWN_DEFAULT, NULL, NULL,
                                   NULL, &error_output, &status, NULL));
        g_assert_false(g_spawn_check_wait_status(status, NULL));
        g_assert_nonnull(strstr(error_output, "exactly 128KiB"));
    }
    g_assert_cmpint(unlink(prefix_path), ==, 0);
    g_assert_cmpint(unlink(path), ==, 0);
}

static void test_deferred_copy(void)
{
    const size_t payload_offset = 208 + 4 * 6;
    const size_t length = NIKON_SLA_DEFERRED_SIZE;
    const uint64_t end = NIKON_SLA_DEFERRED_BASE + length;
    g_autofree char *path = fixture_container();
    g_autofree char *data = NULL;
    g_autofree uint8_t *actual = g_malloc(length);
    g_autofree uint8_t *zero = g_malloc0(length);
    gsize size;
    uint8_t *payload, *source;
    QTestState *qts;

    g_assert_true(g_file_get_contents(path, &data, &size, NULL));
    payload = (uint8_t *)data + payload_offset;
    source = payload + (NIKON_SLA_DEFERRED_SOURCE - NIKON_SLA_ROM_BASE);
    /* Synthetic data spanning the complete original runtime call extent. */
    for (size_t i = 0; i < length; i++) {
        source[i] = 0x39 ^ i ^ (i >> 8) ^ (i >> 16);
    }
    stw_be_p(payload + NIKON_SLA_ROM_SIZE,
             fixture_crc(payload, NIKON_SLA_ROM_SIZE));
    stw_be_p(payload + NIKON_SLA_ROM_SIZE + 2,
             fixture_crc((uint8_t *)data,
                         payload_offset + NIKON_SLA_ROM_SIZE + 2));
    g_assert_true(g_file_set_contents(path, data, size, NULL));
    qts = qtest_initf("-machine nikonsla -smp 2 -bios %s", path);
    qtest_irq_intercept_in(qts, "/machine/sla/gic");
    qtest_memread(qts, NIKON_SLA_DEFERRED_BASE, actual, length);
    g_assert_cmpmem(actual, length, zero, length);

    /* The existing controller uses the actual private bus, not a host seed. */
    prefix_copy(qts, NIKON_SLA_DEFERRED_SOURCE, NIKON_SLA_DEFERRED_BASE,
                length);
    g_assert_cmphex(qtest_readl(qts, 0x04251030), ==, 8);
    g_assert_true(qtest_get_irq(qts, 45 - 32));
    qtest_memread(qts, NIKON_SLA_DEFERRED_BASE, actual, length);
    g_assert_cmpmem(actual, length, source, length);
    qtest_system_reset(qts);
    qtest_memread(qts, NIKON_SLA_DEFERRED_BASE, actual, length);
    g_assert_cmpmem(actual, length, source, length);

    /* Neither the COPY2 alignment gap nor an unknown trailing BSS is RAM. */
    for (unsigned i = 0; i < 2; i++) {
        prefix_copy(qts, NIKON_SLA_DEFERRED_SOURCE,
                    i ? end : NIKON_SLA_DEFERRED_BASE - 4, 4);
        g_assert_cmphex(qtest_readl(qts, 0x04251030), ==, 1);
        g_assert_cmphex(qtest_readl(qts, 0x04251010), ==, 3);
        qtest_system_reset(qts);
    }
    qtest_memread(qts, NIKON_SLA_DEFERRED_BASE, actual, length);
    g_assert_cmpmem(actual, length, source, length);
    qtest_quit(qts);
    g_assert_cmpint(unlink(path), ==, 0);
}

static void test_signal(void)
{
    g_autofree char *path = fixture_container();
    QTestState *qts = qtest_initf("-machine nikonsla -bios %s", path);
    static const unsigned bits[] = { 0, 1, 4, 2, 3 };
    unsigned i;

    nikon_signal_test(qts, "/machine/sla/signal", 0x2b000000, 0x28000000,
                       0x2b001000);
    qtest_quit(qts);

    /* A fresh interception observes the actual five-output OR and private GIC. */
    qts = qtest_initf("-machine nikonsla -bios %s", path);
    qtest_irq_intercept_in(qts, "/machine/sla/gic");
    qtest_writel(qts, 0x28000000, 0xf);
    qtest_writel(qts, 0x2b000004, 0x3c);
    g_assert_false(qtest_get_irq(qts, 237 - 32));
    qtest_writel(qts, 0x2b000000, 0x1f);
    qtest_writel(qts, 0x2b00000c, 0x3ff);
    for (i = 0; i < ARRAY_SIZE(bits); i++) {
        qtest_set_irq_in(qts, "/machine/sla/signal", "signal", i, 1);
    }
    g_assert_cmphex(qtest_readl(qts, 0x2b000008), ==, 0x155);
    for (i = 0; i < ARRAY_SIZE(bits); i++) {
        g_assert_true(qtest_get_irq(qts, 237 - 32));
        qtest_writel(qts, 0x2b000008, 1U << (2 * bits[i]));
    }
    g_assert_false(qtest_get_irq(qts, 237 - 32));
    qtest_quit(qts);
    g_assert_cmpint(unlink(path), ==, 0);
}

static void test_staging(void)
{
    g_autofree char *path = fixture_container();
    QTestState *qts = qtest_initf("-machine nikonsla -bios %s", path);

    /* The private host has no drive or endpoint and supplies no link event. */
    g_assert_cmphex(qtest_readl(qts, 0x2b000010), ==, 0);
    nikon_staging_test(qts, "/machine/sla/staging", 0x20202000);
    qtest_quit(qts);
    g_assert_cmpint(unlink(path), ==, 0);
}

static void test_gate(void)
{
    g_autofree char *path = fixture_container();
    QTestState *qts = qtest_initf("-machine nikonsla -bios %s", path);

    nikon_gate_test(qts, "/machine/sla/gate", 0x20100000);
    qtest_quit(qts);
    g_assert_cmpint(unlink(path), ==, 0);
}

static void test_router(void)
{
    g_autofree char *path = fixture_container();
    QTestState *qts = qtest_initf("-machine nikonsla -bios %s", path);

    nikon_router_test(qts, "/machine/sla/router", 0x20060000);
    qtest_quit(qts);
    g_assert_cmpint(unlink(path), ==, 0);
}

static void test_router_requests(void)
{
    g_autofree char *path = fixture_container();
    QTestState *qts = qtest_initf("-machine nikonsla -bios %s", path);

    nikon_router_requests_test(qts, "/machine/sla/router", 0x20060000);
    qtest_quit(qts);
    g_assert_cmpint(unlink(path), ==, 0);
}

static void test_router_timing(void)
{
    g_autofree char *path = fixture_container();
    QTestState *qts = qtest_initf("-machine nikonsla -bios %s "
        "-global nikon-peripheral-event-router.input-period-ns=1000", path);

    nikon_router_timing_test(qts, "/machine/sla/router", 0x20060000);
    qtest_quit(qts);
    g_assert_cmpint(unlink(path), ==, 0);
}

static void test_router_period_rejection(void)
{
    g_autofree char *path = NULL;

    if (g_test_subprocess()) {
        QTestState *qts = qtest_initf("-machine nikonsla -bios %s "
            "-global nikon-peripheral-event-router.input-period-ns=1",
            g_getenv("NIKON_ROUTER_FIXTURE"));

        qtest_quit(qts);
        return;
    }
    path = fixture_container();
    g_setenv("NIKON_ROUTER_FIXTURE", path, true);
    g_test_trap_subprocess(NULL, 5 * G_TIME_SPAN_SECOND, 0);
    g_test_trap_assert_failed();
    g_test_trap_assert_stderr("*input-period-ns must be0 or at least2*");
    g_unsetenv("NIKON_ROUTER_FIXTURE");
    g_assert_cmpint(unlink(path), ==, 0);
}

static void test_status(void)
{
    g_autofree char *path = fixture_container();
    QTestState *qts = qtest_initf("-machine nikonsla -bios %s", path);

    nikon_status_test(qts, 0x24060000, 0x04280000);
    qtest_quit(qts);
    g_assert_cmpint(unlink(path), ==, 0);
}

static void test_flags(void)
{
    g_autofree char *path = fixture_container();
    QTestState *qts = qtest_initf("-machine nikonsla -bios %s", path);

    nikon_test_flags(qts);
    qtest_quit(qts);
    g_assert_cmpint(unlink(path), ==, 0);
}

static void test_pairs(void)
{
    g_autofree char *path = fixture_container();
    QTestState *qts = qtest_initf("-machine nikonsla -bios %s", path);

    nikon_pairs_test(qts, "/machine/sla/pairs", 0x20380000, 0x203c0000);
    qtest_quit(qts);
    g_assert_cmpint(unlink(path), ==, 0);
}

static void test_provider_topology(void)
{
    static const char *const children[] = {
        "pairs", "sampler", "table", "shared", "parameters",
        "results", "lookupbytes", "lookupwords",
    };
    static const struct {
        const char *child;
        const char *input;
        unsigned pin;
        uint64_t base;
        unsigned pending;
        uint32_t bits;
        unsigned irq;
    } event[] = {
        { "pairs", "pending-bit", 0, 0x20380000, 0x24, 1, 99 },
        { "pairs", "pending-bit", 32, 0x203c0000, 0x24, 1, 100 },
        { "sampler", "external-event", 1, 0x20201000, 0x2c, 2, 116 },
        { "table", "external-event", 1, 0x20204000, 0x2c, 0x20000, 121 },
        { "shared", "external-event", 0, 0x20205000, 0x2c, 1, 122 },
        { "parameters", "external-event", 0, 0x20203000, 0x2c, 1, 124 },
    };
    g_autofree char *path = fixture_container();
    QTestState *qts = qtest_initf("-machine nikonsla -bios %s", path);
    unsigned i, j;

    for (i = 0; i < ARRAY_SIZE(children); i++) {
        g_autofree char *child = g_strdup_printf("/machine/sla/%s", children[i]);
        QDict *response = qtest_qmp(qts, "{'execute':'qom-list',"
                                    "'arguments':{'path':%s}}", child);

        g_assert_true(qdict_haskey(response, "return"));
        qobject_unref(response);
    }
    /* One interception for all actual private GIC links in this VM. */
    qtest_irq_intercept_in(qts, "/machine/sla/gic");
    qtest_writel(qts, 0x203800e8, 0);
    qtest_writel(qts, 0x203c00e8, 0);
    for (i = 0; i < ARRAY_SIZE(event); i++) {
        g_autofree char *child = g_strdup_printf("/machine/sla/%s", event[i].child);

        qtest_set_irq_in(qts, child, event[i].input, event[i].pin, 1);
        g_assert_cmphex(qtest_readl(qts, event[i].base + event[i].pending),
                        ==, event[i].bits);
        for (j = 0; j < ARRAY_SIZE(event); j++) {
            g_assert_cmpint(qtest_get_irq(qts, event[j].irq - 32), ==, i == j);
        }
        qtest_writel(qts, event[i].base + event[i].pending, event[i].bits);
        g_assert_false(qtest_get_irq(qts, event[i].irq - 32));
        qtest_set_irq_in(qts, child, event[i].input, event[i].pin, 0);
    }
    /* Typed-only provider families: actual private config, no fake test cause.
     * IRQ105/106/115 injection is covered by their shared units, not this VM. */
    qtest_writel(qts, 0x20200080, 0x89abcdef);
    g_assert_cmphex(qtest_readl(qts, 0x20200080), ==, 0x89abcdef);
    for (i = 0; i < 2; i++) {
        uint64_t base = 0x24073400 - i * 0x1000;

        qtest_writel(qts, base + 0x84, 0x8004);
        g_assert_cmphex(qtest_readl(qts, base + 0x84), ==, 0x8004);
        qtest_writel(qts, base, 0x4000);
        g_assert_cmphex(qtest_readl(qts, base), ==, 0x4000);
    }
    qtest_writeb(qts, 0x24073000, 1);
    g_assert_cmphex(qtest_readb(qts, 0x24073000), ==, 1);
    qtest_writel(qts, 0x24072000, 1);
    g_assert_cmphex(qtest_readl(qts, 0x24072000), ==, 1);
    qtest_quit(qts);
    g_assert_cmpint(unlink(path), ==, 0);
}

static void test_config_links(void)
{
    const uint32_t config = 0x20010000, mxic = 0x20016000;
    g_autofree char *path = fixture_container();
    QTestState *qts = qtest_initf("-machine nikonsla -bios %s", path);
    uint32_t active = qtest_readl(qts, mxic + 0x20);
    uint32_t draft = active ^ 0x10000;

    qtest_irq_intercept_in(qts, "/machine/sla/gic");
    /* The original config view selector selects the actual MXIC table view. */
    qtest_writel(qts, mxic + 0x20, draft);
    g_assert_cmphex(qtest_readl(qts, mxic + 0x20), ==, draft);
    qtest_writel(qts, config + 0x44, 1);
    g_assert_cmphex(qtest_readl(qts, mxic + 0x20), ==, active);
    qtest_writel(qts, config + 0x44, 0);
    g_assert_cmphex(qtest_readl(qts, mxic + 0x20), ==, draft);

    qtest_writel(qts, config + 0xc, 1);
    qtest_writel(qts, mxic + 0x808, 1);
    qtest_writel(qts, config + 0x34, 1);
    qtest_writel(qts, mxic + 0x82c, 1);
    g_assert_false(qtest_get_irq(qts, 128 - 32));
    qtest_clock_step(qts, 9999);
    g_assert_false(qtest_get_irq(qts, 128 - 32));
    qtest_clock_step(qts, 1);
    g_assert_cmphex(qtest_readl(qts, config + 0x10), ==, 1);
    g_assert_cmphex(qtest_readl(qts, mxic + 0x80c), ==, 1);
    g_assert_true(qtest_get_irq(qts, 128 - 32));
    /* ACKing one real cause cannot discard the other group's pending IRQ. */
    qtest_writel(qts, config + 0x10, 1);
    g_assert_true(qtest_get_irq(qts, 128 - 32));
    qtest_writel(qts, mxic + 0x80c, 1);
    g_assert_false(qtest_get_irq(qts, 128 - 32));
    qtest_quit(qts);
    g_assert_cmpint(unlink(path), ==, 0);
}

static void test_secure_tick(void)
{
    /* Self-written ARM fixture, not Nikon instructions.  It configures the
     * actual secure timer and GIC, including the original active-view RMW.
     * The IRQ vector records IAR29, disables the timer, and performs EOI. */
    static const uint32_t program[] = {
        0xf10c00c0, /* cpsid if */
        0xe59f0074, /* ldr r0, =GICD */
        0xe3a01001, /* mov r1, #1 */
        0xe5801000, /* str r1, [r0] */
        0xe3a01202, /* mov r1, #1 << 29 */
        0xe5801100, /* str r1, [r0, #0x100] */
        0xe5902300, /* ldr r2, [r0, #0x300] */
        0xe3822202, /* orr r2, r2, #1 << 29 */
        0xe5802300, /* str r2, [r0, #0x300] */
        0xe5902300, /* ldr r2, [r0, #0x300] */
        0xe59f3058, /* ldr r3, =observations */
        0xe5832004, /* str r2, [r3, #4] */
        0xe5801280, /* str r1, [r0, #0x280] */
        0xe59f0048, /* ldr r0, =GICC */
        0xe3a010ff, /* mov r1, #0xff */
        0xe5801004, /* str r1, [r0, #4] */
        0xe3a01001, /* mov r1, #1 */
        0xe5801000, /* str r1, [r0] */
        0xe59f003c, /* ldr r0, =vectors */
        0xee0c0f10, /* mcr p15, 0, r0, c12, c0, 0: VBAR */
        0xe3a00000, /* mov r0, #0 */
        0xee0e0f32, /* mcr p15, 0, r0, c14, c2, 1: secure CTL */
        0xe30b0b80, /* movw r0, #48000 */
        0xee0e0f12, /* mcr p15, 0, r0, c14, c2, 0: secure TVAL */
        0xe3a00001, /* mov r0, #1 */
        0xee0e0f32, /* enable secure timer */
        0xf57ff06f, /* isb */
        0xf1080080, /* cpsie i */
        0xe320f003, /* wfi */
        0xeafffffd, /* b preceding wfi */
        0xe320f000, 0xe320f000,
        0xf8101000, 0xf8102000, 0xc1940000, 0x10020000,
    };
    static const uint32_t handler[] = {
        0xe59f0028, /* ldr r0, =GICC */
        0xe590100c, /* ldr r1, [r0, #0xc]: IAR */
        0xe59f2024, /* ldr r2, =observations */
        0xe5821000, /* record actual acknowledged IRQ */
        0xe3a03000,
        0xee0e3f32, /* disable secure timer */
        0xe5801010, /* str r1, [r0, #0x10]: EOI */
        0xe3a03001,
        0xe5823008, /* record completed handler */
        0xeafffffe,
        0xe320f000, 0xe320f000,
        0xf8102000, 0xc1940000,
    };
    const size_t payload_offset = 208 + 4 * 6;
    g_autofree char *path = fixture_container();
    g_autofree char *data = NULL;
    gsize size;
    uint8_t *payload;
    QTestState *qts;
    int64_t deadline;
    unsigned i;

    g_assert_true(g_file_get_contents(path, &data, &size, NULL));
    payload = (uint8_t *)data + payload_offset;
    for (i = 0; i < ARRAY_SIZE(program); i++) {
        stl_le_p(payload + 0xbcd4 + i * 4, program[i]);
    }
    for (i = 0; i < ARRAY_SIZE(handler); i++) {
        stl_le_p(payload + 0xbe00 + i * 4, handler[i]);
    }
    /* IRQ vector branches from VA10020018 to our synthetic handler1002BE00. */
    stl_le_p(payload + 0x18, 0xea002f78);
    stw_be_p(payload + NIKON_SLA_ROM_SIZE,
             fixture_crc(payload, NIKON_SLA_ROM_SIZE));
    stw_be_p(payload + NIKON_SLA_ROM_SIZE + 2,
             fixture_crc((uint8_t *)data,
                         payload_offset + NIKON_SLA_ROM_SIZE + 2));
    g_assert_true(g_file_set_contents(path, data, size, NULL));
    qts = qtest_initf("-machine nikonsla -smp 1 -bios %s -accel tcg -S", path);
    qtest_qmp_assert_success(qts, "{'execute':'cont'}");
    deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
    while (!qtest_readl(qts, 0xc1940008) &&
           g_get_monotonic_time() < deadline) {
        g_usleep(1000);
    }
    qtest_qmp_assert_success(qts, "{'execute':'stop'}");
    g_assert_cmphex(qtest_readl(qts, 0xc1940004), ==, 0);
    g_assert_cmphex(qtest_readl(qts, 0xc1940000), ==, 29);
    g_assert_cmphex(qtest_readl(qts, 0xc1940008), ==, 1);
    qtest_quit(qts);
    unlink(path);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/nikon/sla/external-input", test_external_input);
    qtest_add_func("/nikon/sla/loaded-domain", test_loaded_domain);
    qtest_add_func("/nikon/sla/research-image-extent", test_research_image_extent);
    qtest_add_func("/nikon/sla/research-image-rejected-extent",
                   test_research_image_rejected_extent);
    qtest_add_func("/nikon/sla/startup-observation", test_startup_observation);
    qtest_add_func("/nikon/sla/copy-private", test_copy_private);
    qtest_add_func("/nikon/sla/external-interrupts", test_external_interrupts);
    qtest_add_func("/nikon/sla/events", test_events);
    qtest_add_func("/nikon/sla/notifier", test_notifier);
    qtest_add_func("/nikon/sla/counter", test_counter);
    qtest_add_func("/nikon/sla/local-code", test_local_code);
    qtest_add_func("/nikon/sla/signal", test_signal);
    qtest_add_func("/nikon/sla/staging", test_staging);
    qtest_add_func("/nikon/sla/router", test_router);
    qtest_add_func("/nikon/sla/router-requests", test_router_requests);
    qtest_add_func("/nikon/sla/router-timing", test_router_timing);
    qtest_add_func("/nikon/sla/router-period-rejection",
                   test_router_period_rejection);
    qtest_add_func("/nikon/sla/gate", test_gate);
    qtest_add_func("/nikon/sla/status", test_status);
    qtest_add_func("/nikon/sla/flags", test_flags);
    qtest_add_func("/nikon/sla/config-links", test_config_links);
    qtest_add_func("/nikon/sla/provider-topology", test_provider_topology);
    qtest_add_func("/nikon/sla/pairs", test_pairs);
    qtest_add_func("/nikon/sla/secure-tick", test_secure_tick);
    qtest_add_func("/nikon/sla/deferred-copy", test_deferred_copy);
    return g_test_run();
}
