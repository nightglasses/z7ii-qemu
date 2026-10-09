/*
 * Original twelve-channel timing interface, explicitly functional semantics.
 * Period/current-count geometry comes from guest tables and setup callers.
 * Opcode0 periodic / opcode31 one-shot / opcode1 free-running source, 3GHz,
 * threshold bit0 and W1C are virtual board assumptions. Opcode1 reports its
 * configured threshold once and keeps counting; it is not a periodic alias.
 * Channels0..4 also expose a nominal compare waveform and period bit4.
 * Extra feature activation remains unsupported; this is not a DMA engine.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "migration/vmstate.h"
#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "hw/core/qdev-clock.h"
#include "hw/nikon/devices.h"

OBJECT_DECLARE_SIMPLE_TYPE(NikonTimingState, NIKON_TIMING)

typedef struct NikonTimingChannel {
    NikonTimingState *owner;
    QEMUTimer *timer;
    qemu_irq irq;
    uint32_t reset, control, opcode, period, status, captured[2], compare;
    uint32_t extra_config;
    uint64_t count, compare_cycles;
    int64_t epoch;
    unsigned index;
    bool configured, running, complete, threshold_reported;
    bool capture_enabled, capture_level;
    bool compare_enabled, compare_polarity, compare_started;
} NikonTimingChannel;

struct NikonTimingState {
    SysBusDevice parent_obj;
    MemoryRegion regs[2];
    Clock *clock;
    NikonTimingChannel channel[12];
    qemu_irq compare_output[5];
    uint32_t reset, master, enabled;
    bool reported_error;
};

static uint64_t nikon_timing_count(NikonTimingChannel *c)
{
    uint64_t count = c->count;

    if (c->running) {
        count += clock_ns_to_ticks(c->owner->clock,
                    qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) - c->epoch);
    }
    return count;
}

static void nikon_timing_irq(NikonTimingChannel *c)
{
    qemu_set_irq(c->irq, !!c->status);
}

/* Virtual waveform: phase < compare, optionally inverted; inactive low. */
static void nikon_timing_compare_output(NikonTimingChannel *c)
{
    bool level = false;

    if (c->index > 4) {
        return;
    }
    if (c->running && c->compare_enabled && c->compare_started) {
        uint64_t phase = nikon_timing_count(c) % ((uint64_t)c->period + 1);

        level = (phase < c->compare) ^ c->compare_polarity;
    }
    qemu_set_irq(c->owner->compare_output[c->index], level);
}

/* Named virtual mode0: one external signal per capture-capable channel. */
static void nikon_timing_capture(void *opaque, int input, int level)
{
    NikonTimingState *s = opaque;
    NikonTimingChannel *c = &s->channel[5 + input];
    unsigned edge = level ? 0 : 1;
    unsigned gate = 1U << (edge + 1);

    if (c->capture_level == !!level) {
        return;
    }
    c->capture_level = !!level;
    if (!c->capture_enabled || !c->running || !(c->control & gate)) {
        return;
    }
    c->captured[edge] = (uint32_t)nikon_timing_count(c);
    /* The guest's primary callback consumes status bits1/2, not PWM bit4. */
    c->status |= gate;
    nikon_timing_irq(c);
}

static void nikon_timing_schedule(NikonTimingChannel *c)
{
    uint64_t period = (uint64_t)c->period + 1;
    uint64_t count = nikon_timing_count(c);
    uint64_t remaining = count < period ? period - count : 1;

    if (c->compare_enabled) {
        uint64_t phase = count % period;

        remaining = period - phase;
        if (c->compare > phase && c->compare < period) {
            remaining = MIN(remaining, c->compare - phase);
        }
        if (!c->compare_started) {
            remaining = 1;
        }
    } else if (c->opcode == 1 && c->threshold_reported) {
        /* A feature transition must also cancel its previous edge timer. */
        timer_del(c->timer);
        return;
    }
    /* Ceil to a nanosecond: no premature expiry from sub-ns nominal ticks. */
    timer_mod(c->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                        clock_ticks_to_ns(c->owner->clock, remaining) + 1);
}

static void nikon_timing_update(NikonTimingChannel *c)
{
    NikonTimingState *s = c->owner;
    bool enabled = !s->reset && s->master &&
                   (s->enabled & (1U << c->index)) && !c->reset &&
                   ((c->control & 1) || c->compare_enabled) &&
                   c->configured && !c->complete;

    if (c->running && !enabled) {
        c->count = nikon_timing_count(c);
        c->running = false;
        timer_del(c->timer);
        nikon_timing_compare_output(c);
    } else if (!c->running && enabled) {
        c->epoch = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        c->running = true;
        c->compare_started = false;
        nikon_timing_schedule(c);
    }
}

