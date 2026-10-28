/*
 * Capture unit16: explicit finite grayscale statistics/DMA profile.
 * Geometry is source-backed; arithmetic/input/ring timing are hypotheses.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/bswap.h"
#include "qemu/log.h"
#include "qemu/timer.h"
#include "qemu/aio-wait.h"
#include "block/thread-pool.h"
#include "hw/core/sysbus.h"
#include "hw/core/qdev-properties.h"
#include "hw/nikon/devices.h"
#include "hw/nikon/readoutpipeline.h"
#include "hw/nikon/sensor.h"
#include "hw/nikon/statistics.h"
#include "migration/vmstate.h"
#include "system/address-spaces.h"
#include "system/dma.h"

OBJECT_DECLARE_SIMPLE_TYPE(NikonStatistics, NIKON_STATISTICS)

typedef struct StatisticsOutput {
    uint32_t address, width, height, pitch, bytes;
} StatisticsOutput;

typedef struct StatisticsJob {
    uint32_t configuration[NIKON_CAPTURE_ENDPOINT_WORDS];
    unsigned cursor[10], banks;
    uint64_t revision;
    NikonCaptureInput input;
    uint8_t *input_data, *data[10];
    StatisticsOutput out[10];
    uint32_t results[12], scalars[4];
} StatisticsJob;

typedef struct StatisticsTask {
    NikonStatistics *owner;
    StatisticsJob job;
    uint64_t token;
    bool submitted, ready, success;
} StatisticsTask;

struct NikonStatistics {
    SysBusDevice parent_obj;
    DeviceState *endpoints, *banks, *pipeline, *capture;
    QEMUTimer *timer;
    uint32_t committed[NIKON_CAPTURE_ENDPOINT_WORDS];
    unsigned cursor[10];
    bool selected[2], valid, running, active, stopping;
    StatisticsTask *compute;
    uint64_t token, inflight, revision;
    uint64_t last_input, completed, rejected, cancelled, blocked;
};

static void statistics_task_free(StatisticsTask *task);
static void statistics_rise(void *opaque, int n, int level);

static void statistics_cancel(NikonStatistics *s, bool record)
{
    s->token++;
    if (s->active) {
        timer_del(s->timer);
        s->active = false;
        if (record) {
            s->cancelled++;
        }
    }
    if (s->compute && (!s->compute->submitted || s->compute->ready)) {
        statistics_task_free(s->compute);
    }
}

static uint32_t global(const uint32_t *words, unsigned offset)
{
    return words[(offset - 0x200) / 4];
}

static uint32_t endpoint(const uint32_t *words, unsigned n, unsigned offset)
{
    return words[(0x200 + n * 0x100 + offset) / 4];
}

static unsigned statistics_banks(NikonStatistics *s)
{
    uint32_t run = 0, enable = 0, reset = 0;

    nikon_capture_configuration(s->capture, 8, &run);
    nikon_capture_configuration(s->capture, 0x10, &enable);
    nikon_capture_configuration(s->capture, 0, &reset);
    /* Original service objects independently own units40/80 hex. */
    return ((run & enable & ~reset) >> 6) & 3;
}

static bool statistics_ram(uint64_t address, uint64_t bytes)
{
    if (!address || !bytes || address + bytes > 0x100000000ULL) {
        return false;
    }
    while (bytes) {
        hwaddr offset, span = bytes;
        MemoryRegion *mr = address_space_translate(&address_space_memory,
            address, &offset, &span, true, MEMTXATTRS_UNSPECIFIED);

        if (!span || !memory_region_is_ram(mr) ||
            !memory_access_is_direct(mr, true, MEMTXATTRS_UNSPECIFIED)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "nikon statistics output PA=0x%08" PRIx64
                          " bytes=0x%" PRIx64 " region=%s is not writable RAM\n",
                          address, bytes, memory_region_name(mr));
            return false;
        }
        address += span;
        bytes -= span;
    }
    return true;
}

