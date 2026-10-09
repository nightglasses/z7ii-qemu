/*
 * Nikon source-bound processing frontend and explicit produced-byte endpoint.
 * The external processor is disconnected by default. No encoding or conversion
 * is inferred from the indexed input addresses. Zero reset and status timing
 * are functional conventions; the original register ordering is preserved.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/timer.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "migration/vmstate.h"
#include "system/address-spaces.h"
#include "system/block-backend.h"
#include "system/dma.h"
#include "hw/nikon/devices.h"
#include "hw/nikon/processing.h"

OBJECT_DECLARE_SIMPLE_TYPE(NikonProcessingState, NIKON_PROCESSING)

#define PROCESSING_SIZE 0x1000
#define PROCESSING_WORDS (PROCESSING_SIZE / 4)
#define PROCESSING_INPUTS 128

struct NikonProcessingState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    qemu_irq irq;
    BlockBackend *result;
    QEMUTimer *timer;
    uint32_t result_slot;
    uint64_t generation, produced, target, extent;
    uint32_t config[PROCESSING_WORDS];
    uint32_t inputs[PROCESSING_INPUTS];
    uint32_t packet[PROCESSING_WORDS];
    uint32_t pending;
    bool active, reading, reported_error;
};

/* Finite original 104b949c / 104b9c56 fields. The retained words do not claim
 * a physical writable mask or a decoded codec parameter vocabulary. */
static const uint16_t config_offsets[] = {
    0x404, 0x408, 0x440, 0x464,
    0x470, 0x474, 0x478, 0x47c, 0x480,
    0x490, 0x494, 0x498, 0x49c, 0x4a0, 0x4a4,
    0x4b0, 0x4b4, 0x4c4, 0x4c8, 0x4f0,
    0xa80, 0xa84, 0xa88, 0xa8c, 0xb78,
    0xc24, 0xc80, 0xc84, 0xc88,
    0xd40, 0xd44, 0xd48, 0xd4c, 0xd50, 0xd54, 0xd58, 0xd5c,
    0xd60, 0xd64, 0xd68, 0xd6c, 0xd70, 0xd74, 0xd78, 0xd7c,
    0xd80, 0xd84, 0xdc8, 0xdcc, 0xdec, 0xdf0,
    0xe00, 0xe04, 0xe0c, 0xe10, 0xe14, 0xe18, 0xe1c, 0xe20,
    0xe30, 0xe34, 0xe38, 0xe3c, 0xe40, 0xe44, 0xe48, 0xe4c,
    0xe50, 0xe54, 0xe58, 0xe5c, 0xe60, 0xe64, 0xe68, 0xe6c,
    0xe70, 0xe74, 0xe78, 0xe7c, 0xe80, 0xe84, 0xe88, 0xe8c,
    0xe90, 0xe94, 0xe98, 0xea0, 0xea8, 0xeac, 0xeb0, 0xeb4,
    0xeb8, 0xebc, 0xec0, 0xec4, 0xec8, 0xecc, 0xed0, 0xed4,
    0xed8, 0xedc, 0xee0, 0xee4, 0xee8, 0xeec, 0xef0, 0xef4,
    0xef8, 0xefc,
};

static bool nikon_processing_config(hwaddr offset)
{
    unsigned i;

    for (i = 0; i < ARRAY_SIZE(config_offsets); i++) {
        if (config_offsets[i] == offset) {
            return true;
        }
    }
    return false;
}

static void nikon_processing_irq(NikonProcessingState *s)
{
    qemu_set_irq(s->irq, !!(s->pending & s->config[0xc24 / 4]));
}

static MemTxResult nikon_processing_error(NikonProcessingState *s,
                                         hwaddr offset, unsigned size,
                                         bool write, uint64_t value,
                                         const char *reason)
{
    nikon_access_error(&s->reported_error, s->regs.addr + offset, size,
                       write, value, reason);
    return MEMTX_ERROR;
}

static void nikon_processing_clear(NikonProcessingState *s)
{
    timer_del(s->timer);
    s->generation++;
    memset(s->config, 0, sizeof(s->config));
    memset(s->inputs, 0, sizeof(s->inputs));
    memset(s->packet, 0, sizeof(s->packet));
    s->pending = 0;
    s->active = false;
    s->reading = false;
    s->produced = 0;
    nikon_processing_irq(s);
}

static bool nikon_processing_writable(hwaddr address, uint64_t length)
{
    if (!address || length > UINT32_MAX || address > UINT32_MAX - length) {
        return false;
    }
    while (length) {
        MemoryRegionSection section = memory_region_find(get_system_memory(),
                                                        address, length);
        uint64_t span;
        bool writable;

        if (!section.mr) {
            return false;
        }
        span = int128_get64(section.size);
        writable = span && span <= length &&
                   section.offset_within_address_space == address &&
                   memory_region_is_ram(section.mr) &&
                   memory_access_is_direct(section.mr, true,
                                            MEMTXATTRS_UNSPECIFIED) &&
                   !section.readonly;
        memory_region_unref(section.mr);
        if (!writable) {
            return false;
        }
        address += span;
        length -= span;
    }
    return true;
}

