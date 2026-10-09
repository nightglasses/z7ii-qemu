/*
 * VR RX peripheral protocol recovered from the original loaded application.
 * Register reference: Renesas RX Driver Package RX64M ccrx/iodefine.h.
 * This binds the observed register layout, not the unproved MCU part number.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "qemu/host-utils.h"
#include "qemu/timer.h"
#include "qemu/main-loop.h"
#include "qapi/error.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/nikon/devices.h"
#include "hw/nikon/vr.h"
#include "hw/timer/renesas_cmt.h"
#include "system/address-spaces.h"
#include "system/reset.h"

OBJECT_DECLARE_SIMPLE_TYPE(NikonVRState, NIKON_VR_PERIPHERALS)
#define TYPE_NIKON_VR_SCI_SLAVE "nikon-vr-sci-slave"
OBJECT_DECLARE_SIMPLE_TYPE(NikonVRSlave, NIKON_VR_SCI_SLAVE)

enum VRBankID {
    VR_DMA, VR_DTC, VR_ICU, VR_PORT, VR_SCI1, VR_SCI5, VR_MTU,
    VR_MTU6, VR_ADC, VR_RSPI0, VR_RSPI1, VR_SCIFA10, VR_TPU,
    VR_MTU1, VR_MTU2, VR_RSPI2, VR_BANKS
};

typedef struct VRBank {
    MemoryRegion memory;
    NikonVRState *soc;
    hwaddr base;
    unsigned id;
    uint8_t regs[0x1000];
} VRBank;

typedef struct VRSCI {
    NikonVRState *soc;
    SSIBus *bus;
    QEMUTimer *timer;
    unsigned channel;
    uint8_t smr, brr, scr, tdr, ssr, rdr, scmr, semr, spmr;
    uint8_t mode[0x20];
    bool tdr_full, shifting;
} VRSCI;

typedef struct VRSPI {
    NikonVRState *soc;
    SSIBus *bus;
    QEMUTimer *timer;
    unsigned channel;
    uint32_t tx[4], rx[4];
    unsigned tx_head, tx_count, rx_head, rx_count, batch_remaining;
    bool shifting;
} VRSPI;

typedef struct VRCounterDef {
    unsigned bank, start_bank, start, mask;
    unsigned tcr, tmdr, tier, tsr, tcnt, tgr[4], compares, event;
    bool select_a;
} VRCounterDef;

typedef struct VRCounter {
    NikonVRState *soc;
    const VRCounterDef *def;
    QEMUTimer *timer;
    int64_t epoch;
    uint16_t count;
    bool running, clock;
} VRCounter;

/* MTU1/2/3/4/6/7 and TPU0..5, offsets are relative to their MMIO banks. */
static const VRCounterDef vr_counter_def[] = {
    { VR_MTU1, VR_MTU, 128, 2, 0, 1, 4, 5, 6, { 8, 10 }, 2, 8, true },
    { VR_MTU2, VR_MTU, 128, 4, 0, 1, 4, 5, 6, { 8, 10 }, 2, 12, true },
    { VR_MTU, VR_MTU, 128, 64, 0, 2, 8, 56, 16,
      { 24, 26, 36, 38 }, 4, 16, true },
    { VR_MTU, VR_MTU, 128, 128, 1, 3, 9, 57, 18,
      { 28, 30, 40, 42 }, 4, 21, true },
    { VR_MTU6, VR_MTU6, 128, 64, 0, 2, 8, 56, 16,
      { 24, 26, 36, 38 }, 4, 30, true },
    { VR_MTU6, VR_MTU6, 128, 128, 1, 3, 9, 57, 18,
      { 28, 30, 40, 42 }, 4, 35, true },
    { VR_TPU, VR_TPU, 0, 1, 16, 17, 20, 21, 22,
      { 24, 26, 28, 30 }, 4, 15, false },
    { VR_TPU, VR_TPU, 0, 2, 32, 33, 36, 37, 38,
      { 40, 42 }, 2, 20, false },
    { VR_TPU, VR_TPU, 0, 4, 48, 49, 52, 53, 54,
      { 56, 58 }, 2, 24, false },
    { VR_TPU, VR_TPU, 0, 8, 64, 65, 68, 69, 70,
      { 72, 74, 76, 78 }, 4, 28, false },
    { VR_TPU, VR_TPU, 0, 16, 80, 81, 84, 85, 86,
      { 88, 90 }, 2, 33, false },
    { VR_TPU, VR_TPU, 0, 32, 96, 97, 100, 101, 102,
      { 104, 106 }, 2, 37, false },
};

struct NikonVRState {
    SysBusDevice parent_obj;
    MemoryRegion *memory;
    AddressSpace as;
    VRBank bank[VR_BANKS];
    RCMTState cmt;
    VRSCI sci[3];
    VRSPI spi[3];
    VRCounter counter[ARRAY_SIZE(vr_counter_def)];
    QEMUTimer *adc_timer;
    uint16_t analog[3], analog_connected, adc_valid;
    uint32_t adc_sum[3];
    unsigned adc_remaining, adc_samples;
    uint8_t ir[256], ier[32], ipr[256], dtcer[256], dmrsr[8];
    uint8_t irqcr[16], input[19], connected[19];
    uint16_t fir;
    uint32_t grp[6], gen[6];
    bool source[256];
    int requested;
    uint64_t pclk_hz;
    qemu_irq irq, fastirq;
    qemu_irq output[19 * 8];
    QEMUBH *event_bh;
    bool deferred[256], dtc_active;
    bool dma_active, reported_error, as_initialized;
};

static void vr_request_event(NikonVRState *s, unsigned vector);

static unsigned vr_spi_bank(unsigned channel)
{
    return channel == 2 ? VR_RSPI2 : VR_RSPI0 + channel;
}

static unsigned vr_spi_channel(VRBank *b)
{
    return b->id == VR_RSPI2 ? 2 : b->id - VR_RSPI0;
}

static unsigned vr_sci_channel(VRBank *b)
{
    return b->id == VR_SCIFA10 ? 2 : b->id == VR_SCI5;
}

struct NikonVRSlave {
    SSIPeripheral parent_obj;
    NikonVRState *soc;
};

static unsigned vr_ipr_index(unsigned vector)
{
    switch (vector) {
    case 16: case 18: return 0;
    case 21: return 1;
    case 23: return 2;
    case 26: case 27: return 3;
    case 28 ... 31: return vector - 24;
    default: return vector;
    }
}

/* ICUb has 256 independent IPR entries; upstream ICUa stores only 142. */
static void vr_icu_update(NikonVRState *s)
{
    unsigned n, priority = 0;
    int winner = -1;

    for (n = 16; n < 256; n++) {
        unsigned p = s->ipr[vr_ipr_index(n)] & 15;

        if (s->ir[n] && (s->ier[n / 8] & (1U << (n & 7))) && p > priority) {
            winner = n;
            priority = p;
        }
    }
    s->requested = winner;
    qemu_set_irq(s->irq, 0);
    qemu_set_irq(s->fastirq, 0);
    if (winner >= 0) {
        qemu_irq line = (s->fir & 0x8000) &&
                        (s->fir & 255) == winner ? s->fastirq : s->irq;

        qemu_set_irq(line, (priority << 8) | winner);
    }
}

static void vr_ack(void *opaque, int n, int level)
{
    NikonVRState *s = opaque;

    if (level >= 16 && level < 256 && !s->source[level]) {
        s->ir[level] = 0;
    }
    vr_icu_update(s);
}

static void vr_irq_input(void *opaque, int n, int level)
{
    NikonVRState *s = opaque;

    /* CMT and the public inputs are edge events, group levels are internal. */
    if (level && !s->source[n]) {
        vr_request_event(s, n);
    }
    s->source[n] = !!level;
    vr_icu_update(s);
}

static void vr_group_update(NikonVRState *s, unsigned group)
{
    static const unsigned vector[] = { 106, 110, 111, 112, 113, 107 };
    unsigned v = vector[group];

    s->source[v] = !!(s->grp[group] & s->gen[group]);
    s->ir[v] = s->source[v];
    vr_icu_update(s);
}

