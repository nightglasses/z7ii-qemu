/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * EX STM32 timer functional protocol. QEMU Clock/QEMUTimer provide the time
 * base. The original's CC1-only service requires compare scheduling, bounded
 * CNT, buffered PSC and RCC-derived clocks absent from upstream's F2 timer.
 */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "qemu/timer.h"
#include "qemu/host-utils.h"
#include "hw/core/cpu.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "hw/nikon/extimer.h"
#include "qapi/error.h"

enum {
    CR1, CR2, SMCR, DIER, SR, EGR, CCMR1, CCMR2, CCER,
    CNT, PSC, ARR, RCR, CCR1, CCR2, CCR3, CCR4, BDTR, DCR, DMAR, OR,
};

struct NikonEXTimerState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;
    Clock *clk;
    QEMUTimer *deadline;
    CPUState *cpu;
    uint64_t base;
    uint32_t reg[21];
    uint32_t active_arr;
    uint32_t active_psc;
    uint32_t active_ccr[4];
    uint32_t width;
    uint32_t channels;
    uint32_t number;
    bool advanced;
    bool down;
    bool trigger;
    bool capture[4];
    bool output[4];
    uint8_t capture_count[4];
    uint8_t repetition;
    uint8_t burst;
    int64_t epoch;
    uint64_t fraction;
    qemu_irq irq[4];
    qemu_irq pad[4];
    qemu_irq dma[7];
};

static void timer_schedule(NikonEXTimerState *s);

static uint32_t timer_mask(NikonEXTimerState *s)
{
    return s->width == 32 ? UINT32_MAX : UINT16_MAX;
}

static unsigned timer_channel(NikonEXTimerState *s, unsigned channel)
{
    return (s->reg[CCMR1 + channel / 2] >> ((channel % 2) * 8)) & 0xff;
}

static void timer_irq(NikonEXTimerState *s)
{
    uint32_t flags = s->reg[SR] & s->reg[DIER] & 0xff;
    qemu_set_irq(s->irq[0], s->advanced ? flags & 1 : !!flags);
    qemu_set_irq(s->irq[1], s->advanced && (flags & 0x1e));
    qemu_set_irq(s->irq[2], s->advanced && (flags & 0x60));
    qemu_set_irq(s->irq[3], s->advanced && (flags & 0x80));
}

static void timer_outputs(NikonEXTimerState *s)
{
    for (unsigned i = 0; i < s->channels; i++) {
        unsigned mode = (timer_channel(s, i) >> 4) & 7;
        if (mode == 4 || mode == 5) {
            s->output[i] = mode == 5;
        } else if (mode == 6 || mode == 7) {
            bool active = s->down ? s->reg[CNT] <= s->active_ccr[i] :
                                    s->reg[CNT] < s->active_ccr[i];
            s->output[i] = active ^ (mode == 7);
        }
        bool enabled = (s->reg[CCER] & (1 << (i * 4))) &&
                       !(timer_channel(s, i) & 3) &&
                       (!s->advanced || (s->reg[BDTR] & (1 << 15)));
        int value = enabled ?
            s->output[i] ^ !!(s->reg[CCER] & (2 << (i * 4))) : -1;
        qemu_set_irq(s->pad[i], value);
    }
}

static void timer_event(NikonEXTimerState *s, unsigned event)
{
    s->reg[SR] |= 1U << event;
    bool update_dma = s->reg[CR2] & 8;
    if (event < 7 && (s->reg[DIER] & (1 << (event + 8))) &&
        (event < 1 || event > 4 || !update_dma)) {
        qemu_irq_pulse(s->dma[event]);
    }
    if (event == 0 && update_dma) {
        for (unsigned i = 0; i < s->channels; i++) {
            if (s->reg[DIER] & (1 << (i + 9))) {
                qemu_irq_pulse(s->dma[i + 1]);
            }
        }
    }
}

static void timer_reload(NikonEXTimerState *s)
{
    s->active_psc = s->reg[PSC];
    s->active_arr = s->reg[ARR];
    for (unsigned i = 0; i < s->channels; i++) {
        s->active_ccr[i] = s->reg[CCR1 + i];
    }
    uint64_t period = (uint64_t)s->active_arr + 1;
    s->reg[CNT] %= period;
    s->fraction = 0;
}

static bool timer_running(NikonEXTimerState *s)
{
    unsigned slave = s->reg[SMCR] & 7;
    return (s->reg[CR1] & 1) && clock_get_hz(s->clk) && s->active_arr &&
           slave < 7 && !(s->reg[SMCR] & (1 << 14)) &&
           (slave != 5 || s->trigger);
}