static bool nikon_processing_output(unsigned slot, const uint32_t *config,
                                    size_t length,
                                    hwaddr *address, Error **errp)
{
    uint32_t capacity;

    if (slot >= 4) {
        error_setg(errp, "Processing result slot must be in 0..3");
        return false;
    }
    *address = config[(0x470 + slot * 4) / 4];
    capacity = config[(slot & 1 ? 0x49c : 0x494) / 4];
    if (!length || length != capacity || (*address & 1) ||
        !nikon_processing_writable(*address, length)) {
        error_setg(errp, "Processing result requires a complete capacity-sized "
                   "buffer in configured writable RAM");
        return false;
    }
    return true;
}

static void nikon_processing_complete(NikonProcessingState *s, unsigned slot)
{
    /* Native105a7932 maps raw9/raw10 to normalized22/21;105a7f0e emits
     * channel0/channel1 event objects with the retained capacity/address pair.
     * Short final results require different metadata and are not fabricated.
     */
    s->pending |= 1U << (9 + (slot & 1));
    s->reading = false;
    s->active = false;
    nikon_processing_irq(s);
}

bool nikon_processing_publish(DeviceState *dev, unsigned slot,
                              const uint8_t *data, size_t length, Error **errp)
{
    NikonProcessingState *s = NIKON_PROCESSING(dev);
    MemTxAttrs attrs = MEMTXATTRS_UNSPECIFIED;
    hwaddr address;

    if (!s->active || s->reading || !data) {
        error_setg(errp, "Processing result has no active external request");
        return false;
    }
    if (!nikon_processing_output(slot, s->packet, length, &address, errp)) {
        return false;
    }
    attrs.memory = true;
    if (dma_memory_write(&address_space_memory, address, data, length, attrs) !=
        MEMTX_OK) {
        error_setg(errp, "Processing result DMA failed");
        return false;
    }
    s->produced = length;
    nikon_processing_complete(s, slot);
    return true;
}

static void nikon_processing_schedule(NikonProcessingState *s)
{
    uint64_t count = MIN(s->extent - s->produced, 4096);

    /* The 48MHz byte service rate is a virtual timing profile. */
    timer_mod(s->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
              count * 1000000000ULL / 48000000 + 1);
}

static void nikon_processing_result(void *opaque)
{
    NikonProcessingState *s = opaque;
    uint8_t bytes[4096];
    unsigned count = MIN(s->extent - s->produced, sizeof(bytes));
    uint64_t generation = s->generation;
    MemTxAttrs attrs = MEMTXATTRS_UNSPECIFIED;
    int64_t length;
    int ret;

    if (!s->active || !s->reading) {
        return;
    }
    length = blk_is_available(s->result) ? blk_getlength(s->result) : -1;
    if (s->generation != generation || !s->reading) {
        return;
    }
    if (length != (int64_t)s->extent) {
        goto failed;
    }
    ret = blk_pread(s->result, s->produced, count, bytes, 0);
    if (s->generation != generation || !s->reading) {
        return;
    }
    if (ret < 0 || !nikon_processing_writable(s->target + s->produced, count)) {
        goto failed;
    }
    length = blk_getlength(s->result);
    if (s->generation != generation || !s->reading) {
        return;
    }
    if (length != (int64_t)s->extent) {
        goto failed;
    }
    attrs.memory = true;
    if (dma_memory_write(&address_space_memory, s->target + s->produced,
                         bytes, count, attrs) != MEMTX_OK) {
        goto failed;
    }
    s->produced += count;
    if (s->produced == s->extent) {
        nikon_processing_complete(s, s->result_slot);
    } else {
        nikon_processing_schedule(s);
    }
    return;

failed:
    s->reading = false;
    nikon_processing_error(s, 0x454, 4, true, 1,
                           "processing-external-result-unavailable-or-dma-failed");
}

static void nikon_processing_start(NikonProcessingState *s)
{
    Error *local_err = NULL;
    hwaddr address;

    timer_del(s->timer);
    s->generation++;
    memcpy(s->packet, s->config, sizeof(s->packet));
    s->active = true;
    s->reading = false;
    s->produced = 0;
    if (!s->result) {
        return;
    }
    s->extent = s->packet[(s->result_slot & 1 ? 0x49c : 0x494) / 4];
    if (!nikon_processing_output(s->result_slot, s->packet, s->extent,
                                 &address, &local_err)) {
        nikon_processing_error(s, 0x454, 4, true, 1,
                               "processing-external-result-output-unbound");
        error_free(local_err);
        return;
    }
    s->target = address;
    s->reading = true;
    nikon_processing_schedule(s);
}