static void vr_sci_group(NikonVRState *s, unsigned channel)
{
    VRSCI *sci = &s->sci[channel];
    unsigned shift = channel == 2 ? 8 : channel ? 10 : 2;
    unsigned group = channel == 2 ? 3 : 1;
    uint32_t mask = 3U << shift;
    uint32_t state = ((sci->ssr & 4) && (sci->scr & 4) ? 1 : 0) |
                     ((sci->ssr & 0x38) && (sci->scr & 0x40) ? 2 : 0);

    s->grp[group] = (s->grp[group] & ~mask) | (state << shift);
    vr_group_update(s, group);
}

static MemTxResult vr_error(VRBank *b, hwaddr offset, unsigned size,
                            bool write, uint64_t value)
{
    nikon_access_error(&b->soc->reported_error, b->base + offset,
                       size, write, value, "VR peripheral register or phase");
    return MEMTX_ERROR;
}

static uint32_t vr_load(const uint8_t *p, unsigned size)
{
    switch (size) {
    case 1: return *p;
    case 2: return lduw_le_p(p);
    case 4: return ldl_le_p(p);
    default: g_assert_not_reached();
    }
}

static void vr_store(uint8_t *p, uint32_t value, unsigned size)
{
    switch (size) {
    case 1: *p = value; break;
    case 2: stw_le_p(p, value); break;
    case 4: stl_le_p(p, value); break;
    default: g_assert_not_reached();
    }
}

static bool vr_dma_reg(hwaddr offset, unsigned size)
{
    unsigned r = offset & 0x3f;

    if (offset == 0x200 || offset == 0x204) {
        return size == 1;
    }
    if (offset >= 0x200) {
        return false;
    }
    switch (r) {
    case 0: case 4: case 8: case 24: return size == 4;
    case 12: case 16: case 20: return size == 2;
    case 19: case 28: case 29: case 30: case 31: return size == 1;
    default: return false;
    }
}

static unsigned vr_dma_step(uint32_t address, unsigned mode, unsigned size)
{
    switch (mode) {
    case 0: return address;
    case 1: return address - size;
    case 2: return address + size;
    default: g_assert_not_reached();
    }
}

static void vr_dma_complete(NikonVRState *s, unsigned channel, bool error)
{
    uint8_t *r = s->bank[VR_DMA].regs + channel * 0x40;

    r[28] &= ~1;
    r[30] &= ~0x80;
    r[30] |= error ? 1 : 0x10;
    if (r[19] & (error ? 8 : 0x10)) {
        s->ir[channel < 4 ? 120 + channel : 124] = 1;
        vr_icu_update(s);
    }
}

static bool vr_dma_request(NikonVRState *s, unsigned vector)
{
    unsigned channel;
    bool taken = false;

    if (s->dma_active || !(s->bank[VR_DMA].regs[0x200] & 1)) {
        return false;
    }
    s->dma_active = true;
    for (channel = 0; channel < 8; channel++) {
        uint8_t *r = s->bank[VR_DMA].regs + channel * 0x40;
        uint16_t mode = lduw_le_p(r + 16), address_mode = lduw_le_p(r + 20);
        unsigned size = 1U << ((mode >> 8) & 3);
        unsigned sm = address_mode >> 14, dm = (address_mode >> 6) & 3;
        uint32_t source, dest, count;
        uint8_t value[4];
        MemTxResult result;

        if (!(r[28] & 1) || (mode & 3) != 1 || s->dmrsr[channel] != vector) {
            continue;
        }
        taken = true;
        /* Normal transfer mode is the original SCI and RSPI contract. */
        if ((mode & 0xc000) || size > 4 || sm == 3 || dm == 3) {
            vr_error(&s->bank[VR_DMA], channel * 0x40 + 16, 2, true, mode);
            vr_dma_complete(s, channel, true);
            continue;
        }
        source = ldl_le_p(r);
        dest = ldl_le_p(r + 4);
        count = ldl_le_p(r + 8) & 0xffff;
        if (!count) {
            count = 0x10000;
        }
        r[30] |= 0x80;
        result = address_space_read(&s->as, source, MEMTXATTRS_UNSPECIFIED,
                                    value, size);
        if (result == MEMTX_OK) {
            result = address_space_write(&s->as, dest,
                                         MEMTXATTRS_UNSPECIFIED, value, size);
        }
        if (result != MEMTX_OK) {
            vr_dma_complete(s, channel, true);
            continue;
        }
        stl_le_p(r, vr_dma_step(source, sm, size));
        stl_le_p(r + 4, vr_dma_step(dest, dm, size));
        stl_le_p(r + 8, (ldl_le_p(r + 8) & 0xffff0000) | ((count - 1) & 0xffff));
        r[30] &= ~0x80;
        if (count == 1) {
            vr_dma_complete(s, channel, false);
        }
    }
    s->dma_active = false;
    return taken;
}

static bool vr_dtc_request(NikonVRState *s, unsigned vector)
{
    VRBank *b = &s->bank[VR_DTC];
    uint8_t pointer[4], descriptor[16], data[4];
    uint32_t address;
    bool interrupt = false;
    unsigned chain;
    MemTxResult result;

    if (!(b->regs[12] & 1) || !s->dtcer[vector] ||
        !(s->ier[vector / 8] & (1U << (vector & 7)))) {
        return false;
    }
    if (s->ir[vector]) {
        return true; /* ICU discards reactivation while IR is pending. */
    }
    if (b->regs[8] & 1) {
        vr_error(b, 8, 1, false, b->regs[8]);
        return true; /* Original descriptors use full address mode. */
    }
    result = address_space_read(&s->as, ldl_le_p(b->regs + 4) + vector * 4,
                                MEMTXATTRS_UNSPECIFIED, pointer, 4);
    if (result != MEMTX_OK) {
        vr_error(b, 4, 4, false, ldl_le_p(b->regs + 4));
        return true;
    }
    address = ldl_le_p(pointer);
    s->dtc_active = true;
    stw_le_p(b->regs + 14, 0x8000 | vector);
    for (chain = 0; chain < 256; chain++, address += sizeof(descriptor)) {
        unsigned mra, mrb, mode, size, sm, dm, count, initial;
        uint32_t sar, dar;
        bool last, writeback;

        result = address_space_read(&s->as, address, MEMTXATTRS_UNSPECIFIED,
                                    descriptor, sizeof(descriptor));
        if (result != MEMTX_OK) {
            break;
        }
        /* RX little-endian full-address descriptors: MRA/MRB at bytes 3/2. */
        mra = descriptor[3];
        mrb = descriptor[2];
        mode = mra >> 6;
        size = 1U << ((mra >> 4) & 3);
        sm = (mra >> 2) & 3;
        dm = (mrb >> 2) & 3;
        writeback = !(mra & 1);
        if (mode > 1 || size > 4 || sm == 3 || dm == 3 || (mrb & 3)) {
            result = MEMTX_ERROR;
            break;
        }
        sar = ldl_le_p(descriptor + 4);
        dar = ldl_le_p(descriptor + 8);
        initial = lduw_le_p(descriptor + 14);
        count = mode == 1 ? initial & 255 : initial;
        if (!count) {
            count = mode == 1 ? 256 : 65536;
        }
        result = address_space_read(&s->as, sar, MEMTXATTRS_UNSPECIFIED,
                                    data, size);
        if (result == MEMTX_OK) {
            result = address_space_write(&s->as, dar, MEMTXATTRS_UNSPECIFIED,
                                         data, size);
        }
        if (result != MEMTX_OK) {
            break;
        }
        sar = vr_dma_step(sar, sm, size);
        dar = vr_dma_step(dar, dm, size);
        last = count == 1;
        count--;
        if (mode == 1 && last) {
            unsigned repeat = initial >> 8;
            unsigned repeats = repeat ? repeat : 256;

            count = repeat;
            if (mrb & 0x10) {
                if (sm == 1) {
                    sar += repeats * size;
                } else if (sm == 2) {
                    sar -= repeats * size;
                }
            } else if (dm == 1) {
                dar += repeats * size;
            } else if (dm == 2) {
                dar -= repeats * size;
            }
        }
        if (writeback) {
            stl_le_p(descriptor + 4, sar);
            stl_le_p(descriptor + 8, dar);
            stw_le_p(descriptor + 14, mode == 1 ?
                     (initial & 0xff00) | count : count);
            result = address_space_write(&s->as, address,
                                         MEMTXATTRS_UNSPECIFIED, descriptor,
                                         sizeof(descriptor));
            if (result != MEMTX_OK) {
                break;
            }
        }
        interrupt = !!(mrb & 0x20) || (last && writeback && !mode);
        if (!(mrb & 0x80) || ((mrb & 0x40) && !last)) {
            if (last && writeback && !mode) {
                s->dtcer[vector] = 0;
            }
            break;
        }
    }
    s->dtc_active = false;
    stw_le_p(b->regs + 14, vector);
    if (result != MEMTX_OK || chain == 256) {
        vr_error(b, 4, 4, false, address);
        return true;
    }
    return !interrupt;
}

