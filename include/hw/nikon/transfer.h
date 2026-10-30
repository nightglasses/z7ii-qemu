/* Owned graphics stream submission, separate from byte-copy completion.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_NIKON_TRANSFER_H
#define HW_NIKON_TRANSFER_H

#include "hw/nikon/devices.h" /* IWYU pragma: keep */

/* Functional allocation bound, not a recovered hardware capacity. */
#define NIKON_TRANSFER_STREAM_BYTES 0x20000U

/* Complete C1.70 LUT/color-conversion profile stream extents. */
#define NIKON_TRANSFER_LUT_SETUP_BYTES 224U
#define NIKON_TRANSFER_LUT_DRAW_BYTES 1688U

/* Finite C1.70 black YUV422 rectangle-clear profile. */
#define NIKON_TRANSFER_CLEAR_SETUP_BYTES 224U
#define NIKON_TRANSFER_CLEAR_DRAW_BYTES 1648U

/* Finite C1.70 black RGBA8888 composition-surface profile. */
#define NIKON_TRANSFER_RGBA_SETUP_BYTES 232U
#define NIKON_TRANSFER_RGBA_DRAW_BYTES 416U

/* Finite C1.70 black RGBA8888 720x480 single-path profile. */
#define NIKON_TRANSFER_RGBA_SINGLE_SETUP_BYTES 176U
#define NIKON_TRANSFER_RGBA_SINGLE_DRAW_BYTES 368U

/* Finite C1.70 gray rectangle using the preceding 720x480 context. */
#define NIKON_TRANSFER_RGBA_GRAY_SETUP_BYTES 168U
#define NIKON_TRANSFER_RGBA_GRAY_DRAW_BYTES 232U

typedef struct NikonTransferStreamRequest {
    uint64_t token;
    uint32_t source;
    /* Count-port units: each unit captures eight source bytes. */
    uint32_t units64;
    GBytes *bytes;
} NikonTransferStreamRequest;

/* Returns a new GBytes reference to an immutable pending submission.
 * Caller must unref bytes; reset, rejection, execution, or unrealize
 * invalidates the token, not a held byte snapshot. The device executes only
 * its finite paired profiles and owns completion/IRQ129; this observer API
 * does not complete or acknowledge a request.
 */
bool nikon_transfer_stream_request(DeviceState *dev, unsigned bank,
                                   NikonTransferStreamRequest *request);

#endif
