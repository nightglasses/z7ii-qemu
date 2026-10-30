/*
 * Completed functional mono/CFA RAW DMA, not a sensor-ready notification.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_NIKON_PAIRED_H
#define HW_NIKON_PAIRED_H

#include "hw/core/qdev.h"
#include "hw/nikon/raw.h"

typedef enum NikonRawCodec {
    NIKON_RAW_LINEAR,
    NIKON_RAW_LOOKUP8,
} NikonRawCodec;

/* Frozen producer transfer, not pixels. LOOKUP8 codes are exact table knots;
 * reconstruction uses the declared P6 endpoint convention (index*257)>>4.
 */
typedef struct NikonRawTransfer {
    NikonRawCodec codec;
    uint16_t lookup[256];
} NikonRawTransfer;

typedef struct NikonPairedRawFrame {
    uint64_t generation;
    uint32_t address, width, height, stride, bits;
    NikonRawEncoding encoding;
    NikonRawTransfer transfer;
    unsigned slot;
} NikonPairedRawFrame;

/*
 * NULL invalidates the source epoch. A zero generation withdraws one slot
 * before a producer starts overwriting its RAM; completed snapshots survive.
 */
void nikon_paired_frame(DeviceState *dev, const NikonPairedRawFrame *frame);

typedef struct NikonPairedGridFrame {
    uint64_t generation;
    uint32_t address, width, height, stride;
} NikonPairedGridFrame;

/* Completed packed12 confidence-map DMA. NULL invalidates the producer epoch;
 * generation zero withdraws this address before overwrite. Owned copies survive.
 * At most eight distinct addresses belong to one epoch; a new pool requires
 * NULL invalidation. Slots stay stable so withdrawal cannot erase consumption.
 */
void nikon_paired_grid_frame(DeviceState *dev,
                            const NikonPairedGridFrame *frame);

#endif
