/* Native master cadence and externally clocked slave FIFO transactions.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/main-loop.h"
#include "qemu/rcu.h"
#include "qemu/timer.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-clock.h"
#include "hw/ssi/ssi.h"
#include "qemu/bswap.h"
#include "system/address-spaces.h"
#include "hw/nikon/devices.h"
#include "hw/nikon/peer.h"
#include "system/cpu-timers.h"
#include "system/runstate.h"
#include "system/system.h"

int (*qemu_main)(void);

typedef struct SPIFixture {
    DeviceState *dev;
    DeviceState *peripheral;
    MemoryRegion *regs;
    Clock *clock;
    qemu_irq observed[4];
    bool level[4];
} SPIFixture;

#define TYPE_SPI_ORACLE "nikon-spi-test-oracle"
typedef struct SPIOracle {
    SSIPeripheral parent_obj;
    uint32_t mosi[128];
    unsigned count;
} SPIOracle;

static uint32_t oracle_transfer(SSIPeripheral *dev, uint32_t value)
{
    SPIOracle *s = (SPIOracle *)dev;

    g_assert_cmpuint(s->count, <, ARRAY_SIZE(s->mosi));
    s->mosi[s->count++] = value;
    return value ^ 0xa55a;
}

static void oracle_realize(SSIPeripheral *dev, Error **errp)
{
}

static void oracle_class_init(ObjectClass *klass, const void *data)
{
    SSIPeripheralClass *ssc = SSI_PERIPHERAL_CLASS(klass);

    ssc->realize = oracle_realize;
    ssc->transfer = oracle_transfer;
    ssc->cs_polarity = SSI_CS_NONE;
}

static const TypeInfo oracle_type = {
    .name = TYPE_SPI_ORACLE,
    .parent = TYPE_SSI_PERIPHERAL,
    .instance_size = sizeof(SPIOracle),
    .class_init = oracle_class_init,
};

static SPIOracle *attach_oracle(SPIFixture *f)
{
    f->peripheral = qdev_new(TYPE_SPI_ORACLE);
    object_property_add_child(OBJECT(f->dev), "oracle",
                              OBJECT(f->peripheral));
    g_assert_true(qdev_realize(f->peripheral,
                              qdev_get_child_bus(f->dev, "spi"), &error_abort));
    return (SPIOracle *)f->peripheral;
}

static void observe(void *opaque, int n, int level)
{
    SPIFixture *f = opaque;

    f->level[n] = !!level;
}

static SPIFixture *fixture(bool dma)
{
    SPIFixture *f = g_new0(SPIFixture, 1);

    f->dev = qdev_new(TYPE_NIKON_SPI);
    object_property_add_child(object_get_objects_root(), "spi[*]",
                              OBJECT(f->dev));
    qdev_prop_set_bit(f->dev, "dma", dma);
    qdev_prop_set_uint32(f->dev, "channel", dma ? 2 : 0);
    /* Drive a source: updating the input directly does not call its callback. */
    f->clock = clock_new(OBJECT(f->dev), "test-clock");
    clock_set_hz(f->clock, 48000000);
    qdev_connect_clock_in(f->dev, "spi-clock", f->clock);
    g_assert_true(sysbus_realize(SYS_BUS_DEVICE(f->dev), &error_abort));
    for (unsigned i = 0; i < ARRAY_SIZE(f->observed); i++) {
        f->observed[i] = qemu_allocate_irq(observe, f, i);
        if (i < 2) {
            sysbus_connect_irq(SYS_BUS_DEVICE(f->dev), i, f->observed[i]);
        } else {
            qdev_connect_gpio_out_named(f->dev, "dma", i - 2, f->observed[i]);
        }
    }
    device_cold_reset(f->dev);
    f->regs = sysbus_mmio_get_region(SYS_BUS_DEVICE(f->dev), 0);
    return f;
}

static void cleanup(SPIFixture *f)
{
    if (f->peripheral) {
        qdev_unrealize(f->peripheral);
        object_unparent(OBJECT(f->peripheral));
        object_unref(OBJECT(f->peripheral));
    }
    qdev_unrealize(f->dev);
    object_unparent(OBJECT(f->dev));
    object_unref(OBJECT(f->dev));
    for (unsigned i = 0; i < ARRAY_SIZE(f->observed); i++) {
        qemu_free_irq(f->observed[i]);
    }
    g_free(f);
}

static void write_word(SPIFixture *f, hwaddr offset, uint32_t value)
{
    g_assert_cmpint(memory_region_dispatch_write(
                        f->regs, offset, value, MO_32,
                        MEMTXATTRS_UNSPECIFIED),
                    ==, MEMTX_OK);
    aio_poll(qemu_get_aio_context(), false);
}

static uint32_t read_word(SPIFixture *f, hwaddr offset)
{
    uint64_t value;

    g_assert_cmpint(memory_region_dispatch_read(
                        f->regs, offset, &value, MO_32,
                        MEMTXATTRS_UNSPECIFIED),
                    ==, MEMTX_OK);
    return value;
}

static void prepare(SPIFixture *f)
{
    write_word(f, 0x60, 0);       /* Open the original active-low port gate. */
    write_word(f, 0x08, 0);       /* Divide by two when DIVENABLE is set. */
    write_word(f, 0x0c, 0x22);    /* Synchronous master with DIVENABLE. */
    write_word(f, 0x10, 0x0f00);  /* Sixteen bits per frame. */
}

