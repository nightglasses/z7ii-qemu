/* Real QEMU DMA region boundaries and deferred address-space retirement.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/rcu.h"
#include "qemu/timer.h"
#include "hw/core/sysbus.h"
#include "hw/core/cpu.h"
#include "hw/nikon/devices.h"
#include "system/address-spaces.h"
#include "system/dma.h"
#include "system/system.h"

/* The system library excludes system/main.c, as in QEMU's native fuzz runner. */
int (*qemu_main)(void);

typedef struct MemoryFixture {
    Object *owner;
    MemoryRegion root;
    MemoryRegion ram;
    MemoryRegion rom;
    MemoryRegion protection;
    MemoryRegion source;
} MemoryFixture;

static MemoryFixture *memory_new(unsigned boundary, unsigned protection)
{
    MemoryFixture *f = g_new0(MemoryFixture, 1);

    f->owner = object_new(TYPE_CONTAINER);
    /* Link properties resolve a canonical QOM path. A detached object would
     * resolve to an empty path and select NULL/system memory instead. */
    object_property_add_child(object_get_objects_root(), "memory[*]", f->owner);
    memory_region_init(&f->root, f->owner, "test.memory", 0x10000);
    memory_region_init_ram_flags_nomigrate(&f->ram, f->owner, "test.ram",
                                          boundary ? boundary : 0x4000, 0,
                                          &error_abort);
    memory_region_add_subregion(&f->root, 0, &f->ram);
    if (boundary) {
        memory_region_init_ram_flags_nomigrate(&f->rom, f->owner, "test.rom",
                                              boundary, 0, &error_abort);
        if (protection == 1) {
            memory_region_init_alias(&f->protection, f->owner,
                                     "test.alias", &f->rom, 0, boundary);
        } else if (protection == 2) {
            memory_region_init(&f->protection, f->owner,
                               "test.container", boundary);
            memory_region_add_subregion(&f->protection, 0, &f->rom);
        }
        memory_region_set_readonly(protection ? &f->protection : &f->rom, true);
        memory_region_add_subregion(&f->root, boundary,
                                    protection ? &f->protection : &f->rom);
        memory_region_init_ram_flags_nomigrate(&f->source, f->owner,
                                              "test.source", 0x1000, 0,
                                              &error_abort);
        memory_region_add_subregion(&f->root, 0x1000, &f->source);
    }
    return f;
}

static void memory_free(MemoryFixture *f)
{
    MemoryRegion *regions[] = {
        &f->source, &f->rom, &f->protection, &f->ram,
    };

    /* Remove topology while its owner and embedded regions are still alive.
     * Owner property destruction must not rebuild maps of partially destroyed
     * siblings. Retired views are drained before releasing fixture storage.
     */
    for (unsigned i = 0; i < ARRAY_SIZE(regions); i++) {
        if (regions[i]->container) {
            memory_region_del_subregion(regions[i]->container, regions[i]);
        }
    }
    drain_call_rcu();

    object_unparent(f->owner);
    object_unref(f->owner);
    drain_call_rcu();
    g_free(f);
}

static void select_memory(DeviceState *dev, MemoryFixture *f)
{
    object_property_set_link(OBJECT(dev), "memory", OBJECT(&f->root),
                             &error_abort);
    g_assert_true(object_property_get_link(OBJECT(dev), "memory",
                                           &error_abort) == OBJECT(&f->root));
}

static DeviceState *device_new(const char *type, MemoryFixture *f)
{
    DeviceState *dev = qdev_new(type);

    select_memory(dev, f);
    g_assert_true(sysbus_realize(SYS_BUS_DEVICE(dev), &error_abort));
    return dev;
}

static void device_free(DeviceState *dev)
{
    qdev_unrealize(dev);
    object_unparent(OBJECT(dev));
    object_unref(OBJECT(dev));
    drain_call_rcu();
}

static void write_register(DeviceState *dev, hwaddr offset, uint32_t value)
{
    MemoryRegion *regs = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);

    g_assert_cmpint(memory_region_dispatch_write(regs, offset, value, MO_32,
                                                MEMTXATTRS_UNSPECIFIED), ==,
                    MEMTX_OK);
}

