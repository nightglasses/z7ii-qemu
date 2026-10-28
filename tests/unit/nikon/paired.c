/* Public paired-device asynchronous ownership and publication boundaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/bswap.h"
#include "qemu/main-loop.h"
#include "qemu/thread.h"
#include "qemu/timer.h"
#include "block/thread-pool.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/sysbus.h"
#include "hw/nikon/devices.h"
#include "hw/nikon/paired.h"
#include "system/address-spaces.h"
#include "system/cpu-timers.h"
#include "system/runstate.h"
#include "system/system.h"

int (*qemu_main)(void);

#define RAM_BASE 0x10000000
#define RAM_SIZE 0x20000
#define MAIN_OFFSET 0x10000
#define GRID_OFFSET 0x16000

typedef struct PairedFixture {
    DeviceState *dev, *gate;
    MemoryRegion ram;
    uint8_t *bytes;
    qemu_irq irq;
    bool level;
    bool reset_on_irq;
    unsigned rises;
    NikonRawEncoding encoding;
} PairedFixture;

typedef struct PoolBarrier {
    QemuSemaphore entered, release;
    bool block, done;
} PoolBarrier;

static int barrier_worker(void *opaque)
{
    PoolBarrier *b = opaque;

    qemu_sem_post(&b->entered);
    if (b->block) {
        qemu_sem_wait(&b->release);
    }
    return 0;
}

static void barrier_completed(void *opaque, int ret)
{
    PoolBarrier *b = opaque;

    g_assert_cmpint(ret, ==, 0);
    b->done = true;
}

static void barrier_submit(PoolBarrier *b, bool block)
{
    *b = (PoolBarrier) { .block = block };
    qemu_sem_init(&b->entered, 0);
    qemu_sem_init(&b->release, 0);
    thread_pool_submit_aio(barrier_worker, b, barrier_completed, b);
}

static void blocker_start(PoolBarrier *b)
{
    int64_t deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;

    barrier_submit(b, true);
    while (qemu_sem_timedwait(&b->entered, 0)) {
        g_assert_cmpint(g_get_monotonic_time(), <, deadline);
        aio_poll(qemu_get_aio_context(), false);
        g_thread_yield();
    }
}

static void worker_returned(PoolBarrier *blocker, PoolBarrier *marker)
{
    /* The single-worker FIFO makes this marker a public host-work barrier.
     * No main-Aio polling occurs between releasing the blocker and this wait.
     */
    barrier_submit(marker, false);
    qemu_sem_post(&blocker->release);
    g_assert_cmpint(qemu_sem_timedwait(&marker->entered, 5000), ==, 0);
    g_assert_false(blocker->done);
    g_assert_false(marker->done);
}

static void barrier_clear(PoolBarrier *b)
{
    g_assert_true(b->done);
    qemu_sem_destroy(&b->entered);
    qemu_sem_destroy(&b->release);
}

static void irq_level(void *opaque, int n, int level)
{
    PairedFixture *f = opaque;

    f->rises += level && !f->level;
    f->level = !!level;
    if (level && f->reset_on_irq) {
        f->reset_on_irq = false;
        device_cold_reset(f->dev);
    }
}

static void write_port(PairedFixture *f, unsigned offset, uint32_t value)
{
    MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(f->dev), 0);

    g_assert_cmpint(memory_region_dispatch_write(mr, offset, value, MO_32,
                        MEMTXATTRS_UNSPECIFIED), ==, MEMTX_OK);
}

static uint32_t read_port(PairedFixture *f, unsigned offset)
{
    MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(f->dev), 0);
    uint64_t value;

    g_assert_cmpint(memory_region_dispatch_read(mr, offset, &value, MO_32,
                        MEMTXATTRS_UNSPECIFIED), ==, MEMTX_OK);
    return value;
}

static uint64_t counter(PairedFixture *f, const char *name)
{
    return object_property_get_uint(OBJECT(f->dev), name, &error_abort);
}

static bool runtime_has(PairedFixture *f, const char *text)
{
    g_autofree char *runtime = object_property_get_str(OBJECT(f->dev),
                                                     "runtime", &error_abort);

    return strstr(runtime, text) != NULL;
}

static void poll_until(PairedFixture *f, const char *text,
                       PoolBarrier *blocker, PoolBarrier *marker)
{
    int64_t deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;

    while (!runtime_has(f, text) || !blocker->done || !marker->done) {
        g_assert_cmpint(g_get_monotonic_time(), <, deadline);
        aio_poll(qemu_get_aio_context(), false);
        if (runstate_is_running()) {
            qemu_clock_run_timers(QEMU_CLOCK_VIRTUAL);
        }
        g_thread_yield();
    }
}

static void advance(int64_t ns)
{
    cpus_set_virtual_clock(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + ns);
    qemu_clock_run_timers(QEMU_CLOCK_VIRTUAL);
}

static void matrix(PairedFixture *f, unsigned offset, unsigned bits,
                    bool low_first, const int values[9])
{
    uint32_t mask = (1U << bits) - 1;

    write_port(f, offset, values[0]);
    for (unsigned i = 1; i < 9; i += 2) {
        uint32_t a = values[i] & mask, b = values[i + 1] & mask;

        write_port(f, offset + 2 * (i + 1),
                   low_first ? a | b << 16 : a << 16 | b);
    }
}

