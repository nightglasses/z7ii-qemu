/*
 * RX functional clock/protection and RTC initialization devices.
 * Register meanings follow Renesas SDK; virtual settling is a model assumption.
 * No peripheral transfer, interrupt or MCU part number is inferred here.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "hw/nikon/renesas.h"
#include "hw/nikon/devices.h"
#include "hw/core/qdev-properties.h"
#include "system/runstate.h"

typedef struct NikonRXRegister {
    uint16_t address;
    uint8_t width, protect;
    uint32_t reset;
} NikonRXRegister;

/* Relative to SYSTEM 80000. Only actual named registers are present. */
static const NikonRXRegister system_registers[] = {
    { 0x08, 2, 2, 1 },                    /* SYSCR1: RAM enabled */
    { 0x0c, 2, 2, 0 },                    /* SBYCR */
    { 0x10, 4, 2, UINT32_MAX },            /* MSTPCRA */
    { 0x14, 4, 2, UINT32_MAX },            /* MSTPCRB */
    { 0x18, 4, 2, UINT32_MAX },            /* MSTPCRC */
    { 0x1c, 4, 2, UINT32_MAX },            /* MSTPCRD */
    { 0x20, 4, 1, 0 },                    /* SCKCR */
    { 0x24, 2, 1, 0 },                    /* SCKCR2 */
    { 0x26, 2, 1, 0 },                    /* SCKCR3 */
    { 0x28, 2, 1, 0 },                    /* PLLCR */
    { 0x2a, 1, 1, 1 },                    /* PLLCR2: stopped */
    { 0x30, 1, 1, 0 },                    /* BCKCR */
    { 0x32, 1, 1, 1 },                    /* MOSCCR: stopped */
    { 0x33, 1, 1, 1 },                    /* SOSCCR: stopped */
    { 0x34, 1, 1, 0 },                    /* LOCOCR */
    { 0x35, 1, 1, 0 },                    /* ILOCOCR */
    { 0x36, 1, 1, 1 },                    /* HOCOCR: stopped */
    { 0x37, 1, 1, 0 },                    /* HOCOCR2 */
    { 0x3c, 1, 0, 0 },                    /* OSCOVFSR: derived, read only */
    { 0x40, 1, 1, 0 },                    /* OSTDCR */
    { 0x41, 1, 1, 0 },                    /* OSTDSR */
    { 0xa0, 1, 2, 0 },                    /* OPCCR */
    { 0xa1, 1, 2, 0 },                    /* RSTCKCR */
    { 0xa2, 1, 2, 0 },                    /* MOSCWTCR */
    { 0xa3, 1, 2, 0 },                    /* SOSCWTCR */
    { 0xc2, 2, 2, 0 },                    /* SWRR: A501 software reset key */
    { 0x3fe, 2, 0, 0 },                   /* PRCR: A5 key, PRC[3,1,0] */
    { 0x101c, 1, 1, 0 },                  /* MEMWAIT: software-selected wait */
};

typedef struct NikonRXSystem {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    uint32_t values[ARRAY_SIZE(system_registers)];
    QEMUTimer *settle;
    int64_t settling_ns;
    uint8_t stable;
    bool reported_error;
} NikonRXSystem;

OBJECT_DECLARE_SIMPLE_TYPE(NikonRXSystem, NIKON_RX_SYSTEM)

static int system_index(hwaddr address, unsigned size)
{
    unsigned i;

    for (i = 0; i < ARRAY_SIZE(system_registers); i++) {
        if (address == system_registers[i].address &&
            size == system_registers[i].width) {
            return i;
        }
    }
    return -1;
}

static uint32_t system_value(NikonRXSystem *s, unsigned address)
{
    unsigned i;

    for (i = 0; i < ARRAY_SIZE(system_registers); i++) {
        if (system_registers[i].address == address) {
            return s->values[i];
        }
    }
    g_assert_not_reached();
}

