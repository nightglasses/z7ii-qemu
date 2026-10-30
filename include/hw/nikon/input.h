/*
 * EX GPIO scan and maintained I2C-expander capture-selector input.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_NIKON_INPUT_H
#define HW_NIKON_INPUT_H

#include "hw/core/qdev.h"

#define TYPE_NIKON_INPUT "nikon-input"
#define NIKON_INPUT_PADS 17
#define NIKON_INPUT_EXPANDER_PADS 16

/* Named GPIO input pad indices backed by original EX sampling. */
#define NIKON_INPUT_PAD_MAIN_POWER 3  /* PC5, 0x25, KEY_F4: Main power switch */
#define NIKON_INPUT_PAD_HALF_PRESS 9  /* PG1, 0x61, KEY_F10: Shutter S1 */
#define NIKON_INPUT_PAD_AF_ON      10 /* PG2, 0x62, KEY_F11: AF-ON button */
#define NIKON_INPUT_PAD_FULL_PRESS 11 /* PG13, 0x6d, KEY_F12: Shutter S2 */

/* EX registry IDs from the original 0803de88 initializer. Each output is an
 * external electrical level; the guest performs sampling and debouncing.
 * Connect "pad"[i] to EX GPIO [id >> 4][id & 15] before realize.
 * Connect "capture-selector"[0] to EX I2C expander pad0: high is photo,
 * low is movie. F13 toggles this maintained contact; a new input starts photo.
 * Reset retains the host contact and key latch; unrealize releases the line.
 */
extern const uint8_t nikon_input_gpio_ids[NIKON_INPUT_PADS];

/* Connect "expander-pad"[i] to the EX I2C0 0x22 external pad whose ID is
 * nikon_input_expander_ids[i]. These sixteen independent contacts are active
 * low and initially released high. Reset retains held contacts; unrealize
 * releases every line with -1. Pad0 remains the separate maintained selector.
 */
extern const uint8_t nikon_input_expander_ids[NIKON_INPUT_EXPANDER_PADS];

#endif
