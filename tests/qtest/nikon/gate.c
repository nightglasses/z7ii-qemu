/* SLA event gate finite source profile and explicit pending inputs.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "qobject/qdict.h"
#include "libqtest.h"
#include "hw/nikon/gatepacket.h"

void nikon_gate_test(QTestState *q, const char *path, uint64_t base);

static uint64_t gate_property(QTestState *q, const char *path, const char *name)
{
    QDict *response = qtest_qmp(q, "{'execute':'qom-get','arguments':"
                               "{'path':%s,'property':%s}}", path, name);
    uint64_t value;

    g_assert_true(qdict_haskey(response, "return"));
    value = qdict_get_int(response, "return");

    qobject_unref(response);
    return value;
}

static size_t gate_program(unsigned kind, uint8_t *data)
{
    static const struct {
        unsigned kind, count;
        unsigned offset[8], words[8];
    } programs[] = {
        { 4, 2, { 4, 0x10 }, { 2, 16 } },
        { 5, 3, { 0, 0x200, 0x400 }, { 4, 81, 4 } },
        { 6, 3, { 0, 0x200, 0x400 }, { 4, 81, 4 } },
        { 7, 6, { 0, 0x64, 0x80, 0xc0, 0x100, 0x22c },
                 { 22, 5, 10, 10, 10, 5 } },
        { 8, 2, { 0, 0x78 }, { 28, 4 } },
        { 9, 1, { 0 }, { 34 } },
        { 10, 3, { 0, 0x40, 0x80 }, { 10, 8, 17 } },
        { 11, 3, { 0, 0x40, 0x80 }, { 10, 8, 17 } },
        { 12, 8, { 0, 0x20, 0x30, 0x50, 0x70, 0x90, 0xb0, 0xd0 },
                  { 7, 2, 6, 6, 6, 6, 6, 5 } },
        { 13, 2, { 0, 0x80 }, { 4, 26 } },
        { 14, 1, { 0 }, { 6 } },
        { 15, 2, { 0, 0x10 }, { 3, 14 } },
        { 16, 1, { 0 }, { 8 } },
        { 17, 4, { 0, 0x20, 0x60, 0x80 }, { 2, 15, 5, 7 } },
        { 18, 4, { 0, 0x10, 0x40, 0x60 }, { 3, 7, 2, 9 } },
    };
    unsigned p, b, j;
    size_t cursor = 0;

    for (p = 0; p < ARRAY_SIZE(programs); p++) {
        if (programs[p].kind != kind) {
            continue;
        }
        for (b = 0; b < programs[p].count; b++) {
            unsigned words = programs[p].words[b];

            stl_le_p(data + cursor, programs[p].offset[b] | (words << 16) |
                (b + 1 == programs[p].count ? 0x01000000 : 0x11000000));
            for (j = 1; j < words; j++) {
                stl_le_p(data + cursor + j * 4, 0x80000000 + cursor + j);
            }
            cursor += words * 4;
        }
        return cursor;
    }
    g_assert_not_reached();
}

static void gate_commands(QTestState *q, const char *path, uint64_t base)
{
    static const struct { unsigned offset, kind; } engines[] = {
        { 0x800, 4 }, { 0x820, 4 }, { 0x840, 4 }, { 0x880, 7 },
        { 0x940, 10 }, { 0x960, 10 }, { 0x980, 10 }, { 0x9a0, 10 },
        { 0x9c0, 10 }, { 0x9e0, 10 }, { 0xa00, 17 }, { 0xa20, 17 },
        { 0xa40, 12 }, { 0xb60, 17 }, { 0xb80, 17 }, { 0xba0, 10 },
        { 0xbc0, 10 }, { 0xd00, 15 }, { 0xd20, 15 }, { 0xd40, 11 },
        { 0xd60, 11 }, { 0xd80, 11 }, { 0xda0, 11 }, { 0xdc0, 11 },
        { 0xde0, 11 }, { 0xe00, 11 }, { 0xe20, 11 },
        { 0xe60, 5 }, { 0xe80, 6 },
        { 0x8a0, 8 }, { 0x8c0, 8 }, { 0x8e0, 8 }, { 0xc80, 8 },
        { 0x900, 9 }, { 0xac0, 9 }, { 0xcc0, 9 }, { 0xce0, 9 },
        { 0xa80, 13 }, { 0xaa0, 13 }, { 0xa60, 14 },
        { 0xe40, 16 }, { 0x920, 18 },
    };
    const uint32_t source = 0xc1004000;
    uint64_t words = 0;
    unsigned i, j;

    for (i = 0; i < ARRAY_SIZE(engines); i++) {
        uint64_t port = base + engines[i].offset;
        uint8_t data[NIKON_GATE_PACKET_MAX];
        size_t length = gate_program(engines[i].kind, data);

        if (engines[i].kind == 9) {
            /* Import retains logical54 without dereferencing its value. */
            stl_le_p(data + 0x58, 0x20100000);
        }
        qtest_memwrite(q, source, data, length);
        if (engines[i].kind == 5 || engines[i].kind == 6) {
            uint8_t trailer[40];

            memset(trailer, 0xff, sizeof(trailer));
            qtest_memwrite(q, source + length, trailer, sizeof(trailer));
        }
        qtest_writel(q, port + 0x10, source); /* No reset pair yet. */
        g_assert_cmphex(qtest_readl(q, port + 0x10), ==, 0);
        if (engines[i].kind == 10) {
            qtest_writel(q, port + 8, 0x12340000 + i);
            qtest_writel(q, port + 0xc, source + 0x1000);
        }
        qtest_writel(q, port + 0x14, 1);
        qtest_writel(q, port + 0x10, source); /* Held reset rejects. */
        qtest_writel(q, port + 0x14, 0);
        if (engines[i].kind == 10) {
            g_assert_cmphex(qtest_readl(q, port + 8), ==, 0x12340000 + i);
            g_assert_cmphex(qtest_readl(q, port + 0xc), ==, source + 0x1000);
        }
        qtest_writel(q, port + 0x10, source + 1);
        qtest_writel(q, port + 0x10, 0);
        qtest_writel(q, port + 0x10, 0x20100000); /* No MMIO source DMA. */
        qtest_writel(q, port + 0x10, 0xffffffff);
        qtest_writel(q, port + 0x10, source);
        words += length / 4;
        g_assert_cmphex(qtest_readl(q, port + 0x10), ==, source);
        g_assert_cmpuint(gate_property(q, path, "loaded-packets"), ==, i + 1);
        g_assert_cmpuint(gate_property(q, path, "loaded-words"), ==, words);
        g_assert_cmphex(gate_property(q, path, "configured-mask"), ==,
                        (UINT64_C(1) << (i + 1)) - 1);
        qtest_writel(q, port, 2); /* Unknown start value rejects. */
        g_assert_cmphex(qtest_readl(q, port), ==, 0);
        qtest_writel(q, port, 1);
        qtest_writel(q, port, 1); /* Duplicate start cannot complete work. */
        qtest_clock_step(q, 100000);
        g_assert_cmphex(qtest_readl(q, port), ==, 1);
        g_assert_cmphex(qtest_readl(q, port + 0x10), ==, source);
        g_assert_false(qtest_get_irq(q, 0));
        for (j = 0; j < 3; j++) {
            g_assert_cmphex(qtest_readl(q, base + 0x18 + 4 * j), ==, 0);
        }
    }
    /* Isolate the last port's reset above the old32-bit mask boundary. */
    qtest_writel(q, base + engines[i - 1].offset + 0x14, 1);
    g_assert_cmphex(gate_property(q, path, "configured-mask"), ==,
                    ((UINT64_C(1) << i) - 1) &
                    ~(UINT64_C(1) << (i - 1)));
    g_assert_cmphex(qtest_readl(q, base + engines[i - 1].offset + 0x10), ==, 0);
    for (j = 0; j + 1 < i; j++) {
        g_assert_cmphex(qtest_readl(q, base + engines[j].offset + 0x10), ==,
                        source);
    }
    /* Rejected late commands retain every already configured engine. */
    qtest_writel(q, base + 0x810, source);
    g_assert_cmpuint(gate_property(q, path, "loaded-packets"), ==, i);
    qtest_writel(q, base + 0x814, 1);
    g_assert_cmphex(gate_property(q, path, "configured-mask"), ==,
                    ((UINT64_C(1) << i) - 1) & ~UINT64_C(1) &
                    ~(UINT64_C(1) << (i - 1)));
    qtest_writel(q, base + 0x814, 0);
    {
        uint8_t data[72];

        gate_program(4, data);
        qtest_memwrite(q, source, data, sizeof(data));
    }
    qtest_writel(q, source + 8, 0x11100010); /* Missing terminator. */
    qtest_writel(q, base + 0x810, source);
    g_assert_cmpuint(gate_property(q, path, "loaded-packets"), ==, i);
    g_assert_cmphex(qtest_readl(q, base + 0x810), ==, 0);
    for (i = 0; i < 8; i++) {
        uint64_t pair = base + 0x4000 + i * 0x10;

        qtest_writel(q, pair, 0x773);
        qtest_writel(q, pair + 4, 0x3fff7fff);
        qtest_writel(q, pair, 0x800);
        qtest_writel(q, pair + 4, 0x8000);
        g_assert_cmphex(qtest_readl(q, pair), ==, 0x773);
        g_assert_cmphex(qtest_readl(q, pair + 4), ==, 0x3fff7fff);
    }
    for (i = 0; i < 0x6000 / 4; i++) {
        uint32_t value = (i * 17) & 0xffff;

        qtest_writel(q, 0x24000000 + i * 4, value);
        g_assert_cmphex(qtest_readl(q, 0x24000000 + i * 4), ==, value);
    }
    qtest_writel(q, 0x24000000, 0x10000);
    qtest_writew(q, 0x24000000, 0xff);
    g_assert_cmphex(qtest_readl(q, 0x24000000), ==, 0);
    g_assert_cmphex(qtest_readl(q, base + 0x148), ==, 0);
    for (i = 0; i < 0x1000 / 4; i++) {
        qtest_writel(q, 0x24010000 + 4 * i, 0xa8000000 + i);
        g_assert_cmphex(qtest_readl(q, 0x24010000 + 4 * i), ==, 0xa8000000 + i);
        g_assert_cmphex(qtest_readl(q, base + 0x148), ==,
                        i + 1 == 0x1000 / 4 ? 0x100 : 0);
    }
    qtest_writel(q, 0x24010000, 0);
    qtest_writel(q, base + 0xa54, 1);
    g_assert_cmphex(qtest_readl(q, base + 0x148), ==, 0x100);
    /* Type18 requests provisioning; only the following CPU writes supply it. */
    qtest_writel(q, base + 0x2c0, 1);
    g_assert_cmphex(qtest_readl(q, base + 0x148), ==, 0);
    g_assert_cmphex(qtest_readl(q, 0x24010ffc), ==, 0xa80003ff);
    qtest_writel(q, 0x24010000, 0);
    qtest_writel(q, 0x24010000, 0);
    qtest_clock_step(q, 1000000);
    g_assert_cmphex(qtest_readl(q, base + 0x148), ==, 0);
    for (i = 1; i < 0x1000 / 4; i++) {
        qtest_writel(q, 0x24010000 + 4 * i, 0);
        g_assert_cmphex(qtest_readl(q, base + 0x148), ==,
                        i + 1 == 0x1000 / 4 ? 0x100 : 0);
    }
    qtest_writel(q, base + 0x2c0, 0);
    qtest_writel(q, base + 0x2c0, 2);
    qtest_writew(q, base + 0x2c0, 1);
    g_assert_cmphex(qtest_readl(q, base + 0x148), ==, 0x100);
    qtest_writel(q, base + 0x934, 1);
    qtest_writel(q, base + 0x934, 0);
    g_assert_cmphex(qtest_readl(q, base + 0x148), ==, 0x100);
    for (i = 0; i < 3; i++) {
        g_assert_cmphex(qtest_readl(q, base + 0x18 + 4 * i), ==, 0);
    }
    qtest_system_reset(q);
    g_assert_cmpuint(gate_property(q, path, "loaded-packets"), ==, 0);
    g_assert_cmphex(gate_property(q, path, "configured-mask"), ==, 0);
    for (i = 0; i < ARRAY_SIZE(engines); i++) {
        g_assert_cmphex(qtest_readl(q, base + engines[i].offset + 0x10), ==, 0);
    }
    g_assert_cmphex(qtest_readl(q, 0x24005ffc), ==, 0);
    g_assert_cmphex(qtest_readl(q, 0x24010ffc), ==, 0);
    g_assert_cmphex(qtest_readl(q, base + 0x148), ==, 0);
}