static void configure_raw(PairedFixture *f, unsigned raw_words)
{
    static const struct { unsigned offset; uint32_t value; } controls[] = {
        { 0x40, 5 }, { 0x44, 0 }, { 0x48, 0x00400024 }, { 0x4c, 1 },
        { 0x84, 1 }, { 0x88, 0 }, { 0x8c, 3 }, { 0x98, 0x400 },
        { 0x9c, 35 }, { 0xa0, 3 }, { 0xa8, 4 }, { 0xb8, 1 }, { 0xbc, 1 },
        { 0xc4, 64 }, { 0xd4, 0x0c060202 }, { 0xd8, 9 }, { 0xdc, 0x30300 },
        { 0xe0, 0x158 }, { 0xe8, 0 }, { 0xf0, 0x8e }, { 0xf8, 1 },
        { 0x100, 1 }, { 0x108, 3 }, { 0x10c, 0 }, { 0x11c, 1 }, { 0x120, 1 },
        { 0x124, 0 }, { 0x12c, 1 }, { 0x138, 0 }, { 0x204, 0x00240040 },
        { 0x30c, 96 }, { 0x394, 96 }, { 0x3d8, 0 }, { 0x400, 0x0004001c },
        { 0x404, 1 }, { 0x408, 0 }, { 0x40c, 0x00380004 }, { 0x410, 0 },
        { 0x420, 1 }, { 0x424, 0 }, { 0x428, 1 }, { 0x42c, 3 }, { 0x4c8, 64 },
        { 0xb14, 0x80000000 }, { 0xb60, 0x80000000 }, { 0xb90, 0x80000000 },
    };
    static const uint32_t denoise[] = {
        7, 0x00010003, 0x03010000, 0x1f1f1f1f, 0, 0x03020001, 0,
    };
    static const uint32_t detail[] = {
        0x00060301, 0, 0x40040600, 0x04080808,
        0x00040010, 0, 0x001f0000, 0x1f100804,
    };
    static const uint32_t adaptive[] = {
        0x0032001e, 0x003c0fff, 0x04000fff, 0x081f0c0e,
        0x04000000, 0x08080808, 0x00000808, 0x0af00020,
    };
    static const uint32_t radial[] = {
        270, 0x00120020, 0x00030205, 0, 0x0520, 0x0101,
    };
    static const unsigned post[][2] = {
        { 0xb38, 0xb58 }, { 0xb94, 0xbbc },
        { 0xd20, 0xd9c }, { 0xdc0, 0xdec },
    };
    static const int a[] = { 1024, 0, 0, 0, 512, 0, 0, 0, 256 };
    static const int b[] = { 512, 0, 0, 0, 256, -256, -256, 256, 0 };
    static const int c[] = { 1024, 0, 0, 0, 1024, 0, 0, 0, 1024 };
    uint32_t color[51] = { 0 };

    for (unsigned i = 0; i < ARRAY_SIZE(controls); i++) {
        write_port(f, controls[i].offset, controls[i].value);
    }
    for (unsigned phase = 0; phase < 4; phase++) {
        uint32_t raw = RAM_BASE + phase * 0x1000;
        uint32_t main = RAM_BASE + MAIN_OFFSET + (phase % 2) * 0x2000;

        if (phase < raw_words) {
            write_port(f, 0x218 + 4 * phase, raw);
        }
        if (phase + 4 < raw_words) {
            write_port(f, 0x310 + 4 * phase, raw);
        }
        write_port(f, 0x398 + 4 * phase,
                   RAM_BASE + GRID_OFFSET + phase * 0x1000);
        write_port(f, 0x488 + 4 * phase, main);
        write_port(f, 0x498 + 4 * phase, main + 64 * 28);
    }
    for (unsigned i = 0; i < ARRAY_SIZE(denoise); i++) {
        write_port(f, 0x660 + 4 * i, denoise[i]);
    }
    for (unsigned i = 0; i < ARRAY_SIZE(detail); i++) {
        write_port(f, 0x73c + 4 * i, detail[i]);
        write_port(f, 0x75c + 4 * i, adaptive[i]);
    }
    for (unsigned i = 0; i < ARRAY_SIZE(radial); i++) {
        write_port(f, 0x724 + 4 * i, radial[i]);
    }
    color[0x0c / 4] = color[0x1c / 4] = 0x02020202;
    color[0x54 / 4] = 0x0bb80190;
    color[0x58 / 4] = 0x10808030;
    color[0x68 / 4] = 0x02020302;
    color[0x78 / 4] = 0x02020202;
    color[0xac / 4] = 0x00050009;
    color[0xbc / 4] = 2 << 16;
    color[0xc4 / 4] = 0x0804;
    for (unsigned i = 0; i < ARRAY_SIZE(color); i++) {
        write_port(f, i == 50 ? 0x9d8 : 0x900 + 4 * i, color[i]);
    }
    for (unsigned group = 0; group < ARRAY_SIZE(post); group++) {
        for (unsigned offset = post[group][0]; offset <= post[group][1];
             offset += 4) {
            uint32_t value = offset == 0xbac ? 256 :
                offset == 0xdd4 ? 3 : offset == 0xdd8 ? 5 : 0;

            if (offset == 0xdc0 || offset == 0xdc4) {
                value = 10 << 8;
            } else if (offset == 0xdd0) {
                value = 0x080401;
            } else if (offset == 0xde4) {
                value = 0x8080;
            } else if (offset == 0xde8) {
                value = 0x010101;
            }
            write_port(f, offset, value);
        }
    }
    for (unsigned i = 0; i < 16; i++) {
        write_port(f, 0x1900 + 4 * i, 0x01000100);
    }
    for (unsigned i = 0; i < 17; i++) {
        write_port(f, 0x1a08 + 4 * i, i == 16 ? 256 : 0x01000100);
    }
    matrix(f, 0xb00, 13, false, a);
    matrix(f, 0xb64, 12, false, b);
    matrix(f, 0xb94, 13, true, c);
    write_port(f, 0xb44, 0x2e8b2e8b);
    write_port(f, 0xb48, 0x003b0027);
}

static void configure(PairedFixture *f)
{
    configure_raw(f, 8);
}

static PairedFixture *fixture_new_raw(unsigned raw_words, bool stream)
{
    PairedFixture *f = g_new0(PairedFixture, 1);
    uint8_t calibration[0x5e00] = { 0 };
    g_autofree char *path = NULL;
    int fd = g_file_open_tmp("nikon-paired-XXXXXX", &path, NULL);

    g_assert_cmpint(fd, >=, 0);
    close(fd);
    stl_le_p(calibration + 0xb14, 0x80000000);
    stl_le_p(calibration + 0xb60, 0x80000000);
    stl_le_p(calibration + 0xb90, 0x80000000);
    g_assert_true(g_file_set_contents(path, (const char *)calibration,
                                      sizeof(calibration), NULL));
    memory_region_init_ram_flags_nomigrate(&f->ram, NULL, "paired-test-ram",
                                           RAM_SIZE, 0, &error_abort);
    memory_region_add_subregion(get_system_memory(), RAM_BASE, &f->ram);
    f->bytes = memory_region_get_ram_ptr(&f->ram);
    memset(f->bytes, 0xa5, RAM_SIZE);
    for (unsigned i = 0; i < 96 * 36; i += 3) {
        memcpy(f->bytes + i, "\x00\x08\x80", 3); /* Two RAW12 samples2048. */
    }
    f->dev = qdev_new(TYPE_NIKON_PAIRED_EVENT_BANKS);
    qdev_prop_set_string(f->dev, "calibration", path);
    if (stream) {
        f->gate = qdev_new(TYPE_NIKON_EVENT_GATE);
        g_assert_true(sysbus_realize(SYS_BUS_DEVICE(f->gate), &error_abort));
        device_cold_reset(f->gate);
        object_property_set_link(OBJECT(f->dev), "gate", OBJECT(f->gate),
                                  &error_abort);
    }
    f->irq = qemu_allocate_irq(irq_level, f, 0);
    sysbus_connect_irq(SYS_BUS_DEVICE(f->dev), 0, f->irq);
    g_assert_true(sysbus_realize(SYS_BUS_DEVICE(f->dev), &error_abort));
    g_assert_cmpint(unlink(path), ==, 0);
    device_cold_reset(f->dev);
    if (stream) {
        /* GPIO links resolve through QOM paths. Realize the receiver first so
         * its input has a rooted path, as it does in the machine wiring.
         */
        qdev_connect_gpio_out_named(f->gate, "input-capacity", 0,
            qdev_get_gpio_in_named(f->dev, "input-capacity", 0));
        g_assert_true(object_property_get_link(OBJECT(f->gate),
            "input-capacity[0]", &error_abort) ==
            OBJECT(qdev_get_gpio_in_named(f->dev, "input-capacity", 0)));
    }
    configure_raw(f, raw_words);
    vm_start();
    return f;
}

static PairedFixture *fixture_new(void)
{
    return fixture_new_raw(8, false);
}

static void fixture_free(PairedFixture *f)
{
    if (qdev_is_realized(f->dev)) {
        qdev_unrealize(f->dev);
    }
    if (f->gate) {
        qdev_connect_gpio_out_named(f->gate, "input-capacity", 0, NULL);
        qdev_unrealize(f->gate);
    }
    object_unparent(OBJECT(f->dev));
    object_unref(OBJECT(f->dev));
    if (f->gate) {
        object_unparent(OBJECT(f->gate));
        object_unref(OBJECT(f->gate));
    }
    qemu_free_irq(f->irq);
    memory_region_del_subregion(get_system_memory(), &f->ram);
    object_unparent(OBJECT(&f->ram));
    g_free(f);
}

static void unpublished(PairedFixture *f)
{
    g_assert_cmpuint(counter(f, "completed"), ==, 0);
    g_assert_cmphex(read_port(f, 0x24), ==, 0);
    g_assert_false(f->level);
    g_assert_cmpuint(f->rises, ==, 0);
    for (unsigned i = MAIN_OFFSET; i < RAM_SIZE; i++) {
        g_assert_cmphex(f->bytes[i], ==, 0xa5);
    }
}

