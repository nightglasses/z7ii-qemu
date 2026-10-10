/* SLA logical10F: configuration, paired requests and two result windows.
 * No physical IP identity or measurement algorithm is inferred.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "hw/nikon/devices.h"
#include "hw/nikon/readout.h"

OBJECT_DECLARE_SIMPLE_TYPE(NikonReadoutState, NIKON_READOUT)

struct NikonReadoutState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    qemu_irq irq, request;
    uint32_t config[0x310 / 4];
    uint32_t first[NIKON_READOUT_WORDS];
    uint32_t second[NIKON_READOUT_WORDS];
    uint32_t pending;
    uint64_t generation;
    bool active, results_valid, status_valid, state_valid, state;
    bool reported_error;
};

static void readout_irq(NikonReadoutState *s)
{
    qemu_set_irq(s->irq, s->pending != 0);
}

static void readout_cancel(NikonReadoutState *s)
{
    s->generation++;
    if (!s->generation) {
        s->generation++;
    }
    s->active = false;
    qemu_set_irq(s->request, 0);
}

uint64_t nikon_results_generation(DeviceState *dev)
{
    NikonReadoutState *s = NIKON_READOUT(dev);

    return s->active ? s->generation : 0;
}

bool nikon_results_publish(DeviceState *dev, uint64_t generation,
                          uint32_t events,
                          const uint32_t first[NIKON_READOUT_WORDS],
                          const uint32_t second[NIKON_READOUT_WORDS])
{
    NikonReadoutState *s = NIKON_READOUT(dev);

    if (!s->active || !generation || generation != s->generation ||
        (events & ~0x15U) || !first || !second) {
        return false;
    }
    memcpy(s->first, first, sizeof(s->first));
    memcpy(s->second, second, sizeof(s->second));
    s->results_valid = s->status_valid = true;
    s->pending |= events;
    s->active = false;
    qemu_set_irq(s->request, 0);
    readout_irq(s);
    return true;
}

void nikon_results_state(DeviceState *dev, bool state)
{
    NikonReadoutState *s = NIKON_READOUT(dev);

    s->state = state;
    s->state_valid = true;
}

/* These are field widths written by the complete source consumer, not a
 * speculative register-file aperture. Fullword values remain fullword where
 * the consumer supplies a uint32_t without further qualification.
 */
static bool readout_mask(hwaddr offset, uint32_t *mask)
{
    switch (offset) {
    case 0x20: case 0x24: case 0x28:
    case 0x80: case 0x84: case 0x88:
    case 0x94: case 0x98: case 0x9c: case 0xa0:
    case 0xa4: case 0xa8: case 0xac:
    case 0xb0: case 0xb4: case 0xb8:
        *mask = UINT32_MAX;
        return true;
    case 0xbc:
        *mask = 0xff;
        return true;
    case 0x40:
        *mask = 0x7fff;
        return true;
    case 0x44:
        *mask = 0x1773;
        return true;
    case 0x48: case 0x4c: case 0x50:
    case 0x58: case 0x5c: case 0x60:
    case 0x64: case 0x68: case 0x6c:
    case 0x74: case 0x78: case 0x7c:
        *mask = 0xffff;
        return true;
    case 0x54:
        *mask = 0x773;
        return true;
    case 0x70:
        *mask = 0x3fffbfd;
        return true;
    case 0x8c: case 0x90:
        *mask = 0x3fff;
        return true;
    case 0x304:
        *mask = 1;
        return true;
    case 0x308: case 0x30c:
        *mask = 3;
        return true;
    default:
        return false;
    }
}

static MemTxResult readout_error(NikonReadoutState *s, hwaddr offset,
                                unsigned size, bool write, uint64_t value)
{
    nikon_access_error(&s->reported_error, s->regs.addr + offset, size,
                      write, value, "readout-access-not-supported");
    return MEMTX_ERROR;
}

