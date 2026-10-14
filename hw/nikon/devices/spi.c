/*
 * Original eleven-channel SPI protocol, with an explicit disconnected bus.
 * Socionext SPI is an IP protocol reference, not a Nikon silicon binding.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "qemu/fifo32.h"
#include "qemu/timer.h"
#include "migration/vmstate.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "hw/ssi/ssi.h"
#include "hw/nikon/devices.h"
#include "hw/nikon/peer.h"

#define SPI_DEPTH 32
#define SPI_HZ 48000000
OBJECT_DECLARE_SIMPLE_TYPE(NikonSPIState, NIKON_SPI)

struct NikonSPIState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    SSIBus *bus;
    Fifo32 tx, rx;
    Clock *clock;
    QEMUTimer *timer;
    qemu_irq irq[2];
    qemu_irq dma[2];
    uint32_t channel;
    uint32_t div, ctrl, aux, select, polarity, ien, pending;
    uint32_t dma_timeout, delay, enable, watermark, slave_request;
    const NikonSPIAsyncOps *async_ops;
    void *async_opaque;
    uint64_t token;
    int64_t next_deadline_ns;
    bool dma_bound, async_bound, waiting, submitted;
    bool asserted, shifting, overflow, reported_error;
};

static void nikon_spi_irq(NikonSPIState *s)
{
    /* Both guest wrappers use the common ISR. First-line routing is assumed. */
    qemu_set_irq(s->irq[0], !s->asserted && (s->pending & s->ien));
    qemu_set_irq(s->irq[1], 0); /* No external second-line producer. */
    /* Existing HDMAC consumes one demand beat and rechecks these real FIFOs. */
    qemu_set_irq(s->dma[0], s->dma_bound && !s->asserted &&
                 (s->enable & 1) && (s->ctrl & 0x400) &&
                 !fifo32_is_full(&s->tx));
    qemu_set_irq(s->dma[1], s->dma_bound && !s->asserted &&
                 (s->enable & 1) && (s->ctrl & 0x400) &&
                 !fifo32_is_empty(&s->rx));
}

static bool nikon_spi_txmark(NikonSPIState *s)
{
    return fifo32_num_used(&s->tx) <= ((s->watermark >> 8) & 0xff);
}

static bool nikon_spi_rxmark(NikonSPIState *s)
{
    return fifo32_num_used(&s->rx) >= MAX(1, s->watermark & 0xff);
}

static void nikon_spi_events(NikonSPIState *s)
{
    if (fifo32_is_empty(&s->tx)) {
        s->pending |= 1;
    }
    if (nikon_spi_txmark(s)) {
        s->pending |= 2;
    }
    if (nikon_spi_rxmark(s)) {
        s->pending |= 4;
    }
    if (fifo32_is_full(&s->rx)) {
        s->pending |= 8;
    }
    nikon_spi_irq(s);
}

static void nikon_spi_schedule(NikonSPIState *s)
{
    unsigned bits = ((s->aux >> 8) & 31) + 1;
    uint64_t cycles = bits * ((s->ctrl & 2) ? 2 * (s->div + 1) : 1);
    int64_t period_ns;

    if (s->shifting) {
        return;
    }
    if (s->asserted || !(s->enable & 1) || !(s->ctrl & 0x20) ||
        fifo32_is_empty(&s->tx)) {
        s->next_deadline_ns = 0;
        return;
    }
    /*
     * Independent virtual input clock; no external clock is invented for
     * slave mode. Master frames use the public divider formula.
     */
    s->shifting = true;
    period_ns = MAX(1, clock_ticks_to_ns(s->clock, cycles + s->delay));
    if (!s->next_deadline_ns) {
        s->next_deadline_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + period_ns;
    } else {
        s->next_deadline_ns += period_ns;
    }
    timer_mod(s->timer, s->next_deadline_ns);
}

