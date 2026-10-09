/* Native asynchronous peer wires. No guest packet, ACK or time is invented.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "qapi/error.h"
#include "chardev/char-fe.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "hw/nikon/peer.h"
#include "hw/nikon/wire.h"

#define PEER_QUEUE_LIMIT (64 * NIKON_WIRE_SIZE)

struct NikonPeer {
    DeviceState parent_obj;
    CharFrontend chr;
    GByteArray *output;
    unsigned output_offset;
    guint watch;
    uint8_t input[NIKON_WIRE_SIZE];
    unsigned input_length;
    bool connected;
    bool failed;
    NikonPeerEndpointOps endpoint;
    void *endpoint_opaque;
    NikonPeerState state;
    void *state_opaque;
    bool pending;
    uint64_t token;
    unsigned bits;
    NikonPeerKind kind;
    uint32_t line;
    NikonPeerComplete complete;
    void *complete_opaque;
    bool incoming;
    uint64_t incoming_token;
    unsigned incoming_bits;
    NikonPeerKind incoming_kind;
    uint32_t incoming_line;
};

static void peer_flush(NikonPeer *s);

static void peer_discard(NikonPeer *s)
{
    uint64_t token = s->incoming_token;
    bool incoming = s->incoming;

    if (s->watch) {
        g_source_remove(s->watch);
        s->watch = 0;
    }
    s->pending = false;
    s->incoming = false;
    s->complete = NULL;
    s->input_length = 0;
    s->output_offset = 0;
    g_byte_array_set_size(s->output, 0);
    if (incoming && s->endpoint.cancel) {
        s->endpoint.cancel(s->endpoint_opaque, token);
    }
}

static void peer_failure(NikonPeer *s, const char *reason)
{
    error_report("nikon-peer: %s; no guest completion", reason);
    s->failed = true;
    s->connected = false;
    peer_discard(s);
    if (s->state) {
        s->state(s->state_opaque, false);
    }
    qemu_chr_fe_disconnect(&s->chr);
}

static void peer_invalid(NikonPeer *s)
{
    peer_failure(s, "invalid transport frame");
}

static gboolean peer_writable(void *channel, GIOCondition condition, void *opaque)
{
    NikonPeer *s = opaque;

    s->watch = 0;
    if (condition & G_IO_HUP) {
        return G_SOURCE_REMOVE;
    }
    peer_flush(s);
    /* A rejected submission may retry after native output backpressure clears.
     * Accepted clocks remain pending and are never transmitted a second time.
     */
    if (s->connected && !s->failed && s->state &&
        s->output->len + NIKON_WIRE_SIZE <= PEER_QUEUE_LIMIT) {
        s->state(s->state_opaque, true);
    }
    return G_SOURCE_REMOVE;
}

static void peer_flush(NikonPeer *s)
{
    while (s->connected && s->output_offset < s->output->len) {
        int count = qemu_chr_fe_write(&s->chr,
                       s->output->data + s->output_offset,
                       s->output->len - s->output_offset);

        if (count <= 0) {
            if (!s->watch) {
                s->watch = qemu_chr_fe_add_watch(&s->chr, G_IO_OUT | G_IO_HUP,
                                                 peer_writable, s);
            }
            return;
        }
        s->output_offset += count;
    }
    if (s->output_offset == s->output->len) {
        s->output_offset = 0;
        g_byte_array_set_size(s->output, 0);
    }
}

