/* VR SCI5 peer endpoint: real pad levels and guest-owned serial bytes.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/core/qdev-properties.h"
#include "hw/nikon/peer.h"
#include "hw/nikon/vr.h"
#include "system/reset.h"

OBJECT_DECLARE_SIMPLE_TYPE(NikonVRPeer, NIKON_VR_PEER)

struct NikonVRPeer {
    DeviceState parent_obj;
    NikonPeer *peer;
    DeviceState *peripherals;
    bool attached, connected, pending, reply_ready;
    uint64_t token;
    uint32_t word, reply;
    unsigned bits, byte_index;
};

static void vr_peer_clock(NikonVRPeer *s);

static void vr_peer_reply(NikonVRPeer *s)
{
    if (s->pending && s->reply_ready &&
        nikon_peer_reply(s->peer, s->token, s->reply)) {
        s->pending = s->reply_ready = false;
    }
}

static void vr_peer_received(void *opaque, uint64_t token, uint32_t word)
{
    NikonVRPeer *s = opaque;

    if (!s->pending || s->token != token) {
        return;
    }
    s->reply |= (word & 255) << (s->byte_index * 8);
    if (++s->byte_index < s->bits / 8) {
        vr_peer_clock(s);
    } else {
        s->reply_ready = true;
        vr_peer_reply(s);
    }
}

static void vr_peer_clock(NikonVRPeer *s)
{
    /* MAS VR configuration clears CTRL.MSB1ST; SCI5 clears SCMR.SDIR.
     * A little-endian 16-bit DMA word clocks its low byte, then its high byte.
     */
    if (!nikon_vr_peripherals_clock(s->peripherals, s->token,
                                    s->word >> (s->byte_index * 8), 8,
                                    vr_peer_received, s)) {
        error_report("nikon-vr-peer: SCI5 byte clock unavailable; no reply");
    }
}

static void vr_peer_request(void *opaque, const NikonPeerRequest *request)
{
    NikonVRPeer *s = opaque;

    s->pending = true;
    s->reply_ready = false;
    s->token = request->token;
    s->word = request->word;
    s->reply = 0;
    s->bits = request->bits;
    s->byte_index = 0;
    if (s->bits != 8 && s->bits != 16) {
        error_report("nikon-vr-peer: source supports 8/16-bit clocks; no reply");
        return;
    }
    vr_peer_clock(s);
}

static void vr_peer_cancel(void *opaque, uint64_t token)
{
    NikonVRPeer *s = opaque;

    nikon_vr_peripherals_cancel(s->peripherals, token);
    if (s->pending && s->token == token) {
        s->pending = s->reply_ready = false;
    }
}

static void vr_peer_wire(void *opaque, NikonPeerKind kind, uint32_t line,
                         bool level, uint64_t time_ns)
{
    NikonVRPeer *s = opaque;

    if (kind == NIKON_PEER_SELECT && !line) {
        nikon_vr_peripherals_select(s->peripherals, level);
    } else if (kind == NIKON_PEER_GPIO) {
        nikon_vr_peripherals_gpio(s->peripherals, line, level);
    }
}

static bool vr_peer_gpio(void *opaque, unsigned pin, bool level)
{
    NikonVRPeer *s = opaque;

    return s->connected &&
           nikon_peer_wire(s->peer, NIKON_PEER_GPIO, pin, level);
}

static void vr_peer_connected(void *opaque, bool connected)
{
    NikonVRPeer *s = opaque;
    bool changed = s->connected != connected;

    s->connected = connected;
    if (!s->attached) {
        return;
    }
    if (!connected && s->pending) {
        uint64_t token = s->token;

        nikon_peer_endpoint_cancel(s->peer, token);
        vr_peer_cancel(s, token);
    } else if (connected) {
        vr_peer_reply(s);
        if (changed) {
            nikon_vr_peripherals_gpio_out(s->peripherals, vr_peer_gpio, s);
        } else {
            nikon_vr_peripherals_gpio_retry(s->peripherals);
        }
    }
}

static const NikonPeerEndpointOps vr_peer_operations = {
    .request = vr_peer_request,
    .cancel = vr_peer_cancel,
    .wire = vr_peer_wire,
};

static const Property vr_peer_properties[] = {
    DEFINE_PROP_LINK("peer", NikonVRPeer, peer, TYPE_NIKON_PEER, NikonPeer *),
    DEFINE_PROP_LINK("peripherals", NikonVRPeer, peripherals,
                     TYPE_NIKON_VR_PERIPHERALS, DeviceState *),
};

static void vr_peer_reset(DeviceState *dev)
{
    NikonVRPeer *s = NIKON_VR_PEER(dev);

    if (s->pending) {
        uint64_t token = s->token;

        nikon_peer_endpoint_cancel(s->peer, token);
        vr_peer_cancel(s, token);
    }
}

static void vr_peer_realize(DeviceState *dev, Error **errp)
{
    NikonVRPeer *s = NIKON_VR_PEER(dev);

    if (!s->peer || !s->peripherals) {
        error_setg(errp, "nikon-vr-peer requires peer and peripherals links");
        return;
    }
    if (!nikon_peer_state(s->peer, vr_peer_connected, s)) {
        error_setg(errp, "nikon-vr-peer requires an unused peer frontend");
        return;
    }
    if (!nikon_vr_peripherals_gpio_out(s->peripherals, vr_peer_gpio, s)) {
        nikon_peer_state(s->peer, NULL, NULL);
        error_setg(errp, "nikon-vr-peer requires an unused VR GPIO observer");
        return;
    }
    if (!nikon_peer_endpoint(s->peer, &vr_peer_operations, s)) {
        nikon_vr_peripherals_gpio_out(s->peripherals, NULL, s);
        nikon_peer_state(s->peer, NULL, NULL);
        error_setg(errp, "VR SCI endpoint cannot attach during an incoming request");
        return;
    }
    s->attached = true;
    /* This frontend has no parent bus to include it in system resets. */
    qemu_register_resettable(OBJECT(dev));
    /* The normal SPI select wire starts inactive, as for the local SSI slave. */
    nikon_vr_peripherals_select(s->peripherals, true);
}

static void vr_peer_unrealize(DeviceState *dev)
{
    NikonVRPeer *s = NIKON_VR_PEER(dev);

    if (s->attached) {
        qemu_unregister_resettable(OBJECT(dev));
        vr_peer_reset(dev);
        nikon_peer_endpoint(s->peer, NULL, NULL);
        nikon_peer_state(s->peer, NULL, NULL);
        nikon_vr_peripherals_gpio_out(s->peripherals, NULL, s);
        s->attached = false;
    }
}

static void vr_peer_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = vr_peer_realize;
    dc->unrealize = vr_peer_unrealize;
    device_class_set_legacy_reset(dc, vr_peer_reset);
    device_class_set_props(dc, vr_peer_properties);
}

static const TypeInfo vr_peer_type = {
    .name = TYPE_NIKON_VR_PEER,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(NikonVRPeer),
    .class_init = vr_peer_class_init,
};

static void vr_peer_register(void)
{
    type_register_static(&vr_peer_type);
}
type_init(vr_peer_register)
