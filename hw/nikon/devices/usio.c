/*
 * USIO asynchronous UART functional profile, not a silicon identification.
 * Register layout: fixed Socionext header and Linux milbeaut_usio driver.
 * Clock, FIFO depth and reset timing are explicit virtual-board assumptions.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/fifo32.h"
#include "qemu/timer.h"
#include "chardev/char-fe.h"
#include "chardev/char-serial.h"
#include "migration/vmstate.h" /* IWYU pragma: keep */
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "hw/nikon/devices.h"

#define UART_NOMINAL_HZ 100000000
#define UART_DEPTH 128
#define SCR_TXE 0x01
#define SCR_RXE 0x02
#define SCR_TBIE 0x04
#define SCR_TIE 0x08
#define SCR_RIE 0x10
#define SCR_UPCL 0x80
#define SSR_TBI 0x01
#define SSR_TDRE 0x02
#define SSR_RDRF 0x04
#define SSR_ORE 0x08
#define SSR_FRE 0x10
#define SSR_PE 0x20
#define SSR_REC 0x80
#define FCR_FE1 0x001
#define FCR_FE2 0x002
#define FCR_FCL1 0x004
#define FCR_FCL2 0x008
#define FCR_FSET 0x010
#define FCR_FLD 0x020
#define FCR_FLST 0x040
#define FCR_FSEL 0x100
#define FCR_FTIE 0x200
#define FCR_FDRQ 0x400
#define FCR_FRIIE 0x800
#define FCR_FLSTE 0x1000

OBJECT_DECLARE_SIMPLE_TYPE(NikonUSIOState, NIKON_USIO)

struct NikonUSIOState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    CharFrontend chr;
    Clock *clock;
    QEMUTimer *tx_timer, *rx_timer;
    uint64_t clock_hz;
    Fifo32 tx, rx;
    qemu_irq irq[2]; /* RX, TX; machine supplies source-qualified routing. */
    uint8_t smr, scr, escr, errors, threshold[2];
    uint16_t dr, bgr, fcr, shift;
    uint16_t history[UART_DEPTH];
    uint64_t written, saved, transmitted, retries;
    bool shifting, rx_idle, rx_ready, saved_valid, history_lost, reported_error;
};

static unsigned nikon_usio_bits(NikonUSIOState *s)
{
    static const unsigned lengths[] = { 8, 5, 6, 7, 9 };

    return lengths[s->escr & 7];
}

static int64_t nikon_usio_frame_ns(NikonUSIOState *s)
{
    unsigned bits = 1 + nikon_usio_bits(s) +
                    ((s->smr & 0x20) ? 1 : !!(s->escr & 0x10)) +
                    ((s->smr & 8) ? 2 : 1);

    return MAX(1, clock_ticks_to_ns(s->clock, bits * (s->bgr + 1ULL)));
}

static unsigned nikon_usio_tx_index(NikonUSIOState *s)
{
    return !!(s->fcr & FCR_FSEL);
}

static bool nikon_usio_fifo_enabled(NikonUSIOState *s, bool tx)
{
    unsigned index = nikon_usio_tx_index(s) ^ !tx;

    return s->fcr & (FCR_FE1 << index);
}

static unsigned nikon_usio_capacity(NikonUSIOState *s, bool tx)
{
    return nikon_usio_fifo_enabled(s, tx) ?
           UART_DEPTH / ((s->escr & 7) == 4 ? 2 : 1) : 1;
}

static uint8_t nikon_usio_status(NikonUSIOState *s)
{
    unsigned rx = fifo32_num_used(&s->rx);
    unsigned threshold = MAX(1, s->threshold[nikon_usio_tx_index(s) ^ 1]);
    uint8_t status = s->errors;

    if (!s->shifting && fifo32_is_empty(&s->tx)) {
        status |= SSR_TBI;
    }
    if (fifo32_is_empty(&s->tx)) {
        status |= SSR_TDRE;
    }
    if (rx && (!nikon_usio_fifo_enabled(s, false) || rx >= threshold ||
               s->rx_ready || ((s->fcr & FCR_FRIIE) && s->rx_idle))) {
        status |= SSR_RDRF;
    }
    return status;
}

