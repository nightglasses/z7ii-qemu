/* Private SLA finite status profile and complete source system-control RMW.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "libqtest.h"

void nikon_status_test(QTestState *q, uint64_t status, uint64_t system);

void nikon_status_test(QTestState *q, uint64_t status, uint64_t system)
{
    static const unsigned offsets[] = { 0, 8, 0xc, 0x10, 0x14 };
    static const uint32_t values[] = { 1, 0x87654321, 0xa5,
                                      0x10293847, 0xaabbccdd };
    static const uint32_t banks[] = { 0x73ffffff, 0xffffffff, 0x01ffc1ff,
                                     0x31ffffff, 0x000004ee };
    static const unsigned indices[] = { 0x7b, 0x6d, 0x66, 0x67, 0x68,
                                        0x69, 0x6a, 0x6b, 0x6c };
    unsigned i;

    for (i = 0; i < ARRAY_SIZE(offsets); i++) {
        qtest_writel(q, status + offsets[i], values[i]);
    }
    qtest_writel(q, status + 0x20, 0);
    qtest_clock_step(q, 1000000);
    for (i = 0; i < ARRAY_SIZE(offsets); i++) {
        g_assert_cmphex(qtest_readl(q, status + offsets[i]), ==, values[i]);
    }
    /* Original59514 reads these words; the disconnected profile is unavailable. */
    for (i = 0; i < 3; i++) {
        g_assert_cmphex(qtest_readl(q, status + 0x24), ==, 0);
        g_assert_cmphex(qtest_readl(q, status + 0x30), ==, 0);
    }
    for (i = 0; i < ARRAY_SIZE(banks); i++) {
        qtest_writel(q, system + 0x78 + i * 4, banks[i]);
    }
    for (i = 0; i < ARRAY_SIZE(indices); i++) {
        uint64_t word = system + 0x78 + (indices[i] >> 5) * 4;
        uint32_t bit = 1U << (indices[i] & 31);
        uint32_t original = qtest_readl(q, word);

        qtest_writel(q, word, original | bit);
        g_assert_cmphex(qtest_readl(q, word), ==, original | bit);
        qtest_writel(q, word, qtest_readl(q, word) & ~bit);
        g_assert_cmphex(qtest_readl(q, word), ==, original & ~bit);
        qtest_writel(q, word, original);
    }
    for (i = 0; i < ARRAY_SIZE(banks); i++) {
        g_assert_cmphex(qtest_readl(q, system + 0x78 + i * 4), ==, banks[i]);
    }
}