static void nikon_spi_complete(NikonSPIState *s, uint32_t received)
{
    unsigned bits = ((s->aux >> 8) & 31) + 1;
    uint32_t mask = bits == 32 ? UINT32_MAX : (1U << bits) - 1;

    s->shifting = false;
    s->waiting = s->submitted = false;
    fifo32_pop(&s->tx);
    if (!(s->aux & 8)) {
        if (fifo32_is_full(&s->rx)) {
            s->overflow = true;
            s->pending |= 0x80;
        } else {
            fifo32_push(&s->rx, received & mask);
        }
    }
    if (fifo32_is_empty(&s->tx)) {
        s->pending |= 0x10; /* Transfer done is produced by the timer action. */
    }
    nikon_spi_events(s);
    nikon_spi_schedule(s);
}

static void nikon_spi_submit(NikonSPIState *s)
{
    uint8_t bytes[4];
    unsigned bits = ((s->aux >> 8) & 31) + 1;
    uint32_t mask = bits == 32 ? UINT32_MAX : (1U << bits) - 1;

    /* Hold the FIFO head until the peer has actually completed this frame. */
    if (!s->async_ops || !s->waiting || s->submitted) {
        return;
    }
    fifo8_peek_buf(&s->tx.fifo, bytes, sizeof(bytes));
    s->submitted = true;
    if (!s->async_ops->request(s->async_opaque, s->token,
                               ldl_le_p(bytes) & mask, bits) && s->waiting) {
        s->submitted = false;
    }
}

static void nikon_spi_shift(void *opaque)
{
    NikonSPIState *s = opaque;
    uint8_t bytes[4];
    unsigned bits = ((s->aux >> 8) & 31) + 1;
    uint32_t mask = bits == 32 ? UINT32_MAX : (1U << bits) - 1;

    if (s->asserted || !(s->enable & 1) || !(s->ctrl & 0x20) ||
        fifo32_is_empty(&s->tx)) {
        s->shifting = false;
        s->next_deadline_ns = 0;
        return;
    }
    if (s->async_bound) {
        s->token++;
        s->waiting = true;
        nikon_spi_submit(s);
        return;
    }
    /* In-process upstream SSI retains its disconnected MISO-low behavior. */
    fifo8_peek_buf(&s->tx.fifo, bytes, sizeof(bytes));
    nikon_spi_complete(s, ssi_transfer(s->bus, ldl_le_p(bytes) & mask));
}

void nikon_spi_async_ready(DeviceState *dev)
{
    nikon_spi_submit(NIKON_SPI(dev));
}

void nikon_spi_async_complete(DeviceState *dev, uint64_t token,
                              uint32_t received)
{
    NikonSPIState *s = NIKON_SPI(dev);

    if (s->waiting && s->submitted && s->token == token) {
        /* External backpressure breaks the synchronous serial cadence. */
        s->next_deadline_ns = 0;
        nikon_spi_complete(s, received);
    }
}

void nikon_spi_attach_async(DeviceState *dev, const NikonSPIAsyncOps *ops,
                            void *opaque)
{
    NikonSPIState *s = NIKON_SPI(dev);

    /* Board wiring is immutable while an enabled port owns a transaction. */
    assert(!(s->enable & 1) && !s->shifting);
    assert(ops && ops->request && ops->cancel);
    s->async_bound = true;
    s->async_ops = ops;
    s->async_opaque = opaque;
}

static void nikon_spi_cancel(NikonSPIState *s)
{
    uint64_t token = s->token;
    bool submitted = s->submitted;

    timer_del(s->timer);
    s->shifting = false;
    s->next_deadline_ns = 0;
    s->waiting = s->submitted = false;
    s->token++;
    if (submitted) {
        s->async_ops->cancel(s->async_opaque, token);
    }
}

void nikon_spi_detach_async(DeviceState *dev, void *owner)
{
    NikonSPIState *s = NIKON_SPI(dev);
    bool busy = s->shifting;

    if (!s->async_ops || s->async_opaque != owner) {
        return;
    }
    nikon_spi_cancel(s);
    s->async_ops = NULL;
    s->async_opaque = NULL;
    /* Unplugging a peer cannot turn its unfinished frame into an SSI zero. */
    s->shifting = busy;
}

static void nikon_spi_flush(NikonSPIState *s)
{
    nikon_spi_cancel(s);
    fifo32_reset(&s->tx);
    fifo32_reset(&s->rx);
    s->enable = s->pending = s->slave_request = 0;
    s->overflow = false;
    nikon_spi_irq(s);
}

