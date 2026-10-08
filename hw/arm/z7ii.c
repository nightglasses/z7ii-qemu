/*
 * Z7II MAS diagnostic machine. This is not a Nikon board hardware model.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "hw/core/boards.h"
#include "hw/core/loader.h"
#include "hw/arm/machines-qom.h"
#include "hw/core/qdev-properties.h"
#include "hw/cpu/a15mpcore.h"
#include "system/address-spaces.h"
#include "system/reset.h"
#include "system/runstate.h"
#include "target/arm/cpu.h"

#define TYPE_Z7II_MACHINE MACHINE_TYPE_NAME("z7ii-mas-diagnostic")
OBJECT_DECLARE_SIMPLE_TYPE(Z7IIMachineState, Z7II_MACHINE)

#define MAS_BASE 0x10020000ULL
#define MAS_MAX_SIZE 0x2477b78ULL
#define STACK_SLOT_SIZE 0x4000
#define MEMC_BASE 0x04264000ULL
#define CBAR_BASE 0x1d000000ULL
#define TIMER_HZ 48000000ULL
#define Z7II_MAX_CPUS 2

#define TYPE_Z7II_MEMC "z7ii-memc-diagnostic"
OBJECT_DECLARE_SIMPLE_TYPE(Z7IIMemcState, Z7II_MEMC)

struct Z7IIMemcState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    uint32_t mode;
    uint32_t timing;
    bool mode_initialized;
    bool timing_initialized;
    bool reported_error;
};

struct Z7IIMachineState {
    MachineState parent_obj;
    ARMCPU cpu[Z7II_MAX_CPUS];
    unsigned num_cpus;
    MemoryRegion image;
    MemoryRegion unknown;
    MemoryRegion copy_ram[2];
    MemoryRegion table_ram;
    MemoryRegion stack_ram[6];
    MemoryRegion temporary_stack;
    MemoryRegion early_release;
    MemoryRegion cpu1_startup_stack;
    Z7IIMemcState memc;
    bool reported_unknown;
};

static void z7ii_access_error(bool *reported, hwaddr addr, unsigned size,
                              bool write, uint64_t value, const char *reason)
{
    CPUState *cpu = current_cpu;
    g_autofree char *message = NULL;

    /* Debugger/monitor probes must not become guest execution evidence. */
    if (!cpu || *reported) {
        return;
    }
    *reported = true;
    message = g_strdup_printf("z7ii-mas-diagnostic: uncovered access "
                              "cpu=%d pc=0x%08" PRIx64 " address=0x%08"
                              HWADDR_PRIx " direction=%s width=%u "
                              "value=0x%" PRIx64 " transaction=error reason=%s",
                              cpu->cpu_index,
                              (uint64_t)CPU_GET_CLASS(cpu)->get_pc(cpu),
                              addr, write ? "write" : "read", size, value,
                              reason);
    error_report("%s", message);
    qemu_log_mask(LOG_GUEST_ERROR, "%s\n", message);
    qemu_system_vmstop_request(RUN_STATE_DEBUG);
}

static MemTxResult z7ii_unknown_read(void *opaque, hwaddr addr,
                                    uint64_t *value, unsigned size,
                                    MemTxAttrs attrs)
{
    Z7IIMachineState *s = opaque;
    z7ii_access_error(&s->reported_unknown, addr, size, false, 0,
                      "uncovered-address");
    *value = 0; /* Output is invalid: the transaction explicitly fails. */
    return MEMTX_ERROR;
}

