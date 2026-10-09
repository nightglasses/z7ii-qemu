/*
 * Finite 2014 dual-object configuration and disconnected event profile.
 * Cold readback, opaque controls, mask1 inhibition, W1C and IRQ routing
 * are virtual conventions. No reset effects, status3 or frames are inferred.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "nikon.h"

#define BANK_SIZE 0x4000
OBJECT_DECLARE_SIMPLE_TYPE(NikonDualEventBanksState, NIKON_DUAL_EVENT_BANKS)

typedef struct NikonDualEventBanksRegion {
    NikonDualEventBanksState *parent;
    MemoryRegion regs;
    unsigned index;
} NikonDualEventBanksRegion;

struct NikonDualEventBanksState {
    SysBusDevice parent_obj;
    NikonDualEventBanksRegion region[5];
    qemu_irq irq[3];
    uint32_t config[2][BANK_SIZE / 4];
    uint32_t mask[2], pending[2], inputs[2];
    uint32_t global[4];
    bool reported_error;
};

/* Only settled generated configuration words, not physical legal masks. */
static bool nikon_dual_event_banks_config(hwaddr offset, uint32_t *mask)
{
    switch (offset) {
    case 0: case 0x1000: case 0x2000: case 0x3000: case 0x1224:
        *mask = 1;
        break;
    case 0x100: case 0x2100: case 0x3100:
        /* Stop only; start/pause and status producers are unbound. */
        *mask = 0;
        break;
    case 0x1100: case 0x1400: case 0x140c:
        *mask = 0xff;
        break;
    case 0x1200:
        *mask = 0x37f0311f;
        break;
    case 0x1204: case 0x1240: case 0x1250: case 0x1424:
        *mask = 0xffff;
        break;
    case 0x1600:
        *mask = 0x3fff;
        break;
    case 0x1604:
        *mask = 0x101;
        break;
    case 0x1410:
        *mask = 0x301;
        break;
    case 0x1414:
        *mask = 0x00ff01ff;
        break;
    case 0x1418:
        *mask = 0x00ff00ff;
        break;
    case 0x1404:
        *mask = 0x1f001f1f;
        break;
    case 0x1408:
        *mask = 0x1f1f;
        break;
    case 0x500: case 0x2880: case 0x3880: case 0x120c:
    case 0x141c: case 0x1420:
        *mask = UINT32_MAX;
        break;
    default:
        return false;
    }
    return true;
}

static void nikon_dual_event_banks_irq(NikonDualEventBanksState *s)
{
    unsigned route, bank;

    for (route = 0; route < 3; route++) {
        bool asserted = false;

        for (bank = 0; bank < 2; bank++) {
            asserted |= s->config[bank][0x1100 / 4] == route + 1 &&
                        !!(s->pending[bank] & ~s->mask[bank]);
        }
        qemu_set_irq(s->irq[route], asserted);
    }
}

static void nikon_dual_event_banks_input(void *opaque, int index, int level)
{
    NikonDualEventBanksState *s = opaque;
    unsigned bank = index / 32;
    uint32_t bit = 1U << (index % 32);
    bool rising = level && !(s->inputs[bank] & bit);

    s->inputs[bank] = (s->inputs[bank] & ~bit) | (level ? bit : 0);
    if (rising) {
        s->pending[bank] |= bit;
        nikon_dual_event_banks_irq(s);
    }
}

static MemTxResult nikon_dual_event_banks_error(NikonDualEventBanksRegion *r, hwaddr offset,
                                 unsigned size, bool write, uint64_t value)
{
    nikon_access_error(&r->parent->reported_error, r->regs.addr + offset,
                      size, write, value,
                      "dual-event-banks-access-or-activation-not-supported");
    return MEMTX_ERROR;
}

static bool nikon_dual_event_banks_global(unsigned index, hwaddr offset, uint32_t *mask,
                              unsigned *slot)
{
    if (offset == 0) {
        *slot = index == 4 ? 3 : index - 2;
        *mask = index == 2 ? 0x111 : index == 3 ? 0x11 : 1;
        return true;
    }
    if (index == 3 && offset == 0x100) {
        *slot = 2; /* D100 needs independent storage from E000. */
        *mask = 1;
        return true;
    }
    return false;
}