static MemTxResult nikon_spi_error(NikonSPIState *s, hwaddr offset,
                                unsigned size, bool write, uint64_t value)
{
    nikon_access_error(&s->reported_error,
                     s->regs.addr + offset,
                     size, write, value, "spi-access-or-phase-not-supported");
    return MEMTX_ERROR;
}

static MemTxResult nikon_spi_read(void *opaque, hwaddr offset, uint64_t *value,
                               unsigned size, MemTxAttrs attrs)
{
    NikonSPIState *s = opaque;

    *value = 0;
    if ((size != 4 && !(size == 2 && offset == 4 && s->dma_bound &&
                       (s->ctrl & 0x400))) || (offset & 3)) {
        goto error;
    }
    switch (offset) {
    case 4:
        if (s->asserted || fifo32_is_empty(&s->rx)) {
            goto error;
        }
        *value = fifo32_pop(&s->rx);
        nikon_spi_events(s);
        break;
    case 8:
        *value = s->div;
        break;
    case 0xc:
        *value = s->ctrl;
        break;
    case 0x10:
        *value = s->aux;
        break;
    case 0x14:
        *value = (s->shifting ? 1 : 0) |
                 (fifo32_is_empty(&s->tx) ? 4 : 0) |
                 (nikon_spi_txmark(s) ? 8 : 0) |
                 (fifo32_is_full(&s->tx) ? 0x10 : 0) |
                 (fifo32_is_empty(&s->rx) ? 0x20 : 0) |
                 (nikon_spi_rxmark(s) ? 0x40 : 0) |
                 (fifo32_is_full(&s->rx) ? 0x80 : 0) |
                 (s->overflow ? 0x100 : 0);
        break;
    case 0x18:
        *value = s->select;
        break;
    case 0x1c:
        *value = s->polarity;
        break;
    case 0x20:
        *value = s->ien;
        break;
    case 0x24:
        *value = s->pending;
        break;
    case 0x2c:
        *value = fifo32_num_used(&s->tx);
        break;
    case 0x30:
        *value = fifo32_num_used(&s->rx);
        break;
    case 0x34:
        *value = s->dma_timeout;
        break;
    case 0x38:
        *value = s->delay;
        break;
    case 0x3c:
        *value = s->enable;
        break;
    case 0x48:
        *value = SPI_DEPTH;
        break;
    case 0x4c:
        *value = s->watermark;
        break;
    case 0x60:
        *value = s->asserted;
        break;
    case 0x64:
        *value = s->slave_request;
        break;
    default:
        goto error;
    }
    return MEMTX_OK;
error:
    return nikon_spi_error(s, offset, size, false, 0);
}

