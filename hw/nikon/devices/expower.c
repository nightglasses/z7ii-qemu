/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Original EX qualification writes and verifies two finite control registers.
 * This software functional profile supplies no reset readback or ready event.
 */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/i2c/i2c.h"
#include "hw/nikon/expower.h"
#include "migration/vmstate.h"

typedef struct NikonEXPowerState {
    I2CSlave parent_obj;
    uint16_t reg;
    uint8_t pointer_bytes;
    uint16_t pointer;
    uint8_t count;
    uint8_t value;
    bool selected;
    bool written;
} NikonEXPowerState;

static NikonEXPowerState *expower_state(I2CSlave *slave)
{
    return container_of(slave, NikonEXPowerState, parent_obj);
}

static int expower_event(I2CSlave *slave, enum i2c_event event)
{
    NikonEXPowerState *s = expower_state(slave);

    switch (event) {
    case I2C_START_SEND:
        s->pointer = 0;
        s->count = 0;
        s->selected = false;
        break;
    case I2C_START_RECV:
        /* Undefined reset values and incomplete/reserved commands NACK. */
        return s->selected && s->written ? 0 : -1;
    default:
        break;
    }
    return 0;
}

static int expower_send(I2CSlave *slave, uint8_t data)
{
    NikonEXPowerState *s = expower_state(slave);

    if (s->count < s->pointer_bytes) {
        s->pointer = (s->pointer << 8) | data;
        s->count++;
        if (s->count == s->pointer_bytes) {
            s->selected = s->pointer == s->reg;
            return s->selected ? 0 : -1;
        }
        return 0;
    }
    if (!s->selected || s->count != s->pointer_bytes) {
        return -1;
    }
    s->value = data;
    s->written = true;
    s->count++;
    return 0;
}

static uint8_t expower_recv(I2CSlave *slave)
{
    /*
     * START_RECV accepts only this initialized, selected single-byte register.
     * Its selection remains stable until another command, including extra
     * receive clocks issued by the controller's DR/shift-register pipeline.
     */
    return expower_state(slave)->value;
}

static void expower_reset_hold(Object *obj, ResetType type)
{
    NikonEXPowerState *s = expower_state(I2C_SLAVE(obj));

    s->pointer = 0;
    s->count = 0;
    s->value = 0;
    s->selected = false;
    s->written = false;
}

static int expower_post_load(void *opaque, int version)
{
    NikonEXPowerState *s = opaque;

    if (s->count > s->pointer_bytes + 1 ||
        (s->selected && s->pointer != s->reg)) {
        return -EINVAL;
    }
    return 0;
}

static const VMStateDescription expower_vmstate = {
    .name = "nikon-ex-power-register",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = expower_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_I2C_SLAVE(parent_obj, NikonEXPowerState),
        VMSTATE_UINT16(pointer, NikonEXPowerState),
        VMSTATE_UINT8(count, NikonEXPowerState),
        VMSTATE_UINT8(value, NikonEXPowerState),
        VMSTATE_BOOL(selected, NikonEXPowerState),
        VMSTATE_BOOL(written, NikonEXPowerState),
        VMSTATE_END_OF_LIST()
    },
};

static void expower_init(Object *obj)
{
    NikonEXPowerState *s = expower_state(I2C_SLAVE(obj));

    s->reg = 0x23;
    s->pointer_bytes = 1;
}

static void expower_aux_init(Object *obj)
{
    NikonEXPowerState *s = expower_state(I2C_SLAVE(obj));

    s->reg = 0x1033;
    s->pointer_bytes = 2;
}

static void expower_class_init(ObjectClass *klass, const void *data)
{
    I2CSlaveClass *ic = I2C_SLAVE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    ic->recv = expower_recv;
    ic->send = expower_send;
    ic->event = expower_event;
    dc->vmsd = &expower_vmstate;
    dc->desc = "EX source-defined power control register functional profile";
    dc->user_creatable = false;
    rc->phases.hold = expower_reset_hold;
}

static const TypeInfo expower_types[] = {
    {
        .name = TYPE_NIKON_EX_POWER,
        .parent = TYPE_I2C_SLAVE,
        .instance_size = sizeof(NikonEXPowerState),
        .instance_init = expower_init,
        .class_init = expower_class_init,
    }, {
        .name = TYPE_NIKON_EX_POWER_AUX,
        .parent = TYPE_I2C_SLAVE,
        .instance_size = sizeof(NikonEXPowerState),
        .instance_init = expower_aux_init,
        .class_init = expower_class_init,
    },
};
DEFINE_TYPES(expower_types)