static MemTxResult readout_read(void *opaque, hwaddr offset, uint64_t *value,
                              unsigned size, MemTxAttrs attrs)
{
    NikonReadoutState *s = opaque;
    uint32_t mask;

    *value = 0;
    if (size != 4 || (offset & 3)) {
        return readout_error(s, offset, size, false, 0);
    }
    if (offset == 4 && s->state_valid) {
        *value = s->state;
    } else if (offset == 0x30 && s->status_valid) {
        *value = s->pending;
    } else if (offset >= 0x100 && offset < 0x1a0 && s->results_valid) {
        *value = s->first[(offset - 0x100) / 4];
    } else if (offset >= 0x200 && offset < 0x2a0 && s->results_valid) {
        *value = s->second[(offset - 0x200) / 4];
    } else if (readout_mask(offset, &mask)) {
        *value = s->config[offset / 4];
    } else {
        return readout_error(s, offset, size, false, 0);
    }
    return MEMTX_OK;
}

static MemTxResult readout_write(void *opaque, hwaddr offset, uint64_t value,
                               unsigned size, MemTxAttrs attrs)
{
    NikonReadoutState *s = opaque;
    uint32_t mask;

    if (size != 4 || (offset & 3)) {
        return readout_error(s, offset, size, true, value);
    }
    if (offset == 0 || offset == 0x300) {
        if (value > 1) {
            return readout_error(s, offset, size, true, value);
        }
        s->config[offset / 4] = value;
        if (!offset || !value) {
            readout_cancel(s);
        } else if (s->config[0]) {
            /* Every paired submission gets a distinct producer token. */
            readout_cancel(s);
            s->active = true;
            qemu_set_irq(s->request, 1);
        }
    } else if (offset == 4) {
        if (value != 1) {
            return readout_error(s, offset, size, true, value);
        }
        readout_cancel(s);
        s->config[0] = s->config[0x300 / 4] = 0;
        s->pending = 0;
        s->results_valid = s->status_valid = s->state_valid = false;
        readout_irq(s);
    } else if (offset == 0x30) {
        if (!s->status_valid || (value & ~0x15U)) {
            return readout_error(s, offset, size, true, value);
        }
        s->pending &= ~value;
        readout_irq(s);
    } else if (readout_mask(offset, &mask) && !(value & ~mask)) {
        s->config[offset / 4] = value;
    } else {
        return readout_error(s, offset, size, true, value);
    }
    return MEMTX_OK;
}

static const MemoryRegionOps readout_ops = {
    .read_with_attrs = readout_read,
    .write_with_attrs = readout_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
};

static void readout_reset(DeviceState *dev)
{
    NikonReadoutState *s = NIKON_READOUT(dev);

    readout_cancel(s);
    memset(s->config, 0, sizeof(s->config));
    memset(s->first, 0, sizeof(s->first));
    memset(s->second, 0, sizeof(s->second));
    s->pending = 0;
    s->results_valid = s->status_valid = s->state_valid = false;
    s->reported_error = false;
    readout_irq(s);
}

static void readout_unrealize(DeviceState *dev)
{
    readout_reset(dev);
}

static void readout_init(Object *obj)
{
    NikonReadoutState *s = NIKON_READOUT(obj);

    memory_region_init_io(&s->regs, obj, &readout_ops, s, "nikon.readout", 0x310);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
    qdev_init_gpio_out_named(DEVICE(obj), &s->request, "request", 1);
}

static const VMStateDescription readout_vmstate = {
    .name = "nikon-readout", .unmigratable = true,
};

static void readout_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &readout_vmstate;
    dc->user_creatable = false;
    dc->unrealize = readout_unrealize;
    device_class_set_legacy_reset(dc, readout_reset);
}

static const TypeInfo readout_type = {
    .name = TYPE_NIKON_READOUT,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonReadoutState),
    .instance_init = readout_init,
    .class_init = readout_class_init,
};

static void readout_register_types(void)
{
    type_register_static(&readout_type);
}
type_init(readout_register_types)
