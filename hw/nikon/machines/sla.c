/*
 * Nikon SLA loaded ARM software domain, independent of the MAS address space.
 * Firmware owns its MMU, banked SPs, COPY/ZERO and secondary-core release.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/core/boards.h"
#include "hw/arm/machines-qom.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "hw/intc/arm_gic.h"
#include "hw/nikon/bundle.h"
#include "hw/nikon/devices.h"
#include "hw/nikon/sla.h"
#include "system/address-spaces.h"
#include "system/reset.h"
#include "system/system.h"
#include "target/arm/cpu.h"

#define TYPE_NIKON_SLA_MACHINE MACHINE_TYPE_NAME("nikonsla")
OBJECT_DECLARE_SIMPLE_TYPE(NikonSLAMachine, NIKON_SLA_MACHINE)

#define SLA_MAX_CPUS 2
#define SLA_CBAR 0xf8100000ULL
#define SLA_TIMER_HZ 48000000ULL
#define SLA_STACK_SIZE 0x4000
#define SLA_RAM_COUNT (6 * SLA_MAX_CPUS + 7)

struct NikonSLAMachine {
    MachineState parent_obj;
    ARMCPU cpu[SLA_MAX_CPUS];
    unsigned num_cpus;
    MemoryRegion image, uncovered;
    MemoryRegion ram[SLA_RAM_COUNT], storage[SLA_RAM_COUNT];
    unsigned ram_count;
    DeviceState *memc;
    NikonBundle *bundle;
    GBytes *payload;
    bool reported_error;
};

static MemTxResult sla_uncovered_read(void *opaque, hwaddr addr,
                                     uint64_t *value, unsigned size,
                                     MemTxAttrs attrs)
{
    NikonSLAMachine *s = opaque;

    nikon_access_error(&s->reported_error, addr, size, false, 0,
                       "SLA uncovered input");
    *value = 0; /* Invalid output: the transaction fails. */
    return MEMTX_ERROR;
}

static MemTxResult sla_uncovered_write(void *opaque, hwaddr addr,
                                      uint64_t value, unsigned size,
                                      MemTxAttrs attrs)
{
    NikonSLAMachine *s = opaque;

    nikon_access_error(&s->reported_error, addr, size, true, value,
                       "SLA uncovered output");
    return MEMTX_ERROR;
}

static const MemoryRegionOps sla_uncovered_ops = {
    .read_with_attrs = sla_uncovered_read,
    .write_with_attrs = sla_uncovered_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
};

static MemTxResult sla_rom_read(void *opaque, hwaddr offset, uint64_t *value,
                               unsigned size, MemTxAttrs attrs)
{
    NikonSLAMachine *s = opaque;

    *value = 0;
    return nikon_memc_nor_access(s->memc, NIKON_SLA_ROM_BASE + offset,
                                 size, false, 0);
}

static MemTxResult sla_rom_write(void *opaque, hwaddr offset, uint64_t value,
                                unsigned size, MemTxAttrs attrs)
{
    NikonSLAMachine *s = opaque;

    return nikon_memc_nor_access(s->memc, NIKON_SLA_ROM_BASE + offset,
                                 size, true, value);
}

static const MemoryRegionOps sla_rom_ops = {
    .read_with_attrs = sla_rom_read,
    .write_with_attrs = sla_rom_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
};

static void sla_reset(void *opaque)
{
    NikonSLAMachine *s = opaque;
    unsigned i;

    s->reported_error = false;
    for (i = 0; i < s->num_cpus; i++) {
        cpu_reset(CPU(&s->cpu[i]));
        cpu_set_pc(CPU(&s->cpu[i]), NIKON_SLA_ENTRY);
    }
}

static void sla_ram(NikonSLAMachine *s, DeviceState *mxic, const char *name,
                    hwaddr base, uint64_t size)
{
    unsigned index = s->ram_count++;
    unsigned page_offset = base & 0xfff;
    g_autofree char *storage_name = g_strdup_printf("%s.storage", name);

    assert(index < SLA_RAM_COUNT);
    memory_region_init_ram(&s->storage[index], NULL, storage_name,
                           size + page_offset, &error_fatal);
    memory_region_init_alias(&s->ram[index], NULL, name, &s->storage[index],
                             page_offset, size);
    memory_region_add_subregion(get_system_memory(), base, &s->ram[index]);
    if (mxic) {
        nikon_mxic_attach_ram(mxic, &s->ram[index], base);
    }
}

