/*
 * Host keys drive the seventeen GPIO inputs selected by original EX software.
 * F10 (pad 9, PG1) drives shutter half-press (S1); F11 (pad 10, PG2) AF-ON;
 * F12 (pad 11, PG13) shutter full-press (S2); F4 (pad 3, PC5) main power.
 * F13 toggles the maintained capture selector; F14/F15 select mode ADC detents.
 * F16..F19 drive two bounded four-phase command-dial contact sequences.
 * Sixteen independent active-low expander contacts supply normal buttons.
 * Host key assignments are a functional profile; the guest owns sampling,
 * debounce, message construction and capture policy.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
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

const uint8_t nikon_input_expander_ids[NIKON_INPUT_EXPANDER_PADS] = {
    9, 3, 8, 23, 2, 22, 21, 6, 7, 5, 4, 14, 15, 13, 12, 11,
};

static const unsigned int nikon_input_expander_keys[NIKON_INPUT_EXPANDER_PADS] = {
    KEY_F20, KEY_ENTER, KEY_F21, KEY_DELETE, KEY_I, KEY_EQUAL, KEY_MINUS,
    KEY_RIGHT, KEY_LEFT, KEY_UP, KEY_DOWN, KEY_D, KEY_A, KEY_W, KEY_S, KEY_F22,
};

#define NIKON_INPUT_DIAL_QUEUE 8
#define NIKON_INPUT_PHASE_NS 50000000

static const unsigned mode_counts[] = {
    115, 473, 975, 1506, 2057, 2604, 3136, 3747,
};

typedef struct NikonInputDial {
    NikonInputState *owner;
    QEMUTimer *timer;
    int8_t queue[NIKON_INPUT_DIAL_QUEUE];
    unsigned index, head, count, phase;
} NikonInputDial;

struct NikonInputState {
    DeviceState parent_obj;
    QemuInputHandlerState *handler;
    qemu_irq pads[NIKON_INPUT_PADS];
    qemu_irq expander_pads[NIKON_INPUT_EXPANDER_PADS];
    qemu_irq capture_selector;
    qemu_irq mode_adc;
    NikonInputDial dial[2];
    unsigned mode_position;
    bool down[NIKON_INPUT_PADS];
    bool expander_down[NIKON_INPUT_EXPANDER_PADS];
    bool control_down[6];
    bool selector_down;
    bool movie;
};

static unsigned nikon_input_dial_pad(unsigned index)
{
    return index ? 5 : 7;
}

static void nikon_input_dial_publish(NikonInputDial *d, unsigned state)
{
    unsigned pad = nikon_input_dial_pad(d->index);

    qemu_set_irq(d->owner->pads[pad], (state >> 1) & 1);
    qemu_set_irq(d->owner->pads[pad + 1], state & 1);
}

static void nikon_input_dial_cancel(NikonInputDial *d)
{
    unsigned pad = nikon_input_dial_pad(d->index);

    timer_del(d->timer);
    d->head = d->count = d->phase = 0;
    nikon_input_dial_publish(d, (!d->owner->down[pad] << 1) |
                                !d->owner->down[pad + 1]);
}

static void nikon_input_dial_tick(void *opaque)
{
    NikonInputDial *d = opaque;
    static const unsigned forward[] = { 3, 2, 0, 1, 3 };
    static const unsigned reverse[] = { 3, 1, 0, 2, 3 };

    d->phase++;
    nikon_input_dial_publish(d, d->queue[d->head] > 0 ?
                               forward[d->phase] : reverse[d->phase]);
    if (d->phase == 4) {
        d->head = (d->head + 1) % NIKON_INPUT_DIAL_QUEUE;
        d->count--;
        d->phase = 0;
    }
    if (d->count) {
        timer_mod(d->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                            NIKON_INPUT_PHASE_NS);
    }
}

static bool nikon_input_dial_step(NikonInputDial *d, int64_t step, Error **errp)
{
    unsigned pad = nikon_input_dial_pad(d->index);

    if (step < -1 || step > 1) {
        error_setg(errp, "dial step must be -1, 0 (cancel), or 1");
        return false;
    }
    if (!qdev_is_realized(DEVICE(d->owner))) {
        error_setg(errp, "dial requires a realized input frontend");
        return false;
    }
    if (!step) {
        nikon_input_dial_cancel(d);
        return true;
    }
    if (d->owner->down[pad] || d->owner->down[pad + 1]) {
        error_setg(errp, "dial contacts are owned by a held raw key");
        return false;
    }
    if (d->count == NIKON_INPUT_DIAL_QUEUE) {
        error_setg(errp, "dial queue is full (8 detents including active)");
        return false;
    }
    d->queue[(d->head + d->count) % NIKON_INPUT_DIAL_QUEUE] = step;
    if (!d->count++) {
        /* Keep every phase visible to the original EX polling loop. */
        timer_mod(d->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                            NIKON_INPUT_PHASE_NS);
    }
    return true;
}

