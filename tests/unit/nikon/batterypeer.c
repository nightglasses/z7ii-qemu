/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include <poll.h>
#include "qemu/sockets.h"
#include "../../../tools/nikon/batterypeer.h"

static const uint8_t query[] = { 2, 0x0c, 0, 0x0e };
static const uint8_t ack[] = { 2, 0x0e, 1, 1, 0x0c };
static const char profile1[] = "000000000000000001003412"
                               "64a54523cdab00" "23015604ab89cd";
static const char profile3[] = "01020304050607080300ffff"
                               "0000ffff000000" "0100ffffffff01";
static const char *tool;

static uint8_t checksum(const uint8_t *bytes, size_t count)
{
    uint8_t value = 0;

    for (size_t i = 0; i < count; i++) {
        value ^= bytes[i];
    }
    return value;
}

static size_t make_request(uint8_t *bytes, uint8_t command,
                           const uint8_t *payload, size_t count)
{
    bytes[0] = 2;
    bytes[1] = command;
    bytes[2] = count;
    if (count) {
        memcpy(bytes + 3, payload, count);
    }
    bytes[count + 3] = checksum(bytes, count + 3);
    return count + 4;
}

static void profile_byte(char *hex, size_t offset, uint8_t value)
{
    static const char digits[] = "0123456789abcdef";

    hex[2 * offset] = digits[value >> 4];
    hex[2 * offset + 1] = digits[value & 15];
}

static NikonBatteryPeer initialized(const char *hex)
{
    NikonBatteryProfile profile;
    NikonBatteryPeer peer;

    g_assert_true(nikon_battery_parse_profile(hex, &profile));
    g_assert_cmpint(nikon_battery_peer_init(&peer, 0, &profile, NULL, NULL),
                    ==, NIKON_BATTERY_FOUND);
    return peer;
}

static void auth_request(uint8_t bytes[21])
{
    bytes[0] = 2;
    bytes[1] = 1;
    bytes[2] = 17;
    g_assert_true(nikon_battery_parse_challenge(
        "c1bac8b20c6ffe82fecd05132fbd7c1003", bytes + 3));
    bytes[20] = checksum(bytes, 20);
}

static const uint8_t *feed_packet(NikonBatteryPeer *peer, const uint8_t *bytes,
                                 size_t count, size_t expected_reply)
{
    const uint8_t *reply = NULL;
    size_t size = 0;

    for (size_t i = 0; i < count; i++) {
        NikonBatteryPeerResult result = nikon_battery_peer_receive(
            peer, bytes[i], &reply, &size);

        if (i + 1 < count) {
            g_assert_cmpint(result, ==, NIKON_BATTERY_PEER_MORE);
            g_assert_null(reply);
            g_assert_cmpuint(size, ==, 0);
        } else {
            g_assert_cmpint(result, ==, NIKON_BATTERY_PEER_REPLY);
            g_assert_nonnull(reply);
            g_assert_cmpuint(size, ==, expected_reply);
            g_assert_cmphex(checksum(reply, size), ==, 0);
        }
    }
    return reply;
}

static void reject_packet(NikonBatteryPeer *peer, const uint8_t *bytes,
                          size_t count)
{
    NikonBatteryPeerResult status = NIKON_BATTERY_PEER_MORE;
    const uint8_t *reply = (const uint8_t *)"stale";
    size_t size = 99;

    for (size_t i = 0; i < count && status != NIKON_BATTERY_PEER_ERROR; i++) {
        status = nikon_battery_peer_receive(peer, bytes[i], &reply, &size);
        g_assert_cmpint(status, !=, NIKON_BATTERY_PEER_REPLY);
        g_assert_null(reply);
        g_assert_cmpuint(size, ==, 0);
    }
    g_assert_cmpint(status, ==, NIKON_BATTERY_PEER_ERROR);
    g_assert_cmpint(peer->phase, ==, NIKON_BATTERY_PEER_FAILED);
    g_assert_nonnull(peer->error);
    /* A subsequent good header cannot resynchronize a failed session. */
    g_assert_cmpint(nikon_battery_peer_receive(peer, 2, &reply, &size),
                    ==, NIKON_BATTERY_PEER_ERROR);
    g_assert_null(reply);
    g_assert_cmpuint(size, ==, 0);
}