static void enqueue(SPIFixture *f)
{
    for (unsigned i = 0; i < 4; i++) {
        write_word(f, 0, 0x1200 + i);
    }
}

/* Explicit 100 MHz, 16 bits and DIV6: the sampled word period is 2240 ns. */
static void elapsed_prepare(SPIFixture *f)
{
    prepare(f);
    clock_update_hz(f->clock, 100000000);
    write_word(f, 8, 6);
}

static void test_elapsed_observations(void)
{
    SPIFixture *f = fixture(false);
    SPIOracle *s = attach_oracle(f);
    int64_t start;

    elapsed_prepare(f);
    enqueue(f);
    start = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    write_word(f, 0x3c, 1);
    /* No main-loop/timer dispatch between advancing time and observing MMIO. */
    cpus_set_virtual_clock(start + 2239);
    g_assert_cmphex(read_word(f, 0x14) & 0x21, ==, 0x21);
    g_assert_cmpuint(s->count, ==, 0);
    cpus_set_virtual_clock(start + 2240);
    g_assert_cmphex(read_word(f, 0x14) & 0x20, ==, 0);
    g_assert_cmpuint(s->count, ==, 1);
    g_assert_cmphex(read_word(f, 4), ==, 0x1200 ^ 0xa55a);
    cpus_set_virtual_clock(start + 4 * 2240);
    /* DR itself must catch up before checking for a genuinely empty RX FIFO. */
    for (unsigned i = 1; i < 4; i++) {
        g_assert_cmphex(read_word(f, 4), ==, (0x1200 + i) ^ 0xa55a);
    }
    g_assert_cmpuint(s->count, ==, 4);
    for (unsigned i = 0; i < 4; i++) {
        g_assert_cmphex(s->mosi[i], ==, 0x1200 + i);
    }
    g_assert_cmpuint(read_word(f, 0x2c), ==, 0);
    g_assert_cmpuint(read_word(f, 0x30), ==, 0);
    qemu_clock_run_timers(QEMU_CLOCK_VIRTUAL);
    g_assert_cmpuint(s->count, ==, 4); /* Removed elapsed timer cannot replay. */
    write_word(f, 0, 0x5678);
    g_assert_cmpuint(read_word(f, 0x2c), ==, 1);
    g_assert_cmpuint(s->count, ==, 4); /* New idle word gets a future deadline. */
    cpus_set_virtual_clock(start + 5 * 2240);
    g_assert_cmphex(read_word(f, 4), ==, 0x5678 ^ 0xa55a);
    g_assert_cmpuint(s->count, ==, 5);
    cleanup(f);
}

static void test_elapsed_invalid(void)
{
    static const struct { hwaddr offset; uint32_t value; } invalid[] = {
        { 0x40, 0 }, { 0x28, 0x200 }, { 0x3c, 2 }, { 8, 0 },
        { 0x10, 0x2000 }, { 0x4c, 0x10000 }, { 0x64, 1 },
    };
    SPIFixture *f = fixture(false);
    SPIOracle *s = attach_oracle(f);
    uint64_t value;

    elapsed_prepare(f);
    g_assert_cmpint(memory_region_dispatch_read(f->regs, 4, &value, MO_32,
                                               MEMTXATTRS_UNSPECIFIED),
                    ==, MEMTX_ERROR); /* True empty DR remains unsupported. */
    enqueue(f);
    write_word(f, 0x3c, 1);
    cpus_set_virtual_clock(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 100000);
    g_assert_cmpint(memory_region_dispatch_read(f->regs, 0x14, &value, MO_16,
                                               MEMTXATTRS_UNSPECIFIED),
                    ==, MEMTX_ERROR);
    g_assert_cmpint(memory_region_dispatch_read(f->regs, 0x40, &value, MO_32,
                                               MEMTXATTRS_UNSPECIFIED),
                    ==, MEMTX_ERROR);
    g_assert_cmpint(memory_region_dispatch_write(f->regs, 0, 0x9999, MO_16,
                                                MEMTXATTRS_UNSPECIFIED),
                    ==, MEMTX_ERROR);
    for (unsigned i = 0; i < ARRAY_SIZE(invalid); i++) {
        g_assert_cmpint(memory_region_dispatch_write(f->regs, invalid[i].offset,
                                                    invalid[i].value, MO_32,
                                                    MEMTXATTRS_UNSPECIFIED),
                        ==, MEMTX_ERROR);
        g_assert_cmpuint(s->count, ==, 0);
    }
    /* A valid W1C first completes elapsed frames, then clears their pending. */
    write_word(f, 0x28, 0x1ff);
    g_assert_cmpuint(s->count, ==, 4);
    g_assert_cmphex(read_word(f, 0x24), ==, 0);
    for (unsigned i = 0; i < 4; i++) {
        g_assert_cmphex(read_word(f, 4), ==, (0x1200 + i) ^ 0xa55a);
    }
    g_assert_cmpint(memory_region_dispatch_read(f->regs, 4, &value, MO_32,
                                               MEMTXATTRS_UNSPECIFIED),
                    ==, MEMTX_ERROR);
    g_assert_cmpuint(s->count, ==, 4);
    cleanup(f);
}

