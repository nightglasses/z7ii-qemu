/*
 * Polling USIO transmit profile; register protocol from original MAS.
 * Nominal UART clock and one-word holding capacity are virtual-board inputs.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "chardev/char-fe.h"
#include "migration/vmstate.h"
#include "hw/core/sysbus.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "z7ii.h"

#define USIO_BASE 0x2c090000ULL
#define UART_NOMINAL_HZ 100000000
OBJECT_DECLARE_SIMPLE_TYPE(Z7IIUSIOState, Z7II_USIO)

struct Z7IIUSIOState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    CharFrontend chr;
    Clock *clock;
    QEMUTimer *tx_timer;
    uint8_t smr, scr, escr;
    uint16_t dr, bgr, fcr;
    bool pending;
    bool reported_error;
};

static int64_t z7ii_usio_frame_ns(Z7IIUSIOState *s)
{
    /* Linux USIO uses baud = clock / (BGR + 1); virtual framing is 8N1. */
    return clock_ticks_to_ns(s->clock, 10ULL * (s->bgr + 1));
}

static void z7ii_usio_tx(void *opaque)
{
    Z7IIUSIOState *s = opaque;
    uint8_t byte = s->dr;

    if (!s->pending) {
        return;
    }
    /* No backend is an explicit disconnected transmitter, not RX input. */
    if (qemu_chr_fe_backend_connected(&s->chr) &&
        qemu_chr_fe_write(&s->chr, &byte, 1) != 1) {
        timer_mod(s->tx_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                  z7ii_usio_frame_ns(s));
        return;
    }
    s->pending = false;
}

static void z7ii_usio_clear_tx(Z7IIUSIOState *s)
{
    timer_del(s->tx_timer);
    s->pending = false;
}

static MemTxResult z7ii_usio_error(Z7IIUSIOState *s, hwaddr offset,
                                  unsigned size, bool write, uint64_t value,
                                  const char *reason)
{
    z7ii_access_error(&s->reported_error, USIO_BASE + offset, size,
                       write, value, reason);
    return MEMTX_ERROR;
}

static bool z7ii_usio_width(hwaddr offset, unsigned size)
{
    return (offset <= 3 && size == 1) ||
           ((offset == 4 || offset == 6 || offset == 0xc) && size == 2);
}

static MemTxResult z7ii_usio_read(void *opaque, hwaddr offset, uint64_t *value,
                                 unsigned size, MemTxAttrs attrs)
{
    Z7IIUSIOState *s = opaque;

    *value = 0;
    if (!z7ii_usio_width(offset, size)) {
        return z7ii_usio_error(s, offset, size, false, 0,
                               "usio-access-not-supported");
    }
    switch (offset) {
    case 0: *value = s->smr; break;
    case 1: *value = s->scr; break;
    case 2: *value = s->escr; break;
    case 3: *value = s->pending ? 0 : 2; break; /* TDRE; no fabricated RDRF. */
    case 4: *value = s->dr; break; /* Original nine-bit read/modify/write path. */
    case 6: *value = s->bgr; break;
    case 0xc: *value = s->fcr; break;
    default: g_assert_not_reached();
    }
    return MEMTX_OK;
}

static MemTxResult z7ii_usio_write(void *opaque, hwaddr offset, uint64_t value,
                                  unsigned size, MemTxAttrs attrs)
{
    Z7IIUSIOState *s = opaque;

    if (!z7ii_usio_width(offset, size)) {
        return z7ii_usio_error(s, offset, size, true, value,
                               "usio-access-not-supported");
    }
    switch (offset) {
    case 0:
        if (value != 1 || s->pending) {
            goto unsupported;
        }
        s->smr = value;
        break;
    case 1:
        if (value != 0 && value != 1 && value != 0x80 && value != 0x81) {
            goto unsupported;
        }
        if (value & 0x80) {
            z7ii_usio_clear_tx(s);
            s->dr = 0;
        } else if (!(value & 1) && s->pending) {
            goto unsupported;
        }
        /* UPCL is a reset action, self-clearing in this functional profile. */
        s->scr = value & 1;
        break;
    case 2:
        if (value != 0 || s->pending) {
            goto unsupported;
        }
        s->escr = value;
        break;
    case 3:
        if (value != 0x80) { /* REC; no RX/error producer in this TX profile. */
            goto unsupported;
        }
        break;
    case 4:
        if (value > 0x1ff || s->pending || s->scr != 1 || s->smr != 1 ||
            s->bgr != 0x363 || (s->fcr != 0 && s->fcr != 0x401)) {
            return z7ii_usio_error(s, offset, size, true, value,
                                   "usio-transmit-phase-not-supported");
        }
        s->dr = value;
        s->pending = true;
        timer_mod(s->tx_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                  z7ii_usio_frame_ns(s));
        break;
    case 6:
        if (value != 0x363 || s->pending) {
            goto unsupported;
        }
        s->bgr = value;
        break;
    case 0xc:
        if (value != 0 && value != 0xc && value != 0x405) {
            goto unsupported;
        }
        if (value & 4) { /* FCL1 transmit flush; reset cancels deferred output. */
            z7ii_usio_clear_tx(s);
        } else if (s->pending) {
            goto unsupported;
        }
        s->fcr = value & ~0xc; /* FCL1/FCL2 actions self-clear. */
        break;
    default:
        g_assert_not_reached();
    }
    return MEMTX_OK;

unsupported:
    return z7ii_usio_error(s, offset, size, true, value,
                           "usio-value-profile-not-supported");
}

static const MemoryRegionOps z7ii_usio_ops = {
    .read_with_attrs = z7ii_usio_read,
    .write_with_attrs = z7ii_usio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
};

static void z7ii_usio_reset(DeviceState *dev)
{
    Z7IIUSIOState *s = Z7II_USIO(dev);

    z7ii_usio_clear_tx(s);
    s->smr = s->scr = s->escr = 0;
    s->dr = s->bgr = s->fcr = 0;
    s->reported_error = false;
    clock_update_hz(s->clock, UART_NOMINAL_HZ);
}

static void z7ii_usio_init(Object *obj)
{
    Z7IIUSIOState *s = Z7II_USIO(obj);

    s->clock = qdev_init_clock_out(DEVICE(obj), "nominal-uart-clock");
    s->tx_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, z7ii_usio_tx, s);
    memory_region_init_io(&s->regs, obj, &z7ii_usio_ops, s, "z7ii.usio", 0x10);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
}

static void z7ii_usio_finalize(Object *obj)
{
    Z7IIUSIOState *s = Z7II_USIO(obj);

    timer_free(s->tx_timer);
}

static const Property z7ii_usio_properties[] = {
    DEFINE_PROP_CHR("chardev", Z7IIUSIOState, chr),
};

static const VMStateDescription z7ii_usio_vmstate = {
    .name = "z7ii-usio",
    .unmigratable = true,
};

static void z7ii_usio_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &z7ii_usio_vmstate;
    device_class_set_props(dc, z7ii_usio_properties);
    device_class_set_legacy_reset(dc, z7ii_usio_reset);
    dc->user_creatable = false;
}

static const TypeInfo z7ii_usio_type = {
    .name = TYPE_Z7II_USIO,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Z7IIUSIOState),
    .instance_init = z7ii_usio_init,
    .instance_finalize = z7ii_usio_finalize,
    .class_init = z7ii_usio_class_init,
};

static void z7ii_usio_register_types(void)
{
    type_register_static(&z7ii_usio_type);
}
type_init(z7ii_usio_register_types)