static void test_profile(void)
{
    NikonBatteryProfile profile, sentinel;
    char mutated[sizeof(profile1)];
    g_autofree char *uppercase = g_ascii_strup(profile3, -1);

    g_assert_true(nikon_battery_parse_profile(profile1, &profile));
    g_assert_cmphex(profile.bytes[8], ==, 1);
    g_assert_cmphex(profile.bytes[10], ==, 0x34);
    g_assert_cmphex(profile.bytes[11], ==, 0x12);
    static const uint8_t fields[] = {
        100, 0xa5, 0x45, 0x23, 0xcd, 0xab, 0,
        0x23, 1, 0x56, 4, 0xab, 0x89, 0xcd,
    };

    g_assert_cmpuint(sizeof(profile.bytes), ==, 26);
    g_assert_cmpmem(profile.bytes + 12, sizeof(fields), fields, sizeof(fields));
    g_assert_true(nikon_battery_parse_profile(profile3, &profile));
    g_assert_cmphex(profile.bytes[8], ==, 3);
    g_assert_true(nikon_battery_parse_profile(uppercase, &profile));
    memset(&profile, 0xa5, sizeof(profile));
    sentinel = profile;
    g_assert_false(nikon_battery_parse_profile(NULL, &profile));
    g_assert_false(nikon_battery_parse_profile(profile1, NULL));
    g_assert_false(nikon_battery_parse_profile("", &profile));
    g_assert_false(nikon_battery_parse_profile(profile1 + 1, &profile));
    g_assert_false(nikon_battery_parse_profile(
        "000000000000000001003412", &profile));
    g_assert_false(nikon_battery_parse_profile(
        "00000000000000000100341264a54523cdab0023015604ab89cd00", &profile));
    for (unsigned i = 0; i < 52; i++) {
        memcpy(mutated, profile1, sizeof(mutated));
        mutated[i] = 'x';
        g_assert_false(nikon_battery_parse_profile(mutated, &profile));
    }
    static const struct {
        unsigned offset;
        uint8_t value;
    } invalid[] = {
        { 8, 0 }, { 8, 2 }, { 9, 1 }, { 12, 101 },
        { 18, 1 }, { 18, 0xff },
    };

    for (unsigned i = 0; i < G_N_ELEMENTS(invalid); i++) {
        memcpy(mutated, profile1, sizeof(mutated));
        profile_byte(mutated, invalid[i].offset, invalid[i].value);
        g_assert_false(nikon_battery_parse_profile(mutated, &profile));
    }
    memcpy(mutated, profile1, sizeof(mutated));
    profile_byte(mutated, 19, 0);
    profile_byte(mutated, 20, 0);
    g_assert_false(nikon_battery_parse_profile(mutated, &profile));
    memcpy(mutated, profile1, sizeof(mutated));
    profile_byte(mutated, 21, 0);
    profile_byte(mutated, 22, 0);
    g_assert_false(nikon_battery_parse_profile(mutated, &profile));
    memcpy(mutated, profile1, sizeof(mutated));
    profile_byte(mutated, 19, 0xff);
    profile_byte(mutated, 20, 0xff);
    g_assert_false(nikon_battery_parse_profile(mutated, &profile));
    g_assert_cmpmem(&profile, sizeof(profile), &sentinel, sizeof(sentinel));

    /* All unconstrained bytes retain their full width. */
    memcpy(mutated, profile3, sizeof(mutated));
    profile_byte(mutated, 12, 100);
    profile_byte(mutated, 13, 0xff);
    for (unsigned i = 14; i < 18; i++) {
        profile_byte(mutated, i, 0xff);
    }
    for (unsigned i = 19; i < 26; i++) {
        profile_byte(mutated, i, 0xff);
    }
    g_assert_true(nikon_battery_parse_profile(mutated, &profile));
    profile_byte(mutated, 19, 1);
    profile_byte(mutated, 20, 0);
    profile_byte(mutated, 21, 1);
    profile_byte(mutated, 22, 0);
    g_assert_true(nikon_battery_parse_profile(mutated, &profile));
}

static void test_class1(void)
{
    NikonBatteryPeer peer = initialized(profile1);
    uint8_t auth[21];
    const uint8_t *reply;
    static const uint8_t expected_profile[] = {
        2, 6, 0x0c, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0x34, 0x12, 0x2f,
    };
    static const uint8_t expected_ack[] = { 2, 6, 1, 0, 5 };

    auth_request(auth);
    reply = feed_packet(&peer, auth, sizeof(auth), 29);
    g_assert_cmpmem(reply, 29, peer.auth_reply, 29);
    g_assert_cmpint(peer.phase, ==, NIKON_BATTERY_PEER_PROFILE);
    reply = feed_packet(&peer, query, sizeof(query), 16);
    g_assert_cmpmem(reply, 16, expected_profile, sizeof(expected_profile));
    g_assert_cmpint(peer.phase, ==, NIKON_BATTERY_PEER_OBSERVE);
    reply = feed_packet(&peer, ack, sizeof(ack), 5);
    g_assert_cmpmem(reply, 5, expected_ack, sizeof(expected_ack));
    g_assert_cmpint(peer.phase, ==, NIKON_BATTERY_PEER_OBSERVE);
    g_assert_true(nikon_battery_peer_eof(&peer));
    static const uint8_t later[] = { 2, 0x20 };

    reject_packet(&peer, later, sizeof(later));
}

static void test_fragmentation(void)
{
    NikonBatteryPeer initial = initialized(profile1);
    uint8_t auth[21];

    auth_request(auth);
    /*
     * Every possible split of the maximum-size frame leaves an incomplete
     * request without a reply until the remaining bytes arrive.
     */
    for (size_t split = 1; split < sizeof(auth); split++) {
        NikonBatteryPeer peer = initial;
        const uint8_t *reply;
        size_t size;

        for (size_t i = 0; i < split; i++) {
            g_assert_cmpint(nikon_battery_peer_receive(&peer, auth[i],
                                                      &reply, &size),
                            ==, NIKON_BATTERY_PEER_MORE);
            g_assert_null(reply);
            g_assert_cmpuint(size, ==, 0);
        }
        g_assert_cmpuint(peer.used, ==, split);
        feed_packet(&peer, auth + split, sizeof(auth) - split, 29);
        g_assert_cmpuint(peer.used, ==, 0);
    }
}

