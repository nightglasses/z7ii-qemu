/* Effective ROMD source boundaries must select the neighboring backing.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/rcu.h"
#include "qemu/timer.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/nikon/transfer.h"
#include "system/address-spaces.h"
#include "system/system.h"

int (*qemu_main)(void);

typedef struct TransferFixture {
    DeviceState *dev;
    MemoryRegion romd, alias, destination, neighbor_ram, neighbor_romd, mmio;
    qemu_irq irq;
    bool level;
    unsigned reads;
} TransferFixture;

static uint64_t side_effect_read(void *opaque, hwaddr addr, unsigned size)
{
    TransferFixture *f = opaque;

    f->reads++;
    return 0x44444444;
}

static const MemoryRegionOps side_effect_ops = {
    .read = side_effect_read,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl = { .min_access_size = 1, .max_access_size = 4 },
};

static void irq_level(void *opaque, int n, int level)
{
    TransferFixture *f = opaque;

    f->level = !!level;
}

static MemTxResult write_register(TransferFixture *f, hwaddr offset,
                                  uint32_t value)
{
    MemoryRegion *regs = sysbus_mmio_get_region(SYS_BUS_DEVICE(f->dev), 0);

    return memory_region_dispatch_write(regs, offset, value, MO_32,
                                         MEMTXATTRS_UNSPECIFIED);
}

static MemTxResult request(TransferFixture *f, uint32_t source,
                           uint32_t length)
{
    const uint32_t packet[][2] = {
        { 0xc18, 0 }, { 0xc1c, 0x8000 },
        { 0xc00, source }, { 0xc04, 0x2000 },
        { 0xc34, 0 }, { 0xc38, 0 }, { 0xc10, 8 },
        { 0xc20, length }, { 0xc24, 0 }, { 0xc28, 0 },
    };

    for (unsigned i = 0; i < ARRAY_SIZE(packet); i++) {
        g_assert_cmpint(write_register(f, packet[i][0], packet[i][1]), ==,
                        MEMTX_OK);
    }
    g_assert_false(f->level);
    return write_register(f, 0xc18, 1);
}

static void advance(void)
{
    qemu_clock_advance_virtual_time(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                                    100000);
}

static void test_romd_boundary(void)
{
    TransferFixture *f = g_new0(TransferFixture, 1);
    MemoryRegion *root = get_system_memory();
    uint8_t *source, *destination;

    f->dev = qdev_new(TYPE_NIKON_TRANSFER);
    object_property_add_child(object_get_objects_root(), "transfer-memory[*]",
                              OBJECT(f->dev));
    memory_region_init_rom_device(&f->romd, OBJECT(f->dev), &side_effect_ops, f,
                                  "test.transfer-romd", 17, &error_abort);
    memory_region_add_subregion(root, 0x1000, &f->romd);
    memory_region_init_alias(&f->alias, OBJECT(f->dev), "test.transfer-alias",
                             &f->romd, 3, 5);
    memory_region_add_subregion(root, 0x1100, &f->alias);
    memory_region_init_ram_flags_nomigrate(&f->destination, OBJECT(f->dev),
                                          "test.transfer-destination", 32, 0,
                                          &error_abort);
    memory_region_add_subregion(root, 0x2000, &f->destination);
    memory_region_init_ram_flags_nomigrate(&f->neighbor_ram, OBJECT(f->dev),
                                          "test.transfer-neighbor-ram", 16, 0,
                                          &error_abort);
    memory_region_init_rom_device(&f->neighbor_romd, OBJECT(f->dev),
                                  &side_effect_ops, f,
                                  "test.transfer-neighbor-romd", 16,
                                  &error_abort);
    memory_region_init_io(&f->mmio, OBJECT(f->dev), &side_effect_ops, f,
                          "test.transfer-mmio", 16);
    source = memory_region_get_ram_ptr(&f->romd);
    destination = memory_region_get_ram_ptr(&f->destination);
    for (unsigned i = 0; i < 17; i++) {
        source[i] = 0xb0 + i;
    }
    f->irq = qemu_allocate_irq(irq_level, f, 0);
    sysbus_connect_irq(SYS_BUS_DEVICE(f->dev), 1, f->irq);
    g_assert_true(sysbus_realize(SYS_BUS_DEVICE(f->dev), &error_abort));
    device_cold_reset(f->dev);

    for (unsigned romd = 0; romd < 2; romd++) {
        MemoryRegion *neighbor = romd ? &f->neighbor_romd : &f->neighbor_ram;
        uint8_t *neighbor_bytes = memory_region_get_ram_ptr(neighbor);
        uint8_t expected[8];

        for (unsigned i = 0; i < 16; i++) {
            neighbor_bytes[i] = (romd ? 0xd0 : 0xc0) + i;
        }
        memcpy(expected, source + 5, 3);
        memcpy(expected + 3, neighbor_bytes, 5);
        memset(destination, 0xa5, 32);
        memory_region_add_subregion(root, 0x1105, neighbor);
        g_assert_cmpint(request(f, 0x1102, sizeof(expected)), ==, MEMTX_OK);
        /* No eager bytes or IRQ; completion follows the whole real packet. */
        for (unsigned i = 0; i < 32; i++) {
            g_assert_cmphex(destination[i], ==, 0xa5);
        }
        for (unsigned events = 0; !f->level; events++) {
            g_assert_cmpuint(events, <, 8);
            advance();
        }
        g_assert_cmpmem(destination, sizeof(expected), expected,
                        sizeof(expected));
        g_assert_cmpuint(f->reads, ==, 0);
        for (unsigned i = sizeof(expected); i < 32; i++) {
            g_assert_cmphex(destination[i], ==, 0xa5);
        }
        g_assert_cmpint(write_register(f, 0xc18, 0), ==, MEMTX_OK);
        g_assert_false(f->level);
        /* The stream frontend must select the same effective ROMD sections. */
        g_assert_cmpint(write_register(f, 0x1214, 0xffff), ==, MEMTX_OK);
        g_assert_cmpint(write_register(f, 0x1298, 0), ==, MEMTX_OK);
        g_assert_cmpint(write_register(f, 0x10d4, 0x1102), ==, MEMTX_OK);
        g_assert_cmpint(write_register(f, 0x10dc, 1), ==, MEMTX_OK);
        g_assert_cmpint(write_register(f, 0x10d0, 1), ==, MEMTX_OK);
        {
            NikonTransferStreamRequest stream;
            size_t length;
            const void *bytes;

            g_assert_true(nikon_transfer_stream_request(f->dev, 0, &stream));
            bytes = g_bytes_get_data(stream.bytes, &length);
            g_assert_cmpmem(bytes, length, expected, sizeof(expected));
            g_bytes_unref(stream.bytes);
        }
        g_assert_false(f->level);
        device_cold_reset(f->dev);
        memory_region_del_subregion(root, neighbor);
    }

    /* A formerly direct section can become inaccessible before the timer. */
    memory_region_add_subregion(root, 0x1105, &f->neighbor_ram);
    for (unsigned readonly = 0; readonly < 2; readonly++) {
        memset(destination, 0xa5, 32);
        g_assert_cmpint(request(f, 0x1102, 8), ==, MEMTX_OK);
        if (readonly) {
            memory_region_set_readonly(&f->destination, true);
        } else {
            memory_region_rom_device_set_romd(&f->romd, false);
        }
        advance();
        g_assert_false(f->level);
        g_assert_cmpuint(f->reads, ==, 0);
        for (unsigned i = 0; i < 32; i++) {
            g_assert_cmphex(destination[i], ==, 0xa5);
        }
        memory_region_set_readonly(&f->destination, false);
        memory_region_rom_device_set_romd(&f->romd, true);
    }
    memory_region_del_subregion(root, &f->neighbor_ram);

    /* Preflight of the complete request must not read MMIO or write a prefix. */
    memset(destination, 0xa5, 32);
    memory_region_add_subregion(root, 0x1105, &f->mmio);
    g_assert_cmpint(request(f, 0x1102, 8), ==, MEMTX_ERROR);
    advance();
    g_assert_false(f->level);
    g_assert_cmpuint(f->reads, ==, 0);
    for (unsigned i = 0; i < 32; i++) {
        g_assert_cmphex(destination[i], ==, 0xa5);
    }
    memory_region_del_subregion(root, &f->mmio);

    qdev_unrealize(f->dev);
    memory_region_del_subregion(root, &f->alias);
    memory_region_del_subregion(root, &f->romd);
    memory_region_del_subregion(root, &f->destination);
    drain_call_rcu();
    object_unparent(OBJECT(f->dev));
    object_unref(OBJECT(f->dev));
    qemu_free_irq(f->irq);
    drain_call_rcu();
    g_free(f);
}