static uint32_t read_register(DeviceState *dev, hwaddr offset)
{
    MemoryRegion *regs = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
    uint64_t value;

    g_assert_cmpint(memory_region_dispatch_read(regs, offset, &value, MO_32,
                                               MEMTXATTRS_UNSPECIFIED), ==,
                    MEMTX_OK);
    return value;
}

static void advance(void)
{
    qemu_clock_advance_virtual_time(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                                    100000);
}

static void dma_request(DeviceState *dev, unsigned width, uint32_t source,
                         uint32_t destination)
{
    uint32_t transfer = width == 4 ? 0x08180000 : 0x04180000;

    write_register(dev, 0, 0x90000000);
    write_register(dev, 0x18, source);
    write_register(dev, 0x1c, destination);
    write_register(dev, 0x14, transfer);
    write_register(dev, 0x10, 0xa0000000);
}

static void fill_request(DeviceState *dev)
{
    write_register(dev, 0, 0);
    write_register(dev, 4, 0x11);
    write_register(dev, 8, 0x11);
    write_register(dev, 0x14, 1);
    write_register(dev, 0x18, 0);
    write_register(dev, 0x1c, 0);
    write_register(dev, 0x20, 0x4000);
    write_register(dev, 0x24, 0x4000);
    write_register(dev, 0x28, 1);
    write_register(dev, 0x2c, 0);
    write_register(dev, 0x30, 0);
    write_register(dev, 0x10, 1);
    g_assert_cmphex(read_register(dev, 8), ==, 1);
    g_assert_cmphex(read_register(dev, 0xc), ==, 0);
}

static void check_boundary(unsigned width, unsigned protection)
{
    const uint8_t source[4] = { 0x44, 0x33, 0x22, 0x11 };
    unsigned span = width / 2;
    MemoryFixture *f = memory_new(span, protection);
    DeviceState *dma = device_new(TYPE_NIKON_HDMAC, f);
    uint8_t *ram = memory_region_get_ram_ptr(&f->ram);
    uint8_t *rom = memory_region_get_ram_ptr(&f->rom);
    MemoryRegion *permission = protection ? &f->protection : &f->rom;
    uint32_t transfer = width == 4 ? 0x08180000 : 0x04180000;
    AddressSpace *probe = g_new0(AddressSpace, 1);
    uint8_t observed[4] = { 0 };
    MemTxResult read_result;

    memcpy(memory_region_get_ram_ptr(&f->source), source, sizeof(source));
    g_assert_true(object_property_get_link(OBJECT(dma), "memory",
                                           &error_abort) == OBJECT(&f->root));
    address_space_init(probe, &f->root, "test.source-probe");
    read_result = address_space_read(probe, 0x1000, MEMTXATTRS_UNSPECIFIED,
                                     observed, width);
    g_test_message("source-read result=%u bytes=%02x%02x%02x%02x",
                   read_result, observed[0], observed[1], observed[2],
                   observed[3]);
    g_assert_cmpint(read_result, ==, MEMTX_OK);
    g_assert_cmpmem(observed, width, source, width);
    address_space_destroy_free(probe);
    memset(ram, 0xa5, span);
    memset(rom, 0x5a, span);
    dma_request(dma, width, 0x1000, 0);
    g_test_message("armed source=%08x destination=%08x CA=%08x CB=%08x",
                   read_register(dma, 0x18), read_register(dma, 0x1c),
                   read_register(dma, 0x10), read_register(dma, 0x14));
    g_assert_cmphex(read_register(dma, 0x18), ==, 0x1000);
    advance();
    g_assert_cmphex(read_register(dma, 0x14), ==,
                    (transfer & ~0x70000) | 0x40000);
    g_assert_cmphex(read_register(dma, 0x10) & 0x80000000, ==, 0);
    for (unsigned i = 0; i < span; i++) {
        g_assert_cmphex(ram[i], ==, 0xa5);
        g_assert_cmphex(rom[i], ==, 0x5a);
    }
    /* Live topology permission changes must refresh the listener, rather
     * than leaving a stale refusal or allowing a newly readonly span. */
    memory_region_set_readonly(permission, false);
    dma_request(dma, width, 0x1000, 0);
    advance();
    g_assert_cmphex(read_register(dma, 0x14), ==,
                    (transfer & ~0x70000) | 0x50000);
    g_assert_cmpmem(ram, span, source, span);
    g_assert_cmpmem(rom, span, source + span, span);
    memory_region_set_readonly(permission, true);
    memset(ram, 0xa5, span);
    memset(rom, 0x5a, span);
    dma_request(dma, width, 0x1000, 0);
    advance();
    g_assert_cmphex(read_register(dma, 0x14), ==,
                    (transfer & ~0x70000) | 0x40000);
    for (unsigned i = 0; i < span; i++) {
        g_assert_cmphex(ram[i], ==, 0xa5);
        g_assert_cmphex(rom[i], ==, 0x5a);
    }
    device_free(dma);
    memory_free(f);
}

