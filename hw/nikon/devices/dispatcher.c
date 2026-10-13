/*
 * Finite byte-only 2016/2017 configuration and disconnected IRQ profile.
 * Cold zero, mask1 inhibition, W1C and child/top mapping are virtual laws.
 * Disabled setup status stays zero: no completed request or RX is invented.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "migration/vmstate.h" /* IWYU pragma: keep */
#include "hw/core/sysbus.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/irq.h"
#include "hw/nikon/devices.h"

OBJECT_DECLARE_SIMPLE_TYPE(NikonByteEventRouterState, NIKON_BYTE_EVENT_ROUTER)

typedef struct NikonByteEventRouterConfig {
    hwaddr offset;
    uint8_t mask;
} NikonByteEventRouterConfig;

static const NikonByteEventRouterConfig config_words[] = {
    { 0xc000, 0xff }, { 0xc018, 0xff }, { 0xc0a4, 0 },
    { 0xc0ac, 0xff }, { 0xc0b0, 0xff }, { 0xc0b4, 0xff },
    { 0xc0b8, 0xff }, { 0xc0bc, 0xff }, { 0xc0c0, 0xff },
    { 0xc0c4, 0xff }, { 0xc0c8, 0xff }, { 0xc0cc, 0xff },
    { 0x7fc, 3 }, { 0x10004, 0x1f }, { 0x1f81c, 0 },
    { 0x1f82c, 0xff }, { 0x1f830, 0xff }, { 0x1f834, 0xff },
    { 0x1f838, 0xff }, { 0x1f83c, 0xff }, { 0x1f840, 0xff },
    { 0x1f844, 0xff }, { 0x1f848, 0xff }, { 0x1f84c, 0xff },
};

struct NikonByteEventRouterState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    qemu_irq irq[2];
    uint8_t config[ARRAY_SIZE(config_words)];
    uint8_t pending[9], masks[9], inputs[4];
    uint8_t external_status;
    bool reported_error;
};

static int nikon_byte_event_router_config(hwaddr offset)
{
    unsigned i;

    for (i = 0; i < ARRAY_SIZE(config_words); i++) {
        if (config_words[i].offset == offset) {
            return i;
        }
    }
    return -1;
}

static int nikon_byte_event_router_child(hwaddr offset, hwaddr first)
{
    return offset >= first && offset <= first + 32 && !(offset & 3) ?
           (offset - first) / 4 : -1;
}

static uint8_t nikon_byte_event_router_top(NikonByteEventRouterState *s)
{
    /* Only simple children with complete ACK paths have fixture producers. */
    return ((s->pending[0] & ~s->masks[0]) ? 0x80 : 0) |
           ((s->pending[1] & ~s->masks[1]) ? 0x40 : 0) |
           ((s->pending[3] & ~s->masks[3]) ? 0x10 : 0) |
           ((s->pending[6] & ~s->masks[6]) ? 2 : 0);
}

static void nikon_byte_event_router_irq(NikonByteEventRouterState *s)
{
    /* Avoid an invented duplicate IRQ callback for the same child418. */
    qemu_set_irq(s->irq[0], !!(nikon_byte_event_router_top(s) & 0xd0));
    qemu_set_irq(s->irq[1], !!(s->pending[6] & ~s->masks[6]));
}

static void nikon_byte_event_router_input(void *opaque, int index, int level)
{
    NikonByteEventRouterState *s = opaque;
    static const unsigned children[] = { 0, 1, 3, 6 };
    unsigned group = index / 8;
    uint8_t bit = 1U << (index % 8);
    bool rising = level && !(s->inputs[group] & bit);

    s->inputs[group] = (s->inputs[group] & ~bit) | (level ? bit : 0);
    if (rising) {
        s->pending[children[group]] |= bit;
        nikon_byte_event_router_irq(s);
    }
}

static void nikon_byte_event_router_status_input(void *opaque, int index,
                                                 int level)
{
    static const unsigned bits[] = { 1, 4, 5, 6, 7 };
    NikonByteEventRouterState *s = opaque;
    uint8_t bit = 1U << bits[index];

    s->external_status = (s->external_status & ~bit) |
                         (level ? bit : 0);
}

static MemTxResult nikon_byte_event_router_error(NikonByteEventRouterState *s, hwaddr offset,
                                 unsigned size, bool write, uint64_t value)
{
    nikon_access_error(&s->reported_error, s->regs.addr + offset, size,
                      write, value,
                      "byte-event-router-access-or-activation-not-supported");
    return MEMTX_ERROR;
}

