/*
 * Functional region decoder coupled to this board's finite DDR backings.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "migration/vmstate.h"
#include "hw/core/sysbus.h"
#include "hw/nikon/devices.h"

OBJECT_DECLARE_SIMPLE_TYPE(NikonMXICState, NIKON_MXIC)

typedef struct NikonRegionUnit {
    uint32_t draft[8];
    uint32_t active[8];
    unsigned written;
} NikonRegionUnit;

typedef struct NikonRAMEndpoint {
    MemoryRegion *ram;
    hwaddr address;
} NikonRAMEndpoint;

struct NikonMXICState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    NikonRegionUnit unit[2];
    uint32_t configuration[32];
    GArray *endpoints;
    bool reported_error;
};

/* Explicit virtual preloader decode profile, using original boot configuration.
 * These are not observed physical reset registers or installed DDR capacity. */
static const uint32_t initial_regions[2][8] = {
    { 0x60000000, 0x50000000, 0x50000000, 0xb0000000,
      0x50000000, 0, 0x30000, 0x40000000 },
    { 0x60000000, 0x50000000, 0x50000000, 0xb0000000 },
};

static bool nikon_mxic_unit_covers(NikonRegionUnit *unit, unsigned regions,
                                 uint64_t address, uint64_t size)
{
    unsigned i, matches = 0;

    for (i = 0; i < regions; i++) {
        uint64_t length = unit->active[i * 2];
        uint64_t start = unit->active[i * 2 + 1];

        if (length && start <= address && address + size <= start + length) {
            matches++;
        }
    }
    /* No priority is guessed if multiple regions cover a DDR endpoint. */
    return matches == 1;
}

static void nikon_mxic_apply(NikonMXICState *s)
{
    unsigned i;

    memory_region_transaction_begin();
    for (i = 0; i < s->endpoints->len; i++) {
        NikonRAMEndpoint *endpoint = &g_array_index(s->endpoints,
                                                 NikonRAMEndpoint, i);
        uint64_t size = memory_region_size(endpoint->ram);
        bool enabled = nikon_mxic_unit_covers(&s->unit[0], 4,
                                            endpoint->address, size) &&
                       nikon_mxic_unit_covers(&s->unit[1], 2,
                                            endpoint->address, size);

        memory_region_set_enabled(endpoint->ram, enabled);
    }
    memory_region_transaction_commit();
}

void nikon_mxic_attach_ram(DeviceState *dev, MemoryRegion *ram, hwaddr address)
{
    NikonMXICState *s = NIKON_MXIC(dev);
    NikonRAMEndpoint endpoint = { .ram = ram, .address = address };

    g_array_append_val(s->endpoints, endpoint);
    nikon_mxic_apply(s);
}

static MemTxResult nikon_mxic_error(NikonMXICState *s, hwaddr offset,
                                  unsigned size, bool write,
                                  uint64_t value, const char *reason)
{
    nikon_access_error(&s->reported_error, s->regs.addr + offset,
                       size, write, value, reason);
    return MEMTX_ERROR;
}

static int nikon_mxic_field(hwaddr offset, unsigned *word)
{
    if (offset >= 0x20 && offset < 0x40) {
        *word = (offset - 0x20) / 4;
        return 0;
    }
    if (offset >= 0xd50 && offset < 0xd60) {
        *word = (offset - 0xd50) / 4;
        return 1;
    }
    return -1;
}

static MemTxResult nikon_mxic_read(void *opaque, hwaddr offset,
                                 uint64_t *value, unsigned size,
                                 MemTxAttrs attrs)
{
    NikonMXICState *s = opaque;
    unsigned word;
    int unit = nikon_mxic_field(offset, &word);

    *value = 0;
    if (size != 4 || (offset & 3)) {
        return nikon_mxic_error(s, offset, size, false, 0,
                               "mxic-access-not-supported");
    }
    if (unit >= 0) {
        *value = s->unit[unit].draft[word];
        return MEMTX_OK;
    }
    if (offset >= 0xa00 && offset < 0xa80) {
        *value = s->configuration[(offset - 0xa00) / 4];
        return MEMTX_OK;
    }
    return nikon_mxic_error(s, offset, size, false, 0,
                           "mxic-access-not-supported");
}