static void test_boundary(void)
{
    for (unsigned width = 2; width <= 4; width *= 2) {
        for (unsigned protection = 0; protection < 3; protection++) {
            check_boundary(width, protection);
        }
    }
}

static void assert_bytes(const uint8_t *data, unsigned length, uint8_t value)
{
    for (unsigned i = 0; i < length; i++) {
        g_assert_cmphex(data[i], ==, value);
    }
}

static void test_display_parameter(void)
{
    DeviceState *dev = qdev_new(TYPE_NIKON_DUAL_EVENT_BANKS);
    MemoryRegion *display, *parameter, *gate, *table;
    uint64_t value;

    g_assert_true(sysbus_realize(SYS_BUS_DEVICE(dev), &error_abort));
    device_cold_reset(dev);
    /* Original 1046d3c6/1046d3ee pack three halfwords into the +0x4000
     * object's words at physical 20145420/20145424. The bank1 subregion at
     * 20144000 owns those addresses over the larger bank0 aperture. */
    display = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 1);
    g_assert_cmpint(memory_region_dispatch_write(display, 0x1420,
                    0x89abcdef, MO_32, MEMTXATTRS_UNSPECIFIED), ==, MEMTX_OK);
    g_assert_cmpint(memory_region_dispatch_write(display, 0x1424,
                    0x76543210, MO_32, MEMTXATTRS_UNSPECIFIED), ==, MEMTX_OK);
    g_assert_cmpint(memory_region_dispatch_read(display, 0x1420, &value,
                    MO_32, MEMTXATTRS_UNSPECIFIED), ==, MEMTX_OK);
    g_assert_cmphex(value, ==, 0x89abcdef);
    g_assert_cmpint(memory_region_dispatch_read(display, 0x1424, &value,
                    MO_32, MEMTXATTRS_UNSPECIFIED), ==, MEMTX_OK);
    g_assert_cmphex(value, ==, 0x76543210);
    g_assert_cmpint(memory_region_dispatch_write(display, 0x1428, 0,
                    MO_32, MEMTXATTRS_UNSPECIFIED), ==, MEMTX_ERROR);

    /* Original 1046d6d0 reads the overlay-update command after a layer
     * configuration pass and writes one unless the hardware reports three.
     * The same port-relative word exists independently in both apertures. */
    for (unsigned bank = 0; bank < 2; bank++) {
        MemoryRegion *overlay = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev),
                                                       bank);

        g_assert_cmpint(memory_region_dispatch_read(overlay, 0x2604, &value,
                        MO_32, MEMTXATTRS_UNSPECIFIED), ==, MEMTX_OK);
        g_assert_cmphex(value, ==, 0);
        g_assert_cmpint(memory_region_dispatch_write(overlay, 0x2604, 1,
                        MO_32, MEMTXATTRS_UNSPECIFIED), ==, MEMTX_OK);
        g_assert_cmpint(memory_region_dispatch_read(overlay, 0x2604, &value,
                        MO_32, MEMTXATTRS_UNSPECIFIED), ==, MEMTX_OK);
        g_assert_cmphex(value, ==, 1);
        g_assert_cmpint(memory_region_dispatch_write(overlay, 0x2604, 2,
                        MO_32, MEMTXATTRS_UNSPECIFIED), ==, MEMTX_ERROR);
        g_assert_cmpint(memory_region_dispatch_write(overlay, 0x2604, 3,
                        MO_32, MEMTXATTRS_UNSPECIFIED), ==, MEMTX_ERROR);
        g_assert_cmpint(memory_region_dispatch_read(overlay, 0x2604, &value,
                        MO_32, MEMTXATTRS_UNSPECIFIED), ==, MEMTX_OK);
        g_assert_cmphex(value, ==, 1);
    }

    /* Region3 is the D000 aperture: D100 is the observed gate and D200 is
     * the sole direct caller's zero-valued parameter. */
    parameter = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 3);
    g_assert_cmpint(memory_region_dispatch_read(parameter, 0x100, &value,
                    MO_32, MEMTXATTRS_UNSPECIFIED), ==, MEMTX_OK);
    g_assert_cmphex(value, ==, 0);
    g_assert_cmpint(memory_region_dispatch_write(parameter, 0x100, 1,
                    MO_32, MEMTXATTRS_UNSPECIFIED), ==, MEMTX_OK);
    g_assert_cmpint(memory_region_dispatch_write(parameter, 0x200, 0,
                    MO_32, MEMTXATTRS_UNSPECIFIED), ==, MEMTX_OK);
    g_assert_cmpint(memory_region_dispatch_read(parameter, 0x200, &value,
                    MO_32, MEMTXATTRS_UNSPECIFIED), ==, MEMTX_OK);
    g_assert_cmphex(value, ==, 0);
    g_assert_cmpint(memory_region_dispatch_write(parameter, 0x200, 1,
                    MO_32, MEMTXATTRS_UNSPECIFIED), ==, MEMTX_ERROR);
    g_assert_cmpint(memory_region_dispatch_write(parameter, 0x100, 0,
                    MO_32, MEMTXATTRS_UNSPECIFIED), ==, MEMTX_OK);

    /* Table offset zero must remain a table entry rather than aliasing the
     * sparse global/gate slots. */
    table = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 5);
    g_assert_cmpint(memory_region_dispatch_write(table, 0, 0x0abc0def,
                    MO_32, MEMTXATTRS_UNSPECIFIED), ==, MEMTX_OK);
    g_assert_cmpint(memory_region_dispatch_read(table, 0, &value, MO_32,
                    MEMTXATTRS_UNSPECIFIED), ==, MEMTX_OK);
    g_assert_cmphex(value, ==, 0x0abc0def);

    /* Original 1046e3c2 uses sparse C000 words: C004/C0A0 bracket the
     * transaction, C008/C0A4 carry packed fields and C0C8 carries a word.
     * The older paired wrappers use the same C0A0 gate. */
    gate = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 13);
    {
        static const struct {
            hwaddr offset;
            uint32_t mask;
        } fields[] = {
            { 0xc, 0xff }, { 0x10, 0x10f }, { 0x14, 0x1f },
            { 0x18, 0x00ff00ff }, { 0x2c, 1 }, { 0x34, 1 },
            { 0x90, 0x0001003f },
        };

        for (unsigned i = 0; i < ARRAY_SIZE(fields); i++) {
            g_assert_cmpint(memory_region_dispatch_write(
                            gate, fields[i].offset, fields[i].mask,
                            MO_32, MEMTXATTRS_UNSPECIFIED), ==, MEMTX_OK);
            g_assert_cmpint(memory_region_dispatch_read(
                            gate, fields[i].offset, &value,
                            MO_32, MEMTXATTRS_UNSPECIFIED), ==, MEMTX_OK);
            g_assert_cmphex(value, ==, fields[i].mask);
            g_assert_cmpint(memory_region_dispatch_write(
                            gate, fields[i].offset, fields[i].mask + 1,
                            MO_32, MEMTXATTRS_UNSPECIFIED), ==, MEMTX_ERROR);
        }
    }
    g_assert_cmpint(memory_region_dispatch_write(gate, 8, 0xabcd, MO_32,
                    MEMTXATTRS_UNSPECIFIED), ==, MEMTX_OK);
    g_assert_cmpint(memory_region_dispatch_write(gate, 0xa4, 0x5a, MO_32,
                    MEMTXATTRS_UNSPECIFIED), ==, MEMTX_OK);
    g_assert_cmpint(memory_region_dispatch_write(gate, 0xc8, 0x12345678,
                    MO_32, MEMTXATTRS_UNSPECIFIED), ==, MEMTX_OK);
    g_assert_cmpint(memory_region_dispatch_read(gate, 0xa0, &value, MO_32,
                    MEMTXATTRS_UNSPECIFIED), ==, MEMTX_OK);
    g_assert_cmphex(value, ==, 0);
    g_assert_cmpint(memory_region_dispatch_write(gate, 0xa0, 1, MO_32,
                    MEMTXATTRS_UNSPECIFIED), ==, MEMTX_OK);
    g_assert_cmpint(memory_region_dispatch_write(gate, 4, 1, MO_32,
                    MEMTXATTRS_UNSPECIFIED), ==, MEMTX_OK);
    g_assert_cmpint(memory_region_dispatch_read(gate, 0xa0, &value, MO_32,
                    MEMTXATTRS_UNSPECIFIED), ==, MEMTX_OK);
    g_assert_cmphex(value, ==, 1);
    g_assert_cmpint(memory_region_dispatch_write(gate, 0xa0, 2, MO_32,
                    MEMTXATTRS_UNSPECIFIED), ==, MEMTX_ERROR);
    g_assert_cmpint(memory_region_dispatch_write(gate, 0, 0, MO_32,
                    MEMTXATTRS_UNSPECIFIED), ==, MEMTX_ERROR);
    g_assert_cmpint(memory_region_dispatch_write(gate, 4, 0, MO_32,
                    MEMTXATTRS_UNSPECIFIED), ==, MEMTX_OK);
    g_assert_cmpint(memory_region_dispatch_write(gate, 0xa0, 0, MO_32,
                    MEMTXATTRS_UNSPECIFIED), ==, MEMTX_OK);
    g_assert_cmpint(memory_region_dispatch_read(table, 0, &value, MO_32,
                    MEMTXATTRS_UNSPECIFIED), ==, MEMTX_OK);
    g_assert_cmphex(value, ==, 0x0abc0def);
    device_cold_reset(dev);
    for (unsigned bank = 0; bank < 2; bank++) {
        MemoryRegion *overlay = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev),
                                                       bank);

        g_assert_cmpint(memory_region_dispatch_read(overlay, 0x2604, &value,
                        MO_32, MEMTXATTRS_UNSPECIFIED), ==, MEMTX_OK);
        g_assert_cmphex(value, ==, 0);
    }
    g_assert_cmpint(memory_region_dispatch_read(display, 0x1420, &value,
                    MO_32, MEMTXATTRS_UNSPECIFIED), ==, MEMTX_OK);
    g_assert_cmphex(value, ==, 0);
    g_assert_cmpint(memory_region_dispatch_read(display, 0x1424, &value,
                    MO_32, MEMTXATTRS_UNSPECIFIED), ==, MEMTX_OK);
    g_assert_cmphex(value, ==, 0);
    g_assert_cmpint(memory_region_dispatch_read(parameter, 0x100, &value,
                    MO_32, MEMTXATTRS_UNSPECIFIED), ==, MEMTX_OK);
    g_assert_cmphex(value, ==, 0);
    g_assert_cmpint(memory_region_dispatch_read(parameter, 0x200, &value,
                    MO_32, MEMTXATTRS_UNSPECIFIED), ==, MEMTX_OK);
    g_assert_cmphex(value, ==, 0);
    g_assert_cmpint(memory_region_dispatch_read(gate, 4, &value, MO_32,
                    MEMTXATTRS_UNSPECIFIED), ==, MEMTX_OK);
    g_assert_cmphex(value, ==, 0);
    g_assert_cmpint(memory_region_dispatch_read(gate, 8, &value, MO_32,
                    MEMTXATTRS_UNSPECIFIED), ==, MEMTX_OK);
    g_assert_cmphex(value, ==, 0);
    g_assert_cmpint(memory_region_dispatch_read(gate, 0xa0, &value, MO_32,
                    MEMTXATTRS_UNSPECIFIED), ==, MEMTX_OK);
    g_assert_cmphex(value, ==, 0);
    g_assert_cmpint(memory_region_dispatch_read(gate, 0xc8, &value, MO_32,
                    MEMTXATTRS_UNSPECIFIED), ==, MEMTX_OK);
    g_assert_cmphex(value, ==, 0);
    device_free(dev);
}

