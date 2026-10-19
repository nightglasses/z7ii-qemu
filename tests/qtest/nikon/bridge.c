/* Explicit disconnected bridge inputs, not a synthetic camera peer.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "qemu/crc-ccitt.h"
#include "hw/core/sysbus.h"
#include "hw/nikon/devices.h"
#include "hw/nikon/sla.h"
#include "hw/nikon/ex.h"
#include "hw/nikon/input.h"
#include "qobject/qlist.h"
#include "../libqtest.h"
#include "bridge.h"

#define BRIDGE 0x20130000
#define RAW (BRIDGE + 0xd020)
#define MASK (BRIDGE + 0xd024)
#define ACK (BRIDGE + 0xd028)
#define RESET (BRIDGE + 0xc5e0)
#define READY (BRIDGE + 0xc908)

static void event(QTestState *q, const char *path, unsigned input, int level)
{
    qtest_set_irq_in(q, path, "peer-event", input, level);
}

static void test_edges(QTestState *q, const char *path)
{
    /* Original ISR's conditional flag8 path tests raw bit4 (LSLS #27). */
    static const unsigned bits[] = { 0, 2, 4 };
    unsigned i;

    g_assert_cmphex(qtest_readl(q, RAW), ==, 0);
    g_assert_cmphex(qtest_readl(q, READY), ==, 0);
    qtest_clock_step(q, 1000000);
    g_assert_cmphex(qtest_readl(q, RAW), ==, 0);
    for (i = 0; i < ARRAY_SIZE(bits); i++) {
        unsigned bit = 1U << bits[i];

        qtest_writel(q, MASK, 0);
        event(q, path, i, 1);
        g_assert_cmphex(qtest_readl(q, RAW), ==, bit);
        g_assert_false(qtest_get_irq(q, 0));
        qtest_writel(q, MASK, bit);
        g_assert_true(qtest_get_irq(q, 0));
        /* A different ACK neither consumes nor hides this pending edge. */
        qtest_writel(q, ACK, 0x40);
        g_assert_cmphex(qtest_readl(q, RAW), ==, bit);
        qtest_writel(q, ACK, bit);
        g_assert_false(qtest_get_irq(q, 0));
        event(q, path, i, 1);
        g_assert_cmphex(qtest_readl(q, RAW), ==, 0);
        event(q, path, i, 0);
        event(q, path, i, 1);
        g_assert_cmphex(qtest_readl(q, RAW), ==, bit);
        qtest_writel(q, ACK, bit);
        event(q, path, i, 0);
    }
}

static void test_ready(QTestState *q, const char *path)
{
    qtest_writel(q, MASK, 0xffff);
    qtest_set_irq_in(q, path, "peer-ready", 0, 1);
    g_assert_cmphex(qtest_readl(q, READY), ==, 4);
    g_assert_cmphex(qtest_readl(q, BRIDGE + 0x4908), ==, 0);
    g_assert_cmphex(qtest_readl(q, RAW), ==, 0);
    g_assert_false(qtest_get_irq(q, 0));
    event(q, path, 2, 1);
    qtest_writel(q, ACK, 0x10);
    g_assert_cmphex(qtest_readl(q, READY), ==, 4);
    /* The source-known bit2 acknowledgement is not a readiness producer. */
    qtest_writel(q, BRIDGE + 0x829c, 1);
    qtest_set_irq_in(q, path, "peer-ready", 0, 0);
    g_assert_cmphex(qtest_readl(q, READY), ==, 0);
    g_assert_cmphex(qtest_readl(q, RAW), ==, 0);
    event(q, path, 2, 0);
}

static void test_reset(QTestState *q, const char *path)
{
    qtest_writel(q, MASK, 0xffff);
    event(q, path, 0, 1);
    g_assert_true(qtest_get_irq(q, 0));
    qtest_writel(q, RESET, 0xf000f);
    g_assert_cmphex(qtest_readl(q, RAW), ==, 0);
    g_assert_false(qtest_get_irq(q, 0));
    event(q, path, 1, 1);
    qtest_writel(q, RESET, 0);
    event(q, path, 0, 1);
    event(q, path, 1, 1);
    g_assert_cmphex(qtest_readl(q, RAW), ==, 0);
    event(q, path, 1, 0);
    event(q, path, 1, 1);
    g_assert_cmphex(qtest_readl(q, RAW), ==, 4);
    qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
    g_assert_cmphex(qtest_readl(q, RAW), ==, 0);
    qtest_writel(q, MASK, 0xffff);
    event(q, path, 1, 1);
    g_assert_cmphex(qtest_readl(q, RAW), ==, 0);
    event(q, path, 1, 0);
    event(q, path, 1, 1);
    g_assert_cmphex(qtest_readl(q, RAW), ==, 4);
    event(q, path, 0, 0);
    event(q, path, 1, 0);
}