static bool statistics_outputs(const StatisticsJob *j, StatisticsOutput out[10])
{
    /* C1.70's constructors reserve six/four slots, independent of work count.
     * Endpoint1 alone selects a large-count pool. Cyclic reuse is a finite
     * functional hypothesis, not a recovered physical counter encoding. */
    static const unsigned storage_slots[] = { 6, 256, 6, 6, 6, 4, 4, 4, 4, 4 };
    const uint32_t *w = j->configuration;
    bool any = false;

    for (unsigned n = 0; n < 10; n++) {
        uint32_t packed, bank, base, stride, count, bpp;

        if (!endpoint(w, n, 0x14)) {
            continue;
        }
        unsigned dimensions = n <= 4 ? (n ? 0x2d0 + (n - 1) * 0x18 : 0x278) :
                              n < 8 ? 0x338 : n == 8 ? 0x354 : 0x370;

        packed = global(w, dimensions);
        /* Mode1 clears output extents without erasing retained DMA routes. */
        if (!packed) {
            continue;
        }
        if (endpoint(w, n, 0x14) != 3 || endpoint(w, n, 0x18) != 1 ||
            endpoint(w, n, 0x40) != 1 || endpoint(w, n, 0x4c) ||
            endpoint(w, n, 0x24)) {
            return false;
        }
        bank = endpoint(w, n, 0x3c) & 1;
        count = endpoint(w, n, 0x48) + 1;
        stride = endpoint(w, n, 0x80);
        if ((endpoint(w, n, 0x3c) >> 1) > 1 ||
            endpoint(w, n, 0x44) >= count || j->cursor[n] >= count) {
            return false;
        }
        base = endpoint(w, n, 0x50 + bank * 0x10);
        if (n == 2 || n == 3 || n == 4 || n == 9) {
            for (unsigned i = 1; i < 4; i++) {
                if (endpoint(w, n, 0x50 + bank * 0x10 + i * 4) != base) {
                    return false;
                }
            }
        }
        if (n <= 4) {
            out[n].width = packed & 0xff;
            out[n].height = (packed >> 8) & 0xff;
        } else {
            unsigned shift = n == 8 ? 11 : 10;

            out[n].width = packed & ((1u << shift) - 1);
            out[n].height = packed >> shift;
        }
        bpp = n == 0 ? 16 : n <= 4 ? 2 : 1;
        out[n].pitch = ROUND_UP(out[n].width * bpp, 16);
        uint64_t bytes = (uint64_t)out[n].pitch * out[n].height;
        unsigned slot = j->cursor[n] % MIN(count, storage_slots[n]);
        uint64_t address = (uint64_t)base + (uint64_t)slot * stride;

        if (!out[n].width || !out[n].height || out[n].height > 4096 ||
            (stride && bytes > stride) || bytes > 0x1000000 ||
            !statistics_ram(address, bytes)) {
            return false;
        }
        out[n].address = address;
        out[n].bytes = bytes;
        any = true;
        for (unsigned i = 0; i < n; i++) {
            if (out[i].bytes &&
                address < (uint64_t)out[i].address + out[i].bytes &&
                out[i].address < address + bytes) {
                return false;
            }
        }
    }
    return any;
}

static uint64_t box(const uint64_t *integral, unsigned pitch,
                    unsigned x0, unsigned y0, unsigned x1, unsigned y1)
{
    return integral[y1 * pitch + x1] + integral[y0 * pitch + x0] -
           integral[y1 * pitch + x0] - integral[y0 * pitch + x1];
}

static void statistics_cell(const uint64_t *integral, unsigned width,
                            unsigned height, unsigned x, unsigned y,
                            unsigned columns, unsigned rows, uint32_t result[4])
{
    unsigned x0 = (uint64_t)x * width / columns;
    unsigned y0 = (uint64_t)y * height / rows;
    unsigned x1 = MIN(width, MAX(x0 + 1, (uint64_t)(x + 1) * width / columns));
    unsigned y1 = MIN(height, MAX(y0 + 1, (uint64_t)(y + 1) * height / rows));
    unsigned pitch = width + 1;
    uint64_t sum = box(integral, pitch, x0, y0, x1, y1);
    uint64_t count = (x1 - x0) * (y1 - y0);
    int64_t difference = (int64_t)box(integral, pitch, x1 - 1, y0, x1, y1) -
                        (int64_t)box(integral, pitch, x0, y0, x0 + 1, y1);

    result[0] = CLAMP(difference, -0x800000, 0x7fffff) & 0xffffff;
    result[1] = MIN(sum, 0x1fffffff);
    result[2] = MIN(count, 0x1ffff);
    result[3] = (sum + count / 2) / count;
}

