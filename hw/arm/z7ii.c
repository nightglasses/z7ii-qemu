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
#include "system/address-spaces.h"
#include "system/reset.h"
#include "system/runstate.h"
#include "target/arm/cpu.h"

#define TYPE_Z7II_MACHINE MACHINE_TYPE_NAME("z7ii-mas-diagnostic")
OBJECT_DECLARE_SIMPLE_TYPE(Z7IIMachineState, Z7II_MACHINE)

#define MAS_BASE 0x10020000ULL
#define MAS_MAX_SIZE 0x2477b78ULL
#define STACK_SLOT_SIZE 0x4000

struct Z7IIMachineState {
    MachineState parent_obj;
    ARMCPU cpu;
    MemoryRegion image;
    MemoryRegion unknown;
    MemoryRegion copy_ram[2];
    MemoryRegion table_ram;
    MemoryRegion stack_ram[6];
    bool reported_unknown;
};

static void z7ii_unknown_access(Z7IIMachineState *s, hwaddr addr,
                                unsigned size, bool write, uint64_t value)
{
    CPUState *cpu = current_cpu;
    g_autofree char *message = NULL;

    /* Debugger/monitor probes must not become guest execution evidence. */
    if (!cpu || s->reported_unknown) {
        return;
    }
    s->reported_unknown = true;
    message = g_strdup_printf("z7ii-mas-diagnostic: uncovered access "
                              "cpu=%d pc=0x%08" PRIx64 " address=0x%08"
                              HWADDR_PRIx " direction=%s width=%u "
                              "value=0x%" PRIx64 " transaction=error",
                              cpu->cpu_index,
                              (uint64_t)CPU_GET_CLASS(cpu)->get_pc(cpu),
                              addr, write ? "write" : "read", size, value);
    error_report("%s", message);
    qemu_log_mask(LOG_GUEST_ERROR, "%s\n", message);
    qemu_system_vmstop_request(RUN_STATE_DEBUG);
}

static MemTxResult z7ii_unknown_read(void *opaque, hwaddr addr,
                                    uint64_t *value, unsigned size,
                                    MemTxAttrs attrs)
{
    z7ii_unknown_access(opaque, addr, size, false, 0);
    *value = 0; /* Output is invalid: the transaction explicitly fails. */
    return MEMTX_ERROR;
}

static MemTxResult z7ii_unknown_write(void *opaque, hwaddr addr,
                                     uint64_t value, unsigned size,
                                     MemTxAttrs attrs)
{
    z7ii_unknown_access(opaque, addr, size, true, value);
    return MEMTX_ERROR;
}

static const MemoryRegionOps z7ii_unknown_ops = {
    .read_with_attrs = z7ii_unknown_read,
    .write_with_attrs = z7ii_unknown_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
};

static void z7ii_reset(void *opaque)
{
    Z7IIMachineState *s = opaque;

    s->reported_unknown = false;
    cpu_reset(CPU(&s->cpu));
    /* Even entry means ARM state. No firmware instruction is changed. */
    cpu_set_pc(CPU(&s->cpu), MAS_BASE);
}

static void z7ii_ram(Z7IIMachineState *s, MemoryRegion *mr, const char *name,
                     hwaddr base, uint64_t size)
{
    /* Migratable board RAM uses NULL; its owner must not be a MachineState. */
    memory_region_init_ram(mr, NULL, name, size, &error_fatal);
    memory_region_add_subregion(get_system_memory(), base, mr);
}

static void z7ii_init(MachineState *machine)
{
    Z7IIMachineState *s = Z7II_MACHINE(machine);
    int64_t image_size;
    unsigned i;
    static const hwaddr stack_top[] = {
        0xb2bb0000, 0xb2ba8000, 0xb2ba0000,
        0xb2b98000, 0xb2b90000, 0xb2b88000,
    };

    if (strcmp(machine->cpu_type, ARM_CPU_TYPE_NAME("cortex-a9"))) {
        error_report("z7ii-mas-diagnostic currently requires Cortex-A9");
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
    memory_region_init_rom(&s->image, NULL, "z7ii.mas", image_size,
                           &error_fatal);
    memory_region_add_subregion(get_system_memory(), MAS_BASE, &s->image);
    if (load_image_targphys(machine->firmware, MAS_BASE, image_size,
                            &error_fatal) != image_size) {
        error_report("failed to load the complete MAS diagnostic image");
        exit(EXIT_FAILURE);
    }

    /* Static MAS copy contracts: destinations only; guest performs the copy. */
    z7ii_ram(s, &s->copy_ram[0], "z7ii.copy0", 0xb1800000, 0xaedb8);
    z7ii_ram(s, &s->copy_ram[1], "z7ii.copy1", 0xb2bb0000, 0x105c);
    z7ii_ram(s, &s->table_ram, "z7ii.table", 0xb2b74000, 0x4000);
    /* CPU0's six banked SP values are known. Slot extent is an assumption. */
    for (i = 0; i < ARRAY_SIZE(stack_top); i++) {
        g_autofree char *name = g_strdup_printf("z7ii.stack%u", i);
        z7ii_ram(s, &s->stack_ram[i], name,
                 stack_top[i] - STACK_SLOT_SIZE, STACK_SLOT_SIZE);
    }

    object_initialize_child(OBJECT(s), "cpu", &s->cpu, machine->cpu_type);
    qdev_realize(DEVICE(&s->cpu), NULL, &error_fatal);
    qemu_register_reset(z7ii_reset, s);
    error_report("z7ii-mas-diagnostic: assumptions cpu=%s cpus=1 "
                 "image-base=0x10020000 entry=0x10020000 ARM "
                 "image-bytes=%" PRId64 " cpu0-stack-slot=0x4000; "
                 "no IRQ/timer/IPC/BootROM model", machine->cpu_type, image_size);
}

static void z7ii_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    mc->desc = "Z7II unchanged MAS diagnostic (hypothetical Cortex-A9)";
    mc->init = z7ii_init;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-a9");
    mc->max_cpus = 1;
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