static void nikon_input_get_mode(Object *obj, Visitor *v, const char *name,
                                 void *opaque, Error **errp)
{
    int64_t position = NIKON_INPUT(obj)->mode_position;

    visit_type_int(v, name, &position, errp);
}

static void nikon_input_set_mode(Object *obj, Visitor *v, const char *name,
                                 void *opaque, Error **errp)
{
    NikonInputState *s = NIKON_INPUT(obj);
    int64_t position;

    if (!visit_type_int(v, name, &position, errp)) {
        return;
    }
    if (position < 0 || position >= ARRAY_SIZE(mode_counts)) {
        error_setg(errp, "mode-position must be 0..7");
        return;
    }
    s->mode_position = position;
    if (qdev_is_realized(DEVICE(s))) {
        qemu_set_irq(s->mode_adc, mode_counts[s->mode_position]);
    }
}

static void nikon_input_set_step(Object *obj, Visitor *v, const char *name,
                                 void *opaque, Error **errp)
{
    NikonInputState *s = NIKON_INPUT(obj);
    int64_t step;

    if (visit_type_int(v, name, &step, errp)) {
        nikon_input_dial_step(&s->dial[(uintptr_t)opaque], step, errp);
    }
}

static void nikon_input_get_pad_bool(Object *obj, Visitor *v, const char *name,
                                     void *opaque, Error **errp)
{
    uintptr_t pad = (uintptr_t)opaque;
    bool value = NIKON_INPUT(obj)->down[pad];

    visit_type_bool(v, name, &value, errp);
}

static void nikon_input_set_pad_bool(Object *obj, Visitor *v, const char *name,
                                     void *opaque, Error **errp)
{
    NikonInputState *s = NIKON_INPUT(obj);
    uintptr_t pad = (uintptr_t)opaque;
    bool down;

    if (!visit_type_bool(v, name, &down, errp)) {
        return;
    }
    if (s->down[pad] != down) {
        s->down[pad] = down;
        if (qdev_is_realized(DEVICE(s))) {
            qemu_set_irq(s->pads[pad], !down);
        }
        qemu_log_mask(LOG_GUEST_ERROR,
                      "nikon-input: pad=%u ex-pad=0x%02x %s "
                      "active-low level=%u\n",
                      (unsigned)pad, nikon_input_gpio_ids[pad],
                      down ? "down" : "up", !down);
    }
}

