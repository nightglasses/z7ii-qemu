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
#include "hw/nikon/path.h"
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
        /* The original wait path kicks 1214 with bit24 while the streams are
         * still pending (sub_106ee1f8 case 8/10).  It must be accepted and
         * must not overwrite the retained configuration. */
        g_assert_cmpint(write_register(f, 0x1214, 0x1000000), ==, MEMTX_OK);
        {
            MemoryRegion *regs = sysbus_mmio_get_region(SYS_BUS_DEVICE(f->dev),
                                                        0);
            uint64_t value = 0;

            g_assert_cmpint(memory_region_dispatch_read(regs, 0x1214, &value,
                                                        MO_32,
                                                        MEMTXATTRS_UNSPECIFIED),
                            ==, MEMTX_OK);
            g_assert_cmphex(value, ==, 0xffff);
        }
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

static void build_convert_program(uint8_t *setup, uint8_t *draw)
{
    uint8_t clear_setup[NIKON_TRANSFER_CLEAR_SETUP_BYTES];
    uint8_t clear_draw[NIKON_TRANSFER_CLEAR_DRAW_BYTES];
    unsigned at = 0;
    uint8_t *bytes = setup;
    static const uint32_t secondary[] = {
        0, 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128, 192,
        256, 384, 512, 768, 1024, 1280, 1536, 1792, 2048, 2304,
        2560, 2816, 3072, 3328, 3584, 3840,
    };

#define EMIT(header, ...) \
    clear_emit(bytes, &at, header, (uint32_t[]) { __VA_ARGS__ }, \
               sizeof((uint32_t[]) { __VA_ARGS__ }) / sizeof(uint32_t))
#define REG(r, m, v) EMIT(((r) << 16) | ((m) << 12) | 4, v)
    memset(setup, 0, 224);
    memset(draw, 0, 1736);
    build_clear_program(clear_setup, clear_draw, 0x105, 0x800000, 0x8aa800);
    REG(0x69, 1, 0x80);
    EMIT(0x0041f046, 0x3f800000, 0, 0, 0, 0x3f800000, 0, 0, 0, 0x3f800000);
    EMIT(0x0041f046, 0x3f800000, 0, 0, 0, 0xbf800000, 0,
         0, 0x442c0000, 0x3f800000);
    REG(0x65, 3, 0x2100);
    REG(3, 15, 5);
    REG(5, 15, 0x80000005);
    REG(0x63, 15, 5);
    REG(0x5e, 15, 0x3f800000);
    REG(0x5f, 15, 0);
    REG(0x40, 15, 0x0ac01000);
    EMIT(0x0080f026, 0x12345000, 0, 16, 0x0ac01000, 0);
    REG(6, 15, 1);
    EMIT(0x0a, 0x07090702, 0);
    EMIT(0x00010022, 0, 0, 1024, 682, -1024);
    REG(0x85, 15, 0x10001);
    g_assert_cmpuint(at, ==, 55);
    bytes = draw;
    memcpy(draw, clear_draw, 45 * 4);
    at = 45;
    EMIT(0x010c7046, 0x350000, 0, 0, 0, 0xb58064, 0x3f0241, 0, 0, 0x3f0000);
    REG(0x158, 15, 0x040002aa);
    REG(0x156, 7, 0x001b0100);
    REG(0x152, 15, 0x040002b0);
    REG(0x140, 1, 2);
    REG(0x14d, 15, 0);
    REG(0x14d, 15, 0);
    REG(0x140, 4, 0x210000);
    REG(0x155, 15, 0x410000);
    REG(0x1a0, 4, 0);
    REG(0x184, 1, 0);
    REG(0x140, 1, 2);
    REG(0x14d, 15, 0);
    REG(0x14d, 15, 0);
    REG(0x140, 2, 0);
    REG(0x14d, 15, 0);
    REG(0x14d, 15, 0);
    REG(0x115, 12, 0x01000000);
    REG(0x1aa, 1, 0);
    REG(0x184, 1, 2);
    REG(0x186, 8, 0x07000000);
    REG(0x187, 15, 0);
    stl_le_p(draw + at++ * 4, 0x0188f7fc);
    for (unsigned i = 0; i < 256; i++) {
        stl_le_p(draw + at++ * 4, i * 16);
    }
    EMIT(0x0192f02e, 0x400, 0, 0x400, 0, 0x400, 0);
    REG(0x186, 8, 0x38000000);
    REG(0x187, 15, 0);
    stl_le_p(draw + at++ * 4, 0x0188f0fc);
    for (unsigned i = 0; i < 32; i++) {
        stl_le_p(draw + at++ * 4, secondary[i]);
    }
    REG(0x186, 3, 0x10f);
    REG(0x184, 2, 0);
    REG(0x1a0, 2, 0);
    REG(0x1a2, 9, 0x105);
    REG(0x1a4, 15, 0x800000);
    REG(0x1b5, 15, 0x8aa800);
    REG(0x1a5, 1, 0);
    REG(0x1a7, 4, 0);
    REG(0x115, 8, 0x01000000);
    REG(0x153, 1, 1);
    REG(0x143, 1, 1);
    EMIT(0x00a0f02e, 0x12345000, 0, 16, 0x0ac01000, 0, 2);
    REG(0xa6, 15, 0x10001);
    REG(0x1a6, 1, 1);
    REG(8, 1, 1);
    g_assert_cmpuint(at, ==, 433);
#undef REG
#undef EMIT
}

static void build_rgba_program(uint8_t *setup, uint8_t *draw,
                               uint32_t coordinates, uint32_t segments,
                               bool single)
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
    REG(0x40, 15, single ? 0x07800b40 : 0x0ac01000);
    REG(0x40, 15, single ? 0x07800b40 : 0x0ac01000);
    EMIT(0x0041f046, 0x3f800000, 0, 0, 0, 0x3f800000, 0,
         0, 0, 0x3f800000);
    REG(4, 15, coordinates);
    REG(5, 15, 0xd0000005);
    REG(2, 15, segments);
    REG(3, 15, 0x10000005);
    REG(0x65, 3, 0x2100);
    REG(0x66, 15, 0x6004);
    EMIT(0x0080f026, 0x12345000, 0, 0x200,
         single ? 0x07800b40 : 0x08701000, 0);
    REG(6, 15, 1);
    REG(0x85, 15, 0x10001);
    if (!single) {
        REG(5, 15, 0xd0000005);
        REG(3, 15, 0x10000005);
        EMIT(0x0080f026, 0x12346000, 0, 0x200, 0x02501000, 0x1f790);
        REG(6, 15, 1);
        REG(0x85, 15, 0x10002);
    }
    g_assert_cmpuint(at, ==, single ? 43 : 57);

    bytes = draw;
    at = 0;
    REG(0x140, 8, 0);
    REG(0x190, 1, 0);
    REG(0x14e, 14, 0x1b0000);
    REG(0x156, 14, 0x1b0000);
    REG(0x1a2, 15, 0);
    REG(0x1a3, 15, single ? 0x01e002d0 : 0x02b00400);
    REG(0x184, 4, 0);
    REG(0x1a7, 15, 0x01000101);
    REG(0x11c, 15, single ? 0x01e00000 : 0x02b00000);
    REG(0x1a0, 8, 0);
    REG(0x1aa, 1, 0);
    REG(0x1a0, 4, 0x10000);
    REG(0x184, 1, 0);
    REG(0x184, 2, 0);
    REG(0x1a0, 2, 0);
    REG(0x1a2, 9, 0);
    REG(0x1a4, 15, single ? 0xc175d600 : 0xbf309600);
    REG(0x1a5, 1, 1);
    REG(0x1a7, 4, 0);
    REG(0x140, 1, 0);
    REG(0x14d, 15, 0);
    REG(0x14d, 15, 0);
    EMIT(0x00c0f00e, single ? 0x02cf0000 : 0x03ff0000,
         single ? 0x01df0000 : 0x02af0000);
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
    EMIT(0x00a0f02e, 0x12345000, 0, 0x200,
         single ? 0x07800b40 : 0x08701000, 0,
         0x00040002);
    REG(0xa6, 15, 0x10001);
    REG(0x1a6, 1, 1);
    if (!single) {
        EMIT(0x00a0f02e, 0x12346000, 0, 0x200, 0x02501000, 0x21c,
             0x00040002);
        REG(0xa6, 15, 0x10002);
        REG(0x1a6, 1, 1);
    }
    REG(8, 1, 1);
    g_assert_cmpuint(at, ==, single ? 92 : 103);
#undef REG
#undef EMIT
}

static MemTxResult submit_rgba(TransferFixture *f, unsigned setup_bytes,
                               unsigned draw_bytes)
{
    const uint32_t packet[][2] = {
        { 0x1214, 0x100ffff }, { 0x1298, 0 },
        { 0x10d4, 0x400000 },
        { 0x10dc, setup_bytes / 8 }, { 0x10d0, 1 },
        { 0x10e4, 0x401000 }, { 0x10ec, draw_bytes / 8 },
    };

    for (unsigned i = 0; i < ARRAY_SIZE(packet); i++) {
        g_assert_cmpint(write_register(f, packet[i][0], packet[i][1]), ==,
                        MEMTX_OK);
    }
    return write_register(f, 0x10e0, 1);
}

