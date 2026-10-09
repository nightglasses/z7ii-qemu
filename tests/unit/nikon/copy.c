/* ROMD section boundaries and side-effect-free whole-request rejection.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/rcu.h"
#include "qemu/timer.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/nikon/devices.h"
#include "system/system.h"

int (*qemu_main)(void);

typedef struct CopyFixture {
    Object *owner;
    MemoryRegion root, romd, alias, ram, mmio, adjacent, protection;
    DeviceState *dev;
    qemu_irq irq;
    unsigned reads, rom_reads;
    bool level;
} CopyFixture;

static uint64_t side_effect_read(void *opaque, hwaddr addr, unsigned size)
{
    CopyFixture *f = opaque;

    f->reads++;
    return 0x44444444;
}

static uint64_t rom_read(void *opaque, hwaddr addr, unsigned size)
{
    CopyFixture *f = opaque;

    f->rom_reads++;
    return 0x55555555;
}

static const MemoryRegionOps side_effect_ops = {
    .read = side_effect_read,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps rom_ops = {
    .read = rom_read,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl = { .min_access_size = 1, .max_access_size = 4 },
};

static void irq_level(void *opaque, int n, int level)
{
    CopyFixture *f = opaque;

    f->level = !!level;
}

static void write_register(CopyFixture *f, hwaddr offset, uint32_t value)
{
    MemoryRegion *regs = sysbus_mmio_get_region(SYS_BUS_DEVICE(f->dev), 0);
    MemTxResult result;

    result = memory_region_dispatch_write(regs, offset, value, MO_32,
                                          MEMTXATTRS_UNSPECIFIED);
    if (result != MEMTX_OK) {
        g_test_message("COPY write offset=%" HWADDR_PRIx " value=%08x result=%u",
                       offset, value, result);
    }
    g_assert_cmpint(result, ==, MEMTX_OK);
}

static uint32_t read_register(CopyFixture *f, hwaddr offset)
{
    MemoryRegion *regs = sysbus_mmio_get_region(SYS_BUS_DEVICE(f->dev), 0);
    uint64_t value;

    g_assert_cmpint(memory_region_dispatch_read(regs, offset, &value, MO_32,
                                               MEMTXATTRS_UNSPECIFIED), ==,
                    MEMTX_OK);
    return value;
}

static void copy_request(CopyFixture *f, uint32_t source, uint32_t destination,
                         unsigned length)
{
    write_register(f, 0x30, 0);
    write_register(f, 0, 0x10000000);
    write_register(f, 0x10, length - 1);
    write_register(f, 0x14, source);
    write_register(f, 0x18, destination);
    write_register(f, 0x1c, 0x30f00);
    write_register(f, 0x20, 0x30f00);
    write_register(f, 0x24, 0);
    /* Original setup is an RMW; preserve trigger/mode fields after reuse. */
    write_register(f, 0x28, (read_register(f, 0x28) & ~3U) | 0x9000);
    write_register(f, 0x28, 0x11109003);
    g_assert_false(f->level);
    qemu_clock_advance_virtual_time(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                                    100000);
    g_assert_true(f->level);
}

static void unchanged(CopyFixture *f)
{
    const uint8_t *destination = memory_region_get_ram_ptr(&f->ram);

    g_assert_cmpuint(f->reads, ==, 0);
    g_assert_cmpuint(f->rom_reads, ==, 0);
    for (unsigned i = 0; i < 32; i++) {
        g_assert_cmphex(destination[i], ==, 0xa5);
    }
    g_assert_cmphex(read_register(f, 0x30), ==, 1);
}

