/*
 * Host keys drive the seventeen GPIO inputs selected by original EX software.
 * Host key assignment and active-low switch levels are a functional profile;
 * camera button names, rotary/touch wiring and peer packets remain unbound.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/core/irq.h"
#include "hw/core/resettable.h"
#include "hw/nikon/input.h"
#include "migration/vmstate.h"
#include "standard-headers/linux/input-event-codes.h"
#include "system/reset.h"
#include "ui/input.h"

OBJECT_DECLARE_SIMPLE_TYPE(NikonInputState, NIKON_INPUT)

const uint8_t nikon_input_gpio_ids[NIKON_INPUT_PADS] = {
    0x0a, 0x11, 0x1f, 0x25, 0x1b, 0x47, 0x4d, 0x5b, 0x5e,
    0x61, 0x62, 0x6d, 0x6e, 0x6f, 0x1d, 0x53, 0x1c,
};

static const unsigned int nikon_input_host_keys[NIKON_INPUT_PADS] = {
    KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
    KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
    KEY_1, KEY_2, KEY_3, KEY_4, KEY_5,
};

struct NikonInputState {
    DeviceState parent_obj;
    QemuInputHandlerState *handler;
    qemu_irq pads[NIKON_INPUT_PADS];
    bool down[NIKON_INPUT_PADS];
    uint32_t initial_down_mask;
};

static void nikon_input_event(DeviceState *dev, QemuConsole *src,
                              QemuInputEvent *event)
{
    NikonInputState *s = NIKON_INPUT(dev);
    unsigned int i;

    assert(event->type == INPUT_EVENT_KIND_KEY);
    for (i = 0; i < NIKON_INPUT_PADS; i++) {
        if (event->key.key == nikon_input_host_keys[i]) {
            if (s->down[i] != event->key.down) {
                s->down[i] = event->key.down;
                qemu_set_irq(s->pads[i], !s->down[i]);
            }
            return;
        }
    }
}

static const QemuInputHandler nikon_input_handler = {
    .name = "Nikon EX GPIO scan profile",
    .mask = INPUT_EVENT_MASK_KEY,
    .event = nikon_input_event,
};

static void nikon_input_publish(NikonInputState *s)
{
    unsigned int i;

    for (i = 0; i < NIKON_INPUT_PADS; i++) {
        qemu_set_irq(s->pads[i], !s->down[i]);
    }
}

static void nikon_input_reset_exit(Object *obj, ResetType type)
{
    /* All receiver hold phases have completed. A board reset does not release
     * host keys or turn a held level into a fresh input event.
     */
    nikon_input_publish(NIKON_INPUT(obj));
}

static void nikon_input_realize(DeviceState *dev, Error **errp)
{
    NikonInputState *s = NIKON_INPUT(dev);

    s->handler = qemu_input_handler_register(dev, &nikon_input_handler);
    for (unsigned i = 0; i < NIKON_INPUT_PADS; i++) {
        s->down[i] = (s->initial_down_mask >> i) & 1;
    }
    /* This bus-less frontend is not reached through a resettable qbus. */
    qemu_register_resettable(OBJECT(dev));
    nikon_input_publish(s);
}

static void nikon_input_unrealize(DeviceState *dev)
{
    NikonInputState *s = NIKON_INPUT(dev);
    unsigned int i;

    qemu_unregister_resettable(OBJECT(dev));
    qemu_input_handler_unregister(s->handler);
    s->handler = NULL;
    for (i = 0; i < NIKON_INPUT_PADS; i++) {
        s->down[i] = false;
        qemu_set_irq(s->pads[i], 1);
    }
}

static void nikon_input_init(Object *obj)
{
    NikonInputState *s = NIKON_INPUT(obj);

    qdev_init_gpio_out_named(DEVICE(obj), s->pads, "pad", NIKON_INPUT_PADS);
}

static const VMStateDescription nikon_input_vmstate = {
    .name = TYPE_NIKON_INPUT,
    .unmigratable = true,
};

static void nikon_input_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    dc->realize = nikon_input_realize;
    dc->unrealize = nikon_input_unrealize;
    dc->vmsd = &nikon_input_vmstate;
    rc->phases.exit = nikon_input_reset_exit;
    dc->user_creatable = false;
}

static const TypeInfo nikon_input_type = {
    .name = TYPE_NIKON_INPUT,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(NikonInputState),
    .instance_init = nikon_input_init,
    .class_init = nikon_input_class_init,
};

static void nikon_input_register_types(void)
{
    type_register_static(&nikon_input_type);
}
type_init(nikon_input_register_types)