static uint64_t timer_distance(NikonEXTimerState *s)
{
    uint64_t distance;
    if (s->reg[CR1] & 0x60) {
        distance = s->down ? s->reg[CNT] : s->active_arr - s->reg[CNT];
        distance = MAX(distance, 1);
    } else {
        distance = s->down ? (uint64_t)s->reg[CNT] + 1 :
                            (uint64_t)s->active_arr + 1 - s->reg[CNT];
    }
    for (unsigned i = 0; i < s->channels; i++) {
        if (timer_channel(s, i) & 3) {
            continue;
        }
        uint32_t compare = s->active_ccr[i];
        if (compare <= s->active_arr) {
            if (!s->down && compare > s->reg[CNT]) {
                distance = MIN(distance, (uint64_t)compare - s->reg[CNT]);
            } else if (s->down && compare < s->reg[CNT]) {
                distance = MIN(distance, (uint64_t)s->reg[CNT] - compare);
            }
        }
    }
    return MAX(distance, 1);
}

static void timer_advance(NikonEXTimerState *s, uint64_t ticks)
{
    while (ticks && (s->reg[CR1] & 1) && s->active_arr) {
        uint64_t distance = timer_distance(s);
        uint64_t step = MIN(ticks, distance);
        bool previous_down = s->down;
        uint64_t boundary = s->down ? (uint64_t)s->reg[CNT] + 1 :
                            (uint64_t)s->active_arr + 1 - s->reg[CNT];
        if (s->reg[CR1] & 0x60) {
            boundary = s->down ? s->reg[CNT] :
                                s->active_arr - s->reg[CNT];
            boundary = MAX(boundary, 1);
        }
        bool update = step == boundary;
        if (update) {
            if (s->reg[CR1] & 0x60) {
                s->reg[CNT] = s->down ? 0 : s->active_arr;
                s->down = !s->down;
                s->reg[CR1] = (s->reg[CR1] & ~0x10U) | (s->down ? 0x10 : 0);
            } else {
                s->reg[CNT] = s->down ? s->active_arr : 0;
            }
        } else {
            s->reg[CNT] = s->down ? s->reg[CNT] - step : s->reg[CNT] + step;
        }
        ticks -= step;
        for (unsigned i = 0; i < s->channels; i++) {
            unsigned channel = timer_channel(s, i);
            unsigned center = (s->reg[CR1] >> 5) & 3;
            if (!(channel & 3) &&
                (s->reg[CNT] == s->active_ccr[i] ||
                 (update && s->active_ccr[i] > s->active_arr)) &&
                (!center || center == 3 ||
                 (center == 1 && previous_down) ||
                 (center == 2 && !previous_down))) {
                timer_event(s, i + 1);
                unsigned mode = (channel >> 4) & 7;
                if (mode == 1 || mode == 2) {
                    s->output[i] = mode == 1;
                } else if (mode == 3) {
                    s->output[i] = !s->output[i];
                }
            }
        }
        if (update && !(s->reg[CR1] & 2)) {
            if (s->repetition) {
                s->repetition--;
            } else {
                timer_event(s, 0);
                s->repetition = s->reg[RCR];
                timer_reload(s);
                if (s->reg[CR1] & 8) {
                    s->reg[CR1] &= ~1U;
                }
            }
        }
        timer_outputs(s);
        timer_irq(s);
    }
}

static void timer_sync(NikonEXTimerState *s)
{
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    if (timer_running(s) && now > s->epoch) {
        uint64_t lo, hi;
        mulu64(&lo, &hi, now - s->epoch, clock_get_hz(s->clk));
        Int128 budget = int128_add(int128_make128(lo, hi),
                                   int128_make64(s->fraction));
        s->fraction = 0;
        /*
         * Stop at each compare/update boundary. Buffered PSC can change there;
         * remaining source-clock time must use the new divider.
         */
        while (int128_nz(budget) && timer_running(s)) {
            uint64_t divisor = UINT64_C(1000000000) * (s->active_psc + 1);
            uint64_t distance = timer_distance(s);
            mulu64(&lo, &hi, distance, divisor);
            Int128 cost = int128_make128(lo, hi);
            if (int128_lt(budget, cost)) {
                uint64_t ticks = int128_get64(int128_divu(
                    budget, int128_make64(divisor)));
                s->fraction = int128_get64(int128_remu(
                    budget, int128_make64(divisor)));
                timer_advance(s, ticks);
                break;
            }
            budget = int128_sub(budget, cost);
            timer_advance(s, distance);
        }
    }
    s->epoch = now;
}

