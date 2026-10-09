/* Explicit processing results; this boundary does not implement a codec.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_NIKON_PROCESSING_H
#define HW_NIKON_PROCESSING_H

#include "hw/core/qdev.h"

#define TYPE_NIKON_PROCESSING "nikon-processing-functional"

/* Slots0..3 correspond to470/474/478/47c. Caller holds the BQL and supplies
 * one complete, externally produced output buffer. The guest's corresponding
 * capacity494/49c snapshotted at start must equal length. Stops, resets and
 * flush cancel pending requests. The provided slot uses that same snapshot.
 * The original processing transform and sensor connection remain external.
 */
bool nikon_processing_publish(DeviceState *dev, unsigned slot,
                              const uint8_t *data, size_t length, Error **errp);

#endif