static void nikon_input_event(DeviceState *dev, QemuConsole *src,
                              QemuInputEvent *event)
{
    NikonInputState *s = NIKON_INPUT(dev);
    unsigned int i;

    assert(event->type == INPUT_EVENT_KIND_KEY);
    if (event->key.key >= KEY_F14 && event->key.key <= KEY_F19) {
        unsigned control = event->key.key - KEY_F14;

        if (event->key.down && !s->control_down[control]) {
            if (control < 2) {
                s->mode_position = (s->mode_position +
                                    (control ? 7 : 1)) % 8;
                qemu_set_irq(s->mode_adc, mode_counts[s->mode_position]);
            } else {
                Error *error = NULL;

                nikon_input_dial_step(&s->dial[(control - 2) / 2],
                                     control & 1 ? -1 : 1, &error);
                if (error) {
                    qemu_log_mask(LOG_GUEST_ERROR, "nikon-input: %s\n",
                                  error_get_pretty(error));
                    error_free(error);
                }
            }
        }
        s->control_down[control] = event->key.down;
        return;
    }
    if (event->key.key == KEY_F13) {
        if (event->key.down && !s->selector_down) {
            s->movie = !s->movie;
            qemu_set_irq(s->capture_selector, !s->movie);
            qemu_log_mask(LOG_GUEST_ERROR,
                          "nikon-input: capture-selector=%s ex-io=0 level=%u\n",
                          s->movie ? "movie" : "photo", !s->movie);
        }
        s->selector_down = event->key.down;
        return;
    }
    for (i = 0; i < NIKON_INPUT_EXPANDER_PADS; i++) {
        if (event->key.key == nikon_input_expander_keys[i]) {
            if (s->expander_down[i] != event->key.down) {
                s->expander_down[i] = event->key.down;
                qemu_set_irq(s->expander_pads[i], !s->expander_down[i]);
                qemu_log_mask(LOG_GUEST_ERROR,
                    "nikon-input: expander-pad=%u ex-pad=%u %s active-low"
                    " level=%u\n", i, nikon_input_expander_ids[i],
                    s->expander_down[i] ? "down" : "up", !s->expander_down[i]);
            }
            return;
        }
    }
    for (i = 0; i < NIKON_INPUT_PADS; i++) {
        if (event->key.key == nikon_input_host_keys[i]) {
            if (s->down[i] != event->key.down) {
                s->down[i] = event->key.down;
                if (i >= 5 && i <= 8) {
                    /* Raw contacts take ownership of their whole dial. */
                    nikon_input_dial_cancel(&s->dial[i < 7 ? 1 : 0]);
                } else {
                    qemu_set_irq(s->pads[i], !s->down[i]);
                }
                /* Assistance receipt: every host key that reaches a pad. */
                qemu_log_mask(LOG_GUEST_ERROR,
                              "nikon-input: pad=%u ex-pad=%#04x %s active-low"
                              " level=%u\n", i, nikon_input_gpio_ids[i],
                              s->down[i] ? "down" : "up", !s->down[i]);
            }
            return;
        }
    }
}

static const QemuInputHandler nikon_input_handler = {
    .name = "Nikon EX mechanical input profile",
    .mask = INPUT_EVENT_MASK_KEY,
    .event = nikon_input_event,
};

static void nikon_input_publish(NikonInputState *s)
{
    unsigned int i;

    for (i = 0; i < NIKON_INPUT_PADS; i++) {
        qemu_set_irq(s->pads[i], !s->down[i]);
    }
    for (i = 0; i < NIKON_INPUT_EXPANDER_PADS; i++) {
        qemu_set_irq(s->expander_pads[i], !s->expander_down[i]);
    }
    qemu_set_irq(s->capture_selector, !s->movie);
    qemu_set_irq(s->mode_adc, mode_counts[s->mode_position]);
}

