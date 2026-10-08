/*
 * Eight-channel HDMAC-compatible functional DMA interface.
 * Register/GIC geometry comes from original MAS and matching author sources.
 * Zero reset and nominal bus timing are virtual board assumptions.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "migration/vmstate.h"
#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "system/address-spaces.h"
#include "system/dma.h"
#include "z7ii.h"

#define HDMAC_DE (1U << 31)
#define HDMAC_EB (1U << 31)
#define HDMAC_ST (1U << 29)
#define HDMAC_SS (7U << 16)
#define HDMAC_CI (1U << 19)
#define HDMAC_EI (1U << 20)
#define HDMAC_BASE 0x2c020000ULL
OBJECT_DECLARE_SIMPLE_TYPE(Z7IIHDMACState, Z7II_HDMAC)

typedef struct Z7IIHDMACChannel {
    Z7IIHDMACState *owner;
    uint32_t ca, cb, source, destination;
    QEMUTimer *timer;
    qemu_irq irq;
    bool running;
    int64_t ready_at;
    uint64_t generation;
    uint32_t remaining;
} Z7IIHDMACChannel;

struct Z7IIHDMACState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    Clock *clock;
    uint32_t bank;
    uint32_t control;
    unsigned cursor;
    Z7IIHDMACChannel channel[8];
    bool reported_error;
    bool request[32];
};

static bool z7ii_hdmac_requested(Z7IIHDMACChannel *c)
{
    unsigned selection = (c->ca >> 24) & 31;

    return !selection || c->owner->request[selection];
}

static void z7ii_hdmac_irq(Z7IIHDMACChannel *c)
{
    unsigned status = (c->cb >> 16) & 7;

    qemu_set_irq(c->irq, status == 5 ? !!(c->cb & HDMAC_CI) :
                 (status >= 1 && status <= 4 && (c->cb & HDMAC_EI)));
}

static void z7ii_hdmac_finish(Z7IIHDMACChannel *c, unsigned status)
{
    timer_del(c->timer);
    c->generation++;
    c->running = false;
    c->ca &= ~HDMAC_EB;
    c->cb = (c->cb & ~HDMAC_SS) | (status << 16);
    z7ii_hdmac_irq(c);
}

static void z7ii_hdmac_transfer(void *opaque)
{
    Z7IIHDMACChannel *trigger = opaque;
    Z7IIHDMACState *s = trigger->owner;
    Z7IIHDMACChannel *c = NULL;
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    unsigned start = (s->control & (1U << 28)) ? s->cursor : 0;
    unsigned candidate;
    unsigned width, i;
    uint32_t beats;
    bool fixed_source, fixed_destination;
    uint8_t data[4];
    uint64_t generation;

    /* Fixed low-index or rotating arbitration among ready virtual requests. */
    for (candidate = 0; candidate < 8; candidate++) {
        unsigned index = (start + candidate) % 8;

        if (s->channel[index].running && s->channel[index].ready_at <= now &&
            z7ii_hdmac_requested(&s->channel[index])) {
            c = &s->channel[index];
            s->cursor = (index + 1) % 8;
            break;
        }
    }
    if (!c) {
        return;
    }
    if (c != trigger && trigger->running) {
        timer_mod(trigger->timer, now);
    }
    width = 1U << ((c->cb >> 26) & 3);
    /* Demand requests accept one beat, then re-evaluate the actual wire. */
    beats = ((c->ca >> 24) & 31) ? 1 : c->remaining;
    fixed_source = c->cb & (1U << 25);
    fixed_destination = c->cb & (1U << 24);
    generation = c->generation;

    for (i = 0; i < beats; i++) {
        MemTxResult result;

        if ((uint64_t)c->source + width > 1ULL << 32 ||
            (uint64_t)c->destination + width > 1ULL << 32) {
            z7ii_hdmac_finish(c, 1);
            return;
        }
        result = dma_memory_read(&address_space_memory, c->source, data, width,
                                  MEMTXATTRS_UNSPECIFIED);
        if (!c->running || c->generation != generation) {
            return;
        }
        if (result != MEMTX_OK) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "z7ii.hdmac: bank%u source bus error "
                          "at 0x%08x; SS3\n", s->bank, c->source);
            z7ii_hdmac_finish(c, 3);
            return;
        }
        result = dma_memory_write(&address_space_memory, c->destination,
                                   data, width, MEMTXATTRS_UNSPECIFIED);
        if (!c->running || c->generation != generation) {
            return;
        }
        if (result != MEMTX_OK) {
            qemu_log_mask(LOG_GUEST_ERROR, "z7ii.hdmac: bank%u destination bus "
                          "error at 0x%08x; SS4\n", s->bank, c->destination);
            z7ii_hdmac_finish(c, 4);
            return;
        }
        if (!fixed_source) {
            if ((uint64_t)c->source + width == 1ULL << 32 && c->remaining > 1) {
                z7ii_hdmac_finish(c, 1);
                return;
            }
            c->source += width;
        }
        if (!fixed_destination) {
            if ((uint64_t)c->destination + width == 1ULL << 32 &&
                c->remaining > 1) {
                z7ii_hdmac_finish(c, 1);
                return;
            }
            c->destination += width;
        }
        c->remaining--;
    }
    if (c->remaining) {
        if (z7ii_hdmac_requested(c)) {
            c->ready_at = now + MAX(1, clock_ticks_to_ns(s->clock, 1));
            timer_mod(c->timer, c->ready_at);
        }
        return;
    }
    /* End counters are a functional convention, not measured readback. */
    c->ca &= ~0xfffffU;
    z7ii_hdmac_finish(c, 5);
}

