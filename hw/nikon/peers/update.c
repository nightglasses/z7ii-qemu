/* Source-bounded functional SPI update service, independent of MCU execution.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "hw/core/qdev-properties.h"
#include "hw/nikon/update.h"
#include "hw/nikon/vrinput.h"
#include "migration/vmstate.h"
#include "system/reset.h"

struct NikonUpdatePeer {
    DeviceState parent_obj;
    NikonPeer *peer;
    DeviceState *input;
    uint32_t stream_size;
    char *origin;
    bool attached, connected, selected, valid, failed;
    bool pending, reply_ready;
    uint64_t token, frames;
    uint32_t reply;
    uint8_t frame[268], status;
    unsigned received;
};

static void update_discard(NikonUpdatePeer *s)
{
    s->selected = s->valid = false;
    s->received = 0;
    nikon_vr_input_upload_cancel(s->input);
}

static void update_reply(NikonUpdatePeer *s)
{
    if (s->pending && s->reply_ready &&
        nikon_peer_reply(s->peer, s->token, s->reply)) {
        s->pending = s->reply_ready = false;
    }
}

static void update_request(void *opaque, const NikonPeerRequest *request)
{
    NikonUpdatePeer *s = opaque;
    unsigned i;

    s->pending = true;
    s->reply_ready = false;
    s->token = request->token;
    s->reply = 0;
    if (!s->selected || !s->connected ||
        request->kind != NIKON_PEER_CLOCK || request->line ||
        (request->bits != 8 && request->bits != 16)) {
        /* An inactive or unsupported clock remains uncompleted until cancel.
         * It cannot turn into a clock belonging to a later CS interval.
         */
        return;
    }
    for (i = 0; i < request->bits / 8; i++) {
        uint8_t output = s->received ? 0xff : s->status;

        if (s->received < sizeof(s->frame)) {
            s->frame[s->received++] = request->word >> (i * 8);
        } else {
            s->valid = false;
        }
        /* Bound MAS profile: 16-bit little-endian words, LSB-first bytes.
         * The unconsumed response tail is explicitly functional idle 0xffff.
         */
        s->reply |= (uint32_t)output << (i * 8);
    }
    s->reply_ready = true;
    update_reply(s);
}

static void update_cancel(void *opaque, uint64_t token)
{
    NikonUpdatePeer *s = opaque;

    if (s->pending && s->token == token) {
        s->pending = s->reply_ready = false;
        s->failed = true;
        update_discard(s);
    }
}

static void update_wire(void *opaque, NikonPeerKind kind, uint32_t line,
                        bool level, uint64_t time_ns)
{
    NikonUpdatePeer *s = opaque;
    Error *error = NULL;
    g_autoptr(GBytes) packet = NULL;

    if (kind != NIKON_PEER_SELECT || line || !s->connected) {
        return;
    }
    if (!level) {
        if (!s->selected) {
            s->selected = s->valid = true;
            s->received = 0;
            s->status = 1;
            /* SPI is full duplex: latch before receiving the command. Only
             * actual write/flush completion can make 2 or 3 available.
             */
            nikon_vr_input_upload_status(s->input, &s->status);
        }
        return;
    }
    if (!s->selected) {
        return;
    }
    s->selected = false;
    s->frames++;
    if (s->pending) {
        nikon_peer_endpoint_cancel(s->peer, s->token);
        update_discard(s);
        s->failed = true;
        return;
    }
    if (!s->valid || (s->received != 152 && s->received != 268)) {
        update_discard(s);
        s->failed = true;
        return;
    }
    packet = g_bytes_new(s->frame, s->received);
    if (!nikon_vr_input_upload_packet(s->input, DEVICE(s), packet, &error)) {
        /* Never reuse a previous eligible 2 after rejecting the new frame. */
        update_discard(s);
        s->failed = true;
        error_report_err(error);
    } else if (s->frame[0] == 0xf5) {
        s->failed = false;
    }
    s->valid = false;
    s->received = 0;
}

static void update_connected(void *opaque, bool connected)
{
    NikonUpdatePeer *s = opaque;

    s->connected = connected;
    if (!s->attached) {
        return;
    }
    if (!connected) {
        if (s->pending) {
            nikon_peer_endpoint_cancel(s->peer, s->token);
        }
        s->pending = s->reply_ready = false;
        s->failed = true;
        update_discard(s);
    } else {
        update_reply(s);
    }
}

static const NikonPeerEndpointOps update_operations = {
    .request = update_request,
    .cancel = update_cancel,
    .wire = update_wire,
};