static void admit(PairedFixture *f)
{
    NikonPairedRawFrame frame = {
        .generation = 1, .address = RAM_BASE, .width = 64, .height = 36,
        .stride = 96, .bits = 12, .encoding = f->encoding,
        .transfer = { .codec = NIKON_RAW_LINEAR }, .slot = 0,
    };
    NikonPairedGridFrame grid = {
        .generation = 1, .address = RAM_BASE + GRID_OFFSET,
        .width = 60, .height = 40, .stride = 96,
    };

    write_port(f, 0x10, 1);
    write_port(f, 8, 1);
    nikon_paired_grid_frame(f->dev, &grid);
    nikon_paired_frame(f->dev, &frame);
    g_assert_cmpuint(counter(f, "rejected"), ==, 0);
    g_assert_true(runtime_has(f, "bank0 request=2 active=1"));
    unpublished(f);
    advance(999999);
    g_assert_true(runtime_has(f, "inflight=0"));
    unpublished(f);
    advance(1);
    g_assert_true(runtime_has(f, "inflight=1"));
    g_assert_true(runtime_has(f, "compute=1"));
    unpublished(f);
}

static void published_pixels(PairedFixture *f)
{
    for (unsigned y = 0; y < 42; y++) {
        for (unsigned x = 0; x < 64; x++) {
            unsigned expected = x >= 56 ? 0xa5 : y < 28 ? 128 :
                                x & 1 ? 96 : 144;

            g_assert_cmpuint(f->bytes[MAIN_OFFSET + y * 64 + x], ==, expected);
        }
    }
    for (unsigned y = 0; y < 40; y++) {
        for (unsigned x = 0; x < 96; x++) {
            g_assert_cmpuint(f->bytes[GRID_OFFSET + y * 96 + x], ==,
                             0xa5);
        }
    }
}

static void published(PairedFixture *f)
{
    g_assert_cmpuint(counter(f, "completed"), ==, 1);
    g_assert_cmpuint(counter(f, "rejected"), ==, 0);
    g_assert_cmpuint(counter(f, "bytes"), ==, 56 * 28 * 3 / 2);
    g_assert_cmphex(read_port(f, 0x24), ==, 0x1002);
    g_assert_true(f->level);
    g_assert_cmpuint(f->rises, ==, 1);
    published_pixels(f);
}

static void test_async_completion(gconstpointer opaque)
{
    unsigned flags = GPOINTER_TO_UINT(opaque);
    bool pause = flags & 1;
    PoolBarrier blocker, marker;
    PairedFixture *f = fixture_new();

    if (flags & 2) {
        f->encoding = NIKON_RAW_RGGB;
    }
    blocker_start(&blocker);
    admit(f);
    memset(f->bytes, 0, 96 * 36); /* An admitted job owns a frozen RAW copy. */
    if (pause) {
        g_assert_cmpint(vm_stop(RUN_STATE_PAUSED), ==, 0);
    }
    worker_returned(&blocker, &marker);
    unpublished(f); /* The host worker returned without any main-Aio callback. */
    g_assert_true(runtime_has(f, "compute=1"));
    if (pause) {
        poll_until(f, "compute=2", &blocker, &marker);
        unpublished(f);
        g_assert_true(runtime_has(f, "inflight=1"));
        vm_start();
    }
    poll_until(f, "inflight=0", &blocker, &marker);
    published(f);
    advance(2000000);
    published(f);
    write_port(f, 0x24, UINT32_MAX);
    g_assert_false(f->level);
    g_assert_cmpuint(counter(f, "completed"), ==, 1);
    barrier_clear(&blocker);
    barrier_clear(&marker);
    fixture_free(f);
}

static void test_async_cancel(gconstpointer opaque)
{
    unsigned flags = GPOINTER_TO_UINT(opaque);
    unsigned mode = flags & 3;
    PoolBarrier blocker, marker;
    PairedFixture *f = fixture_new();

    if (flags & 4) {
        f->encoding = NIKON_RAW_RGGB;
    }
    blocker_start(&blocker);
    admit(f);
    if (mode == 2) {
        g_assert_cmpint(vm_stop(RUN_STATE_PAUSED), ==, 0);
        worker_returned(&blocker, &marker);
        poll_until(f, "compute=2", &blocker, &marker);
        unpublished(f);
        device_cold_reset(f->dev);
    } else {
        if (mode == 1) {
            write_port(f, 0xb58, 32 << 16); /* Valid new numerical config. */
        } else {
            device_cold_reset(f->dev);
        }
        g_assert_true(runtime_has(f, "inflight=1"));
        unpublished(f);
        worker_returned(&blocker, &marker);
    }
    poll_until(f, "inflight=0", &blocker, &marker);
    if (mode == 1) {
        /* A numerical upload does not cancel an already owned snapshot,
         * even when its host computation was still queued at publication.
         */
        published(f);
        g_assert_cmpuint(counter(f, "cancelled"), ==, 0);
    } else {
        unpublished(f);
        g_assert_cmpuint(counter(f, "cancelled"), ==, 1);
    }
    device_cold_reset(f->dev); /* Also cancel a valid re-admission's future timer. */
    vm_start();
    advance(2000000);
    if (mode == 1) {
        g_assert_cmpuint(counter(f, "completed"), ==, 1);
        g_assert_cmphex(read_port(f, 0x24), ==, 0);
        g_assert_false(f->level);
        g_assert_cmpuint(f->rises, ==, 1);
    } else {
        unpublished(f);
    }
    barrier_clear(&blocker);
    barrier_clear(&marker);
    fixture_free(f);
}

static void test_async_unrealize(gconstpointer opaque)
{
    PoolBarrier blocker, marker;
    PairedFixture *f = fixture_new();

    if (GPOINTER_TO_UINT(opaque)) {
        f->encoding = NIKON_RAW_RGGB;
    }
    blocker_start(&blocker);
    admit(f);
    worker_returned(&blocker, &marker);
    unpublished(f);
    qdev_unrealize(f->dev); /* Drains main-Aio owner cleanup before returning. */
    g_assert_true(runtime_has(f, "inflight=0"));
    unpublished(f);
    poll_until(f, "inflight=0", &blocker, &marker);
    barrier_clear(&blocker);
    barrier_clear(&marker);
    fixture_free(f);
    advance(2000000);
}

static void test_stage_cancel(gconstpointer opaque)
{
    unsigned target = GPOINTER_TO_UINT(opaque) & 7;
    bool unrealize = GPOINTER_TO_UINT(opaque) & 8;
    bool middle_post = GPOINTER_TO_UINT(opaque) & 16;
    PoolBarrier blockers[12], markers[12];
    unsigned hop = 0;
    PairedFixture *f = fixture_new();
    g_autofree char *text = g_strdup_printf(" stage=%u remaining=", target);

    f->encoding = NIKON_RAW_RGGB;
    if (middle_post) {
        write_port(f, 0xb58, (32 << 16) | 16);
        write_port(f, 0xd28, (8 << 8) | 4);
    }
    blocker_start(&blockers[0]);
    admit(f);
    g_assert_true(runtime_has(f, " stage=0 remaining=1"));
    while (!runtime_has(f, text) ||
           (middle_post && !runtime_has(f, "postpass=2"))) {
        g_assert_cmpuint(hop + 1, <, ARRAY_SIZE(blockers));
        worker_returned(&blockers[hop], &markers[hop]);
        /* Queue the next blocker before delivering the current stage's Aio
         * callbacks. POST may join and resubmit another pass with the same
         * stage number. The single-worker FIFO marker, rather than a presumed
         * enum increment, proves every prior child callback was delivered.
         */
        blocker_start(&blockers[hop + 1]);
        poll_until(f, "inflight=1", &blockers[hop], &markers[hop]);
        hop++;
        unpublished(f);
        g_assert_true(runtime_has(f, "inflight=1"));
    }
    if (unrealize) {
        worker_returned(&blockers[hop], &markers[hop]);
        unpublished(f);
        qdev_unrealize(f->dev);
    } else {
        device_cold_reset(f->dev);
        g_assert_true(runtime_has(f, "inflight=1"));
        unpublished(f);
        worker_returned(&blockers[hop], &markers[hop]);
    }
    poll_until(f, "inflight=0", &blockers[hop], &markers[hop]);
    unpublished(f);
    g_assert_cmpuint(counter(f, "cancelled"), ==, 1);
    advance(2000000);
    unpublished(f);
    for (unsigned i = 0; i <= hop; i++) {
        barrier_clear(&blockers[i]);
        barrier_clear(&markers[i]);
    }
    fixture_free(f);
}

