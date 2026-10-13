/* Request-only program bootstrap boundary; no processor ISA or reply model.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_NIKON_PROCESSOR_H
#define HW_NIKON_PROCESSOR_H

#include "hw/nikon/devices.h"

#define NIKON_PROCESSOR_BOOTSTRAP_SIZE 0x4000

typedef struct NikonProcessorRequest {
    uint64_t token;
    uint32_t configuration[6]; /* Offsets 00/04/08/0c/10/14. */
    uint32_t written;
    uint8_t bootstrap[NIKON_PROCESSOR_BOOTSTRAP_SIZE];
} NikonProcessorRequest;

/* Side-effect-free copy of an owned pending attempt. Stop/reset invalidates it.
 * No API supplies status, a completion, message, IRQ or a guest ready word.
 */
bool nikon_processor_request(DeviceState *dev, NikonProcessorRequest *request);

#endif