static void timer_schedule(NikonEXTimerState *s)
{
    timer_del(s->deadline);
    if (timer_running(s)) {
        uint64_t hz = clock_get_hz(s->clk);
        uint64_t divisor = UINT64_C(1000000000) * (s->active_psc + 1);
        uint64_t lo, hi;
        mulu64(&lo, &hi, timer_distance(s), divisor);
        Int128 duration = int128_sub(int128_make128(lo, hi),
                                     int128_make64(s->fraction));
        duration = int128_divu(int128_add(duration, int128_make64(hz - 1)),
                              int128_make64(hz));
        duration = int128_min(duration, int128_make64(INT64_MAX - s->epoch));
        timer_mod(s->deadline, s->epoch + int128_get64(duration));
    }
}

static void nikon_extimer_expired(void *opaque)
{
    NikonEXTimerState *s = opaque;
    timer_sync(s);
    timer_schedule(s);
}

static void timer_clock(void *opaque, ClockEvent event)
{
    NikonEXTimerState *s = opaque;
    if (event == ClockPreUpdate) {
        timer_sync(s);
    } else {
        s->epoch = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        timer_schedule(s);
    }
}

static bool timer_valid(NikonEXTimerState *s, unsigned reg)
{
    if (reg >= ARRAY_SIZE(s->reg)) {
        return false;
    }
    if ((reg == RCR || reg == BDTR) && !s->advanced) {
        return false;
    }
    if (reg == OR && s->number != 2 && s->number != 5 && s->number != 11) {
        return false;
    }
    if (s->channels == 0 && (reg == SMCR || reg == CCMR1 || reg == CCMR2 ||
                            reg == CCER || reg == DCR || reg == DMAR)) {
        return false;
    }
    if (s->number >= 9 && (reg == CCMR2 || reg == DCR || reg == DMAR)) {
        return false;
    }
    return reg < CCR1 || reg > CCR4 || reg - CCR1 < s->channels;
}

static MemTxResult timer_bad(NikonEXTimerState *s, hwaddr offset,
                             unsigned size, bool write)
{
    CPUClass *cc = CPU_GET_CLASS(s->cpu);
    qemu_log_mask(LOG_GUEST_ERROR,
                  "EX timer: CPU%d PC0x%" VADDR_PRIx " unsupported %s "
                  "address0x%" HWADDR_PRIx " width%u\n", s->cpu->cpu_index,
                  cc->get_pc(s->cpu), write ? "write" : "read",
                  s->base + offset, size);
    return MEMTX_ERROR;
}

static MemTxResult timer_read(void *opaque, hwaddr addr, uint64_t *value,
                             unsigned size, MemTxAttrs attrs)
{
    NikonEXTimerState *s = opaque;
    unsigned reg = addr / 4;
    if ((addr & 3) + size > 4 || !timer_valid(s, reg)) {
        return timer_bad(s, addr, size, false);
    }
    timer_sync(s);
    unsigned target = reg;
    if (reg == DMAR) {
        target = (s->reg[DCR] & 0x1f) + s->burst;
        if (!timer_valid(s, target) || target == DMAR) {
            return timer_bad(s, addr, size, false);
        }
        s->burst = (s->burst + 1) % (((s->reg[DCR] >> 8) & 0x1f) + 1);
    }
    *value = extract32(s->reg[target], (addr & 3) * 8, size * 8);
    if (target >= CCR1 && target <= CCR4 &&
        (timer_channel(s, target - CCR1) & 3)) {
        s->reg[SR] &= ~(2U << (target - CCR1));
        timer_irq(s);
    }
    timer_schedule(s);
    return MEMTX_OK;
}

static void timer_write_reg(NikonEXTimerState *s, unsigned reg, uint32_t value)
{
    if (reg == SR) {
        s->reg[SR] &= value;
    } else if (reg == EGR) {
        if (value & 1) {
            timer_reload(s);
            s->down = s->reg[CR1] & 0x10;
            s->reg[CNT] = s->down ? s->active_arr : 0;
            s->repetition = s->reg[RCR];
            if (!(s->reg[CR1] & 6)) {
                timer_event(s, 0);
            }
        }
        for (unsigned event = 1; event < 8; event++) {
            if (value & (1 << event)) {
                timer_event(s, event);
            }
        }
    } else if (reg == CNT) {
        s->reg[CNT] = (value & timer_mask(s)) %
                     ((uint64_t)s->active_arr + 1);
        s->fraction = 0;
    } else if (reg == PSC) {
        s->reg[PSC] = value & 0xffff;
    } else if (reg == ARR) {
        s->reg[ARR] = value & timer_mask(s);
        if (!(s->reg[CR1] & 0x80)) {
            s->active_arr = s->reg[ARR];
            s->reg[CNT] %= (uint64_t)s->active_arr + 1;
        }
    } else if (reg >= CCR1 && reg <= CCR4) {
        s->reg[reg] = value & timer_mask(s);
        if (!(timer_channel(s, reg - CCR1) & 8)) {
            s->active_ccr[reg - CCR1] = s->reg[reg];
        }
    } else if (reg == CR1) {
        s->reg[CR1] = value & 0x3ff;
        if (!(value & 0x60)) {
            s->down = value & 0x10;
        } else {
            s->reg[CR1] = deposit32(s->reg[CR1], 4, 1, s->down);
        }
    } else if (reg == RCR) {
        s->reg[RCR] = value & 0xff;
    } else {
        s->reg[reg] = value;
    }
}