static void nikon_input_reset_enter(Object *obj, ResetType type)
{
    NikonInputState *s = NIKON_INPUT(obj);

    for (unsigned i = 0; i < ARRAY_SIZE(s->dial); i++) {
        timer_del(s->dial[i].timer);
        s->dial[i].head = s->dial[i].count = s->dial[i].phase = 0;
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
    for (i = 0; i < ARRAY_SIZE(s->dial); i++) {
        nikon_input_dial_cancel(&s->dial[i]);
    }
    for (i = 0; i < NIKON_INPUT_PADS; i++) {
        s->down[i] = false;
        qemu_set_irq(s->pads[i], 1);
    }
    for (i = 0; i < NIKON_INPUT_EXPANDER_PADS; i++) {
        s->expander_down[i] = false;
        qemu_set_irq(s->expander_pads[i], -1);
    }
    s->selector_down = s->movie = false;
    qemu_set_irq(s->capture_selector, -1);
    qemu_set_irq(s->mode_adc, -1);
    memset(s->control_down, 0, sizeof(s->control_down));
}

static void nikon_input_init(Object *obj)
{
    NikonInputState *s = NIKON_INPUT(obj);

    s->mode_position = 7;
    for (unsigned i = 0; i < ARRAY_SIZE(s->dial); i++) {
        s->dial[i].owner = s;
        s->dial[i].index = i;
        s->dial[i].timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                        nikon_input_dial_tick, &s->dial[i]);
    }
    qdev_init_gpio_out_named(DEVICE(obj), s->pads, "pad", NIKON_INPUT_PADS);
    qdev_init_gpio_out_named(DEVICE(obj), s->expander_pads, "expander-pad",
                             NIKON_INPUT_EXPANDER_PADS);
    qdev_init_gpio_out_named(DEVICE(obj), &s->capture_selector,
                             "capture-selector", 1);
    qdev_init_gpio_out_named(DEVICE(obj), &s->mode_adc, "mode-adc", 1);
}

static void nikon_input_finalize(Object *obj)
{
    NikonInputState *s = NIKON_INPUT(obj);

    for (unsigned i = 0; i < ARRAY_SIZE(s->dial); i++) {
        timer_free(s->dial[i].timer);
    }
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
    rc->phases.enter = nikon_input_reset_enter;
    rc->phases.exit = nikon_input_reset_exit;
    dc->user_creatable = false;
    object_class_property_add(oc, "mode-position", "int",
                              nikon_input_get_mode, nikon_input_set_mode,
                              NULL, NULL);
    object_class_property_add(oc, "dial0-step", "int", NULL,
                              nikon_input_set_step, NULL, (void *)0);
    object_class_property_add(oc, "dial1-step", "int", NULL,
                              nikon_input_set_step, NULL, (void *)1);
    object_class_property_add(oc, "shutter-half-press", "bool",
                              nikon_input_get_pad_bool,
                              nikon_input_set_pad_bool, NULL,
                              (void *)(uintptr_t)NIKON_INPUT_PAD_HALF_PRESS);
    object_class_property_add(oc, "half-press", "bool",
                              nikon_input_get_pad_bool,
                              nikon_input_set_pad_bool, NULL,
                              (void *)(uintptr_t)NIKON_INPUT_PAD_HALF_PRESS);
    object_class_property_add(oc, "shutter-full-press", "bool",
                              nikon_input_get_pad_bool,
                              nikon_input_set_pad_bool, NULL,
                              (void *)(uintptr_t)NIKON_INPUT_PAD_FULL_PRESS);
    object_class_property_add(oc, "full-press", "bool",
                              nikon_input_get_pad_bool,
                              nikon_input_set_pad_bool, NULL,
                              (void *)(uintptr_t)NIKON_INPUT_PAD_FULL_PRESS);
    object_class_property_add(oc, "af-on", "bool",
                              nikon_input_get_pad_bool,
                              nikon_input_set_pad_bool, NULL,
                              (void *)(uintptr_t)NIKON_INPUT_PAD_AF_ON);
    object_class_property_add(oc, "main-power", "bool",
                              nikon_input_get_pad_bool,
                              nikon_input_set_pad_bool, NULL,
                              (void *)(uintptr_t)NIKON_INPUT_PAD_MAIN_POWER);
}

static const TypeInfo nikon_input_type = {
    .name = TYPE_NIKON_INPUT,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(NikonInputState),
    .instance_init = nikon_input_init,
    .instance_finalize = nikon_input_finalize,
    .class_init = nikon_input_class_init,
};

static void nikon_input_register_types(void)
{
    type_register_static(&nikon_input_type);
}
type_init(nikon_input_register_types)