static void vr_request_event(NikonVRState *s, unsigned vector)
{
    if (s->dma_active || s->dtc_active) {
        s->deferred[vector] = true;
        qemu_bh_schedule(s->event_bh);
        return;
    }
    if (!vr_dma_request(s, vector) && !vr_dtc_request(s, vector)) {
        s->ir[vector] = 1;
    }
    vr_icu_update(s);
}

static void vr_deferred_events(void *opaque)
{
    NikonVRState *s = opaque;
    unsigned vector;

    for (vector = 0; vector < 256; vector++) {
        if (s->deferred[vector]) {
            s->deferred[vector] = false;
            vr_request_event(s, vector);
        }
    }
}

static void vr_sci_event(NikonVRState *s, unsigned channel, bool transmit)
{
    VRSCI *sci = &s->sci[channel];
    unsigned vector = (channel == 2 ? 104 : channel ? 84 : 60) + transmit;
    unsigned enable = transmit ? 0x80 : 0x40;

    if (sci->scr & enable) {
        vr_request_event(s, vector);
    }
}

static void vr_selected_events(NikonVRState *s, bool select_a, unsigned event,
                               uint64_t count)
{
    unsigned first = select_a ? 208 : 128;
    unsigned last = select_a ? 256 : 208;
    unsigned base = select_a ? 0x9d0 : 0x780;
    unsigned vector;

    for (vector = first; vector < last; vector++) {
        if (s->bank[VR_ICU].regs[base + vector - first] == event) {
            uint64_t n;

            for (n = 0; n < count && !s->ir[vector]; n++) {
                vr_request_event(s, vector);
            }
        }
    }
    vr_icu_update(s);
}

static void vr_selected_event(NikonVRState *s, bool select_a, unsigned event)
{
    vr_selected_events(s, select_a, event, 1);
}

static unsigned vr_counter_period(VRCounter *c)
{
    const VRCounterDef *d = c->def;
    uint8_t *r = c->soc->bank[d->bank].regs;
    unsigned clear = r[d->tcr] >> 5;
    unsigned compare = clear == 1 ? 0 : clear == 2 ? 1 :
                       clear == 5 ? 2 : clear == 6 ? 3 : 4;

    return compare < d->compares ?
           MAX(1, lduw_le_p(r + d->tgr[compare])) : 65536;
}

static unsigned vr_counter_divisor(VRCounter *c)
{
    const VRCounterDef *d = c->def;
    uint8_t *r = c->soc->bank[d->bank].regs;
    unsigned prescale = r[d->tcr] & 7;
    unsigned extended = d->bank == VR_MTU1 ? r[20] & 7 :
                        d->bank == VR_MTU2 ? r[12] & 7 :
                        d->bank == VR_TPU ? 0 : r[76 + d->tcr] & 7;

    /* MTU3a TCR2 tables: nonzero TPSC2 overrides TCR.TPSC. */
    if (extended && extended <= 3) {
        return 2U << ((extended - 1) * 2);
    }
    if (extended == 4 && d->bank == VR_MTU2) {
        return 256;
    }
    if (extended) {
        vr_error(&c->soc->bank[d->bank], d->tcr, 1, false, r[d->tcr]);
        return 0;
    }
    if (prescale < 4) {
        return 1U << (prescale * 2);
    }
    if ((d->bank == VR_MTU || d->bank == VR_MTU6) && prescale <= 5) {
        return prescale == 4 ? 256 : 1024;
    }
    if (d->bank == VR_MTU1 && prescale == 6) {
        return 256;
    }
    if (d->bank == VR_MTU2 && prescale == 7) {
        return 1024;
    }
    return 0; /* Clock pin or cascade, supplied by an actual producer. */
}

static void vr_counter_advance(VRCounter *c, uint64_t ticks)
{
    const VRCounterDef *d = c->def;
    NikonVRState *s = c->soc;
    uint8_t *r = s->bank[d->bank].regs;
    unsigned period = vr_counter_period(c), i;

    for (i = 0; i < d->compares; i++) {
        unsigned target = lduw_le_p(r + d->tgr[i]);
        unsigned distance = (target + period - c->count % period) % period;

        if (!distance) {
            distance = period;
        }
        if (target <= period && ticks >= distance) {
            r[d->tsr] |= 1U << i;
            if (r[d->tier] & (1U << i)) {
                vr_selected_events(s, d->select_a, d->event + i,
                                   1 + (ticks - distance) / period);
            }
        }
    }
    if (period == 65536 && ticks >= 65536U - c->count) {
        r[d->tsr] |= 0x10;
        if (r[d->tier] & 0x10) {
            vr_selected_events(s, d->select_a, d->event + d->compares,
                               1 + (ticks - (65536U - c->count)) / 65536);
        }
    }
    c->count = (c->count + ticks) % period;
    stw_le_p(r + d->tcnt, c->count);
}

static void vr_counter_sync(VRCounter *c)
{
    unsigned divisor = vr_counter_divisor(c);
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    uint64_t ticks;

    if (c->running && divisor) {
        ticks = muldiv64(now - c->epoch, c->soc->pclk_hz,
                         (uint64_t)NANOSECONDS_PER_SECOND * divisor);
        if (ticks) {
            c->epoch += muldiv64(ticks,
                                (uint64_t)NANOSECONDS_PER_SECOND * divisor,
                                c->soc->pclk_hz);
            vr_counter_advance(c, ticks);
        }
    }
}

static void vr_counter_schedule(VRCounter *c)
{
    const VRCounterDef *d = c->def;
    NikonVRState *s = c->soc;
    uint8_t *r = s->bank[d->bank].regs;
    unsigned period = vr_counter_period(c), distance = period, i;
    unsigned divisor = vr_counter_divisor(c);
    bool running = !!(s->bank[d->start_bank].regs[d->start] & d->mask);

    timer_del(c->timer);
    if (running != c->running) {
        c->epoch = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        c->running = running;
    }
    if (!running || !divisor) {
        return;
    }
    for (i = 0; i < d->compares; i++) {
        unsigned target = lduw_le_p(r + d->tgr[i]);
        unsigned delta = (target + period - c->count % period) % period;

        if (target <= period) {
            distance = MIN(distance, delta ? delta : period);
        }
    }
    if (period == 65536) {
        distance = MIN(distance, 65536U - c->count);
    }
    timer_mod(c->timer, c->epoch +
              MAX(1, DIV_ROUND_UP((uint64_t)distance * NANOSECONDS_PER_SECOND *
                                 divisor, s->pclk_hz)));
}

static void vr_counter_expire(void *opaque)
{
    VRCounter *c = opaque;

    vr_counter_sync(c);
    vr_counter_schedule(c);
}

static void vr_counter_clock(void *opaque, int n, int level)
{
    NikonVRState *s = opaque;
    VRCounter *c = &s->counter[n];
    uint8_t *r = s->bank[c->def->bank].regs;
    unsigned edge = (r[c->def->tcr] >> 3) & 3;
    bool fire = edge == 0 ? !c->clock && level :
                edge == 1 ? c->clock && !level : c->clock != !!level;

    if (c->running && !vr_counter_divisor(c) && fire) {
        vr_counter_advance(c, 1);
    }
    c->clock = !!level;
}

static void vr_counters_sync(NikonVRState *s, unsigned bank)
{
    unsigned i;

    for (i = 0; i < ARRAY_SIZE(s->counter); i++) {
        VRCounter *c = &s->counter[i];

        if (c->def->bank == bank || c->def->start_bank == bank) {
            vr_counter_sync(c);
        }
    }
}

static void vr_counters_written(NikonVRState *s, VRBank *b)
{
    unsigned i;

    for (i = 0; i < ARRAY_SIZE(s->counter); i++) {
        VRCounter *c = &s->counter[i];

        if (c->def->bank == b->id) {
            c->count = lduw_le_p(b->regs + c->def->tcnt);
            c->epoch = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        }
        if (c->def->bank == b->id || c->def->start_bank == b->id) {
            vr_counter_schedule(c);
        }
    }
}

