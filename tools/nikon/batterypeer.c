/*
 * Finite, explicitly synthetic battery UART profile.
 * Protocol provenance: docs/runtime/batterypeer.md.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include <poll.h>
#include <sys/un.h>
#include "qapi/error.h"
#include "qemu/sockets.h"
#include "batterypeer.h"

static const uint8_t profile_request[] = { 2, 0x0c, 0, 0x0e };
static const uint8_t ack_request[] = { 2, 0x0e, 1, 1, 0x0c };

static uint8_t packet_xor(const uint8_t *bytes, size_t size)
{
    uint8_t result = 0;

    for (size_t i = 0; i < size; i++) {
        result ^= bytes[i];
    }
    return result;
}

static unsigned profile_class(const NikonBatteryProfile *profile)
{
    return profile->bytes[8] | (unsigned)profile->bytes[9] << 8;
}

static unsigned profile_u16(const NikonBatteryProfile *profile, unsigned offset)
{
    return profile->bytes[offset] | (unsigned)profile->bytes[offset + 1] << 8;
}

static bool valid_profile(const NikonBatteryProfile *profile)
{
    unsigned numerator = profile_u16(profile, 19);
    unsigned denominator = profile_u16(profile, 21);

    /* EX's signed correction/table calculation is not safe for all bytes. */
    return (profile_class(profile) == 1 || profile_class(profile) == 3) &&
           profile->bytes[12] <= 100 && profile->bytes[18] == 0 &&
           numerator > 0 && numerator <= denominator;
}

static void envelope(uint8_t *reply, size_t size)
{
    /* Header and unused payload bytes are declared synthetic framing. */
    reply[0] = 2;
    reply[1] = 6;
    reply[2] = size - 4;
    reply[size - 1] = packet_xor(reply, size - 1);
}

bool nikon_battery_parse_profile(const char *hex, NikonBatteryProfile *out)
{
    NikonBatteryProfile profile;

    if (!hex || !out || strlen(hex) != 2 * sizeof(profile.bytes)) {
        return false;
    }
    for (unsigned i = 0; i < sizeof(profile.bytes); i++) {
        int high = g_ascii_xdigit_value(hex[2 * i]);
        int low = g_ascii_xdigit_value(hex[2 * i + 1]);

        if (high < 0 || low < 0) {
            return false;
        }
        profile.bytes[i] = (high << 4) | low;
    }
    if (!valid_profile(&profile)) {
        return false;
    }
    *out = profile;
    return true;
}

NikonBatterySearchStatus nikon_battery_peer_init(
    NikonBatteryPeer *peer, uint32_t seed, const NikonBatteryProfile *profile,
    NikonBatteryCancelled cancelled, void *opaque)
{
    NikonBatteryPeer candidate = { .phase = NIKON_BATTERY_PEER_AUTH };
    NikonBatterySearchStatus status;

    if (!peer || !profile || !valid_profile(profile)) {
        return NIKON_BATTERY_INVALID;
    }
    if (cancelled && cancelled(opaque)) {
        return NIKON_BATTERY_CANCELLED;
    }
    if (!nikon_battery_challenge(seed, candidate.challenge)) {
        return NIKON_BATTERY_VERIFY_FAILED;
    }
    status = nikon_battery_first_auth(candidate.challenge,
                                     seed & UINT32_C(0x7fffffff),
                                     cancelled, opaque, candidate.auth_reply);
    if (status != NIKON_BATTERY_FOUND) {
        return status;
    }
    memcpy(candidate.profile_reply + 3, profile->bytes, 12);
    envelope(candidate.profile_reply, sizeof(candidate.profile_reply));
    envelope(candidate.ack_reply, sizeof(candidate.ack_reply));
    envelope(candidate.control_reply, sizeof(candidate.control_reply));
    candidate.status_reply[3] = profile->bytes[12];
    candidate.status_reply[5] = profile->bytes[13];
    memcpy(candidate.status_reply + 6, profile->bytes + 14, 2);
    memcpy(candidate.status_reply + 10, profile->bytes + 16, 2);
    candidate.status_reply[12] = profile->bytes[18];
    envelope(candidate.status_reply, sizeof(candidate.status_reply));
    memcpy(candidate.metrics_reply + 12, profile->bytes + 19, 2);
    memcpy(candidate.metrics_reply + 14, profile->bytes + 21, 2);
    memcpy(candidate.metrics_reply + 18, profile->bytes + 23, 2);
    candidate.metrics_reply[32] = profile->bytes[25];
    envelope(candidate.metrics_reply, sizeof(candidate.metrics_reply));
    envelope(candidate.echo_reply, sizeof(candidate.echo_reply));
    candidate.class1 = profile_class(profile) == 1;
    if (cancelled && cancelled(opaque)) {
        return NIKON_BATTERY_CANCELLED;
    }
    *peer = candidate;
    return NIKON_BATTERY_FOUND;
}

