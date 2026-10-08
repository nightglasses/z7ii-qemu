/*
 * Four finite Cadence-compatible I2C interfaces from original MAS geometry.
 * Idle reset, FIFO depth and nominal clock are virtual board assumptions.
 * Unbound monitor/bit7/extended-address operations are deliberately rejected.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/fifo8.h"
#include "qemu/timer.h"
#include "migration/vmstate.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "hw/i2c/i2c.h"
#include "z7ii.h"

#define I2C_IRQ_MASK 0x2ff
#define I2C_HOLD 0x10
#define I2C_MASTER 2
OBJECT_DECLARE_SIMPLE_TYPE(Z7III2CState, Z7II_I2C)

struct Z7III2CState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    I2CBus *bus;
    Fifo8 tx, rx;
    Clock *clock;
    QEMUTimer *timer;
    qemu_irq irq;
    uint32_t instance, control, address, pending, mask;
    uint32_t remaining, pause, timeout;
    bool active, addressed, receiving, waiting, transfer_done, reported_error;
};

static void z7ii_i2c_irq(Z7III2CState *s)
{
    qemu_set_irq(s->irq, s->pending & ~s->mask);
}

static int64_t z7ii_i2c_period(Z7III2CState *s, unsigned cycles)
{
    unsigned divisor = 22 * (((s->control >> 14) & 3) + 1) *
                       (((s->control >> 8) & 63) + 1);

    return MAX(1, clock_ticks_to_ns(s->clock, (uint64_t)divisor * cycles));
}

static void z7ii_i2c_stop(Z7III2CState *s)
{
    timer_del(s->timer);
    if (i2c_bus_busy(s->bus)) {
        i2c_end_transfer(s->bus);
    }
    s->active = s->addressed = s->waiting = false;
}

static void z7ii_i2c_schedule(Z7III2CState *s)
{
    if (!s->active || s->transfer_done || timer_pending(s->timer)) {
        return;
    }
    s->waiting = s->addressed && !s->receiving &&
                 fifo8_is_empty(&s->tx) && (s->control & I2C_HOLD);
    if (s->waiting && !s->timeout) {
        return; /* No data, no configured timeout and no invented completion. */
    }
    timer_mod(s->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
              z7ii_i2c_period(s, s->waiting ? s->timeout + 1 : 9));
}

static void z7ii_i2c_complete(Z7III2CState *s)
{
    s->transfer_done = true;
    s->pending |= 1;
    if (!(s->control & I2C_HOLD)) {
        z7ii_i2c_stop(s);
    }
    z7ii_i2c_irq(s);
}

static void z7ii_i2c_step(void *opaque)
{
    Z7III2CState *s = opaque;

    if (!s->active) {
        return;
    }
    if (s->waiting) {
        s->pending |= 8;
        z7ii_i2c_stop(s);
        z7ii_i2c_irq(s);
        return;
    }
    if (!s->addressed) {
        if (i2c_start_transfer(s->bus, s->address, s->receiving)) {
            s->pending |= 4; /* Official bus address NACK, not empty data. */
            z7ii_i2c_stop(s);
            z7ii_i2c_irq(s);
            return;
        }
        s->addressed = true;
    }
    if (s->receiving) {
        if (!s->remaining) {
            z7ii_i2c_complete(s);
            return;
        }
        if (fifo8_is_full(&s->rx)) {
            s->pending |= 2;
            z7ii_i2c_irq(s);
            return; /* Guest DATA pop/rearm resumes the finite receiver. */
        }
        fifo8_push(&s->rx, i2c_recv(s->bus));
        s->remaining--;
        if (!s->remaining) {
            i2c_nack(s->bus);
            s->pending |= 2;
            z7ii_i2c_complete(s);
            return;
        }
        if (fifo8_is_full(&s->rx)) {
            s->pending |= 2;
        }
    } else {
        if (!fifo8_is_empty(&s->tx)) {
            if (i2c_send(s->bus, fifo8_pop(&s->tx))) {
                s->pending |= 4;
                z7ii_i2c_stop(s);
                z7ii_i2c_irq(s);
                return;
            }
        }
        if (fifo8_is_empty(&s->tx)) {
            s->pending |= 2;
            if (!(s->control & I2C_HOLD)) {
                z7ii_i2c_complete(s);
                return;
            }
        }
    }
    z7ii_i2c_irq(s);
    z7ii_i2c_schedule(s);
}

