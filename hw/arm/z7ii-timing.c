/*
 * Original twelve-channel timing interface, explicitly functional semantics.
 * Period/current-count geometry comes from guest tables and setup callers.
 * Opcode0 periodic / opcode31 one-shot, 3GHz, expiry bit0 and W1C are virtual
 * board assumptions. Unknown feature requests fail; this is not a DMA engine.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "migration/vmstate.h"
#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "hw/core/qdev-clock.h"
#include "z7ii.h"

OBJECT_DECLARE_SIMPLE_TYPE(Z7IITimingState, Z7II_TIMING)

typedef struct Z7IITimingChannel {
    Z7IITimingState *owner;
    QEMUTimer *timer;
    qemu_irq irq;
    uint32_t reset, control, opcode, period, status;
    uint64_t count;
    int64_t epoch;
    unsigned index;
    bool configured, running, complete;
} Z7IITimingChannel;

struct Z7IITimingState {
    SysBusDevice parent_obj;
    MemoryRegion regs[2];
    Clock *clock;
    Z7IITimingChannel channel[12];
    uint32_t reset, master, enabled;
    bool reported_error;
};

static uint64_t z7ii_timing_count(Z7IITimingChannel *c)
{
    uint64_t count = c->count;

    if (c->running) {
        count += clock_ns_to_ticks(c->owner->clock,
                    qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) - c->epoch);
    }
    return count;
}

static void z7ii_timing_irq(Z7IITimingChannel *c)
{
    qemu_set_irq(c->irq, !!c->status);
}

static void z7ii_timing_schedule(Z7IITimingChannel *c)
{
    uint64_t period = (uint64_t)c->period + 1;
    uint64_t count = z7ii_timing_count(c);
    uint64_t remaining = count < period ? period - count : 1;

    /* Ceil to a nanosecond: no premature expiry from sub-ns nominal ticks. */
    timer_mod(c->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                        clock_ticks_to_ns(c->owner->clock, remaining) + 1);
}

static void z7ii_timing_update(Z7IITimingChannel *c)
{
    Z7IITimingState *s = c->owner;
    bool enabled = !s->reset && s->master &&
                   (s->enabled & (1U << c->index)) && !c->reset &&
                   (c->control & 1) && c->configured && !c->complete;

    if (c->running && !enabled) {
        c->count = z7ii_timing_count(c);
        c->running = false;
        timer_del(c->timer);
    } else if (!c->running && enabled) {
        c->epoch = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        c->running = true;
        z7ii_timing_schedule(c);
    }
}

static void z7ii_timing_expire(void *opaque)
{
    Z7IITimingChannel *c = opaque;
    uint64_t period = (uint64_t)c->period + 1;
    uint64_t count;

    if (!c->running) {
        return;
    }
    count = z7ii_timing_count(c);
    if (count < period) {
        z7ii_timing_schedule(c);
        return;
    }
    c->status |= 1;
    if (c->opcode == 0x31) {
        c->count = period;
        c->running = false;
        c->complete = true;
    } else {
        c->count = count % period;
        c->epoch = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        z7ii_timing_schedule(c);
    }
    z7ii_timing_irq(c);
}

static void z7ii_timing_channel_reset(Z7IITimingChannel *c)
{
    timer_del(c->timer);
    c->control = c->opcode = c->period = c->status = 0;
    c->count = 0;
    c->configured = c->running = c->complete = false;
    z7ii_timing_irq(c);
}

static MemTxResult z7ii_timing_error(Z7IITimingState *s, unsigned bank,
                                    hwaddr offset, unsigned size, bool write,
                                    uint64_t value)
{
    z7ii_access_error(&s->reported_error, 0x2c010000 + bank * 0x1000 + offset,
                      size, write, value,
                      "timing-access-or-feature-not-supported");
    return MEMTX_ERROR;
}

static MemTxResult z7ii_timing_read_bank(Z7IITimingState *s, unsigned bank,
                                        hwaddr offset, uint64_t *value,
                                        unsigned size)
{
    Z7IITimingChannel *c;
    uint64_t count;

    *value = 0;
    if (size != 4 || (offset & 3) || offset >= 0xd00) {
        goto error;
    }
    if (offset < 0x100) {
        if (!bank && offset == 0) {
            *value = s->reset;
        } else if (!bank && offset == 4) {
            *value = s->master;
        } else if (bank && offset == 0) {
            *value = s->enabled;
        } else {
            goto error;
        }
        return MEMTX_OK;
    }
    c = &s->channel[(offset >> 8) - 1];
    switch (offset & 0xff) {
    case 0:
        *value = bank ? c->reset : c->control;
        break;
    case 4:
        *value = bank ? c->opcode : 0;
        break;
    case 8:
        *value = bank ? c->period : 0;
        break;
    case 12:
        if (!bank) {
            goto error;
        }
        count = z7ii_timing_count(c);
        *value = c->opcode == 0x31 ? MIN(count, (uint64_t)c->period + 1) :
                                    count % ((uint64_t)c->period + 1);
        break;
    case 16:
        if (bank) {
            goto error;
        }
        *value = c->status;
        break;
    default:
        goto error;
    }
    return MEMTX_OK;
error:
    return z7ii_timing_error(s, bank, offset, size, false, 0);
}

