/*
 * Original eleven-channel SPI protocol, with an explicit disconnected bus.
 * Socionext SPI is an IP protocol reference, not a Nikon silicon binding.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/fifo32.h"
#include "qemu/timer.h"
#include "migration/vmstate.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "hw/ssi/ssi.h"
#include "z7ii.h"

#define SPI_DEPTH 32
#define SPI_HZ 48000000
OBJECT_DECLARE_SIMPLE_TYPE(Z7IISPIState, Z7II_SPI)

struct Z7IISPIState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    SSIBus *bus;
    Fifo32 tx, rx;
    Clock *clock;
    QEMUTimer *timer;
    qemu_irq irq[2];
    uint32_t channel;
    uint32_t div, ctrl, aux, select, polarity, ien, pending;
    uint32_t dma_timeout, delay, enable, watermark, slave_request;
    bool asserted, shifting, overflow, reported_error;
};

static void z7ii_spi_irq(Z7IISPIState *s)
{
    /* Both guest wrappers use the common ISR. First-line routing is assumed. */
    qemu_set_irq(s->irq[0], !s->asserted && (s->pending & s->ien));
    qemu_set_irq(s->irq[1], 0); /* No external second-line producer. */
}

static bool z7ii_spi_txmark(Z7IISPIState *s)
{
    return fifo32_num_used(&s->tx) <= ((s->watermark >> 8) & 0xff);
}

static bool z7ii_spi_rxmark(Z7IISPIState *s)
{
    return fifo32_num_used(&s->rx) >= MAX(1, s->watermark & 0xff);
}

static void z7ii_spi_events(Z7IISPIState *s)
{
    if (fifo32_is_empty(&s->tx)) {
        s->pending |= 1;
    }
    if (z7ii_spi_txmark(s)) {
        s->pending |= 2;
    }
    if (z7ii_spi_rxmark(s)) {
        s->pending |= 4;
    }
    if (fifo32_is_full(&s->rx)) {
        s->pending |= 8;
    }
    z7ii_spi_irq(s);
}

static void z7ii_spi_schedule(Z7IISPIState *s)
{
    unsigned bits = ((s->aux >> 8) & 31) + 1;
    uint64_t cycles = bits * ((s->ctrl & 2) ? 2 * (s->div + 1) : 1);

    if (s->asserted || !(s->enable & 1) || !(s->ctrl & 0x20) ||
        fifo32_is_empty(&s->tx) || s->shifting) {
        return;
    }
    /*
     * Independent virtual input clock; no external clock is invented for
     * slave mode. Master frames use the public divider formula.
     */
    s->shifting = true;
    timer_mod(s->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
              MAX(1, clock_ticks_to_ns(s->clock, cycles + s->delay)));
}

static void z7ii_spi_shift(void *opaque)
{
    Z7IISPIState *s = opaque;
    unsigned bits = ((s->aux >> 8) & 31) + 1;
    uint32_t mask = bits == 32 ? UINT32_MAX : (1U << bits) - 1;
    uint32_t received;

    s->shifting = false;
    if (s->asserted || !(s->enable & 1) || !(s->ctrl & 0x20) ||
        fifo32_is_empty(&s->tx)) {
        return;
    }
    /*
     * Upstream SSI with no attached peripheral returns disconnected MISO=0.
     * No sensor/flash reply or command acknowledgement is supplied.
     */
    received = ssi_transfer(s->bus, fifo32_pop(&s->tx) & mask) & mask;
    if (!(s->aux & 8)) {
        if (fifo32_is_full(&s->rx)) {
            s->overflow = true;
            s->pending |= 0x80;
        } else {
            fifo32_push(&s->rx, received);
        }
    }
    if (fifo32_is_empty(&s->tx)) {
        s->pending |= 0x10; /* Transfer done is produced by the timer action. */
    }
    z7ii_spi_events(s);
    z7ii_spi_schedule(s);
}

static void z7ii_spi_cancel(Z7IISPIState *s)
{
    timer_del(s->timer);
    s->shifting = false;
}

static void z7ii_spi_flush(Z7IISPIState *s)
{
    z7ii_spi_cancel(s);
    fifo32_reset(&s->tx);
    fifo32_reset(&s->rx);
    s->enable = s->pending = s->slave_request = 0;
    s->overflow = false;
    z7ii_spi_irq(s);
}

static MemTxResult z7ii_spi_error(Z7IISPIState *s, hwaddr offset,
                                unsigned size, bool write, uint64_t value)
{
    z7ii_access_error(&s->reported_error,
                     0x2c0a0000ULL + s->channel * 0x100 + offset,
                     size, write, value, "spi-access-or-phase-not-supported");
    return MEMTX_ERROR;
}