static int statistics_compute(void *opaque)
{
    StatisticsTask *task = opaque;
    StatisticsJob *j = &task->job;
    unsigned width = j->input.width, height = j->input.height;
    unsigned pitch = width + 1;
    g_autofree uint64_t *integral = NULL;

    integral = g_new0(uint64_t, (size_t)pitch * (height + 1));
    for (unsigned y = 0; y < height; y++) {
        uint64_t sum = 0;

        for (unsigned x = 0; x < width; x++) {
            sum += lduw_le_p(j->input_data + y * j->input.stride + x * 2);
            integral[(y + 1) * pitch + x + 1] =
                integral[y * pitch + x + 1] + sum;
        }
    }
    for (unsigned n = 0; n < 10; n++) {
        const StatisticsOutput *o = &j->out[n];
        uint8_t *data;

        if (!o->bytes) {
            continue;
        }
        data = j->data[n] = g_malloc0(o->bytes);
        for (unsigned y = 0; y < o->height; y++) {
            for (unsigned x = 0; x < o->width; x++) {
                uint32_t cell[4];
                uint8_t *row = data + y * o->pitch;

                statistics_cell(integral, width, height, x, y,
                                 o->width, o->height, cell);
                if (!n) {
                    for (unsigned i = 0; i < 4; i++) {
                        stl_le_p(row + x * 16 + i * 4, cell[i]);
                    }
                } else if (n <= 4) {
                    stw_le_p(row + x * 2, cell[3]);
                } else {
                    row[x] = cell[3] >> 8;
                }
            }
        }
    }
    for (unsigned q = 0; q < 4; q++) {
        uint32_t cell[4];
        unsigned base = q < 2 ? q : q + 4;

        statistics_cell(integral, width, height, q % 2, q / 2, 2, 2, cell);
        j->results[base] = cell[0];
        j->results[base + 2] = cell[1];
        j->results[base + 4] = cell[2];
        j->scalars[q] = cell[3];
    }
    return 0;
}

static void statistics_task_free(StatisticsTask *task)
{
    NikonStatistics *s = task->owner;

    assert(s->compute == task);
    s->compute = NULL;
    if (task->submitted) {
        assert(s->inflight == 1);
        s->inflight--;
    }
    g_free(task->job.input_data);
    for (unsigned n = 0; n < 10; n++) {
        g_free(task->job.data[n]);
    }
    g_free(task);
    aio_wait_kick();
    object_unref(OBJECT(s));
}

