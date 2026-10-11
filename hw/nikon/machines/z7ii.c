/*
 * Z7II MAS diagnostic machine. This is not a Nikon board hardware model.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/cutils.h"
#include "hw/core/boards.h"
#include "hw/core/loader.h"
#include "hw/core/irq.h"
#include "hw/core/qdev.h"
#include "hw/arm/machines-qom.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "hw/core/or-irq.h"
#include "hw/intc/arm_gic.h"
#include "system/address-spaces.h"
#include "system/reset.h"
#include "system/system.h"
#include "system/tcg.h"
#include "target/arm/cpu.h"
#include "hw/nikon/devices.h"
#include "hw/nikon/bundle.h"
#include "hw/nikon/storage.h"
#include "hw/nikon/processing.h"
#include "hw/nikon/sensor.h"
#include "hw/nikon/cmos.h"
#include "hw/nikon/frame.h"
#include "hw/nikon/sla.h"
#include "hw/nikon/bootshim.h"
#include "hw/nikon/ex.h"
#include "hw/nikon/input.h"
#include "hw/ssi/ssi.h"
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
#define Z7II_DOWNLOAD_BASE 0xb0d00000ULL
#define Z7II_DOWNLOAD_SIZE 0x400000ULL
#define Z7II_SELECTOR_ONE_BASE 0xb0900000ULL
#define Z7II_SELECTOR_ONE_SIZE 0x400000ULL

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

typedef struct Z7IINorSlot {
    MemoryRegion region;
    Z7IIMachineState *machine;
    hwaddr base;
} Z7IINorSlot;

struct Z7IIMachineState {
    MachineState parent_obj;
    ARMCPU cpu[Z7II_MAX_CPUS];
    unsigned num_cpus;
    char *bundle_path;
    char *boot_shim;
    char *sla_boot_prefix;
    char *mas_boot_prefix;
    char *ex_adc_counts;
    int ex_adc_sample[4];
    uint8_t ex_adc_mask;
    bool frame_contract;
    NikonBundle *bundle;
    MemoryRegion image;
    Z7IINorSlot nv_slot[5];
    Z7IINorSlot boot_prefix;
    MemoryRegion unknown;
    MemoryRegion storage[ARRAY_SIZE(z7ii_storage)];
    /* Selector-zero download buffer consumed by the RMU peer contract. */
    MemoryRegion download;
    /* Selector-one packed download arena; independent from selector zero. */
    MemoryRegion selector_one_download;
    MemoryRegion temporary_stack;
    MemoryRegion early_release;
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
    return nikon_memc_nor_access(slot->machine->memc, slot->base + offset,
                                 size, true, value);
}

static MemTxResult z7ii_nv_read(void *opaque, hwaddr offset, uint64_t *value,
                              unsigned size, MemTxAttrs attrs)
{
    Z7IINorSlot *slot = opaque;

    *value = 0; /* ROMD serves valid reads; this path preserves MEMC errors. */
    return nikon_memc_nor_access(slot->machine->memc, slot->base + offset,
                                 size, false, 0);
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
}

static bool z7ii_functional_shim(Z7IIMachineState *s)
{
    return nikon_bootshim_is_functional(s->boot_shim);
}

static void z7ii_prepare_functional_shim(Z7IIMachineState *s)
{
    /*
     * functional-v1 is a replaceable loader contract.  It supplies only
     * package-external FF NOR inputs and the finite early copy destinations
     * observed in the unchanged bundle trace.  The guest still owns CP15,
     * TTBR, SMP release, IRQ, DMA completion and every display operation.
     */
    if (!z7ii_functional_shim(s)) {
        return;
    }
    if (!s->mas_boot_prefix) {
        s->mas_boot_prefix = g_strdup("erased");
    }
    if (!s->sla_boot_prefix) {
        s->sla_boot_prefix = g_strdup("erased");
    }
    nikon_bootshim_prepare_mas(s->boot_shim, get_system_memory(),
                               s->shim_mas, &error_fatal);
    error_report("z7ii-mas-diagnostic: boot-shim=functional-v1; erased prefixes "
                 "and finite early-copy views are functional assumptions; "
                 "CP15, SMP release, IRQ, DMA completion and pixels remain guest-owned");
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
    nikon_ex_set_firmware(ex, payload, &error_fatal);
    for (i = 0; i < ARRAY_SIZE(s->ex_adc_sample); i++) {
        if (s->ex_adc_mask & (1u << i)) {
            nikon_ex_adc_input(ex, 0, i, s->ex_adc_sample[i]);
        }
    }
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

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
}

