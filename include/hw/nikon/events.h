/* Three-cause event latch with an explicit external producer.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_NIKON_EVENTS_H
#define HW_NIKON_EVENTS_H

#include "hw/core/qdev.h"

#define TYPE_NIKON_EVENTS "nikon-events-functional"

/* One MMIO region (16 bytes), one SysBus IRQ output. An external provider
 * explicitly publishes already-produced cause bits 1..3 while holding BQL.
 * No edge/level, elapsed time or register read is an event source.
 * Enable bits 10..12 select those three causes. Status is W0C,
 * matching the original handler's read/XOR-0xe/write acknowledgement.
 * The correspondence and cold masked state are functional conventions,
 * not a claim of physical IP identity or reset values.
 */
bool nikon_events_publish(DeviceState *dev, uint32_t causes, Error **errp);

#endif
