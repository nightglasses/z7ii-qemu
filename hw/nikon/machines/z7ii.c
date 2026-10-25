/*
 * Z7II MAS diagnostic machine. This is not a Nikon board hardware model.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/cutils.h"
#include "exec/target_page.h"
#include "hw/core/boards.h"
#include "hw/core/loader.h"
#include "hw/core/irq.h" /* IWYU pragma: keep */
#include "hw/core/split-irq.h"
#include "hw/core/qdev.h"
#include "hw/arm/machines-qom.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h" /* IWYU pragma: keep */
#include "hw/core/or-irq.h"
#include "hw/intc/arm_gic.h"
#include "system/address-spaces.h"
#include "system/reset.h"
#include "system/system.h"
#include "system/tcg.h"
#include "target/arm/cpu.h" /* IWYU pragma: keep */
#include "hw/nikon/devices.h"
#include "hw/nikon/bundle.h"
#include "hw/nikon/storage.h"
#include "hw/nikon/processing.h"
#include "hw/nikon/sensor.h"
#include "hw/nikon/cmos.h"
#include "hw/nikon/readoutpipeline.h"
#include "hw/nikon/statistics.h"
#include "hw/nikon/frame.h"
#include "hw/nikon/sla.h"
#include "hw/nikon/bootshim.h"
#include "hw/nikon/ex.h"
#include "hw/nikon/expower.h"
#include "hw/nikon/input.h"
#include "hw/nikon/events.h"
#include "hw/nikon/exconfig.h"
#include "hw/ssi/ssi.h" /* IWYU pragma: keep */
#include "hw/i2c/i2c.h"

#define TYPE_Z7II_MACHINE MACHINE_TYPE_NAME("z7ii-mas-diagnostic")
OBJECT_DECLARE_SIMPLE_TYPE(Z7IIMachineState, Z7II_MACHINE)

#define MAS_BASE 0x10020000ULL
#define MAS_MAX_SIZE 0x2477b78ULL
#define MEMC_BASE 0x04264000ULL
#define CLOCK_BASE 0x04281000ULL
#define COMPLETION_BASE 0x04287000ULL
#define CBAR_BASE 0xf8100000ULL
#define TIMER_HZ 48000000ULL
#define Z7II_MAX_CPUS 2
#define Z7II_SLA_CPUS 2
#define Z7II_EX_CPUS 1

#define EX_ADC_POWER_RAIL_COUNT 4095

typedef struct NikonEXFunctionalInputs {
    int adc[4];
    uint8_t adc_mask;
    uint32_t io_mask;
    uint32_t io_high;
    bool power_good;
    bool gpiod13;
} NikonEXFunctionalInputs;

/* Deterministic board contract used by the complete bundle composition. */
static const NikonEXFunctionalInputs z7ii_ex_functional_inputs = {
    /* The configured power path samples channel 0 against a firmware
     * threshold of 3819.  Full-scale is a finite synthetic board-rail input,
     * not a recovered voltage measurement. */
    .adc = { EX_ADC_POWER_RAIL_COUNT, 3500, 3500, 3500 },
    .adc_mask = (1u << 0) | (1u << 1) | (1u << 2) | (1u << 3),
    /* The power-monitor expander setup leaves the four observed control
     * pins low; an omitted pin remains electrically disconnected. */
    .io_mask = (1u << 0) | (1u << 1) | (1u << 3) | (1u << 10),
    .io_high = 0,
    .power_good = true,
    .gpiod13 = true,
};
#define Z7II_DOWNLOAD_BASE 0xb0d00000ULL
#define Z7II_DOWNLOAD_SIZE 0x400000ULL
#define Z7II_SELECTOR_ONE_BASE 0xb0900000ULL
#define Z7II_SELECTOR_ONE_SIZE 0x400000ULL
#define Z7II_DISPLAY_BUFFER_BASE 0xcb4a7000ULL
#define Z7II_DISPLAY_BUFFER_SIZE 0x1194000ULL
#define Z7II_LCD_COMPOSITION_BASE 0xbf309600ULL
#define Z7II_LCD_COMPOSITION_SIZE 0x4b0000ULL
#define Z7II_RESOURCE57_RECORD0_BASE 0xbec5f200ULL
#define Z7II_RESOURCE57_RECORD0_SIZE 0x4b0000ULL
#define Z7II_RESOURCE57_RECORD1_BASE 0xbf10f200ULL
#define Z7II_RESOURCE57_RECORD1_SIZE 0x1fa400ULL
#define Z7II_RESOURCE57_RECORD7_BASE 0xc175d600ULL
#define Z7II_RESOURCE57_RECORD7_SIZE 0x4b0000ULL
#define Z7II_KIND40_BASE 0x62059800ULL
#define Z7II_KIND40_SIZE 0x1c8000ULL
#define Z7II_READOUT_PRIMARY_BASE 0x61654800ULL
#define Z7II_READOUT_PRIMARY_SIZE 0x603000ULL
#define Z7II_READOUT_SECONDARY_BASE 0x61c57800ULL
#define Z7II_READOUT_SECONDARY_SIZE 0x402000ULL
#define Z7II_INTERPOLATION_BASE 0xc24cb400ULL
#define Z7II_INTERPOLATION_SIZE 0xf00000ULL
#define Z7II_GRID_BASE 0x5d170000ULL
#define Z7II_GRID_SIZE 0x6c000ULL
#define Z7II_ROW_STORAGE_BASE 0x50300000ULL
#define Z7II_ROW_STORAGE_SIZE 0x340000ULL
#define Z7II_NOR_BASE 0x10000000ULL
#define Z7II_NOR_SIZE 0x04000000ULL
#define Z7II_NOR_SECTOR_SIZE 0x00020000ULL
#define Z7II_NOR_UNLOCK_ONE 0x10000aaaULL
#define Z7II_NOR_UNLOCK_TWO 0x10000554ULL
#define Z7II_NOR_CFI_QUERY 0x100000aaULL
#define Z7II_DESCRIPTOR11_BASE 0x12a80000ULL
#define Z7II_DESCRIPTOR11_SIZE 0x00260000ULL
#define Z7II_DESCRIPTOR12_BASE 0x12ce0000ULL
#define Z7II_DESCRIPTOR12_SIZE 0x000e0000ULL
#define Z7II_DESCRIPTOR17_BASE 0x132c0000ULL
#define Z7II_DESCRIPTOR17_SIZE 0x00020000ULL
#define Z7II_DESCRIPTOR18_BASE 0x132e0000ULL
#define Z7II_DESCRIPTOR18_SIZE 0x000a0000ULL
#define Z7II_DESCRIPTOR32_BASE 0x13e80000ULL
#define Z7II_DESCRIPTOR32_SIZE 0x00180000ULL

/* NOR command protocol state for the mapped flash device. */
typedef enum Z7IINorReadMode {
    Z7II_NOR_ARRAY,
    Z7II_NOR_AUTOSELECT,
    Z7II_NOR_CFI,
} Z7IINorReadMode;

typedef enum Z7IINorCommandStage {
    Z7II_NOR_COMMAND_IDLE,
    Z7II_NOR_UNLOCK_ONE_SEEN,
    Z7II_NOR_UNLOCK_TWO_SEEN,
    Z7II_NOR_WORD_PROGRAM,
    Z7II_NOR_ERASE_UNLOCK_ONE,
    Z7II_NOR_ERASE_UNLOCK_TWO,
    Z7II_NOR_ERASE_SELECT,
    Z7II_NOR_BUFFER_COUNT,
    Z7II_NOR_BUFFER_DATA,
    Z7II_NOR_BUFFER_CONFIRM,
} Z7IINorCommandStage;

/* Source address-class records 21..26, with source-bound storage consumers. */
static const struct {
    const char *name;
    hwaddr base;
    uint64_t size;
} z7ii_storage[] = {
    { "z7ii.kernel",   0xb1800000, 0x4700000 },
    { "z7ii.private",  0xb5f00000, 0x1e00000 },
    { "z7ii.modules",  0xb7d00000, 0x2d00000 },
    { "z7ii.code",     0xbaa00000, 0x0900000 },
    { "z7ii.graphics", 0xbb300000, 0x0e00000 },
    { "z7ii.pixels",   0xbc100000, 0x1100000 },
};

/*
 * Original pool rows119832a8..119833e0; kind18 has its own grid region.
 * These are separate pool extents, not an enclosing DDR conversion class.
 */
static const struct {
    const char *name;
    hwaddr base;
    uint64_t size;
} z7ii_capture_pools[] = {
    { "z7ii.capture-kind16", 0x54900000, 0x8834000 },
    { "z7ii.capture-kind17", 0x5d134000, 0x3c000 },
    { "z7ii.capture-kind19", 0x5d1dc000, 0x144000 },
    { "z7ii.capture-kind20", 0xafa00000, 0x43800 },
    { "z7ii.capture-kind21", 0x5d320000, 0x195000 },
    { "z7ii.capture-kind22", 0xafc3dc00, 0x60000 },
    { "z7ii.capture-kind23", 0x5d4b5000, 0x3c00 },
    /* Kind32 also supplies 12000-byte slots when kind18's count exceeds6. */
    { "z7ii.capture-kind32", 0x5df45400, 0x198d000 },
};

typedef struct Z7IINorSlot {
    MemoryRegion region;
    Z7IIMachineState *machine;
    hwaddr base;
} Z7IINorSlot;

typedef struct Z7IIRAM {
    MemoryRegion backing;
    MemoryRegion view;
} Z7IIRAM;

struct Z7IIMachineState {
    MachineState parent_obj;
    ARMCPU cpu[Z7II_MAX_CPUS];
    unsigned num_cpus;
    char *bundle_path;
    char *sla_boot_prefix;
    char *mas_boot_prefix;
    char *cmos_image;
    DeviceState *ex_input_dev;
    QEMUTimer *ex_input_timer;
    bool frame_contract;
    char *descriptor11_input;
    char *descriptor12_input;
    char *descriptor17_input;
    char *descriptor18_input;
    char *descriptor32_input;
    Z7IINorReadMode nor_read_mode;
    Z7IINorCommandStage nor_command_stage;
    hwaddr nor_buffer_sector;
    hwaddr nor_buffer_start;
    uint16_t nor_buffer_data[64];
    uint16_t nor_buffer_words;
    uint16_t nor_buffer_index;
    bool nor_regions_ready;
    bool boot_prefix_present;
    bool reported_nor;
    NikonBundle *bundle;
    MemoryRegion image;
    Z7IINorSlot nv_slot[5];
    Z7IINorSlot boot_prefix;
    MemoryRegion unknown;
    Z7IIRAM storage[ARRAY_SIZE(z7ii_storage)];
    Z7IIRAM resource57_record0;
    Z7IIRAM resource57_record1;
    Z7IIRAM resource57_record7;
    Z7IIRAM lcd_composition;
    Z7IIRAM display_buffers;
    Z7IIRAM kind40_buffer;
    Z7IIRAM readout_primary;
    Z7IIRAM readout_secondary;
    Z7IIRAM interpolation_pool;
    Z7IIRAM grid_pool;
    Z7IIRAM capture_pools[ARRAY_SIZE(z7ii_capture_pools)];
    Z7IIRAM row_storage;
    /* Selector-zero download buffer consumed by the RMU peer contract. */
    Z7IIRAM download;
    /* Selector-one packed download arena; independent from selector zero. */
    Z7IIRAM selector_one_download;
    Z7IIRAM temporary_stack;
    Z7IIRAM early_release;
    MemoryRegion shim_mas[NIKON_BOOT_SHIM_MAS_VIEW_COUNT];
    MemoryRegion peer_window;
    DeviceState *memc;
    bool reported_unknown;
};

static MemTxResult z7ii_unknown_read(void *opaque, hwaddr addr,
                                    uint64_t *value, unsigned size,
                                    MemTxAttrs attrs)
{
    Z7IIMachineState *s = opaque;
    nikon_access_error(&s->reported_unknown, addr, size, false, 0,
                      "uncovered-address");
    *value = 0; /* Output is invalid: the transaction explicitly fails. */
    return MEMTX_ERROR;
}