static MemTxResult nikon_dual_event_banks_read(void *opaque, hwaddr offset,
                                uint64_t *value, unsigned size,
                                MemTxAttrs attrs)
{
    NikonDualEventBanksRegion *r = opaque;
    NikonDualEventBanksState *s = r->parent;
    uint32_t mask;
    unsigned slot;

    *value = 0;
    if (size != 4 || (offset & 3)) {
        return nikon_dual_event_banks_error(r, offset, size, false, 0);
    }
    if (r->index < 2) {
        if (offset == 0x1214) {
            *value = s->pending[r->index];
        } else if (offset == 0x1210) {
            *value = s->mask[r->index];
        } else if (nikon_dual_event_banks_config(offset, &mask)) {
            *value = s->config[r->index][offset / 4];
        } else {
            return nikon_dual_event_banks_error(r, offset, size, false, 0);
        }
    } else if (nikon_dual_event_banks_global(r->index, offset, &mask, &slot)) {
        *value = s->global[slot];
    } else {
        return nikon_dual_event_banks_error(r, offset, size, false, 0);
    }
    return MEMTX_OK;
}

static MemTxResult nikon_dual_event_banks_write(void *opaque, hwaddr offset,
                                 uint64_t value, unsigned size,
                                 MemTxAttrs attrs)
{
    NikonDualEventBanksRegion *r = opaque;
    NikonDualEventBanksState *s = r->parent;
    uint32_t mask;
    unsigned slot;

    if (size != 4 || (offset & 3)) {
        return nikon_dual_event_banks_error(r, offset, size, true, value);
    }
    if (r->index < 2) {
        if (offset == 0x1214) {
            s->pending[r->index] &= ~value;
        } else if (offset == 0x1210) {
            s->mask[r->index] = value;
        } else if (nikon_dual_event_banks_config(offset, &mask) && !(value & ~mask) &&
                   (offset != 0x1200 || !(value & 3))) {
            s->config[r->index][offset / 4] = value;
        } else {
            return nikon_dual_event_banks_error(r, offset, size, true, value);
        }
    } else if (nikon_dual_event_banks_global(r->index, offset, &mask, &slot) &&
               !(value & ~mask)) {
        s->global[slot] = value;
    } else {
        return nikon_dual_event_banks_error(r, offset, size, true, value);
    }
    nikon_dual_event_banks_irq(s);
    return MEMTX_OK;
}

static const MemoryRegionOps nikon_dual_event_banks_ops = {
    .read_with_attrs = nikon_dual_event_banks_read,
    .write_with_attrs = nikon_dual_event_banks_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
};

static void nikon_dual_event_banks_reset(DeviceState *dev)
{
    NikonDualEventBanksState *s = NIKON_DUAL_EVENT_BANKS(dev);

    memset(s->config, 0, sizeof(s->config));
    memset(s->mask, 0, sizeof(s->mask));
    memset(s->pending, 0, sizeof(s->pending));
    memset(s->global, 0, sizeof(s->global));
    s->reported_error = false;
    /* Actual provider levels survive reset; no edge is manufactured. */
    nikon_dual_event_banks_irq(s);
}

static void nikon_dual_event_banks_init(Object *obj)
{
    NikonDualEventBanksState *s = NIKON_DUAL_EVENT_BANKS(obj);
    unsigned i;

    for (i = 0; i < 5; i++) {
        NikonDualEventBanksRegion *r = &s->region[i];

        r->parent = s;
        r->index = i;
        memory_region_init_io(&r->regs, obj, &nikon_dual_event_banks_ops, r,
                              "nikon.dual-event-banks", i < 2 ? BANK_SIZE :
                              i == 3 ? 0x104 : 4);
        sysbus_init_mmio(SYS_BUS_DEVICE(obj), &r->regs);
    }
    for (i = 0; i < 3; i++) {
        sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq[i]);
    }
    qdev_init_gpio_in_named(DEVICE(obj), nikon_dual_event_banks_input, "pending-bit", 64);
}

static const VMStateDescription nikon_dual_event_banks_vmstate = {
    .name = "nikon-dual-event-banks",
    .unmigratable = true,
};

static void nikon_dual_event_banks_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &nikon_dual_event_banks_vmstate;
    dc->user_creatable = false;
    device_class_set_legacy_reset(dc, nikon_dual_event_banks_reset);
}

static const TypeInfo nikon_dual_event_banks_type = {
    .name = TYPE_NIKON_DUAL_EVENT_BANKS,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonDualEventBanksState),
    .instance_init = nikon_dual_event_banks_init,
    .class_init = nikon_dual_event_banks_class_init,
};

static void nikon_dual_event_banks_register_types(void)
{
    type_register_static(&nikon_dual_event_banks_type);
}
type_init(nikon_dual_event_banks_register_types)