static void setup_irq(void *opaque, int n, int level)
{
    ((bool *)opaque)[n] = !!level;
}

static void test_empty_configuration(void)
{
    TransferFixture f = { .dev = qdev_new(TYPE_NIKON_TRANSFER) };
    bool levels[3] = { false, false, false };
    qemu_irq lines[3];
    MemoryRegion *regs;
    uint64_t value;

    object_property_add_child(object_get_objects_root(), "transfer-setup[*]",
                              OBJECT(f.dev));
    for (unsigned i = 0; i < 3; i++) {
        lines[i] = qemu_allocate_irq(setup_irq, levels, i);
        sysbus_connect_irq(SYS_BUS_DEVICE(f.dev), i, lines[i]);
    }
    g_assert_true(sysbus_realize(SYS_BUS_DEVICE(f.dev), &error_abort));
    device_cold_reset(f.dev);
    regs = sysbus_mmio_get_region(SYS_BUS_DEVICE(f.dev), 0);
    /* Original106ee04a with both optional table lengths zero: no job. */
    g_assert_cmpint(write_register(&f, 0x1214, 0x100ffff), ==, MEMTX_OK);
    g_assert_cmpint(write_register(&f, 0x1298, 0), ==, MEMTX_OK);
    g_assert_cmpint(memory_region_dispatch_read(regs, 0x1214, &value, MO_32,
                    MEMTXATTRS_UNSPECIFIED), ==, MEMTX_OK);
    g_assert_cmphex(value, ==, 0x100ffff);
    g_assert_cmpint(write_register(&f, 0x1214, 0xffff), ==, MEMTX_OK);
    g_assert_cmpint(memory_region_dispatch_read(regs, 0x1214, &value, MO_32,
                    MEMTXATTRS_UNSPECIFIED), ==, MEMTX_OK);
    g_assert_cmphex(value, ==, 0xffff);
    g_assert_cmpint(write_register(&f, 0x1214, 0x200ffff), ==, MEMTX_ERROR);
    g_assert_cmpint(write_register(&f, 0x1298, 1), ==, MEMTX_ERROR);
    /* Admitting the empty setup does not admit table DMA or other fields. */
    g_assert_cmpint(write_register(&f, 0x10d0, 1), ==, MEMTX_ERROR);
    g_assert_cmpint(write_register(&f, 0x1218, 0), ==, MEMTX_ERROR);
    /* Transfer's word-only valid-size rule rejects before its callback. */
    g_assert_cmpint(memory_region_dispatch_write(regs, 0x1214, 0xffff, MO_16,
                    MEMTXATTRS_UNSPECIFIED), ==, MEMTX_DECODE_ERROR);
    advance();
    for (unsigned i = 0; i < 3; i++) {
        g_assert_false(levels[i]);
    }
    device_cold_reset(f.dev);
    g_assert_cmpint(memory_region_dispatch_read(regs, 0x1214, &value, MO_32,
                    MEMTXATTRS_UNSPECIFIED), ==, MEMTX_OK);
    g_assert_cmphex(value, ==, 0);
    qdev_unrealize(f.dev);
    object_unparent(OBJECT(f.dev));
    object_unref(OBJECT(f.dev));
    drain_call_rcu();
    for (unsigned i = 0; i < 3; i++) {
        qemu_free_irq(lines[i]);
    }
}