static MemTxResult timer_write(void *opaque, hwaddr addr, uint64_t value,
                              unsigned size, MemTxAttrs attrs)
{
    NikonEXTimerState *s = opaque;
    unsigned reg = addr / 4;
    if ((addr & 3) + size > 4 || !timer_valid(s, reg)) {
        return timer_bad(s, addr, size, true);
    }
    timer_sync(s);
    uint32_t data = deposit32(s->reg[reg], (addr & 3) * 8, size * 8, value);
    if (reg == SMCR && ((data & 7) < 4 && (data & 7))) {
        return timer_bad(s, addr, size, true); /* No encoder waveform. */
    }
    if ((reg == CCMR1 || reg == CCMR2) &&
        (((data & 3) && (data & 0xf0)) ||
         ((data & 0x300) && (data & 0xf000)))) {
        return timer_bad(s, addr, size, true); /* Capture filter clock. */
    }
    if (reg == DMAR) {
        unsigned target = (s->reg[DCR] & 0x1f) + s->burst;
        if (!timer_valid(s, target) || target == DMAR) {
            return timer_bad(s, addr, size, true);
        }
        timer_write_reg(s, target, data);
        s->burst = (s->burst + 1) % (((s->reg[DCR] >> 8) & 0x1f) + 1);
    } else {
        timer_write_reg(s, reg, data);
        if (reg == DCR) {
            s->burst = 0;
        }
    }
    timer_outputs(s);
    timer_irq(s);
    timer_schedule(s);
    return MEMTX_OK;
}

static void timer_latch(NikonEXTimerState *s, unsigned channel)
{
    unsigned config = timer_channel(s, channel);
    if (++s->capture_count[channel] == (1 << ((config >> 2) & 3))) {
        s->capture_count[channel] = 0;
        if (s->reg[SR] & (2 << channel)) {
            s->reg[SR] |= 1 << (channel + 9);
        }
        s->reg[CCR1 + channel] = s->reg[CNT];
        timer_event(s, channel + 1);
    }
}

static void timer_capture(void *opaque, int pin, int level)
{
    NikonEXTimerState *s = opaque;
    bool previous = s->capture[pin];
    s->capture[pin] = level;
    timer_sync(s);
    for (unsigned channel = 0; channel < s->channels; channel++) {
        unsigned selection = timer_channel(s, channel) & 3;
        unsigned polarity = (s->reg[CCER] >> (channel * 4 + 1)) & 5;
        bool selected = selection == 1 ? channel == pin :
                        selection == 2 && (channel ^ 1) == pin;
        bool edge = previous != !!level &&
                    (polarity == 5 || (!!level != !!(polarity & 1)));
        if (selected && edge && (s->reg[CCER] & (1 << (channel * 4)))) {
            timer_latch(s, channel);
        }
    }
    timer_irq(s);
    timer_schedule(s);
}

static void timer_trigger(void *opaque, int pin, int level)
{
    NikonEXTimerState *s = opaque;
    timer_sync(s);
    bool edge = !s->trigger && level;
    s->trigger = level;
    if (edge) {
        unsigned slave = s->reg[SMCR] & 7;
        timer_event(s, 6);
        for (unsigned channel = 0; channel < s->channels; channel++) {
            if ((timer_channel(s, channel) & 3) == 3 &&
                (s->reg[CCER] & (1 << (channel * 4)))) {
                timer_latch(s, channel);
            }
        }
        if (slave == 4) {
            timer_reload(s);
            s->reg[CNT] = s->down ? s->active_arr : 0;
            if (!(s->reg[CR1] & 6)) {
                timer_event(s, 0);
            }
        } else if (slave == 6) {
            s->reg[CR1] |= 1;
        } else if ((slave == 7 || (s->reg[SMCR] & (1 << 14))) &&
                   (s->reg[CR1] & 1)) {
            timer_advance(s, 1);
        }
    }
    timer_outputs(s);
    timer_irq(s);
    timer_schedule(s);
}

