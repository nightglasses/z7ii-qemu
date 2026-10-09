/*
 * Original IPUD-named packet transport, with a disconnected default peer.
 * FIFO depth, nominal timing, pending and external framing are virtual laws.
 * No RMU transaction, remote reply or physical controller identity is invented.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "qemu/fifo32.h"
#include "qemu/timer.h"
#include "chardev/char-fe.h"
#include "migration/vmstate.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "z7ii.h"

#define IPUD_BASE 0x24020000ULL
#define IPUD_DEPTH 32
OBJECT_DECLARE_SIMPLE_TYPE(Z7IIIPUDState, Z7II_IPUD)

struct Z7IIIPUDState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    Fifo32 tx, rx;
    Clock *clock;
    QEMUTimer *timer;
    CharFrontend peer;
    qemu_irq irq;
    uint32_t config, endpoint_config, pending, mask;
    unsigned staged, committed, written, received;
    uint8_t incoming[12];
    bool consuming, reported_error;
};

static void z7ii_ipud_irq(Z7IIIPUDState *s)
{
    qemu_set_irq(s->irq, !!(s->pending & s->mask));
}

static void z7ii_ipud_schedule(Z7IIIPUDState *s)
{
    if (s->committed && !s->consuming) {
        s->consuming = true;
        /* Virtual 48MHz transport clock, 32 ticks per retained word. */
        timer_mod(s->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                  MAX(1, clock_ticks_to_ns(s->clock, 32)));
    }
}

static void z7ii_ipud_consume(void *opaque)
{
    Z7IIIPUDState *s = opaque;
    unsigned before = fifo32_num_used(&s->tx);
    uint8_t bytes[4];
    int consumed;

    s->consuming = false;
    if (!s->committed) {
        return;
    }
    /* Official peek leaves the queued word retained until fully sent. */
    assert(fifo8_peek_buf(&s->tx.fifo, bytes, sizeof(bytes)) == sizeof(bytes));
    if (qemu_chr_fe_backend_connected(&s->peer)) {
        consumed = qemu_chr_fe_write(&s->peer, bytes + s->written,
                                     4 - s->written);
        if (consumed > 0) {
            s->written += consumed;
        }
        if (s->written != 4) {
            z7ii_ipud_schedule(s);
            return;
        }
    }
    /* Unconnected output is the named sink, never a received peer response. */
    fifo32_pop(&s->tx);
    s->written = 0;
    s->committed--;
    if (before > 29 && fifo32_num_used(&s->tx) <= 29) {
        s->pending |= 4;
    }
    z7ii_ipud_irq(s);
    z7ii_ipud_schedule(s);
}

static int z7ii_ipud_can_receive(void *opaque)
{
    Z7IIIPUDState *s = opaque;

    return fifo32_num_free(&s->rx) >= 3 ? 12 - s->received : 0;
}

static void z7ii_ipud_receive(void *opaque, const uint8_t *bytes, int size)
{
    Z7IIIPUDState *s = opaque;
    unsigned i;

    assert(size <= z7ii_ipud_can_receive(s));
    memcpy(s->incoming + s->received, bytes, size);
    s->received += size;
    if (s->received == 12) {
        /* Virtual external API: three little-endian words per frame. */
        for (i = 0; i < 3; i++) {
            fifo32_push(&s->rx, ldl_le_p(s->incoming + i * 4));
        }
        s->received = 0;
        s->pending |= 1;
        z7ii_ipud_irq(s);
    }
}

static void z7ii_ipud_peer_event(void *opaque, QEMUChrEvent event)
{
    Z7IIIPUDState *s = opaque;

    if (event == CHR_EVENT_CLOSED) {
        /* A truncated external frame is not a packet or a manufactured word. */
        s->received = 0;
    }
}

static MemTxResult z7ii_ipud_error(Z7IIIPUDState *s, hwaddr offset,
                                  unsigned size, bool write, uint64_t value)
{
    z7ii_access_error(&s->reported_error, IPUD_BASE + offset, size,
                      write, value, "ipud-access-or-phase-not-supported");
    return MEMTX_ERROR;
}

static MemTxResult z7ii_ipud_read(void *opaque, hwaddr offset, uint64_t *value,
                                 unsigned size, MemTxAttrs attrs)
{
    Z7IIIPUDState *s = opaque;

    *value = 0;
    if (size != 4 || (offset & 3)) {
        return z7ii_ipud_error(s, offset, size, false, 0);
    }
    switch (offset) {
    case 0:
        if (fifo32_is_empty(&s->rx)) {
            return z7ii_ipud_error(s, offset, size, false, 0);
        }
        *value = fifo32_pop(&s->rx);
        qemu_chr_fe_accept_input(&s->peer);
        break;
    case 0xc:
        *value = fifo32_num_used(&s->rx) |
                 (fifo32_num_used(&s->tx) << 8);
        break;
    case 0x14:
        *value = s->pending;
        break;
    case 0x1c:
        *value = s->mask;
        break;
    case 0x80:
        /* Named RMU-unavailable reset state; physical reset is unknown. */
        break;
    default:
        return z7ii_ipud_error(s, offset, size, false, 0);
    }
    return MEMTX_OK;
}

