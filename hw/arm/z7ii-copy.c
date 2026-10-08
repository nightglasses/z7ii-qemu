/*
 * Four-channel original default byte-copy interface.
 * Count/source/destination are joined to the original memcpy callers.
 * Zero reset, 48MHz throughput, residual count and bus-error status1 are
 * virtual-board conventions; success8 is the original software condition.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-clock.h"
#include "migration/vmstate.h"
#include "system/address-spaces.h"
#include "system/dma.h"
#include "z7ii.h"

OBJECT_DECLARE_SIMPLE_TYPE(Z7IICopyState, Z7II_COPY)

/* Original setup BFI fields, separate from preserved trigger/mode bits. */
#define COPY_SETUP_MASK 0x00f09110U

typedef struct Z7IICopyChannel {
    Z7IICopyState *owner;
    QEMUTimer *timer;
    qemu_irq irq;
    uint32_t reg[9];
    uint64_t remaining;
    unsigned configured;
    bool running;
} Z7IICopyChannel;

struct Z7IICopyState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    Clock *clock;
    Z7IICopyChannel channel[4];
    uint32_t control;
    bool reported_error;
};

static void z7ii_copy_irq(Z7IICopyChannel *c)
{
    qemu_set_irq(c->irq, (c->reg[6] & (1U << 15)) && c->reg[8]);
}

static void z7ii_copy_finish(Z7IICopyChannel *c, uint32_t status)
{
    timer_del(c->timer);
    c->running = false;
    c->reg[6] &= ~3U;
    c->reg[8] = status;
    z7ii_copy_irq(c);
}

static void z7ii_copy_schedule(Z7IICopyChannel *c)
{
    timer_mod(c->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
              clock_ticks_to_ns(c->owner->clock, MIN(c->remaining, 256)) + 1);
}

static bool z7ii_copy_memory_range(hwaddr addr, uint64_t length, bool write)
{
    while (length) {
        MemoryRegionSection section = memory_region_find(get_system_memory(),
                                                        addr, length);
        uint64_t span;
        bool direct;

        if (!section.mr) {
            return false;
        }
        span = int128_get64(section.size);
        direct = section.offset_within_address_space == addr && span &&
                 memory_access_is_direct(section.mr, write,
                                         MEMTXATTRS_UNSPECIFIED) &&
                 (!write || !section.readonly);
        memory_region_unref(section.mr);
        if (!direct) {
            return false;
        }
        addr += span;
        length -= span;
    }
    return true;
}

static void z7ii_copy_step(void *opaque)
{
    Z7IICopyChannel *c = opaque;
    uint8_t buffer[256];
    unsigned length = MIN(c->remaining, sizeof(buffer));
    MemTxAttrs attrs = MEMTXATTRS_UNSPECIFIED;
    MemTxResult result;

    if (!c->running) {
        return;
    }
    /* ROMD is a direct source, but QEMU's memory attribute only admits RAM. */
    if (!z7ii_copy_memory_range(c->reg[1], c->remaining, false) ||
        !z7ii_copy_memory_range(c->reg[2], c->remaining, true)) {
        z7ii_copy_finish(c, 1);
        return;
    }
    result = dma_memory_read(&address_space_memory, c->reg[1], buffer,
                             length, attrs);
    if (result == MEMTX_OK) {
        /* Destinations remain ordinary writable RAM, never ROMD or MMIO. */
        attrs.memory = true;
        result = dma_memory_write(&address_space_memory, c->reg[2], buffer,
                                  length, attrs);
    }
    if (result != MEMTX_OK) {
        z7ii_copy_finish(c, 1);
        return;
    }
    c->reg[1] += length;
    c->reg[2] += length;
    c->remaining -= length;
    /* Functional residual encoding, not recovered silicon progress format. */
    c->reg[0] = c->remaining ? c->remaining - 1 : 0;
    if (!c->remaining) {
        z7ii_copy_finish(c, 8);
    } else {
        z7ii_copy_schedule(c);
    }
}

static MemTxResult z7ii_copy_error(Z7IICopyState *s, hwaddr offset,
                                  unsigned size, bool write, uint64_t value)
{
    z7ii_access_error(&s->reported_error, 0x04251000 + offset, size, write,
                      value, "copy-access-or-profile-not-supported");
    return MEMTX_ERROR;
}

static Z7IICopyChannel *z7ii_copy_channel(Z7IICopyState *s, hwaddr offset,
                                        unsigned *index)
{
    if (offset < 0x10 || offset >= 0xd0) {
        return NULL;
    }
    offset -= 0x10;
    *index = (offset % 0x30) / 4;
    return *index <= 8 ? &s->channel[offset / 0x30] : NULL;
}

static MemTxResult z7ii_copy_read(void *opaque, hwaddr offset,
                                 uint64_t *value, unsigned size,
                                 MemTxAttrs attrs)
{
    Z7IICopyState *s = opaque;
    Z7IICopyChannel *c;
    unsigned index;

    *value = 0;
    if (size == 4 && !(offset & 3)) {
        if (!offset) {
            *value = s->control;
            return MEMTX_OK;
        }
        c = z7ii_copy_channel(s, offset, &index);
        if (c && index != 7) {
            *value = c->reg[index];
            return MEMTX_OK;
        }
    }
    return z7ii_copy_error(s, offset, size, false, 0);
}

