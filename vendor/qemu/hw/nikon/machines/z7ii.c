/*
 * Z7II MAS diagnostic machine. This is not a Nikon board hardware model.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/core/boards.h"
#include "hw/core/loader.h"
#include "hw/core/irq.h"
#include "hw/arm/machines-qom.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "hw/core/or-irq.h"
#include "hw/intc/arm_gic.h"
#include "system/address-spaces.h"
#include "system/reset.h"
#include "system/system.h"
#include "target/arm/cpu.h"
#include "hw/nikon/devices.h"

#define TYPE_Z7II_MACHINE MACHINE_TYPE_NAME("z7ii-mas-diagnostic")
OBJECT_DECLARE_SIMPLE_TYPE(Z7IIMachineState, Z7II_MACHINE)

#define MAS_BASE 0x10020000ULL
#define MAS_MAX_SIZE 0x2477b78ULL
#define STACK_SLOT_SIZE 0x4000
#define MEMC_BASE 0x04264000ULL
#define CLOCK_BASE 0x04281000ULL
#define COMPLETION_BASE 0x04287000ULL
#define CBAR_BASE 0xf8100000ULL
#define TIMER_HZ 48000000ULL
#define Z7II_MAX_CPUS 2

typedef struct Z7IINorSlot {
    MemoryRegion region;
    Z7IIMachineState *machine;
    hwaddr base;
} Z7IINorSlot;

struct Z7IIMachineState {
    MachineState parent_obj;
    ARMCPU cpu[Z7II_MAX_CPUS];
    unsigned num_cpus;
    MemoryRegion image;
    Z7IINorSlot nv_slot[5];
    MemoryRegion unknown;
    MemoryRegion copy_ram[2];
    MemoryRegion table_ram;
    MemoryRegion bss_ram;
    MemoryRegion bss_storage;
    MemoryRegion kernel_arena;
    MemoryRegion relocated_code;
    MemoryRegion module_buffer;
    MemoryRegion private_class;
    MemoryRegion stack_ram[6 * Z7II_MAX_CPUS];
    MemoryRegion temporary_stack;
    MemoryRegion early_release;
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

static MemTxResult z7ii_nor_write(void *opaque, hwaddr offset,
                                 uint64_t value, unsigned size,
                                 MemTxAttrs attrs)
{
    Z7IIMachineState *s = opaque;

    return nikon_memc_nor_access(s->memc, MAS_BASE + offset,
                                 size, true, value);
}

static MemTxResult z7ii_nor_read(void *opaque, hwaddr offset,
                                uint64_t *value, unsigned size,
                                MemTxAttrs attrs)
{
    Z7IIMachineState *s = opaque;

    /* Normal ROMD reads bypass this callback and read the original bytes. */
    *value = 0;
    return nikon_memc_nor_access(s->memc, MAS_BASE + offset,
                                 size, false, 0);
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

    /* Same MEMC protection and unsupported-command contract, correct PA. */
    return z7ii_nor_write(slot->machine, slot->base - MAS_BASE + offset,
                         value, size, attrs);
}

static MemTxResult z7ii_nv_read(void *opaque, hwaddr offset, uint64_t *value,
                              unsigned size, MemTxAttrs attrs)
{
    Z7IINorSlot *slot = opaque;

    return z7ii_nor_read(slot->machine, slot->base - MAS_BASE + offset,
                        value, size, attrs);
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
    /* Even entry means ARM state. No firmware instruction is changed. */
    for (i = 0; i < s->num_cpus; i++) {
        cpu_reset(CPU(&s->cpu[i]));
        cpu_set_pc(CPU(&s->cpu[i]), MAS_BASE);
    }
}

