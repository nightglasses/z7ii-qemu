/* Descriptor-only CMOS -> processing -> display handoff.
 * No stage generates pixels or announces a first frame.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_NIKON_FRAME_H
#define HW_NIKON_FRAME_H

#include "hw/core/qdev.h"

#define TYPE_NIKON_FRAME_CONTRACT "nikon-frame-contract"
#define NIKON_FRAME_CONTRACT_REGS 0x100

typedef struct NikonFrameDescriptor {
    uint64_t source, destination;
    uint32_t width, height, stride, bytes_per_pixel;
    uint32_t format, bayer, bit_depth, packing, flags;
    uint64_t sequence;
} NikonFrameDescriptor;

typedef enum NikonFrameStage {
    NIKON_FRAME_UNBOUND,
    NIKON_FRAME_CAPTURE_PENDING,
    NIKON_FRAME_CAPTURED,
    NIKON_FRAME_PROCESS_PENDING,
    NIKON_FRAME_PROCESSED,
    NIKON_FRAME_DISPLAY_PENDING,
    NIKON_FRAME_DISPLAYED,
    NIKON_FRAME_ERROR,
} NikonFrameStage;

bool nikon_frame_capture_submit(DeviceState *, const NikonFrameDescriptor *, Error **);
bool nikon_frame_capture_complete(DeviceState *, Error **);
bool nikon_frame_process_submit(DeviceState *, const NikonFrameDescriptor *, Error **);
bool nikon_frame_process_complete(DeviceState *, Error **);
bool nikon_frame_display_submit(DeviceState *, const NikonFrameDescriptor *, Error **);
bool nikon_frame_display_complete(DeviceState *, Error **);
void nikon_frame_reset(DeviceState *);
NikonFrameStage nikon_frame_stage(DeviceState *);
const NikonFrameDescriptor *nikon_frame_capture_descriptor(DeviceState *);
const NikonFrameDescriptor *nikon_frame_process_descriptor(DeviceState *);
const NikonFrameDescriptor *nikon_frame_display_descriptor(DeviceState *);

#endif
