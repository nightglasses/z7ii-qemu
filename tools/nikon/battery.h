/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef NIKON_BATTERY_TOOL_H
#define NIKON_BATTERY_TOOL_H

#include <stdbool.h>
#include <stdint.h>

#define NIKON_BATTERY_CHALLENGE_SIZE 17
#define NIKON_BATTERY_REPLY_SIZE 29
#define NIKON_BATTERY_SEED_LIMIT UINT64_C(0x80000000)

typedef bool (*NikonBatteryCancelled)(void *opaque);

typedef enum NikonBatterySearchStatus {
    NIKON_BATTERY_FOUND,
    NIKON_BATTERY_NOT_FOUND,
    NIKON_BATTERY_CANCELLED,
    NIKON_BATTERY_INVALID,
    NIKON_BATTERY_VERIFY_FAILED,
} NikonBatterySearchStatus;

typedef struct NikonBatterySearchResult {
    uint64_t tried;
    /* Valid only for NIKON_BATTERY_FOUND; not a unique 32-bit seed. */
    uint32_t representative;
} NikonBatterySearchResult;

/* Exactly 34 hex digits, in wire byte order; high five bits must be zero. */
bool nikon_battery_parse_challenge(
    const char *hex, uint8_t challenge[NIKON_BATTERY_CHALLENGE_SIZE]);
const char *nikon_battery_backend(void);

/* Diagnostic generation accepts all 32 seed bits; search covers their pairs. */
bool nikon_battery_challenge(uint32_t seed,
                            uint8_t challenge[NIKON_BATTERY_CHALLENGE_SIZE]);

/*
 * Nonempty [start, start + count) must fit in the 31-bit representative domain.
 * Cancellation is checked before each candidate and before publishing a hit.
 * The callback is invoked synchronously; a NULL callback never cancels.
 */
NikonBatterySearchStatus nikon_battery_search(
    const uint8_t challenge[NIKON_BATTERY_CHALLENGE_SIZE],
    uint64_t start, uint64_t count,
    NikonBatteryCancelled cancelled, void *opaque,
    NikonBatterySearchResult *result);

/*
 * Rebuild the full ring and independently recompute all 17 bytes using
 * bit-serial field multiplication before deriving H. Output is untouched
 * unless FOUND. This is only the source-backed first-auth, retained-tail=0
 * lifecycle; it supplies neither battery identity nor later command replies.
 */
NikonBatterySearchStatus nikon_battery_first_auth(
    const uint8_t challenge[NIKON_BATTERY_CHALLENGE_SIZE],
    uint32_t representative, NikonBatteryCancelled cancelled, void *opaque,
    uint8_t reply[NIKON_BATTERY_REPLY_SIZE]);

/* Eight predetermined vectors checked against the independent Python oracle. */
bool nikon_battery_selftest(void);

#endif
