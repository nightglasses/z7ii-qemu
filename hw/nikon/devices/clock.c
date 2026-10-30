/* Nikon finite functional device.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/timer.h"
#include "migration/vmstate.h"
#include "hw/core/sysbus.h"
#include "hw/core/qdev-clock.h"
#include "hw/nikon/devices.h"

#define NOMINAL_HZ 48000000
OBJECT_DECLARE_SIMPLE_TYPE(NikonClockGatesState, NIKON_CLOCK_GATES)

typedef struct NikonClockChannel {
    NikonClockGatesState *owner;
    unsigned index;
    Clock *output;
    QEMUTimer *lock_timer;
    bool locked;
} NikonClockChannel;

struct NikonClockGatesState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    uint32_t stopped[6];
    uint32_t known[6];
    uint32_t pll[3];
    uint32_t selector[10];
    uint32_t bit_control[2];
    uint32_t word_control;
    NikonClockChannel channel[9];
    Clock *spi_clock, *spi8_clock;
    bool reported_error;
};

static void nikon_spi_clock_rates(NikonClockGatesState *s)
{
    /* MAS103b0540 reads selector5 OR880008;10370c32 decodes these fields. */
    uint32_t value = s->selector[5] | 0x880008;
    uint64_t hz = 0, hz8 = 0;

    switch ((value >> 20) & 15) {
    case 8:
        hz = 80000000;
        break;
    case 9:
        hz = 100000000;
        break;
    }
    switch ((value >> 16) & 15) {
    case 8:
        hz8 = 80000000;
        break;
    case 9:
        hz8 = 100000000;
        break;
    case 10:
        hz8 = 61530000;
        break;
    }
    clock_update_hz(s->spi_clock, hz);
    clock_update_hz(s->spi8_clock, hz8);
}

/* Original nine-record group table; sentinel 0x20 means no such field. */
static const struct {
    unsigned reg;
    uint32_t stop_mask;
    uint32_t output_mask;
} nikon_pll_group[] = {
    { 0, 4, 8 }, { 0, 0, 2 }, { 0, 0x10, 0x20 },
    { 0, 0x40, 0x80 }, { 0, 0x100, 0x200 }, { 0, 0x400, 0x800 },
    { 1, 1, 2 }, { 1, 0x70, 0 }, { 2, 1, 2 },
};

static MemTxResult nikon_clock_error(NikonClockGatesState *s, hwaddr offset,
                                   unsigned size, bool write, uint64_t value,
                                   const char *reason)
{
    nikon_access_error(&s->reported_error, s->regs.addr + offset, size,
                      write, value, reason);
    return MEMTX_ERROR;
}

/* Six stop banks share SET(+0x100) and CLEAR(+0x200) mask aliases. */
static int nikon_clock_bank(hwaddr offset, hwaddr first)
{
    if (offset < first || offset >= first + 6 * 4 || (offset & 3)) {
        return -1;
    }
    return (offset - first) / 4;
}

static void nikon_pll_output(NikonClockChannel *channel)
{
    unsigned i = channel->index;
    uint32_t word = channel->owner->pll[nikon_pll_group[i].reg];
    bool enabled = channel->locked &&
                   !(word & nikon_pll_group[i].stop_mask) &&
                   !(word & nikon_pll_group[i].output_mask);

    /* Independent nominal virtual rates, never inferred from multiplier 0x163. */
    clock_update_hz(channel->output, enabled ? NOMINAL_HZ : 0);
}

static void nikon_pll_locked(void *opaque)
{
    NikonClockChannel *channel = opaque;

    channel->locked = true;
    nikon_pll_output(channel);
    qemu_log_mask(LOG_GUEST_ERROR,
                  "nikon-syscontrol: PLL group=%u modeled-lock nominal-Hz=%u\n",
                  channel->index, (unsigned)NOMINAL_HZ);
}

static void nikon_pll_update(NikonClockGatesState *s, unsigned reg,
                           uint32_t before)
{
    unsigned i;

    for (i = 0; i < ARRAY_SIZE(nikon_pll_group); i++) {
        NikonClockChannel *channel = &s->channel[i];
        uint32_t mask = nikon_pll_group[i].stop_mask;

        if (nikon_pll_group[i].reg != reg) {
            continue;
        }
        if (s->pll[reg] & mask) {
            timer_del(channel->lock_timer);
            channel->locked = false;
        } else if ((before & mask) ||
                   (!mask && (before & nikon_pll_group[i].output_mask) &&
                    !(s->pll[reg] & nikon_pll_group[i].output_mask))) {
            /* A 10us virtual lock interval is an explicit functional assumption.
             * No register is auto-cleared; output still follows guest gating. */
            channel->locked = false;
            timer_mod(channel->lock_timer,
                      qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 10000);
        }
        nikon_pll_output(channel);
    }
}

static MemTxResult nikon_clock_read(void *opaque, hwaddr offset,
                                  uint64_t *value, unsigned size,
                                  MemTxAttrs attrs)
{
    NikonClockGatesState *s = opaque;
    int bank = nikon_clock_bank(offset, 0x38);

    *value = 0; /* Invalid unless the complete word is known. */
    if (size == 4 && !(offset & 3)) {
        if (offset <= 0x24) {
            *value = s->selector[offset / 4];
            return MEMTX_OK;
        }
        if (offset >= 0x28 && offset <= 0x30) {
            *value = s->pll[(offset - 0x28) / 4];
            return MEMTX_OK;
        }
        if (offset == 0x34) {
            *value = s->word_control;
            return MEMTX_OK;
        }
        if (offset == 0x58 || offset == 0x5c) {
            *value = s->bit_control[(offset - 0x58) / 4];
            return MEMTX_OK;
        }
    }
    if (size != 4 || bank < 0) {
        return nikon_clock_error(s, offset, size, false, 0,
                                "clock-access-not-supported");
    }
    if (s->known[bank] != UINT32_MAX) {
        return nikon_clock_error(s, offset, size, false, 0,
                                "clock-stop-bits-uninitialized");
    }
    *value = s->stopped[bank];
    return MEMTX_OK;
}

