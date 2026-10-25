/*
 * Capture service parameters and externally published numerical results.
 * Configuration retention alone produces no status, IRQ, timer or DMA.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "hw/core/sysbus.h"
#include "hw/nikon/devices.h"
#include "hw/nikon/statistics.h"

#define CAPTURE_BANK_SIZE 0x44
#define CAPTURE_BANK_WORDS (CAPTURE_BANK_SIZE / 4)
OBJECT_DECLARE_SIMPLE_TYPE(NikonCaptureBanksState, NIKON_CAPTURE_BANKS)

typedef struct NikonCaptureBank {
    NikonCaptureBanksState *owner;
    MemoryRegion memory;
    unsigned index;
} NikonCaptureBank;

struct NikonCaptureBanksState {
    SysBusDevice parent_obj;
    NikonCaptureBank bank[2];
    uint32_t configuration[2][CAPTURE_BANK_WORDS];
    bool reported_error;
    uint32_t results[2][12];
    uint32_t read_mask[2];
    unsigned results_valid;
};

void nikon_capture_banks_results(DeviceState *dev, unsigned banks,
                                 const uint32_t words[12])
{
    NikonCaptureBanksState *s = NIKON_CAPTURE_BANKS(dev);

    for (unsigned bank = 0; bank < 2; bank++) {
        if (banks & (1u << bank)) {
            memcpy(s->results[bank], words, sizeof(s->results[bank]));
            s->read_mask[bank] = 0;
        }
    }
    s->results_valid |= banks & 3;
}

static bool nikon_capture_bank_mask(hwaddr offset, uint32_t *mask)
{
    if (offset <= 0x1c) {
        *mask = 0xffff;
    } else if (offset == 0x20 || offset == 0x24) {
        *mask = 0xff;
    } else if (offset == 0x40) {
        /* 10209fea ORs an unmasked word with another word shifted by4. */
        *mask = UINT32_MAX;
    } else {
        return false;
    }
    return true;
}

static MemTxResult nikon_capture_bank_error(NikonCaptureBank *bank,
                                           hwaddr offset, unsigned size,
                                           bool write, uint64_t value)
{
    nikon_access_error(&bank->owner->reported_error, bank->memory.addr + offset,
                       size, write, value,
                       "capture-bank-configuration-not-supported");
    return MEMTX_ERROR;
}

static MemTxResult nikon_capture_bank_read(void *opaque, hwaddr offset,
                                          uint64_t *value, unsigned size,
                                          MemTxAttrs attrs)
{
    NikonCaptureBank *bank = opaque;
    uint32_t mask;

    *value = 0;
    if (size == 4 && !(offset & 3) && offset >= 0x100 &&
        offset <= 0x12c && (bank->owner->results_valid & (1u << bank->index))) {
        unsigned word = (offset - 0x100) / 4;

        *value = bank->owner->results[bank->index][word];
        bank->owner->read_mask[bank->index] |= 1u << word;
        return MEMTX_OK;
    }
    if (size != 4 || (offset & 3) ||
        !nikon_capture_bank_mask(offset, &mask)) {
        return nikon_capture_bank_error(bank, offset, size, false, 0);
    }
    *value = bank->owner->configuration[bank->index][offset / 4];
    return MEMTX_OK;
}

static MemTxResult nikon_capture_bank_write(void *opaque, hwaddr offset,
                                           uint64_t value, unsigned size,
                                           MemTxAttrs attrs)
{
    NikonCaptureBank *bank = opaque;
    uint32_t mask;

    if (size != 4 || (offset & 3) ||
        !nikon_capture_bank_mask(offset, &mask) || (value & ~mask)) {
        return nikon_capture_bank_error(bank, offset, size, true, value);
    }
    bank->owner->configuration[bank->index][offset / 4] = value;
    return MEMTX_OK;
}

static const MemoryRegionOps nikon_capture_bank_ops = {
    .read_with_attrs = nikon_capture_bank_read,
    .write_with_attrs = nikon_capture_bank_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4,
               .unaligned = false },
};

static void nikon_capture_banks_reset(DeviceState *dev)
{
    NikonCaptureBanksState *s = NIKON_CAPTURE_BANKS(dev);

    memset(s->configuration, 0, sizeof(s->configuration));
    s->reported_error = false;
    s->results_valid = false;
    memset(s->read_mask, 0, sizeof(s->read_mask));
    memset(s->results, 0, sizeof(s->results));
}

static void nikon_capture_banks_init(Object *obj)
{
    NikonCaptureBanksState *s = NIKON_CAPTURE_BANKS(obj);

    object_property_add_uint32_ptr(obj, "read-mask0", &s->read_mask[0],
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint32_ptr(obj, "read-mask1", &s->read_mask[1],
                                   OBJ_PROP_FLAG_READ);
    for (unsigned i = 0; i < ARRAY_SIZE(s->bank); i++) {
        NikonCaptureBank *bank = &s->bank[i];

        bank->owner = s;
        bank->index = i;
        memory_region_init_io(&bank->memory, obj, &nikon_capture_bank_ops, bank,
                              "nikon.capture-bank", 0x130);
        sysbus_init_mmio(SYS_BUS_DEVICE(s), &bank->memory);
    }
}

static const VMStateDescription nikon_capture_banks_vmstate = {
    .name = "nikon-capture-banks",
    .unmigratable = true,
};

static void nikon_capture_banks_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &nikon_capture_banks_vmstate;
    dc->user_creatable = false;
    dc->unrealize = nikon_capture_banks_reset;
    device_class_set_legacy_reset(dc, nikon_capture_banks_reset);
}

static const TypeInfo nikon_capture_banks_type = {
    .name = TYPE_NIKON_CAPTURE_BANKS,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonCaptureBanksState),
    .instance_init = nikon_capture_banks_init,
    .class_init = nikon_capture_banks_class_init,
};

static void nikon_capture_banks_register_types(void)
{
    type_register_static(&nikon_capture_banks_type);
}

type_init(nikon_capture_banks_register_types)
