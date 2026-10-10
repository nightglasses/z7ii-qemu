/* Source-bounded frame save handoff. SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HW_NIKON_SAVE_H
#define HW_NIKON_SAVE_H

#include "hw/core/qdev.h"
#include "qapi/error.h"

#define TYPE_NIKON_SAVE "nikon-save"
OBJECT_DECLARE_SIMPLE_TYPE(NikonSaveState, NIKON_SAVE)

/* The original C1.70 save evidence reaches a local file request worker at
 * 1044b300 (command 42091001), a sector callback at 1047e768, and the
 * Block-I/O bridge at 104d5a3a. These values describe the handoff only; this
 * device does not assign a format or create media bytes.
 */
typedef enum NikonSaveFormat {
    NIKON_SAVE_FORMAT_UNKNOWN,
    NIKON_SAVE_FORMAT_RAW,
    NIKON_SAVE_FORMAT_JPEG,
} NikonSaveFormat;

typedef enum NikonSavePhase {
    NIKON_SAVE_IDLE,
    NIKON_SAVE_REQUESTED,
    NIKON_SAVE_COMMITTED,
    NIKON_SAVE_FAILED,
    NIKON_SAVE_CANCELLED,
} NikonSavePhase;

typedef struct NikonSaveRequest {
    uint64_t token;
    uint32_t source;
    uint32_t length;
    NikonSaveFormat format;
} NikonSaveRequest;

typedef struct NikonSaveStatus {
    NikonSavePhase phase;
    uint64_t requests;
    uint64_t commits;
    uint64_t failures;
    uint64_t token;
    uint32_t source;
    uint32_t length;
    NikonSaveFormat format;
    uint32_t error;
} NikonSaveStatus;

/* Host-side handoff contract. All functions are synchronous state updates;
 * durable I/O belongs to a later source-backed writer. In particular, a
 * commit records completion but never writes a file or manufactures an image.
 */
bool nikon_save_request(DeviceState *dev, const NikonSaveRequest *request,
                        Error **errp);
bool nikon_save_commit(DeviceState *dev, uint64_t token, Error **errp);
bool nikon_save_fail(DeviceState *dev, uint64_t token, uint32_t error,
                     Error **errp);
bool nikon_save_cancel(DeviceState *dev, uint64_t token, Error **errp);
bool nikon_save_status(DeviceState *dev, NikonSaveStatus *status,
                       Error **errp);

#endif
