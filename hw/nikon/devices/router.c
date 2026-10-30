/*
 * Nikon peripheral-event router: finite configuration and pending profile.
 * 102dc808/83c use paired set/clear ports; 102dc86c reads set-port state.
 * Cold state, W1C and mask polarity are explicit virtual conventions.
 * Request resync/fences and input-clock boundary mapping are finite functional
 * hypotheses; input-period-ns is virtual camera input, not a recovered rate.
 * No host callback, guest flag, DMA or completed pixel frame is supplied.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/timer.h"
#include "migration/vmstate.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/nikon/devices.h"

OBJECT_DECLARE_SIMPLE_TYPE(NikonPeripheralEventRouterState,
                          NIKON_PERIPHERAL_EVENT_ROUTER)

struct NikonPeripheralEventRouterState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    qemu_irq irq[3];
    qemu_irq automatic_rise;
    uint32_t config[0x330 / 4];
    uint32_t pending[3], levels[3];
    QEMUTimer *boundary_timer, *sync_timer, *aux_timer;
    int64_t boundary_deadline;
    uint32_t input_period_ns;
    bool high;
    bool reported_error;
};

static bool nikon_event_router_config(hwaddr offset, uint32_t *mask)
{
    unsigned lane;

    if (offset >= 0x70 && offset <= 0x9c) {
        *mask = UINT32_MAX;
        return true;
    }
    if (offset >= 0xb4 && offset <= 0xc4) {
        *mask = 0xff;
        return true;
    }
    for (lane = 0; lane < 6; lane++) {
        hwaddr relative = offset - lane * 0x60;

        switch (relative) {
        case 0xc8: case 0xcc: case 0xd0: case 0xd4:
        case 0xd8: case 0xdc: case 0xec:
            *mask = UINT32_MAX;
            return true;
        case 0xe8:
            *mask = 0xff;
            return true;
        }
    }
    switch (offset) {
    case 0: case 4:
        *mask = 0x3f;
        break;
    case 8: case 0xc:
        *mask = 0x7fff;
        break;
    case 0x10: case 0x14: case 0x18: case 0x1c:
        *mask = 1;
        break;
    case 0x20: case 0x24:
        *mask = 0x7ff;
        break;
    case 0xa4:
        *mask = 0x1f;
        break;
    case 0x328: case 0x32c:
        *mask = 0xff;
        break;
    case 0x34: case 0x40: case 0x4c:
    case 0xa0: case 0xa8: case 0xac: case 0xb0: case 0x314:
        *mask = UINT32_MAX;
        break;
    default:
        return false;
    }
    return true;
}

static void nikon_event_router_irq(NikonPeripheralEventRouterState *s)
{
    static const unsigned masks[] = { 0x34, 0x40, 0x4c };
    unsigned domain;

    for (domain = 0; domain < 3; domain++) {
        /* Domains1/2 and polarity are named virtual mask conventions. */
        qemu_set_irq(s->irq[domain],
                     !!(s->pending[domain] & s->config[masks[domain] / 4]));
    }
}

static bool nikon_event_router_clocked(NikonPeripheralEventRouterState *s)
{
    return s->input_period_ns && s->config[0x10 / 4] &&
           !(s->config[0] & 3) && !(s->config[0x70 / 4] & 2);
}

static void nikon_event_router_drive(NikonPeripheralEventRouterState *s,
                                     bool high)
{
    if (s->high != high) {
        s->high = high;
        /* Functional phase mapping, consumed by original 102d6b6a. */
        s->pending[1] |= high ? 0x80 : 0x10;
        nikon_event_router_irq(s);
    }
}

static void nikon_event_router_clock(NikonPeripheralEventRouterState *s)
{
    if (!s->input_period_ns || !s->config[0x10 / 4]) {
        timer_del(s->boundary_timer);
        s->high = false;
    } else if (s->config[0x70 / 4] & 2) {
        timer_del(s->boundary_timer);
        nikon_event_router_drive(s, s->config[0x70 / 4] & 1);
    } else if (s->config[0] & 3) {
        timer_del(s->boundary_timer);
        s->high = false;
    } else if (!timer_pending(s->boundary_timer)) {
        s->boundary_deadline = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                              s->input_period_ns - s->input_period_ns / 2;
        timer_mod(s->boundary_timer, s->boundary_deadline);
    }
}