static void nikon_usio_irq(NikonUSIOState *s)
{
    uint8_t status = nikon_usio_status(s);
    bool rx = (s->scr & SCR_RXE) && (s->scr & SCR_RIE) &&
              (status & (SSR_RDRF | SSR_ORE | SSR_FRE | SSR_PE));
    bool tx = (s->scr & SCR_TXE) &&
              (((s->scr & SCR_TBIE) && (status & SSR_TBI)) ||
               ((s->scr & SCR_TIE) && (status & SSR_TDRE)) ||
               ((s->fcr & FCR_FTIE) && (s->fcr & FCR_FDRQ)));

    qemu_set_irq(s->irq[0], rx);
    qemu_set_irq(s->irq[1], tx);
}

static bool nikon_usio_cts(NikonUSIOState *s)
{
    int modem = 0;

    if (!(s->escr & 0x80)) {
        return true;
    }
    /* A byte stream with no modem input does not establish asserted CTS. */
    return qemu_chr_fe_ioctl(&s->chr, CHR_IOCTL_SERIAL_GET_TIOCM, &modem) == 0 &&
           (modem & CHR_TIOCM_CTS);
}

static void nikon_usio_schedule(NikonUSIOState *s)
{
    if (!(s->scr & SCR_TXE) ||
        (!s->shifting && fifo32_is_empty(&s->tx))) {
        return;
    }
    if (!s->shifting) {
        s->shift = fifo32_pop(&s->tx);
        s->shifting = true;
        if (fifo32_is_empty(&s->tx)) {
            s->fcr |= FCR_FDRQ;
        }
    }
    if (!timer_pending(s->tx_timer)) {
        timer_mod(s->tx_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                  nikon_usio_frame_ns(s));
    }
    nikon_usio_irq(s);
}

static void nikon_usio_tx(void *opaque)
{
    NikonUSIOState *s = opaque;
    uint8_t byte = s->shift & ((1U << MIN(8, nikon_usio_bits(s))) - 1);

    if (!s->shifting || !(s->scr & SCR_TXE)) {
        return;
    }
    /* Disconnected TX can shift out; only actual backend bytes can enter RX. */
    if ((s->smr & 1) && (!nikon_usio_cts(s) ||
        (qemu_chr_fe_backend_connected(&s->chr) &&
         qemu_chr_fe_write(&s->chr, &byte, 1) != 1))) {
        s->retries++;
        timer_mod(s->tx_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                  nikon_usio_frame_ns(s));
        return;
    }
    s->transmitted++;
    s->shifting = false;
    nikon_usio_schedule(s);
    nikon_usio_irq(s);
}

static void nikon_usio_clear_tx(NikonUSIOState *s)
{
    timer_del(s->tx_timer);
    s->shifting = false;
    fifo32_reset(&s->tx);
    s->fcr |= FCR_FDRQ;
    s->fcr &= ~FCR_FLST;
    s->saved_valid = s->history_lost = false;
    s->written = s->saved = s->transmitted = s->retries = 0;
}

static void nikon_usio_rx_idle(void *opaque)
{
    NikonUSIOState *s = opaque;

    s->rx_idle = !fifo32_is_empty(&s->rx);
    if ((s->fcr & FCR_FRIIE) && s->rx_idle) {
        s->rx_ready = true;
    }
    nikon_usio_irq(s);
}

static int nikon_usio_can_receive(void *opaque)
{
    NikonUSIOState *s = opaque;

    if (!(s->scr & SCR_RXE)) {
        return 0;
    }
    return MAX(0, (int)nikon_usio_capacity(s, false) -
                  (int)fifo32_num_used(&s->rx));
}

static void nikon_usio_receive(void *opaque, const uint8_t *buf, int size)
{
    NikonUSIOState *s = opaque;
    unsigned mask = (1U << MIN(8, nikon_usio_bits(s))) - 1;

    if (!(s->scr & SCR_RXE)) {
        return;
    }
    for (int i = 0; i < size; i++) {
        if (!nikon_usio_can_receive(s)) {
            s->errors |= SSR_ORE;
            s->fcr &= ~(FCR_FE1 << (nikon_usio_tx_index(s) ^ 1));
            break;
        }
        fifo32_push(&s->rx, buf[i] & mask);
    }
    s->rx_idle = false;
    if (fifo32_num_used(&s->rx) >=
        MAX(1, s->threshold[nikon_usio_tx_index(s) ^ 1])) {
        s->rx_ready = true;
    }
    timer_mod(s->rx_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
              MAX(1, clock_ticks_to_ns(s->clock, 8 * (s->bgr + 1ULL))));
    nikon_usio_irq(s);
}