static NikonBatteryPeerResult fail(NikonBatteryPeer *peer, const char *error)
{
    peer->phase = NIKON_BATTERY_PEER_FAILED;
    peer->error = error;
    return NIKON_BATTERY_PEER_ERROR;
}

static size_t request_length(uint8_t command)
{
    switch (command) {
    case 1:
        return 21;
    case 2:
        return 6;
    case 6: /* EX echoes the complete preceding status response. */
        return 14;
    case 7:
    case 0x0a:
    case 0x0b:
    case 0x0c:
    case 0x25:
    case 0x26:
        return 4;
    case 0x0e:
        return 5;
    case 0x89:
        return 18;
    default:
        return 0;
    }
}

NikonBatteryPeerResult nikon_battery_peer_receive(
    NikonBatteryPeer *peer, uint8_t byte, const uint8_t **reply, size_t *size)
{
    if (reply) {
        *reply = NULL;
    }
    if (size) {
        *size = 0;
    }
    if (!peer || !reply || !size) {
        return NIKON_BATTERY_PEER_ERROR;
    }
    if (peer->phase == NIKON_BATTERY_PEER_FAILED) {
        return NIKON_BATTERY_PEER_ERROR;
    }
    if (peer->phase == NIKON_BATTERY_PEER_UNINITIALIZED) {
        return fail(peer, "peer was not initialized");
    }
    /* EX08024964 is a standalone TX1/RX0 operation, also used on re-entry. */
    if (!peer->used && !byte) {
        peer->status_echo_available = false;
        return NIKON_BATTERY_PEER_MORE;
    }
    if (peer->used >= sizeof(peer->request)) {
        return fail(peer, "request exceeds finite frame bound");
    }
    peer->request[peer->used++] = byte;
    if (peer->used == 1) {
        peer->request_size = 0;
        if (byte != 2) {
            return fail(peer, "unknown request header");
        }
    } else if (peer->used == 2) {
        peer->request_size = request_length(byte);
        if (!peer->request_size) {
            return fail(peer, "unsupported request command");
        }
        if (byte == 0x0c && peer->phase == NIKON_BATTERY_PEER_AUTH) {
            return fail(peer, "0c before authentication reply");
        }
        if (byte != 1 && byte != 0x0c &&
            peer->phase != NIKON_BATTERY_PEER_OBSERVE) {
            return fail(peer, "operation before identity profile reply");
        }
        if (byte == 0x0e && !peer->class1) {
            return fail(peer, "0e outside class1 profile");
        }
        if (byte == 6 && !peer->status_echo_available) {
            return fail(peer, "status echo without preceding status query");
        }
    } else if (peer->used == 3) {
        uint8_t expected = peer->request_size - 4;

        if (byte != expected) {
            return fail(peer, "invalid request length/header byte");
        }
    }
    if (!peer->request_size || peer->used < peer->request_size) {
        return NIKON_BATTERY_PEER_MORE;
    }
    if (packet_xor(peer->request, peer->request_size)) {
        return fail(peer, "request XOR mismatch");
    }
    switch (peer->request[1]) {
    case 2:
        if (peer->request[3] > 2) {
            return fail(peer, "02 control outside original caller bounds");
        }
        *reply = peer->control_reply;
        *size = sizeof(peer->control_reply);
        break;
    case 6:
        if (memcmp(peer->request, peer->status_reply,
                   sizeof(peer->status_reply))) {
            return fail(peer, "status echo differs from preceding reply");
        }
        *reply = peer->echo_reply;
        *size = sizeof(peer->echo_reply);
        break;
    case 7:
    case 0x25:
    case 0x26:
        *reply = peer->control_reply;
        *size = sizeof(peer->control_reply);
        break;
    case 0x0a:
        *reply = peer->metrics_reply;
        *size = sizeof(peer->metrics_reply);
        break;
    case 0x0b:
        *reply = peer->status_reply;
        *size = sizeof(peer->status_reply);
        break;
    case 0x89:
        if ((peer->request[3] | (unsigned)peer->request[4] << 8) > 999 ||
            (peer->request[5] | (unsigned)peer->request[6] << 8) > 999 ||
            peer->request[7] < 1 || peer->request[7] > 8 ||
            peer->request[16] > 6) {
            return fail(peer,
                        "89 configuration outside original builder bounds");
        }
        *reply = peer->control_reply;
        *size = sizeof(peer->control_reply);
        break;
    case 1:
        if (memcmp(peer->request + 3, peer->challenge,
                   sizeof(peer->challenge))) {
            return fail(peer, "wire challenge differs from configured seed");
        }
        *reply = peer->auth_reply;
        *size = sizeof(peer->auth_reply);
        peer->phase = NIKON_BATTERY_PEER_PROFILE;
        break;
    case 0x0c:
        if (memcmp(peer->request, profile_request, sizeof(profile_request))) {
            return fail(peer, "0c request differs from finite contract");
        }
        *reply = peer->profile_reply;
        *size = sizeof(peer->profile_reply);
        peer->phase = NIKON_BATTERY_PEER_OBSERVE;
        break;
    case 0x0e:
        if (memcmp(peer->request, ack_request, sizeof(ack_request))) {
            return fail(peer, "0e request differs from finite contract");
        }
        *reply = peer->ack_reply;
        *size = sizeof(peer->ack_reply);
        peer->phase = NIKON_BATTERY_PEER_OBSERVE;
        break;
    default:
        return fail(peer, "unsupported complete request");
    }
    peer->status_echo_available = peer->request[1] == 0x0b;
    peer->used = 0;
    return NIKON_BATTERY_PEER_REPLY;
}

