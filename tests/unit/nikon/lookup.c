/* Real QOM/MMIO, complete provider samples and original mixed-width tables.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "qemu/timer.h"
#include "qapi/error.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/nikon/lookup.h"
#include "system/system.h"

int (*qemu_main)(void);

typedef struct LookupFixture {
    DeviceState *dev;
    qemu_irq irq;
    bool level;
    bool words;
} LookupFixture;

static void irq_level(void *opaque, int n, int level)
{
    LookupFixture *f = opaque;

    f->level = !!level;
}

static void fixture_init(LookupFixture *f, bool words)
{
    f->words = words;
    f->dev = qdev_new(words ? TYPE_NIKON_LOOKUP_WORDS : TYPE_NIKON_LOOKUP_BYTES);
    f->irq = qemu_allocate_irq(irq_level, f, 0);
    sysbus_connect_irq(SYS_BUS_DEVICE(f->dev), 0, f->irq);
    g_assert_true(sysbus_realize(SYS_BUS_DEVICE(f->dev), &error_abort));
    device_cold_reset(f->dev);
}

static void fixture_clear(LookupFixture *f)
{
    qdev_unrealize(f->dev);
    object_unparent(OBJECT(f->dev));
    object_unref(OBJECT(f->dev));
    qemu_free_irq(f->irq);
}

static void write_port(LookupFixture *f, unsigned bank, hwaddr offset,
                       uint32_t value, MemOp op, MemTxResult expected)
{
    MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(f->dev), bank);

    g_assert_cmpint(memory_region_dispatch_write(mr, offset, value, op,
                                                MEMTXATTRS_UNSPECIFIED), ==,
                    expected);
}

static uint32_t read_port(LookupFixture *f, unsigned bank, hwaddr offset,
                          MemOp op, MemTxResult expected)
{
    MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(f->dev), bank);
    uint64_t value;

    g_assert_cmpint(memory_region_dispatch_read(mr, offset, &value, op,
                                               MEMTXATTRS_UNSPECIFIED), ==,
                    expected);
    return value;
}

static void test_configuration(gconstpointer opaque)
{
    bool words = GPOINTER_TO_INT(opaque);
    LookupFixture f = { 0 };
    NikonLookupSample empty = { 0 };
    const struct { unsigned offset; uint32_t value; } config[] = {
        { 0, 0x3e }, { 4, 0xf }, { 0x10, 0x12345678 },
        { 0x14, 0x87654321 }, { 0x18, 0xffff }, { 0x1c, 0xabcd },
        { 0x30, 0x11223344 }, { 0x34, 0x55667788 }, { 0x40, 0x89abcdef },
        { 0x50, 0xffff }, { 0x54, 0x99887766 }, { 0x58, 0xdeadbeef },
        { 0x84, UINT32_MAX }, { 0x90, 0x3000f7 }, { 0x94, 0x3000f7 },
        { 0xe0, 0xff3f },
    };
    const struct { unsigned offset; uint32_t value; } extra[] = {
        { 0x20, 0xffff }, { 0x24, 0x1234 }, { 0x28, 0xabcd },
        { 0xb0, 0xffffff }, { 0xc0, 0x03ff03ff }, { 0xc4, 0xffff8000 },
        { 0xc8, 0x03ff03ff }, { 0xcc, 0x7fff },
        { 0xd0, 0x03ff03ff }, { 0xd4, 0xffffffff },
    };
    uint8_t table[64], exported[64];
    unsigned i, t;

    fixture_init(&f, words);
    /* Source stop at cold idle: clear both commands, set the stop request,
     * then observe idle without creating an event or external result. */
    write_port(&f, 0, 0, 0, MO_32, MEMTX_OK);
    write_port(&f, 0, 4, 0, MO_32, MEMTX_OK);
    write_port(&f, 1, words ? 4 : 1, words ? 0x80000000 : 0x80,
               words ? MO_32 : MO_8, MEMTX_OK);
    g_assert_cmphex(read_port(&f, 0, 0x88, MO_32, MEMTX_OK), ==, 0);
    read_port(&f, 0, 0x80, MO_32, MEMTX_ERROR);
    write_port(&f, 0, 0x80, UINT32_MAX, MO_32, MEMTX_ERROR);
    read_port(&f, 1, 0x100, words ? MO_32 : MO_8, MEMTX_ERROR);
    g_assert_false(nikon_lookup_table(f.dev, 0, exported));
    for (i = 0; i < G_N_ELEMENTS(config); i++) {
        write_port(&f, 0, config[i].offset, config[i].value, MO_32, MEMTX_OK);
    }
    write_port(&f, 0, 8, words ? 0x10110c01 : 0x10c01, MO_32, MEMTX_OK);
    if (words) {
        for (i = 0; i < G_N_ELEMENTS(extra); i++) {
            write_port(&f, 0, extra[i].offset, extra[i].value, MO_32, MEMTX_OK);
            g_assert_cmphex(read_port(&f, 0, extra[i].offset, MO_32, MEMTX_OK),
                            ==, extra[i].value);
        }
        write_port(&f, 0, 0xc4, 0x8000, MO_32, MEMTX_ERROR);
        write_port(&f, 0, 0xc0, 0x04000400, MO_32, MEMTX_ERROR);
        g_assert_cmphex(read_port(&f, 0, 0xc4, MO_32, MEMTX_OK), ==, 0xffff8000);
        write_port(&f, 0, 0xf0, 1, MO_32, MEMTX_ERROR);
    } else {
        write_port(&f, 0, 0xf0, 0xa5a55a5a, MO_32, MEMTX_OK);
        for (i = 0; i < G_N_ELEMENTS(extra); i++) {
            write_port(&f, 0, extra[i].offset, extra[i].value, MO_32, MEMTX_ERROR);
        }
    }
    write_port(&f, 0, 0, 0x80000000, MO_32, MEMTX_ERROR);
    write_port(&f, 0, 0, 0x8000, MO_32, MEMTX_ERROR);
    write_port(&f, 0, 0xc, 0, MO_32, MEMTX_ERROR);
    write_port(&f, 0, 0, 0, MO_16, MEMTX_ERROR);
    g_assert_cmphex(read_port(&f, 0, 0, MO_32, MEMTX_OK), ==, 0x3e);
    read_port(&f, 0, 0x90, MO_32, MEMTX_ERROR);
    qemu_clock_advance_virtual_time(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                                    1000000000);
    g_assert_false(f.level);
    g_assert_cmphex(read_port(&f, 0, 0x88, MO_32, MEMTX_OK), ==, 0);
    g_assert_true(nikon_lookup_publish(f.dev, &empty));
    for (i = 0; i < G_N_ELEMENTS(config); i++) {
        g_assert_cmphex(read_port(&f, 0, config[i].offset, MO_32, MEMTX_OK),
                        ==, config[i].value);
    }

    /* Every table is staged from actual bytes. Word packing is the explicit
     * big-endian BFI in 95E58, while the MMIO interface stays little-endian. */
    for (t = 0; t < 4; t++) {
        for (i = 0; i < 64; i++) {
            table[i] = (t * 67 + i * 3) ^ 0x5a;
        }
        for (i = 0; i < 64; i += words ? 4 : 1) {
            uint32_t value = words ? ldl_be_p(table + i) : table[i];

            write_port(&f, 1, 0x100 + t * 64 + i, value,
                       words ? MO_32 : MO_8, MEMTX_OK);
            g_assert_cmphex(read_port(&f, 1, 0x100 + t * 64 + i,
                                      words ? MO_32 : MO_8, MEMTX_OK), ==, value);
            if (i + (words ? 4 : 1) < 64) {
                g_assert_false(nikon_lookup_table(f.dev, t, exported));
            }
        }
        g_assert_true(nikon_lookup_table(f.dev, t, exported));
        g_assert_cmpmem(exported, 64, table, 64);
    }
    write_port(&f, 1, 0x100, 0, MO_16, MEMTX_ERROR);
    if (words) {
        write_port(&f, 1, 0x101, 0, MO_32, MEMTX_ERROR);
    }
    write_port(&f, 1, 0x80, 0, words ? MO_32 : MO_8, MEMTX_ERROR);
    g_assert_false(nikon_lookup_table(f.dev, 4, exported));
    device_cold_reset(f.dev);
    g_assert_false(nikon_lookup_table(f.dev, 0, exported));
    g_assert_cmphex(read_port(&f, 0, 0x88, MO_32, MEMTX_OK), ==, 0);
    g_assert_cmphex(read_port(&f, 0, 0, MO_32, MEMTX_OK), ==, 0);
    fixture_clear(&f);
}

