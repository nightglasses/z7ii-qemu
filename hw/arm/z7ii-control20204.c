/*
 * Finite 20204 configuration/table and disconnected data-input profile.
 * Cold state, retained controls, W1C and fixture sampling are virtual laws;
 * no physical reset, frame format or activation meaning is inferred.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "z7ii.h"

#define CONTROL_BASE 0x20204000ULL
#define CONFIG_WORDS (0x42c / 4)
OBJECT_DECLARE_SIMPLE_TYPE(Z7IIControl20204State, Z7II_CONTROL20204)

struct Z7IIControl20204State {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    qemu_irq irq;
    uint32_t config[CONFIG_WORDS];
    uint32_t pending, data_levels[3], data_seen[3], sample[3];
    uint8_t events;
    bool sample_valid, reported_error;
};

/* Generated fields, with retained RMW bits; not physical mask proofs. */
static bool z7ii_control_config(hwaddr offset, uint32_t *mask, bool *rmw)
{
    *rmw = false;
    if ((offset >= 0x5c && offset <= 0x94) ||
        (offset >= 0xd8 && offset <= 0x110)) {
        *mask = 0x3fff;
        return true;
    }
    if ((offset >= 0x98 && offset <= 0xd0) ||
        (offset >= 0x114 && offset <= 0x14c) ||
        (offset >= 0x150 && offset <= 0x180)) {
        *mask = 0xf;
        return true;
    }
    if (offset >= 0x200 && offset <= 0x3fc) {
        *mask = 0x3fff3fff;
        return true;
    }
    switch (offset) {
    case 0: case 4: case 8:
        *mask = 1;
        *rmw = true;
        break;
    case 0x40:
        *mask = 0x7003f;
        *rmw = true;
        break;
    case 0x44:
        *mask = 0x70003;
        *rmw = true;
        break;
    case 0x48:
        *mask = 3;
        *rmw = true;
        break;
    case 0x20: case 0x400: case 0x420:
        *mask = UINT32_MAX;
        break;
    case 0x4c: case 0x50:
        *mask = 0x7fff;
        break;
    case 0x54:
        *mask = 0x0fffffff;
        break;
    case 0x58:
        *mask = 0x3fff;
        break;
    case 0xd4: case 0x190:
        *mask = 0x1fffffff;
        break;
    case 0x184: case 0x1a0:
        *mask = 1;
        break;
    case 0x188:
        *mask = 0xf;
        break;
    case 0x18c:
        *mask = 0xff;
        break;
    case 0x404: case 0x424:
        *mask = 0xffff;
        break;
    default:
        return false;
    }
    return true;
}

static void z7ii_control_irq(Z7IIControl20204State *s)
{
    /* Offset20 is a full guest parameter, with no proven mask semantics. */
    qemu_set_irq(s->irq, !!s->pending);
}

static void z7ii_control_data(void *opaque, int index, int level)
{
    Z7IIControl20204State *s = opaque;
    unsigned word = index / 32;
    uint32_t bit = 1U << (index % 32);

    s->data_levels[word] = (s->data_levels[word] & ~bit) | (level ? bit : 0);
    s->data_seen[word] |= bit;
}

static void z7ii_control_event(void *opaque, int index, int level)
{
    Z7IIControl20204State *s = opaque;
    static const uint32_t pending[] = { 0x100, 0x20000, 0x2000000 };
    bool rising = level && !(s->events & (1 << index));

    s->events = (s->events & ~(1 << index)) | (!!level << index);
    if (!rising || (!index && (s->data_seen[0] != UINT32_MAX ||
                              s->data_seen[1] != UINT32_MAX ||
                              s->data_seen[2] != UINT32_MAX))) {
        return;
    }
    if (!index) {
        /* Complete explicit publication, not an invented hardware frame. */
        memcpy(s->sample, s->data_levels, sizeof(s->sample));
        s->sample_valid = true;
    }
    s->pending |= pending[index];
    z7ii_control_irq(s);
}