static bool peer_send(NikonPeer *s, unsigned type, NikonPeerKind kind,
                      uint64_t token, unsigned bits, uint32_t line,
                      uint32_t word)
{
    uint8_t frame[NIKON_WIRE_SIZE] = { 0 };

    /* Account only outstanding bytes when the backend accepted a prefix. */
    if (s->output_offset && s->output->len + sizeof(frame) > PEER_QUEUE_LIMIT) {
        g_byte_array_remove_range(s->output, 0, s->output_offset);
        s->output_offset = 0;
    }
    if (!s->connected || s->failed ||
        s->output->len + sizeof(frame) > PEER_QUEUE_LIMIT) {
        return false;
    }
    stl_le_p(frame, NIKON_WIRE_MAGIC);
    frame[NIKON_WIRE_TYPE] = type;
    frame[NIKON_WIRE_BITS] = bits;
    frame[NIKON_WIRE_KIND] = kind;
    stq_le_p(frame + NIKON_WIRE_TOKEN, token);
    stq_le_p(frame + NIKON_WIRE_TIME, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
    stl_le_p(frame + NIKON_WIRE_LINE, line);
    stl_le_p(frame + NIKON_WIRE_WORD, word);
    g_byte_array_append(s->output, frame, sizeof(frame));
    peer_flush(s);
    return true;
}

static bool peer_operation(NikonPeerKind kind, unsigned bits, uint32_t line)
{
    if (kind == NIKON_PEER_CLOCK) {
        return bits >= 1 && bits <= 32 && !line;
    }
    return kind == NIKON_PEER_I2C && bits == 8 &&
           line >= NIKON_I2C_START && line <= NIKON_I2C_NACK;
}

static bool peer_word(unsigned type, NikonPeerKind kind, uint32_t phase,
                      uint32_t word)
{
    if (type == NIKON_WIRE_CANCEL) {
        return !word;
    }
    if (kind == NIKON_PEER_CLOCK) {
        return true;
    }
    if (type == NIKON_WIRE_REPLY) {
        return word == NIKON_I2C_NAK ||
               (phase == NIKON_I2C_READ ? word <= 0xff : !word);
    }
    return type == NIKON_WIRE_REQUEST &&
           (phase <= NIKON_I2C_WRITE ? word <= 0xff : !word);
}

static void peer_frame(NikonPeer *s)
{
    const uint8_t *frame = s->input;
    unsigned type = frame[NIKON_WIRE_TYPE], bits = frame[NIKON_WIRE_BITS];
    NikonPeerKind kind = frame[NIKON_WIRE_KIND];
    uint64_t token = ldq_le_p(frame + NIKON_WIRE_TOKEN);
    uint32_t line = ldl_le_p(frame + NIKON_WIRE_LINE);
    uint32_t word = ldl_le_p(frame + NIKON_WIRE_WORD);
    uint64_t time = ldq_le_p(frame + NIKON_WIRE_TIME);

    if ((uint32_t)ldl_le_p(frame) != NIKON_WIRE_MAGIC ||
        frame[NIKON_WIRE_RESERVED] || ldq_le_p(frame + NIKON_WIRE_PADDING)) {
        peer_invalid(s);
        return;
    }
    if (type == NIKON_WIRE_SIGNAL) {
        if (token || bits || word > 1 ||
            (kind != NIKON_PEER_SELECT && kind != NIKON_PEER_GPIO)) {
            peer_invalid(s);
            return;
        }
        if (s->endpoint.wire) {
            s->endpoint.wire(s->endpoint_opaque, kind, line, word, time);
        }
        return;
    }
    if (!token || !peer_operation(kind, bits, line) ||
        !peer_word(type, kind, line, word)) {
        peer_invalid(s);
        return;
    }
    if (type == NIKON_WIRE_REPLY) {
        NikonPeerComplete complete;
        void *opaque;

        /* Cancelled and stale replies cannot complete a new transaction. */
        if (!s->pending || s->token != token || s->bits != bits ||
            s->kind != kind || s->line != line) {
            return;
        }
        complete = s->complete;
        opaque = s->complete_opaque;
        s->pending = false;
        s->complete = NULL;
        complete(opaque, token, word);
    } else if (type == NIKON_WIRE_REQUEST) {
        NikonPeerRequest request = {
            .token = token, .time_ns = time, .kind = kind,
            .word = word, .line = line, .bits = bits,
        };

        if (s->incoming) {
            peer_invalid(s);
            return;
        }
        s->incoming = true;
        s->incoming_token = token;
        s->incoming_bits = bits;
        s->incoming_kind = kind;
        s->incoming_line = line;
        if (s->endpoint.request) {
            s->endpoint.request(s->endpoint_opaque, &request);
        }
        /* No endpoint or an unarmed guest retains the request, never replies. */
    } else if (type == NIKON_WIRE_CANCEL) {
        if (!s->incoming || s->incoming_token != token ||
            s->incoming_bits != bits || s->incoming_kind != kind ||
            s->incoming_line != line) {
            return;
        }
        s->incoming = false;
        if (s->endpoint.cancel) {
            s->endpoint.cancel(s->endpoint_opaque, token);
        }
    } else {
        peer_invalid(s);
    }
}

static int peer_can_read(void *opaque)
{
    NikonPeer *s = opaque;

    return s->failed ? 0 : NIKON_WIRE_SIZE - s->input_length;
}

static void peer_read(void *opaque, const uint8_t *data, int length)
{
    NikonPeer *s = opaque;

    while (length > 0 && !s->failed) {
        unsigned count = MIN(length, NIKON_WIRE_SIZE - s->input_length);

        memcpy(s->input + s->input_length, data, count);
        s->input_length += count;
        data += count;
        length -= count;
        if (s->input_length == NIKON_WIRE_SIZE) {
            s->input_length = 0;
            peer_frame(s);
        }
    }
}

static void peer_event(void *opaque, QEMUChrEvent event)
{
    NikonPeer *s = opaque;

    if (event != CHR_EVENT_OPENED && event != CHR_EVENT_CLOSED) {
        return;
    }
    s->connected = event == CHR_EVENT_OPENED;
    if (!s->connected) {
        peer_discard(s);
    } else {
        s->failed = false;
    }
    if (s->state) {
        s->state(s->state_opaque, s->connected);
    }
}

bool nikon_peer_endpoint(NikonPeer *s, const NikonPeerEndpointOps *ops, void *opaque)
{
    if (ops && s->incoming) {
        return false;
    }
    if (!ops && s->incoming) {
        nikon_peer_endpoint_cancel(s, s->incoming_token);
    }
    g_assert(!s->incoming);
    s->endpoint = ops ? *ops : (NikonPeerEndpointOps) { 0 };
    s->endpoint_opaque = opaque;
    return true;
}

bool nikon_peer_endpoint_cancel(NikonPeer *s, uint64_t token)
{
    if (!s->incoming || s->incoming_token != token) {
        return false;
    }
    s->incoming = false;
    if (s->endpoint.cancel) {
        s->endpoint.cancel(s->endpoint_opaque, token);
    }
    return true;
}

bool nikon_peer_state(NikonPeer *s, NikonPeerState notify, void *opaque)
{
    if (notify && s->state && s->state_opaque != opaque) {
        return false;
    }
    s->state = notify;
    s->state_opaque = opaque;
    if (notify) {
        notify(opaque, s->connected);
    }
    return true;
}

static bool peer_request(NikonPeer *s, NikonPeerKind kind, uint64_t token,
                         uint32_t line, uint32_t word, unsigned bits,
                         NikonPeerComplete complete, void *opaque)
{
    if (!token || !complete || s->pending || !peer_operation(kind, bits, line) ||
        !peer_word(NIKON_WIRE_REQUEST, kind, line, word) ||
        !s->connected || s->failed) {
        return false;
    }
    s->pending = true;
    s->token = token;
    s->bits = bits;
    s->kind = kind;
    s->line = line;
    s->complete = complete;
    s->complete_opaque = opaque;
    if (!peer_send(s, NIKON_WIRE_REQUEST, kind, token, bits, line, word)) {
        s->pending = false;
        s->complete = NULL;
        return false;
    }
    return true;
}

bool nikon_peer_clock(NikonPeer *s, uint64_t token, uint32_t word, unsigned bits,
                      NikonPeerComplete complete, void *opaque)
{
    return peer_request(s, NIKON_PEER_CLOCK, token, 0, word, bits,
                        complete, opaque);
}

bool nikon_peer_i2c(NikonPeer *s, uint64_t token, NikonI2CPhase phase,
                    uint8_t value, NikonPeerComplete complete, void *opaque)
{
    return peer_request(s, NIKON_PEER_I2C, token, phase, value, 8,
                        complete, opaque);
}

void nikon_peer_cancel(NikonPeer *s, uint64_t token)
{
    if (s->pending && s->token == token) {
        bool sent = peer_send(s, NIKON_WIRE_CANCEL, s->kind,
                              token, s->bits, s->line, 0);
        s->pending = false;
        s->complete = NULL;
        if (!sent && s->connected) {
            /* Dropping a cancel while retaining the wire would leave the
             * remote guest transaction live. Closing cancels both endpoints.
             */
            s->connected = false;
            peer_discard(s);
            qemu_chr_fe_disconnect(&s->chr);
        }
    }
}

bool nikon_peer_wire(NikonPeer *s, NikonPeerKind kind, uint32_t line, bool level)
{
    bool sent;

    if (kind != NIKON_PEER_SELECT && kind != NIKON_PEER_GPIO) {
        return false;
    }
    sent = peer_send(s, NIKON_WIRE_SIGNAL, kind, 0, 0, line, level);
    if (!sent && s->connected && !s->failed) {
        /* GPIO edges and chip-select changes cannot be reconstructed by a
         * retry of the current level.  A full wire must fail visibly rather
         * than silently discard an actual guest transition.
         */
        peer_failure(s, "signal output queue is full");
    }
    return sent;
}

bool nikon_peer_reply(NikonPeer *s, uint64_t token, uint32_t word)
{
    if (!s->incoming || s->incoming_token != token ||
        !peer_word(NIKON_WIRE_REPLY, s->incoming_kind, s->incoming_line, word) ||
        !peer_send(s, NIKON_WIRE_REPLY, s->incoming_kind, token,
                   s->incoming_bits, s->incoming_line, word)) {
        return false;
    }
    s->incoming = false;
    return true;
}

static const Property peer_properties[] = {
    DEFINE_PROP_CHR("chardev", NikonPeer, chr),
};

static void peer_realize(DeviceState *dev, Error **errp)
{
    NikonPeer *s = NIKON_PEER(dev);

    /* No chardev is a deliberate disconnected peer, not a successful wire. */
    qemu_chr_fe_set_handlers(&s->chr, peer_can_read, peer_read, peer_event,
                             NULL, s, NULL, true);
}

static void peer_unrealize(DeviceState *dev)
{
    NikonPeer *s = NIKON_PEER(dev);

    s->connected = false;
    peer_discard(s);
    if (s->state) {
        s->state(s->state_opaque, false);
    }
    qemu_chr_fe_deinit(&s->chr, false);
}

static void peer_init(Object *obj)
{
    NIKON_PEER(obj)->output = g_byte_array_new();
}

static void peer_finalize(Object *obj)
{
    NikonPeer *s = NIKON_PEER(obj);

    g_byte_array_unref(s->output);
}

static void peer_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = peer_realize;
    dc->unrealize = peer_unrealize;
    device_class_set_props(dc, peer_properties);
}

static const TypeInfo peer_type = {
    .name = TYPE_NIKON_PEER,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(NikonPeer),
    .instance_init = peer_init,
    .instance_finalize = peer_finalize,
    .class_init = peer_class_init,
};

static void peer_register(void)
{
    type_register_static(&peer_type);
}
type_init(peer_register)