static void test_stream_submission(void)
{
    TransferFixture f = { .dev = qdev_new(TYPE_NIKON_TRANSFER) };
    MemoryRegion ram, mmio;
    MemoryRegion *root = get_system_memory();
    bool levels[3] = { false, false, false };
    bool requests[2] = { false, false };
    qemu_irq lines[3], outputs[2];
    NikonTransferStreamRequest first, second, next;
    const uint8_t *snapshot;
    uint8_t *source;
    size_t length;
    uint64_t value;

    object_property_add_child(object_get_objects_root(), "transfer-stream[*]",
                              OBJECT(f.dev));
    memory_region_init_ram_flags_nomigrate(&ram, OBJECT(f.dev),
                                          "test.stream-source", 4096, 0,
                                          &error_abort);
    memory_region_add_subregion(root, 0x3000, &ram);
    memory_region_init_io(&mmio, OBJECT(f.dev), &side_effect_ops, &f,
                          "test.stream-mmio", 1024);
    memory_region_add_subregion(root, 0x4000, &mmio);
    source = memory_region_get_ram_ptr(&ram);
    for (unsigned i = 0; i < 4096; i++) {
        source[i] = i ^ (i >> 3) ^ 0xa5;
    }
    for (unsigned i = 0; i < 3; i++) {
        lines[i] = qemu_allocate_irq(setup_irq, levels, i);
        sysbus_connect_irq(SYS_BUS_DEVICE(f.dev), i, lines[i]);
    }
    /* Connect after realize so the old model's red signal is the MMIO write,
     * rather than absence of the new host-facing output name. */
    g_assert_true(sysbus_realize(SYS_BUS_DEVICE(f.dev), &error_abort));
    device_cold_reset(f.dev);
    g_assert_cmpint(write_register(&f, 0x1214, 0x100ffff), ==, MEMTX_OK);
    g_assert_cmpint(write_register(&f, 0x1298, 0), ==, MEMTX_OK);
    g_assert_cmpint(write_register(&f, 0x10d4, 0x3000), ==, MEMTX_OK);
    for (unsigned i = 0; i < 2; i++) {
        outputs[i] = qemu_allocate_irq(setup_irq, requests, i);
        qdev_connect_gpio_out_named(f.dev, "stream-request", i, outputs[i]);
    }
    g_assert_cmpint(write_register(&f, 0x10d0, 1), ==, MEMTX_ERROR);
    g_assert_cmpint(write_register(&f, 0x10dc, 28), ==, MEMTX_OK);
    g_assert_cmpint(write_register(&f, 0x10d0, 1), ==, MEMTX_OK);
    g_assert_true(requests[0]);
    g_assert_false(requests[1]);
    g_assert_true(nikon_transfer_stream_request(f.dev, 0, &first));
    g_assert_cmphex(first.source, ==, 0x3000);
    g_assert_cmpuint(first.units64, ==, 28);
    snapshot = g_bytes_get_data(first.bytes, &length);
    /* Original descriptor extent is 56 words; MMIO count is 28 units64. */
    g_assert_cmpmem(snapshot, length, source, 224);
    g_assert_cmpuint(first.token, >, 0);
    source[0] ^= 0xff;
    g_assert_cmphex(snapshot[0], ==, (source[0] ^ 0xff));
    g_assert_cmpint(write_register(&f, 0x10d4, 0x3080), ==, MEMTX_ERROR);
    g_assert_cmpint(write_register(&f, 0x10dc, 1), ==, MEMTX_ERROR);
    g_assert_cmpint(write_register(&f, 0x1214, 0xffff), ==, MEMTX_ERROR);
    g_assert_cmpint(write_register(&f, 0x1298, 0), ==, MEMTX_ERROR);
    g_assert_cmpint(write_register(&f, 0x10d0, 1), ==, MEMTX_ERROR);
    g_assert_cmpint(write_register(&f, 0x10d8, 0), ==, MEMTX_ERROR);
    /* One stream and an early ACK cannot manufacture or suppress completion. */
    g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
    advance();
    g_assert_true(requests[0]);
    for (unsigned i = 0; i < 3; i++) {
        g_assert_false(levels[i]);
    }
    g_assert_cmpint(memory_region_dispatch_read(
        sysbus_mmio_get_region(SYS_BUS_DEVICE(f.dev), 0), 0x58, &value,
        MO_32, MEMTXATTRS_UNSPECIFIED), ==, MEMTX_ERROR);
    g_assert_cmpint(write_register(&f, 0x10e4, 0x3080), ==, MEMTX_OK);
    g_assert_cmpint(write_register(&f, 0x10ec, 211), ==, MEMTX_OK);
    g_assert_cmpint(write_register(&f, 0x10e0, 1), ==, MEMTX_OK);
    g_assert_true(nikon_transfer_stream_request(f.dev, 1, &second));
    g_assert_cmpuint(second.units64, ==, 211);
    g_assert_cmpuint(second.token, !=, first.token);
    snapshot = g_bytes_get_data(second.bytes, &length);
    g_assert_cmpmem(snapshot, length, source + 128, 1688);
    g_assert_cmpint(write_register(&f, 0x10e4, 0x3000), ==, MEMTX_ERROR);
    g_assert_cmpint(write_register(&f, 0x10ec, 1), ==, MEMTX_ERROR);
    g_assert_cmpint(write_register(&f, 0x10e0, 1), ==, MEMTX_ERROR);
    g_assert_true(nikon_transfer_stream_request(f.dev, 1, &next));
    g_assert_cmpuint(next.token, ==, second.token);
    g_assert_cmpuint(next.units64, ==, 211);
    g_assert_true(g_bytes_equal(next.bytes, second.bytes));
    g_bytes_unref(next.bytes);
    g_assert_false(nikon_transfer_stream_request(f.dev, 2, &next));
    /* The owned pair is consumed together and produces the inferred IRQ129. */
    advance();
    g_assert_false(requests[0]);
    g_assert_false(requests[1]);
    g_assert_false(nikon_transfer_stream_request(f.dev, 0, &next));
    g_assert_true(levels[0]);
    g_assert_false(levels[1]);
    g_assert_false(levels[2]);
    g_assert_cmpint(memory_region_dispatch_read(
        sysbus_mmio_get_region(SYS_BUS_DEVICE(f.dev), 0), 0x58, &value,
        MO_32, MEMTXATTRS_UNSPECIFIED), ==, MEMTX_OK);
    g_assert_cmphex(value, ==, 0);
    g_assert_cmpint(write_register(&f, 0x10d4, 0x3000), ==, MEMTX_ERROR);
    g_assert_cmpint(write_register(&f, 0x1020, 1), ==, MEMTX_ERROR);
    g_assert_cmpint(memory_region_dispatch_write(
        sysbus_mmio_get_region(SYS_BUS_DEVICE(f.dev), 0), 0x1020, 0,
        MO_16, MEMTXATTRS_UNSPECIFIED), ==, MEMTX_DECODE_ERROR);
    g_assert_true(levels[0]);
    g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
    for (unsigned i = 0; i < 3; i++) {
        g_assert_false(levels[i]);
    }
    g_assert_cmpint(memory_region_dispatch_read(
        sysbus_mmio_get_region(SYS_BUS_DEVICE(f.dev), 0), 0x58, &value,
        MO_32, MEMTXATTRS_UNSPECIFIED), ==, MEMTX_OK);
    g_assert_cmphex(value, ==, 0);
    device_cold_reset(f.dev);
    g_assert_cmpint(memory_region_dispatch_read(
        sysbus_mmio_get_region(SYS_BUS_DEVICE(f.dev), 0), 0x58, &value,
        MO_32, MEMTXATTRS_UNSPECIFIED), ==, MEMTX_ERROR);
    g_assert_false(requests[0]);
    g_assert_false(requests[1]);
    g_assert_false(nikon_transfer_stream_request(f.dev, 0, &next));
    /* Existing snapshots remain valid, but their ownership tokens expire. */
    g_assert_cmpuint(g_bytes_get_size(first.bytes), ==, 224);
    g_assert_cmpint(write_register(&f, 0x1214, 0xffff), ==, MEMTX_OK);
    g_assert_cmpint(write_register(&f, 0x1298, 0), ==, MEMTX_OK);
    g_assert_cmpint(write_register(&f, 0x10d4, 0x4000), ==, MEMTX_OK);
    g_assert_cmpint(write_register(&f, 0x10dc, 28), ==, MEMTX_OK);
    g_assert_cmpint(write_register(&f, 0x10d0, 1), ==, MEMTX_ERROR);
    g_assert_cmpuint(f.reads, ==, 0);
    g_assert_cmpint(write_register(&f, 0x10dc,
        NIKON_TRANSFER_STREAM_BYTES / 8 + 1), ==, MEMTX_ERROR);
    g_assert_cmpint(write_register(&f, 0x10d4, 0x3001), ==, MEMTX_ERROR);
    g_assert_cmpint(write_register(&f, 0x10d4, 0x3000), ==, MEMTX_OK);
    g_assert_cmpint(write_register(&f, 0x10d0, 1), ==, MEMTX_OK);
    g_assert_true(nikon_transfer_stream_request(f.dev, 0, &next));
    g_assert_cmpuint(next.token, >, second.token);
    /* A source-backed single-stream form remains pending and has no IRQ law. */
    advance();
    g_assert_true(requests[0]);
    g_assert_false(requests[1]);
    for (unsigned i = 0; i < 3; i++) {
        g_assert_false(levels[i]);
    }
    g_bytes_unref(first.bytes);
    g_bytes_unref(second.bytes);
    g_bytes_unref(next.bytes);
    qdev_unrealize(f.dev);
    g_assert_false(requests[0]);
    g_assert_false(nikon_transfer_stream_request(f.dev, 0, &next));
    memory_region_del_subregion(root, &ram);
    memory_region_del_subregion(root, &mmio);
    drain_call_rcu();
    object_unparent(OBJECT(f.dev));
    object_unref(OBJECT(f.dev));
    drain_call_rcu();
    for (unsigned i = 0; i < 3; i++) {
        qemu_free_irq(lines[i]);
    }
    for (unsigned i = 0; i < 2; i++) {
        qemu_free_irq(outputs[i]);
    }
}

