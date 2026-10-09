/* Explicit functional update peer. SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HW_NIKON_UPDATE_H
#define HW_NIKON_UPDATE_H

#include "hw/core/qdev.h"
#include "hw/nikon/peer.h"

#define TYPE_NIKON_UPDATE_PEER "nikon-update-peer"
OBJECT_DECLARE_SIMPLE_TYPE(NikonUpdatePeer, NIKON_UPDATE_PEER)

/* Owns a peer frontend and one realized per-record input until unrealize.
 * This native profile never attaches the original SCI or CPU resident slots.
 */
DeviceState *nikon_update_peer_create(Object *owner, NikonPeer *peer,
                                     DeviceState *input, uint32_t stream_size,
                                     const char *origin, Error **errp);

#endif