static void nikon_usio_event(void *opaque, QEMUChrEvent event)
{
    NikonUSIOState *s = opaque;

    if (event == CHR_EVENT_BREAK && (s->scr & SCR_RXE)) {
        uint8_t zero = 0;

        nikon_usio_receive(s, &zero, 1);
        s->errors |= SSR_FRE;
        s->fcr &= ~(FCR_FE1 << (nikon_usio_tx_index(s) ^ 1));
        nikon_usio_irq(s);
    }
}

static MemTxResult nikon_usio_error(NikonUSIOState *s, hwaddr offset,
                                  unsigned size, bool write, uint64_t value,
                                  const char *reason)
{
    nikon_access_error(&s->reported_error, s->regs.addr + offset, size,
                       write, value, reason);
    return MEMTX_ERROR;
}

static bool nikon_usio_width(hwaddr offset, unsigned size)
{
    return (offset <= 7 && size == 1) ||
           (offset >= 0xc && offset <= 0xf && size == 1) ||
           ((offset == 4 || offset == 6 || offset == 0xc || offset == 0xe) &&
            size == 2);
}

static MemTxResult nikon_usio_read(void *opaque, hwaddr offset, uint64_t *value,
                                 unsigned size, MemTxAttrs attrs)
{
    NikonUSIOState *s = opaque;
    unsigned shift = (offset & 1) * 8;

    *value = 0;
    if (!nikon_usio_width(offset, size)) {
        return nikon_usio_error(s, offset, size, false, 0,
                               "usio-access-not-supported");
    }
    switch (offset) {
    case 0: *value = s->smr; break;
    case 1: *value = s->scr; break;
    case 2: *value = s->escr; break;
    case 3: *value = nikon_usio_status(s); break;
    case 4:
        if (!fifo32_is_empty(&s->rx)) {
            s->dr = fifo32_pop(&s->rx);
            nikon_usio_irq(s);
            qemu_chr_fe_accept_input(&s->chr);
        }
        if (fifo32_is_empty(&s->rx)) {
            s->rx_idle = s->rx_ready = false;
            timer_del(s->rx_timer);
        } else {
            s->rx_idle = false;
            timer_mod(s->rx_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                      MAX(1, clock_ticks_to_ns(s->clock,
                                              8 * (s->bgr + 1ULL))));
        }
        *value = s->dr;
        break;
    case 5: *value = s->dr >> 8; break;
    case 6: case 7: *value = s->bgr >> shift; break;
    case 0xc: case 0xd: *value = s->fcr >> shift; break;
    case 0xe: case 0xf:
        *value = (fifo32_num_used(&s->tx) <<
                  (nikon_usio_tx_index(s) * 8)) |
                 (fifo32_num_used(&s->rx) <<
                  ((nikon_usio_tx_index(s) ^ 1) * 8));
        *value >>= shift;
        break;
    default: g_assert_not_reached();
    }
    if (size == 1) {
        *value &= 0xff;
    }
    return MEMTX_OK;
}

static uint16_t nikon_usio_merge(uint16_t old, hwaddr offset,
                                unsigned size, uint64_t value)
{
    unsigned shift = (offset & 1) * 8;
    uint16_t mask = size == 2 ? 0xffff : (0xff << shift);

    return (old & ~mask) | ((value << shift) & mask);
}