bool nikon_battery_peer_eof(NikonBatteryPeer *peer)
{
    if (!peer || peer->phase == NIKON_BATTERY_PEER_UNINITIALIZED ||
        peer->phase == NIKON_BATTERY_PEER_FAILED) {
        return false;
    }
    if (peer->used) {
        fail(peer, "EOF in partial request");
        return false;
    }
    return true;
}

const char *nikon_battery_peer_phase(const NikonBatteryPeer *peer)
{
    switch (peer->phase) {
    case NIKON_BATTERY_PEER_AUTH:
        return "await-auth";
    case NIKON_BATTERY_PEER_PROFILE:
        return "await-profile";
    case NIKON_BATTERY_PEER_OBSERVE:
        return "observing";
    case NIKON_BATTERY_PEER_FAILED:
        return "failed";
    default:
        return "uninitialized";
    }
}

static void log_wire(const char *direction, const uint8_t *bytes, size_t size)
{
    fprintf(stderr, "peer %s ", direction);
    for (size_t i = 0; i < size; i++) {
        fprintf(stderr, "%02x", bytes[i]);
    }
    fputc('\n', stderr);
}

static bool was_cancelled(NikonBatteryCancelled cancelled, void *opaque)
{
    return cancelled && cancelled(opaque);
}

/*
 * Nonblocking I/O plus a bounded poll also handles a cancellation signal
 * arriving between the flag check and the next I/O syscall.
 */
static int wait_socket(int fd, short events,
                       NikonBatteryCancelled cancelled, void *opaque)
{
    struct pollfd pfd = { .fd = fd, .events = events };

    while (!was_cancelled(cancelled, opaque)) {
        int result = poll(&pfd, 1, 250);

        if (result < 0 && errno != EINTR) {
            return -1;
        }
        if (result > 0) {
            if (pfd.revents & POLLNVAL) {
                errno = EBADF;
                return -1;
            }
            if (pfd.revents & (events | POLLERR | POLLHUP)) {
                return 1;
            }
        }
    }
    return 0;
}

