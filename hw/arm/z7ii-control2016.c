/*
 * Finite byte-only 2016/2017 configuration and disconnected IRQ profile.
 * Cold zero, mask1 inhibition, W1C and child/top mapping are virtual laws.
 * Disabled setup status stays zero: no completed request or RX is invented.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "z7ii.h"

#define CONTROL_BASE 0x20160000ULL
OBJECT_DECLARE_SIMPLE_TYPE(Z7IIControl2016State, Z7II_CONTROL2016)

typedef struct Z7IIControl2016Config {
    hwaddr offset;
    uint8_t mask;
} Z7IIControl2016Config;

static const Z7IIControl2016Config config_words[] = {
    { 0xc000, 0xff }, { 0xc018, 0xff }, { 0xc0a4, 0 },
    { 0xc0ac, 0xff }, { 0xc0b0, 0xff }, { 0xc0b4, 0xff },
    { 0xc0b8, 0xff }, { 0xc0bc, 0xff }, { 0xc0c0, 0xff },
    { 0xc0c4, 0xff }, { 0xc0c8, 0xff }, { 0xc0cc, 0xff },
    { 0x7fc, 3 }, { 0x10004, 0x1f }, { 0x1f81c, 0 },
    { 0x1f82c, 0xff }, { 0x1f830, 0xff }, { 0x1f834, 0xff },
    { 0x1f838, 0xff }, { 0x1f83c, 0xff }, { 0x1f840, 0xff },
    { 0x1f844, 0xff }, { 0x1f848, 0xff }, { 0x1f84c, 0xff },
};

struct Z7IIControl2016State {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    qemu_irq irq[2];
    uint8_t config[ARRAY_SIZE(config_words)];
    uint8_t pending[9], masks[9], inputs[4];
    bool reported_error;
};

static int z7ii_2016_config(hwaddr offset)
{
    unsigned i;

    for (i = 0; i < ARRAY_SIZE(config_words); i++) {
        if (config_words[i].offset == offset) {
            return i;
        }
    }
    return -1;
}

static int z7ii_2016_child(hwaddr offset, hwaddr first)
{
    return offset >= first && offset <= first + 32 && !(offset & 3) ?
           (offset - first) / 4 : -1;
}

static uint8_t z7ii_2016_top(Z7IIControl2016State *s)
{
    /* Only simple children with complete ACK paths have fixture producers. */
    return ((s->pending[0] & ~s->masks[0]) ? 0x80 : 0) |
           ((s->pending[1] & ~s->masks[1]) ? 0x40 : 0) |
           ((s->pending[3] & ~s->masks[3]) ? 0x10 : 0) |
           ((s->pending[6] & ~s->masks[6]) ? 2 : 0);
}

static void z7ii_2016_irq(Z7IIControl2016State *s)
{
    /* Avoid an invented duplicate IRQ callback for the same child418. */
    qemu_set_irq(s->irq[0], !!(z7ii_2016_top(s) & 0xd0));
    qemu_set_irq(s->irq[1], !!(s->pending[6] & ~s->masks[6]));
}

static void z7ii_2016_input(void *opaque, int index, int level)
{
    Z7IIControl2016State *s = opaque;
    static const unsigned children[] = { 0, 1, 3, 6 };
    unsigned group = index / 8;
    uint8_t bit = 1U << (index % 8);
    bool rising = level && !(s->inputs[group] & bit);

    s->inputs[group] = (s->inputs[group] & ~bit) | (level ? bit : 0);
    if (rising) {
        s->pending[children[group]] |= bit;
        z7ii_2016_irq(s);
    }
}

static MemTxResult z7ii_2016_error(Z7IIControl2016State *s, hwaddr offset,
                                 unsigned size, bool write, uint64_t value)
{
    z7ii_access_error(&s->reported_error, CONTROL_BASE + offset, size,
                      write, value,
                      "control2016-access-or-activation-not-supported");
    return MEMTX_ERROR;
}

static MemTxResult z7ii_2016_read(void *opaque, hwaddr offset,
                                uint64_t *value, unsigned size,
                                MemTxAttrs attrs)
{
    Z7IIControl2016State *s = opaque;
    int raw_index = z7ii_2016_child(offset, 0x400);
    int mask_index = z7ii_2016_child(offset, 0x600);
    int config_index = z7ii_2016_config(offset);

    *value = 0;
    if (size != 1) {
        return z7ii_2016_error(s, offset, size, false, 0);
    }
    if (offset == 0x5c0) {
        *value = z7ii_2016_top(s);
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
        return z7ii_2016_error(s, offset, size, false, 0);
    }
    return MEMTX_OK;
}

static MemTxResult z7ii_2016_write(void *opaque, hwaddr offset,
                                 uint64_t value, unsigned size,
                                 MemTxAttrs attrs)
{
    Z7IIControl2016State *s = opaque;
    int raw_index = z7ii_2016_child(offset, 0x400);
    int mask_index = z7ii_2016_child(offset, 0x600);
    int config_index = z7ii_2016_config(offset);

    if (size != 1) {
        return z7ii_2016_error(s, offset, size, true, value);
    }
    if (offset == 0xc0a8 || offset == 0x1f824) {
        if (value) {
            return z7ii_2016_error(s, offset, size, true, value);
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
        return z7ii_2016_error(s, offset, size, true, value);
    }
    z7ii_2016_irq(s);
    return MEMTX_OK;
}

static const MemoryRegionOps z7ii_2016_ops = {
    .read_with_attrs = z7ii_2016_read,
    .write_with_attrs = z7ii_2016_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
};

static void z7ii_2016_reset(DeviceState *dev)
{
    Z7IIControl2016State *s = Z7II_CONTROL2016(dev);

    memset(s->config, 0, sizeof(s->config));
    memset(s->masks, 0, sizeof(s->masks));
    memset(s->pending, 0, sizeof(s->pending));
    s->reported_error = false;
    /* Retain actual input levels; resetting does not generate new edges. */
    z7ii_2016_irq(s);
}

static void z7ii_2016_init(Object *obj)
{
    Z7IIControl2016State *s = Z7II_CONTROL2016(obj);
    unsigned i;

    memory_region_init_io(&s->regs, obj, &z7ii_2016_ops, s,
                          "z7ii.control2016", 0x20000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
    for (i = 0; i < 2; i++) {
        sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq[i]);
    }
    qdev_init_gpio_in_named(DEVICE(obj), z7ii_2016_input, "pending-bit", 32);
}

static const VMStateDescription z7ii_2016_vmstate = {
    .name = "z7ii-control2016",
    .unmigratable = true,
};

static void z7ii_2016_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &z7ii_2016_vmstate;
    dc->user_creatable = false;
    device_class_set_legacy_reset(dc, z7ii_2016_reset);
}

static const TypeInfo z7ii_2016_type = {
    .name = TYPE_Z7II_CONTROL2016,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Z7IIControl2016State),
    .instance_init = z7ii_2016_init,
    .class_init = z7ii_2016_class_init,
};

static void z7ii_2016_register_types(void)
{
    type_register_static(&z7ii_2016_type);
}
type_init(z7ii_2016_register_types)
