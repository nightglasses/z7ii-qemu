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

static void build_lut_program(uint8_t *setup, uint8_t *draw,
                              uint32_t source, uint32_t destination)
{
    uint32_t *setup_words = (uint32_t *)setup;
    uint32_t *draw_words = (uint32_t *)draw;

    memset(setup, 0, NIKON_TRANSFER_LUT_SETUP_BYTES);
    memset(draw, 0, NIKON_TRANSFER_LUT_DRAW_BYTES);
    stl_le_p(&setup_words[0], 0x00691004);
    stl_le_p(&setup_words[22], 0x00653004);
    stl_le_p(&setup_words[36], 0x0080f026);
    stl_le_p(&setup_words[37], source);
    stl_le_p(&setup_words[39], 16);
    stl_le_p(&setup_words[40], 0x00800080);
    stl_le_p(&setup_words[42], 0x0006f004);

    stl_le_p(&draw_words[0], 0x01408004);
    stl_le_p(&draw_words[10], 0x01a3f004);
    stl_le_p(&draw_words[11], 0x00200020);
    stl_le_p(&draw_words[87], 0x0188f7fc);
    for (unsigned i = 0; i < 256; i++) {
        stl_le_p(&draw_words[88 + i], i << 4);
    }
    stl_le_p(&draw_words[344], 0x0192f02e);
    stl_le_p(&draw_words[355], 0x0188f0fc);
    for (unsigned i = 0; i < 32; i++) {
        stl_le_p(&draw_words[356 + i], i << 7);
    }
    stl_le_p(&draw_words[396], 0x01a4f004);
    stl_le_p(&draw_words[397], destination);
    stl_le_p(&draw_words[403], 0x01000000);
    stl_le_p(&draw_words[408], 0x00a0f02e);
    stl_le_p(&draw_words[409], source);
    stl_le_p(&draw_words[414], 2);
    stl_le_p(&draw_words[415], 0x00a6f004);
}

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
                                          "test.stream-source", 0x10000, 0,
                                          &error_abort);
    memory_region_add_subregion(root, 0x3000, &ram);
    memory_region_init_io(&mmio, OBJECT(f.dev), &side_effect_ops, &f,
                          "test.stream-mmio", 1024);
    memory_region_add_subregion(root, 0x20000, &mmio);
    source = memory_region_get_ram_ptr(&ram);
    for (unsigned i = 0; i < 0x10000; i++) {
        source[i] = i ^ (i >> 3) ^ 0xa5;
    }
    build_lut_program(source, source + 0x100, 0x6000, 0xb000);
    memset(source + 0x8000, 0x5a, 1024);
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
    g_assert_cmpint(write_register(&f, 0x10d4, 0x3100), ==, MEMTX_ERROR);
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
    g_assert_cmpint(write_register(&f, 0x10e4, 0x3100), ==, MEMTX_OK);
    g_assert_cmpint(write_register(&f, 0x10ec, 211), ==, MEMTX_OK);
    g_assert_cmpint(write_register(&f, 0x10e0, 1), ==, MEMTX_OK);
    g_assert_true(nikon_transfer_stream_request(f.dev, 1, &second));
    g_assert_cmpuint(second.units64, ==, 211);
    g_assert_cmpuint(second.token, !=, first.token);
    snapshot = g_bytes_get_data(second.bytes, &length);
    g_assert_cmpmem(snapshot, length, source + 0x100, 1688);
    g_assert_cmpint(write_register(&f, 0x10e4, 0x3000), ==, MEMTX_ERROR);
    g_assert_cmpint(write_register(&f, 0x10ec, 1), ==, MEMTX_ERROR);
    g_assert_cmpint(write_register(&f, 0x10e0, 1), ==, MEMTX_ERROR);
    g_assert_true(nikon_transfer_stream_request(f.dev, 1, &next));
    g_assert_cmpuint(next.token, ==, second.token);
    g_assert_cmpuint(next.units64, ==, 211);
    g_assert_true(g_bytes_equal(next.bytes, second.bytes));
    g_bytes_unref(next.bytes);
    g_assert_false(nikon_transfer_stream_request(f.dev, 2, &next));
    /* The owned pair is executed before it produces IRQ129. */
    advance();
    g_assert_false(requests[0]);
    g_assert_false(requests[1]);
    g_assert_false(nikon_transfer_stream_request(f.dev, 0, &next));
    g_assert_true(levels[0]);
    g_assert_false(levels[1]);
    g_assert_false(levels[2]);
    for (unsigned y = 0; y < 32; y++) {
        for (unsigned x = 0; x < 32; x++) {
            uint8_t sample = source[0x3000 + y * 4 * 128 + x * 4];

            g_assert_cmphex(source[0x8000 + y * 32 + x], ==,
                            sample & 0xf8);
        }
    }
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
    g_assert_cmpint(write_register(&f, 0x10d4, 0x20000), ==, MEMTX_OK);
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