static void test_elapsed_cancel_clock(void)
{
    SPIFixture *f = fixture(false);
    SPIOracle *s = attach_oracle(f);
    int64_t start;

    elapsed_prepare(f);
    enqueue(f);
    start = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    write_word(f, 0x3c, 1);
    cpus_set_virtual_clock(start + 2239);
    write_word(f, 0x3c, 0);
    cpus_set_virtual_clock(start + 100000);
    g_assert_cmpuint(read_word(f, 0x2c), ==, 4);
    g_assert_cmpuint(s->count, ==, 0);
    write_word(f, 0x3c, 1);
    device_cold_reset(f->dev);
    cpus_set_virtual_clock(start + 200000);
    g_assert_cmpuint(read_word(f, 0x30), ==, 0);
    g_assert_cmpuint(s->count, ==, 0);

    elapsed_prepare(f);
    clock_update_hz(f->clock, 0);
    write_word(f, 0, 0x3456);
    write_word(f, 0, 0x789a);
    write_word(f, 0x3c, 1);
    cpus_set_virtual_clock(start + 300000);
    g_assert_cmpuint(read_word(f, 0x30), ==, 0);
    g_assert_cmpuint(s->count, ==, 0);
    clock_update_hz(f->clock, 100000000);
    cpus_set_virtual_clock(start + 301000);
    clock_update_hz(f->clock, 0);
    cpus_set_virtual_clock(start + 400000);
    g_assert_cmphex(read_word(f, 4), ==, 0x3456 ^ 0xa55a);
    g_assert_cmpuint(s->count, ==, 1); /* Active sampled word only. */
    g_assert_cmpuint(read_word(f, 0x2c), ==, 1);
    clock_update_hz(f->clock, 100000000);
    cpus_set_virtual_clock(start + 402239);
    g_assert_cmpuint(read_word(f, 0x30), ==, 0);
    cpus_set_virtual_clock(start + 402240);
    g_assert_cmphex(read_word(f, 4), ==, 0x789a ^ 0xa55a);
    g_assert_cmpuint(s->count, ==, 2);
    cleanup(f);
}

static void elapsed_clock_change(bool stop)
{
    SPIFixture *f = fixture(false);
    SPIOracle *s = attach_oracle(f);
    int64_t start;

    elapsed_prepare(f);
    for (unsigned i = 0; i < 6; i++) {
        write_word(f, 0, 0x1200 + i);
    }
    start = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    write_word(f, 0x3c, 1);
    cpus_set_virtual_clock(start + 3 * 2240 + 100);
    /* No MMIO/timer observation has yet retired the three old-rate words. */
    clock_update_hz(f->clock, stop ? 0 : 50000000);
    g_assert_cmpuint(s->count, ==, 3);
    g_assert_cmpuint(read_word(f, 0x30), ==, 3);
    for (unsigned i = 0; i < 3; i++) {
        g_assert_cmphex(read_word(f, 4), ==, (0x1200 + i) ^ 0xa55a);
    }
    /* The fourth word was already active when the input rate changed. */
    cpus_set_virtual_clock(start + 4 * 2240 - 1);
    g_assert_cmpuint(read_word(f, 0x30), ==, 0);
    cpus_set_virtual_clock(start + 4 * 2240);
    g_assert_cmphex(read_word(f, 4), ==, 0x1203 ^ 0xa55a);
    if (stop) {
        cpus_set_virtual_clock(start + 20000);
        g_assert_cmpuint(read_word(f, 0x30), ==, 0);
        g_assert_cmpuint(read_word(f, 0x2c), ==, 2);
        g_assert_cmpuint(s->count, ==, 4);
        clock_update_hz(f->clock, 100000000);
        cpus_set_virtual_clock(start + 22239);
        g_assert_cmpuint(read_word(f, 0x30), ==, 0);
        cpus_set_virtual_clock(start + 22240);
        g_assert_cmphex(read_word(f, 4), ==, 0x1204 ^ 0xa55a);
        cpus_set_virtual_clock(start + 24480);
    } else {
        cpus_set_virtual_clock(start + 13439);
        g_assert_cmpuint(read_word(f, 0x30), ==, 0);
        cpus_set_virtual_clock(start + 13440);
        g_assert_cmphex(read_word(f, 4), ==, 0x1204 ^ 0xa55a);
        cpus_set_virtual_clock(start + 17920);
    }
    g_assert_cmphex(read_word(f, 4), ==, 0x1205 ^ 0xa55a);
    g_assert_cmpuint(s->count, ==, 6);
    for (unsigned i = 0; i < 6; i++) {
        g_assert_cmphex(s->mosi[i], ==, 0x1200 + i);
    }
    qemu_clock_run_timers(QEMU_CLOCK_VIRTUAL);
    g_assert_cmpuint(s->count, ==, 6);
    cleanup(f);
}

static void test_elapsed_rate_change(void)
{
    elapsed_clock_change(false);
}

static void test_elapsed_clock_stop(void)
{
    elapsed_clock_change(true);
}

typedef struct AsyncOracle {
    unsigned requests, cancellations;
    uint64_t token;
} AsyncOracle;

static bool async_request(void *opaque, uint64_t token, uint32_t word,
                          unsigned bits)
{
    AsyncOracle *s = opaque;

    g_assert_cmphex(word, ==, 0x1234);
    g_assert_cmpuint(bits, ==, 16);
    s->requests++;
    s->token = token;
    return true;
}

static void async_cancel(void *opaque, uint64_t token)
{
    AsyncOracle *s = opaque;

    g_assert_cmpuint(token, ==, s->token);
    s->cancellations++;
}

