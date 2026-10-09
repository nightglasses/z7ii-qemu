/* ARM SPI frontend binding. Common peer transport has no target dependency.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/core/qdev-properties.h"
#include "hw/nikon/devices.h"
#include "hw/nikon/peer.h"

#define TYPE_NIKON_SPI_PEER "nikon-spi-peer"
OBJECT_DECLARE_SIMPLE_TYPE(NikonSPIPeer, NIKON_SPI_PEER)

struct NikonSPIPeer {
    DeviceState parent_obj;
    NikonPeer *peer;
    DeviceState *spi;
    bool attached;
};

static void spi_received(void *opaque, uint64_t token, uint32_t word)
{
    NikonSPIPeer *s = opaque;

    nikon_spi_async_complete(s->spi, token, word);
}

static bool spi_request(void *opaque, uint64_t token, uint32_t word, unsigned bits)
{
    NikonSPIPeer *s = opaque;

    return nikon_peer_clock(s->peer, token, word, bits, spi_received, s);
}

static void spi_cancel(void *opaque, uint64_t token)
{
    NikonSPIPeer *s = opaque;

    nikon_peer_cancel(s->peer, token);
}

static void spi_connected(void *opaque, bool connected)
{
    NikonSPIPeer *s = opaque;

    if (connected && s->attached) {
        nikon_spi_async_ready(s->spi);
    }
}

static const NikonSPIAsyncOps spi_operations = {
    .request = spi_request,
    .cancel = spi_cancel,
};

static const Property spi_peer_properties[] = {
    DEFINE_PROP_LINK("peer", NikonSPIPeer, peer, TYPE_NIKON_PEER, NikonPeer *),
    DEFINE_PROP_LINK("spi", NikonSPIPeer, spi, TYPE_NIKON_SPI, DeviceState *),
};

static void spi_peer_realize(DeviceState *dev, Error **errp)
{
    NikonSPIPeer *s = NIKON_SPI_PEER(dev);

    if (!s->peer || !s->spi) {
        error_setg(errp, "nikon-spi-peer requires peer and spi object links");
        return;
    }
    if (!nikon_peer_state(s->peer, spi_connected, s)) {
        error_setg(errp, "nikon-spi-peer endpoint already has a frontend");
        return;
    }
    nikon_spi_attach_async(s->spi, &spi_operations, s);
    s->attached = true;
    nikon_peer_state(s->peer, spi_connected, s);
}

static void spi_peer_unrealize(DeviceState *dev)
{
    NikonSPIPeer *s = NIKON_SPI_PEER(dev);

    if (s->attached) {
        nikon_peer_state(s->peer, NULL, NULL);
        nikon_spi_detach_async(s->spi, s);
        s->attached = false;
    }
}

static void spi_peer_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = spi_peer_realize;
    dc->unrealize = spi_peer_unrealize;
    device_class_set_props(dc, spi_peer_properties);
}

static const TypeInfo spi_peer_type = {
    .name = TYPE_NIKON_SPI_PEER,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(NikonSPIPeer),
    .class_init = spi_peer_class_init,
};

static void spi_peer_register(void)
{
    type_register_static(&spi_peer_type);
}
type_init(spi_peer_register)
