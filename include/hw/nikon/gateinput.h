/*
 * Finite paired MAIN to gate type0 input transport, not a pixel processor.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef NIKON_GATE_INPUT_H
#define NIKON_GATE_INPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "hw/core/qdev.h"

typedef struct NikonGateInputShape {
    uint32_t width, height, stride;
} NikonGateInputShape;

typedef enum NikonGateInputStatus {
    NIKON_GATE_INPUT_UNSUPPORTED,
    NIKON_GATE_INPUT_READY,
    NIKON_GATE_INPUT_BUSY,
} NikonGateInputStatus;

typedef struct NikonGateInputVersion {
    /* Cancellation changes epoch; successful consumption changes generation. */
    uint64_t epoch, generation;
} NikonGateInputVersion;

/* Pure capacity read, including the current version while BUSY/unsupported.
 * Tokens expire on consumption, configuration withdrawal, stop or reset.
 * Named output input-capacity pulses on release or epoch change; its sink must
 * defer rechecking because configuration writes may still be in progress.
 */
NikonGateInputStatus nikon_gate_input_status(DeviceState *dev,
    const NikonGateInputShape *shape, NikonGateInputVersion *version);
bool nikon_gate_input_publish(DeviceState *dev, uint64_t generation,
    const NikonGateInputShape *shape, const uint8_t *y, size_t y_size,
    const uint8_t *uv, size_t uv_size);
/* Read the complete tight NV12 snapshot without consuming it. */
bool nikon_gate_input_copy(DeviceState *dev, uint64_t generation,
    uint8_t *y, size_t y_size, uint8_t *uv, size_t uv_size);

#endif
