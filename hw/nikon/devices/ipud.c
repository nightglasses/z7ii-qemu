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
#include "nikon.h"

#define IPUD_DEPTH 32
OBJECT_DECLARE_SIMPLE_TYPE(NikonIPUDState, NIKON_IPUD)

struct NikonIPUDState {
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

static void nikon_ipud_irq(NikonIPUDState *s)
{
    qemu_set_irq(s->irq, !!(s->pending & s->mask));
}

static void nikon_ipud_schedule(NikonIPUDState *s)
{
    if (s->committed && !s->consuming) {
        s->consuming = true;
        /* Virtual 48MHz transport clock, 32 ticks per retained word. */
        timer_mod(s->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                  MAX(1, clock_ticks_to_ns(s->clock, 32)));
    }
}

static void nikon_ipud_consume(void *opaque)
{
    NikonIPUDState *s = opaque;
    unsigned before = fifo32_num_used(&s->tx);
    uint32_t copied;
    uint8_t bytes[4];
    int consumed;

    s->consuming = false;
    if (!s->committed) {
        return;
    }
    /* Official peek leaves the queued word retained until fully sent. */
    copied = fifo8_peek_buf(&s->tx.fifo, bytes, sizeof(bytes));
    assert(copied == sizeof(bytes));
    if (qemu_chr_fe_backend_connected(&s->peer)) {
        consumed = qemu_chr_fe_write(&s->peer, bytes + s->written,
                                     4 - s->written);
        if (consumed > 0) {
            s->written += consumed;
        }
        if (s->written != 4) {
            nikon_ipud_schedule(s);
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
    nikon_ipud_irq(s);
    nikon_ipud_schedule(s);
}

static int nikon_ipud_can_receive(void *opaque)
{
    NikonIPUDState *s = opaque;

    return fifo32_num_free(&s->rx) >= 3 ? 12 - s->received : 0;
}

static void nikon_ipud_receive(void *opaque, const uint8_t *bytes, int size)
{
    NikonIPUDState *s = opaque;
    unsigned i;

    assert(size <= nikon_ipud_can_receive(s));
    memcpy(s->incoming + s->received, bytes, size);
    s->received += size;
    if (s->received == 12) {
        /* Virtual external API: three little-endian words per frame. */
        for (i = 0; i < 3; i++) {
            fifo32_push(&s->rx, ldl_le_p(s->incoming + i * 4));
        }
        s->received = 0;
        s->pending |= 1;
        nikon_ipud_irq(s);
    }
}

static void nikon_ipud_peer_event(void *opaque, QEMUChrEvent event)
{
    NikonIPUDState *s = opaque;

    if (event == CHR_EVENT_CLOSED) {
        /* A truncated external frame is not a packet or a manufactured word. */
        s->received = 0;
    }
}

static MemTxResult nikon_ipud_error(NikonIPUDState *s, hwaddr offset,
                                  unsigned size, bool write, uint64_t value)
{
    nikon_access_error(&s->reported_error, s->regs.addr + offset, size,
                      write, value, "ipud-access-or-phase-not-supported");
    return MEMTX_ERROR;
}

static MemTxResult nikon_ipud_read(void *opaque, hwaddr offset, uint64_t *value,
                                 unsigned size, MemTxAttrs attrs)
{
    NikonIPUDState *s = opaque;

    *value = 0;
    if (size != 4 || (offset & 3)) {
        return nikon_ipud_error(s, offset, size, false, 0);
    }
    switch (offset) {
    case 0:
        if (fifo32_is_empty(&s->rx)) {
            return nikon_ipud_error(s, offset, size, false, 0);
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
        return nikon_ipud_error(s, offset, size, false, 0);
    }
    return MEMTX_OK;
}

static MemTxResult nikon_ipud_write(void *opaque, hwaddr offset, uint64_t value,
                                  unsigned size, MemTxAttrs attrs)
{
    NikonIPUDState *s = opaque;

    if (size != 4 || (offset & 3)) {
        return nikon_ipud_error(s, offset, size, true, value);
    }
    switch (offset) {
    case 4:
        if (s->staged == 3 || fifo32_is_full(&s->tx)) {
            return nikon_ipud_error(s, offset, size, true, value);
        }
        fifo32_push(&s->tx, value);
        s->staged++;
        break;
    case 8:
        if (value == 2) {
            if (s->staged != 3 || s->config != 0x1d00 ||
                s->endpoint_config != 0x7ffff) {
                return nikon_ipud_error(s, offset, size, true, value);
            }
            s->committed += 3;
            s->staged = 0;
            nikon_ipud_schedule(s);
        } else if (value != 4) {
            return nikon_ipud_error(s, offset, size, true, value);
        }
        /* CMD4 acknowledges consumption; RXDATA already popped each word. */
        break;
    case 0x10:
        if (value != 0x1d00) {
            return nikon_ipud_error(s, offset, size, true, value);
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
            return nikon_ipud_error(s, offset, size, true, value);
        }
        s->endpoint_config = value;
        break;
    default:
        /* Includes RMU84..94: no remote transaction or reply is bound. */
        return nikon_ipud_error(s, offset, size, true, value);
    }
    nikon_ipud_irq(s);
    return MEMTX_OK;
}

static const MemoryRegionOps nikon_ipud_ops = {
    .read_with_attrs = nikon_ipud_read,
    .write_with_attrs = nikon_ipud_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
};

static void nikon_ipud_reset(DeviceState *dev)
{
    NikonIPUDState *s = NIKON_IPUD(dev);

    timer_del(s->timer);
    fifo32_reset(&s->tx);
    fifo32_reset(&s->rx);
    s->config = s->endpoint_config = s->pending = s->mask = 0;
    s->staged = s->committed = s->written = s->received = 0;
    s->consuming = s->reported_error = false;
    clock_update_hz(s->clock, 48000000);
    nikon_ipud_irq(s);
    qemu_chr_fe_accept_input(&s->peer);
}

static void nikon_ipud_realize(DeviceState *dev, Error **errp)
{
    NikonIPUDState *s = NIKON_IPUD(dev);

    qemu_chr_fe_set_handlers(&s->peer, nikon_ipud_can_receive,
                             nikon_ipud_receive, nikon_ipud_peer_event,
                             NULL, s, NULL, true);
}

static void nikon_ipud_init(Object *obj)
{
    NikonIPUDState *s = NIKON_IPUD(obj);

    fifo32_create(&s->tx, IPUD_DEPTH);
    fifo32_create(&s->rx, IPUD_DEPTH);
    s->clock = qdev_init_clock_out(DEVICE(obj), "nominal-transport-clock");
    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, nikon_ipud_consume, s);
    memory_region_init_io(&s->regs, obj, &nikon_ipud_ops, s, "nikon.ipud", 0x98);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
}

static void nikon_ipud_finalize(Object *obj)
{
    NikonIPUDState *s = NIKON_IPUD(obj);

    timer_free(s->timer);
    fifo32_destroy(&s->tx);
    fifo32_destroy(&s->rx);
}

static const Property nikon_ipud_properties[] = {
    DEFINE_PROP_CHR("peer", NikonIPUDState, peer),
};

static const VMStateDescription nikon_ipud_vmstate = {
    .name = "nikon-ipud",
    .unmigratable = true,
};

static void nikon_ipud_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = nikon_ipud_realize;
    dc->vmsd = &nikon_ipud_vmstate;
    dc->user_creatable = false;
    device_class_set_props(dc, nikon_ipud_properties);
    device_class_set_legacy_reset(dc, nikon_ipud_reset);
}

static const TypeInfo nikon_ipud_type = {
    .name = TYPE_NIKON_IPUD,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonIPUDState),
    .instance_init = nikon_ipud_init,
    .instance_finalize = nikon_ipud_finalize,
    .class_init = nikon_ipud_class_init,
};

static void nikon_ipud_register_types(void)
{
    type_register_static(&nikon_ipud_type);
}
type_init(nikon_ipud_register_types)