static void gate_routes(QTestState *q, const char *path, uint64_t base)
{
    g_autofree char *reply = NULL;
    unsigned i;

    qtest_system_reset(q);
    g_assert_cmpuint(gate_property(q, path, "route-commits"), ==, 0);
    qtest_writel(q, base + 4, 1);
    qtest_writel(q, base + 0xc, 1U << 30);
    qtest_set_irq_in(q, path, "pending-bit", 30, 0);
    qtest_set_irq_in(q, path, "pending-bit", 30, 1);
    g_assert_true(qtest_get_irq(q, 0));
    qtest_writel(q, base + 0x2684, 0x76543210);
    qtest_writel(q, base + 0x2690, 0xabcd);
    g_assert_cmphex(gate_property(q, path, "applied-route1"), ==, 0);
    g_assert_cmphex(gate_property(q, path, "applied-route2"), ==, 0);
    qtest_writel(q, base + 0x2680, 1);
    g_assert_cmphex(gate_property(q, path, "applied-route1"), ==, 0x76543210);
    g_assert_cmphex(gate_property(q, path, "applied-route2"), ==, 0xabcd);
    g_assert_cmpuint(gate_property(q, path, "route-commits"), ==, 1);

    /* Source teardown clears one family; the other survives the next latch. */
    qtest_writel(q, base + 0x2684, 0);
    g_assert_cmphex(gate_property(q, path, "applied-route1"), ==, 0x76543210);
    qtest_writel(q, base + 0x2680, 1);
    g_assert_cmphex(gate_property(q, path, "applied-route1"), ==, 0);
    g_assert_cmphex(gate_property(q, path, "applied-route2"), ==, 0xabcd);
    qtest_writel(q, base + 0x2684, 0xfedcba98);
    qtest_writel(q, base + 0x2690, 0x1234);
    qtest_writel(q, base + 0x2680, 1);
    qtest_writel(q, base + 0x2680, 1); /* Same snapshot, no processing event. */
    g_assert_cmpuint(gate_property(q, path, "route-commits"), ==, 4);

    qtest_writel(q, base + 0x2680, 0);
    qtest_writel(q, base + 0x2680, 2);
    qtest_writel(q, base + 0x2680, 0x80000001);
    qtest_writeb(q, base + 0x2680, 1);
    qtest_writew(q, base + 0x2680, 1);
    qtest_writel(q, base + 0x2681, 1);
    qtest_writel(q, base + 0x2690, 0x10000);
    qtest_writew(q, base + 0x2684, 0);
    reply = qtest_hmp(q, "xp /1wx 0x%" PRIx64, base + 0x2680);
    g_assert_nonnull(strstr(reply, "Cannot access memory"));
    qtest_clock_step(q, 1000000);
    g_assert_cmpuint(gate_property(q, path, "route-commits"), ==, 4);
    g_assert_cmphex(gate_property(q, path, "applied-route1"), ==, 0xfedcba98);
    g_assert_cmphex(gate_property(q, path, "applied-route2"), ==, 0x1234);
    g_assert_cmphex(qtest_readl(q, base + 0x2690), ==, 0x1234);
    g_assert_cmpuint(gate_property(q, path, "parameter-valid"), ==, 0);
    g_assert_cmpuint(gate_property(q, path, "configured-mask"), ==, 0);
    g_assert_cmpuint(gate_property(q, path, "loaded-packets"), ==, 0);
    for (i = 0; i < 3; i++) {
        uint32_t expected = i == 0 ? 1U << 30 : 0;

        g_assert_cmphex(qtest_readl(q, base + 0xc + i * 4), ==, expected);
        g_assert_cmphex(qtest_readl(q, base + 0x18 + i * 4), ==, expected);
    }
    g_assert_true(qtest_get_irq(q, 0));

    qtest_system_reset(q);
    g_assert_cmpuint(gate_property(q, path, "route-commits"), ==, 0);
    g_assert_cmphex(gate_property(q, path, "applied-route1"), ==, 0);
    g_assert_cmphex(gate_property(q, path, "applied-route2"), ==, 0);
    g_assert_cmphex(qtest_readl(q, base + 0x2684), ==, 0);
    g_assert_cmphex(qtest_readl(q, base + 0x2690), ==, 0);
    qtest_writel(q, base + 0x2680, 1);
    g_assert_cmpuint(gate_property(q, path, "route-commits"), ==, 1);
    g_assert_false(qtest_get_irq(q, 0));
}

