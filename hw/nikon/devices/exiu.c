/*
 * Sixteen-input EXIU-compatible external interrupt functional interface.
 * Original MAS object table supplies pad/GIC routes. The disconnected,
 * rising-edge, masked reset profile is virtual, not measured silicon reset.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "nikon.h"

#define EXIU_MASK 0xffff
OBJECT_DECLARE_SIMPLE_TYPE(NikonEXIUState, NIKON_EXIU)

struct NikonEXIUState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    qemu_irq irq[16];
    uint16_t inputs;
    uint16_t mask;
    uint16_t polarity;
    uint16_t edge;
    uint16_t pending;
    bool reported_error;
};

static void nikon_exiu_update(NikonEXIUState *s)
{
    uint16_t active = ~(s->inputs ^ s->polarity);
    unsigned i;

    /* Level requests latch too; W1C cannot clear an asserted level. */
    s->pending |= active & ~s->edge;
    for (i = 0; i < 16; i++) {
        qemu_set_irq(s->irq[i], !!(s->pending & ~s->mask & (1U << i)));
    }
}

static void nikon_exiu_input(void *opaque, int n, int level)
{
    NikonEXIUState *s = opaque;
    uint16_t bit = 1U << n;
    bool old = !!(s->inputs & bit);

    s->inputs = (s->inputs & ~bit) | (level ? bit : 0);
    if (old != !!level && !!level == !!(s->polarity & bit)) {
        s->pending |= bit & s->edge;
    }
    nikon_exiu_update(s);
}

static MemTxResult nikon_exiu_error(NikonEXIUState *s, hwaddr offset,
                                  unsigned size, bool write, uint64_t value)
{
    nikon_access_error(&s->reported_error, s->regs.addr + offset, size,
                      write, value, "exiu-access-not-supported");
    return MEMTX_ERROR;
}

static MemTxResult nikon_exiu_read(void *opaque, hwaddr offset, uint64_t *value,
                                 unsigned size, MemTxAttrs attrs)
{
    NikonEXIUState *s = opaque;

    *value = 0;
    if (size != 4 || (offset & 3)) {
        return nikon_exiu_error(s, offset, size, false, 0);
    }
    switch (offset) {
    case 0:
        *value = s->mask;
        break;
    case 4:
        /* Only the original external-source profile is supported. */
        break;
    case 8:
        *value = s->pending & ~s->mask;
        break;
    case 0xc:
        *value = s->pending;
        break;
    case 0x10:
        /*
         * Virtual self-clearing command port: no retained clear strobes.
         * Original guest and vendor driver both use read/OR/write here.
         * Physical command readback is not established.
         */
        break;
    case 0x14:
        *value = s->polarity;
        break;
    case 0x18:
        *value = s->edge;
        break;
    default:
        return nikon_exiu_error(s, offset, size, false, 0);
    }
    return MEMTX_OK;
}

static MemTxResult nikon_exiu_write(void *opaque, hwaddr offset, uint64_t value,
                                  unsigned size, MemTxAttrs attrs)
{
    NikonEXIUState *s = opaque;

    if (size != 4 || (offset & 3) || value > EXIU_MASK) {
        return nikon_exiu_error(s, offset, size, true, value);
    }
    switch (offset) {
    case 0:
        s->mask = value;
        break;
    case 4:
        if (value) {
            return nikon_exiu_error(s, offset, size, true, value);
        }
        break;
    case 0x10:
        s->pending &= ~value;
        break;
    case 0x14:
        s->polarity = value;
        break;
    case 0x18:
        s->edge = value;
        break;
    default:
        return nikon_exiu_error(s, offset, size, true, value);
    }
    /* Configuration changes produce level state, never synthetic edges. */
    nikon_exiu_update(s);
    return MEMTX_OK;
}

static const MemoryRegionOps nikon_exiu_ops = {
    .read_with_attrs = nikon_exiu_read,
    .write_with_attrs = nikon_exiu_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
};

static void nikon_exiu_reset(DeviceState *dev)
{
    NikonEXIUState *s = NIKON_EXIU(dev);

    /* Do not replace external pad levels on controller reset. */
    s->mask = s->polarity = s->edge = EXIU_MASK;
    s->pending = 0;
    s->reported_error = false;
    nikon_exiu_update(s);
}

static void nikon_exiu_init(Object *obj)
{
    NikonEXIUState *s = NIKON_EXIU(obj);
    unsigned i;

    memory_region_init_io(&s->regs, obj, &nikon_exiu_ops, s, "nikon.exiu", 0x20);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
    qdev_init_gpio_in(DEVICE(obj), nikon_exiu_input, 16);
    for (i = 0; i < 16; i++) {
        sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq[i]);
    }
}

static const VMStateDescription nikon_exiu_vmstate = {
    .name = "nikon-exiu",
    .unmigratable = true,
};

static void nikon_exiu_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &nikon_exiu_vmstate;
    dc->user_creatable = false;
    device_class_set_legacy_reset(dc, nikon_exiu_reset);
}

static const TypeInfo nikon_exiu_type = {
    .name = TYPE_NIKON_EXIU,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonEXIUState),
    .instance_init = nikon_exiu_init,
    .class_init = nikon_exiu_class_init,
};

static void nikon_exiu_register_types(void)
{
    type_register_static(&nikon_exiu_type);
}
type_init(nikon_exiu_register_types)