static void clear_emit(uint8_t *bytes, unsigned *at, uint32_t header,
                       const uint32_t *values, unsigned count)
{
    g_assert_cmpuint(((header >> 3) & 255) + 1, ==, count);
    stl_le_p(bytes + 4 * (*at)++, header);
    for (unsigned i = 0; i < count; i++) {
        stl_le_p(bytes + 4 * (*at)++, values[i]);
    }
}

/*
 * Assemble synthetic register packets from the finite protocol. No original
 * firmware command bytes are included in the fixture.
 */
static void build_clear_program(uint8_t *setup, uint8_t *draw,
                                uint32_t format, uint32_t first,
                                uint32_t second)
{
    unsigned at = 0;
    uint8_t *bytes = setup;
#define EMIT(header, ...) \
    clear_emit(bytes, &at, header, (uint32_t[]) { __VA_ARGS__ }, \
               sizeof((uint32_t[]) { __VA_ARGS__ }) / sizeof(uint32_t))
#define REG(reg, mask, value) EMIT(((reg) << 16) | ((mask) << 12) | 4, value)

    memset(setup, 0, NIKON_TRANSFER_CLEAR_SETUP_BYTES);
    memset(draw, 0, NIKON_TRANSFER_CLEAR_DRAW_BYTES);
    REG(0x69, 1, 0x80);
    for (unsigned i = 0; i < 2; i++) {
        EMIT(0x0041f046, 0x3f800000, 0, 0, 0, 0x3f800000, 0,
             0, 0, 0x3f800000);
    }
    REG(0x65, 3, 0x100);
    REG(0x40, 15, 0x02b00400);
    REG(3, 15, 5);
    REG(5, 15, 0x80000005);
    REG(0x63, 15, 5);
    REG(0x5e, 15, 0x3f800000);
    REG(0x5f, 15, 0);
    EMIT(0x0080f026, 0x12345678, 0, 16, 0x02b00400, 0);
    REG(6, 15, 1);
    EMIT(0xa, 0x07090702, 0);
    EMIT(0x00010022, 0, 6, 1024, 682, 0xfffffc00);
    REG(0x85, 15, 0x10001);
    g_assert_cmpuint(at, ==, 55); /* final zero alignment word */

    bytes = draw;
    at = 0;
    REG(0x140, 8, 0);
    REG(0x190, 1, 0);
    REG(0x14e, 14, 0x1b0000);
    REG(0x156, 14, 0x1b0000);
    REG(0x1a2, 15, format);
    REG(0x1a3, 15, 0x02b00400);
    REG(0x184, 4, 0);
    REG(0x1a7, 15, 0x101);
    REG(0x11c, 15, 0x02b00000);
    REG(0x1a0, 8, 0);
    EMIT(0x00c0f00e, 0x03ff0000, 0x02af0000);
    REG(0x100, 1, 1);
    REG(0x115, 1, 1);
    REG(0x1b0, 1, 13);
    REG(0x1b3, 1, 1);
    EMIT(0x01c1f046, 0x7a8000, 0x1671c6, 0x749, 0x100, 0x6ad259,
         0x75653, 0x7ad200, 0x753132, 0x200);
    REG(0x184, 8, 0);
    REG(0x1a1, 1, 0);
    REG(0x115, 12, 0);
    for (unsigned i = 0; i < 3; i++) {
        REG(0x140, i == 2 ? 2 : 1, 0);
        REG(0x14d, 15, 0);
        REG(0x14d, 15, 0);
    }
    REG(0x184, 1, 8);
    REG(0x186, 8, 0x07000000);
    REG(0x187, 15, 0);
    stl_le_p(draw + 4 * at++, 0x0188f7fc);
    for (unsigned i = 0; i < 256; i++) {
        stl_le_p(draw + 4 * at++, (255 - i) << 4);
    }
    EMIT(0x0192f02e, 0x400, 0, 0x400, 0, 0x400, 0);
    REG(0x186, 8, 0x38000000);
    REG(0x187, 15, 0);
    stl_le_p(draw + 4 * at++, 0x0188f0fc);
    for (unsigned i = 0; i < 32; i++) {
        stl_le_p(draw + 4 * at++, i * 17);
    }
    REG(0x186, 3, 0x10f);
    REG(0x184, 2, 0);
    REG(0x1a0, 2, 0);
    REG(0x1a2, 9, format);
    REG(0x1a4, 15, first);
    REG(0x1b5, 15, second);
    REG(0x1a7, 4, 0);
    REG(0x185, 15, 0);
    REG(0x184, 1, 8);
    REG(0x184, 8, 0);
    REG(0x1a0, 4, 0);
    REG(0x1a5, 1, 0);
    REG(0x1aa, 1, 0);
    EMIT(0x00a0f02e, 0x12345678, 0, 16, 0x02b00400, 0, 0);
    REG(0xa6, 15, 0x10001);
    REG(0x1a6, 1, 1);
    REG(8, 1, 1);
    g_assert_cmpuint(at, ==, 411);
#undef REG
#undef EMIT
}

