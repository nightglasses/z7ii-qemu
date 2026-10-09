/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HW_NIKON_COMMAND_H
#define HW_NIKON_COMMAND_H

#include "hw/core/qdev.h"

#define TYPE_NIKON_COMMAND "nikon-command"
#define NIKON_COMMAND_BANKS 3
#define NIKON_COMMAND_ENGINES 8
#define NIKON_COMMAND_WORDS 16

typedef struct NikonCommandRequest {
    uint64_t token;
    uint16_t slots;
    uint16_t parameter;
    uint16_t extension;
    uint16_t written;
    uint8_t control;
    uint32_t words[NIKON_COMMAND_WORDS];
} NikonCommandRequest;

/* Providers consume actual guest words. Reading a request has no side effects.
 * Completion requires a live request; receive requires a free engine. Neither
 * operation interprets a frame or supplies a peer response on its own. */
bool nikon_command_request(DeviceState *dev, unsigned bank, unsigned engine,
                           NikonCommandRequest *request);
bool nikon_command_complete(DeviceState *dev, unsigned bank, unsigned engine,
                            uint64_t token, uint16_t causes);
bool nikon_command_receive(DeviceState *dev, unsigned bank, unsigned engine,
                           unsigned slot, const uint32_t *words, unsigned count);

#endif