static MemTxResult z7ii_unknown_write(void *opaque, hwaddr addr,
                                     uint64_t value, unsigned size,
                                     MemTxAttrs attrs)
{
    Z7IIMachineState *s = opaque;
    nikon_access_error(&s->reported_unknown, addr, size, true, value,
                      "uncovered-address");
    return MEMTX_ERROR;
}

static const MemoryRegionOps z7ii_unknown_ops = {
    .read_with_attrs = z7ii_unknown_read,
    .write_with_attrs = z7ii_unknown_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
};

static void z7ii_nor_set_romd(Z7IIMachineState *s, bool enabled)
{
    if (!s->nor_regions_ready) {
        return;
    }
    memory_region_rom_device_set_romd(&s->image, enabled);
    if (s->boot_prefix_present) {
        memory_region_rom_device_set_romd(&s->boot_prefix.region, enabled);
    }
    for (unsigned i = 0; i < ARRAY_SIZE(s->nv_slot); i++) {
        memory_region_rom_device_set_romd(&s->nv_slot[i].region, enabled);
    }
}

static void z7ii_nor_protocol_reset(Z7IIMachineState *s)
{
    s->nor_read_mode = Z7II_NOR_ARRAY;
    s->nor_command_stage = Z7II_NOR_COMMAND_IDLE;
    s->nor_buffer_words = 0;
    s->nor_buffer_index = 0;
    z7ii_nor_set_romd(s, true);
}

static MemTxResult z7ii_nor_reject(Z7IIMachineState *s, hwaddr address,
                                   unsigned size, bool write, uint64_t value,
                                   const char *reason)
{
    nikon_access_error(&s->reported_nor, address, size, write, value, reason);
    return MEMTX_ERROR;
}

static uint8_t *z7ii_nor_external_pointer(Z7IIMachineState *s,
                                          hwaddr address, uint64_t size)
{
    for (unsigned i = 0; i < ARRAY_SIZE(s->nv_slot); i++) {
        Z7IINorSlot *slot = &s->nv_slot[i];
        uint64_t region_size = memory_region_size(&slot->region);

        if (address >= slot->base && size <= region_size &&
            address - slot->base <= region_size - size) {
            return memory_region_get_ram_ptr(&slot->region) +
                   (address - slot->base);
        }
    }
    return NULL;
}

static MemTxResult z7ii_nor_protocol_read(Z7IIMachineState *s,
                                          hwaddr address, uint64_t *value,
                                          unsigned size)
{
    hwaddr offset = address - Z7II_NOR_BASE;

    *value = 0;
    if (nikon_memc_nor_backend_access(s->memc, address, size, false, 0) !=
        MEMTX_OK) {
        return MEMTX_ERROR;
    }
    if (size != 2 || (address & 1)) {
        return z7ii_nor_reject(s, address, size, false, 0,
                               "nor-x16-read-not-supported");
    }
    if (s->nor_read_mode == Z7II_NOR_AUTOSELECT &&
        (offset == 0 || offset == 2 || offset == 0x1c || offset == 0x1e)) {
        /* Explicit unresolved identity input, not a claimed physical ID. */
        *value = UINT16_MAX;
        return MEMTX_OK;
    }
    if (s->nor_read_mode == Z7II_NOR_CFI) {
        switch (offset) {
        case 0x4e:
            *value = 26; /* Original service exposes exactly 64 MiB. */
            return MEMTX_OK;
        case 0x54:
            *value = 7; /* 64 x16 words, the recovered 25/29 path's bound. */
            return MEMTX_OK;
        case 0x5e:
            *value = 0;
            return MEMTX_OK;
        case 0x60:
            *value = 2; /* (0x0200 << 8) = 128 KiB erase sector. */
            return MEMTX_OK;
        default:
            break;
        }
    }
    return z7ii_nor_reject(s, address, size, false, 0,
                           "nor-query-read-not-supported");
}

static MemTxResult z7ii_nor_protocol_write(Z7IIMachineState *s,
                                           hwaddr address, uint64_t value,
                                           unsigned size)
{
    uint8_t *cell;
    hwaddr sector;

    if (nikon_memc_nor_backend_access(s->memc, address, size, true, value) !=
        MEMTX_OK) {
        return MEMTX_ERROR;
    }
    if (value == 0xf0 &&
        (address == Z7II_NOR_BASE || address == Z7II_NOR_UNLOCK_ONE)) {
        z7ii_nor_protocol_reset(s);
        return MEMTX_OK;
    }
    if (s->nor_read_mode != Z7II_NOR_ARRAY) {
        return z7ii_nor_reject(s, address, size, true, value,
                               "nor-query-exit-command-not-supported");
    }

    switch (s->nor_command_stage) {
    case Z7II_NOR_COMMAND_IDLE:
        if (address == Z7II_NOR_UNLOCK_ONE && value == 0xaa) {
            s->nor_command_stage = Z7II_NOR_UNLOCK_ONE_SEEN;
            return MEMTX_OK;
        }
        if (address == Z7II_NOR_CFI_QUERY && value == 0x98) {
            s->nor_read_mode = Z7II_NOR_CFI;
            z7ii_nor_set_romd(s, false);
            return MEMTX_OK;
        }
        break;
    case Z7II_NOR_UNLOCK_ONE_SEEN:
        if (address == Z7II_NOR_UNLOCK_TWO && value == 0x55) {
            s->nor_command_stage = Z7II_NOR_UNLOCK_TWO_SEEN;
            return MEMTX_OK;
        }
        break;
    case Z7II_NOR_UNLOCK_TWO_SEEN:
        if (address == Z7II_NOR_UNLOCK_ONE && value == 0x90) {
            s->nor_read_mode = Z7II_NOR_AUTOSELECT;
            s->nor_command_stage = Z7II_NOR_COMMAND_IDLE;
            z7ii_nor_set_romd(s, false);
            return MEMTX_OK;
        }
        if (address == Z7II_NOR_UNLOCK_ONE && value == 0xa0) {
            s->nor_command_stage = Z7II_NOR_WORD_PROGRAM;
            return MEMTX_OK;
        }
        if (address == Z7II_NOR_UNLOCK_ONE && value == 0x80) {
            s->nor_command_stage = Z7II_NOR_ERASE_UNLOCK_ONE;
            return MEMTX_OK;
        }
        sector = address & ~(Z7II_NOR_SECTOR_SIZE - 1);
        if (address == sector && value == 0x25 &&
            z7ii_nor_external_pointer(s, sector, Z7II_NOR_SECTOR_SIZE)) {
            s->nor_buffer_sector = sector;
            s->nor_command_stage = Z7II_NOR_BUFFER_COUNT;
            return MEMTX_OK;
        }
        break;
    case Z7II_NOR_WORD_PROGRAM:
        cell = z7ii_nor_external_pointer(s, address, 2);
        if (cell) {
            stw_le_p(cell, lduw_le_p(cell) & value);
            s->nor_command_stage = Z7II_NOR_COMMAND_IDLE;
            return MEMTX_OK;
        }
        break;
    case Z7II_NOR_ERASE_UNLOCK_ONE:
        if (address == Z7II_NOR_UNLOCK_ONE && value == 0xaa) {
            s->nor_command_stage = Z7II_NOR_ERASE_UNLOCK_TWO;
            return MEMTX_OK;
        }
        break;
    case Z7II_NOR_ERASE_UNLOCK_TWO:
        if (address == Z7II_NOR_UNLOCK_TWO && value == 0x55) {
            s->nor_command_stage = Z7II_NOR_ERASE_SELECT;
            return MEMTX_OK;
        }
        break;
    case Z7II_NOR_ERASE_SELECT:
        sector = address & ~(Z7II_NOR_SECTOR_SIZE - 1);
        cell = z7ii_nor_external_pointer(s, sector, Z7II_NOR_SECTOR_SIZE);
        if (address == sector && value == 0x30 && cell) {
            memset(cell, 0xff, Z7II_NOR_SECTOR_SIZE);
            s->nor_command_stage = Z7II_NOR_COMMAND_IDLE;
            return MEMTX_OK;
        }
        break;
    case Z7II_NOR_BUFFER_COUNT:
        if (address == s->nor_buffer_sector && value < 64) {
            s->nor_buffer_words = value + 1;
            s->nor_buffer_index = 0;
            s->nor_command_stage = Z7II_NOR_BUFFER_DATA;
            return MEMTX_OK;
        }
        break;
    case Z7II_NOR_BUFFER_DATA:
        if (s->nor_buffer_index == 0) {
            s->nor_buffer_start = address;
        }
        if (address == s->nor_buffer_start + s->nor_buffer_index * 2 &&
            address >= s->nor_buffer_sector &&
            address + 2 <= s->nor_buffer_sector + Z7II_NOR_SECTOR_SIZE &&
            z7ii_nor_external_pointer(s, address, 2)) {
            s->nor_buffer_data[s->nor_buffer_index++] = value;
            if (s->nor_buffer_index == s->nor_buffer_words) {
                s->nor_command_stage = Z7II_NOR_BUFFER_CONFIRM;
            }
            return MEMTX_OK;
        }
        break;
    case Z7II_NOR_BUFFER_CONFIRM:
        if (address == s->nor_buffer_sector && value == 0x29) {
            for (unsigned i = 0; i < s->nor_buffer_words; i++) {
                cell = z7ii_nor_external_pointer(s,
                    s->nor_buffer_start + i * 2, 2);
                g_assert(cell);
                stw_le_p(cell, lduw_le_p(cell) & s->nor_buffer_data[i]);
            }
            s->nor_buffer_words = 0;
            s->nor_buffer_index = 0;
            s->nor_command_stage = Z7II_NOR_COMMAND_IDLE;
            return MEMTX_OK;
        }
        break;
    default:
        g_assert_not_reached();
    }

    s->nor_command_stage = Z7II_NOR_COMMAND_IDLE;
    return z7ii_nor_reject(s, address, size, true, value,
                           "nor-command-sequence-not-supported");
}

static MemTxResult z7ii_nor_write(void *opaque, hwaddr offset,
                                 uint64_t value, unsigned size,
                                 MemTxAttrs attrs)
{
    return z7ii_nor_protocol_write(opaque, MAS_BASE + offset, value, size);
}

static MemTxResult z7ii_nor_read(void *opaque, hwaddr offset,
                                uint64_t *value, unsigned size,
                                MemTxAttrs attrs)
{
    return z7ii_nor_protocol_read(opaque, MAS_BASE + offset, value, size);
}

static const MemoryRegionOps z7ii_nor_ops = {
    .read_with_attrs = z7ii_nor_read,
    .write_with_attrs = z7ii_nor_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
};

static MemTxResult z7ii_nv_write(void *opaque, hwaddr offset, uint64_t value,
                               unsigned size, MemTxAttrs attrs)
{
    Z7IINorSlot *slot = opaque;

    return z7ii_nor_protocol_write(slot->machine, slot->base + offset,
                                    value, size);
}

static MemTxResult z7ii_nv_read(void *opaque, hwaddr offset, uint64_t *value,
                              unsigned size, MemTxAttrs attrs)
{
    Z7IINorSlot *slot = opaque;

    return z7ii_nor_protocol_read(slot->machine, slot->base + offset,
                                  value, size);
}

static const MemoryRegionOps z7ii_nv_ops = {
    .read_with_attrs = z7ii_nv_read,
    .write_with_attrs = z7ii_nv_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
};

static void z7ii_reset(void *opaque)
{
    Z7IIMachineState *s = opaque;
    unsigned i;

    s->reported_unknown = false;
    s->reported_nor = false;
    z7ii_nor_protocol_reset(s);
    /* Even entry means ARM state. No firmware instruction is changed. */
    for (i = 0; i < s->num_cpus; i++) {
        cpu_reset(CPU(&s->cpu[i]));
        cpu_set_pc(CPU(&s->cpu[i]), MAS_BASE);
    }
}

