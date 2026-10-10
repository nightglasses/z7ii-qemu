/*
 * Four finite Cadence-compatible I2C interfaces from original MAS geometry.
 * Idle reset, FIFO depth and nominal clock are virtual board assumptions.
 * Unbound monitor/bit7/extended-address operations are deliberately rejected.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/fifo8.h"
#include "qemu/timer.h"
#include "migration/vmstate.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "hw/i2c/i2c.h"
#include "hw/nikon/devices.h"
#include "hw/nikon/peer.h"

#define I2C_IRQ_MASK 0x2ff
#define I2C_HOLD 0x10
#define I2C_MASTER 2
OBJECT_DECLARE_SIMPLE_TYPE(NikonI2CState, NIKON_I2C)

struct NikonI2CState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    I2CBus *bus;
    Fifo8 tx, rx;
    Clock *clock;
    QEMUTimer *timer;
    qemu_irq irq;
    uint32_t instance, control, address, pending, mask;
    uint32_t remaining, pause, timeout;
    const NikonI2CAsyncOps *async_ops;
    void *async_opaque;
    uint64_t token;
    NikonI2CPhase phase;
    uint8_t phase_value;
    bool async_bound, request_pending, submitted;
    bool nack_pending, stop_pending, completion_pending;
    bool active, addressed, receiving, waiting, transfer_done, reported_error;
};

static void nikon_i2c_irq(NikonI2CState *s)
{
    qemu_set_irq(s->irq, s->pending & ~s->mask);
}

static int64_t nikon_i2c_period(NikonI2CState *s, unsigned cycles)
{
    unsigned divisor = 22 * (((s->control >> 14) & 3) + 1) *
                       (((s->control >> 8) & 63) + 1);

    return MAX(1, clock_ticks_to_ns(s->clock, (uint64_t)divisor * cycles));
}

static void nikon_i2c_cancel_request(NikonI2CState *s)
{
    uint64_t token = s->token;
    bool submitted = s->submitted;

    s->request_pending = s->submitted = false;
    s->token++;
    if (submitted) {
        s->async_ops->cancel(s->async_opaque, token);
    }
}

static void nikon_i2c_stop(NikonI2CState *s)
{
    timer_del(s->timer);
    nikon_i2c_cancel_request(s);
    if (s->bus && !s->async_bound && i2c_bus_busy(s->bus)) {
        i2c_end_transfer(s->bus);
    }
    s->active = s->addressed = s->waiting = false;
    s->nack_pending = s->stop_pending = s->completion_pending = false;
}

static void nikon_i2c_schedule(NikonI2CState *s)
{
    unsigned cycles = 9;

    if (!s->active || s->request_pending ||
        (s->transfer_done && !s->stop_pending) || timer_pending(s->timer)) {
        return;
    }
    s->waiting = !s->stop_pending && !s->nack_pending &&
                 s->addressed && !s->receiving &&
                 fifo8_is_empty(&s->tx) && (s->control & I2C_HOLD);
    if (s->waiting && !s->timeout) {
        return; /* No data, no configured timeout and no invented completion. */
    }
    if (s->async_bound) {
        if (s->stop_pending || s->nack_pending) {
            cycles = 1;
        } else if (s->addressed && s->receiving && s->remaining == 1) {
            cycles = 8; /* Final ninth clock is the explicit master NACK. */
        }
    }
    timer_mod(s->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
              nikon_i2c_period(s, s->waiting ? s->timeout + 1 : cycles));
}

static void nikon_i2c_end_async(NikonI2CState *s, bool completion)
{
    timer_del(s->timer);
    s->waiting = false;
    s->stop_pending = true;
    s->completion_pending = completion;
    nikon_i2c_schedule(s);
}

static void nikon_i2c_complete(NikonI2CState *s)
{
    if (s->async_bound && !(s->control & I2C_HOLD)) {
        nikon_i2c_end_async(s, true);
        return;
    }
    s->transfer_done = true;
    s->pending |= 1;
    if (!(s->control & I2C_HOLD)) {
        nikon_i2c_stop(s);
    }
    nikon_i2c_irq(s);
}

static void nikon_i2c_submit(NikonI2CState *s)
{
    if (!s->async_ops || !s->request_pending || s->submitted) {
        return;
    }
    s->submitted = true;
    if (!s->async_ops->request(s->async_opaque, s->token, s->phase,
                               s->phase_value) && s->request_pending) {
        s->submitted = false;
    }
}