static void test_attention(void)
{
    NikonBatteryPeer peer = initialized(profile1);
    const uint8_t *reply;
    size_t size;
    uint8_t auth[21];

    auth_request(auth);
    g_assert_cmpint(nikon_battery_peer_receive(&peer, 0, &reply, &size),
                    ==, NIKON_BATTERY_PEER_MORE);
    g_assert_null(reply);
    g_assert_cmpuint(size, ==, 0);
    feed_packet(&peer, auth, sizeof(auth), 29);
    feed_packet(&peer, query, sizeof(query), 16);
    feed_packet(&peer, ack, sizeof(ack), 5);
    g_assert_cmpint(nikon_battery_peer_receive(&peer, 0, &reply, &size),
                    ==, NIKON_BATTERY_PEER_MORE);
    feed_packet(&peer, auth, sizeof(auth), 29);
    peer = initialized(profile1);
    for (unsigned i = 0; i < 2; i++) {
        g_assert_cmpint(nikon_battery_peer_receive(&peer, 0, &reply, &size),
                        ==, NIKON_BATTERY_PEER_MORE);
        g_assert_null(reply);
        g_assert_cmpuint(size, ==, 0);
        g_assert_cmpint(peer.phase, ==, NIKON_BATTERY_PEER_AUTH);
    }
    feed_packet(&peer, auth, sizeof(auth), 29);
    g_assert_cmpint(nikon_battery_peer_receive(&peer, 0, &reply, &size),
                    ==, NIKON_BATTERY_PEER_MORE);
    g_assert_null(reply);
    g_assert_cmpuint(size, ==, 0);
    g_assert_cmpint(peer.phase, ==, NIKON_BATTERY_PEER_PROFILE);
    feed_packet(&peer, query, sizeof(query), 16);
}

static void test_retry(void)
{
    NikonBatteryPeer peer = initialized(profile1);
    uint8_t auth[21];

    auth_request(auth);
    for (unsigned cycle = 0; cycle < 2; cycle++) {
        feed_packet(&peer, auth, sizeof(auth), 29);
        feed_packet(&peer, auth, sizeof(auth), 29);
        feed_packet(&peer, query, sizeof(query), 16);
        feed_packet(&peer, query, sizeof(query), 16);
        feed_packet(&peer, ack, sizeof(ack), 5);
        feed_packet(&peer, ack, sizeof(ack), 5);
    }
    g_assert_cmpint(peer.phase, ==, NIKON_BATTERY_PEER_OBSERVE);
}

static NikonBatteryPeer profile_ready(const char *hex)
{
    NikonBatteryPeer peer = initialized(hex);
    uint8_t auth[21];

    auth_request(auth);
    feed_packet(&peer, auth, sizeof(auth), 29);
    feed_packet(&peer, query, sizeof(query), 16);
    g_assert_cmpint(peer.phase, ==, NIKON_BATTERY_PEER_OBSERVE);
    return peer;
}

static NikonBatteryPeer control_ready(void)
{
    NikonBatteryPeer peer = profile_ready(profile1);

    feed_packet(&peer, ack, sizeof(ack), 5);
    return peer;
}

static void test_control(void)
{
    static const uint8_t request[] = { 2, 2, 2, 0, 0, 2 };
    static const uint8_t expected[] = { 2, 6, 0, 4 };
    NikonBatteryPeer peer = control_ready();

    for (unsigned attempt = 0; attempt < 3; attempt++) {
        const uint8_t *reply = feed_packet(&peer, request, sizeof(request), 4);

        g_assert_cmpmem(reply, 4, expected, sizeof(expected));
        g_assert_cmpint(peer.phase, ==, NIKON_BATTERY_PEER_OBSERVE);
    }
}

static void test_control_rejection(void)
{
    const uint8_t request[] = { 2, 2, 2, 0, 0, 2 };
    NikonBatteryPeer ready = control_ready(), peer = initialized(profile1);
    uint8_t auth[21], changed[sizeof(request)];

    reject_packet(&peer, request, sizeof(request));
    auth_request(auth);
    peer = initialized(profile1);
    feed_packet(&peer, auth, sizeof(auth), 29);
    reject_packet(&peer, request, sizeof(request));
    peer = ready;
    memcpy(changed, request, sizeof(request));
    changed[3] = 3;
    changed[5] = checksum(changed, 5);
    reject_packet(&peer, changed, sizeof(changed));
    peer = ready;
    memcpy(changed, request, sizeof(request));
    changed[2] = 3;
    changed[5] = checksum(changed, 5);
    reject_packet(&peer, changed, sizeof(changed));
    peer = ready;
    memcpy(changed, request, sizeof(request));
    changed[5] ^= 1;
    reject_packet(&peer, changed, sizeof(changed));
}

static const struct {
    uint8_t command, payload_size, payload[14];
} operations[] = {
    { 2, 2, { 0, 0 } },
    { 2, 2, { 1, 255 } },
    { 2, 2, { 2, 127 } },
    { 7, 0 }, { 0x25, 0 }, { 0x26, 0 },
    { 0x0b, 0 }, { 0x0a, 0 },
    { 0x89, 14, { 0, 0, 0, 0, 1 } },
    { 0x89, 14, { 0xe7, 3, 0xe7, 3, 8,
                   255, 255, 255, 255, 255, 255, 255, 255, 6 } },
};

