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
    DeviceState *gpio;
    uint32_t pin;
    bool attached;
    bool selected;
    bool select_known;
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
        if (s->select_known) {
            nikon_peer_wire(s->peer, NIKON_PEER_SELECT, 0, s->selected);
        }
        nikon_spi_async_ready(s->spi);
    }
}

static void spi_select(void *opaque, int index, int level)
{
    NikonSPIPeer *s = opaque;
    bool driven_level;

    /* Resolved floating reset-low is not an actual driven active-low CS. */
    if (s->gpio) {
        if (!nikon_gpio_output(s->gpio, s->pin, &driven_level)) {
            s->select_known = false;
            return;
        }
        level = driven_level;
    }
    s->selected = !!level;
    s->select_known = true;
    if (s->attached) {
        nikon_peer_wire(s->peer, NIKON_PEER_SELECT, 0, s->selected);
    }
}

static const NikonSPIAsyncOps spi_operations = {
    .request = spi_request,
    .cancel = spi_cancel,
};

static const Property spi_peer_properties[] = {
    DEFINE_PROP_LINK("peer", NikonSPIPeer, peer, TYPE_NIKON_PEER, NikonPeer *),
    DEFINE_PROP_LINK("spi", NikonSPIPeer, spi, TYPE_NIKON_SPI, DeviceState *),
    DEFINE_PROP_LINK("gpio", NikonSPIPeer, gpio, TYPE_NIKON_GPIO, DeviceState *),
    DEFINE_PROP_UINT32("pin", NikonSPIPeer, pin, UINT32_MAX),
};

static void spi_peer_realize(DeviceState *dev, Error **errp)
{
    NikonSPIPeer *s = NIKON_SPI_PEER(dev);

    if (!s->peer || !s->spi) {
        error_setg(errp, "nikon-spi-peer requires peer and spi object links");
        return;
    }
    if ((!s->gpio && s->pin != UINT32_MAX) ||
        (s->gpio && s->pin >= 12 * 16)) {
        error_setg(errp, "nikon-spi-peer GPIO requires a valid pin below 192");
        return;
    }
    if (!nikon_peer_state(s->peer, spi_connected, s)) {
        error_setg(errp, "nikon-spi-peer endpoint already has a frontend");
        return;
    }
    nikon_spi_attach_async(s->spi, &spi_operations, s);
    s->attached = true;
    if (s->gpio) {
        qdev_connect_gpio_out_named(s->gpio, "pad", s->pin,
                                   qdev_get_gpio_in_named(dev, "select", 0));
        spi_select(s, 0, 0);
    }
    nikon_peer_state(s->peer, spi_connected, s);
}

static void spi_peer_unrealize(DeviceState *dev)
{
    NikonSPIPeer *s = NIKON_SPI_PEER(dev);

    if (s->attached) {
        if (s->gpio) {
            qdev_connect_gpio_out_named(s->gpio, "pad", s->pin, NULL);
        }
        nikon_peer_state(s->peer, NULL, NULL);
        nikon_spi_detach_async(s->spi, s);
        s->attached = false;
    }
}

static void spi_peer_init(Object *obj)
{
    qdev_init_gpio_in_named(DEVICE(obj), spi_select, "select", 1);
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
    .instance_init = spi_peer_init,
    .class_init = spi_peer_class_init,
};

static void spi_peer_register(void)
{
    type_register_static(&spi_peer_type);
}
type_init(spi_peer_register)