static void test_default_pool15(void)
{
    const hwaddr base = 0xcc700000, size = 0xf41d000;
    const uint8_t first[] = { 0x91, 0x28, 0x73, 0x46 };
    const uint8_t last[] = { 0x5c, 0xa3, 0x2e, 0xd1 };
    const uint8_t neighbor[] = { 0x36, 0xb9, 0x64, 0x87 };
    MemoryRegionSection start, tail, next, kernel, graphics;
    AddressSpace *cpu_as;
    uint8_t value[4];

    if (!g_test_subprocess()) {
        g_test_trap_subprocess(NULL, 10 * G_TIME_SPAN_SECOND, 0);
        g_test_trap_assert_passed();
        return;
    }
    cpu_as = cpu_get_address_space(first_cpu, 0);
    start = memory_region_find(get_system_memory(), base, 4);
    tail = memory_region_find(get_system_memory(), base + size - 4, 4);
    next = memory_region_find(get_system_memory(), base + size, 4);
    kernel = memory_region_find(get_system_memory(), 0xb2d02400, 4);
    graphics = memory_region_find(get_system_memory(), 0xbb300000, 4);
    g_assert_true(memory_region_is_ram(start.mr));
    g_assert_true(start.mr == tail.mr);
    g_assert_cmphex(start.offset_within_region, ==, 0);
    g_assert_cmphex(tail.offset_within_region, ==, size - 4);
    g_assert_cmphex(memory_region_size(start.mr), ==, size);
    g_assert_true(start.mr != kernel.mr && start.mr != graphics.mr);
    /* The exclusive upper bound is already kind28's base. It is independent
     * RAM, not padding or an alias of this pool. The preceding gap stays a fault. */
    g_assert_true(memory_region_is_ram(next.mr) && next.mr != start.mr);
    g_assert_cmpint(address_space_read(cpu_as, base - 1,
                    MEMTXATTRS_UNSPECIFIED, value, 1), ==, MEMTX_ERROR);
    g_assert_cmpint(address_space_write(cpu_as, base,
                    MEMTXATTRS_UNSPECIFIED, first, 4), ==, MEMTX_OK);
    g_assert_cmpint(dma_memory_read(&address_space_memory, base, value, 4,
                    MEMTXATTRS_UNSPECIFIED), ==, MEMTX_OK);
    g_assert_cmpmem(value, 4, first, 4);
    g_assert_cmpint(dma_memory_write(&address_space_memory, base + size - 4,
                    last, 4, MEMTXATTRS_UNSPECIFIED), ==, MEMTX_OK);
    g_assert_cmpint(address_space_read(cpu_as, base + size - 4,
                    MEMTXATTRS_UNSPECIFIED, value, 4), ==, MEMTX_OK);
    g_assert_cmpmem(value, 4, last, 4);
    g_assert_cmpint(dma_memory_write(&address_space_memory, base + size,
                    neighbor, 4, MEMTXATTRS_UNSPECIFIED), ==, MEMTX_OK);
    g_assert_cmpint(address_space_read(cpu_as, base,
                    MEMTXATTRS_UNSPECIFIED, value, 4), ==, MEMTX_OK);
    g_assert_cmpmem(value, 4, first, 4);
    g_assert_cmpint(address_space_read(cpu_as, base + size - 4,
                    MEMTXATTRS_UNSPECIFIED, value, 4), ==, MEMTX_OK);
    g_assert_cmpmem(value, 4, last, 4);
    memory_region_unref(start.mr);
    memory_region_unref(tail.mr);
    memory_region_unref(next.mr);
    memory_region_unref(kernel.mr);
    memory_region_unref(graphics.mr);
}