static bool vr_counter_status(NikonVRState *s, VRBank *b, hwaddr offset)
{
    unsigned i;

    for (i = 0; i < ARRAY_SIZE(s->counter); i++) {
        if (s->counter[i].def->bank == b->id &&
            s->counter[i].def->tsr == offset) {
            return true;
        }
    }
    return false;
}

static void vr_adc_schedule(NikonVRState *s)
{
    uint8_t *r = s->bank[VR_ADC].regs;
    unsigned selected = lduw_le_p(r + 4) & 7;

    timer_del(s->adc_timer);
    if (!(lduw_le_p(r) & 0x8000)) {
        s->adc_remaining = 0;
        return;
    }
    if (!s->adc_remaining) {
        unsigned addition = r[12] & 7;

        /* The original ADADC=5 selects sixteen samples (S12AD extension). */
        if (addition > 3 && addition != 5) {
            vr_error(&s->bank[VR_ADC], 12, 1, true, r[12]);
            return;
        }
        s->adc_samples = addition == 5 ? 16 : addition + 1;
        s->adc_remaining = s->adc_samples;
        memset(s->adc_sum, 0, sizeof(s->adc_sum));
    }
    if ((lduw_le_p(r) & 0x8000) && selected &&
        (s->analog_connected & selected) == selected) {
        /* Named functional settling interval, no measured silicon latency. */
        timer_mod(s->adc_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000);
    }
}

static void vr_adc_complete(void *opaque)
{
    NikonVRState *s = opaque;
    uint8_t *r = s->bank[VR_ADC].regs;
    unsigned selected = lduw_le_p(r + 4) & 7, i;
    uint16_t csr = lduw_le_p(r);

    if (!(csr & 0x8000) || (s->analog_connected & selected) != selected) {
        return;
    }
    for (i = 0; i < 3; i++) {
        if (selected & (1U << i)) {
            s->adc_sum[i] += s->analog[i];
        }
    }
    if (--s->adc_remaining) {
        vr_adc_schedule(s);
        return;
    }
    for (i = 0; i < 3; i++) {
        if (selected & (1U << i)) {
            bool addition = !!(lduw_le_p(r + 8) & (1U << i));
            uint16_t data = !addition ? s->analog[i] :
                            !(r[12] & 0x80) ? s->adc_sum[i] :
                            s->adc_sum[i] / s->adc_samples;

            if (lduw_le_p(r + 14) & 0x8000) {
                data <<= 4;
            }
            stw_le_p(r + 32 + i * 2, data);
            s->adc_valid |= 1U << i;
        }
    }
    stw_le_p(r, csr & ~0x8000);
    if (csr & 0x1000) {
        vr_selected_event(s, false, 64);
    }
    if (((csr >> 13) & 3) == 2) {
        stw_le_p(r, csr);
        vr_adc_schedule(s);
    }
}

static void vr_analog_input(void *opaque, int n, int level)
{
    NikonVRState *s = opaque;

    if (level < 0 || level > 4095) {
        s->analog_connected &= ~(1U << n);
    } else {
        s->analog[n] = level;
        s->analog_connected |= 1U << n;
    }
    vr_adc_schedule(s);
}

static void vr_sci_tx_ready(NikonVRState *s, unsigned channel)
{
    VRSCI *sci = &s->sci[channel];

    if ((sci->scr & 0x20) && !sci->tdr_full) {
        sci->ssr |= 0x80;
        vr_sci_event(s, channel, true);
    }
}

static void vr_sci_schedule(VRSCI *sci)
{
    uint64_t cycles;

    if (!(sci->smr & 0x80) || (sci->scr & 2) || !(sci->scr & 0x20) ||
        !sci->tdr_full || sci->shifting) {
        return;
    }
    cycles = 8 * 4 * (sci->brr + 1) * (1U << ((sci->smr & 3) * 2));
    sci->shifting = true;
    timer_mod(sci->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
              MAX(1, muldiv64(cycles, NANOSECONDS_PER_SECOND,
                             sci->soc->pclk_hz)));
}

static uint32_t vr_sci_frame(VRSCI *sci, uint32_t value, bool receive)
{
    NikonVRState *s = sci->soc;
    uint32_t output = 0;

    if ((sci->scr & 0x20) && sci->tdr_full) {
        output = sci->tdr;
        sci->tdr_full = false;
        sci->ssr &= ~4;
    }
    if (receive && (sci->scr & 0x10)) {
        if (sci->ssr & 0x40) {
            sci->ssr |= 0x20;
        } else {
            sci->rdr = value & 255;
            sci->ssr |= 0x40;
            vr_sci_event(s, sci->channel, false);
        }
    }
    vr_sci_tx_ready(s, sci->channel);
    if (!sci->tdr_full) {
        sci->ssr |= 4;
    }
    vr_sci_group(s, sci->channel);
    vr_sci_schedule(sci);
    return output;
}

static void vr_sci_master_shift(void *opaque)
{
    VRSCI *sci = opaque;
    bool connected = !QTAILQ_EMPTY(&BUS(sci->bus)->children);
    uint32_t value;

    sci->shifting = false;
    if (!(sci->scr & 0x20) || !sci->tdr_full || (sci->scr & 2)) {
        return;
    }
    value = ssi_transfer(sci->bus, sci->tdr);
    /* An unconnected synchronous input does not supply a peer's byte. */
    vr_sci_frame(sci, value, connected);
}

static void vr_spi_schedule(VRSPI *spi);

static void vr_spi_group(VRSPI *spi)
{
    NikonVRState *s = spi->soc;
    uint8_t *r = s->bank[vr_spi_bank(spi->channel)].regs;
    unsigned shift = 16 + spi->channel * 2;
    uint32_t state = ((r[15] & 4) && !spi->tx_count && !spi->shifting ? 1 : 0) |
                     ((r[0] & 2) && (r[3] & 0x1d) ? 2 : 0);

    s->grp[3] = (s->grp[3] & ~(3U << shift)) | (state << shift);
    vr_group_update(s, 3);
}

static void vr_spi_event(VRSPI *spi, bool transmit)
{
    NikonVRState *s = spi->soc;
    uint8_t *r = s->bank[vr_spi_bank(spi->channel)].regs;
    unsigned vector = (spi->channel == 2 ? 108 : 38 + spi->channel * 2) +
                      transmit;

    if (r[0] & (transmit ? 0x20 : 0x80)) {
        vr_request_event(s, vector);
    }
}

static void vr_spi_ready(VRSPI *spi)
{
    uint8_t *r = spi->soc->bank[vr_spi_bank(spi->channel)].regs;

    if (r[3] & 0x20) {
        if ((r[0] & 0x48) == 0x48) {
            vr_spi_event(spi, true);
        }
    }
    vr_spi_schedule(spi);
    vr_spi_group(spi);
}

static unsigned vr_spi_bits(uint16_t command)
{
    unsigned spb = (command >> 8) & 15;

    /* RX RSPI SPB: 7..15 encode 8..16, 4/5/6 encode 20/24/32. */
    return spb >= 7 ? spb + 1 : spb == 4 ? 20 : spb == 5 ? 24 :
           spb == 6 ? 32 : 0;
}

static void vr_spi_shift(void *opaque)
{
    VRSPI *spi = opaque;
    NikonVRState *s = spi->soc;
    VRBank *b = &s->bank[vr_spi_bank(spi->channel)];
    unsigned command_index = b->regs[9] & 7;
    unsigned bits = vr_spi_bits(lduw_le_p(b->regs + 16 + command_index * 2));
    uint32_t mask = bits == 32 ? UINT32_MAX : (1U << bits) - 1;
    uint32_t received;

    spi->shifting = false;
    if ((b->regs[0] & 0x48) != 0x48 || !spi->tx_count) {
        return;
    }
    /* Unattached upstream SSI has functional MISO idle=0, no peer reply. */
    received = ssi_transfer(spi->bus, spi->tx[spi->tx_head] & mask) & mask;
    if (spi->rx_count == (b->regs[11] & 3) + 1U) {
        b->regs[3] |= 1;
    } else {
        spi->rx[(spi->rx_head + spi->rx_count) & 3] =
            received;
        spi->rx_count++;
        if (spi->rx_count == (b->regs[11] & 3) + 1U) {
            b->regs[3] |= 0x80;
        }
    }
    spi->tx_head = (spi->tx_head + 1) & 3;
    spi->tx_count--;
    if (!--spi->batch_remaining) {
        b->regs[3] |= 0x60;
    }
    b->regs[9] = (command_index + 1) % ((b->regs[8] & 7) + 1);
    if (b->regs[3] & 0x80) {
        vr_spi_event(spi, false);
    }
    vr_spi_ready(spi);
}

