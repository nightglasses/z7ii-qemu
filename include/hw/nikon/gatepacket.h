/*
 * Finite MAS processing-register packet layouts, not a pixel processor.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef NIKON_GATE_PACKET_H
#define NIKON_GATE_PACKET_H

#define NIKON_GATE_REGISTERS (0x240 / 4)
#define NIKON_GATE_PACKET_MAX 248

typedef struct NikonGatePacket {
    uint32_t value[NIKON_GATE_REGISTERS];
    bool present[NIKON_GATE_REGISTERS];
} NikonGatePacket;

size_t nikon_gate_packet_size(unsigned kind);
bool nikon_gate_packet_decode(unsigned kind, const uint8_t *data, size_t length,
                              NikonGatePacket *result);

#endif
