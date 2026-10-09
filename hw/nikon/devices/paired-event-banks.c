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
#include "nikon.h"

#define BANK_SIZE 0x680
OBJECT_DECLARE_SIMPLE_TYPE(NikonPairedEventBanksState, NIKON_PAIRED_EVENT_BANKS)

typedef struct NikonPairedEventBanksBank {
    NikonPairedEventBanksState *parent;
    MemoryRegion regs;
    qemu_irq irq;
    uint32_t config[BANK_SIZE / 4];
    uint32_t mask, pending, inputs;
    unsigned index;
} NikonPairedEventBanksBank;

struct NikonPairedEventBanksState {
    SysBusDevice parent_obj;
    NikonPairedEventBanksBank bank[2];
    bool reported_error;
};

static bool nikon_paired_event_banks_config(hwaddr offset, unsigned bank)
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
static bool nikon_paired_event_banks_value(hwaddr offset, uint32_t value)
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

static void nikon_paired_event_banks_irq(NikonPairedEventBanksBank *b)
{
    qemu_set_irq(b->irq, !!(b->pending & ~b->mask));
}

static void nikon_paired_event_banks_input(void *opaque, int index, int level)
{
    NikonPairedEventBanksState *s = opaque;
    NikonPairedEventBanksBank *b = &s->bank[index / 32];
    uint32_t bit = 1U << (index % 32);
    bool rising = level && !(b->inputs & bit);

    b->inputs = (b->inputs & ~bit) | (level ? bit : 0);
    if (rising) {
        b->pending |= bit;
        nikon_paired_event_banks_irq(b);
    }
}

static MemTxResult nikon_paired_event_banks_error(NikonPairedEventBanksBank *b, hwaddr offset,
                                 unsigned size, bool write, uint64_t value)
{
    nikon_access_error(&b->parent->reported_error,
                      b->regs.addr + offset,
                      size, write, value,
                      "paired-event-banks-access-or-command-not-supported");
    return MEMTX_ERROR;
}

static MemTxResult nikon_paired_event_banks_read(void *opaque, hwaddr offset,
                                uint64_t *value, unsigned size,
                                MemTxAttrs attrs)
{
    NikonPairedEventBanksBank *b = opaque;

    *value = 0;
    if (size != 4 || (offset & 3)) {
        return nikon_paired_event_banks_error(b, offset, size, false, 0);
    }
    if (offset == 0x24) {
        *value = b->pending;
    } else if (offset == 0xe8) {
        *value = b->mask;
    } else if (nikon_paired_event_banks_config(offset, b->index)) {
        *value = b->config[offset / 4];
    } else {
        return nikon_paired_event_banks_error(b, offset, size, false, 0);
    }
    return MEMTX_OK;
}

static MemTxResult nikon_paired_event_banks_write(void *opaque, hwaddr offset,
                                 uint64_t value, unsigned size,
                                 MemTxAttrs attrs)
{
    NikonPairedEventBanksBank *b = opaque;

    if (size != 4 || (offset & 3)) {
        return nikon_paired_event_banks_error(b, offset, size, true, value);
    }
    if (offset == 0x24) {
        b->pending &= ~value;
    } else if (offset == 0xe8) {
        b->mask = value;
    } else if (nikon_paired_event_banks_config(offset, b->index) &&
               nikon_paired_event_banks_value(offset, value)) {
        /* Opaque control inputs/configuration never clear pending or fields. */
        b->config[offset / 4] = value;
    } else {
        /* 08/10/54/60/6c/78/580 requests have no recovered event source. */
        return nikon_paired_event_banks_error(b, offset, size, true, value);
    }
    nikon_paired_event_banks_irq(b);
    return MEMTX_OK;
}

static const MemoryRegionOps nikon_paired_event_banks_ops = {
    .read_with_attrs = nikon_paired_event_banks_read,
    .write_with_attrs = nikon_paired_event_banks_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
};

static void nikon_paired_event_banks_reset(DeviceState *dev)
{
    NikonPairedEventBanksState *s = NIKON_PAIRED_EVENT_BANKS(dev);
    unsigned i;

    for (i = 0; i < 2; i++) {
        memset(s->bank[i].config, 0, sizeof(s->bank[i].config));
        s->bank[i].mask = s->bank[i].pending = 0;
        /* Keep external levels; reset must not generate a new rising edge. */
        nikon_paired_event_banks_irq(&s->bank[i]);
    }
    s->reported_error = false;
}

static void nikon_paired_event_banks_init(Object *obj)
{
    NikonPairedEventBanksState *s = NIKON_PAIRED_EVENT_BANKS(obj);
    unsigned i;

    for (i = 0; i < 2; i++) {
        NikonPairedEventBanksBank *b = &s->bank[i];

        b->parent = s;
        b->index = i;
        memory_region_init_io(&b->regs, obj, &nikon_paired_event_banks_ops, b,
                              i ? "nikon.control203c" : "nikon.paired-event-banks",
                              BANK_SIZE);
        sysbus_init_mmio(SYS_BUS_DEVICE(obj), &b->regs);
        sysbus_init_irq(SYS_BUS_DEVICE(obj), &b->irq);
    }
    qdev_init_gpio_in_named(DEVICE(obj), nikon_paired_event_banks_input, "pending-bit", 64);
}

static const VMStateDescription nikon_paired_event_banks_vmstate = {
    .name = "nikon-paired-event-banks",
    .unmigratable = true,
};

static void nikon_paired_event_banks_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &nikon_paired_event_banks_vmstate;
    dc->user_creatable = false;
    device_class_set_legacy_reset(dc, nikon_paired_event_banks_reset);
}

static const TypeInfo nikon_paired_event_banks_type = {
    .name = TYPE_NIKON_PAIRED_EVENT_BANKS,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonPairedEventBanksState),
    .instance_init = nikon_paired_event_banks_init,
    .class_init = nikon_paired_event_banks_class_init,
};

static void nikon_paired_event_banks_register_types(void)
{
    type_register_static(&nikon_paired_event_banks_type);
}
type_init(nikon_paired_event_banks_register_types)
