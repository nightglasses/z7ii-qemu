/*
 * EX hardware random number generator.
 *
 * The original media record path builds a descriptor whose base is 0x50060800
 * (`sub_802e36`) and drives it through `sub_804c40`/`sub_804ccd2`/`sub_804c6c`:
 * the control word at offset 0 has bits 2 and 3 cleared, the status word at
 * offset 4 has bits 5 and 6 cleared, and later bit 2 of the control word is set
 * again.  Those offsets and bits match the STM32F4 RNG register layout
 * (CR.RNGEN bit2, CR.IE bit3, SR.DRDY bit0, SR.CEIS bit5, SR.SEIS bit6), so the
 * window is modelled as that finite register block:
 *
 *   0x00 control: bit2 enable, bit3 interrupt enable, rest writable
 *   0x04 status:  bit0 data ready, bits 5/6 write-zero-to-clear error flags
 *   0x08 data:    32 random bits, reading clears data ready
 *
 * The default input is a fresh draw from the host generator. The optional
 * -global nikon-ex-rng-functional.input-word=0x12345678 selects a fixed input
 * word for reproducible runs. All 32-bit values, including zero, are valid;
 * this fixed-word profile is not entropy and keeps the ready protocol intact.
 * Any other offset or width is an explicit error, so an
 * unmodelled access still stops the guest.  The block is a functional
 * register-level model, not a claim about the vendor silicon.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/sysbus.h"
#include "hw/nikon/devices.h"

#define EX_RNG_CR_RNGEN (1u << 2)
#define EX_RNG_CR_IE (1u << 3)
#define EX_RNG_SR_DRDY (1u << 0)
#define EX_RNG_SR_CEIS (1u << 5)
#define EX_RNG_SR_SEIS (1u << 6)
#define EX_RNG_SR_ERRORS (EX_RNG_SR_CEIS | EX_RNG_SR_SEIS)

OBJECT_DECLARE_SIMPLE_TYPE(NikonEXRngState, NIKON_EX_RNG)

struct NikonEXRngState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    uint32_t control;
    uint32_t status;
    char *input_word;
    uint32_t fixed_word;
    bool reported_error;
};

static MemTxResult nikon_exrng_error(NikonEXRngState *s, hwaddr offset,
                                     unsigned size, bool write, uint64_t value)
{
    nikon_access_error(&s->reported_error, s->regs.addr + offset, size,
                       write, value, "ex-rng-access-not-supported");
    return MEMTX_ERROR;
}

static bool nikon_exrng_ready(const NikonEXRngState *s)
{
    return (s->control & EX_RNG_CR_RNGEN) && (s->status & EX_RNG_SR_DRDY);
}

static MemTxResult nikon_exrng_read(void *opaque, hwaddr offset, uint64_t *value,
                                    unsigned size, MemTxAttrs attrs)
{
    NikonEXRngState *s = opaque;

    *value = 0;
    if (size != 4 || (offset & 3)) {
        return nikon_exrng_error(s, offset, size, false, 0);
    }
    switch (offset) {
    case 0x00:
        *value = s->control;
        return MEMTX_OK;
    case 0x04:
        *value = s->status;
        return MEMTX_OK;
    case 0x08: {
        uint32_t random;

        if (!nikon_exrng_ready(s)) {
            /* No data available: the data register reads back as zero and
             * the ready bit stays clear; no synthetic value is produced. */
            return MEMTX_OK;
        }
        random = s->input_word ? s->fixed_word : g_random_int();
        *value = random;
        s->status &= ~EX_RNG_SR_DRDY;
        return MEMTX_OK;
    }
    default:
        return nikon_exrng_error(s, offset, size, false, 0);
    }
}

static MemTxResult nikon_exrng_write(void *opaque, hwaddr offset,
                                     uint64_t value, unsigned size,
                                     MemTxAttrs attrs)
{
    NikonEXRngState *s = opaque;

    if (size != 4 || (offset & 3) || value > UINT32_MAX) {
        return nikon_exrng_error(s, offset, size, true, value);
    }
    switch (offset) {
    case 0x00:
        s->control = value & (EX_RNG_CR_RNGEN | EX_RNG_CR_IE);
        /* Enabling the block makes one word available for reading. */
        if (s->control & EX_RNG_CR_RNGEN) {
            s->status |= EX_RNG_SR_DRDY;
        } else {
            s->status &= ~EX_RNG_SR_DRDY;
        }
        return MEMTX_OK;
    case 0x04:
        /* Bits 5 and 6 are cleared by writing zero to them. */
        s->status &= (value & EX_RNG_SR_ERRORS) | ~EX_RNG_SR_ERRORS;
        return MEMTX_OK;
    case 0x08:
        /* The data register is read-only on the original block. */
        return MEMTX_OK;
    default:
        return nikon_exrng_error(s, offset, size, true, value);
    }
}

static const MemoryRegionOps nikon_exrng_ops = {
    .read_with_attrs = nikon_exrng_read,
    .write_with_attrs = nikon_exrng_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 4, .unaligned = true },
};

static void nikon_exrng_reset(DeviceState *dev)
{
    NikonEXRngState *s = NIKON_EX_RNG(dev);

    s->control = 0;
    s->status = 0;
    s->reported_error = false;
}

static void nikon_exrng_init(Object *obj)
{
    NikonEXRngState *s = NIKON_EX_RNG(obj);

    memory_region_init_io(&s->regs, obj, &nikon_exrng_ops, s, "nikon.exrng",
                          0x400);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
}

static void nikon_exrng_realize(DeviceState *dev, Error **errp)
{
    NikonEXRngState *s = NIKON_EX_RNG(dev);
    uint64_t value;

    if (s->input_word) {
        if (parse_uint(s->input_word, NULL, 0, &value) || value > UINT32_MAX) {
            error_setg(errp,
                       "EX RNG input-word must be an unsigned 32-bit integer");
            return;
        }
        s->fixed_word = value;
    }
}

static const Property nikon_exrng_properties[] = {
    DEFINE_PROP_STRING("input-word", NikonEXRngState, input_word),
};

static const VMStateDescription nikon_exrng_vmstate = {
    .name = "nikon-ex-rng",
    .unmigratable = true,
};

static void nikon_exrng_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = nikon_exrng_realize;
    device_class_set_props(dc, nikon_exrng_properties);
    dc->vmsd = &nikon_exrng_vmstate;
    dc->user_creatable = false;
    device_class_set_legacy_reset(dc, nikon_exrng_reset);
}

static const TypeInfo nikon_exrng_type = {
    .name = TYPE_NIKON_EX_RNG,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonEXRngState),
    .instance_init = nikon_exrng_init,
    .class_init = nikon_exrng_class_init,
};

static void nikon_exrng_register_types(void)
{
    type_register_static(&nikon_exrng_type);
}

type_init(nikon_exrng_register_types)
