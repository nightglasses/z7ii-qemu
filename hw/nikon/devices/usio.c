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
#include "nikon.h"

#define UART_NOMINAL_HZ 100000000
OBJECT_DECLARE_SIMPLE_TYPE(NikonUSIOState, NIKON_USIO)

struct NikonUSIOState {
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

static int64_t nikon_usio_frame_ns(NikonUSIOState *s)
{
    /* Linux USIO uses baud = clock / (BGR + 1); virtual framing is 8N1. */
    return clock_ticks_to_ns(s->clock, 10ULL * (s->bgr + 1));
}

static void nikon_usio_tx(void *opaque)
{
    NikonUSIOState *s = opaque;
    uint8_t byte = s->dr;

    if (!s->pending) {
        return;
    }
    /* No backend is an explicit disconnected transmitter, not RX input. */
    if (qemu_chr_fe_backend_connected(&s->chr) &&
        qemu_chr_fe_write(&s->chr, &byte, 1) != 1) {
        timer_mod(s->tx_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                  nikon_usio_frame_ns(s));
        return;
    }
    s->pending = false;
}

static void nikon_usio_clear_tx(NikonUSIOState *s)
{
    timer_del(s->tx_timer);
    s->pending = false;
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
    return (offset <= 3 && size == 1) ||
           ((offset == 4 || offset == 6 || offset == 0xc) && size == 2);
}

static MemTxResult nikon_usio_read(void *opaque, hwaddr offset, uint64_t *value,
                                 unsigned size, MemTxAttrs attrs)
{
    NikonUSIOState *s = opaque;

    *value = 0;
    if (!nikon_usio_width(offset, size)) {
        return nikon_usio_error(s, offset, size, false, 0,
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

static MemTxResult nikon_usio_write(void *opaque, hwaddr offset, uint64_t value,
                                  unsigned size, MemTxAttrs attrs)
{
    NikonUSIOState *s = opaque;

    if (!nikon_usio_width(offset, size)) {
        return nikon_usio_error(s, offset, size, true, value,
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
            nikon_usio_clear_tx(s);
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
            return nikon_usio_error(s, offset, size, true, value,
                                   "usio-transmit-phase-not-supported");
        }
        s->dr = value;
        s->pending = true;
        timer_mod(s->tx_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                  nikon_usio_frame_ns(s));
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
            nikon_usio_clear_tx(s);
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
    return nikon_usio_error(s, offset, size, true, value,
                           "usio-value-profile-not-supported");
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
    s->smr = s->scr = s->escr = 0;
    s->dr = s->bgr = s->fcr = 0;
    s->reported_error = false;
    clock_update_hz(s->clock, UART_NOMINAL_HZ);
}

static void nikon_usio_init(Object *obj)
{
    NikonUSIOState *s = NIKON_USIO(obj);

    s->clock = qdev_init_clock_out(DEVICE(obj), "nominal-uart-clock");
    s->tx_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, nikon_usio_tx, s);
    memory_region_init_io(&s->regs, obj, &nikon_usio_ops, s, "nikon.usio", 0x10);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
}

static void nikon_usio_finalize(Object *obj)
{
    NikonUSIOState *s = NIKON_USIO(obj);

    timer_free(s->tx_timer);
}

static const Property nikon_usio_properties[] = {
    DEFINE_PROP_CHR("chardev", NikonUSIOState, chr),
};

static const VMStateDescription nikon_usio_vmstate = {
    .name = "nikon-usio",
    .unmigratable = true,
};

static void nikon_usio_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

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