static MemTxResult nikon_mxic_write(void *opaque, hwaddr offset,
                                  uint64_t value, unsigned size,
                                  MemTxAttrs attrs)
{
    NikonMXICState *s = opaque;
    unsigned word, i, regions;
    int unit = nikon_mxic_field(offset, &word);

    if (size != 4 || (offset & 3)) {
        return nikon_mxic_error(s, offset, size, true, value,
                               "mxic-access-not-supported");
    }
    if (unit >= 0) {
        s->unit[unit].draft[word] = value;
        s->unit[unit].written |= 1U << word;
        return MEMTX_OK;
    }
    if (offset >= 0xa00 && offset < 0xa80 && value <= 1) {
        s->configuration[(offset - 0xa00) / 4] = value;
        return MEMTX_OK;
    }
    if (offset != 0 && offset != 0xd40) {
        return nikon_mxic_error(s, offset, size, true, value,
                               "mxic-access-not-supported");
    }
    unit = offset == 0 ? 0 : 1;
    regions = unit == 0 ? 4 : 2;
    if (value != 1) {
        return nikon_mxic_error(s, offset, size, true, value,
                               "mxic-command-not-supported");
    }
    if (s->unit[unit].written != (1U << (regions * 2)) - 1) {
        return nikon_mxic_error(s, offset, size, true, value,
                               "mxic-incomplete-region-transaction");
    }
    for (i = 0; i < regions; i++) {
        uint64_t length = s->unit[unit].draft[i * 2];
        uint64_t address = s->unit[unit].draft[i * 2 + 1];

        /* Byte units and 64KiB granularity are declared virtual semantics. */
        if ((length & 0xffff) || (address & 0xffff) ||
            address + length > (1ULL << 32)) {
            return nikon_mxic_error(s, offset, size, true, value,
                                   "mxic-region-geometry-not-supported");
        }
    }
    memcpy(s->unit[unit].active, s->unit[unit].draft,
           sizeof(s->unit[unit].active));
    s->unit[unit].written = 0;
    nikon_mxic_apply(s);
    qemu_log_mask(LOG_GUEST_ERROR,
                  "nikon-mxic: unit=%d complete-region-plan applied; "
                  "finite-DDR-endpoints=%u\n", unit, s->endpoints->len);
    return MEMTX_OK;
}

static const MemoryRegionOps nikon_mxic_ops = {
    .read_with_attrs = nikon_mxic_read,
    .write_with_attrs = nikon_mxic_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
};

static void nikon_mxic_reset(DeviceState *dev)
{
    NikonMXICState *s = NIKON_MXIC(dev);
    unsigned unit;

    for (unit = 0; unit < 2; unit++) {
        memcpy(s->unit[unit].draft, initial_regions[unit],
               sizeof(s->unit[unit].draft));
        memcpy(s->unit[unit].active, initial_regions[unit],
               sizeof(s->unit[unit].active));
        s->unit[unit].written = 0;
    }
    s->reported_error = false;
    memset(s->configuration, 0, sizeof(s->configuration));
    nikon_mxic_apply(s);
}

static void nikon_mxic_init(Object *obj)
{
    NikonMXICState *s = NIKON_MXIC(obj);

    s->endpoints = g_array_new(false, false, sizeof(NikonRAMEndpoint));
    memory_region_init_io(&s->regs, obj, &nikon_mxic_ops, s,
                          "nikon.mxic-regions", 0xd60);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
}

static void nikon_mxic_finalize(Object *obj)
{
    NikonMXICState *s = NIKON_MXIC(obj);

    g_array_free(s->endpoints, true);
}

static const VMStateDescription nikon_mxic_vmstate = {
    .name = "nikon-mxic",
    .unmigratable = true,
};

static void nikon_mxic_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &nikon_mxic_vmstate;
    device_class_set_legacy_reset(dc, nikon_mxic_reset);
    dc->user_creatable = false;
}

static const TypeInfo nikon_mxic_type = {
    .name = TYPE_NIKON_MXIC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonMXICState),
    .instance_init = nikon_mxic_init,
    .instance_finalize = nikon_mxic_finalize,
    .class_init = nikon_mxic_class_init,
};

static void nikon_mxic_register_types(void)
{
    type_register_static(&nikon_mxic_type);
}
type_init(nikon_mxic_register_types)
