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
#include "z7ii.h"

#define EXIU_BASE 0x0426f000ULL
#define EXIU_MASK 0xffff
OBJECT_DECLARE_SIMPLE_TYPE(Z7IIEXIUState, Z7II_EXIU)

struct Z7IIEXIUState {
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

static void z7ii_exiu_update(Z7IIEXIUState *s)
{
    uint16_t active = ~(s->inputs ^ s->polarity);
    unsigned i;

    /* Level requests latch too; W1C cannot clear an asserted level. */
    s->pending |= active & ~s->edge;
    for (i = 0; i < 16; i++) {
        qemu_set_irq(s->irq[i], !!(s->pending & ~s->mask & (1U << i)));
    }
}

static void z7ii_exiu_input(void *opaque, int n, int level)
{
    Z7IIEXIUState *s = opaque;
    uint16_t bit = 1U << n;
    bool old = !!(s->inputs & bit);

    s->inputs = (s->inputs & ~bit) | (level ? bit : 0);
    if (old != !!level && !!level == !!(s->polarity & bit)) {
        s->pending |= bit & s->edge;
    }
    z7ii_exiu_update(s);
}

static MemTxResult z7ii_exiu_error(Z7IIEXIUState *s, hwaddr offset,
                                  unsigned size, bool write, uint64_t value)
{
    z7ii_access_error(&s->reported_error, EXIU_BASE + offset, size,
                      write, value, "exiu-access-not-supported");
    return MEMTX_ERROR;
}

static MemTxResult z7ii_exiu_read(void *opaque, hwaddr offset, uint64_t *value,
                                 unsigned size, MemTxAttrs attrs)
{
    Z7IIEXIUState *s = opaque;

    *value = 0;
    if (size != 4 || (offset & 3)) {
        return z7ii_exiu_error(s, offset, size, false, 0);
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
        /* Virtual self-clearing command port: no retained clear strobes.
         * Original guest and vendor driver both use read/OR/write here.
         * Physical command readback is not established. */
        break;
    case 0x14:
        *value = s->polarity;
        break;
    case 0x18:
        *value = s->edge;
        break;
    default:
        return z7ii_exiu_error(s, offset, size, false, 0);
    }
    return MEMTX_OK;
}

static MemTxResult z7ii_exiu_write(void *opaque, hwaddr offset, uint64_t value,
                                  unsigned size, MemTxAttrs attrs)
{
    Z7IIEXIUState *s = opaque;

    if (size != 4 || (offset & 3) || value > EXIU_MASK) {
        return z7ii_exiu_error(s, offset, size, true, value);
    }
    switch (offset) {
    case 0:
        s->mask = value;
        break;
    case 4:
        if (value) {
            return z7ii_exiu_error(s, offset, size, true, value);
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
        return z7ii_exiu_error(s, offset, size, true, value);
    }
    /* Configuration changes produce level state, never synthetic edges. */
    z7ii_exiu_update(s);
    return MEMTX_OK;
}

static const MemoryRegionOps z7ii_exiu_ops = {
    .read_with_attrs = z7ii_exiu_read,
    .write_with_attrs = z7ii_exiu_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
};

static void z7ii_exiu_reset(DeviceState *dev)
{
    Z7IIEXIUState *s = Z7II_EXIU(dev);

    /* Do not replace external pad levels on controller reset. */
    s->mask = s->polarity = s->edge = EXIU_MASK;
    s->pending = 0;
    s->reported_error = false;
    z7ii_exiu_update(s);
}

static void z7ii_exiu_init(Object *obj)
{
    Z7IIEXIUState *s = Z7II_EXIU(obj);
    unsigned i;

    memory_region_init_io(&s->regs, obj, &z7ii_exiu_ops, s, "z7ii.exiu", 0x20);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
    qdev_init_gpio_in(DEVICE(obj), z7ii_exiu_input, 16);
    for (i = 0; i < 16; i++) {
        sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq[i]);
    }
}

static const VMStateDescription z7ii_exiu_vmstate = {
    .name = "z7ii-exiu",
    .unmigratable = true,
};

static void z7ii_exiu_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &z7ii_exiu_vmstate;
    dc->user_creatable = false;
    device_class_set_legacy_reset(dc, z7ii_exiu_reset);
}

static const TypeInfo z7ii_exiu_type = {
    .name = TYPE_Z7II_EXIU,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Z7IIEXIUState),
    .instance_init = z7ii_exiu_init,
    .class_init = z7ii_exiu_class_init,
};

static void z7ii_exiu_register_types(void)
{
    type_register_static(&z7ii_exiu_type);
}
type_init(z7ii_exiu_register_types)