static const uint8_t *operation_reply(uint8_t command, bool class3,
                                      size_t *size)
{
    static const uint8_t ack4[] = { 2, 6, 0, 4 };
    static const uint8_t status[2][14] = {
        { 2, 6, 10, 100, 0, 0xa5, 0x45, 0x23,
          0, 0, 0xcd, 0xab, 0, 0xcf },
        { 2, 6, 10, 0, 0, 0, 255, 255, 0, 0, 0, 0, 0, 0x0e },
    };
    static const uint8_t metrics[2][35] = {
        { [0] = 2, [1] = 6, [2] = 31,
          [12] = 0x23, [13] = 1, [14] = 0x56, [15] = 4,
          [18] = 0xab, [19] = 0x89, [32] = 0xcd, [34] = 0x84 },
        { [0] = 2, [1] = 6, [2] = 31,
          [12] = 1, [14] = 255, [15] = 255,
          [18] = 255, [19] = 255, [32] = 1, [34] = 0x1b },
    };

    if (command == 0x0b) {
        *size = sizeof(status[0]);
        return status[class3];
    }
    if (command == 0x0a) {
        *size = sizeof(metrics[0]);
        return metrics[class3];
    }
    *size = sizeof(ack4);
    return ack4;
}

static void test_operations(void)
{
    const char *profiles[] = { profile1, profile3 };

    for (unsigned class3 = 0; class3 < G_N_ELEMENTS(profiles); class3++) {
        /* Operational requests do not require optional class1 command0e. */
        NikonBatteryPeer peer = profile_ready(profiles[class3]);

        for (unsigned i = 0; i < G_N_ELEMENTS(operations); i++) {
            uint8_t request[18];
            size_t reply_size;
            const uint8_t *expected = operation_reply(operations[i].command,
                                                      class3, &reply_size);
            size_t count = make_request(request, operations[i].command,
                                        operations[i].payload,
                                        operations[i].payload_size);

            for (unsigned retry = 0; retry < 2; retry++) {
                const uint8_t *reply = feed_packet(&peer, request, count,
                                                  reply_size);

                g_assert_cmpmem(reply, reply_size, expected, reply_size);
                g_assert_cmpint(peer.phase, ==, NIKON_BATTERY_PEER_OBSERVE);
            }
        }
        g_assert_true(nikon_battery_peer_eof(&peer));
    }
}

static void check_splits(const NikonBatteryPeer *initial,
                         const uint8_t *request, size_t count,
                         size_t reply_size)
{
    for (size_t split = 1; split < count; split++) {
        NikonBatteryPeer peer = *initial;
        const uint8_t *reply;
        size_t size;

        for (size_t i = 0; i < split; i++) {
            g_assert_cmpint(nikon_battery_peer_receive(&peer, request[i],
                                                      &reply, &size),
                            ==, NIKON_BATTERY_PEER_MORE);
            g_assert_null(reply);
            g_assert_cmpuint(size, ==, 0);
        }
        g_assert_cmpuint(peer.used, ==, split);
        feed_packet(&peer, request + split, count - split, reply_size);
        g_assert_cmpuint(peer.used, ==, 0);
    }
}

static void test_operation_fragmentation(void)
{
    NikonBatteryPeer initial = profile_ready(profile1);

    for (unsigned i = 0; i < G_N_ELEMENTS(operations); i++) {
        uint8_t request[18];
        size_t reply_size;
        size_t count = make_request(request, operations[i].command,
                                    operations[i].payload,
                                    operations[i].payload_size);

        operation_reply(operations[i].command, false, &reply_size);
        check_splits(&initial, request, count, reply_size);
    }
}

static void test_operation_rejection(void)
{
    NikonBatteryPeer initial = initialized(profile1);
    NikonBatteryPeer ready = profile_ready(profile1);
    uint8_t auth[21];

    auth_request(auth);
    for (unsigned i = 0; i < G_N_ELEMENTS(operations); i++) {
        uint8_t request[18], changed[18];
        size_t count = make_request(request, operations[i].command,
                                    operations[i].payload,
                                    operations[i].payload_size);
        NikonBatteryPeer peer = initial;

        reject_packet(&peer, request, count);
        peer = initial;
        feed_packet(&peer, auth, sizeof(auth), 29);
        reject_packet(&peer, request, count);
        peer = ready;
        memcpy(changed, request, count);
        changed[count - 1] ^= 1;
        reject_packet(&peer, changed, count);
        peer = ready;
        memcpy(changed, request, count);
        changed[2]++;
        changed[count - 1] = checksum(changed, count - 1);
        reject_packet(&peer, changed, count);
    }

    static const struct {
        unsigned offset;
        uint8_t value;
    } invalid89[] = {
        { 3, 0xe8 }, { 5, 0xe8 }, { 7, 0 }, { 7, 9 }, { 16, 7 },
    };
    uint8_t request[18];

    for (unsigned i = 0; i < G_N_ELEMENTS(invalid89); i++) {
        NikonBatteryPeer peer = ready;

        make_request(request, 0x89, operations[9].payload, 14);
        request[invalid89[i].offset] = invalid89[i].value;
        request[17] = checksum(request, 17);
        reject_packet(&peer, request, sizeof(request));
    }
}