static void vr_spi_schedule(VRSPI *spi)
{
    NikonVRState *s = spi->soc;
    VRBank *b = &s->bank[vr_spi_bank(spi->channel)];
    uint16_t command = lduw_le_p(b->regs + 16 + (b->regs[9] & 7) * 2);
    unsigned bits = vr_spi_bits(command);
    uint64_t cycles;

    if ((b->regs[0] & 0x48) != 0x48 || !spi->tx_count || spi->shifting) {
        return;
    }
    if (!spi->batch_remaining) {
        if (spi->tx_count < (b->regs[11] & 3) + 1U) {
            return;
        }
        spi->batch_remaining = spi->tx_count;
    }
    if (!bits) {
        vr_error(b, 16, 2, true, command);
        return;
    }
    cycles = bits * 2 * (b->regs[10] + 1) * (1U << ((command >> 2) & 3));
    spi->shifting = true;
    b->regs[3] &= ~0x40;
    timer_mod(spi->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
              MAX(1, muldiv64(cycles, NANOSECONDS_PER_SECOND, s->pclk_hz)));
}

static uint32_t vr_sci_clock(NikonVRState *s, uint32_t value)
{
    VRSCI *sci = &s->sci[1];

    /* External clock frames are accepted only in the configured 8-bit mode. */
    if (!(sci->smr & 0x80) || (sci->scr & 3) != 2) {
        vr_error(&s->bank[VR_SCI5], 2, 1, false, 0);
        return 0;
    }
    return vr_sci_frame(sci, value, true);
}

static bool vr_port_valid(hwaddr offset)
{
    static const uint8_t pfs_pin[] = {
        0xaf, 0xff, 0xff, 0x1f, 0xff, 0x77, 0xc1, 0xfe,
        0xcf, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x27,
        0xff, 0x00, 0x28,
    };
    unsigned port = offset & 31;

    if (offset < 0x80 || (offset >= 0xc0 && offset < 0x100)) {
        return port <= 0x12 && port != 0x11;
    }
    if (offset >= 0x80 && offset < 0xa6) {
        return (offset - 0x80) / 2 != 0x11;
    }
    /* MPC PWPR and pin function selection P00PFS through PJ7PFS. */
    if (offset >= 0x140 && offset < 0x1d8) {
        return pfs_pin[(offset - 0x140) / 8] & (1U << (offset & 7));
    }
    return offset == 0x11f;
}

static uint8_t vr_pad(NikonVRState *s, unsigned port)
{
    uint8_t *r = s->bank[VR_PORT].regs;
    uint8_t gpio_output = r[port] & ~r[0x60 + port];
    /* An unconnected input is low; PCR pulls it up, a real input overrides it. */
    uint8_t input = (s->input[port] & s->connected[port]) |
                    (r[0xc0 + port] & ~s->connected[port]);

    return (r[0x20 + port] & gpio_output) | (input & ~gpio_output);
}

static void vr_pad_input(void *opaque, int pin, int level)
{
    NikonVRState *s = opaque;
    unsigned port = pin / 8, bit = pin & 7;
    bool old = !!(vr_pad(s, port) & (1U << bit));
    int irq = port == 1 && bit == 7 ? 7 :
              port == 4 && bit == 7 ? 15 : -1;

    s->connected[port] |= 1U << bit;
    s->input[port] = (s->input[port] & ~(1U << bit)) | (!!level << bit);
    /* Source-configured P17/IRQ7 and SCI5 select P47/IRQ15. */
    if (irq >= 0 && (s->bank[VR_PORT].regs[0x140 + pin] & 0x40)) {
        unsigned mode = (s->irqcr[irq] >> 2) & 3;
        unsigned vector = 64 + irq;
        bool now = !!(vr_pad(s, port) & (1U << bit));
        bool fire = mode == 0 ? !now :
                    mode == 1 ? old && !now :
                    mode == 2 ? !old && now : old != now;

        if (mode == 0) {
            s->source[vector] = !now;
            s->ir[vector] = !now;
        } else if (fire) {
            vr_request_event(s, vector);
        }
        vr_icu_update(s);
    }
}

static void vr_port_update(NikonVRState *s)
{
    unsigned p, b;

    for (p = 0; p < 19; p++) {
        uint8_t v = vr_pad(s, p);

        for (b = 0; b < 8; b++) {
            qemu_set_irq(s->output[p * 8 + b], (v >> b) & 1);
        }
    }
}

static int vr_group_register(hwaddr offset, bool *enable)
{
    static const unsigned address[] = {
        0x600, 0x630, 0x634, 0x830, 0x834, 0x638,
    };
    unsigned i;

    for (i = 0; i < ARRAY_SIZE(address); i++) {
        if (offset == address[i] || offset == address[i] + 0x40) {
            *enable = offset != address[i];
            return i;
        }
    }
    return -1;
}

static MemTxResult vr_icu_read(VRBank *b, hwaddr offset, uint64_t *value,
                              unsigned size)
{
    NikonVRState *s = b->soc;
    bool enable;
    int group = vr_group_register(offset, &enable);

    if (group >= 0 && size == 4) {
        *value = enable ? s->gen[group] : s->grp[group];
    } else if (offset < 0x100 && size == 1) {
        *value = s->ir[offset];
    } else if (offset < 0x200 && size == 1) {
        *value = s->dtcer[offset - 0x100];
    } else if (offset >= 0x200 && offset < 0x220 && size == 1) {
        *value = s->ier[offset - 0x200];
    } else if (offset >= 0x300 && offset < 0x400 && size == 1) {
        *value = s->ipr[offset - 0x300];
    } else if (offset >= 0x400 && offset <= 0x41c && !(offset & 3) && size == 1) {
        *value = s->dmrsr[(offset - 0x400) / 4];
    } else if (offset >= 0x500 && offset < 0x510 && size == 1) {
        *value = s->irqcr[offset - 0x500];
    } else if (offset == 0x2f0 && size == 2) {
        *value = s->fir;
    } else if ((offset == 0x2e0 || offset == 0x2e1) && size == 1) {
        *value = 0;
    } else if (((offset >= 0x780 && offset < 0x7d0) ||
                (offset >= 0x9d0 && offset < 0xa00) ||
                offset == 0xa00 || offset == 0xa01 ||
                (offset >= 0x580 && offset < 0x584) ||
                (offset >= 0x520 && offset < 0x524)) && size == 1) {
        *value = b->regs[offset];
    } else if ((offset == 0x510 || offset == 0x512 ||
                offset == 0x528 || offset == 0x52a) && size == 2) {
        *value = lduw_le_p(b->regs + offset);
    } else {
        return vr_error(b, offset, size, false, 0);
    }
    return MEMTX_OK;
}

