/*
 * Finite original 20202 control/configuration and external-event profile.
 * Reset, staged readback, request completion and event polarity are virtual
 * conventions; no physical pipeline, frame source or clock is inferred.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "z7ii.h"

#define CONTROL_BASE 0x20202000ULL
#define CONFIG_WORDS (0x254 / 4)
OBJECT_DECLARE_SIMPLE_TYPE(Z7IIControl20202State, Z7II_CONTROL20202)

struct Z7IIControl20202State {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    qemu_irq irq;
    uint32_t applied[CONFIG_WORDS], staged[CONFIG_WORDS];
    uint32_t pending, mask;
    uint8_t requests, inputs;
    bool enabled, configuring, reported_error;
};

/* Source-generated masks, not a claim about physical writable bits. */
static bool z7ii_control_config(hwaddr offset, uint32_t *mask, bool *sign16)
{
    *sign16 = false;
    if ((offset >= 0x100 && offset <= 0x11c) ||
        (offset >= 0x124 && offset <= 0x140) ||
        offset == 0x148 || offset == 0x14c) {
        *mask = 0x03ff03ff;
        return true;
    }
    switch (offset) {
    case 0x120: case 0x144:
        *sign16 = true;
        *mask = UINT32_MAX;
        break;
    case 0x40: case 0x4c: case 0x68: case 0x6c:
    case 0x80: case 0x84: case 0x88: case 0xa8: case 0xac:
    case 0xbc: case 0xc0: case 0xc8:
    case 0x208: case 0x20c: case 0x218: case 0x22c:
        *mask = 0xff;
        break;
    case 0x44: case 0x48: case 0xa0: case 0xa4: case 0xb4: case 0x210:
        *mask = 0xffff;
        break;
    case 0x50: case 0x54: case 0x58: case 0x5c:
    case 0x204: case 0x214: case 0x220: case 0x228: case 0x230:
    case 0x234: case 0x238: case 0x23c: case 0x240:
    case 0x244: case 0x248: case 0x24c: case 0x250:
        *mask = UINT32_MAX;
        break;
    case 0x60:
        *mask = 0x001f0007;
        break;
    case 0x64:
        *mask = 0x07070707;
        break;
    case 0x8c: case 0x90: case 0x94:
        *mask = 0x1fff7fff;
        break;
    case 0x98:
        *mask = 0xf;
        break;
    case 0x9c:
        *mask = 0x003f3f3f;
        break;
    case 0xb0: case 0xc4:
        *mask = 0x00ff0fff;
        break;
    case 0xb8:
        *mask = 0x00003f3f;
        break;
    case 0xcc:
        *mask = 0x3fff3fff;
        break;
    case 0xd0:
        *mask = 0x000f003f;
        break;
    case 0xd4:
        *mask = 0x773ff3ff;
        break;
    case 0x200: case 0x224:
        *mask = 1;
        break;
    default:
        return false;
    }
    return true;
}

static void z7ii_control_irq(Z7IIControl20202State *s)
{
    qemu_set_irq(s->irq, s->enabled && !!(s->pending & s->mask));
}

static void z7ii_control_event(void *opaque, int index, int level)
{
    Z7IIControl20202State *s = opaque;
    /* Named fixture inputs: req08 -> bit1, req14 -> bit2, req0c -> bit16. */
    static const uint8_t request[] = { 1, 4, 2 };
    static const uint32_t pending[] = { 2, 4, 0x10000 };
    bool rising = level && !(s->inputs & (1 << index));

    s->inputs = (s->inputs & ~(1 << index)) | (!!level << index);
    if (rising && s->enabled && (s->requests & request[index])) {
        s->requests &= ~request[index];
        s->pending |= pending[index];
        z7ii_control_irq(s);
    }
}

static MemTxResult z7ii_control_error(Z7IIControl20202State *s,
                                    hwaddr offset, unsigned size,
                                    bool write, uint64_t value)
{
    z7ii_access_error(&s->reported_error, CONTROL_BASE + offset, size,
                      write, value,
                      "control20202-access-or-phase-not-supported");
    return MEMTX_ERROR;
}

