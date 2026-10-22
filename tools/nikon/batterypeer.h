/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef NIKON_BATTERY_PEER_H
#define NIKON_BATTERY_PEER_H

#include "battery.h"
#include <stddef.h>

typedef struct NikonBatteryProfile {
    /* Identity/class/opaque12, status7, metrics7; all multi-byte fields LE. */
    uint8_t bytes[26];
} NikonBatteryProfile;

typedef enum NikonBatteryPeerPhase {
    NIKON_BATTERY_PEER_UNINITIALIZED,
    NIKON_BATTERY_PEER_AUTH,
    NIKON_BATTERY_PEER_PROFILE,
    NIKON_BATTERY_PEER_OBSERVE,
    NIKON_BATTERY_PEER_FAILED,
} NikonBatteryPeerPhase;

typedef enum NikonBatteryPeerResult {
    NIKON_BATTERY_PEER_MORE,
    NIKON_BATTERY_PEER_REPLY,
    NIKON_BATTERY_PEER_ERROR,
} NikonBatteryPeerResult;

typedef struct NikonBatteryPeer {
    NikonBatteryPeerPhase phase;
    uint8_t challenge[17], auth_reply[29], profile_reply[16], ack_reply[5];
    uint8_t control_reply[4];
    uint8_t status_reply[14], metrics_reply[35], echo_reply[12];
    uint8_t request[21];
    size_t used, request_size;
    bool class1;
    bool status_echo_available;
    const char *error;
} NikonBatteryPeer;

/*
 * Exactly 52 hex digits; explicit class1/3 and bounded virtual status/metrics.
 * Invalid input leaves output unchanged, including partial hex failures.
 */
bool nikon_battery_parse_profile(const char *hex, NikonBatteryProfile *out);

/* Computes and verifies all arithmetic before a socket is opened. */
NikonBatterySearchStatus nikon_battery_peer_init(
    NikonBatteryPeer *peer, uint32_t seed, const NikonBatteryProfile *profile,
    NikonBatteryCancelled cancelled, void *opaque);

/*
 * One byte at a time accommodates any stream fragmentation without resync.
 * REPLY is emitted only for a complete, exact, in-order request with good XOR.
 * A failure is terminal. Reply points into peer and is valid until next feed;
 * other results return NULL/0, never a stale packet. These calls do no I/O.
 */
NikonBatteryPeerResult nikon_battery_peer_receive(
    NikonBatteryPeer *peer, uint8_t byte, const uint8_t **reply, size_t *size);
bool nikon_battery_peer_eof(NikonBatteryPeer *peer);
const char *nikon_battery_peer_phase(const NikonBatteryPeer *peer);

/*
 * Raw Unix stream; logs wire requests/replies and failures to stderr.
 * Returns 0 on clean EOF, 3 on failure, 130 on cancellation. Clean EOF makes
 * no assertion about guest acceptance, initialization or later transactions.
 */
int nikon_battery_peer_run(NikonBatteryPeer *peer, const char *socket_path,
                           NikonBatteryCancelled cancelled, void *opaque);

#endif