static void test_romd_boundary(void)
{
    CopyFixture *f = g_new0(CopyFixture, 1);
    uint8_t *source, *destination;

    /* ROM-device RAM registration requires a real DeviceState owner. */
    f->dev = qdev_new(TYPE_NIKON_COPY);
    f->owner = OBJECT(f->dev);
    object_property_add_child(object_get_objects_root(), "copy-memory[*]",
                              f->owner);
    memory_region_init(&f->root, f->owner, "test.copy", 0x10000);
    memory_region_init_rom_device(&f->romd, f->owner, &rom_ops, f,
                                  "test.romd", 17, &error_abort);
    memory_region_add_subregion(&f->root, 0x1000, &f->romd);
    memory_region_init_alias(&f->alias, f->owner, "test.romd-alias", &f->romd,
                             3, 5);
    memory_region_add_subregion(&f->root, 0x1100, &f->alias);
    memory_region_init_io(&f->mmio, f->owner, &side_effect_ops, f,
                          "test.side-effect", 16);
    memory_region_add_subregion(&f->root, 0x1105, &f->mmio);
    memory_region_init_io(&f->adjacent, f->owner, &side_effect_ops, f,
                          "test.adjacent", 16);
    memory_region_add_subregion(&f->root, 0x1011, &f->adjacent);
    memory_region_init_ram_flags_nomigrate(&f->ram, f->owner, "test.ram", 32,
                                          0, &error_abort);
    memory_region_add_subregion(&f->root, 0x2000, &f->ram);
    source = memory_region_get_ram_ptr(&f->romd);
    destination = memory_region_get_ram_ptr(&f->ram);
    for (unsigned i = 0; i < 17; i++) {
        source[i] = 0xb0 + i;
    }
    memset(destination, 0xa5, 32);
    object_property_set_link(OBJECT(f->dev), "memory", OBJECT(&f->root),
                             &error_abort);
    f->irq = qemu_allocate_irq(irq_level, f, 0);
    sysbus_connect_irq(SYS_BUS_DEVICE(f->dev), 0, f->irq);
    g_assert_true(sysbus_realize(SYS_BUS_DEVICE(f->dev), &error_abort));
    device_cold_reset(f->dev);

    /* A short ROMD block and a shorter alias both end inside the first beat. */
    copy_request(f, 0x1010, 0x2000, 2);
    unchanged(f);
    g_assert_cmphex(read_register(f, 0x10), ==, 1);
    g_assert_cmphex(read_register(f, 0x14), ==, 0x1010);
    copy_request(f, 0x1102, 0x2000, 4);
    unchanged(f);
    g_assert_cmphex(read_register(f, 0x10), ==, 3);
    g_assert_cmphex(read_register(f, 0x14), ==, 0x1102);
    /* Live topology removal turns the same endpoint into a hole. */
    memory_region_del_subregion(&f->root, &f->mmio);
    copy_request(f, 0x1102, 0x2000, 4);
    unchanged(f);
    g_assert_cmphex(read_register(f, 0x10), ==, 3);

    /* A fully covered source still performs a real, exact ROMD transaction. */
    copy_request(f, 0x1100, 0x2000, 4);
    g_assert_cmphex(read_register(f, 0x30), ==, 8);
    g_assert_cmpmem(destination, 4, source + 3, 4);
    g_assert_cmpuint(f->reads, ==, 0);
    g_assert_cmpuint(f->rom_reads, ==, 0);
    for (unsigned i = 4; i < 32; i++) {
        g_assert_cmphex(destination[i], ==, 0xa5);
    }

    /* Effective readonly permissions survive the same all-section cache. */
    for (unsigned container = 0; container < 2; container++) {
        MemoryRegion child;

        if (container) {
            memory_region_init(&f->protection, f->owner, "test.container", 16);
            memory_region_init_alias(&child, f->owner, "test.child", &f->ram,
                                     0, 16);
            memory_region_add_subregion(&f->protection, 0, &child);
        } else {
            memory_region_init_alias(&f->protection, f->owner, "test.readonly",
                                     &f->ram, 0, 16);
        }
        memory_region_set_readonly(&f->protection, true);
        memory_region_add_subregion(&f->root, 0x2040, &f->protection);
        memset(destination, 0xa5, 32);
        copy_request(f, 0x1100, 0x2040, 4);
        unchanged(f);
        memory_region_set_readonly(&f->protection, false);
        copy_request(f, 0x1100, 0x2040, 4);
        g_assert_cmphex(read_register(f, 0x30), ==, 8);
        g_assert_cmpmem(destination, 4, source + 3, 4);
        memory_region_del_subregion(&f->root, &f->protection);
        drain_call_rcu();
        if (container) {
            memory_region_del_subregion(&f->protection, &child);
            object_unparent(OBJECT(&child));
        }
        object_unparent(OBJECT(&f->protection));
        drain_call_rcu();
    }
    /* ROMD mode changes refresh cached qualification, not just its length. */
    memory_region_rom_device_set_romd(&f->romd, false);
    memset(destination, 0xa5, 32);
    copy_request(f, 0x1100, 0x2000, 4);
    unchanged(f);
    memory_region_rom_device_set_romd(&f->romd, true);
    copy_request(f, 0x1100, 0x2000, 4);
    g_assert_cmphex(read_register(f, 0x30), ==, 8);
    g_assert_cmpmem(destination, 4, source + 3, 4);

    qdev_unrealize(f->dev);
    drain_call_rcu();
    object_unparent(OBJECT(f->dev));
    object_unref(OBJECT(f->dev));
    qemu_free_irq(f->irq);
    drain_call_rcu();
    g_free(f);
}

int main(int argc, char **argv)
{
    g_autofree char *directory = g_dir_make_tmp("nikonXXXXXX", NULL);
    g_autofree char *socket = g_build_filename(directory, "qtest", NULL);
    g_autofree char *chardev = g_strdup_printf("unix:%s,server=on,wait=off",
                                             socket);
    const char *const arguments[] = {
        argv[0], "-machine", "none", "-accel", "qtest", "-display", "none",
        "-nodefaults", "-qtest", chardev, NULL
    };
    g_auto(GStrv) qemu_argv = g_new0(char *, G_N_ELEMENTS(arguments));
    int result;

    g_assert_nonnull(directory);
    g_test_init(&argc, &argv, NULL);
    for (unsigned i = 0; i < G_N_ELEMENTS(arguments) - 1; i++) {
        qemu_argv[i] = g_strdup(arguments[i]);
    }
    qemu_init(G_N_ELEMENTS(arguments) - 1, qemu_argv);
    g_test_add_func("/nikon/copy/romd-boundary", test_romd_boundary);
    result = g_test_run();
    qemu_cleanup(result);
    unlink(socket);
    rmdir(directory);
    return result;
}