static MemTxResult vr_icu_write(VRBank *b, hwaddr offset, uint64_t value,
                               unsigned size)
{
    NikonVRState *s = b->soc;
    bool enable;
    int group = vr_group_register(offset, &enable);

    if (group >= 0 && size == 4 && enable) {
        s->gen[group] = value;
        vr_group_update(s, group);
    } else if (offset < 0x100 && size == 1) {
        if (!(value & 1) && !s->source[offset]) {
            s->ir[offset] = 0;
        }
    } else if (offset < 0x200 && size == 1) {
        s->dtcer[offset - 0x100] = value & 1;
    } else if (offset >= 0x200 && offset < 0x220 && size == 1) {
        s->ier[offset - 0x200] = value;
    } else if (offset >= 0x300 && offset < 0x400 && size == 1) {
        s->ipr[offset - 0x300] = value & 15;
    } else if (offset >= 0x400 && offset <= 0x41c && !(offset & 3) && size == 1) {
        s->dmrsr[(offset - 0x400) / 4] = value;
    } else if (offset >= 0x500 && offset < 0x510 && size == 1) {
        s->irqcr[offset - 0x500] = value & 12;
    } else if (offset == 0x2f0 && size == 2) {
        s->fir = value & 0x80ff;
    } else if ((offset == 0x2e0 || offset == 0x2e1) && size == 1) {
        if (value & 1) {
            s->ir[offset == 0x2e0 ? 27 : 26] = 1;
        }
    } else if (((offset >= 0x780 && offset < 0x7d0) ||
                (offset >= 0x9d0 && offset < 0xa00)) && size == 1) {
        if (!(b->regs[0xa00] & 1)) {
            b->regs[offset] = value;
        }
    } else if ((offset == 0xa00 || offset == 0xa01) && size == 1) {
        b->regs[offset] = value & (offset == 0xa00 ? 1 : 3);
    } else if (((offset >= 0x580 && offset < 0x584) ||
                (offset >= 0x520 && offset < 0x524)) && size == 1) {
        b->regs[offset] = value;
    } else if ((offset == 0x510 || offset == 0x512 ||
                offset == 0x528 || offset == 0x52a) && size == 2) {
        stw_le_p(b->regs + offset, value);
    } else {
        return vr_error(b, offset, size, true, value);
    }
    vr_icu_update(s);
    return MEMTX_OK;
}

static MemTxResult vr_sci_read(VRBank *b, hwaddr offset, uint64_t *value,
                              unsigned size)
{
    NikonVRState *s = b->soc;
    unsigned channel = vr_sci_channel(b);
    VRSCI *sci = &s->sci[channel];

    if (size != 1) {
        return vr_error(b, offset, size, false, 0);
    }
    switch (offset) {
    case 0: *value = sci->smr; break;
    case 1: *value = sci->brr; break;
    case 2: *value = sci->scr; break;
    case 3: *value = sci->tdr; break;
    case 4: *value = sci->ssr; break;
    case 5:
        *value = sci->rdr;
        sci->ssr &= ~0x40;
        break;
    case 6: *value = sci->scmr; break;
    case 7: *value = sci->semr; break;
    case 13: *value = sci->spmr; break;
    case 28:
        if (channel != 2) {
            return vr_error(b, offset, size, false, 0);
        }
        *value = sci->mode[offset];
        break;
    default: return vr_error(b, offset, size, false, 0);
    }
    return MEMTX_OK;
}

static MemTxResult vr_sci_write(VRBank *b, hwaddr offset, uint64_t value,
                               unsigned size)
{
    NikonVRState *s = b->soc;
    unsigned channel = vr_sci_channel(b);
    VRSCI *sci = &s->sci[channel];

    if (size != 1) {
        return vr_error(b, offset, size, true, value);
    }
    switch (offset) {
    case 0: sci->smr = value; break;
    case 1: sci->brr = value; break;
    case 2:
        sci->scr = value;
        if (!(value & 0x20)) {
            timer_del(sci->timer);
            sci->shifting = false;
            sci->tdr_full = false;
            sci->ssr |= 0x84;
        }
        vr_sci_tx_ready(s, channel);
        break;
    case 3:
        sci->tdr = value;
        sci->tdr_full = true;
        sci->ssr &= ~0x84;
        break;
    case 4:
        /* Error and RDRF are cleared by the original read/clear sequence. */
        sci->ssr = (sci->ssr & 0x84) | (sci->ssr & value & 0x78) | (value & 1);
        break;
    case 6: sci->scmr = value; break;
    case 7: sci->semr = value; break;
    case 13: sci->spmr = value; break;
    case 28:
        if (channel != 2) {
            return vr_error(b, offset, size, true, value);
        }
        /* Normal-mode FIFO reset commands are self-clearing. */
        if (value & 4) {
            sci->ssr &= ~0x78;
        }
        if (value & 2) {
            sci->tdr_full = false;
            sci->shifting = false;
            timer_del(sci->timer);
            sci->ssr |= 0x84;
        }
        sci->mode[offset] = value & ~6;
        break;
    default: return vr_error(b, offset, size, true, value);
    }
    vr_sci_group(s, channel);
    vr_sci_schedule(sci);
    return MEMTX_OK;
}

/* Width whitelist for the actual MTU3/4/6/7 bootstrap registers. */
static bool vr_mtu_reg(hwaddr offset, unsigned size)
{
    switch (offset) {
    case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7:
    case 8: case 9: case 10: case 56: case 57:
    case 76: case 77: case 80: case 128: case 129: case 130:
    case 142: case 145: case 146: case 147: case 148: case 156:
    case 160: case 161: case 162: case 163: case 164: case 165:
    case 166: case 167: case 168: case 169: case 170: case 171:
        return size == 1;
    case 16: case 18: case 24: case 26: case 28: case 30:
    case 36: case 38: case 40: case 42:
    case 114: case 116: case 136: case 138: case 140:
    case 152: case 154:
        return size == 2;
    default: return false;
    }
}

static bool vr_tpu_reg(hwaddr offset, unsigned size)
{
    unsigned channel, local;

    if (offset <= 1 || (offset >= 8 && offset <= 13)) {
        return size == 1; /* TSTR, TSYR, per-channel NFCR. */
    }
    if (offset < 16 || offset >= 112) {
        return false;
    }
    channel = offset / 16 - 1;
    local = offset & 15;
    if (local <= 5) {
        return size == 1 && (local != 3 || channel == 0 || channel == 3);
    }
    return !(local & 1) && size == 2 &&
           (local <= 10 || channel == 0 || channel == 3);
}

static bool vr_adc_reg(hwaddr offset, unsigned size)
{
    switch (offset) {
    case 0: case 4: case 6: case 8: case 10: case 14: case 32:
    case 34: case 36: case 102: return size == 2;
    case 12: case 124: case 224: case 225: case 226: return size == 1;
    default: return false;
    }
}

static bool vr_serial_reg(hwaddr offset, unsigned size)
{
    /* RSPI0/1 and SCIFA10 are separate protocols; no unspecified input exists. */
    switch (offset) {
    case 0: case 1: case 2: case 3: case 6: case 7: case 8: case 9:
    case 10: case 11: case 12: case 13: case 14: case 15: case 32:
        return size == 1;
    case 4: return size == 1 || size == 2 || size == 4;
    case 16: case 18: case 20: case 22: case 24: case 26:
    case 28: case 30: return size == 2;
    default: return false;
    }
}

static bool vr_config_reg(VRBank *b, hwaddr offset, unsigned size)
{
    switch (b->id) {
    case VR_DTC:
        if (offset == 14) {
            return size == 2;
        }
        return (offset == 0 || offset == 8 || offset == 12) ? size == 1 :
               offset == 4 && size == 4;
    case VR_MTU: case VR_MTU6: return vr_mtu_reg(offset, size);
    case VR_MTU1:
        switch (offset) {
        case 0: case 1: case 2: case 3: case 4: case 5: case 20:
            return size == 1;
        case 6: case 8: case 10: case 12: case 14: case 16: case 18:
            return size == 2;
        default: return false;
        }
    case VR_MTU2:
        switch (offset) {
        case 0: case 1: case 2: case 3: case 4: case 5: case 12:
            return size == 1;
        case 6: case 8: case 10: return size == 2;
        default: return false;
        }
    case VR_TPU: return vr_tpu_reg(offset, size);
    case VR_ADC: return vr_adc_reg(offset, size);
    case VR_RSPI0: case VR_RSPI1: case VR_RSPI2:
        return vr_serial_reg(offset, size);
    default: return false;
    }
}