static MemTxResult z7ii_spi_read(void *opaque, hwaddr offset, uint64_t *value,
                               unsigned size, MemTxAttrs attrs)
{
    Z7IISPIState *s = opaque;

    *value = 0;
    if (size != 4 || (offset & 3)) {
        goto error;
    }
    switch (offset) {
    case 4:
        if (s->asserted || fifo32_is_empty(&s->rx)) {
            goto error;
        }
        *value = fifo32_pop(&s->rx);
        z7ii_spi_events(s);
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
                 (z7ii_spi_txmark(s) ? 8 : 0) |
                 (fifo32_is_full(&s->tx) ? 0x10 : 0) |
                 (fifo32_is_empty(&s->rx) ? 0x20 : 0) |
                 (z7ii_spi_rxmark(s) ? 0x40 : 0) |
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
    return z7ii_spi_error(s, offset, size, false, 0);
}

static MemTxResult z7ii_spi_write(void *opaque, hwaddr offset, uint64_t value,
                                unsigned size, MemTxAttrs attrs)
{
    Z7IISPIState *s = opaque;

    if (size != 4 || (offset & 3)) {
        goto error;
    }
    switch (offset) {
    case 0:
        if (s->asserted || fifo32_is_full(&s->tx)) {
            goto error;
        }
        fifo32_push(&s->tx, value);
        z7ii_spi_events(s);
        break;
    case 8:
        if (value > 0xff || (s->enable & 1)) {
            goto error;
        }
        s->div = value;
        break;
    case 0xc:
        if ((value & ~0xc3f) || (value & 0x400) || (s->enable & 1)) {
            goto error; /* No DMA engine bound to this SPI profile. */
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
        z7ii_spi_irq(s);
        break;
    case 0x28:
        if (value & ~0x1ff) {
            goto error;
        }
        s->pending &= ~value;
        if (value & 0x80) {
            s->overflow = false;
        }
        z7ii_spi_irq(s);
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
            z7ii_spi_cancel(s);
        }
        z7ii_spi_events(s);
        break;
    case 0x4c:
        if (value & ~0xffff) {
            goto error;
        }
        s->watermark = value;
        z7ii_spi_events(s);
        break;
    case 0x60:
        if (value > 1) {
            goto error;
        }
        s->asserted = value;
        if (value) {
            z7ii_spi_flush(s);
        }
        z7ii_spi_irq(s);
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
    z7ii_spi_schedule(s);
    return MEMTX_OK;
error:
    return z7ii_spi_error(s, offset, size, true, value);
}

static const MemoryRegionOps z7ii_spi_ops = {
    .read_with_attrs = z7ii_spi_read,
    .write_with_attrs = z7ii_spi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl = { .min_access_size = 1, .max_access_size = 4 },
};

static void z7ii_spi_reset(DeviceState *dev)
{
    Z7IISPIState *s = Z7II_SPI(dev);

    s->asserted = true;
    z7ii_spi_flush(s);
    s->div = s->ctrl = s->aux = s->select = s->polarity = s->ien = 0;
    s->dma_timeout = s->delay = s->watermark = 0;
    s->reported_error = false;
    clock_update_hz(s->clock, SPI_HZ);
}

static void z7ii_spi_init(Object *obj)
{
    Z7IISPIState *s = Z7II_SPI(obj);

    fifo32_create(&s->tx, SPI_DEPTH);
    fifo32_create(&s->rx, SPI_DEPTH);
    s->bus = ssi_create_bus(DEVICE(obj), "spi");
    s->clock = qdev_init_clock_out(DEVICE(obj), "nominal-spi-clock");
    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, z7ii_spi_shift, s);
    memory_region_init_io(&s->regs, obj, &z7ii_spi_ops, s, "z7ii.spi", 0x100);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq[0]);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq[1]);
}

static void z7ii_spi_finalize(Object *obj)
{
    Z7IISPIState *s = Z7II_SPI(obj);

    timer_free(s->timer);
    fifo32_destroy(&s->tx);
    fifo32_destroy(&s->rx);
}

static const Property z7ii_spi_properties[] = {
    DEFINE_PROP_UINT32("channel", Z7IISPIState, channel, 0),
};
static const VMStateDescription z7ii_spi_vmstate = {
    .name = "z7ii-spi", .unmigratable = true,
};
static void z7ii_spi_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &z7ii_spi_vmstate;
    device_class_set_props(dc, z7ii_spi_properties);
    device_class_set_legacy_reset(dc, z7ii_spi_reset);
    dc->user_creatable = false;
}
static const TypeInfo z7ii_spi_type = {
    .name = TYPE_Z7II_SPI,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Z7IISPIState),
    .instance_init = z7ii_spi_init,
    .instance_finalize = z7ii_spi_finalize,
    .class_init = z7ii_spi_class_init,
};
static void z7ii_spi_register_types(void)
{
    type_register_static(&z7ii_spi_type);
}
type_init(z7ii_spi_register_types)