static void test_lifecycle(void)
{
    MemoryFixture *f[2] = { memory_new(0, 0), memory_new(0, 0) };
    DeviceState *dma = device_new(TYPE_NIKON_HDMAC, f[0]);
    DeviceState *fill = device_new(TYPE_NIKON_FILL, f[0]);

    for (unsigned cycle = 0; cycle < 32; cycle++) {
        unsigned previous = cycle % 2;
        unsigned current = 1 - previous;
        uint8_t *old = memory_region_get_ram_ptr(&f[previous]->ram);
        uint8_t *selected = memory_region_get_ram_ptr(&f[current]->ram);

        memset(old, 0xa5, 0x4000);
        memset(selected, 0x5a, 0x4000);
        /* Queue a genuine old-domain timer, then retire both address spaces.
         * Realize against the new domain while prior RCU callbacks are queued. */
        dma_request(dma, 4, 0x10, 0x20);
        qdev_unrealize(dma);
        qdev_unrealize(fill);
        select_memory(dma, f[current]);
        select_memory(fill, f[current]);
        /* Unrealize retains QOM's parent bus. Re-realize that same attached
         * device through its standard property, without reattaching the bus. */
        object_property_set_bool(OBJECT(dma), "realized", true, &error_abort);
        object_property_set_bool(OBJECT(fill), "realized", true, &error_abort);
        drain_call_rcu();
        advance();
        assert_bytes(old, 0x4000, 0xa5);
        assert_bytes(selected, 0x4000, 0x5a);
        g_assert_cmphex(read_register(dma, 0x14), ==, 0);

        selected[0x10] = 0x78;
        selected[0x11] = 0x56;
        selected[0x12] = 0x34;
        selected[0x13] = 0x12;
        dma_request(dma, 4, 0x10, 0x20);
        advance();
        g_assert_cmphex(read_register(dma, 0x14), ==, 0x081d0000);
        g_assert_cmpmem(selected + 0x10, 4, selected + 0x20, 4);
        fill_request(fill);
        assert_bytes(selected, 0x4000, 0);
        assert_bytes(old, 0x4000, 0xa5);
    }
    /* Finalization happens before retirement callbacks, just as hot unplug.
     * Device storage must never contain the callback's address-space object. */
    memset(memory_region_get_ram_ptr(&f[0]->ram), 0xa5, 0x4000);
    memset(memory_region_get_ram_ptr(&f[1]->ram), 0x5a, 0x4000);
    memset((uint8_t *)memory_region_get_ram_ptr(&f[0]->ram) + 0x10, 0x78, 4);
    dma_request(dma, 4, 0x10, 0x20);
    device_free(dma);
    device_free(fill);
    drain_call_rcu();
    advance();
    assert_bytes((uint8_t *)memory_region_get_ram_ptr(&f[0]->ram) + 0x20,
                 0x4000 - 0x20, 0xa5);
    assert_bytes(memory_region_get_ram_ptr(&f[1]->ram), 0x4000, 0x5a);
    memory_free(f[0]);
    memory_free(f[1]);
}

