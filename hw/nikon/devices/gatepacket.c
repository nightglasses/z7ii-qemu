/*
 * Source-shaped register writes for the original processing command loaders.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "hw/nikon/gatepacket.h"

typedef struct GateBlock {
    uint16_t offset;
    uint8_t words; /* Includes the command header. */
} GateBlock;

typedef struct GateLayout {
    unsigned kind, count;
    GateBlock block[8];
} GateLayout;

/*
 * MAS103b2fa8/3226/3f04/4214/4346/46ec/49b0 and the observed client
 * structures. These finite layouts do not decode the register arithmetic.
 */
static const GateLayout layouts[] = {
    { 4, 2, { { 4, 2 }, { 0x10, 16 } } },
    { 7, 6, { { 0, 22 }, { 0x64, 5 }, { 0x80, 10 }, { 0xc0, 10 },
              { 0x100, 10 }, { 0x22c, 5 } } },
    { 10, 3, { { 0, 10 }, { 0x40, 8 }, { 0x80, 17 } } },
    { 11, 3, { { 0, 10 }, { 0x40, 8 }, { 0x80, 17 } } },
    { 12, 8, { { 0, 7 }, { 0x20, 2 }, { 0x30, 6 }, { 0x50, 6 },
               { 0x70, 6 }, { 0x90, 6 }, { 0xb0, 6 }, { 0xd0, 5 } } },
    { 15, 2, { { 0, 3 }, { 0x10, 14 } } },
    { 17, 4, { { 0, 2 }, { 0x20, 15 }, { 0x60, 5 }, { 0x80, 7 } } },
};

static const GateLayout *gate_layout(unsigned kind)
{
    unsigned i;

    for (i = 0; i < ARRAY_SIZE(layouts); i++) {
        if (layouts[i].kind == kind) {
            return &layouts[i];
        }
    }
    return NULL;
}

size_t nikon_gate_packet_size(unsigned kind)
{
    const GateLayout *layout = gate_layout(kind);
    size_t length = 0;
    unsigned i;

    if (layout) {
        for (i = 0; i < layout->count; i++) {
            length += layout->block[i].words * 4;
        }
    }
    return length;
}

bool nikon_gate_packet_decode(unsigned kind, const uint8_t *data, size_t length,
                              NikonGatePacket *result)
{
    const GateLayout *layout = gate_layout(kind);
    NikonGatePacket decoded = { 0 };
    unsigned i, j;
    size_t cursor = 0;

    if (!layout || !data || length != nikon_gate_packet_size(kind)) {
        return false;
    }
    for (i = 0; i < layout->count; i++) {
        const GateBlock *block = &layout->block[i];
        uint32_t expected = block->offset | (block->words << 16) |
                            (i + 1 == layout->count ? 0x01000000 : 0x11000000);

        if (ldl_le_p(data + cursor) != expected) {
            return false;
        }
        for (j = 1; j < block->words; j++) {
            unsigned index = block->offset / 4 + j - 1;

            decoded.value[index] = ldl_le_p(data + cursor + j * 4);
            decoded.present[index] = true;
        }
        cursor += block->words * 4;
    }
    *result = decoded;
    return true;
}