static void nikon_event_router_boundary(void *opaque)
{
    NikonPeripheralEventRouterState *s = opaque;

    if (!nikon_event_router_clocked(s)) {
        return;
    }
    if (!s->high) {
        /* Address generators see the automatic input before its guest IRQ. */
        qemu_irq_pulse(s->automatic_rise);
    }
    nikon_event_router_drive(s, !s->high);
    s->boundary_deadline += s->high ? s->input_period_ns / 2 :
                           s->input_period_ns - s->input_period_ns / 2;
    timer_mod(s->boundary_timer, s->boundary_deadline);
}

static void nikon_event_router_resync(NikonPeripheralEventRouterState *s)
{
    timer_del(s->boundary_timer);
    if (!(s->config[0x70 / 4] & 2)) {
        s->high = false;
    }
    nikon_event_router_clock(s);
}

static void nikon_event_router_synchronized(void *opaque)
{
    NikonPeripheralEventRouterState *s = opaque;

    /* 102d986e clears special11, submits2c and waits for event20. */
    s->pending[1] |= 0x400;
    nikon_event_router_irq(s);
}

static void nikon_event_router_applied(void *opaque)
{
    NikonPeripheralEventRouterState *s = opaque;

    /* Auxiliary fence ACK bit0 is a virtual convention, not a frame. */
    s->pending[2] |= 1;
    nikon_event_router_irq(s);
}

static void nikon_event_router_event(void *opaque, int index, int level)
{
    NikonPeripheralEventRouterState *s = opaque;
    unsigned domain = index < 11 ? 0 : index < 43 ? 1 : 2;
    unsigned bit = index - (domain == 0 ? 0 : domain == 1 ? 11 : 43);
    uint32_t mask = 1u << bit;
    bool rising = level && !(s->levels[domain] & mask);

    s->levels[domain] = (s->levels[domain] & ~mask) | (level ? mask : 0);
    if (rising) {
        s->pending[domain] |= mask;
        nikon_event_router_irq(s);
    }
}

static int nikon_event_router_pending(hwaddr offset)
{
    switch (offset) {
    case 0x38: return 0;
    case 0x44: return 1;
    case 0x50: return 2;
    default: return -1;
    }
}

static MemTxResult nikon_event_router_error(NikonPeripheralEventRouterState *s,
                                           hwaddr offset, unsigned size,
                                           bool write, uint64_t value)
{
    nikon_access_error(&s->reported_error, s->regs.addr + offset, size,
                      write, value,
                      "peripheral-event-router-access-or-activation-not-supported");
    return MEMTX_ERROR;
}

static MemTxResult nikon_event_router_read(void *opaque, hwaddr offset,
                                uint64_t *value, unsigned size,
                                MemTxAttrs attrs)
{
    NikonPeripheralEventRouterState *s = opaque;
    uint32_t mask;
    int domain;

    *value = 0;
    if (size != 4 || (offset & 3)) {
        return nikon_event_router_error(s, offset, size, false, 0);
    }
    domain = nikon_event_router_pending(offset);
    if (domain >= 0) {
        *value = s->pending[domain];
    } else if (nikon_event_router_config(offset, &mask)) {
        /* Clear aliases have no recovered read consumer. */
        if (offset <= 0x24 && (offset & 4)) {
            return nikon_event_router_error(s, offset, size, false, 0);
        }
        *value = s->config[offset / 4];
    } else {
        return nikon_event_router_error(s, offset, size, false, 0);
    }
    return MEMTX_OK;
}

