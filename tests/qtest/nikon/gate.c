/* SLA event gate finite source profile and explicit pending inputs.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "libqtest.h"

void nikon_gate_test(QTestState *q, const char *path, uint64_t base);

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
    /* All sixteen type0 slots are idle before a parameter submission. */
    for (i = 0; i < 16; i++) {
        g_autofree char *reply = qtest_hmp(q, "xp /1wx 0x%" PRIx64,
                                          base + 0x2040 + 4 * i);

        g_assert_null(strstr(reply, "Cannot access memory"));
        g_assert_cmphex(qtest_readl(q, base + 0x2040 + 4 * i), ==, 0);
    }
    /* Full thirteen-word packets, all slots, retained ownership. No timer or
     * unrelated pending ACK is a substitute for an actual graph consumer. */
    for (i = 0; i < 16; i++) {
        uint64_t packet = base + 0x400 + 0x40 * i;

        qtest_writel(q, base + 0x2c, i); /* Incomplete packet rejects. */
        g_assert_cmphex(qtest_readl(q, base + 0x2040 + 4 * i), ==, 0);
        for (j = 0; j < 13; j++) {
            qtest_writel(q, packet + 4 * j, packet_values[j]);
            g_assert_cmphex(qtest_readl(q, packet + 4 * j), ==,
                            packet_values[j]);
        }
        g_assert_cmphex(qtest_readl(q, base + 0x2040 + 4 * i), ==, 0);
        qtest_writel(q, base + 0x2c, i);
        for (group = 0; group < 16; group++) {
            g_assert_cmphex(qtest_readl(q, base + 0x2040 + 4 * group), ==,
                            group <= i);
        }
        qtest_writel(q, packet + 36, 0); /* Busy packet is immutable. */
        qtest_writel(q, base + 0x2040 + 4 * i, 0);
        qtest_writel(q, base + 0x2c, i);
        g_assert_cmphex(qtest_readl(q, packet + 36), ==,
                        packet_values[9]);
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
        g_assert_cmphex(qtest_readl(q, base + 0x2040 + 4 * i), ==, 1);
    }
    g_assert_false(qtest_get_irq(q, 0));
    qtest_writel(q, base + 4, 1);
    qtest_writel(q, base + 0xc, 1);
    qtest_writel(q, base + 4, 0);
    qtest_clock_step(q, 20000);
    g_assert_cmphex(qtest_readl(q, base + 0x18), ==, 1);
    qtest_writel(q, base + 0x18, 1);
    for (i = 0; i < 16; i++) {
        g_assert_cmphex(qtest_readl(q, base + 0x2040 + 4 * i), ==, 1);
    }
    qtest_system_reset(q);
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
}