static MemTxResult z7ii_timing_write_bank(Z7IITimingState *s, unsigned bank,
                                         hwaddr offset, uint64_t value,
                                         unsigned size)
{
    Z7IITimingChannel *c;
    unsigned i;

    if (size != 4 || (offset & 3) || offset >= 0xd00) {
        goto error;
    }
    if (offset < 0x100) {
        if (!bank && offset == 0 && value <= 1) {
            s->reset = value;
            if (value) {
                s->enabled = s->master = 0;
                for (i = 0; i < 12; i++) {
                    z7ii_timing_channel_reset(&s->channel[i]);
                    s->channel[i].reset = 0;
                }
            }
        } else if (!bank && offset == 4 && value <= 1) {
            s->master = value;
        } else if (bank && offset == 0 && !(value & ~0xfffU)) {
            s->enabled = value;
        } else if (!(bank && offset == 12 && !value)) {
            /* Shared packet+4/submit+8 semantics are not established. */
            goto error;
        }
        for (i = 0; i < 12; i++) {
            z7ii_timing_update(&s->channel[i]);
        }
        return MEMTX_OK;
    }
    c = &s->channel[(offset >> 8) - 1];
    switch (offset & 0xff) {
    case 0:
        if (value > 1) {
            goto error;
        }
        if (bank) {
            c->reset = value;
            if (value) {
                z7ii_timing_channel_reset(c);
            }
        } else {
            c->control = value;
        }
        break;
    case 4:
        if (bank) {
            if (value != 0 && value != 0x31) {
                goto error;
            }
            if (c->running && value != c->opcode) {
                goto error;
            }
            c->opcode = value;
        } else if (value) {
            goto error;
        }
        break;
    case 8:
        if (bank) {
            timer_del(c->timer);
            c->period = value;
            c->count = 0;
            c->configured = true;
            c->running = c->complete = false;
        } else if (value) {
            goto error;
        }
        break;
    case 16:
        if (bank) {
            /* Only actual channels5/6 expose this optional enable word. */
            if ((c->index != 5 && c->index != 6) || value) {
                goto error;
            }
        } else {
            if (value & ~0x3fU) {
                goto error;
            }
            c->status &= ~value;
            z7ii_timing_irq(c);
        }
        break;
    case 28:
        if (!bank || c->index > 6 || value) {
            goto error;
        }
        break;
    case 36:
    case 56:
        if (!bank || c->index > 4 || value) {
            goto error;
        }
        break;
    default:
        goto error;
    }
    z7ii_timing_update(c);
    return MEMTX_OK;
error:
    return z7ii_timing_error(s, bank, offset, size, true, value);
}

#define TIMING_BANK_OPS(n) \
static MemTxResult z7ii_timing_read##n(void *opaque, hwaddr offset, \
                                      uint64_t *value, unsigned size, \
                                      MemTxAttrs attrs) \
{ return z7ii_timing_read_bank(opaque, n, offset, value, size); } \
static MemTxResult z7ii_timing_write##n(void *opaque, hwaddr offset, \
                                       uint64_t value, unsigned size, \
                                       MemTxAttrs attrs) \
{ return z7ii_timing_write_bank(opaque, n, offset, value, size); } \
static const MemoryRegionOps z7ii_timing_ops##n = { \
    .read_with_attrs = z7ii_timing_read##n, \
    .write_with_attrs = z7ii_timing_write##n, \
    .endianness = DEVICE_LITTLE_ENDIAN, \
    .valid = { .min_access_size = 1, .max_access_size = 4, \
               .unaligned = true }, \
    .impl = { .min_access_size = 1, .max_access_size = 4, .unaligned = true }, \
};
TIMING_BANK_OPS(0)
TIMING_BANK_OPS(1)

static void z7ii_timing_reset(DeviceState *dev)
{
    Z7IITimingState *s = Z7II_TIMING(dev);
    unsigned i;

    s->reset = s->master = s->enabled = 0;
    s->reported_error = false;
    clock_update_hz(s->clock, 3000000000ULL);
    for (i = 0; i < 12; i++) {
        s->channel[i].reset = 0;
        z7ii_timing_channel_reset(&s->channel[i]);
    }
}

static void z7ii_timing_init(Object *obj)
{
    Z7IITimingState *s = Z7II_TIMING(obj);
    unsigned i;

    s->clock = qdev_init_clock_out(DEVICE(obj), "nominal-timing-clock");
    memory_region_init_io(&s->regs[0], obj, &z7ii_timing_ops0, s,
                          "z7ii.timing-a", 0xd00);
    memory_region_init_io(&s->regs[1], obj, &z7ii_timing_ops1, s,
                          "z7ii.timing-b", 0xd00);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs[0]);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs[1]);
    for (i = 0; i < 12; i++) {
        Z7IITimingChannel *c = &s->channel[i];

        c->owner = s;
        c->index = i;
        c->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, z7ii_timing_expire, c);
        sysbus_init_irq(SYS_BUS_DEVICE(obj), &c->irq);
    }
}

static void z7ii_timing_finalize(Object *obj)
{
    Z7IITimingState *s = Z7II_TIMING(obj);
    unsigned i;

    for (i = 0; i < 12; i++) {
        timer_free(s->channel[i].timer);
    }
}

static const VMStateDescription z7ii_timing_vmstate = {
    .name = "z7ii-timing", .unmigratable = true,
};

static void z7ii_timing_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &z7ii_timing_vmstate;
    dc->user_creatable = false;
    device_class_set_legacy_reset(dc, z7ii_timing_reset);
}

static const TypeInfo z7ii_timing_type = {
    .name = TYPE_Z7II_TIMING,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Z7IITimingState),
    .instance_init = z7ii_timing_init,
    .instance_finalize = z7ii_timing_finalize,
    .class_init = z7ii_timing_class_init,
};

static void z7ii_timing_register_types(void)
{
    type_register_static(&z7ii_timing_type);
}
type_init(z7ii_timing_register_types)