static MemTxResult z7ii_ipud_write(void *opaque, hwaddr offset, uint64_t value,
                                  unsigned size, MemTxAttrs attrs)
{
    Z7IIIPUDState *s = opaque;

    if (size != 4 || (offset & 3)) {
        return z7ii_ipud_error(s, offset, size, true, value);
    }
    switch (offset) {
    case 4:
        if (s->staged == 3 || fifo32_is_full(&s->tx)) {
            return z7ii_ipud_error(s, offset, size, true, value);
        }
        fifo32_push(&s->tx, value);
        s->staged++;
        break;
    case 8:
        if (value == 2) {
            if (s->staged != 3 || s->config != 0x1d00 ||
                s->endpoint_config != 0x7ffff) {
                return z7ii_ipud_error(s, offset, size, true, value);
            }
            s->committed += 3;
            s->staged = 0;
            z7ii_ipud_schedule(s);
        } else if (value != 4) {
            return z7ii_ipud_error(s, offset, size, true, value);
        }
        /* CMD4 acknowledges consumption; RXDATA already popped each word. */
        break;
    case 0x10:
        if (value != 0x1d00) {
            return z7ii_ipud_error(s, offset, size, true, value);
        }
        s->config = value;
        break;
    case 0x18:
        s->pending &= ~(value & 5);
        break;
    case 0x1c:
        s->mask = value;
        break;
    case 0x20:
        if (value != 0x7ffff) {
            return z7ii_ipud_error(s, offset, size, true, value);
        }
        s->endpoint_config = value;
        break;
    default:
        /* Includes RMU84..94: no remote transaction or reply is bound. */
        return z7ii_ipud_error(s, offset, size, true, value);
    }
    z7ii_ipud_irq(s);
    return MEMTX_OK;
}

static const MemoryRegionOps z7ii_ipud_ops = {
    .read_with_attrs = z7ii_ipud_read,
    .write_with_attrs = z7ii_ipud_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
};

static void z7ii_ipud_reset(DeviceState *dev)
{
    Z7IIIPUDState *s = Z7II_IPUD(dev);

    timer_del(s->timer);
    fifo32_reset(&s->tx);
    fifo32_reset(&s->rx);
    s->config = s->endpoint_config = s->pending = s->mask = 0;
    s->staged = s->committed = s->written = s->received = 0;
    s->consuming = s->reported_error = false;
    clock_update_hz(s->clock, 48000000);
    z7ii_ipud_irq(s);
    qemu_chr_fe_accept_input(&s->peer);
}

static void z7ii_ipud_realize(DeviceState *dev, Error **errp)
{
    Z7IIIPUDState *s = Z7II_IPUD(dev);

    qemu_chr_fe_set_handlers(&s->peer, z7ii_ipud_can_receive,
                             z7ii_ipud_receive, z7ii_ipud_peer_event,
                             NULL, s, NULL, true);
}

static void z7ii_ipud_init(Object *obj)
{
    Z7IIIPUDState *s = Z7II_IPUD(obj);

    fifo32_create(&s->tx, IPUD_DEPTH);
    fifo32_create(&s->rx, IPUD_DEPTH);
    s->clock = qdev_init_clock_out(DEVICE(obj), "nominal-transport-clock");
    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, z7ii_ipud_consume, s);
    memory_region_init_io(&s->regs, obj, &z7ii_ipud_ops, s, "z7ii.ipud", 0x98);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
}

static void z7ii_ipud_finalize(Object *obj)
{
    Z7IIIPUDState *s = Z7II_IPUD(obj);

    timer_free(s->timer);
    fifo32_destroy(&s->tx);
    fifo32_destroy(&s->rx);
}

static const Property z7ii_ipud_properties[] = {
    DEFINE_PROP_CHR("peer", Z7IIIPUDState, peer),
};

static const VMStateDescription z7ii_ipud_vmstate = {
    .name = "z7ii-ipud",
    .unmigratable = true,
};

static void z7ii_ipud_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = z7ii_ipud_realize;
    dc->vmsd = &z7ii_ipud_vmstate;
    dc->user_creatable = false;
    device_class_set_props(dc, z7ii_ipud_properties);
    device_class_set_legacy_reset(dc, z7ii_ipud_reset);
}

static const TypeInfo z7ii_ipud_type = {
    .name = TYPE_Z7II_IPUD,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Z7IIIPUDState),
    .instance_init = z7ii_ipud_init,
    .instance_finalize = z7ii_ipud_finalize,
    .class_init = z7ii_ipud_class_init,
};

static void z7ii_ipud_register_types(void)
{
    type_register_static(&z7ii_ipud_type);
}
type_init(z7ii_ipud_register_types)