static bool z7ii_hdmac_start_supported(uint32_t ca, uint32_t cb,
                                       uint32_t source, uint32_t destination)
{
    unsigned beat = (ca >> 20) & 15;
    unsigned width = (cb >> 26) & 3;

    /* No wrap/reload, transfer transforms, address pitch or pause emulation. */
    return width < 3 && !(ca & (1U << 30)) &&
           (beat == 0 || beat == 8 || beat == 9 || beat == 11 ||
            beat == 13 || beat == 15) &&
           !(cb & 0xc0e0ff00U) && ((cb >> 28) & 3) < 3 &&
           !(source & ((1U << width) - 1)) &&
           !(destination & ((1U << width) - 1));
}

static void z7ii_hdmac_schedule(Z7IIHDMACChannel *c)
{
    Z7IIHDMACState *s = c->owner;
    uint32_t beats = (((c->ca >> 16) & 15) + 1) * ((c->ca & 0xffff) + 1);

    unsigned selection = (c->ca >> 24) & 31;

    if (c->running || !(s->control & HDMAC_DE) || !(c->ca & HDMAC_EB) ||
        (!selection && !(c->ca & HDMAC_ST))) {
        return;
    }
    c->running = true;
    c->generation++;
    c->remaining = beats;
    c->ready_at = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                  MAX(1, clock_ticks_to_ns(s->clock, selection ? 1 : beats));
    if (z7ii_hdmac_requested(c)) {
        timer_mod(c->timer, c->ready_at);
    }
}

static void z7ii_hdmac_request(void *opaque, int selection, int level)
{
    Z7IIHDMACState *s = opaque;
    unsigned i;

    s->request[selection] = level;
    for (i = 0; i < 8; i++) {
        Z7IIHDMACChannel *c = &s->channel[i];

        if (!c->running || ((c->ca >> 24) & 31) != selection) {
            continue;
        }
        if (!level) {
            timer_del(c->timer);
        } else {
            c->ready_at = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                          MAX(1, clock_ticks_to_ns(s->clock, 1));
            timer_mod(c->timer, c->ready_at);
        }
    }
}

static MemTxResult z7ii_hdmac_error(Z7IIHDMACState *s, hwaddr offset,
                                   unsigned size, bool write, uint64_t value)
{
    z7ii_access_error(&s->reported_error,
                      HDMAC_BASE + s->bank * 0x10000 + offset,
                      size, write, value,
                      "hdmac-access-or-command-not-supported");
    return MEMTX_ERROR;
}

static MemTxResult z7ii_hdmac_read(void *opaque, hwaddr offset, uint64_t *value,
                                  unsigned size, MemTxAttrs attrs)
{
    Z7IIHDMACState *s = opaque;
    Z7IIHDMACChannel *c;

    *value = 0;
    if (size != 4 || (offset & 3) || (offset && offset < 0x10) ||
        offset >= 0x90) {
        return z7ii_hdmac_error(s, offset, size, false, 0);
    }
    if (!offset) {
        *value = s->control;
        return MEMTX_OK;
    }
    c = &s->channel[(offset - 0x10) >> 4];
    switch (offset & 15) {
    case 0:
        *value = c->ca;
        break;
    case 4:
        *value = c->cb;
        break;
    case 8:
        *value = c->source;
        break;
    case 12:
        *value = c->destination;
        break;
    }
    return MEMTX_OK;
}

