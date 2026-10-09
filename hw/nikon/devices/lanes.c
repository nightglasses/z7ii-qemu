/*
 * Original two-lane configuration writer, SLA10089b06..10089b32.
 * The complete interface supplies three boolean controls and two opaque
 * parameters. Retention and cold zero are functional assumptions; their
 * physical effects, result source and completion protocol remain unbound.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "hw/core/sysbus.h"
#include "hw/nikon/devices.h"
#include "hw/nikon/lanes.h"

OBJECT_DECLARE_SIMPLE_TYPE(NikonLanesState, NIKON_LANES)

typedef struct NikonLane {
    NikonLanesState *owner;
    MemoryRegion regs;
    uint32_t config[5];
} NikonLane;

struct NikonLanesState {
    SysBusDevice parent_obj;
    NikonLane lane[2];
    bool reported_error;
};

static MemTxResult nikon_lane_error(NikonLane *lane, hwaddr offset,
                                    unsigned size, bool write, uint64_t value)
{
    nikon_access_error(&lane->owner->reported_error, lane->regs.addr + offset,
                       size, write, value, "lane-configuration-not-supported");
    return MEMTX_ERROR;
}

static MemTxResult nikon_lane_read(void *opaque, hwaddr offset, uint64_t *value,
                                   unsigned size, MemTxAttrs attrs)
{
    NikonLane *lane = opaque;

    *value = 0;
    /* The whole original method contains no MMIO read or result consumer. */
    return nikon_lane_error(lane, offset, size, false, 0);
}

static MemTxResult nikon_lane_write(void *opaque, hwaddr offset, uint64_t value,
                                    unsigned size, MemTxAttrs attrs)
{
    NikonLane *lane = opaque;

    if (size != 4 || (offset & 3) || offset >= sizeof(lane->config) ||
        (offset <= 8 && value > 1)) {
        return nikon_lane_error(lane, offset, size, true, value);
    }
    /* Each source store takes effect independently, in guest program order.
     * Configuration never fabricates a result, edge, IRQ or guest callback. */
    lane->config[offset / 4] = value;
    return MEMTX_OK;
}

static const MemoryRegionOps nikon_lane_ops = {
    .read_with_attrs = nikon_lane_read,
    .write_with_attrs = nikon_lane_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
};

static void nikon_lanes_reset(DeviceState *dev)
{
    NikonLanesState *s = NIKON_LANES(dev);
    unsigned i;

    for (i = 0; i < ARRAY_SIZE(s->lane); i++) {
        memset(s->lane[i].config, 0, sizeof(s->lane[i].config));
    }
    s->reported_error = false;
}

static void nikon_lanes_init(Object *obj)
{
    NikonLanesState *s = NIKON_LANES(obj);
    unsigned i;

    for (i = 0; i < ARRAY_SIZE(s->lane); i++) {
        NikonLane *lane = &s->lane[i];
        g_autofree char *name = g_strdup_printf("nikon.lane%u", i);

        lane->owner = s;
        memory_region_init_io(&lane->regs, obj, &nikon_lane_ops, lane,
                              name, sizeof(lane->config));
        sysbus_init_mmio(SYS_BUS_DEVICE(obj), &lane->regs);
    }
}

static const VMStateDescription nikon_lanes_vmstate = {
    .name = "nikon-lanes",
    .unmigratable = true,
};

static void nikon_lanes_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &nikon_lanes_vmstate;
    dc->user_creatable = false;
    device_class_set_legacy_reset(dc, nikon_lanes_reset);
}

static const TypeInfo nikon_lanes_type = {
    .name = TYPE_NIKON_LANES,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonLanesState),
    .instance_init = nikon_lanes_init,
    .class_init = nikon_lanes_class_init,
};

static void nikon_lanes_register_types(void)
{
    type_register_static(&nikon_lanes_type);
}
type_init(nikon_lanes_register_types)
