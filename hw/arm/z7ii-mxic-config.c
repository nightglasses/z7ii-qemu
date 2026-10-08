/* Functional MXIC snapshot/configuration transaction; not a traffic simulator.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/timer.h"
#include "migration/vmstate.h"
#include "hw/core/sysbus.h"
#include "z7ii.h"

OBJECT_DECLARE_SIMPLE_TYPE(Z7IIMXICConfigState, Z7II_MXIC_CONFIG)

static const unsigned config_offsets[] = {
    0x38, 0x3c, 0x100, 0x104, 0x108, 0x10c, 0x110, 0x114, 0x118, 0x11c,
    0x5004, 0x5020, 0x5024, 0x5028, 0x502c, 0x5030, 0x5034, 0x5038, 0x503c,
};
static const unsigned rate_offsets[] = {
    0x2648, 0x2490, 0x24a0, 0x2790, 0x2798,
    0x2628, 0x2658, 0x24a8, 0x27b0, 0x2618,
};

struct Z7IIMXICConfigState {
    SysBusDevice parent_obj;
    MemoryRegion regs, slots;
    QEMUTimer *timer;
    uint32_t draft[19], active[19];
    uint32_t draft_slots[512], active_slots[512];
    uint32_t routing[4], levels[36], rates[10];
    uint32_t written;
    bool slot_written[512];
    unsigned slots_written;
    bool active_view, access, busy, reported_error;
};

static int field_index(hwaddr offset, const unsigned *offsets, unsigned count)
{
    unsigned i;
    for (i = 0; i < count; i++) {
        if (offset == offsets[i]) {
            return i;
        }
    }
    return -1;
}

static int level_index(hwaddr offset)
{
    unsigned group, row, word;
    if (offset < 0x2000 || offset >= 0x2164) {
        return -1;
    }
    group = (offset - 0x2000) / 0x100;
    offset = (offset - 0x2000) % 0x100;
    row = offset / 0x40;
    word = offset % 0x40;
    if (group > 1 || row > 1 || word >= 0x24) {
        return -1;
    }
    return group * 18 + row * 9 + word / 4;
}

static uint32_t config_mask(unsigned index)
{
    if (index == 0) {
        return 0xffffff01;
    }
    if (index == 1) {
        return 0xff;
    }
    if (index < 10) {
        return 0x1f1f1f1f;
    }
    /* Nikon +5004/+5020 group has no public field binding: full word state. */
    return UINT32_MAX;
}

static MemTxResult config_error(Z7IIMXICConfigState *s, hwaddr address,
                               unsigned size, bool write, uint64_t value,
                               const char *reason)
{
    z7ii_access_error(&s->reported_error, address, size, write, value, reason);
    return MEMTX_ERROR;
}

static void config_complete(void *opaque)
{
    Z7IIMXICConfigState *s = opaque;
    memcpy(s->active, s->draft, sizeof(s->active));
    memcpy(s->active_slots, s->draft_slots, sizeof(s->active_slots));
    s->busy = false;
    s->written = 0;
    s->slots_written = 0;
    memset(s->slot_written, 0, sizeof(s->slot_written));
    qemu_log_mask(LOG_GUEST_ERROR,
                  "z7ii-mxic-config: configuration and 512 slots applied\n");
}

static MemTxResult config_read(void *opaque, hwaddr offset, uint64_t *value,
                               unsigned size, MemTxAttrs attrs)
{
    Z7IIMXICConfigState *s = opaque;
    int index;
    *value = 0;
    if (size != 4 || (offset & 3)) {
        goto unsupported;
    }
    if (offset == 0x44 || offset == 0x60) {
        *value = offset == 0x44 ? s->active_view : s->access;
        return MEMTX_OK;
    }
    index = field_index(offset, config_offsets, G_N_ELEMENTS(config_offsets));
    if (index >= 0) {
        *value = s->active_view ? s->active[index] : s->draft[index];
        if (index == 0) {
            *value = (*value & ~1U) | !s->busy;
        }
        return MEMTX_OK;
    }
    if (offset >= 0x4800 && offset < 0x5000) {
        index = (offset - 0x4800) / 4;
        *value = s->active_view ? s->active_slots[index] : s->draft_slots[index];
        return MEMTX_OK;
    }
    if (offset >= 0x5010 && offset < 0x5020) {
        *value = s->routing[(offset - 0x5010) / 4];
        return MEMTX_OK;
    }
    index = level_index(offset);
    if (index >= 0) {
        *value = s->levels[index];
        return MEMTX_OK;
    }
    index = field_index(offset, rate_offsets, G_N_ELEMENTS(rate_offsets));
    if (index >= 0) {
        *value = s->rates[index];
        return MEMTX_OK;
    }
unsupported:
    return config_error(s, 0x20010000ULL + offset, size, false, 0,
                        "mxic-config-access-not-supported");
}