static void test_status_echo(void)
{
    static const uint8_t expected[] = { 2, 6, 8, 0, 0, 0, 0, 0, 0, 0, 0, 12 };
    uint8_t status_query[4], echo[14];
    size_t count = make_request(status_query, 0x0b, NULL, 0);
    const char *profiles[] = { profile1, profile3 };

    for (unsigned i = 0; i < G_N_ELEMENTS(profiles); i++) {
        NikonBatteryPeer peer = profile_ready(profiles[i]);
        const uint8_t *reply = feed_packet(&peer, status_query, count, 14);

        memcpy(echo, reply, sizeof(echo));
        check_splits(&peer, echo, sizeof(echo), sizeof(expected));
        reply = feed_packet(&peer, echo, sizeof(echo), sizeof(expected));
        g_assert_cmpmem(reply, sizeof(expected), expected, sizeof(expected));
        g_assert_cmpint(peer.phase, ==, NIKON_BATTERY_PEER_OBSERVE);
        reject_packet(&peer, echo, sizeof(echo));
    }
}

static void test_echo_rejection(void)
{
    NikonBatteryPeer ready = profile_ready(profile1), pending = ready, peer;
    uint8_t status_query[4], echo[14], changed[14], auth[21], other[4];
    size_t count = make_request(status_query, 0x0b, NULL, 0);
    const uint8_t *reply = feed_packet(&pending, status_query, count, 14);

    memcpy(echo, reply, sizeof(echo));
    peer = ready;
    reject_packet(&peer, echo, sizeof(echo));
    for (unsigned i = 0; i < sizeof(echo); i++) {
        peer = pending;
        memcpy(changed, echo, sizeof(changed));
        changed[i] ^= 1;
        if (i + 1 < sizeof(changed)) {
            changed[13] = checksum(changed, 13);
        }
        reject_packet(&peer, changed, sizeof(changed));
    }
    peer = pending;
    count = make_request(other, 7, NULL, 0);
    feed_packet(&peer, other, count, 4);
    reject_packet(&peer, echo, sizeof(echo));
    peer = pending;
    feed_packet(&peer, query, sizeof(query), 16);
    reject_packet(&peer, echo, sizeof(echo));
    peer = pending;
    const uint8_t *unused;
    size_t size;

    g_assert_cmpint(nikon_battery_peer_receive(&peer, 0, &unused, &size),
                    ==, NIKON_BATTERY_PEER_MORE);
    g_assert_null(unused);
    g_assert_cmpuint(size, ==, 0);
    reject_packet(&peer, echo, sizeof(echo));
    peer = pending;
    auth_request(auth);
    feed_packet(&peer, auth, sizeof(auth), 29);
    feed_packet(&peer, query, sizeof(query), 16);
    reject_packet(&peer, echo, sizeof(echo));
}

static void test_rejection(void)
{
    NikonBatteryPeer initial = initialized(profile1), peer;
    uint8_t auth[21], mutated[21];
    static const uint8_t bad_header[] = { 3 };
    static const uint8_t unknown[] = { 2, 0xff };

    auth_request(auth);
    peer = initial;
    reject_packet(&peer, bad_header, sizeof(bad_header));
    peer = initial;
    reject_packet(&peer, unknown, sizeof(unknown));
    peer = initial;
    reject_packet(&peer, query, sizeof(query));
    peer = initial;
    feed_packet(&peer, auth, sizeof(auth), 29);
    reject_packet(&peer, ack, sizeof(ack));
    peer = initial;
    memcpy(mutated, auth, sizeof(mutated));
    mutated[2] = 16;
    reject_packet(&peer, mutated, sizeof(mutated));
    peer = initial;
    memcpy(mutated, auth, sizeof(mutated));
    mutated[20] ^= 1;
    reject_packet(&peer, mutated, sizeof(mutated));
    /* A valid packet XOR cannot hide a mismatch in any challenge byte. */
    for (unsigned byte = 3; byte < 20; byte++) {
        peer = initial;
        memcpy(mutated, auth, sizeof(mutated));
        mutated[byte] ^= 1;
        mutated[20] = checksum(mutated, 20);
        reject_packet(&peer, mutated, sizeof(mutated));
    }
    peer = initial;
    feed_packet(&peer, auth, sizeof(auth), 29);
    feed_packet(&peer, query, sizeof(query), 16);
    uint8_t bad_ack[] = { 2, 0x0e, 1, 2, 0x0f };

    reject_packet(&peer, bad_ack, sizeof(bad_ack));
    peer = initial;
    feed_packet(&peer, auth, sizeof(auth), 29);
    uint8_t bad_query[] = { 2, 0x0c, 0, 0x0f };

    reject_packet(&peer, bad_query, sizeof(bad_query));
    peer = initial;
    feed_packet(&peer, auth, sizeof(auth), 29);
    feed_packet(&peer, query, sizeof(query), 16);
    memcpy(bad_ack, ack, sizeof(bad_ack));
    bad_ack[4] ^= 1;
    reject_packet(&peer, bad_ack, sizeof(bad_ack));
}

static void test_class3(void)
{
    NikonBatteryPeer peer = initialized(profile3);
    uint8_t auth[21];

    auth_request(auth);
    feed_packet(&peer, auth, sizeof(auth), 29);
    const uint8_t *reply = feed_packet(&peer, query, sizeof(query), 16);

    g_assert_cmphex(reply[11], ==, 3);
    g_assert_cmpint(peer.phase, ==, NIKON_BATTERY_PEER_OBSERVE);
    g_assert_true(nikon_battery_peer_eof(&peer));
    reject_packet(&peer, ack, sizeof(ack));
}

