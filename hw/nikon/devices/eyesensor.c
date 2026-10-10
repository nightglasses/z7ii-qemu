/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Source-bound I2C peripheral for the MAS logical eye-distance input.
 *
 * The recovered driver selects logical channel 3, mapped to MAS I2C2, and
 * uses seven-bit address 0x38.  The register layout is the public RPR-0521RS
 * software contract; measurement values remain cold zero until an explicit
 * provider is added.  This device does not synthesize an interrupt, exposure,
 * frame, or readiness event.
 */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/i2c/i2c.h"
#include "hw/nikon/devices.h"
#include "migration/vmstate.h"

OBJECT_DECLARE_SIMPLE_TYPE(NikonEyeSensorState, NIKON_EYE_SENSOR)

#define EYE_REG_SYSTEM_CTRL 0x40
#define EYE_REG_MODE_CTRL 0x41
#define EYE_REG_ALS_CTRL 0x42
#define EYE_REG_PXS_CTRL 0x43
#define EYE_REG_PXS_DATA 0x44
#define EYE_REG_ALS_DATA0 0x46
#define EYE_REG_ALS_DATA1 0x48
#define EYE_REG_INTERRUPT 0x4a
#define EYE_REG_PS_OFFSET 0x53
#define EYE_REG_ID 0x92

struct NikonEyeSensorState {
    I2CSlave parent_obj;
    uint8_t regs[0x100];
    uint8_t pointer;
    bool command;
};

static bool eye_reg_valid(uint8_t reg)
{
    switch (reg) {
    case EYE_REG_SYSTEM_CTRL:
    case EYE_REG_MODE_CTRL:
    case EYE_REG_ALS_CTRL:
    case EYE_REG_PXS_CTRL:
    case EYE_REG_PXS_DATA:
    case EYE_REG_PXS_DATA + 1:
    case EYE_REG_ALS_DATA0:
    case EYE_REG_ALS_DATA0 + 1:
    case EYE_REG_ALS_DATA1:
    case EYE_REG_ALS_DATA1 + 1:
    case EYE_REG_INTERRUPT:
    case EYE_REG_PS_OFFSET:
    case EYE_REG_PS_OFFSET + 1:
    case EYE_REG_ID:
        return true;
    default:
        return false;
    }
}

static bool eye_reg_writable(uint8_t reg)
{
    switch (reg) {
    case EYE_REG_SYSTEM_CTRL:
    case EYE_REG_MODE_CTRL:
    case EYE_REG_ALS_CTRL:
    case EYE_REG_PXS_CTRL:
    case EYE_REG_INTERRUPT:
    case EYE_REG_PS_OFFSET:
    case EYE_REG_PS_OFFSET + 1:
        return true;
    default:
        return false;
    }
}

static int eye_event(I2CSlave *slave, enum i2c_event event)
{
    NikonEyeSensorState *s = NIKON_EYE_SENSOR(slave);

    if (event == I2C_START_SEND) {
        s->command = true;
    } else if (event == I2C_START_RECV && !eye_reg_valid(s->pointer)) {
        /* A read from an unqualified register is a real device NACK. */
        return -1;
    }
    return 0;
}

static int eye_send(I2CSlave *slave, uint8_t value)
{
    NikonEyeSensorState *s = NIKON_EYE_SENSOR(slave);

    if (s->command) {
        if (!eye_reg_valid(value)) {
            return -1;
        }
        s->pointer = value;
        s->command = false;
        return 0;
    }
    if (!eye_reg_valid(s->pointer) || !eye_reg_writable(s->pointer)) {
        return -1;
    }
    s->regs[s->pointer++] = value;
    return 0;
}

static uint8_t eye_recv(I2CSlave *slave)
{
    NikonEyeSensorState *s = NIKON_EYE_SENSOR(slave);
    uint8_t value = s->regs[s->pointer];

    s->pointer++;
    return value;
}

static void eye_reset(Object *obj, ResetType type)
{
    NikonEyeSensorState *s = NIKON_EYE_SENSOR(obj);

    memset(s->regs, 0, sizeof(s->regs));
    s->regs[EYE_REG_ID] = 0xe0;
    s->pointer = 0;
    s->command = true;
}

static const VMStateDescription eye_vmstate = {
    .name = TYPE_NIKON_EYE_SENSOR,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_I2C_SLAVE(parent_obj, NikonEyeSensorState),
        VMSTATE_UINT8_ARRAY(regs, NikonEyeSensorState, 0x100),
        VMSTATE_UINT8(pointer, NikonEyeSensorState),
        VMSTATE_BOOL(command, NikonEyeSensorState),
        VMSTATE_END_OF_LIST()
    },
};

static void eye_class_init(ObjectClass *klass, const void *data)
{
    I2CSlaveClass *ic = I2C_SLAVE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    ic->event = eye_event;
    ic->send = eye_send;
    ic->recv = eye_recv;
    dc->desc = "RPR-0521RS-compatible MAS eye sensor input";
    dc->vmsd = &eye_vmstate;
    dc->user_creatable = true;
    rc->phases.hold = eye_reset;
}

static const TypeInfo eye_type = {
    .name = TYPE_NIKON_EYE_SENSOR,
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(NikonEyeSensorState),
    .class_init = eye_class_init,
};

static void eye_register(void)
{
    type_register_static(&eye_type);
}

type_init(eye_register)
