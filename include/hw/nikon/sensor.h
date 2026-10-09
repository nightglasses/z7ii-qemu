/* External CMOS frame source. Controller register decoding remains separate.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_NIKON_SENSOR_H
#define HW_NIKON_SENSOR_H

#include "hw/core/qdev.h"

#define TYPE_NIKON_SENSOR "nikon-sensor"

/* Opaque input rows: this interface does not assign a Bayer or packing format.
 * The controller must supply its actual byte geometry and physical destination.
 * Durations are an explicit functional input in virtual nanoseconds.
 */
typedef struct NikonSensorFrame {
    uint32_t row_bytes;
    uint32_t rows;
    uint32_t stride;
    uint64_t destination;
    uint64_t exposure_ns;
    uint64_t readout_ns;
} NikonSensorFrame;

enum NikonSensorPhase {
    NIKON_SENSOR_IDLE,
    NIKON_SENSOR_EXPOSING,
    NIKON_SENSOR_READING,
    NIKON_SENSOR_DONE,
    NIKON_SENSOR_ERROR,
};

/* Output levels, not unbound Nikon GIC lines or physical sensor pin numbers. */
enum NikonSensorOutput {
    NIKON_SENSOR_EXPOSURE_ACTIVE,
    NIKON_SENSOR_READOUT_ACTIVE,
    NIKON_SENSOR_FRAME_DONE,
    NIKON_SENSOR_FRAME_ERROR,
    NIKON_SENSOR_OUTPUTS,
};

bool nikon_sensor_begin_frame(DeviceState *dev, const NikonSensorFrame *frame,
                              Error **errp);
void nikon_sensor_cancel_frame(DeviceState *dev);

#endif
