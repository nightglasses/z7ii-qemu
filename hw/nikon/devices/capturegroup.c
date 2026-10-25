/*
 * Finite capture-group parameters and opaque address retention.
 * Automatic TG edges drive an explicit functional address-cursor profile.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "hw/core/sysbus.h"
#include "hw/nikon/devices.h"

#define CAPTURE_GROUP_SIZE 0x584
#define CAPTURE_GROUP_WORDS (CAPTURE_GROUP_SIZE / 4)
OBJECT_DECLARE_SIMPLE_TYPE(NikonCaptureGroupState, NIKON_CAPTURE_GROUP)

static const hwaddr result_offsets[] = {
    0x370, 0x374, 0x378, 0x478, 0x578, 0x37c, 0x47c, 0x57c,
};

/* Bank, initial unit, count-minus-one, three strides, two three-address sets. */
static const hwaddr cursor_offsets[] = {
    0x33c, 0x344, 0x348, 0x380, 0x480, 0x580,
    0x350, 0x450, 0x550, 0x360, 0x460, 0x560,
};

struct NikonCaptureGroupState {
    SysBusDevice parent_obj;
    MemoryRegion memory;
    uint32_t configuration[CAPTURE_GROUP_WORDS];
    enum {
        GROUP_RESULT_COLD,
        GROUP_RESULT_UNKNOWN,
        GROUP_RESULT_PUBLISHED,
    } result_state;
    bool commit_selected[2];
    uint32_t active[ARRAY_SIZE(cursor_offsets)];
    uint32_t results[ARRAY_SIZE(result_offsets)];
    uint16_t cursor_written;
    unsigned cursor;
    bool active_valid;
    bool running;
    bool started;
    bool reported_error;
};

static int nikon_capture_group_result(hwaddr offset)
{
    for (unsigned i = 0; i < ARRAY_SIZE(result_offsets); i++) {
        if (offset == result_offsets[i]) {
            return i;
        }
    }
    return -1;
}

static bool nikon_capture_group_cursor_valid(NikonCaptureGroupState *s)
{
    const uint32_t *a = s->active;

    if (s->cursor_written != (1u << ARRAY_SIZE(cursor_offsets)) - 1 ||
        a[0] > 1 || a[1] > a[2]) {
        return false;
    }
    for (unsigned i = 0; i < 6; i++) {
        uint32_t stride = a[3 + i % 3];

        if (!stride || (uint64_t)a[6 + i] + (uint64_t)a[2] * stride >
                       UINT32_MAX) {
            return false;
        }
    }
    return true;
}

static void nikon_capture_group_automatic_rise(void *opaque, int index, int level)
{
    NikonCaptureGroupState *s = opaque;

    if (!level || !s->running || !s->active_valid) {
        return;
    }
    /* Functional phase: first edge selects start, later edges wrap by count. */
    s->cursor = s->started ? (s->cursor + 1) % (s->active[2] + 1) :
                            s->active[1];
    s->started = true;
    s->results[0] = s->active[0];
    s->results[1] = s->cursor;
    for (unsigned i = 0; i < 6; i++) {
        s->results[2 + i] = s->active[6 + i] +
                            s->cursor * s->active[3 + i % 3];
    }
    s->result_state = GROUP_RESULT_PUBLISHED;
}

static void nikon_capture_group_commit_selected(void *opaque, int index,
                                               int level)
{
    NikonCaptureGroupState *s = opaque;

    s->commit_selected[index] = !!level;
}

static void nikon_capture_group_commit(void *opaque, int index, int level)
{
    NikonCaptureGroupState *s = opaque;

    if (level && s->commit_selected[index]) {
        /* Commands change the generator, not the last published packet.
         * 1023aed8 ->10239dc6(force0) reads it before the next shared commit.
         * Latch retention is a functional hypothesis, not a fresh result.
         */
        if (s->result_state == GROUP_RESULT_COLD) {
            s->result_state = GROUP_RESULT_UNKNOWN;
        }
        s->started = false;
        for (unsigned i = 0; i < ARRAY_SIZE(cursor_offsets); i++) {
            s->active[i] = s->configuration[cursor_offsets[i] / 4];
        }
        s->active_valid = nikon_capture_group_cursor_valid(s);
    }
}

static void nikon_capture_group_run(void *opaque, int index, int level)
{
    NikonCaptureGroupState *s = opaque;

    if (s->running != !!level) {
        s->running = !!level;
        s->started = false;
        /* Run transitions cannot create results or restore cold-empty. */
        if (s->result_state == GROUP_RESULT_COLD) {
            s->result_state = GROUP_RESULT_UNKNOWN;
        }
    }
}