static void test_configuration(QTestState *q)
{
    unsigned bank, context, i;

    /* Full source-defined word ports, not one sampled initialization value. */
    for (bank = 0; bank < 2; bank++) {
        uint64_t base = BRIDGE + bank * 0x8000;

        for (context = 0; context < 4; context++) {
            uint64_t lane = base + context * 0x400;

            qtest_writel(q, lane + 0x88, 0x100 + bank * 4 + context);
            qtest_writel(q, lane + 0x8c, 0x200 + bank * 4 + context);
            qtest_writel(q, base + 0x45f0, 0x10000U << context);
            for (i = 0; i < 2; i++) {
                qtest_writel(q, base + 0x1c8 + i * 4,
                             0x12340000 + context * 2 + i);
                g_assert_cmphex(qtest_readl(q, base + 0x1c8 + i * 4), ==,
                                0x12340000 + context * 2 + i);
            }
        }
        qtest_writel(q, base + 0x45f0, 0);
        qtest_writel(q, base + 0x4128, 0xf);
        for (context = 0; context < 4; context++) {
            g_assert_cmphex(qtest_readl(q, base + context * 0x400 + 0x104),
                            ==, 1);
        }
    }
    qtest_clock_step(q, 10000);
    for (bank = 0; bank < 2; bank++) {
        for (context = 0; context < 4; context++) {
            g_assert_cmphex(qtest_readl(q, BRIDGE + bank * 0x8000 +
                                       context * 0x400 + 0x104), ==, 3);
        }
    }
    /* Mapping publication is forty independent words; it has no IRQ. */
    for (i = 0; i < 40; i++) {
        qtest_writel(q, BRIDGE + 0xd0b0 + i * 4, 0x34560000 + i);
        g_assert_cmphex(qtest_readl(q, BRIDGE + 0xd0b0 + i * 4), ==,
                        0x34560000 + i);
    }
    g_assert_cmphex(qtest_readl(q, RAW), ==, 0);
    qtest_writel(q, RESET, 0xf000f);
    qtest_clock_step(q, 10000);
    qtest_writel(q, RESET, 0);
    for (i = 0; i < 40; i++) {
        g_assert_cmphex(qtest_readl(q, BRIDGE + 0xd0b0 + i * 4), ==, 0);
    }
}

static void test_requests(QTestState *q, bool loopback)
{
    unsigned lane;

    qtest_writel(q, MASK, 0xffff);
    qtest_writel(q, BRIDGE + 0xc5f4, 0x1b);
    qtest_writel(q, BRIDGE + 0xc5f8, 0x1b);
    for (lane = 0; lane < 8; lane++) {
        uint64_t base = BRIDGE + 0x8000 + (lane & 3) * 0x400 +
                        (lane >= 4 ? 0x200 : 0);

        qtest_writel(q, base + 0x88, 2);
        qtest_writel(q, base + 0x8c, 3);
    }
    qtest_writel(q, BRIDGE + 0xc128, 0xf00f);
    qtest_clock_step(q, 10000);
    g_assert_cmphex(qtest_readl(q, RAW), ==, 0);
    qtest_writel(q, BRIDGE + 0xc90c, 6);
    g_assert_cmphex(qtest_readl(q, BRIDGE + 0x4908), ==, 0);
    g_assert_cmphex(qtest_readl(q, RAW), ==, 0);
    qtest_clock_step(q, 10000);
    g_assert_cmphex(qtest_readl(q, BRIDGE + 0x4908), ==, 1);
    g_assert_cmphex(qtest_readl(q, RAW), ==, loopback ? 3 : 2);
    g_assert_true(qtest_get_irq(q, 0));
    qtest_writel(q, ACK, loopback ? 3 : 2);
    /* Mode1 command4 completes locally without sending another start. */
    qtest_writel(q, BRIDGE + 0xc90c, 4);
    qtest_clock_step(q, 10000);
    g_assert_cmphex(qtest_readl(q, RAW), ==, 2);
    qtest_writel(q, ACK, 2);
    qtest_writel(q, BRIDGE + 0xc13c, 2);
    g_assert_cmphex(qtest_readl(q, RAW), ==, 0);
    qtest_clock_step(q, 10000);
    /* Original MAS/SLA ISR: LSLS raw,#26; BPL skips the flag4 setter.
     * Configuration therefore uses raw bit5, not unconsumed bit6.
     */
    g_assert_cmphex(qtest_readl(q, RAW), ==, loopback ? 0x21 : 0x20);
    qtest_writel(q, ACK, loopback ? 0x21 : 0x20);

    /* Pending control and configuration completion must both be cancelled. */
    qtest_writel(q, BRIDGE + 0xc90c, 6);
    qtest_writel(q, BRIDGE + 0xc13c, 2);
    qtest_writel(q, RESET, 0xf000f);
    qtest_clock_step(q, 1000000);
    qtest_writel(q, RESET, 0);
    g_assert_cmphex(qtest_readl(q, RAW), ==, 0);
    g_assert_cmphex(qtest_readl(q, BRIDGE + 0x4908), ==, 0);
    g_assert_false(qtest_get_irq(q, 0));
}

