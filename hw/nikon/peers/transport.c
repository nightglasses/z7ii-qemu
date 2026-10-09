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
    NikonPeerComplete complete;
    void *complete_opaque;
    bool incoming;
    uint64_t incoming_token;
    unsigned incoming_bits;
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

static void peer_invalid(NikonPeer *s)
{
    error_report("nikon-peer: invalid transport frame; no guest completion");
    s->failed = true;
    s->connected = false;
    peer_discard(s);
    if (s->state) {
        s->state(s->state_opaque, false);
    }
    qemu_chr_fe_disconnect(&s->chr);
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
    if (kind != NIKON_PEER_CLOCK || !token || bits < 1 || bits > 32 || line) {
        peer_invalid(s);
        return;
    }
    if (type == NIKON_WIRE_REPLY) {
        NikonPeerComplete complete;
        void *opaque;

        /* Cancelled and stale replies cannot complete a new transaction. */
        if (!s->pending || s->token != token || s->bits != bits) {
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
        if (s->endpoint.request) {
            s->endpoint.request(s->endpoint_opaque, &request);
        }
        /* No endpoint or an unarmed guest retains the request, never replies. */
    } else if (type == NIKON_WIRE_CANCEL) {
        if (word || !s->incoming || s->incoming_token != token) {
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

void nikon_peer_endpoint(NikonPeer *s, const NikonPeerEndpointOps *ops, void *opaque)
{
    g_assert(!s->incoming);
    s->endpoint = ops ? *ops : (NikonPeerEndpointOps) { 0 };
    s->endpoint_opaque = opaque;
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

bool nikon_peer_clock(NikonPeer *s, uint64_t token, uint32_t word, unsigned bits,
                      NikonPeerComplete complete, void *opaque)
{
    if (!token || !complete || s->pending || bits < 1 || bits > 32 ||
        !s->connected || s->failed) {
        return false;
    }
    s->pending = true;
    s->token = token;
    s->bits = bits;
    s->complete = complete;
    s->complete_opaque = opaque;
    if (!peer_send(s, NIKON_WIRE_REQUEST, NIKON_PEER_CLOCK, token, bits, 0, word)) {
        s->pending = false;
        s->complete = NULL;
        return false;
    }
    return true;
}

void nikon_peer_cancel(NikonPeer *s, uint64_t token)
{
    if (s->pending && s->token == token) {
        bool sent = peer_send(s, NIKON_WIRE_CANCEL, NIKON_PEER_CLOCK,
                              token, s->bits, 0, 0);
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
    if (kind != NIKON_PEER_SELECT && kind != NIKON_PEER_GPIO) {
        return false;
    }
    return peer_send(s, NIKON_WIRE_SIGNAL, kind, 0, 0, line, level);
}

bool nikon_peer_reply(NikonPeer *s, uint64_t token, uint32_t word)
{
    if (!s->incoming || s->incoming_token != token ||
        !peer_send(s, NIKON_WIRE_REPLY, NIKON_PEER_CLOCK, token,
                   s->incoming_bits, 0, word)) {
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