static MemTxResult vr_read(void *opaque, hwaddr offset, uint64_t *value,
                          unsigned size, MemTxAttrs attrs)
{
    VRBank *b = opaque;
    NikonVRState *s = b->soc;

    *value = 0;
    switch (b->id) {
    case VR_ICU: return vr_icu_read(b, offset, value, size);
    case VR_SCI1: case VR_SCI5: case VR_SCIFA10:
        return vr_sci_read(b, offset, value, size);
    case VR_PORT:
        if (size != 1 || !vr_port_valid(offset)) {
            break;
        }
        *value = offset >= 0x40 && offset < 0x60 ?
                 vr_pad(s, offset & 31) : b->regs[offset];
        return MEMTX_OK;
    case VR_DMA:
        if (!vr_dma_reg(offset, size)) {
            break;
        }
        *value = vr_load(b->regs + offset, size);
        return MEMTX_OK;
    case VR_RSPI0: case VR_RSPI1: case VR_RSPI2:
        if (!vr_serial_reg(offset, size)) {
            break;
        }
        if (offset == 4) {
            VRSPI *spi = &s->spi[vr_spi_channel(b)];

            if (!spi->rx_count) {
                break;
            }
            *value = spi->rx[spi->rx_head];
            spi->rx_head = (spi->rx_head + 1) & 3;
            if (!--spi->rx_count) {
                b->regs[3] &= ~0x80;
            } else if (b->regs[3] & 0x80) {
                vr_spi_event(spi, false);
            }
        } else {
            *value = vr_load(b->regs + offset, size);
        }
        return MEMTX_OK;
    default:
        if (!vr_config_reg(b, offset, size)) {
            break;
        }
        if (b->id == VR_ADC && offset >= 32 && offset <= 36 &&
            !(s->adc_valid & (1U << ((offset - 32) / 2)))) {
            break;
        }
        vr_counters_sync(s, b->id);
        /* No ADC conversion or serial receive data exists without an input. */
        *value = vr_load(b->regs + offset, size);
        return MEMTX_OK;
    }
    return vr_error(b, offset, size, false, 0);
}

static MemTxResult vr_write(void *opaque, hwaddr offset, uint64_t value,
                           unsigned size, MemTxAttrs attrs)
{
    VRBank *b = opaque;
    NikonVRState *s = b->soc;

    switch (b->id) {
    case VR_ICU: return vr_icu_write(b, offset, value, size);
    case VR_SCI1: case VR_SCI5: case VR_SCIFA10:
        return vr_sci_write(b, offset, value, size);
    case VR_PORT:
        if (size != 1 || !vr_port_valid(offset) ||
            (offset >= 0x40 && offset < 0x60)) {
            break;
        }
        if (offset == 0x11f) {
            /* B0WI locks changes to PFSWE until the guest clears B0WI. */
            b->regs[offset] = (value & 0x80) |
                             (b->regs[offset] & 0x80 ?
                              b->regs[offset] & 0x40 : value & 0x40);
        } else if (offset >= 0x140 && offset < 0x1d8) {
            if (!(b->regs[0x11f] & 0x40)) {
                return MEMTX_OK; /* Hardware protection ignores the write. */
            }
            b->regs[offset] = value;
        } else {
            b->regs[offset] = value;
        }
        vr_port_update(s);
        return MEMTX_OK;
    case VR_DMA:
        if (!vr_dma_reg(offset, size)) {
            break;
        }
        if (offset < 0x200 && (offset & 0x3f) == 30) {
            b->regs[offset] &= value | 0x80;
        } else {
            vr_store(b->regs + offset, value, size);
        }
        /* A real empty TDR supplies the initial TX request after DTE changes. */
        vr_sci_tx_ready(s, 0);
        vr_sci_tx_ready(s, 1);
        vr_sci_tx_ready(s, 2);
        vr_spi_ready(&s->spi[0]);
        vr_spi_ready(&s->spi[1]);
        vr_spi_ready(&s->spi[2]);
        return MEMTX_OK;
    case VR_RSPI0: case VR_RSPI1: case VR_RSPI2:
        if (!vr_serial_reg(offset, size)) {
            break;
        }
        if (offset == 4) {
            VRSPI *spi = &s->spi[vr_spi_channel(b)];

            if (!(b->regs[3] & 0x20) ||
                spi->tx_count >= (b->regs[11] & 3) + 1U) {
                break;
            }
            spi->tx[(spi->tx_head + spi->tx_count) & 3] = value;
            if (++spi->tx_count == (b->regs[11] & 3) + 1U) {
                b->regs[3] &= ~0x20;
            }
        } else if (offset == 3) {
            b->regs[3] = (b->regs[3] & 0xe2) | (b->regs[3] & value & 0x1d);
        } else if (offset == 9) {
            break; /* Sequence monitor is read-only. */
        } else {
            vr_store(b->regs + offset, value, size);
        }
        if (offset == 0 && !(value & 0x40)) {
            VRSPI *spi = &s->spi[vr_spi_channel(b)];

            timer_del(spi->timer);
            spi->shifting = false;
            b->regs[3] |= 0x40;
        }
        vr_spi_ready(&s->spi[vr_spi_channel(b)]);
        return MEMTX_OK;
    default:
        if (!vr_config_reg(b, offset, size)) {
            break;
        }
        if (b->id == VR_DTC && offset == 14) {
            break;
        }
        if (b->id == VR_ADC && offset >= 32 && offset <= 36) {
            break; /* Conversion results are read-only. */
        }
        vr_counters_sync(s, b->id);
        if (vr_counter_status(s, b, offset) && size == 1) {
            b->regs[offset] = (value & 0xc0) |
                              (b->regs[offset] & value & 0x3f);
        } else {
            vr_store(b->regs + offset, value, size);
        }
        vr_counters_written(s, b);
        if (b->id == VR_ADC) {
            vr_adc_schedule(s);
        }
        return MEMTX_OK;
    }
    return vr_error(b, offset, size, true, value);
}

static const MemoryRegionOps vr_ops = {
    .read_with_attrs = vr_read,
    .write_with_attrs = vr_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl = { .min_access_size = 1, .max_access_size = 4 },
};

static void vr_reset(void *opaque)
{
    NikonVRState *s = opaque;
    unsigned i;

    for (i = 0; i < VR_BANKS; i++) {
        memset(s->bank[i].regs, 0, sizeof(s->bank[i].regs));
    }
    memset(s->ir, 0, sizeof(s->ir));
    memset(s->ier, 0, sizeof(s->ier));
    memset(s->ipr, 0, sizeof(s->ipr));
    memset(s->dtcer, 0, sizeof(s->dtcer));
    memset(s->dmrsr, 0, sizeof(s->dmrsr));
    memset(s->irqcr, 0, sizeof(s->irqcr));
    memset(s->grp, 0, sizeof(s->grp));
    memset(s->gen, 0, sizeof(s->gen));
    memset(s->source, 0, sizeof(s->source));
    memset(s->deferred, 0, sizeof(s->deferred));
    qemu_bh_cancel(s->event_bh);
    s->dtc_active = false;
    s->requested = -1;
    s->fir = 0;
    s->reported_error = s->dma_active = false;
    s->bank[VR_PORT].regs[0x11f] = 0x80;
    timer_del(s->adc_timer);
    s->adc_valid = 0;
    s->adc_remaining = 0;
    for (i = 0; i < ARRAY_SIZE(s->counter); i++) {
        VRCounter *c = &s->counter[i];
        unsigned compare;

        timer_del(c->timer);
        c->count = 0;
        c->running = c->clock = false;
        c->epoch = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        for (compare = 0; compare < c->def->compares; compare++) {
            stw_le_p(s->bank[c->def->bank].regs + c->def->tgr[compare], 0xffff);
        }
    }
    for (i = 0; i < 3; i++) {
        timer_del(s->spi[i].timer);
        s->spi[i].shifting = false;
        s->spi[i].tx_head = s->spi[i].tx_count = 0;
        s->spi[i].rx_head = s->spi[i].rx_count = 0;
        s->spi[i].batch_remaining = 0;
        s->bank[vr_spi_bank(i)].regs[3] = 0x60;
        timer_del(s->sci[i].timer);
        memset(&s->sci[i].smr, 0,
               sizeof(VRSCI) - offsetof(VRSCI, smr));
        s->sci[i].brr = s->sci[i].tdr = 0xff;
        s->sci[i].ssr = 0x84;
        s->sci[i].scmr = 0x72;
    }
    vr_icu_update(s);
    vr_port_update(s);
}