static void system_settle(void *opaque)
{
    NikonRXSystem *s = opaque;

    s->stable = (!(system_value(s, 0x32) & 1) ? 1 : 0) |
                (!(system_value(s, 0x33) & 1) ? 2 : 0) |
                (!(system_value(s, 0x36) & 1) ? 8 : 0);
    /* PLL lock requires its selected input oscillator to be enabled/stable. */
    if (!(system_value(s, 0x2a) & 1) &&
        (s->stable & (system_value(s, 0x28) & 0x10 ? 8 : 1))) {
        s->stable |= 4;
    }
}

static MemTxResult system_read(void *opaque, hwaddr address, uint64_t *value,
                               unsigned size, MemTxAttrs attrs)
{
    NikonRXSystem *s = opaque;
    int i = system_index(address, size);

    if (i < 0 || address == 0xc2) {
        nikon_access_error(&s->reported_error, 0x80000 + address, size,
                           false, 0, "RX SYSTEM unknown register or width");
        *value = 0;
        return MEMTX_ERROR;
    }
    *value = address == 0x3c ? s->stable : s->values[i];
    return MEMTX_OK;
}

static MemTxResult system_write(void *opaque, hwaddr address, uint64_t value,
                                unsigned size, MemTxAttrs attrs)
{
    NikonRXSystem *s = opaque;
    int i = system_index(address, size);

    if (i < 0 || address == 0x3c) {
        nikon_access_error(&s->reported_error, 0x80000 + address, size,
                           true, value, "RX SYSTEM unknown or read-only register");
        return MEMTX_ERROR;
    }
    if (address == 0x3fe) {
        if ((value & 0xff00) == 0xa500) {
            s->values[i] = value & 0x0b;
        }
        return MEMTX_OK;
    }
    if (system_registers[i].protect &&
        !(system_value(s, 0x3fe) & system_registers[i].protect)) {
        return MEMTX_OK; /* Hardware write protection ignores locked writes. */
    }
    if (address == 0xc2) {
        /* Original application 17b99 requests a real loaded-domain reset. */
        if (value == 0xa501) {
            qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
        }
        return MEMTX_OK;
    }
    s->values[i] = value;
    if (address == 0x28 || address == 0x2a ||
        (address >= 0x32 && address <= 0x37)) {
        /* Stop bits take effect immediately; start/PLL settling uses a timer. */
        s->stable &= (!(system_value(s, 0x32) & 1) ? 0xff : ~5) &
                     (!(system_value(s, 0x33) & 1) ? 0xff : ~2) &
                     (!(system_value(s, 0x36) & 1) ? 0xff : ~12) &
                     (!(system_value(s, 0x2a) & 1) ? 0xff : ~4);
        timer_mod(s->settle, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                            s->settling_ns);
    }
    return MEMTX_OK;
}

static const MemoryRegionOps system_ops = {
    .read_with_attrs = system_read, .write_with_attrs = system_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl = { .min_access_size = 1, .max_access_size = 4 },
};

static void system_reset(DeviceState *dev)
{
    NikonRXSystem *s = NIKON_RX_SYSTEM(dev);
    unsigned i;

    timer_del(s->settle);
    for (i = 0; i < ARRAY_SIZE(system_registers); i++) {
        s->values[i] = system_registers[i].reset;
    }
    s->stable = 0;
    s->reported_error = false;
}

static void system_init(Object *obj)
{
    NikonRXSystem *s = NIKON_RX_SYSTEM(obj);

    memory_region_init_io(&s->iomem, obj, &system_ops, s,
                          "nikon.rx.system", 0x2000);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);
    s->settle = timer_new_ns(QEMU_CLOCK_VIRTUAL, system_settle, s);
}

static void system_finalize(Object *obj)
{
    timer_free(NIKON_RX_SYSTEM(obj)->settle);
}

