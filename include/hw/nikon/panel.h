/*
 * Functional LCD scanout observer for the MAS composition buffer.
 *
 * The observer only reads guest RAM and publishes a QEMU console surface.  It
 * creates no guest register, IRQ, completion or status, and it never writes to
 * guest memory.  Pixel format and active-row geometry come from the transfer
 * device's documented RGBA program, not from a recovered panel specification.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_NIKON_PANEL_H
#define HW_NIKON_PANEL_H

#include "hw/core/qdev.h"

#define TYPE_NIKON_PANEL "nikon-panel-functional"

/*
 * Convert one RGBA8888 row-major image into the little-endian 0xAARRGGBB
 * words a 32-bit console surface stores (byte order B, G, R, A).
 */
void nikon_panel_convert_rgba8888(uint32_t *dst, const uint8_t *src,
                                  size_t pixels);

#endif