static MemTxResult z7ii_control_read(void *opaque, hwaddr offset,
                                   uint64_t *value, unsigned size,
                                   MemTxAttrs attrs)
{
    Z7IIControl20202State *s = opaque;
    uint32_t mask;
    bool sign16;

    *value = 0;
    if (size != 4 || (offset & 3)) {
        return z7ii_control_error(s, offset, size, false, 0);
    }
    if (z7ii_control_config(offset, &mask, &sign16)) {
        /* Named applied-state view. Staged fields are never published early. */
        *value = s->applied[offset / 4];
        return MEMTX_OK;
    }
    switch (offset) {
    case 8:
        *value = !!(s->requests & 1);
        break;
    case 0x20:
        *value = s->mask;
        break;
    case 0x2c:
        *value = s->pending;
        break;
    default:
        return z7ii_control_error(s, offset, size, false, 0);
    }
    return MEMTX_OK;
}

static MemTxResult z7ii_control_write(void *opaque, hwaddr offset,
                                    uint64_t value, unsigned size,
                                    MemTxAttrs attrs)
{
    Z7IIControl20202State *s = opaque;
    uint32_t mask;
    bool sign16;

    if (size != 4 || (offset & 3)) {
        return z7ii_control_error(s, offset, size, true, value);
    }
    if (z7ii_control_config(offset, &mask, &sign16)) {
        if ((value & ~mask) ||
            (sign16 && value != (uint32_t)(int32_t)(int16_t)value) ||
            s->requests) {
            return z7ii_control_error(s, offset, size, true, value);
        }
        if (s->configuring) {
            s->staged[offset / 4] = value;
        } else {
            /* Original subblock setters also write without a bracket. */
            s->applied[offset / 4] = value;
        }
        return MEMTX_OK;
    }
    switch (offset) {
    case 0: case 4:
        if (value != 1) {
            return z7ii_control_error(s, offset, size, true, value);
        }
        /* Cancellation does not acknowledge a separately latched event. */
        s->requests = 0;
        break;
    case 8: case 0xc: case 0x14:
        if (value != 1 || !s->enabled || s->configuring) {
            return z7ii_control_error(s, offset, size, true, value);
        }
        s->requests |= offset == 8 ? 1 : offset == 0xc ? 2 : 4;
        break;
    case 0x10:
        if (value > 1 || s->requests || (!value && s->configuring) ||
            (value && !s->configuring)) {
            return z7ii_control_error(s, offset, size, true, value);
        }
        if (!value) {
            memcpy(s->staged, s->applied, sizeof(s->applied));
            s->configuring = true;
        } else {
            memcpy(s->applied, s->staged, sizeof(s->applied));
            s->configuring = false;
        }
        break;
    case 0x18:
        if (value > 1) {
            return z7ii_control_error(s, offset, size, true, value);
        }
        s->enabled = value;
        if (!value) {
            s->requests = 0;
        }
        break;
    case 0x20:
        s->mask = value;
        break;
    case 0x2c:
        s->pending &= ~(value & 0x10006);
        break;
    default:
        return z7ii_control_error(s, offset, size, true, value);
    }
    z7ii_control_irq(s);
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
    Z7IIControl20202State *s = Z7II_CONTROL20202(dev);

    memset(s->applied, 0, sizeof(s->applied));
    memset(s->staged, 0, sizeof(s->staged));
    s->pending = s->mask = s->requests = 0;
    s->enabled = s->configuring = s->reported_error = false;
    /* Preserve actual external levels; reset cannot invent a rising edge. */
    z7ii_control_irq(s);
}

static void z7ii_control_init(Object *obj)
{
    Z7IIControl20202State *s = Z7II_CONTROL20202(obj);

    memory_region_init_io(&s->regs, obj, &z7ii_control_ops, s,
                          "z7ii.control20202", 0x254);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
    qdev_init_gpio_in_named(DEVICE(obj), z7ii_control_event,
                           "external-event", 3);
}

static const VMStateDescription z7ii_control_vmstate = {
    .name = "z7ii-control20202",
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
    .name = TYPE_Z7II_CONTROL20202,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Z7IIControl20202State),
    .instance_init = z7ii_control_init,
    .class_init = z7ii_control_class_init,
};

static void z7ii_control_register_types(void)
{
    type_register_static(&z7ii_control_type);
}
type_init(z7ii_control_register_types)