static void timer_break(void *opaque, int pin, int level)
{
    NikonEXTimerState *s = opaque;
    if (s->advanced && (s->reg[BDTR] & (1 << 12)) &&
        (!!level == !!(s->reg[BDTR] & (1 << 13)))) {
        s->reg[BDTR] &= ~(1 << 15);
        timer_event(s, 7);
        timer_outputs(s);
        timer_irq(s);
    }
}

static const MemoryRegionOps timer_ops = {
    .read_with_attrs = timer_read, .write_with_attrs = timer_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl = { .min_access_size = 1, .max_access_size = 4 },
};

static void timer_reset_hold(Object *obj, ResetType type)
{
    NikonEXTimerState *s = NIKON_EX_TIMER(obj);
    timer_del(s->deadline);
    memset(s->reg, 0, sizeof(s->reg));
    memset(s->active_ccr, 0, sizeof(s->active_ccr));
    memset(s->capture_count, 0, sizeof(s->capture_count));
    memset(s->output, 0, sizeof(s->output));
    s->reg[ARR] = s->active_arr = timer_mask(s);
    s->active_psc = s->repetition = s->burst = s->fraction = 0;
    s->down = false;
    s->epoch = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
}

static void timer_reset_exit(Object *obj, ResetType type)
{
    NikonEXTimerState *s = NIKON_EX_TIMER(obj);
    timer_outputs(s);
    timer_irq(s);
}

static void timer_realize(DeviceState *dev, Error **errp)
{
    NikonEXTimerState *s = NIKON_EX_TIMER(dev);
    if (!s->cpu || !clock_has_source(s->clk) ||
        (s->width != 16 && s->width != 32) ||
        s->channels > 4 || s->number < 1 || s->number > 14) {
        error_setg(errp, "EX timer requires cpu/clk and a valid timer profile");
    }
}

static void nikon_extimer_init(Object *obj)
{
    NikonEXTimerState *s = NIKON_EX_TIMER(obj);
    s->clk = qdev_init_clock_in(DEVICE(s), "clk", timer_clock, s,
                               ClockPreUpdate | ClockUpdate);
    s->deadline = timer_new_ns(QEMU_CLOCK_VIRTUAL, nikon_extimer_expired, s);
    memory_region_init_io(&s->mmio, obj, &timer_ops, s,
                          TYPE_NIKON_EX_TIMER, 0x400);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->mmio);
    for (unsigned i = 0; i < 4; i++) {
        sysbus_init_irq(SYS_BUS_DEVICE(s), &s->irq[i]);
    }
    qdev_init_gpio_in_named(DEVICE(s), timer_capture, "capture", 4);
    qdev_init_gpio_in_named(DEVICE(s), timer_trigger, "trigger", 1);
    qdev_init_gpio_in_named(DEVICE(s), timer_break, "break", 1);
    qdev_init_gpio_out_named(DEVICE(s), s->pad, "output", 4);
    qdev_init_gpio_out_named(DEVICE(s), s->dma, "dma", 7);
}

static void timer_finalize(Object *obj)
{
    timer_free(NIKON_EX_TIMER(obj)->deadline);
}

static const Property timer_properties[] = {
    DEFINE_PROP_UINT32("width", NikonEXTimerState, width, 16),
    DEFINE_PROP_UINT32("channels", NikonEXTimerState, channels, 4),
    DEFINE_PROP_UINT32("number", NikonEXTimerState, number, 0),
    DEFINE_PROP_BOOL("advanced", NikonEXTimerState, advanced, false),
    DEFINE_PROP_UINT64("base", NikonEXTimerState, base, 0),
    DEFINE_PROP_LINK("cpu", NikonEXTimerState, cpu, TYPE_CPU, CPUState *),
};

static void timer_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    dc->realize = timer_realize;
    dc->desc = "EX STM32 counter/compare timer functional protocol";
    device_class_set_props(dc, timer_properties);
    rc->phases.hold = timer_reset_hold;
    rc->phases.exit = timer_reset_exit;
}

static const TypeInfo timer_type = {
    .name = TYPE_NIKON_EX_TIMER, .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonEXTimerState), .instance_init = nikon_extimer_init,
    .instance_finalize = timer_finalize, .class_init = timer_class_init,
};

static void timer_register(void)
{
    type_register_static(&timer_type);
}
type_init(timer_register)