static void test_rgba_clear_profile(bool single)
{
    const unsigned extent = single ? 0x151800 : 0x2b0000;
    const unsigned active_offset = single ? 0 : 0x6000;
    const unsigned active = extent - active_offset;
    const unsigned slot = 0x4b0000;
    enum {
        UNKNOWN, TRUNCATED, SETUP_TRUNCATED, PATH, COORDINATE, FORMAT,
        GEOMETRY, CLIP, ENDING, OPAQUE_ZERO, OPAQUE_EQUAL,
        OPAQUE_ECHO, RANGE, OVERLAP, TARGET_FIELD, INITIAL_CLEAR,
        INITIAL_UNKNOWN, FINAL_CLEAR, SUCCESS, MAP_LOSS,
        RESET, CANCEL, CASES,
    };
    const hwaddr target = single ? 0xc175d600 : 0xbf309600;
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
                                          "test.rgba-surface", slot + 8, 0,
                                          &error_abort);
    memory_region_add_subregion(root, target - 4, &surface);
    command_data = memory_region_get_ram_ptr(&commands);
    surface_data = memory_region_get_ram_ptr(&surface);
    f.irq = qemu_allocate_irq(irq_level, &f, 0);
    sysbus_connect_irq(SYS_BUS_DEVICE(f.dev), 0, f.irq);
    g_assert_true(sysbus_realize(SYS_BUS_DEVICE(f.dev), &error_abort));

    for (unsigned test = UNKNOWN; test < CASES; test++) {
        uint32_t segments = 0x402000;
        unsigned setup_bytes = single ? NIKON_TRANSFER_RGBA_SINGLE_SETUP_BYTES :
                                        NIKON_TRANSFER_RGBA_SETUP_BYTES;
        unsigned draw_bytes = single ? NIKON_TRANSFER_RGBA_SINGLE_DRAW_BYTES :
                                       NIKON_TRANSFER_RGBA_DRAW_BYTES;
        bool complete = test == SUCCESS || test == INITIAL_CLEAR;
        bool accepted = complete || test == MAP_LOSS ||
                        test == RESET || test == CANCEL;
        g_autofree uint8_t *before = NULL;

        if (single && test == OPAQUE_EQUAL) {
            continue; /* Only the two-path profile has a second operand. */
        }
        device_cold_reset(f.dev);
        memset(command_data, 0, 0x3000);
        memset(surface_data, 0xa5, slot + 8);
        build_rgba_program(command_data, command_data + 0x1000,
                           0x402100, segments, single);
        memcpy(command_data + 0x2000,
               (uint8_t[]) { 2, 7, 9, 7, 0 }, 5);
        for (unsigned i = 0; i < 5; i++) {
            const uint32_t coordinate[] = {
                0, single ? 0 : 0x40c00000,
                single ? 0x44340000 : 0x44800000,
                single ? 0x43f00000 : 0x442a8000,
                single ? 0xc4340000 : 0xc4800000,
            };
            stl_le_p(command_data + 0x2100 + 4 * i, coordinate[i]);
        }
        if (test == INITIAL_CLEAR || test == MAP_LOSS ||
            test == RESET || test == CANCEL) {
            stl_le_p(command_data + 0x1000 + 25 * 4, 8);
        }
        switch (test) {
        case UNKNOWN:
            stl_le_p(command_data + 0x1000 + 4, 1);
            break;
        case TRUNCATED:
            draw_bytes -= 8;
            break;
        case SETUP_TRUNCATED:
            setup_bytes -= 8;
            break;
        case PATH:
            command_data[0x2002] ^= 1;
            break;
        case COORDINATE:
            stl_le_p(command_data + 0x2108, 0x7fc00000);
            break;
        case FORMAT:
            stl_le_p(command_data + 0x1000 + 9 * 4, 1);
            break;
        case GEOMETRY:
            command_data[0x1000 + 11 * 4] ^= 1;
            break;
        case CLIP:
            command_data[0x1000 + 45 * 4] ^= 1;
            break;
        case ENDING:
            stl_le_p(command_data + 0x1000 + (single ? 91 : 102) * 4, 0);
            break;
        case OPAQUE_ZERO:
            stl_le_p(command_data + 34 * 4, 0);
            stl_le_p(command_data + 0x1000 + 80 * 4, 0);
            break;
        case OPAQUE_EQUAL:
            stl_le_p(command_data + 48 * 4, 0x12345000);
            stl_le_p(command_data + 0x1000 + 91 * 4, 0x12345000);
            break;
        case OPAQUE_ECHO:
            stl_le_p(command_data + 0x1000 + 80 * 4, 0x12345004);
            break;
        case RANGE:
            memory_region_set_readonly(&surface, true);
            break;
        case OVERLAP:
            stl_le_p(command_data + 26 * 4, target);
            memcpy(surface_data + 4, (uint8_t[]) { 2, 7, 9, 7, 0 }, 5);
            break;
        case TARGET_FIELD:
            stl_le_p(command_data + 0x1000 + 33 * 4,
                     target + slot - extent + 8);
            break;
        case INITIAL_UNKNOWN:
            stl_le_p(command_data + 0x1000 + 25 * 4, 0x18);
            break;
        case FINAL_CLEAR:
            stl_le_p(command_data + 0x1000 + 60 * 4, 0);
            break;
        default:
            break;
        }
        before = g_memdup2(surface_data, slot + 8);
        g_assert_cmpint(submit_rgba(&f, setup_bytes, draw_bytes), ==,
                        accepted ? MEMTX_OK : MEMTX_ERROR);
        g_assert_false(f.level);
        if (test == RANGE) {
            memory_region_set_readonly(&surface, false);
        } else if (test == MAP_LOSS) {
            memory_region_set_readonly(&surface, true);
        } else if (test == RESET) {
            device_cold_reset(f.dev);
        } else if (test == CANCEL) {
            qdev_unrealize(f.dev);
        }
        advance();
        if (test == MAP_LOSS) {
            memory_region_set_readonly(&surface, false);
        }
        g_assert_cmpint(f.level, ==, complete);
        if (complete) {
            g_assert_cmpmem(surface_data, 4 + active_offset,
                            before, 4 + active_offset);
            for (unsigned i = 0; i < active; i++) {
                g_assert_cmphex(surface_data[4 + active_offset + i], ==,
                                i % 4 == 3 ? 0xff : 0);
            }
            g_assert_cmpuint(active_offset + active, ==, extent);
            g_assert_cmpmem(surface_data + 4 + extent, slot - extent + 4,
                            before + 4 + extent, slot - extent + 4);
            g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
            g_assert_false(f.level);
        } else {
            g_assert_cmpmem(surface_data, slot + 8, before, slot + 8);
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

static void test_rgba_clear(void)
{
    test_rgba_clear_profile(false);
}

static void test_rgba_single_clear(void)
{
    test_rgba_clear_profile(true);
}

static void build_rgba_gray_program(uint8_t *setup, uint8_t *draw)
{
    uint8_t full_setup[NIKON_TRANSFER_RGBA_SETUP_BYTES];
    uint8_t full_draw[NIKON_TRANSFER_RGBA_DRAW_BYTES];

    build_rgba_program(full_setup, full_draw, 0x402100, 0x402000, true);
    /* The continuation omits register69 and full target/clip initialization. */
    memcpy(setup, full_setup + 2 * 4, NIKON_TRANSFER_RGBA_GRAY_SETUP_BYTES);
    memcpy(draw, full_draw + 20 * 4, 18 * 4);
    memcpy(draw + 18 * 4, full_draw + 38 * 4, 6 * 4);
    memcpy(draw + 24 * 4, full_draw + 59 * 4, 33 * 4);
    stl_le_p(draw + 57 * 4, 0);
    stl_le_p(setup + 32 * 4, 0x12346000);
    stl_le_p(setup + 40 * 4, 0x10002);
    stl_le_p(draw + 5 * 4, 8);
    stl_le_p(draw + 41 * 4, 0xffb4b4b4);
    stl_le_p(draw + 45 * 4, 0x12346000);
    stl_le_p(draw + 52 * 4, 0x10002);
}

static void test_rgba_gray(void)
{
    enum { EXTENT = 720 * 480 * 4, SLOT = 0x4b0000 };
    enum {
        SUCCESS, COLD, OTHER_CONTEXT, CANCELED_INIT, FAILED_INIT, RESET_CONTEXT,
        UNKNOWN, SHORT_SETUP, SHORT_DRAW, PATH, COORDINATE, COLOR, TARGET,
        KEY_ZERO, KEY_ECHO, MODE, ENDING, RANGE, OVERLAP, REJECT_INVALIDATE,
        REPEAT, MAP_LOSS, RESET_PENDING, CANCEL, CASES,
    };
    TransferFixture f = { .dev = qdev_new(TYPE_NIKON_TRANSFER) };
    MemoryRegion commands, surface, other;
    MemoryRegion *root = get_system_memory();
    uint8_t *command_data, *surface_data;

    object_property_add_child(object_get_objects_root(), "transfer-gray[*]",
                              OBJECT(f.dev));
    memory_region_init_ram_flags_nomigrate(&commands, OBJECT(f.dev),
                                          "test.gray-command", 0x3000, 0,
                                          &error_abort);
    memory_region_add_subregion(root, 0x400000, &commands);
    memory_region_init_ram_flags_nomigrate(&surface, OBJECT(f.dev),
                                          "test.gray-surface", SLOT + 8, 0,
                                          &error_abort);
    memory_region_add_subregion(root, 0xc175d600 - 4, &surface);
    memory_region_init_ram_flags_nomigrate(&other, OBJECT(f.dev),
                                          "test.gray-other", 0x2b0000, 0,
                                          &error_abort);
    memory_region_add_subregion(root, 0xbf309600, &other);
    command_data = memory_region_get_ram_ptr(&commands);
    surface_data = memory_region_get_ram_ptr(&surface);
    f.irq = qemu_allocate_irq(irq_level, &f, 0);
    sysbus_connect_irq(SYS_BUS_DEVICE(f.dev), 0, f.irq);
    g_assert_true(sysbus_realize(SYS_BUS_DEVICE(f.dev), &error_abort));

    for (unsigned test = SUCCESS; test < CASES; test++) {
        unsigned setup_bytes = NIKON_TRANSFER_RGBA_GRAY_SETUP_BYTES;
        unsigned draw_bytes = NIKON_TRANSFER_RGBA_GRAY_DRAW_BYTES;
        bool complete = test == SUCCESS || test == REPEAT || test == COLOR;
        bool accepted = complete || test == MAP_LOSS ||
                        test == RESET_PENDING || test == CANCEL;
        g_autofree uint8_t *before = NULL;

        device_cold_reset(f.dev);
        memset(surface_data, 0xa5, SLOT + 8);
        memset(command_data, 0, 0x3000);
        memcpy(command_data + 0x2000,
               (uint8_t[]) { 2, 7, 9, 7, 0 }, 5);
        if (test != COLD) {
            const uint32_t coordinate[] = {
                0, 0, 0x44340000, 0x43f00000, 0xc4340000,
            };

            build_rgba_program(command_data, command_data + 0x1000,
                               0x402100, 0x402000, true);
            for (unsigned i = 0; i < ARRAY_SIZE(coordinate); i++) {
                stl_le_p(command_data + 0x2100 + i * 4, coordinate[i]);
            }
            g_assert_cmpint(submit_rgba(&f,
                            NIKON_TRANSFER_RGBA_SINGLE_SETUP_BYTES,
                            NIKON_TRANSFER_RGBA_SINGLE_DRAW_BYTES), ==, MEMTX_OK);
            g_assert_false(f.level);
            if (test == CANCELED_INIT) {
                device_cold_reset(f.dev);
            } else if (test == FAILED_INIT) {
                memory_region_set_readonly(&surface, true);
            }
            advance();
            g_assert_cmpint(f.level, ==,
                            test != CANCELED_INIT && test != FAILED_INIT);
            memory_region_set_readonly(&surface, false);
            g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
        }
        if (test == OTHER_CONTEXT) {
            const uint32_t coordinate[] = {
                0, 0x40c00000, 0x44800000, 0x442a8000, 0xc4800000,
            };

            build_rgba_program(command_data, command_data + 0x1000,
                               0x402100, 0x402000, false);
            for (unsigned i = 0; i < ARRAY_SIZE(coordinate); i++) {
                stl_le_p(command_data + 0x2100 + i * 4, coordinate[i]);
            }
            g_assert_cmpint(submit_rgba(&f, NIKON_TRANSFER_RGBA_SETUP_BYTES,
                            NIKON_TRANSFER_RGBA_DRAW_BYTES), ==, MEMTX_OK);
            advance();
            g_assert_true(f.level);
            g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
        } else if (test == RESET_CONTEXT) {
            device_cold_reset(f.dev);
        }
        build_rgba_gray_program(command_data, command_data + 0x1000);
        for (unsigned i = 0; i < 5; i++) {
            static const uint32_t coordinate[] = {
                0x42c00000, 0x42ac0000, 0x44040000, 0x439a0000, 0xc4040000,
            };
            stl_le_p(command_data + 0x2100 + i * 4, coordinate[i]);
        }
        switch (test) {
        case UNKNOWN:
            command_data[4] ^= 1;
            break;
        case SHORT_SETUP:
            setup_bytes -= 8;
            break;
        case SHORT_DRAW:
            draw_bytes -= 8;
            break;
        case PATH:
            command_data[0x2002] = 12;
            break;
        case COORDINATE:
            stl_le_p(command_data + 0x2108, 0x7fc00000);
            break;
        case COLOR:
            stl_le_p(command_data + 0x1000 + 41 * 4, 0x7fb4b4b4);
            break;
        case REJECT_INVALIDATE:
            stl_le_p(command_data + 0x1000 + 25 * 4, 0);
            break;
        case TARGET:
            stl_le_p(command_data + 0x1000 + 13 * 4, 0xc175d604);
            break;
        case KEY_ZERO:
            stl_le_p(command_data + 32 * 4, 0);
            stl_le_p(command_data + 0x1000 + 45 * 4, 0);
            break;
        case KEY_ECHO:
            stl_le_p(command_data + 0x1000 + 45 * 4, 4);
            break;
        case MODE:
            stl_le_p(command_data + 0x1000 + 25 * 4, 0);
            break;
        case ENDING:
            stl_le_p(command_data + 0x1000 + 56 * 4, 0);
            break;
        case RANGE:
            memory_region_set_readonly(&surface, true);
            break;
        case OVERLAP:
            stl_le_p(command_data + 20 * 4, 0xc175d600);
            break;
        default:
            break;
        }
        before = g_memdup2(surface_data, SLOT + 8);
        g_assert_cmpint(submit_rgba(&f, setup_bytes, draw_bytes), ==,
                        accepted ? MEMTX_OK : MEMTX_ERROR);
        g_assert_false(f.level);
        if (test == RANGE) {
            memory_region_set_readonly(&surface, false);
        } else if (test == MAP_LOSS) {
            memory_region_set_readonly(&surface, true);
        } else if (test == RESET_PENDING) {
            device_cold_reset(f.dev);
        } else if (test == CANCEL) {
            qdev_unrealize(f.dev);
        }
        advance();
        memory_region_set_readonly(&surface, false);
        g_assert_cmpint(f.level, ==, complete);
        if (complete) {
            for (unsigned y = 0; y < 480; y++) {
                for (unsigned x = 0; x < 720; x++) {
                    uint32_t expected = x >= 96 && x < 624 &&
                                        y >= 86 && y < 394 ?
                                        (test == COLOR ? 0xff5a5a5a :
                                         0xffb4b4b4) : 0xff000000;

                    uint32_t actual = ldl_le_p(surface_data + 4 +
                                               (y * 720 + x) * 4);

                    g_assert_cmphex(actual, ==, expected);
                }
            }
            g_assert_cmpmem(surface_data, 4, before, 4);
            g_assert_cmpmem(surface_data + 4 + EXTENT, SLOT - EXTENT + 4,
                            before + 4 + EXTENT, SLOT - EXTENT + 4);
            g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
        } else {
            g_assert_cmpmem(surface_data, SLOT + 8, before, SLOT + 8);
        }
        if (test == REPEAT || test == REJECT_INVALIDATE ||
            test == MAP_LOSS || test == RESET_PENDING) {
            stl_le_p(command_data + 0x1000 + 41 * 4, 0xffb4b4b4);
            stl_le_p(command_data + 0x1000 + 25 * 4, 8);
            g_assert_cmpint(submit_rgba(&f, NIKON_TRANSFER_RGBA_GRAY_SETUP_BYTES,
                            NIKON_TRANSFER_RGBA_GRAY_DRAW_BYTES), ==,
                            test == REPEAT ? MEMTX_OK : MEMTX_ERROR);
            advance();
            g_assert_cmpint(f.level, ==, test == REPEAT);
        }
    }
    memory_region_del_subregion(root, &other);
    memory_region_del_subregion(root, &surface);
    memory_region_del_subregion(root, &commands);
    drain_call_rcu();
    object_unparent(OBJECT(f.dev));
    object_unref(OBJECT(f.dev));
    drain_call_rcu();
    qemu_free_irq(f.irq);
}

static void path_floats(uint8_t *destination, const float *values, unsigned count)
{
    for (unsigned i = 0; i < count; i++) {
        uint32_t bits;

        memcpy(&bits, &values[i], 4);
        stl_le_p(destination + i * 4, bits);
    }
}

static void build_path_program(uint8_t *data)
{
    uint8_t setup[NIKON_TRANSFER_RGBA_GRAY_SETUP_BYTES];
    uint8_t draw[NIKON_TRANSFER_RGBA_GRAY_DRAW_BYTES];
    const float matrix[] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
    const float polygon[] = {
        10, 10, 30, 10, 30, 30, 10, 30,
        15, 15, 15, 25, 25, 25, 25, 15,
    };
    const float quadratic[] = { 40, 10, 60, 10, 60, 30, 40, 30 };

    build_rgba_gray_program(setup, draw);
    for (unsigned i = 0; i < 2; i++) {
        uint8_t *s = data + i * 41 * 4;
        uint8_t *d = data + 0x1000 + i * 55 * 4;
        uint32_t ncoord = i ? 8 : 16, nseg = i ? 4 : 10;

        memcpy(s, setup, 41 * 4);
        memcpy(d, draw, 55 * 4);
        stl_le_p(s + 4 * 4, nseg);
        path_floats(s + 10 * 4, matrix, 9);
        path_floats(s + 10 * 4, (float[]) { i ? 1 : 2 }, 1);
        path_floats(s + 16 * 4, (float[]) { i ? 100 : 4, i ? 0 : 3 }, 2);
        stl_le_p(s + 20 * 4, 0x402100 + i * 0x200);
        stl_le_p(s + 22 * 4, 0xd0000000 | ncoord);
        stl_le_p(s + 24 * 4, 0x402000 + i * 0x200);
        stl_le_p(s + 26 * 4, 0x10000000 | nseg);
        stl_le_p(s + 32 * 4, 0x12345000 + i * 0x1000);
        stl_le_p(s + 40 * 4, 0x10003 + i);
        stl_le_p(d + 41 * 4, 0xff000000);
        stl_le_p(d + 45 * 4, 0x12345000 + i * 0x1000);
        stl_le_p(d + 50 * 4, 0x40102);
        stl_le_p(d + 52 * 4, 0x10003 + i);
    }
    stl_le_p(data + 0x1000 + 110 * 4, 0x00081004);
    stl_le_p(data + 0x1000 + 111 * 4, 1);
    memcpy(data + 0x2000, (uint8_t[]) { 2, 4, 4, 4, 0, 2, 4, 4, 4, 0 }, 10);
    memcpy(data + 0x2200, (uint8_t[]) { 2, 10, 4, 0 }, 4);
    path_floats(data + 0x2100, polygon, ARRAY_SIZE(polygon));
    path_floats(data + 0x2300, quadratic, ARRAY_SIZE(quadratic));
}

static void test_path_fill(void)
{
    enum { SLOT = 0x4b0000, EXTENT = 720 * 480 * 4 };
    enum {
        SUCCESS, COLD, COUNT_LIMIT, UNKNOWN, SHORT_DRAW, COORD_COUNT, SEG_COUNT,
        CUBIC, NAN_COORD, INF_MATRIX, PERSPECTIVE, KEY, MEMORY, OVERLAP,
        LATE_BAD, FILL_RULE, COLOR, ALPHA, ALIAS, SAME_WINDING, EVEN_ODD,
        COLORS, CHANNELS, BAD_RELATIVE, CLIPPED, REPEAT,
        DATA_CHANGE, MAP_LOSS, RESET, CANCEL, CASES,
    };
    TransferFixture f = { .dev = qdev_new(TYPE_NIKON_TRANSFER) };
    MemoryRegion commands, surface, alias;
    MemoryRegion *root = get_system_memory();
    uint8_t *data, *pixels;

    object_property_add_child(object_get_objects_root(), "transfer-path[*]",
                              OBJECT(f.dev));
    memory_region_init_ram_flags_nomigrate(&commands, OBJECT(f.dev),
                                          "test.path-command", 0x20000, 0,
                                          &error_abort);
    memory_region_add_subregion(root, 0x400000, &commands);
    memory_region_init_ram_flags_nomigrate(&surface, OBJECT(f.dev),
                                          "test.path-surface", SLOT + 8, 0,
                                          &error_abort);
    memory_region_add_subregion(root, 0xc175d600 - 4, &surface);
    memory_region_init_alias(&alias, OBJECT(f.dev), "test.path-alias",
                            &surface, 4 + 0x10000, 64);
    memory_region_add_subregion(root, 0x500000, &alias);
    data = memory_region_get_ram_ptr(&commands);
    pixels = memory_region_get_ram_ptr(&surface);
    f.irq = qemu_allocate_irq(irq_level, &f, 0);
    sysbus_connect_irq(SYS_BUS_DEVICE(f.dev), 0, f.irq);
    g_assert_true(sysbus_realize(SYS_BUS_DEVICE(f.dev), &error_abort));
    for (unsigned test = SUCCESS; test < CASES; test++) {
        unsigned setup_bytes = 82 * 4, draw_bytes = 112 * 4;
        bool colored = test == COLORS || test == CHANNELS;
        bool complete = test == SUCCESS || test == SAME_WINDING ||
                        test == EVEN_ODD || colored ||
                        test == CLIPPED || test == REPEAT || test == ALPHA ||
                        test == COLOR;
        bool accepted = complete || test == DATA_CHANGE || test == MAP_LOSS ||
                        test == RESET || test == CANCEL || test == ALPHA;
        g_autofree uint8_t *before = NULL;

        device_cold_reset(f.dev);
        memset(data, 0, 0x20000);
        memset(pixels, 0xa5, SLOT + 8);
        if (test != COLD) {
            build_rgba_program(data, data + 0x1000, 0x402100, 0x402000, true);
            memcpy(data + 0x2000, (uint8_t[]) { 2, 7, 9, 7, 0 }, 5);
            path_floats(data + 0x2100, (float[]) { 0, 0, 720, 480, -720 }, 5);
            g_assert_cmpint(submit_rgba(&f, 176, 368), ==, MEMTX_OK);
            advance();
            g_assert_true(f.level);
            g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
            for (unsigned i = 0; i < EXTENT; i += 4) {
                stl_le_p(pixels + 4 + i, 0xffb4b4b4);
            }
        }
        build_path_program(data);
        if (colored) {
            for (unsigned i = 0; i < 2; i++) {
                uint8_t *a = data + i * 41 * 4;
                uint8_t *b = data + 0x1000 + i * 55 * 4;
                uint32_t paint = test == CHANNELS ?
                    (i ? 0xffa0c0e0 : 0xff204080) :
                    (i ? 0xffb4b4b4 : 0xffe6e6e6);

                stl_le_p(a + 4 * 4, 5);
                stl_le_p(a + 22 * 4, 0xd0000005);
                stl_le_p(a + 26 * 4, 0x10000005);
                stl_le_p(b + 41 * 4, paint);
                stl_le_p(b + 50 * 4, 0x40002);
                memcpy(data + 0x2000 + i * 0x200,
                       (uint8_t[]) { 2, 7, 9, 7, 0 }, 5);
                path_floats(data + 0x2100 + i * 0x200,
                            (float[]) { i ? 40 : 10, 10, 20, 20, -20 }, 5);
            }
            path_floats(data + 41 * 4 + 16 * 4, (float[]) { 0, 0 }, 2);
        }
        switch (test) {
        case COUNT_LIMIT:
            setup_bytes = ROUND_UP(41 * (NIKON_PATH_MAX_BATCH + 1), 2) * 4;
            draw_bytes = ROUND_UP(55 * (NIKON_PATH_MAX_BATCH + 1) + 2, 2) * 4;
            break;
        case UNKNOWN:
            data[0] ^= 1;
            break;
        case SHORT_DRAW:
            draw_bytes -= 8;
            break;
        case COORD_COUNT:
            stl_le_p(data + 22 * 4, 0xd000000f);
            break;
        case SEG_COUNT:
            stl_le_p(data + 26 * 4, 0x10000009);
            break;
        case CUBIC:
            data[0x2001] = 12;
            break;
        case NAN_COORD:
            stl_le_p(data + 0x2100, 0x7fc00000);
            break;
        case INF_MATRIX:
            stl_le_p(data + 10 * 4, 0x7f800000);
            break;
        case PERSPECTIVE:
            path_floats(data + 12 * 4, (float[]) { 1 }, 1);
            break;
        case KEY:
            stl_le_p(data + 0x1000 + 52 * 4, 0x10005);
            break;
        case MEMORY:
            stl_le_p(data + 24 * 4, 0x600000);
            break;
        case OVERLAP:
            stl_le_p(data + 20 * 4, 0xc175d600);
            break;
        case LATE_BAD:
            data[0x2203] = 0xff;
            break;
        case FILL_RULE:
            stl_le_p(data + 0x1000 + 50 * 4, 0x40202);
            break;
        case COLOR:
            stl_le_p(data + 0x1000 + 41 * 4, 0x7f010000);
            break;
        case ALPHA:
            for (unsigned i = 0; i < EXTENT; i += 4) {
                pixels[4 + i + 3] = 128;
            }
            break;
        case ALIAS:
            stl_le_p(data + 20 * 4, 0x500000);
            memcpy(pixels + 4 + 0x10000, data + 0x2100, 64);
            break;
        case SAME_WINDING:
        case EVEN_ODD:
            path_floats(data + 0x2120,
                        (float[]) { 15, 15, 25, 15, 25, 25, 15, 25 }, 8);
            if (test == EVEN_ODD) {
                stl_le_p(data + 0x1000 + 50 * 4, 0x40002);
            }
            break;
        case BAD_RELATIVE:
            data[0x2000] = 7;
            break;
        case CLIPPED:
            path_floats(data + 16 * 4, (float[]) { -30, 3 }, 2);
            break;
        default:
            break;
        }
        before = g_memdup2(pixels, SLOT + 8);
        g_assert_cmpint(submit_rgba(&f, setup_bytes, draw_bytes), ==,
                        accepted ? MEMTX_OK : MEMTX_ERROR);
        g_assert_false(f.level);
        if (test == DATA_CHANGE) {
            data[0x2201] = 0xff;
        } else if (test == MAP_LOSS) {
            memory_region_set_readonly(&surface, true);
        } else if (test == RESET) {
            device_cold_reset(f.dev);
        } else if (test == CANCEL) {
            qdev_unrealize(f.dev);
        }
        advance();
        memory_region_set_readonly(&surface, false);
        g_assert_cmpint(f.level, ==, complete);
        if (complete) {
            bool fringe = false;

            for (unsigned y = 0; y < 480; y++) {
                for (unsigned x = 0; x < 720; x++) {
                    uint32_t actual = ldl_le_p(pixels + 4 + (y * 720 + x) * 4);
                    bool first = test == CLIPPED ? x < 30 && y >= 13 && y < 33 :
                                     x >= 24 && x < 64 && y >= 13 && y < 33;
                    bool second = colored ? x >= 40 && x < 60 && y >= 10 && y < 30 :
                                            x >= 140 && x < 160 && y >= 10 && y < 30;

                    if (!first && !second) {
                        g_assert_cmphex(actual, ==, test == ALPHA ?
                                        0x80b4b4b4 : 0xffb4b4b4);
                    }
                    if (colored) {
                        uint32_t paint = test == CHANNELS ?
                            (second ? 0xffa0c0e0 : 0xff204080) :
                            (second ? 0xffb4b4b4 : 0xffe6e6e6);

                        g_assert_cmphex(actual, ==,
                                        first || second ? paint : 0xffb4b4b4);
                    }
                    fringe |= (actual & 255) > 0 && (actual & 255) < 180;
                }
            }
            if (!colored) {
                g_assert_true(fringe);
            }
            if (!colored && test != CLIPPED) {
                uint32_t body = ldl_le_p(pixels + 4 + (14 * 720 + 26) * 4);
                uint32_t hole = ldl_le_p(pixels + 4 + (20 * 720 + 40) * 4);

                g_assert_cmphex(body, ==, test == COLOR ?
                                0xff5b5a5a : 0xff000000);
                g_assert_cmphex(hole, ==, test == SAME_WINDING ?
                                0xff000000 : test == ALPHA ?
                                0x80b4b4b4 : 0xffb4b4b4);
            }
            if (!colored) {
                uint32_t inside = ldl_le_p(pixels + 4 + (25 * 720 + 145) * 4);
                uint32_t outside = ldl_le_p(pixels + 4 + (12 * 720 + 154) * 4);

                g_assert_cmphex(inside, ==, 0xff000000);
                g_assert_cmphex(outside, ==, test == ALPHA ?
                                0x80b4b4b4 : 0xffb4b4b4);
            }
            g_assert_cmpmem(pixels, 4, before, 4);
            g_assert_cmpmem(pixels + 4 + EXTENT, SLOT - EXTENT + 4,
                            before + 4 + EXTENT, SLOT - EXTENT + 4);
            g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
            if (test == REPEAT) {
                g_assert_cmpint(submit_rgba(&f, setup_bytes, draw_bytes), ==,
                                MEMTX_OK);
                advance();
                g_assert_true(f.level);
            }
        } else {
            g_assert_cmpmem(pixels, SLOT + 8, before, SLOT + 8);
        }
    }
    memory_region_del_subregion(root, &alias);
    memory_region_del_subregion(root, &surface);
    memory_region_del_subregion(root, &commands);
    drain_call_rcu();
    object_unparent(OBJECT(f.dev));
    object_unref(OBJECT(f.dev));
    drain_call_rcu();
    qemu_free_irq(f.irq);
}

static void build_initialized_paths(uint8_t *data, unsigned count,
                                    uint32_t color_prefixes,
                                    unsigned *setup_bytes, unsigned *draw_bytes)
{
    uint8_t initial_setup[NIKON_TRANSFER_RGBA_SETUP_BYTES];
    uint8_t initial_draw[NIKON_TRANSFER_RGBA_DRAW_BYTES];
    uint8_t path_setup[NIKON_TRANSFER_RGBA_GRAY_SETUP_BYTES];
    uint8_t path_draw[NIKON_TRANSFER_RGBA_GRAY_DRAW_BYTES];
    unsigned sa = 2, da = 20;

    build_rgba_program(initial_setup, initial_draw, 0x451000, 0x450000, true);
    build_rgba_gray_program(path_setup, path_draw);
    memcpy(data, initial_setup, 2 * 4);
    memcpy(data + 0x20000, initial_draw, 20 * 4);
    for (unsigned i = 0; i < count; i++) {
        uint8_t *a = data + sa * 4, *b = data + 0x20000 + da * 4;
        uint32_t key = 0x10001 + i;

        memcpy(a, path_setup, 41 * 4);
        stl_le_p(a + 20 * 4, 0x451000 + i * 0x20);
        stl_le_p(a + 24 * 4, 0x450000 + i * 8);
        stl_le_p(a + 40 * 4, key);
        memcpy(b, path_draw, 24 * 4);
        stl_le_p(b + 13 * 4, 0xbf309600);
        da += 24;
        if (!i) {
            /*
             * Initialization's clip/flags seam precedes the first path latch.
             */
            memcpy(data + 0x20000 + da * 4, initial_draw + 44 * 4, 15 * 4);
            da += 15;
        } else if (color_prefixes & (1U << (i % 32))) {
            stl_le_p(data + 0x20000 + da++ * 4, 0x01848004);
            stl_le_p(data + 0x20000 + da++ * 4, 0);
        }
        b = data + 0x20000 + da * 4;
        memcpy(b, path_draw + 24 * 4, 31 * 4);
        stl_le_p(b + 17 * 4, 0xff464646);
        stl_le_p(b + 28 * 4, key);
        memcpy(data + 0x50000 + i * 8, (uint8_t[]) { 2, 7, 9, 7, 0 }, 5);
        path_floats(data + 0x51000 + i * 0x20,
                    (float[]) { 10 + 20 * (i % 4), 10, 30, 20, -30 }, 5);
        sa += 41;
        da += 31;
    }
    stl_le_p(data + 0x20000 + da++ * 4, 0x00081004);
    stl_le_p(data + 0x20000 + da++ * 4, 1);
    *setup_bytes = ROUND_UP(sa, 2) * 4;
    *draw_bytes = ROUND_UP(da, 2) * 4;
}

static MemTxResult submit_initialized_paths(TransferFixture *f,
                                            unsigned setup_bytes,
                                            unsigned draw_bytes)
{
    const uint32_t packet[][2] = {
        { 0x1214, 0x100ffff }, { 0x1298, 0 },
        { 0x10d4, 0x400000 }, { 0x10dc, setup_bytes / 8 }, { 0x10d0, 1 },
        { 0x10e4, 0x420000 }, { 0x10ec, draw_bytes / 8 },
    };

    for (unsigned i = 0; i < ARRAY_SIZE(packet); i++) {
        g_assert_cmpint(write_register(f, packet[i][0], packet[i][1]), ==,
                        MEMTX_OK);
    }
    return write_register(f, 0x10e0, 1);
}

static void test_initialized_paths(void)
{
    enum { EXTENT = 720 * 480 * 4, GUARDS = EXTENT + 8 };
    enum {
        SUCCESS, ONE, LIMIT, PREFIX_ONE, PREFIX_ALL, PREFIX_MIXED, COLLISION,
        STROKE, REPEAT, IMMUTABLE, RETAINED, BAD_GLOBAL, BAD_SETUP, BAD_CLIP,
        BAD_DISABLE,
        BAD_FLAGS, LATE_BAD, SHORT_SETUP, SHORT_DRAW, BAD_PADDING,
        BAD_ENDING, FOREIGN_TARGET, BAD_COORDINATES, MAP_LOSS, RESET, CANCEL,
        CASES,
    };
    TransferFixture f = { .dev = qdev_new(TYPE_NIKON_TRANSFER) };
    MemoryRegion commands, surface;
    MemoryRegion *root = get_system_memory();
    uint8_t *data, *pixels;

    object_property_add_child(object_get_objects_root(),
                              "transfer-init-path[*]", OBJECT(f.dev));
    memory_region_init_ram_flags_nomigrate(&commands, OBJECT(f.dev),
        "test.init-path-command", 0x60000, 0, &error_abort);
    memory_region_add_subregion(root, 0x400000, &commands);
    memory_region_init_ram_flags_nomigrate(&surface, OBJECT(f.dev),
        "test.init-path-surface", GUARDS, 0, &error_abort);
    memory_region_add_subregion(root, 0xbf309600 - 4, &surface);
    data = memory_region_get_ram_ptr(&commands);
    pixels = memory_region_get_ram_ptr(&surface);
    f.irq = qemu_allocate_irq(irq_level, &f, 0);
    sysbus_connect_irq(SYS_BUS_DEVICE(f.dev), 0, f.irq);
    g_assert_true(sysbus_realize(SYS_BUS_DEVICE(f.dev), &error_abort));
    for (unsigned test = SUCCESS; test < CASES; test++) {
        unsigned count = test == ONE ? 1 : test == LIMIT ? 256 :
                         test == COLLISION ? 7 : test == STROKE ? 2 : 4;
        unsigned setup_bytes, draw_bytes;
        uint32_t prefixes = test == PREFIX_ONE ? 2 :
                            test == PREFIX_ALL || test == COLLISION ?
                            UINT32_MAX :
                            test == PREFIX_MIXED ? 10 : 0;
        bool complete = test <= RETAINED;
        bool accepted = complete || test >= MAP_LOSS;
        g_autofree uint8_t *expected = g_malloc(GUARDS);

        g_test_message("initialized path case %u", test);
        device_cold_reset(f.dev);
        memset(data, 0, 0x60000);
        memset(pixels, 0xa5, GUARDS);
        memcpy(expected, pixels, GUARDS);
        build_initialized_paths(data, count, prefixes,
                                &setup_bytes, &draw_bytes);
        if (test == STROKE) {
            uint8_t fill[41 * 4], *stroke = data + 43 * 4;

            memcpy(fill, stroke, sizeof(fill));
            memset(stroke, 0, 29 * 4);
            memcpy(stroke, fill + 19 * 4, 8 * 4);
            stl_le_p(stroke + 8 * 4, 0x005af01e);
            stl_le_p(stroke + 13 * 4, 0x00654004);
            stl_le_p(stroke + 15 * 4, 0x00653004);
            stl_le_p(stroke + 16 * 4, 0x2000);
            memcpy(stroke + 17 * 4, fill + 29 * 4, 12 * 4);
            memmove(data + 0x20000 + 90 * 4,
                    data + 0x20000 + 114 * 4, 31 * 4);
            stl_le_p(data + 0x20000 + 121 * 4, 0x00081004);
            stl_le_p(data + 0x20000 + 122 * 4, 1);
            stl_le_p(data + 0x20000 + 123 * 4, 0);
            setup_bytes = 72 * 4;
            draw_bytes = 124 * 4;
            count = 1; /* The inherited zero-width stroke changes no pixels. */
        }
        if (test == SUCCESS) {
            g_assert_cmpuint(setup_bytes, ==, 664);
            g_assert_cmpuint(draw_bytes, ==, 1032);
        } else if (test == COLLISION) {
            g_assert_cmpuint(draw_bytes, ==, 1736);
        }
        switch (test) {
        case BAD_GLOBAL:
            data[0x20000] ^= 1;
            break;
        case BAD_SETUP:
            data[4] ^= 1;
            break;
        case BAD_CLIP:
            data[0x20000 + 45 * 4] ^= 1;
            break;
        case BAD_DISABLE:
            stl_le_p(data + 0x20000 + 56 * 4, 0x01000000);
            break;
        case BAD_FLAGS:
            data[0x20000 + 58 * 4] ^= 1;
            break;
        case LATE_BAD:
            data[0x20000 + (90 + 2 * 55 + 43) * 4] ^= 1;
            break;
        case SHORT_SETUP:
            setup_bytes -= 8;
            break;
        case SHORT_DRAW:
            draw_bytes -= 8;
            break;
        case BAD_PADDING:
            data[0x20000 + draw_bytes - 4] = 1;
            break;
        case BAD_ENDING:
            data[0x20000 + (draw_bytes / 4 - 3) * 4] ^= 1;
            break;
        case FOREIGN_TARGET:
            stl_le_p(data + 0x20000 + 103 * 4, 0xbf309604);
            break;
        case BAD_COORDINATES:
            data[0x50000 + 3 * 8] = 0xff;
            break;
        default:
            break;
        }
        if (complete) {
            for (unsigned i = 0; i < count; i++) {
                for (unsigned y = 10; y < 30; y++) {
                    for (unsigned x = 10 + 20 * (i % 4);
                         x < 40 + 20 * (i % 4); x++) {
                        stl_le_p(expected + 4 + (y * 720 + x) * 4, 0xff464646);
                    }
                }
            }
        }
        g_assert_cmpint(submit_initialized_paths(&f, setup_bytes, draw_bytes),
                        ==, accepted ? MEMTX_OK : MEMTX_ERROR);
        g_assert_false(f.level);
        if (test == IMMUTABLE) {
            memset(data, 0xff, setup_bytes);
            memset(data + 0x20000, 0xff, draw_bytes);
        } else if (test == MAP_LOSS) {
            memory_region_set_readonly(&surface, true);
        } else if (test == RESET) {
            device_cold_reset(f.dev);
        } else if (test == CANCEL) {
            qdev_unrealize(f.dev);
        }
        advance();
        memory_region_set_readonly(&surface, false);
        g_assert_cmpint(f.level, ==, complete);
        g_assert_cmpmem(pixels, GUARDS, expected, GUARDS);
        if (complete) {
            g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
        }
        if (test == REPEAT || test == RETAINED) {
            if (test == RETAINED) {
                memmove(data, data + 8, setup_bytes - 8);
                memmove(data + 0x20000, data + 0x20000 + 20 * 4, 24 * 4);
                memmove(data + 0x20000 + 24 * 4,
                        data + 0x20000 + 59 * 4, draw_bytes - 59 * 4);
                setup_bytes -= 8;
                draw_bytes = ROUND_UP(55 * count + 2, 2) * 4;
            }
            g_assert_cmpint(submit_initialized_paths(&f, setup_bytes,
                                                    draw_bytes),
                            ==, MEMTX_OK);
            advance();
            g_assert_true(f.level);
            g_assert_cmpmem(pixels, GUARDS, expected, GUARDS);
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
    enum { VALID_105, VALID_107, TEMP_105, TEMP_107, IMMUTABLE_105, IMMUTABLE_107,
           TEMP_ODD, TEMP_IMAGE, TEMP_SOLID_IMAGE, TEMP_HIGH, TEMP_UPPER,
           TEMP_HEADER, EARLY_DRAW, FINAL_HEADER, HEADER, TRUNCATED, FORMAT_FIRST,
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
        bool complete = test <= IMMUTABLE_107;
        bool profile107 = test == VALID_107 || test == TEMP_107 ||
                          test == IMMUTABLE_107 || test == OVERFLOW_107 ||
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
        if ((test >= TEMP_105 && test <= IMMUTABLE_107) || test >= RESET) {
            stl_le_p(data + 0x1000 + 66 * 4, 0);
        }
        switch (test) {
        case TEMP_ODD:
            stl_le_p(data + 0x1000 + 66 * 4, 1);
            break;
        case TEMP_IMAGE:
            stl_le_p(data + 0x1000 + 66 * 4, 2);
            break;
        case TEMP_SOLID_IMAGE:
            stl_le_p(data + 0x1000 + 66 * 4, 14);
            break;
        case TEMP_HIGH:
            stl_le_p(data + 0x1000 + 66 * 4, 0x100);
            break;
        case TEMP_UPPER:
            stl_le_p(data + 0x1000 + 66 * 4, 0x01000000);
            break;
        case TEMP_HEADER:
            stl_le_p(data + 0x1000 + 65 * 4, 0x01842004);
            break;
        case EARLY_DRAW:
            stl_le_p(data + 0x1000 + 65 * 4, 0x01a61004);
            stl_le_p(data + 0x1000 + 66 * 4, 1);
            break;
        case FINAL_HEADER:
            stl_le_p(data + 0x1000 + 388 * 4, 0x01842004);
            break;
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
        if (test == IMMUTABLE_105 || test == IMMUTABLE_107) {
            stl_le_p(data + 0x1000 + 66 * 4, 0xffffffff);
            stl_le_p(draw + 66 * 4, 0xffffffff);
        } else if (test == RESET) {
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

static void build_image_program(uint8_t *setup, uint8_t *draw)
{
    unsigned at = 0;
    uint8_t *bytes = setup;
    const float transform[] = { 1, 0, 0, 0, 1, 0, 10, 20, 1 };
    const uint32_t sample[] = {
        0x3c0000, 0, 0xbf4000, 0, 0x3d0000, 0xc14000, 0, 0, 0x3f0000,
    };

#define WORD(v) do { stl_le_p(bytes + 4 * at++, (v)); } while (0)
#define REG(r, mask, v) do { \
    WORD(((r) << 16) | ((mask) << 12) | 4); \
    WORD(v); \
} while (0)
    WORD(0x0041f046);
    path_floats(setup + 4, transform, 9);
    at += 9;
    REG(0x65, 3, 0x2100);
    REG(3, 15, 5);
    REG(5, 15, 0x80000005);
    REG(0x63, 15, 5);
    REG(0x5e, 15, 0x3f800000);
    REG(0x5f, 15, 0);
    REG(0x40, 15, 0x07800b40);
    WORD(0x0080f026);
    WORD(0x12345000);
    WORD(0);
    WORD(0x10);
    WORD(0x07800b40);
    WORD(0);
    REG(6, 15, 1);
    WORD(0x0a);
    WORD(0x07090702);
    WORD(0);
    WORD(0x00010022);
    WORD(0);
    WORD(0);
    WORD(4);
    WORD(3);
    WORD(-4);
    REG(0x85, 15, 0x10010);
    WORD(0);
    g_assert_cmpuint(at, ==, 44);
    at = 0;
    bytes = draw;
    WORD(0x010c7046);
    for (unsigned i = 0; i < 9; i++) {
        WORD(sample[i]);
    }
    REG(0x158, 15, 0x00080004);
    REG(0x156, 7, 0x001b0018);
    REG(0x152, 15, 0x00080004);
    REG(0x140, 1, 2);
    REG(0x14d, 15, 0);
    REG(0x14d, 15, 0);
    REG(0x140, 4, 0x210000);
    REG(0x155, 15, 0x403000);
    REG(0x1a0, 4, 0x10000);
    REG(0x184, 1, 8);
    REG(0x140, 1, 2);
    REG(0x14d, 15, 0);
    REG(0x14d, 15, 0);
    REG(0x140, 2, 0);
    REG(0x14d, 15, 0);
    REG(0x14d, 15, 0);
    REG(0x115, 12, 0x01000000);
    REG(0x185, 15, 0xff4848f0);
    REG(0x1aa, 1, 0);
    REG(0x184, 1, 14);
    REG(0x186, 3, 0x108);
    REG(0x184, 2, 0);
    REG(0x1a0, 2, 0);
    REG(0x1a2, 9, 0);
    REG(0x1a4, 15, 0xc175d600);
    REG(0x1a5, 1, 1);
    REG(0x1a7, 4, 0);
    REG(0x115, 8, 0x01000000);
    REG(0x153, 1, 1);
    REG(0x143, 1, 1);
    WORD(0x00a0f02e);
    WORD(0x12345000);
    WORD(0);
    WORD(0x10);
    WORD(0x07800b40);
    WORD(0);
    WORD(2);
    REG(0xa6, 15, 0x10010);
    REG(0x1a6, 1, 1);
    REG(8, 1, 1);
    WORD(0);
    g_assert_cmpuint(at, ==, 84);
#undef REG
#undef WORD
}

static void test_image_fill(void)
{
    enum { SLOT = 0x4b0000, EXTENT = 720 * 480 * 4 };
    enum {
        SUCCESS, COLD, SHORT, UNKNOWN, TYPE, SEGMENT, COORDINATE, TRANSFORM,
        SAMPLE, PERSPECTIVE, ZERO_SIZE, SIZE_MISMATCH, TOO_BIG, PAINT, MODE,
        KEY, OPAQUE, MEMORY, OVERLAP, COMMAND_OVERLAP, ALIAS, ALPHA,
        DATA_CHANGE, MAP_LOSS, RESET, REPEAT, CLAMP_SAMPLE, FLIP, CANCEL, CASES,
    };
    TransferFixture f = { .dev = qdev_new(TYPE_NIKON_TRANSFER) };
    MemoryRegion commands, surface, alias;
    MemoryRegion *root = get_system_memory();
    uint8_t *data, *pixels;

    object_property_add_child(object_get_objects_root(), "transfer-image[*]",
                              OBJECT(f.dev));
    memory_region_init_ram_flags_nomigrate(&commands, OBJECT(f.dev),
                                          "test.image-command", 0x20000, 0,
                                          &error_abort);
    memory_region_add_subregion(root, 0x400000, &commands);
    memory_region_init_ram_flags_nomigrate(&surface, OBJECT(f.dev),
                                          "test.image-surface", SLOT + 8, 0,
                                          &error_abort);
    memory_region_add_subregion(root, 0xc175d600 - 4, &surface);
    memory_region_init_alias(&alias, OBJECT(f.dev), "test.image-alias",
                            &surface, 4 + 0x10000, 32);
    memory_region_add_subregion(root, 0x500000, &alias);
    data = memory_region_get_ram_ptr(&commands);
    pixels = memory_region_get_ram_ptr(&surface);
    f.irq = qemu_allocate_irq(irq_level, &f, 0);
    sysbus_connect_irq(SYS_BUS_DEVICE(f.dev), 0, f.irq);
    g_assert_true(sysbus_realize(SYS_BUS_DEVICE(f.dev), &error_abort));
    for (unsigned test = SUCCESS; test < CASES; test++) {
        unsigned setup_bytes = 176;
        bool complete = test == SUCCESS || test == DATA_CHANGE ||
                        test == REPEAT || test == CLAMP_SAMPLE || test == FLIP ||
                        test == ALPHA;
        bool accepted = complete || test == ALPHA || test == MAP_LOSS ||
                        test == RESET || test == CANCEL;
        g_autofree uint8_t *before = NULL;

        g_test_message("image case %u", test);
        device_cold_reset(f.dev);
        memset(data, 0, 0x20000);
        memset(pixels, 0xa5, SLOT + 8);
        if (test != COLD) {
            build_rgba_program(data, data + 0x1000, 0x402100, 0x402000, true);
            memcpy(data + 0x2000, (uint8_t[]) { 2, 7, 9, 7, 0 }, 5);
            path_floats(data + 0x2100, (float[]) { 0, 0, 720, 480, -720 }, 5);
            g_assert_cmpint(submit_rgba(&f, 176, 368), ==, MEMTX_OK);
            advance();
            g_assert_true(f.level);
            g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
            for (unsigned i = 0; i < EXTENT; i += 4) {
                stl_le_p(pixels + 4 + i, 0xff808080);
            }
        }
        build_image_program(data, data + 0x1000);
        for (unsigned i = 0; i < 32; i++) {
            data[0x3000 + i] = (uint8_t[]) { 0, 64, 128, 255 }[(i + i / 8) % 4];
        }
        switch (test) {
        case SHORT: setup_bytes -= 8; break;
        case UNKNOWN: data[0] ^= 1; break;
        case TYPE: stl_le_p(data + 15 * 4, 0xc0000005); break;
        case SEGMENT: data[33 * 4 + 1] = 5; break;
        case COORDINATE: stl_le_p(data + 38 * 4, 0x7fffffff); break;
        case TRANSFORM: stl_le_p(data + 4, 0x7fc00000); break;
        case SAMPLE: stl_le_p(data + 0x1000 + 4, 0x1000000); break;
        case PERSPECTIVE: stl_le_p(data + 0x1000 + 7 * 4, 0x3f0000); break;
        case ZERO_SIZE: stl_le_p(data + 0x1000 + 11 * 4, 0); break;
        case SIZE_MISMATCH: stl_le_p(data + 0x1000 + 15 * 4, 0x00090004); break;
        case TOO_BIG:
            stl_le_p(data + 0x1000 + 11 * 4, 0x10010004);
            stl_le_p(data + 0x1000 + 15 * 4, 0x10010004);
            break;
        case PAINT: stl_le_p(data + 0x1000 + 45 * 4, 0x7f4848f0); break;
        case MODE: stl_le_p(data + 0x1000 + 49 * 4, 2); break;
        case KEY: stl_le_p(data + 0x1000 + 78 * 4, 0x10011); break;
        case OPAQUE: stl_le_p(data + 0x1000 + 71 * 4, 0); break;
        case MEMORY: stl_le_p(data + 0x1000 + 25 * 4, 0x600000); break;
        case OVERLAP: stl_le_p(data + 0x1000 + 25 * 4, 0xc175d600); break;
        case COMMAND_OVERLAP:
            stl_le_p(data + 0x1000 + 25 * 4, 0x400000);
            break;
        case ALIAS: stl_le_p(data + 0x1000 + 25 * 4, 0x500000); break;
        case ALPHA:
            for (unsigned i = 0; i < EXTENT; i += 4) {
                pixels[4 + i + 3] = 128;
            }
            break;
        case CLAMP_SAMPLE:
            stl_le_p(data + 0x1000 + 3 * 4, 0x3f0000);
            stl_le_p(data + 0x1000 + 6 * 4, 0x3f0000);
            break;
        case FLIP:
            path_floats(data + 5 * 4, (float[]) { -1 }, 1);
            path_floats(data + 8 * 4, (float[]) { 23 }, 1);
            stl_le_p(data + 0x1000 + 5 * 4, 0xbd0000);
            stl_le_p(data + 0x1000 + 6 * 4, 0x417000);
            break;
        default: break;
        }
        before = g_memdup2(pixels, SLOT + 8);
        g_assert_cmpint(submit_rgba(&f, setup_bytes, 336), ==,
                        accepted ? MEMTX_OK : MEMTX_ERROR);
        g_assert_false(f.level);
        if (test == DATA_CHANGE) {
            data[0x3000] = 255;
        } else if (test == MAP_LOSS) {
            memory_region_set_readonly(&surface, true);
        } else if (test == RESET) {
            device_cold_reset(f.dev);
        } else if (test == CANCEL) {
            qdev_unrealize(f.dev);
        }
        advance();
        memory_region_set_readonly(&surface, false);
        g_assert_cmpint(f.level, ==, complete);
        if (complete) {
            for (unsigned y = 0; y < 480; y++) {
                for (unsigned x = 0; x < 720; x++) {
                    uint32_t expected = test == ALPHA ? 0x80808080 : 0xff808080;
                    uint32_t actual = ldl_le_p(pixels + 4 + (y * 720 + x) * 4);

                    if (x >= 10 && x < 14 && y >= 20 && y < 23) {
                        unsigned row = test == FLIP ? 22 - y : y - 20;
                        unsigned a = data[0x3000 + (test == CLAMP_SAMPLE ? 31 :
                                                    row * 8 + x - 10)];
                        expected = 0xff000000;
                        for (unsigned c = 0; c < 3; c++) {
                            unsigned color = (0xff4848f0 >> (8 * c)) & 255;
                            unsigned associated = (color * a + 127) / 255;

                            if (test == ALPHA) {
                                unsigned weight = a * 255 + 128 * (255 - a);
                                unsigned result = (associated * 65025ULL +
                                    128 * 128 * (255 - a) + weight / 2) / weight;

                                expected |= result << (8 * c);
                            } else {
                                expected |= (associated +
                                    (128 * (255 - a) + 127) / 255) << (8 * c);
                            }
                        }
                        if (test == ALPHA) {
                            expected = (expected & 0xffffff) |
                                ((a * 255 + 128 * (255 - a) + 127) / 255) << 24;
                        }
                    }
                    if (actual != expected) {
                        g_test_message("image pixel %u/%u: %08x != %08x",
                                        x, y, actual, expected);
                    }
                    g_assert_cmphex(actual, ==, expected);
                }
            }
            g_assert_cmpmem(pixels, 4, before, 4);
            g_assert_cmpmem(pixels + 4 + EXTENT, SLOT - EXTENT + 4,
                            before + 4 + EXTENT, SLOT - EXTENT + 4);
            g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
            if (test == REPEAT) {
                g_assert_cmpint(submit_rgba(&f, 176, 336), ==, MEMTX_OK);
                advance();
                g_assert_true(f.level);
            }
        } else {
            g_assert_cmpmem(pixels, SLOT + 8, before, SLOT + 8);
        }
    }
    memory_region_del_subregion(root, &alias);
    memory_region_del_subregion(root, &surface);
    memory_region_del_subregion(root, &commands);
    drain_call_rcu();
    object_unparent(OBJECT(f.dev));
    object_unref(OBJECT(f.dev));
    drain_call_rcu();
    qemu_free_irq(f.irq);
}

static void build_scaled_program(uint8_t *setup, uint8_t *draw, bool over)
{
    build_convert_program(setup, draw);
    stl_le_p(setup + 13 * 4, 0x3fb60b00);
    stl_le_p(setup + 17 * 4, 0xbfb5dd80);
    stl_le_p(setup + 20 * 4, 0x442bffff);
    stl_le_p(setup + 50 * 4, 720);
    stl_le_p(setup + 51 * 4, 480);
    stl_le_p(setup + 52 * 4, -720);
    stl_le_p(draw + 9 * 4, 0x107);
    stl_le_p(draw + 46 * 4, 0x34fff8);
    stl_le_p(draw + 54 * 4, 0x3efffe);
    stl_le_p(draw + 56 * 4, 0x02d001e0);
    stl_le_p(draw + 60 * 4, 0x02d001e0);
    stl_le_p(draw + 405 * 4, 0x107);
    stl_le_p(draw + 409 * 4, 0x955000);
    stl_le_p(draw + 417 * 4, 7);
    stl_le_p(draw + 419 * 4, 7);
    if (over) {
        memmove(setup, setup + 12 * 4, 176);
        memmove(draw, draw + 45 * 4, 1552);
        stl_le_p(setup + 42 * 4, 0x10002);
        stl_le_p(draw + 27 * 4, 0x10000);
        stl_le_p(draw + 29 * 4, 2);
        stl_le_p(draw + 309 * 4, 0x0192f01e);
        stl_le_p(draw + 314 * 4, 0x01963004);
        stl_le_p(draw + 315 * 4, 0x400);
        stl_le_p(draw + 366 * 4, 1);
        stl_le_p(draw + 383 * 4, 0x10002);
    }
}

static void test_rgba_yuv(void)
{
    enum { W = 1024, H = 682, PLANE = W * H, INPUT = W * 688 * 4,
           SLOT = 0x4b0000 };
    enum { SUCCESS, TEMP2, TEMP4, TEMP6, TEMP_ODD, TEMP_HEADER, EARLY_DRAW,
           SHORT, HEADER, GEOMETRY, SAMPLE, LUT, SECONDARY, FORMAT,
           MODE, KEY, OPERAND, DELTA, OVERFLOW, SOURCE_RANGE, SOURCE_OVERLAP,
           COMMAND_OVERLAP, SOURCE_ALIAS, PLANE_ALIAS, PARTIAL_ALPHA,
           DATA_CHANGE, MAP_LOSS, RESET, CANCEL, CASES };
    static const uint32_t rgba[] = {
        0xff000000, 0xffffffff, 0xff0000ff, 0xff00ff00,
        0xffff0000, 0xff00ffff, 0xffffff00,
    };
    static const uint8_t yuv[][3] = {
        {16,128,128}, {235,128,128}, {82,90,240}, {144,54,34},
        {41,240,110}, {210,16,146}, {169,166,16},
    };
    TransferFixture f = { .dev = qdev_new(TYPE_NIKON_TRANSFER) };
    MemoryRegion commands, input, output, source_alias, plane_alias;
    MemoryRegion *root = get_system_memory();
    uint8_t *data, *source, *target;

    object_property_add_child(object_get_objects_root(), "transfer-convert[*]",
                              OBJECT(f.dev));
    memory_region_init_ram_flags_nomigrate(&commands, OBJECT(f.dev),
        "test.convert-command", 0x4000, 0, &error_abort);
    memory_region_add_subregion(root, 0x400000, &commands);
    memory_region_init_ram_flags_nomigrate(&input, OBJECT(f.dev),
        "test.convert-input", INPUT, 0, &error_abort);
    memory_region_add_subregion(root, 0x410000, &input);
    memory_region_init_ram_flags_nomigrate(&output, OBJECT(f.dev),
        "test.convert-output", SLOT + 8, 0, &error_abort);
    memory_region_add_subregion(root, 0x800000 - 4, &output);
    memory_region_init_alias(&source_alias, OBJECT(f.dev), "test.convert-alias",
                            &output, 4, INPUT);
    memory_region_add_subregion(root, 0x1000000, &source_alias);
    memory_region_init_alias(&plane_alias, OBJECT(f.dev), "test.convert-planes",
                            &output, 4, PLANE);
    data = memory_region_get_ram_ptr(&commands);
    source = memory_region_get_ram_ptr(&input);
    target = memory_region_get_ram_ptr(&output);
    f.irq = qemu_allocate_irq(irq_level, &f, 0);
    sysbus_connect_irq(SYS_BUS_DEVICE(f.dev), 0, f.irq);
    g_assert_true(sysbus_realize(SYS_BUS_DEVICE(f.dev), &error_abort));
    for (unsigned test = SUCCESS; test < CASES; test++) {
        bool complete = test <= TEMP6 || test == DATA_CHANGE;
        bool accepted = complete || test == PARTIAL_ALPHA || test == MAP_LOSS ||
                        test == RESET || test == CANCEL;
        unsigned draw_bytes = 1736;
        g_autofree uint8_t *before = NULL;

        g_test_message("convert case %u", test);
        device_cold_reset(f.dev);
        memset(data, 0, 0x4000);
        memset(target, 0xa5, SLOT + 8);
        for (unsigned y = 0; y < 688; y++) {
            for (unsigned x = 0; x < W; x++) {
                stl_le_p(source + 4 * (y * W + x), y ? rgba[(x + y) % 7] :
                                                               0x00123456);
            }
        }
        build_convert_program(data, data + 0x1000);
        switch (test) {
        case TEMP2: case TEMP4: case TEMP6:
            stl_le_p(data + 0x1000 + 74 * 4, 2 * (test - TEMP2 + 1)); break;
        case TEMP_ODD: stl_le_p(data + 0x1000 + 74 * 4, 1); break;
        case TEMP_HEADER: stl_le_p(data + 0x1000 + 73 * 4, 0x0184f004); break;
        case EARLY_DRAW: stl_le_p(data + 0x1000 + 73 * 4, 0x01a61004); break;
        case SHORT: draw_bytes -= 8; break;
        case HEADER: data[0] ^= 1; break;
        case GEOMETRY: stl_le_p(data + 51 * 4, 681); break;
        case SAMPLE: stl_le_p(data + 0x1000 + 50 * 4, 0x358064); break;
        case LUT: stl_le_p(data + 0x1000 + 100 * 4, 0xfff); break;
        case SECONDARY: stl_le_p(data + 0x1000 + 367 * 4, 0); break;
        case FORMAT: stl_le_p(data + 0x1000 + 405 * 4, 0x107); break;
        case MODE: stl_le_p(data + 0x1000 + 92 * 4, 14); break;
        case KEY: stl_le_p(data + 0x1000 + 428 * 4, 0x10002); break;
        case OPERAND: stl_le_p(data + 0x1000 + 421 * 4, 0); break;
        case DELTA: stl_le_p(data + 0x1000 + 409 * 4, 0x8aa804); break;
        case OVERFLOW:
            stl_le_p(data + 0x1000 + 407 * 4, 0xfff00000);
            stl_le_p(data + 0x1000 + 409 * 4, 0xfffaa800);
            break;
        case SOURCE_RANGE: stl_le_p(data + 0x1000 + 70 * 4, 0x700000); break;
        case SOURCE_OVERLAP: stl_le_p(data + 0x1000 + 70 * 4, 0x800000); break;
        case COMMAND_OVERLAP: stl_le_p(data + 0x1000 + 70 * 4, 0x400000); break;
        case SOURCE_ALIAS: stl_le_p(data + 0x1000 + 70 * 4, 0x1000000); break;
        case PLANE_ALIAS:
            memory_region_add_subregion_overlap(root, 0x8aa800, &plane_alias, 1);
            break;
        case PARTIAL_ALPHA: source[INPUT - 1] = 128; break;
        default: break;
        }
        before = g_memdup2(target, SLOT + 8);
        g_assert_cmpint(submit_rgba(&f, 224, draw_bytes), ==,
                        accepted ? MEMTX_OK : MEMTX_ERROR);
        g_assert_false(f.level);
        if (test == DATA_CHANGE) {
            stl_le_p(source + 4 * (687 * W), rgba[2]);
        } else if (test == MAP_LOSS) {
            memory_region_set_readonly(&output, true);
        } else if (test == RESET) {
            device_cold_reset(f.dev);
        } else if (test == CANCEL) {
            qdev_unrealize(f.dev);
        }
        advance();
        memory_region_set_readonly(&output, false);
        if (test == PLANE_ALIAS) {
            memory_region_del_subregion(root, &plane_alias);
        }
        g_assert_cmpint(f.level, ==, complete);
        if (complete) {
            for (unsigned y = 0; y < H; y++) {
                unsigned sy = (-98404.0 / 67108864 * (y + 6.5) +
                               66113.0 / 65536) * 688;
                for (unsigned x = 0; x < W; x += 2) {
                    unsigned a = sy ? (x + sy) % 7 : 0;
                    unsigned b = sy ? (x + 1 + sy) % 7 : 0;
                    if (test == DATA_CHANGE && y == 0 && x == 0) { a = 2; }
                    g_assert_cmpuint(target[4 + y * W + x], ==, yuv[a][0]);
                    g_assert_cmpuint(target[5 + y * W + x], ==, yuv[b][0]);
                    g_assert_cmpuint(target[4 + PLANE + y * W + x], ==,
                                     (yuv[a][1] + yuv[b][1] + 1) / 2);
                    g_assert_cmpuint(target[5 + PLANE + y * W + x], ==,
                                     (yuv[a][2] + yuv[b][2] + 1) / 2);
                }
            }
            g_assert_cmpmem(target, 4, before, 4);
            g_assert_cmpmem(target + 4 + 2 * PLANE, SLOT - 2 * PLANE + 4,
                            before + 4 + 2 * PLANE, SLOT - 2 * PLANE + 4);
            g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
        } else {
            g_assert_cmpmem(target, SLOT + 8, before, SLOT + 8);
        }
    }
    memory_region_del_subregion(root, &source_alias);
    memory_region_del_subregion(root, &output);
    memory_region_del_subregion(root, &input);
    memory_region_del_subregion(root, &commands);
    drain_call_rcu();
    object_unparent(OBJECT(f.dev));
    object_unref(OBJECT(f.dev));
    drain_call_rcu();
    qemu_free_irq(f.irq);
}

static void test_scaled_yuva(void)
{
    enum { W = 1024, H = 682, PLANE = W * H, INPUT = 720 * 480 * 4,
           SLOT = 0x4b0000 };
    enum { SUCCESS, TEMP2, TEMP4, TEMP6, IMMUTABLE,
           TEMP_ODD, TEMP_SOLID, TEMP_HIGH, TEMP_HEADER, EARLY_DRAW, FINAL_MODE,
           SHORT, TRANSFORM, SAMPLE, SOURCE_SIZE, FORMAT, FILTER,
           LUT, GEOMETRY, DELTA, OVERFLOW, RANGE, SOURCE_ALIAS, PLANE_ALIAS,
           DATA_CHANGE, EDGE_KEEP, EDGE_COLOR, MAP_LOSS, RESET, CANCEL, CASES };
    static const uint32_t rgba[] = {
        0xff000000, 0xffffffff, 0xff0000ff, 0xff00ff00,
        0xffff0000, 0xff00ffff, 0xffffff00,
    };
    static const uint8_t yuv[][3] = {
        {16,128,128}, {235,128,128}, {82,90,240}, {144,54,34},
        {41,240,110}, {210,16,146}, {169,166,16},
    };
    TransferFixture f = { .dev = qdev_new(TYPE_NIKON_TRANSFER) };
    MemoryRegion commands, input, output, source_alias, plane_alias;
    MemoryRegion *root = get_system_memory();
    uint8_t *data, *source, *target;

    object_property_add_child(object_get_objects_root(), "transfer-scaled[*]",
                              OBJECT(f.dev));
    memory_region_init_ram_flags_nomigrate(&commands, OBJECT(f.dev),
        "test.scaled-command", 0x4000, 0, &error_abort);
    memory_region_add_subregion(root, 0x400000, &commands);
    memory_region_init_ram_flags_nomigrate(&input, OBJECT(f.dev),
        "test.scaled-input", INPUT, 0, &error_abort);
    memory_region_add_subregion(root, 0x410000, &input);
    memory_region_init_ram_flags_nomigrate(&output, OBJECT(f.dev),
        "test.scaled-output", SLOT + 8, 0, &error_abort);
    memory_region_add_subregion(root, 0x800000 - 4, &output);
    memory_region_init_alias(&source_alias, OBJECT(f.dev), "test.scaled-alias",
                            &output, 4, INPUT);
    memory_region_add_subregion(root, 0x1000000, &source_alias);
    memory_region_init_alias(&plane_alias, OBJECT(f.dev), "test.scaled-planes",
                            &output, 4, PLANE);
    data = memory_region_get_ram_ptr(&commands);
    source = memory_region_get_ram_ptr(&input);
    target = memory_region_get_ram_ptr(&output);
    f.irq = qemu_allocate_irq(irq_level, &f, 0);
    sysbus_connect_irq(SYS_BUS_DEVICE(f.dev), 0, f.irq);
    g_assert_true(sysbus_realize(SYS_BUS_DEVICE(f.dev), &error_abort));
    for (unsigned test = SUCCESS; test < CASES; test++) {
        bool complete = test <= IMMUTABLE || test == DATA_CHANGE ||
                        test == EDGE_KEEP || test == EDGE_COLOR;
        bool accepted = complete || test == MAP_LOSS || test == RESET ||
                        test == CANCEL;
        unsigned draw_bytes = 1736;
        g_autofree uint8_t *before = NULL;

        g_test_message("scaled case %u", test);
        device_cold_reset(f.dev);
        memset(data, 0, 0x4000);
        memset(target, 0xa5, SLOT + 8);
        for (unsigned i = 0; i < 720 * 480; i++) {
            unsigned color = (i % 720 + i / 720) % 7;
            unsigned alpha = test == EDGE_KEEP ? 0 : test == EDGE_COLOR ? 255 :
                              (uint8_t[]) { 0, 64, 128, 255 }[i % 4];
            if (test == EDGE_COLOR) {
                color = 4;
            }
            stl_le_p(source + 4 * i, (rgba[color] & 0xffffff) | (alpha << 24));
        }
        for (unsigned i = 0; i < 2 * PLANE; i += 4) {
            stl_le_p(target + 4 + i, test == EDGE_COLOR ? 0x8080ebeb : 0x80801010);
        }
        memset(target + 4 + 2 * PLANE,
               test == EDGE_KEEP || test == EDGE_COLOR ? 255 : 0, PLANE);
        build_convert_program(data, data + 0x1000);
        stl_le_p(data + 13 * 4, 0x3fb60b00);
        stl_le_p(data + 17 * 4, 0xbfb5dd80);
        stl_le_p(data + 20 * 4, 0x442bffff);
        stl_le_p(data + 50 * 4, 720);
        stl_le_p(data + 51 * 4, 480);
        stl_le_p(data + 52 * 4, -720);
        stl_le_p(data + 0x1000 + 9 * 4, 0x107);
        stl_le_p(data + 0x1000 + 46 * 4, 0x34fff8);
        stl_le_p(data + 0x1000 + 54 * 4, 0x3efffe);
        stl_le_p(data + 0x1000 + 56 * 4, 0x02d001e0);
        stl_le_p(data + 0x1000 + 60 * 4, 0x02d001e0);
        stl_le_p(data + 0x1000 + 405 * 4, 0x107);
        stl_le_p(data + 0x1000 + 409 * 4, 0x955000);
        stl_le_p(data + 0x1000 + 417 * 4, 7);
        stl_le_p(data + 0x1000 + 419 * 4, 7);
        if (test == IMMUTABLE || test == DATA_CHANGE ||
            test == MAP_LOSS || test == RESET ||
            test == CANCEL) {
            stl_le_p(data + 0x1000 + 74 * 4, 6);
        }
        switch (test) {
        case TEMP2: case TEMP4: case TEMP6:
            stl_le_p(data + 0x1000 + 74 * 4, 2 * (test - TEMP2 + 1)); break;
        case TEMP_ODD: stl_le_p(data + 0x1000 + 74 * 4, 1); break;
        case TEMP_SOLID: stl_le_p(data + 0x1000 + 74 * 4, 8); break;
        case TEMP_HIGH: stl_le_p(data + 0x1000 + 74 * 4, 0x100); break;
        case TEMP_HEADER: stl_le_p(data + 0x1000 + 73 * 4, 0x0184f004); break;
        case EARLY_DRAW: stl_le_p(data + 0x1000 + 73 * 4, 0x01a61004); break;
        case FINAL_MODE: stl_le_p(data + 0x1000 + 92 * 4, 6); break;
        case SHORT: draw_bytes -= 8; break;
        case TRANSFORM: stl_le_p(data + 13 * 4, 0x3f800000); break;
        case SAMPLE: stl_le_p(data + 0x1000 + 54 * 4, 0x3f0000); break;
        case SOURCE_SIZE: stl_le_p(data + 0x1000 + 60 * 4, 0x040002b0); break;
        case FORMAT: stl_le_p(data + 0x1000 + 405 * 4, 0x105); break;
        case FILTER: stl_le_p(data + 0x1000 + 417 * 4, 1); break;
        case LUT: stl_le_p(data + 0x1000 + 100 * 4, 0xfff); break;
        case GEOMETRY: stl_le_p(data + 51 * 4, 479); break;
        case DELTA: stl_le_p(data + 0x1000 + 409 * 4, 0x8aa800); break;
        case OVERFLOW:
            stl_le_p(data + 0x1000 + 407 * 4, 0xffe00000);
            stl_le_p(data + 0x1000 + 409 * 4, 0xfff55000);
            break;
        case RANGE: stl_le_p(data + 0x1000 + 70 * 4, 0x700000); break;
        case SOURCE_ALIAS: stl_le_p(data + 0x1000 + 70 * 4, 0x1000000); break;
        case PLANE_ALIAS:
            memory_region_add_subregion_overlap(root, 0x955000, &plane_alias, 1);
            break;
        default: break;
        }
        before = g_memdup2(target, SLOT + 8);
        g_assert_cmpint(submit_rgba(&f, 224, draw_bytes), ==,
                        accepted ? MEMTX_OK : MEMTX_ERROR);
        g_assert_false(f.level);
        g_assert_cmpmem(target, SLOT + 8, before, SLOT + 8);
        if (test == IMMUTABLE) {
            memset(data, 0xff, 224);
            memset(data + 0x1000, 0xff, 1736);
        }
        if (test == DATA_CHANGE) {
            stl_le_p(source + 4 * (479 * 720), rgba[2]);
        } else if (test == MAP_LOSS) {
            memory_region_set_readonly(&output, true);
        } else if (test == RESET) {
            device_cold_reset(f.dev);
        } else if (test == CANCEL) {
            qdev_unrealize(f.dev);
        }
        advance();
        memory_region_set_readonly(&output, false);
        if (test == PLANE_ALIAS) {
            memory_region_del_subregion(root, &plane_alias);
        }
        g_assert_cmpint(f.level, ==, complete);
        if (complete) {
            bool edge = false;
            double z = 131070.0 / 131072;
            for (unsigned y = 0; y < H; y++) {
                unsigned sy = (-98404.0 / 67108864 * (y + 6.5) +
                               66113.0 / 65536) * 480 / z;
                for (unsigned x = 0; x < W; x += 2) {
                    unsigned colors[2];
                    for (unsigned j = 0; j < 2; j++) {
                        unsigned sx = 131064.0 / 134217728 * (x + j + 0.5) * 720 / z;
                        unsigned a = source[4 * (sy * 720 + sx) + 3];
                        unsigned actual = target[4 + 2 * PLANE + y * W + x + j];
                        colors[j] = test == EDGE_COLOR ? 4 :
                                    a ? (sx + sy) % 7 : 0;
                        if (test == DATA_CHANGE && sx == 0 && sy == 479) {
                            colors[j] = 2;
                        }
                        if (test == EDGE_KEEP) {
                            g_assert_cmpuint(actual, <=, 8);
                            if (y > 0 && x + j < W - 1) {
                                g_assert_cmpuint(actual, ==, 0);
                            }
                            edge |= actual > 0;
                        } else if (test == EDGE_COLOR) {
                            g_assert_cmpuint(actual, ==, 255);
                        } else {
                            g_assert_cmpuint(actual, <=, a);
                            g_assert_cmpuint(actual + 4, >=, a);
                            if (y > 0 && x + j < W - 1) {
                                g_assert_cmpuint(actual, ==, a);
                            }
                        }
                    }
                    unsigned at = 4 + 2 * (y * W + x);
                    if (test == EDGE_COLOR) {
                        /* Analytic rectangle coverage bounds Cairo quantization. */
                        for (unsigned j = 0; j < 2; j++) {
                            unsigned lift = y == 0 ? 1 : 0;
                            lift += x + j == W - 1 ? 2 : 0;
                            unsigned expected = (uint8_t[]) {41,42,42,43}[lift];
                            g_assert_cmpuint(target[at + j] + 1, >=, expected);
                            g_assert_cmpuint(target[at + j], <=, expected + 1);
                            if (y > 0 && x + j < W - 1) {
                                g_assert_cmpuint(target[at + j], ==, 41);
                            }
                            edge |= target[at + j] > 41;
                        }
                        g_assert_cmpuint(target[at + 2], >=, 239);
                        g_assert_cmpuint(target[at + 2], <=, 240);
                        g_assert_cmpuint(target[at + 3], ==, 110);
                        continue;
                    }
                    g_assert_cmpuint(target[at], ==, yuv[colors[0]][0]);
                    g_assert_cmpuint(target[at + 1], ==, yuv[colors[1]][0]);
                    g_assert_cmpuint(target[at + 2], ==,
                        (yuv[colors[0]][1] + yuv[colors[1]][1] + 1) / 2);
                    g_assert_cmpuint(target[at + 3], ==,
                        (yuv[colors[0]][2] + yuv[colors[1]][2] + 1) / 2);
                }
            }
            if (test == EDGE_KEEP || test == EDGE_COLOR) {
                g_assert_true(edge);
            }
            g_assert_cmpmem(target, 4, before, 4);
            g_assert_cmpmem(target + 4 + 3 * PLANE, SLOT - 3 * PLANE + 4,
                            before + 4 + 3 * PLANE, SLOT - 3 * PLANE + 4);
            g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
        } else {
            g_assert_cmpmem(target, SLOT + 8, before, SLOT + 8);
        }
    }
    memory_region_del_subregion(root, &source_alias);
    memory_region_del_subregion(root, &output);
    memory_region_del_subregion(root, &input);
    memory_region_del_subregion(root, &commands);
    drain_call_rcu();
    object_unparent(OBJECT(f.dev));
    object_unref(OBJECT(f.dev));
    drain_call_rcu();
    qemu_free_irq(f.irq);
}

static void test_yuva_over(void)
{
    enum { W = 1024, H = 682, PLANE = W * H, INPUT = 720 * 480 * 4,
           SLOT = 0x4b0000 };
    enum { SUCCESS, TEMP2, TEMP4, TEMP6, TEMP_ODD, TEMP_HEADER, EARLY_DRAW,
           FINAL_MODE, REPEAT, COLD, RESET_INIT, FAILED_INIT, OTHER_INIT,
           CANCELED_INIT, REJECTED, SHORT, BLEND, FORMAT, SAMPLER, LUT,
           PARTIAL_WRITE, KEY, OPAQUE, DELTA, OVERFLOW, RANGE, SOURCE_ALIAS,
           PLANE_ALIAS, DATA_CHANGE, MAP_LOSS, RESET, CANCEL, CASES };
    static const uint8_t expected[4][3][4] = {
        {{16,128,128,0}, {41,240,110,128}, {41,240,110,255}},
        {{235,128,128,64}, {119,195,117,160}, {90,212,115,255}},
        {{235,128,128,128}, {170,165,122,192}, {138,184,119,255}},
        {{235,128,128,255}, {235,128,128,255}, {235,128,128,255}},
    };
    TransferFixture f = { .dev = qdev_new(TYPE_NIKON_TRANSFER) };
    MemoryRegion commands, input, output, source_alias, plane_alias;
    MemoryRegion *root = get_system_memory();
    uint8_t *data, *source, *target;

    object_property_add_child(object_get_objects_root(), "transfer-over[*]",
                              OBJECT(f.dev));
    memory_region_init_ram_flags_nomigrate(&commands, OBJECT(f.dev),
        "test.over-command", 0x4000, 0, &error_abort);
    memory_region_add_subregion(root, 0x400000, &commands);
    memory_region_init_ram_flags_nomigrate(&input, OBJECT(f.dev),
        "test.over-input", INPUT, 0, &error_abort);
    memory_region_add_subregion(root, 0x410000, &input);
    memory_region_init_ram_flags_nomigrate(&output, OBJECT(f.dev),
        "test.over-output", SLOT + 8, 0, &error_abort);
    memory_region_add_subregion(root, 0x800000 - 4, &output);
    memory_region_init_alias(&source_alias, OBJECT(f.dev), "test.over-alias",
                            &output, 4, INPUT);
    memory_region_add_subregion(root, 0x1000000, &source_alias);
    memory_region_init_alias(&plane_alias, OBJECT(f.dev), "test.over-planes",
                            &output, 4, PLANE);
    data = memory_region_get_ram_ptr(&commands);
    source = memory_region_get_ram_ptr(&input);
    target = memory_region_get_ram_ptr(&output);
    f.irq = qemu_allocate_irq(irq_level, &f, 0);
    sysbus_connect_irq(SYS_BUS_DEVICE(f.dev), 0, f.irq);
    g_assert_true(sysbus_realize(SYS_BUS_DEVICE(f.dev), &error_abort));
    for (unsigned test = SUCCESS; test < CASES; test++) {
        bool complete = test <= TEMP6 || test == REPEAT || test == DATA_CHANGE;
        bool accepted = complete || test == MAP_LOSS || test == RESET ||
                        test == CANCEL;
        unsigned draw_bytes = 1552;
        g_autofree uint8_t *before = NULL;

        g_test_message("over case %u", test);
        device_cold_reset(f.dev);
        memset(source, 0, INPUT);
        memset(target, 0, SLOT + 8);
        build_scaled_program(data, data + 0x1000, false);
        if (test != COLD) {
            if (test == FAILED_INIT) {
                stl_le_p(data + 0x1000 + 417 * 4, 1);
            }
            g_assert_cmpint(submit_rgba(&f, 224, 1736), ==,
                            test == FAILED_INIT ? MEMTX_ERROR : MEMTX_OK);
            if (test == CANCELED_INIT) {
                device_cold_reset(f.dev);
            }
            advance();
            if (test != FAILED_INIT && test != CANCELED_INIT) {
                g_assert_true(f.level);
                g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
            }
        }
        if (test == RESET_INIT) {
            device_cold_reset(f.dev);
        } else if (test == OTHER_INIT) {
            build_clear_program(data, data + 0x1000, 0x105, 0x800000, 0x8aa800);
            g_assert_cmpint(submit_rgba(&f, 224, 1648), ==, MEMTX_OK);
            advance();
            g_assert_true(f.level);
            g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
        } else if (test == REJECTED) {
            build_scaled_program(data, data + 0x1000, true);
            stl_le_p(data + 0x1000 + 27 * 4, 0);
            g_assert_cmpint(submit_rgba(&f, 176, 1552), ==, MEMTX_ERROR);
        }
        for (unsigned i = 0; i < 720 * 480; i++) {
            stl_le_p(source + 4 * i, 0xffffff |
                      ((uint32_t)(uint8_t[]) {0,64,128,255}[i % 4] << 24));
        }
        memset(target, 0xa5, SLOT + 8);
        for (unsigned i = 0; i < PLANE; i += 2) {
            stl_le_p(target + 4 + 2 * i, 0x6ef02929); /* Opaque blue in YUV. */
        }
        for (unsigned i = 0; i < PLANE; i++) {
            target[4 + 2 * PLANE + i] = (uint8_t[]) {0,128,255}[i % 3];
        }
        build_scaled_program(data, data + 0x1000, true);
        switch (test) {
        case TEMP2: case TEMP4: case TEMP6:
            stl_le_p(data + 0x1000 + 29 * 4, 2 * (test - TEMP2 + 1)); break;
        case TEMP_ODD: stl_le_p(data + 0x1000 + 29 * 4, 1); break;
        case TEMP_HEADER: stl_le_p(data + 0x1000 + 28 * 4, 0x0184f004); break;
        case EARLY_DRAW: stl_le_p(data + 0x1000 + 28 * 4, 0x01a61004); break;
        case FINAL_MODE: stl_le_p(data + 0x1000 + 47 * 4, 6); break;
        case SHORT: draw_bytes -= 8; break;
        case BLEND: stl_le_p(data + 0x1000 + 27 * 4, 0); break;
        case FORMAT: stl_le_p(data + 0x1000 + 360 * 4, 0x105); break;
        case SAMPLER: stl_le_p(data + 0x1000 + 9 * 4, 0x3f0000); break;
        case LUT: stl_le_p(data + 0x1000 + 55 * 4, 99); break;
        case PARTIAL_WRITE: stl_le_p(data + 0x1000 + 314 * 4, 0x0196f004); break;
        case KEY: stl_le_p(data + 42 * 4, 0x10003); break;
        case OPAQUE: stl_le_p(data + 0x1000 + 376 * 4, 0); break;
        case DELTA: stl_le_p(data + 0x1000 + 364 * 4, 0x8aa800); break;
        case OVERFLOW:
            stl_le_p(data + 0x1000 + 362 * 4, 0xffe00000);
            stl_le_p(data + 0x1000 + 364 * 4, 0xfff55000);
            break;
        case RANGE: stl_le_p(data + 0x1000 + 25 * 4, 0x700000); break;
        case SOURCE_ALIAS: stl_le_p(data + 0x1000 + 25 * 4, 0x1000000); break;
        case PLANE_ALIAS:
            memory_region_add_subregion_overlap(root, 0x955000, &plane_alias, 1);
            break;
        default: break;
        }
        before = g_memdup2(target, SLOT + 8);
        g_assert_cmpint(submit_rgba(&f, 176, draw_bytes), ==,
                        accepted ? MEMTX_OK : MEMTX_ERROR);
        g_assert_false(f.level);
        if (test == DATA_CHANGE) {
            stl_le_p(source + 4 * (479 * 720), 0xffffffff);
        } else if (test == MAP_LOSS) {
            memory_region_set_readonly(&output, true);
        } else if (test == RESET) {
            device_cold_reset(f.dev);
        } else if (test == CANCEL) {
            qdev_unrealize(f.dev);
        }
        advance();
        memory_region_set_readonly(&output, false);
        if (test == PLANE_ALIAS) {
            memory_region_del_subregion(root, &plane_alias);
        }
        g_assert_cmpint(f.level, ==, complete);
        if (complete) {
            for (unsigned y = 0; y < H; y++) {
                unsigned sy = (-98404.0 / 67108864 * (y + 6.5) +
                               66113.0 / 65536) * 480 / (131070.0 / 131072);
                for (unsigned x = 0; x < W; x += 2) {
                    const uint8_t *want[2];
                    unsigned at = y * W + x;
                    for (unsigned j = 0; j < 2; j++) {
                        unsigned sx = 131064.0 / 134217728 * (x + j + 0.5) *
                                      720 / (131070.0 / 131072);
                        unsigned a = (sy * 720 + sx) % 4;
                        if (test == DATA_CHANGE && sy == 479 && sx == 0) {
                            a = 3;
                        }
                        want[j] = expected[a][(at + j) % 3];
                    }
                    uint8_t values[] = {want[0][0], want[1][0],
                        (want[0][1] + want[1][1] + 1) / 2,
                        (want[0][2] + want[1][2] + 1) / 2};
                    unsigned tolerance = y == 0 || x == W - 2 ? 2 : 0;
                    for (unsigned c = 0; c < 4; c++) {
                        unsigned actual = target[4 + 2 * at + c];
                        g_assert_cmpuint(actual + tolerance, >=, values[c]);
                        g_assert_cmpuint(actual, <=, values[c] + tolerance);
                    }
                    for (unsigned j = 0; j < 2; j++) {
                        unsigned actual = target[4 + 2 * PLANE + at + j];
                        g_assert_cmpuint(actual + tolerance, >=, want[j][3]);
                        g_assert_cmpuint(actual, <=, want[j][3] + tolerance);
                    }
                }
            }
            g_assert_cmpmem(target, 4, before, 4);
            g_assert_cmpmem(target + 4 + 3 * PLANE, SLOT - 3 * PLANE + 4,
                            before + 4 + 3 * PLANE, SLOT - 3 * PLANE + 4);
            g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
            if (test == REPEAT) {
                g_assert_cmpint(submit_rgba(&f, 176, 1552), ==, MEMTX_OK);
                advance();
                g_assert_true(f.level);
            }
        } else {
            g_assert_cmpmem(target, SLOT + 8, before, SLOT + 8);
            if (test == MAP_LOSS || test == RESET) {
                build_scaled_program(data, data + 0x1000, true);
                g_assert_cmpint(submit_rgba(&f, 176, 1552), ==, MEMTX_ERROR);
                advance();
                g_assert_false(f.level);
                g_assert_cmpmem(target, SLOT + 8, before, SLOT + 8);
            }
        }
    }
    memory_region_del_subregion(root, &source_alias);
    memory_region_del_subregion(root, &output);
    memory_region_del_subregion(root, &input);
    memory_region_del_subregion(root, &commands);
    drain_call_rcu();
    object_unparent(OBJECT(f.dev));
    object_unref(OBJECT(f.dev));
    drain_call_rcu();
    qemu_free_irq(f.irq);
}

typedef struct InlineRectangle {
    int32_t x, y, width, height;
    uint32_t paint;
} InlineRectangle;

static void inline_word(uint8_t *bytes, unsigned *at, uint32_t value)
{
    stl_le_p(bytes + 4 * (*at)++, value);
}

static void inline_pair(uint8_t *bytes, unsigned *at, uint32_t header,
                         uint32_t value)
{
    inline_word(bytes, at, header);
    inline_word(bytes, at, value);
}

static void build_inline_rectangles(uint8_t *setup, uint8_t *draw,
                                     const InlineRectangle *rectangles,
                                     unsigned count, uint32_t target,
                                     bool initial, uint32_t first_key,
                                     unsigned *setup_bytes,
                                     unsigned *draw_bytes)
{
    unsigned a = 0, b = 0;

#define A(h, v) inline_pair(setup, &a, h, v)
#define B(h, v) inline_pair(draw, &b, h, v)
    if (initial) {
        A(0x00691004, 0x80);
        B(0x01408004, 0); B(0x01901004, 0);
        B(0x014ee004, 0x1b0000); B(0x0156e004, 0x1b0000);
        B(0x01a2f004, 0); B(0x01a3f004, 0x01e002d0);
        B(0x01844004, 0); B(0x01a7f004, 0x01000101);
        B(0x011cf004, 480U << 16); B(0x01a08004, 0);
        inline_word(draw, &b, 0x00c0f00e);
        inline_word(draw, &b, 719U << 16);
        inline_word(draw, &b, 479U << 16);
        B(0x01001004, 1); B(0x01151004, 1);
        B(0x01b01004, 13); B(0x01b31004, 1);
        B(0x01848004, 0); B(0x01a11004, 0);
    } else {
        B(0x01848004, 0);
    }
    for (unsigned i = 0; i < count; i++) {
        const InlineRectangle *r = &rectangles[i];
        uint32_t key = 0x10000 | ((first_key - 0x10001 + i) % 0xfffe + 1);
        uint32_t scratch = (i & 1) ? 0x22220000 : 0x11110000;

        for (unsigned m = 0; m < 2; m++) {
            inline_word(setup, &a, 0x0041f046);
            for (unsigned k = 0; k < 9; k++) {
                inline_word(setup, &a, k % 4 ? 0 : 0x3f800000);
            }
        }
        A(0x00653004, 0x100); A(0x0040f004, 0x01e002d0);
        A(0x0003f004, 5); A(0x0005f004, 0x80000005);
        A(0x0063f004, 5); A(0x005ef004, 0x3f800000);
        A(0x005ff004, 0);
        inline_word(setup, &a, 0x0080f026);
        inline_word(setup, &a, scratch); inline_word(setup, &a, 0);
        inline_word(setup, &a, 16);
        inline_word(setup, &a, 0x01e002d0); inline_word(setup, &a, 0);
        A(0x0006f004, 1);
        inline_word(setup, &a, 0x0a);
        inline_word(setup, &a, 0x07090702); inline_word(setup, &a, 0);
        inline_word(setup, &a, 0x00010022);
        inline_word(setup, &a, r->x); inline_word(setup, &a, r->y);
        inline_word(setup, &a, r->width);
        inline_word(setup, &a, r->height);
        inline_word(setup, &a, -r->width);
        A(0x0085f004, key);

        B(0x0115c004, 0);
        B(0x01401004, 0); B(0x014df004, 0); B(0x014df004, 0);
        B(0x01401004, 0); B(0x014df004, 0); B(0x014df004, 0);
        B(0x01402004, 0); B(0x014df004, 0); B(0x014df004, 0);
        B(0x01841004, 8); B(0x01863004, 0x108); B(0x01842004, 0);
        B(0x01a02004, 0); B(0x01a29004, 0); B(0x01a4f004, target);
        B(0x01a74004, 0); B(0x0185f004, r->paint);
        B(0x01841004, 8); B(0x01848004, 0);
        B(0x01a04004, 0); B(0x01a51004, 0); B(0x01aa1004, 0);
        inline_word(draw, &b, 0x00a0f02e);
        inline_word(draw, &b, scratch); inline_word(draw, &b, 0);
        inline_word(draw, &b, 16);
        inline_word(draw, &b, 0x01e002d0);
        inline_word(draw, &b, 0); inline_word(draw, &b, 0);
        B(0x00a6f004, key); B(0x01a61004, 1);
        if (i + 1 < count) {
            B(0x01848004, 0);
        }
    }
    B(0x00081004, 1);
    if (a & 1) {
        inline_word(setup, &a, 0);
    }
    if (b & 1) {
        inline_word(draw, &b, 0);
    }
    *setup_bytes = a * 4;
    *draw_bytes = b * 4;
#undef A
#undef B
}

static MemTxResult submit_inline_rectangles(TransferFixture *f, uint32_t source,
                                             unsigned setup_bytes,
                                             unsigned draw_bytes, bool initial)
{
    const uint32_t packet[][2] = {
        {0x1214, initial ? 0x100ffff : 0xffff},
        {0x1298, 0}, {0x10d4, source},
        {0x10dc, setup_bytes / 8}, {0x10d0, 1},
        {0x10e4, 0x420000}, {0x10ec, draw_bytes / 8},
    };

    for (unsigned i = 0; i < ARRAY_SIZE(packet); i++) {
        g_assert_cmpint(write_register(f, packet[i][0], packet[i][1]), ==,
                        MEMTX_OK);
    }
    return write_register(f, 0x10e0, 1);
}

static void build_rectangle_prefixes(uint8_t *setup, uint8_t *draw,
                                     const InlineRectangle *rectangles,
                                     unsigned count, uint32_t target,
                                     uint32_t key, const uint8_t *prefixes,
                                     unsigned *setup_bytes,
                                     unsigned *draw_bytes)
{
    unsigned a = 0, b = 0;

    build_inline_rectangles(setup, draw, rectangles, count, target, false, key,
                            setup_bytes, draw_bytes);
    for (unsigned i = 0; i < count; i++) {
        unsigned skip = prefixes[i] & 1 ? 0 : 10;

        memmove(setup + 4 * a, setup + 4 * (53 * i + skip), 4 * (53 - skip));
        a += 53 - skip;
        if (prefixes[i] & 2) {
            memmove(draw + 4 * b, draw + 4 * (59 * i), 8);
            b += 2;
        }
        memmove(draw + 4 * b, draw + 4 * (59 * i + 2), 57 * 4);
        b += 57;
    }
    inline_pair(draw, &b, 0x00081004, 1);
    if (a & 1) {
        inline_word(setup, &a, 0);
    }
    if (b & 1) {
        inline_word(draw, &b, 0);
    }
    *setup_bytes = 4 * a;
    *draw_bytes = 4 * b;
}

static void test_rectangle_prefixes(void)
{
    enum { SLOT = 0x4b0000, GUARDS = SLOT + 8 };
    enum { A8_CHAIN, NONE, MATRIX, COLOR, BOTH, MIXED, OBSERVED, MAXIMUM, RAW,
           SKIP_KEY, REUSE_KEY, IMMUTABLE,
           NO_CONTEXT, RESET_CONTEXT, FAILED_CONTEXT, YUV105_CONTEXT,
           YUVA107_CONTEXT, ACTIVE_MATRIX,
           FIRST_HEADER, SECOND_HEADER,
           COLOR_VALUE, COLOR_MASK, DOUBLE_COLOR, ACTIVE_COLOR, MODE, EARLY_LATCH,
           KEY, INNER_KEY, TARGET, SCRATCH, SHORT_SETUP, SHORT_DRAW,
           SETUP_PAD, DRAW_PAD,
           ENDING, SOURCE_ALIAS, LOSS, RESET, CANCEL, CASES };
    TransferFixture f = { .dev = qdev_new(TYPE_NIKON_TRANSFER) };
    MemoryRegion commands, output, alias;
    MemoryRegion *root = get_system_memory();
    uint8_t *data, *pixels;

    object_property_add_child(object_get_objects_root(), "transfer-prefixes[*]",
                              OBJECT(f.dev));
    memory_region_init_ram_flags_nomigrate(&commands, OBJECT(f.dev),
        "test.rectangle-prefix-command", 0x40000, 0, &error_abort);
    memory_region_add_subregion(root, 0x400000, &commands);
    memory_region_init_ram_flags_nomigrate(&output, OBJECT(f.dev),
        "test.rectangle-prefix-output", GUARDS, 0, &error_abort);
    memory_region_add_subregion(root, 0x800000, &output);
    memory_region_init_alias(&alias, OBJECT(f.dev), "test.rectangle-prefix-alias",
                             &output, 4, SLOT);
    memory_region_add_subregion(root, 0x1000000, &alias);
    data = memory_region_get_ram_ptr(&commands);
    pixels = memory_region_get_ram_ptr(&output);
    f.irq = qemu_allocate_irq(irq_level, &f, 0);
    sysbus_connect_irq(SYS_BUS_DEVICE(f.dev), 0, f.irq);
    g_assert_true(sysbus_realize(SYS_BUS_DEVICE(f.dev), &error_abort));
    for (unsigned test = A8_CHAIN; test < CASES; test++) {
        InlineRectangle rectangles[NIKON_PATH_MAX_BATCH];
        uint8_t prefixes[NIKON_PATH_MAX_BATCH];
        unsigned count = test == MAXIMUM ? 256 :
                         test == SETUP_PAD || test == DRAW_PAD ? 1 : 6;
        unsigned setup_bytes, draw_bytes;
        unsigned first_setup, first_draw;
        uint32_t key = test == SKIP_KEY ? 0x10080 :
                       test == REUSE_KEY ? 0x10001 : 0x10009;
        uint32_t source = 0x400000;
        bool complete = test <= IMMUTABLE;
        bool accepted = complete || test >= LOSS;
        g_autofree uint8_t *before = NULL;
        g_autofree uint8_t *expected = NULL;
        g_autofree uint8_t *setup_copy = NULL;
        g_autofree uint8_t *draw_copy = NULL;

        g_test_message("rectangle prefix case %u", test);
        device_cold_reset(f.dev);
        memset(data, 0, 0x40000);
        memset(pixels, 0xa5, GUARDS);
        for (unsigned i = 0; i < 8; i++) {
            rectangles[i] = (InlineRectangle) { i * 8, 2, 4, 4, 0xff123456 };
        }
        if (test != NO_CONTEXT) {
            build_inline_rectangles(data, data + 0x20000, rectangles,
                test == A8_CHAIN ? 6 : 8,
                0x800004, true, 0x10001, &setup_bytes, &draw_bytes);
            g_assert_cmpint(submit_inline_rectangles(&f, source, setup_bytes,
                                                      draw_bytes, true), ==,
                            MEMTX_OK);
            if (test == FAILED_CONTEXT) {
                memory_region_set_readonly(&output, true);
            }
            advance();
            memory_region_set_readonly(&output, false);
            g_assert_cmpint(f.level, ==, test != FAILED_CONTEXT);
            if (f.level) {
                g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
            }
            if (test == RESET_CONTEXT) {
                device_cold_reset(f.dev);
            }
        }
        if (test == A8_CHAIN) {
            for (unsigned i = 0; i < 2; i++) {
                build_image_program(data, data + 0x20000);
                stl_le_p(data + 42 * 4, 0x10007 + i);
                stl_le_p(data + 0x20000 + 59 * 4, 0x800004);
                stl_le_p(data + 0x20000 + 78 * 4, 0x10007 + i);
                stl_le_p(data + 0x20000 + 29 * 4, i ? 14 : 8);
                for (unsigned p = 0; p < 32; p++) {
                    data[0x3000 + p] = (p * 67) & 255;
                }
                g_assert_cmpint(submit_inline_rectangles(&f, source, 176, 336,
                                                          false), ==, MEMTX_OK);
                advance();
                g_assert_true(f.level);
                g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
            }
        } else if (test == YUV105_CONTEXT || test == YUVA107_CONTEXT) {
            uint32_t format = test == YUV105_CONTEXT ? 0x105 : 0x107;
            uint32_t primary = 1024 * 682 * (format == 0x107 ? 2 : 1);

            build_clear_program(data, data + 0x20000, format,
                                0x800004, 0x800004 + primary);
            g_assert_cmpint(submit_inline_rectangles(&f, source, 224, 1648,
                                                      true), ==, MEMTX_OK);
            advance();
            g_assert_true(f.level);
            g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
        }
        for (unsigned i = 0; i < count; i++) {
            rectangles[i] = (InlineRectangle) {
                10 + i % 12, 20 + i % 8, 17, 9, 0x12345678U * (i + 1),
            };
            prefixes[i] = test >= NONE && test <= BOTH ? test - NONE :
                          test == MIXED ? i % 4 :
                          test == OBSERVED || test == A8_CHAIN ? (i ? 3 : 0) :
                          test == MAXIMUM ? (i * 3) % 4 : 0;
        }
        if (test == OBSERVED) {
            for (unsigned i = 0; i < count; i++) {
                rectangles[i] = i == 0 ?
                    (InlineRectangle) {136, 6, 112, 40, 0} : i == 1 ?
                    (InlineRectangle) {594, 6, 100, 38, 0} :
                    (InlineRectangle) {90, 402, 36, 36, 0};
            }
        }
        if (test == RAW || test == FIRST_HEADER || test == SECOND_HEADER ||
            test == EARLY_LATCH || test == SOURCE_ALIAS) {
            prefixes[0] = 1;
        } else if (test == COLOR_VALUE || test == COLOR_MASK ||
                   test == DOUBLE_COLOR) {
            prefixes[0] = 2;
        }
        build_rectangle_prefixes(data, data + 0x20000, rectangles, count,
            0x800004, key, prefixes, &setup_bytes, &draw_bytes);
        if (test == OBSERVED) {
            g_assert_cmpuint(setup_bytes, ==, 1232);
            g_assert_cmpuint(draw_bytes, ==, 1416);
        }
        first_setup = prefixes[0] & 1 ? 10 : 0;
        first_draw = prefixes[0] & 2 ? 2 : 0;
        switch (test) {
        case RAW:
            for (unsigned i = 0; i < 9; i++) {
                stl_le_p(data + 4 * (i + 1),
                         (uint32_t[]) {0x7fc00001, 0x7f800000,
                                       0xffffffff, 0x01a61004}[i % 4]);
            }
            break;
        case ACTIVE_MATRIX:
            stl_le_p(data + 4 * (first_setup + 1), 0x40000000); break;
        case FIRST_HEADER: stl_le_p(data, 0x0041f03e); break;
        case SECOND_HEADER: stl_le_p(data + 40, 0x0042f046); break;
        case COLOR_VALUE: stl_le_p(data + 0x20004, 0x01000000); break;
        case COLOR_MASK: stl_le_p(data + 0x20000, 0x01841004); break;
        case DOUBLE_COLOR:
            memmove(data + 0x20008, data + 0x20000, draw_bytes);
            stl_le_p(data + 0x20000, 0x01848004);
            stl_le_p(data + 0x20004, 0);
            draw_bytes += 8;
            break;
        case ACTIVE_COLOR:
            stl_le_p(data + 0x20000 + 4 * (first_draw + 39), 0x01000000); break;
        case MODE:
            stl_le_p(data + 0x20000 + 4 * (first_draw + 37), 0); break;
        case EARLY_LATCH: stl_le_p(data + 40, 0x0006f004); break;
        case KEY: stl_le_p(data + 4 * (first_setup + 42), 0x10008); break;
        case INNER_KEY:
            stl_le_p(data + 4 * (43 + 42), 0x1000b);
            stl_le_p(data + 0x20000 + 4 * (57 + 54), 0x1000b);
            break;
        case TARGET:
            stl_le_p(data + 0x20000 + 4 * (57 + 31), 0x800008); break;
        case SCRATCH: stl_le_p(data + 4 * (first_setup + 25), 0); break;
        case SHORT_SETUP: setup_bytes -= 8; break;
        case SHORT_DRAW: draw_bytes -= 8; break;
        case SETUP_PAD: stl_le_p(data + setup_bytes - 4, 1); break;
        case DRAW_PAD: stl_le_p(data + 0x20000 + draw_bytes - 4, 1); break;
        case ENDING: stl_le_p(data + 0x20000 + draw_bytes - 4, 0); break;
        case SOURCE_ALIAS:
            source = 0x1000000;
            memcpy(pixels + 4, data, setup_bytes);
            break;
        default: break;
        }
        before = g_memdup2(pixels, GUARDS);
        expected = g_memdup2(pixels, GUARDS);
        setup_copy = g_memdup2(data, setup_bytes);
        draw_copy = g_memdup2(data + 0x20000, draw_bytes);
        for (unsigned i = 0; i < count; i++) {
            InlineRectangle *r = &rectangles[i];

            for (unsigned y = r->y; y < r->y + r->height; y++) {
                for (unsigned x = r->x; x < r->x + r->width; x++) {
                    stl_le_p(expected + 4 + 4 * (720 * y + x), r->paint);
                }
            }
        }
        g_assert_cmpint(submit_inline_rectangles(&f, source, setup_bytes,
                                                 draw_bytes, false), ==,
                        accepted ? MEMTX_OK : MEMTX_ERROR);
        g_assert_false(f.level);
        g_assert_cmpmem(pixels, GUARDS, before, GUARDS);
        if (test == IMMUTABLE) {
            memset(data, 0xff, 0x40000);
        } else if (test == LOSS) {
            memory_region_set_readonly(&output, true);
        } else if (test == RESET) {
            device_cold_reset(f.dev);
        } else if (test == CANCEL) {
            qdev_unrealize(f.dev);
        }
        advance();
        memory_region_set_readonly(&output, false);
        g_assert_cmpint(f.level, ==, complete);
        g_assert_cmpmem(pixels, GUARDS, complete ? expected : before, GUARDS);
        if (test != IMMUTABLE) {
            g_assert_cmpmem(data, setup_bytes, setup_copy, setup_bytes);
            g_assert_cmpmem(data + 0x20000, draw_bytes, draw_copy, draw_bytes);
        }
        if (complete) {
            g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
        }
    }
    memory_region_del_subregion(root, &alias);
    memory_region_del_subregion(root, &output);
    memory_region_del_subregion(root, &commands);
    drain_call_rcu();
    object_unparent(OBJECT(f.dev));
    object_unref(OBJECT(f.dev));
    drain_call_rcu();
    qemu_free_irq(f.irq);
}

static void test_inline_rectangles(void)
{
    enum { W = 720, H = 480, BYTES = W * H * 4, SLOT = 0x4b0000 };
    enum { SUCCESS, TRANSPARENT, OVERLAP, ONE, TWO, THREE, FIVE, MAXIMUM,
           TOO_MANY, SHORT_SETUP, SHORT_DRAW, SETUP_PAD, DRAW_PAD, HEADER,
           MATRIX, SEGMENTS, COORDINATES, WIDTH, POSITION, SIZE, CLIP, FORMAT,
           BLEND, MODE, KEY, KEY_ZERO, SCRATCH_ZERO, SCRATCH_ECHO, TARGET,
           TARGET_ALIGN, RANGE, ADDRESS_WRAP, READONLY, COMMAND_OVERLAP,
           SOURCE_ALIAS,
           ENDING, DATA_CHANGE, MAP_LOSS, RESET, CANCEL, CASES };
    const InlineRectangle original[] = {
        {128, 2, 124, 48, 0}, {266, 2, 104, 48, 0x80332211},
        {436, 2, 153, 48, 0xffaabbcc}, {195, 60, 328, 48, 0x44556677},
    };
    TransferFixture f = { .dev = qdev_new(TYPE_NIKON_TRANSFER) };
    MemoryRegion commands, output, alias, high;
    MemoryRegion *root = get_system_memory();
    uint8_t *data, *pixels, *high_pixels;

    object_property_add_child(object_get_objects_root(), "transfer-inline[*]",
                              OBJECT(f.dev));
    memory_region_init_ram_flags_nomigrate(&commands, OBJECT(f.dev),
        "test.inline-command", 0x40000, 0, &error_abort);
    memory_region_add_subregion(root, 0x400000, &commands);
    memory_region_init_ram_flags_nomigrate(&output, OBJECT(f.dev),
        "test.inline-output", SLOT + 8, 0, &error_abort);
    memory_region_add_subregion(root, 0x800000 - 4, &output);
    memory_region_init_alias(&alias, OBJECT(f.dev), "test.inline-alias",
                            &output, 4, SLOT);
    memory_region_add_subregion(root, 0x1000000, &alias);
    memory_region_init_ram_flags_nomigrate(&high, OBJECT(f.dev),
        "test.inline-high", BYTES + 0x10000, 0, &error_abort);
    memory_region_add_subregion(root, 0xffff0000, &high);
    data = memory_region_get_ram_ptr(&commands);
    pixels = memory_region_get_ram_ptr(&output);
    high_pixels = memory_region_get_ram_ptr(&high);
    f.irq = qemu_allocate_irq(irq_level, &f, 0);
    sysbus_connect_irq(SYS_BUS_DEVICE(f.dev), 0, f.irq);
    g_assert_true(sysbus_realize(SYS_BUS_DEVICE(f.dev), &error_abort));
    /* The LUT family shares reset header1a11 with rectangle initialization.
     * Its32x32 geometry must still dispatch to the real LUT renderer. */
    build_lut_program(data, data + 0x20000, 0x410000, 0x800000);
    stl_le_p(data + 0x20000 + 33 * 4, 0x01a11004);
    /* The original LUT also shares the initialized image's f24 body header. */
    stl_le_p(data + 0x20000 + 35 * 4, 0x010c7046);
    g_assert_cmpint(submit_inline_rectangles(&f, 0x400000, 224, 1688, true), ==,
                    MEMTX_OK);
    advance();
    g_assert_true(f.level);
    g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
    for (unsigned test = SUCCESS; test < CASES; test++) {
        InlineRectangle rectangles[NIKON_PATH_MAX_BATCH + 1];
        unsigned count = test == ONE ? 1 : test == TWO ? 2 : test == THREE ? 3 :
                         test == FIVE ? 5 : test == MAXIMUM ? 256 :
                         test == TOO_MANY ? 257 : 4;
        unsigned setup_bytes, draw_bytes;
        uint32_t source = 0x400000;
        bool complete = test <= MAXIMUM || test == DATA_CHANGE;
        bool accepted = complete || test == MAP_LOSS || test == RESET ||
                        test == CANCEL;
        g_autofree uint8_t *before = NULL;
        g_autofree uint8_t *expected = NULL;

        g_test_message("inline rectangle case %u", test);
        device_cold_reset(f.dev);
        memset(data, 0, 0x40000);
        memset(pixels, 0xa5, SLOT + 8);
        memset(high_pixels, 0x5a, BYTES + 0x10000);
        for (unsigned i = 0; i < count; i++) {
            rectangles[i] = i < 4 ? original[i] : (InlineRectangle) {
                i % 600, i % 400, 7, 8, 0x01020304U * i,
            };
            if (test == TRANSPARENT) {
                rectangles[i].paint = 0;
            } else if (test == OVERLAP) {
                rectangles[i].x = 2;
                rectangles[i].y = 3;
            }
        }
        build_inline_rectangles(data, data + 0x20000, rectangles, count,
                                0x800000, true, 0x1fffd,
                                &setup_bytes, &draw_bytes);
        if (count == 4) {
            g_assert_cmpuint(setup_bytes, ==, 856);
            g_assert_cmpuint(draw_bytes, ==, 1088);
        }
        switch (test) {
        case SHORT_SETUP: setup_bytes -= 8; break;
        case SHORT_DRAW: draw_bytes -= 8; break;
        case SETUP_PAD:
            /* Three records have a setup padding word. */
            build_inline_rectangles(data, data + 0x20000, rectangles, 3,
                                    0x800000, true, 0x1fffd,
                                    &setup_bytes, &draw_bytes);
            stl_le_p(data + setup_bytes - 4, 1);
            break;
        case DRAW_PAD: stl_le_p(data + 0x20000 + draw_bytes - 4, 1); break;
        case HEADER: data[2 * 4] ^= 1; break;
        case MATRIX: stl_le_p(data + (2 + 11) * 4, 0x40000000); break;
        case SEGMENTS: data[(2 + 43) * 4] = 4; break;
        case COORDINATES: stl_le_p(data + (2 + 50) * 4, -123); break;
        case WIDTH: stl_le_p(data + (2 + 48) * 4, 0); break;
        case POSITION: stl_le_p(data + (2 + 46) * 4, 720); break;
        case SIZE: stl_le_p(data + (2 + 23) * 4, 0x01e102d0); break;
        case CLIP: stl_le_p(data + 0x20000 + 21 * 4, 0x02ce0000); break;
        case FORMAT: stl_le_p(data + 0x20000 + 9 * 4, 0x105); break;
        case BLEND: stl_le_p(data + 0x20000 + (35 + 41) * 4, 0x10000); break;
        case MODE: stl_le_p(data + 0x20000 + (35 + 21) * 4, 14); break;
        case KEY: stl_le_p(data + 0x20000 + (35 + 59 * 3 + 54) * 4, 1); break;
        case KEY_ZERO: stl_le_p(data + (2 + 52) * 4, 0x10000); break;
        case SCRATCH_ZERO: stl_le_p(data + (2 + 35) * 4, 0); break;
        case SCRATCH_ECHO: stl_le_p(data + 0x20000 + (35 + 47) * 4, 1); break;
        case TARGET: stl_le_p(data + 0x20000 + (35 + 59 * 3 + 31) * 4,
                             0x800004); break;
        case TARGET_ALIGN: case RANGE: case ADDRESS_WRAP:
        case READONLY: case COMMAND_OVERLAP:
            for (unsigned i = 0; i < 4; i++) {
                stl_le_p(data + 0x20000 + (35 + 59 * i + 31) * 4,
                    test == TARGET_ALIGN ? 0x800001 : test == RANGE ?
                    0x2000000 : test == ADDRESS_WRAP ? 0xfffff000 :
                    test == COMMAND_OVERLAP ? 0x400000 : 0x800000);
            }
            if (test == READONLY) {
                memory_region_set_readonly(&output, true);
            }
            break;
        case SOURCE_ALIAS:
            source = 0x1000000;
            memcpy(pixels + 4, data, setup_bytes);
            break;
        case ENDING: stl_le_p(data + 0x20000 + 269 * 4, 0); break;
        default: break;
        }
        before = g_memdup2(pixels, SLOT + 8);
        expected = g_memdup2(pixels, SLOT + 8);
        for (unsigned i = 0; i < count; i++) {
            InlineRectangle *r = &rectangles[i];

            for (unsigned y = r->y; y < r->y + r->height; y++) {
                for (unsigned x = r->x; x < r->x + r->width; x++) {
                    stl_le_p(expected + 4 + 4 * (y * W + x), r->paint);
                }
            }
        }
        g_assert_cmpint(submit_inline_rectangles(&f, source, setup_bytes,
                                                 draw_bytes, true), ==,
                        accepted ? MEMTX_OK : MEMTX_ERROR);
        g_assert_false(f.level);
        g_assert_cmpmem(pixels, SLOT + 8, before, SLOT + 8);
        if (test == DATA_CHANGE) {
            memset(data, 0xff, 0x40000);
        } else if (test == MAP_LOSS) {
            memory_region_set_readonly(&output, true);
        } else if (test == RESET) {
            device_cold_reset(f.dev);
        } else if (test == CANCEL) {
            qdev_unrealize(f.dev);
        }
        advance();
        memory_region_set_readonly(&output, false);
        g_assert_cmpint(f.level, ==, complete);
        g_assert_cmpmem(pixels, SLOT + 8,
                        complete ? expected : before, SLOT + 8);
        for (unsigned i = 0; i < BYTES + 0x10000; i++) {
            g_assert_cmphex(high_pixels[i], ==, 0x5a);
        }
        if (complete) {
            g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
            g_assert_false(f.level);
        }
    }
    memory_region_del_subregion(root, &alias);
    memory_region_del_subregion(root, &high);
    memory_region_del_subregion(root, &output);
    memory_region_del_subregion(root, &commands);
    drain_call_rcu();
    object_unparent(OBJECT(f.dev));
    object_unref(OBJECT(f.dev));
    drain_call_rcu();
    qemu_free_irq(f.irq);
}

static void test_rectangle_continuation(void)
{
    enum { SLOT = 0x4b0000, GUARDS = SLOT + 8 };
    enum { SUCCESS, COLORED, OVERLAP, ONE, TWO, THREE, FIVE, MAXIMUM,
           TOO_MANY, WRAP, CHAIN, TARGET_CHANGE, NO_CONTEXT, RESET_CONTEXT,
           LUT_CONTEXT, REJECT_CONTEXT, FAILED_EXEC_CONTEXT, KEY_ECHO, REPLAY,
           SETUP_PAD, DRAW_PAD, TRANSITION, LATE_FIELD, ENDING, SOURCE_ALIAS,
           LOSS, RESET, DATA_CHANGE, BEFORE_COMPLETE, BEFORE_ACK, CANCEL, CASES };
    const InlineRectangle seed[] = {
        {128, 2, 124, 48, 0xff112233}, {266, 2, 104, 48, 0xff223344},
        {436, 2, 153, 48, 0xff334455}, {195, 60, 328, 48, 0xff445566},
    };
    const InlineRectangle original[] = {
        {286, 58, 92, 52, 0}, {284, 56, 104, 56, 0},
        {398, 58, 117, 52, 0}, {396, 56, 129, 56, 0},
        {398, 58, 125, 52, 0}, {396, 56, 129, 56, 0},
        {201, 64, 316, 40, 0}, {201, 64, 316, 40, 0},
        {2, 304, 80, 48, 0}, {660, 304, 58, 48, 0},
    };
    TransferFixture f = { .dev = qdev_new(TYPE_NIKON_TRANSFER) };
    MemoryRegion commands, output[2], alias;
    MemoryRegion *root = get_system_memory();
    uint8_t *data, *pixels[2];

    object_property_add_child(object_get_objects_root(), "transfer-context[*]",
                              OBJECT(f.dev));
    memory_region_init_ram_flags_nomigrate(&commands, OBJECT(f.dev),
        "test.context-command", 0x40000, 0, &error_abort);
    memory_region_add_subregion(root, 0x400000, &commands);
    data = memory_region_get_ram_ptr(&commands);
    for (unsigned i = 0; i < 2; i++) {
        g_autofree char *name = g_strdup_printf("test.context-output%u", i);

        memory_region_init_ram_flags_nomigrate(&output[i], OBJECT(f.dev),
                                               name, GUARDS, 0, &error_abort);
        memory_region_add_subregion(root, i ? 0x1000000 - 4 : 0x800000 - 4,
                                    &output[i]);
        pixels[i] = memory_region_get_ram_ptr(&output[i]);
    }
    memory_region_init_alias(&alias, OBJECT(f.dev), "test.context-alias",
                            &output[0], 4, SLOT);
    memory_region_add_subregion(root, 0x1800000, &alias);
    f.irq = qemu_allocate_irq(irq_level, &f, 0);
    sysbus_connect_irq(SYS_BUS_DEVICE(f.dev), 0, f.irq);
    g_assert_true(sysbus_realize(SYS_BUS_DEVICE(f.dev), &error_abort));
    for (unsigned test = SUCCESS; test < CASES; test++) {
        InlineRectangle rectangles[NIKON_PATH_MAX_BATCH + 1];
        unsigned count = test == ONE ? 1 : test == TWO ? 2 :
                         test == THREE || test == SETUP_PAD ||
                         test == DRAW_PAD || test == CHAIN ? 3 :
                         test == FIVE ? 5 : test == MAXIMUM ? 256 :
                         test == TOO_MANY ? 257 : 10;
        uint32_t first_key = test == WRAP ? 0x10001 : 0x10005;
        uint32_t source = 0x400000;
        unsigned target_index = test == TARGET_CHANGE;
        uint32_t target = target_index ? 0x1000000 : 0x800000;
        unsigned setup_bytes, draw_bytes;
        bool accepted = test <= TARGET_CHANGE || test == REPLAY || test >= LOSS;
        bool complete = accepted && test != LOSS && test != RESET &&
                        test != CANCEL;
        g_autofree uint8_t *before0 = NULL;
        g_autofree uint8_t *before1 = NULL;
        g_autofree uint8_t *expected = NULL;

        g_test_message("rectangle continuation case %u", test);
        device_cold_reset(f.dev);
        memset(data, 0, 0x40000);
        memset(pixels[0], 0xa5, GUARDS);
        memset(pixels[1], 0x5a, GUARDS);
        if (test != NO_CONTEXT) {
            build_inline_rectangles(data, data + 0x20000, seed, 4, 0x800000,
                true, test == WRAP ? 0x1fffb : 0x10001,
                &setup_bytes, &draw_bytes);
            if (test == REJECT_CONTEXT) {
                stl_le_p(data + (2 + 11) * 4, 0x40000000);
            }
            g_assert_cmpint(submit_inline_rectangles(&f, source, setup_bytes,
                                                      draw_bytes, true), ==,
                            test == REJECT_CONTEXT ? MEMTX_ERROR : MEMTX_OK);
            if (test == BEFORE_COMPLETE) {
                g_assert_cmpint(write_register(&f, 0x10d4, source), ==,
                                MEMTX_ERROR);
            } else if (test == FAILED_EXEC_CONTEXT) {
                memory_region_set_readonly(&output[0], true);
            }
            advance();
            memory_region_set_readonly(&output[0], false);
            g_assert_cmpint(f.level, ==, test != REJECT_CONTEXT &&
                                        test != FAILED_EXEC_CONTEXT);
            if (test == BEFORE_ACK) {
                g_assert_cmpint(write_register(&f, 0x10d4, source), ==,
                                MEMTX_ERROR);
            }
            if (f.level) {
                g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
            }
            if (test == RESET_CONTEXT) {
                device_cold_reset(f.dev);
            } else if (test == LUT_CONTEXT) {
                build_lut_program(data, data + 0x20000, 0x410000, 0x1000000);
                g_assert_cmpint(submit_inline_rectangles(&f, source, 224, 1688,
                                                          true),
                                ==, MEMTX_OK);
                advance();
                g_assert_true(f.level);
                g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
            }
        }
        for (unsigned i = 0; i < count; i++) {
            rectangles[i] = original[i % ARRAY_SIZE(original)];
            if (test == COLORED || test == OVERLAP || test == CHAIN) {
                rectangles[i].paint = 0x12345678U * (i + 1);
            }
            if (test == OVERLAP) {
                rectangles[i].x = 280;
                rectangles[i].y = 58;
            }
        }
        build_inline_rectangles(data, data + 0x20000, rectangles, count, target,
                                false, first_key, &setup_bytes, &draw_bytes);
        if (count == 10) {
            g_assert_cmpuint(setup_bytes, ==, 2120);
            g_assert_cmpuint(draw_bytes, ==, 2368);
        }
        switch (test) {
        case KEY_ECHO: stl_le_p(data + 0x20000 + (2 + 54) * 4,
                              first_key + 1); break;
        case SETUP_PAD: stl_le_p(data + setup_bytes - 4, 1); break;
        case DRAW_PAD: stl_le_p(data + 0x20000 + draw_bytes - 4, 1); break;
        case TRANSITION: stl_le_p(data + 0x20000 + 60 * 4, 1); break;
        case LATE_FIELD:
            stl_le_p(data + (53 * (count - 1) + 11) * 4, 0x40000000);
            break;
        case ENDING: stl_le_p(data + 0x20000 + 59 * count * 4, 0); break;
        case SOURCE_ALIAS:
            source = 0x1800000;
            memcpy(pixels[0] + 4, data, setup_bytes);
            break;
        default: break;
        }
        before0 = g_memdup2(pixels[0], GUARDS);
        before1 = g_memdup2(pixels[1], GUARDS);
        expected = g_memdup2(pixels[target_index], GUARDS);
        for (unsigned i = 0; i < count; i++) {
            InlineRectangle *r = &rectangles[i];

            for (unsigned y = r->y; y < r->y + r->height; y++) {
                for (unsigned x = r->x; x < r->x + r->width; x++) {
                    stl_le_p(expected + 4 + 4 * (y * 720 + x), r->paint);
                }
            }
        }
        /* The bound is enforced before any write even for count 257. */
        accepted &= test != TOO_MANY;
        complete &= test != TOO_MANY;
        g_assert_cmpint(submit_inline_rectangles(&f, source, setup_bytes,
                                                 draw_bytes, false), ==,
                        accepted ? MEMTX_OK : MEMTX_ERROR);
        g_assert_false(f.level);
        g_assert_cmpmem(pixels[0], GUARDS, before0, GUARDS);
        g_assert_cmpmem(pixels[1], GUARDS, before1, GUARDS);
        if (test == LOSS) {
            memory_region_set_readonly(&output[0], true);
        } else if (test == RESET) {
            device_cold_reset(f.dev);
        } else if (test == DATA_CHANGE) {
            memset(data, 0xff, 0x40000);
        } else if (test == CANCEL) {
            qdev_unrealize(f.dev);
        }
        advance();
        memory_region_set_readonly(&output[0], false);
        g_assert_cmpint(f.level, ==, complete);
        g_assert_cmpmem(pixels[0], GUARDS,
                        complete && !target_index ? expected : before0, GUARDS);
        g_assert_cmpmem(pixels[1], GUARDS,
                        complete && target_index ? expected : before1, GUARDS);
        if (complete) {
            g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
        }
        if (test == REPLAY || test == LOSS || test == RESET) {
            g_assert_cmpint(submit_inline_rectangles(&f, source, setup_bytes,
                                                      draw_bytes, false), ==,
                            test == REPLAY ? MEMTX_OK : MEMTX_ERROR);
            advance();
            g_assert_cmpint(f.level, ==, test == REPLAY);
            g_assert_cmpmem(pixels[0], GUARDS,
                            complete ? expected : before0, GUARDS);
            if (f.level) {
                g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
            }
        } else if (test == CHAIN) {
            /* Retain clip/format and next key across another odd batch. */
            for (unsigned i = 0; i < 3; i++) {
                InlineRectangle *r = &rectangles[i];

                r->paint ^= 0xffffffff;
                for (unsigned y = r->y; y < r->y + r->height; y++) {
                    for (unsigned x = r->x; x < r->x + r->width; x++) {
                        stl_le_p(expected + 4 + 4 * (y * 720 + x), r->paint);
                    }
                }
            }
            build_inline_rectangles(data, data + 0x20000, rectangles, 3, target,
                                    false, first_key + count,
                                    &setup_bytes, &draw_bytes);
            g_assert_cmpint(submit_inline_rectangles(&f, source, setup_bytes,
                                                      draw_bytes, false), ==,
                            MEMTX_OK);
            advance();
            g_assert_true(f.level);
            g_assert_cmpmem(pixels[0], GUARDS, expected, GUARDS);
            g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
        }
    }
    memory_region_del_subregion(root, &alias);
    for (unsigned i = 0; i < 2; i++) {
        memory_region_del_subregion(root, &output[i]);
    }
    memory_region_del_subregion(root, &commands);
    drain_call_rcu();
    object_unparent(OBJECT(f.dev));
    object_unref(OBJECT(f.dev));
    drain_call_rcu();
    qemu_free_irq(f.irq);
}

static uint32_t rgba_over_reference(uint32_t paint, uint32_t destination,
                                    unsigned coverage)
{
    uint64_t source = (paint >> 24) * coverage;
    uint64_t backdrop = (destination >> 24) * (65025 - source);
    uint64_t alpha = source * 255 + backdrop;
    uint32_t result;

    if (!source) {
        return destination;
    }
    result = ((2 * alpha + 65025) / 130050) << 24;
    for (unsigned c = 0; c < 3; c++) {
        uint64_t numerator = ((paint >> (8 * c)) & 255) * source * 255 +
                             ((destination >> (8 * c)) & 255) * backdrop;

        result |= ((2 * numerator + alpha) / (2 * alpha)) << (8 * c);
    }
    return result;
}

static void test_rgba_over_target(uint32_t target)
{
    enum { SLOT = 0x4b0000, EXTENT = 720 * 480 * 4 };
    enum { SUCCESS, OPAQUE_DEST, PARTIAL_DEST, COLORED, ALPHA_GRID,
           ZERO_SOURCE, OPAQUE_SOURCE, FRACTIONAL, CLIPPED, OFFSCREEN,
           REPEAT, CONTINUE, PRIOR_CONTEXT, FULLSCREEN, NAN_COORD, INF_COORD,
           BOUND, WIDTH_ZERO,
           HEIGHT_NEGATIVE, CLOSURE, SEGMENT, MATRIX, BLEND, FORMAT,
           GEOMETRY, CLIP, COORD_ALIGN, COORD_RANGE, RAW_OVERLAP,
           COORD_ALIAS, COMMAND_ALIAS, READONLY, TARGET_ALIGN, TARGET_OVERFLOW,
           TARGET_RANGE, TARGET_SHORT, FOREIGN_CONTEXT, DATA_CHANGE, MAP_LOSS,
           PATH_CHANGE, RESET, CANCEL, CASES };
    static const unsigned alphas[] = { 0, 1, 51, 127, 128, 204, 254, 255 };
    TransferFixture f = { .dev = qdev_new(TYPE_NIKON_TRANSFER) };
    MemoryRegion commands, surface, alias, command_alias;
    MemoryRegion *root = get_system_memory();
    uint8_t *data, *pixels;
    unsigned prefix = target ? 4 : 0;
    unsigned guards = target ? SLOT + 8 : EXTENT + 8;

    object_property_add_child(object_get_objects_root(), "transfer-over[*]",
                              OBJECT(f.dev));
    memory_region_init_ram_flags_nomigrate(&commands, OBJECT(f.dev),
        "test.over-command", 0x20000, 0, &error_abort);
    memory_region_add_subregion(root, 0x400000, &commands);
    memory_region_init_ram_flags_nomigrate(&surface, OBJECT(f.dev),
        "test.over-surface", guards, 0, &error_abort);
    memory_region_add_subregion(root, target - prefix, &surface);
    memory_region_init_alias(&alias, OBJECT(f.dev), "test.over-alias",
                            &surface, prefix + 0x10000, 32);
    memory_region_add_subregion(root, 0x500000, &alias);
    memory_region_init_alias(&command_alias, OBJECT(f.dev),
                            "test.over-command-alias", &surface,
                            prefix + 0x20000, 0x20000);
    data = memory_region_get_ram_ptr(&commands);
    pixels = memory_region_get_ram_ptr(&surface);
    f.irq = qemu_allocate_irq(irq_level, &f, 0);
    sysbus_connect_irq(SYS_BUS_DEVICE(f.dev), 0, f.irq);
    g_assert_true(sysbus_realize(SYS_BUS_DEVICE(f.dev), &error_abort));
    for (unsigned test = SUCCESS; test < CASES; test++) {
        unsigned phases = test == ALPHA_GRID ? ARRAY_SIZE(alphas) : 1;

        for (unsigned phase = 0; phase < phases; phase++) {
            float rectangle[] = { 91, 158, 537, 164, -537 };
            uint32_t paint = 0xcc000000;
            bool complete = test <= FULLSCREEN || test == FOREIGN_CONTEXT ||
                            test == DATA_CHANGE;
            bool accepted = complete || test >= MAP_LOSS;
            g_autofree uint8_t *before = NULL;
            g_autofree uint8_t *expected = NULL;

            g_test_message("RGBA target %08x case %u/%u", target, test, phase);
            device_cold_reset(f.dev);
            memset(data, 0, 0x20000);
            if (test == PRIOR_CONTEXT) {
                build_rgba_program(data, data + 0x1000,
                                   0x402100, 0x402000, true);
                stl_le_p(data + 0x1000 + 33 * 4, target ? target - 4 : 4);
                memcpy(data + 0x2000, (uint8_t[]) { 2, 7, 9, 7, 0 }, 5);
                path_floats(data + 0x2100,
                            (float[]) { 0, 0, 720, 480, -720 }, 5);
                g_assert_cmpint(submit_rgba(&f, 176, 368), ==, MEMTX_OK);
                advance();
                g_assert_true(f.level);
                g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
            }
            memset(pixels, 0xa5, guards);
            memset(pixels + prefix, 0, EXTENT);
            if (test == COLORED || test == FRACTIONAL || test == CLIPPED) {
                paint = 0x80c08040;
            } else if (test == ALPHA_GRID) {
                paint = alphas[phase] << 24 | 0x00e020f0;
            } else if (test == ZERO_SOURCE) {
                paint = 0x00abcdef;
            } else if (test == OPAQUE_SOURCE) {
                paint = 0xff123456;
            }
            if (test == FRACTIONAL) {
                memcpy(rectangle, (float[]) { 10.5, 20, 2, 3, -2 }, 20);
            } else if (test == CLIPPED) {
                memcpy(rectangle, (float[]) { -2, 478, 5, 4, -5 }, 20);
            } else if (test == OFFSCREEN) {
                rectangle[0] = 800;
            } else if (test == FULLSCREEN) {
                memcpy(rectangle, (float[]) { 0, 0, 720, 480, -720 }, 20);
                paint = 0xff000000;
            }
            for (unsigned p = 0; p < EXTENT / 4; p++) {
                uint32_t background = 0;

                if (test == OPAQUE_DEST) {
                    background = 0xffffffff;
                } else if (test == PARTIAL_DEST) {
                    background = 0x800000ff;
                } else if (test == ALPHA_GRID || test == COLORED ||
                           test == FRACTIONAL || test == CLIPPED ||
                           test == ZERO_SOURCE || test == OPAQUE_SOURCE) {
                    background = alphas[p % ARRAY_SIZE(alphas)] << 24 |
                                 (0x00e02040 ^ (p * 0x010305 & 0xffffff));
                }
                stl_le_p(pixels + prefix + p * 4, background);
            }
            build_rgba_program(data, data + 0x1000, 0x402100, 0x402000, true);
            memcpy(data + 0x2000, (uint8_t[]) { 2, 7, 9, 7, 0 }, 5);
            path_floats(data + 0x2100, rectangle, 5);
            stl_le_p(data + 0x1000 + 76 * 4, paint);
            stl_le_p(data + 0x1000 + 33 * 4, target);
            if (test == FULLSCREEN) {
                stl_le_p(data + 0x1000 + 25 * 4, 8);
            }
            switch (test) {
            case NAN_COORD: stl_le_p(data + 0x2100, 0x7fc00000); break;
            case INF_COORD: stl_le_p(data + 0x2104, 0x7f800000); break;
            case BOUND: path_floats(data + 0x2100, (float[]) { 4097 }, 1); break;
            case WIDTH_ZERO:
                path_floats(data + 0x2108, (float[]) { 0, 164, 0 }, 3); break;
            case HEIGHT_NEGATIVE:
                path_floats(data + 0x210c, (float[]) { -164 }, 1); break;
            case CLOSURE: path_floats(data + 0x2110, (float[]) { -536 }, 1); break;
            case SEGMENT: data[0x2001] = 5; break;
            case MATRIX: stl_le_p(data + 12 * 4, 0x40000000); break;
            case BLEND: stl_le_p(data + 0x1000 + 23 * 4, 0x20000); break;
            case FORMAT: stl_le_p(data + 0x1000 + 9 * 4, 1); break;
            case GEOMETRY: stl_le_p(data + 0x1000 + 11 * 4, 0x01df02d0); break;
            case CLIP: stl_le_p(data + 0x1000 + 45 * 4, 0x02ce0000); break;
            case COORD_ALIGN:
                memmove(data + 0x2101, data + 0x2100, 20);
                stl_le_p(data + 22 * 4, 0x402101); break;
            case COORD_RANGE: stl_le_p(data + 22 * 4, 0x600000); break;
            case RAW_OVERLAP: stl_le_p(data + 22 * 4, target); break;
            case COORD_ALIAS:
                memcpy(pixels + prefix + 0x10000, data + 0x2100, 20);
                stl_le_p(data + 22 * 4, 0x500000); break;
            case COMMAND_ALIAS:
                memcpy(pixels + prefix + 0x20000, data, 0x20000);
                memory_region_del_subregion(root, &commands);
                memory_region_add_subregion(root, 0x400000, &command_alias);
                break;
            case READONLY: memory_region_set_readonly(&surface, true); break;
            case TARGET_ALIGN:
                stl_le_p(data + 0x1000 + 33 * 4, target + 1); break;
            case TARGET_OVERFLOW:
                stl_le_p(data + 0x1000 + 33 * 4, UINT32_MAX - EXTENT + 5);
                break;
            case TARGET_RANGE:
                stl_le_p(data + 0x1000 + 33 * 4, 0x700000); break;
            case TARGET_SHORT:
                stl_le_p(data + 0x1000 + 33 * 4, target + SLOT - EXTENT + 8);
                break;
            default: break;
            }
            before = g_memdup2(pixels, guards);
            expected = g_memdup2(pixels, guards);
            for (unsigned y = 0; y < 480; y++) {
                for (unsigned x = 0; x < 720; x++) {
                    unsigned coverage = test == FRACTIONAL ?
                        (y >= 20 && y < 23 ? x == 11 ? 255 :
                         x == 10 || x == 12 ? 128 : 0 : 0) :
                        x >= rectangle[0] && x < rectangle[0] + rectangle[2] &&
                        y >= rectangle[1] && y < rectangle[1] + rectangle[3] ?
                        255 : 0;
                    uint8_t *p = expected + prefix + (y * 720 + x) * 4;

                    stl_le_p(p, rgba_over_reference(paint, ldl_le_p(p), coverage));
                    if (test == REPEAT) {
                        stl_le_p(p, rgba_over_reference(paint, ldl_le_p(p),
                                                        coverage));
                    }
                }
            }
            g_assert_cmpint(submit_rgba(&f, 176, 368), ==,
                            accepted ? MEMTX_OK : MEMTX_ERROR);
            g_assert_false(f.level);
            g_assert_cmpmem(pixels, guards, before, guards);
            if (test == COMMAND_ALIAS) {
                memory_region_del_subregion(root, &command_alias);
                memory_region_add_subregion(root, 0x400000, &commands);
            } else if (test == DATA_CHANGE) {
                memset(data, 0xff, 176);
                memset(data + 0x1000, 0xff, 368);
            } else if (test == PATH_CHANGE) {
                data[0x2001] = 5;
            } else if (test == MAP_LOSS) {
                memory_region_set_readonly(&surface, true);
            } else if (test == RESET) {
                device_cold_reset(f.dev);
            } else if (test == CANCEL) {
                qdev_unrealize(f.dev);
            }
            advance();
            memory_region_set_readonly(&surface, false);
            g_assert_cmpint(f.level, ==, complete);
            if (complete) {
                g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
                if (test == REPEAT) {
                    g_assert_cmpint(submit_rgba(&f, 176, 368), ==, MEMTX_OK);
                    advance();
                    g_assert_true(f.level);
                    g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
                }
            }
            g_assert_cmpmem(pixels, guards, complete ? expected : before, guards);
            if (test == CONTINUE || test == PRIOR_CONTEXT ||
                test == FOREIGN_CONTEXT || test == PATH_CHANGE ||
                test == MAP_LOSS || test == RESET) {
                bool retained = test == CONTINUE || test == PRIOR_CONTEXT;

                build_rgba_gray_program(data, data + 0x1000);
                stl_le_p(data + 0x1000 + 13 * 4,
                         test == FOREIGN_CONTEXT ? target + 4 : target);
                path_floats(data + 0x2100,
                            (float[]) { 96, 86, 528, 308, -528 }, 5);
                g_assert_cmpint(submit_rgba(&f, 168, 232), ==,
                                retained ? MEMTX_OK : MEMTX_ERROR);
                advance();
                g_assert_cmpint(f.level, ==, retained);
                if (retained) {
                    for (unsigned y = 86; y < 394; y++) {
                        for (unsigned x = 96; x < 624; x++) {
                            stl_le_p(expected + prefix + (y * 720 + x) * 4,
                                     0xffb4b4b4);
                        }
                    }
                }
                g_assert_cmpmem(pixels, guards, complete ? expected : before,
                                guards);
                if (retained) {
                    g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
                }
            }
        }
    }
    memory_region_del_subregion(root, &alias);
    memory_region_del_subregion(root, &surface);
    memory_region_del_subregion(root, &commands);
    drain_call_rcu();
    object_unparent(OBJECT(f.dev));
    object_unref(OBJECT(f.dev));
    drain_call_rcu();
    qemu_free_irq(f.irq);
}

static void test_rgba_over(void)
{
    test_rgba_over_target(0xc175d600);
}

static void test_rgba_targets(void)
{
    static const uint32_t targets[] = {
        0xbec5f200, 0, 0x800004, 0xffeae800,
    };

    for (unsigned i = 0; i < ARRAY_SIZE(targets); i++) {
        test_rgba_over_target(targets[i]);
    }
}

static void test_rgba_pair_target(uint32_t target)
{
    enum { EXTENT = 1024 * 688 * 4, TOP = 6 * 1024 * 4 };
    enum { SUCCESS, REPEAT, CAPTURED, CONTEXT, ALIGN, OVERFLOW, UNBACKED,
           SHORT, READONLY, PARTIAL_READONLY, TOP_READONLY, RAW_TOP, COORD_ALIAS,
           COMMAND_ALIAS, MAP_LOSS, INPUT_LOSS, PATH_CHANGE, RESET,
           CANCEL, CASES };
    TransferFixture f = { .dev = qdev_new(TYPE_NIKON_TRANSFER) };
    MemoryRegion commands, surface, alias, command_alias, readonly;
    MemoryRegion *root = get_system_memory();
    unsigned prefix = target ? 4 : 0, guards = EXTENT + prefix + 4;
    uint8_t *data, *pixels;

    object_property_add_child(object_get_objects_root(), "transfer-pair[*]",
                              OBJECT(f.dev));
    memory_region_init_ram_flags_nomigrate(&commands, OBJECT(f.dev),
        "test.pair-command", 0x3000, 0, &error_abort);
    memory_region_add_subregion(root, 0x400000, &commands);
    memory_region_init_ram_flags_nomigrate(&surface, OBJECT(f.dev),
        "test.pair-surface", guards, 0, &error_abort);
    memory_region_add_subregion(root, target - prefix, &surface);
    memory_region_init_alias(&alias, OBJECT(f.dev), "test.pair-alias",
                            &surface, prefix + 0x100, 32);
    memory_region_add_subregion(root, 0x500000, &alias);
    memory_region_init_alias(&command_alias, OBJECT(f.dev),
        "test.pair-command-alias", &surface, prefix + 0x1000, 0x3000);
    memory_region_init_alias(&readonly, OBJECT(f.dev), "test.pair-readonly",
                            &surface, prefix + EXTENT - 4, 4);
    memory_region_set_readonly(&readonly, true);
    data = memory_region_get_ram_ptr(&commands);
    pixels = memory_region_get_ram_ptr(&surface);
    f.irq = qemu_allocate_irq(irq_level, &f, 0);
    sysbus_connect_irq(SYS_BUS_DEVICE(f.dev), 0, f.irq);
    g_assert_true(sysbus_realize(SYS_BUS_DEVICE(f.dev), &error_abort));
    for (unsigned test = SUCCESS; test < CASES; test++) {
        bool complete = test <= CONTEXT;
        bool accepted = complete || test >= MAP_LOSS;
        g_autofree uint8_t *before = NULL;
        g_autofree uint8_t *expected = NULL;

        g_test_message("RGBA pair target %08x case %u", target, test);
        device_cold_reset(f.dev);
        memset(data, 0, 0x3000);
        if (test == CONTEXT) {
            build_rgba_program(data, data + 0x1000, 0x402100, 0x402000, true);
            stl_le_p(data + 0x1000 + 33 * 4, target);
            memcpy(data + 0x2000, (uint8_t[]) { 2, 7, 9, 7, 0 }, 5);
            path_floats(data + 0x2100,
                        (float[]) { 0, 0, 720, 480, -720 }, 5);
            g_assert_cmpint(submit_rgba(&f, 176, 368), ==, MEMTX_OK);
            advance();
            g_assert_true(f.level);
            g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
        }
        memset(pixels, 0xa5, guards);
        build_rgba_program(data, data + 0x1000, 0x402100, 0x402000, false);
        stl_le_p(data + 0x1000 + 33 * 4, target);
        memcpy(data + 0x2000, (uint8_t[]) { 2, 7, 9, 7, 0 }, 5);
        path_floats(data + 0x2100,
                    (float[]) { 0, 6, 1024, 682, -1024 }, 5);
        switch (test) {
        case ALIGN: stl_le_p(data + 0x1000 + 33 * 4, target + 1); break;
        case OVERFLOW:
            stl_le_p(data + 0x1000 + 33 * 4, UINT32_MAX - EXTENT + 5);
            break;
        case UNBACKED: stl_le_p(data + 0x1000 + 33 * 4, 0x700000); break;
        case SHORT: stl_le_p(data + 0x1000 + 33 * 4, target + 8); break;
        case READONLY: memory_region_set_readonly(&surface, true); break;
        case PARTIAL_READONLY:
        case TOP_READONLY:
            memory_region_add_subregion_overlap(root, (uint64_t)target +
                (test == TOP_READONLY ? 0x100 : EXTENT - 4), &readonly, 1);
            break;
        case RAW_TOP:
            memcpy(pixels + prefix + 0x100,
                   (uint8_t[]) { 2, 7, 9, 7, 0 }, 5);
            stl_le_p(data + 26 * 4, target + 0x100);
            break;
        case COORD_ALIAS:
            memcpy(pixels + prefix + 0x100, data + 0x2100, 20);
            stl_le_p(data + 22 * 4, 0x500000);
            break;
        case COMMAND_ALIAS:
            memcpy(pixels + prefix + 0x1000, data, 0x3000);
            memory_region_del_subregion(root, &commands);
            memory_region_add_subregion(root, 0x400000, &command_alias);
            break;
        default: break;
        }
        before = g_memdup2(pixels, guards);
        expected = g_memdup2(pixels, guards);
        for (unsigned y = 6; y < 688; y++) {
            for (unsigned x = 0; x < 1024; x++) {
                stl_le_p(expected + prefix + (y * 1024 + x) * 4, 0xff000000);
            }
        }
        g_assert_cmpint(submit_rgba(&f, 232, 416), ==,
                        accepted ? MEMTX_OK : MEMTX_ERROR);
        g_assert_false(f.level);
        g_assert_cmpmem(pixels, guards, before, guards);
        if (test == COMMAND_ALIAS) {
            memory_region_del_subregion(root, &command_alias);
            memory_region_add_subregion(root, 0x400000, &commands);
        } else if (test == PARTIAL_READONLY || test == TOP_READONLY) {
            memory_region_del_subregion(root, &readonly);
        } else if (test == CAPTURED) {
            memset(data, 0xff, 232);
            memset(data + 0x1000, 0xff, 416);
        } else if (test == MAP_LOSS) {
            memory_region_set_readonly(&surface, true);
        } else if (test == INPUT_LOSS) {
            memory_region_del_subregion(root, &commands);
        } else if (test == PATH_CHANGE) {
            data[0x2001] = 5;
        } else if (test == RESET) {
            device_cold_reset(f.dev);
        } else if (test == CANCEL) {
            qdev_unrealize(f.dev);
        }
        advance();
        memory_region_set_readonly(&surface, false);
        if (test == INPUT_LOSS) {
            memory_region_add_subregion(root, 0x400000, &commands);
        }
        g_assert_cmpint(f.level, ==, complete);
        g_assert_cmpmem(pixels, guards, complete ? expected : before, guards);
        if (complete) {
            g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
            if (test == REPEAT) {
                g_assert_cmpint(submit_rgba(&f, 232, 416), ==, MEMTX_OK);
                advance();
                g_assert_true(f.level);
                g_assert_cmpmem(pixels, guards, expected, guards);
                g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
            }
        }
        if (test == SUCCESS || test == CONTEXT || test == MAP_LOSS ||
            test == RESET) {
            build_rgba_gray_program(data, data + 0x1000);
            stl_le_p(data + 0x1000 + 13 * 4, target);
            path_floats(data + 0x2100,
                        (float[]) { 96, 86, 528, 308, -528 }, 5);
            g_assert_cmpint(submit_rgba(&f, 168, 232), ==, MEMTX_ERROR);
            advance();
            g_assert_false(f.level);
            g_assert_cmpmem(pixels, guards, complete ? expected : before, guards);
        }
    }
    memory_region_del_subregion(root, &alias);
    memory_region_del_subregion(root, &surface);
    memory_region_del_subregion(root, &commands);
    drain_call_rcu();
    object_unparent(OBJECT(f.dev));
    object_unref(OBJECT(f.dev));
    drain_call_rcu();
    qemu_free_irq(f.irq);
}

static void test_rgba_pair_targets(void)
{
    static const uint32_t targets[] = { 0xbec5f200, 0, 0x800004, 0xffd50000 };

    for (unsigned i = 0; i < ARRAY_SIZE(targets); i++) {
        test_rgba_pair_target(targets[i]);
    }
}

static void build_initialized_image(uint8_t *setup, uint8_t *draw,
                                     uint32_t target)
{
    const InlineRectangle rectangle = { 0, 0, 4, 3, 0 };
    uint8_t image_setup[176], image_draw[336];
    unsigned setup_bytes, draw_bytes;

    build_inline_rectangles(setup, draw, &rectangle, 1, target, true, 0x10001,
                            &setup_bytes, &draw_bytes);
    build_image_program(image_setup, image_draw);
    /* Initialization and identity are followed by the image's own matrix. */
    memcpy(setup + 12 * 4, image_setup, sizeof(image_setup));
    memcpy(draw + 35 * 4, image_draw, sizeof(image_draw) - 4);
    stl_le_p(draw + (35 + 59) * 4, target);
}

static void test_image_prefixes(void)
{
    enum { SLOT = 0x4b0000, EXTENT = 720 * 480 * 4, GUARDS = SLOT + 8 };
    enum { SUCCESS, RAW_GRID, REPEAT, IMMUTABLE, CLEAR_CONTEXT, COLD, FOREIGN,
           RESET_CONTEXT, FAILED_CONTEXT, MATRIX_HEADER, COLOR_HEADER,
           COLOR_ENABLE, COLOR_BITS, ACTIVE_MATRIX, SAMPLE, MODE, TEMP_MODE,
           KEY, EARLY_LATCH, SETUP_SHORT, DRAW_SHORT, PADDING, SOURCE_PREFIX,
           MAP_LOSS, INPUT_LOSS, RESET, CANCEL, CASES };
    static const uint32_t raw[] = {
        0x7fc00001, 0x7f800000, 0xffffffff, 0x01a61004,
    };
    TransferFixture f = { .dev = qdev_new(TYPE_NIKON_TRANSFER) };
    MemoryRegion commands, surface;
    MemoryRegion *root = get_system_memory();
    uint8_t *data, *pixels;

    object_property_add_child(object_get_objects_root(), "transfer-prefix[*]",
                              OBJECT(f.dev));
    memory_region_init_ram_flags_nomigrate(&commands, OBJECT(f.dev),
        "test.prefix-command", 0x60000, 0, &error_abort);
    memory_region_add_subregion(root, 0x400000, &commands);
    memory_region_init_ram_flags_nomigrate(&surface, OBJECT(f.dev),
        "test.prefix-surface", GUARDS, 0, &error_abort);
    memory_region_add_subregion(root, 0x800000, &surface);
    data = memory_region_get_ram_ptr(&commands);
    pixels = memory_region_get_ram_ptr(&surface);
    f.irq = qemu_allocate_irq(irq_level, &f, 0);
    sysbus_connect_irq(SYS_BUS_DEVICE(f.dev), 0, f.irq);
    g_assert_true(sysbus_realize(SYS_BUS_DEVICE(f.dev), &error_abort));
    for (unsigned test = SUCCESS; test < CASES; test++) {
        unsigned phases = test == SUCCESS ? 16 : test == RAW_GRID ? 36 : 1;

        for (unsigned phase = 0; phase < phases; phase++) {
            unsigned prefixes = test == SUCCESS ? (3 + phase / 4) % 4 : 3;
            unsigned sp = prefixes & 1 ? 10 : 0;
            unsigned dp = prefixes & 2 ? 2 : 0;
            unsigned setup_bytes = 176 + sp * 4, draw_bytes = 336 + dp * 4;
            bool complete = test <= CLEAR_CONTEXT;
            bool accepted = complete || test >= MAP_LOSS;
            uint8_t *a = data + sp * 4;
            uint8_t *b = data + 0x20000 + dp * 4;
            g_autofree uint8_t *before = NULL;
            g_autofree uint8_t *expected = NULL;

            g_test_message("image prefix case%u phase%u", test, phase);
            device_cold_reset(f.dev);
            memset(data, 0, 0x60000);
            memset(pixels, 0xa5, GUARDS);
            if (test != COLD) {
                if (test == CLEAR_CONTEXT) {
                    const InlineRectangle clear = { 0, 0, 720, 480, 0 };
                    unsigned as, bs;

                    build_inline_rectangles(data, data + 0x20000, &clear, 1,
                        0x800004, true, 0x10001, &as, &bs);
                    g_assert_cmpint(submit_inline_rectangles(&f, 0x400000,
                        as, bs, true), ==, MEMTX_OK);
                } else {
                    build_initialized_image(data, data + 0x20000,
                        test == FOREIGN ? 0x800008 : 0x800004);
                    g_assert_cmpint(submit_inline_rectangles(&f, 0x400000,
                        224, 472, true), ==, MEMTX_OK);
                }
                advance();
                g_assert_true(f.level);
                g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
                if (test == RESET_CONTEXT) {
                    device_cold_reset(f.dev);
                } else if (test == FAILED_CONTEXT) {
                    stl_le_p(data + 0x20000 + (35 + 49) * 4, 15);
                    g_assert_cmpint(submit_inline_rectangles(&f, 0x400000,
                        224, 472, true), ==, MEMTX_ERROR);
                }
            }
            memset(data, 0, 0x60000);
            memset(pixels, 0xa5, GUARDS);
            for (unsigned i = 0; i < EXTENT / 4; i++) {
                stl_le_p(pixels + 4 + i * 4, 0x80402010);
            }
            build_image_program(a, b);
            stl_le_p(b + 59 * 4, 0x800004);
            stl_le_p(b + 45 * 4, 0xff000000);
            stl_le_p(b + 29 * 4, 8 | (2 * (phase % 4)));
            if (sp) {
                stl_le_p(data, 0x0041f046);
                memset(data + 4, test == RAW_GRID ? 0 : 0xff, 36);
            }
            if (test == RAW_GRID) {
                stl_le_p(data + (phase / 4 + 1) * 4, raw[phase % 4]);
            }
            if (dp) {
                stl_le_p(data + 0x20000, 0x01848004);
            }
            for (unsigned i = 0; i < 32; i++) {
                data[0x3000 + i] = (i * 67) & 255;
            }
            switch (test) {
            case MATRIX_HEADER: stl_le_p(data, 0x0041f03e); break;
            case COLOR_HEADER: stl_le_p(data + 0x20000, 0x01844004); break;
            case COLOR_ENABLE: stl_le_p(data + 0x20000 + 4, 0x01000000); break;
            case COLOR_BITS: stl_le_p(data + 0x20000 + 4, 1); break;
            case ACTIVE_MATRIX: stl_le_p(a + 4, 0x7fc00000); break;
            case SAMPLE: stl_le_p(b + 4, 0x01000000); break;
            case MODE: stl_le_p(b + 49 * 4, 15); break;
            case TEMP_MODE: stl_le_p(b + 29 * 4, 9); break;
            case KEY: stl_le_p(b + 78 * 4, 0x10002); break;
            case EARLY_LATCH: stl_le_p(a, 0x0006f004); break;
            case SETUP_SHORT: setup_bytes -= 8; break;
            case DRAW_SHORT: draw_bytes -= 8; break;
            case PADDING: stl_le_p(b + 83 * 4, 1); break;
            case SOURCE_PREFIX: stl_le_p(b + 25 * 4, 0x400000); break;
            default: break;
            }
            before = g_memdup2(pixels, GUARDS);
            expected = g_memdup2(pixels, GUARDS);
            for (unsigned pass = 0; pass < (test == REPEAT ? 2 : 1); pass++) {
                for (unsigned y = 20; y < 23; y++) {
                    for (unsigned x = 10; x < 14; x++) {
                        uint8_t *p = expected + 4 + 4 * (y * 720 + x);
                        unsigned alpha = data[0x3000 + 8 * (y - 20) + x - 10];

                        stl_le_p(p, rgba_over_reference(0xff000000,
                                                       ldl_le_p(p), alpha));
                    }
                }
            }
            g_assert_cmpint(submit_inline_rectangles(&f, 0x400000,
                setup_bytes, draw_bytes, false), ==,
                accepted ? MEMTX_OK : MEMTX_ERROR);
            g_assert_false(f.level);
            g_assert_cmpmem(pixels, GUARDS, before, GUARDS);
            if (test == IMMUTABLE) {
                memset(data, 0xff, setup_bytes);
                memset(data + 0x20000, 0xff, draw_bytes);
            } else if (test == MAP_LOSS) {
                memory_region_set_readonly(&surface, true);
            } else if (test == INPUT_LOSS) {
                memory_region_del_subregion(root, &commands);
            } else if (test == RESET) {
                device_cold_reset(f.dev);
            } else if (test == CANCEL) {
                qdev_unrealize(f.dev);
            }
            advance();
            memory_region_set_readonly(&surface, false);
            if (test == INPUT_LOSS) {
                memory_region_add_subregion(root, 0x400000, &commands);
            }
            g_assert_cmpint(f.level, ==, complete);
            if (complete) {
                g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
                if (test == REPEAT) {
                    g_assert_cmpint(submit_inline_rectangles(&f, 0x400000,
                        setup_bytes, draw_bytes, false), ==, MEMTX_OK);
                    advance();
                    g_assert_true(f.level);
                    g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
                }
            }
            g_assert_cmpmem(pixels, GUARDS, complete ? expected : before, GUARDS);
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

static void test_initialized_image(void)
{
    enum { SLOT = 0x4b0000, EXTENT = 720 * 480 * 4, GUARDS = SLOT + 8 };
    enum { SUCCESS, CAPTURED, ALTERNATE, ZERO_TARGET, REPEAT, IMAGE_CHANGE,
           PRE_MODE1, PRE_MODE2, PRE_MODE3, DATA_CHANGE,
           COLD_RETAINED, COLD_ZERO, FOREIGN_RETAINED,
           RESET_RETAINED, FAILED_RETAINED,
           FOREIGN_PATH, SHORT_SETUP, SHORT_DRAW, INIT_MODE, INIT_HEADER,
           INIT_CLIP, INIT_FORMAT, MATRIX, SAMPLE, FORMAT, MODE, PRE_MODE, KEY,
           TARGET_ALIGN, TARGET_RANGE, TARGET_END, SOURCE_RANGE, SOURCE_OVERLAP,
           SOURCE_ALIAS, COMMAND_ALIAS, READONLY, MAP_LOSS, INPUT_LOSS,
           RESET, CANCEL, CASES };
    TransferFixture f = { .dev = qdev_new(TYPE_NIKON_TRANSFER) };
    MemoryRegion commands, surface, alias, zero, input_alias, high;
    MemoryRegion *root = get_system_memory();
    uint8_t *data, *pixels;

    object_property_add_child(object_get_objects_root(), "transfer-init-image[*]",
                              OBJECT(f.dev));
    memory_region_init_ram_flags_nomigrate(&commands, OBJECT(f.dev),
        "test.init-image-command", 0x20000, 0, &error_abort);
    memory_region_add_subregion(root, 0x400000, &commands);
    memory_region_init_ram_flags_nomigrate(&surface, OBJECT(f.dev),
        "test.init-image-surface", GUARDS, 0, &error_abort);
    memory_region_add_subregion(root, 0xbec5f200 - 4, &surface);
    memory_region_init_alias(&alias, OBJECT(f.dev), "test.init-image-alias",
                            &surface, 4, EXTENT);
    memory_region_add_subregion(root, 0x800000, &alias);
    memory_region_init_alias(&zero, OBJECT(f.dev), "test.init-image-zero",
                            &surface, 4, EXTENT);
    memory_region_add_subregion(root, 0, &zero);
    memory_region_init_alias(&input_alias, OBJECT(f.dev),
                            "test.init-image-input-alias", &surface,
                            4 + 0x20000, 0x20000);
    memory_region_init_alias(&high, OBJECT(f.dev), "test.init-image-high",
                            &surface, 0, EXTENT + 8);
    memory_region_add_subregion(root, 0xfff00000, &high);
    data = memory_region_get_ram_ptr(&commands);
    pixels = memory_region_get_ram_ptr(&surface);
    f.irq = qemu_allocate_irq(irq_level, &f, 0);
    sysbus_connect_irq(SYS_BUS_DEVICE(f.dev), 0, f.irq);
    g_assert_true(sysbus_realize(SYS_BUS_DEVICE(f.dev), &error_abort));
    for (unsigned test = SUCCESS; test < CASES; test++) {
        unsigned setup_bytes = 224, draw_bytes = 472;
        unsigned width = test == CAPTURED ? 64 : 8;
        unsigned height = test == CAPTURED ? 40 : 4;
        uint32_t target = test == ALTERNATE ? 0x800000 :
                          test == ZERO_TARGET || test == COLD_ZERO ?
                          0 : 0xbec5f200;
        bool complete = test <= DATA_CHANGE || test == FOREIGN_PATH;
        bool accepted = complete || test >= MAP_LOSS;
        g_autofree uint8_t *before = NULL;
        g_autofree uint8_t *expected = NULL;
        uint8_t *a = data + 12 * 4, *b = data + 0x1000 + 35 * 4;

        g_test_message("initialized image case %u", test);
        device_cold_reset(f.dev);
        memset(data, 0, 0x20000);
        memset(pixels, 0xa5, GUARDS);
        for (unsigned i = 0; i < EXTENT / 4; i++) {
            stl_le_p(pixels + 4 + i * 4,
                     (i % 3) * 127U << 24 | (i * 0x010305 & 0xffffff));
        }
        build_initialized_image(data, data + 0x1000, target);
        for (unsigned i = 0; i < width * height; i++) {
            data[0x3000 + i] = (i * 37 + i / width * 13) & 255;
        }
        if (test == CAPTURED) {
            path_floats(a + 4,
                        (float[]) { 1, 0, 0, 0, -1, 0, 283, 478, 1 }, 9);
            stl_le_p(a + 38 * 4, 50);
            stl_le_p(a + 39 * 4, 36);
            stl_le_p(a + 40 * 4, -50);
            stl_le_p(b + 1 * 4, 0x390000);
            stl_le_p(b + 3 * 4, 0xc11b00);
            stl_le_p(b + 5 * 4, 0xb9999a);
            stl_le_p(b + 6 * 4, 0x427e66);
            stl_le_p(b + 11 * 4, 0x400028);
            stl_le_p(b + 15 * 4, 0x400028);
            stl_le_p(b + 45 * 4, 0xff000000);
        }
        if (test == COLD_RETAINED || test == COLD_ZERO ||
            test == FOREIGN_RETAINED ||
            test == RESET_RETAINED || test == FAILED_RETAINED) {
            if (test != COLD_RETAINED && test != COLD_ZERO) {
                stl_le_p(b + 59 * 4, test == FOREIGN_RETAINED ? 0x800000 : target);
                g_assert_cmpint(submit_rgba(&f, 224, 472), ==, MEMTX_OK);
                advance();
                g_assert_true(f.level);
                g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
                if (test == RESET_RETAINED) {
                    device_cold_reset(f.dev);
                } else if (test == FAILED_RETAINED) {
                    stl_le_p(b + 49 * 4, 0xdead);
                    g_assert_cmpint(submit_rgba(&f, 224, 472), ==, MEMTX_ERROR);
                }
            }
            build_image_program(data, data + 0x1000);
            stl_le_p(data + 0x1000 + 59 * 4, target);
            setup_bytes = 176;
            draw_bytes = 336;
        }
        switch (test) {
        case PRE_MODE1: case PRE_MODE2: case PRE_MODE3:
            stl_le_p(b + 29 * 4, 8 | (2 * (test - PRE_MODE1 + 1))); break;
        case SHORT_SETUP: setup_bytes -= 8; break;
        case SHORT_DRAW: draw_bytes -= 8; break;
        case INIT_MODE: stl_le_p(data + 4, 0x81); break;
        case INIT_HEADER: stl_le_p(data + 2 * 4, 0x0041f03e); break;
        case INIT_CLIP: stl_le_p(data + 0x1000 + 21 * 4, 718U << 16); break;
        case INIT_FORMAT: stl_le_p(data + 0x1000 + 9 * 4, 1); break;
        case MATRIX: stl_le_p(a + 4, 0x7fc00000); break;
        case SAMPLE: stl_le_p(b + 1 * 4, 0x01000000); break;
        case FORMAT: stl_le_p(b + 13 * 4, 0x001b0019); break;
        case MODE: stl_le_p(b + 49 * 4, 15); break;
        case PRE_MODE: stl_le_p(b + 29 * 4, 9); break;
        case KEY: stl_le_p(b + 78 * 4, 0x10011); break;
        case TARGET_ALIGN: stl_le_p(b + 59 * 4, target + 1); break;
        case TARGET_RANGE: stl_le_p(b + 59 * 4, 0x900000); break;
        case TARGET_END: stl_le_p(b + 59 * 4, 0xfff00004); break;
        case SOURCE_RANGE: stl_le_p(b + 25 * 4, 0x700000); break;
        case SOURCE_OVERLAP: stl_le_p(b + 25 * 4, target); break;
        case SOURCE_ALIAS:
            stl_le_p(b + 25 * 4, 0x800000); break;
        case COMMAND_ALIAS:
            memcpy(pixels + 4 + 0x20000, data, 0x20000);
            memory_region_del_subregion(root, &commands);
            memory_region_add_subregion(root, 0x400000, &input_alias); break;
        case READONLY: memory_region_set_readonly(&surface, true); break;
        default: break;
        }
        before = g_memdup2(pixels, GUARDS);
        expected = g_memdup2(pixels, GUARDS);
        for (unsigned y = 0; y < 480; y++) {
            for (unsigned x = 0; x < 720; x++) {
                bool covered = test == CAPTURED ?
                    x >= 283 && x < 333 && y >= 442 && y < 478 :
                    x >= 10 && x < 14 && y >= 20 && y < 23;
                unsigned sx = test == CAPTURED ? x - 283 : x - 10;
                unsigned sy = test == CAPTURED ? 477 - y : y - 20;

                if (covered && test != IMAGE_CHANGE) {
                    uint8_t *p = expected + 4 + (y * 720 + x) * 4;
                    unsigned alpha = data[0x3000 + sy * width + sx];
                    uint32_t paint = test == CAPTURED ? 0xff000000 : 0xff4848f0;
                    uint32_t associated = alpha << 24;

                    for (unsigned c = 0; c < 3; c++) {
                        associated |= ((((paint >> (8 * c)) & 255) * alpha +
                                         127) / 255) << (8 * c);
                    }
                    /* Normalize the separately rounded premultiplied overlay. */
                    for (unsigned pass = 0;
                         pass < (test == REPEAT || test == ZERO_TARGET ? 2 : 1);
                         pass++) {
                        uint64_t weight = alpha * 255 + p[3] * (255 - alpha);

                        if (!alpha) {
                            continue;
                        }
                        for (unsigned c = 0; c < 3; c++) {
                            uint64_t color = ((associated >> (8 * c)) & 255) *
                                65025ULL + p[c] * p[3] * (255 - alpha);

                            p[c] = (2 * color + weight) / (2 * weight);
                        }
                        p[3] = (2 * weight + 255) / 510;
                    }
                }
            }
        }
        g_assert_cmpint(submit_rgba(&f, setup_bytes, draw_bytes), ==,
                        accepted ? MEMTX_OK : MEMTX_ERROR);
        g_assert_false(f.level);
        g_assert_cmpmem(pixels, GUARDS, before, GUARDS);
        if (test == COMMAND_ALIAS) {
            memory_region_del_subregion(root, &input_alias);
            memory_region_add_subregion(root, 0x400000, &commands);
        } else if (test == DATA_CHANGE) {
            memset(data, 0xff, 224);
            memset(data + 0x1000, 0xff, 472);
        } else if (test == IMAGE_CHANGE) {
            memset(data + 0x3000, 0, width * height);
        } else if (test == MAP_LOSS) {
            memory_region_set_readonly(&surface, true);
        } else if (test == INPUT_LOSS) {
            memory_region_del_subregion(root, &commands);
        } else if (test == RESET) {
            device_cold_reset(f.dev);
        } else if (test == CANCEL) {
            qdev_unrealize(f.dev);
        }
        advance();
        memory_region_set_readonly(&surface, false);
        if (test == INPUT_LOSS) {
            memory_region_add_subregion(root, 0x400000, &commands);
        }
        g_assert_cmpint(f.level, ==, complete);
        if (complete) {
            g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
            if (test == REPEAT || test == ZERO_TARGET) {
                build_image_program(data, data + 0x1000);
                stl_le_p(data + 0x1000 + 59 * 4, target);
                stl_le_p(data + 0x1000 + 29 * 4, 14);
                g_assert_cmpint(submit_rgba(&f, 176, 336), ==, MEMTX_OK);
                advance();
                g_assert_true(f.level);
                g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
            } else if (test == FOREIGN_PATH) {
                build_path_program(data);
                g_assert_cmpint(submit_rgba(&f, 328, 448), ==, MEMTX_ERROR);
            }
        }
        g_assert_cmpmem(pixels, GUARDS, complete ? expected : before, GUARDS);
    }
    memory_region_del_subregion(root, &high);
    memory_region_del_subregion(root, &zero);
    memory_region_del_subregion(root, &alias);
    memory_region_del_subregion(root, &surface);
    memory_region_del_subregion(root, &commands);
    drain_call_rcu();
    object_unparent(OBJECT(f.dev));
    object_unref(OBJECT(f.dev));
    drain_call_rcu();
    qemu_free_irq(f.irq);
}

static void test_rgba_image_family(bool normal)
{
    enum { EXTENT = 720 * 480 * 4, GUARDS = EXTENT + 8,
           COMMAND_BYTES = 0x60000, SOURCE = 0x410000 };
    enum {
        INITIAL, INITIAL_ALTERNATE, RETAINED, CAPTURE, WIDE, MAX_AREA, TALL,
        CLAMP, FLIP, MODULATE, A8,
        FRACTIONAL, TEMP10, TEMP12, TEMP14,
        CACHED, COLOR_PREFIX, BOTH_PREFIXES, DATA_CHANGE, COMMAND_CHANGE,
        FORMAT, FLAGS, FINAL_MODE, TEMP_BITS, PAINT_SHAPE,
        DIMENSION, AREA, ZERO_SIZE, SIZE_MISMATCH,
        SOURCE_BOUND, SOURCE_WRAP, COMMAND_OVERLAP, SOURCE_ALIAS,
        COLD, FOREIGN, MAP_LOSS, INPUT_LOSS, RESET, CANCEL, CASES,
    };
    TransferFixture f = { .dev = qdev_new(TYPE_NIKON_TRANSFER) };
    MemoryRegion commands, surface, alias, target_alias;
    MemoryRegion *root = get_system_memory();
    uint8_t *data, *pixels;

    object_property_add_child(object_get_objects_root(), "transfer-rgba-image[*]",
                              OBJECT(f.dev));
    memory_region_init_ram_flags_nomigrate(&commands, OBJECT(f.dev),
        "test.rgba-image-command", COMMAND_BYTES, 0, &error_abort);
    memory_region_add_subregion(root, 0x400000, &commands);
    memory_region_init_ram_flags_nomigrate(&surface, OBJECT(f.dev),
        "test.rgba-image-surface", GUARDS, 0, &error_abort);
    memory_region_add_subregion(root, 0xc175d600 - 4, &surface);
    memory_region_init_alias(&alias, OBJECT(f.dev), "test.rgba-image-alias",
                            &surface, 4, 128);
    memory_region_add_subregion(root, 0x700000, &alias);
    memory_region_init_alias(&target_alias, OBJECT(f.dev),
                            "test.rgba-image-target", &surface, 0, GUARDS);
    memory_region_add_subregion(root, 0x800000, &target_alias);
    data = memory_region_get_ram_ptr(&commands);
    pixels = memory_region_get_ram_ptr(&surface);
    f.irq = qemu_allocate_irq(irq_level, &f, 0);
    sysbus_connect_irq(SYS_BUS_DEVICE(f.dev), 0, f.irq);
    g_assert_true(sysbus_realize(SYS_BUS_DEVICE(f.dev), &error_abort));

    for (unsigned iteration = 0; iteration < CASES + (normal ? 12 : 0);
         iteration++) {
        unsigned extra = normal ? 12 : 0;
        unsigned test = iteration < CANCEL ? iteration :
            iteration < CANCEL + extra ?
            (unsigned[]) { RETAINED, CACHED, COLOR_PREFIX, BOTH_PREFIXES }
                [(iteration - CANCEL) / 3] : CANCEL;
        unsigned phase = iteration >= CANCEL && iteration < CANCEL + extra ?
                         (iteration - CANCEL) % 3 + 1 : 0;
        uint8_t *a = data, *b = data + 0x1000;
        unsigned setup_bytes = 176, draw_bytes = 336;
        unsigned width = 8, height = 4;
        unsigned x0 = 10, y0 = 20, rw = 4, rh = 3;
        uint32_t paint = !normal && test == MODULATE ?
                         0xff4080c0 : 0xffffffff;
        bool a8 = test == A8;
        uint32_t target = test == INITIAL_ALTERNATE ? 0x800004 : 0xc175d600;
        bool complete = test < FORMAT;
        bool accepted = complete || test >= MAP_LOSS;
        g_autofree uint8_t *before = NULL;
        g_autofree uint8_t *expected = NULL;

        g_test_message("%s image case %u phase %u",
                       normal ? "normal" : "modulated", test, phase);
        device_cold_reset(f.dev);
        memset(data, 0, COMMAND_BYTES);
        memset(pixels, 0xa5, GUARDS);
        if (test != COLD) {
            build_rgba_program(data, b, 0x402100, 0x402000, true);
            memcpy(data + 0x2000, (uint8_t[]) { 2, 7, 9, 7, 0 }, 5);
            path_floats(data + 0x2100, (float[]) { 0, 0, 720, 480, -720 }, 5);
            g_assert_cmpint(submit_rgba(&f, 176, 368), ==, MEMTX_OK);
            advance();
            g_assert_true(f.level);
            g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
        }
        if (normal && test != COLD) {
            /* Establish a non-white paint in the preceding image job. */
            build_image_program(a, b);
            stl_le_p(b + 45 * 4, 0xff4080c0);
            g_assert_cmpint(submit_rgba(&f, 176, 336), ==, MEMTX_OK);
            advance();
            g_assert_true(f.level);
            g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
        }
        for (unsigned i = 0; i < EXTENT; i += 4) {
            /* Different destination alpha and channels expose incorrect
             * premultiplication or an A8-only interpretation of RGBA. */
            stl_le_p(pixels + 4 + i, (i & 4) ? 0x80503010 : 0xff706050);
        }
        if (test == INITIAL || test == INITIAL_ALTERNATE) {
            build_initialized_image(a, b, target);
            a += 12 * 4;
            b += 35 * 4;
            setup_bytes = 224;
            draw_bytes = 472;
        } else {
            build_image_program(a, b);
        }
        if (test == CAPTURE) {
            const uint32_t sample[] = {
                0x3b0000, 0, 0xc10000, 0, 0xb61112, 0x3f0000,
                0, 0, 0x3f0000,
            };

            width = normal ? 48 : 16;
            height = normal ? 40 : 480;
            x0 = normal ? 12 : 64;
            y0 = normal ? 10 : 0;
            rw = normal ? 40 : 3;
            rh = normal ? 40 : 480;
            path_floats(a + 4,
                (float[]) { 1, 0, 0, 0, -1, 0,
                            normal ? 12 : 64, normal ? 50 : 480, 1 }, 9);
            stl_le_p(a + 38 * 4, rw);
            stl_le_p(a + 39 * 4, rh);
            stl_le_p(a + 40 * 4, -(uint32_t)rw);
            for (unsigned i = 0; i < 9; i++) {
                stl_le_p(b + (i + 1) * 4, sample[i]);
            }
            if (normal) {
                stl_le_p(b + 1 * 4, 0x395555);
                stl_le_p(b + 3 * 4, 0xbd0000);
                stl_le_p(b + 5 * 4, 0xb9999a);
                stl_le_p(b + 6 * 4, 0x3f4000);
            }
        } else if (test == WIDE) {
            width = 480;
            height = 16;
        } else if (test == MAX_AREA) {
            width = 4096;
            height = 16;
        } else if (test == TALL) {
            width = 16;
            height = 4096;
        } else if (test == DIMENSION) {
            width = 4097;
            height = 1;
        } else if (test == AREA) {
            width = 257;
            height = 256;
        } else if (test == ZERO_SIZE) {
            width = 0;
        }
        stl_le_p(b + 11 * 4, width << 16 | height);
        stl_le_p(b + 15 * 4, width << 16 | height);
        stl_le_p(b + 13 * 4, a8 ? 0x001b0018 : 0x001b0100);
        stl_le_p(b + 25 * 4, SOURCE);
        stl_le_p(b + 45 * 4, paint);
        for (unsigned y = 0; y < height; y++) {
            for (unsigned x = 0; x < width; x++) {
                uint8_t *p = data + (SOURCE - 0x400000) + 4 * (y * width + x);

                if (a8) {
                    data[SOURCE - 0x400000 + y * width + x] =
                        (uint8_t[]) { 0, 64, 128, 255 }[(x + y) % 4];
                    continue;
                }
                p[0] = 17 * x + 3 * y;
                p[1] = 5 * x + 11 * y;
                p[2] = 13 * x + 7 * y;
                p[3] = (uint8_t[]) { 0, 64, 128, 255 }[(x + y) % 4];
            }
        }
        if (test == CLAMP) {
            stl_le_p(b + 3 * 4, 0x3f0000);
            stl_le_p(b + 6 * 4, 0x3f0000);
        } else if (test == FLIP) {
            stl_le_p(b + 5 * 4, 0xbd0000);
            stl_le_p(b + 6 * 4, 0x417000);
        } else if (test == FRACTIONAL) {
            path_floats(a + 7 * 4, (float[]) { 10.5f }, 1);
            rw = 5;
        } else if (test >= TEMP10 && test <= TEMP14) {
            stl_le_p(b + 29 * 4, (normal ? 2 : 10) + 2 * (test - TEMP10));
        } else if (test == FORMAT) {
            stl_le_p(b + 13 * 4, 0x001b0000);
        } else if (test == FLAGS) {
            stl_le_p(b + 13 * 4, 0x011b0100);
        } else if (test == FINAL_MODE) {
            stl_le_p(b + 49 * 4, 4);
        } else if (test == TEMP_BITS) {
            stl_le_p(b + 29 * 4, normal ? 1 : 9);
        } else if (test == PAINT_SHAPE) {
            stl_le_p(b + 44 * 4, 0x0185e004);
        } else if (test == SIZE_MISMATCH) {
            stl_le_p(b + 15 * 4, 0x00080005);
        } else if (test == SOURCE_BOUND) {
            /* A8's 32 bytes fit here; this RGBA plane's 128 bytes do not. */
            stl_le_p(b + 25 * 4, 0x400000 + COMMAND_BYTES - 32);
        } else if (test == SOURCE_WRAP) {
            stl_le_p(b + 25 * 4, 0xfffffff0);
        } else if (test == COMMAND_OVERLAP) {
            stl_le_p(b + 25 * 4, 0x400000);
        } else if (test == SOURCE_ALIAS) {
            stl_le_p(b + 25 * 4, 0x700000);
        } else if (test == FOREIGN) {
            stl_le_p(b + 59 * 4, 0xc175d604);
        }
        if (normal) {
            /* The mode1 producer omits paint entirely; the preceding job's
             * color must not modulate the source. Keep a malformed paint
             * command as a negative grammar case. */
            stl_le_p(b + 29 * 4, test == TEMP_BITS ? 1 :
                test >= TEMP10 && test <= TEMP14 ?
                2 + 2 * (test - TEMP10) : 2 * phase);
            if (test != FINAL_MODE) {
                stl_le_p(b + 49 * 4, 2);
            }
            memmove(b + 44 * 4, b + 46 * 4,
                    (draw_bytes == 472 ? 37 : 38) * 4);
            if (test == PAINT_SHAPE) {
                stl_le_p(b + 44 * 4, 0x0185e004);
            }
            draw_bytes -= 8;
        }
        if (test == CACHED || test == BOTH_PREFIXES) {
            memmove(data + 40, data, 176);
            stl_le_p(data, 0x0041f046);
            path_floats(data + 4,
                (float[]) { 1, 0, 0, 0, 1, 0, 0, 0, 1 }, 9);
            setup_bytes += 40;
        }
        if (test == COLOR_PREFIX || test == BOTH_PREFIXES) {
            memmove(data + 0x1000 + 8, data + 0x1000, draw_bytes);
            stl_le_p(data + 0x1000, 0x01848004);
            stl_le_p(data + 0x1000 + 4, 0);
            draw_bytes += 8;
        }
        before = g_memdup2(pixels, GUARDS);
        expected = g_memdup2(pixels, GUARDS);
        g_assert_cmpint(submit_rgba(&f, setup_bytes, draw_bytes), ==,
                        accepted ? MEMTX_OK : MEMTX_ERROR);
        g_assert_false(f.level);
        g_assert_cmpmem(pixels, GUARDS, before, GUARDS);
        if (test == DATA_CHANGE) {
            stl_le_p(data + (SOURCE - 0x400000), 0xff2040c0);
        } else if (test == COMMAND_CHANGE) {
            memset(data, 0xff, setup_bytes);
            memset(data + 0x1000, 0xff, draw_bytes);
        } else if (test == MAP_LOSS) {
            memory_region_set_readonly(&surface, true);
        } else if (test == INPUT_LOSS) {
            memory_region_del_subregion(root, &commands);
        } else if (test == RESET) {
            device_cold_reset(f.dev);
        } else if (test == CANCEL) {
            qdev_unrealize(f.dev);
        }
        if (complete) {
            /* Independent integer oracle: these matrices select texels using
             * rational coordinates, or the captured exact row reversal. */
            for (unsigned y = y0; y < y0 + rh; y++) {
                for (unsigned x = x0; x < x0 + rw; x++) {
                    unsigned tx = width * (2 * (x - x0) + 1) / 16;
                    unsigned ty = height * (2 * (y - y0) + 1) / 8;
                    uint8_t *p = expected + 4 + 4 * (y * 720 + x);
                    const uint8_t *sample;
                    unsigned alpha, weight;

                    if (test == CAPTURE) {
                        tx = x - x0;
                        ty = normal ? 49 - y : 479 - y;
                    } else if (test == CLAMP) {
                        tx = width - 1;
                        ty = height - 1;
                    } else if (test == FLIP) {
                        ty = height * (2 * (22 - y) + 1) / 8;
                    }
                    sample = data + (SOURCE - 0x400000) +
                             (a8 ? 1 : 4) * (ty * width + tx);
                    alpha = sample[a8 ? 0 : 3];
                    if (test == FRACTIONAL && (x == x0 || x == x0 + rw - 1)) {
                        alpha = (alpha * 128 + 127) / 255;
                    }
                    weight = alpha * 255 + p[3] * (255 - alpha);

                    if (!alpha) {
                        continue;
                    }
                    for (unsigned c = 0; c < 3; c++) {
                        unsigned color = (paint >> (8 * c)) & 255;
                        unsigned associated =
                            ((a8 ? 255 : sample[c]) * color * alpha +
                             32512) / 65025;
                        uint64_t mixed = associated * 65025ULL +
                            p[c] * p[3] * (255 - alpha);

                        p[c] = (2 * mixed + weight) / (2 * weight);
                    }
                    p[3] = (2 * weight + 255) / 510;
                }
            }
            if (test == CAPTURE && normal) {
                g_assert_cmphex((uint32_t)ldl_le_p(expected + 4 +
                    (10 * 720 + 12) * 4), ==, 0xff11ad75);
            } else if (test == CAPTURE) {
                /* Literal non-gray opaque and partial-alpha anchors. */
                g_assert_cmphex((uint32_t)ldl_le_p(expected + 4 + 64 * 4), ==,
                                0xff19959d);
                g_assert_cmphex((uint32_t)ldl_le_p(expected + 4 +
                                                 (720 + 64) * 4), ==,
                                0xff417575);
            }
        }
        advance();
        memory_region_set_readonly(&surface, false);
        if (test == INPUT_LOSS) {
            memory_region_add_subregion(root, 0x400000, &commands);
        }
        g_assert_cmpint(f.level, ==, complete);
        g_assert_cmpmem(pixels, GUARDS, complete ? expected : before, GUARDS);
    }
    memory_region_del_subregion(root, &target_alias);
    memory_region_del_subregion(root, &alias);
    memory_region_del_subregion(root, &surface);
    memory_region_del_subregion(root, &commands);
    drain_call_rcu();
    object_unparent(OBJECT(f.dev));
    object_unref(OBJECT(f.dev));
    drain_call_rcu();
    qemu_free_irq(f.irq);
}

static void test_rgba_image(void)
{
    test_rgba_image_family(false);
}

static void test_normal_image(void)
{
    test_rgba_image_family(true);
}

static void build_stroked_paths(uint8_t *data, unsigned count,
                                uint32_t target, uint32_t key)
{
    g_autofree uint8_t *path = g_malloc0(0x2400);

    build_path_program(path);
    for (unsigned i = 0; i < count; i++) {
        uint8_t *a = data + i * 70 * 4;
        uint8_t *b = data + 0x20000 + i * 86 * 4;
        unsigned at = 0;
        uint32_t k = 0x10000 | ((key - 0x10001 + 2 * i) % 0xfffe + 1);

        memcpy(a, path, 41 * 4);
        memcpy(b, path + 0x1000, 55 * 4);
        stl_le_p(a + 4 * 4, 5);
        path_floats(a + 10 * 4,
                    (float[]) { 2, 0, 0, 0, 3, 0, 4, 6, 1 }, 9);
        stl_le_p(a + 20 * 4, 0x450100);
        stl_le_p(a + 22 * 4, 0xd0000008);
        stl_le_p(a + 24 * 4, 0x450000);
        stl_le_p(a + 26 * 4, 0x10000005);
        stl_le_p(a + 40 * 4, k);
        stl_le_p(b + 13 * 4, target);
        stl_le_p(b + 41 * 4, 0xff0000ff);
        stl_le_p(b + 52 * 4, k);
        a += 41 * 4;
        b += 55 * 4;
        memcpy(a, data + i * 70 * 4 + 19 * 4, 8 * 4);
        at = 8;
        inline_word(a, &at, 0x005af01e);
        inline_word(a, &at, 0); inline_word(a, &at, 0);
        inline_word(a, &at, 0x3f800000); /* Half width in object space. */
        inline_word(a, &at, 0x3fddb3d7); /* sqrt(3): miter limit two. */
        inline_pair(a, &at, 0x00654004, 0);
        inline_pair(a, &at, 0x00653004, 0x2000);
        memcpy(a + at * 4, data + i * 70 * 4 + 29 * 4, 12 * 4);
        stl_le_p(a + 28 * 4, 0x10000 | ((k & 65535) % 0xfffe + 1));
        memcpy(b, data + 0x20000 + i * 86 * 4 + 24 * 4, 31 * 4);
        stl_le_p(b + 17 * 4, 0xff00ff00);
        stl_le_p(b + 21 * 4, ldl_le_p(a + 20 * 4));
        stl_le_p(b + 28 * 4, ldl_le_p(a + 28 * 4));
    }
    stl_le_p(data + 0x20000 + count * 86 * 4, 0x00081004);
    stl_le_p(data + 0x20000 + (count * 86 + 1) * 4, 1);
    memcpy(data + 0x50000, (uint8_t[]) { 2, 4, 4, 4, 0 }, 5);
    path_floats(data + 0x50100,
                (float[]) { 10, 10, 30, 10, 30, 30, 10, 30 }, 8);
}

static void test_stroked_paths(void)
{
    enum { SLOT = 0x4b0000, EXTENT = 720 * 480 * 4, GUARDS = SLOT + 8 };
    enum { SUCCESS, TWO, THREE, LIMIT, WRAP, ZERO_WIDTH, REPEAT, DATA_CHANGE,
           MASKED, MASKED_ALL, MASKED_LIMIT, SIZE_COLLISION, CLEAR_CONTINUE,
           COLD, FOREIGN, TOO_MANY, SHORT_SETUP, SHORT_DRAW, FIRST_STROKE,
           UNKNOWN, DASH, PHASE, NEGATIVE_WIDTH, NAN_WIDTH, INF_EXTENT,
           CAP, JOIN, STYLE_BITS, TYPE, COUNT, MATRIX, KEY, TARGET,
           TARGET_END, SOURCE_RANGE, SOURCE_ALIAS, COMMAND_ALIAS,
           MASK_HEADER, MASK_ENABLED, MASK_BITS, MASK_LOWBITS, MASK_POSITION,
           MASK_LATE_ENABLED, MASK_SHORT, MASK_TAIL,
           CLEAR_FOREIGN, CLEAR_RESET, CLEAR_FAILED, LATE_BAD,
           MAP_LOSS, RESET, CANCEL, CASES };
    TransferFixture f = { .dev = qdev_new(TYPE_NIKON_TRANSFER) };
    MemoryRegion commands, surface, alias, command_alias, high;
    MemoryRegion *root = get_system_memory();
    uint8_t *data, *pixels;

    object_property_add_child(object_get_objects_root(), "transfer-stroke[*]",
                              OBJECT(f.dev));
    memory_region_init_ram_flags_nomigrate(&commands, OBJECT(f.dev),
        "test.stroke-command", 0x60000, 0, &error_abort);
    memory_region_add_subregion(root, 0x400000, &commands);
    memory_region_init_ram_flags_nomigrate(&surface, OBJECT(f.dev),
        "test.stroke-surface", GUARDS, 0, &error_abort);
    memory_region_add_subregion(root, 0xbec5f200 - 4, &surface);
    memory_region_init_alias(&alias, OBJECT(f.dev), "test.stroke-alias",
                            &surface, 4 + 0x10000, 64);
    memory_region_add_subregion(root, 0x500000, &alias);
    memory_region_init_alias(&command_alias, OBJECT(f.dev),
                            "test.stroke-command-alias", &surface,
                            4 + 0x20000, 0x60000);
    memory_region_init_alias(&high, OBJECT(f.dev), "test.stroke-high",
                            &surface, 0, EXTENT + 8);
    memory_region_add_subregion(root, 0xfff00000, &high);
    data = memory_region_get_ram_ptr(&commands);
    pixels = memory_region_get_ram_ptr(&surface);
    f.irq = qemu_allocate_irq(irq_level, &f, 0);
    sysbus_connect_irq(SYS_BUS_DEVICE(f.dev), 0, f.irq);
    g_assert_true(sysbus_realize(SYS_BUS_DEVICE(f.dev), &error_abort));
    for (unsigned test = SUCCESS; test < CASES; test++) {
        unsigned count = test == TWO ? 2 :
                         test == THREE || test == MASKED ||
                         test == MASKED_ALL || test == MASK_LATE_ENABLED ? 3 :
                         test == SIZE_COLLISION ? 5 :
                         test == LIMIT || test == MASKED_LIMIT ? 128 :
                         test == TOO_MANY ? 129 : 1;
        unsigned setup_bytes = count * 70 * 4;
        unsigned draw_bytes = (count * 86 + 2) * 4;
        uint32_t key = test == WRAP ? 0x1fffe : 0x10009;
        bool complete = test <= CLEAR_CONTINUE;
        bool accepted = complete || test >= LATE_BAD;
        g_autofree uint8_t *before = NULL;
        g_autofree uint8_t *expected = NULL;
        uint8_t *stroke;

        g_test_message("stroked path case %u", test);
        device_cold_reset(f.dev);
        memset(data, 0, 0x60000);
        memset(pixels, 0xa5, GUARDS);
        if (test != COLD) {
            if (test == MASKED || test == CLEAR_CONTINUE ||
                (test >= CLEAR_FOREIGN && test <= CLEAR_FAILED)) {
                const InlineRectangle clear = { 0, 0, 720, 480, 0 };
                unsigned setup, draw;

                build_inline_rectangles(data, data + 0x20000, &clear, 1,
                    0xbec5f200, true, 0x10001, &setup, &draw);
                g_assert_cmpint(submit_inline_rectangles(&f, 0x400000,
                    setup, draw, true), ==, MEMTX_OK);
                advance();
                g_assert_true(f.level);
                g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
                if (test == CLEAR_RESET) {
                    device_cold_reset(f.dev);
                } else if (test == CLEAR_FAILED) {
                    stl_le_p(data + 13 * 4, 0x7fc00000);
                    g_assert_cmpint(submit_inline_rectangles(&f, 0x400000,
                        setup, draw, true), ==, MEMTX_ERROR);
                } else if (test == CLEAR_CONTINUE) {
                    build_inline_rectangles(data, data + 0x20000, &clear, 1,
                        0xbec5f200, false, 0x10002, &setup, &draw);
                    g_assert_cmpint(submit_inline_rectangles(&f, 0x400000,
                        setup, draw, false), ==, MEMTX_OK);
                    advance();
                    g_assert_true(f.level);
                    g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
                }
            } else {
                build_initialized_image(data, data + 0x1000, 0xbec5f200);
                g_assert_cmpint(submit_rgba(&f, 224, 472), ==, MEMTX_OK);
                advance();
                g_assert_true(f.level);
                g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
            }
        }
        for (unsigned i = 0; i < EXTENT / 4; i++) {
            stl_le_p(pixels + 4 + 4 * i, 0x80402010);
        }
        memset(data, 0, 0x60000);
        build_stroked_paths(data, count,
                            test == FOREIGN || test == CLEAR_FOREIGN ?
                            0xc175d600 :
                            test == TARGET_END ? 0xfff00004 : 0xbec5f200, key);
        stroke = data + 41 * 4;
        if (test == MASKED || test == MASKED_ALL || test == MASKED_LIMIT ||
            test == SIZE_COLLISION ||
            test == CLEAR_CONTINUE ||
            test == REPEAT || test == DATA_CHANGE || test >= MASK_HEADER) {
            unsigned writes = test == MASKED_ALL || test == MASKED_LIMIT ||
                              test == MASK_LATE_ENABLED ? count : 1;

            for (unsigned i = 0; i < writes; i++) {
                unsigned at = i * 88 + (test == MASK_POSITION ? 26 : 24);
                uint8_t *b = data + 0x20000 + at * 4;

                memmove(b + 8, b, draw_bytes - at * 4);
                stl_le_p(b, test == MASK_HEADER ? 0x01844004 : 0x01848004);
                stl_le_p(b + 4, test == MASK_ENABLED ? 0x01000000 :
                         test == MASK_BITS ? 0x02000000 :
                         test == MASK_LOWBITS ? 1 :
                         test == MASK_LATE_ENABLED && i == count - 1 ?
                         0x01000000 : 0);
                draw_bytes += 8;
            }
            if (test == MASK_SHORT) {
                draw_bytes = 104;
            } else if (test == MASK_TAIL) {
                stl_le_p(data + 0x20000 + draw_bytes - 4, 2);
            }
        }
        switch (test) {
        case ZERO_WIDTH: stl_le_p(stroke + 11 * 4, 0); break;
        case SHORT_SETUP: setup_bytes -= 8; break;
        case SHORT_DRAW: draw_bytes -= 8; break;
        case FIRST_STROKE:
            memmove(data, stroke, 29 * 4);
            memmove(data + 0x20000, data + 0x20000 + 55 * 4, 33 * 4);
            setup_bytes = 120; draw_bytes = 136; break;
        case UNKNOWN: stl_le_p(stroke + 8 * 4, 0x005af022); break;
        case DASH: stl_le_p(stroke + 9 * 4, 1); break;
        case PHASE: stl_le_p(stroke + 10 * 4, 0x3f800000); break;
        case NEGATIVE_WIDTH: stl_le_p(stroke + 11 * 4, 0xbf800000); break;
        case NAN_WIDTH: stl_le_p(stroke + 11 * 4, 0x7fc00000); break;
        case INF_EXTENT: stl_le_p(stroke + 12 * 4, 0x7f800000); break;
        case CAP: stl_le_p(stroke + 14 * 4, 3U << 16); break;
        case JOIN: stl_le_p(stroke + 14 * 4, 3U << 20); break;
        case STYLE_BITS: stl_le_p(stroke + 14 * 4, 1); break;
        case TYPE: stl_le_p(stroke + 3 * 4, 0xc0000008); break;
        case COUNT: stl_le_p(stroke + 7 * 4, 0x10000004); break;
        case MATRIX: stl_le_p(data + 10 * 4, 0x7fc00000); break;
        case KEY: stl_le_p(stroke + 28 * 4, key); break;
        case TARGET: stl_le_p(data + 0x20000 + 13 * 4, 0xbec5f204); break;
        case SOURCE_RANGE: stl_le_p(stroke + 1 * 4, 0x600000); break;
        case SOURCE_ALIAS: stl_le_p(stroke + 1 * 4, 0x500000); break;
        case COMMAND_ALIAS:
            memcpy(pixels + 4 + 0x20000, data, 0x60000);
            memory_region_del_subregion(root, &commands);
            memory_region_add_subregion(root, 0x400000, &command_alias); break;
        default: break;
        }
        before = g_memdup2(pixels, GUARDS);
        expected = g_memdup2(pixels, GUARDS);
        for (unsigned y = 0; y < 480; y++) {
            for (unsigned x = 0; x < 720; x++) {
                bool fill = x >= 24 && x < 64 && y >= 36 && y < 96;
                bool outline = x >= 22 && x < 66 && y >= 33 && y < 99 &&
                    !(x >= 26 && x < 62 && y >= 39 && y < 93);
                uint8_t *p = expected + 4 + 4 * (y * 720 + x);

                if (fill) {
                    stl_le_p(p, 0xff0000ff);
                }
                if (outline && test != ZERO_WIDTH) {
                    stl_le_p(p, 0xff00ff00);
                }
            }
        }
        g_assert_cmpint(submit_inline_rectangles(&f, 0x400000, setup_bytes,
                                                draw_bytes, false), ==,
                        accepted ? MEMTX_OK : MEMTX_ERROR);
        g_assert_false(f.level);
        g_assert_cmpmem(pixels, GUARDS, before, GUARDS);
        if (test == COMMAND_ALIAS) {
            memory_region_del_subregion(root, &command_alias);
            memory_region_add_subregion(root, 0x400000, &commands);
        } else if (test == DATA_CHANGE) {
            memset(data, 0xff, setup_bytes);
            memset(data + 0x20000, 0xff, draw_bytes);
        } else if (test == LATE_BAD) {
            data[0x50004] = 5;
        } else if (test == MAP_LOSS) {
            memory_region_set_readonly(&surface, true);
        } else if (test == RESET) {
            device_cold_reset(f.dev);
        } else if (test == CANCEL) {
            qdev_unrealize(f.dev);
        }
        advance();
        memory_region_set_readonly(&surface, false);
        g_assert_cmpint(f.level, ==, complete);
        if (complete) {
            g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
            if (test == REPEAT) {
                g_assert_cmpint(submit_inline_rectangles(&f, 0x400000,
                    setup_bytes, draw_bytes, false), ==, MEMTX_OK);
                advance();
                g_assert_true(f.level);
                g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
            }
        }
        g_assert_cmpmem(pixels, GUARDS, complete ? expected : before, GUARDS);
    }
    memory_region_del_subregion(root, &high);
    memory_region_del_subregion(root, &alias);
    memory_region_del_subregion(root, &surface);
    memory_region_del_subregion(root, &commands);
    drain_call_rcu();
    object_unparent(OBJECT(f.dev));
    object_unref(OBJECT(f.dev));
    drain_call_rcu();
    qemu_free_irq(f.irq);
}

static void test_stroke_mask(void)
{
    const uint8_t line[] = { 2, 4 }, corner[] = { 2, 4, 4 };
    uint32_t matrix[9], coordinates[8];
    g_autofree uint8_t *mask = g_malloc0(NIKON_PATH_MASK_BYTES);
    g_autofree uint8_t *bevel = g_malloc0(NIKON_PATH_MASK_BYTES);
    unsigned sums[3] = { 0 };
    const char *reason = NULL;
    NikonPathStroke stroke = { 2, 3.4641016f, 0, 0 };
    const union { uint32_t bits; float value; } not_number = { 0x7fc00000 },
                                                infinity = { 0x7f800000 };

    path_floats((uint8_t *)matrix,
                (float[]) { 2, 0, 0, 0, 3, 0, 4, 6, 1 }, 9);
    path_floats((uint8_t *)coordinates,
                (float[]) { 10, 10, 20, 10, 20, 20 }, 6);
    for (unsigned cap = 0; cap < 3; cap++) {
        for (unsigned join = 0; join < 3; join++) {
            stroke.cap = cap;
            stroke.join = join;
            memset(mask, 0, NIKON_PATH_MASK_BYTES);
            g_assert_true(nikon_path_mask(mask, 720, 480, line, 2,
                coordinates, 4, matrix, false, &stroke, &reason));
            for (unsigned y = 0; y < 480; y++) {
                for (unsigned x = 0; x < 720; x++) {
                    unsigned actual = mask[y * 720 + x];
                    bool body = x >= 24 && x < 44 && y >= 30 && y < 42;
                    bool square = x >= 20 && x < 48 && y >= 30 && y < 42;

                    if (cap != 1) {
                        g_assert_cmpuint(actual, ==, (cap == 0 ? body : square) ?
                                         255 : 0);
                    } else if (body || !square) {
                        g_assert_cmpuint(actual, ==, body ? 255 : 0);
                    }
                }
            }
            g_assert_cmpuint(mask[35 * 720 + 22], ==, cap ? 255 : 0);
            g_assert_cmpuint(mask[35 * 720 + 45], ==, cap ? 255 : 0);
            g_assert_cmpuint(mask[30 * 720 + 20], ==, cap == 2 ? 255 : 0);
        }
    }
    stroke.cap = 0;
    for (unsigned join = 0; join < 3; join++) {
        stroke.join = join;
        memset(mask, 0, NIKON_PATH_MASK_BYTES);
        g_assert_true(nikon_path_mask(mask, 720, 480, corner, 3,
            coordinates, 6, matrix, false, &stroke, &reason));
        for (unsigned y = 30; y < 36; y++) {
            for (unsigned x = 44; x < 48; x++) {
                sums[join] += mask[y * 720 + x];
            }
        }
        if (join == 2) {
            memcpy(bevel, mask, NIKON_PATH_MASK_BYTES);
        }
    }
    g_assert_cmpuint(sums[0], ==, 4 * 6 * 255);
    g_assert_cmpuint(sums[0], >, sums[1]);
    g_assert_cmpuint(sums[1], >, sums[2]);
    stroke.join = 0;
    stroke.extent = 0;
    memset(mask, 0, NIKON_PATH_MASK_BYTES);
    g_assert_true(nikon_path_mask(mask, 720, 480, corner, 3,
        coordinates, 6, matrix, false, &stroke, &reason));
    g_assert_cmpmem(mask, NIKON_PATH_MASK_BYTES, bevel, NIKON_PATH_MASK_BYTES);
    stroke.radius = 0;
    memset(mask, 0, NIKON_PATH_MASK_BYTES);
    g_assert_true(nikon_path_mask(mask, 720, 480, line, 2,
        coordinates, 4, matrix, false, &stroke, &reason));
    for (unsigned i = 0; i < NIKON_PATH_MASK_BYTES; i++) {
        g_assert_cmpuint(mask[i], ==, 0);
    }
    stroke.radius = 2;
    path_floats((uint8_t *)coordinates,
                (float[]) { 10, 10, 20, 10, 10, 20, 20, 20 }, 8);
    g_assert_true(nikon_path_mask(mask, 720, 480,
        (uint8_t[]) { 2, 4, 2, 4 }, 4, coordinates, 8, matrix, false,
        &stroke, &reason));
    g_assert_cmpuint(mask[36 * 720 + 34], ==, 255);
    g_assert_cmpuint(mask[66 * 720 + 34], ==, 255);
    g_assert_cmpuint(mask[51 * 720 + 34], ==, 0);
    g_assert_false(nikon_path_mask(NULL, 720, 480, line, 2,
        coordinates, 4, matrix, false, NULL, &reason));
    for (unsigned test = 0; test < 8; test++) {
        stroke = (NikonPathStroke) { 2, 3.4641016f, 0, 0 };
        switch (test) {
        case 0: stroke.radius = -1; break;
        case 1: stroke.radius = not_number.value; break;
        case 2: stroke.radius = 4097; break;
        case 3: stroke.extent = -1; break;
        case 4: stroke.extent = infinity.value; break;
        case 5: stroke.extent = 4097; break;
        case 6: stroke.cap = 3; break;
        case 7: stroke.join = 3; break;
        }
        g_assert_false(nikon_path_mask(NULL, 720, 480, line, 2,
            coordinates, 4, matrix, false, &stroke, &reason));
    }
}

static void test_matrix_overwrite(void)
{
    enum { SLOT = 0x4b0000, EXTENT = 720 * 480 * 4, GUARDS = SLOT + 8 };
    enum { ZERO, AFFINE, PROJECTIVE, NONFINITE, RAW_GRID, COUNTS,
           CONTINUE, REPEAT, IMMUTABLE, FIRST_HEADER, SECOND_HEADER,
           FINAL_TRANSFORM, FINAL_NAN, FINAL_ZERO, EARLY_LATCH, SHORT_SETUP,
           SHORT_DRAW, MAP_LOSS, RESET, CANCEL, CASES };
    static const uint32_t patterns[] = {
        0x7fc00001, 0x7f800000, 0xffffffff, 0x01a61004,
    };

    for (unsigned image = 0; image < 2; image++) {
        TransferFixture f = { .dev = qdev_new(TYPE_NIKON_TRANSFER) };
        MemoryRegion commands, surface;
        MemoryRegion *root = get_system_memory();
        InlineRectangle rectangles[256];
        uint8_t *data, *pixels;

        object_property_add_child(object_get_objects_root(), "transfer-matrix[*]",
                                  OBJECT(f.dev));
        memory_region_init_ram_flags_nomigrate(&commands, OBJECT(f.dev),
            "test.matrix-command", 0x60000, 0, &error_abort);
        memory_region_add_subregion(root, 0x400000, &commands);
        memory_region_init_ram_flags_nomigrate(&surface, OBJECT(f.dev),
            "test.matrix-surface", GUARDS, 0, &error_abort);
        memory_region_add_subregion(root, 0x800000, &surface);
        data = memory_region_get_ram_ptr(&commands);
        pixels = memory_region_get_ram_ptr(&surface);
        f.irq = qemu_allocate_irq(irq_level, &f, 0);
        sysbus_connect_irq(SYS_BUS_DEVICE(f.dev), 0, f.irq);
        g_assert_true(sysbus_realize(SYS_BUS_DEVICE(f.dev), &error_abort));
        for (unsigned test = ZERO; test < CASES; test++) {
            unsigned phases = test == RAW_GRID ? 9 * ARRAY_SIZE(patterns) :
                              test == COUNTS && !image ? 4 : 1;

            for (unsigned phase = 0; phase < phases; phase++) {
                unsigned count = test == COUNTS && !image ?
                    (unsigned[]) { 1, 4, 6, 256 }[phase] : 4;
                unsigned setup_bytes, draw_bytes;
                bool complete = test <= IMMUTABLE;
                bool accepted = complete || test >= MAP_LOSS;
                g_autofree uint8_t *before = NULL;
                g_autofree uint8_t *expected = NULL;

                g_test_message("matrix family%u case%u phase%u", image, test, phase);
                device_cold_reset(f.dev);
                memset(data, 0, 0x60000);
                memset(pixels, 0xa5, GUARDS);
                for (unsigned p = 0; p < EXTENT / 4; p++) {
                    stl_le_p(pixels + 4 + 4 * p, 0xff203040);
                }
                if (image) {
                    build_initialized_image(data, data + 0x20000, 0x800004);
                    stl_le_p(data + 0x20000 + (35 + 45) * 4, 0xff000000);
                    for (unsigned p = 0; p < 32; p++) {
                        data[0x3000 + p] = (p * 67) & 255;
                    }
                    setup_bytes = 224;
                    draw_bytes = 472;
                } else {
                    for (unsigned i = 0; i < count; i++) {
                        rectangles[i] = (InlineRectangle) {
                            10 + (i % 4) * 16, 20 + (i / 4) * 2, 10, 1,
                            0x80000000 | (i * 0x010305 & 0xffffff),
                        };
                    }
                    build_inline_rectangles(data, data + 0x20000, rectangles,
                        count, 0x800004, true, 0x10001,
                        &setup_bytes, &draw_bytes);
                }
                for (unsigned i = 0; i < (image ? 1 : count); i++) {
                    uint8_t *matrix = data + (2 + (image ? 0 : 53 * i)) * 4;

                    memset(matrix + 4, 0, 9 * 4);
                    if (test == AFFINE) {
                        path_floats(matrix + 4,
                            (float[]) { 0, 1, 0, -1, 0, 0, 500, -600, 1 }, 9);
                    } else if (test == PROJECTIVE) {
                        path_floats(matrix + 4,
                            (float[]) { 1, 0, 2, 0, 1, 3, 0, 0, 0 }, 9);
                    } else if (test == NONFINITE) {
                        for (unsigned k = 0; k < 9; k++) {
                            stl_le_p(matrix + (k + 1) * 4, patterns[k % 4]);
                        }
                    } else if (test == RAW_GRID) {
                        stl_le_p(matrix + (phase / ARRAY_SIZE(patterns) + 1) * 4,
                                 patterns[phase % ARRAY_SIZE(patterns)]);
                    }
                }
                switch (test) {
                case FIRST_HEADER: stl_le_p(data + 2 * 4, 0x0041f03e); break;
                case SECOND_HEADER: stl_le_p(data + 12 * 4, 0x0042f046); break;
                case FINAL_TRANSFORM:
                    stl_le_p(data + (image ? 15 : 13) * 4,
                             image ? 0x3f800000 : 0x40000000); break;
                case FINAL_NAN: stl_le_p(data + 13 * 4, 0x7fc00000); break;
                case FINAL_ZERO: stl_le_p(data + 21 * 4, 0); break;
                case EARLY_LATCH: stl_le_p(data + 12 * 4, 0x0006f004); break;
                case SHORT_SETUP: setup_bytes -= 8; break;
                case SHORT_DRAW: draw_bytes -= 8; break;
                default: break;
                }
                before = g_memdup2(pixels, GUARDS);
                expected = g_memdup2(pixels, GUARDS);
                if (image) {
                    for (unsigned y = 20; y < 23; y++) {
                        for (unsigned x = 10; x < 14; x++) {
                            uint8_t *p = expected + 4 + 4 * (y * 720 + x);
                            unsigned a = data[0x3000 + 8 * (y - 20) + x - 10];

                            stl_le_p(p, rgba_over_reference(0xff000000,
                                                            ldl_le_p(p), a));
                        }
                    }
                } else {
                    for (unsigned i = 0; i < count; i++) {
                        InlineRectangle *r = &rectangles[i];

                        for (unsigned x = r->x; x < r->x + r->width; x++) {
                            stl_le_p(expected + 4 + 4 * (r->y * 720 + x), r->paint);
                        }
                    }
                }
                g_assert_cmpint(submit_inline_rectangles(&f, 0x400000,
                    setup_bytes, draw_bytes, true), ==,
                    accepted ? MEMTX_OK : MEMTX_ERROR);
                g_assert_false(f.level);
                g_assert_cmpmem(pixels, GUARDS, before, GUARDS);
                if (test == IMMUTABLE) {
                    memset(data, 0xff, setup_bytes);
                    memset(data + 0x20000, 0xff, draw_bytes);
                } else if (test == MAP_LOSS) {
                    memory_region_set_readonly(&surface, true);
                } else if (test == RESET) {
                    device_cold_reset(f.dev);
                } else if (test == CANCEL) {
                    qdev_unrealize(f.dev);
                }
                advance();
                memory_region_set_readonly(&surface, false);
                g_assert_cmpint(f.level, ==, complete);
                if (complete) {
                    g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
                }
                if (test == CONTINUE || test == REPEAT) {
                    if (image) {
                        if (test == CONTINUE) {
                            build_image_program(data, data + 0x20000);
                            stl_le_p(data + 0x20000 + 59 * 4, 0x800004);
                            stl_le_p(data + 0x20000 + 45 * 4, 0xff000000);
                            setup_bytes = 176;
                            draw_bytes = 336;
                        }
                        for (unsigned y = 20; y < 23; y++) {
                            for (unsigned x = 10; x < 14; x++) {
                                uint8_t *p = expected + 4 + 4 * (y * 720 + x);
                                unsigned a = data[0x3000 + 8 * (y - 20) + x - 10];

                                stl_le_p(p, rgba_over_reference(0xff000000,
                                                                ldl_le_p(p), a));
                            }
                        }
                    } else if (test == CONTINUE) {
                        build_inline_rectangles(data, data + 0x20000, rectangles,
                            count, 0x800004, false, 0x10001 + count,
                            &setup_bytes, &draw_bytes);
                        for (unsigned i = 0; i < count; i++) {
                            memset(data + (53 * i + 1) * 4, 0xff, 9 * 4);
                        }
                    }
                    g_assert_cmpint(submit_inline_rectangles(&f, 0x400000,
                        setup_bytes, draw_bytes, test == REPEAT), ==, MEMTX_OK);
                    advance();
                    g_assert_true(f.level);
                    g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
                }
                g_assert_cmpmem(pixels, GUARDS, complete ? expected : before,
                                GUARDS);
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
}

static void test_yuv_matrix_overwrite(void)
{
    enum { PLANE = 1024 * 682, INPUT = 1024 * 688 * 4, SLOT = 0x4b0000 };
    enum { BASE, RAW_GRID, REPEAT, IMMUTABLE, FIRST_HEADER, SECOND_HEADER,
           ACTIVE_MATRIX, ACTIVE_NAN, EARLY_LATCH, SHORT_SETUP, SHORT_DRAW,
           RETAINED_ACTIVE, MAP_LOSS, RESET, CANCEL, CASES };
    static const uint32_t patterns[] = {
        0x7fc00001, 0x7f800000, 0xffffffff, 0x01a61004,
    };

    for (unsigned profile = 0; profile < 4; profile++) {
        bool convert = profile >= 2, yuva = profile & 1;
        unsigned primary = PLANE * (yuva ? 2 : 1);
        TransferFixture f = { .dev = qdev_new(TYPE_NIKON_TRANSFER) };
        MemoryRegion commands, input, output;
        MemoryRegion *root = get_system_memory();
        uint8_t *data, *source, *target;

        object_property_add_child(object_get_objects_root(), "transfer-yuv-matrix[*]",
                                  OBJECT(f.dev));
        memory_region_init_ram_flags_nomigrate(&commands, OBJECT(f.dev),
            "test.yuv-matrix-command", 0x4000, 0, &error_abort);
        memory_region_add_subregion(root, 0x400000, &commands);
        memory_region_init_ram_flags_nomigrate(&input, OBJECT(f.dev),
            "test.yuv-matrix-input", INPUT, 0, &error_abort);
        memory_region_add_subregion(root, 0x410000, &input);
        memory_region_init_ram_flags_nomigrate(&output, OBJECT(f.dev),
            "test.yuv-matrix-output", SLOT + 8, 0, &error_abort);
        memory_region_add_subregion(root, 0x800000 - 4, &output);
        data = memory_region_get_ram_ptr(&commands);
        source = memory_region_get_ram_ptr(&input);
        target = memory_region_get_ram_ptr(&output);
        memset(source, 0, INPUT);
        f.irq = qemu_allocate_irq(irq_level, &f, 0);
        sysbus_connect_irq(SYS_BUS_DEVICE(f.dev), 0, f.irq);
        g_assert_true(sysbus_realize(SYS_BUS_DEVICE(f.dev), &error_abort));
        for (unsigned test = BASE; test < CASES; test++) {
            unsigned phases = test == RAW_GRID || test == RETAINED_ACTIVE ?
                              9 * ARRAY_SIZE(patterns) : 1;

            if (test == RETAINED_ACTIVE && profile != 3) {
                continue;
            }
            for (unsigned phase = 0; phase < phases; phase++) {
                unsigned setup_bytes = 224, draw_bytes = convert ? 1736 : 1648;
                bool complete = test <= IMMUTABLE;
                bool accepted = complete || test >= MAP_LOSS;
                g_autofree uint8_t *before = NULL;
                g_autofree uint8_t *expected = NULL;

                g_test_message("yuv matrix profile%u case%u phase%u",
                               profile, test, phase);
                device_cold_reset(f.dev);
                memset(data, 0, 0x4000);
                memset(target, 0xa5, SLOT + 8);
                if (convert && yuva) {
                    memset(target + 4 + primary, 0, PLANE);
                }
                if (convert && yuva) {
                    build_scaled_program(data, data + 0x1000, false);
                } else if (convert) {
                    build_convert_program(data, data + 0x1000);
                } else {
                    build_clear_program(data, data + 0x1000,
                                        yuva ? 0x107 : 0x105,
                                        0x800000, 0x800000 + primary);
                }
                if (test == RETAINED_ACTIVE) {
                    g_assert_cmpint(submit_rgba(&f, 224, 1736), ==, MEMTX_OK);
                    advance();
                    g_assert_true(f.level);
                    g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
                    build_scaled_program(data, data + 0x1000, true);
                    setup_bytes = 176;
                    draw_bytes = 1552;
                    stl_le_p(data + (1 + phase / ARRAY_SIZE(patterns)) * 4,
                             patterns[phase % ARRAY_SIZE(patterns)]);
                } else {
                    memset(data + 3 * 4, 0, 9 * 4);
                    if (test == RAW_GRID) {
                        stl_le_p(data + (3 + phase / ARRAY_SIZE(patterns)) * 4,
                                 patterns[phase % ARRAY_SIZE(patterns)]);
                    }
                }
                switch (test) {
                case FIRST_HEADER: stl_le_p(data + 2 * 4, 0x0041f03e); break;
                case SECOND_HEADER: stl_le_p(data + 12 * 4, 0x0042f046); break;
                case ACTIVE_MATRIX: stl_le_p(data + 13 * 4, 0x40000000); break;
                case ACTIVE_NAN: stl_le_p(data + 21 * 4, 0x7fc00000); break;
                case EARLY_LATCH: stl_le_p(data + 12 * 4, 0x0006f004); break;
                case SHORT_SETUP: setup_bytes -= 8; break;
                case SHORT_DRAW: draw_bytes -= 8; break;
                default: break;
                }
                before = g_memdup2(target, SLOT + 8);
                expected = g_memdup2(target, SLOT + 8);
                if (yuva) {
                    for (unsigned i = 0; i < primary; i += 4) {
                        stl_le_p(expected + 4 + i, 0x80801010);
                    }
                    memset(expected + 4 + primary, 0, PLANE);
                } else {
                    memset(expected + 4, 16, PLANE);
                    memset(expected + 4 + primary, 128, PLANE);
                }
                g_assert_cmpint(submit_rgba(&f, setup_bytes, draw_bytes), ==,
                                accepted ? MEMTX_OK : MEMTX_ERROR);
                g_assert_false(f.level);
                g_assert_cmpmem(target, SLOT + 8, before, SLOT + 8);
                if (test == IMMUTABLE) {
                    memset(data, 0xff, setup_bytes);
                    memset(data + 0x1000, 0xff, draw_bytes);
                } else if (test == MAP_LOSS) {
                    memory_region_set_readonly(&output, true);
                } else if (test == RESET) {
                    device_cold_reset(f.dev);
                } else if (test == CANCEL) {
                    qdev_unrealize(f.dev);
                }
                advance();
                memory_region_set_readonly(&output, false);
                g_assert_cmpint(f.level, ==, complete);
                if (complete) {
                    g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
                    if (test == REPEAT) {
                        g_assert_cmpint(submit_rgba(&f, setup_bytes, draw_bytes),
                                        ==, MEMTX_OK);
                        advance();
                        g_assert_true(f.level);
                        g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
                    }
                }
                g_assert_cmpmem(target, SLOT + 8, complete ? expected : before,
                                SLOT + 8);
            }
        }
        memory_region_del_subregion(root, &output);
        memory_region_del_subregion(root, &input);
        memory_region_del_subregion(root, &commands);
        drain_call_rcu();
        object_unparent(OBJECT(f.dev));
        object_unref(OBJECT(f.dev));
        drain_call_rcu();
        qemu_free_irq(f.irq);
    }
}

static void test_relative_paths(void)
{
    const uint8_t absolute[] = { 2, 4, 10, 4, 4, 0 };
    const float reference[] = {
        10, 10, 30, 10, 40, 10, 40, 20, 10, 20, 10, 10,
    };
    const uint8_t relative[] = { 3, 5, 11, 7, 9, 0 };
    const float offsets[] = { 10, 10, 20, 0, 10, 0, 10, 10, -30, -10 };
    const uint8_t axes[] = { 2, 4, 10, 6, 8, 0 };
    const float axis_values[] = { 10, 10, 30, 10, 40, 10, 40, 20, 10, 10 };
    const uint8_t invalid[] = { 1, 12, 13, 14, 15, 255 };
    uint32_t points[ARRAY_SIZE(reference)], values[ARRAY_SIZE(reference)];
    uint32_t matrix[9];
    NikonPathStroke stroke = { 1, 1.7320508f, 0, 0 };
    g_autofree uint8_t *expected = g_malloc0(720 * 480);
    g_autofree uint8_t *actual = g_malloc0(720 * 480);
    const char *reason = NULL;

    path_floats((uint8_t *)points, reference, ARRAY_SIZE(reference));
    path_floats((uint8_t *)matrix,
                (float[]) { 2, 0, 0, 0, 3, 0, 4, 6, 1 }, 9);
    for (unsigned i = 0; i < 4; i++) {
        bool stroke_case = i & 1;
        bool absolute_axes = i & 2;

        memset(expected, 0, 720 * 480);
        memset(actual, 0, 720 * 480);
        path_floats((uint8_t *)values,
                    absolute_axes ? axis_values : offsets,
                    ARRAY_SIZE(offsets));
        g_assert_true(nikon_path_mask(expected, 720, 480, absolute,
            ARRAY_SIZE(absolute), points, ARRAY_SIZE(points), matrix, false,
            stroke_case ? &stroke : NULL, &reason));
        g_assert_true(nikon_path_mask(actual, 720, 480,
            absolute_axes ? axes : relative, ARRAY_SIZE(relative), values,
            ARRAY_SIZE(offsets), matrix, false,
            stroke_case ? &stroke : NULL, &reason));
        g_assert_cmpmem(actual, 720 * 480, expected, 720 * 480);
    }
    for (unsigned i = 0; i < ARRAY_SIZE(invalid); i++) {
        uint8_t segments[] = { 2, invalid[i], 0 };

        g_assert_false(nikon_path_mask(NULL, 720, 480, segments,
            ARRAY_SIZE(segments), points, ARRAY_SIZE(points), matrix, false,
            NULL, &reason));
    }
    path_floats((uint8_t *)values,
                (float[]) { 4090, 0, 10, 0, -1, 1 }, 6);
    g_assert_false(nikon_path_mask(NULL, 720, 480,
        (uint8_t[]) { 2, 11, 0 }, 3, values, 6, matrix, false, NULL, &reason));
}

static void build_vector_batch(uint8_t *data, bool inherited, uint32_t first,
                               uint32_t fill, uint32_t stroke,
                               unsigned *setup_bytes, unsigned *draw_bytes,
                               unsigned *stroke_word)
{
    const InlineRectangle rectangles[] = {
        { 2, 304, 80, 48, 0 }, { 2, 304, 80, 48, 0 },
    };
    const float coordinates[] = {
        8, 305, 68, 5, 0, 5, 5, 36, 0, 5, -5, 5, -68,
        -5, 0, -5, -5, -36, 0, -5, 5, -5,
    };
    g_autofree uint8_t *paths = g_malloc0(0x60000);
    unsigned a, b, at;
    uint32_t key = 0x10000 | ((first - 0x10001 + 2) % 0xfffe + 1);

    build_inline_rectangles(data, data + 0x20000, rectangles, 2,
                             0xbec5f200, false, first, &a, &b);
    a = 106;
    b = 118; /* Remove the rectangle batch ending, retaining both clears. */
    build_stroked_paths(paths, 1, 0xbec5f200, key);
    path_floats(paths + 10 * 4,
                (float[]) { 1, 0, 0, 0, 1, 0, 0, 0, 1 }, 9);
    stl_le_p(paths + 4 * 4, 10);
    stl_le_p(paths + 22 * 4, 0xd0000016);
    stl_le_p(paths + 26 * 4, 0x1000000a);
    stl_le_p(paths + (41 + 3) * 4, 0xd0000016);
    stl_le_p(paths + (41 + 7) * 4, 0x1000000a);
    stl_le_p(paths + 0x20000 + 41 * 4, fill);
    stl_le_p(paths + 0x20000 + (55 + 17) * 4, stroke);
    stl_le_p(paths + (41 + 12) * 4, 0x4077def6);
    memcpy(data + a * 4, paths, 41 * 4);
    a += 41;
    memcpy(data + 0x20000 + b * 4, paths + 0x20000, 24 * 4);
    b += 24;
    inline_pair(data + 0x20000, &b, 0x01848004, 0);
    memcpy(data + 0x20000 + b * 4, paths + 0x20000 + 24 * 4,
           31 * 4);
    b += 31;
    if (!inherited) {
        memcpy(data + a * 4, paths, 19 * 4);
        a += 19;
        memcpy(data + 0x20000 + b * 4, paths + 0x20000, 24 * 4);
        b += 24;
    }
    *stroke_word = a;
    memcpy(data + a * 4, paths + 41 * 4, 29 * 4);
    a += 29;
    memcpy(data + 0x20000 + b * 4, paths + 0x20000 + 55 * 4, 31 * 4);
    b += 31;
    at = b;
    inline_pair(data + 0x20000, &at, 0x00081004, 1);
    b = at;
    if (a & 1) {
        inline_word(data, &a, 0);
    }
    if (b & 1) {
        inline_word(data + 0x20000, &b, 0);
    }
    *setup_bytes = a * 4;
    *draw_bytes = b * 4;
    memcpy(data + 0x50000,
           (uint8_t[]) { 2, 7, 11, 9, 11, 7, 11, 9, 11, 0 }, 10);
    path_floats(data + 0x50100, coordinates, ARRAY_SIZE(coordinates));
}

static void test_vector_batch(void)
{
    enum { EXTENT = 720 * 480 * 4, GUARDS = EXTENT + 8 };
    enum { SUCCESS, INHERITED, UNCACHED_FIRST, STROKE_MATRIX, ZERO_ALPHA,
           COLORED, WRAP, IMMUTABLE, REPEAT,
           COLD, BAD_KEY, BAD_STROKE, BAD_SEGMENT, BAD_MATRIX, BAD_ALIAS,
           LATE_SEGMENT, MAP_LOSS, RESET, CANCEL, CASES };
    const uint8_t segments[] = { 2, 4, 10, 4, 10, 4, 10, 4, 10, 0 };
    const float coordinates[] = {
        8, 305, 76, 305, 81, 305, 81, 310, 81, 346, 81, 351, 76, 351,
        8, 351, 3, 351, 3, 346, 3, 310, 3, 305, 8, 305,
    };
    uint32_t points[ARRAY_SIZE(coordinates)], matrix[9];
    NikonPathStroke stroke = { 1, 3.8729835f, 0, 0 };
    TransferFixture f = { .dev = qdev_new(TYPE_NIKON_TRANSFER) };
    MemoryRegion commands, surface, alias;
    MemoryRegion *root = get_system_memory();
    uint8_t *data, *pixels;
    g_autofree uint8_t *fill_mask = g_malloc0(720 * 480);
    g_autofree uint8_t *stroke_mask = g_malloc0(720 * 480);
    const char *reason = NULL;

    path_floats((uint8_t *)points, coordinates, ARRAY_SIZE(coordinates));
    path_floats((uint8_t *)matrix,
                (float[]) { 1, 0, 0, 0, 1, 0, 0, 0, 1 }, 9);
    g_assert_true(nikon_path_mask(fill_mask, 720, 480, segments,
        ARRAY_SIZE(segments), points, ARRAY_SIZE(points), matrix, false,
        NULL, &reason));
    g_assert_true(nikon_path_mask(stroke_mask, 720, 480, segments,
        ARRAY_SIZE(segments), points, ARRAY_SIZE(points), matrix, true,
        &stroke, &reason));
    object_property_add_child(object_get_objects_root(), "transfer-vector[*]",
                              OBJECT(f.dev));
    memory_region_init_ram_flags_nomigrate(&commands, OBJECT(f.dev),
        "test.vector-command", 0x60000, 0, &error_abort);
    memory_region_add_subregion(root, 0x400000, &commands);
    memory_region_init_ram_flags_nomigrate(&surface, OBJECT(f.dev),
        "test.vector-surface", GUARDS, 0, &error_abort);
    memory_region_add_subregion(root, 0xbec5f200 - 4, &surface);
    memory_region_init_alias(&alias, OBJECT(f.dev), "test.vector-alias",
                            &surface, 4, 128);
    memory_region_add_subregion(root, 0x500000, &alias);
    data = memory_region_get_ram_ptr(&commands);
    pixels = memory_region_get_ram_ptr(&surface);
    f.irq = qemu_allocate_irq(irq_level, &f, 0);
    sysbus_connect_irq(SYS_BUS_DEVICE(f.dev), 0, f.irq);
    g_assert_true(sysbus_realize(SYS_BUS_DEVICE(f.dev), &error_abort));
    for (unsigned test = SUCCESS; test < CASES; test++) {
        unsigned a, b, at;
        uint32_t fill = test == ZERO_ALPHA ? 0x00123456 :
                        test == COLORED ? 0x604080c0 : 0x80000000;
        uint32_t paint = test == ZERO_ALPHA ? 0x00010203 : 0xffa0a0a0;
        bool complete = test <= REPEAT;
        bool accepted = complete || test >= LATE_SEGMENT;
        g_autofree uint8_t *before = NULL;
        g_autofree uint8_t *expected = NULL;

        g_test_message("vector batch case %u", test);
        device_cold_reset(f.dev);
        memset(data, 0, 0x60000);
        memset(pixels, 0xa5, GUARDS);
        if (test != COLD) {
            const InlineRectangle clear = { 0, 0, 720, 480, 0 };

            build_inline_rectangles(data, data + 0x20000, &clear, 1,
                0xbec5f200, true, 0x10001, &a, &b);
            g_assert_cmpint(submit_inline_rectangles(&f, 0x400000, a, b,
                                                    true), ==, MEMTX_OK);
            advance();
            g_assert_true(f.level);
            g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
        }
        for (unsigned i = 0; i < EXTENT / 4; i++) {
            stl_le_p(pixels + 4 + 4 * i, 0x7f204060);
        }
        memset(data, 0, 0x60000);
        build_vector_batch(data, test == INHERITED,
                           test == WRAP ? 0x1fffd : 0x1003e,
                           fill, paint, &a, &b, &at);
        switch (test) {
        case UNCACHED_FIRST:
            memmove(data, data + 10 * 4, a - 10 * 4);
            a -= 10 * 4;
            at -= 10;
            memmove(data + 0x20000, data + 0x20008, b - 8);
            b -= 8;
            g_assert_cmpuint(a, ==, 744);
            g_assert_cmpuint(b, ==, 920);
            break;
        case STROKE_MATRIX:
            stl_le_p(data + (at - 19 + 16) * 4, 0x3f800000);
            break;
        case BAD_KEY:
            stl_le_p(data + (at + 28) * 4, 0x10001);
            break;
        case BAD_STROKE:
            stl_le_p(data + (at + 9) * 4, 1);
            break;
        case BAD_SEGMENT:
            data[0x50002] = 12;
            break;
        case BAD_MATRIX:
            stl_le_p(data + 106 * 4 + 10 * 4, 0x7fc00000);
            break;
        case BAD_ALIAS:
            stl_le_p(data + (at + 1) * 4, 0x500000);
            break;
        default:
            break;
        }
        before = g_memdup2(pixels, GUARDS);
        expected = g_memdup2(pixels, GUARDS);
        for (unsigned y = 0; y < 480; y++) {
            for (unsigned x = 0; x < 720; x++) {
                unsigned p = y * 720 + x;
                uint32_t value = x >= 2 && x < 82 && y >= 304 && y < 352 ?
                                 0 : 0x7f204060;

                value = rgba_over_reference(fill, value, fill_mask[p]);
                value = rgba_over_reference(paint, value,
                    test == STROKE_MATRIX ? (x ? stroke_mask[p - 1] : 0) :
                    stroke_mask[p]);
                stl_le_p(expected + 4 + 4 * p, value);
            }
        }
        g_assert_cmpint(submit_inline_rectangles(&f, 0x400000, a, b, false),
                        ==, accepted ? MEMTX_OK : MEMTX_ERROR);
        g_assert_false(f.level);
        g_assert_cmpmem(pixels, GUARDS, before, GUARDS);
        if (test == IMMUTABLE) {
            memset(data, 0xff, a);
            memset(data + 0x20000, 0xff, b);
        } else if (test == LATE_SEGMENT) {
            data[0x50009] = 12;
        } else if (test == MAP_LOSS) {
            memory_region_set_readonly(&surface, true);
        } else if (test == RESET) {
            device_cold_reset(f.dev);
        } else if (test == CANCEL) {
            qdev_unrealize(f.dev);
        }
        advance();
        memory_region_set_readonly(&surface, false);
        g_assert_cmpint(f.level, ==, complete);
        g_assert_cmpmem(pixels, GUARDS, complete ? expected : before, GUARDS);
        if (complete) {
            g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
            if (test == SUCCESS) {
                g_assert_cmphex((uint32_t)ldl_le_p(pixels + 4 +
                                                  4 * (328 * 720 + 40)),
                                ==, 0x80000000);
                g_assert_cmphex((uint32_t)ldl_le_p(pixels + 4 +
                                                  4 * (304 * 720 + 40)),
                                ==, 0xffa0a0a0);
                g_assert_cmphex((uint32_t)ldl_le_p(pixels + 4 +
                                                  4 * (304 * 720 + 2)),
                                ==, 0);
            }
            if (test == REPEAT) {
                g_assert_cmpint(submit_inline_rectangles(&f, 0x400000, a, b,
                                                        false), ==, MEMTX_OK);
                advance();
                g_assert_true(f.level);
                g_assert_cmpmem(pixels, GUARDS, expected, GUARDS);
                g_assert_cmpint(write_register(&f, 0x1020, 0), ==, MEMTX_OK);
            }
        }
    }
    memory_region_del_subregion(root, &alias);
    memory_region_del_subregion(root, &surface);
    memory_region_del_subregion(root, &commands);
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
    g_test_add_func("/nikon/transfer/rgba-single-clear", test_rgba_single_clear);
    g_test_add_func("/nikon/transfer/rgba-gray", test_rgba_gray);
    g_test_add_func("/nikon/transfer/path-fill", test_path_fill);
    g_test_add_func("/nikon/transfer/initialized-paths",
                    test_initialized_paths);
    g_test_add_func("/nikon/transfer/image-fill", test_image_fill);
    g_test_add_func("/nikon/transfer/rgba-yuv", test_rgba_yuv);
    g_test_add_func("/nikon/transfer/scaled-yuva", test_scaled_yuva);
    g_test_add_func("/nikon/transfer/yuva-over", test_yuva_over);
    g_test_add_func("/nikon/transfer/inline-rectangles", test_inline_rectangles);
    g_test_add_func("/nikon/transfer/rectangle-continuation",
                    test_rectangle_continuation);
    g_test_add_func("/nikon/transfer/rectangle-prefixes", test_rectangle_prefixes);
    g_test_add_func("/nikon/transfer/rgba-over", test_rgba_over);
    g_test_add_func("/nikon/transfer/rgba-targets", test_rgba_targets);
    g_test_add_func("/nikon/transfer/rgba-pair-targets", test_rgba_pair_targets);
    g_test_add_func("/nikon/transfer/initialized-image", test_initialized_image);
    g_test_add_func("/nikon/transfer/rgba-image", test_rgba_image);
    g_test_add_func("/nikon/transfer/normal-image", test_normal_image);
    g_test_add_func("/nikon/transfer/image-prefixes", test_image_prefixes);
    g_test_add_func("/nikon/transfer/stroked-paths", test_stroked_paths);
    g_test_add_func("/nikon/transfer/vector-batch", test_vector_batch);
    g_test_add_func("/nikon/transfer/relative-paths", test_relative_paths);
    g_test_add_func("/nikon/transfer/stroke-mask", test_stroke_mask);
    g_test_add_func("/nikon/transfer/matrix-overwrite", test_matrix_overwrite);
    g_test_add_func("/nikon/transfer/yuv-matrix-overwrite", test_yuv_matrix_overwrite);
    result = g_test_run();
    qemu_cleanup(result);
    unlink(socket);
    rmdir(directory);
    return result;
}
