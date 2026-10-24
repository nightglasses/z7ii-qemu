/*
 * Finite CComp request/acknowledge GPIO peer.
 * A complete request pulse produces an ACK pulse on the virtual clock; the
 * original GPIO/EXIU/GIC and guest callback retain all event dispatch work.
 * The service delay and pulse width are virtual, not measured hardware timing.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "hw/nikon/devices.h"
#include "migration/vmstate.h"

#define CCOMP_SERVICE_NS 1000
#define CCOMP_ACK_NS 1000

OBJECT_DECLARE_SIMPLE_TYPE(NikonCCompState, NIKON_CCOMP)

typedef enum NikonCCompPhase {
    CCOMP_IDLE,
    CCOMP_REQUEST_HIGH,
    CCOMP_WAIT_ACK,
    CCOMP_ACK_HIGH,
    CCOMP_DISCARD_HIGH,
} NikonCCompPhase;

struct NikonCCompState {
    SysBusDevice parent_obj;
    QEMUTimer *timer;
    qemu_irq ack;
    int64_t ack_end_ns;
    NikonCCompPhase phase;
    bool request;
};

static void nikon_ccomp_tick(void *opaque)
{
    NikonCCompState *s = opaque;

    switch (s->phase) {
    case CCOMP_WAIT_ACK:
        s->phase = CCOMP_ACK_HIGH;
        timer_mod(s->timer, s->ack_end_ns);
        qemu_set_irq(s->ack, 1);
        break;
    case CCOMP_ACK_HIGH:
        s->phase = CCOMP_IDLE;
        qemu_set_irq(s->ack, 0);
        break;
    default:
        break;
    }
}

static void nikon_ccomp_request(void *opaque, int index, int level)
{
    NikonCCompState *s = opaque;
    bool high = !!level;

    if (s->request == high) {
        return;
    }
    s->request = high;
    if (!s->timer) {
        return;
    }
    switch (s->phase) {
    case CCOMP_IDLE:
        if (high) {
            s->phase = CCOMP_REQUEST_HIGH;
        }
        break;
    case CCOMP_REQUEST_HIGH:
        s->phase = CCOMP_WAIT_ACK;
        s->ack_end_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                       CCOMP_SERVICE_NS + CCOMP_ACK_NS;
        timer_mod(s->timer, s->ack_end_ns - CCOMP_ACK_NS);
        break;
    case CCOMP_WAIT_ACK:
    case CCOMP_ACK_HIGH:
        /* Virtual rejection policy: overlap cancels, without queuing an ACK. */
        timer_del(s->timer);
        s->phase = CCOMP_DISCARD_HIGH;
        qemu_set_irq(s->ack, 0);
        break;
    case CCOMP_DISCARD_HIGH:
        s->phase = CCOMP_IDLE;
        break;
    }
}

static void nikon_ccomp_reset(DeviceState *dev)
{
    NikonCCompState *s = NIKON_CCOMP(dev);

    if (s->timer) {
        timer_del(s->timer);
    }
    /* Retain the real request level; its old falling edge is not a new pulse. */
    s->phase = CCOMP_IDLE;
    qemu_set_irq(s->ack, 0);
}

static void nikon_ccomp_realize(DeviceState *dev, Error **errp)
{
    NikonCCompState *s = NIKON_CCOMP(dev);

    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, nikon_ccomp_tick, s);
}

static void nikon_ccomp_unrealize(DeviceState *dev)
{
    NikonCCompState *s = NIKON_CCOMP(dev);

    nikon_ccomp_reset(dev);
    timer_free(s->timer);
    s->timer = NULL;
}

static void nikon_ccomp_init(Object *obj)
{
    NikonCCompState *s = NIKON_CCOMP(obj);

    qdev_init_gpio_in_named(DEVICE(obj), nikon_ccomp_request, "request", 1);
    qdev_init_gpio_out_named(DEVICE(obj), &s->ack, "ack", 1);
}

static const VMStateDescription nikon_ccomp_vmstate = {
    .name = "nikon-ccomp",
    .unmigratable = true,
};

static void nikon_ccomp_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = nikon_ccomp_realize;
    dc->unrealize = nikon_ccomp_unrealize;
    dc->vmsd = &nikon_ccomp_vmstate;
    dc->user_creatable = false;
    device_class_set_legacy_reset(dc, nikon_ccomp_reset);
}

static const TypeInfo nikon_ccomp_type = {
    .name = TYPE_NIKON_CCOMP,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonCCompState),
    .instance_init = nikon_ccomp_init,
    .class_init = nikon_ccomp_class_init,
};

static void nikon_ccomp_register_types(void)
{
    type_register_static(&nikon_ccomp_type);
}

type_init(nikon_ccomp_register_types)