static void test_elapsed_async(void)
{
    static const NikonSPIAsyncOps ops = {
        .request = async_request, .cancel = async_cancel,
    };
    SPIFixture *f = fixture(false);
    AsyncOracle peer = { 0 };
    uint64_t value;

    elapsed_prepare(f);
    nikon_spi_attach_async(f->dev, &ops, &peer);
    write_word(f, 0, 0x1234);
    write_word(f, 0x3c, 1);
    cpus_set_virtual_clock(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 100000);
    g_assert_cmpuint(read_word(f, 0x30), ==, 0);
    g_assert_cmpuint(peer.requests, ==, 0); /* MMIO cannot submit a peer clock. */
    qemu_clock_run_timers(QEMU_CLOCK_VIRTUAL);
    g_assert_cmpuint(peer.requests, ==, 1);
    g_assert_cmpint(memory_region_dispatch_read(f->regs, 4, &value, MO_32,
                                               MEMTXATTRS_UNSPECIFIED),
                    ==, MEMTX_ERROR); /* Time cannot manufacture a response. */
    nikon_spi_async_complete(f->dev, peer.token + 1, 0xffff);
    g_assert_cmpuint(read_word(f, 0x30), ==, 0);
    nikon_spi_async_complete(f->dev, peer.token, 0xabcd);
    g_assert_cmphex(read_word(f, 4), ==, 0xabcd);
    g_assert_cmpuint(peer.requests, ==, 1);
    nikon_spi_detach_async(f->dev, &peer);
    cleanup(f);
}

typedef struct RefillOracle {
    SPIFixture *f;
    unsigned count, depth, max_depth;
    bool cancel;
} RefillOracle;

static void refill(void *opaque, int line, int level)
{
    RefillOracle *s = opaque;

    if (level) {
        s->depth++;
        s->max_depth = MAX(s->max_depth, s->depth);
        if (s->count < 32) {
            g_assert_cmpint(memory_region_dispatch_write(s->f->regs, 0,
                                                        0x2000 + s->count++,
                                                        MO_32,
                                                        MEMTXATTRS_UNSPECIFIED),
                            ==, MEMTX_OK);
        }
        if (s->cancel) {
            g_assert_cmpint(memory_region_dispatch_write(s->f->regs, 0x3c, 0,
                                                        MO_32,
                                                        MEMTXATTRS_UNSPECIFIED),
                            ==, MEMTX_OK);
        }
        read_word(s->f, 0x14);
        s->depth--;
    }
}

static void elapsed_reentrant(bool timer, bool cancel)
{
    SPIFixture *f = fixture(true);
    SPIOracle *s = attach_oracle(f);
    RefillOracle feed = { .f = f, .cancel = cancel };
    qemu_irq demand = qemu_allocate_irq(refill, &feed, 0);

    elapsed_prepare(f);
    write_word(f, 0xc, 0x422);
    for (unsigned i = 0; i < 32; i++) {
        write_word(f, 0, 0x1200 + i);
    }
    write_word(f, 0x3c, 1);
    qdev_connect_gpio_out_named(f->dev, "dma", 0, demand);
    cpus_set_virtual_clock(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000000);
    if (timer) {
        /* IRQ sink reads must not recursively dispatch the next due word. */
        qemu_clock_run_timers(QEMU_CLOCK_VIRTUAL);
        g_assert_cmpuint(s->count, ==, 64);
        for (unsigned i = 0; i < 32; i++) {
            g_assert_cmphex(s->mosi[32 + i], ==, 0x2000 + i);
        }
    } else {
        /* Each completion refills through MMIO. One observation stays finite. */
        g_assert_cmpuint(read_word(f, 0x2c), ==, 32);
        g_assert_cmpuint(s->count, ==, cancel ? 1 : 32);
    }
    g_assert_cmpuint(feed.count, ==, cancel ? 1 : 32);
    g_assert_cmpuint(feed.max_depth, ==, 1);
    for (unsigned i = 0; i < (cancel ? 1 : 32); i++) {
        g_assert_cmphex(s->mosi[i], ==, 0x1200 + i);
    }
    device_cold_reset(f->dev);
    qdev_connect_gpio_out_named(f->dev, "dma", 0, f->observed[2]);
    qemu_free_irq(demand);
    qemu_clock_run_timers(QEMU_CLOCK_VIRTUAL);
    g_assert_cmpuint(s->count, ==, cancel ? 1 : timer ? 64 : 32);
    cleanup(f);
}

static void test_elapsed_reentrant_mmio(void)
{
    elapsed_reentrant(false, false);
}

static void test_elapsed_reentrant_timer(void)
{
    elapsed_reentrant(true, false);
}

static void test_elapsed_reentrant_cancel(void)
{
    elapsed_reentrant(false, true);
}

