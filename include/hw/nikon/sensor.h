/* External CMOS frame source. Controller register decoding remains separate.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_NIKON_SENSOR_H
#define HW_NIKON_SENSOR_H

#include "hw/core/qdev.h"

#define TYPE_NIKON_SENSOR "nikon-sensor"
#define TYPE_NIKON_SDES "nikon-sdes"
#define TYPE_NIKON_CAPTURE_CONTROL "nikon-capture-control"
#define TYPE_NIKON_READOUT_CONTROL "nikon-readout-control"

enum NikonReadoutRegion {
    NIKON_READOUT_BUFFERS,       /* VA20069800, size0x384 */
    NIKON_READOUT_PARAMETERS,    /* VA2006b000, size0x404 */
    NIKON_READOUT_REGIONS,
};

/* Actual query102092fa selects0..3 and reads4/2/4/2 words.
 * No observed data, request completion or IRQ is produced by configuration.
 * Explicit command2006b400=1 pulses named readout-request; no default provider.
 */
bool nikon_readout_publish(DeviceState *dev, unsigned selector,
                           const uint32_t *words, size_t count, Error **errp);
bool nikon_readout_configuration(DeviceState *dev, enum NikonReadoutRegion region,
                                 unsigned offset, uint32_t *value);

/* Original configuration/commit family at VA20068000, IRQ94..98.
 * One region of size0x1180. Status publication word counts are2/1/2/2/1.
 * No pixel encoding, destination or completion meaning is assigned here.
 */
enum NikonCaptureEvent {
    NIKON_CAPTURE_FIRST_EVENT,
    NIKON_CAPTURE_SECOND_EVENT,
    NIKON_CAPTURE_THIRD_EVENT,
    NIKON_CAPTURE_FOURTH_EVENT,
    NIKON_CAPTURE_FIFTH_EVENT,
    NIKON_CAPTURE_EVENTS,
};

bool nikon_capture_publish(DeviceState *dev, enum NikonCaptureEvent event,
                           const uint32_t *words, size_t count, Error **errp);
bool nikon_capture_configuration(DeviceState *dev, unsigned offset,
                                 uint32_t *value);

/* SDES Driver's three independently mapped regions, in this order. */
enum NikonSdesRegion {
    NIKON_SDES_CONFIGURATION,
    NIKON_SDES_STATUS,
    NIKON_SDES_PARAMETERS,
    NIKON_SDES_REGIONS,
};

/* Five original handlers, registered to IRQ89 through IRQ93 respectively.
 * Publication supplies an actual external event's complete status snapshot;
 * it does not identify a frame, exposure, data format or physical sensor.
 */
enum NikonSdesEvent {
    NIKON_SDES_STATUS_EVENT,
    NIKON_SDES_PRIMARY_EVENT,
    NIKON_SDES_PRIMARY_DETAIL_EVENT,
    NIKON_SDES_SECONDARY_EVENT,
    NIKON_SDES_SECONDARY_DETAIL_EVENT,
    NIKON_SDES_EVENTS,
};

bool nikon_sdes_publish(DeviceState *dev, enum NikonSdesEvent event,
                        const uint32_t *words, size_t count, Error **errp);

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
