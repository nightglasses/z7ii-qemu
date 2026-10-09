/* EX GPIO scan input, not a camera button-name assignment.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_NIKON_INPUT_H
#define HW_NIKON_INPUT_H

#include "hw/core/qdev.h"

#define TYPE_NIKON_INPUT "nikon-input"
#define NIKON_INPUT_PADS 17

/* EX registry IDs from the original 0803de88 initializer. Each output is an
 * external electrical level; the guest performs sampling and debouncing.
 * Connect "pad"[i] to EX GPIO [id >> 4][id & 15] before realize.
 */
extern const uint8_t nikon_input_gpio_ids[NIKON_INPUT_PADS];

#endif
