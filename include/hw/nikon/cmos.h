/* Guest-configured CMOS transport endpoint.
 *
 * The C1.70 evidence does not bind a sensor register map, pixel encoding or
 * completion wire.  This interface therefore keeps those values opaque while
 * providing a deterministic frame transport for an explicitly configured
 * guest request.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_NIKON_CMOS_H
#define HW_NIKON_CMOS_H

#include "hw/core/qdev.h"

#define TYPE_NIKON_CMOS "nikon-cmos-functional"
#define NIKON_CMOS_REGS 0x1000

enum NikonCmosPhase {
    NIKON_CMOS_IDLE,
    NIKON_CMOS_EXPOSING,
    NIKON_CMOS_READING,
    NIKON_CMOS_DONE,
    NIKON_CMOS_ERROR,
};

enum NikonCmosEvent {
    NIKON_CMOS_EVENT_DONE = 1u << 0,
    NIKON_CMOS_EVENT_ERROR = 1u << 1,
    NIKON_CMOS_EVENT_CANCEL = 1u << 2,
};

/* Cancellation is a guest command; no physical sensor pin or IRQ number is
 * assigned here.  A machine may wire the single SysBus IRQ if a source-bound
 * consumer is recovered later. */
void nikon_cmos_cancel(DeviceState *dev);

/* The diagnostic CMOS transport is inert until a source provider is bound.
 * `image` accepts an explicit binary PGM/PPM input; the guest still supplies
 * geometry, Bayer order, packing, timing and destination buffers. The fixture
 * qtests opt in to the deterministic byte source through `fixture-provider`.
 */
bool nikon_cmos_provider_bound(DeviceState *dev);

#endif
