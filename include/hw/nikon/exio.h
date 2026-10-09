/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HW_NIKON_EXIO_H
#define HW_NIKON_EXIO_H

#include "hw/i2c/i2c.h"

#define TYPE_NIKON_EX_IO "nikon-ex-io"
OBJECT_DECLARE_SIMPLE_TYPE(NikonEXIOState, NIKON_EX_IO)
#define NIKON_EX_IO_PINS 24

/* pad inputs: 0/1 drive a level, -1 releases the external driver.
 * pad-out outputs: 0/1 push-pull drive, -1 high impedance.
 * interrupt is active low.
 * reset is active low. No camera button identity is assigned to these pins.
 */
#endif
