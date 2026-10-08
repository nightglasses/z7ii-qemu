/*
 * Bounded functional timestamp counter, with an independent nominal clock.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "qemu/timer.h"
#include "hw/core/sysbus.h"
#include "hw/core/qdev-clock.h"
#include "z7ii.h"

#define TIMESTAMP_BASE 0x04266000ULL
#define TIMESTAMP_NOMINAL_HZ 48000000
#define TYPE_Z7II_TIMESTAMP "z7ii-timestamp-functional"
OBJECT_DECLARE_SIMPLE_TYPE(Z7IITimestampState, Z7II_TIMESTAMP)

struct Z7IITimestampState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    Clock *clock;
    uint64_t ticks_then;
    int64_t ns_then;
    uint32_t control;
    bool reported_error;
};

/* Same epoch/Clock conversion as upstream SSECounter, without its scaling IP. */
static uint64_t z7ii_timestamp_count(Z7IITimestampState *s)
{
    if (!(s->control & 1)) {
        return s->ticks_then;
    }
    return s->ticks_then + clock_ns_to_ticks(s->clock,
        qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) - s->ns_then);
}

static MemTxResult z7ii_timestamp_error(Z7IITimestampState *s, hwaddr offset,
                                       unsigned size, bool write,
                                       uint64_t value, const char *reason)
{
    z7ii_access_error(&s->reported_error, TIMESTAMP_BASE + offset,
                       size, write, value, reason);
    return MEMTX_ERROR;
}

static MemTxResult z7ii_timestamp_read(void *opaque, hwaddr offset,
                                      uint64_t *value, unsigned size,
                                      MemTxAttrs attrs)
{
    Z7IITimestampState *s = opaque;

    *value = 0;
    if (size != 4 || (offset & 3)) {
        return z7ii_timestamp_error(s, offset, size, false, 0,
                                    "timestamp-access-not-supported");
    }
    switch (offset) {
    case 0:
        *value = s->control;
        break;
    case 4:
        /* No external Halt-on-Debug signal, as in upstream SSECounter. */
        *value = 0;
        break;
    case 8:
        *value = (uint32_t)z7ii_timestamp_count(s);
        break;
    case 0xc:
        *value = z7ii_timestamp_count(s) >> 32;
        break;
    case 0x20:
        *value = TIMESTAMP_NOMINAL_HZ;
        break;
    default:
        return z7ii_timestamp_error(s, offset, size, false, 0,
                                    "timestamp-access-not-supported");
    }
    return MEMTX_OK;
}

static MemTxResult z7ii_timestamp_write(void *opaque, hwaddr offset,
                                       uint64_t value, unsigned size,
                                       MemTxAttrs attrs)
{
    Z7IITimestampState *s = opaque;

    if (size != 4 || (offset & 3)) {
        return z7ii_timestamp_error(s, offset, size, true, value,
                                    "timestamp-access-not-supported");
    }
    switch (offset) {
    case 0:
        if (value & ~3U) {
            return z7ii_timestamp_error(s, offset, size, true, value,
                                        "timestamp-control-not-supported");
        }
        s->ticks_then = z7ii_timestamp_count(s);
        s->ns_then = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        s->control = value;
        break;
    case 8:
    case 0xc:
        if (s->control & 1) {
            return z7ii_timestamp_error(s, offset, size, true, value,
                                        "timestamp-count-write-while-enabled");
        }
        if (offset == 8) {
            s->ticks_then = (s->ticks_then & 0xffffffff00000000ULL) | value;
        } else {
            s->ticks_then = (s->ticks_then & 0xffffffffULL) | (value << 32);
        }
        break;
    case 0x20:
        if ((s->control & 1) || value != TIMESTAMP_NOMINAL_HZ) {
            return z7ii_timestamp_error(s, offset, size, true, value,
                                        "timestamp-frequency-profile-not-supported");
        }
        break;
    default:
        return z7ii_timestamp_error(s, offset, size, true, value,
                                    "timestamp-access-not-supported");
    }
    return MEMTX_OK;
}

static const MemoryRegionOps z7ii_timestamp_ops = {
    .read_with_attrs = z7ii_timestamp_read,
    .write_with_attrs = z7ii_timestamp_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
};

static void z7ii_timestamp_reset(DeviceState *dev)
{
    Z7IITimestampState *s = Z7II_TIMESTAMP(dev);

    /* Explicit virtual-board assumptions; no real loader/clock claim. */
    s->control = 1;
    s->ticks_then = 0;
    s->ns_then = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    clock_update_hz(s->clock, TIMESTAMP_NOMINAL_HZ);
    s->reported_error = false;
}

static void z7ii_timestamp_init(Object *obj)
{
    Z7IITimestampState *s = Z7II_TIMESTAMP(obj);

    s->clock = qdev_init_clock_out(DEVICE(obj), "nominal-clock");
    memory_region_init_io(&s->regs, obj, &z7ii_timestamp_ops, s,
                          "z7ii.timestamp", 0x24);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
}

static const VMStateDescription z7ii_timestamp_vmstate = {
    .name = "z7ii-timestamp",
    .unmigratable = true,
};

static void z7ii_timestamp_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &z7ii_timestamp_vmstate;
    device_class_set_legacy_reset(dc, z7ii_timestamp_reset);
    dc->user_creatable = false;
}

static const TypeInfo z7ii_timestamp_type = {
    .name = TYPE_Z7II_TIMESTAMP,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Z7IITimestampState),
    .instance_init = z7ii_timestamp_init,
    .class_init = z7ii_timestamp_class_init,
};

static void z7ii_timestamp_register_types(void)
{
    type_register_static(&z7ii_timestamp_type);
}
type_init(z7ii_timestamp_register_types)