static void test_parallel_completion(void)
{
    PairedFixture *f = fixture_new();
    int64_t deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;

    aio_context_set_thread_pool_params(qemu_get_aio_context(), 0, 4,
                                       &error_abort);
    f->encoding = NIKON_RAW_RGGB;
    admit(f);
    memset(f->bytes, 0, 96 * 36); /* All shards must use the owned input. */
    while (counter(f, "completed") != 1) {
        g_assert_cmpint(g_get_monotonic_time(), <, deadline);
        aio_poll(qemu_get_aio_context(), false);
        qemu_clock_run_timers(QEMU_CLOCK_VIRTUAL);
        g_thread_yield();
    }
    g_assert_true(runtime_has(f, "inflight=0"));
    published(f);
    advance(2000000);
    published(f);
    fixture_free(f);
    aio_context_set_thread_pool_params(qemu_get_aio_context(), 0, 1,
                                       &error_abort);
}

static void test_irq_reset(void)
{
    PairedFixture *f = fixture_new();
    PoolBarrier blocker, marker;

    f->encoding = NIKON_RAW_RGGB;
    f->reset_on_irq = true;
    blocker_start(&blocker);
    admit(f);
    worker_returned(&blocker, &marker);
    poll_until(f, "inflight=0", &blocker, &marker);
    g_assert_false(f->reset_on_irq);
    g_assert_cmpuint(counter(f, "completed"), ==, 1);
    g_assert_cmpuint(counter(f, "rejected"), ==, 0);
    g_assert_cmpuint(counter(f, "cancelled"), ==, 0);
    g_assert_cmphex(read_port(f, 0x24), ==, 0);
    g_assert_false(f->level);
    g_assert_cmpuint(f->rises, ==, 1);
    published_pixels(f);
    advance(2000000);
    g_assert_cmpuint(counter(f, "completed"), ==, 1);
    g_assert_false(f->level);
    barrier_clear(&blocker);
    barrier_clear(&marker);
    fixture_free(f);
}

static void alternate_outputs(PairedFixture *f)
{
    for (unsigned phase = 0; phase < 4; phase++) {
        uint32_t main = RAM_BASE + 0x8000 + (phase % 2) * 0x2000;

        write_port(f, 0x3a8 + 4 * phase,
                   RAM_BASE + GRID_OFFSET + phase * 0x1000);
        write_port(f, 0x4a8 + 4 * phase, main);
        write_port(f, 0x4b8 + 4 * phase, main + 64 * 28);
    }
}

static void notify_phase(PairedFixture *f, unsigned selected, unsigned phase)
{
    NikonPairedRawFrame frame = {
        .generation = 2, .address = RAM_BASE + selected * 0x4000 + phase * 0x1000,
        .width = 64, .height = 36, .stride = 96, .bits = 12,
        .encoding = NIKON_RAW_MONO,
        .transfer = { .codec = NIKON_RAW_LINEAR }, .slot = phase,
    };
    NikonPairedGridFrame grid = {
        .generation = 2, .address = RAM_BASE + GRID_OFFSET + phase * 0x1000,
        .width = 60, .height = 40, .stride = 96,
    };
    unsigned offset = frame.address - RAM_BASE;

    for (unsigned i = 0; i < 96 * 36; i += 3) {
        memcpy(f->bytes + offset + i, "\x00\x08\x80", 3);
    }
    nikon_paired_grid_frame(f->dev, &grid);
    nikon_paired_frame(f->dev, &frame);
}

static void ring_word(PairedFixture *f, unsigned word)
{
    unsigned phase = word % 4;

    write_port(f, (word < 4 ? 0x228 : 0x320) + 4 * phase,
               RAM_BASE + 0x4000 + phase * 0x1000);
}

static void stage_ring(PairedFixture *f, unsigned words)
{
    write_port(f, 0xfc, 1);
    for (unsigned word = 0; word < words; word++) {
        ring_word(f, word);
    }
    write_port(f, 0x10, 1);
}

static void phase_pixels(PairedFixture *f, unsigned selected, unsigned phase)
{
    unsigned main = (selected ? 0x8000 : MAIN_OFFSET) +
                    (phase % 2) * 0x2000;

    for (unsigned y = 0; y < 42; y++) {
        for (unsigned x = 0; x < 64; x++) {
            unsigned expected = x >= 56 ? 0xa5 : y < 28 ? 128 :
                                x & 1 ? 96 : 144;

            g_assert_cmpuint(f->bytes[main + y * 64 + x], ==, expected);
        }
    }
    for (unsigned y = 0; y < 40; y++) {
        for (unsigned x = 0; x < 96; x++) {
            g_assert_cmpuint(f->bytes[GRID_OFFSET + phase * 0x1000 +
                                      y * 96 + x], ==,
                             0xa5);
        }
    }
}

static void finish_queued(PairedFixture *f, PoolBarrier *blocker)
{
    PoolBarrier marker;

    worker_returned(blocker, &marker);
    poll_until(f, "inflight=0", blocker, &marker);
    barrier_clear(blocker);
    barrier_clear(&marker);
}

static void first_phase(PairedFixture *f)
{
    PoolBarrier blocker;

    alternate_outputs(f);
    blocker_start(&blocker);
    admit(f);
    finish_queued(f, &blocker);
    published(f);
    write_port(f, 0x24, UINT32_MAX);
    g_assert_false(f->level);
}

static void queue_phase(PairedFixture *f, PoolBarrier *blocker,
                        unsigned selected, unsigned phase)
{
    blocker_start(blocker);
    notify_phase(f, selected, phase);
    advance(1000000);
    g_assert_true(runtime_has(f, "inflight=1"));
    g_assert_true(runtime_has(f, "compute=1"));
}

