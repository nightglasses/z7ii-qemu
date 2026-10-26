/*
 * Original finite 2038/203c controls, configuration and raw event inputs.
 * Cold zero, retained readback, mask1 inhibition, W1C and rising fixture
 * inputs are virtual laws. Request admission does not produce work or events.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/nikon/devices.h"

#define BANK_SIZE 0x5e00
OBJECT_DECLARE_SIMPLE_TYPE(NikonPairedEventBanksState, NIKON_PAIRED_EVENT_BANKS)

typedef enum NikonPairedRequestState {
    PAIRED_QUIESCENT,
    PAIRED_ARMED,
    PAIRED_AWAIT_INPUT,
    PAIRED_DRAIN_PENDING,
} NikonPairedRequestState;

typedef struct NikonPairedEventBanksBank {
    NikonPairedEventBanksState *parent;
    MemoryRegion regs;
    qemu_irq irq;
    uint32_t config[BANK_SIZE / 4];
    uint32_t mask, pending, inputs;
    NikonPairedRequestState request;
    unsigned index;
} NikonPairedEventBanksBank;

struct NikonPairedEventBanksState {
    SysBusDevice parent_obj;
    NikonPairedEventBanksBank bank[2];
    bool reported_error;
};

static bool nikon_paired_event_banks_config(hwaddr offset, unsigned bank)
{
    /* SOURCE11 96cba..994c2 setters and 994e8 word-table publication.
     * These are retained configuration, not fabricated results. Gaps containing
     * 994e2 status consumers deliberately remain inaccessible.
     */
    static const struct {
        unsigned first, last;
    } ranges[] = {
        { 0x40, 0x50 }, { 0x94, 0xa0 }, { 0xb8, 0xcc },
        { 0xd4, 0xe4 }, { 0xf8, 0x110 }, { 0x11c, 0x144 },
        { 0x200, 0x210 }, { 0x218, 0x234 }, { 0x24c, 0x258 },
        { 0x300, 0x34c },
        { 0x364, 0x384 }, { 0x394, 0x3b4 }, { 0x3c4, 0x3c8 },
        { 0x3d8, 0x3e0 }, { 0x3e8, 0x3f0 },
        { 0x400, 0x414 }, { 0x420, 0x470 },
        { 0x488, 0x4c8 }, { 0x500, 0x678 }, { 0x700, 0x778 },
        { 0x900, 0x9b0 }, { 0x9b4, 0x9d8 },
        { 0xb00, 0xb60 }, { 0xb64, 0xbbc },
        { 0xd00, 0xe18 }, { 0xf00, 0xf2c },
        { 0x1100, 0x113c }, { 0x1200, 0x1244 },
        { 0x1300, 0x1320 }, { 0x1328, 0x1348 },
        { 0x1350, 0x1370 }, { 0x1378, 0x1398 },
        /* MAS1036bf88/bfae: 22*64B and14*68B CPU-published tables. */
        { 0x1400, 0x1d34 },
        { 0x1d38, 0x1e34 }, { 0x1f00, 0x1f40 }, { 0x1f48, 0x1f88 },
        { 0x1f90, 0x1fb0 }, { 0x1fb8, 0x1fd8 },
        { 0x1fe0, 0x2000 }, { 0x2008, 0x2028 }, { 0x2030, 0x2050 },
        { 0x2058, 0x2078 }, { 0x2080, 0x20a0 }, { 0x20a8, 0x20c8 },
        { 0x20d0, 0x20dc }, { 0x2100, 0x22fc },
        { 0x2400, 0x288c }, { 0x2890, 0x2c94 },
        { 0x3900, 0x3a98 }, { 0x3aa0, 0x3c38 },
        { 0x3c40, 0x3dd8 }, { 0x3de0, 0x3f78 },
        { 0x3f80, 0x4118 }, { 0x4120, 0x42b8 },
        { 0x42c0, 0x44c0 }, { 0x44c8, 0x45c8 },
        { 0x45d0, 0x46d0 }, { 0x46d8, 0x47d8 },
        { 0x47e0, 0x48e0 }, { 0x48e8, 0x49e8 },
        { 0x49f0, 0x4a70 }, { 0x4a74, 0x4af4 },
        { 0x4afc, 0x4b7c }, { 0x4b80, 0x4c00 },
        { 0x4c08, 0x4c88 }, { 0x4c8c, 0x4d0c },
        { 0x4d14, 0x4d94 }, { 0x4d98, 0x4e18 },
        { 0x4e1c, 0x4f1c }, { 0x4f20, 0x4fa0 },
        { 0x4fa4, 0x5024 }, { 0x502c, 0x512c },
        { 0x5134, 0x5334 }, { 0x533c, 0x553c },
        { 0x5544, 0x5744 }, { 0x574c, 0x594c },
        { 0x5954, 0x5a54 }, { 0x5a5c, 0x5adc },
        { 0x5ae4, 0x5b64 },
        { 0x5c00, 0x5c7c }, { 0x5c80, 0x5cfc },
        { 0x5d00, 0x5d7c }, { 0x5d80, 0x5dfc },
    };
    unsigned i;

    for (i = 0; i < ARRAY_SIZE(ranges); i++) {
        if (offset >= ranges[i].first && offset <= ranges[i].last) {
            return true;
        }
    }
    switch (offset) {
    case 0: case 4: case 0xc: case 0x20: case 0x84: case 0x88: case 0x8c:
    /*
     * MAS1036a2ac / SLA10096d20 write the full eight-field packet;
     * a8 retains byte2 without a modeled completion side effect.
     */
    case 0xa8: case 0xac: case 0xf0: case 0x67c:
        return true;
    case 0xd0: case 0xb0:
        return bank == 0;
    default:
        return false;
    }
}