static void z7ii_ram(Z7IIRAM *ram, const char *name,
                     hwaddr base, uint64_t size)
{
    hwaddr offset = base & (qemu_target_page_size() - 1);
    g_autofree char *backing_name = g_strdup_printf("%s.storage", name);

    /* Migratable board RAM uses NULL; its owner must not be a MachineState. */
    memory_region_init_ram(&ram->backing, NULL, backing_name, size + offset,
                           &error_fatal);
    /* Match physical and RAM page offsets without mapping the prefix. */
    memory_region_init_alias(&ram->view, NULL, name, &ram->backing, offset, size);
    memory_region_add_subregion(get_system_memory(), base, &ram->view);
}

static void z7ii_erased_slot(Z7IIMachineState *s, unsigned index,
                             const char *name, hwaddr base, uint64_t size)
{
    Z7IINorSlot *slot = &s->nv_slot[index];

    slot->machine = s;
    slot->base = base;
    memory_region_init_rom_device(&slot->region, NULL, &z7ii_nv_ops, slot,
                                  name, size, &error_fatal);
    memset(memory_region_get_ram_ptr(&slot->region), 0xff, size);
    memory_region_add_subregion(get_system_memory(), base, &slot->region);
}

static void z7ii_load_descriptor_input(Z7IIMachineState *s, const char *path,
                                      unsigned descriptor, hwaddr base,
                                      size_t expected_size)
{
    g_autoptr(GError) error = NULL;
    g_autofree char *data = NULL;
    gsize size;
    uint8_t *destination;

    if (!path || !*path) {
        return;
    }
    if (!g_file_get_contents(path, &data, &size, &error)) {
        error_report("cannot load descriptor%u input '%s': %s",
                     descriptor, path, error->message);
        exit(EXIT_FAILURE);
    }
    if (size != expected_size) {
        error_report("descriptor%u input must contain exactly %zu KiB",
                     descriptor, expected_size / 1024);
        exit(EXIT_FAILURE);
    }
    destination = z7ii_nor_external_pointer(s, base, size);
    g_assert(destination);
    memcpy(destination, data, size);
}

static void z7ii_map_boot_prefix(Z7IIMachineState *s, GBytes *bytes)
{
    Z7IINorSlot *slot = &s->boot_prefix;

    slot->machine = s;
    slot->base = 0x10000000;
    memory_region_init_rom_device(&slot->region, NULL, &z7ii_nv_ops, slot,
                                  "z7ii.mas-boot-prefix", g_bytes_get_size(bytes),
                                  &error_fatal);
    memcpy(memory_region_get_ram_ptr(&slot->region),
           g_bytes_get_data(bytes, NULL), g_bytes_get_size(bytes));
    memory_region_add_subregion(get_system_memory(), slot->base, &slot->region);
    s->boot_prefix_present = true;
}

static void z7ii_select_bundle_inputs(Z7IIMachineState *s)
{
    if (!s->bundle) {
        return;
    }
    if (!s->mas_boot_prefix) {
        s->mas_boot_prefix = g_strdup("erased");
    }
    if (!s->sla_boot_prefix) {
        s->sla_boot_prefix = g_strdup("erased");
    }
}

static void z7ii_prepare_bundle_loader(Z7IIMachineState *s)
{
    /*
     * The bundle loader supplies only
     * package-external FF NOR inputs and the finite early copy destinations
     * observed in the unchanged bundle trace.  The guest still owns CP15,
     * TTBR, SMP release, IRQ, DMA completion and every display operation.
     */
    if (!s->bundle) {
        return;
    }
    nikon_bootshim_prepare_mas(NIKON_BOOT_SHIM_FUNCTIONAL,
                               get_system_memory(),
                               s->shim_mas, &error_fatal);
    error_report("z7ii-mas-diagnostic: bundle loader; erased prefixes "
                 "and finite early-copy views are functional assumptions; "
                 "CP15, SMP release, IRQ, DMA completion and pixels remain guest-owned");
}

static void z7ii_ex_input_tick(void *opaque)
{
    Z7IIMachineState *s = opaque;
    NikonEXState *ex = NIKON_EX(s->ex_input_dev);
    static const unsigned pins[][2] = {
        { 0, 10 }, /* GPIOA.10, six-channel bit 0 */
        { 1, 1 },  /* GPIOB.1, six-channel bit 1 */
        { 1, 15 }, /* GPIOB.15 */
        { 1, 11 }, /* GPIOB.11 */
        { 1, 12 }, /* GPIOB.12 */
        { 5, 3 },  /* GPIOF.3 */
        { 6, 2 },  /* GPIOG.2, four-channel bit 2 */
    };

    for (unsigned i = 0; i < ARRAY_SIZE(pins); i++) {
        /*
         * These are external power-good wires.  The original firmware treats
         * all of them low as a rail fault (it logs 20001 and reboots with
         * status 14), so a good rail is asserted high.  Replay the declared
         * board level after the firmware changes GPIO modes so the input IDR
         * reflects it; a failed rail profile drives them low.
         */
        qemu_set_irq(nikon_ex_gpio_input(ex, pins[i][0], pins[i][1]),
                     z7ii_ex_functional_inputs.power_good ? 1 : 0);
    }
    if (z7ii_ex_functional_inputs.gpiod13) {
        /* GPIOD.13 is a separate board input polled by the configured
         * battery path. Keep the declared external level present across
         * GPIO reset/mode changes; do not infer a status or fabricate an
         * edge for the guest. */
        qemu_set_irq(nikon_ex_gpio_input(ex, 3, 13), 1);
    }
    timer_mod(s->ex_input_timer,
              qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 1);
}

static void z7ii_apply_ex_config_data(uint8_t *copy, gsize size)
{
    /*
     * The supplied EX component has an erased factory window, so install the
     * class-factory structure the firmware's own block validator accepts.
     * This is model data for the bundle board record, not recovered factory
     * calibration; see include/hw/nikon/exconfig.h for the block layout.
     */
    if (!nikon_ex_config_compose(copy, size)) {
        error_report("EX firmware is too small for the factory config input");
        exit(EXIT_FAILURE);
    }
}

static void z7ii_attach_ex(Z7IIMachineState *s, DeviceState *gpio,
                           DeviceState *spi_ports[])
{
    g_autoptr(GBytes) payload = nikon_bundle_component(s->bundle,
                                                     NIKON_COMPONENT_EX);
    DeviceState *dev = qdev_new(TYPE_NIKON_EX);
    NikonEXState *ex = NIKON_EX(dev);
    DeviceState *input = qdev_new(TYPE_NIKON_INPUT);
    unsigned i;

    object_property_add_child(OBJECT(s), "ex", OBJECT(dev));
    gsize size;
    const guint8 *data = g_bytes_get_data(payload, &size);
    guint8 *copy = g_memdup2(data, size);

    z7ii_apply_ex_config_data(copy, size);
    payload = g_bytes_new_take(copy, size);
    nikon_ex_set_firmware(ex, payload, &error_fatal);
    for (i = 0; i < ARRAY_SIZE(z7ii_ex_functional_inputs.adc); i++) {
        if (z7ii_ex_functional_inputs.adc_mask & (1u << i)) {
            nikon_ex_adc_input(ex, 0, i, z7ii_ex_functional_inputs.adc[i]);
        }
    }
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    for (i = 0; i < 24; i++) {
        if (z7ii_ex_functional_inputs.io_mask & (1u << i)) {
            qemu_set_irq(qdev_get_gpio_in_named(nikon_ex_io(ex), "pad", i),
                         !!(z7ii_ex_functional_inputs.io_high & (1u << i)));
        }
    }

    /* Separate source channels: MAS TxA clocks EX SPI1 RX; RxA clocks SPI4 TX.
     * The source MCU uses software NSS. CS zero identifies the SSI endpoint;
     * it neither acknowledges a frame nor drives the GPIO handshake.
     * MAS channels 0/1 use the original no-DMA resource sentinel 0x158.
     */
    nikon_ex_spi_slave(ex, 0,
        (SSIBus *)qdev_get_child_bus(spi_ports[0], "spi"), 0, 16, true,
        &error_fatal);
    nikon_ex_spi_slave(ex, 3,
        (SSIBus *)qdev_get_child_bus(spi_ports[1], "spi"), 0, 16, true,
        &error_fatal);

    /* These physical pairs are a functional board assumption backed by both
     * original software roles. Only actual guest GPIO drives propagate.
     */
    nikon_ex_gpio_connect_output(ex, 1, 14,
        qdev_get_gpio_in_named(gpio, "external-level", 5 * 16 + 9));
    nikon_ex_gpio_connect_output(ex, 3, 12,
        qdev_get_gpio_in_named(gpio, "external-level", 6 * 16 + 15));
    nikon_ex_gpio_connect_output(ex, 4, 3,
        qdev_get_gpio_in_named(gpio, "external-level", 5 * 16 + 10));
    qdev_connect_gpio_out_named(gpio, "pad", 6 * 16 + 12,
                               nikon_ex_gpio_input(ex, 6, 6));

    object_property_add_child(OBJECT(s), "input", OBJECT(input));
    for (i = 0; i < NIKON_INPUT_PADS; i++) {
        unsigned id = nikon_input_gpio_ids[i];

        qdev_connect_gpio_out_named(input, "pad", i,
                                   nikon_ex_gpio_input(ex, id >> 4, id & 15));
    }
    qdev_realize_and_unref(input, NULL, &error_fatal);
    s->ex_input_dev = dev;
    s->ex_input_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL,
                                     z7ii_ex_input_tick, s);
    /*
     * The declared board levels are wires, not something that appears later:
     * publish them before the first guest instruction.  The periodic tick then
     * keeps them present across GPIO mode changes and resets.
     */
    z7ii_ex_input_tick(s);
}