static void statistics_computed(void *opaque, int ret)
{
    StatisticsTask *task = opaque;
    NikonStatistics *s = task->owner;

    task->ready = true;
    task->success = ret == 0;
    if (s->stopping || !s->active || task->token != s->token) {
        object_ref(OBJECT(s));
        statistics_task_free(task);
        if (!s->stopping) {
            statistics_rise(s, 0, 1);
        }
        object_unref(OBJECT(s));
        return;
    }
    /* Host completion cannot publish while the virtual clock is stopped. */
    timer_mod(s->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
}

static void statistics_complete(void *opaque)
{
    NikonStatistics *s = opaque;
    StatisticsTask *task = s->compute;
    StatisticsJob *j;
    StatisticsOutput out[10] = { 0 };
    uint32_t causes[2] = { 0x40, 0 };
    uint32_t stage[2] = { 0x20, 0 };
    uint64_t token = s->token;

    if (!s->active) {
        return;
    }
    assert(task && task->token == s->token);
    j = &task->job;
    if ((statistics_banks(s) & j->banks) != j->banks) {
        statistics_cancel(s, true);
        return;
    }
    if (!statistics_outputs(j, out)) {
        s->rejected++;
        qemu_log_mask(LOG_GUEST_ERROR,
                      "nikon statistics rejected output geometry/route/RAM\n");
        goto out;
    }
    if (!task->submitted) {
        memcpy(j->out, out, sizeof(out));
        task->submitted = true;
        s->inflight++;
        thread_pool_submit_aio(statistics_compute, task,
                               statistics_computed, task);
        return;
    }
    assert(task->ready);
    if (!task->success) {
        s->rejected++;
        goto out;
    }
    for (unsigned n = 0; n < 10; n++) {
        const StatisticsOutput *o = &j->out[n];

        if (o->bytes &&
            dma_memory_write(&address_space_memory, o->address, j->data[n],
                             o->bytes, MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
            s->rejected++;
            goto out;
        }
    }
    nikon_capture_banks_results(s->banks, j->banks, j->results);
    nikon_capture_endpoints_results(s->endpoints, j->scalars);

    /* Mode1's source consumer switches endpoint1 after its ring completes. */
    if (out[1].bytes && j->cursor[1] == endpoint(j->configuration, 1, 0x48)) {
        stage[0] |= 0x80;
    }
    s->completed++;
    for (unsigned n = 0; n < 10; n++) {
        if (out[n].bytes && j->revision == s->revision) {
            s->cursor[n] = (j->cursor[n] + 1) %
                           (endpoint(j->configuration, n, 0x48) + 1);
        }
    }
    s->active = false;
    object_ref(OBJECT(s));
    statistics_task_free(task);
    /* Synchronous consumers may ACK, stop or reset from either event. */
    /* sw8 configures these outputs; its fence follows this job's real work. */
    nikon_capture_publish(s->capture, NIKON_CAPTURE_THIRD_EVENT,
                          stage, 2, &error_abort);
    if (!s->stopping && token == s->token) {
        nikon_capture_publish(s->capture, NIKON_CAPTURE_FIRST_EVENT,
                              causes, 2, &error_abort);
    }
    object_unref(OBJECT(s));
    return;
out:
    s->active = false;
    statistics_task_free(task);
}

static void statistics_rise(void *opaque, int n, int level)
{
    NikonStatistics *s = opaque;
    NikonCaptureInput input;
    StatisticsTask *task;
    int64_t now;

    if (!level || s->stopping || !s->valid || !s->running || s->compute) {
        return;
    }
    if (nikon_capture_data_pending(s->capture)) {
        s->blocked++;
        return;
    }
    if (!nikon_readout_pipeline_input(s->pipeline, &input) ||
        input.generation == s->last_input) {
        return;
    }
    s->last_input = input.generation;
    now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    if (input.width < 2 || input.height < 2 ||
        (uint64_t)input.width * input.height > 4 * 1024 * 1024 ||
        now < 0 || now > INT64_MAX - 1000000) {
        s->rejected++;
        return;
    }
    task = g_new0(StatisticsTask, 1);
    task->owner = s;
    task->token = s->token;
    task->job.input_data = g_memdup2(input.data,
                                    (size_t)input.stride * input.height);
    task->job.input = input;
    task->job.input.data = task->job.input_data;
    memcpy(task->job.configuration, s->committed, sizeof(s->committed));
    task->job.revision = s->revision;
    memcpy(task->job.cursor, s->cursor, sizeof(s->cursor));
    task->job.banks = statistics_banks(s);
    object_ref(OBJECT(s));
    s->compute = task;
    s->active = true;
    /* Virtual computation latency, not guest-queue synchronization. */
    timer_mod(s->timer, now + 1000000);
}

static void statistics_select(void *opaque, int n, int level)
{
    NIKON_STATISTICS(opaque)->selected[n] = !!level;
}

static void statistics_commit(void *opaque, int n, int level)
{
    NikonStatistics *s = opaque;
    uint32_t words[NIKON_CAPTURE_ENDPOINT_WORDS];

    if (!level || !s->selected[n]) {
        return;
    }
    nikon_capture_endpoints_snapshot(s->endpoints, words);
    if (!s->valid || memcmp(s->committed, words, sizeof(words))) {
        memcpy(s->committed, words, sizeof(words));
        s->revision++;
        for (unsigned i = 0; i < 10; i++) {
            s->cursor[i] = endpoint(words, i, 0x44);
        }
    }
    s->valid = true;
}

static void statistics_run(void *opaque, int n, int level)
{
    NikonStatistics *s = opaque;

    s->running = !!level;
    if (!level) {
        statistics_cancel(s, true);
    }
}

static void statistics_bank_run(void *opaque, int n, int level)
{
    NikonStatistics *s = opaque;

    if (!level && s->active && (s->compute->job.banks & (1u << n))) {
        statistics_cancel(s, true);
    }
}

static void statistics_reset(DeviceState *dev)
{
    NikonStatistics *s = NIKON_STATISTICS(dev);

    statistics_cancel(s, false);
    s->valid = s->running = false;
    s->last_input = 0;
    s->revision = 0;
    memset(s->selected, 0, sizeof(s->selected));
    memset(s->committed, 0, sizeof(s->committed));
    memset(s->cursor, 0, sizeof(s->cursor));
}

static void statistics_unrealize(DeviceState *dev)
{
    NikonStatistics *s = NIKON_STATISTICS(dev);

    s->stopping = true;
    statistics_reset(dev);
    AIO_WAIT_WHILE(NULL, s->inflight);
}

static void statistics_realize(DeviceState *dev, Error **errp)
{
    NikonStatistics *s = NIKON_STATISTICS(dev);

    s->stopping = false;
    if (!s->endpoints || !s->banks || !s->pipeline || !s->capture) {
        error_setg(errp, "statistics requires endpoints, banks, pipeline, "
                   "capture");
    }
}

static void statistics_init(Object *obj)
{
    NikonStatistics *s = NIKON_STATISTICS(obj);
    DeviceState *dev = DEVICE(obj);

    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, statistics_complete, s);
    qdev_init_gpio_in_named(dev, statistics_select, "commit-selected", 2);
    qdev_init_gpio_in_named(dev, statistics_commit, "commit", 2);
    qdev_init_gpio_in_named(dev, statistics_run, "run", 1);
    qdev_init_gpio_in_named(dev, statistics_bank_run, "bank-run", 2);
    qdev_init_gpio_in_named(dev, statistics_rise, "input-ready", 1);
    qdev_init_gpio_in_named(dev, statistics_rise, "capacity-ready", 1);
    object_property_add_uint64_ptr(obj, "completed", &s->completed,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(obj, "rejected", &s->rejected,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(obj, "cancelled", &s->cancelled,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(obj, "blocked", &s->blocked,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(obj, "inflight", &s->inflight,
                                   OBJ_PROP_FLAG_READ);
}

static void statistics_finalize(Object *obj)
{
    NikonStatistics *s = NIKON_STATISTICS(obj);

    timer_free(s->timer);
    assert(!s->compute && !s->inflight);
}

static const Property statistics_properties[] = {
    DEFINE_PROP_LINK("endpoints", NikonStatistics, endpoints,
                     TYPE_NIKON_CAPTURE_ENDPOINTS, DeviceState *),
    DEFINE_PROP_LINK("banks", NikonStatistics, banks,
                     TYPE_NIKON_CAPTURE_BANKS, DeviceState *),
    DEFINE_PROP_LINK("pipeline", NikonStatistics, pipeline,
                     TYPE_NIKON_READOUT_PIPELINE, DeviceState *),
    DEFINE_PROP_LINK("capture", NikonStatistics, capture,
                     TYPE_NIKON_CAPTURE_CONTROL, DeviceState *),
};

static const VMStateDescription statistics_vmstate = {
    .name = TYPE_NIKON_STATISTICS,
    .unmigratable = true,
};

static void statistics_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = statistics_realize;
    dc->unrealize = statistics_unrealize;
    dc->user_creatable = false;
    dc->vmsd = &statistics_vmstate;
    device_class_set_props(dc, statistics_properties);
    device_class_set_legacy_reset(dc, statistics_reset);
}

static const TypeInfo statistics_type = {
    .name = TYPE_NIKON_STATISTICS,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonStatistics),
    .instance_init = statistics_init,
    .instance_finalize = statistics_finalize,
    .class_init = statistics_class_init,
};

static void statistics_register_types(void)
{
    type_register_static(&statistics_type);
}
type_init(statistics_register_types)