static bool cancel_now(void *opaque)
{
    (void)opaque;
    return true;
}

static void check_partial_eof(const NikonBatteryPeer *initial,
                              const uint8_t *request, size_t count)
{
    for (size_t length = 1; length < count; length++) {
        NikonBatteryPeer peer = *initial;
        const uint8_t *reply;
        size_t size;

        for (size_t i = 0; i < length; i++) {
            g_assert_cmpint(nikon_battery_peer_receive(&peer, request[i],
                                                      &reply, &size),
                            ==, NIKON_BATTERY_PEER_MORE);
            g_assert_null(reply);
            g_assert_cmpuint(size, ==, 0);
        }
        g_assert_false(nikon_battery_peer_eof(&peer));
        g_assert_cmpstr(peer.error, ==, "EOF in partial request");
    }
}

static void test_operation_eof_reset_cancel(void)
{
    NikonBatteryPeer ready = profile_ready(profile1), pending = ready;
    NikonBatteryProfile profile;
    uint8_t request[18], echo[14], auth[21];
    const uint8_t *reply;
    size_t size;

    for (unsigned i = 0; i < G_N_ELEMENTS(operations); i++) {
        size_t count = make_request(request, operations[i].command,
                                    operations[i].payload,
                                    operations[i].payload_size);

        check_partial_eof(&ready, request, count);
    }
    make_request(request, 0x0b, NULL, 0);
    memcpy(echo, feed_packet(&pending, request, 4, 14), sizeof(echo));
    check_partial_eof(&pending, echo, sizeof(echo));
    g_assert_cmpint(nikon_battery_peer_receive(&pending, echo[0],
                                              &reply, &size),
                    ==, NIKON_BATTERY_PEER_MORE);
    NikonBatteryPeer partial = pending;

    g_assert_true(nikon_battery_parse_profile(profile1, &profile));
    g_assert_cmpint(nikon_battery_peer_init(&pending, 0, &profile,
                                          cancel_now, NULL),
                    ==, NIKON_BATTERY_CANCELLED);
    g_assert_cmpmem(&pending, sizeof(pending), &partial, sizeof(partial));
    g_assert_cmpint(nikon_battery_peer_init(&pending, 0, &profile, NULL, NULL),
                    ==, NIKON_BATTERY_FOUND);
    g_assert_cmpint(pending.phase, ==, NIKON_BATTERY_PEER_AUTH);
    g_assert_cmpuint(pending.used, ==, 0);
    auth_request(auth);
    feed_packet(&pending, auth, sizeof(auth), 29);
    feed_packet(&pending, query, sizeof(query), 16);
    reject_packet(&pending, echo, sizeof(echo));
}

static void test_eof_reset_cancel(void)
{
    NikonBatteryPeer initial = initialized(profile1);
    uint8_t auth[21];

    auth_request(auth);
    g_assert_true(nikon_battery_peer_eof(&initial));
    for (size_t length = 1; length < sizeof(auth); length++) {
        NikonBatteryPeer peer = initial;
        const uint8_t *reply;
        size_t size;

        for (size_t i = 0; i < length; i++) {
            g_assert_cmpint(nikon_battery_peer_receive(&peer, auth[i],
                                                      &reply, &size),
                            ==, NIKON_BATTERY_PEER_MORE);
        }
        g_assert_false(nikon_battery_peer_eof(&peer));
        g_assert_cmpstr(peer.error, ==, "EOF in partial request");
    }
    NikonBatteryProfile profile;
    NikonBatteryPeer peer = initial;

    g_assert_true(nikon_battery_parse_profile(profile1, &profile));
    g_assert_cmpint(nikon_battery_peer_init(&peer, 0, &profile,
                                          cancel_now, NULL),
                    ==, NIKON_BATTERY_CANCELLED);
    g_assert_cmpmem(&peer, sizeof(peer), &initial, sizeof(initial));
    g_assert_cmpint(nikon_battery_peer_run(&peer, "/unused", cancel_now, NULL),
                    ==, 130);
    /* Explicit reinitialization clears all partial/failed state. */
    reject_packet(&peer, query, sizeof(query));
    g_assert_cmpint(nikon_battery_peer_init(&peer, UINT32_C(0x80000000),
                                          &profile, NULL, NULL),
                    ==, NIKON_BATTERY_FOUND);
    feed_packet(&peer, auth, sizeof(auth), 29);
}