static void z7ii_init(MachineState *machine)
{
    Z7IIMachineState *s = Z7II_MACHINE(machine);
    DeviceState *gic;
    DeviceState *mxic;
    DeviceState *gpio;
    DeviceState *exiu;
    DeviceState *bridge;
    DeviceState *readout;
    DeviceState *capture;
    DeviceState *work = NULL;
    DeviceState *pipeline = NULL;
    DeviceState *parameters;
    DeviceState *group;
    DeviceState *cmos;
    DeviceState *endpoints, *banks, *statistics = NULL;
    DeviceState *router, *input_split = NULL;
    DeviceState *spi_ports[11];
    SysBusDevice *gic_bus;
    int64_t image_size;
    g_autoptr(GBytes) mas = NULL;
    g_autoptr(GBytes) mas_boot_prefix = NULL;
    unsigned i;

    if (strcmp(machine->cpu_type, ARM_CPU_TYPE_NAME("cortex-a7"))) {
        error_report("z7ii-mas-diagnostic currently requires Cortex-A7");
        exit(EXIT_FAILURE);
    }
    if (!!machine->firmware == !!s->bundle_path) {
        error_report("select exactly one input: -bios <raw MAS payload> "
                     "or -machine bundle=<decoded container>");
        exit(EXIT_FAILURE);
    }
    if (s->sla_boot_prefix && !s->bundle_path) {
        error_report("sla-boot-prefix requires a complete bundle and private SLA");
        exit(EXIT_FAILURE);
    }
    if (machine->smp.cpus > Z7II_MAX_CPUS) {
        error_report("the MAS domain supports at most %u CPUs; "
                     "maxcpus reserves execution capacity for private domains",
                     Z7II_MAX_CPUS);
        exit(EXIT_FAILURE);
    }
    if (s->bundle_path && tcg_enabled() && qemu_tcg_mttcg_enabled() &&
        machine->smp.max_cpus < machine->smp.cpus + Z7II_SLA_CPUS +
                                Z7II_EX_CPUS) {
        error_report("multi-thread TCG needs capacity for MAS, SLA and EX; "
                     "use -smp cpus=%u,maxcpus=%u or -accel tcg,thread=single",
                     machine->smp.cpus,
                     machine->smp.cpus + Z7II_SLA_CPUS + Z7II_EX_CPUS);
        exit(EXIT_FAILURE);
    }
    if (s->bundle_path) {
        s->bundle = nikon_bundle_load(s->bundle_path, &error_fatal);
        mas = nikon_bundle_component(s->bundle, NIKON_COMPONENT_MAS);
        image_size = g_bytes_get_size(mas);
    } else {
        image_size = get_image_size(machine->firmware, &error_fatal);
    }
    if (image_size <= 0 || (uint64_t)image_size > MAS_MAX_SIZE) {
        error_report("MAS diagnostic image must contain 1..%llu bytes",
                     (unsigned long long)MAS_MAX_SIZE);
        exit(EXIT_FAILURE);
    }
    /* A complete bundle selects the composed board path; raw MAS stays isolated. */
    z7ii_select_bundle_inputs(s);
    mas_boot_prefix = nikon_boot_prefix_load(s->mas_boot_prefix, &error_fatal);

    /* Reject all uncovered addresses, including missing RAM and devices. */
    memory_region_init_io(&s->unknown, OBJECT(s), &z7ii_unknown_ops, s,
                          "z7ii.uncovered", 1ULL << 32);
    memory_region_add_subregion_overlap(get_system_memory(), 0,
                                        &s->unknown, -1);

    /* Candidate VA placement is a diagnostic physical placement assumption. */
    /* ROMD preserves direct reads but sends all guest writes through MEMC. */
    memory_region_init_rom_device(&s->image, NULL, &z7ii_nor_ops, s,
                                  "z7ii.mas", image_size, &error_fatal);
    memory_region_add_subregion(get_system_memory(), MAS_BASE, &s->image);
    if (mas) {
        memcpy(memory_region_get_ram_ptr(&s->image),
               g_bytes_get_data(mas, NULL), image_size);
    } else if (load_image_targphys(machine->firmware, MAS_BASE, image_size,
                                   &error_fatal) != image_size) {
        error_report("failed to load the complete MAS diagnostic image");
        exit(EXIT_FAILURE);
    }
    if (mas_boot_prefix) {
        /* Independent external MAS input, never the SLA prefix or clone RAM. */
        z7ii_map_boot_prefix(s, mas_boot_prefix);
    }
    /*
     * Union of the original external descriptor views3,5..32 and six
     * alternatives, fixed source f75cf38b. Only adjacent/overlapping covered
     * ranges coalesce. Descriptor0/1/2 and zero-span4 are excluded; actual
     * MAS ROM and every uncovered hole retain their prior ownership.
     * Each physical byte has one shared ROMD cell, including contained and
     * aliased views. FF is a named external input, not recovered valid NVM.
     */
    {
        static const struct {
            hwaddr start;
            uint64_t size;
        } external[] = {
            { 0x12960000, 0x8e0000 },
            { 0x132c0000, 0x0c0000 },
            { 0x133a0000, 0x160000 },
            { 0x13540000, 0x900000 },
            { 0x13e80000, 0x180000 },
        };

        for (i = 0; i < ARRAY_SIZE(external); i++) {
            g_autofree char *name = g_strdup_printf("z7ii.external-nor%u", i);

            z7ii_erased_slot(s, i, name, external[i].start, external[i].size);
        }
        z7ii_load_descriptor_input(s, s->descriptor11_input, 11,
                                  Z7II_DESCRIPTOR11_BASE,
                                  Z7II_DESCRIPTOR11_SIZE);
        z7ii_load_descriptor_input(s, s->descriptor12_input, 12,
                                  Z7II_DESCRIPTOR12_BASE,
                                  Z7II_DESCRIPTOR12_SIZE);
        z7ii_load_descriptor_input(s, s->descriptor17_input, 17,
                                  Z7II_DESCRIPTOR17_BASE,
                                  Z7II_DESCRIPTOR17_SIZE);
        z7ii_load_descriptor_input(s, s->descriptor18_input, 18,
                                  Z7II_DESCRIPTOR18_BASE,
                                  Z7II_DESCRIPTOR18_SIZE);
        z7ii_load_descriptor_input(s, s->descriptor32_input, 32,
                                  Z7II_DESCRIPTOR32_BASE,
                                  Z7II_DESCRIPTOR32_SIZE);
        s->nor_regions_ready = true;
    }

    /*
     * The complete software storage profile replaces allocation-sized RAM.
     * Each source class has one backing shared by CPU and DMA; kernel copies,
     * BSS, page tables and per-core stacks all belong to class21. Classes22..26
     * supply private objects, modules, code and both graphics heaps. Logical
     * class aliases are translated only by the guest MMU, never a bus mirror.
     * Cold zero and full-class capacity are functional-board assumptions, not
     * installed DDR geometry. Guest code produces copies, PTEs, allocator
     * pointers and object metadata. Address-class flags alone do not grant RAM:
     * the other catch-all and external classes retain their existing owners.
     */
    for (i = 0; i < ARRAY_SIZE(z7ii_storage); i++) {
        z7ii_ram(&s->storage[i], z7ii_storage[i].name,
                 z7ii_storage[i].base, z7ii_storage[i].size);
    }
    /*
     * ROM table123b73ec record0 publishes a 4b0000-byte resource57 slot.
     * The natural runtime fill clears a 720x480x4 prefix at BEC5F200. Back
     * only this cold-zero slot; the guest owns its metadata and contents.
     */
    z7ii_ram(&s->resource57_record0, "z7ii.resource57-record0",
             Z7II_RESOURCE57_RECORD0_BASE, Z7II_RESOURCE57_RECORD0_SIZE);
    /*
     * Record1 publishes an exact 1fa400-byte slot through b181d390. The next
     * natural engine0 fill clears all 960x540x4 bytes at BF10F200. Its end is
     * the separately published record5 base, so neither neighbor is padding.
     */
    z7ii_ram(&s->resource57_record1, "z7ii.resource57-record1",
             Z7II_RESOURCE57_RECORD1_BASE, Z7II_RESOURCE57_RECORD1_SIZE);
    /*
     * The same table's record7 publishes a second 4b0000-byte slot through
     * b181d38c. The next natural request clears the same 720x480x4 prefix at
     * C175D600. Keep record8 at the exact record7 end uncovered.
     */
    z7ii_ram(&s->resource57_record7, "z7ii.resource57-record7",
             Z7II_RESOURCE57_RECORD7_BASE, Z7II_RESOURCE57_RECORD7_SIZE);
    /*
     * ROM table123b73ec record5 requests 4b0000 bytes and publishes its
     * resource57 allocation through b181d388. Original writer10634b04/06
     * stores BF309600 and advances the exact slot end to BF7B9600. Backing
     * this one cold-zero slot is a functional-board storage assumption; the
     * guest owns allocation metadata, contents, rendering and later reuse.
     */
    z7ii_ram(&s->lcd_composition, "z7ii.lcd-composition",
             Z7II_LCD_COMPOSITION_BASE, Z7II_LCD_COMPOSITION_SIZE);
    /*
     * Original image-memory acquire requests publish six contiguous physical
     * buffers. The first triplet starts at CB4A7000 with 384000-byte spacing;
     * the second starts at CBF33000 with 258000-byte spacing. Their exact union
     * ends at CC63B000. This RAM supplies storage only: guest code owns every
     * pointer, descriptor and pixel, and the image devices own completion.
     */
    z7ii_ram(&s->display_buffers, "z7ii.display-buffers",
             Z7II_DISPLAY_BUFFER_BASE, Z7II_DISPLAY_BUFFER_SIZE);
    /*
     * 10238314 acquires kind40, length1c8000, into holder b26b48d8.
     * Its original object publishes PA62059800; 103c5a7a uses source record15
     * to return VA36d59800. Guest PTEs translate that view back to this RAM.
     * Back only this owned allocation, not the enclosing 5f900000 class.
     */
    z7ii_ram(&s->kind40_buffer, "z7ii.kind40-buffer",
             Z7II_KIND40_BASE, Z7II_KIND40_SIZE);
    /*
     * ROM11982938 kinds38/39 select11983440/58, with no mode overrides.
     * 1023cb62 requests201000 bytes from each; 1023869e publishes their
     * descriptor pointers as readout destinations. Back the complete three-
     * and two-unit pools, not the enclosing conversion class. Cold zero is
     * a functional assumption; normal CMOS DMA owns submitted frame bytes.
     */
    z7ii_ram(&s->readout_primary, "z7ii.readout-primary",
             Z7II_READOUT_PRIMARY_BASE, Z7II_READOUT_PRIMARY_SIZE);
    z7ii_ram(&s->readout_secondary, "z7ii.readout-secondary",
             Z7II_READOUT_SECONDARY_BASE, Z7II_READOUT_SECONDARY_SIZE);
    /*
     * Initializer10306fb0 selects kind74 through ROM table11982938[74].
     * Row11983548 declares base C24CB400, size F00000, 10000-byte units.
     * Original102b64b0 acquires 19c80 bytes at C33AB400 and translates it
     * to VA3D5AB400 for software interpolation-record producer10404354.
     * Back the complete declared pool; cold zero is a functional-board
     * assumption. The guest owns allocation, page tables and all records.
     * Neither a physical VA alias nor adjacent RAM is added.
     */
    z7ii_ram(&s->interpolation_pool, "z7ii.interpolation-pool",
             Z7II_INTERPOLATION_BASE, Z7II_INTERPOLATION_SIZE);
    /*
     * ROM table11982938[18] selects row119832f0: six 12000-byte units
     * at PA5D170000. Original102cd692/1030ac6e publish the descriptors;
     * 1024feb6 reads the first unit through an identity guest mapping.
     * Back the whole declared pool with the same cold-zero RAM convention.
     * This supplies storage, not samples or completion from the separate
     * endpoint1 statistics path; no producer or status is inferred here.
     */
    z7ii_ram(&s->grid_pool, "z7ii.grid-pool", Z7II_GRID_BASE, Z7II_GRID_SIZE);
    /*
     * 1030ab92/ad70/af00/b0ee/b9aa/b2ce establish the endpoint pool
     * family;10239dc6 uses kind23 for capture-group cursors. Back all other
     * complete declared pools17..23 once, without duplicating kind18.
     * Cold zero is a functional assumption. Storage performs no statistics,
     * frame DMA, result publication or EPP completion on the guest's behalf.
     */
    for (i = 0; i < ARRAY_SIZE(z7ii_capture_pools); i++) {
        z7ii_ram(&s->capture_pools[i], z7ii_capture_pools[i].name,
                 z7ii_capture_pools[i].base, z7ii_capture_pools[i].size);
    }
    /*
     * Resource11/12 destinations from 10248468; full loader extents at
     * 118d0930/118d09d0. Original code supplies all contents from NOR.
     */
    z7ii_ram(&s->row_storage, "z7ii.row-storage",
             Z7II_ROW_STORAGE_BASE, Z7II_ROW_STORAGE_SIZE);
    /*
     * Observational LCD scanout for the composition buffer.  A panel scans its
     * buffer continuously; this observer samples the guest-owned region on a
     * virtual-clock cadence and publishes a console surface.  It adds no guest
     * register, IRQ, completion or status and never writes guest memory.
     */
    {
        DeviceState *panel = qdev_new(TYPE_NIKON_PANEL);

        qdev_prop_set_uint64(panel, "base", Z7II_LCD_COMPOSITION_BASE);
        qdev_prop_set_uint32(panel, "width", 1024);
        qdev_prop_set_uint32(panel, "height", 688);
        qdev_prop_set_uint32(panel, "skip-rows", 6);
        object_property_add_child(OBJECT(machine), "panel", OBJECT(panel));
        sysbus_realize_and_unref(SYS_BUS_DEVICE(panel), &error_fatal);
    }
    z7ii_prepare_bundle_loader(s);
    if (s->bundle) {
        DeviceState *processor = qdev_new(TYPE_NIKON_PROCESSOR);

        /* Original MAS boot control and its 16-KiB CPU-written program view.
         * Request-only: no processor execution, status, reply or IRQ source.
         * No SLA route is inferred from shared numerical addresses.
         */
        object_property_add_child(OBJECT(machine), "processor", OBJECT(processor));
        sysbus_realize_and_unref(SYS_BUS_DEVICE(processor), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(processor), 0, 0x2c040000);
        sysbus_mmio_map(SYS_BUS_DEVICE(processor), 1, 0x04294000);
    }
    /*
     * The unchanged selector-zero loader initializes B0D00000 as a 4 MiB
     * scratch buffer, copies MAS view0 (0x107a4f34, 0x3b1c0 bytes) into it,
     * and passes the same range to the RMU/IPUD peer.  Keep this as a
     * dedicated source-backed region rather than folding it into a DDR class:
     * the static contract does not identify it as ordinary allocator storage.
     */
    z7ii_ram(&s->download, "z7ii.download", Z7II_DOWNLOAD_BASE,
             Z7II_DOWNLOAD_SIZE);
    /* Selector one initializes a separate four-MiB arena at B0900000. The
     * firmware partitions it into three page-aligned COPY destinations and
     * validates the aggregate extent against this exact bound. Keep it as a
     * named region rather than folding it into a DDR class or aliasing B0D. */
    z7ii_ram(&s->selector_one_download, "z7ii.selector-one-download",
             Z7II_SELECTOR_ONE_BASE, Z7II_SELECTOR_ONE_SIZE);
    /* O394 + O4c6c + O5bd0/O5f40 require 8 + 40 + 40 bytes. */
    z7ii_ram(&s->temporary_stack, "z7ii.temporary-stack", 0x04008ea8, 0x58);
    /* CPU0 O0xd848 produces the magic; secondary WFE/read/clear is guest code. */
    z7ii_ram(&s->early_release, "z7ii.early-release", 0x04008ffc, 4);

    {
        DeviceState *config = qdev_new(TYPE_NIKON_MXIC_CONFIG);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(config), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(config), 0, 0x20010000);
        sysbus_mmio_map(SYS_BUS_DEVICE(config), 1, 0x24074000);
    }
    mxic = sysbus_create_simple(TYPE_NIKON_MXIC, 0x20016000, NULL);
    for (i = 0; i < ARRAY_SIZE(z7ii_storage); i++) {
        nikon_mxic_attach_ram(mxic, &s->storage[i].view, z7ii_storage[i].base);
    }

    s->num_cpus = machine->smp.cpus;
    for (i = 0; i < s->num_cpus; i++) {
        g_autofree char *name = g_strdup_printf("cpu%u", i);
        object_initialize_child(OBJECT(s), name, &s->cpu[i], machine->cpu_type);
        qdev_prop_set_uint64(DEVICE(&s->cpu[i]), "reset-cbar", CBAR_BASE);
        qdev_prop_set_uint64(DEVICE(&s->cpu[i]), "cntfrq", TIMER_HZ);
        qdev_realize(DEVICE(&s->cpu[i]), NULL, &error_fatal);
    }

    s->memc = sysbus_create_simple(TYPE_NIKON_MEMC, MEMC_BASE, NULL);
    sysbus_create_simple(TYPE_NIKON_CLOCK_GATES, CLOCK_BASE, NULL);

    sysbus_create_simple(TYPE_NIKON_TIMESTAMP, 0x04266000, NULL);
    sysbus_create_simple(TYPE_NIKON_SYSCONFIG, 0x04280000, NULL);
    gpio = qdev_new(TYPE_NIKON_GPIO);
    object_property_add_child(OBJECT(machine), "gpio", OBJECT(gpio));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(gpio), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(gpio), 0, 0x04282000);
    for (i = 0; i < 2; i++) {
        DeviceState *sdi = qdev_new(TYPE_NIKON_SDI);

        qdev_prop_set_uint32(sdi, "bank", i);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(sdi), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(sdi), 0, 0x20020000 + i * 0x20000);
    }

    /* Whole upstream GIC, independently configured from the CPU's EL2. */
    gic = qdev_new(TYPE_ARM_GIC);
    gic_bus = SYS_BUS_DEVICE(gic);
    qdev_prop_set_uint32(gic, "num-cpu", s->num_cpus);
    qdev_prop_set_uint32(gic, "num-irq", 256); /* SGI/PPI + 224 SPIs. */
    /* Original ISACTIVER initialization relies on the pre-v2 read-only view. */
    qdev_prop_set_uint32(gic, "revision", 1);
    qdev_prop_set_bit(gic, "has-security-extensions", true);
    qdev_prop_set_bit(gic, "has-virtualization-extensions", false);
    sysbus_realize_and_unref(gic_bus, &error_fatal);
    sysbus_mmio_map(gic_bus, 0, CBAR_BASE + 0x1000);
    sysbus_mmio_map(gic_bus, 1, CBAR_BASE + 0x2000);
    for (i = 0; i < 3; i++) {
        DeviceState *usio = qdev_new(TYPE_NIKON_USIO);
        g_autofree char *name = g_strdup_printf("usio%u", i);

        /* Original three copied driver records select offsets 0, 0x10, 0x20. */
        object_property_add_child(OBJECT(machine), name, OBJECT(usio));
        if (serial_hd(i)) {
            qdev_prop_set_chr(usio, "chardev", serial_hd(i));
        }
        sysbus_realize_and_unref(SYS_BUS_DEVICE(usio), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(usio), 0, 0x2c090000 + i * 0x10);
        /* Original six ISR registrations: even RX, odd TX, channels 0..2. */
        sysbus_connect_irq(SYS_BUS_DEVICE(usio), 0,
                           qdev_get_gpio_in(gic, 186 + i * 2 - 32));
        sysbus_connect_irq(SYS_BUS_DEVICE(usio), 1,
                           qdev_get_gpio_in(gic, 187 + i * 2 - 32));
    }
    {
        router = qdev_new(TYPE_NIKON_PERIPHERAL_EVENT_ROUTER);

        /* Functional virtual camera boundary input; not measured TG timing. */
        qdev_prop_set_uint32(router, "input-period-ns", 33333334);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(router), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(router), 0, 0x20060000);
        for (i = 0; i < 3; i++) {
            sysbus_connect_irq(SYS_BUS_DEVICE(router), i,
                               qdev_get_gpio_in(gic, 86 + i - 32));
        }
    }
    {
        DeviceState *sdes = qdev_new(TYPE_NIKON_SDES);
        static const hwaddr addresses[NIKON_SDES_REGIONS] = {
            [NIKON_SDES_CONFIGURATION] = 0x20061000,
            [NIKON_SDES_STATUS] = 0x20064000,
            [NIKON_SDES_PARAMETERS] = 0x20065000,
        };

        object_property_add_child(OBJECT(machine), "sdes", OBJECT(sdes));
        sysbus_realize_and_unref(SYS_BUS_DEVICE(sdes), &error_fatal);
        for (i = 0; i < NIKON_SDES_REGIONS; i++) {
            sysbus_mmio_map(SYS_BUS_DEVICE(sdes), i, addresses[i]);
        }
        for (i = 0; i < NIKON_SDES_EVENTS; i++) {
            sysbus_connect_irq(SYS_BUS_DEVICE(sdes), i,
                               qdev_get_gpio_in(gic, 89 + i - 32));
        }
        /* Original handler/status interface; no invented sensor-frame source. */
    }
    {
        DeviceState *channels = qdev_new(TYPE_NIKON_SDES_CHANNELS);

        object_property_add_child(OBJECT(machine), "sdes-channels",
                                  OBJECT(channels));
        sysbus_realize_and_unref(SYS_BUS_DEVICE(channels), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(channels), 0, 0x20063000);
        /* Original 102d6b6a consumes IRQ87 bits18/10 for sequence stops. */
        qdev_connect_gpio_out_named(channels, "stopped", 0,
            qdev_get_gpio_in_named(router, "external-pending", 11 + 18));
        qdev_connect_gpio_out_named(channels, "stopped", 1,
            qdev_get_gpio_in_named(router, "external-pending", 11 + 10));
    }
    {
        capture = qdev_new(TYPE_NIKON_CAPTURE_CONTROL);
        group = qdev_new(TYPE_NIKON_CAPTURE_GROUP);
        if (s->bundle) {
            statistics = qdev_new(TYPE_NIKON_STATISTICS);
            work = qdev_new(TYPE_NIKON_CAPTURE_WORK);
            pipeline = qdev_new(TYPE_NIKON_READOUT_PIPELINE);
            object_property_add_child(OBJECT(machine), "readout-pipeline",
                                      OBJECT(pipeline));
            object_property_add_child(OBJECT(machine), "capture-processing",
                                      OBJECT(work));
            object_property_add_child(OBJECT(machine), "capture-statistics",
                                      OBJECT(statistics));
        }

        object_property_add_child(OBJECT(machine), "capture-control",
                                  OBJECT(capture));
        sysbus_realize_and_unref(SYS_BUS_DEVICE(capture), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(capture), 0, 0x20068000);
        sysbus_mmio_map(SYS_BUS_DEVICE(capture), 1, 0x20069d00);
        for (i = 0; i < NIKON_CAPTURE_EVENTS; i++) {
            sysbus_connect_irq(SYS_BUS_DEVICE(capture), i,
                               qdev_get_gpio_in(gic, 94 + i - 32));
        }
        /* Original status/command interface; external publication is unbound. */
        object_property_add_child(OBJECT(machine), "capture-group", OBJECT(group));
        sysbus_realize_and_unref(SYS_BUS_DEVICE(group), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(group), 0, 0x2006b800);
        for (i = 0; i < 2; i++) {
            qdev_connect_gpio_out_named(capture, "command-data", i * 32 + 4,
                qdev_get_gpio_in_named(group, "commit-selected", i));
        }
        qdev_connect_gpio_out_named(capture, "run-state", 5,
                                   qdev_get_gpio_in_named(group, "run", 0));
        if (statistics) {
            DeviceState *split = qdev_new(TYPE_SPLIT_IRQ);

            input_split = split;
            qdev_prop_set_uint16(split, "num-lines", 4);
            qdev_realize_and_unref(split, NULL, &error_fatal);
            qdev_connect_gpio_out(split, 0,
                qdev_get_gpio_in_named(group, "automatic-rise", 0));
            qdev_connect_gpio_out(split, 1,
                qdev_get_gpio_in_named(statistics, "automatic-rise", 0));
            qdev_connect_gpio_out(split, 3,
                qdev_get_gpio_in_named(work, "automatic-rise", 0));
            qdev_connect_gpio_out_named(router, "automatic-rise", 0,
                                       qdev_get_gpio_in(split, 0));
            qdev_connect_gpio_out_named(capture, "run-state", 4,
                qdev_get_gpio_in_named(statistics, "run", 0));
            for (unsigned i = 0; i < 2; i++) {
                qdev_connect_gpio_out_named(capture, "run-state", 6 + i,
                    qdev_get_gpio_in_named(statistics, "bank-run", i));
            }
        } else {
            qdev_connect_gpio_out_named(router, "automatic-rise", 0,
                qdev_get_gpio_in_named(group, "automatic-rise", 0));
        }
    }
    {
        parameters = qdev_new(TYPE_NIKON_CAPTURE_PARAMETERS);

        object_property_add_child(OBJECT(machine), "capture-parameters",
                                  OBJECT(parameters));
        sysbus_realize_and_unref(SYS_BUS_DEVICE(parameters), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(parameters), 0, 0x20069500);
    }
    {
        endpoints = qdev_new(TYPE_NIKON_CAPTURE_ENDPOINTS);

        object_property_add_child(OBJECT(machine), "capture-endpoints",
                                  OBJECT(endpoints));
        sysbus_realize_and_unref(SYS_BUS_DEVICE(endpoints), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(endpoints), 0, 0x2006a200);
    }
    {
        banks = qdev_new(TYPE_NIKON_CAPTURE_BANKS);

        object_property_add_child(OBJECT(machine), "capture-banks", OBJECT(banks));
        sysbus_realize_and_unref(SYS_BUS_DEVICE(banks), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(banks), 0, 0x2006a000);
        sysbus_mmio_map(SYS_BUS_DEVICE(banks), 1, 0x2006be00);
    }
    {
        static const hwaddr addresses[NIKON_READOUT_REGIONS] = {
            [NIKON_READOUT_BUFFERS] = 0x20069800,
            [NIKON_READOUT_PARAMETERS] = 0x2006b000,
        };

        readout = qdev_new(TYPE_NIKON_READOUT_CONTROL);
        object_property_add_child(OBJECT(machine), "readout-control",
                                  OBJECT(readout));
        sysbus_realize_and_unref(SYS_BUS_DEVICE(readout), &error_fatal);
        for (i = 0; i < NIKON_READOUT_REGIONS; i++) {
            sysbus_mmio_map(SYS_BUS_DEVICE(readout), i, addresses[i]);
        }
        for (i = 0; i < 2; i++) {
            DeviceState *split = qdev_new(TYPE_SPLIT_IRQ);

            qdev_prop_set_uint16(split, "num-lines", statistics ? 5 : 2);
            qdev_realize_and_unref(split, NULL, &error_fatal);
            qdev_connect_gpio_out(split, 0,
                qdev_get_gpio_in_named(group, "commit", i));
            qdev_connect_gpio_out(split, 1,
                qdev_get_gpio_in_named(readout, "commit", i));
            if (statistics) {
                static const unsigned work_bits[] = { 0, 2, 7 };

                qdev_connect_gpio_out(split, 2,
                    qdev_get_gpio_in_named(statistics, "commit", i));
                qdev_connect_gpio_out_named(capture, "command-data", i * 32 + 3,
                    qdev_get_gpio_in_named(statistics, "commit-selected", i));
                qdev_connect_gpio_out(split, 3,
                    qdev_get_gpio_in_named(work, "commit", i));
                qdev_connect_gpio_out(split, 4,
                    qdev_get_gpio_in_named(pipeline, "commit", i));
                for (unsigned j = 0; j < ARRAY_SIZE(work_bits); j++) {
                    qemu_irq target = qdev_get_gpio_in_named(work,
                        "commit-selected", i * 3 + j);

                    if (j < 2) {
                        DeviceState *select = qdev_new(TYPE_SPLIT_IRQ);

                        qdev_prop_set_uint16(select, "num-lines", 2);
                        qdev_realize_and_unref(select, NULL, &error_fatal);
                        qdev_connect_gpio_out(select, 0, target);
                        qdev_connect_gpio_out(select, 1,
                            qdev_get_gpio_in_named(pipeline,
                                "commit-selected", i * 2 + j));
                        target = qdev_get_gpio_in(select, 0);
                    }
                    qdev_connect_gpio_out_named(capture, "command-data",
                        i * 32 + work_bits[j], target);
                }
            }

            qdev_connect_gpio_out_named(capture, "command-data", i * 32 + 1,
                qdev_get_gpio_in_named(readout, "commit-selected", i));
            qdev_connect_gpio_out_named(capture, "command-publish", i,
                                       qdev_get_gpio_in(split, 0));
        }
        qdev_connect_gpio_out_named(capture, "run-state", 1,
                                   qdev_get_gpio_in_named(readout, "run", 0));
        /* Guest requests have no default provider, IRQ or pixel data source. */
    }
    {
        cmos = qdev_new(TYPE_NIKON_CMOS);

        object_property_add_child(OBJECT(machine), "cmos", OBJECT(cmos));
        if (s->cmos_image) {
            qdev_prop_set_string(cmos, "image", s->cmos_image);
        }
        qdev_prop_set_bit(cmos, "fixture-provider",
                          s->bundle && !s->cmos_image);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(cmos), &error_fatal);
        /* Diagnostic transport aperture; no C1.70 physical MMIO is claimed. */
        sysbus_mmio_map(SYS_BUS_DEVICE(cmos), 0, 0x24080000);
    }
    if (s->bundle) {
        object_property_set_link(OBJECT(pipeline), "readout", OBJECT(readout),
                                 &error_fatal);
        object_property_set_link(OBJECT(pipeline), "cmos", OBJECT(cmos),
                                 &error_fatal);
        object_property_set_link(OBJECT(pipeline), "capture", OBJECT(capture),
                                 &error_fatal);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(pipeline), &error_fatal);
        /* One input boundary clocks transport admission and processing. */
        qdev_connect_gpio_out(input_split, 2,
            qdev_get_gpio_in_named(pipeline, "automatic-rise", 0));
        for (unsigned i = 0; i < 4; i++) {
            static const unsigned bits[] = { 0, 2, 3, 9 };
            qemu_irq target = qdev_get_gpio_in_named(work, "run", i);

            if (i < 3) {
                DeviceState *split = qdev_new(TYPE_SPLIT_IRQ);

                qdev_prop_set_uint16(split, "num-lines", 2);
                qdev_realize_and_unref(split, NULL, &error_fatal);
                qdev_connect_gpio_out(split, 0, target);
                qdev_connect_gpio_out(split, 1,
                    qdev_get_gpio_in_named(pipeline, "input-run", i));
                target = qdev_get_gpio_in(split, 0);
            }

            qdev_connect_gpio_out_named(capture, "run-state", bits[i],
                                       target);
        }
        object_property_set_link(OBJECT(work), "capture", OBJECT(capture),
                                 &error_fatal);
        object_property_set_link(OBJECT(work), "parameters", OBJECT(parameters),
                                 &error_fatal);
        object_property_set_link(OBJECT(work), "readout", OBJECT(readout),
                                 &error_fatal);
        object_property_set_link(OBJECT(work), "pipeline", OBJECT(pipeline),
                                 &error_fatal);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(work), &error_fatal);
        object_property_set_link(OBJECT(statistics), "endpoints",
                                 OBJECT(endpoints), &error_fatal);
        object_property_set_link(OBJECT(statistics), "banks", OBJECT(banks),
                                 &error_fatal);
        object_property_set_link(OBJECT(statistics), "pipeline",
                                 OBJECT(pipeline), &error_fatal);
        object_property_set_link(OBJECT(statistics), "capture", OBJECT(capture),
                                 &error_fatal);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(statistics), &error_fatal);
        qdev_connect_gpio_out_named(readout, "readout-request", 0,
            qdev_get_gpio_in_named(pipeline, "request", 0));
        qdev_connect_gpio_out_named(readout, "readout-cancel", 0,
            qdev_get_gpio_in_named(pipeline, "cancel", 0));
        for (i = 0; i < NIKON_CMOS_PIPELINE_EVENTS; i++) {
            qdev_connect_gpio_out_named(cmos, "pipeline-event", i,
                qdev_get_gpio_in_named(pipeline, "cmos-event", i));
        }
    }
    if (s->frame_contract) {
        DeviceState *frame = qdev_new(TYPE_NIKON_FRAME_CONTRACT);

        object_property_add_child(OBJECT(machine), "frame",
                                  OBJECT(frame));
        sysbus_realize_and_unref(SYS_BUS_DEVICE(frame), &error_fatal);
        /* Diagnostic-only descriptor handoff; no C1.70 MMIO claim. */
        sysbus_mmio_map(SYS_BUS_DEVICE(frame), 0, 0x24081000);
    }
    bridge = qdev_new(TYPE_NIKON_BRIDGE);
    object_property_add_child(OBJECT(machine), "bridge", OBJECT(bridge));
    qdev_prop_set_bit(bridge, "notification", !!s->bundle);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(bridge), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(bridge), 0, 0x20130000);
    sysbus_connect_irq(SYS_BUS_DEVICE(bridge), 0,
                       qdev_get_gpio_in(gic, 127 - 32));
    /* Only the source local receive aperture is exposed, not a 4 GiB bank. */
    memory_region_init_alias(&s->peer_window, OBJECT(machine), "z7ii.peer",
                             sysbus_mmio_get_region(SYS_BUS_DEVICE(bridge), 2),
                             0x40000000, 0x10000000);
    memory_region_add_subregion(get_system_memory(), 0x40000000,
                               &s->peer_window);
    sysbus_create_simple(TYPE_NIKON_IPUD, 0x24020000,
                         qdev_get_gpio_in(gic, 101 - 32));
    sysbus_create_simple(TYPE_NIKON_PROCESSING, 0x24070000,
                         qdev_get_gpio_in(gic, 110 - 32));
    {
        DeviceState *transfer = sysbus_create_simple(TYPE_NIKON_TRANSFER,
                                  0x20180000,
                                  qdev_get_gpio_in(gic, 129 - 32));

        for (i = 1; i < 3; i++) {
            sysbus_connect_irq(SYS_BUS_DEVICE(transfer), i,
                               qdev_get_gpio_in(gic, 129 + i - 32));
        }
    }
    sysbus_create_simple(TYPE_NIKON_STAGED_CONFIGURATION, 0x20202000,
                         qdev_get_gpio_in(gic, 123 - 32));
    sysbus_create_simple(TYPE_NIKON_EVENT_GATE, 0x20100000,
                         qdev_get_gpio_in(gic, 102 - 32));
    sysbus_create_simple(TYPE_NIKON_EVENTS, 0x2c012000,
                         qdev_get_gpio_in(gic, 158 - 32));
    /* IRQ229 belongs to a software callback registration, not a proven wire. */
    sysbus_create_simple(TYPE_NIKON_REQUEST_STATUS, 0x24060000, NULL);
    sysbus_create_simple(TYPE_NIKON_DATA_SAMPLER, 0x20201000,
                         qdev_get_gpio_in(gic, 116 - 32));
    sysbus_create_simple(TYPE_NIKON_TABLE_SAMPLER, 0x20204000,
                         qdev_get_gpio_in(gic, 121 - 32));
    sysbus_create_simple(TYPE_NIKON_SHARED_PARAMETER_EVENTS, 0x20205000,
                         qdev_get_gpio_in(gic, 122 - 32));
    sysbus_create_simple(TYPE_NIKON_PARAMETER_EVENTS, 0x20203000,
                         qdev_get_gpio_in(gic, 124 - 32));
    {
        DeviceState *control = sysbus_create_simple(TYPE_NIKON_BYTE_EVENT_ROUTER,
                              0x20160000, qdev_get_gpio_in(gic, 132 - 32));

        sysbus_connect_irq(SYS_BUS_DEVICE(control), 1,
                           qdev_get_gpio_in(gic, 133 - 32));
    }
    {
        DeviceState *control = qdev_new(TYPE_NIKON_DUAL_EVENT_BANKS);
        static const hwaddr addresses[] = {
            0x20144000, 0x2014b000, 0x2014d000, 0x2014e000,
            /* Two source table objects select offsets 0 and 0x10000. */
            0x24040000, 0x24042000, 0x24045000, 0x24046000,
            0x24050000, 0x24052000, 0x24055000, 0x24056000,
            0x2014c000,
        };

        control->id = g_strdup("display");
        object_property_add_child(OBJECT(machine), "display", OBJECT(control));
        sysbus_realize_and_unref(SYS_BUS_DEVICE(control), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(control), 0, 0x20140000);
        sysbus_connect_irq(SYS_BUS_DEVICE(control), 0,
                           qdev_get_gpio_in(gic, 111 - 32));
        for (i = 0; i < ARRAY_SIZE(addresses); i++) {
            sysbus_mmio_map(SYS_BUS_DEVICE(control), i + 1, addresses[i]);
        }
        for (i = 1; i < 3; i++) {
            sysbus_connect_irq(SYS_BUS_DEVICE(control), i,
                               qdev_get_gpio_in(gic, 111 + i - 32));
        }
    }
    {
        DeviceState *control = sysbus_create_simple(TYPE_NIKON_PAIRED_EVENT_BANKS,
                              0x20380000, qdev_get_gpio_in(gic, 99 - 32));

        sysbus_mmio_map(SYS_BUS_DEVICE(control), 1, 0x203c0000);
        sysbus_connect_irq(SYS_BUS_DEVICE(control), 1,
                           qdev_get_gpio_in(gic, 100 - 32));
    }
    {
        DeviceState *fill = sysbus_create_simple(TYPE_NIKON_FILL, 0x20070000,
                                                qdev_get_gpio_in(gic, 103 - 32));
        sysbus_connect_irq(SYS_BUS_DEVICE(fill), 1,
                           qdev_get_gpio_in(gic, 104 - 32));
    }
    exiu = sysbus_create_simple(TYPE_NIKON_EXIU, 0x0426f000, NULL);
    /* Original 16 EXIU objects map GPIO5[7:15]/GPIO6[0:6] to IRQ65..80. */
    for (i = 0; i < 16; i++) {
        unsigned pad = i < 9 ? 5 * 16 + 7 + i : 6 * 16 + i - 9;

        qdev_connect_gpio_out_named(gpio, "pad", pad,
                                    qdev_get_gpio_in(exiu, i));
        sysbus_connect_irq(SYS_BUS_DEVICE(exiu), i,
                           qdev_get_gpio_in(gic, 65 + i - 32));
    }
    {
        DeviceState *ccomp = qdev_new(TYPE_NIKON_CCOMP);

        object_property_add_child(OBJECT(machine), "ccomp", OBJECT(ccomp));
        sysbus_realize_and_unref(SYS_BUS_DEVICE(ccomp), &error_fatal);
        /* Original pulse/ACK roles; service timing is a virtual board law. */
        qdev_connect_gpio_out_named(gpio, "pad", 6 * 16 + 8,
                                   qdev_get_gpio_in_named(ccomp, "request", 0));
        qdev_connect_gpio_out_named(ccomp, "ack", 0,
            qdev_get_gpio_in_named(gpio, "external-level", 5 * 16 + 7));
    }
    for (i = 0; i < 11; i++) {
        DeviceState *spi = qdev_new(TYPE_NIKON_SPI);
        g_autofree char *name = g_strdup_printf("spi%u", i);

        object_property_add_child(OBJECT(machine), name, OBJECT(spi));
        spi_ports[i] = spi;
        qdev_prop_set_uint32(spi, "channel", i);
        qdev_prop_set_bit(spi, "dma", i >= 2 && i <= 4);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(spi), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(spi), 0, 0x2c0a0000 + i * 0x100);
        /*
         * Both original wrappers enter one ISR; common-pending routing is
         * a virtual-board assumption, not a measured TX/RX IRQ identity.
         */
        sysbus_connect_irq(SYS_BUS_DEVICE(spi), 0,
                           qdev_get_gpio_in(gic, 193 + i * 2 - 32));
        sysbus_connect_irq(SYS_BUS_DEVICE(spi), 1,
                           qdev_get_gpio_in(gic, 192 + i * 2 - 32));
    }
    for (i = 0; i < 4; i++) {
        DeviceState *i2c = qdev_new(TYPE_NIKON_I2C);
        g_autofree char *name = g_strdup_printf("i2c%u", i);

        object_property_add_child(OBJECT(machine), name, OBJECT(i2c));
        qdev_prop_set_uint32(i2c, "instance", i);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(i2c), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(i2c), 0, 0x2c018000 + i * 0x1000);
        sysbus_connect_irq(SYS_BUS_DEVICE(i2c), 0,
                           qdev_get_gpio_in(gic, 214 + i - 32));
        /* The complete bundle's source-bound board map selects logical3 on
         * I2C2 at address 0x38. Raw MAS diagnostics keep the bus empty so
         * tests can attach their own endpoint and preserve real NACKs. */
        if (i == 2 && s->bundle) {
            i2c_slave_create_simple(nikon_i2c_bus(i2c),
                                    TYPE_NIKON_EYE_SENSOR, 0x38);
            /*
             * Original unit6 handle 0x00020040 selects this bus. Samples
             * remain unavailable until explicitly supplied as board input.
             */
            i2c_slave_create_simple(nikon_i2c_bus(i2c),
                                    TYPE_NIKON_ACCELERATION, 0x1d);
        }
    }
    for (i = 0; i < 2; i++) {
        DeviceState *hdmac = qdev_new(TYPE_NIKON_HDMAC);
        unsigned channel;

        qdev_prop_set_uint32(hdmac, "bank", i);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(hdmac), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(hdmac), 0, 0x2c020000 + i * 0x10000);
        for (channel = 0; channel < 8; channel++) {
            unsigned irq = (i ? 142 : 150) + channel;

            sysbus_connect_irq(SYS_BUS_DEVICE(hdmac), channel,
                               qdev_get_gpio_in(gic, irq - 32));
        }
        if (i == 0) {
            /* Original SPI resources 12a..12f bind these six demand wires. */
            for (channel = 2; channel <= 4; channel++) {
                unsigned request = 7 + 2 * (channel - 2);

                qdev_connect_gpio_out_named(spi_ports[channel], "dma", 0,
                                            qdev_get_gpio_in(hdmac, request));
                qdev_connect_gpio_out_named(spi_ports[channel], "dma", 1,
                                            qdev_get_gpio_in(hdmac, request + 1));
            }
        }
        if (i == 1) {
            DeviceState *audio = qdev_new(TYPE_NIKON_AUDIO);

            sysbus_realize_and_unref(SYS_BUS_DEVICE(audio), &error_fatal);
            sysbus_mmio_map(SYS_BUS_DEVICE(audio), 0, 0x2c080000);
            sysbus_connect_irq(SYS_BUS_DEVICE(audio), 0,
                               qdev_get_gpio_in(gic, 167 - 32));
            sysbus_connect_irq(SYS_BUS_DEVICE(audio), 1,
                               qdev_get_gpio_in(gic, 168 - 32));
            /* Original logical DMA IDs132..139 select bank1 dynamically. */
            qdev_connect_gpio_out(audio, 0, qdev_get_gpio_in(hdmac, 3));
            qdev_connect_gpio_out(audio, 1, qdev_get_gpio_in(hdmac, 7));
            qdev_connect_gpio_out(audio, 2, qdev_get_gpio_in(hdmac, 5));
        }
    }
    {
        DeviceState *timing = qdev_new(TYPE_NIKON_TIMING);

        sysbus_realize_and_unref(SYS_BUS_DEVICE(timing), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(timing), 0, 0x2c010000);
        sysbus_mmio_map(SYS_BUS_DEVICE(timing), 1, 0x2c011000);
        for (i = 0; i < 12; i++) {
            sysbus_connect_irq(SYS_BUS_DEVICE(timing), i,
                               qdev_get_gpio_in(gic, 172 + i - 32));
        }
    }
    {
        DeviceState *copy = qdev_new(TYPE_NIKON_COPY);

        object_property_add_child(OBJECT(machine), "copy", OBJECT(copy));
        sysbus_realize_and_unref(SYS_BUS_DEVICE(copy), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(copy), 0, 0x04251000);
        for (i = 0; i < 4; i++) {
            sysbus_connect_irq(SYS_BUS_DEVICE(copy), i,
                               qdev_get_gpio_in(gic, 45 + i - 32));
        }
    }
    {
        DeviceState *signal = qdev_new(TYPE_NIKON_SIGNAL);
        DeviceState *aggregate = qdev_new(TYPE_OR_IRQ);
        DeviceState *storage = qdev_new(TYPE_NIKON_STORAGE_HOST);
        static const hwaddr storage_addresses[] = {
            [NIKON_STORAGE_DBI] = 0x2a900000,
            [NIKON_STORAGE_CONFIG] = 0x2a800000,
            [NIKON_STORAGE_MEMORY] = 0x2a810000,
            [NIKON_STORAGE_PHY] = 0x2b200000,
            [NIKON_STORAGE_PLATFORM] = 0x2b100000,
        };
        static const unsigned storage_irqs[] = {242, 243, 244, 246};

        sysbus_realize_and_unref(SYS_BUS_DEVICE(signal), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(signal), 0, 0x2b000000);
        sysbus_mmio_map(SYS_BUS_DEVICE(signal), 1, 0x28000000);
        qdev_prop_set_uint16(aggregate, "num-lines", 5);
        qdev_realize_and_unref(aggregate, NULL, &error_fatal);
        for (i = 0; i < 5; i++) {
            sysbus_connect_irq(SYS_BUS_DEVICE(signal), i,
                               qdev_get_gpio_in(aggregate, i));
        }
        /* Original ISR registration supplies the single shared IRQ237. */
        qdev_connect_gpio_out(aggregate, 0,
                             qdev_get_gpio_in(gic, 237 - 32));

        object_property_add_child(OBJECT(machine), "storage", OBJECT(storage));
        if (blk_by_name("storage")) {
            qdev_prop_set_string(storage, "drive", "storage");
        }
        object_property_set_link(OBJECT(storage), "memory",
                                 OBJECT(get_system_memory()), &error_fatal);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(storage), &error_fatal);
        for (i = 0; i < ARRAY_SIZE(storage_addresses); i++) {
            sysbus_mmio_map(SYS_BUS_DEVICE(storage), i, storage_addresses[i]);
        }
        for (i = 0; i < ARRAY_SIZE(storage_irqs); i++) {
            sysbus_connect_irq(SYS_BUS_DEVICE(storage), i,
                               qdev_get_gpio_in(gic, storage_irqs[i] - 32));
        }
        qdev_connect_gpio_out_named(storage, "link", 0,
                                   qdev_get_gpio_in_named(signal, "signal", 3));
    }
    /* The guest registers/enables IRQ250; this is its external SPI input. */
    sysbus_create_simple(TYPE_NIKON_COMPLETION, COMPLETION_BASE,
                         qdev_get_gpio_in(gic, 250 - 32));
    for (i = 0; i < s->num_cpus; i++) {
        DeviceState *cpu = DEVICE(&s->cpu[i]);
        const int timer_irq[] = {
            [GTIMER_PHYS] = 30, [GTIMER_VIRT] = 27,
            [GTIMER_HYP] = 26, [GTIMER_SEC] = 29,
        };
        int irq;

        /* Same upstream A15MPCore timer/PPI array layout, no invented tick. */
        for (irq = 0; irq < ARRAY_SIZE(timer_irq); irq++) {
            qdev_connect_gpio_out(cpu, irq,
                qdev_get_gpio_in(gic, 256 - 32 + i * 32 + timer_irq[irq]));
        }
        sysbus_connect_irq(gic_bus, i, qdev_get_gpio_in(cpu, ARM_CPU_IRQ));
        sysbus_connect_irq(gic_bus, i + s->num_cpus,
                           qdev_get_gpio_in(cpu, ARM_CPU_FIQ));
        sysbus_connect_irq(gic_bus, i + 2 * s->num_cpus,
                           qdev_get_gpio_in(cpu, ARM_CPU_VIRQ));
        sysbus_connect_irq(gic_bus, i + 3 * s->num_cpus,
                           qdev_get_gpio_in(cpu, ARM_CPU_VFIQ));
    }
    if (s->bundle) {
        g_autoptr(GBytes) payload = nikon_bundle_component(s->bundle,
                                                          NIKON_COMPONENT_SLA);
        g_autoptr(GBytes) boot_prefix =
            nikon_boot_prefix_load(s->sla_boot_prefix, &error_fatal);
        DeviceState *sla = nikon_sla_create(OBJECT(machine), "sla", payload,
                                            boot_prefix,
                                            NIKON_BOOT_SHIM_FUNCTIONAL,
                                            true,
                                            Z7II_SLA_CPUS, NULL,
                                            &error_fatal);
        DeviceState *sla_bridge = nikon_sla_bridge(sla);

        /* Source-complementary handshake; the board trace is not recovered. */
        qdev_connect_gpio_out_named(sla, "peer-ready", 0,
                                   qdev_get_gpio_in_named(gpio, "external-level",
                                                          6 * 16 + 3));
        nikon_bridge_attach_peer(bridge, 1, nikon_sla_address_space(sla));
        nikon_bridge_attach_peer(sla_bridge, 1, &address_space_memory);
        /* Inert unless both devices explicitly opt into notifications. */
        nikon_bridge_connect_notifications(bridge, sla_bridge);
        /* Paired request routing is a functional board assumption. */
        qdev_connect_gpio_out_named(bridge, "peer-start", 0,
            qdev_get_gpio_in_named(sla_bridge, "peer-control", 0));
        qdev_connect_gpio_out_named(bridge, "peer-configuration", 0,
            qdev_get_gpio_in_named(sla_bridge, "peer-control", 1));
        qdev_connect_gpio_out_named(sla_bridge, "peer-start", 0,
            qdev_get_gpio_in_named(bridge, "peer-control", 0));
        qdev_connect_gpio_out_named(sla_bridge, "peer-configuration", 0,
            qdev_get_gpio_in_named(bridge, "peer-control", 1));
        /* EX owns a private M4 address space; never alias it into MAS RAM. */
        z7ii_attach_ex(s, gpio, spi_ports);
    }
    qemu_register_reset(z7ii_reset, s);
    error_report("z7ii-mas-diagnostic: assumptions cpu=%s cpus=%u "
                 "image-base=0x10020000 entry=0x10020000 ARM "
                 "image-bytes=%" PRId64 " input=%s; "
                 "MEMC=0x04264000 CBAR=0xf8100000 GICv1=256 "
                 "cntfrq=48000000 secure-timer-PPI=29 early-release-RAM=4B; "
                 "storage-classes=6 storage-MiB=186 cold-zero; "
                 "resource57-record0-base=0xbec5f200 bytes=0x4b0000 cold-zero; "
                 "resource57-record1-base=0xbf10f200 bytes=0x1fa400 cold-zero; "
                 "resource57-record7-base=0xc175d600 bytes=0x4b0000 cold-zero; "
                 "interpolation-pool-base=0xc24cb400 bytes=0xf00000 cold-zero; "
                 "grid-pool-base=0x5d170000 bytes=0x6c000 cold-zero; "
                 "capture-pools=16,17,19,20,21,22,23,32 source-extents cold-zero; "
                 "readout-pools=0x61654800+0x603000,0x61c57800+0x402000 cold-zero; "
                 "lcd-composition-base=0xbf309600 bytes=0x4b0000 cold-zero; "
                 "display-buffer-base=0xcb4a7000 bytes=0x1194000 cold-zero; "
                 "no NOR writer/BootROM; private SLA2/EX1 for bundle input",
                 machine->cpu_type, s->num_cpus, image_size,
                 s->bundle ? "complete-decoded-container" : "raw-MAS");
}

