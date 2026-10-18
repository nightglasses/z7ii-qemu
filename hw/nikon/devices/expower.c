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

typedef struct NikonEXMonitorState {
    I2CSlave parent_obj;
    uint8_t command;
    uint8_t count;
    uint8_t rx_index;
    uint8_t write_value;
    bool valid;
    bool calibrated;
    bool post_calibration_read;
    uint8_t ready_polls;
} NikonEXMonitorState;

static NikonEXMonitorState *exmonitor_state(I2CSlave *slave)
{
    return container_of(slave, NikonEXMonitorState, parent_obj);
}

static int exmonitor_event(I2CSlave *slave, enum i2c_event event)
{
    NikonEXMonitorState *s = exmonitor_state(slave);

    if (event == I2C_START_SEND) {
        s->command = 0;
        s->count = 0;
        s->rx_index = 0;
        s->write_value = 0;
        s->valid = false;
    } else if (event == I2C_START_RECV) {
        s->rx_index = 0;
        return s->valid ? 0 : -1;
    }
    return 0;
}

static int exmonitor_send(I2CSlave *slave, uint8_t data)
{
    NikonEXMonitorState *s = exmonitor_state(slave);

    if (s->count > 1) {
        return -1;
    }
    /* The boot monitor uses the status extensions 0x10 and 0x11 after
     * calibration; they are read-only command selectors. */
    if (s->count == 0 && data > 0x11) {
        return -1;
    }
    if (s->count == 0) {
        s->command = data;
        s->count = 1;
        s->valid = true;
    } else {
        s->write_value = data;
        s->count = 2;
        if (s->command == 0x06 && data == 0x33) {
            s->calibrated = true;
            s->ready_polls = 0;
        }
    }
    return 0;
}

static uint8_t exmonitor_recv(I2CSlave *slave)
{
    NikonEXMonitorState *s = exmonitor_state(slave);

    uint8_t index = s->rx_index++;
    uint8_t value;

    switch (s->command) {
    case 0x00:
        value = (s->calibrated ? 0xa0 : 1) + index;
        s->post_calibration_read = s->calibrated;
        break;
    case 0x01:
        value = (s->calibrated ? 1 : 3) + index;
        break;
    case 0x0b:
        /* The final qualification result accepts the ready value 1. */
        value = index == 0 ? 1 : 0;
        if (index == 0) {
            s->ready_polls++;
        }
        break;
    case 0x03:
        value = 1 + index;
        break;
    default:
        value = 0;
        break;
    }
    if (s->command == 0x03 && s->calibrated &&
        s->post_calibration_read) {
        s->calibrated = false;
        s->post_calibration_read = false;
    }

    return value;
}

static void exmonitor_reset_hold(Object *obj, ResetType type)
{
    NikonEXMonitorState *s = exmonitor_state(I2C_SLAVE(obj));

    s->command = 0;
    s->count = 0;
    s->rx_index = 0;
    s->valid = false;
    s->calibrated = false;
    s->post_calibration_read = false;
    s->ready_polls = 0;
}

static const VMStateDescription exmonitor_vmstate = {
    .name = "nikon-ex-monitor",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_I2C_SLAVE(parent_obj, NikonEXMonitorState),
        VMSTATE_UINT8(command, NikonEXMonitorState),
        VMSTATE_UINT8(count, NikonEXMonitorState),
        VMSTATE_UINT8(rx_index, NikonEXMonitorState),
        VMSTATE_UINT8(write_value, NikonEXMonitorState),
        VMSTATE_BOOL(valid, NikonEXMonitorState),
        VMSTATE_BOOL(calibrated, NikonEXMonitorState),
        VMSTATE_BOOL(post_calibration_read, NikonEXMonitorState),
        VMSTATE_UINT8(ready_polls, NikonEXMonitorState),
        VMSTATE_END_OF_LIST()
    },
};

typedef struct NikonEXStatusState {
    I2CSlave parent_obj;
    uint8_t tx[2];
    uint8_t count;
    uint8_t rx_index;
    bool valid;
} NikonEXStatusState;

static NikonEXStatusState *exstatus_state(I2CSlave *slave)
{
    return container_of(slave, NikonEXStatusState, parent_obj);
}

static int exstatus_event(I2CSlave *slave, enum i2c_event event)
{
    NikonEXStatusState *s = exstatus_state(slave);

    if (event == I2C_START_SEND) {
        s->count = 0;
        s->rx_index = 0;
        s->valid = false;
    } else if (event == I2C_START_RECV) {
        return s->valid ? 0 : -1;
    }
    return 0;
}

static int exstatus_send(I2CSlave *slave, uint8_t data)
{
    NikonEXStatusState *s = exstatus_state(slave);

    if (s->count >= ARRAY_SIZE(s->tx)) {
        return -1;
    }
    s->tx[s->count++] = data;
    s->valid = s->count == 2 &&
               ((s->tx[0] == 0x08 && s->tx[1] == 0x01) ||
                (s->tx[0] == 0xa9 && s->tx[1] == 0x00));
    return 0;
}

static uint8_t exstatus_recv(I2CSlave *slave)
{
    NikonEXStatusState *s = exstatus_state(slave);

    /* The transfer is four bytes long.  The EX service copies the fourth
     * received byte into its status word before applying the class parser;
     * the leading bytes are reserved/status padding in this profile. */
    uint8_t index = s->rx_index++;
    uint8_t value = index == 3 ? 0xa9 : 0;
    return value;
}

static void exstatus_reset_hold(Object *obj, ResetType type)
{
    NikonEXStatusState *s = exstatus_state(I2C_SLAVE(obj));

    memset(s->tx, 0, sizeof(s->tx));
    s->count = 0;
    s->rx_index = 0;
    s->valid = false;
}

static const VMStateDescription exstatus_vmstate = {
    .name = "nikon-ex-status",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_I2C_SLAVE(parent_obj, NikonEXStatusState),
        VMSTATE_UINT8_ARRAY(tx, NikonEXStatusState, 2),
        VMSTATE_UINT8(count, NikonEXStatusState),
        VMSTATE_UINT8(rx_index, NikonEXStatusState),
        VMSTATE_BOOL(valid, NikonEXStatusState),
        VMSTATE_END_OF_LIST()
    },
};

static void exmonitor_class_init(ObjectClass *klass, const void *data)
{
    I2CSlaveClass *ic = I2C_SLAVE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    ic->recv = exmonitor_recv;
    ic->send = exmonitor_send;
    ic->event = exmonitor_event;
    dc->vmsd = &exmonitor_vmstate;
    dc->desc = "EX functional power monitor protocol";
    dc->user_creatable = false;
    rc->phases.hold = exmonitor_reset_hold;
}

static void exstatus_class_init(ObjectClass *klass, const void *data)
{
    I2CSlaveClass *ic = I2C_SLAVE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    ic->recv = exstatus_recv;
    ic->send = exstatus_send;
    ic->event = exstatus_event;
    dc->vmsd = &exstatus_vmstate;
    dc->desc = "EX functional power status protocol";
    dc->user_creatable = false;
    rc->phases.hold = exstatus_reset_hold;
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
    }, {
        .name = TYPE_NIKON_EX_MONITOR,
        .parent = TYPE_I2C_SLAVE,
        .instance_size = sizeof(NikonEXMonitorState),
        .class_init = exmonitor_class_init,
    }, {
        .name = TYPE_NIKON_EX_STATUS,
        .parent = TYPE_I2C_SLAVE,
        .instance_size = sizeof(NikonEXStatusState),
        .class_init = exstatus_class_init,
    },
};
DEFINE_TYPES(expower_types)
