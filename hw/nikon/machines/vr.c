/*
 * Nikon VR loaded-component RX software profile, not a cold-reset MCU board.
 * ROM placement follows original record fields; guest owns COPY and ZERO.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/core/boards.h"
#include "hw/core/qdev-properties.h"
#include "hw/nikon/bundle.h"
#include "hw/nikon/devices.h"
#include "hw/nikon/renesas.h"
#include "system/address-spaces.h"
#include "system/reset.h"
#include "target/rx/cpu.h"

#define TYPE_NIKON_VR_MACHINE MACHINE_TYPE_NAME("nikonvr")
OBJECT_DECLARE_SIMPLE_TYPE(NikonVRMachine, NIKON_VR_MACHINE)

typedef struct NikonVRRange {
    uint32_t start, end;
} NikonVRRange;

struct NikonVRMachine {
    MachineState parent_obj;
    RXCPU cpu;
    MemoryRegion image, uncovered;
    MemoryRegion ram[25];
    MemoryRegion storage[25];
    unsigned ram_count;
    NikonBundle *bundle;
    GBytes *body, *metadata;
    bool reported_error;
};

static MemTxResult vr_uncovered_read(void *opaque, hwaddr offset,
                                     uint64_t *value, unsigned size,
                                     MemTxAttrs attrs)
{
    NikonVRMachine *s = opaque;

    nikon_access_error(&s->reported_error, offset, size, false, 0,
                       "VR uncovered input");
    *value = 0; /* Invalid output: no external ROM or peripheral value invented. */
    return MEMTX_ERROR;
}

static MemTxResult vr_uncovered_write(void *opaque, hwaddr offset,
                                      uint64_t value, unsigned size,
                                      MemTxAttrs attrs)
{
    NikonVRMachine *s = opaque;

    nikon_access_error(&s->reported_error, offset, size, true, value,
                       "VR uncovered output");
    return MEMTX_ERROR;
}

static const MemoryRegionOps vr_uncovered_ops = {
    .read_with_attrs = vr_uncovered_read,
    .write_with_attrs = vr_uncovered_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
};

static gint vr_range_compare(gconstpointer a, gconstpointer b)
{
    const NikonVRRange *ra = a, *rb = b;

    return (ra->start > rb->start) - (ra->start < rb->start);
}

static void vr_reset(void *opaque)
{
    NikonVRMachine *s = opaque;

    cpu_reset(CPU(&s->cpu));
    /* A named caller-state assumption: the package starts with PUSHM. */
    s->cpu.env.regs[0] = s->cpu.env.isp = 0x2000;
    cpu_set_pc(CPU(&s->cpu), 0xfff00010);
    s->reported_error = false;
}

static void vr_map_ram(NikonVRMachine *s, const uint8_t *body, size_t size)
{
    g_autoptr(GArray) ranges = g_array_new(false, false, sizeof(NikonVRRange));
    unsigned i;

    if (size < 0xcf4) {
        error_report("Nikon VR body has no complete COPY/ZERO table");
        exit(EXIT_FAILURE);
    }
    for (i = 0; i < 13; i++) {
        const uint8_t *record = body + 0xc00 + i * 12;
        uint32_t source = ldl_le_p(record), end = ldl_le_p(record + 4);
        uint32_t target = ldl_le_p(record + 8);
        uint64_t length = (uint64_t)end - source;
        NikonVRRange range;

        if (source < 0xfff00000 || end < source ||
            (uint64_t)end - 0xfff00000 > size ||
            (uint64_t)target + length > UINT32_MAX) {
            error_report("Nikon VR COPY record %u has invalid bounds", i);
            exit(EXIT_FAILURE);
        }
        if (!length) {
            continue;
        }
        range = (NikonVRRange) { target, target + length };
        g_array_append_val(ranges, range);
    }
    for (i = 0; i < 11; i++) {
        const uint8_t *record = body + 0xc9c + i * 8;
        NikonVRRange range = { ldl_le_p(record), ldl_le_p(record + 4) };

        if (range.end < range.start) {
            error_report("Nikon VR ZERO record %u has invalid bounds", i);
            exit(EXIT_FAILURE);
        }
        if (range.end != range.start) {
            g_array_append_val(ranges, range);
        }
    }
    {
        /* 4 KiB virtual caller stack, independently named from guest tables. */
        NikonVRRange range = { 0x1000, 0x2000 };

        g_array_append_val(ranges, range);
    }
    g_array_sort(ranges, vr_range_compare);
    for (i = 0; i < ranges->len; ) {
        NikonVRRange range = g_array_index(ranges, NikonVRRange, i++);
        g_autofree char *name = NULL;
        g_autofree char *storage_name = NULL;
        uint32_t page_offset;

        while (i < ranges->len &&
               g_array_index(ranges, NikonVRRange, i).start <= range.end) {
            NikonVRRange next = g_array_index(ranges, NikonVRRange, i++);

            range.end = MAX(range.end, next.end);
        }
        name = g_strdup_printf("nikon.vr.ram%u", s->ram_count);
        storage_name = g_strdup_printf("nikon.vr.storage%u", s->ram_count);
        page_offset = range.start & 0xfff; /* RX TCG has 4 KiB pages. */
        memory_region_init_ram(&s->storage[s->ram_count], NULL, storage_name,
                               (uint64_t)range.end - range.start + page_offset,
                               &error_fatal);
        /* TLB RAM/physical page offsets must agree, without mapping padding. */
        memory_region_init_alias(&s->ram[s->ram_count], NULL, name,
                                 &s->storage[s->ram_count], page_offset,
                                 (uint64_t)range.end - range.start);
        memory_region_add_subregion(get_system_memory(), range.start,
                                    &s->ram[s->ram_count++]);
    }
}