static void test_late_callback(void)
{
    SPIFixture *f = fixture(false);
    int64_t start;

    prepare(f);
    enqueue(f);
    start = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    write_word(f, 0x3c, 1);

    /*
     * Model a busy main loop without stepping each intermediate deadline.
     * The independent serial clock has completed all four frames by now.
     */
    cpus_set_virtual_clock(start + 100000);
    qemu_clock_run_timers(QEMU_CLOCK_VIRTUAL);
    g_assert_cmphex(read_word(f, 0x2c), ==, 0);
    g_assert_cmphex(read_word(f, 0x30), ==, 4);
    g_assert_cmphex(read_word(f, 0x24) & 0x10, ==, 0x10);

    /* An idle FIFO starts a new cadence instead of inheriting the old one. */
    write_word(f, 0, 0x5678);
    g_assert_cmphex(read_word(f, 0x2c), ==, 1);
    qemu_clock_run_timers(QEMU_CLOCK_VIRTUAL);
    g_assert_cmphex(read_word(f, 0x2c), ==, 1);
    cpus_set_virtual_clock(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 100000);
    qemu_clock_run_timers(QEMU_CLOCK_VIRTUAL);
    g_assert_cmphex(read_word(f, 0x2c), ==, 0);

    /* Disable and reset both discard an outstanding cadence deadline. */
    enqueue(f);
    write_word(f, 0x3c, 0);
    cpus_set_virtual_clock(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 100000);
    qemu_clock_run_timers(QEMU_CLOCK_VIRTUAL);
    g_assert_cmphex(read_word(f, 0x2c), ==, 4);
    device_cold_reset(f->dev);
    prepare(f);
    enqueue(f);
    write_word(f, 0x3c, 1);
    cpus_set_virtual_clock(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 100000);
    qemu_clock_run_timers(QEMU_CLOCK_VIRTUAL);
    g_assert_cmphex(read_word(f, 0x2c), ==, 0);
    g_assert_cmphex(read_word(f, 0x30), ==, 4);
    cleanup(f);
}

static void slave_prepare(SPIFixture *f, unsigned bits, unsigned flags)
{
    write_word(f, 0x60, 0);
    write_word(f, 0x0c, 0x400); /* Source-bound DMA, external slave clock. */
    write_word(f, 0x10, ((bits - 1) << 8) | flags);
    write_word(f, 0x4c, 1);
    write_word(f, 0x20, 0x1ff);
}

static void slave_enable(SPIFixture *f)
{
    write_word(f, 0x3c, 1);
    write_word(f, 0x64, 1);
    write_word(f, 0x28, 0x1ff);
}

static void reject_clock(SPIFixture *f, unsigned bits, bool sample_tx)
{
    uint32_t transmitted = 0xfeedface;
    uint32_t tx = read_word(f, 0x2c), rx = read_word(f, 0x30);
    uint32_t status = read_word(f, 0x14), pending = read_word(f, 0x24);
    bool levels[4];

    memcpy(levels, f->level, sizeof(levels));
    g_assert_false(nikon_spi_slave_clock(f->dev, 0xabcd, bits,
                                         sample_tx ? &transmitted : NULL));
    g_assert_cmphex(transmitted, ==, 0xfeedface);
    g_assert_cmphex(read_word(f, 0x2c), ==, tx);
    g_assert_cmphex(read_word(f, 0x30), ==, rx);
    g_assert_cmphex(read_word(f, 0x14), ==, status);
    g_assert_cmphex(read_word(f, 0x24), ==, pending);
    g_assert_cmpmem(f->level, sizeof(f->level), levels, sizeof(levels));
}

static void test_slave_word(void)
{
    SPIFixture *f = fixture(true);
    uint32_t transmitted = 0;

    slave_prepare(f, 16, 0);
    write_word(f, 0, 0xdead1234);
    slave_enable(f);
    g_assert_true(f->level[2]);
    g_assert_false(f->level[3]);
    /* Time alone never consumes an externally clocked FIFO. */
    cpus_set_virtual_clock(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000000);
    /* Elapsed virtual time plus MMIO still cannot supply an external clock. */
    g_assert_cmphex(read_word(f, 0x2c), ==, 1);
    g_assert_cmphex(read_word(f, 0x30), ==, 0);
    qemu_clock_run_timers(QEMU_CLOCK_VIRTUAL);
    g_assert_cmphex(read_word(f, 0x2c), ==, 1);
    g_assert_cmphex(read_word(f, 0x30), ==, 0);
    g_assert_true(nikon_spi_slave_clock(f->dev, 0xfeedbeef, 16, &transmitted));
    g_assert_cmphex(transmitted, ==, 0x1234);
    g_assert_cmphex(read_word(f, 0x2c), ==, 0);
    g_assert_cmphex(read_word(f, 0x30), ==, 1);
    g_assert_cmphex(read_word(f, 0x24) & 0x10, ==, 0x10);
    g_assert_true(f->level[0]);
    g_assert_false(f->level[1]);
    g_assert_true(f->level[3]);
    g_assert_cmphex(read_word(f, 4), ==, 0xbeef);
    g_assert_false(f->level[3]);
    write_word(f, 0x28, 0x1ff);
    g_assert_false(f->level[0]);
    g_assert_cmphex(read_word(f, 0x64), ==, 1);
    reject_clock(f, 16, true);

    write_word(f, 0x3c, 0);
    write_word(f, 0x10, 0x1f00);
    write_word(f, 0, 0x11223344);
    slave_enable(f);
    g_assert_true(nikon_spi_slave_clock(f->dev, 0xffeeddcc, 32, &transmitted));
    g_assert_cmphex(transmitted, ==, 0x11223344);
    g_assert_cmphex(read_word(f, 4), ==, 0xffeeddcc);
    cleanup(f);
}

