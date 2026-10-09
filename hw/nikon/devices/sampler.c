/*
 * Finite 20201 configuration and disconnected external-data profile.
 * Cold state, retained readback, W1C and fixture sampling are virtual laws;
 * no physical frame source, pulse reset or activation meaning is inferred.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "nikon.h"

#define CONFIG_WORDS (0x1a4 / 4)
OBJECT_DECLARE_SIMPLE_TYPE(NikonDataSamplerState, NIKON_DATA_SAMPLER)

struct NikonDataSamplerState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    qemu_irq irq;
    uint32_t config[CONFIG_WORDS];
    uint32_t pending, data_levels, data_seen, sample;
    uint8_t events;
    bool sample_valid, reported_error;
};

/* Source-generated field bounds, not physical writable-mask claims. */
static bool nikon_control_config(hwaddr offset, uint32_t *mask)
{
    if ((offset >= 0xa0 && offset <= 0xb4) ||
        (offset >= 0x130 && offset <= 0x14c) ||
        (offset >= 0x178 && offset <= 0x194)) {
        *mask = UINT32_MAX;
        return true;
    }
    switch (offset) {
    case 4: case 0x4c: case 0xc4: case 0x11c: case 0x164:
        *mask = 1;
        break;
    case 0x20: case 0x50: case 0x54: case 0x5c: case 0x64:
    case 0xc8: case 0xcc: case 0x104: case 0x108: case 0x10c:
    case 0x110: case 0x118: case 0x120: case 0x124: case 0x128:
    case 0x12c: case 0x160: case 0x168: case 0x16c: case 0x170:
        *mask = UINT32_MAX;
        break;
    case 0x60:
        *mask = 0xff;
        break;
    case 0xb8: case 0xbc: case 0xc0:
        *mask = 0x7ff;
        break;
    case 0x100:
        *mask = 0xd;
        break;
    default:
        return false;
    }
    return true;
}

static void nikon_control_irq(NikonDataSamplerState *s)
{
    /* Offset20 is a retained parameter, with no proven masking semantics. */
    qemu_set_irq(s->irq, !!s->pending);
}

static void nikon_control_data(void *opaque, int index, int level)
{
    NikonDataSamplerState *s = opaque;
    uint32_t bit = 1U << index;

    s->data_levels = (s->data_levels & ~bit) | (level ? bit : 0);
    s->data_seen |= bit;
}

static void nikon_control_event(void *opaque, int index, int level)
{
    NikonDataSamplerState *s = opaque;
    static const uint32_t pending[] = { 1, 2, 0x40 };
    bool rising = level && !(s->events & (1 << index));

    s->events = (s->events & ~(1 << index)) | (!!level << index);
    if (!rising || (!index && s->data_seen != UINT32_MAX)) {
        return;
    }
    if (!index) {
        /* Explicit fixture publication, never an invented frame or reply. */
        s->sample = s->data_levels;
        s->sample_valid = true;
    }
    s->pending |= pending[index];
    nikon_control_irq(s);
}

static MemTxResult nikon_control_error(NikonDataSamplerState *s,
                                    hwaddr offset, unsigned size,
                                    bool write, uint64_t value)
{
    nikon_access_error(&s->reported_error, s->regs.addr + offset, size,
                      write, value,
                      "data-sampler-access-or-activation-not-supported");
    return MEMTX_ERROR;
}

static MemTxResult nikon_control_read(void *opaque, hwaddr offset,
                                   uint64_t *value, unsigned size,
                                   MemTxAttrs attrs)
{
    NikonDataSamplerState *s = opaque;
    uint32_t mask;

    *value = 0;
    if (size != 4 || (offset & 3)) {
        return nikon_control_error(s, offset, size, false, 0);
    }
    if (nikon_control_config(offset, &mask) || offset == 0x44 ||
        offset == 0x48) {
        *value = s->config[offset / 4];
    } else if (offset == 0x2c) {
        *value = s->pending;
    } else if (offset == 0x1a0 && s->sample_valid) {
        *value = s->sample;
    } else {
        return nikon_control_error(s, offset, size, false, 0);
    }
    return MEMTX_OK;
}

static MemTxResult nikon_control_write(void *opaque, hwaddr offset,
                                    uint64_t value, unsigned size,
                                    MemTxAttrs attrs)
{
    NikonDataSamplerState *s = opaque;
    uint32_t mask;

    if (size != 4 || (offset & 3)) {
        return nikon_control_error(s, offset, size, true, value);
    }
    if (nikon_control_config(offset, &mask)) {
        if (value & ~mask) {
            return nikon_control_error(s, offset, size, true, value);
        }
        s->config[offset / 4] = value;
    } else if (offset == 0x44 && value >= 1 && value <= 3) {
        /* Original pairs are sequential, with valid intermediate state. */
        s->config[offset / 4] = value;
    } else if (offset == 0x48 && (value == 0 || value == 1 || value == 7)) {
        s->config[offset / 4] = value;
    } else if (offset == 0x2c) {
        /* ISR acknowledges before reading1A0: ACK must retain the sample. */
        s->pending &= ~(value & 0x43);
        nikon_control_irq(s);
    } else if (offset != 0 || value != 0) {
        return nikon_control_error(s, offset, size, true, value);
    }
    /* Deasserted00 has no inferred reset, pending or configuration effect. */
    return MEMTX_OK;
}

static const MemoryRegionOps nikon_control_ops = {
    .read_with_attrs = nikon_control_read,
    .write_with_attrs = nikon_control_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
};

static void nikon_control_reset(DeviceState *dev)
{
    NikonDataSamplerState *s = NIKON_DATA_SAMPLER(dev);

    memset(s->config, 0, sizeof(s->config));
    s->pending = s->data_seen = s->sample = 0;
    s->sample_valid = s->reported_error = false;
    /* Preserve pin levels without creating an edge; require new publication. */
    nikon_control_irq(s);
}

static void nikon_control_init(Object *obj)
{
    NikonDataSamplerState *s = NIKON_DATA_SAMPLER(obj);

    memory_region_init_io(&s->regs, obj, &nikon_control_ops, s,
                          "nikon.data-sampler", 0x1a4);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
    qdev_init_gpio_in_named(DEVICE(obj), nikon_control_event,
                           "external-event", 3);
    qdev_init_gpio_in_named(DEVICE(obj), nikon_control_data, "data-bit", 32);
}

static const VMStateDescription nikon_control_vmstate = {
    .name = "nikon-data-sampler",
    .unmigratable = true,
};

static void nikon_control_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &nikon_control_vmstate;
    dc->user_creatable = false;
    device_class_set_legacy_reset(dc, nikon_control_reset);
}

static const TypeInfo nikon_control_type = {
    .name = TYPE_NIKON_DATA_SAMPLER,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonDataSamplerState),
    .instance_init = nikon_control_init,
    .class_init = nikon_control_class_init,
};

static void nikon_control_register_types(void)
{
    type_register_static(&nikon_control_type);
}
type_init(nikon_control_register_types)