static void build_rgba_program(uint8_t *setup, uint8_t *draw,
                               uint32_t coordinates, uint32_t segments)
{
    unsigned at = 0;
    uint8_t *bytes = setup;
#define EMIT(header, ...) \
    clear_emit(bytes, &at, header, (uint32_t[]) { __VA_ARGS__ }, \
               sizeof((uint32_t[]) { __VA_ARGS__ }) / sizeof(uint32_t))
#define REG(reg, mask, value) EMIT(((reg) << 16) | ((mask) << 12) | 4, value)

    memset(setup, 0, NIKON_TRANSFER_RGBA_SETUP_BYTES);
    memset(draw, 0, NIKON_TRANSFER_RGBA_DRAW_BYTES);
    REG(0x69, 1, 0x80);
    EMIT(0x005ef00e, 0x3f800000, 0);
    REG(0x63, 15, 5);
    REG(0x40, 15, 0x0ac01000);
    REG(0x40, 15, 0x0ac01000);
    EMIT(0x0041f046, 0x3f800000, 0, 0, 0, 0x3f800000, 0,
         0, 0, 0x3f800000);
    REG(4, 15, coordinates);
    REG(5, 15, 0xd0000005);
    REG(2, 15, segments);
    REG(3, 15, 0x10000005);
    REG(0x65, 3, 0x2100);
    REG(0x66, 15, 0x6004);
    EMIT(0x0080f026, 0x12345000, 0, 0x200, 0x08701000, 0);
    REG(6, 15, 1);
    REG(0x85, 15, 0x10001);
    REG(5, 15, 0xd0000005);
    REG(3, 15, 0x10000005);
    EMIT(0x0080f026, 0x12346000, 0, 0x200, 0x02501000, 0x1f790);
    REG(6, 15, 1);
    REG(0x85, 15, 0x10002);
    g_assert_cmpuint(at, ==, 57); /* final zero alignment word */

    bytes = draw;
    at = 0;
    REG(0x140, 8, 0);
    REG(0x190, 1, 0);
    REG(0x14e, 14, 0x1b0000);
    REG(0x156, 14, 0x1b0000);
    REG(0x1a2, 15, 0);
    REG(0x1a3, 15, 0x02b00400);
    REG(0x184, 4, 0);
    REG(0x1a7, 15, 0x01000101);
    REG(0x11c, 15, 0x02b00000);
    REG(0x1a0, 8, 0);
    REG(0x1aa, 1, 0);
    REG(0x1a0, 4, 0x10000);
    REG(0x184, 1, 0);
    REG(0x184, 2, 0);
    REG(0x1a0, 2, 0);
    REG(0x1a2, 9, 0);
    REG(0x1a4, 15, 0xbf309600);
    REG(0x1a5, 1, 1);
    REG(0x1a7, 4, 0);
    REG(0x140, 1, 0);
    REG(0x14d, 15, 0);
    REG(0x14d, 15, 0);
    EMIT(0x00c0f00e, 0x03ff0000, 0x02af0000);
    REG(0x100, 1, 1);
    REG(0x115, 1, 1);
    REG(0x1b0, 1, 0xd);
    REG(0x1b3, 1, 1);
    REG(0x184, 8, 0);
    REG(0x1a1, 1, 0);
    REG(0x184, 1, 8);
    REG(0x140, 1, 0);
    REG(0x14d, 15, 0);
    REG(0x14d, 15, 0);
    REG(0x140, 2, 0);
    REG(0x14d, 15, 0);
    REG(0x14d, 15, 0);
    REG(0x115, 12, 0);
    REG(0x185, 15, 0xff000000);
    REG(0x186, 3, 0x108);
    EMIT(0x00a0f02e, 0x12345000, 0, 0x200, 0x08701000, 0,
         0x00040002);
    REG(0xa6, 15, 0x10001);
    REG(0x1a6, 1, 1);
    EMIT(0x00a0f02e, 0x12346000, 0, 0x200, 0x02501000, 0x21c,
         0x00040002);
    REG(0xa6, 15, 0x10002);
    REG(0x1a6, 1, 1);
    REG(8, 1, 1);
    g_assert_cmpuint(at, ==, 103); /* final zero alignment word */
#undef REG
#undef EMIT
}