static MemTxResult nikon_usio_write(void *opaque, hwaddr offset, uint64_t value,
                                  unsigned size, MemTxAttrs attrs)
{
    NikonUSIOState *s = opaque;
    uint16_t next;

    if (!nikon_usio_width(offset, size)) {
        return nikon_usio_error(s, offset, size, true, value,
                               "usio-access-not-supported");
    }
    switch (offset) {
    case 0:
        /* UART MD0/1 only; synchronous CSIO needs its own SSI transport. */
        if ((value & ~0x2d) || ((value >> 5) > 1)) {
            goto unsupported;
        }
        s->smr = value;
        break;
    case 1:
        if (value & ~0x9f) {
            goto unsupported;
        }
        if (value & SCR_UPCL) {
            nikon_usio_clear_tx(s);
            fifo32_reset(&s->rx);
            s->dr = s->errors = 0;
            s->rx_idle = s->rx_ready = false;
            timer_del(s->rx_timer);
        }
        s->scr = value & ~SCR_UPCL;
        if (!(s->scr & SCR_TXE)) {
            timer_del(s->tx_timer);
        }
        break;
    case 2:
        if ((value & 0x40) || ((value & 7) > 4)) {
            goto unsupported;
        }
        s->escr = value;
        break;
    case 3:
        /* Status bits are read-only; REC clears latched receive errors. */
        if (value & 0x40) {
            goto unsupported;
        }
        if (value & SSR_REC) {
            s->errors = 0;
        }
        break;
    case 4:
        next = nikon_usio_merge(s->dr, offset, size, value);
        if (next > 0x1ff ||
            fifo32_num_used(&s->tx) >= nikon_usio_capacity(s, true)) {
            goto unsupported;
        }
        s->dr = next;
        if (s->saved_valid && s->written - s->saved >=
            nikon_usio_capacity(s, true)) {
            s->history_lost = true;
            if (s->fcr & FCR_FLSTE) {
                s->fcr |= FCR_FLST;
            }
        }
        s->history[s->written++ % UART_DEPTH] = next;
        fifo32_push(&s->tx, next);
        break;
    case 5:
        next = nikon_usio_merge(s->dr, offset, size, value);
        if (next > 0x1ff) {
            goto unsupported;
        }
        s->dr = next; /* Ninth bit staging; low-byte/halfword write commits. */
        break;
    case 6: case 7:
        next = nikon_usio_merge(s->bgr, offset, size, value);
        if (next & 0x8000) {
            goto unsupported; /* External clock has no board input. */
        }
        s->bgr = next;
        break;
    case 0xc: case 0xd:
        next = nikon_usio_merge(s->fcr, offset, size, value);
        if (next & ~0x1f7f) {
            goto unsupported; /* Reserved/test bits have no functional input. */
        }
        if ((next ^ s->fcr) & FCR_FSEL) {
            if ((s->fcr | next) & (FCR_FE1 | FCR_FE2) ||
                s->shifting || !fifo32_is_empty(&s->tx) ||
                !fifo32_is_empty(&s->rx)) {
                goto unsupported;
            }
        }
        if ((next & FCR_FSET) && !fifo32_is_empty(&s->tx)) {
            goto unsupported;
        }
        if ((next & FCR_FLD) &&
            (!s->saved_valid || s->history_lost || s->shifting ||
             (s->scr & SCR_TXE) ||
             (next & (FCR_FE1 << nikon_usio_tx_index(s))))) {
            goto unsupported;
        }
        /* FDRQ is set by empty-state transitions, never by a software one. */
        s->fcr = (next & ~(FCR_FCL1 | FCR_FCL2 | FCR_FSET | FCR_FLD |
                          FCR_FDRQ | FCR_FLST)) |
                 (s->fcr & next & FCR_FDRQ) | (s->fcr & FCR_FLST);
        if (next & (FCR_FCL1 << nikon_usio_tx_index(s))) {
            nikon_usio_clear_tx(s);
        }
        if (next & (FCR_FCL1 << (nikon_usio_tx_index(s) ^ 1))) {
            fifo32_reset(&s->rx);
            s->rx_idle = s->rx_ready = false;
            timer_del(s->rx_timer);
        }
        if (next & FCR_FSET) {
            s->saved = s->written;
            s->saved_valid = true;
            s->history_lost = false;
            s->fcr &= ~FCR_FLST;
        }
        if (next & FCR_FLD) {
            fifo32_reset(&s->tx);
            for (uint64_t i = s->saved; i < s->written; i++) {
                fifo32_push(&s->tx, s->history[i % UART_DEPTH]);
            }
        }
        break;
    case 0xe: case 0xf:
        /* Read gives live occupancy; writes program each FIFO's RX threshold. */
        next = nikon_usio_merge(s->threshold[0] | (s->threshold[1] << 8),
                                offset, size, value);
        if ((next & 0xff) > UART_DEPTH || (next >> 8) > UART_DEPTH) {
            goto unsupported;
        }
        s->threshold[0] = next;
        s->threshold[1] = next >> 8;
        break;
    default: g_assert_not_reached();
    }
    nikon_usio_schedule(s);
    nikon_usio_irq(s);
    qemu_chr_fe_accept_input(&s->chr);
    return MEMTX_OK;

unsupported:
    return nikon_usio_error(s, offset, size, true, value,
                           "usio-mode-or-capacity-not-supported");
}

