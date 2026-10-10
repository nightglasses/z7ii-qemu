/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HW_NIKON_LOOKUP_H
#define HW_NIKON_LOOKUP_H

#include "hw/core/qdev.h"

#define TYPE_NIKON_LOOKUP_BYTES "nikon-lookup-bytes"
#define TYPE_NIKON_LOOKUP_WORDS "nikon-lookup-words"
#define NIKON_LOOKUP_TABLES 4
#define NIKON_LOOKUP_TABLE_SIZE 64

/* One complete external observation, never a fabricated processing result.
 * Pending words append causes until the guest acknowledges them. Lane values
 * are the high result field, 8 bits for bytes and 2 bits for words. The last
 * four fields exist only in the word interface and must be zero for bytes.
 * A real sample with busy's sign bit clear retires owned operations; merely
 * clearing a guest submit field or disconnecting a provider does not. */
typedef struct NikonLookupSample {
    uint32_t pending;
    uint32_t busy;
    uint32_t count;
    uint32_t result;
    uint32_t lane[2];
    uint32_t auxiliary_pending;
    uint32_t auxiliary_result;
    uint32_t selection;
    uint32_t flags;
    uint32_t available;
    uint32_t extent;
} NikonLookupSample;

bool nikon_lookup_publish(DeviceState *dev, const NikonLookupSample *sample);
void nikon_lookup_disconnect(DeviceState *dev);
/* Export the actual 64 source bytes only after all bytes were staged. The
 * word interface reverses its guest's explicit four-byte packing on export. */
bool nikon_lookup_table(DeviceState *dev, unsigned table,
                        uint8_t data[NIKON_LOOKUP_TABLE_SIZE]);

#endif