static void update_reset(DeviceState *dev)
{
    NikonUpdatePeer *s = NIKON_UPDATE_PEER(dev);

    if (s->pending) {
        nikon_peer_endpoint_cancel(s->peer, s->token);
    }
    s->pending = s->reply_ready = s->failed = false;
    s->frames = 0;
    update_discard(s);
}

static void update_realize(DeviceState *dev, Error **errp)
{
    NikonUpdatePeer *s = NIKON_UPDATE_PEER(dev);

    if (!s->peer || !s->input) {
        error_setg(errp, "nikon-update-peer requires explicit peer/input links");
        return;
    }
    if (!nikon_peer_state(s->peer, update_connected, s)) {
        error_setg(errp, "nikon-update-peer requires an unused peer frontend");
        return;
    }
    if (!nikon_vr_input_upload_attach(s->input, dev, s->stream_size,
                                     s->origin, errp)) {
        nikon_peer_state(s->peer, NULL, NULL);
        return;
    }
    if (!nikon_peer_endpoint(s->peer, &update_operations, s)) {
        nikon_vr_input_upload_detach(s->input, dev);
        nikon_peer_state(s->peer, NULL, NULL);
        error_setg(errp, "Update endpoint cannot attach during an incoming request");
        return;
    }
    s->attached = true;
    qemu_register_resettable(OBJECT(dev));
}

static void update_unrealize(DeviceState *dev)
{
    NikonUpdatePeer *s = NIKON_UPDATE_PEER(dev);

    if (s->attached) {
        qemu_unregister_resettable(OBJECT(dev));
        update_reset(dev);
        nikon_peer_endpoint(s->peer, NULL, NULL);
        nikon_peer_state(s->peer, NULL, NULL);
        nikon_vr_input_upload_detach(s->input, dev);
        s->attached = false;
    }
}

static bool update_get_failed(Object *obj, Error **errp)
{
    return NIKON_UPDATE_PEER(obj)->failed;
}

static void update_init(Object *obj)
{
    NikonUpdatePeer *s = NIKON_UPDATE_PEER(obj);

    object_property_add_uint64_ptr(obj, "frames", &s->frames, OBJ_PROP_FLAG_READ);
    object_property_add_bool(obj, "failed", update_get_failed, NULL);
}

static const Property update_properties[] = {
    DEFINE_PROP_LINK("peer", NikonUpdatePeer, peer, TYPE_NIKON_PEER, NikonPeer *),
    DEFINE_PROP_LINK("input", NikonUpdatePeer, input, TYPE_NIKON_VR_INPUT,
                     DeviceState *),
    DEFINE_PROP_UINT32("stream-size", NikonUpdatePeer, stream_size, 0),
    DEFINE_PROP_STRING("origin", NikonUpdatePeer, origin),
};

static const VMStateDescription update_vmstate = {
    .name = TYPE_NIKON_UPDATE_PEER,
    .unmigratable = true,
};

static void update_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = update_realize;
    dc->unrealize = update_unrealize;
    dc->vmsd = &update_vmstate;
    dc->hotpluggable = false;
    device_class_set_legacy_reset(dc, update_reset);
    device_class_set_props(dc, update_properties);
}

static const TypeInfo update_type = {
    .name = TYPE_NIKON_UPDATE_PEER,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(NikonUpdatePeer),
    .instance_init = update_init,
    .class_init = update_class_init,
};

static void update_register(void)
{
    type_register_static(&update_type);
}
type_init(update_register)

DeviceState *nikon_update_peer_create(Object *owner, NikonPeer *peer,
                                     DeviceState *input, uint32_t stream_size,
                                     const char *origin, Error **errp)
{
    DeviceState *dev;

    if (!owner || !peer || !input || !origin || !*origin ||
        !object_dynamic_cast(OBJECT(input), TYPE_NIKON_VR_INPUT)) {
        error_setg(errp, "Update factory requires explicit owner/peer/input");
        return NULL;
    }
    if (object_property_find(owner, "update")) {
        error_setg(errp, "Update factory owner already has an update child");
        return NULL;
    }
    dev = qdev_new(TYPE_NIKON_UPDATE_PEER);
    object_property_set_link(OBJECT(dev), "peer", OBJECT(peer), &error_abort);
    object_property_set_link(OBJECT(dev), "input", OBJECT(input), &error_abort);
    qdev_prop_set_uint32(dev, "stream-size", stream_size);
    qdev_prop_set_string(dev, "origin", origin);
    object_property_add_child(owner, "update", OBJECT(dev));
    if (!qdev_realize(dev, NULL, errp)) {
        object_unparent(OBJECT(dev));
        object_unref(OBJECT(dev));
        return NULL;
    }
    object_unref(OBJECT(dev));
    return dev;
}