void nikon_gate_test(QTestState *q, const char *path, uint64_t base)
{
    static const struct {
        unsigned first, last;
    } maps[] = {
        { 0x2200, 0x22fc }, { 0x2300, 0x23fc },
        { 0x2880, 0x28f4 }, { 0x2900, 0x2924 },
    };
    static const unsigned packed_words[] = {
        0xf8, 0xfc, 0x100, 0x104, 0x120, 0x124, 0x128, 0x12c,
    };
    static const uint32_t packet_values[] = {
        0xff771f71, 0x0001ff3f, 0xffffffff, 0xabcdef01,
        0x1ffff, 0x1ffff, 0x1ffff, 0x1ffff, 0x3fff7fff,
        0x12345678, 0x87654321, 0x80000000, 0xffffffff,
    };
    static const unsigned slot_holes[] = {
        0x3fc, 0x434, 0x438, 0x43c, 0x7f4, 0x7f8, 0x7fc,
        0x203c, 0x2080,
    };
    unsigned i, j, group;

    qtest_irq_intercept_out_named(q, path, "sysbus-irq");
    {
        g_autofree char *reply = qtest_hmp(q, "xp /1wx 0x%" PRIx64,
                                          base + 0xe74);

        g_assert_null(strstr(reply, "Cannot access memory"));
    }
    {
        g_autofree char *reply = qtest_hmp(q, "xp /1wx 0x%" PRIx64,
                                          base + 0x914);

        g_assert_null(strstr(reply, "Cannot access memory"));
    }
    /*
     * Resource37 command reset must be a real state transition, not a
     * discarded write or a fabricated processing completion.
     */
    qtest_writel(q, base + 0x814, 1);
    {
        g_autofree char *reply = qtest_hmp(q, "xp /1wx 0x%" PRIx64,
                                          base + 0x814);

        g_assert_null(strstr(reply, "Cannot access memory"));
        g_assert_cmphex(qtest_readl(q, base + 0x814), ==, 1);
    }
    qtest_writel(q, base + 0x814, 0);
    g_assert_cmphex(qtest_readl(q, base + 0x814), ==, 0);
    g_assert_false(qtest_get_irq(q, 0));
    qtest_system_reset(q);
    gate_commands(q, path, base);
    /* All sixteen type0 slots are idle before a parameter submission. */
    for (i = 0; i < 16; i++) {
        g_autofree char *reply = qtest_hmp(q, "xp /1wx 0x%" PRIx64,
                                          base + 0x2040 + 4 * i);

        g_assert_null(strstr(reply, "Cannot access memory"));
        g_assert_cmphex(qtest_readl(q, base + 0x2040 + 4 * i), ==, 0);
    }
    /* Full parameter capture and reload before any processor is started. */
    for (i = 0; i < 16; i++) {
        uint64_t packet = base + 0x400 + 0x40 * i;

        qtest_writel(q, base + 0x2c, i); /* Incomplete packet rejects. */
        g_assert_cmphex(qtest_readl(q, base + 0x2040 + 4 * i), ==, 0);
        for (j = 0; j < 13; j++) {
            qtest_writel(q, packet + 4 * j, packet_values[j]);
            g_assert_cmphex(qtest_readl(q, packet + 4 * j), ==, 0);
        }
        g_assert_cmphex(qtest_readl(q, base + 0x2040 + 4 * i), ==, 0);
        qtest_writel(q, base + 0x2c, i);
        for (j = 0; j < 13; j++) {
            g_assert_cmphex(qtest_readl(q, packet + 4 * j), ==,
                            packet_values[j]);
        }
        /* Restaging does not retire pixels. */
        qtest_writel(q, packet + 36, 0);
        g_assert_cmphex(qtest_readl(q, packet + 36), ==, packet_values[9]);
        qtest_writel(q, base + 0x2040 + 4 * i, 0);
        qtest_writel(q, base + 0x2c, i);
        g_assert_cmphex(qtest_readl(q, packet + 36), ==,
                        packet_values[9]);
        g_assert_cmpuint(gate_property(q, path, "parameter-commits"), ==,
                         2 * i + 1);
        for (j = 0; j < 13; j++) {
            if (j != 9) {
                qtest_writel(q, packet + 4 * j, packet_values[j]);
            }
        }
        qtest_writel(q, base + 0x2c, i);
        g_assert_cmphex(qtest_readl(q, packet + 36), ==, 0);
        g_assert_cmpuint(gate_property(q, path, "parameter-commits"), ==,
                         2 * i + 2);
        g_assert_cmphex(gate_property(q, path, "parameter-valid"), ==,
                        (1U << (i + 1)) - 1);
        for (group = 0; group < 16; group++) {
            g_assert_cmphex(qtest_readl(q, base + 0x2040 + 4 * group), ==, 0);
        }
    }
    qtest_writel(q, base + 0x2c, 16);
    qtest_writew(q, base + 0x2c, 0);
    for (i = 0; i < G_N_ELEMENTS(slot_holes); i++) {
        g_autofree char *reply = qtest_hmp(q, "xp /1wx 0x%" PRIx64,
                                          base + slot_holes[i]);

        g_assert_nonnull(strstr(reply, "Cannot access memory"));
    }
    qtest_clock_step(q, 1000000);
    for (i = 0; i < 16; i++) {
        g_assert_cmphex(qtest_readl(q, base + 0x2040 + 4 * i), ==, 0);
    }
    /* Local controls cannot discard captured parameters. */
    qtest_writel(q, base + 0xe74, 1);
    qtest_writel(q, base + 0xe74, 0);
    qtest_writel(q, base + 0xe94, 1);
    qtest_writel(q, base + 0xe94, 0);
    qtest_writel(q, base + 0x934, 1);
    qtest_writel(q, base + 0x934, 0);
    qtest_writel(q, base + 0x2c0, 1);
    for (i = 0; i < 16; i++) {
        g_assert_cmphex(qtest_readl(q, base + 0x2040 + 4 * i), ==, 0);
    }
    g_assert_false(qtest_get_irq(q, 0));
    qtest_writel(q, base + 4, 1);
    qtest_writel(q, base + 0xc, 1);
    qtest_writel(q, base + 4, 0);
    qtest_clock_step(q, 20000);
    g_assert_cmphex(qtest_readl(q, base + 0x18), ==, 1);
    qtest_writel(q, base + 0x18, 1);
    for (i = 0; i < 16; i++) {
        g_assert_cmphex(qtest_readl(q, base + 0x2040 + 4 * i), ==, 0);
    }
    g_assert_cmphex(gate_property(q, path, "parameter-valid"), ==, 0xffff);
    g_assert_cmpuint(gate_property(q, path, "parameter-commits"), ==, 32);
    {
        uint8_t data[72];
        g_autofree char *reply = NULL;

        gate_program(4, data);
        qtest_memwrite(q, 0xc1004000, data, sizeof(data));
        qtest_writel(q, base + 0x814, 1);
        qtest_writel(q, base + 0x814, 0);
        qtest_writel(q, base + 0x810, 0xc1004000);
        qtest_writel(q, base + 0x800, 1);
        reply = qtest_hmp(q, "xp /1wx 0x%" PRIx64, base + 0x2040);
        g_assert_nonnull(strstr(reply, "Cannot access memory"));
        for (j = 0; j < 13; j++) {
            qtest_writel(q, base + 0x400 + 4 * j, packet_values[j]);
        }
        qtest_writel(q, base + 0x2c, 0); /* Active use is still unmodeled. */
        g_assert_cmphex(qtest_readl(q, base + 0x424), ==, 0);
        g_assert_cmpuint(gate_property(q, path, "parameter-commits"), ==, 32);
        qtest_writel(q, base + 0x814, 1);
        qtest_writel(q, base + 0x2c, 0);
        g_assert_cmphex(qtest_readl(q, base + 0x424), ==, packet_values[9]);
        g_assert_cmpuint(gate_property(q, path, "parameter-commits"), ==, 33);
    }
    qtest_system_reset(q);
    g_assert_cmphex(gate_property(q, path, "parameter-valid"), ==, 0);
    g_assert_cmpuint(gate_property(q, path, "parameter-commits"), ==, 0);
    for (i = 0; i < 16; i++) {
        g_assert_cmphex(qtest_readl(q, base + 0x2040 + 4 * i), ==, 0);
        for (j = 0; j < 13; j++) {
            g_assert_cmphex(qtest_readl(q, base + 0x400 + 0x40 * i + 4 * j),
                            ==, 0);
        }
        qtest_writel(q, base + 0x2c, i); /* Reset invalidates staged words. */
        g_assert_cmphex(qtest_readl(q, base + 0x2040 + 4 * i), ==, 0);
    }
    /* Bad reserved bits, widths and unaligned access cannot stage a packet. */
    for (i = 0; i < G_N_ELEMENTS(packet_values); i++) {
        uint64_t address = base + 0x400 + 4 * i;
        uint32_t invalid = i == 0 ? 2 : i == 1 ? 0x40 :
                           i >= 4 && i <= 7 ? 0x20000 :
                           i == 8 ? 0x8000 : 0;

        if (invalid) {
            qtest_writel(q, address, invalid);
            g_assert_cmphex(qtest_readl(q, address), ==, 0);
        }
        qtest_writeb(q, address, 1);
        qtest_writew(q, address, 1);
        qtest_writel(q, address + 1, 1);
        g_assert_cmphex(qtest_readl(q, address), ==, 0);
    }
    qtest_writel(q, base + 0x2c, 0);
    g_assert_cmphex(qtest_readl(q, base + 0x2040), ==, 0);
    /* Actual 944D2 initializer write does not reset or invent pending. */
    qtest_writel(q, base, 0);
    qtest_writel(q, base + 8, 0);
    g_assert_cmphex(qtest_readl(q, base + 4), ==, 0);
    for (i = 0; i < 3; i++) {
        g_assert_cmphex(qtest_readl(q, base + 0xc + i * 4), ==, 0);
        g_assert_cmphex(qtest_readl(q, base + 0x18 + i * 4), ==, 0);
    }
    qtest_clock_step(q, 1000000);
    g_assert_false(qtest_get_irq(q, 0));
    g_assert_cmphex(qtest_readl(q, base + 0x18), ==, 0);

    /* Source 94836/948A6/94996 ends with 0x20102800=1.  It records an
     * outstanding submit only; no completion, ready bit, IRQ, guest memory
     * write or pixel producer is supplied by this gate. */
    g_assert_cmphex(qtest_readl(q, base + 0x2800), ==, 0);
    qtest_writel(q, base + 0x2800, 1);
    g_assert_cmphex(qtest_readl(q, base + 0x2800), ==, 1);
    g_assert_false(qtest_get_irq(q, 0));
    qtest_clock_step(q, 1000000);
    g_assert_cmphex(qtest_readl(q, base + 0x2800), ==, 1);
    qtest_writel(q, base + 0x2800, 0);
    qtest_writel(q, base + 0x2800, 2);
    qtest_writew(q, base + 0x2800, 1);
    g_assert_cmphex(qtest_readl(q, base + 0x2800), ==, 1);

    for (group = 0; group < 2; group++) {
        uint64_t input = base + 0x2000 + group * 0x100;
        uint64_t parameter = base + 0x2600 + group * 0x40;
        uint32_t packed = 0x12340;

        /* Complete 945B6 packet paths and 9458A/945A2 low-nibble RMW. */
        qtest_writel(q, base + 0x20a4, 0);
        qtest_writel(q, input, packed);
        qtest_writel(q, input + 4, 0x3fff);
        qtest_writel(q, input + 8, 0xa5);
        qtest_writel(q, input + 0x10, 0x89abcdef);
        qtest_writel(q, input + 0x14, 0x76543210);
        qtest_writel(q, input + 0x18, 0x9876);
        qtest_writel(q, input, (qtest_readl(q, input) & ~0xf) | 1);
        g_assert_cmphex(qtest_readl(q, input), ==, packed | 1);
        qtest_writel(q, input, qtest_readl(q, input) & ~0xf);
        g_assert_cmphex(qtest_readl(q, input), ==, packed);
        g_assert_cmphex(qtest_readl(q, input + 8), ==, 0xa5);
        g_assert_cmphex(qtest_readl(q, input + 0x10), ==, 0x89abcdef);
        g_assert_cmphex(qtest_readl(q, input + 0x14), ==, 0x76543210);
        g_assert_cmphex(qtest_readl(q, input + 0x18), ==, 0x9876);

        /* Regression: 94640 has an unclamped OR operand, for both inputs. */
        qtest_writel(q, input + 4,
                     (qtest_readl(q, input + 4) & ~1) | 0x80010001);
        g_assert_cmphex(qtest_readl(q, input + 4), ==, 0x80013fff);
        qtest_writel(q, input + 4, qtest_readl(q, input + 4) & ~1);
        g_assert_cmphex(qtest_readl(q, input + 4), ==, 0x80013ffe);

        /* 946E0 packet selects independent 2600/2640 parameter groups. */
        qtest_writel(q, parameter + 0x20, 0);
        qtest_writel(q, parameter, 0xabcd);
        qtest_writel(q, parameter + 4, 0x12345678);
        qtest_writel(q, parameter + 8, 0xfedcba98);
        qtest_writel(q, parameter + 0xc, 0x87);
        qtest_writel(q, parameter + 0x10, 0x98760054);
        qtest_writel(q, parameter + 0x18, 0x32);
        g_assert_cmphex(qtest_readl(q, parameter), ==, 0xabcd);
        g_assert_cmphex(qtest_readl(q, parameter + 4), ==, 0x12345678);
        g_assert_cmphex(qtest_readl(q, parameter + 8), ==, 0xfedcba98);
        g_assert_cmphex(qtest_readl(q, parameter + 0xc), ==, 0x87);
        g_assert_cmphex(qtest_readl(q, parameter + 0x10), ==, 0x98760054);
        g_assert_cmphex(qtest_readl(q, parameter + 0x18), ==, 0x32);
    }
    for (i = 0; i < G_N_ELEMENTS(maps); i++) {
        for (j = maps[i].first; j <= maps[i].last; j += 4) {
            uint32_t value = 0x87654321 ^ j;

            qtest_writel(q, base + j, value);
            g_assert_cmphex(qtest_readl(q, base + j), ==, value);
        }
    }
    /* Full-byte packed 94836 values; getter extraction does not narrow writes. */
    for (i = 0; i < 6; i++) {
        qtest_writel(q, base + 0x2820 + i * 4, 0xfedcba98 ^ i);
        g_assert_cmphex(qtest_readl(q, base + 0x2820 + i * 4), ==,
                        0xfedcba98 ^ i);
    }
    qtest_writel(q, base + 0x2838, 0xfedc);
    qtest_writel(q, base + 0x2684, 0xfedcba98);
    qtest_writel(q, base + 0x2690, 0xabcd);
    g_assert_cmphex(qtest_readl(q, base + 0x2838), ==, 0xfedc);
    g_assert_cmphex(qtest_readl(q, base + 0x2684), ==, 0xfedcba98);
    g_assert_cmphex(qtest_readl(q, base + 0x2690), ==, 0xabcd);
    qtest_writel(q, base + 0xf4, 0xfff00);
    qtest_writel(q, base + 0x108, 0xabc00);
    qtest_writel(q, base + 0x10c, 0xab0fff00);
    g_assert_cmphex(qtest_readl(q, base + 0xf4), ==, 0xfff00);
    g_assert_cmphex(qtest_readl(q, base + 0x108), ==, 0xabc00);
    g_assert_cmphex(qtest_readl(q, base + 0x10c), ==, 0xab0fff00);
    for (i = 0; i < G_N_ELEMENTS(packed_words); i++) {
        qtest_writel(q, base + packed_words[i], 0x87654320 ^ (i << 4));
        g_assert_cmphex(qtest_readl(q, base + packed_words[i]), ==,
                        0x87654320 ^ (i << 4));
    }
    for (i = 0x30; i <= 0x34; i += 4) {
        qtest_writel(q, base + i, qtest_readl(q, base + i) | 0x80000000);
        g_assert_cmphex(qtest_readl(q, base + i), ==, 0x80000000);
    }
    qtest_clock_step(q, 1000000);
    g_assert_cmphex(qtest_readl(q, base + 0x18), ==, 0);
    g_assert_false(qtest_get_irq(q, 0));

    /* Gate-open RMW 9453A, full-word masks 949DA, three separate raw words. */
    qtest_writel(q, base + 4, (qtest_readl(q, base + 4) & ~3) | 1);
    for (i = 0; i < 3; i++) {
        qtest_writel(q, base + 0xc + i * 4, 0x80000000);
        qtest_set_irq_in(q, path, "pending-bit", i * 32 + 31, 1);
        g_assert_cmphex(qtest_readl(q, base + 0x18 + i * 4), ==, 0x80000000);
    }
    g_assert_true(qtest_get_irq(q, 0));
    for (i = 0; i < 3; i++) {
        qtest_writel(q, base + 0x18 + i * 4,
                     qtest_readl(q, base + 0x18 + i * 4));
        g_assert_cmphex(qtest_readl(q, base + 0x18 + i * 4), ==, 0);
        g_assert_cmpint(qtest_get_irq(q, 0), ==, i != 2);
    }
    qtest_set_irq_in(q, path, "pending-bit", 95, 1);
    g_assert_cmphex(qtest_readl(q, base + 0x20), ==, 0);
    qtest_set_irq_in(q, path, "pending-bit", 95, 0);
    qtest_set_irq_in(q, path, "pending-bit", 95, 1);
    qtest_writel(q, base + 0x14, 0);
    g_assert_cmphex(qtest_readl(q, base + 0x20), ==, 0x80000000);
    g_assert_false(qtest_get_irq(q, 0));
    qtest_writel(q, base + 0x14, 0x80000000);
    g_assert_true(qtest_get_irq(q, 0));

    /* Retained baseline virtual stop: deferred once, never a data producer. */
    qtest_writel(q, base + 0xc, 1);
    qtest_writel(q, base + 0x20, 0x80000000);
    qtest_writel(q, base + 4, 0);
    qtest_clock_step(q, 10000);
    qtest_writel(q, base + 4, 1);
    qtest_clock_step(q, 100000);
    g_assert_cmphex(qtest_readl(q, base + 0x18), ==, 0);
    qtest_writel(q, base + 4, 0);
    g_assert_cmphex(qtest_readl(q, base + 0x18), ==, 0);
    qtest_clock_step(q, 19999);
    g_assert_cmphex(qtest_readl(q, base + 0x18), ==, 0);
    qtest_clock_step(q, 1);
    g_assert_cmphex(qtest_readl(q, base + 0x18), ==, 1);
    g_assert_true(qtest_get_irq(q, 0));
    qtest_writel(q, base + 0x18, 1);
    g_assert_false(qtest_get_irq(q, 0));
    qtest_clock_step(q, 1000000);
    g_assert_cmphex(qtest_readl(q, base + 0x18), ==, 0);

    qtest_system_reset(q);
    g_assert_cmphex(qtest_readl(q, base + 0x2004), ==, 0);
    g_assert_cmphex(qtest_readl(q, base + 0x2924), ==, 0);
    g_assert_cmphex(qtest_readl(q, base + 0x2800), ==, 0);
    qtest_writel(q, base + 4, 1);
    qtest_set_irq_in(q, path, "pending-bit", 95, 1);
    g_assert_cmphex(qtest_readl(q, base + 0x20), ==, 0);
    g_assert_false(qtest_get_irq(q, 0));
    gate_routes(q, path, base);
}