/*
 * QEMU's synchronous unix_connect retries EINTR internally. Create its socket
 * through qemuutil, but keep the connect nonblocking so cancellation can win
 * even when a Unix listener's accept queue remains full.
 * Returns fd, -1 with errp on failure, or -2 on cancellation.
 */
static int connect_socket(const char *path, NikonBatteryCancelled cancelled,
                          void *opaque, Error **errp)
{
    struct sockaddr_un address = { .sun_family = AF_UNIX };
    size_t length = strlen(path);
    int fd;

    if (!length || length >= sizeof(address.sun_path)) {
        error_setg(errp, "Unix socket path must contain 1..%zu bytes",
                   sizeof(address.sun_path) - 1);
        return -1;
    }
    memcpy(address.sun_path, path, length + 1);
    fd = qemu_socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        error_setg_errno(errp, errno, "cannot create Unix socket");
        return -1;
    }
    if (!qemu_set_blocking(fd, false, errp)) {
        close(fd);
        return -1;
    }
    for (;;) {
        if (was_cancelled(cancelled, opaque)) {
            goto cancelled;
        }
        if (!connect(fd, (struct sockaddr *)&address,
                     offsetof(struct sockaddr_un, sun_path) + length + 1)) {
            return fd;
        }
        if (errno == EISCONN) {
            return fd;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            /*
             * Linux Unix sockets report EAGAIN for a full accept queue.
             * No connect is pending: polling this fd can be writable and
             * SO_ERROR can be zero while it is still unconnected.
             */
            if (poll(NULL, 0, 250) < 0 && errno != EINTR) {
                goto failed;
            }
            continue;
        }
        if (errno != EINPROGRESS && errno != EALREADY) {
            goto failed;
        }
        for (;;) {
            int ready = wait_socket(fd, POLLOUT, cancelled, opaque);
            int socket_error;
            socklen_t error_size = sizeof(socket_error);

            if (!ready || was_cancelled(cancelled, opaque)) {
                goto cancelled;
            }
            if (ready < 0 || getsockopt(fd, SOL_SOCKET, SO_ERROR,
                                        &socket_error, &error_size)) {
                goto failed;
            }
            if (!socket_error) {
                return fd;
            }
            if (socket_error != EINPROGRESS && socket_error != EALREADY) {
                errno = socket_error;
                goto failed;
            }
        }
    }
failed:
    error_setg_errno(errp, errno, "cannot connect to Unix socket '%s'", path);
    close(fd);
    return -1;
cancelled:
    close(fd);
    return -2;
}

static int write_reply(int fd, const uint8_t *reply, size_t size,
                       NikonBatteryCancelled cancelled, void *opaque)
{
    /*
     * EX08023878..88 configures9600 baud,9-bit word/even parity/one stop:
     * eight data bits plus parity, start and stop take11 wire bits.
     * A raw socket must not deliver a whole reply before EX enables RX.
     */
    const int64_t frame_us = DIV_ROUND_UP(11 * G_USEC_PER_SEC, 9600);
    int64_t deadline = g_get_monotonic_time() + frame_us;
    size_t sent = 0;

    while (sent < size) {
        while (!was_cancelled(cancelled, opaque)) {
            int64_t remaining = deadline - g_get_monotonic_time();

            if (remaining <= 0) {
                break;
            }
            struct timespec delay = {
                .tv_sec = remaining / G_USEC_PER_SEC,
                .tv_nsec = (remaining % G_USEC_PER_SEC) * 1000,
            };

            if (nanosleep(&delay, NULL) < 0 && errno != EINTR) {
                return -1;
            }
        }
        if (was_cancelled(cancelled, opaque)) {
            return 0;
        }
        int ready = wait_socket(fd, POLLOUT, cancelled, opaque);

        if (ready <= 0) {
            return ready;
        }
        if (was_cancelled(cancelled, opaque)) {
            return 0;
        }
#ifdef MSG_NOSIGNAL
        ssize_t count = send(fd, reply + sent, 1, MSG_NOSIGNAL);
#else
        ssize_t count = send(fd, reply + sent, 1, 0);
#endif

        if (count < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            return -1;
        }
        if (!count) {
            errno = EPIPE;
            return -1;
        }
        sent += count;
        deadline = g_get_monotonic_time() + frame_us;
    }
    return 1;
}