static void sla_init(MachineState *machine)
{
    NikonSLAMachine *s = NIKON_SLA_MACHINE(machine);
    DeviceState *mxic, *gic, *fill;
    SysBusDevice *gic_bus;
    const void *payload;
    size_t size;
    unsigned i;
    static const hwaddr stack_top[] = {
        0xc1920000, 0xc1918000, 0xc1910000,
        0xc1908000, 0xc1900000, 0xc18f8000,
    };

    if (strcmp(machine->cpu_type, ARM_CPU_TYPE_NAME("cortex-a7"))) {
        error_report("nikonsla requires the functional Cortex-A7 profile");
        exit(EXIT_FAILURE);
    }
    if (!machine->firmware) {
        error_report("nikonsla requires -bios <complete decoded-container.bin>");
        exit(EXIT_FAILURE);
    }
    s->bundle = nikon_bundle_load(machine->firmware, &error_fatal);
    s->payload = nikon_bundle_component(s->bundle, NIKON_COMPONENT_SLA);
    payload = g_bytes_get_data(s->payload, &size);
    if (size != NIKON_SLA_ROM_SIZE) {
        error_report("nikonsla requires the complete C1.70 SLA component");
        exit(EXIT_FAILURE);
    }

    memory_region_init_io(&s->uncovered, OBJECT(s), &sla_uncovered_ops, s,
                          "nikon.sla.uncovered", 1ULL << 32);
    memory_region_add_subregion_overlap(get_system_memory(), 0,
                                        &s->uncovered, -1);
    s->memc = sysbus_create_simple(TYPE_NIKON_MEMC, 0x04264000, NULL);
    memory_region_init_rom_device(&s->image, NULL, &sla_rom_ops, s,
                                  "nikon.sla.image", size, &error_fatal);
    memcpy(memory_region_get_ram_ptr(&s->image), payload, size);
    memory_region_add_subregion(get_system_memory(), NIKON_SLA_ROM_BASE,
                                &s->image);

    mxic = sysbus_create_simple(TYPE_NIKON_MXIC, 0x20016000, NULL);
    /* Original COPY targets only: no initialized resources are host copied. */
    sla_ram(s, mxic, "nikon.sla.copy1", NIKON_SLA_COPY1_BASE,
            NIKON_SLA_COPY1_SIZE);
    sla_ram(s, mxic, "nikon.sla.copy2", NIKON_SLA_COPY2_BASE,
            NIKON_SLA_COPY2_SIZE);
    /* Original fill rounds 0x8b71d4 bytes up to 0x4000-byte blocks. */
    sla_ram(s, mxic, "nikon.sla.bss", 0xc1017d80, 0x8b8000);
    /* Original secondary table COPY and startup caller range. */
    sla_ram(s, mxic, "nikon.sla.table", 0xc18e4000, 0x4000);
    /* Source config bounds consumed by the native kernel arena initializer. */
    sla_ram(s, mxic, "nikon.sla.kernel-arena", 0xc1940000, 0x36c0000);
    sla_ram(s, NULL, "nikon.sla.temporary-stack", 0x04008e00, 0x100);
    sla_ram(s, NULL, "nikon.sla.release", 0x04008ffc, 4);
    s->num_cpus = machine->smp.cpus;
    for (i = 0; i < s->num_cpus; i++) {
        unsigned mode;
        g_autofree char *name = g_strdup_printf("cpu%u", i);

        for (mode = 0; mode < ARRAY_SIZE(stack_top); mode++) {
            g_autofree char *stack = g_strdup_printf("nikon.sla.cpu%u.stack%u",
                                                    i, mode);
            sla_ram(s, mxic, stack, stack_top[mode] - (i + 1) * SLA_STACK_SIZE,
                    SLA_STACK_SIZE);
        }
        object_initialize_child(OBJECT(s), name, &s->cpu[i], machine->cpu_type);
        qdev_prop_set_uint64(DEVICE(&s->cpu[i]), "reset-cbar", SLA_CBAR);
        qdev_prop_set_uint64(DEVICE(&s->cpu[i]), "cntfrq", SLA_TIMER_HZ);
        qdev_realize(DEVICE(&s->cpu[i]), NULL, &error_fatal);
    }

    sysbus_create_simple(TYPE_NIKON_CLOCK_GATES, 0x04281000, NULL);
    sysbus_create_simple(TYPE_NIKON_SYSCONFIG, 0x04280000, NULL);
    sysbus_create_simple(TYPE_NIKON_GPIO, 0x04282000, NULL);
    /* COPY1 c1000000 supplies the counter used by source 1002012c/10020158. */
    sysbus_create_simple(TYPE_NIKON_TIMESTAMP, 0x04266000, NULL);
    {
        DeviceState *usio = qdev_new(TYPE_NIKON_USIO);

        /* Callback 10020294 supplies this port to 10020180/100201ec. */
        if (serial_hd(0)) {
            qdev_prop_set_chr(usio, "chardev", serial_hd(0));
        }
        sysbus_realize_and_unref(SYS_BUS_DEVICE(usio), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(usio), 0, 0x2c090000);
        /* These original callees poll TX; no SLA UART IRQ route is proven. */
    }
    {
        DeviceState *config = qdev_new(TYPE_NIKON_MXIC_CONFIG);

        sysbus_realize_and_unref(SYS_BUS_DEVICE(config), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(config), 0, 0x20010000);
        sysbus_mmio_map(SYS_BUS_DEVICE(config), 1, 0x24074000);
    }
    for (i = 0; i < 2; i++) {
        DeviceState *sdi = qdev_new(TYPE_NIKON_SDI);

        qdev_prop_set_uint32(sdi, "bank", i);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(sdi), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(sdi), 0, 0x20020000 + i * 0x20000);
    }

    gic = qdev_new(TYPE_ARM_GIC);
    gic_bus = SYS_BUS_DEVICE(gic);
    qdev_prop_set_uint32(gic, "num-cpu", s->num_cpus);
    qdev_prop_set_uint32(gic, "num-irq", 256);
    qdev_prop_set_uint32(gic, "revision", 1);
    qdev_prop_set_bit(gic, "has-security-extensions", true);
    qdev_prop_set_bit(gic, "has-virtualization-extensions", false);
    sysbus_realize_and_unref(gic_bus, &error_fatal);
    sysbus_mmio_map(gic_bus, 0, SLA_CBAR + 0x1000);
    sysbus_mmio_map(gic_bus, 1, SLA_CBAR + 0x2000);
    fill = sysbus_create_simple(TYPE_NIKON_FILL, 0x20070000,
                                qdev_get_gpio_in(gic, 103 - 32));
    sysbus_connect_irq(SYS_BUS_DEVICE(fill), 1,
                       qdev_get_gpio_in(gic, 104 - 32));
    for (i = 0; i < s->num_cpus; i++) {
        DeviceState *cpu = DEVICE(&s->cpu[i]);
        const int timer_irq[] = {
            [GTIMER_PHYS] = 30, [GTIMER_VIRT] = 27,
            [GTIMER_HYP] = 26, [GTIMER_SEC] = 29,
        };
        unsigned irq;

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
    qemu_register_reset(sla_reset, s);
    error_report("nikonsla: loaded C1.70 SLA ARM entry=1002bcd4; "
                 "functional Cortex-A7 cpus=%u CBAR=f8100000 GICv1=256 "
                 "cntfrq=48000000; virtual stack slots=16KiB "
                 "temporary caller stack=256B; guest MMU/COPY/ZERO/release; "
                 "no BootROM, calibration input or MAS peer ACK supplied",
                 s->num_cpus);
}

static void sla_finalize(Object *object)
{
    NikonSLAMachine *s = NIKON_SLA_MACHINE(object);

    g_clear_pointer(&s->payload, g_bytes_unref);
    nikon_bundle_free(s->bundle);
}

static void sla_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Nikon SLA loaded ARM software domain";
    mc->init = sla_init;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-a7");
    mc->default_ram_size = 0;
    mc->max_cpus = SLA_MAX_CPUS;
    mc->ignore_memory_transaction_failures = false;
}

static const TypeInfo sla_machine = {
    .name = TYPE_NIKON_SLA_MACHINE,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(NikonSLAMachine),
    .instance_finalize = sla_finalize,
    .class_init = sla_class_init,
    .interfaces = arm_machine_interfaces,
};

static void sla_register_types(void)
{
    type_register_static(&sla_machine);
}
type_init(sla_register_types)