static bool nikon_capture_group_mask(hwaddr offset, uint32_t *mask)
{
    if (offset <= 0x1c) {
        *mask = offset <= 4 || offset == 0x1c ? 0xff : 0xffff;
    } else if (offset >= 0x20 && offset <= 0x7c) {
        /* Three CB0 rows: a complete byte followed by seven masked bytes. */
        *mask = (offset - 0x20) % 0x20 == 0 ? 0xff : 0x7f;
    } else if ((offset >= 0x80 && offset <= 0x180) ||
               (offset >= 0x200 && offset <= 0x20c) ||
               (offset >= 0x234 && offset <= 0x250) ||
               (offset >= 0x2a4 && offset <= 0x2ac)) {
        *mask = 0xffff;
    } else if ((offset >= 0x214 && offset <= 0x228) ||
               (offset >= 0x2b0 && offset <= 0x2b8)) {
        *mask = 0xff;
    } else if (offset >= 0x350 && offset <= 0x580 &&
               ((offset & 0xff) == 0x50 || (offset & 0xff) == 0x60 ||
                (offset & 0xff) == 0x80)) {
        /* Three original provider slots, two address sets and one extra word. */
        *mask = UINT32_MAX;
    } else {
        switch (offset) {
        case 0x184: case 0x210: case 0x22c: case 0x2a0: case 0x33c:
            *mask = UINT32_MAX;
            break;
        case 0x230:
            /* Only the source-provided count0/1 packet footprint is known. */
            *mask = 1;
            break;
        case 0x294:
            *mask = 0xffff;
            break;
        case 0x298: case 0x29c:
            *mask = 0x7fffff;
            break;
        case 0x318: case 0x324: case 0x344: case 0x348: case 0x34c:
            *mask = 0xff;
            break;
        default:
            return false;
        }
    }
    return true;
}

static MemTxResult nikon_capture_group_error(NikonCaptureGroupState *s,
                                            hwaddr offset, unsigned size,
                                            bool write, uint64_t value)
{
    nikon_access_error(&s->reported_error, s->memory.addr + offset,
                       size, write, value,
                       "capture-group-configuration-not-supported");
    return MEMTX_ERROR;
}

static MemTxResult nikon_capture_group_read(void *opaque, hwaddr offset,
                                           uint64_t *value, unsigned size,
                                           MemTxAttrs attrs)
{
    NikonCaptureGroupState *s = opaque;
    uint32_t mask;
    int result;

    *value = 0;
    if (size != 4 || (offset & 3)) {
        return nikon_capture_group_error(s, offset, size, false, 0);
    }
    result = nikon_capture_group_result(offset);
    if (result >= 0) {
        if (s->result_state == GROUP_RESULT_UNKNOWN) {
            return nikon_capture_group_error(s, offset, size, false, 0);
        }
        *value = s->results[result];
        return MEMTX_OK;
    }
    if (!nikon_capture_group_mask(offset, &mask)) {
        return nikon_capture_group_error(s, offset, size, false, 0);
    }
    *value = s->configuration[offset / 4];
    return MEMTX_OK;
}

static MemTxResult nikon_capture_group_write(void *opaque, hwaddr offset,
                                            uint64_t value, unsigned size,
                                            MemTxAttrs attrs)
{
    NikonCaptureGroupState *s = opaque;
    uint32_t mask;

    if (size != 4 || (offset & 3) ||
        !nikon_capture_group_mask(offset, &mask) || (value & ~mask)) {
        return nikon_capture_group_error(s, offset, size, true, value);
    }
    s->configuration[offset / 4] = value;
    for (unsigned i = 0; i < ARRAY_SIZE(cursor_offsets); i++) {
        if (offset == cursor_offsets[i]) {
            s->cursor_written |= 1u << i;
            break;
        }
    }
    return MEMTX_OK;
}

static const MemoryRegionOps nikon_capture_group_ops = {
    .read_with_attrs = nikon_capture_group_read,
    .write_with_attrs = nikon_capture_group_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4,
               .unaligned = false },
};

static void nikon_capture_group_reset(DeviceState *dev)
{
    NikonCaptureGroupState *s = NIKON_CAPTURE_GROUP(dev);

    memset(s->configuration, 0, sizeof(s->configuration));
    memset(s->commit_selected, 0, sizeof(s->commit_selected));
    memset(s->active, 0, sizeof(s->active));
    memset(s->results, 0, sizeof(s->results));
    s->cursor_written = 0;
    s->cursor = 0;
    s->active_valid = false;
    s->running = false;
    s->started = false;
    s->result_state = GROUP_RESULT_COLD;
    s->reported_error = false;
}

static void nikon_capture_group_init(Object *obj)
{
    NikonCaptureGroupState *s = NIKON_CAPTURE_GROUP(obj);

    memory_region_init_io(&s->memory, obj, &nikon_capture_group_ops, s,
                          "nikon.capture-group", CAPTURE_GROUP_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->memory);
    qdev_init_gpio_in_named(DEVICE(s), nikon_capture_group_commit_selected,
                           "commit-selected", 2);
    qdev_init_gpio_in_named(DEVICE(s), nikon_capture_group_commit, "commit", 2);
    qdev_init_gpio_in_named(DEVICE(s), nikon_capture_group_run, "run", 1);
    qdev_init_gpio_in_named(DEVICE(s), nikon_capture_group_automatic_rise,
                           "automatic-rise", 1);
}

static const VMStateDescription nikon_capture_group_vmstate = {
    .name = "nikon-capture-group",
    .unmigratable = true,
};

static void nikon_capture_group_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &nikon_capture_group_vmstate;
    dc->user_creatable = false;
    dc->unrealize = nikon_capture_group_reset;
    device_class_set_legacy_reset(dc, nikon_capture_group_reset);
}

static const TypeInfo nikon_capture_group_type = {
    .name = TYPE_NIKON_CAPTURE_GROUP,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonCaptureGroupState),
    .instance_init = nikon_capture_group_init,
    .class_init = nikon_capture_group_class_init,
};

static void nikon_capture_group_register_types(void)
{
    type_register_static(&nikon_capture_group_type);
}

type_init(nikon_capture_group_register_types)