static void vr_init(Object *obj)
{
    static const struct { hwaddr base; unsigned size; const char *name; } map[] = {
        { 0x82000, 0x205, "vr.dmac" },
        { 0x82400, 0x010, "vr.dtc" },
        { 0x87000, 0xa02, "vr.icub" },
        { 0x8c000, 0x200, "vr.port-mpc" },
        { 0x8a020, 0x00e, "vr.sci1" },
        { 0x8a0a0, 0x00e, "vr.sci5" },
        { 0xc1200, 0x100, "vr.mtu" },
        { 0xc1a00, 0x100, "vr.mtu6" },
        { 0x89000, 0x0e3, "vr.s12ad" },
        { 0xd0100, 0x021, "vr.rspi0" },
        { 0xd0140, 0x021, "vr.rspi1" },
        { 0xd0040, 0x020, "vr.scifa10" },
        { 0x88100, 0x070, "vr.tpu" },
        { 0xc1380, 0x020, "vr.mtu1" },
        { 0xc1400, 0x020, "vr.mtu2" },
        { 0xd0300, 0x021, "vr.rspi2" },
    };
    NikonVRState *s = NIKON_VR_PERIPHERALS(obj);
    unsigned i;

    for (i = 0; i < VR_BANKS; i++) {
        VRBank *b = &s->bank[i];

        b->soc = s;
        b->id = i;
        b->base = map[i].base;
        memory_region_init_io(&b->memory, obj, &vr_ops, b, map[i].name, map[i].size);
        sysbus_init_mmio(SYS_BUS_DEVICE(obj), &b->memory);
    }
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->fastirq);
    qdev_init_gpio_in(DEVICE(obj), vr_irq_input, 256);
    qdev_init_gpio_in_named(DEVICE(obj), vr_ack, "ack", 1);
    qdev_init_gpio_in_named(DEVICE(obj), vr_pad_input, "pad", 19 * 8);
    qdev_init_gpio_out_named(DEVICE(obj), s->output, "pad-out", 19 * 8);
    qdev_init_gpio_in_named(DEVICE(obj), vr_counter_clock, "timer-clock",
                           ARRAY_SIZE(s->counter));
    qdev_init_gpio_in_named(DEVICE(obj), vr_analog_input, "analog", 3);
    s->event_bh = qemu_bh_new(vr_deferred_events, s);
    s->adc_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, vr_adc_complete, s);
    object_initialize_child(obj, "cmt", &s->cmt, TYPE_RENESAS_CMT);
    for (i = 0; i < ARRAY_SIZE(s->counter); i++) {
        VRCounter *c = &s->counter[i];

        c->soc = s;
        c->def = &vr_counter_def[i];
        c->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, vr_counter_expire, c);
    }
    for (i = 0; i < 3; i++) {
        VRSPI *spi = &s->spi[i];
        VRSCI *sci = &s->sci[i];
        g_autofree char *name = g_strdup_printf("rspi%u", i);
        g_autofree char *sci_name = g_strdup_printf("sci%u", i == 2 ? 10 :
                                                  i == 1 ? 5 : 1);

        spi->soc = s;
        spi->channel = i;
        spi->bus = ssi_create_bus(DEVICE(obj), name);
        spi->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, vr_spi_shift, spi);
        sci->soc = s;
        sci->channel = i;
        sci->bus = ssi_create_bus(DEVICE(obj), sci_name);
        sci->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, vr_sci_master_shift, sci);
    }
}

static void vr_realize(DeviceState *dev, Error **errp)
{
    NikonVRState *s = NIKON_VR_PERIPHERALS(dev);
    SysBusDevice *cmt = SYS_BUS_DEVICE(&s->cmt);
    unsigned i;

    if (!s->memory || !s->pclk_hz) {
        error_setg(errp, "VR peripherals need memory and a named functional pclk-hz");
        return;
    }
    address_space_init(&s->as, s->memory, "vr-peripheral-dma");
    s->as_initialized = true;
    qdev_prop_set_uint64(DEVICE(cmt), "input-freq", s->pclk_hz);
    if (!sysbus_realize(cmt, errp)) {
        return;
    }
    for (i = 0; i < CMT_NR_IRQ; i++) {
        sysbus_connect_irq(cmt, i, qdev_get_gpio_in(dev, 28 + i));
    }
    memory_region_add_subregion(s->memory, 0x88000, sysbus_mmio_get_region(cmt, 0));
    qemu_register_reset(vr_reset, s);
    vr_reset(s);
}

static void vr_finalize(Object *obj)
{
    NikonVRState *s = NIKON_VR_PERIPHERALS(obj);
    unsigned i;

    qemu_unregister_reset(vr_reset, s);
    if (s->as_initialized) {
        address_space_destroy(&s->as);
    }
    for (i = 0; i < 3; i++) {
        timer_free(s->spi[i].timer);
        timer_free(s->sci[i].timer);
    }
    for (i = 0; i < ARRAY_SIZE(s->counter); i++) {
        timer_free(s->counter[i].timer);
    }
    timer_free(s->adc_timer);
    qemu_bh_delete(s->event_bh);
}

static const Property vr_properties[] = {
    DEFINE_PROP_LINK("memory", NikonVRState, memory, TYPE_MEMORY_REGION,
                     MemoryRegion *),
    DEFINE_PROP_UINT64("pclk-hz", NikonVRState, pclk_hz, 0),
};

static void vr_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = vr_realize;
    device_class_set_props(dc, vr_properties);
    /* No migration contract is claimed for this loaded-component profile. */
    dc->user_creatable = false;
}

static uint32_t vr_slave_transfer(SSIPeripheral *dev, uint32_t value)
{
    return vr_sci_clock(NIKON_VR_SCI_SLAVE(dev)->soc, value);
}

static int vr_slave_cs(SSIPeripheral *dev, bool level)
{
    /* SSI's argument is the physical level, low means selected. */
    vr_pad_input(NIKON_VR_SCI_SLAVE(dev)->soc, 4 * 8 + 7, level);
    return 0;
}

static void vr_slave_realize(SSIPeripheral *dev, Error **errp)
{
    NikonVRSlave *s = NIKON_VR_SCI_SLAVE(dev);

    if (!s->soc) {
        error_setg(errp, "SCI slave needs the VR peripheral link");
    } else {
        vr_pad_input(s->soc, 4 * 8 + 7, 1);
        dev->cs = true;
    }
}

static const Property vr_slave_properties[] = {
    DEFINE_PROP_LINK("peripherals", NikonVRSlave, soc,
                     TYPE_NIKON_VR_PERIPHERALS, NikonVRState *),
};

static void vr_slave_class_init(ObjectClass *oc, const void *data)
{
    SSIPeripheralClass *sc = SSI_PERIPHERAL_CLASS(oc);

    sc->realize = vr_slave_realize;
    sc->transfer = vr_slave_transfer;
    sc->set_cs = vr_slave_cs;
    sc->cs_polarity = SSI_CS_LOW;
    device_class_set_props(DEVICE_CLASS(oc), vr_slave_properties);
}

DeviceState *nikon_vr_peripherals_create(Object *owner, MemoryRegion *memory,
                                       qemu_irq irq, qemu_irq fir,
                                       uint64_t pclk_hz)
{
    DeviceState *dev = qdev_new(TYPE_NIKON_VR_PERIPHERALS);
    NikonVRState *s = NIKON_VR_PERIPHERALS(dev);
    unsigned i;

    object_property_add_child(owner, "vr-peripherals", OBJECT(dev));
    object_property_set_link(OBJECT(dev), "memory", OBJECT(memory), &error_fatal);
    qdev_prop_set_uint64(dev, "pclk-hz", pclk_hz);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 1, fir);
    for (i = 0; i < VR_BANKS; i++) {
        memory_region_add_subregion(memory, s->bank[i].base, &s->bank[i].memory);
    }
    return dev;
}

qemu_irq nikon_vr_peripherals_ack(DeviceState *dev)
{
    return qdev_get_gpio_in_named(dev, "ack", 0);
}

DeviceState *nikon_vr_peripherals_attach_spi(DeviceState *dev, SSIBus *bus)
{
    DeviceState *slave = qdev_new(TYPE_NIKON_VR_SCI_SLAVE);

    object_property_set_link(OBJECT(slave), "peripherals", OBJECT(dev), &error_fatal);
    ssi_realize_and_unref(slave, bus, &error_fatal);
    return slave;
}

static const TypeInfo vr_types[] = {
    { .name = TYPE_NIKON_VR_PERIPHERALS, .parent = TYPE_SYS_BUS_DEVICE,
      .instance_size = sizeof(NikonVRState), .instance_init = vr_init,
      .instance_finalize = vr_finalize, .class_init = vr_class_init },
    { .name = TYPE_NIKON_VR_SCI_SLAVE, .parent = TYPE_SSI_PERIPHERAL,
      .instance_size = sizeof(NikonVRSlave), .class_init = vr_slave_class_init },
};

static void vr_register_types(void)
{
    type_register_static_array(vr_types, ARRAY_SIZE(vr_types));
}
type_init(vr_register_types)