static void nikon_i2c_request(NikonI2CState *s, NikonI2CPhase phase, uint8_t value)
{
    s->phase = phase;
    s->phase_value = value;
    s->token++;
    s->request_pending = true;
    nikon_i2c_submit(s);
}

static void nikon_i2c_progress(NikonI2CState *s)
{
    if (s->receiving) {
        if (!s->remaining) {
            nikon_i2c_complete(s);
            return;
        }
        if (fifo8_is_full(&s->rx)) {
            s->pending |= 2;
            nikon_i2c_irq(s);
            return;
        }
    } else if (fifo8_is_empty(&s->tx)) {
        s->pending |= 2;
        if (!(s->control & I2C_HOLD)) {
            nikon_i2c_complete(s);
            return;
        }
    }
    nikon_i2c_irq(s);
    nikon_i2c_schedule(s);
}

static void nikon_i2c_async_step(NikonI2CState *s)
{
    if (s->stop_pending) {
        nikon_i2c_request(s, NIKON_I2C_STOP, 0);
    } else if (s->nack_pending) {
        nikon_i2c_request(s, NIKON_I2C_NACK, 0);
    } else if (s->waiting) {
        s->pending |= 8;
        nikon_i2c_irq(s);
        nikon_i2c_end_async(s, false);
    } else if (!s->addressed) {
        nikon_i2c_request(s, NIKON_I2C_START,
                          s->address | (s->receiving ? 0x80 : 0));
    } else if (s->receiving && s->remaining && !fifo8_is_full(&s->rx)) {
        nikon_i2c_request(s, NIKON_I2C_READ, 0);
    } else if (!s->receiving && !fifo8_is_empty(&s->tx)) {
        nikon_i2c_request(s, NIKON_I2C_WRITE, fifo8_peek(&s->tx));
    } else {
        nikon_i2c_progress(s);
    }
}

void nikon_i2c_async_ready(DeviceState *dev)
{
    nikon_i2c_submit(NIKON_I2C(dev));
}

I2CBus *nikon_i2c_bus(DeviceState *dev)
{
    NikonI2CState *s = NIKON_I2C(dev);

    return s->bus;
}

void nikon_i2c_async_complete(DeviceState *dev, uint64_t token, uint32_t result)
{
    NikonI2CState *s = NIKON_I2C(dev);

    if (!s->request_pending || !s->submitted || s->token != token ||
        (result > 0xff && result != NIKON_I2C_NAK) ||
        (s->phase != NIKON_I2C_READ && result && result != NIKON_I2C_NAK)) {
        return;
    }
    s->request_pending = s->submitted = false;
    if (s->phase == NIKON_I2C_WRITE) {
        fifo8_pop(&s->tx); /* Even a NAK clocks this actual transmitted byte. */
    }
    if (result == NIKON_I2C_NAK) {
        s->pending |= 4;
        nikon_i2c_irq(s);
        if (s->phase == NIKON_I2C_STOP) {
            nikon_i2c_stop(s);
        } else {
            s->nack_pending = false;
            nikon_i2c_end_async(s, false);
        }
        return;
    }
    switch (s->phase) {
    case NIKON_I2C_START:
        s->addressed = true;
        nikon_i2c_progress(s);
        break;
    case NIKON_I2C_WRITE:
        nikon_i2c_progress(s);
        break;
    case NIKON_I2C_READ:
        fifo8_push(&s->rx, result);
        if (s->remaining) {
            s->remaining--;
        }
        if (!s->remaining) {
            s->pending |= 2;
            s->nack_pending = true;
            nikon_i2c_irq(s);
            nikon_i2c_schedule(s);
        } else {
            nikon_i2c_progress(s);
        }
        break;
    case NIKON_I2C_NACK:
        s->nack_pending = false;
        nikon_i2c_complete(s);
        break;
    case NIKON_I2C_STOP: {
        bool completion = s->completion_pending;

        nikon_i2c_stop(s);
        if (completion) {
            s->transfer_done = true;
            s->pending |= 1;
            nikon_i2c_irq(s);
        }
        break;
    }
    }
}

bool nikon_i2c_attach_async(DeviceState *dev, const NikonI2CAsyncOps *ops,
                            void *opaque, Error **errp)
{
    NikonI2CState *s = NIKON_I2C(dev);

    assert(ops && ops->request && ops->cancel);
    if (!s->bus) {
        error_setg(errp, "I2C controller must be realized before attaching a peer");
        return false;
    }
    if (s->active) {
        error_setg(errp, "I2C controller has an active transaction");
        return false;
    }
    if (s->async_ops) {
        error_setg(errp, "I2C controller already has an asynchronous peer");
        return false;
    }
    if (BUS(s->bus)->num_children) {
        error_setg(errp, "I2C controller already has local slave devices");
        return false;
    }
    s->async_bound = true;
    s->async_ops = ops;
    s->async_opaque = opaque;
    BUS(s->bus)->full = true; /* Local slave devices and remote bus are exclusive. */
    return true;
}