static MemTxResult z7ii_i2c_error(Z7III2CState *s, hwaddr offset,
                                 unsigned size, bool write, uint64_t value)
{
    z7ii_access_error(&s->reported_error,
                     0x2c018000ULL + s->instance * 0x1000 + offset,
                     size, write, value, "i2c-access-or-variant-not-supported");
    return MEMTX_ERROR;
}

static MemTxResult z7ii_i2c_read(void *opaque, hwaddr offset, uint64_t *value,
                                unsigned size, MemTxAttrs attrs)
{
    Z7III2CState *s = opaque;

    *value = 0;
    if (size != 4 || (offset & 3)) {
        goto error;
    }
    switch (offset) {
    case 0:
        *value = s->control;
        break;
    case 4:
        *value = (s->active ? 0x100 : 0) |
                 (!fifo8_is_empty(&s->tx) ? 0x40 : 0) |
                 (!fifo8_is_empty(&s->rx) ? 0x20 : 0) |
                 (s->receiving ? 8 : 0);
        break;
    case 8:
        *value = s->address;
        break;
    case 0xc:
        if (fifo8_is_empty(&s->rx)) {
            s->pending |= 0x80;
            z7ii_i2c_irq(s);
        } else {
            *value = fifo8_pop(&s->rx);
            z7ii_i2c_schedule(s);
        }
        break;
    case 0x10:
        *value = s->pending;
        break;
    case 0x14:
        if (s->control & I2C_MASTER) {
            unsigned used = fifo8_num_used(&s->tx);

            *value = (s->control & 1) ? s->remaining : used ? used - 1 : 0;
        } else {
            *value = fifo8_num_used((s->control & 1) ? &s->tx : &s->rx);
        }
        break;
    case 0x18:
        *value = s->pause;
        break;
    case 0x1c:
        *value = s->timeout;
        break;
    case 0x20:
        *value = s->mask;
        break;
    default:
        goto error;
    }
    return MEMTX_OK;
error:
    return z7ii_i2c_error(s, offset, size, false, 0);
}

static MemTxResult z7ii_i2c_write(void *opaque, hwaddr offset, uint64_t value,
                                 unsigned size, MemTxAttrs attrs)
{
    Z7III2CState *s = opaque;

    if (size != 4 || (offset & 3)) {
        goto error;
    }
    switch (offset) {
    case 0:
        if (value & ~0xff7fU ||
            (s->active && ((value ^ s->control) & ~0x59U)) ||
            (s->active && ((value ^ s->control) & 1) &&
             (!s->addressed || !(s->control & I2C_HOLD) ||
              !fifo8_is_empty(&s->tx)))) {
            goto error;
        }
        if (value & 0x40) {
            z7ii_i2c_stop(s);
            fifo8_reset(&s->tx);
            fifo8_reset(&s->rx);
            s->remaining = 0;
        }
        s->control = value & ~0x40U;
        if (s->active && s->addressed && !(s->control & I2C_HOLD) &&
            (s->transfer_done || (!s->receiving && fifo8_is_empty(&s->tx)))) {
            if (s->transfer_done) {
                z7ii_i2c_stop(s);
            } else {
                z7ii_i2c_complete(s);
            }
        } else {
            z7ii_i2c_schedule(s);
        }
        break;
    case 8:
        if (value > 0x7f || ((s->control & I2C_MASTER) &&
            (!(s->control & 4) || (s->control & 0x20))) ||
            (s->active && (!s->addressed || !(s->control & I2C_HOLD) ||
                           value != s->address))) {
            goto error;
        }
        s->address = value;
        if (s->control & I2C_MASTER) {
            timer_del(s->timer);
            /* Upstream repeated START retains the same selected endpoint. */
            s->addressed = false;
            s->transfer_done = false;
            s->active = true;
            s->receiving = s->control & 1;
            z7ii_i2c_schedule(s);
        }
        break;
    case 0xc:
        if (value > 0xff || (s->active && s->receiving)) {
            goto error;
        }
        if (fifo8_is_full(&s->tx)) {
            s->pending |= 0x40;
            z7ii_i2c_irq(s);
        } else {
            fifo8_push(&s->tx, value);
            s->transfer_done = false;
            if (s->waiting) {
                timer_del(s->timer);
            }
            z7ii_i2c_schedule(s);
        }
        break;
    case 0x10:
        if (value & ~I2C_IRQ_MASK) {
            goto error;
        }
        s->pending &= ~value;
        break;
    case 0x14:
        if (value > 0xff || !(s->control & 1)) {
            goto error;
        }
        s->remaining = value;
        if (value) {
            s->transfer_done = false;
        }
        z7ii_i2c_schedule(s);
        break;
    case 0x18:
        if (value > 0xf) {
            goto error;
        }
        s->pause = value;
        break;
    case 0x1c:
        if (value > 0xff) {
            goto error;
        }
        s->timeout = value;
        if (s->active && s->waiting) {
            timer_del(s->timer);
            z7ii_i2c_schedule(s);
        }
        break;
    case 0x24:
        if (value & ~I2C_IRQ_MASK) {
            goto error;
        }
        s->mask &= ~value;
        break;
    case 0x28:
        if (value & ~I2C_IRQ_MASK) {
            goto error;
        }
        s->mask |= value;
        break;
    default:
        goto error;
    }
    z7ii_i2c_irq(s);
    return MEMTX_OK;
error:
    return z7ii_i2c_error(s, offset, size, true, value);
}