static MemTxResult nikon_spi_write(void *opaque, hwaddr offset, uint64_t value,
                                unsigned size, MemTxAttrs attrs)
{
    NikonSPIState *s = opaque;

    if ((size != 4 && !(size == 2 && offset == 0 && s->dma_bound &&
                       (s->ctrl & 0x400))) || (offset & 3)) {
        goto error;
    }
    switch (offset) {
    case 0:
        if (s->asserted || fifo32_is_full(&s->tx)) {
            goto error;
        }
        fifo32_push(&s->tx, value);
        nikon_spi_events(s);
        break;
    case 8:
        if (value > 0xff || (s->enable & 1)) {
            goto error;
        }
        s->div = value;
        break;
    case 0xc:
        if ((value & ~0xc3f) || ((value & 0x400) && !s->dma_bound) ||
            (s->enable & 1)) {
            goto error;
        }
        s->ctrl = value;
        break;
    case 0x10:
        if ((value & ~0x1fbf) || (s->enable & 1)) {
            goto error;
        }
        s->aux = value;
        break;
    case 0x18:
    case 0x1c:
    case 0x38:
        if (value > 0xff || (s->enable & 1)) {
            goto error;
        }
        if (offset == 0x18) {
            s->select = value;
        }
        if (offset == 0x1c) {
            s->polarity = value;
        }
        if (offset == 0x38) {
            s->delay = value;
        }
        break;
    case 0x20:
        if (value & ~0x1ff) {
            goto error;
        }
        s->ien = value;
        nikon_spi_irq(s);
        break;
    case 0x28:
        if (value & ~0x1ff) {
            goto error;
        }
        s->pending &= ~value;
        if (value & 0x80) {
            s->overflow = false;
        }
        nikon_spi_irq(s);
        break;
    case 0x34:
        s->dma_timeout = value;
        break;
    case 0x3c:
        if (value & ~(s->channel == 8 ? 0xf : 1)) {
            goto error;
        }
        if ((value & 1) && s->asserted) {
            goto error;
        }
        s->enable = value;
        if (!(value & 1)) {
            nikon_spi_cancel(s);
        }
        nikon_spi_events(s);
        break;
    case 0x4c:
        if (value & ~0xffff) {
            goto error;
        }
        s->watermark = value;
        nikon_spi_events(s);
        break;
    case 0x60:
        if (value > 1) {
            goto error;
        }
        s->asserted = value;
        if (value) {
            nikon_spi_flush(s);
        }
        nikon_spi_irq(s);
        break;
    case 0x64:
        if (value > 1 || ((s->ctrl & 0x20) && value)) {
            goto error;
        }
        s->slave_request = value;
        break;
    default:
        goto error;
    }
    nikon_spi_schedule(s);
    return MEMTX_OK;
error:
    return nikon_spi_error(s, offset, size, true, value);
}

static const MemoryRegionOps nikon_spi_ops = {
    .read_with_attrs = nikon_spi_read,
    .write_with_attrs = nikon_spi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl = { .min_access_size = 1, .max_access_size = 4 },
};

static void nikon_spi_reset(DeviceState *dev)
{
    NikonSPIState *s = NIKON_SPI(dev);

    s->asserted = true;
    nikon_spi_flush(s);
    s->div = s->ctrl = s->aux = s->select = s->polarity = s->ien = 0;
    s->dma_timeout = s->delay = s->watermark = 0;
    s->reported_error = false;
    clock_update_hz(s->clock, SPI_HZ);
}

static void nikon_spi_init(Object *obj)
{
    NikonSPIState *s = NIKON_SPI(obj);

    fifo32_create(&s->tx, SPI_DEPTH);
    fifo32_create(&s->rx, SPI_DEPTH);
    s->bus = ssi_create_bus(DEVICE(obj), "spi");
    s->clock = qdev_init_clock_out(DEVICE(obj), "nominal-spi-clock");
    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, nikon_spi_shift, s);
    memory_region_init_io(&s->regs, obj, &nikon_spi_ops, s, "nikon.spi", 0x100);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq[0]);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq[1]);
    qdev_init_gpio_out_named(DEVICE(obj), s->dma, "dma", ARRAY_SIZE(s->dma));
}

static void nikon_spi_finalize(Object *obj)
{
    NikonSPIState *s = NIKON_SPI(obj);

    nikon_spi_cancel(s);
    timer_free(s->timer);
    fifo32_destroy(&s->tx);
    fifo32_destroy(&s->rx);
}

static const Property nikon_spi_properties[] = {
    DEFINE_PROP_UINT32("channel", NikonSPIState, channel, 0),
    DEFINE_PROP_BOOL("dma", NikonSPIState, dma_bound, false),
};
static const VMStateDescription nikon_spi_vmstate = {
    .name = "nikon-spi", .unmigratable = true,
};
static void nikon_spi_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &nikon_spi_vmstate;
    device_class_set_props(dc, nikon_spi_properties);
    device_class_set_legacy_reset(dc, nikon_spi_reset);
    dc->user_creatable = false;
}
static const TypeInfo nikon_spi_type = {
    .name = TYPE_NIKON_SPI,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonSPIState),
    .instance_init = nikon_spi_init,
    .instance_finalize = nikon_spi_finalize,
    .class_init = nikon_spi_class_init,
};
static void nikon_spi_register_types(void)
{
    type_register_static(&nikon_spi_type);
}
type_init(nikon_spi_register_types)
