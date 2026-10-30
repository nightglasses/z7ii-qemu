/*
 * Finite unit8 confidence-grid operation; no scheduling or event ownership.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef NIKON_GATE_PLANE_H
#define NIKON_GATE_PLANE_H

#include "hw/nikon/gatework.h"

typedef struct NikonGatePlaneOutput {
    uint32_t address, width, height, stride;
} NikonGatePlaneOutput;

typedef struct NikonGatePlaneWork {
    AddressSpace *as;
    const uint32_t *config;             /* 0x4080 / 4 words */
    const uint32_t (*slots)[13];
    uint16_t parameter_valid;
    const NikonGateWorkCommand *commands;
    size_t command_count;
    const uint16_t (*table)[1024];      /* Six channel/parity banks */
    unsigned phase, width, height;
    const uint16_t *rgb;                /* Immutable, tight three planes */
    uint32_t input_address[3], input_stride[3];
    /* Runs after computation/preflight, immediately before the first DMA. */
    bool (*begin)(void *opaque, const NikonGatePlaneOutput *output);
    void *opaque;
} NikonGatePlaneWork;

/*
 * Failure leaves guest RAM and output unchanged. Caller holds RAM topology.
 * A false begin returns CONFIG; invalid work never calls begin.
 */
NikonGateWorkResult nikon_gate_plane_execute(const NikonGatePlaneWork *work,
                                            NikonGatePlaneOutput *output);

#endif