static MemTxResult z7ii_control_error(Z7IIControl20204State *s,
                                    hwaddr offset, unsigned size,
                                    bool write, uint64_t value)
{
    z7ii_access_error(&s->reported_error, CONTROL_BASE + offset, size,
                      write, value,
                      "control20204-access-or-activation-not-supported");
    return MEMTX_ERROR;
}

static MemTxResult z7ii_control_read(void *opaque, hwaddr offset,
                                   uint64_t *value, unsigned size,
                                   MemTxAttrs attrs)
{
    Z7IIControl20204State *s = opaque;
    uint32_t mask;
    bool rmw;

    *value = 0;
    if (size != 4 || (offset & 3)) {
        return z7ii_control_error(s, offset, size, false, 0);
    }
    if (z7ii_control_config(offset, &mask, &rmw)) {
        *value = s->config[offset / 4];
    } else if (offset == 0x2c) {
        *value = s->pending;
    } else if (offset >= 0x194 && offset <= 0x19c && s->sample_valid) {
        *value = s->sample[(offset - 0x194) / 4];
    } else {
        return z7ii_control_error(s, offset, size, false, 0);
    }
    return MEMTX_OK;
}

static MemTxResult z7ii_control_write(void *opaque, hwaddr offset,
                                    uint64_t value, unsigned size,
                                    MemTxAttrs attrs)
{
    Z7IIControl20204State *s = opaque;
    uint32_t mask;
    bool rmw;

    if (size != 4 || (offset & 3)) {
        return z7ii_control_error(s, offset, size, true, value);
    }
    if (z7ii_control_config(offset, &mask, &rmw)) {
        uint32_t changed = rmw ? value ^ s->config[offset / 4] : value;

        if (changed & ~mask) {
            return z7ii_control_error(s, offset, size, true, value);
        }
        s->config[offset / 4] = value;
    } else if (offset == 0x2c) {
        /* Original ACK precedes all three data reads: preserve the snapshot. */
        s->pending &= ~(value & 0x2020100);
        z7ii_control_irq(s);
    } else {
        return z7ii_control_error(s, offset, size, true, value);
    }
    return MEMTX_OK;
}

static const MemoryRegionOps z7ii_control_ops = {
    .read_with_attrs = z7ii_control_read,
    .write_with_attrs = z7ii_control_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
};

static void z7ii_control_reset(DeviceState *dev)
{
    Z7IIControl20204State *s = Z7II_CONTROL20204(dev);

    memset(s->config, 0, sizeof(s->config));
    memset(s->data_seen, 0, sizeof(s->data_seen));
    memset(s->sample, 0, sizeof(s->sample));
    s->pending = 0;
    s->sample_valid = s->reported_error = false;
    /* Keep actual pin levels; require new publication without a reset edge. */
    z7ii_control_irq(s);
}

static void z7ii_control_init(Object *obj)
{
    Z7IIControl20204State *s = Z7II_CONTROL20204(obj);

    memory_region_init_io(&s->regs, obj, &z7ii_control_ops, s,
                          "z7ii.control20204", 0x42c);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
    qdev_init_gpio_in_named(DEVICE(obj), z7ii_control_event,
                           "external-event", 3);
    qdev_init_gpio_in_named(DEVICE(obj), z7ii_control_data, "data-bit", 96);
}

static const VMStateDescription z7ii_control_vmstate = {
    .name = "z7ii-control20204",
    .unmigratable = true,
};

static void z7ii_control_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &z7ii_control_vmstate;
    dc->user_creatable = false;
    device_class_set_legacy_reset(dc, z7ii_control_reset);
}

static const TypeInfo z7ii_control_type = {
    .name = TYPE_Z7II_CONTROL20204,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Z7IIControl20204State),
    .instance_init = z7ii_control_init,
    .class_init = z7ii_control_class_init,
};

static void z7ii_control_register_types(void)
{
    type_register_static(&z7ii_control_type);
}
type_init(z7ii_control_register_types)