static char *z7ii_get_bundle(Object *obj, Error **errp)
{
    return g_strdup(Z7II_MACHINE(obj)->bundle_path);
}

static void z7ii_set_bundle(Object *obj, const char *value, Error **errp)
{
    Z7IIMachineState *s = Z7II_MACHINE(obj);

    g_free(s->bundle_path);
    s->bundle_path = g_strdup(value);
}

static char *z7ii_get_descriptor11_input(Object *obj, Error **errp)
{
    const char *value = Z7II_MACHINE(obj)->descriptor11_input;

    return g_strdup(value ? value : "");
}

static void z7ii_set_descriptor11_input(Object *obj, const char *value,
                                       Error **errp)
{
    Z7IIMachineState *s = Z7II_MACHINE(obj);

    if (phase_check(PHASE_MACHINE_INITIALIZED)) {
        error_setg(errp,
                   "descriptor11-input must be configured before machine init");
        return;
    }
    g_free(s->descriptor11_input);
    s->descriptor11_input = g_strdup(value && *value ? value : NULL);
}

static char *z7ii_get_descriptor12_input(Object *obj, Error **errp)
{
    const char *value = Z7II_MACHINE(obj)->descriptor12_input;

    return g_strdup(value ? value : "");
}

static void z7ii_set_descriptor12_input(Object *obj, const char *value,
                                       Error **errp)
{
    Z7IIMachineState *s = Z7II_MACHINE(obj);

    if (phase_check(PHASE_MACHINE_INITIALIZED)) {
        error_setg(errp,
                   "descriptor12-input must be configured before machine init");
        return;
    }
    g_free(s->descriptor12_input);
    s->descriptor12_input = g_strdup(value && *value ? value : NULL);
}

