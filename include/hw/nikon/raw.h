/* Explicit virtual sensor encoding, not a recovered silicon CFA specification.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_NIKON_RAW_H
#define HW_NIKON_RAW_H

typedef enum NikonRawEncoding {
    NIKON_RAW_MONO,
    NIKON_RAW_RGGB,
    NIKON_RAW_GRBG,
    NIKON_RAW_GBRG,
    NIKON_RAW_BGGR,
} NikonRawEncoding;

#endif