static void z7ii_init(MachineState *machine)
{
    Z7IIMachineState *s = Z7II_MACHINE(machine);
    DeviceState *gic;
    DeviceState *mxic;
    DeviceState *gpio;
    DeviceState *exiu;
    DeviceState *bridge;
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
    if (s->ex_adc_mask && !s->bundle_path) {
        error_report("ex-adc-counts requires a complete bundle and private EX");
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
    z7ii_prepare_functional_shim(s);
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
        nikon_mxic_attach_ram(mxic, &s->storage[i], z7ii_storage[i].base);
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
        DeviceState *router = qdev_new(TYPE_NIKON_PERIPHERAL_EVENT_ROUTER);

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
        /* Guest configuration/commands; no default completion provider. */
    }
    {
        DeviceState *capture = qdev_new(TYPE_NIKON_CAPTURE_CONTROL);

        object_property_add_child(OBJECT(machine), "capture-control",
                                  OBJECT(capture));
        sysbus_realize_and_unref(SYS_BUS_DEVICE(capture), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(capture), 0, 0x20068000);
        for (i = 0; i < NIKON_CAPTURE_EVENTS; i++) {
            sysbus_connect_irq(SYS_BUS_DEVICE(capture), i,
                               qdev_get_gpio_in(gic, 94 + i - 32));
        }
        /* Original status/command interface; external publication is unbound. */
    }
    {
        DeviceState *readout = qdev_new(TYPE_NIKON_READOUT_CONTROL);
        static const hwaddr addresses[NIKON_READOUT_REGIONS] = {
            [NIKON_READOUT_BUFFERS] = 0x20069800,
            [NIKON_READOUT_PARAMETERS] = 0x2006b000,
        };

        object_property_add_child(OBJECT(machine), "readout-control",
                                  OBJECT(readout));
        sysbus_realize_and_unref(SYS_BUS_DEVICE(readout), &error_fatal);
        for (i = 0; i < NIKON_READOUT_REGIONS; i++) {
            sysbus_mmio_map(SYS_BUS_DEVICE(readout), i, addresses[i]);
        }
        /* Guest requests have no default provider, IRQ or pixel data source. */
    }
    {
        DeviceState *cmos = qdev_new(TYPE_NIKON_CMOS);

        object_property_add_child(OBJECT(machine), "cmos", OBJECT(cmos));
        sysbus_realize_and_unref(SYS_BUS_DEVICE(cmos), &error_fatal);
        /* Diagnostic transport aperture, with no C1.70 physical MMIO claim. */
        sysbus_mmio_map(SYS_BUS_DEVICE(cmos), 0, 0x24080000);
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
        };

        control->id = g_strdup("display");
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
                                            boot_prefix, s->boot_shim,
                                            Z7II_SLA_CPUS, NULL,
                                            &error_fatal);
        DeviceState *sla_bridge = nikon_sla_bridge(sla);

        /* Source-complementary handshake; the board trace is not recovered. */
        qdev_connect_gpio_out_named(sla, "peer-ready", 0,
                                   qdev_get_gpio_in_named(gpio, "external-level",
                                                          6 * 16 + 3));
        nikon_bridge_attach_peer(bridge, 1, nikon_sla_address_space(sla));
        nikon_bridge_attach_peer(sla_bridge, 1, &address_space_memory);
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
                 "MEMC=0x04264000 profile-only CBAR=0xf8100000 GICv1=256 "
                 "cntfrq=48000000 secure-timer-PPI=29 early-release-RAM=4B; "
                 "storage-classes=6 storage-MiB=186 cold-zero; "
                 "no NOR writer/BootROM; private SLA2/EX1 for bundle input",
                 machine->cpu_type, s->num_cpus, image_size,
                 s->bundle ? "complete-decoded-container" : "raw-MAS");
}