static char *z7ii_get_descriptor17_input(Object *obj, Error **errp)
{
    const char *value = Z7II_MACHINE(obj)->descriptor17_input;

    return g_strdup(value ? value : "");
}

static void z7ii_set_descriptor17_input(Object *obj, const char *value,
                                        Error **errp)
{
    Z7IIMachineState *s = Z7II_MACHINE(obj);

    if (phase_check(PHASE_MACHINE_INITIALIZED)) {
        error_setg(errp,
                   "descriptor17-input must be configured before machine init");
        return;
    }
    g_free(s->descriptor17_input);
    s->descriptor17_input = g_strdup(value && *value ? value : NULL);
}

static char *z7ii_get_descriptor18_input(Object *obj, Error **errp)
{
    const char *value = Z7II_MACHINE(obj)->descriptor18_input;

    return g_strdup(value ? value : "");
}

static void z7ii_set_descriptor18_input(Object *obj, const char *value,
                                       Error **errp)
{
    Z7IIMachineState *s = Z7II_MACHINE(obj);

    if (phase_check(PHASE_MACHINE_INITIALIZED)) {
        error_setg(errp,
                   "descriptor18-input must be configured before machine init");
        return;
    }
    g_free(s->descriptor18_input);
    s->descriptor18_input = g_strdup(value && *value ? value : NULL);
}