static void test_async_bank_staging(void)
{
    PairedFixture *f = fixture_new();
    PoolBarrier blocker;
    uint8_t before[RAM_SIZE - 0x8000];

    first_phase(f);
    queue_phase(f, &blocker, 0, 1);
    memcpy(before, f->bytes + 0x8000, sizeof(before));
    stage_ring(f, 8);
    g_assert_cmpuint(counter(f, "cancelled"), ==, 0);
    g_assert_cmpuint(counter(f, "completed"), ==, 1);
    g_assert_cmphex(read_port(f, 0x24), ==, 0);
    g_assert_false(f->level);
    g_assert_cmpmem(f->bytes + 0x8000, sizeof(before), before, sizeof(before));
    finish_queued(f, &blocker);
    g_assert_cmpuint(counter(f, "completed"), ==, 2);
    g_assert_cmpuint(counter(f, "cancelled"), ==, 0);
    g_assert_cmphex(read_port(f, 0x24), ==, 0x1002);
    g_assert_true(f->level);
    phase_pixels(f, 0, 1); /* The old job retains its original selected view. */
    g_assert_cmpuint(f->bytes[0x8000], ==, 0xa5);
    g_assert_cmpuint(f->bytes[0xa000], ==, 0xa5);
    write_port(f, 0x10, 1); /* A kick never acknowledges the previous result. */
    g_assert_cmphex(read_port(f, 0x24), ==, 0x1002);
    g_assert_true(f->level);

    notify_phase(f, 1, 2); /* A real next RAW cannot bypass pending W1C. */
    memcpy(before, f->bytes + 0x8000, sizeof(before));
    advance(2000000);
    g_assert_cmpuint(counter(f, "completed"), ==, 2);
    g_assert_cmphex(read_port(f, 0x24), ==, 0x1002);
    g_assert_true(f->level);
    g_assert_cmpmem(f->bytes + 0x8000, sizeof(before), before, sizeof(before));
    blocker_start(&blocker);
    write_port(f, 0x24, UINT32_MAX);
    g_assert_false(f->level);
    advance(1000000);
    g_assert_true(runtime_has(f, "inflight=1"));
    finish_queued(f, &blocker);
    phase_pixels(f, 1, 2);
    g_assert_cmpuint(counter(f, "completed"), ==, 3);
    g_assert_cmphex(read_port(f, 0x24), ==, 0x1002);
    write_port(f, 0x24, UINT32_MAX);
    queue_phase(f, &blocker, 1, 3);
    finish_queued(f, &blocker);
    phase_pixels(f, 1, 3);
    g_assert_cmpuint(counter(f, "completed"), ==, 4);
    g_assert_cmpuint(counter(f, "cancelled"), ==, 0);
    g_assert_cmpuint(counter(f, "rejected"), ==, 0);
    g_assert_cmphex(read_port(f, 0x24), ==, 0x1402);
    g_assert_cmphex(read_port(f, 0x118), ==, 4 * 56 * 28);
    g_assert_cmpuint(f->rises, ==, 4);
    fixture_free(f);
}

static void test_async_bank_partial(void)
{
    PairedFixture *f = fixture_new();
    PoolBarrier blocker;
    uint8_t before[RAM_SIZE - 0x8000];

    first_phase(f);
    stage_ring(f, 7);
    notify_phase(f, 1, 1);
    memcpy(before, f->bytes + 0x8000, sizeof(before));
    advance(2000000);
    g_assert_cmpuint(counter(f, "completed"), ==, 1);
    g_assert_true(runtime_has(f, "inflight=0"));
    g_assert_cmpmem(f->bytes + 0x8000, sizeof(before), before, sizeof(before));
    ring_word(f, 7);
    advance(2000000); /* The final address is not itself a commit command. */
    g_assert_cmpuint(counter(f, "completed"), ==, 1);
    g_assert_true(runtime_has(f, "inflight=0"));
    g_assert_cmphex(read_port(f, 0x24), ==, 0);
    g_assert_false(f->level);
    blocker_start(&blocker);
    write_port(f, 0x10, 1);
    advance(1000000);
    g_assert_true(runtime_has(f, "inflight=1"));
    finish_queued(f, &blocker);
    phase_pixels(f, 1, 1);
    g_assert_cmpuint(counter(f, "completed"), ==, 2);
    g_assert_cmpuint(counter(f, "cancelled"), ==, 0);
    g_assert_cmpuint(counter(f, "rejected"), ==, 0);
    g_assert_cmphex(read_port(f, 0x24), ==, 0x1002);
    fixture_free(f);
}

static void test_async_bank_commit_rewrite(void)
{
    PairedFixture *f = fixture_new();
    PoolBarrier blocker;
    uint8_t before[RAM_SIZE - 0x8000];

    first_phase(f);
    queue_phase(f, &blocker, 0, 1);
    stage_ring(f, 8);
    ring_word(f, 0); /* Even an equal address begins a new publication. */
    g_assert_cmpuint(counter(f, "cancelled"), ==, 0);
    finish_queued(f, &blocker);
    g_assert_cmpuint(counter(f, "completed"), ==, 2);
    g_assert_cmpuint(counter(f, "cancelled"), ==, 0);
    phase_pixels(f, 0, 1);
    g_assert_cmphex(read_port(f, 0x24), ==, 0x1002);
    write_port(f, 0x24, UINT32_MAX);
    notify_phase(f, 1, 2);
    memcpy(before, f->bytes + 0x8000, sizeof(before));
    advance(2000000);
    g_assert_true(runtime_has(f, "inflight=0"));
    g_assert_cmpuint(counter(f, "completed"), ==, 2);
    write_port(f, 0x10, 1); /* The invalidated commit cannot borrow old words. */
    advance(2000000);
    g_assert_true(runtime_has(f, "inflight=0"));
    g_assert_cmpuint(counter(f, "completed"), ==, 2);
    for (unsigned word = 1; word < 8; word++) {
        ring_word(f, word);
    }
    advance(2000000); /* A fresh whole publication still needs its own kick. */
    g_assert_true(runtime_has(f, "inflight=0"));
    g_assert_cmpuint(counter(f, "completed"), ==, 2);
    g_assert_cmphex(read_port(f, 0x24), ==, 0);
    g_assert_false(f->level);
    g_assert_cmpmem(f->bytes + 0x8000, sizeof(before), before, sizeof(before));
    blocker_start(&blocker);
    write_port(f, 0x10, 1);
    advance(1000000);
    g_assert_true(runtime_has(f, "inflight=1"));
    finish_queued(f, &blocker);
    phase_pixels(f, 1, 2);
    g_assert_cmpuint(counter(f, "completed"), ==, 3);
    g_assert_cmpuint(counter(f, "cancelled"), ==, 0);
    g_assert_cmpuint(counter(f, "rejected"), ==, 0);
    g_assert_cmphex(read_port(f, 0x24), ==, 0x1002);
    fixture_free(f);
}

static void test_async_initial_partial(void)
{
    PairedFixture *f = fixture_new_raw(7, false);
    PoolBarrier blocker;

    notify_phase(f, 0, 0);
    write_port(f, 0x10, 1);
    write_port(f, 8, 1);
    advance(2000000);
    unpublished(f);
    g_assert_true(runtime_has(f, "inflight=0"));
    g_assert_cmpuint(counter(f, "rejected"), ==, 0);
    write_port(f, 0x31c, RAM_BASE + 3 * 0x1000);
    advance(2000000); /* 08 must preserve coverage, not auto-commit it. */
    unpublished(f);
    g_assert_true(runtime_has(f, "inflight=0"));
    g_assert_cmpuint(counter(f, "rejected"), ==, 0);
    blocker_start(&blocker);
    write_port(f, 0x10, 1);
    advance(1000000);
    g_assert_true(runtime_has(f, "inflight=1"));
    finish_queued(f, &blocker);
    published(f);
    g_assert_cmpuint(counter(f, "cancelled"), ==, 0);
    fixture_free(f);
}