static void test_slave_rx_only(void)
{
    SPIFixture *f = fixture(true);

    slave_prepare(f, 16, 0);
    write_word(f, 0x4c, 21);
    write_word(f, 0x20, 4);
    slave_enable(f);
    reject_clock(f, 16, true); /* No real transmit word can be sampled. */
    for (unsigned i = 0; i < 21; i++) {
        g_assert_true(nikon_spi_slave_clock(f->dev, 0x5500 + i, 16, NULL));
        g_assert_cmphex(read_word(f, 0x30), ==, i + 1);
        g_assert_cmpint(f->level[0], ==, i == 20);
        g_assert_true(f->level[3]);
    }
    g_assert_cmphex(read_word(f, 0x2c), ==, 0);
    g_assert_cmphex(read_word(f, 0x24) & 0x10, ==, 0);
    for (unsigned i = 0; i < 21; i++) {
        g_assert_cmphex(read_word(f, 4), ==, 0x5500 + i);
    }
    g_assert_false(f->level[3]);
    write_word(f, 0x28, 0x1ff);
    g_assert_false(f->level[0]);
    cleanup(f);
}

static void test_slave_backpressure(void)
{
    SPIFixture *f = fixture(true);
    uint32_t transmitted;

    slave_prepare(f, 8, 0);
    slave_enable(f);
    for (unsigned i = 0; i < 32; i++) {
        g_assert_true(nikon_spi_slave_clock(f->dev, i, 8, NULL));
    }
    g_assert_cmphex(read_word(f, 0x14) & 0x180, ==, 0x80);
    write_word(f, 0, 0x12345678);
    reject_clock(f, 8, true);
    g_assert_cmphex(read_word(f, 4), ==, 0);
    g_assert_true(nikon_spi_slave_clock(f->dev, 0xfeed, 8, &transmitted));
    g_assert_cmphex(transmitted, ==, 0x78);
    g_assert_cmphex(read_word(f, 0x30), ==, 32);

    /* Explicit RX discard can transmit even while the receive FIFO is full. */
    write_word(f, 0x3c, 0);
    write_word(f, 0x10, 0x0708);
    write_word(f, 0, 0x9abcdef0);
    slave_enable(f);
    g_assert_true(nikon_spi_slave_clock(f->dev, 0x12, 8, &transmitted));
    g_assert_cmphex(transmitted, ==, 0xf0);
    g_assert_cmphex(read_word(f, 0x30), ==, 32);
    reject_clock(f, 8, false); /* Empty TX plus RX discard is no transaction. */
    for (unsigned i = 1; i < 32; i++) {
        g_assert_cmphex(read_word(f, 4), ==, i);
    }
    g_assert_cmphex(read_word(f, 4), ==, 0xed);
    g_assert_cmphex(read_word(f, 0x14) & 0x100, ==, 0);
    cleanup(f);
}

static void test_slave_guards(void)
{
    SPIFixture *f = fixture(true);
    uint32_t transmitted;

    reject_clock(f, 16, false); /* Board reset keeps the port closed. */
    slave_prepare(f, 16, 0);
    write_word(f, 0, 0x1234);
    reject_clock(f, 16, true); /* Disabled. */
    write_word(f, 0x3c, 1);
    reject_clock(f, 16, true); /* No guest slave request. */
    write_word(f, 0x64, 1);
    reject_clock(f, 0, true);
    reject_clock(f, 8, true);
    reject_clock(f, 17, true);
    reject_clock(f, 33, true);
    write_word(f, 0x64, 0);
    reject_clock(f, 16, true);
    write_word(f, 0x64, 1);
    write_word(f, 0x3c, 0);
    write_word(f, 0x0c, 0x420);
    write_word(f, 0x3c, 1);
    reject_clock(f, 16, true); /* Master mode owns a timer, not this clock. */
    write_word(f, 0x3c, 0);
    write_word(f, 0x0c, 0x400);
    slave_enable(f);
    g_assert_true(nikon_spi_slave_clock(f->dev, 0xabcd, 16, &transmitted));
    g_assert_cmphex(transmitted, ==, 0x1234);
    g_assert_cmphex(read_word(f, 4), ==, 0xabcd);
    device_cold_reset(f->dev);
    cpus_set_virtual_clock(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000000);
    qemu_clock_run_timers(QEMU_CLOCK_VIRTUAL);
    reject_clock(f, 16, true);
    g_assert_cmphex(read_word(f, 0x2c), ==, 0);
    g_assert_cmphex(read_word(f, 0x30), ==, 0);
    slave_prepare(f, 16, 0);
    write_word(f, 0, 0x5678);
    slave_enable(f);
    g_assert_true(nikon_spi_slave_clock(f->dev, 0xedcb, 16, &transmitted));
    g_assert_cmphex(transmitted, ==, 0x5678);
    g_assert_cmphex(read_word(f, 4), ==, 0xedcb);
    cleanup(f);
}

static void dma_write(MemoryRegion *regs, hwaddr offset, uint32_t value)
{
    g_assert_cmpint(memory_region_dispatch_write(regs, offset, value, MO_32,
                                                MEMTXATTRS_UNSPECIFIED),
                    ==, MEMTX_OK);
    aio_poll(qemu_get_aio_context(), false);
}

static uint32_t dma_read(MemoryRegion *regs, hwaddr offset)
{
    uint64_t value;

    g_assert_cmpint(memory_region_dispatch_read(regs, offset, &value, MO_32,
                                               MEMTXATTRS_UNSPECIFIED),
                    ==, MEMTX_OK);
    return value;
}