static char *z7ii_get_descriptor32_input(Object *obj, Error **errp)
{
    const char *value = Z7II_MACHINE(obj)->descriptor32_input;

    return g_strdup(value ? value : "");
}

static void z7ii_set_descriptor32_input(Object *obj, const char *value,
                                       Error **errp)
{
    Z7IIMachineState *s = Z7II_MACHINE(obj);

    if (phase_check(PHASE_MACHINE_INITIALIZED)) {
        error_setg(errp,
                   "descriptor32-input must be configured before machine init");
        return;
    }
    g_free(s->descriptor32_input);
    s->descriptor32_input = g_strdup(value && *value ? value : NULL);
}

static char *z7ii_get_sla_boot_prefix(Object *obj, Error **errp)
{
    const char *value = Z7II_MACHINE(obj)->sla_boot_prefix;

    return g_strdup(value ? value : "");
}

static void z7ii_set_sla_boot_prefix(Object *obj, const char *value, Error **errp)
{
    Z7IIMachineState *s = Z7II_MACHINE(obj);

    g_free(s->sla_boot_prefix);
    s->sla_boot_prefix = g_strdup(value);
}

static char *z7ii_get_mas_boot_prefix(Object *obj, Error **errp)
{
    const char *value = Z7II_MACHINE(obj)->mas_boot_prefix;

    return g_strdup(value ? value : "");
}

