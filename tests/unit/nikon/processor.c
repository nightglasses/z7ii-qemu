/* Original bootstrap access shape, real caller bytes and no fake execution.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/timer.h"
#include "qemu/rcu.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/nikon/processor.h"
#include "system/system.h"

int (*qemu_main)(void);

typedef struct ProcessorFixture {
    DeviceState *dev;
    qemu_irq request;
    bool active;
} ProcessorFixture;

static void request_line(void *opaque, int n, int level)
{
    *(bool *)opaque = !!level;
}

static ProcessorFixture *fixture(void)
{
    ProcessorFixture *f = g_new0(ProcessorFixture, 1);

    f->dev = qdev_new(TYPE_NIKON_PROCESSOR);
    object_property_add_child(object_get_objects_root(), "processor[*]",
                              OBJECT(f->dev));
    f->request = qemu_allocate_irq(request_line, &f->active, 0);
    qdev_connect_gpio_out_named(f->dev, "request", 0, f->request);
    g_assert_true(sysbus_realize(SYS_BUS_DEVICE(f->dev), &error_abort));
    device_cold_reset(f->dev);
    return f;
}

static void cleanup(ProcessorFixture *f)
{
    qdev_unrealize(f->dev);
    g_assert_false(f->active);
    object_unparent(OBJECT(f->dev));
    object_unref(OBJECT(f->dev));
    drain_call_rcu();
    qemu_free_irq(f->request);
    g_free(f);
}

static MemTxResult write_port(ProcessorFixture *f, unsigned region,
                              hwaddr offset, uint64_t value, MemOp size)
{
    return memory_region_dispatch_write(
        sysbus_mmio_get_region(SYS_BUS_DEVICE(f->dev), region), offset,
        value, size, MEMTXATTRS_UNSPECIFIED);
}

static MemTxResult read_port(ProcessorFixture *f, unsigned region,
                             hwaddr offset, uint64_t *value, MemOp size)
{
    return memory_region_dispatch_read(
        sysbus_mmio_get_region(SYS_BUS_DEVICE(f->dev), region), offset,
        value, size, MEMTXATTRS_UNSPECIFIED);
}

static void write_word(ProcessorFixture *f, hwaddr offset, uint32_t value)
{
    g_assert_cmpint(write_port(f, 0, offset, value, MO_32), ==, MEMTX_OK);
}

static void prepare(ProcessorFixture *f)
{
    /* Same word stores as original1020b38a, no supplied firmware bytes. */
    write_word(f, 0x0c, 0);
    write_word(f, 0, 0);
    write_word(f, 4, 1);
    write_word(f, 8, 1);
    for (unsigned i = 0; i < NIKON_PROCESSOR_BOOTSTRAP_SIZE; i += 4) {
        g_assert_cmpint(write_port(f, 1, i, 0xa5120000 ^ i, MO_32), ==,
                        MEMTX_OK);
    }
    write_word(f, 0x14, 0);
    write_word(f, 8, 0);
}

