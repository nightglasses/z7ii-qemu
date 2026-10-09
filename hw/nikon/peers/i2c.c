/* Nonblocking I2C master binding; no target-specific firmware bytes here.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/irq.h"
#include "hw/nikon/devices.h"
#include "hw/nikon/peer.h"

#define TYPE_NIKON_I2C_PEER "nikon-i2c-peer"
OBJECT_DECLARE_SIMPLE_TYPE(NikonI2CPeer, NIKON_I2C_PEER)

struct NikonI2CPeer {
    DeviceState parent_obj;
    NikonPeer *peer;
    DeviceState *i2c;
    DeviceState *gpio;
    uint32_t control, notify;
    qemu_irq notification;
    bool attached;
    bool control_known, control_level;
};

static void i2c_received(void *opaque, uint64_t token, uint32_t result)
{
    NikonI2CPeer *s = opaque;

    nikon_i2c_async_complete(s->i2c, token, result);
}

static bool i2c_request(void *opaque, uint64_t token, NikonI2CPhase phase,
                        uint8_t value)
{
    NikonI2CPeer *s = opaque;

    return nikon_peer_i2c(s->peer, token, phase, value, i2c_received, s);
}

static void i2c_cancel(void *opaque, uint64_t token)
{
    NikonI2CPeer *s = opaque;

    nikon_peer_cancel(s->peer, token);
}

static void i2c_connected(void *opaque, bool connected)
{
    NikonI2CPeer *s = opaque;

    if (connected && s->attached) {
        if (s->gpio) {
            s->control_known = nikon_gpio_output(s->gpio, s->control,
                                                 &s->control_level);
        }
        if (s->control_known) {
            nikon_peer_wire(s->peer, NIKON_PEER_GPIO,
                            NIKON_TOUCH_CONTROL, s->control_level);
        }
        nikon_i2c_async_ready(s->i2c);
    }
}

static void i2c_control(void *opaque, int index, int level)
{
    NikonI2CPeer *s = opaque;
    bool driven;

    if (s->gpio) {
        if (!nikon_gpio_output(s->gpio, s->control, &driven)) {
            s->control_known = false;
            return;
        }
        level = driven;
    }
    s->control_known = true;
    s->control_level = !!level;
    if (s->attached) {
        nikon_peer_wire(s->peer, NIKON_PEER_GPIO,
                        NIKON_TOUCH_CONTROL, s->control_level);
    }
}

static void i2c_notification(void *opaque, NikonPeerKind kind, uint32_t line,
                             bool level, uint64_t time_ns)
{
    NikonI2CPeer *s = opaque;

    if (kind == NIKON_PEER_GPIO && line == NIKON_TOUCH_NOTIFY) {
        qemu_set_irq(s->notification, level);
    }
}

static const NikonPeerEndpointOps i2c_endpoint = {
    .wire = i2c_notification,
};

static const NikonI2CAsyncOps i2c_operations = {
    .request = i2c_request,
    .cancel = i2c_cancel,
};

static const Property i2c_peer_properties[] = {
    DEFINE_PROP_LINK("peer", NikonI2CPeer, peer, TYPE_NIKON_PEER, NikonPeer *),
    DEFINE_PROP_LINK("i2c", NikonI2CPeer, i2c, TYPE_NIKON_I2C, DeviceState *),
    DEFINE_PROP_LINK("gpio", NikonI2CPeer, gpio, TYPE_NIKON_GPIO, DeviceState *),
    DEFINE_PROP_UINT32("control", NikonI2CPeer, control, UINT32_MAX),
    DEFINE_PROP_UINT32("notify", NikonI2CPeer, notify, UINT32_MAX),
};

static void i2c_peer_realize(DeviceState *dev, Error **errp)
{
    NikonI2CPeer *s = NIKON_I2C_PEER(dev);

    if (!s->peer || !s->i2c) {
        error_setg(errp, "nikon-i2c-peer requires peer and i2c object links");
        return;
    }
    if ((!s->gpio && (s->control != UINT32_MAX || s->notify != UINT32_MAX)) ||
        (s->gpio && (s->control >= 192 || s->notify >= 192 ||
                     s->control == s->notify))) {
        error_setg(errp, "nikon-i2c-peer GPIO requires distinct control/notify pins");
        return;
    }
    if (!nikon_peer_state(s->peer, i2c_connected, s)) {
        error_setg(errp, "nikon-i2c-peer endpoint already has a frontend");
        return;
    }
    /* Reserve the endpoint before the controller's permanent async binding.
     * Realization runs under the BQL, so these reservations are atomic with
     * respect to incoming transport events. A rejected incoming request must
     * remain pending until its actual sender cancels it. */
    if (!nikon_peer_endpoint(s->peer, &i2c_endpoint, s)) {
        error_setg(errp, "nikon-i2c-peer endpoint has a pending incoming request");
        nikon_peer_state(s->peer, NULL, NULL);
        return;
    }
    if (!nikon_i2c_attach_async(s->i2c, &i2c_operations, s, errp)) {
        nikon_peer_endpoint(s->peer, NULL, NULL);
        nikon_peer_state(s->peer, NULL, NULL);
        return;
    }
    s->attached = true;
    if (s->gpio) {
        qdev_connect_gpio_out_named(s->gpio, "pad", s->control,
                                   qdev_get_gpio_in_named(dev, "control", 0));
        qdev_connect_gpio_out_named(dev, "notify", 0,
            qdev_get_gpio_in_named(s->gpio, "external-level", s->notify));
        i2c_control(s, 0, 0);
    }
    nikon_peer_state(s->peer, i2c_connected, s);
}

static void i2c_peer_unrealize(DeviceState *dev)
{
    NikonI2CPeer *s = NIKON_I2C_PEER(dev);

    if (s->attached) {
        if (s->gpio) {
            qdev_connect_gpio_out_named(s->gpio, "pad", s->control, NULL);
            qdev_connect_gpio_out_named(dev, "notify", 0, NULL);
        }
        nikon_peer_endpoint(s->peer, NULL, NULL);
        nikon_peer_state(s->peer, NULL, NULL);
        nikon_i2c_detach_async(s->i2c, s);
        s->attached = false;
    }
}

static void i2c_peer_init(Object *obj)
{
    NikonI2CPeer *s = NIKON_I2C_PEER(obj);

    qdev_init_gpio_in_named(DEVICE(obj), i2c_control, "control", 1);
    qdev_init_gpio_out_named(DEVICE(obj), &s->notification, "notify", 1);
}

static void i2c_peer_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = i2c_peer_realize;
    dc->unrealize = i2c_peer_unrealize;
    device_class_set_props(dc, i2c_peer_properties);
}

static const TypeInfo i2c_peer_type = {
    .name = TYPE_NIKON_I2C_PEER,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(NikonI2CPeer),
    .instance_init = i2c_peer_init,
    .class_init = i2c_peer_class_init,
};

static void i2c_peer_register(void)
{
    type_register_static(&i2c_peer_type);
}
type_init(i2c_peer_register)