static void test_provider(gconstpointer opaque)
{
    bool words = GPOINTER_TO_INT(opaque);
    LookupFixture f = { 0 };
    NikonLookupSample sample = {
        .pending = 0x8001, .busy = 0x80000000, .count = 0x1234,
        .result = 0xdeadbeef, .lane = { 2, 3 },
        .auxiliary_pending = 0x81, .auxiliary_result = 0xaabbccdd,
    };
    NikonLookupSample invalid;
    MemOp auxop = words ? MO_32 : MO_8;
    unsigned ack = words ? 0x2c : 0xe;

    fixture_init(&f, words);
    /* A submitted opaque operation owns busy even before any provider is
     * connected. Clearing its request fields is not a completion receipt. */
    write_port(&f, 0, 0, 0x4000, MO_32, MEMTX_OK);
    g_assert_cmphex(read_port(&f, 0, 0x88, MO_32, MEMTX_OK), ==, 0x80000000);
    write_port(&f, 0, 0, 0, MO_32, MEMTX_OK);
    write_port(&f, 1, words ? 4 : 1, words ? 0x80000000 : 0x80,
               auxop, MEMTX_OK);
    g_assert_cmphex(read_port(&f, 0, 0x88, MO_32, MEMTX_OK), ==, 0x80000000);
    read_port(&f, 0, 0x80, MO_32, MEMTX_ERROR);
    if (words) {
        sample.selection = 0x1f;
        sample.flags = 3;
        sample.available = 1;
        sample.extent = 480;
        write_port(&f, 1, 0, 0x739, MO_32, MEMTX_OK);
        write_port(&f, 1, 0xc, 0x3f, MO_32, MEMTX_OK);
        write_port(&f, 1, 0x10, 0x3f, MO_32, MEMTX_OK);
        write_port(&f, 1, 0x14, 0x12345678, MO_32, MEMTX_OK);
        write_port(&f, 1, 0x18, 0xffff, MO_32, MEMTX_OK);
        write_port(&f, 1, 0x1c, 0xffff, MO_32, MEMTX_OK);
        write_port(&f, 1, 0x24, 7, MO_32, MEMTX_OK);
        write_port(&f, 1, 0x28, 0x87654321, MO_32, MEMTX_OK);
        write_port(&f, 1, 0x10, 0x40, MO_32, MEMTX_ERROR);
        g_assert_cmphex(read_port(&f, 1, 0x28, MO_32, MEMTX_OK), ==, 0x87654321);
    } else {
        for (unsigned i = 0; i <= 10; i++) {
            if (i != 2) {
                write_port(&f, 1, i, i == 0 ? 0x1f : i == 1 ? 0x83 : 0xa5,
                           MO_8, MEMTX_OK);
            }
        }
        write_port(&f, 1, 2, 0, MO_8, MEMTX_ERROR);
        write_port(&f, 1, 0, 0x20, MO_8, MEMTX_ERROR);
    }
    read_port(&f, 1, ack, auxop, MEMTX_ERROR);
    write_port(&f, 1, ack, 0, auxop, MEMTX_ERROR);
    g_assert_true(nikon_lookup_publish(f.dev, &sample));
    g_assert_false(f.level); /* Real causes are retained behind the zero mask. */
    g_assert_cmphex(read_port(&f, 0, 0x80, MO_32, MEMTX_OK), ==, 0x8001);
    g_assert_cmphex(read_port(&f, 0, 0x70, MO_32, MEMTX_OK), ==, 0x1234);
    g_assert_cmphex(read_port(&f, 0, 0x74, MO_32, MEMTX_OK), ==, 0xdeadbeef);
    g_assert_cmphex(read_port(&f, 0, 0x90, MO_32, MEMTX_OK), ==, 2U << 24);
    g_assert_cmphex(read_port(&f, 0, 0x94, MO_32, MEMTX_OK), ==, 3U << 24);
    if (words) {
        g_assert_cmphex(read_port(&f, 1, 0x20, MO_32, MEMTX_OK), ==, 0xaabbccdd);
        g_assert_cmphex(read_port(&f, 1, 0x30, MO_32, MEMTX_OK), ==, 0x1f);
        g_assert_cmphex(read_port(&f, 1, 0x34, MO_32, MEMTX_OK), ==, 3);
        g_assert_cmphex(read_port(&f, 1, 0x38, MO_32, MEMTX_OK), ==, 1);
        g_assert_cmphex(read_port(&f, 1, 0x3c, MO_32, MEMTX_OK), ==, 480);
    } else {
        g_assert_cmphex(read_port(&f, 1, 0xb, MO_8, MEMTX_OK), ==, 0xbb);
        g_assert_cmphex(read_port(&f, 1, 0xc, MO_8, MEMTX_OK), ==, 0xcc);
        g_assert_cmphex(read_port(&f, 1, 0xd, MO_8, MEMTX_OK), ==, 0xdd);
        g_assert_cmphex(read_port(&f, 1, 0xf, MO_8, MEMTX_OK), ==, 0xaa);
    }
    write_port(&f, 0, 0x84, 1, MO_32, MEMTX_OK);
    g_assert_true(f.level);
    write_port(&f, 0, 0x80, 1, MO_32, MEMTX_OK);
    g_assert_false(f.level);
    write_port(&f, 0, 0x84, 0x8004, MO_32, MEMTX_OK);
    g_assert_true(f.level);
    sample.pending = 4;
    sample.auxiliary_pending = 2;
    g_assert_true(nikon_lookup_publish(f.dev, &sample));
    write_port(&f, 0, 0x80, 0x8000, MO_32, MEMTX_OK);
    g_assert_cmphex(read_port(&f, 0, 0x80, MO_32, MEMTX_OK), ==, 4);
    g_assert_true(f.level);
    g_assert_cmphex(read_port(&f, 1, ack, auxop, MEMTX_OK), ==, 0x83);
    write_port(&f, 1, ack, words ? ~0x81U : 0x7e, auxop, MEMTX_OK);
    g_assert_cmphex(read_port(&f, 1, ack, auxop, MEMTX_OK), ==, 2);
    g_assert_true(f.level); /* Auxiliary inverse ACK is not the main ACK. */
    write_port(&f, 1, ack, 0, auxop, MEMTX_OK);
    write_port(&f, 0, 0x80, 4, MO_32, MEMTX_OK);
    g_assert_false(f.level);

    /* The source stop/request writes are real stored fields. They cannot
     * change an external busy result, fabricate completion or clear causes. */
    write_port(&f, 0, 0, 0x4000, MO_32, MEMTX_OK);
    write_port(&f, 0, 4, 0x4000, MO_32, MEMTX_OK);
    write_port(&f, 1, words ? 4 : 1, words ? 0x80000001 : 0x81,
               auxop, MEMTX_OK);
    write_port(&f, 0, 0, 0, MO_32, MEMTX_OK);
    write_port(&f, 0, 4, 0, MO_32, MEMTX_OK);
    g_assert_cmphex(read_port(&f, 0, 0x88, MO_32, MEMTX_OK), ==, 0x80000000);
    qemu_clock_advance_virtual_time(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                                    1000000000);
    g_assert_false(f.level);
    invalid = sample;
    invalid.lane[0] = words ? 4 : 0x100;
    invalid.pending = UINT32_MAX;
    g_assert_false(nikon_lookup_publish(f.dev, &invalid));
    g_assert_cmphex(read_port(&f, 0, 0x80, MO_32, MEMTX_OK), ==, 0);
    write_port(&f, 0, 0x70, 0, MO_32, MEMTX_ERROR);
    g_assert_cmphex(read_port(&f, 0, 0x70, MO_32, MEMTX_OK), ==, 0x1234);
    nikon_lookup_disconnect(f.dev);
    read_port(&f, 0, 0x80, MO_32, MEMTX_ERROR);
    g_assert_cmphex(read_port(&f, 0, 0x88, MO_32, MEMTX_OK), ==, 0x80000000);
    g_assert_false(f.level);
    g_assert_cmphex(read_port(&f, 0, 0x84, MO_32, MEMTX_OK), ==, 0x8004);
    sample.busy = 0;
    sample.pending = 0;
    sample.auxiliary_pending = 0;
    g_assert_true(nikon_lookup_publish(f.dev, &sample));
    g_assert_cmphex(read_port(&f, 0, 0x88, MO_32, MEMTX_OK), ==, 0);
    write_port(&f, 0, 4, 0x4000, MO_32, MEMTX_OK);
    g_assert_cmphex(read_port(&f, 0, 0x88, MO_32, MEMTX_OK), ==, 0x80000000);
    device_cold_reset(f.dev);
    g_assert_cmphex(read_port(&f, 0, 0x88, MO_32, MEMTX_OK), ==, 0);
    read_port(&f, 0, 0x80, MO_32, MEMTX_ERROR);
    fixture_clear(&f);
}