/* Only opaque command shapes remain constrained. Setters retain full words;
 * source packing is a producer convention, not a proved hardware legal mask.
 */
static bool nikon_paired_event_banks_value(hwaddr offset, uint32_t value)
{
    switch (offset) {
    case 0: case 4:
        return value == 3;
    case 0xc:
        return value == 1;
    case 0xd0:
        return value <= 1;
    case 0xf0:
        return value == 0x8e || value == 0xfc400;
    default:
        return true;
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
    } else if (offset == 0x18) {
        /* The original consumer distinguishes only zero from nonzero. */
        *value = b->request == PAIRED_AWAIT_INPUT ||
                 b->request == PAIRED_DRAIN_PENDING;
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
    if (offset == 0x10 || offset == 8) {
        NikonPairedRequestState required, next;

        /*
         * MAS10369faa arms with 10=1 before 08=1. Admission returns to the
         * original software notification path; it is not a completion IRQ.
         */
        if (offset == 0x10) {
            required = PAIRED_QUIESCENT;
            next = PAIRED_ARMED;
        } else {
            required = PAIRED_ARMED;
            next = PAIRED_AWAIT_INPUT;
        }
        if (value != 1 || b->request != required) {
            return nikon_paired_event_banks_error(b, offset, size, true, value);
        }
        b->request = next;
        return MEMTX_OK;
    }
    if (offset == 0x24) {
        b->pending &= ~value;
    } else if (offset == 0xe8) {
        b->mask = value;
    } else if (nikon_paired_event_banks_config(offset, b->index) &&
               nikon_paired_event_banks_value(offset, value)) {
        if (offset == 0xc && b->request == PAIRED_AWAIT_INPUT) {
            /* 10369fc4: literal203800ec minus e0 is the finish request. */
            b->request = PAIRED_DRAIN_PENDING;
        }
        /* Opaque control inputs/configuration never clear pending or fields. */
        b->config[offset / 4] = value;
    } else {
        /* 54/60/6c/78 cancellation and result production remain unsupported. */
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
        s->bank[i].request = PAIRED_QUIESCENT;
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
