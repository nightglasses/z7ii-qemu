/*
 * Six finite status banks consumed and acknowledged by the SLA interrupt path.
 * W1C, edge latching and reset are explicit functional conventions; the
 * unknown external producer is disconnected unless a board supplies inputs.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/nikon/devices.h"
#include "hw/nikon/flags.h"

#define FLAGS_BANKS 6
#define FLAGS_BITS 32

OBJECT_DECLARE_SIMPLE_TYPE(NikonFlagsState, NIKON_FLAGS)

struct NikonFlagsState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    qemu_irq irq;
    uint32_t pending[FLAGS_BANKS];
    uint32_t levels[FLAGS_BANKS];
    bool reported_error;
};

static int nikon_flags_bank(hwaddr offset)
{
    switch (offset) {
    case 0: return 0;
    case 4: return 1;
    case 8: return 2;
    case 0xc: return 3;
    case 0x10: return 4;
    case 0x1c: return 5;
    default: return -1;
    }
}

static void nikon_flags_irq(NikonFlagsState *s)
{
    uint32_t pending = 0;
    unsigned bank;

    for (bank = 0; bank < FLAGS_BANKS; bank++) {
        pending |= s->pending[bank];
    }
    qemu_set_irq(s->irq, !!pending);
}

static void nikon_flags_event(void *opaque, int index, int level)
{
    NikonFlagsState *s = opaque;
    unsigned bank = index / FLAGS_BITS;
    uint32_t bit = 1U << (index % FLAGS_BITS);
    bool rising = level && !(s->levels[bank] & bit);

    s->levels[bank] = (s->levels[bank] & ~bit) | (level ? bit : 0);
    if (rising) {
        s->pending[bank] |= bit;
        nikon_flags_irq(s);
    }
}

static MemTxResult nikon_flags_error(NikonFlagsState *s, hwaddr offset,
                                    unsigned size, bool write, uint64_t value)
{
    nikon_access_error(&s->reported_error, s->regs.addr + offset,
                      size, write, value, "flags-access-not-supported");
    return MEMTX_ERROR;
}

static MemTxResult nikon_flags_read(void *opaque, hwaddr offset,
                                  uint64_t *value, unsigned size,
                                  MemTxAttrs attrs)
{
    NikonFlagsState *s = opaque;
    int bank = nikon_flags_bank(offset);

    *value = 0;
    if (size != 4 || bank < 0) {
        return nikon_flags_error(s, offset, size, false, 0);
    }
    *value = s->pending[bank];
    return MEMTX_OK;
}

static MemTxResult nikon_flags_write(void *opaque, hwaddr offset,
                                   uint64_t value, unsigned size,
                                   MemTxAttrs attrs)
{
    NikonFlagsState *s = opaque;
    int bank = nikon_flags_bank(offset);

    if (size != 4 || bank < 0) {
        return nikon_flags_error(s, offset, size, true, value);
    }
    s->pending[bank] &= ~(uint32_t)value;
    nikon_flags_irq(s);
    return MEMTX_OK;
}

static const MemoryRegionOps nikon_flags_ops = {
    .read_with_attrs = nikon_flags_read,
    .write_with_attrs = nikon_flags_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
};

static void nikon_flags_reset(DeviceState *dev)
{
    NikonFlagsState *s = NIKON_FLAGS(dev);

    memset(s->pending, 0, sizeof(s->pending));
    s->reported_error = false;
    /* Retain real provider levels: reset cannot turn a held input into an edge. */
    nikon_flags_irq(s);
}

static void nikon_flags_init(Object *obj)
{
    NikonFlagsState *s = NIKON_FLAGS(obj);

    memory_region_init_io(&s->regs, obj, &nikon_flags_ops, s,
                          "nikon.flags", 0x20);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
    qdev_init_gpio_in_named(DEVICE(obj), nikon_flags_event,
                           "event", FLAGS_BANKS * FLAGS_BITS);
}

static const VMStateDescription nikon_flags_vmstate = {
    .name = TYPE_NIKON_FLAGS,
    .unmigratable = true,
};

static void nikon_flags_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &nikon_flags_vmstate;
    dc->user_creatable = false;
    device_class_set_legacy_reset(dc, nikon_flags_reset);
}

static const TypeInfo nikon_flags_type = {
    .name = TYPE_NIKON_FLAGS,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonFlagsState),
    .instance_init = nikon_flags_init,
    .class_init = nikon_flags_class_init,
};

static void nikon_flags_register_types(void)
{
    type_register_static(&nikon_flags_type);
}
type_init(nikon_flags_register_types)