static void test_disconnect(gconstpointer opaque)
{
    bool words = GPOINTER_TO_INT(opaque);
    LookupFixture f = { 0 };
    NikonLookupSample causes = {
        .pending = 0x8001, .auxiliary_pending = 0x81, .result = 0x12345678,
    };
    NikonLookupSample empty = { 0 };
    MemOp auxop = words ? MO_32 : MO_8;
    unsigned ack = words ? 0x2c : 0xe;

    fixture_init(&f, words);
    write_port(&f, 0, 0x84, 0x8001, MO_32, MEMTX_OK);
    g_assert_true(nikon_lookup_publish(f.dev, &causes));
    g_assert_true(f.level);
    nikon_lookup_disconnect(f.dev);
    g_assert_false(f.level);
    read_port(&f, 0, 0x80, MO_32, MEMTX_ERROR);
    read_port(&f, 0, 0x74, MO_32, MEMTX_ERROR);
    read_port(&f, 1, ack, auxop, MEMTX_ERROR);
    write_port(&f, 0, 0x80, UINT32_MAX, MO_32, MEMTX_ERROR);
    write_port(&f, 1, ack, 0, auxop, MEMTX_ERROR);

    /* A complete zero-cause reconnect replaces data, not unacknowledged
     * causes. A rejected ACK while disconnected cannot consume them either. */
    g_assert_true(nikon_lookup_publish(f.dev, &empty));
    g_assert_true(f.level);
    g_assert_cmphex(read_port(&f, 0, 0x80, MO_32, MEMTX_OK), ==, 0x8001);
    g_assert_cmphex(read_port(&f, 0, 0x74, MO_32, MEMTX_OK), ==, 0);
    g_assert_cmphex(read_port(&f, 1, ack, auxop, MEMTX_OK), ==, 0x81);
    write_port(&f, 0, 0x80, 1, MO_32, MEMTX_OK);
    write_port(&f, 1, ack, words ? ~1U : 0xfe, auxop, MEMTX_OK);
    g_assert_cmphex(read_port(&f, 0, 0x80, MO_32, MEMTX_OK), ==, 0x8000);
    g_assert_cmphex(read_port(&f, 1, ack, auxop, MEMTX_OK), ==, 0x80);
    g_assert_true(f.level);
    nikon_lookup_disconnect(f.dev);
    g_assert_false(f.level);
    g_assert_true(nikon_lookup_publish(f.dev, &empty));
    g_assert_cmphex(read_port(&f, 0, 0x80, MO_32, MEMTX_OK), ==, 0x8000);
    g_assert_cmphex(read_port(&f, 1, ack, auxop, MEMTX_OK), ==, 0x80);
    write_port(&f, 1, ack, 0, auxop, MEMTX_OK);
    g_assert_true(f.level);
    write_port(&f, 0, 0x80, 0x8000, MO_32, MEMTX_OK);
    g_assert_false(f.level);
    nikon_lookup_disconnect(f.dev);
    g_assert_true(nikon_lookup_publish(f.dev, &empty));
    g_assert_cmphex(read_port(&f, 0, 0x80, MO_32, MEMTX_OK), ==, 0);
    g_assert_cmphex(read_port(&f, 1, ack, auxop, MEMTX_OK), ==, 0);

    /* Reset, including reset while disconnected, really discards causes. */
    causes.pending = 2;
    causes.auxiliary_pending = 2;
    write_port(&f, 0, 0x84, 2, MO_32, MEMTX_OK);
    g_assert_true(nikon_lookup_publish(f.dev, &causes));
    g_assert_true(f.level);
    nikon_lookup_disconnect(f.dev);
    device_cold_reset(f.dev);
    read_port(&f, 0, 0x80, MO_32, MEMTX_ERROR);
    g_assert_true(nikon_lookup_publish(f.dev, &empty));
    write_port(&f, 0, 0x84, 2, MO_32, MEMTX_OK);
    g_assert_cmphex(read_port(&f, 0, 0x80, MO_32, MEMTX_OK), ==, 0);
    g_assert_cmphex(read_port(&f, 1, ack, auxop, MEMTX_OK), ==, 0);
    g_assert_false(f.level);
    fixture_clear(&f);
}