static void nikon_timing_expire(void *opaque)
{
    NikonTimingChannel *c = opaque;
    uint64_t period = (uint64_t)c->period + 1;
    uint64_t count;

    if (!c->running) {
        return;
    }
    count = nikon_timing_count(c);
    if (c->compare_enabled) {
        c->compare_started = true;
        if (c->opcode == 1) {
            uint64_t cycles = count / period;

            if (cycles > c->compare_cycles) {
                c->status |= 0x10;
                c->compare_cycles = cycles;
            }
            c->threshold_reported |= count >= period;
            nikon_timing_compare_output(c);
            nikon_timing_schedule(c);
            nikon_timing_irq(c);
            return;
        }
    }
    if (count < period) {
        nikon_timing_compare_output(c);
        nikon_timing_schedule(c);
        return;
    }
    c->status |= c->compare_enabled ? 0x10 : 1;
    if (c->opcode == 1) {
        c->threshold_reported = true;
    } else if (c->opcode == 0x31) {
        c->count = period;
        c->running = false;
        c->complete = true;
    } else {
        c->count = count % period;
        c->epoch = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        nikon_timing_schedule(c);
    }
    nikon_timing_irq(c);
    nikon_timing_compare_output(c);
}

static void nikon_timing_channel_reset(NikonTimingChannel *c)
{
    timer_del(c->timer);
    c->control = c->opcode = c->period = c->status = 0;
    c->count = 0;
    c->configured = c->running = c->complete = c->threshold_reported = false;
    c->capture_enabled = false;
    c->compare_enabled = c->compare_polarity = c->compare_started = false;
    c->compare = 0;
    c->extra_config = 0;
    c->compare_cycles = 0;
    c->captured[0] = c->captured[1] = 0;
    /* Reset changes controller state, never an external pad level. */
    nikon_timing_irq(c);
    nikon_timing_compare_output(c);
}

static MemTxResult nikon_timing_error(NikonTimingState *s, unsigned bank,
                                    hwaddr offset, unsigned size, bool write,
                                    uint64_t value)
{
    nikon_access_error(&s->reported_error, s->regs[bank].addr + offset,
                      size, write, value,
                      "timing-access-or-feature-not-supported");
    return MEMTX_ERROR;
}

static MemTxResult nikon_timing_read_bank(NikonTimingState *s, unsigned bank,
                                        hwaddr offset, uint64_t *value,
                                        unsigned size)
{
    NikonTimingChannel *c;
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
        count = nikon_timing_count(c);
        if (c->opcode == 1) {
            *value = (uint32_t)count;
        } else {
            *value = c->opcode == 0x31 ? MIN(count, (uint64_t)c->period + 1) :
                                        count % ((uint64_t)c->period + 1);
        }
        break;
    case 16:
        if (bank) {
            goto error;
        }
        *value = c->status;
        break;
    case 20:
    case 24:
        if (!bank || (c->index != 5 && c->index != 6)) {
            goto error;
        }
        *value = c->captured[((offset & 0xff) - 20) / 4];
        break;
    case 36: case 40: case 44: case 48:
        if (!bank || c->index > 4) {
            goto error;
        }
        switch (offset & 0xff) {
        case 36:
            *value = c->compare_enabled;
            break;
        case 40:
            *value = c->compare_polarity;
            break;
        case 44:
            *value = c->compare;
            break;
        case 48:
            /* Virtual phase result; original producer/meaning unknown. */
            *value = nikon_timing_count(c) % ((uint64_t)c->period + 1);
            break;
        }
        break;
    case 56: case 60:
        if (!bank || c->index > 4) {
            goto error;
        }
        *value = (offset & 0xff) == 56 ? 0 : c->extra_config;
        break;
    default:
        goto error;
    }
    return MEMTX_OK;
error:
    return nikon_timing_error(s, bank, offset, size, false, 0);
}

