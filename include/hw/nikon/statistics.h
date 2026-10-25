/*
 * Finite capture processing profile; numerical semantics are functional.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_NIKON_STATISTICS_H
#define HW_NIKON_STATISTICS_H

#include "hw/core/qdev.h"

#define TYPE_NIKON_STATISTICS "nikon-capture-statistics"
#define NIKON_CAPTURE_ENDPOINT_WORDS (0xb84 / 4)

typedef struct NikonCaptureInput {
    uint64_t generation;
    uint32_t width, height, stride;
    const uint8_t *data;
    /* Unit1 input qualification held from CMOS submission to completion. */
    bool synchronized;
} NikonCaptureInput;

/* Borrowed immutable completed DMA bytes, valid until the next completion. */
bool nikon_readout_pipeline_input(DeviceState *dev, NikonCaptureInput *input);
void nikon_capture_endpoints_snapshot(
    DeviceState *dev, uint32_t words[NIKON_CAPTURE_ENDPOINT_WORDS]);
void nikon_capture_endpoints_results(DeviceState *dev, const uint32_t words[4]);
void nikon_capture_banks_results(DeviceState *dev, unsigned banks,
                                 const uint32_t words[12]);
bool nikon_capture_data_pending(DeviceState *dev);

#endif
