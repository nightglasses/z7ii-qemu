/*
 * Completed functional monochrome RAW DMA, not a sensor-ready notification.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_NIKON_PAIRED_H
#define HW_NIKON_PAIRED_H

#include "hw/core/qdev.h"

typedef struct NikonPairedRawFrame {
    uint64_t generation;
    uint32_t address, width, height, stride, bits;
    unsigned slot;
} NikonPairedRawFrame;

/*
 * NULL invalidates the source epoch. A zero generation withdraws one slot
 * before a producer starts overwriting its RAM; completed snapshots survive.
 */
void nikon_paired_frame(DeviceState *dev, const NikonPairedRawFrame *frame);

#endif