static void test_cli(void)
{
    char bad_class[sizeof(profile1)], bad_hex[sizeof(profile1)];
    char bad_charge[sizeof(profile1)], bad_correction[sizeof(profile1)];
    char bad_denominator[sizeof(profile1)];

    memcpy(bad_class, profile1, sizeof(bad_class));
    profile_byte(bad_class, 8, 2);
    memcpy(bad_hex, profile1, sizeof(bad_hex));
    bad_hex[51] = 'z';
    memcpy(bad_charge, profile1, sizeof(bad_charge));
    profile_byte(bad_charge, 12, 101);
    memcpy(bad_correction, profile1, sizeof(bad_correction));
    profile_byte(bad_correction, 18, 0xff);
    memcpy(bad_denominator, profile1, sizeof(bad_denominator));
    profile_byte(bad_denominator, 21, 0);
    profile_byte(bad_denominator, 22, 0);
    const struct {
        const char *seed, *profile;
    } invalid[] = {
        { "-1", profile1 }, { "+1", profile1 }, { "0x10", profile1 },
        { " 0", profile1 }, { "4294967296", profile1 },
        { "18446744073709551616", profile1 }, { "0", "" },
        { "0", "000000000000000001003412" },
        { "0", bad_class }, { "0", bad_hex }, { "0", bad_charge },
        { "0", bad_correction }, { "0", bad_denominator },
    };

    for (unsigned i = 0; i < G_N_ELEMENTS(invalid); i++) {
        const char *args[] = { tool, "peer", "/must-not-connect",
                              invalid[i].seed, invalid[i].profile, NULL };
        g_autofree char *output = NULL;
        g_autofree char *errors = NULL;
        g_autoptr(GError) error = NULL;
        int status;

        g_assert_true(g_spawn_sync(NULL, (char **)args, NULL, 0, NULL, NULL,
                                   &output, &errors, &status, &error));
        g_assert_no_error(error);
        g_assert_false(g_spawn_check_exit_status(status, &error));
        g_assert_error(error, G_SPAWN_EXIT_ERROR, 2);
        g_assert_cmpstr(output, ==, "");
        g_assert_nonnull(strstr(errors, "usage:"));
        g_assert_null(strstr(errors, "connect failed"));
    }
}

static bool socket_ready(int fd, short events, int64_t deadline)
{
    struct pollfd pfd = { .fd = fd, .events = events };

    for (;;) {
        int64_t remaining = deadline - g_get_monotonic_time();

        if (remaining <= 0) {
            return false;
        }
        int result = poll(&pfd, 1, (remaining + 999) / 1000);

        if (result < 0 && errno == EINTR) {
            continue;
        }
        return result > 0 && (pfd.revents & events);
    }
}

static bool socket_io(int fd, const uint8_t *tx, uint8_t *rx, size_t size)
{
    int64_t deadline = g_get_monotonic_time() + 10 * G_USEC_PER_SEC;
    size_t offset = 0;
    int flags = 0;

#ifdef MSG_NOSIGNAL
    flags = MSG_NOSIGNAL;
#endif
    while (offset < size) {
        if (!socket_ready(fd, tx ? POLLOUT : POLLIN, deadline)) {
            return false;
        }
        ssize_t count = tx ? send(fd, tx + offset, size - offset, flags) :
                             recv(fd, rx + offset, size - offset, 0);

        if (count < 0 && (errno == EINTR || errno == EAGAIN ||
                         errno == EWOULDBLOCK)) {
            continue;
        }
        if (count <= 0) {
            return false;
        }
        offset += count;
    }
    return true;
}

static bool socket_exchange(int fd, const uint8_t *request, size_t count,
                            const uint8_t *expected, size_t reply_size)
{
    uint8_t reply[35];

    return socket_io(fd, request, NULL, count) &&
           socket_io(fd, NULL, reply, reply_size) &&
           !memcmp(reply, expected, reply_size);
}

static bool wait_peer(GPid child, int *status)
{
    int64_t deadline = g_get_monotonic_time() + 10 * G_USEC_PER_SEC;

    while (g_get_monotonic_time() < deadline) {
        pid_t result = waitpid(child, status, WNOHANG);

        if (result == child) {
            return true;
        }
        if (result < 0 && errno != EINTR) {
            return false;
        }
        g_usleep(1000);
    }
    kill(child, SIGKILL);
    while (waitpid(child, status, 0) < 0 && errno == EINTR) {
    }
    return false;
}