static MemTxResult submit_rgba(TransferFixture *f, unsigned draw_bytes)
{
    const uint32_t packet[][2] = {
        { 0x1214, 0x100ffff }, { 0x1298, 0 },
        { 0x10d4, 0x400000 },
        { 0x10dc, NIKON_TRANSFER_RGBA_SETUP_BYTES / 8 }, { 0x10d0, 1 },
        { 0x10e4, 0x401000 }, { 0x10ec, draw_bytes / 8 },
    };

    for (unsigned i = 0; i < ARRAY_SIZE(packet); i++) {
        g_assert_cmpint(write_register(f, packet[i][0], packet[i][1]), ==,
                        MEMTX_OK);
    }
    return write_register(f, 0x10e0, 1);
}

static void test_rgba_clear(void)
{
    enum { OUTPUT = 0x2b0000, SLOT = 0x4b0000 };
    enum {
        UNKNOWN, TRUNCATED, PATH, FORMAT, RANGE, OVERLAP, TARGET_FIELD,
        SUCCESS, RESET, CANCEL, CASES,
    };
    const hwaddr target = 0xbf309600;
    TransferFixture f = { .dev = qdev_new(TYPE_NIKON_TRANSFER) };
    MemoryRegion commands, surface;
    MemoryRegion *root = get_system_memory();
    uint8_t *command_data, *surface_data;

    object_property_add_child(object_get_objects_root(), "transfer-rgba[*]",
                              OBJECT(f.dev));
    memory_region_init_ram_flags_nomigrate(&commands, OBJECT(f.dev),
                                          "test.rgba-command", 0x3000, 0,
                                          &error_abort);
    memory_region_add_subregion(root, 0x400000, &commands);
    memory_region_init_ram_flags_nomigrate(&surface, OBJECT(f.dev),
                                          "test.rgba-surface", SLOT + 8, 0,
                                          &error_abort);
    memory_region_add_subregion(root, target - 4, &surface);
    command_data = memory_region_get_ram_ptr(&commands);
    surface_data = memory_region_get_ram_ptr(&surface);
    f.irq = qemu_allocate_irq(irq_level, &f, 0);
    sysbus_connect_irq(SYS_BUS_DEVICE(f.dev), 0, f.irq);
    g_assert_true(sysbus_realize(SYS_BUS_DEVICE(f.dev), &error_abort));

    for (unsigned test = UNKNOWN; test < CASES; test++) {
        uint32_t segments = 0x402000;
        unsigned draw_bytes = NIKON_TRANSFER_RGBA_DRAW_BYTES;
        bool accepted = test == SUCCESS || test == RESET || test == CANCEL;
        g_autofree uint8_t *before = NULL;

        device_cold_reset(f.dev);
        memset(command_data, 0, 0x3000);
        memset(surface_data, 0xa5, SLOT + 8);
        build_rgba_program(command_data, command_data + 0x1000,
                           0x402100, segments);
        memcpy(command_data + 0x2000,
               (uint8_t[]) { 2, 7, 9, 7, 0 }, 5);
        for (unsigned i = 0; i < 5; i++) {
            static const uint32_t coordinate[] = {
                0, 0x40c00000, 0x44800000, 0x442a8000, 0xc4800000,
            };
            stl_le_p(command_data + 0x2100 + 4 * i, coordinate[i]);
        }
        switch (test) {
        case UNKNOWN:
            stl_le_p(command_data + 0x1000 + 4, 1);
            break;
        case TRUNCATED:
            draw_bytes -= 8;
            break;
        case PATH:
            command_data[0x2002] ^= 1;
            break;
        case FORMAT:
            stl_le_p(command_data + 0x1000 + 9 * 4, 1);
            break;
        case RANGE:
            memory_region_set_readonly(&surface, true);
            break;
        case OVERLAP:
            stl_le_p(command_data + 26 * 4, target);
            memcpy(surface_data + 4, (uint8_t[]) { 2, 7, 9, 7, 0 }, 5);
            break;
        case TARGET_FIELD:
            stl_le_p(command_data + 0x1000 + 33 * 4, target + 4);
            break;
        default:
            break;
        }
        before = g_memdup2(surface_data, SLOT + 8);
        g_assert_cmpint(submit_rgba(&f, draw_bytes), ==,
                        accepted ? MEMTX_OK : MEMTX_ERROR);
        g_assert_false(f.level);
        if (test == RANGE) {
            memory_region_set_readonly(&surface, false);
        } else if (test == RESET) {
            device_cold_reset(f.dev);
        } else if (test == CANCEL) {
            qdev_unrealize(f.dev);
        }
        advance();
        g_assert_cmpint(f.level, ==, test == SUCCESS);
        if (test == SUCCESS) {
            g_assert_cmpmem(surface_data, 4, before, 4);
            for (unsigned i = 0; i < OUTPUT; i++) {
                g_assert_cmphex(surface_data[4 + i], ==,
                                i % 4 == 3 ? 0xff : 0);
            }
            g_assert_cmpmem(surface_data + 4 + OUTPUT, SLOT - OUTPUT + 4,
                            before + 4 + OUTPUT, SLOT - OUTPUT + 4);
            g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
            g_assert_false(f.level);
        } else {
            g_assert_cmpmem(surface_data, SLOT + 8, before, SLOT + 8);
        }
    }
    memory_region_del_subregion(root, &surface);
    memory_region_del_subregion(root, &commands);
    drain_call_rcu();
    object_unparent(OBJECT(f.dev));
    object_unref(OBJECT(f.dev));
    drain_call_rcu();
    qemu_free_irq(f.irq);
}

