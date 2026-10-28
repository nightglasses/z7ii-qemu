/*
 * Finite continuous-graph execution, without scheduling or event ownership.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef NIKON_GATE_WORK_H
#define NIKON_GATE_WORK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "system/memory.h"
#include "hw/nikon/gatepacket.h"

typedef struct NikonGateWorkCommand {
    uint32_t offset;
    const NikonGatePacket *packet;
    uint32_t auxiliary[2];
} NikonGateWorkCommand;

typedef struct NikonGateWork {
    AddressSpace *as;
    const uint32_t *config;             /* 0x4080 / 4 words */
    const uint32_t (*slots)[13];
    uint16_t parameter_valid;
    const NikonGateWorkCommand *commands;
    size_t command_count;
    const uint8_t *input;               /* Owned, tightly packed NV12 */
    size_t input_size;
    unsigned width, height, phase;
} NikonGateWork;

typedef enum NikonGateWorkResult {
    NIKON_GATE_WORK_OK,
    NIKON_GATE_WORK_CONFIG,
    NIKON_GATE_WORK_TOPOLOGY,
    NIKON_GATE_WORK_FORMAT,
    NIKON_GATE_WORK_GRID,
    NIKON_GATE_WORK_AFFINE,
    NIKON_GATE_WORK_MEMORY,
    NIKON_GATE_WORK_ALIAS,
    NIKON_GATE_WORK_PIXELS,
} NikonGateWorkResult;

typedef struct NikonGateWorkPlan NikonGateWorkPlan;

/* Prepare/publish require the main thread; compute touches only owned data.
 * Every failure leaves guest RAM unchanged. Publish checks CURRENT layout and
 * RAM backing, not just the saved destination addresses.
 */
NikonGateWorkResult nikon_gate_work_prepare(const NikonGateWork *work,
                                           NikonGateWorkPlan **plan);
NikonGateWorkResult nikon_gate_work_compute(NikonGateWorkPlan *plan);
NikonGateWorkResult nikon_gate_work_publish(NikonGateWorkPlan *plan,
                                           const NikonGateWork *current);
void nikon_gate_work_free(NikonGateWorkPlan *plan);
/* Synchronous composition of the same three operations. */
NikonGateWorkResult nikon_gate_work_execute(const NikonGateWork *work);
const char *nikon_gate_work_result_name(NikonGateWorkResult result);

#endif
