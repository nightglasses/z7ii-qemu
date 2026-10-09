/*
 * Original two-engine zero-fill and runtime control profile.
 * Units, stop acknowledgement, IRQ masks and error detail are virtual laws.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "migration/vmstate.h"
#include "system/address-spaces.h"
#include "system/dma.h"
#include "z7ii.h"

OBJECT_DECLARE_SIMPLE_TYPE(Z7IIFillState, Z7II_FILL)

typedef struct Z7IIFillEngine {
    uint32_t control, mask, flags, pattern, parameter, destination;
    uint32_t dimension, width, stride, count;
    uint32_t status, error, command;
    unsigned configured;
    bool busy;
    qemu_irq irq;
} Z7IIFillEngine;

struct Z7IIFillState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    Z7IIFillEngine engine[2];
    bool reported_error;
};

static void z7ii_fill_irq(Z7IIFillEngine *e)
{
    /* Named mask bits0/4 follow corresponding latched result bits. */
    qemu_set_irq(e->irq, !!(e->status & e->mask & 0x11));
}

static MemTxResult z7ii_fill_error(Z7IIFillState *s, hwaddr offset,
                                 unsigned size, bool write, uint64_t value,
                                 const char *reason)
{
    z7ii_access_error(&s->reported_error, 0x20070000ULL + offset,
                      size, write, value, reason);
    return MEMTX_ERROR;
}

static MemTxResult z7ii_fill_read(void *opaque, hwaddr offset,
                                uint64_t *value, unsigned size,
                                MemTxAttrs attrs)
{
    Z7IIFillState *s = opaque;
    Z7IIFillEngine *e = &s->engine[offset / 0x40];

    *value = 0;
    if (size != 4 || (offset & 3)) {
        return z7ii_fill_error(s, offset, size, false, 0,
                              "fill-access-not-supported");
    }
    switch (offset & 0x3f) {
    case 0:
        *value = e->control;
        break;
    case 4:
        *value = e->mask;
        break;
    case 8:
        *value = e->status;
        break;
    case 0xc:
        *value = e->error;
        break;
    case 0x10:
        *value = e->command;
        break;
    case 0x14:
        *value = e->flags;
        break;
    case 0x18:
        *value = e->pattern;
        break;
    case 0x1c:
        *value = e->dimension;
        break;
    case 0x20:
        *value = e->width;
        break;
    case 0x24:
        *value = e->stride;
        break;
    case 0x28:
        *value = e->count;
        break;
    case 0x2c:
        *value = e->parameter;
        break;
    case 0x30:
        *value = e->destination;
        break;
    default:
        return z7ii_fill_error(s, offset, size, false, 0,
                              "fill-access-not-supported");
    }
    return MEMTX_OK;
}

static MemTxResult z7ii_fill_start(Z7IIFillState *s, Z7IIFillEngine *e,
                                 hwaddr offset, MemTxAttrs attrs)
{
    uint64_t length = (uint64_t)e->width * e->count;
    MemTxResult result;

    if (e->control || e->configured != 127 || e->flags != 1 ||
        e->pattern || e->dimension || e->parameter ||
        e->width != 0x4000 || e->stride != e->width ||
        !e->count || e->count >= 0x4000 || (e->destination & 3) ||
        (uint64_t)e->destination + length > (1ULL << 32)) {
        return z7ii_fill_error(s, offset, 4, true, 1,
                              "fill-request-profile-not-supported");
    }
    e->busy = true;
    e->command = 1;
    /* Preserve existing synchronous byte-count zero mode. RAM-only official
     * DMA transactions reject device MMIO and produce genuine bus results. */
    attrs.memory = true;
    result = dma_memory_set(&address_space_memory, e->destination,
                            0, length, attrs);
    e->busy = false;
    e->command = 0;
    e->status = result == MEMTX_OK ? 1 : 0x10;
    /* Named destination-bus code1, not a Nikon physical error encoding. */
    e->error = result == MEMTX_OK ? 0 : 1U << 16;
    z7ii_fill_irq(e);
    qemu_log_mask(LOG_GUEST_ERROR,
                  "z7ii-fill: engine=%u destination=0x%08x bytes=0x%" PRIx64
                  " bus-result=%u status=0x%x\n",
                  (unsigned)(e - s->engine), e->destination, length,
                  result, e->status);
    return MEMTX_OK;
}

