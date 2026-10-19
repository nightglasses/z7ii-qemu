/*
 * EX channel engine register block.
 *
 * The original EX software reaches one window that the machine did not map:
 * `sub_8023E04` loads `0x50060800` into a stack descriptor and submits it to
 * `sub_8044C40`/`sub_8044CD2`, which read and rewrite a control word at
 * offset 0 (clearing bits 2 and 3, then setting bit 2) and a second word at
 * offset 4 (clearing bits 5 and 6).  `sub_8044D16`/`sub_8044D20` index a
 * 32-bit table at offset 0x50 with a limit of twenty entries and log 14005 or
 * 14006 beyond it.
 *
 * This device therefore models exactly that shape: two words and twenty
 * channel entries, all writable and readable, with no interrupt, no completion
 * and no fabricated status.  Every other offset and every other width is an
 * explicit error, so an unmodelled access still stops the guest instead of
 * silently reading zero.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "hw/core/sysbus.h"
#include "hw/nikon/devices.h"

#define EXENGINE_CHANNELS 20

OBJECT_DECLARE_SIMPLE_TYPE(NikonEXEngineState, NIKON_EX_ENGINE)

struct NikonEXEngineState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    uint32_t control;
    uint32_t status;
    uint32_t channel[EXENGINE_CHANNELS];
    bool reported_error;
};

static MemTxResult nikon_exengine_error(NikonEXEngineState *s, hwaddr offset,
                                        unsigned size, bool write,
                                        uint64_t value)
{
    nikon_access_error(&s->reported_error, s->regs.addr + offset, size,
                       write, value, "ex-engine-access-not-supported");
    return MEMTX_ERROR;
}

static MemTxResult nikon_exengine_read(void *opaque, hwaddr offset,
                                       uint64_t *value, unsigned size,
                                       MemTxAttrs attrs)
{
    NikonEXEngineState *s = opaque;
    unsigned channel = (offset - 0x50) / 4;

    *value = 0;
    if (size != 4 || (offset & 3)) {
        return nikon_exengine_error(s, offset, size, false, 0);
    }
    if (offset == 0x00) {
        *value = s->control;
        return MEMTX_OK;
    }
    if (offset == 0x04) {
        *value = s->status;
        return MEMTX_OK;
    }
    if (offset >= 0x50 && channel < EXENGINE_CHANNELS) {
        *value = s->channel[channel];
        return MEMTX_OK;
    }
    return nikon_exengine_error(s, offset, size, false, 0);
}

static MemTxResult nikon_exengine_write(void *opaque, hwaddr offset,
                                        uint64_t value, unsigned size,
                                        MemTxAttrs attrs)
{
    NikonEXEngineState *s = opaque;
    unsigned channel = (offset - 0x50) / 4;

    if (size != 4 || (offset & 3) || value > UINT32_MAX) {
        return nikon_exengine_error(s, offset, size, true, value);
    }
    if (offset == 0x00) {
        s->control = value;
        return MEMTX_OK;
    }
    if (offset == 0x04) {
        s->status = value;
        return MEMTX_OK;
    }
    if (offset >= 0x50 && channel < EXENGINE_CHANNELS) {
        s->channel[channel] = value;
        return MEMTX_OK;
    }
    return nikon_exengine_error(s, offset, size, true, value);
}

static const MemoryRegionOps nikon_exengine_ops = {
    .read_with_attrs = nikon_exengine_read,
    .write_with_attrs = nikon_exengine_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
};

static void nikon_exengine_reset(DeviceState *dev)
{
    NikonEXEngineState *s = NIKON_EX_ENGINE(dev);

    s->control = 0;
    s->status = 0;
    memset(s->channel, 0, sizeof(s->channel));
    s->reported_error = false;
}

static void nikon_exengine_init(Object *obj)
{
    NikonEXEngineState *s = NIKON_EX_ENGINE(obj);

    memory_region_init_io(&s->regs, obj, &nikon_exengine_ops, s,
                          "nikon.exengine", 0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
}

static const VMStateDescription nikon_exengine_vmstate = {
    .name = "nikon-ex-engine",
    .unmigratable = true,
};

static void nikon_exengine_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &nikon_exengine_vmstate;
    dc->user_creatable = false;
    device_class_set_legacy_reset(dc, nikon_exengine_reset);
}

static const TypeInfo nikon_exengine_type = {
    .name = TYPE_NIKON_EX_ENGINE,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonEXEngineState),
    .instance_init = nikon_exengine_init,
    .class_init = nikon_exengine_class_init,
};

static void nikon_exengine_register_types(void)
{
    type_register_static(&nikon_exengine_type);
}

type_init(nikon_exengine_register_types)