static MemTxResult nikon_clock_write(void *opaque, hwaddr offset,
                                   uint64_t value, unsigned size,
                                   MemTxAttrs attrs)
{
    NikonClockGatesState *s = opaque;
    int bank = nikon_clock_bank(offset, 0x138);
    bool set = bank >= 0;
    uint32_t mask = value;

    if (size == 4 && !(offset & 3)) {
        static const uint32_t writable[] = {
            0x7ffffffe, 0xffff0073, 0x3f07ff07,
        };
        if (offset <= 0x24) {
            /* Only selector5 has a source-bound SPI frequency decoder. */
            s->selector[offset / 4] = value;
            if (offset == 0x14) {
                nikon_spi_clock_rates(s);
            }
            return MEMTX_OK;
        }
        if (offset >= 0x28 && offset <= 0x30) {
            unsigned reg = (offset - 0x28) / 4;
            uint32_t before = s->pll[reg];
            if (value & ~writable[reg]) {
                return nikon_clock_error(s, offset, size, true, value,
                                        "clock-control-fields-not-supported");
            }
            s->pll[reg] = value;
            nikon_pll_update(s, reg, before);
            return MEMTX_OK;
        }
        if (offset == 0x34) {
            /*
             * Original O3908cc..912 performs full-word mask OR/BIC requests.
             * Zero reset and full32 configuration are virtual assumptions;
             * physical fields remain unknown, without invented rate/events.
             */
            s->word_control = value;
            return MEMTX_OK;
        }
        if (offset == 0x58 || offset == 0x5c) {
            /* Original O390914..960 is a mutex-protected word RMW request.
             * Physical roles remain unbound; no rate/ready effect is invented. */
            s->bit_control[(offset - 0x58) / 4] = value;
            return MEMTX_OK;
        }
    }

    if (!set) {
        bank = nikon_clock_bank(offset, 0x238);
    }
    if (size != 4 || bank < 0) {
        return nikon_clock_error(s, offset, size, true, value,
                                "clock-access-not-supported");
    }
    if (set) {
        s->stopped[bank] |= mask;
    } else {
        s->stopped[bank] &= ~mask;
    }
    s->known[bank] |= mask;
    return MEMTX_OK;
}

static const MemoryRegionOps nikon_clock_ops = {
    .read_with_attrs = nikon_clock_read,
    .write_with_attrs = nikon_clock_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
};

static void nikon_clock_reset(DeviceState *dev)
{
    NikonClockGatesState *s = NIKON_CLOCK_GATES(dev);
    unsigned i;

    /* Canonical virtual-board assumptions, not measured hardware reset words. */
    memset(s->stopped, 0, sizeof(s->stopped));
    memset(s->known, 0xff, sizeof(s->known));
    memset(s->selector, 0, sizeof(s->selector));
    nikon_spi_clock_rates(s);
    memset(s->bit_control, 0, sizeof(s->bit_control));
    s->word_control = 0;
    s->pll[0] = 0xffe;
    s->pll[1] = 0x73;
    s->pll[2] = 3;
    for (i = 0; i < ARRAY_SIZE(s->channel); i++) {
        timer_del(s->channel[i].lock_timer);
        s->channel[i].locked = false;
        clock_update_hz(s->channel[i].output, 0);
    }
    s->reported_error = false;
}

static void nikon_clock_init(Object *obj)
{
    NikonClockGatesState *s = NIKON_CLOCK_GATES(obj);
    unsigned i;

    s->spi_clock = qdev_init_clock_out(DEVICE(obj), "spi-clock");
    s->spi8_clock = qdev_init_clock_out(DEVICE(obj), "spi8-clock");
    for (i = 0; i < ARRAY_SIZE(s->channel); i++) {
        g_autofree char *name = g_strdup_printf("pll%u", i);
        s->channel[i].owner = s;
        s->channel[i].index = i;
        s->channel[i].output = qdev_init_clock_out(DEVICE(obj), name);
        s->channel[i].lock_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                               nikon_pll_locked, &s->channel[i]);
    }
    memory_region_init_io(&s->regs, obj, &nikon_clock_ops, s,
                          "nikon.clock-gates", 0x250);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
}

static void nikon_clock_finalize(Object *obj)
{
    NikonClockGatesState *s = NIKON_CLOCK_GATES(obj);
    unsigned i;

    for (i = 0; i < ARRAY_SIZE(s->channel); i++) {
        timer_free(s->channel[i].lock_timer);
    }
}

static const VMStateDescription nikon_clock_vmstate = {
    .name = "nikon-clock",
    .unmigratable = true,
};

static void nikon_clock_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &nikon_clock_vmstate;
    device_class_set_legacy_reset(dc, nikon_clock_reset);
    dc->user_creatable = false;
}

static const TypeInfo nikon_clock_type = {
    .name = TYPE_NIKON_CLOCK_GATES,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonClockGatesState),
    .instance_init = nikon_clock_init,
    .instance_finalize = nikon_clock_finalize,
    .class_init = nikon_clock_class_init,
};

static void nikon_clock_register_types(void)
{
    type_register_static(&nikon_clock_type);
}
type_init(nikon_clock_register_types)