static MemTxResult z7ii_fill_write(void *opaque, hwaddr offset,
                                 uint64_t value, unsigned size,
                                 MemTxAttrs attrs)
{
    Z7IIFillState *s = opaque;
    Z7IIFillEngine *e = &s->engine[offset / 0x40];
    hwaddr reg = offset & 0x3f;

    if (size != 4 || (offset & 3)) {
        return z7ii_fill_error(s, offset, size, true, value,
                              "fill-access-not-supported");
    }
    if (reg == 8) {
        /* Full raw ISR ACK clears only implemented real result bits. */
        e->status &= ~(value & 0x11);
        z7ii_fill_irq(e);
        return MEMTX_OK;
    }
    if (reg == 0x10 && value == 0) {
        /* Supported DMA is synchronous: no deferred work can survive stop.
         * Stop remains a causal command transition independent of ACK. */
        e->command = 2;
        return MEMTX_OK;
    }
    if (e->busy) {
        return z7ii_fill_error(s, offset, size, true, value,
                              "fill-operation-busy");
    }
    if (e->status && reg != 0 && reg != 4) {
        return z7ii_fill_error(s, offset, size, true, value,
                              "fill-result-not-acknowledged");
    }
    switch (reg) {
    case 0:
        if (value > 1) {
            break;
        }
        e->control = value;
        return MEMTX_OK;
    case 4:
        e->mask = value;
        e->configured |= 1;
        z7ii_fill_irq(e);
        return MEMTX_OK;
    case 0x14:
        if (value & ~0x11U) {
            break;
        }
        e->flags = value;
        e->configured |= 4;
        return MEMTX_OK;
    case 0x18:
        if (value & ~0x03ffffffU) {
            break;
        }
        e->pattern = value;
        e->configured |= 2;
        return MEMTX_OK;
    case 0x1c: case 0x20: case 0x24: case 0x28:
        if (value > 0xffff) {
            break;
        }
        if (reg == 0x1c) {
            e->dimension = value;
        } else if (reg == 0x20) {
            e->width = value;
            e->configured |= 8;
        } else if (reg == 0x24) {
            e->stride = value;
            e->configured |= 16;
        } else {
            e->count = value;
            e->configured |= 32;
        }
        return MEMTX_OK;
    case 0x2c:
        e->parameter = value;
        return MEMTX_OK;
    case 0x30:
        e->destination = value;
        e->configured |= 64;
        return MEMTX_OK;
    case 0x10:
        if (value == 1) {
            return z7ii_fill_start(s, e, offset, attrs);
        }
        break;
    default:
        return z7ii_fill_error(s, offset, size, true, value,
                              "fill-access-not-supported");
    }
    return z7ii_fill_error(s, offset, size, true, value,
                          "fill-value-profile-not-supported");
}

static const MemoryRegionOps z7ii_fill_ops = {
    .read_with_attrs = z7ii_fill_read,
    .write_with_attrs = z7ii_fill_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
};

static void z7ii_fill_reset(DeviceState *dev)
{
    Z7IIFillState *s = Z7II_FILL(dev);
    unsigned i;

    /* Explicit virtual inactive reset, not recovered silicon. */
    for (i = 0; i < 2; i++) {
        qemu_irq irq = s->engine[i].irq;
        memset(&s->engine[i], 0, sizeof(s->engine[i]));
        s->engine[i].irq = irq;
        z7ii_fill_irq(&s->engine[i]);
    }
    s->reported_error = false;
}

static void z7ii_fill_init(Object *obj)
{
    Z7IIFillState *s = Z7II_FILL(obj);

    memory_region_init_io(&s->regs, obj, &z7ii_fill_ops, s, "z7ii.fill", 0x80);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->engine[0].irq);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->engine[1].irq);
}

static const VMStateDescription z7ii_fill_vmstate = {
    .name = "z7ii-fill",
    .unmigratable = true,
};

static void z7ii_fill_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &z7ii_fill_vmstate;
    device_class_set_legacy_reset(dc, z7ii_fill_reset);
    dc->user_creatable = false;
}

static const TypeInfo z7ii_fill_type = {
    .name = TYPE_Z7II_FILL,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Z7IIFillState),
    .instance_init = z7ii_fill_init,
    .class_init = z7ii_fill_class_init,
};

static void z7ii_fill_register_types(void)
{
    type_register_static(&z7ii_fill_type);
}
type_init(z7ii_fill_register_types)