static void test_control_input(QTestState *q, const char *path)
{
    qtest_writel(q, MASK, 0);
    qtest_set_irq_in(q, path, "peer-control", 1, 1);
    g_assert_cmphex(qtest_readl(q, RAW), ==, 0x20);
    g_assert_false(qtest_get_irq(q, 0));
    qtest_writel(q, MASK, 0x20);
    g_assert_true(qtest_get_irq(q, 0));
    qtest_writel(q, ACK, 0x20);
    qtest_set_irq_in(q, path, "peer-control", 1, 1);
    g_assert_cmphex(qtest_readl(q, RAW), ==, 0);
    qtest_writel(q, RESET, 0xf000f);
    qtest_writel(q, RESET, 0);
    qtest_set_irq_in(q, path, "peer-control", 1, 1);
    g_assert_cmphex(qtest_readl(q, RAW), ==, 0);
    qtest_set_irq_in(q, path, "peer-control", 1, 0);
    qtest_set_irq_in(q, path, "peer-control", 1, 1);
    g_assert_cmphex(qtest_readl(q, RAW), ==, 0x20);
    qtest_set_irq_in(q, path, "peer-control", 1, 0);
}

/* A real private SLA provider with synthetic input, never original code. */
static char *peer_bundle(void)
{
    static const char *const names[] = {
        "ex1840_013000.bi", "_tpj01_v140.bin", "eg1840_mas_01700",
        "vr1840_010100.bi", "eg1840_sla_01700",
    };
    const size_t header = 208;
    const size_t total = header + 3 * 6 + 0x80000 + 2 +
                         NIKON_SLA_ROM_SIZE + 2 + 16;
    g_autofree uint8_t *data = g_malloc0(total);
    char *path = NULL;
    size_t offset = header;
    unsigned i;
    int fd;

    memset(data, ' ', 32);
    stl_be_p(data + 32, ARRAY_SIZE(names));
    stl_be_p(data + 36, header);
    for (i = 0; i < ARRAY_SIZE(names); i++) {
        size_t length = i == 0 ? 0x80000 :
                       i == 4 ? NIKON_SLA_ROM_SIZE : 4;
        uint8_t *entry = data + 48 + i * 32;

        memcpy(entry, names[i], strlen(names[i]));
        stl_be_p(entry + 16, offset);
        stl_be_p(entry + 20, length + 2);
        if (i == 0) {
            /* Valid finite EX vectors; qtest executes no synthetic code. */
            stl_le_p(data + offset, 0x20040000);
            stl_le_p(data + offset + 4, 0x08000009);
        } else {
            stl_le_p(data + offset, 0xeafffffe);
        }
        stw_be_p(data + offset + length,
                 crc_ccitt_false(0, data + offset, length));
        offset += length + 2;
    }
    stw_be_p(data + offset, crc_ccitt_false(0, data, offset));
    fd = g_file_open_tmp("nikon-bridge-XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    g_assert_true(g_file_set_contents(path, (const char *)data, total, NULL));
    return path;
}

static void test_combined_domains(void)
{
    g_autofree char *bundle = peer_bundle();
    g_autofree char *quoted = g_shell_quote(bundle);
    QTestState *q = qtest_initf("-M z7ii-mas-diagnostic,bundle=%s "
                              "-smp cpus=2,maxcpus=5 -nodefaults", quoted);
    QDict *reply = qtest_qmp(q, "{'execute':'query-cpus-fast'}");

    g_assert_cmpuint(qlist_size(qdict_get_qlist(reply, "return")), ==, 5);
    qobject_unref(reply);
    reply = qtest_qmp(q, "{'execute':'qom-get','arguments':{"
                         "'path':'/machine/ex','property':'type'}}");
    g_assert_cmpstr(qdict_get_str(reply, "return"), ==, TYPE_NIKON_EX);
    qobject_unref(reply);
    reply = qtest_qmp(q, "{'execute':'qom-get','arguments':{"
                         "'path':'/machine/input','property':'type'}}");
    g_assert_cmpstr(qdict_get_str(reply, "return"), ==, TYPE_NIKON_INPUT);
    qobject_unref(reply);
    reply = qtest_qmp(q, "{'execute':'qom-get','arguments':{"
                         "'path':'/machine/bridge','property':'notification'}}");
    g_assert_true(qdict_get_bool(reply, "return"));
    qobject_unref(reply);
    reply = qtest_qmp(q, "{'execute':'qom-get','arguments':{"
                         "'path':'/machine/sla/bridge','property':'notification'}}");
    g_assert_true(qdict_get_bool(reply, "return"));
    qobject_unref(reply);
    reply = qtest_qmp(q, "{'execute':'qom-get','arguments':{"
                         "'path':'/machine/sla','property':'boot-prefix-present'}}");
    g_assert_true(qdict_get_bool(reply, "return"));
    qobject_unref(reply);
    /* EX private ROM cannot overwrite MAS's original ROM address space. */
    g_assert_cmphex(qtest_readl(q, 0x10020000), ==, 0xeafffffe);
    g_assert_cmphex(qtest_readl(q, 0x10000000), ==, 0xffffffff);
    qtest_writel(q, 0xb1000000, 0x11223344);
    g_assert_cmphex(qtest_readl(q, 0xb1000000), ==, 0x11223344);
    qtest_writel(q, 0x04008000, 0x55667788);
    g_assert_cmphex(qtest_readl(q, 0x04008000), ==, 0x55667788);
    g_assert_cmphex(qtest_readl(q, 0x08000000), ==, 0);
    qtest_quit(q);
    g_assert_cmpint(unlink(bundle), ==, 0);
}

static void mapping(QTestState *q, unsigned bank, uint32_t remote,
                    uint32_t local, uint32_t mask)
{
    uint64_t base = BRIDGE + bank * 0x8000 + 0x50b0;
    unsigned i;

    for (i = 0; i < 40; i++) {
        qtest_writel(q, base + i * 4, 0);
    }
    qtest_writel(q, base, remote >> 12);
    qtest_writel(q, base + 0x20, mask);
    qtest_writel(q, base + 0x40, local >> 12);
}

static void test_peer_window(void)
{
    g_autofree char *bundle = peer_bundle();
    g_autofree char *quoted = g_shell_quote(bundle);
    QTestState *q = qtest_initf("-M z7ii-mas-diagnostic,bundle=%s -smp 2 "
                               "-serial none", quoted);
    QDict *reply;
    const char *path = "/machine/bridge";
    const uint64_t first = 0x460f0000;
    const uint64_t second = 0x464f0000;

    reply = qtest_qmp(q, "{'execute':'qom-get','arguments':{"
                         "'path':'/machine/bridge','property':'notification'}}");
    g_assert_true(qdict_get_bool(reply, "return"));
    qobject_unref(reply);
    reply = qtest_qmp(q, "{'execute':'qom-get','arguments':{"
                         "'path':'/machine/sla/bridge','property':'notification'}}");
    g_assert_true(qdict_get_bool(reply, "return"));
    qobject_unref(reply);
    qtest_irq_intercept_out_named(q, path, SYSBUS_DEVICE_GPIO_IRQ);
    mapping(q, 1, 0xc60f0000, first, 0xffff0);
    g_assert_cmphex(qtest_readq(q, first), ==, 0);
    qtest_writeq(q, first, UINT64_C(0x1122334455667788));
    qtest_writew(q, first + 9, 0xa1b2);
    g_assert_cmphex(qtest_readq(q, first), ==,
                    UINT64_C(0x1122334455667788));
    g_assert_cmphex(qtest_readw(q, first + 9), ==, 0xa1b2);

    /* A new local view accesses the same peer bytes, not a device-side copy. */
    mapping(q, 1, 0xc60f0000, second, 0xffff0);
    g_assert_cmphex(qtest_readq(q, second), ==,
                    UINT64_C(0x1122334455667788));
    g_assert_cmphex(qtest_readw(q, second + 9), ==, 0xa1b2);
    mapping(q, 0, 0xc5000000, second, 0xffff0);
    g_assert_cmphex(qtest_readq(q, second), ==,
                    UINT64_C(0x1122334455667788));
    g_assert_cmphex(qtest_readl(q, RAW), ==, 0);
    g_assert_false(qtest_get_irq(q, 0));

    /* Unknown descriptor attributes cannot turn into a successful transfer. */
    qtest_writel(q, BRIDGE + 0xd110, 1);
    qtest_writeq(q, second, 0);
    qtest_writel(q, BRIDGE + 0xd110, 0);
    g_assert_cmphex(qtest_readq(q, second), ==,
                    UINT64_C(0x1122334455667788));
    /* Ambiguous rows and non-prefix masks cannot select a peer silently. */
    qtest_writel(q, BRIDGE + 0xd0b4, 0xc60f0);
    qtest_writel(q, BRIDGE + 0xd0d4, 0xffff0);
    qtest_writel(q, BRIDGE + 0xd0f4, second >> 12);
    qtest_writeq(q, second, 0);
    qtest_writel(q, BRIDGE + 0xd0d4, 0);
    g_assert_cmphex(qtest_readq(q, second), ==,
                    UINT64_C(0x1122334455667788));
    qtest_writel(q, BRIDGE + 0xd0d0, 0xffff2);
    qtest_writeq(q, second, 0);
    qtest_writel(q, BRIDGE + 0xd0d0, 0xffff0);
    g_assert_cmphex(qtest_readq(q, second), ==,
                    UINT64_C(0x1122334455667788));
    /* Reset cancels mappings, but it does not clear the peer's real RAM. */
    qtest_writel(q, RESET, 0xf000f);
    qtest_writel(q, RESET, 0);
    qtest_writeq(q, second, 0);
    mapping(q, 1, 0xc60f0000, first, 0xffff0);
    g_assert_cmphex(qtest_readq(q, first), ==,
                    UINT64_C(0x1122334455667788));
    qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
    /* Synthetic loopback tests both output pulses through real input/IRQ. */
    qtest_qmp_assert_success(q, "{'execute':'qom-set','arguments':{"
                            "'path':'/machine/bridge',"
                            "'property':'peer-start[0]',"
                            "'value':'/machine/bridge/peer-control[0]'}}");
    qtest_qmp_assert_success(q, "{'execute':'qom-set','arguments':{"
                            "'path':'/machine/bridge',"
                            "'property':'peer-configuration[0]',"
                            "'value':'/machine/bridge/peer-control[0]'}}");
    test_requests(q, true);
    qtest_quit(q);
    g_assert_cmpint(unlink(bundle), ==, 0);
}

static void test_sla_page_table(void)
{
    /* Eight chunks serialized from the original five SLA region records. */
    static const uint32_t pages[40] = {
        0xe6000, 0xe8000, 0xa8000, 0xb0000,
        0x50000, 0xc0000, 0xd0000, 0xb0000,
        0xfe000, 0xfe000, 0xf8000, 0xf8000,
        0xf8000, 0xf0000, 0xf0000, 0xf0000,
        0x00000, 0x02000, 0x08000, 0x10000,
        0x18000, 0x20000, 0x30000, 0x40000,
    };
    g_autofree char *bundle = peer_bundle();
    g_autofree char *quoted = g_shell_quote(bundle);
    QTestState *q = qtest_initf("-M z7ii-mas-diagnostic,bundle=%s -smp 2 "
                               "-serial none", quoted);
    const uint64_t register_view = 0x40030000;
    const uint64_t receive_view = 0x47cf0000;
    const uint64_t mas_ring = 0xb7cf0000;
    const uint64_t sample = UINT64_C(0x0817263544536271);
    unsigned i;

    qtest_writeq(q, mas_ring, sample);
    /* Access the real private SLA bridge MMIO through the MAS bridge. */
    mapping(q, 1, 0x20130000, register_view, 0xffff0);
    qtest_writel(q, register_view + 0xc5e0, 0xf000f);
    qtest_writel(q, register_view + 0xc5e0, 0);
    /* A frozen record index8 is not a published remote-page descriptor. */
    qtest_writel(q, register_view + 0xd0d0, 0xb7cf0);
    g_assert_cmphex(qtest_readl(q, register_view + 0xd0d0), ==, 0xb7cf0);
    mapping(q, 1, 0x40000000, 0x40000000, 0xf0000);
    qtest_writeq(q, receive_view, 0);
    g_assert_cmphex(qtest_readq(q, mas_ring), ==, sample);

    mapping(q, 1, 0x20130000, register_view, 0xffff0);
    for (i = 0; i < ARRAY_SIZE(pages); i++) {
        qtest_writel(q, register_view + 0xd0b0 + i * 4, pages[i]);
        g_assert_cmphex(qtest_readl(q, register_view + 0xd0b0 + i * 4),
                        ==, pages[i]);
    }
    /* Slot7 is the class0 page; slot8 is mask0 in this full packet. */
    g_assert_cmphex(qtest_readl(q, register_view + 0xd0cc), ==, 0xb0000);
    g_assert_cmphex(qtest_readl(q, register_view + 0xd0d0), ==, 0xfe000);
    mapping(q, 1, 0x40000000, 0x40000000, 0xf0000);
    g_assert_cmphex(qtest_readq(q, receive_view), ==, sample);
    qtest_writew(q, receive_view + 10, 0x9768);
    g_assert_cmphex(qtest_readw(q, mas_ring + 10), ==, 0x9768);

    /* A dynamic remote-page update changes the target, not a mask/alias. */
    mapping(q, 1, 0x20130000, register_view, 0xffff0);
    qtest_writel(q, register_view + 0xd0cc, 0xa0000);
    mapping(q, 1, 0x40000000, 0x40000000, 0xf0000);
    qtest_writeq(q, receive_view, 0);
    g_assert_cmphex(qtest_readq(q, mas_ring), ==, sample);
    mapping(q, 1, 0x20130000, register_view, 0xffff0);
    qtest_writel(q, register_view + 0xd0cc, 0xb0000);
    mapping(q, 1, 0x40000000, 0x40000000, 0xf0000);
    g_assert_cmphex(qtest_readq(q, receive_view), ==, sample);

    mapping(q, 1, 0x20130000, register_view, 0xffff0);
    qtest_writel(q, register_view + 0xc5e0, 0xf000f);
    qtest_writel(q, register_view + 0xc5e0, 0);
    mapping(q, 1, 0x40000000, 0x40000000, 0xf0000);
    qtest_writeq(q, receive_view, 0);
    g_assert_cmphex(qtest_readq(q, mas_ring), ==, sample);
    g_assert_cmphex(qtest_readw(q, mas_ring + 10), ==, 0x9768);
    qtest_quit(q);
    g_assert_cmpint(unlink(bundle), ==, 0);
}

void nikon_bridge_add_tests(void)
{
    qtest_add_func("/nikon/combined/domains", test_combined_domains);
    qtest_add_func("/nikon/bridge/peer-window", test_peer_window);
    qtest_add_func("/nikon/bridge/sla-page-table", test_sla_page_table);
}

void nikon_test_bridge(QTestState *q)
{
    const char *path = "/machine/bridge";

    qtest_irq_intercept_out_named(q, path, SYSBUS_DEVICE_GPIO_IRQ);
    qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
    test_edges(q, path);
    qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
    test_ready(q, path);
    qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
    test_reset(q, path);
    qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
    test_configuration(q);
    qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
    test_requests(q, false);
    qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
    test_control_input(q, path);
    qtest_qmp_assert_success(q, "{'execute':'system_reset'}");
}