static char *z7ii_get_bundle(Object *obj, Error **errp)
{
    return g_strdup(Z7II_MACHINE(obj)->bundle_path);
}

static char *z7ii_get_boot_shim(Object *obj, Error **errp)
{
    const char *value = Z7II_MACHINE(obj)->boot_shim;

    return g_strdup(value ? value : "none");
}

static void z7ii_set_boot_shim(Object *obj, const char *value, Error **errp)
{
    Z7IIMachineState *s = Z7II_MACHINE(obj);

    if (!value || !*value || !strcmp(value, "none") ||
        !strcmp(value, "functional-v1")) {
        g_free(s->boot_shim);
        s->boot_shim = g_strdup(value && *value ? value : "none");
        return;
    }
    error_setg(errp, "boot-shim must be none or functional-v1");
}

static void z7ii_set_bundle(Object *obj, const char *value, Error **errp)
{
    Z7IIMachineState *s = Z7II_MACHINE(obj);

    g_free(s->bundle_path);
    s->bundle_path = g_strdup(value);
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

static void z7ii_set_mas_boot_prefix(Object *obj, const char *value, Error **errp)
{
    Z7IIMachineState *s = Z7II_MACHINE(obj);

    g_free(s->mas_boot_prefix);
    s->mas_boot_prefix = g_strdup(value);
}

static char *z7ii_get_ex_adc_counts(Object *obj, Error **errp)
{
    const char *value = Z7II_MACHINE(obj)->ex_adc_counts;

    return g_strdup(value ? value : "");
}

static void z7ii_set_ex_adc_counts(Object *obj, const char *value, Error **errp)
{
    Z7IIMachineState *s = Z7II_MACHINE(obj);
    g_auto(GStrv) entries = g_strsplit(value, ";", -1);
    int samples[4] = { 0 };
    uint8_t mask = 0;

    if (phase_check(PHASE_MACHINE_INITIALIZED)) {
        error_setg(errp, "ex-adc-counts must be configured before machine init");
        return;
    }
    for (unsigned i = 0; entries[i] && value[0]; i++) {
        g_auto(GStrv) pair = g_strsplit(entries[i], ":", -1);
        int channel, sample;

        if (g_strv_length(pair) != 2 ||
            qemu_strtoi(pair[0], NULL, 10, &channel) ||
            qemu_strtoi(pair[1], NULL, 10, &sample) ||
            (channel != 0 && channel != 1 && channel != 3) ||
            sample < 0 || sample > 4095 || (mask & (1u << channel))) {
            error_setg(errp, "ex-adc-counts expects unique channel:raw-count "
                       "entries separated by ';'; channels 0,1,3; counts 0..4095");
            return;
        }
        samples[channel] = sample;
        mask |= 1u << channel;
    }
    memcpy(s->ex_adc_sample, samples, sizeof(samples));
    s->ex_adc_mask = mask;
    g_free(s->ex_adc_counts);
    s->ex_adc_counts = g_strdup(value);
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
    g_free(s->bundle_path);
    g_free(s->sla_boot_prefix);
    g_free(s->mas_boot_prefix);
    g_free(s->ex_adc_counts);
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
    object_class_property_add_str(oc, "boot-shim", z7ii_get_boot_shim,
                                  z7ii_set_boot_shim);
    object_class_property_set_description(oc, "boot-shim",
        "Versioned functional loader profile: none or functional-v1");
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
    object_class_property_add_str(oc, "ex-adc-counts", z7ii_get_ex_adc_counts,
                                 z7ii_set_ex_adc_counts);
    object_class_property_set_description(oc, "ex-adc-counts",
        "Explicit functional ADC1 channel:raw-count entries separated by ';' "
        "(channels 0,1,3; counts 0..4095); omitted channels remain disconnected");
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