int main(int argc, char **argv)
{
    g_autofree char *directory = g_dir_make_tmp("nikonXXXXXX", NULL);
    g_autofree char *socket = g_build_filename(directory, "qtest", NULL);
    g_autofree char *chardev = g_strdup_printf("unix:%s,server=on,wait=off",
                                             socket);
    g_autofree char *image = g_build_filename(directory, "loop.bin", NULL);
    const char *qemu_argv[] = {
        argv[0], "-machine", "none", "-accel", "qtest", "-display", "none",
        "-nodefaults", "-qtest", chardev, NULL, NULL, NULL
    };
    int qemu_argc = G_N_ELEMENTS(qemu_argv) - 3;
    int result;

    g_assert_nonnull(directory);
    g_test_init(&argc, &argv, NULL);
    if (g_test_subprocess()) {
        const char loop[] = { 0xfe, 0xff, 0xff, 0xea };

        g_assert_true(g_file_set_contents(image, loop, sizeof(loop), NULL));
        qemu_argv[2] = "z7ii-mas-diagnostic";
        qemu_argv[qemu_argc++] = "-bios";
        qemu_argv[qemu_argc++] = image;
    }
    qemu_argv[qemu_argc] = NULL;
    qemu_init(qemu_argc, (char **)qemu_argv);
    g_test_add_func("/nikon/memory/boundary", test_boundary);
    g_test_add_func("/nikon/memory/display-parameter", test_display_parameter);
    g_test_add_func("/nikon/memory/lifecycle", test_lifecycle);
    g_test_add_func("/nikon/memory/default-pool15", test_default_pool15);
    result = g_test_run();
    qemu_cleanup(result);
    unlink(socket);
    unlink(image);
    rmdir(directory);
    return result;
}