static const MemoryRegionOps nikon_usio_ops = {
    .read_with_attrs = nikon_usio_read,
    .write_with_attrs = nikon_usio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
};

static void nikon_usio_reset(DeviceState *dev)
{
    NikonUSIOState *s = NIKON_USIO(dev);

    nikon_usio_clear_tx(s);
    fifo32_reset(&s->rx);
    timer_del(s->rx_timer);
    s->rx_idle = s->rx_ready = false;
    s->smr = s->scr = s->escr = s->errors = 0;
    s->dr = s->bgr = 0;
    s->fcr = FCR_FDRQ;
    s->threshold[0] = s->threshold[1] = 0;
    s->reported_error = false;
    clock_update_hz(s->clock, s->clock_hz);
    nikon_usio_irq(s);
}

static void nikon_usio_realize(DeviceState *dev, Error **errp)
{
    NikonUSIOState *s = NIKON_USIO(dev);

    if (!s->clock_hz) {
        error_setg(errp, "nikon-usio clock-hz must be nonzero");
        return;
    }
    qemu_chr_fe_set_handlers(&s->chr, nikon_usio_can_receive,
                             nikon_usio_receive, nikon_usio_event, NULL,
                             s, NULL, true);
}

static void nikon_usio_init(Object *obj)
{
    NikonUSIOState *s = NIKON_USIO(obj);

    s->clock = qdev_init_clock_out(DEVICE(obj), "nominal-uart-clock");
    s->tx_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, nikon_usio_tx, s);
    s->rx_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, nikon_usio_rx_idle, s);
    fifo32_create(&s->tx, UART_DEPTH);
    fifo32_create(&s->rx, UART_DEPTH);
    memory_region_init_io(&s->regs, obj, &nikon_usio_ops, s, "nikon.usio", 0x10);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq[0]);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq[1]);
    object_property_add_uint64_ptr(obj, "tx-written", &s->written,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(obj, "tx-transmitted", &s->transmitted,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(obj, "tx-retries", &s->retries,
                                   OBJ_PROP_FLAG_READ);
}

static void nikon_usio_finalize(Object *obj)
{
    NikonUSIOState *s = NIKON_USIO(obj);

    timer_free(s->tx_timer);
    timer_free(s->rx_timer);
    fifo32_destroy(&s->tx);
    fifo32_destroy(&s->rx);
}

static const Property nikon_usio_properties[] = {
    DEFINE_PROP_CHR("chardev", NikonUSIOState, chr),
    DEFINE_PROP_UINT64("clock-hz", NikonUSIOState, clock_hz,
                       UART_NOMINAL_HZ),
};

static const VMStateDescription nikon_usio_vmstate = {
    .name = "nikon-usio",
    .unmigratable = true,
};

static void nikon_usio_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = nikon_usio_realize;
    dc->vmsd = &nikon_usio_vmstate;
    device_class_set_props(dc, nikon_usio_properties);
    device_class_set_legacy_reset(dc, nikon_usio_reset);
    dc->user_creatable = false;
}

static const TypeInfo nikon_usio_type = {
    .name = TYPE_NIKON_USIO,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonUSIOState),
    .instance_init = nikon_usio_init,
    .instance_finalize = nikon_usio_finalize,
    .class_init = nikon_usio_class_init,
};

static void nikon_usio_register_types(void)
{
    type_register_static(&nikon_usio_type);
}
type_init(nikon_usio_register_types)