static const MemoryRegionOps z7ii_i2c_ops = {
    .read_with_attrs = z7ii_i2c_read,
    .write_with_attrs = z7ii_i2c_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl = { .min_access_size = 1, .max_access_size = 4 },
};

static void z7ii_i2c_reset(DeviceState *dev)
{
    Z7III2CState *s = Z7II_I2C(dev);

    z7ii_i2c_stop(s);
    fifo8_reset(&s->tx);
    fifo8_reset(&s->rx);
    s->control = s->address = s->pending = s->remaining = 0;
    s->pause = s->timeout = 0;
    s->mask = I2C_IRQ_MASK;
    s->receiving = s->transfer_done = s->reported_error = false;
    clock_update_hz(s->clock, 48000000);
    z7ii_i2c_irq(s);
}

static void z7ii_i2c_init(Object *obj)
{
    Z7III2CState *s = Z7II_I2C(obj);
    fifo8_create(&s->tx, 16);
    fifo8_create(&s->rx, 16);
    s->clock = qdev_init_clock_out(DEVICE(obj), "nominal-i2c-clock");
    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, z7ii_i2c_step, s);
    memory_region_init_io(&s->regs, obj, &z7ii_i2c_ops, s, "z7ii.i2c", 0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
}

static void z7ii_i2c_realize(DeviceState *dev, Error **errp)
{
    Z7III2CState *s = Z7II_I2C(dev);
    g_autofree char *name = g_strdup_printf("z7ii-i2c%u", s->instance);

    s->bus = i2c_init_bus(dev, name);
}

static void z7ii_i2c_finalize(Object *obj)
{
    Z7III2CState *s = Z7II_I2C(obj);

    timer_free(s->timer);
    fifo8_destroy(&s->tx);
    fifo8_destroy(&s->rx);
}

static const Property z7ii_i2c_properties[] = {
    DEFINE_PROP_UINT32("instance", Z7III2CState, instance, 0),
};
static const VMStateDescription z7ii_i2c_vmstate = {
    .name = "z7ii-i2c", .unmigratable = true,
};
static void z7ii_i2c_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = z7ii_i2c_realize;
    dc->vmsd = &z7ii_i2c_vmstate;
    device_class_set_props(dc, z7ii_i2c_properties);
    device_class_set_legacy_reset(dc, z7ii_i2c_reset);
    dc->user_creatable = false;
}
static const TypeInfo z7ii_i2c_type = {
    .name = TYPE_Z7II_I2C,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Z7III2CState),
    .instance_init = z7ii_i2c_init,
    .instance_finalize = z7ii_i2c_finalize,
    .class_init = z7ii_i2c_class_init,
};
static void z7ii_i2c_register_types(void)
{
    type_register_static(&z7ii_i2c_type);
}
type_init(z7ii_i2c_register_types)
