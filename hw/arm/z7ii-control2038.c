/*
 * Original finite 2038/203c controls, configuration and raw event inputs.
 * Cold zero, retained readback, mask1 inhibition, W1C and rising fixture
 * inputs are virtual laws. No frame source, reset effects or ready is inferred.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "z7ii.h"

#define CONTROL_BASE 0x20380000ULL
#define BANK_STRIDE 0x40000
#define BANK_SIZE 0x680
OBJECT_DECLARE_SIMPLE_TYPE(Z7IIControl2038State, Z7II_CONTROL2038)

typedef struct Z7IIControl2038Bank {
    Z7IIControl2038State *parent;
    MemoryRegion regs;
    qemu_irq irq;
    uint32_t config[BANK_SIZE / 4];
    uint32_t mask, pending, inputs;
    unsigned index;
} Z7IIControl2038Bank;

struct Z7IIControl2038State {
    SysBusDevice parent_obj;
    Z7IIControl2038Bank bank[2];
    bool reported_error;
};

static bool z7ii_2038_config(hwaddr offset, unsigned bank)
{
    switch (offset) {
    case 0: case 4: case 0xc: case 0x44: case 0x48: case 0x4c: case 0x50:
    case 0x84: case 0xbc: case 0xc0: case 0xc4: case 0xc8: case 0xcc:
    case 0x120: case 0x124: case 0x128: case 0x12c: case 0x130:
    case 0x300: case 0x30c: case 0x310:
    case 0x3e8: case 0x3ec: case 0x3f0: case 0x410: case 0x420: case 0x67c:
    case 0xf0:
        return true;
    case 0xd0:
        return bank == 0;
    default:
        return false;
    }
}

/* Exact recovered command/configuration shapes, not physical legal masks. */
static bool z7ii_2038_value(hwaddr offset, uint32_t value)
{
    switch (offset) {
    case 0: case 4:
        return value == 3;
    case 0xc:
        return value == 1;
    case 0x48:
        return value == 0x28000e || value == 0x14000e;
    case 0xbc:
        return value == 1 || value == 2;
    case 0xc4:
        return value == 0x14;
    case 0xcc:
        return value == 0 || value == 0x14;
    case 0xd0:
        return value <= 1;
    case 0xf0:
        return value == 0x8e || value == 0xfc400;
    case 0x300: case 0x3e8:
        return value == 2;
    case 0x30c: case 0x3f0:
        return value == 0x20 || value == 0x40;
    case 0x310: case 0x3ec:
        return true; /* Original entry parameter; no inferred DMA address. */
    case 0x67c:
        return value == 0x200;
    default:
        return value == 0;
    }
}

static void z7ii_2038_irq(Z7IIControl2038Bank *b)
{
    qemu_set_irq(b->irq, !!(b->pending & ~b->mask));
}

static void z7ii_2038_input(void *opaque, int index, int level)
{
    Z7IIControl2038State *s = opaque;
    Z7IIControl2038Bank *b = &s->bank[index / 32];
    uint32_t bit = 1U << (index % 32);
    bool rising = level && !(b->inputs & bit);

    b->inputs = (b->inputs & ~bit) | (level ? bit : 0);
    if (rising) {
        b->pending |= bit;
        z7ii_2038_irq(b);
    }
}

static MemTxResult z7ii_2038_error(Z7IIControl2038Bank *b, hwaddr offset,
                                 unsigned size, bool write, uint64_t value)
{
    z7ii_access_error(&b->parent->reported_error,
                      CONTROL_BASE + b->index * BANK_STRIDE + offset,
                      size, write, value,
                      "control2038-access-or-command-not-supported");
    return MEMTX_ERROR;
}

static MemTxResult z7ii_2038_read(void *opaque, hwaddr offset,
                                uint64_t *value, unsigned size,
                                MemTxAttrs attrs)
{
    Z7IIControl2038Bank *b = opaque;

    *value = 0;
    if (size != 4 || (offset & 3)) {
        return z7ii_2038_error(b, offset, size, false, 0);
    }
    if (offset == 0x24) {
        *value = b->pending;
    } else if (offset == 0xe8) {
        *value = b->mask;
    } else if (z7ii_2038_config(offset, b->index)) {
        *value = b->config[offset / 4];
    } else {
        return z7ii_2038_error(b, offset, size, false, 0);
    }
    return MEMTX_OK;
}

static MemTxResult z7ii_2038_write(void *opaque, hwaddr offset,
                                 uint64_t value, unsigned size,
                                 MemTxAttrs attrs)
{
    Z7IIControl2038Bank *b = opaque;

    if (size != 4 || (offset & 3)) {
        return z7ii_2038_error(b, offset, size, true, value);
    }
    if (offset == 0x24) {
        b->pending &= ~value;
    } else if (offset == 0xe8) {
        b->mask = value;
    } else if (z7ii_2038_config(offset, b->index) &&
               z7ii_2038_value(offset, value)) {
        /* Opaque control inputs/configuration never clear pending or fields. */
        b->config[offset / 4] = value;
    } else {
        /* 08/10/54/60/6c/78/580 requests have no recovered event source. */
        return z7ii_2038_error(b, offset, size, true, value);
    }
    z7ii_2038_irq(b);
    return MEMTX_OK;
}

static const MemoryRegionOps z7ii_2038_ops = {
    .read_with_attrs = z7ii_2038_read,
    .write_with_attrs = z7ii_2038_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
};

static void z7ii_2038_reset(DeviceState *dev)
{
    Z7IIControl2038State *s = Z7II_CONTROL2038(dev);
    unsigned i;

    for (i = 0; i < 2; i++) {
        memset(s->bank[i].config, 0, sizeof(s->bank[i].config));
        s->bank[i].mask = s->bank[i].pending = 0;
        /* Keep external levels; reset must not generate a new rising edge. */
        z7ii_2038_irq(&s->bank[i]);
    }
    s->reported_error = false;
}

static void z7ii_2038_init(Object *obj)
{
    Z7IIControl2038State *s = Z7II_CONTROL2038(obj);
    unsigned i;

    for (i = 0; i < 2; i++) {
        Z7IIControl2038Bank *b = &s->bank[i];

        b->parent = s;
        b->index = i;
        memory_region_init_io(&b->regs, obj, &z7ii_2038_ops, b,
                              i ? "z7ii.control203c" : "z7ii.control2038",
                              BANK_SIZE);
        sysbus_init_mmio(SYS_BUS_DEVICE(obj), &b->regs);
        sysbus_init_irq(SYS_BUS_DEVICE(obj), &b->irq);
    }
    qdev_init_gpio_in_named(DEVICE(obj), z7ii_2038_input, "pending-bit", 64);
}

static const VMStateDescription z7ii_2038_vmstate = {
    .name = "z7ii-control2038",
    .unmigratable = true,
};

static void z7ii_2038_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &z7ii_2038_vmstate;
    dc->user_creatable = false;
    device_class_set_legacy_reset(dc, z7ii_2038_reset);
}

static const TypeInfo z7ii_2038_type = {
    .name = TYPE_Z7II_CONTROL2038,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Z7IIControl2038State),
    .instance_init = z7ii_2038_init,
    .class_init = z7ii_2038_class_init,
};

static void z7ii_2038_register_types(void)
{
    type_register_static(&z7ii_2038_type);
}
type_init(z7ii_2038_register_types)