void nikon_i2c_detach_async(DeviceState *dev, void *owner)
{
    NikonI2CState *s = NIKON_I2C(dev);

    if (!s->async_ops || s->async_opaque != owner) {
        return;
    }
    timer_del(s->timer);
    nikon_i2c_cancel_request(s);
    s->async_ops = NULL;
    s->async_opaque = NULL;
    /* Physical bus state remains unfinished until the guest cancels it. */
}

static void nikon_i2c_step(void *opaque)
{
    NikonI2CState *s = opaque;

    if (!s->active) {
        return;
    }
    if (s->async_bound) {
        nikon_i2c_async_step(s);
        return;
    }
    if (s->waiting) {
        s->pending |= 8;
        nikon_i2c_stop(s);
        nikon_i2c_irq(s);
        return;
    }
    if (!s->addressed) {
        if (i2c_start_transfer(s->bus, s->address, s->receiving)) {
            s->pending |= 4; /* Official bus address NACK, not empty data. */
            nikon_i2c_stop(s);
            nikon_i2c_irq(s);
            return;
        }
        s->addressed = true;
    }
    if (s->receiving) {
        if (!s->remaining) {
            nikon_i2c_complete(s);
            return;
        }
        if (fifo8_is_full(&s->rx)) {
            s->pending |= 2;
            nikon_i2c_irq(s);
            return; /* Guest DATA pop/rearm resumes the finite receiver. */
        }
        fifo8_push(&s->rx, i2c_recv(s->bus));
        s->remaining--;
        if (!s->remaining) {
            i2c_nack(s->bus);
            s->pending |= 2;
            nikon_i2c_complete(s);
            return;
        }
        if (fifo8_is_full(&s->rx)) {
            s->pending |= 2;
        }
    } else {
        if (!fifo8_is_empty(&s->tx)) {
            if (i2c_send(s->bus, fifo8_pop(&s->tx))) {
                s->pending |= 4;
                nikon_i2c_stop(s);
                nikon_i2c_irq(s);
                return;
            }
        }
        if (fifo8_is_empty(&s->tx)) {
            s->pending |= 2;
            if (!(s->control & I2C_HOLD)) {
                nikon_i2c_complete(s);
                return;
            }
        }
    }
    nikon_i2c_irq(s);
    nikon_i2c_schedule(s);
}

static MemTxResult nikon_i2c_error(NikonI2CState *s, hwaddr offset,
                                 unsigned size, bool write, uint64_t value)
{
    nikon_access_error(&s->reported_error,
                     s->regs.addr + offset,
                     size, write, value, "i2c-access-or-variant-not-supported");
    return MEMTX_ERROR;
}

static MemTxResult nikon_i2c_read(void *opaque, hwaddr offset, uint64_t *value,
                                unsigned size, MemTxAttrs attrs)
{
    NikonI2CState *s = opaque;

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
            nikon_i2c_irq(s);
        } else {
            *value = fifo8_pop(&s->rx);
            nikon_i2c_schedule(s);
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
    case 0x24:
    case 0x28:
        /* Write strobes have no stored readback or interrupt side effect.
         * Read-as-zero is the functional profile for the source's IDR RMW. */
        break;
    default:
        goto error;
    }
    return MEMTX_OK;
error:
    return nikon_i2c_error(s, offset, size, false, 0);
}