static MemTxResult z7ii_hdmac_write(void *opaque, hwaddr offset, uint64_t value,
                                   unsigned size, MemTxAttrs attrs)
{
    Z7IIHDMACState *s = opaque;
    Z7IIHDMACChannel *c;
    uint32_t old;
    unsigned i;

    if (size != 4 || (offset & 3) || (offset && offset < 0x10) ||
        offset >= 0x90) {
        return z7ii_hdmac_error(s, offset, size, true, value);
    }
    if (!offset) {
        if (value & ~0x90000000U) {
            return z7ii_hdmac_error(s, offset, size, true, value);
        }
        /* Preflight every armed request before changing DE or any timer. */
        if (value & HDMAC_DE) {
            for (i = 0; i < 8; i++) {
                c = &s->channel[i];
                if ((c->ca & HDMAC_EB) &&
                    !z7ii_hdmac_start_supported(c->ca, c->cb,
                                                c->source, c->destination)) {
                    return z7ii_hdmac_error(s, offset, size, true, value);
                }
            }
        }
        s->control = value;
        for (i = 0; i < 8; i++) {
            c = &s->channel[i];
            if (!(value & HDMAC_DE) && c->running) {
                z7ii_hdmac_finish(c, 2);
            } else {
                z7ii_hdmac_schedule(c);
            }
        }
        return MEMTX_OK;
    }
    c = &s->channel[(offset - 0x10) >> 4];
    switch (offset & 15) {
    case 0:
        old = c->ca;
        if (c->running && ((value ^ old) & ~HDMAC_EB)) {
            return z7ii_hdmac_error(s, offset, size, true, value);
        }
        if ((value & HDMAC_EB) &&
            !z7ii_hdmac_start_supported(value, c->cb,
                                        c->source, c->destination)) {
            return z7ii_hdmac_error(s, offset, size, true, value);
        }
        c->ca = value;
        if (!(value & HDMAC_EB) && (old & HDMAC_EB)) {
            z7ii_hdmac_finish(c, 2);
        } else {
            z7ii_hdmac_schedule(c);
        }
        break;
    case 4:
        if ((value & 0xc0e0ffffU) || ((value >> 26) & 3) == 3 ||
            ((value >> 28) & 3) == 3 || ((value & HDMAC_SS) &&
                             (value & HDMAC_SS) != (c->cb & HDMAC_SS)) ||
            (c->running &&
             ((value ^ c->cb) & ~(HDMAC_SS | HDMAC_CI | HDMAC_EI)))) {
            return z7ii_hdmac_error(s, offset, size, true, value);
        }
        if ((c->ca & HDMAC_EB) &&
            !z7ii_hdmac_start_supported(c->ca, value,
                                        c->source, c->destination)) {
            return z7ii_hdmac_error(s, offset, size, true, value);
        }
        c->cb = value;
        z7ii_hdmac_irq(c);
        break;
    case 8:
    case 12:
        if (c->running) {
            return z7ii_hdmac_error(s, offset, size, true, value);
        }
        if ((c->ca & HDMAC_EB) &&
            !z7ii_hdmac_start_supported(c->ca, c->cb,
                                        (offset & 15) == 8 ? value : c->source,
                                        (offset & 15) == 12 ? value :
                                                            c->destination)) {
            return z7ii_hdmac_error(s, offset, size, true, value);
        }
        if ((offset & 15) == 8) {
            c->source = value;
        } else {
            c->destination = value;
        }
        break;
    }
    return MEMTX_OK;
}

static const MemoryRegionOps z7ii_hdmac_ops = {
    .read_with_attrs = z7ii_hdmac_read,
    .write_with_attrs = z7ii_hdmac_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
};

static void z7ii_hdmac_reset(DeviceState *dev)
{
    Z7IIHDMACState *s = Z7II_HDMAC(dev);
    unsigned i;

    s->control = 0;
    s->cursor = 0;
    s->reported_error = false;
    clock_update_hz(s->clock, 48000000);
    for (i = 0; i < 8; i++) {
        Z7IIHDMACChannel *c = &s->channel[i];

        timer_del(c->timer);
        c->generation++;
        c->ca = c->cb = c->source = c->destination = 0;
        c->running = false;
        z7ii_hdmac_irq(c);
    }
}

static void z7ii_hdmac_init(Object *obj)
{
    Z7IIHDMACState *s = Z7II_HDMAC(obj);
    unsigned i;

    s->clock = qdev_init_clock_out(DEVICE(obj), "nominal-hdmac-clock");
    qdev_init_gpio_in(DEVICE(obj), z7ii_hdmac_request, 32);
    memory_region_init_io(&s->regs, obj, &z7ii_hdmac_ops, s,
                          "z7ii.hdmac", 0x90);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
    for (i = 0; i < 8; i++) {
        s->channel[i].owner = s;
        s->channel[i].timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                           z7ii_hdmac_transfer, &s->channel[i]);
        sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->channel[i].irq);
    }
}

static void z7ii_hdmac_finalize(Object *obj)
{
    Z7IIHDMACState *s = Z7II_HDMAC(obj);
    unsigned i;

    for (i = 0; i < 8; i++) {
        timer_free(s->channel[i].timer);
    }
}

static const Property z7ii_hdmac_properties[] = {
    DEFINE_PROP_UINT32("bank", Z7IIHDMACState, bank, 0),
};
static const VMStateDescription z7ii_hdmac_vmstate = {
    .name = "z7ii-hdmac", .unmigratable = true,
};

static void z7ii_hdmac_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &z7ii_hdmac_vmstate;
    dc->user_creatable = false;
    device_class_set_props(dc, z7ii_hdmac_properties);
    device_class_set_legacy_reset(dc, z7ii_hdmac_reset);
}

static const TypeInfo z7ii_hdmac_type = {
    .name = TYPE_Z7II_HDMAC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Z7IIHDMACState),
    .instance_init = z7ii_hdmac_init,
    .instance_finalize = z7ii_hdmac_finalize,
    .class_init = z7ii_hdmac_class_init,
};

static void z7ii_hdmac_register_types(void)
{
    type_register_static(&z7ii_hdmac_type);
}
type_init(z7ii_hdmac_register_types)