static MemTxResult z7ii_unknown_write(void *opaque, hwaddr addr,
                                     uint64_t value, unsigned size,
                                     MemTxAttrs attrs)
{
    Z7IIMachineState *s = opaque;
    z7ii_access_error(&s->reported_unknown, addr, size, true, value,
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

static MemTxResult z7ii_memc_error(Z7IIMemcState *s, hwaddr addr,
                                  unsigned size, bool write, uint64_t value,
                                  const char *reason)
{
    z7ii_access_error(&s->reported_error, addr, size, write, value, reason);
    return MEMTX_ERROR;
}

static MemTxResult z7ii_memc_read(void *opaque, hwaddr offset,
                                 uint64_t *value, unsigned size,
                                 MemTxAttrs attrs)
{
    Z7IIMemcState *s = opaque;

    *value = 0; /* Invalid unless the transaction below succeeds. */
    if (size != 4 || (offset != 0 && offset != 0x20)) {
        return z7ii_memc_error(s, MEMC_BASE + offset, size, false, 0,
                               "memc-unsupported-access");
    }
    if ((offset == 0 && !s->mode_initialized) ||
        (offset == 0x20 && !s->timing_initialized)) {
        return z7ii_memc_error(s, MEMC_BASE + offset, size, false, 0,
                               "memc-uninitialized-read");
    }
    *value = offset == 0 ? s->mode : s->timing;
    return MEMTX_OK;
}

static MemTxResult z7ii_memc_write(void *opaque, hwaddr offset,
                                  uint64_t value, unsigned size,
                                  MemTxAttrs attrs)
{
    Z7IIMemcState *s = opaque;

    if (size != 4 || (offset != 0 && offset != 0x20)) {
        return z7ii_memc_error(s, MEMC_BASE + offset, size, true, value,
                               "memc-unsupported-access");
    }
    /* Supported functional profiles, not the complete IP's legal values. */
    if (offset == 0) {
        if (value != 0x21 && value != 0x29) {
            return z7ii_memc_error(s, MEMC_BASE, size, true, value,
                                   "memc-mode-profile-not-supported");
        }
        s->mode = value; /* WDT=1 (x16), PAGE=1, WEOFF=bit3. */
        s->mode_initialized = true;
    } else {
        if (value != 0x153b0902) {
            return z7ii_memc_error(s, MEMC_BASE + offset, size, true, value,
                                   "memc-timing-profile-not-supported");
        }
        s->timing = value;
        s->timing_initialized = true;
    }
    qemu_log_mask(LOG_GUEST_ERROR, "z7ii-memc: write offset=0x%" HWADDR_PRIx
                  " value=0x%" PRIx64 " width=4 accepted-profile\n",
                  offset, value);
    return MEMTX_OK;
}

static const MemoryRegionOps z7ii_memc_ops = {
    .read_with_attrs = z7ii_memc_read,
    .write_with_attrs = z7ii_memc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    /* Dispatch bad widths/offsets too, so rejection records the guest PC. */
    .valid = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
};

static void z7ii_memc_reset(DeviceState *dev)
{
    Z7IIMemcState *s = Z7II_MEMC(dev);

    s->mode_initialized = false;
    s->timing_initialized = false;
    s->reported_error = false;
    /* No hardware reset value is supplied for either register. */
}

static void z7ii_memc_init(Object *obj)
{
    Z7IIMemcState *s = Z7II_MEMC(obj);

    memory_region_init_io(&s->regs, obj, &z7ii_memc_ops, s,
                          "z7ii.memc", 0x100);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
}

static void z7ii_memc_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_legacy_reset(dc, z7ii_memc_reset);
    dc->user_creatable = false;
}

static MemTxResult z7ii_nor_write(void *opaque, hwaddr offset,
                                 uint64_t value, unsigned size,
                                 MemTxAttrs attrs)
{
    Z7IIMachineState *s = opaque;
    Z7IIMemcState *memc = &s->memc;
    const char *reason;

    if (!memc->mode_initialized || !memc->timing_initialized) {
        reason = "nor-memc-not-initialized";
    } else if (memc->mode & 8) {
        reason = "nor-write-protected"; /* WEOFF closes external WEX. */
    } else if (size != 2 || (offset & 1)) {
        reason = "nor-x16-access-not-supported";
    } else {
        /* x16/PAGE selects this backend; NOR command writes remain absent. */
        reason = "nor-backend-not-supported";
    }
    return z7ii_memc_error(memc, MAS_BASE + offset, size, true, value, reason);
}

static MemTxResult z7ii_nor_read(void *opaque, hwaddr offset,
                                uint64_t *value, unsigned size,
                                MemTxAttrs attrs)
{
    Z7IIMachineState *s = opaque;

    /* Normal ROMD reads bypass this callback and read the original bytes. */
    *value = 0;
    return z7ii_memc_error(&s->memc, MAS_BASE + offset, size, false, 0,
                           "nor-indirect-read-not-supported");
}

static const MemoryRegionOps z7ii_nor_ops = {
    .read_with_attrs = z7ii_nor_read,
    .write_with_attrs = z7ii_nor_write,
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

static void z7ii_init(MachineState *machine)
{
    Z7IIMachineState *s = Z7II_MACHINE(machine);
    DeviceState *gic;
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

    /* Static MAS copy contracts: destinations only; guest performs the copy. */
    z7ii_ram(&s->copy_ram[0], "z7ii.copy0", 0xb1800000, 0xaedb8);
    z7ii_ram(&s->copy_ram[1], "z7ii.copy1", 0xb2bb0000, 0x105c);
    z7ii_ram(&s->table_ram, "z7ii.table", 0xb2b74000, 0x4000);
    z7ii_ram(&s->temporary_stack, "z7ii.temporary-stack", 0x04008ed0, 0x30);
    /* CPU0 O0xd848 produces the magic; secondary WFE/read/clear is guest code. */
    z7ii_ram(&s->early_release, "z7ii.early-release", 0x04008ffc, 4);
    if (machine->smp.cpus == 2) {
        /* O0xac + O0x270 + O0xc0cc consume at most 72 bytes before kernel. */
        z7ii_ram(&s->cpu1_startup_stack, "z7ii.cpu1-startup-stack",
                 0xb2b83fb8, 0x48);
    }
    /* CPU0's six banked SP values are known. Slot extent is an assumption. */
    for (i = 0; i < ARRAY_SIZE(stack_top); i++) {
        g_autofree char *name = g_strdup_printf("z7ii.stack%u", i);
        z7ii_ram(&s->stack_ram[i], name,
                 stack_top[i] - STACK_SLOT_SIZE, STACK_SLOT_SIZE);
    }

    s->num_cpus = machine->smp.cpus;
    for (i = 0; i < s->num_cpus; i++) {
        g_autofree char *name = g_strdup_printf("cpu%u", i);
        object_initialize_child(OBJECT(s), name, &s->cpu[i], machine->cpu_type);
        qdev_prop_set_uint64(DEVICE(&s->cpu[i]), "reset-cbar", CBAR_BASE);
        qdev_prop_set_uint64(DEVICE(&s->cpu[i]), "cntfrq", TIMER_HZ);
        qdev_realize(DEVICE(&s->cpu[i]), NULL, &error_fatal);
    }

    object_initialize_child(OBJECT(s), "memc", &s->memc, TYPE_Z7II_MEMC);
    sysbus_realize(SYS_BUS_DEVICE(&s->memc), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->memc), 0, MEMC_BASE);

    /* Upstream VExpress pattern: CPUs exist before timer/GIC wiring. */
    gic = qdev_new(TYPE_A15MPCORE_PRIV);
    gic_bus = SYS_BUS_DEVICE(gic);
    qdev_prop_set_uint32(gic, "num-cpu", s->num_cpus);
    qdev_prop_set_uint32(gic, "num-irq", 256); /* SGI/PPI + 224 SPIs. */
    sysbus_realize_and_unref(gic_bus, &error_fatal);
    sysbus_mmio_map(gic_bus, 0, CBAR_BASE);
    for (i = 0; i < s->num_cpus; i++) {
        DeviceState *cpu = DEVICE(&s->cpu[i]);
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
                 "image-bytes=%" PRId64 " cpu0-stack-slot=0x4000; "
                 "MEMC=0x04264000 profile-only CBAR=0x1d000000 GICv2=256 "
                 "cntfrq=48000000 secure-timer-PPI=29 early-release-RAM=4B; "
                 "CPU1-startup-stack=72B; no NOR writer/IPC devices/BootROM; "
                 "further CPU1 stack extent uncovered",
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
    static const TypeInfo memc_type = {
        .name = TYPE_Z7II_MEMC,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(Z7IIMemcState),
        .instance_init = z7ii_memc_init,
        .class_init = z7ii_memc_class_init,
    };
    type_register_static(&memc_type);
    type_register_static(&z7ii_machine_type);
}
type_init(z7ii_register_types)