static MemTxResult config_write(void *opaque, hwaddr offset, uint64_t value,
                                unsigned size, MemTxAttrs attrs)
{
    Z7IIMXICConfigState *s = opaque;
    int index;
    if (size != 4 || (offset & 3)) {
        goto unsupported;
    }
    if (s->busy) {
        return config_error(s, 0x20010000ULL + offset, size, true, value,
                            "mxic-config-busy");
    }
    if (offset == 0x44 || offset == 0x60) {
        if (value > 1) {
            goto unsupported;
        }
        if (offset == 0x44) {
            s->active_view = value;
        } else {
            s->access = value;
        }
        return MEMTX_OK;
    }
    if (s->active_view) {
        return config_error(s, 0x20010000ULL + offset, size, true, value,
                            "mxic-config-active-view-write");
    }
    if (offset == 0x34) {
        if (value != 1 || !s->access || s->written != 0x7ffff ||
            s->slots_written != 512) {
            return config_error(s, 0x20010034, size, true, value,
                                "mxic-config-incomplete-transaction");
        }
        s->busy = true;
        /* Explicit virtual completion latency; not measured silicon timing. */
        timer_mod_ns(s->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 10000);
        return MEMTX_OK;
    }
    index = field_index(offset, config_offsets, G_N_ELEMENTS(config_offsets));
    if (index >= 0) {
        if (!s->access || (value & ~config_mask(index))) {
            goto unsupported;
        }
        /* TCFLG is state-produced: snapshot writeback cannot set completion. */
        s->draft[index] = index == 0 ? value & ~1U : value;
        s->written |= 1U << index;
        return MEMTX_OK;
    }
    if (offset >= 0x5010 && offset < 0x5020) {
        s->routing[(offset - 0x5010) / 4] = value;
        return MEMTX_OK;
    }
    index = level_index(offset);
    if (index >= 0) {
        uint32_t mask = index % 3 == 0 ? 0x3f3f030f : 0x003f3f03;
        if (value & ~mask) {
            goto unsupported;
        }
        s->levels[index] = value;
        return MEMTX_OK;
    }
    index = field_index(offset, rate_offsets, G_N_ELEMENTS(rate_offsets));
    if (index >= 0) {
        if ((value & ~0x0fff0003U) || (value & 3) == 3) {
            goto unsupported;
        }
        s->rates[index] = value;
        return MEMTX_OK;
    }
unsupported:
    return config_error(s, 0x20010000ULL + offset, size, true, value,
                        "mxic-config-access-not-supported");
}

static MemTxResult slot_read(void *opaque, hwaddr offset, uint64_t *value,
                             unsigned size, MemTxAttrs attrs)
{
    Z7IIMXICConfigState *s = opaque;
    *value = 0;
    return config_error(s, 0x24074000ULL + offset, size, false, 0,
                        "mxic-slot-write-window-read");
}

static MemTxResult slot_write(void *opaque, hwaddr offset, uint64_t value,
                              unsigned size, MemTxAttrs attrs)
{
    Z7IIMXICConfigState *s = opaque;
    unsigned index = offset / 4;
    if (size != 4 || (offset & 3) || !s->access || s->active_view || s->busy ||
        (value & ~0x7f7f7f7fU)) {
        return config_error(s, 0x24074000ULL + offset, size, true, value,
                            "mxic-slot-access-not-supported");
    }
    s->draft_slots[index] = value;
    if (!s->slot_written[index]) {
        s->slot_written[index] = true;
        s->slots_written++;
    }
    return MEMTX_OK;
}

#define MXIC_OPS(reader, writer) { \
    .read_with_attrs = reader, .write_with_attrs = writer, \
    .endianness = DEVICE_LITTLE_ENDIAN, \
    .valid = { .min_access_size = 1, .max_access_size = 8, .unaligned = true }, \
    .impl = { .min_access_size = 1, .max_access_size = 8, .unaligned = true }, \
}
static const MemoryRegionOps config_ops = MXIC_OPS(config_read, config_write);
static const MemoryRegionOps slot_ops = MXIC_OPS(slot_read, slot_write);

static void config_reset(DeviceState *dev)
{
    Z7IIMXICConfigState *s = Z7II_MXIC_CONFIG(dev);
    timer_del(s->timer);
    memset(s->draft, 0, sizeof(s->draft));
    memset(s->active, 0, sizeof(s->active));
    memset(s->draft_slots, 0, sizeof(s->draft_slots));
    memset(s->active_slots, 0, sizeof(s->active_slots));
    memset(s->routing, 0, sizeof(s->routing));
    memset(s->levels, 0, sizeof(s->levels));
    memset(s->rates, 0, sizeof(s->rates));
    memset(s->slot_written, 0, sizeof(s->slot_written));
    s->written = s->slots_written = 0;
    s->active_view = s->access = s->busy = s->reported_error = false;
}

static void config_init(Object *obj)
{
    Z7IIMXICConfigState *s = Z7II_MXIC_CONFIG(obj);
    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, config_complete, s);
    memory_region_init_io(&s->regs, obj, &config_ops, s,
                          "z7ii.mxic-config", 0x5040);
    memory_region_init_io(&s->slots, obj, &slot_ops, s,
                          "z7ii.mxic-slot-write", 0x800);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->slots);
}

static void config_finalize(Object *obj)
{
    timer_free(Z7II_MXIC_CONFIG(obj)->timer);
}

static const VMStateDescription config_vmstate = {
    .name = "z7ii-mxic-config", .unmigratable = true,
};
static void config_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    dc->vmsd = &config_vmstate;
    device_class_set_legacy_reset(dc, config_reset);
    dc->user_creatable = false;
}
static const TypeInfo config_type = {
    .name = TYPE_Z7II_MXIC_CONFIG, .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Z7IIMXICConfigState),
    .instance_init = config_init, .instance_finalize = config_finalize,
    .class_init = config_class_init,
};
static void config_register_types(void)
{
    type_register_static(&config_type);
}
type_init(config_register_types)
