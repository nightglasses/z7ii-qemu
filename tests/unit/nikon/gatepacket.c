/*
 * Native semantic checks of source-shaped register packet decoding.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "hw/nikon/gatepacket.h"

static void type4(void)
{
    uint8_t data[72] = { 0 };
    NikonGatePacket packet = { 0 }, saved;
    unsigned i;

    /* One route register, then fifteen consecutive parameter registers. */
    stl_le_p(data, 0x11020004);
    stl_le_p(data + 4, 0x5a);
    stl_le_p(data + 8, 0x01100010);
    for (i = 0; i < 15; i++) {
        stl_le_p(data + 12 + 4 * i, 0x80000000 + i);
    }
    g_assert_true(nikon_gate_packet_decode(4, data, sizeof(data), &packet));
    for (i = 0; i < NIKON_GATE_REGISTERS; i++) {
        bool present = i == 1 || (i >= 4 && i < 19);

        g_assert_cmpint(packet.present[i], ==, present);
        if (present) {
            g_assert_cmphex(packet.value[i], ==,
                            i == 1 ? 0x5a : 0x80000000 + i - 4);
        }
    }
    saved = packet;
    for (i = 0; i < sizeof(data); i++) {
        g_assert_false(nikon_gate_packet_decode(4, data, i, &packet));
        g_assert_cmpmem(&packet, sizeof(packet), &saved, sizeof(saved));
    }
    /* Header fields, including continuation, are not arbitrary writes. */
    for (i = 0; i < 32; i++) {
        stl_le_p(data, 0x11020004 ^ (1U << i));
        g_assert_false(nikon_gate_packet_decode(4, data, sizeof(data),
                                                &packet));
        g_assert_cmpmem(&packet, sizeof(packet), &saved, sizeof(saved));
    }
    stl_le_p(data, 0x11020004);
    stl_le_p(data + 8, 0x11100010);
    g_assert_false(nikon_gate_packet_decode(4, data, sizeof(data), &packet));
    g_assert_cmpmem(&packet, sizeof(packet), &saved, sizeof(saved));
    g_assert_false(nikon_gate_packet_decode(0, data, sizeof(data), &packet));
    g_assert_false(nikon_gate_packet_decode(4, NULL, sizeof(data), &packet));
}

static void layouts(void)
{
    static const struct {
        unsigned kind, blocks, length;
        unsigned offset[8], words[8];
    } shapes[] = {
        { 5, 3, 356, { 0, 0x200, 0x400 }, { 4, 81, 4 } },
        { 6, 3, 356, { 0, 0x200, 0x400 }, { 4, 81, 4 } },
        { 7, 6, 248, { 0, 0x64, 0x80, 0xc0, 0x100, 0x22c },
                      { 22, 5, 10, 10, 10, 5 } },
        { 8, 2, 128, { 0, 0x78 }, { 28, 4 } },
        { 9, 1, 136, { 0 }, { 34 } },
        { 10, 3, 140, { 0, 0x40, 0x80 }, { 10, 8, 17 } },
        { 11, 3, 140, { 0, 0x40, 0x80 }, { 10, 8, 17 } },
        { 12, 8, 176, { 0, 0x20, 0x30, 0x50, 0x70, 0x90, 0xb0, 0xd0 },
                      { 7, 2, 6, 6, 6, 6, 6, 5 } },
        { 15, 2, 68, { 0, 0x10 }, { 3, 14 } },
        { 17, 4, 116, { 0, 0x20, 0x60, 0x80 }, { 2, 15, 5, 7 } },
    };
    unsigned s, b, j;

    for (s = 0; s < ARRAY_SIZE(shapes); s++) {
        uint8_t data[NIKON_GATE_PACKET_MAX] = { 0 };
        NikonGatePacket packet, expected = { 0 };
        size_t cursor = 0;

        for (b = 0; b < shapes[s].blocks; b++) {
            unsigned first = shapes[s].offset[b] / 4;
            unsigned words = shapes[s].words[b];
            uint32_t header = shapes[s].offset[b] | (words << 16) |
                (b + 1 == shapes[s].blocks ? 0x01000000 : 0x11000000);

            stl_le_p(data + cursor, header);
            for (j = 1; j < words; j++) {
                uint32_t value = 0xfedc0000 + first + j;

                stl_le_p(data + cursor + 4 * j, value);
                expected.present[first + j - 1] = true;
                expected.value[first + j - 1] = value;
            }
            cursor += words * 4;
        }
        g_assert_cmpuint(cursor, ==, shapes[s].length);
        g_assert_cmpuint(nikon_gate_packet_size(shapes[s].kind), ==, cursor);
        g_assert_true(nikon_gate_packet_decode(shapes[s].kind, data, cursor,
                                               &packet));
        g_assert_cmpmem(&packet, sizeof(packet), &expected, sizeof(expected));
    }
    g_assert_cmpuint(nikon_gate_packet_size(4), ==, 72);
    g_assert_cmpuint(nikon_gate_packet_size(99), ==, 0);
}

