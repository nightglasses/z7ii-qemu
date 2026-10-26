/*
 * EX GPIO scan and maintained I2C-expander capture-selector input.
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
 * Connect "capture-selector"[0] to EX I2C expander pad0: high is photo,
 * low is movie. F13 toggles this maintained contact; a new input starts photo.
 * Reset retains the host contact and key latch; unrealize releases the line.
 */
extern const uint8_t nikon_input_gpio_ids[NIKON_INPUT_PADS];

#endif