static MemTxResult submit_clear(TransferFixture *f, unsigned draw_bytes)
{
    const uint32_t packet[][2] = {
        { 0x1214, 0x100ffff }, { 0x1298, 0 },
        { 0x10d4, 0x400000 }, { 0x10dc, 28 }, { 0x10d0, 1 },
        { 0x10e4, 0x401000 }, { 0x10ec, draw_bytes / 8 },
    };

    for (unsigned i = 0; i < ARRAY_SIZE(packet); i++) {
        g_assert_cmpint(write_register(f, packet[i][0], packet[i][1]), ==,
                        MEMTX_OK);
    }
    return write_register(f, 0x10e0, 1);
}

static void test_yuv_clear(void)
{
    enum { PLANE = 1024 * 682, OFFSET = 0x100000, SIZE = 0x300000 };
    enum { VALID_105, VALID_107, HEADER, TRUNCATED, FORMAT_FIRST,
           FORMAT_SECOND, PACKED_ALPHA, GEOMETRY, COLOR, MODE, KEY, LUT,
           OVERFLOW, OVERFLOW_107, OVERLAP_105, DELTA_105, OVERLAP_107,
           DELTA_107, MMIO, STREAM_OVERLAP, STREAM_OVERLAP_107, BLEND,
           TRIGGER, UNBACKED, RESET, MAP_LOSS, BACKING_ALIAS, UNREALIZE,
           CASES };
    TransferFixture f = { .dev = qdev_new(TYPE_NIKON_TRANSFER) };
    MemoryRegion ram, mmio, alias;
    MemoryRegion *root = get_system_memory();
    uint8_t *data;

    object_property_add_child(object_get_objects_root(), "transfer-clear[*]",
                              OBJECT(f.dev));
    memory_region_init_ram_flags_nomigrate(&ram, OBJECT(f.dev),
                                          "test.clear-ram", SIZE, 0,
                                          &error_abort);
    memory_region_add_subregion(root, 0x400000, &ram);
    memory_region_init_io(&mmio, OBJECT(f.dev), &side_effect_ops, &f,
                          "test.clear-mmio", 2 * PLANE);
    memory_region_add_subregion(root, 0x800000, &mmio);
    memory_region_init_alias(&alias, OBJECT(f.dev), "test.clear-alias",
                             &ram, OFFSET, PLANE);
    data = memory_region_get_ram_ptr(&ram);
    f.irq = qemu_allocate_irq(irq_level, &f, 0);
    sysbus_connect_irq(SYS_BUS_DEVICE(f.dev), 0, f.irq);
    g_assert_true(sysbus_realize(SYS_BUS_DEVICE(f.dev), &error_abort));

    for (unsigned test = 0; test < CASES; test++) {
        unsigned length = NIKON_TRANSFER_CLEAR_DRAW_BYTES;
        bool complete = test == VALID_105 || test == VALID_107;
        bool profile107 = test == VALID_107 || test == OVERFLOW_107 ||
                          test == OVERLAP_107 || test == DELTA_107 ||
                          test == STREAM_OVERLAP_107 || test >= RESET;
        bool accepted = complete || test >= RESET;
        uint32_t format = profile107 ? 0x107 : 0x105;
        uint32_t primary = profile107 ? 2 * PLANE : PLANE;
        uint8_t setup[NIKON_TRANSFER_CLEAR_SETUP_BYTES];
        uint8_t draw[NIKON_TRANSFER_CLEAR_DRAW_BYTES];

        device_cold_reset(f.dev);
        memset(data, 0xa5, SIZE);
        build_clear_program(data, data + 0x1000, format, 0x500000,
                            0x500000 + primary);
        switch (test) {
        case HEADER:
            stl_le_p(data + 0x1000 + 71 * 4, 0x0188f7f4);
            break;
        case TRUNCATED:
            length -= 8;
            break;
        case FORMAT_FIRST:
            stl_le_p(data + 0x1000 + 9 * 4, 0x107);
            break;
        case FORMAT_SECOND:
            stl_le_p(data + 0x1000 + 379 * 4, 0x107);
            break;
        case PACKED_ALPHA:
            stl_le_p(data + 0x1000 + 9 * 4, 0x01000107);
            stl_le_p(data + 0x1000 + 379 * 4, 0x01000107);
            break;
        case GEOMETRY:
            stl_le_p(data + 50 * 4, 1023);
            break;
        case COLOR:
            stl_le_p(data + 0x1000 + 387 * 4, 1);
            break;
        case MODE:
            stl_le_p(data + 0x1000 + 389 * 4, 2);
            break;
        case KEY:
            stl_le_p(data + 37 * 4, 0x76543210);
            break;
        case LUT:
            stl_le_p(data + 0x1000 + 72 * 4, 0x1000);
            break;
        case OVERFLOW:
            stl_le_p(data + 0x1000 + 383 * 4, 0xfffff000);
            break;
        case OVERFLOW_107:
            stl_le_p(data + 0x1000 + 381 * 4, 0xfffff000);
            break;
        case OVERLAP_105:
            stl_le_p(data + 0x1000 + 383 * 4, 0x500001);
            break;
        case DELTA_105:
            stl_le_p(data + 0x1000 + 383 * 4, 0x500001 + PLANE);
            break;
        case OVERLAP_107:
            stl_le_p(data + 0x1000 + 383 * 4, 0x500000 + PLANE);
            break;
        case DELTA_107:
            stl_le_p(data + 0x1000 + 383 * 4, 0x500001 + 2 * PLANE);
            break;
        case MMIO:
            stl_le_p(data + 0x1000 + 381 * 4, 0x800000);
            stl_le_p(data + 0x1000 + 383 * 4, 0x800000 + PLANE);
            break;
        case STREAM_OVERLAP:
            stl_le_p(data + 0x1000 + 381 * 4, 0x400000);
            stl_le_p(data + 0x1000 + 383 * 4, 0x400000 + PLANE);
            break;
        case STREAM_OVERLAP_107:
            /* Source400000 lies beyond a105 plane but inside this107 plane. */
            stl_le_p(data + 0x1000 + 381 * 4, 0x355000);
            stl_le_p(data + 0x1000 + 383 * 4, 0x4aa000);
            break;
        case BLEND:
            stl_le_p(data + 0x1000 + 377 * 4, 0x10000);
            break;
        case TRIGGER:
            stl_le_p(data + 0x1000 + 408 * 4, 2);
            break;
        case UNBACKED:
            stl_le_p(data + 0x1000 + 381 * 4, 0x700000);
            stl_le_p(data + 0x1000 + 383 * 4, 0x700000 + PLANE);
            break;
        default:
            break;
        }
        memcpy(setup, data, sizeof(setup));
        memcpy(draw, data + 0x1000, sizeof(draw));
        g_assert_cmpint(submit_clear(&f, length), ==,
                        accepted ? MEMTX_OK : MEMTX_ERROR);
        g_assert_false(f.level);
        for (unsigned i = 0; i < primary + PLANE; i++) {
            g_assert_cmphex(data[OFFSET + i], ==, 0xa5);
        }
        if (test == RESET) {
            device_cold_reset(f.dev);
        } else if (test == UNREALIZE) {
            qdev_unrealize(f.dev);
        } else if (test == MAP_LOSS || test == BACKING_ALIAS) {
            memory_region_set_readonly(&alias, test == MAP_LOSS);
            memory_region_add_subregion_overlap(root, 0x500000 + primary,
                                                &alias, 1);
        }
        advance();
        g_assert_cmpmem(data, sizeof(setup), setup, sizeof(setup));
        g_assert_cmpmem(data + 0x1000, sizeof(draw), draw, sizeof(draw));
        g_assert_cmpint(f.level, ==, complete);
        for (unsigned i = 0; i < SIZE; i++) {
            if (i >= OFFSET && i < OFFSET + primary + PLANE) {
                uint8_t expected = 0xa5;

                if (complete && format == 0x105) {
                    expected = i < OFFSET + PLANE ? 16 : 128;
                } else if (complete && i < OFFSET + primary) {
                    expected = (i - OFFSET) % 4 < 2 ? 16 : 128;
                } else if (complete) {
                    expected = 0;
                }
                g_assert_cmphex(data[i], ==, expected);
            } else if (i >= 0x2000) {
                g_assert_cmphex(data[i], ==, 0xa5);
            }
        }
        g_assert_cmpuint(f.reads, ==, 0);
        if (complete) {
            g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
            g_assert_false(f.level);
        } else if (test == MAP_LOSS || test == BACKING_ALIAS) {
            memory_region_del_subregion(root, &alias);
        }
    }
    memory_region_del_subregion(root, &mmio);
    memory_region_del_subregion(root, &ram);
    drain_call_rcu();
    object_unparent(OBJECT(f.dev));
    object_unref(OBJECT(f.dev));
    drain_call_rcu();
    qemu_free_irq(f.irq);
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
    g_test_add_func("/nikon/transfer/yuv-clear", test_yuv_clear);
    g_test_add_func("/nikon/transfer/rgba-clear", test_rgba_clear);
    result = g_test_run();
    qemu_cleanup(result);
    unlink(socket);
    rmdir(directory);
    return result;
}
