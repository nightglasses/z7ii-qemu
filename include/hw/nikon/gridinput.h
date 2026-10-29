/* Actual RGB-statistics DMA and grid-output publication, under the BQL.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef NIKON_GRID_INPUT_H
#define NIKON_GRID_INPUT_H

#include "hw/core/qdev.h"
#include "hw/nikon/paired.h"

typedef struct NikonGatePlaneInput {
    uint64_t generation;
    uint32_t address[3], stride[3], width, height;
} NikonGatePlaneInput;

/* NULL invalidates the source epoch. Zero generation withdraws these addresses
 * before their producer overwrites RAM. A positive generation follows all DMA.
 */
bool nikon_gate_plane_input(DeviceState *dev, const NikonGatePlaneInput *input);

/* The grid-ready GPIO reports this retained publication/withdrawal descriptor.
 * Its cause is actual device work, not a software-event or ready-state setter.
 */
bool nikon_gate_plane_output(DeviceState *dev, NikonPairedGridFrame *frame);

#endif