static void vr_init(MachineState *machine)
{
    NikonVRMachine *s = NIKON_VR_MACHINE(machine);
    g_autoptr(GBytes) records = NULL;
    const uint8_t *body, *record_data;
    size_t size;

    if (!machine->firmware) {
        error_report("nikonvr requires -bios <complete decoded-container.bin>");
        exit(EXIT_FAILURE);
    }
    s->bundle = nikon_bundle_load(machine->firmware, &error_fatal);
    records = nikon_bundle_component(s->bundle, NIKON_COMPONENT_VR);
    record_data = g_bytes_get_data(records, NULL);
    s->body = nikon_vr_body(records, &s->metadata, &error_fatal);
    body = g_bytes_get_data(s->body, &size);
    /* This machine uses the recovered C1.70 domain; no alternate VA guessed. */
    if ((uint32_t)ldl_le_p(record_data + 264) != 0xfff00000) {
        error_report("nikonvr requires the C1.70 RX record execution profile");
        exit(EXIT_FAILURE);
    }
    memory_region_init_io(&s->uncovered, OBJECT(s), &vr_uncovered_ops, s,
                          "nikon.vr.uncovered", 1ULL << 32);
    memory_region_add_subregion_overlap(get_system_memory(), 0,
                                        &s->uncovered, -1);
    memory_region_init_rom(&s->image, NULL, "nikon.vr.body", size, &error_fatal);
    memcpy(memory_region_get_ram_ptr(&s->image), body, size);
    memory_region_add_subregion(get_system_memory(), 0xfff00000, &s->image);
    vr_map_ram(s, body, size);

    sysbus_create_simple(TYPE_NIKON_RX_SYSTEM, 0x80000, NULL);
    sysbus_create_simple(TYPE_NIKON_RX_RTC, 0x8c400, NULL);

    object_initialize_child(OBJECT(s), "cpu", &s->cpu, TYPE_RX62N_CPU);
    qdev_prop_set_bit(DEVICE(&s->cpu), "has-extb", true);
    qdev_realize(DEVICE(&s->cpu), NULL, &error_fatal);
    qemu_register_reset(vr_reset, s);
    error_report("nikonvr: loaded-component functional assumptions: "
                 "RX existing TCG plus EXTB; entry=fff00010, caller-SP=2000, "
                 "virtual caller-stack=4KiB; guest COPY/ZERO preserved; "
                 "no cold BootROM, fixed exception table or peer ACK supplied");
}

static void vr_finalize(Object *object)
{
    NikonVRMachine *s = NIKON_VR_MACHINE(object);

    g_clear_pointer(&s->body, g_bytes_unref);
    g_clear_pointer(&s->metadata, g_bytes_unref);
    nikon_bundle_free(s->bundle);
}

static void vr_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Nikon VR loaded RX software profile";
    mc->init = vr_init;
    mc->default_cpu_type = TYPE_RX62N_CPU;
    mc->default_ram_size = 0;
    mc->max_cpus = 1;
    mc->ignore_memory_transaction_failures = false;
}

static const TypeInfo vr_machine = {
    .name = TYPE_NIKON_VR_MACHINE,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(NikonVRMachine),
    .instance_finalize = vr_finalize,
    .class_init = vr_class_init,
};

static void vr_register_types(void)
{
    type_register_static(&vr_machine);
}
type_init(vr_register_types)