static MemTxResult z7ii_copy_write(void *opaque, hwaddr offset,
                                  uint64_t value, unsigned size,
                                  MemTxAttrs attrs)
{
    Z7IICopyState *s = opaque;
    Z7IICopyChannel *c;
    unsigned index, i;

    if (size != 4 || (offset & 3)) {
        goto error;
    }
    if (!offset) {
        /*
         * The physical roles/effects of all three global bits are unknown.
         * This virtual profile uses bit28 as enable/cancel and retains20/24
         * as configuration inputs, without submitting work or inventing IRQ.
         */
        if (value & ~0x11100000U) {
            goto error;
        }
        s->control = value;
        if (!(value & (1U << 28))) {
            for (i = 0; i < 4; i++) {
                c = &s->channel[i];
                if (c->running) {
                    z7ii_copy_finish(c, 1);
                }
            }
        }
        return MEMTX_OK;
    }
    c = z7ii_copy_channel(s, offset, &index);
    if (!c || index == 7) {
        goto error;
    }
    if (index == 8) {
        /* Original ISR clears low4 by RMW: zero clears, one preserves. */
        if (value & ~c->reg[8]) {
            goto error;
        }
        c->reg[8] &= value;
        z7ii_copy_irq(c);
        return MEMTX_OK;
    }
    if (c->running) {
        if (index != 6 || (value & 3) ||
            ((value ^ c->reg[6]) & ~3U)) {
            goto error;
        }
        c->reg[6] = value;
        z7ii_copy_finish(c, 1);
        return MEMTX_OK;
    }
    if (index == 6 && (value & 3)) {
        uint64_t length = (uint64_t)c->reg[0] + 1;

        if (value != 0x11109003 || !(s->control & (1U << 28)) ||
            c->configured != 0x3f || c->reg[3] != 0x30f00 ||
            c->reg[4] != 0x30f00 || c->reg[5] || c->reg[8] ||
            (uint64_t)c->reg[1] + length > (1ULL << 32) ||
            (uint64_t)c->reg[2] + length > (1ULL << 32)) {
            goto error;
        }
        c->reg[6] = value;
        c->remaining = length;
        c->running = true;
        z7ii_copy_schedule(c);
        return MEMTX_OK;
    }
    if ((index == 3 || index == 4) && value != 0x30f00) {
        goto error;
    }
    if (index == 5 && value) {
        goto error;
    }
    if (index == 6) {
        bool setup = (value & COPY_SETUP_MASK) == 0x9000 &&
                     !((value ^ c->reg[6]) & ~COPY_SETUP_MASK);

        if (!setup && value != (c->reg[6] & ~3U)) {
            goto error;
        }
    }
    c->reg[index] = value;
    if (index < 6) {
        c->configured |= 1U << index;
    }
    z7ii_copy_irq(c);
    return MEMTX_OK;
error:
    return z7ii_copy_error(s, offset, size, true, value);
}

static const MemoryRegionOps z7ii_copy_ops = {
    .read_with_attrs = z7ii_copy_read,
    .write_with_attrs = z7ii_copy_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
};

static void z7ii_copy_reset(DeviceState *dev)
{
    Z7IICopyState *s = Z7II_COPY(dev);
    unsigned i;

    s->control = 0;
    s->reported_error = false;
    clock_update_hz(s->clock, 48000000);
    for (i = 0; i < 4; i++) {
        Z7IICopyChannel *c = &s->channel[i];

        timer_del(c->timer);
        memset(c->reg, 0, sizeof(c->reg));
        c->remaining = c->configured = 0;
        c->running = false;
        z7ii_copy_irq(c);
    }
}

static void z7ii_copy_init(Object *obj)
{
    Z7IICopyState *s = Z7II_COPY(obj);
    unsigned i;

    s->clock = qdev_init_clock_out(DEVICE(obj), "nominal-copy-clock");
    memory_region_init_io(&s->regs, obj, &z7ii_copy_ops, s, "z7ii.copy", 0xd0);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
    for (i = 0; i < 4; i++) {
        Z7IICopyChannel *c = &s->channel[i];

        c->owner = s;
        c->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, z7ii_copy_step, c);
        sysbus_init_irq(SYS_BUS_DEVICE(obj), &c->irq);
    }
}

static void z7ii_copy_finalize(Object *obj)
{
    Z7IICopyState *s = Z7II_COPY(obj);
    unsigned i;

    for (i = 0; i < 4; i++) {
        timer_free(s->channel[i].timer);
    }
}

static const VMStateDescription z7ii_copy_vmstate = {
    .name = "z7ii-copy", .unmigratable = true,
};

static void z7ii_copy_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &z7ii_copy_vmstate;
    dc->user_creatable = false;
    device_class_set_legacy_reset(dc, z7ii_copy_reset);
}

static const TypeInfo z7ii_copy_type = {
    .name = TYPE_Z7II_COPY,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Z7IICopyState),
    .instance_init = z7ii_copy_init,
    .instance_finalize = z7ii_copy_finalize,
    .class_init = z7ii_copy_class_init,
};

static void z7ii_copy_register_types(void)
{
    type_register_static(&z7ii_copy_type);
}
type_init(z7ii_copy_register_types)
