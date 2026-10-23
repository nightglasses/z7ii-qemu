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
#define TYPE_NIKON_SDES_CHANNELS "nikon-sdes-channels"

#define NIKON_SDES_CHANNEL_COUNT 11
#define NIKON_SDES_CHANNEL_SIZE 0x2fc

/* Independent configuration/control family, original VA20063000.
 * Channel indices follow the original selector table (1,2,4,...,400 hex).
 * offset is relative to that channel's configuration base. Each explicit
 * command write pulses command-publish[index]; it produces no completion.
 */
bool nikon_sdes_channels_configuration(DeviceState *dev, unsigned channel,
                                       unsigned offset, uint32_t *value);
/* Global configuration offset is relative to the MMIO base, not a channel. */
bool nikon_sdes_channels_global_configuration(DeviceState *dev, unsigned offset,
                                              uint32_t *value);

enum NikonReadoutRegion {
    NIKON_READOUT_BUFFERS,       /* VA20069800, size0x384 */
    NIKON_READOUT_PARAMETERS,    /* VA2006b000, size0x404 */
    NIKON_READOUT_REGIONS,
};

/* One accepted guest completion-marker write.  The generation is a
 * monotonically increasing producer token; it is never a hardware address or
 * a completion result.  Providers use it to reject stale observations.
 */
typedef struct NikonReadoutRequest {
    uint64_t generation;
    uint32_t ready;
    uint32_t completion_marker;
} NikonReadoutRequest;

/* Explicit functional interpretation of the C1.70 record at 2006b3c8.
 * This profile contract does not claim a physical pixel encoding. */
#define NIKON_READOUT_C170_FORMAT_PRIMARY 0x00400040u
#define NIKON_READOUT_C170_FORMAT_ALTERNATE 0x00010001u

typedef struct NikonReadoutC170Mapping {
    uint64_t generation;
    uint32_t destination;
    uint32_t stride;
    uint32_t span;
    uint32_t secondary_destination;
    uint32_t secondary_stride;
    uint32_t format;
    uint32_t width;
    uint32_t height;
} NikonReadoutC170Mapping;

bool nikon_readout_c170_mapping(DeviceState *dev, uint64_t generation,
                                NikonReadoutC170Mapping *mapping,
                                Error **errp);

/* Actual query102092fa selects0..3 and reads4/2/4/2 words.  No default
 * provider, completion, IRQ, DMA or pixel data is produced by configuration.
 * An explicit command2006b400=1 creates a request generation and pulses
 * readout-request.  External providers may inspect that request and publish
 * ready/completion observations through the APIs below. The machine only
 * supplies a provider in the complete bundle composition.
 */
bool nikon_readout_request(DeviceState *dev, NikonReadoutRequest *request);
bool nikon_readout_publish_ready(DeviceState *dev, uint64_t generation);
bool nikon_readout_publish_completion(DeviceState *dev, uint64_t generation);
bool nikon_readout_publish(DeviceState *dev, unsigned selector,
                           const uint32_t *words, size_t count, Error **errp);
bool nikon_readout_configuration(DeviceState *dev, enum NikonReadoutRegion region,
                                 unsigned offset, uint32_t *value);

/* Original configuration/commit family at VA20068000, IRQ94..98.
 * One region of size0x1180. Status publication word counts are2/1/2/2/1.
 * External publication owns data causes. The08/0c functional run/stop profile
 * also produces source-mapped quiescence causes through deferred timers;
 * it implies no pixel encoding, DMA destination or completed frame.
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

/* Runtime observations for the external source and timing state machine. */
bool nikon_sensor_provider_bound(DeviceState *dev);
bool nikon_sensor_configured(DeviceState *dev);

#endif