static const Property system_properties[] = {
    DEFINE_PROP_INT64("settling-ns", NikonRXSystem, settling_ns, 1000),
};

static void system_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "RX software profile protected clocks";
    device_class_set_legacy_reset(dc, system_reset);
    device_class_set_props(dc, system_properties);
}

typedef struct NikonRXRTC {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    QEMUTimer *reset;
    uint8_t control[4];
    uint16_t frequency[2];
    bool reported_error;
} NikonRXRTC;

OBJECT_DECLARE_SIMPLE_TYPE(NikonRXRTC, NIKON_RX_RTC)

static void rtc_reset_complete(void *opaque)
{
    NikonRXRTC *s = opaque;

    s->control[1] &= ~2; /* RCR2.RESET self-clears after RTC reset. */
}

static MemTxResult rtc_read(void *opaque, hwaddr address, uint64_t *value,
                            unsigned size, MemTxAttrs attrs)
{
    NikonRXRTC *s = opaque;

    if (address >= 0x22 && address <= 0x28 && !(address & 1) && size == 1) {
        *value = s->control[(address - 0x22) / 2];
        return MEMTX_OK;
    }
    if ((address == 0x2a || address == 0x2c) && size == 2) {
        *value = s->frequency[(address - 0x2a) / 2];
        return MEMTX_OK;
    }
    nikon_access_error(&s->reported_error, 0x8c400 + address, size,
                       false, 0, "RX RTC unsupported register or width");
    *value = 0;
    return MEMTX_ERROR;
}

static MemTxResult rtc_write(void *opaque, hwaddr address, uint64_t value,
                             unsigned size, MemTxAttrs attrs)
{
    NikonRXRTC *s = opaque;

    if (address >= 0x22 && address <= 0x28 && !(address & 1) && size == 1) {
        s->control[(address - 0x22) / 2] = value;
        if (address == 0x24 && (value & 2)) {
            timer_mod(s->reset, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000);
        }
        return MEMTX_OK;
    }
    if ((address == 0x2a || address == 0x2c) && size == 2) {
        s->frequency[(address - 0x2a) / 2] = value;
        return MEMTX_OK;
    }
    nikon_access_error(&s->reported_error, 0x8c400 + address, size,
                       true, value, "RX RTC unsupported register or width");
    return MEMTX_ERROR;
}

static const MemoryRegionOps rtc_ops = {
    .read_with_attrs = rtc_read, .write_with_attrs = rtc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 2 },
    .impl = { .min_access_size = 1, .max_access_size = 2 },
};

static void rtc_reset(DeviceState *dev)
{
    NikonRXRTC *s = NIKON_RX_RTC(dev);

    timer_del(s->reset);
    memset(s->control, 0, sizeof(s->control));
    memset(s->frequency, 0, sizeof(s->frequency));
    s->reported_error = false;
}

static void rtc_init(Object *obj)
{
    NikonRXRTC *s = NIKON_RX_RTC(obj);

    memory_region_init_io(&s->iomem, obj, &rtc_ops, s, "nikon.rx.rtc", 0x80);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);
    s->reset = timer_new_ns(QEMU_CLOCK_VIRTUAL, rtc_reset_complete, s);
}

static void rtc_finalize(Object *obj)
{
    timer_free(NIKON_RX_RTC(obj)->reset);
}

static void rtc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "RX RTC initialization control";
    device_class_set_legacy_reset(dc, rtc_reset);
}

static const TypeInfo renesas_types[] = {
    {
        .name = TYPE_NIKON_RX_SYSTEM, .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(NikonRXSystem), .instance_init = system_init,
        .instance_finalize = system_finalize, .class_init = system_class_init,
    }, {
        .name = TYPE_NIKON_RX_RTC, .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(NikonRXRTC), .instance_init = rtc_init,
        .instance_finalize = rtc_finalize, .class_init = rtc_class_init,
    },
};
DEFINE_TYPES(renesas_types)
