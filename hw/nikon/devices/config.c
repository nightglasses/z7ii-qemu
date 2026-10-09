/* Functional MXIC snapshot/configuration transaction; not a traffic simulator.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/timer.h"
#include "migration/vmstate.h"
#include "hw/core/sysbus.h"
#include "hw/nikon/devices.h"

OBJECT_DECLARE_SIMPLE_TYPE(NikonMXICConfigState, NIKON_MXIC_CONFIG)

typedef struct NikonMXICFieldGroup {
    unsigned base;
    unsigned count;
    unsigned stride;
} NikonMXICFieldGroup;

/* MAS snapshot/transaction closure 5b2db90, and SLA source 1076d278
 * 100538b0/10053b18: two TCFMD words, eight acceptance words, and nine
 * Nikon group-configuration words. Fields retain guest state, not a profile
 * of accepted firmware values.
 */
static const NikonMXICFieldGroup config_groups[] = {
    { 0x38, 2, 4 }, { 0x100, 8, 4 },
    { 0x5004, 1, 4 }, { 0x5020, 8, 4 },
};

/* Complete source-qualified live TRMC word-zero families. The period/mode
 * format has a manufacturer counterpart, but the Nikon array bases differ:
 * do not expose the manufacturer's entire address window by inference.
 * SLA's row has a configuration pointer followed by ten parameter words;
 * two parameters have two consumers. MAS additionally programs +2618.
 * Word-one registers and gaps are not part of either recovered contract.
 */
static const NikonMXICFieldGroup rate_groups[] = {
    { 0x2490, 2, 0x10 }, { 0x2618, 2, 0x10 },
    { 0x2638, 3, 8 }, { 0x2658, 2, 8 },
    { 0x2790, 2, 8 }, { 0x27a8, 2, 8 },
};

struct NikonMXICConfigState {
    SysBusDevice parent_obj;
    MemoryRegion regs, slots;
    QEMUTimer *timer;
    uint32_t draft[19], active[19];
    uint32_t draft_slots[512], active_slots[512];
    uint32_t routing[4], levels[36], rates[13], selectors[124];
    uint32_t written;
    bool slot_written[512];
    unsigned slots_written;
    bool active_view, access, busy, reported_error;
};