static MemTxResult nikon_byte_event_router_read(void *opaque, hwaddr offset,
                                uint64_t *value, unsigned size,
                                MemTxAttrs attrs)
{
    NikonByteEventRouterState *s = opaque;
    int raw_index = nikon_byte_event_router_child(offset, 0x400);
    int mask_index = nikon_byte_event_router_child(offset, 0x600);
    int config_index = nikon_byte_event_router_config(offset);

    *value = 0;
    if (size != 1) {
        return nikon_byte_event_router_error(s, offset, size, false, 0);
    }
    if (offset == 0x5c0) {
        *value = nikon_byte_event_router_top(s);
    } else if (offset == 0xc010) {
        /* External bits consumed by 1047f37e/1047f3be; unwired pins are low. */
        *value = s->external_status;
    } else if (offset == 0xc0a8 || offset == 0x1f824) {
        /* Disconnected disabled configuration never receives completion. */
        *value = 0;
    } else if (raw_index >= 0) {
        *value = s->pending[raw_index];
    } else if (mask_index >= 0) {
        *value = s->masks[mask_index];
    } else if (config_index >= 0) {
        *value = s->config[config_index];
    } else {
        return nikon_byte_event_router_error(s, offset, size, false, 0);
    }
    return MEMTX_OK;
}

static MemTxResult nikon_byte_event_router_write(void *opaque, hwaddr offset,
                                 uint64_t value, unsigned size,
                                 MemTxAttrs attrs)
{
    NikonByteEventRouterState *s = opaque;
    int raw_index = nikon_byte_event_router_child(offset, 0x400);
    int mask_index = nikon_byte_event_router_child(offset, 0x600);
    int config_index = nikon_byte_event_router_config(offset);

    if (size != 1) {
        return nikon_byte_event_router_error(s, offset, size, true, value);
    }
    if (offset == 0xc0a8 || offset == 0x1f824) {
        if (value) {
            return nikon_byte_event_router_error(s, offset, size, true, value);
        }
        /* Original status0 publication, not a timer/request-success input. */
    } else if (raw_index >= 0) {
        s->pending[raw_index] &= ~value;
    } else if (mask_index >= 0) {
        s->masks[mask_index] = value;
    } else if (config_index >= 0 &&
               !(value & ~config_words[config_index].mask) &&
               (offset != 0xc018 || value == 0xf2)) {
        s->config[config_index] = value;
    } else {
        return nikon_byte_event_router_error(s, offset, size, true, value);
    }
    nikon_byte_event_router_irq(s);
    return MEMTX_OK;
}

static const MemoryRegionOps nikon_byte_event_router_ops = {
    .read_with_attrs = nikon_byte_event_router_read,
    .write_with_attrs = nikon_byte_event_router_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
};

static void nikon_byte_event_router_reset(DeviceState *dev)
{
    NikonByteEventRouterState *s = NIKON_BYTE_EVENT_ROUTER(dev);

    memset(s->config, 0, sizeof(s->config));
    memset(s->masks, 0, sizeof(s->masks));
    memset(s->pending, 0, sizeof(s->pending));
    s->reported_error = false;
    /* Retain actual input levels; resetting does not generate new edges. */
    nikon_byte_event_router_irq(s);
}

static void nikon_byte_event_router_init(Object *obj)
{
    NikonByteEventRouterState *s = NIKON_BYTE_EVENT_ROUTER(obj);
    unsigned i;

    memory_region_init_io(&s->regs, obj, &nikon_byte_event_router_ops, s,
                          "nikon.byte-event-router", 0x20000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
    for (i = 0; i < 2; i++) {
        sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq[i]);
    }
    qdev_init_gpio_in_named(DEVICE(obj), nikon_byte_event_router_input,
                            "pending-bit", 32);
    qdev_init_gpio_in_named(DEVICE(obj), nikon_byte_event_router_status_input,
                            "external-status", 5);
}

static void nikon_byte_event_router_realize(DeviceState *dev, Error **errp)
{
    NikonByteEventRouterState *s = NIKON_BYTE_EVENT_ROUTER(dev);

    if (s->external_status & ~0xf2U) {
        error_setg(errp, "initial-status supports external bits 1 and 4..7");
    }
}

static const VMStateDescription nikon_byte_event_router_vmstate = {
    .name = "nikon-byte-event-router",
    .unmigratable = true,
};

static const Property nikon_byte_event_router_properties[] = {
    DEFINE_PROP_UINT8("initial-status", NikonByteEventRouterState,
                      external_status, 0),
};

static void nikon_byte_event_router_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = nikon_byte_event_router_realize;
    dc->vmsd = &nikon_byte_event_router_vmstate;
    dc->user_creatable = false;
    device_class_set_props(dc, nikon_byte_event_router_properties);
    device_class_set_legacy_reset(dc, nikon_byte_event_router_reset);
}

static const TypeInfo nikon_byte_event_router_type = {
    .name = TYPE_NIKON_BYTE_EVENT_ROUTER,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonByteEventRouterState),
    .instance_init = nikon_byte_event_router_init,
    .class_init = nikon_byte_event_router_class_init,
};

static void nikon_byte_event_router_register_types(void)
{
    type_register_static(&nikon_byte_event_router_type);
}
type_init(nikon_byte_event_router_register_types)