static void test_async_bank_clear(gconstpointer opaque)
{
    bool stop = GPOINTER_TO_INT(opaque);
    PairedFixture *f = fixture_new();
    PoolBarrier blocker;
    uint8_t before[RAM_SIZE - 0x8000];

    first_phase(f);
    queue_phase(f, &blocker, 0, 1);
    stage_ring(f, 8);
    memcpy(before, f->bytes + 0x8000, sizeof(before));
    if (stop) {
        static const unsigned commands[] = { 0x54, 0x60, 0x6c, 0x78 };

        write_port(f, 0x84, 0);
        write_port(f, 0xf0, 0xfc400);
        for (unsigned i = 0; i < ARRAY_SIZE(commands); i++) {
            write_port(f, commands[i], 1);
        }
    } else {
        device_cold_reset(f->dev);
    }
    finish_queued(f, &blocker);
    g_assert_cmpuint(counter(f, "completed"), ==, 1);
    g_assert_cmpuint(counter(f, "cancelled"), ==, 1);
    g_assert_cmpmem(f->bytes + 0x8000, sizeof(before), before, sizeof(before));
    g_assert_cmphex(read_port(f, 0x24), ==, stop ? 0x014a0080 : 0);
    g_assert_cmphex(read_port(f, 0xfc), ==, 0);
    if (stop) {
        write_port(f, 0x24, UINT32_MAX);
        write_port(f, 0x84, 1);
        write_port(f, 0xf0, 0x8e);
    } else {
        configure(f);
    }
    g_assert_false(f->level);
    blocker_start(&blocker);
    write_port(f, 0x10, 1);
    write_port(f, 8, 1);
    notify_phase(f, 0, 0);
    advance(1000000);
    g_assert_true(runtime_has(f, "inflight=1"));
    finish_queued(f, &blocker);
    g_assert_cmpuint(counter(f, "completed"), ==, 2);
    g_assert_cmpuint(counter(f, "cancelled"), ==, 1);
    g_assert_cmpuint(counter(f, "rejected"), ==, 0);
    g_assert_cmphex(read_port(f, 0x24), ==, 0x1002);
    phase_pixels(f, 0, 0);
    fixture_free(f);
}

static void test_async_bank_invalid(void)
{
    PairedFixture *f = fixture_new();
    MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(f->dev), 0);

    g_assert_cmpint(memory_region_dispatch_write(mr, 0xfc, 2, MO_32,
                        MEMTXATTRS_UNSPECIFIED), ==, MEMTX_ERROR);
    g_assert_cmphex(read_port(f, 0xfc), ==, 0);
    unpublished(f);
    fixture_free(f);
}

static NikonPairedRawFrame raw_frame(unsigned phase, uint64_t generation)
{
    return (NikonPairedRawFrame) {
        .generation = generation, .address = RAM_BASE + phase * 0x1000,
        .width = 64, .height = 36, .stride = 96, .bits = 12,
        .encoding = NIKON_RAW_MONO, .slot = phase,
    };
}

static NikonPairedGridFrame grid_frame(uint64_t generation)
{
    return (NikonPairedGridFrame) {
        .generation = generation, .address = RAM_BASE + GRID_OFFSET,
        .width = 60, .height = 40, .stride = 96,
    };
}

static void test_grid_dependency(void)
{
    PairedFixture *f = fixture_new();
    NikonPairedRawFrame raw = raw_frame(0, 1);
    NikonPairedGridFrame grid = grid_frame(0);
    PoolBarrier blocker;

    write_port(f, 0x39c, grid.address); /* Two RAW phases read one map address. */
    write_port(f, 0x10, 1);
    write_port(f, 8, 1);
    nikon_paired_frame(f->dev, &raw);
    advance(2000000);
    unpublished(f); /* Nonzero packed RAM is not a producer publication. */
    g_assert_true(runtime_has(f, "bank0 request=2 active=0"));
    nikon_paired_grid_frame(f->dev, &grid);
    advance(2000000);
    unpublished(f);
    blocker_start(&blocker);
    grid.generation = 1;
    nikon_paired_grid_frame(f->dev, &grid);
    advance(999999);
    unpublished(f);
    advance(1);
    finish_queued(f, &blocker);
    published(f);
    write_port(f, 0x24, UINT32_MAX);
    raw = raw_frame(1, 2);
    nikon_paired_frame(f->dev, &raw);
    advance(2000000);
    g_assert_cmpuint(counter(f, "completed"), ==, 1);
    g_assert_cmphex(read_port(f, 0x24), ==, 0);
    g_assert_true(runtime_has(f, "bank0 request=2 active=0"));
    nikon_paired_grid_frame(f->dev, &grid); /* Same generation stays consumed. */
    advance(2000000);
    g_assert_cmpuint(counter(f, "completed"), ==, 1);
    nikon_paired_grid_frame(f->dev, NULL); /* New epoch may restart numbering. */
    blocker_start(&blocker);
    nikon_paired_grid_frame(f->dev, &grid);
    advance(1000000);
    finish_queued(f, &blocker);
    g_assert_cmpuint(counter(f, "completed"), ==, 2);
    for (unsigned i = 0; i < 4 * 0x1000; i++) {
        g_assert_cmphex(f->bytes[GRID_OFFSET + i], ==, 0xa5);
    }
    fixture_free(f);
}

static void test_grid_snapshot(void)
{
    uint8_t baseline[64 * 42], zero[64 * 42];

    for (unsigned variant = 0; variant < 3; variant++) {
        PairedFixture *f = fixture_new();
        NikonPairedRawFrame raw = raw_frame(0, 1);
        NikonPairedGridFrame grid = grid_frame(1);
        PoolBarrier blocker;

        write_port(f, 0xb58, 0x01000080);
        for (unsigned y = 0; y < 36; y++) {
            for (unsigned x = 0; x < 64; x += 2) {
                unsigned a = x < 24 ? 300 : x < 40 ? 3600 : 1000;
                unsigned at = y * 96 + x * 3 / 2;

                f->bytes[at] = a;
                f->bytes[at + 1] = a >> 8 | a << 4;
                f->bytes[at + 2] = a >> 4;
            }
        }
        for (unsigned y = 0; y < 40; y++) {
            memset(f->bytes + GRID_OFFSET + y * 96, variant == 2 ? 0 : 255, 90);
        }
        blocker_start(&blocker);
        write_port(f, 0x10, 1);
        write_port(f, 8, 1);
        nikon_paired_grid_frame(f->dev, &grid);
        nikon_paired_frame(f->dev, &raw);
        g_assert_true(runtime_has(f, "bank0 request=2 active=1"));
        advance(1000000);
        if (variant == 1) {
            grid.generation = 0;
            nikon_paired_grid_frame(f->dev, &grid);
            for (unsigned y = 0; y < 40; y++) {
                memset(f->bytes + GRID_OFFSET + y * 96, 0, 90);
            }
        }
        finish_queued(f, &blocker);
        g_assert_cmpuint(counter(f, "completed"), ==, 1);
        if (variant == 0) {
            memcpy(baseline, f->bytes + MAIN_OFFSET, sizeof(baseline));
        } else if (variant == 1) {
            g_assert_cmpmem(f->bytes + MAIN_OFFSET, sizeof(baseline),
                            baseline, sizeof(baseline));
        } else {
            memcpy(zero, f->bytes + MAIN_OFFSET, sizeof(zero));
        }
        for (unsigned y = 0; y < 40; y++) {
            for (unsigned x = 0; x < 96; x++) {
                unsigned expected = x >= 90 ? 0xa5 : variant == 0 ? 255 : 0;

                g_assert_cmphex(f->bytes[GRID_OFFSET + y * 96 + x], ==, expected);
            }
        }
        fixture_free(f);
    }
    g_assert_cmpint(memcmp(baseline, zero, sizeof(baseline)), !=, 0);
}

static void test_grid_rejection(void)
{
    for (unsigned bad = 0; bad < 7; bad++) {
        PairedFixture *f = fixture_new();
        NikonPairedRawFrame raw = raw_frame(0, 1);
        NikonPairedGridFrame grid = grid_frame(1);

        switch (bad) {
        case 0: grid.width = 58; break;
        case 1: grid.height = 39; break;
        case 2: grid.stride = 92; break;
        case 3:
            grid.address = 0x40000000;
            write_port(f, 0x398, grid.address);
            break;
        case 4:
            grid.address = RAM_BASE + MAIN_OFFSET;
            write_port(f, 0x398, grid.address);
            break;
        case 5: write_port(f, 0xb38, 1); break;
        case 6: write_port(f, 0xb48, 0x003a0027); break;
        }
        write_port(f, 0x10, 1);
        write_port(f, 8, 1);
        nikon_paired_grid_frame(f->dev, &grid);
        nikon_paired_frame(f->dev, &raw);
        advance(2000000);
        unpublished(f);
        g_assert_cmpuint(counter(f, "rejected"), >, 0);
        g_assert_true(runtime_has(f, "inflight=0"));
        fixture_free(f);
    }
}