int main(int argc, char **argv)
{
    g_autofree char *directory = g_dir_make_tmp("nikonXXXXXX", NULL);
    g_autofree char *socket = g_build_filename(directory, "qtest", NULL);
    g_autofree char *chardev = g_strdup_printf("unix:%s,server=on,wait=off", socket);
    char *qemu_argv[] = {
        argv[0], "-machine", "none", "-accel", "qtest", "-display", "none",
        "-nodefaults", "-qtest", chardev, NULL
    };
    int result;

    g_assert_nonnull(directory);
    g_test_init(&argc, &argv, NULL);
    qemu_init(G_N_ELEMENTS(qemu_argv) - 1, qemu_argv);
    g_test_add_data_func("/nikon/lookup/bytes/configuration", GINT_TO_POINTER(0),
                         test_configuration);
    g_test_add_data_func("/nikon/lookup/words/configuration", GINT_TO_POINTER(1),
                         test_configuration);
    g_test_add_data_func("/nikon/lookup/bytes/provider", GINT_TO_POINTER(0),
                         test_provider);
    g_test_add_data_func("/nikon/lookup/words/provider", GINT_TO_POINTER(1),
                         test_provider);
    g_test_add_data_func("/nikon/lookup/bytes/disconnect", GINT_TO_POINTER(0),
                         test_disconnect);
    g_test_add_data_func("/nikon/lookup/words/disconnect", GINT_TO_POINTER(1),
                         test_disconnect);
    result = g_test_run();
    qemu_cleanup(result);
    unlink(socket);
    rmdir(directory);
    return result;
}