static MemTxResult nikon_processing_read(void *opaque, hwaddr offset,
                                        uint64_t *value, unsigned size,
                                        MemTxAttrs attrs)
{
    NikonProcessingState *s = opaque;

    *value = 0;
    if (size != 4 || (offset & 3)) {
        return nikon_processing_error(s, offset, size, false, 0,
                                      "processing-access-width");
    }
    if (nikon_processing_config(offset) || offset == 0x438 ||
        offset == 0x454 || offset == 0xc00 || offset == 0xc30) {
        *value = s->config[offset / 4];
    } else if (offset == 0x43c) {
        *value = s->inputs[s->config[0x438 / 4]];
    } else if (offset == 0xc28) {
        *value = s->pending;
    } else if (offset == 0xfc0) {
        /* No result index is fabricated. Bit7 denotes an outstanding request. */
        *value = s->active ? 0x80 : 0;
    } else {
        return nikon_processing_error(s, offset, size, false, 0,
                                      "processing-result-or-register-unbound");
    }
    return MEMTX_OK;
}

static MemTxResult nikon_processing_write(void *opaque, hwaddr offset,
                                         uint64_t value, unsigned size,
                                         MemTxAttrs attrs)
{
    NikonProcessingState *s = opaque;

    if (size != 4 || (offset & 3)) {
        return nikon_processing_error(s, offset, size, true, value,
                                      "processing-access-width");
    }
    if (offset == 0x438) {
        if (value >= PROCESSING_INPUTS) {
            return nikon_processing_error(s, offset, size, true, value,
                                          "processing-input-selector-unbound");
        }
        s->config[offset / 4] = value;
    } else if (offset == 0x43c) {
        s->inputs[s->config[0x438 / 4]] = value;
    } else if (offset == 0x454 && (value == 1 || value == 2)) {
        s->config[offset / 4] = value;
        if (value == 1) {
            nikon_processing_start(s);
        } else {
            timer_del(s->timer);
            s->generation++;
            s->active = false;
            s->reading = false;
        }
    } else if (offset == 0xc00 && value == 0x800) {
        nikon_processing_clear(s);
    } else if (offset == 0xc00 && value == 0x80) {
        /* Functional flush cancels this explicit result delivery. It cannot
         * manufacture the original short-result metadata read afterward. */
        s->config[offset / 4] = value;
        timer_del(s->timer);
        s->generation++;
        s->active = false;
        s->reading = false;
    } else if (offset == 0xc30) {
        s->pending &= ~value;
        s->config[offset / 4] = value;
        nikon_processing_irq(s);
    } else if (offset == 0x4fc && value == 0) {
        /* Original initialization clears this separate auxiliary result word. */
        s->config[offset / 4] = 0;
    } else if (nikon_processing_config(offset)) {
        if (offset == 0x464 && (value & ~0x11)) {
            return nikon_processing_error(s, offset, size, true, value,
                                          "processing-indexed-control-unbound");
        }
        s->config[offset / 4] = value;
        if (offset == 0xc24) {
            nikon_processing_irq(s);
        }
    } else {
        return nikon_processing_error(s, offset, size, true, value,
                                      "processing-register-or-command-unbound");
    }
    return MEMTX_OK;
}

static const MemoryRegionOps nikon_processing_ops = {
    .read_with_attrs = nikon_processing_read,
    .write_with_attrs = nikon_processing_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl = { .min_access_size = 1, .max_access_size = 4 },
};

static void nikon_processing_reset(DeviceState *dev)
{
    NikonProcessingState *s = NIKON_PROCESSING(dev);

    s->reported_error = false;
    nikon_processing_clear(s);
}

static void nikon_processing_init(Object *obj)
{
    NikonProcessingState *s = NIKON_PROCESSING(obj);

    memory_region_init_io(&s->regs, obj, &nikon_processing_ops, s,
                          "nikon-processing", PROCESSING_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, nikon_processing_result, s);
}

static void nikon_processing_finalize(Object *obj)
{
    NikonProcessingState *s = NIKON_PROCESSING(obj);

    timer_free(s->timer);
}

static void nikon_processing_realize(DeviceState *dev, Error **errp)
{
    NikonProcessingState *s = NIKON_PROCESSING(dev);

    if (s->result_slot >= 4) {
        error_setg(errp, "Processing result-slot must be in 0..3");
        return;
    }
    if (s->result && blk_set_perm(s->result, BLK_PERM_CONSISTENT_READ,
                                  BLK_PERM_ALL, errp) < 0) {
        return;
    }
}

static const Property nikon_processing_properties[] = {
    DEFINE_PROP_DRIVE("result", NikonProcessingState, result),
    DEFINE_PROP_UINT32("result-slot", NikonProcessingState, result_slot, 0),
};

static const VMStateDescription nikon_processing_vmstate = {
    .name = TYPE_NIKON_PROCESSING,
    .unmigratable = 1,
};

static void nikon_processing_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, nikon_processing_reset);
    dc->realize = nikon_processing_realize;
    device_class_set_props(dc, nikon_processing_properties);
    dc->vmsd = &nikon_processing_vmstate;
}

static const TypeInfo nikon_processing_type = {
    .name = TYPE_NIKON_PROCESSING,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonProcessingState),
    .instance_init = nikon_processing_init,
    .instance_finalize = nikon_processing_finalize,
    .class_init = nikon_processing_class_init,
};

static void nikon_processing_register_types(void)
{
    type_register_static(&nikon_processing_type);
}
type_init(nikon_processing_register_types)