static void test_grid_memory(gconstpointer opaque)
{
    bool readonly = GPOINTER_TO_INT(opaque);
    PairedFixture *f = fixture_new();
    MemoryRegion view;
    NikonPairedRawFrame raw = raw_frame(0, 1);
    NikonPairedGridFrame grid = grid_frame(1);
    PoolBarrier blocker;

    if (readonly) {
        memory_region_init_rom(&view, NULL, "paired-grid-rom", 0x1000,
                                &error_abort);
        memset(memory_region_get_ram_ptr(&view), 0xa5, 0x1000);
    } else {
        memory_region_init_alias(&view, NULL, "paired-grid-alias", &f->ram,
                                  MAIN_OFFSET, 0x1000);
    }
    memory_region_add_subregion(get_system_memory(), 0x30000000, &view);
    grid.address = 0x30000000;
    write_port(f, 0x398, grid.address);
    write_port(f, 0x10, 1);
    write_port(f, 8, 1);
    if (readonly) {
        blocker_start(&blocker);
    }
    nikon_paired_grid_frame(f->dev, &grid);
    nikon_paired_frame(f->dev, &raw);
    advance(1000000);
    if (readonly) {
        finish_queued(f, &blocker);
        published(f);
        for (unsigned i = 0; i < 0x1000; i++) {
            g_assert_cmphex(((uint8_t *)memory_region_get_ram_ptr(&view))[i],
                            ==, 0xa5);
        }
    } else {
        unpublished(f);
        g_assert_cmpuint(counter(f, "rejected"), >, 0);
    }
    memory_region_del_subregion(get_system_memory(), &view);
    object_unparent(OBJECT(&view));
    fixture_free(f);
}

static void test_grid_pool_epoch(void)
{
    PairedFixture *f = fixture_new();
    NikonPairedGridFrame grid = grid_frame(1);
    NikonPairedRawFrame raw = raw_frame(0, 1);
    PoolBarrier blocker;

    for (unsigned i = 0; i < 8; i++) {
        grid.address = RAM_BASE + GRID_OFFSET + 4 * i;
        nikon_paired_grid_frame(f->dev, &grid);
    }
    grid.address += 4;
    write_port(f, 0x398, grid.address);
    write_port(f, 0x10, 1);
    write_port(f, 8, 1);
    nikon_paired_frame(f->dev, &raw);
    nikon_paired_grid_frame(f->dev, &grid);
    advance(2000000);
    unpublished(f);
    g_assert_cmpuint(counter(f, "rejected"), ==, 1);
    nikon_paired_grid_frame(f->dev, NULL);
    blocker_start(&blocker);
    nikon_paired_grid_frame(f->dev, &grid);
    advance(1000000);
    finish_queued(f, &blocker);
    g_assert_cmpuint(counter(f, "completed"), ==, 1);
    for (unsigned i = 0; i < 4 * 0x1000; i++) {
        g_assert_cmphex(f->bytes[GRID_OFFSET + i], ==, 0xa5);
    }
    fixture_free(f);
}

static void stream_gate_write(PairedFixture *f, unsigned offset, uint32_t value)
{
    MemoryRegion *mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(f->gate), 0);

    g_assert_cmpint(memory_region_dispatch_write(mr, offset, value, MO_32,
                        MEMTXATTRS_UNSPECIFIED), ==, MEMTX_OK);
}

static void stream_configure(PairedFixture *f)
{
    static const struct { unsigned offset; uint32_t value; } photo[] = {
        { 0x88, 2 }, { 0x8c, 0 }, { 0x94, 92 }, { 0x9c, 879 },
        { 0xa8, 2 }, { 0xe0, 0x156 }, { 0x10c, 1 }, { 0x124, 0 },
        { 0x12c, 0 }, { 0x138, 1 }, { 0x13c, 22 << 16 }, { 0x140, 40 << 16 },
        { 0x3d8, 0 }, { 0x408, 0 }, { 0x410, 0 }, { 0x424, 3 },
    };
    const uint32_t slot[] = {
        0x09000201, 0x15500, 0, 0, 64, 64, 0, 0,
        (28 << 16) | 56, 0, 0x8000, 0, 0,
    };
    uint8_t *packet = f->bytes + 0xf000;

    for (unsigned i = 0; i < ARRAY_SIZE(photo); i++) {
        write_port(f, photo[i].offset, photo[i].value);
    }
    for (unsigned offset = 0x430; offset <= 0x470; offset += 4) {
        write_port(f, offset, 0);
    }
    for (unsigned phase = 0; phase < 4; phase++) {
        write_port(f, 0x488 + phase * 4, 0x40000000);
        write_port(f, 0x498 + phase * 4, 0x40008000);
    }
    for (unsigned i = 0; i < ARRAY_SIZE(slot); i++) {
        stream_gate_write(f, 0x600 + i * 4, slot[i]);
    }
    stream_gate_write(f, 0x2c, 8);
    memset(packet, 0, 356);
    stl_le_p(packet, 0x11040000);
    stl_le_p(packet + 4, 8);
    stl_le_p(packet + 16, 0x11510200);
    stl_le_p(packet + 340, 0x01040400);
    stream_gate_write(f, 0xe74, 1);
    stream_gate_write(f, 0xe74, 0);
    stream_gate_write(f, 0xe70, RAM_BASE + 0xf000);
}

static void stream_poll(PairedFixture *f, const char *state)
{
    int64_t deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;

    while (!runtime_has(f, state)) {
        if (g_get_monotonic_time() >= deadline) {
            g_autofree char *runtime = object_property_get_str(OBJECT(f->dev),
                "runtime", &error_abort);

            g_error("paired stream wait for %s: %s", state, runtime);
        }
        g_assert_cmpint(g_get_monotonic_time(), <, deadline);
        aio_poll(qemu_get_aio_context(), false);
        if (runstate_is_running()) {
            qemu_clock_run_timers(QEMU_CLOCK_VIRTUAL);
        }
        g_thread_yield();
    }
}

static void stream_pixels(PairedFixture *f, bool present)
{
    g_autofree char *encoded = object_property_get_str(OBJECT(f->gate),
                                                       "input-data", &error_abort);
    gsize bytes;
    g_autofree guchar *pixels = g_base64_decode(encoded, &bytes);

    g_assert_cmpuint(bytes, ==, present ? 56 * 42 : 0);
    if (present) {
        for (unsigned i = 0; i < bytes; i++) {
            g_assert_cmpuint(pixels[i], ==, i < 56 * 28 ? 128 :
                             i % 2 ? 96 : 144);
        }
    }
    for (unsigned i = MAIN_OFFSET; i < RAM_SIZE; i++) {
        g_assert_cmphex(f->bytes[i], ==, 0xa5);
    }
}