static char *pipe_text(int fd)
{
    GString *text = g_string_new(NULL);
    char buffer[1024];
    ssize_t size;

    while ((size = read(fd, buffer, sizeof(buffer))) != 0) {
        if (size < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        g_string_append_len(text, buffer, size);
    }
    return g_string_free(text, false);
}

static void test_socket(void)
{
    g_autofree char *directory = g_strdup("/tmp/nikon-bp-XXXXXX");
    g_autofree char *path = NULL;
    g_autofree char *output = NULL;
    g_autofree char *errors = NULL;
    g_autoptr(GError) error = NULL;
    struct sockaddr_un address = { .sun_family = AF_UNIX };
    NikonBatteryPeer expected = initialized(profile1);
    GPid child = 0;
    int listener = -1, fd = -1, out_fd = -1, err_fd = -1, status = 0;
    uint8_t auth[21], reply[29], request[18], echo[14];
    bool ok = false, reaped = false;

    g_assert_nonnull(g_mkdtemp(directory));
    path = g_build_filename(directory, "uart", NULL);
    g_assert_cmpuint(strlen(path), <, sizeof(address.sun_path));
    memcpy(address.sun_path, path, strlen(path) + 1);
    listener = qemu_socket(AF_UNIX, SOCK_STREAM, 0);
    if (listener < 0 ||
        bind(listener, (struct sockaddr *)&address,
             offsetof(struct sockaddr_un, sun_path) + strlen(path) + 1) ||
        listen(listener, 1) || !qemu_set_blocking(listener, false, NULL)) {
        goto out;
    }
    const char *args[] = { tool, "peer", path, "0", profile1, NULL };

    if (!g_spawn_async_with_pipes(NULL, (char **)args, NULL,
                                  G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL,
                                  &child, NULL, &out_fd, &err_fd, &error) ||
        !socket_ready(listener, POLLIN,
                      g_get_monotonic_time() + 10 * G_USEC_PER_SEC)) {
        goto out;
    }
    fd = accept(listener, NULL, NULL);
    if (fd < 0 || !qemu_set_blocking(fd, false, NULL)) {
        goto out;
    }
#ifdef SO_NOSIGPIPE
    int no_sigpipe = 1;

    if (setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE,
                   &no_sigpipe, sizeof(no_sigpipe))) {
        goto out;
    }
#endif
    auth_request(auth);
    int64_t started = g_get_monotonic_time();

    if (!socket_io(fd, auth, NULL, sizeof(auth)) ||
        !socket_io(fd, NULL, reply, sizeof(reply))) {
        goto out;
    }
    int64_t elapsed = g_get_monotonic_time() - started;

    g_test_message("auth reply duration: %" G_GINT64_FORMAT " us", elapsed);
    if (memcmp(reply, expected.auth_reply, sizeof(reply)) || elapsed < 30000) {
        goto out;
    }
    if (!socket_exchange(fd, query, sizeof(query),
                         expected.profile_reply, 16) ||
        !socket_exchange(fd, ack, sizeof(ack), expected.ack_reply, 5)) {
        goto out;
    }
    make_request(request, 0x0b, NULL, 0);
    size_t size;
    const uint8_t *status_reply = operation_reply(0x0b, false, &size);

    if (!socket_exchange(fd, request, 4, status_reply, size)) {
        goto out;
    }
    memcpy(echo, status_reply, sizeof(echo));
    static const uint8_t echo_ack[] = {
        2, 6, 8, 0, 0, 0, 0, 0, 0, 0, 0, 12,
    };

    if (!socket_exchange(fd, echo, sizeof(echo), echo_ack, sizeof(echo_ack))) {
        goto out;
    }
    static const unsigned remaining[] = { 7, 0, 3, 4, 5, 9 };

    for (unsigned i = 0; i < G_N_ELEMENTS(remaining); i++) {
        unsigned index = remaining[i];
        size_t count = make_request(request, operations[index].command,
                                    operations[index].payload,
                                    operations[index].payload_size);
        const uint8_t *bytes = operation_reply(operations[index].command,
                                               false, &size);

        if (!socket_exchange(fd, request, count, bytes, size)) {
            goto out;
        }
    }
    ok = shutdown(fd, SHUT_WR) == 0;
out:
    if (fd >= 0) {
        close(fd);
    }
    if (listener >= 0) {
        close(listener);
    }
    if (child) {
        if (!ok) {
            kill(child, SIGTERM);
        }
        reaped = wait_peer(child, &status);
        g_spawn_close_pid(child);
    }
    if (out_fd >= 0) {
        output = pipe_text(out_fd);
        close(out_fd);
    }
    if (err_fd >= 0) {
        errors = pipe_text(err_fd);
        close(err_fd);
    }
    unlink(path);
    rmdir(directory);
    if (!ok || !reaped || !WIFEXITED(status) || WEXITSTATUS(status)) {
        g_test_message("socket peer failure: %s%s", error ? error->message : "",
                       errors ? errors : "");
    }
    g_assert_true(ok);
    g_assert_true(reaped);
    g_assert_true(WIFEXITED(status));
    g_assert_cmpint(WEXITSTATUS(status), ==, 0);
    g_assert_nonnull(strstr(output, "\"status\":\"eof\""));
    g_assert_nonnull(strstr(output, "\"phase\":\"observing\""));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_assert_cmpint(argc, ==, 2);
    tool = argv[1];
    g_test_add_func("/nikon/batterypeer/profile", test_profile);
    g_test_add_func("/nikon/batterypeer/class1", test_class1);
    g_test_add_func("/nikon/batterypeer/fragmentation", test_fragmentation);
    g_test_add_func("/nikon/batterypeer/attention", test_attention);
    g_test_add_func("/nikon/batterypeer/retry", test_retry);
    g_test_add_func("/nikon/batterypeer/control", test_control);
    g_test_add_func("/nikon/batterypeer/control-rejection",
                    test_control_rejection);
    g_test_add_func("/nikon/batterypeer/operations", test_operations);
    g_test_add_func("/nikon/batterypeer/operation-fragmentation",
                    test_operation_fragmentation);
    g_test_add_func("/nikon/batterypeer/operation-rejection",
                    test_operation_rejection);
    g_test_add_func("/nikon/batterypeer/status-echo", test_status_echo);
    g_test_add_func("/nikon/batterypeer/echo-rejection", test_echo_rejection);
    g_test_add_func("/nikon/batterypeer/rejection", test_rejection);
    g_test_add_func("/nikon/batterypeer/class3", test_class3);
    g_test_add_func("/nikon/batterypeer/eof-reset-cancel",
                    test_eof_reset_cancel);
    g_test_add_func("/nikon/batterypeer/operation-eof-reset-cancel",
                    test_operation_eof_reset_cancel);
    g_test_add_func("/nikon/batterypeer/cli", test_cli);
    g_test_add_func("/nikon/batterypeer/socket", test_socket);
    return g_test_run();
}