static void test_bootstrap(void)
{
    ProcessorFixture *f = fixture();
    NikonProcessorRequest r, second;
    uint64_t value;

    prepare(f); /* The observed parameter-zero access must be meaningful. */
    g_assert_false(f->active);
    g_assert_false(nikon_processor_request(f->dev, &r));
    g_assert_cmpint(read_port(f, 0, 0x10, &value, MO_32), ==, MEMTX_OK);
    g_assert_cmphex(value, ==, 0);
    write_word(f, 0x10, 0);
    write_word(f, 0x10, 0x10000);
    g_assert_true(f->active);
    g_assert_true(nikon_processor_request(f->dev, &r));
    g_assert_cmphex(r.written, ==, 0x3f);
    g_assert_cmphex(r.configuration[0], ==, 0);
    g_assert_cmphex(r.configuration[1], ==, 1);
    g_assert_cmphex(r.configuration[2], ==, 0);
    g_assert_cmphex(r.configuration[3], ==, 0);
    g_assert_cmphex(r.configuration[4], ==, 0x10000);
    g_assert_cmphex(r.configuration[5], ==, 0);
    for (unsigned i = 0; i < sizeof(r.bootstrap); i += 4) {
        uint32_t word = ldl_le_p(r.bootstrap + i);
        g_assert_cmphex(word, ==, 0xa5120000 ^ i);
    }
    qemu_clock_advance_virtual_time(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                                    1000000000);
    g_assert_true(f->active);
    g_assert_true(nikon_processor_request(f->dev, &second));
    g_assert_cmpuint(r.token, ==, second.token);
    g_assert_cmpmem(r.bootstrap, sizeof(r.bootstrap), second.bootstrap,
                    sizeof(second.bootstrap));
    g_assert_cmpint(read_port(f, 0, 0x10, &value, MO_32), ==, MEMTX_OK);
    g_assert_cmphex(value, ==, 0x10000); /* No timed busy-bit clearing. */
    write_word(f, 0x10, 0x10000); /* Duplicate does not own a second attempt. */
    g_assert_true(nikon_processor_request(f->dev, &second));
    g_assert_cmpuint(r.token, ==, second.token);
    g_assert_cmpint(write_port(f, 1, 0, 0, MO_32), ==, MEMTX_ERROR);
    g_assert_cmpint(write_port(f, 0, 0x0c, 7, MO_32), ==, MEMTX_ERROR);
    /* Original timeout1020b47c->41a stops; no ready word or reply needed. */
    write_word(f, 0, 1);
    g_assert_false(f->active);
    g_assert_false(nikon_processor_request(f->dev, &second));
    g_assert_cmpint(read_port(f, 1, 0, &value, MO_32), ==, MEMTX_OK);
    g_assert_cmphex(value, ==, 0xa5120000);
    prepare(f);
    write_word(f, 0x10, 0x10000);
    g_assert_true(nikon_processor_request(f->dev, &second));
    g_assert_cmpuint(r.token, !=, second.token);
    device_cold_reset(f->dev);
    g_assert_false(f->active);
    g_assert_false(nikon_processor_request(f->dev, &second));
    g_assert_cmpint(read_port(f, 1, 0, &value, MO_32), ==, MEMTX_OK);
    g_assert_cmphex(value, ==, 0xa5120000); /* Storage is not a CPU ready state. */
    cleanup(f);
}

static void test_negative(void)
{
    ProcessorFixture *f = fixture();
    uint64_t value;

    g_assert_cmpint(write_port(f, 0, 0x0c, 0x100, MO_32), ==, MEMTX_ERROR);
    g_assert_cmpint(write_port(f, 0, 0x0c, 0, MO_16), ==, MEMTX_ERROR);
    /* QEMU rejects this unaligned word before the device callback. */
    g_assert_cmpint(write_port(f, 0, 0x0d, 0, MO_32), ==,
                    MEMTX_DECODE_ERROR);
    g_assert_cmpint(write_port(f, 0, 0x10, 0x10000, MO_32), ==, MEMTX_ERROR);
    g_assert_cmpint(write_port(f, 0, 4, 2, MO_32), ==, MEMTX_ERROR);
    g_assert_cmpint(write_port(f, 0, 0x10, 0x80000000, MO_32), ==, MEMTX_ERROR);
    for (unsigned i = 0x18; i <= 0x40; i += 4) {
        g_assert_cmpint(read_port(f, 0, i, &value, MO_32), ==, MEMTX_ERROR);
        g_assert_cmpint(write_port(f, 0, i, 0, MO_32), ==, MEMTX_ERROR);
    }
    g_assert_false(f->active);
    prepare(f);
    write_word(f, 0x0c, 0x7f);
    write_word(f, 0x10, 0x10000);
    {
        NikonProcessorRequest r;
        g_assert_true(nikon_processor_request(f->dev, &r));
        g_assert_cmphex(r.configuration[3], ==, 0x7f);
    }
    cleanup(f); /* Unrealize cancels without producing any event. */
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
    g_test_add_func("/nikon/processor/bootstrap", test_bootstrap);
    g_test_add_func("/nikon/processor/negative", test_negative);
    result = g_test_run();
    qemu_cleanup(result);
    unlink(socket);
    rmdir(directory);
    return result;
}