static void type5(void)
{
    uint8_t data[396] = { 0 };
    NikonGatePacket packet, saved;
    static const unsigned headers[] = { 0, 16, 340 };
    static const uint32_t commands[] = {
        0x11040000, 0x11510200, 0x01040400,
    };
    unsigned i, bit;

    for (i = 0; i < ARRAY_SIZE(headers); i++) {
        stl_le_p(data + headers[i], commands[i]);
    }
    stl_le_p(data + 4, 8); /* Source-selected upstream resource. */
    stl_le_p(data + 348, 8);
    stl_le_p(data + 352, 0xfe);
    g_assert_true(nikon_gate_packet_decode(5, data, 356, &packet));
    g_assert_cmphex(packet.value[0], ==, 8);
    g_assert_cmphex(packet.value[0x404 / 4], ==, 8);
    g_assert_cmphex(packet.value[0x408 / 4], ==, 0xfe);
    saved = packet;
    memset(data + 356, 0xff, 40); /* CPU trailer is outside command import. */
    g_assert_true(nikon_gate_packet_decode(6, data, 356, &packet));
    g_assert_cmpmem(&packet, sizeof(packet), &saved, sizeof(saved));
    g_assert_false(nikon_gate_packet_decode(5, data, sizeof(data), &packet));
    for (i = 0; i < 356; i++) {
        g_assert_false(nikon_gate_packet_decode(5, data, i, &packet));
        g_assert_cmpmem(&packet, sizeof(packet), &saved, sizeof(saved));
    }
    for (i = 0; i < ARRAY_SIZE(headers); i++) {
        for (bit = 0; bit < 32; bit++) {
            stl_le_p(data + headers[i], commands[i] ^ (1U << bit));
            g_assert_false(nikon_gate_packet_decode(5, data, 356, &packet));
            g_assert_cmpmem(&packet, sizeof(packet), &saved, sizeof(saved));
        }
        stl_le_p(data + headers[i], commands[i]);
    }
}

static void type89(void)
{
    uint8_t data[136] = { 0 };
    NikonGatePacket packet, saved;
    unsigned length, bit, i;

    stl_le_p(data, 0x01220000);
    stl_le_p(data + 4, 0x6a);
    stl_le_p(data + 0x58, 0xc326b400);
    g_assert_true(nikon_gate_packet_decode(9, data, 136, &packet));
    g_assert_cmphex(packet.value[0], ==, 0x6a);
    g_assert_cmphex(packet.value[0x54 / 4], ==, 0xc326b400);
    saved = packet;
    for (length = 0; length < 136; length++) {
        g_assert_false(nikon_gate_packet_decode(9, data, length, &packet));
        g_assert_cmpmem(&packet, sizeof(packet), &saved, sizeof(saved));
    }
    for (bit = 0; bit < 32; bit++) {
        stl_le_p(data, 0x01220000 ^ (1U << bit));
        g_assert_false(nikon_gate_packet_decode(9, data, 136, &packet));
        g_assert_cmpmem(&packet, sizeof(packet), &saved, sizeof(saved));
    }
    memset(data, 0, sizeof(data));
    stl_le_p(data, 0x111c0000);
    stl_le_p(data + 112, 0x01040078);
    stl_le_p(data + 124, 0x89abcdef);
    g_assert_true(nikon_gate_packet_decode(8, data, 128, &packet));
    g_assert_cmphex(packet.value[0x80 / 4], ==, 0x89abcdef);
    for (i = 0; i < NIKON_GATE_REGISTERS; i++) {
        g_assert_cmpint(packet.present[i], ==, i < 27 || (i >= 30 && i <= 32));
    }
    saved = packet;
    for (length = 0; length < 128; length++) {
        g_assert_false(nikon_gate_packet_decode(8, data, length, &packet));
        g_assert_cmpmem(&packet, sizeof(packet), &saved, sizeof(saved));
    }
    for (i = 0; i < 2; i++) {
        unsigned offset = i ? 112 : 0;
        uint32_t header = i ? 0x01040078 : 0x111c0000;

        for (bit = 0; bit < 32; bit++) {
            stl_le_p(data + offset, header ^ (1U << bit));
            g_assert_false(nikon_gate_packet_decode(8, data, 128, &packet));
            g_assert_cmpmem(&packet, sizeof(packet), &saved, sizeof(saved));
        }
        stl_le_p(data + offset, header);
    }
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/nikon/gatepacket/type4", type4);
    g_test_add_func("/nikon/gatepacket/layouts", layouts);
    g_test_add_func("/nikon/gatepacket/type5", type5);
    g_test_add_func("/nikon/gatepacket/type89", type89);
    return g_test_run();
}