int nikon_battery_peer_run(NikonBatteryPeer *peer, const char *socket_path,
                           NikonBatteryCancelled cancelled, void *opaque)
{
    Error *error = NULL;
    int result = 3;
    int fd;

    if (!peer || peer->phase != NIKON_BATTERY_PEER_AUTH ||
        !socket_path || !*socket_path) {
        fprintf(stderr, "peer requires initialized state and a socket path\n");
        return 3;
    }
    if (was_cancelled(cancelled, opaque)) {
        return 130;
    }
    fd = connect_socket(socket_path, cancelled, opaque, &error);
    if (fd == -2) {
        fprintf(stderr, "peer cancelled while connecting\n");
        return 130;
    }
    if (fd < 0) {
        fprintf(stderr, "peer connect failed: %s\n", error_get_pretty(error));
        error_free(error);
        return was_cancelled(cancelled, opaque) ? 130 : 3;
    }
#if !defined(MSG_NOSIGNAL) && defined(SO_NOSIGPIPE)
    int no_sigpipe = 1;

    if (setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE,
                   &no_sigpipe, sizeof(no_sigpipe))) {
        fprintf(stderr, "peer cannot suppress socket SIGPIPE: %s\n",
                strerror(errno));
        goto out;
    }
#elif !defined(MSG_NOSIGNAL)
    fprintf(stderr, "peer socket SIGPIPE suppression is unavailable\n");
    goto out;
#endif
    fprintf(stderr, "peer connected; raw UART, explicit synthetic profile\n");
    for (;;) {
        uint8_t bytes[64];
        int ready = wait_socket(fd, POLLIN, cancelled, opaque);

        if (!ready) {
            result = 130;
            break;
        }
        if (ready < 0) {
            fprintf(stderr, "peer receive poll failed: %s\n", strerror(errno));
            break;
        }
        ssize_t count = recv(fd, bytes, sizeof(bytes), 0);

        if (count < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            fprintf(stderr, "peer receive failed: %s\n", strerror(errno));
            break;
        }
        if (!count) {
            if (nikon_battery_peer_eof(peer)) {
                fprintf(stderr, "peer EOF at %s; no later-state claim\n",
                        nikon_battery_peer_phase(peer));
                result = 0;
            } else {
                log_wire("rx partial", peer->request, peer->used);
                fprintf(stderr, "peer protocol failure: %s\n", peer->error);
            }
            break;
        }
        log_wire("rx-chunk", bytes, count);
        for (ssize_t i = 0; i < count; i++) {
            const uint8_t *reply;
            size_t size;
            NikonBatteryPeerResult status;

            if (was_cancelled(cancelled, opaque)) {
                result = 130;
                goto out;
            }
            status = nikon_battery_peer_receive(peer, bytes[i], &reply, &size);
            if (status == NIKON_BATTERY_PEER_ERROR) {
                log_wire("rx rejected", peer->request, peer->used);
                fprintf(stderr, "peer protocol failure: %s\n", peer->error);
                goto out;
            }
            if (status == NIKON_BATTERY_PEER_REPLY) {
                log_wire("rx", peer->request, peer->request_size);
                int written = write_reply(fd, reply, size, cancelled, opaque);

                if (written <= 0) {
                    if (!written) {
                        result = 130;
                    } else {
                        fprintf(stderr, "peer reply write failed: %s\n",
                                strerror(errno));
                    }
                    goto out;
                }
                log_wire("tx", reply, size);
            }
        }
    }
out:
    close(fd);
    if (result == 130) {
        fprintf(stderr, "peer cancelled\n");
    }
    return result;
}