static char *z7ii_get_cmos_image(Object *obj, Error **errp)
{
    const char *value = Z7II_MACHINE(obj)->cmos_image;

    return g_strdup(value ? value : "");
}

static void z7ii_set_cmos_image(Object *obj, const char *value, Error **errp)
{
    Z7IIMachineState *s = Z7II_MACHINE(obj);

    g_free(s->cmos_image);
    s->cmos_image = g_strdup(value && *value ? value : NULL);
}

static void z7ii_set_mas_boot_prefix(Object *obj, const char *value, Error **errp)
{
    Z7IIMachineState *s = Z7II_MACHINE(obj);

    g_free(s->mas_boot_prefix);
    s->mas_boot_prefix = g_strdup(value);
}

static bool z7ii_get_frame_contract(Object *obj, Error **errp)
{
    return Z7II_MACHINE(obj)->frame_contract;
}

static void z7ii_set_frame_contract(Object *obj, bool value, Error **errp)
{
    Z7IIMachineState *s = Z7II_MACHINE(obj);

    if (phase_check(PHASE_MACHINE_INITIALIZED)) {
        error_setg(errp, "frame-contract must be configured before machine init");
        return;
    }
    s->frame_contract = value;
}

static void z7ii_finalize(Object *obj)
{
    Z7IIMachineState *s = Z7II_MACHINE(obj);

    nikon_bundle_free(s->bundle);
    timer_free(s->ex_input_timer);
    g_free(s->bundle_path);
    g_free(s->descriptor11_input);
    g_free(s->descriptor12_input);
    g_free(s->descriptor17_input);
    g_free(s->descriptor18_input);
    g_free(s->descriptor32_input);
    g_free(s->sla_boot_prefix);
    g_free(s->mas_boot_prefix);
    g_free(s->cmos_image);
}

static void z7ii_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    mc->desc = "Z7II unchanged MAS diagnostic (hypothetical Cortex-A7/MEMC)";
    mc->init = z7ii_init;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-a7");
    mc->default_cpus = 2; /* Original TSysPrNum/TKerPrNum defaults are two. */
    mc->min_cpus = 1; /* Small synthetic contracts explicitly use -smp 1. */
    mc->max_cpus = Z7II_MAX_CPUS + Z7II_SLA_CPUS + Z7II_EX_CPUS;
    mc->default_ram_size = 0;
    mc->no_parallel = true;
    mc->ignore_memory_transaction_failures = false;
    object_class_property_add_str(oc, "bundle", z7ii_get_bundle, z7ii_set_bundle);
    object_class_property_set_description(oc, "bundle",
        "Complete decoded five-component container (exclusive with -bios)");
    object_class_property_add_str(oc, "descriptor11-input",
                                  z7ii_get_descriptor11_input,
                                  z7ii_set_descriptor11_input);
    object_class_property_set_description(oc, "descriptor11-input",
        "Optional exact 2432 KiB primary NOR input at 0x12a80000; "
        "omitted bytes remain erased");
    object_class_property_add_str(oc, "descriptor12-input",
                                  z7ii_get_descriptor12_input,
                                  z7ii_set_descriptor12_input);
    object_class_property_set_description(oc, "descriptor12-input",
        "Optional exact 896 KiB primary NOR input at 0x12ce0000; "
        "omitted bytes remain erased");
    object_class_property_add_str(oc, "descriptor17-input",
                                  z7ii_get_descriptor17_input,
                                  z7ii_set_descriptor17_input);
    object_class_property_set_description(oc, "descriptor17-input",
        "Optional exact 128 KiB external NOR input at 0x132c0000; "
        "omitted bytes remain erased");
    object_class_property_add_str(oc, "descriptor18-input",
                                  z7ii_get_descriptor18_input,
                                  z7ii_set_descriptor18_input);
    object_class_property_set_description(oc, "descriptor18-input",
        "Optional exact 640 KiB external NOR input at 0x132e0000; "
        "omitted bytes remain erased");
    object_class_property_add_str(oc, "descriptor32-input",
                                  z7ii_get_descriptor32_input,
                                  z7ii_set_descriptor32_input);
    object_class_property_set_description(oc, "descriptor32-input",
        "Optional exact 1536 KiB shared descriptor31/32 NOR input at 0x13e80000; "
        "omitted bytes remain erased");
    object_class_property_add_str(oc, "sla-boot-prefix", z7ii_get_sla_boot_prefix,
                                 z7ii_set_sla_boot_prefix);
    object_class_property_set_description(oc, "sla-boot-prefix",
        "Optional SLA external 128 KiB input: erased or an exact-size file; "
        "omission leaves the range unavailable");
    object_class_property_add_str(oc, "mas-boot-prefix", z7ii_get_mas_boot_prefix,
                                 z7ii_set_mas_boot_prefix);
    object_class_property_set_description(oc, "mas-boot-prefix",
        "Independent MAS external 128 KiB input: erased or an exact-size file; "
        "omission leaves the range unavailable");
    object_class_property_add_str(oc, "cmos-image", z7ii_get_cmos_image,
                                  z7ii_set_cmos_image);
    object_class_property_set_description(oc, "cmos-image",
        "Explicit binary PGM/PPM image for the functional CMOS provider");
    object_class_property_add_bool(oc, "frame-contract", z7ii_get_frame_contract,
                                   z7ii_set_frame_contract);
    object_class_property_set_description(oc, "frame-contract",
        "Diagnostic-only CMOS/processing/display descriptor aperture; disabled by default");
}

static const TypeInfo z7ii_machine_type = {
    .name = TYPE_Z7II_MACHINE,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(Z7IIMachineState),
    .class_init = z7ii_class_init,
    .instance_finalize = z7ii_finalize,
    .interfaces = arm_machine_interfaces,
};


static void z7ii_register_types(void)
{
    type_register_static(&z7ii_machine_type);
}
type_init(z7ii_register_types)