static MemTxResult nikon_timing_write_bank(NikonTimingState *s, unsigned bank,
                                         hwaddr offset, uint64_t value,
                                         unsigned size)
{
    NikonTimingChannel *c;
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
                    nikon_timing_channel_reset(&s->channel[i]);
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
            nikon_timing_update(&s->channel[i]);
        }
        return MEMTX_OK;
    }
    c = &s->channel[(offset >> 8) - 1];
    switch (offset & 0xff) {
    case 0:
        if (value & ~((!bank && (c->index == 5 || c->index == 6)) ? 7U : 1U)) {
            goto error;
        }
        if (bank) {
            c->reset = value;
            if (value) {
                nikon_timing_channel_reset(c);
            }
        } else {
            c->control = value;
        }
        break;
    case 4:
        if (bank) {
            if (value != 0 && value != 1 && value != 0x31) {
                goto error;
            }
            if (c->running && value != c->opcode) {
                goto error;
            }
            if (c->capture_enabled && value != 1) {
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
            c->running = c->complete = c->threshold_reported = false;
            c->captured[0] = c->captured[1] = 0;
            c->compare_cycles = 0;
            c->compare_started = false;
            c->status &= ~0x16U;
            nikon_timing_irq(c);
            nikon_timing_compare_output(c);
        } else if (value) {
            goto error;
        }
        break;
    case 16:
        if (bank) {
            /*
             * Only channels5/6 expose mode0 external capture. Preflight all
             * requirements before retaining an armed configuration.
             */
            if ((c->index != 5 && c->index != 6) || value > 1 ||
                (value && (!c->configured || c->opcode != 1))) {
                goto error;
            }
            c->capture_enabled = value;
        } else {
            if (value & ~0x3fU) {
                goto error;
            }
            c->status &= ~value;
            nikon_timing_irq(c);
        }
        break;
    case 28:
        if (!bank || c->index > 6 || value) {
            goto error;
        }
        break;
    case 36:
        if (!bank || c->index > 4 || value > 1 ||
            (value && !c->configured)) {
            goto error;
        }
        if (value && !c->compare_enabled) {
            /* Free-running basic time is not a completed compare period. */
            c->compare_cycles = nikon_timing_count(c) /
                                ((uint64_t)c->period + 1);
        }
        c->compare_enabled = value;
        c->compare_started = false;
        if (!value) {
            nikon_timing_compare_output(c);
        }
        break;
    case 40:
        if (!bank || c->index > 4 || value > 1) {
            goto error;
        }
        c->compare_polarity = value;
        c->compare_started = false;
        break;
    case 44:
        if (!bank || c->index > 4) {
            goto error;
        }
        c->compare = value;
        c->compare_started = false;
        break;
    case 56:
        if (!bank || c->index > 4 || value) {
            goto error;
        }
        break;
    case 60:
        if (!bank || c->index > 4) {
            goto error;
        }
        c->extra_config = value;
        break;
    default:
        goto error;
    }
    nikon_timing_update(c);
    if (c->running) {
        nikon_timing_schedule(c);
    }
    return MEMTX_OK;
error:
    return nikon_timing_error(s, bank, offset, size, true, value);
}

#define TIMING_BANK_OPS(n) \
static MemTxResult nikon_timing_read##n(void *opaque, hwaddr offset, \
                                      uint64_t *value, unsigned size, \
                                      MemTxAttrs attrs) \
{ return nikon_timing_read_bank(opaque, n, offset, value, size); } \
static MemTxResult nikon_timing_write##n(void *opaque, hwaddr offset, \
                                       uint64_t value, unsigned size, \
                                       MemTxAttrs attrs) \
{ return nikon_timing_write_bank(opaque, n, offset, value, size); } \
static const MemoryRegionOps nikon_timing_ops##n = { \
    .read_with_attrs = nikon_timing_read##n, \
    .write_with_attrs = nikon_timing_write##n, \
    .endianness = DEVICE_LITTLE_ENDIAN, \
    .valid = { .min_access_size = 1, .max_access_size = 4, \
               .unaligned = true }, \
    .impl = { .min_access_size = 1, .max_access_size = 4, .unaligned = true }, \
};
TIMING_BANK_OPS(0)
TIMING_BANK_OPS(1)

static void nikon_timing_reset(DeviceState *dev)
{
    NikonTimingState *s = NIKON_TIMING(dev);
    unsigned i;

    s->reset = s->master = s->enabled = 0;
    s->reported_error = false;
    clock_update_hz(s->clock, 3000000000ULL);
    for (i = 0; i < 12; i++) {
        s->channel[i].reset = 0;
        nikon_timing_channel_reset(&s->channel[i]);
    }
}

static void nikon_timing_init(Object *obj)
{
    NikonTimingState *s = NIKON_TIMING(obj);
    unsigned i;

    s->clock = qdev_init_clock_out(DEVICE(obj), "nominal-timing-clock");
    qdev_init_gpio_in_named(DEVICE(obj), nikon_timing_capture, "capture", 2);
    qdev_init_gpio_out_named(DEVICE(obj), s->compare_output, "compare", 5);
    memory_region_init_io(&s->regs[0], obj, &nikon_timing_ops0, s,
                          "nikon.timing-a", 0xd00);
    memory_region_init_io(&s->regs[1], obj, &nikon_timing_ops1, s,
                          "nikon.timing-b", 0xd00);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs[0]);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs[1]);
    for (i = 0; i < 12; i++) {
        NikonTimingChannel *c = &s->channel[i];

        c->owner = s;
        c->index = i;
        c->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, nikon_timing_expire, c);
        sysbus_init_irq(SYS_BUS_DEVICE(obj), &c->irq);
    }
}

static void nikon_timing_finalize(Object *obj)
{
    NikonTimingState *s = NIKON_TIMING(obj);
    unsigned i;

    for (i = 0; i < 12; i++) {
        timer_free(s->channel[i].timer);
    }
}

static const VMStateDescription nikon_timing_vmstate = {
    .name = "nikon-timing", .unmigratable = true,
};

static void nikon_timing_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &nikon_timing_vmstate;
    dc->user_creatable = false;
    device_class_set_legacy_reset(dc, nikon_timing_reset);
}

static const TypeInfo nikon_timing_type = {
    .name = TYPE_NIKON_TIMING,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonTimingState),
    .instance_init = nikon_timing_init,
    .instance_finalize = nikon_timing_finalize,
    .class_init = nikon_timing_class_init,
};

static void nikon_timing_register_types(void)
{
    type_register_static(&nikon_timing_type);
}
type_init(nikon_timing_register_types)