static void dma_cadence(unsigned words, bool late)
{
    const hwaddr spi_address = 0x2c0a0300, ram_address = 0x80000000;
    SPIFixture *f = fixture(true);
    DeviceState *dma = qdev_new(TYPE_NIKON_HDMAC);
    MemoryRegion ram, *regs;
    uint8_t *bytes;
    int64_t start, deadline;
    const int64_t period = 2240; /* 16 bits, DIV6, explicit 100 MHz input. */
    const unsigned rx_offset = 0x2000;

    object_property_add_child(object_get_objects_root(), "hdmac[*]",
                              OBJECT(dma));
    qdev_prop_set_uint32(dma, "bank", 1);
    memory_region_init_ram_flags_nomigrate(&ram, OBJECT(dma), "spi-dma-ram",
                                           0x4000, 0, &error_abort);
    memory_region_add_subregion(get_system_memory(), ram_address, &ram);
    memory_region_add_subregion(get_system_memory(), spi_address, f->regs);
    g_assert_true(sysbus_realize(SYS_BUS_DEVICE(dma), &error_abort));
    device_cold_reset(dma);
    regs = sysbus_mmio_get_region(SYS_BUS_DEVICE(dma), 0);
    qdev_connect_gpio_out_named(f->dev, "dma", 0,
                                qdev_get_gpio_in(dma, 9));
    qdev_connect_gpio_out_named(f->dev, "dma", 1,
                                qdev_get_gpio_in(dma, 10));
    clock_update_hz(f->clock, 100000000);
    bytes = memory_region_get_ram_ptr(&ram);
    memset(bytes, 0xff, 0x4000);
    for (unsigned i = 0; i < words; i++) {
        stw_le_p(bytes + 2 * i, 0x1200 + i);
    }
    prepare(f);
    write_word(f, 8, 6);
    write_word(f, 0xc, 0x43b);
    write_word(f, 0x4c, 0x1004);
    /* Prime the real FIFO so demand starts low before either timer runs. */
    for (unsigned i = 0; i < 32; i++) {
        write_word(f, 0, lduw_le_p(bytes + 2 * i));
    }
    dma_write(regs, 0, 0x90000000);
    dma_write(regs, 0x18, ram_address + 64);
    dma_write(regs, 0x1c, spi_address);
    /* Halfwords, fixed peripheral destination. */
    dma_write(regs, 0x14, 0x05180000);
    dma_write(regs, 0x10, 0x89000000 | (words - 33));
    dma_write(regs, 0x28, spi_address + 4);
    dma_write(regs, 0x2c, ram_address + rx_offset);
    dma_write(regs, 0x24, 0x06180000); /* Halfwords, fixed peripheral source. */
    dma_write(regs, 0x20, 0x8a000000 | (words - 1));
    start = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    deadline = start + period * (words + 2);
    write_word(f, 0x3c, 1);
    if (late) {
        /* Dispatch was delayed, not the independent SPI/DMA hardware clocks. */
        cpus_set_virtual_clock(deadline);
        qemu_clock_run_timers(QEMU_CLOCK_VIRTUAL);
    } else {
        for (int64_t now = start + 10; now <= deadline; now += 10) {
            cpus_set_virtual_clock(now);
            qemu_clock_run_timers(QEMU_CLOCK_VIRTUAL);
        }
    }
    /* Verify actual data and DMA termination, without TX-DONE assumptions. */
    g_assert_cmphex(dma_read(regs, 0x14) & 0x70000, ==, 0x50000);
    g_assert_cmphex(dma_read(regs, 0x24) & 0x70000, ==, 0x50000);
    g_assert_cmphex(dma_read(regs, 0x18), ==, ram_address + 2 * words);
    g_assert_cmphex(dma_read(regs, 0x2c), ==,
                    ram_address + rx_offset + 2 * words);
    g_assert_cmpuint(read_word(f, 0x2c), ==, 0);
    g_assert_cmpuint(read_word(f, 0x30), ==, 0);
    g_assert_cmphex(read_word(f, 0x14) & 0x100, ==, 0);
    for (unsigned i = 0; i < 2 * words; i++) {
        /* Disconnected SSI MISO. */
        g_assert_cmphex(bytes[rx_offset + i], ==, 0);
    }
    g_assert_cmphex(bytes[rx_offset + 2 * words], ==, 0xff);
    qdev_connect_gpio_out_named(f->dev, "dma", 0, NULL);
    qdev_connect_gpio_out_named(f->dev, "dma", 1, NULL);
    qdev_unrealize(dma);
    memory_region_del_subregion(get_system_memory(), f->regs);
    memory_region_del_subregion(get_system_memory(), &ram);
    drain_call_rcu();
    object_unparent(OBJECT(dma));
    object_unref(OBJECT(dma));
    drain_call_rcu();
    cleanup(f);
}

static void test_dma_fine(void)
{
    dma_cadence(37, false);
}

static void test_dma_late_tail(void)
{
    dma_cadence(37, true);
}

static void test_dma_late_long(void)
{
    dma_cadence(3713, true);
}