static MemTxResult nikon_i2c_write(void *opaque, hwaddr offset, uint64_t value,
                                 unsigned size, MemTxAttrs attrs)
{
    NikonI2CState *s = opaque;

    if (size != 4 || (offset & 3)) {
        goto error;
    }
    switch (offset) {
    case 0:
        if (value & ~0xff7fU ||
            (!(value & 0x40) && s->active &&
             (((value ^ s->control) & ~0x59U) ||
              (((value ^ s->control) & 1) &&
               (!s->addressed || !(s->control & I2C_HOLD) ||
                !fifo8_is_empty(&s->tx) || s->request_pending ||
                s->nack_pending || s->stop_pending))))) {
            goto error;
        }
        if (value & 0x40) {
            nikon_i2c_stop(s);
            fifo8_reset(&s->tx);
            fifo8_reset(&s->rx);
            s->remaining = 0;
        }
        s->control = value & ~0x40U;
        if (s->active && s->addressed && !(s->control & I2C_HOLD) &&
            !s->request_pending && !s->nack_pending && !s->stop_pending &&
            (s->transfer_done || (!s->receiving && fifo8_is_empty(&s->tx)))) {
            if (s->transfer_done) {
                if (s->async_bound) {
                    nikon_i2c_end_async(s, false);
                } else {
                    nikon_i2c_stop(s);
                }
            } else {
                nikon_i2c_complete(s);
            }
        } else {
            nikon_i2c_schedule(s);
        }
        break;
    case 8:
        if (value > 0x7f || ((s->control & I2C_MASTER) &&
            (!(s->control & 4) || (s->control & 0x20))) ||
            (s->active && (!s->addressed || !(s->control & I2C_HOLD) ||
                           value != s->address || s->request_pending ||
                           s->nack_pending || s->stop_pending))) {
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
            nikon_i2c_schedule(s);
        }
        break;
    case 0xc:
        if (value > 0xff || (s->active && s->receiving)) {
            goto error;
        }
        if (fifo8_is_full(&s->tx)) {
            s->pending |= 0x40;
            nikon_i2c_irq(s);
        } else {
            fifo8_push(&s->tx, value);
            s->transfer_done = false;
            if (s->waiting) {
                timer_del(s->timer);
            }
            nikon_i2c_schedule(s);
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
        nikon_i2c_schedule(s);
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
            nikon_i2c_schedule(s);
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
    nikon_i2c_irq(s);
    return MEMTX_OK;
error:
    return nikon_i2c_error(s, offset, size, true, value);
}

static const MemoryRegionOps nikon_i2c_ops = {
    .read_with_attrs = nikon_i2c_read,
    .write_with_attrs = nikon_i2c_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl = { .min_access_size = 1, .max_access_size = 4 },
};

static void nikon_i2c_reset(DeviceState *dev)
{
    NikonI2CState *s = NIKON_I2C(dev);

    nikon_i2c_stop(s);
    fifo8_reset(&s->tx);
    fifo8_reset(&s->rx);
    s->control = s->address = s->pending = s->remaining = 0;
    s->pause = s->timeout = 0;
    s->mask = I2C_IRQ_MASK;
    s->receiving = s->transfer_done = s->reported_error = false;
    clock_update_hz(s->clock, 48000000);
    nikon_i2c_irq(s);
}

static void nikon_i2c_init(Object *obj)
{
    NikonI2CState *s = NIKON_I2C(obj);
    fifo8_create(&s->tx, 16);
    fifo8_create(&s->rx, 16);
    s->clock = qdev_init_clock_out(DEVICE(obj), "nominal-i2c-clock");
    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, nikon_i2c_step, s);
    memory_region_init_io(&s->regs, obj, &nikon_i2c_ops, s, "nikon.i2c", 0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
}

static void nikon_i2c_realize(DeviceState *dev, Error **errp)
{
    NikonI2CState *s = NIKON_I2C(dev);
    g_autofree char *name = g_strdup_printf("nikon-i2c%u", s->instance);

    s->bus = i2c_init_bus(dev, name);
}

static void nikon_i2c_finalize(Object *obj)
{
    NikonI2CState *s = NIKON_I2C(obj);

    nikon_i2c_stop(s);
    timer_free(s->timer);
    fifo8_destroy(&s->tx);
    fifo8_destroy(&s->rx);
}

static const Property nikon_i2c_properties[] = {
    DEFINE_PROP_UINT32("instance", NikonI2CState, instance, 0),
};
static const VMStateDescription nikon_i2c_vmstate = {
    .name = "nikon-i2c", .unmigratable = true,
};
static void nikon_i2c_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = nikon_i2c_realize;
    dc->vmsd = &nikon_i2c_vmstate;
    device_class_set_props(dc, nikon_i2c_properties);
    device_class_set_legacy_reset(dc, nikon_i2c_reset);
    dc->user_creatable = false;
}
static const TypeInfo nikon_i2c_type = {
    .name = TYPE_NIKON_I2C,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonI2CState),
    .instance_init = nikon_i2c_init,
    .instance_finalize = nikon_i2c_finalize,
    .class_init = nikon_i2c_class_init,
};
static void nikon_i2c_register_types(void)
{
    type_register_static(&nikon_i2c_type);
}
type_init(nikon_i2c_register_types)