static void z7ii_ram(MemoryRegion *mr, const char *name,
                     hwaddr base, uint64_t size)
{
    /* Migratable board RAM uses NULL; its owner must not be a MachineState. */
    memory_region_init_ram(mr, NULL, name, size, &error_fatal);
    memory_region_add_subregion(get_system_memory(), base, mr);
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

static void z7ii_init(MachineState *machine)
{
    Z7IIMachineState *s = Z7II_MACHINE(machine);
    DeviceState *gic;
    DeviceState *mxic;
    DeviceState *gpio;
    DeviceState *exiu;
    SysBusDevice *gic_bus;
    int64_t image_size;
    unsigned i;
    static const hwaddr stack_top[] = {
        0xb2bb0000, 0xb2ba8000, 0xb2ba0000,
        0xb2b98000, 0xb2b90000, 0xb2b88000,
    };

    if (strcmp(machine->cpu_type, ARM_CPU_TYPE_NAME("cortex-a7"))) {
        error_report("z7ii-mas-diagnostic currently requires Cortex-A7");
        exit(EXIT_FAILURE);
    }
    if (!machine->firmware) {
        error_report("z7ii-mas-diagnostic requires -bios <raw MAS payload>");
        exit(EXIT_FAILURE);
    }
    image_size = get_image_size(machine->firmware, &error_fatal);
    if (image_size <= 0 || (uint64_t)image_size > MAS_MAX_SIZE) {
        error_report("MAS diagnostic image must contain 1..%llu bytes",
                     (unsigned long long)MAS_MAX_SIZE);
        exit(EXIT_FAILURE);
    }

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
    if (load_image_targphys(machine->firmware, MAS_BASE, image_size,
                            &error_fatal) != image_size) {
        error_report("failed to load the complete MAS diagnostic image");
        exit(EXIT_FAILURE);
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
    }

    /* Static MAS copy contracts: destinations only; guest performs the copy. */
    z7ii_ram(&s->copy_ram[0], "z7ii.copy0", 0xb1800000, 0xaedb8);
    z7ii_ram(&s->copy_ram[1], "z7ii.copy1", 0xb2bb0000, 0x105c);
    /* Explicit byte-block fill profile: count 0x4ac * width 0x4000. */
    /* Preserve RAM/guest page-offset congruence without mapping padding. */
    memory_region_init_ram(&s->bss_storage, NULL, "z7ii.fill-bss-storage",
                           0x12b0dc0, &error_fatal);
    memory_region_init_alias(&s->bss_ram, NULL, "z7ii.fill-bss",
                             &s->bss_storage, 0xdc0, 0x12b0000);
    memory_region_add_subregion(get_system_memory(), 0xb18aedc0, &s->bss_ram);
    /* Original descriptor lowmem bounds, not a physical DDR capacity. */
    z7ii_ram(&s->kernel_arena, "z7ii.kernel-lowmem", 0xb2c00000, 0x3300000);
    /*
     * Original address-class initializer declares [baa00000,bb300000).
     * This finite virtual capacity includes the earlier kernel destination.
     * The guest owns code copies, aliases and PTE updates; no source is seeded.
     */
    z7ii_ram(&s->relocated_code, "z7ii.relocated-code", 0xbaa00000, 0x900000);
    /*
     * Four original bump requests observed from reset: 0x3a0000, 0x31f28,
     * 6 and 0xa410 bytes, each aligned to 64 bytes by the guest allocator.
     * Their contiguous union is finite virtual backing, not heap capacity.
     * The guest owns all copies, allocation pointers and object metadata.
     */
    z7ii_ram(&s->module_buffer, "z7ii.module-buffer", 0xb7d00000, 0x3dc3c0);
    /*
     * Original address-class pair: [14000000,15e00000) <-> [b5f00000,b7d00000).
     * This declared 30MiB virtual storage includes the original message ring.
     * Cold zero is a functional assumption, not physical heap capacity.
     * CPU logical views use the guest MMU, without a new physical bus alias.
     * Guest objects, ring indices and signatures are untouched.
     */
    z7ii_ram(&s->private_class, "z7ii.private-class", 0xb5f00000, 0x1e00000);
    z7ii_ram(&s->table_ram, "z7ii.table", 0xb2b74000, 0x4000);
    /* O394 + O4c6c + O5bd0/O5f40 require 8 + 40 + 40 bytes. */
    z7ii_ram(&s->temporary_stack, "z7ii.temporary-stack", 0x04008ea8, 0x58);
    /* CPU0 O0xd848 produces the magic; secondary WFE/read/clear is guest code. */
    z7ii_ram(&s->early_release, "z7ii.early-release", 0x04008ffc, 4);
    /* Each guest SP is top - (MPIDR selector << 14). The 16KiB backing
     * per mode/core is a declared virtual capacity, not proven by that stride. */
    for (i = 0; i < machine->smp.cpus * ARRAY_SIZE(stack_top); i++) {
        unsigned core = i / ARRAY_SIZE(stack_top);
        unsigned mode = i % ARRAY_SIZE(stack_top);
        g_autofree char *name = g_strdup_printf("z7ii.cpu%u-stack%u", core, mode);
        z7ii_ram(&s->stack_ram[i], name,
                 stack_top[mode] - (core + 1) * STACK_SLOT_SIZE, STACK_SLOT_SIZE);
    }

    {
        DeviceState *config = qdev_new(TYPE_NIKON_MXIC_CONFIG);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(config), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(config), 0, 0x20010000);
        sysbus_mmio_map(SYS_BUS_DEVICE(config), 1, 0x24074000);
    }
    mxic = sysbus_create_simple(TYPE_NIKON_MXIC, 0x20016000, NULL);
    nikon_mxic_attach_ram(mxic, &s->copy_ram[0], 0xb1800000);
    nikon_mxic_attach_ram(mxic, &s->copy_ram[1], 0xb2bb0000);
    nikon_mxic_attach_ram(mxic, &s->bss_ram, 0xb18aedc0);
    nikon_mxic_attach_ram(mxic, &s->kernel_arena, 0xb2c00000);
    nikon_mxic_attach_ram(mxic, &s->relocated_code, 0xbaa00000);
    nikon_mxic_attach_ram(mxic, &s->module_buffer, 0xb7d00000);
    nikon_mxic_attach_ram(mxic, &s->private_class, 0xb5f00000);
    nikon_mxic_attach_ram(mxic, &s->table_ram, 0xb2b74000);
    for (i = 0; i < machine->smp.cpus * ARRAY_SIZE(stack_top); i++) {
        nikon_mxic_attach_ram(mxic, &s->stack_ram[i],
                            stack_top[i % ARRAY_SIZE(stack_top)] -
                            (i / ARRAY_SIZE(stack_top) + 1) * STACK_SLOT_SIZE);
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
    gpio = sysbus_create_simple(TYPE_NIKON_GPIO, 0x04282000, NULL);
    {
        DeviceState *usio = qdev_new(TYPE_NIKON_USIO);

        if (serial_hd(0)) {
            qdev_prop_set_chr(usio, "chardev", serial_hd(0));
        }
        sysbus_realize_and_unref(SYS_BUS_DEVICE(usio), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(usio), 0, 0x2c090000);
    }
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
    {
        DeviceState *router = qdev_new(TYPE_NIKON_PERIPHERAL_EVENT_ROUTER);

        sysbus_realize_and_unref(SYS_BUS_DEVICE(router), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(router), 0, 0x20060000);
        for (i = 0; i < 3; i++) {
            sysbus_connect_irq(SYS_BUS_DEVICE(router), i,
                               qdev_get_gpio_in(gic, 86 + i - 32));
        }
    }
    sysbus_create_simple(TYPE_NIKON_BRIDGE, 0x20130000,
                         qdev_get_gpio_in(gic, 127 - 32));
    sysbus_create_simple(TYPE_NIKON_IPUD, 0x24020000,
                         qdev_get_gpio_in(gic, 101 - 32));
    sysbus_create_simple(TYPE_NIKON_STAGED_CONFIGURATION, 0x20202000,
                         qdev_get_gpio_in(gic, 123 - 32));
    sysbus_create_simple(TYPE_NIKON_EVENT_GATE, 0x20100000,
                         qdev_get_gpio_in(gic, 102 - 32));
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
        DeviceState *control = sysbus_create_simple(TYPE_NIKON_DUAL_EVENT_BANKS,
                              0x20140000, qdev_get_gpio_in(gic, 111 - 32));
        static const hwaddr addresses[] = {
            0x20144000, 0x2014b000, 0x2014d000, 0x2014e000,
        };

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
    for (i = 0; i < 11; i++) {
        DeviceState *spi = qdev_new(TYPE_NIKON_SPI);

        qdev_prop_set_uint32(spi, "channel", i);
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

        qdev_prop_set_uint32(i2c, "instance", i);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(i2c), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(i2c), 0, 0x2c018000 + i * 0x1000);
        sysbus_connect_irq(SYS_BUS_DEVICE(i2c), 0,
                           qdev_get_gpio_in(gic, 214 + i - 32));
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
    qemu_register_reset(z7ii_reset, s);
    error_report("z7ii-mas-diagnostic: assumptions cpu=%s cpus=%u "
                 "image-base=0x10020000 entry=0x10020000 ARM "
                 "image-bytes=%" PRId64 " per-core-mode-stack-slot=0x4000; "
                 "MEMC=0x04264000 profile-only CBAR=0xf8100000 GICv1=256 "
                 "cntfrq=48000000 secure-timer-PPI=29 early-release-RAM=4B; "
                 "six modes/core, virtual stack capacity; "
                 "no NOR writer/IPC devices/BootROM",
                 machine->cpu_type, s->num_cpus, image_size);
}

static void z7ii_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    mc->desc = "Z7II unchanged MAS diagnostic (hypothetical Cortex-A7/MEMC)";
    mc->init = z7ii_init;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-a7");
    mc->default_cpus = 2; /* Original TSysPrNum/TKerPrNum defaults are two. */
    mc->min_cpus = 1; /* Small synthetic contracts explicitly use -smp 1. */
    mc->max_cpus = Z7II_MAX_CPUS;
    mc->default_ram_size = 0;
    mc->ignore_memory_transaction_failures = false;
}

static const TypeInfo z7ii_machine_type = {
    .name = TYPE_Z7II_MACHINE,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(Z7IIMachineState),
    .class_init = z7ii_class_init,
    .interfaces = arm_machine_interfaces,
};


static void z7ii_register_types(void)
{
    type_register_static(&z7ii_machine_type);
}
type_init(z7ii_register_types)