static void test_stream_ready_lifecycle(gconstpointer opaque)
{
    unsigned mode = GPOINTER_TO_UINT(opaque);
    PairedFixture *f = fixture_new_raw(8, true);
    NikonPairedRawFrame frame = {
        .generation = 1, .address = RAM_BASE, .width = 64, .height = 36,
        .stride = 96, .bits = 12, .transfer = { .codec = NIKON_RAW_LINEAR },
    };

    stream_configure(f);
    write_port(f, 0x10, 1);
    write_port(f, 8, 1);
    nikon_paired_frame(f->dev, &frame);
    advance(1000000);
    stream_poll(f, "inflight=0");
    g_assert_cmpuint(counter(f, "completed"), ==, 1);
    stream_pixels(f, true);
    write_port(f, 0x24, UINT32_MAX);
    memset(f->bytes + 0x1000, 0, 96 * 36);
    frame.slot = 1;
    frame.generation = 2;
    frame.address += 0x1000;
    nikon_paired_frame(f->dev, &frame);
    advance(1000000);
    stream_poll(f, "capacity-wait=1");
    g_assert_true(runtime_has(f, "inflight=1"));
    g_assert_true(runtime_has(f, " stage=7 remaining=0"));
    g_assert_cmpuint(counter(f, "completed"), ==, 1);
    g_assert_cmpuint(counter(f, "rejected"), ==, 0);
    g_assert_cmphex(read_port(f, 0x24), ==, 0);
    stream_pixels(f, true);
    if (mode == 0) {
        vm_stop(RUN_STATE_PAUSED);
        stream_gate_write(f, 4, 0); /* Actual epoch withdrawal while paused. */
        g_assert_true(runtime_has(f, "capacity-pending=1"));
        aio_poll(qemu_get_aio_context(), false);
        g_assert_true(runtime_has(f, "capacity-wait=1"));
        g_assert_cmpuint(counter(f, "rejected"), ==, 0);
        stream_pixels(f, false);
        vm_start();
        qemu_clock_run_timers(QEMU_CLOCK_VIRTUAL);
        stream_poll(f, "inflight=0");
        g_assert_cmpuint(counter(f, "rejected"), ==, 1);
    } else if (mode == 1) {
        device_cold_reset(f->dev);
        g_assert_true(runtime_has(f, "inflight=0"));
        g_assert_cmpuint(counter(f, "cancelled"), ==, 1);
        stream_pixels(f, true);
    } else {
        qdev_unrealize(f->dev); /* Also cancels a retained ready aggregate. */
        g_assert_true(runtime_has(f, "inflight=0"));
        g_assert_cmpuint(counter(f, "cancelled"), ==, 1);
        stream_pixels(f, true);
        stream_gate_write(f, 4, 0); /* No deferred work on an unrealized owner. */
    }
    advance(2000000);
    g_assert_cmpuint(counter(f, "completed"), ==, 1);
    g_assert_cmpuint(object_property_get_uint(OBJECT(f->gate), "input-frames",
                                              &error_abort), ==, 1);
    g_assert_false(f->level);
    g_assert_cmpuint(f->rises, ==, 1);
    fixture_free(f);
}

int main(int argc, char **argv)
{
    g_autofree char *directory = g_dir_make_tmp("nikonXXXXXX", NULL);
    g_autofree char *socket = g_build_filename(directory, "qtest", NULL);
    g_autofree char *chardev = g_strdup_printf("unix:%s,server=on,wait=off",
                                             socket);
    const char *qemu_argv[] = {
        argv[0], "-machine", "none", "-accel", "qtest", "-display", "none",
        "-nodefaults", "-S", "-qtest", chardev, NULL,
    };
    g_auto(GStrv) mutable_argv = g_new0(char *, G_N_ELEMENTS(qemu_argv));
    int result;

    g_assert_nonnull(directory);
    g_test_init(&argc, &argv, NULL);
    g_test_add_data_func("/nikon/paired/stream/ready-pause", GUINT_TO_POINTER(0),
                         test_stream_ready_lifecycle);
    g_test_add_data_func("/nikon/paired/stream/ready-reset", GUINT_TO_POINTER(1),
                         test_stream_ready_lifecycle);
    g_test_add_data_func("/nikon/paired/stream/ready-unrealize", GUINT_TO_POINTER(2),
                         test_stream_ready_lifecycle);
    for (unsigned i = 0; i + 1 < G_N_ELEMENTS(qemu_argv); i++) {
        mutable_argv[i] = g_strdup(qemu_argv[i]);
    }
    qemu_init(G_N_ELEMENTS(qemu_argv) - 1, mutable_argv);
    aio_context_set_thread_pool_params(qemu_get_aio_context(), 0, 1,
                                       &error_abort);
    g_test_add_data_func("/nikon/paired/async/completion", GINT_TO_POINTER(0),
                         test_async_completion);
    g_test_add_data_func("/nikon/paired/async/pause", GINT_TO_POINTER(1),
                         test_async_completion);
    g_test_add_data_func("/nikon/paired/async/reset-queued", GUINT_TO_POINTER(0),
                         test_async_cancel);
    g_test_add_data_func("/nikon/paired/async/config-queued", GUINT_TO_POINTER(1),
                         test_async_cancel);
    g_test_add_data_func("/nikon/paired/async/reset-ready", GUINT_TO_POINTER(2),
                         test_async_cancel);
    g_test_add_data_func("/nikon/paired/async/unrealize", GUINT_TO_POINTER(0),
                         test_async_unrealize);
    g_test_add_data_func("/nikon/paired/async/cfa-completion", GUINT_TO_POINTER(2),
                         test_async_completion);
    g_test_add_data_func("/nikon/paired/async/cfa-pause", GUINT_TO_POINTER(3),
                         test_async_completion);
    g_test_add_data_func("/nikon/paired/async/cfa-reset-queued",
                         GUINT_TO_POINTER(4), test_async_cancel);
    g_test_add_data_func("/nikon/paired/async/cfa-config-queued",
                         GUINT_TO_POINTER(5), test_async_cancel);
    g_test_add_data_func("/nikon/paired/async/cfa-reset-ready",
                         GUINT_TO_POINTER(6), test_async_cancel);
    g_test_add_data_func("/nikon/paired/async/cfa-unrealize", GUINT_TO_POINTER(1),
                         test_async_unrealize);
    for (unsigned stage = 0; stage < 7; stage++) {
        g_autofree char *name = g_strdup_printf(
            "/nikon/paired/async/stage%u-reset", stage);

        g_test_add_data_func(name, GUINT_TO_POINTER(stage), test_stage_cancel);
    }
    g_test_add_data_func("/nikon/paired/async/post-middle-reset",
                         GUINT_TO_POINTER(5 | 16), test_stage_cancel);
    g_test_add_data_func("/nikon/paired/async/prefix-unrealize",
                         GUINT_TO_POINTER(1 | 8), test_stage_cancel);
    g_test_add_func("/nikon/paired/async/parallel-completion",
                    test_parallel_completion);
    g_test_add_func("/nikon/paired/async/irq-reset", test_irq_reset);
    g_test_add_func("/nikon/paired/async/bank-staging", test_async_bank_staging);
    g_test_add_func("/nikon/paired/async/bank-partial", test_async_bank_partial);
    g_test_add_func("/nikon/paired/async/bank-commit-rewrite",
                    test_async_bank_commit_rewrite);
    g_test_add_func("/nikon/paired/async/initial-partial",
                    test_async_initial_partial);
    g_test_add_data_func("/nikon/paired/async/bank-reset", GINT_TO_POINTER(0),
                         test_async_bank_clear);
    g_test_add_data_func("/nikon/paired/async/bank-stop", GINT_TO_POINTER(1),
                         test_async_bank_clear);
    g_test_add_func("/nikon/paired/async/bank-invalid", test_async_bank_invalid);
    g_test_add_func("/nikon/paired/grid-dependency", test_grid_dependency);
    g_test_add_func("/nikon/paired/grid-snapshot", test_grid_snapshot);
    g_test_add_func("/nikon/paired/grid-rejection", test_grid_rejection);
    g_test_add_data_func("/nikon/paired/grid-readonly", GINT_TO_POINTER(1),
                         test_grid_memory);
    g_test_add_data_func("/nikon/paired/grid-alias", GINT_TO_POINTER(0),
                         test_grid_memory);
    g_test_add_func("/nikon/paired/grid-pool-epoch", test_grid_pool_epoch);
    result = g_test_run();
    qemu_cleanup(result);
    unlink(socket);
    rmdir(directory);
    return result;
}