static MemTxResult nikon_event_router_write(void *opaque, hwaddr offset,
                                 uint64_t value, unsigned size,
                                 MemTxAttrs attrs)
{
    NikonPeripheralEventRouterState *s = opaque;
    uint32_t mask;
    int domain;

    if (size != 4 || (offset & 3)) {
        return nikon_event_router_error(s, offset, size, true, value);
    }
    if (offset >= 0x28 && offset <= 0x30) {
        if (value != 1) {
            return nikon_event_router_error(s, offset, size, true, value);
        }
        if (offset != 0x30) {
            nikon_event_router_resync(s);
        }
        if (offset != 0x28) {
            QEMUTimer *timer = offset == 0x2c ? s->sync_timer : s->aux_timer;

            if (!timer_pending(timer)) {
                /* Deferred register fence, independent of the input clock. */
                timer_mod(timer,
                          qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000);
            }
        }
        return MEMTX_OK;
    }
    domain = nikon_event_router_pending(offset);
    if (domain >= 0) {
        s->pending[domain] &= ~value;
    } else if (nikon_event_router_config(offset, &mask)) {
        if (offset == 0xa4 ?
            ((value ^ s->config[offset / 4]) & ~mask) != 0 :
            (value & ~mask) != 0) {
            return nikon_event_router_error(s, offset, size, true, value);
        }
        if (offset <= 0x24) {
            unsigned index = (offset & ~4U) / 4;

            if (offset & 4) {
                s->config[index] &= ~value;
            } else {
                s->config[index] |= value;
            }
        } else {
            if (offset == 0x70 && (s->config[offset / 4] & 2) &&
                !(value & 2) && s->input_period_ns && s->config[0x10 / 4]) {
                nikon_event_router_drive(s, false);
            }
            s->config[offset / 4] = value;
        }
    } else {
        return nikon_event_router_error(s, offset, size, true, value);
    }
    if (offset <= 0x24 || offset == 0x70) {
        nikon_event_router_clock(s);
    }
    nikon_event_router_irq(s);
    return MEMTX_OK;
}

static const MemoryRegionOps nikon_event_router_ops = {
    .read_with_attrs = nikon_event_router_read,
    .write_with_attrs = nikon_event_router_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
};

static void nikon_event_router_reset(DeviceState *dev)
{
    NikonPeripheralEventRouterState *s = NIKON_PERIPHERAL_EVENT_ROUTER(dev);

    memset(s->config, 0, sizeof(s->config));
    memset(s->pending, 0, sizeof(s->pending));
    timer_del(s->boundary_timer);
    timer_del(s->sync_timer);
    timer_del(s->aux_timer);
    s->high = false;
    s->boundary_deadline = 0;
    s->reported_error = false;
    /* Reset preserves actual provider levels; it cannot invent a new edge. */
    nikon_event_router_irq(s);
}

static void nikon_event_router_init(Object *obj)
{
    NikonPeripheralEventRouterState *s = NIKON_PERIPHERAL_EVENT_ROUTER(obj);
    unsigned domain;

    s->boundary_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                     nikon_event_router_boundary, s);
    s->sync_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                 nikon_event_router_synchronized, s);
    s->aux_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                nikon_event_router_applied, s);
    memory_region_init_io(&s->regs, obj, &nikon_event_router_ops, s,
                          "nikon.peripheral-event-router", 0x330);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
    for (domain = 0; domain < 3; domain++) {
        sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq[domain]);
    }
    qdev_init_gpio_in_named(DEVICE(obj), nikon_event_router_event,
                           "external-pending", 75);
    qdev_init_gpio_out_named(DEVICE(obj), &s->automatic_rise,
                            "automatic-rise", 1);
}

static void nikon_event_router_realize(DeviceState *dev, Error **errp)
{
    NikonPeripheralEventRouterState *s = NIKON_PERIPHERAL_EVENT_ROUTER(dev);

    if (s->input_period_ns == 1) {
        error_setg(errp, "router input-period-ns must be0 or at least2");
    }
}

static void nikon_event_router_finalize(Object *obj)
{
    NikonPeripheralEventRouterState *s = NIKON_PERIPHERAL_EVENT_ROUTER(obj);

    timer_free(s->boundary_timer);
    timer_free(s->sync_timer);
    timer_free(s->aux_timer);
}

static const Property nikon_event_router_properties[] = {
    DEFINE_PROP_UINT32("input-period-ns", NikonPeripheralEventRouterState,
                       input_period_ns, 0),
};

static const VMStateDescription nikon_event_router_vmstate = {
    .name = "nikon-peripheral-event-router",
    .unmigratable = true,
};

static void nikon_event_router_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &nikon_event_router_vmstate;
    dc->user_creatable = false;
    dc->realize = nikon_event_router_realize;
    dc->unrealize = nikon_event_router_reset;
    device_class_set_props(dc, nikon_event_router_properties);
    device_class_set_legacy_reset(dc, nikon_event_router_reset);
}

static const TypeInfo nikon_event_router_type = {
    .name = TYPE_NIKON_PERIPHERAL_EVENT_ROUTER,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonPeripheralEventRouterState),
    .instance_init = nikon_event_router_init,
    .instance_finalize = nikon_event_router_finalize,
    .class_init = nikon_event_router_class_init,
};

static void nikon_event_router_register_types(void)
{
    type_register_static(&nikon_event_router_type);
}
type_init(nikon_event_router_register_types)