static int field_index(hwaddr offset, const NikonMXICFieldGroup *groups,
                       unsigned count)
{
    unsigned i, first = 0;

    for (i = 0; i < count; i++) {
        if (offset >= groups[i].base &&
            (offset - groups[i].base) % groups[i].stride == 0 &&
            (offset - groups[i].base) / groups[i].stride < groups[i].count) {
            return first + (offset - groups[i].base) / groups[i].stride;
        }
        first += groups[i].count;
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

static int selector_index(hwaddr offset)
{
    unsigned bank, word;
    if (offset < 0x3000 || offset >= 0x33dc) {
        return -1;
    }
    bank = (offset - 0x3000) / 0x100;
    word = (offset - 0x3000) % 0x100;
    if (word <= 0x58) {
        return bank * 31 + word / 4;
    }
    if (word >= 0xbc && word <= 0xd8) {
        return bank * 31 + 23 + (word - 0xbc) / 4;
    }
    return -1;
}

static MemTxResult config_error(NikonMXICConfigState *s, hwaddr address,
                               unsigned size, bool write, uint64_t value,
                               const char *reason)
{
    nikon_access_error(&s->reported_error, address, size, write, value, reason);
    return MEMTX_ERROR;
}

static void config_complete(void *opaque)
{
    NikonMXICConfigState *s = opaque;
    memcpy(s->active, s->draft, sizeof(s->active));
    memcpy(s->active_slots, s->draft_slots, sizeof(s->active_slots));
    s->busy = false;
    s->written = 0;
    s->slots_written = 0;
    memset(s->slot_written, 0, sizeof(s->slot_written));
    qemu_log_mask(LOG_GUEST_ERROR,
                  "nikon-mxic-config: configuration and 512 slots applied\n");
}

static MemTxResult config_read(void *opaque, hwaddr offset, uint64_t *value,
                               unsigned size, MemTxAttrs attrs)
{
    NikonMXICConfigState *s = opaque;
    int index;
    *value = 0;
    if (size != 4 || (offset & 3)) {
        goto unsupported;
    }
    if (offset == 0x44 || offset == 0x60) {
        *value = offset == 0x44 ? s->active_view : s->access;
        return MEMTX_OK;
    }
    index = field_index(offset, config_groups, ARRAY_SIZE(config_groups));
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
    index = field_index(offset, rate_groups, ARRAY_SIZE(rate_groups));
    if (index >= 0) {
        *value = s->rates[index];
        return MEMTX_OK;
    }
    index = selector_index(offset);
    if (index >= 0) {
        *value = s->selectors[index];
        return MEMTX_OK;
    }
unsupported:
    return config_error(s, s->regs.addr + offset, size, false, 0,
                        "mxic-config-access-not-supported");
}

static MemTxResult config_write(void *opaque, hwaddr offset, uint64_t value,
                                unsigned size, MemTxAttrs attrs)
{
    NikonMXICConfigState *s = opaque;
    int index;
    if (size != 4 || (offset & 3)) {
        goto unsupported;
    }
    if (s->busy) {
        return config_error(s, s->regs.addr + offset, size, true, value,
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
        return config_error(s, s->regs.addr + offset, size, true, value,
                            "mxic-config-active-view-write");
    }
    if (offset == 0x34) {
        if (value != 1 || !s->access || s->written != 0x7ffff ||
            s->slots_written != 512) {
            return config_error(s, s->regs.addr + 0x34, size, true, value,
                                "mxic-config-incomplete-transaction");
        }
        s->busy = true;
        /* Explicit virtual completion latency; not measured silicon timing. */
        timer_mod_ns(s->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 10000);
        return MEMTX_OK;
    }
    index = field_index(offset, config_groups, ARRAY_SIZE(config_groups));
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
    index = field_index(offset, rate_groups, ARRAY_SIZE(rate_groups));
    if (index >= 0) {
        if (value & ~0x0fff0003U) {
            goto unsupported;
        }
        s->rates[index] = value;
        return MEMTX_OK;
    }
    index = selector_index(offset);
    if (index >= 0) {
        /* Candidate port/level selectors, not assigned physical master IDs. */
        if (value & ~0x33U) {
            goto unsupported;
        }
        s->selectors[index] = value;
        return MEMTX_OK;
    }
unsupported:
    return config_error(s, s->regs.addr + offset, size, true, value,
                        "mxic-config-access-not-supported");
}

static MemTxResult slot_read(void *opaque, hwaddr offset, uint64_t *value,
                             unsigned size, MemTxAttrs attrs)
{
    NikonMXICConfigState *s = opaque;
    *value = 0;
    return config_error(s, s->slots.addr + offset, size, false, 0,
                        "mxic-slot-write-window-read");
}

static MemTxResult slot_write(void *opaque, hwaddr offset, uint64_t value,
                              unsigned size, MemTxAttrs attrs)
{
    NikonMXICConfigState *s = opaque;
    unsigned index = offset / 4;
    if (size != 4 || (offset & 3) || !s->access || s->active_view || s->busy ||
        (value & ~0x7f7f7f7fU)) {
        return config_error(s, s->slots.addr + offset, size, true, value,
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
    NikonMXICConfigState *s = NIKON_MXIC_CONFIG(dev);
    timer_del(s->timer);
    memset(s->draft, 0, sizeof(s->draft));
    memset(s->active, 0, sizeof(s->active));
    memset(s->draft_slots, 0, sizeof(s->draft_slots));
    memset(s->active_slots, 0, sizeof(s->active_slots));
    memset(s->routing, 0, sizeof(s->routing));
    memset(s->levels, 0, sizeof(s->levels));
    memset(s->rates, 0, sizeof(s->rates));
    memset(s->selectors, 0, sizeof(s->selectors));
    memset(s->slot_written, 0, sizeof(s->slot_written));
    s->written = s->slots_written = 0;
    s->active_view = s->access = s->busy = s->reported_error = false;
}

static void config_init(Object *obj)
{
    NikonMXICConfigState *s = NIKON_MXIC_CONFIG(obj);
    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, config_complete, s);
    memory_region_init_io(&s->regs, obj, &config_ops, s,
                          "nikon.mxic-config", 0x5040);
    memory_region_init_io(&s->slots, obj, &slot_ops, s,
                          "nikon.mxic-slot-write", 0x800);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->slots);
}

static void config_finalize(Object *obj)
{
    timer_free(NIKON_MXIC_CONFIG(obj)->timer);
}

static const VMStateDescription config_vmstate = {
    .name = "nikon-mxic-config", .unmigratable = true,
};
static void config_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    dc->vmsd = &config_vmstate;
    device_class_set_legacy_reset(dc, config_reset);
    dc->user_creatable = false;
}
static const TypeInfo config_type = {
    .name = TYPE_NIKON_MXIC_CONFIG, .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonMXICConfigState),
    .instance_init = config_init, .instance_finalize = config_finalize,
    .class_init = config_class_init,
};
static void config_register_types(void)
{
    type_register_static(&config_type);
}
type_init(config_register_types)