static void assert_stream_idle(TransferFixture *f, const bool requests[2],
                               const bool levels[3])
{
    NikonTransferStreamRequest request;

    for (unsigned bank = 0; bank < 2; bank++) {
        g_assert_false(requests[bank]);
        g_assert_false(nikon_transfer_stream_request(f->dev, bank, &request));
    }
    for (unsigned i = 0; i < 3; i++) {
        g_assert_false(levels[i]);
    }
    g_assert_cmpuint(f->reads, ==, 0);
}

static void test_stream_extent_boundaries(void)
{
    TransferFixture f = { .dev = qdev_new(TYPE_NIKON_TRANSFER) };
    MemoryRegion ram, mmio;
    MemoryRegion *root = get_system_memory();
    MemoryRegion *regs;
    const uint32_t base = 0x100000;
    bool levels[3] = { false, false, false };
    bool requests[2] = { false, false };
    qemu_irq lines[3], outputs[2];
    uint8_t *source;
    uint64_t value;
    const struct {
        unsigned configured;
        uint32_t units64, start;
    } incomplete[] = {
        /* Bits: setup1214, setup1298, source, count. */
        { 12, 1, 1 }, { 13, 1, 1 }, { 14, 1, 1 },
        { 11, 1, 1 }, { 7, 1, 1 }, { 15, 0, 1 },
        { 15, 1, 0 }, { 15, 1, 2 },
    };
    const MemOp widths[] = { MO_8, MO_16, MO_64 };

    object_property_add_child(object_get_objects_root(), "transfer-extent[*]",
                              OBJECT(f.dev));
    memory_region_init_ram_flags_nomigrate(&ram, OBJECT(f.dev),
                                          "test.stream-extent-source",
                                          NIKON_TRANSFER_STREAM_BYTES, 0,
                                          &error_abort);
    memory_region_add_subregion(root, base, &ram);
    memory_region_init_io(&mmio, OBJECT(f.dev), &side_effect_ops, &f,
                          "test.stream-extent-mmio", 16);
    memory_region_add_subregion(root, base + NIKON_TRANSFER_STREAM_BYTES,
                                &mmio);
    source = memory_region_get_ram_ptr(&ram);
    for (unsigned i = 0; i < NIKON_TRANSFER_STREAM_BYTES; i++) {
        source[i] = i ^ (i >> 8) ^ 0x5a;
    }
    for (unsigned i = 0; i < 3; i++) {
        lines[i] = qemu_allocate_irq(setup_irq, levels, i);
        sysbus_connect_irq(SYS_BUS_DEVICE(f.dev), i, lines[i]);
    }
    g_assert_true(sysbus_realize(SYS_BUS_DEVICE(f.dev), &error_abort));
    regs = sysbus_mmio_get_region(SYS_BUS_DEVICE(f.dev), 0);
    for (unsigned bank = 0; bank < 2; bank++) {
        outputs[bank] = qemu_allocate_irq(setup_irq, requests, bank);
        qdev_connect_gpio_out_named(f.dev, "stream-request", bank,
                                    outputs[bank]);
    }

    for (unsigned bank = 0; bank < 2; bank++) {
        hwaddr start = bank ? 0x10e0 : 0x10d0;
        NikonTransferStreamRequest request;
        const void *bytes;
        size_t length;

        for (unsigned i = 0; i < ARRAY_SIZE(incomplete); i++) {
            device_cold_reset(f.dev);
            if (incomplete[i].configured & 1) {
                g_assert_cmpint(write_register(&f, 0x1214, 0xffff), ==,
                                MEMTX_OK);
            }
            if (incomplete[i].configured & 2) {
                g_assert_cmpint(write_register(&f, 0x1298, 0), ==, MEMTX_OK);
            }
            if (incomplete[i].configured & 4) {
                g_assert_cmpint(write_register(&f, start + 4, base), ==,
                                MEMTX_OK);
            }
            if (incomplete[i].configured & 8) {
                g_assert_cmpint(write_register(&f, start + 12,
                    incomplete[i].units64), ==, MEMTX_OK);
            }
            g_assert_cmpint(write_register(&f, start, incomplete[i].start), ==,
                            MEMTX_ERROR);
            assert_stream_idle(&f, requests, levels);
        }

        device_cold_reset(f.dev);
        for (unsigned field = 0; field <= 12; field += 4) {
            /* Stream registers have no read direction, including neighbors. */
            g_assert_cmpint(memory_region_dispatch_read(regs, start + field,
                &value, MO_32, MEMTXATTRS_UNSPECIFIED), ==, MEMTX_ERROR);
            for (unsigned i = 0; i < ARRAY_SIZE(widths); i++) {
                g_assert_cmpint(memory_region_dispatch_write(regs,
                    start + field, 1, widths[i], MEMTXATTRS_UNSPECIFIED), ==,
                    MEMTX_DECODE_ERROR);
                g_assert_cmpint(memory_region_dispatch_read(regs, start + field,
                    &value, widths[i], MEMTXATTRS_UNSPECIFIED), ==,
                    MEMTX_DECODE_ERROR);
            }
        }
        g_assert_cmpint(write_register(&f, start + 8, 0), ==, MEMTX_ERROR);
        assert_stream_idle(&f, requests, levels);
        g_assert_cmpint(write_register(&f, 0x1214, 0xffff), ==, MEMTX_OK);
        g_assert_cmpint(write_register(&f, 0x1298, 0), ==, MEMTX_OK);
        g_assert_cmpint(write_register(&f, start, 1), ==, MEMTX_ERROR);
        assert_stream_idle(&f, requests, levels);

        /* The old two-byte prefix fits RAM; the full eight-byte extent does
         * not. Preflight must reject before reading the neighboring MMIO. */
        g_assert_cmpint(write_register(&f, start + 4,
            base + NIKON_TRANSFER_STREAM_BYTES - 4), ==, MEMTX_OK);
        g_assert_cmpint(write_register(&f, start + 12, 1), ==, MEMTX_OK);
        g_assert_cmpint(write_register(&f, start, 1), ==, MEMTX_ERROR);
        advance();
        assert_stream_idle(&f, requests, levels);

        device_cold_reset(f.dev);
        g_assert_cmpint(write_register(&f, 0x1214, 0xffff), ==, MEMTX_OK);
        g_assert_cmpint(write_register(&f, 0x1298, 0), ==, MEMTX_OK);
        g_assert_cmpint(write_register(&f, start + 4, base), ==, MEMTX_OK);
        g_assert_cmpint(write_register(&f, start + 12,
            NIKON_TRANSFER_STREAM_BYTES / 8 + 1), ==, MEMTX_ERROR);
        g_assert_cmpint(write_register(&f, start, 1), ==, MEMTX_ERROR);
        assert_stream_idle(&f, requests, levels);
        /* Exactly 16384 units64 captures the unchanged 128KiB allocation. */
        g_assert_cmpint(write_register(&f, start + 12,
            NIKON_TRANSFER_STREAM_BYTES / 8), ==, MEMTX_OK);
        g_assert_cmpint(write_register(&f, start, 1), ==, MEMTX_OK);
        g_assert_true(requests[bank]);
        g_assert_false(requests[!bank]);
        g_assert_true(nikon_transfer_stream_request(f.dev, bank, &request));
        g_assert_cmpuint(request.units64, ==, NIKON_TRANSFER_STREAM_BYTES / 8);
        bytes = g_bytes_get_data(request.bytes, &length);
        g_assert_cmpmem(bytes, length, source, NIKON_TRANSFER_STREAM_BYTES);
        g_bytes_unref(request.bytes);
        g_assert_cmpuint(f.reads, ==, 0);
        advance();
        for (unsigned i = 0; i < 3; i++) {
            g_assert_false(levels[i]);
        }
    }
    device_cold_reset(f.dev);
    assert_stream_idle(&f, requests, levels);
    qdev_unrealize(f.dev);
    memory_region_del_subregion(root, &ram);
    memory_region_del_subregion(root, &mmio);
    drain_call_rcu();
    object_unparent(OBJECT(f.dev));
    object_unref(OBJECT(f.dev));
    drain_call_rcu();
    for (unsigned i = 0; i < 3; i++) {
        qemu_free_irq(lines[i]);
    }
    for (unsigned bank = 0; bank < 2; bank++) {
        qemu_free_irq(outputs[bank]);
    }
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
    g_test_add_func("/nikon/transfer/romd-boundary", test_romd_boundary);
    g_test_add_func("/nikon/transfer/empty-configuration",
                    test_empty_configuration);
    g_test_add_func("/nikon/transfer/stream-submission", test_stream_submission);
    g_test_add_func("/nikon/transfer/stream-extent-boundaries",
                    test_stream_extent_boundaries);
    result = g_test_run();
    qemu_cleanup(result);
    unlink(socket);
    rmdir(directory);
    return result;
}
