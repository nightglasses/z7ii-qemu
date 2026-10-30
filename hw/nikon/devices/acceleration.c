/*
 * Finite variant1 acceleration input; no physical sensor identity is claimed.
 * Register shapes: unchanged MAS 103d48d0/103d51cc/103d57e6.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "hw/core/qdev-properties.h"
#include "hw/nikon/devices.h"
#include "migration/vmstate.h"
#include "system/runstate.h"

OBJECT_DECLARE_SIMPLE_TYPE(NikonAccelerationState, NIKON_ACCELERATION)

struct NikonAccelerationState {
    I2CSlave parent_obj;
    int32_t x, y, z;
    bool present, active;
    bool sending, receiving, failed, selected, reported;
    uint8_t reg, used, expected, read_offset;
    uint8_t write[15];
    uint8_t sample[6];
};

static unsigned acceleration_length(uint8_t reg)
{
    switch (reg) {
    case 0x00:
    case 0x01:
        return 0;
    case 0x0e:
    case 0x17:
        return 2;
    case 0x11:
    case 0x15:
    case 0x2a:
        return 1;
    case 0x1d:
        return 4;
    case 0x23:
        return 15;
    default:
        return UINT_MAX;
    }
}

static void acceleration_abort(NikonAccelerationState *s)
{
    s->sending = s->receiving = s->failed = s->selected = false;
    s->used = s->read_offset = 0;
}

static int acceleration_event(I2CSlave *slave, enum i2c_event event)
{
    NikonAccelerationState *s = NIKON_ACCELERATION(slave);

    switch (event) {
    case I2C_START_SEND:
        acceleration_abort(s);
        s->sending = true;
        break;
    case I2C_START_RECV:
        if (s->failed || !s->selected || s->expected ||
            (s->reg == 1 && (!s->active || !s->present))) {
            acceleration_abort(s);
            return -1;
        }
        s->sending = false;
        s->receiving = true;
        s->selected = false;
        s->read_offset = 0;
        break;
    case I2C_FINISH:
        if (s->sending && !s->failed && s->selected &&
            s->expected && s->used == s->expected && s->reg == 0x2a) {
            s->active = s->write[0] & 1;
        }
        /* A selector-only STOP is retained for a separate receive START. */
        if (s->sending && !s->failed && s->selected && !s->expected) {
            s->sending = false;
        } else {
            acceleration_abort(s);
        }
        break;
    case I2C_NACK:
        s->receiving = false;
        break;
    default:
        acceleration_abort(s);
        return -1;
    }
    return 0;
}

static int acceleration_send(I2CSlave *slave, uint8_t value)
{
    NikonAccelerationState *s = NIKON_ACCELERATION(slave);
    unsigned length;

    if (!s->sending || s->failed) {
        return -1;
    }
    if (!s->selected) {
        length = acceleration_length(value);
        if (length == UINT_MAX || (s->active && length && value != 0x2a)) {
            s->failed = true;
            return -1;
        }
        s->reg = value;
        s->expected = length;
        s->selected = true;
        return 0;
    }
    if (s->used >= s->expected ||
        (s->reg == 0x2a && value != 0 && value != 1 &&
         value != 0x2c && value != 0x2d) ||
        (s->reg == 0x23 && value !=
         (s->used == 7 ? 0x2c : s->used == 8 ? 2 : 0)) ||
        (s->reg != 0x2a && s->reg != 0x23 && value != 0)) {
        /* Only the qualified default programming is modelled. */
        s->failed = true;
        return -1;
    }
    s->write[s->used++] = value;
    return 0;
}

static uint8_t acceleration_recv(I2CSlave *slave)
{
    NikonAccelerationState *s = NIKON_ACCELERATION(slave);

    if (s->receiving && s->reg == 0 && s->read_offset++ == 0) {
        return s->active && s->present ? 8 : 0;
    }
    if (s->receiving && s->reg == 1 && s->read_offset < sizeof(s->sample)) {
        return s->sample[s->read_offset++];
    }
    /* Upstream recv cannot NACK a byte; stop uncovered guest execution. */
    if (!s->reported) {
        error_report("nikon: acceleration-read-length-not-supported "
                     "address=0x%02x register=0x%02x offset=%u",
                     I2C_SLAVE(s)->address, s->reg, s->read_offset);
        s->reported = true;
    }
    /* The clocked I2C callback can run without current_cpu. */
    qemu_system_vmstop_request(RUN_STATE_DEBUG);
    s->receiving = false;
    return 0xff;
}

static void acceleration_reset(Object *obj, ResetType type)
{
    NikonAccelerationState *s = NIKON_ACCELERATION(obj);

    acceleration_abort(s);
    s->active = s->reported = false;
    s->reg = s->expected = 0;
    memset(s->write, 0, sizeof(s->write));
}

static void acceleration_realize(DeviceState *dev, Error **errp)
{
    NikonAccelerationState *s = NIKON_ACCELERATION(dev);
    int32_t axes[] = { s->x, s->y, s->z };

    for (unsigned i = 0; i < ARRAY_SIZE(axes); i++) {
        uint16_t encoded;

        if (axes[i] < -2048 || axes[i] > 2047) {
            error_setg(errp, "acceleration sample must fit signed 12 bits");
            return;
        }
        encoded = (axes[i] & 0xfff) << 4;
        s->sample[2 * i] = encoded >> 8;
        s->sample[2 * i + 1] = encoded;
    }
}

static const Property acceleration_properties[] = {
    DEFINE_PROP_BOOL("sample-present", NikonAccelerationState, present, false),
    DEFINE_PROP_INT32("sample-x", NikonAccelerationState, x, 0),
    DEFINE_PROP_INT32("sample-y", NikonAccelerationState, y, 0),
    DEFINE_PROP_INT32("sample-z", NikonAccelerationState, z, 0),
};

static const VMStateDescription acceleration_vmstate = {
    .name = TYPE_NIKON_ACCELERATION,
    .unmigratable = true,
};

static void acceleration_class_init(ObjectClass *klass, const void *data)
{
    I2CSlaveClass *ic = I2C_SLAVE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    ic->event = acceleration_event;
    ic->send = acceleration_send;
    ic->recv = acceleration_recv;
    dc->realize = acceleration_realize;
    dc->desc = "Finite MAS variant1 acceleration input";
    dc->vmsd = &acceleration_vmstate;
    device_class_set_props(dc, acceleration_properties);
    rc->phases.hold = acceleration_reset;
}

static const TypeInfo acceleration_type = {
    .name = TYPE_NIKON_ACCELERATION,
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(NikonAccelerationState),
    .class_init = acceleration_class_init,
};

static void acceleration_register(void)
{
    type_register_static(&acceleration_type);
}

type_init(acceleration_register)