static void dma_reentrant_control(bool cancel)
{
    const hwaddr ram_address = 0x80000000, dma_address = 0x2c030000;
    DeviceState *dma = qdev_new(TYPE_NIKON_HDMAC);
    MemoryRegion ram, *regs;
    uint8_t *bytes;
    qemu_irq demand;
    uint32_t stopped_source;

    object_property_add_child(object_get_objects_root(), "hdmac[*]",
                              OBJECT(dma));
    qdev_prop_set_uint32(dma, "bank", 1);
    memory_region_init_ram_flags_nomigrate(&ram, OBJECT(dma), "dma-control-ram",
                                           0x1000, 0, &error_abort);
    memory_region_add_subregion(get_system_memory(), ram_address, &ram);
    g_assert_true(sysbus_realize(SYS_BUS_DEVICE(dma), &error_abort));
    device_cold_reset(dma);
    regs = sysbus_mmio_get_region(SYS_BUS_DEVICE(dma), 0);
    memory_region_add_subregion(get_system_memory(), dma_address, regs);
    bytes = memory_region_get_ram_ptr(&ram);
    stl_le_p(bytes, cancel ? 0 : 0x91000000); /* DE cancellation or DH pause. */
    stl_le_p(bytes + 4, 0x90000000);
    dma_write(regs, 0, 0x90000000);
    dma_write(regs, 0x18, ram_address);
    dma_write(regs, 0x1c, dma_address);
    /* Two words to fixed own control MMIO. */
    dma_write(regs, 0x14, 0x09180000);
    dma_write(regs, 0x10, 0x89000001);
    demand = qdev_get_gpio_in(dma, 9);
    qemu_set_irq(demand, 1);

    stopped_source = dma_read(regs, 0x18);
    if (cancel) {
        g_assert_cmphex(dma_read(regs, 0), ==, 0);
        g_assert_cmphex(dma_read(regs, 0x14) & 0x70000, ==, 0x20000);
        g_assert_cmphex(dma_read(regs, 0x10) & 0x80000000, ==, 0);
        /* Still-high demand must not resurrect cancelled work. */
        qemu_set_irq(demand, 1);
        cpus_set_virtual_clock(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 100000);
        qemu_clock_run_timers(QEMU_CLOCK_VIRTUAL);
        g_assert_cmphex(dma_read(regs, 0), ==, 0);
        g_assert_cmphex(dma_read(regs, 0x18), ==, stopped_source);
        g_assert_cmphex(dma_read(regs, 0x14) & 0x70000, ==, 0x20000);
    } else {
        g_assert_cmphex(dma_read(regs, 0), ==, 0x91000000);
        g_assert_cmphex(stopped_source, ==, ram_address + 4);
        g_assert_cmphex(dma_read(regs, 0x10) & 0x80000000, ==, 0x80000000);
        g_assert_cmphex(dma_read(regs, 0x14) & 0x70000, ==, 0);
        /* DH clear resumes the same high request, without a new DREQ edge. */
        dma_write(regs, 0, 0x90000000);
        g_assert_cmphex(dma_read(regs, 0), ==, 0x90000000);
        g_assert_cmphex(dma_read(regs, 0x18), ==, ram_address + 8);
        g_assert_cmphex(dma_read(regs, 0x10) & 0x80000000, ==, 0);
        g_assert_cmphex(dma_read(regs, 0x14) & 0x70000, ==, 0x50000);
    }
    qemu_set_irq(demand, 0);
    qdev_unrealize(dma);
    memory_region_del_subregion(get_system_memory(), regs);
    memory_region_del_subregion(get_system_memory(), &ram);
    drain_call_rcu();
    object_unparent(OBJECT(dma));
    object_unref(OBJECT(dma));
    drain_call_rcu();
}

static void test_dma_reentrant_pause(void)
{
    dma_reentrant_control(false);
}

static void test_dma_reentrant_cancel(void)
{
    dma_reentrant_control(true);
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
    type_register_static(&oracle_type);
    vm_start();
    g_test_add_func("/nikon/spi/elapsed/observations", test_elapsed_observations);
    g_test_add_func("/nikon/spi/elapsed/invalid", test_elapsed_invalid);
    g_test_add_func("/nikon/spi/elapsed/cancel-clock", test_elapsed_cancel_clock);
    g_test_add_func("/nikon/spi/elapsed/rate-change", test_elapsed_rate_change);
    g_test_add_func("/nikon/spi/elapsed/clock-stop", test_elapsed_clock_stop);
    g_test_add_func("/nikon/spi/elapsed/async", test_elapsed_async);
    g_test_add_func("/nikon/spi/elapsed/reentrant-mmio",
                    test_elapsed_reentrant_mmio);
    g_test_add_func("/nikon/spi/elapsed/reentrant-timer",
                    test_elapsed_reentrant_timer);
    g_test_add_func("/nikon/spi/elapsed/reentrant-cancel",
                    test_elapsed_reentrant_cancel);
    g_test_add_func("/nikon/spi/late-callback-cadence", test_late_callback);
    g_test_add_func("/nikon/spi/slave/word", test_slave_word);
    g_test_add_func("/nikon/spi/slave/rx-only", test_slave_rx_only);
    g_test_add_func("/nikon/spi/slave/backpressure", test_slave_backpressure);
    g_test_add_func("/nikon/spi/slave/guards", test_slave_guards);
    g_test_add_func("/nikon/spi/dma/fine", test_dma_fine);
    g_test_add_func("/nikon/spi/dma/late-tail", test_dma_late_tail);
    g_test_add_func("/nikon/spi/dma/late-long", test_dma_late_long);
    g_test_add_func("/nikon/spi/dma/reentrant-pause", test_dma_reentrant_pause);
    g_test_add_func("/nikon/spi/dma/reentrant-cancel",
                    test_dma_reentrant_cancel);
    result = g_test_run();
    qemu_cleanup(result);
    unlink(socket);
    rmdir(directory);
    return result;
}
