/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Source-defined EX power control registers plus the functional 0x6b monitor
 * register file and 0x0d status class register.  The 0x6b/0x0d endpoint data
 * is a declared board profile, not a recovered device identity.
 */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/i2c/i2c.h"
#include "hw/nikon/expower.h"
#include "migration/vmstate.h"
#include "qom/object.h"

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

#define EX_MONITOR_REG_COUNT 0x12
#define EX_MONITOR_STATUS_REG 0x0b
#define EX_MONITOR_READY 1

typedef struct NikonEXMonitorState {
    I2CSlave parent_obj;
    uint8_t command;
    uint8_t count;
    uint8_t rx_index;
    bool valid;
    uint8_t reg[EX_MONITOR_REG_COUNT];
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
    if (s->count == 0) {
        if (data >= EX_MONITOR_REG_COUNT) {
            return -1;
        }
        s->command = data;
        s->count = 1;
        s->valid = true;
    } else {
        /* The firmware writes one byte per setup register and then reads it
         * back.  Store the value so the read returns the same byte. */
        s->reg[s->command] = data;
        s->count = 2;
    }
    return 0;
}

static uint8_t exmonitor_recv(I2CSlave *slave)
{
    NikonEXMonitorState *s = exmonitor_state(slave);
    uint8_t index = s->rx_index++;

    /* One finite byte per register read; controller prefetch reads zero. */
    if (index) {
        return 0;
    }
    if (s->command == EX_MONITOR_STATUS_REG) {
        return EX_MONITOR_READY;
    }
    return s->reg[s->command];
}

static void exmonitor_reset_hold(Object *obj, ResetType type)
{
    NikonEXMonitorState *s = exmonitor_state(I2C_SLAVE(obj));

    s->command = 0;
    s->count = 0;
    s->rx_index = 0;
    s->valid = false;
    memset(s->reg, 0, sizeof(s->reg));
}

static const VMStateDescription exmonitor_vmstate = {
    .name = "nikon-ex-monitor",
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (const VMStateField[]) {
        VMSTATE_I2C_SLAVE(parent_obj, NikonEXMonitorState),
        VMSTATE_UINT8(command, NikonEXMonitorState),
        VMSTATE_UINT8(count, NikonEXMonitorState),
        VMSTATE_UINT8(rx_index, NikonEXMonitorState),
        VMSTATE_BOOL(valid, NikonEXMonitorState),
        VMSTATE_UINT8_ARRAY(reg, NikonEXMonitorState, EX_MONITOR_REG_COUNT),
        VMSTATE_END_OF_LIST()
    },
};

#define EX_STATUS_TX_SIZE 2
#define EX_STATUS_RESPONSE_SIZE 4
#define EX_STATUS_CLASS_DEFAULT 1

/*
 * The EX service at 0x0802b6f0 reads four bytes and classifies the fourth:
 * 0xa9/0xaa -> class 1, 0xc9/0xca -> class 2, 0xe9/0xea -> class 3, else 0.
 * Keep one explicit byte per class instead of a literal in the receive path.
 */
static const uint8_t exstatus_class_byte[] = { 0x00, 0xa9, 0xc9, 0xe9 };

typedef struct NikonEXStatusState {
    I2CSlave parent_obj;
    uint8_t tx[EX_STATUS_TX_SIZE];
    uint8_t count;
    uint8_t rx_index;
    bool valid;
    uint8_t class;
} NikonEXStatusState;

static NikonEXStatusState *exstatus_state(I2CSlave *slave)
{
    return container_of(slave, NikonEXStatusState, parent_obj);
}

static void exstatus_init(Object *obj)
{
    NikonEXStatusState *s = exstatus_state(I2C_SLAVE(obj));

    /* A configured class is a board input; reset preserves it. */
    s->class = EX_STATUS_CLASS_DEFAULT;
    object_property_add_uint8_ptr(obj, "class", &s->class,
                                  OBJ_PROP_FLAG_READWRITE);
    object_property_set_description(obj, "class",
                                    "Functional status class 0..3; 1 is the "
                                    "bundle default");
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
    uint8_t index = s->rx_index++;
    uint8_t class = s->class < ARRAY_SIZE(exstatus_class_byte) ?
                    s->class : 0;

    /* The transfer is four bytes long.  The EX service copies the fourth
     * received byte into its status word before applying the class parser;
     * the leading bytes are reserved/status padding in this profile. */
    return index == EX_STATUS_RESPONSE_SIZE - 1 ?
           exstatus_class_byte[class] : 0;
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
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (const VMStateField[]) {
        VMSTATE_I2C_SLAVE(parent_obj, NikonEXStatusState),
        VMSTATE_UINT8_ARRAY(tx, NikonEXStatusState, EX_STATUS_TX_SIZE),
        VMSTATE_UINT8(count, NikonEXStatusState),
        VMSTATE_UINT8(rx_index, NikonEXStatusState),
        VMSTATE_BOOL(valid, NikonEXStatusState),
        VMSTATE_UINT8(class, NikonEXStatusState),
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
    dc->desc = "EX functional power monitor register file";
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
    dc->desc = "EX functional power status class register";
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
        .instance_init = exstatus_init,
        .class_init = exstatus_class_init,
    },
};
DEFINE_TYPES(expower_types)
