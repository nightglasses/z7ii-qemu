/*
 * Finite capture-group metadata and declared RAW statistics reference profile.
 * TG cursor metadata is independent of owned input/DMA completion.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/bswap.h"
#include "qemu/bitops.h"
#include "qemu/timer.h"
#include "qemu/aio-wait.h"
#include "block/thread-pool.h"
#include "migration/vmstate.h"
#include "hw/core/sysbus.h"
#include "hw/core/qdev-properties.h"
#include "hw/nikon/devices.h"
#include "hw/nikon/readoutpipeline.h"
#include "hw/nikon/statistics.h"
#include "hw/nikon/sensor.h"
#include "system/address-spaces.h"
#include "system/dma.h"

#define CAPTURE_GROUP_SIZE 0x584
#define CAPTURE_GROUP_WORDS (CAPTURE_GROUP_SIZE / 4)
OBJECT_DECLARE_SIMPLE_TYPE(NikonCaptureGroupState, NIKON_CAPTURE_GROUP)

static const hwaddr result_offsets[] = {
    0x370, 0x374, 0x378, 0x478, 0x578, 0x37c, 0x47c, 0x57c,
};

/* Bank, initial unit, count-minus-one, three strides, two three-address sets. */
static const hwaddr cursor_offsets[] = {
    0x33c, 0x344, 0x348, 0x380, 0x480, 0x580,
    0x350, 0x450, 0x550, 0x360, 0x460, 0x560,
};

typedef struct GroupRectangle {
    unsigned x, y, width, height;
} GroupRectangle;

typedef struct GroupJob {
    uint32_t configuration[CAPTURE_GROUP_WORDS];
    uint32_t results[ARRAY_SIZE(result_offsets)];
    NikonCaptureInput input;
    uint8_t *input_data, *data;
    GroupRectangle roi[2];
    unsigned width, height, cells, sizes[3];
    uint32_t address[3];
    int coefficients[3][8], offsets[3][8];
    unsigned taps[3], magnitude[3];
} GroupJob;

typedef struct GroupTask {
    NikonCaptureGroupState *owner;
    GroupJob job;
    uint64_t token;
    bool submitted, ready, success;
} GroupTask;

struct NikonCaptureGroupState {
    SysBusDevice parent_obj;
    MemoryRegion memory;
    uint32_t configuration[CAPTURE_GROUP_WORDS];
    enum {
        GROUP_RESULT_COLD,
        GROUP_RESULT_UNKNOWN,
        GROUP_RESULT_PUBLISHED,
    } result_state;
    bool commit_selected[2];
    uint32_t active[ARRAY_SIZE(cursor_offsets)];
    uint32_t results[ARRAY_SIZE(result_offsets)];
    uint16_t cursor_written;
    unsigned cursor;
    bool active_valid;
    bool running;
    bool started;
    bool reported_error;
    DeviceState *pipeline, *capture;
    QEMUTimer *timer;
    uint32_t committed[CAPTURE_GROUP_WORDS];
    GroupTask *compute;
    uint64_t token, inflight;
    bool busy, stopping;
    uint64_t last_input, completed, rejected, cancelled, blocked;
};

static void group_task_free(GroupTask *task);
static void group_input_ready(void *opaque, int index, int level);

static void group_cancel(NikonCaptureGroupState *s)
{
    s->token++;
    if (s->busy) {
        timer_del(s->timer);
        s->busy = false;
        s->cancelled++;
    }
    if (s->compute && (!s->compute->submitted || s->compute->ready)) {
        group_task_free(s->compute);
    }
}

static uint32_t group_word(const GroupJob *j, unsigned offset)
{
    return j->configuration[offset / 4];
}

static bool group_ram(uint64_t address, uint64_t bytes)
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
            return false;
        }
        address += span;
        bytes -= span;
    }
    return true;
}

static bool group_rectangle(GroupRectangle r, unsigned width, unsigned height)
{
    return r.width && r.height && r.x < width && r.y < height &&
           r.width <= width - r.x && r.height <= height - r.y;
}

/* The declared numerical profile has no undocumented correction modes. */
static bool group_profile(GroupJob *j, GroupRectangle roi[2],
                          unsigned *width, unsigned *height, unsigned *cells)
{
    static const unsigned zero[] = {
        0x18, 0x1c, 0x184, 0x200, 0x204, 0x210, 0x214, 0x218,
        0x21c, 0x220, 0x224, 0x228, 0x294, 0x298, 0x29c, 0x2a0,
        0x2a4, 0x2a8, 0x2ac, 0x2b0, 0x2b4, 0x2b8,
    };
    unsigned columns = group_word(j, 0) + 1;
    unsigned rows = group_word(j, 4) + 1;
    unsigned step_x = group_word(j, 0x10), step_y = group_word(j, 0x14);
    unsigned families = group_word(j, 0x22c);

    *width = group_word(j, 0x208);
    *height = group_word(j, 0x20c);
    *cells = columns * rows;
    if (*width > 16384 || *height > 16384 || !*width || !*height ||
        !families || families > 3 ||
        ((families & 1) && group_word(j, 0x230) != 1)) {
        return false;
    }
    for (unsigned i = 0; i < ARRAY_SIZE(zero); i++) {
        if (group_word(j, zero[i])) {
            return false;
        }
    }
    for (unsigned i = 0; (families & 1) && i < 2; i++) {
        unsigned base = 0x234 + i * 16;

        roi[i] = (GroupRectangle) {
            group_word(j, base), group_word(j, base + 4),
            group_word(j, base + 8), group_word(j, base + 12),
        };
        if (!group_rectangle(roi[i], *width, *height)) {
            return false;
        }
    }
    if ((families & 2) && (columns > 9 || rows > 9 || !step_x || !step_y ||
        group_word(j, 8) < step_x / 2 ||
        group_word(j, 12) < step_y / 2 ||
        (uint64_t)group_word(j, 8) + (columns - 1) * step_x +
            (step_x + 1) / 2 > *width ||
        (uint64_t)group_word(j, 12) + (rows - 1) * step_y +
            (step_y + 1) / 2 > *height)) {
        return false;
    }
    for (unsigned i = 0; i < 65; i++) {
        unsigned value = group_word(j, 0x80 + i * 4);

        if (i && value < group_word(j, 0x80 + (i - 1) * 4)) {
            return false;
        }
    }
    if (group_word(j, 0x80) == group_word(j, 0x180)) {
        return false;
    }
    return true;
}

static uint64_t group_box(const uint64_t *integral, unsigned pitch,
                          unsigned x0, unsigned y0, unsigned x1, unsigned y1)
{
    return integral[y1 * pitch + x1] + integral[y0 * pitch + x0] -
           integral[y0 * pitch + x1] - integral[y1 * pitch + x0];
}

static void group_record(const uint64_t *integral, unsigned width,
                         unsigned height, unsigned domain_width,
                         unsigned domain_height, GroupRectangle r,
                         uint8_t *out, unsigned kind)
{
    size_t plane = (size_t)(width + 1) * (height + 1);
    unsigned x0 = (uint64_t)r.x * width / domain_width;
    unsigned y0 = (uint64_t)r.y * height / domain_height;
    unsigned x1 = DIV_ROUND_UP((uint64_t)(r.x + r.width) * width, domain_width);
    unsigned y1 = DIV_ROUND_UP((uint64_t)(r.y + r.height) * height, domain_height);
    uint64_t samples = (x1 - x0) * (y1 - y0);
    uint64_t count = (uint64_t)r.width * r.height;
    uint64_t aggregate[5];
    unsigned p = 0;

    for (unsigned i = 0; i < 5; i++) {
        uint64_t sum = group_box(integral + plane * i, width + 1,
                                  x0, y0, x1, y1);

        aggregate[i] = sum / samples * count + sum % samples * count / samples;
        if (i >= 2) {
            p = MAX(p, aggregate[i] / count);
        }
    }
    if (kind == 1) {
        stl_le_p(out, MIN(aggregate[0], UINT32_MAX));
        stl_le_p(out + 4, count);
        for (unsigned i = 0; i < 3; i++) {
            stl_le_p(out + 8 + i * 4, MIN(aggregate[i + 2], UINT32_MAX));
        }
        return;
    }
    uint32_t sum23 = MIN(aggregate[0], 0x7fffff);
    uint32_t energy29 = MIN(aggregate[1], 0x1fffffff);

    stl_le_p(out, MIN(p, 0xfff) | (sum23 << 12));
    stl_le_p(out + 4, (sum23 >> 20) | (energy29 << 3));
    stl_le_p(out + 8, MIN(aggregate[0], UINT32_MAX));
    stl_le_p(out + 12, count);
    stl_le_p(out + 16, MIN(aggregate[2], UINT32_MAX));
    stw_le_p(out + 20, aggregate[0] / count);
    stl_le_p(out + 24, MIN(aggregate[3], UINT32_MAX));
    stw_le_p(out + 28, r.x + r.width / 2);
    if (!kind) {
        stl_le_p(out + 32, MIN(aggregate[4], UINT32_MAX));
        stw_le_p(out + 36, r.y + r.height / 2);
    }
}

static bool group_routes(GroupJob *j)
{
    /* Validate both address sets and every ring slot before the first write. */
    for (unsigned bank = 0; bank < 2; bank++) {
        for (unsigned i = 0; i < 3; i++) {
            uint32_t base = group_word(j, 0x350 + bank * 16 + i * 0x100);
            uint32_t stride = group_word(j, 0x380 + i * 0x100);
            unsigned count = group_word(j, 0x348) + 1;

            if (!j->sizes[i]) {
                continue;
            }
            if (j->sizes[i] > stride) {
                return false;
            }
            for (unsigned slot = 0; slot < count; slot++) {
                uint64_t a = (uint64_t)base + (uint64_t)slot * stride;

                if (!group_ram(a, j->sizes[i])) {
                    return false;
                }
                for (unsigned family = 0; family < i; family++) {
                    if (!j->sizes[family]) {
                        continue;
                    }
                    for (unsigned other = 0; other < count; other++) {
                        uint64_t b = (uint64_t)group_word(j,
                            0x350 + bank * 16 + family * 0x100) +
                            (uint64_t)other *
                            group_word(j, 0x380 + family * 0x100);

                        if (a < b + j->sizes[family] && b < a + j->sizes[i]) {
                            return false;
                        }
                    }
                }
            }
            if (bank == j->results[0]) {
                j->address[i] = base + j->results[1] * stride;
            }
        }
    }
    return true;
}

static bool group_prepare(GroupJob *j)
{
    if (!group_profile(j, j->roi, &j->width, &j->height, &j->cells)) {
        return false;
    }
    /* Omit only zero contributions, preserving signed taps and their order. */
    for (unsigned f = 0; f < 3; f++) {
        for (unsigned tap = 0; tap < 8; tap++) {
            int coefficient = sextract32(group_word(j,
                0x20 + f * 32 + tap * 4), 0, tap ? 7 : 8);

            if (coefficient) {
                unsigned at = j->taps[f]++;

                j->coefficients[f][at] = coefficient;
                j->offsets[f][at] = (int)tap - 3;
                j->magnitude[f] += abs(coefficient);
            }
        }
        if (!j->magnitude[f]) {
            return false;
        }
    }
    j->sizes[0] = group_word(j, 0x22c) & 1 ? 40 : 0;
    j->sizes[1] = group_word(j, 0x22c) & 1 ? 24 : 0;
    j->sizes[2] = group_word(j, 0x22c) & 2 ? j->cells * 32 : 0;
    return group_routes(j);
}

static int group_compute(void *opaque)
{
    GroupTask *task = opaque;
    GroupJob *j = &task->job;
    unsigned iw = j->input.width, ih = j->input.height;
    size_t plane = (size_t)(iw + 1) * (ih + 1);
    g_autofree uint64_t *integral = NULL;
    g_autofree uint16_t *pixels = NULL;
    uint16_t lut[65536];
    int min_off = 0, max_off = 0;

    for (unsigned raw = 0; raw < 65536; raw++) {
        unsigned index = raw >> 10, fraction = raw & 1023;
        unsigned a = group_word(j, 0x80 + index * 4);
        unsigned b = group_word(j, 0x84 + index * 4);

        lut[raw] = ((a * (1024 - fraction) + b * fraction) / 1024) >> 4;
    }

    pixels = g_new(uint16_t, (size_t)iw * ih);
    for (unsigned y = 0; y < ih; y++) {
        const uint8_t *src = j->input_data + y * j->input.stride;
        uint16_t *dst = pixels + y * iw;

        for (unsigned x = 0; x < iw; x++) {
            dst[x] = lut[lduw_le_p(src + x * 2)];
        }
    }

    for (unsigned f = 0; f < 3; f++) {
        for (unsigned tap = 0; tap < j->taps[f]; tap++) {
            if (j->offsets[f][tap] < min_off) {
                min_off = j->offsets[f][tap];
            }
            if (j->offsets[f][tap] > max_off) {
                max_off = j->offsets[f][tap];
            }
        }
    }
    unsigned safe_x0 = (unsigned)(-min_off);
    unsigned safe_x1 = iw > (unsigned)max_off ? iw - (unsigned)max_off : 0;
    if (safe_x0 > iw) {
        safe_x0 = iw;
    }
    if (safe_x1 < safe_x0) {
        safe_x1 = safe_x0;
    }

    integral = g_new(uint64_t, plane * 5);
    for (unsigned i = 0; i < 5; i++) {
        memset(integral + i * plane, 0, (iw + 1) * sizeof(uint64_t));
    }

    for (unsigned y = 0; y < ih; y++) {
        const uint16_t *row_pixels = pixels + y * iw;
        uint64_t row[5] = { 0 };
        uint64_t *prev[5], *curr[5];

        for (unsigned i = 0; i < 5; i++) {
            prev[i] = integral + i * plane + y * (iw + 1);
            curr[i] = integral + i * plane + (y + 1) * (iw + 1);
            curr[i][0] = 0;
        }

        for (unsigned x = 0; x < safe_x0; x++) {
            unsigned value = row_pixels[x];
            uint64_t values[5] = { value, (uint64_t)value * value };

            for (unsigned f = 0; f < 3; f++) {
                int response = 0;

                for (unsigned tap = 0; tap < j->taps[f]; tap++) {
                    unsigned sx = CLAMP((int)x + j->offsets[f][tap],
                                        0, (int)iw - 1);

                    response += j->coefficients[f][tap] * row_pixels[sx];
                }
                values[2 + f] = abs(response) / j->magnitude[f];
            }
            for (unsigned i = 0; i < 5; i++) {
                row[i] += values[i];
                curr[i][x + 1] = prev[i][x + 1] + row[i];
            }
        }

        for (unsigned x = safe_x0; x < safe_x1; x++) {
            unsigned value = row_pixels[x];
            uint64_t values[5] = { value, (uint64_t)value * value };

            for (unsigned f = 0; f < 3; f++) {
                int response = 0;

                for (unsigned tap = 0; tap < j->taps[f]; tap++) {
                    response += j->coefficients[f][tap] *
                                row_pixels[x + j->offsets[f][tap]];
                }
                values[2 + f] = abs(response) / j->magnitude[f];
            }
            for (unsigned i = 0; i < 5; i++) {
                row[i] += values[i];
                curr[i][x + 1] = prev[i][x + 1] + row[i];
            }
        }

        for (unsigned x = safe_x1; x < iw; x++) {
            unsigned value = row_pixels[x];
            uint64_t values[5] = { value, (uint64_t)value * value };

            for (unsigned f = 0; f < 3; f++) {
                int response = 0;

                for (unsigned tap = 0; tap < j->taps[f]; tap++) {
                    unsigned sx = CLAMP((int)x + j->offsets[f][tap],
                                        0, (int)iw - 1);

                    response += j->coefficients[f][tap] * row_pixels[sx];
                }
                values[2 + f] = abs(response) / j->magnitude[f];
            }
            for (unsigned i = 0; i < 5; i++) {
                row[i] += values[i];
                curr[i][x + 1] = prev[i][x + 1] + row[i];
            }
        }
    }
    j->data = g_malloc0(j->sizes[0] + j->sizes[1] + j->sizes[2]);
    unsigned offset = 0;

    for (unsigned i = 0; i < 3; i++) {
        if (!j->sizes[i]) {
            continue;
        }
        if (i < 2) {
            group_record(integral, iw, ih, j->width, j->height, j->roi[i],
                         j->data + offset, i);
        } else {
            unsigned columns = group_word(j, 0) + 1;
            unsigned sx = group_word(j, 0x10), sy = group_word(j, 0x14);

            for (unsigned cell = 0; cell < j->cells; cell++) {
                GroupRectangle r = {
                    group_word(j, 8) + cell % columns * sx - sx / 2,
                    group_word(j, 12) + cell / columns * sy - sy / 2, sx, sy,
                };

                group_record(integral, iw, ih, j->width, j->height, r,
                              j->data + offset + cell * 32, 2);
            }
        }
        offset += j->sizes[i];
    }
    return 0;
}

static void group_task_free(GroupTask *task)
{
    NikonCaptureGroupState *s = task->owner;

    assert(s->compute == task);
    s->compute = NULL;
    if (task->submitted) {
        assert(s->inflight == 1);
        s->inflight--;
    }
    g_free(task->job.input_data);
    g_free(task->job.data);
    g_free(task);
    aio_wait_kick();
    object_unref(OBJECT(s));
}

static void group_computed(void *opaque, int ret)
{
    GroupTask *task = opaque;
    NikonCaptureGroupState *s = task->owner;

    task->ready = true;
    task->success = ret == 0;
    if (s->stopping || !s->busy || task->token != s->token) {
        object_ref(OBJECT(s));
        group_task_free(task);
        if (!s->stopping) {
            group_input_ready(s, 0, 1);
        }
        object_unref(OBJECT(s));
        return;
    }
    /* Host completion cannot publish while the virtual clock is stopped. */
    timer_mod(s->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
}

static void group_complete(void *opaque)
{
    NikonCaptureGroupState *s = opaque;
    GroupTask *task = s->compute;
    GroupJob *j;
    g_autofree uint8_t *data = NULL;
    unsigned offset = 0;

    if (!s->busy) {
        return;
    }
    assert(task && task->token == s->token);
    if (!task->submitted) {
        task->submitted = true;
        s->inflight++;
        thread_pool_submit_aio(group_compute, task, group_computed, task);
        return;
    }
    assert(task->ready);
    s->busy = false;
    j = &task->job;
    if (nikon_capture_group_pending(s->capture)) {
        s->blocked++;
        goto out;
    }
    if (!task->success || !group_routes(j)) {
        s->rejected++;
        goto out;
    }
    data = g_malloc(j->sizes[0] + j->sizes[1] + j->sizes[2]);
    /* Preserve unclassified destination bytes as they exist at publication. */
    for (unsigned i = 0; i < 3; i++) {
        if (!j->sizes[i]) {
            continue;
        }
        if (dma_memory_read(&address_space_memory, j->address[i], data + offset,
                            j->sizes[i], MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
            s->rejected++;
            goto out;
        }
        unsigned records = i == 2 ? j->cells : 1;
        unsigned stride = i == 0 ? 40 : i == 1 ? 24 : 32;

        for (unsigned record = 0; record < records; record++) {
            uint8_t *to = data + offset + record * stride;
            const uint8_t *from = j->data + offset + record * stride;

            memcpy(to, from, 20);
            if (i != 1) {
                memcpy(to + 20, from + 20, 2);
                memcpy(to + 24, from + 24, 6);
                if (i == 0) {
                    memcpy(to + 32, from + 32, 6);
                }
            }
        }
        offset += j->sizes[i];
    }
    offset = 0;
    for (unsigned i = 0; i < 3; i++) {
        if (!j->sizes[i]) {
            continue;
        }
        if (dma_memory_write(&address_space_memory, j->address[i],
                             data + offset, j->sizes[i],
                             MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
            s->rejected++;
            goto out;
        }
        offset += j->sizes[i];
    }
    memcpy(s->results, j->results, sizeof(s->results));
    s->result_state = GROUP_RESULT_PUBLISHED;
    uint32_t causes[2] = { 1, 0 };

    s->completed++;
    group_task_free(task);
    /* Synchronous consumers may ACK, stop or reset from the event callback. */
    nikon_capture_publish(s->capture, NIKON_CAPTURE_THIRD_EVENT,
                          causes, 2, &error_abort);
    return;
out:
    group_task_free(task);
}

static int nikon_capture_group_result(hwaddr offset)
{
    for (unsigned i = 0; i < ARRAY_SIZE(result_offsets); i++) {
        if (offset == result_offsets[i]) {
            return i;
        }
    }
    return -1;
}

static bool nikon_capture_group_cursor_valid(NikonCaptureGroupState *s)
{
    const uint32_t *a = s->active;

    if (s->cursor_written != (1u << ARRAY_SIZE(cursor_offsets)) - 1 ||
        a[0] > 1 || a[1] > a[2]) {
        return false;
    }
    for (unsigned i = 0; i < 6; i++) {
        uint32_t stride = a[3 + i % 3];

        if (!stride || (uint64_t)a[6 + i] + (uint64_t)a[2] * stride >
                       UINT32_MAX) {
            return false;
        }
    }
    return true;
}

static void group_cursor_packet(NikonCaptureGroupState *s, unsigned cursor,
                                uint32_t results[ARRAY_SIZE(result_offsets)])
{
    results[0] = s->active[0];
    results[1] = cursor;
    for (unsigned i = 0; i < 6; i++) {
        results[2 + i] = s->active[6 + i] + cursor * s->active[3 + i % 3];
    }
}

static void group_input_ready(void *opaque, int index, int level)
{
    NikonCaptureGroupState *s = opaque;

    if (!level || s->stopping || !s->running || !s->active_valid) {
        return;
    }
    if (s->pipeline && s->capture && !s->compute && s->committed[0x22c / 4]) {
        NikonCaptureInput input;
        GroupTask *task;
        unsigned cursor = s->started ? s->cursor : s->active[1];

        if (nikon_capture_group_pending(s->capture)) {
            s->blocked++;
            return;
        }
        if (!nikon_readout_pipeline_main_input(s->pipeline, &input) ||
            !input.synchronized || input.generation == s->last_input) {
            return;
        }
        s->last_input = input.generation;
        if (!input.width || !input.height || input.width > 16384 ||
            input.height > 16384 || input.stride != input.width * 2 ||
            (uint64_t)input.width * input.height > 4 * 1024 * 1024) {
            s->rejected++;
            return;
        }
        task = g_new0(GroupTask, 1);
        task->owner = s;
        task->token = s->token;
        memcpy(task->job.configuration, s->committed, sizeof(s->committed));
        /* A completion latch may describe an older job than the TG cursor. */
        group_cursor_packet(s, cursor, task->job.results);
        if (!group_prepare(&task->job)) {
            s->rejected++;
            g_free(task);
            return;
        }
        task->job.input = input;
        task->job.input_data = g_memdup2(input.data,
                                        (size_t)input.stride * input.height);
        task->job.input.data = task->job.input_data;
        object_ref(OBJECT(s));
        s->compute = task;
        s->busy = true;
        /* A fresh valid job owns the initial cursor before any later TG. */
        s->cursor = cursor;
        s->started = true;
        timer_mod(s->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000000);
    }
}

static void nikon_capture_group_automatic_rise(void *opaque, int index, int level)
{
    NikonCaptureGroupState *s = opaque;

    if (!level || !s->running || !s->active_valid) {
        return;
    }
    /* Functional phase: first edge selects start, later edges wrap by count. */
    s->cursor = s->started ? (s->cursor + 1) % (s->active[2] + 1) :
                            s->active[1];
    s->started = true;
    group_cursor_packet(s, s->cursor, s->results);
    s->result_state = GROUP_RESULT_PUBLISHED;
    group_input_ready(s, 0, 1);
}

static void nikon_capture_group_commit_selected(void *opaque, int index,
                                               int level)
{
    NikonCaptureGroupState *s = opaque;

    s->commit_selected[index] = !!level;
}

static void nikon_capture_group_commit(void *opaque, int index, int level)
{
    NikonCaptureGroupState *s = opaque;

    if (level && s->commit_selected[index]) {
        NikonCaptureInput input;

        group_cancel(s);
        if (s->pipeline && nikon_readout_pipeline_main_input(s->pipeline, &input)) {
            s->last_input = input.generation;
        }
        memcpy(s->committed, s->configuration, sizeof(s->committed));
        /* Commands change the generator, not the last published packet.
         * 1023aed8 ->10239dc6(force0) reads it before the next shared commit.
         * Latch retention is a functional hypothesis, not a fresh result.
         */
        if (s->result_state == GROUP_RESULT_COLD) {
            s->result_state = GROUP_RESULT_UNKNOWN;
        }
        s->started = false;
        for (unsigned i = 0; i < ARRAY_SIZE(cursor_offsets); i++) {
            s->active[i] = s->configuration[cursor_offsets[i] / 4];
        }
        s->active_valid = nikon_capture_group_cursor_valid(s);
    }
}

static void nikon_capture_group_run(void *opaque, int index, int level)
{
    NikonCaptureGroupState *s = opaque;

    if (s->running != !!level) {
        NikonCaptureInput input;

        group_cancel(s);
        if (s->pipeline && nikon_readout_pipeline_main_input(s->pipeline, &input)) {
            s->last_input = input.generation;
        }
        s->running = !!level;
        s->started = false;
        /* Run transitions cannot create results or restore cold-empty. */
        if (s->result_state == GROUP_RESULT_COLD) {
            s->result_state = GROUP_RESULT_UNKNOWN;
        }
    }
}

static bool nikon_capture_group_mask(hwaddr offset, uint32_t *mask)
{
    if (offset <= 0x1c) {
        *mask = offset <= 4 || offset == 0x1c ? 0xff : 0xffff;
    } else if (offset >= 0x20 && offset <= 0x7c) {
        /* Three CB0 rows: a complete byte followed by seven masked bytes. */
        *mask = (offset - 0x20) % 0x20 == 0 ? 0xff : 0x7f;
    } else if ((offset >= 0x80 && offset <= 0x180) ||
               (offset >= 0x200 && offset <= 0x20c) ||
               (offset >= 0x234 && offset <= 0x250) ||
               (offset >= 0x2a4 && offset <= 0x2ac)) {
        *mask = 0xffff;
    } else if ((offset >= 0x214 && offset <= 0x228) ||
               (offset >= 0x2b0 && offset <= 0x2b8)) {
        *mask = 0xff;
    } else if (offset >= 0x350 && offset <= 0x580 &&
               ((offset & 0xff) == 0x50 || (offset & 0xff) == 0x60 ||
                (offset & 0xff) == 0x80)) {
        /* Three original provider slots, two address sets and one extra word. */
        *mask = UINT32_MAX;
    } else {
        switch (offset) {
        case 0x184: case 0x210: case 0x22c: case 0x2a0: case 0x33c:
            *mask = UINT32_MAX;
            break;
        case 0x230:
            /* Only the source-provided count0/1 packet footprint is known. */
            *mask = 1;
            break;
        case 0x294:
            *mask = 0xffff;
            break;
        case 0x298: case 0x29c:
            *mask = 0x7fffff;
            break;
        case 0x318: case 0x324: case 0x344: case 0x348: case 0x34c:
            *mask = 0xff;
            break;
        default:
            return false;
        }
    }
    return true;
}

static MemTxResult nikon_capture_group_error(NikonCaptureGroupState *s,
                                            hwaddr offset, unsigned size,
                                            bool write, uint64_t value)
{
    nikon_access_error(&s->reported_error, s->memory.addr + offset,
                       size, write, value,
                       "capture-group-configuration-not-supported");
    return MEMTX_ERROR;
}

static MemTxResult nikon_capture_group_read(void *opaque, hwaddr offset,
                                           uint64_t *value, unsigned size,
                                           MemTxAttrs attrs)
{
    NikonCaptureGroupState *s = opaque;
    uint32_t mask;
    int result;

    *value = 0;
    if (size != 4 || (offset & 3)) {
        return nikon_capture_group_error(s, offset, size, false, 0);
    }
    result = nikon_capture_group_result(offset);
    if (result >= 0) {
        if (s->result_state == GROUP_RESULT_UNKNOWN) {
            return nikon_capture_group_error(s, offset, size, false, 0);
        }
        *value = s->results[result];
        return MEMTX_OK;
    }
    if (!nikon_capture_group_mask(offset, &mask)) {
        return nikon_capture_group_error(s, offset, size, false, 0);
    }
    *value = s->configuration[offset / 4];
    return MEMTX_OK;
}

static MemTxResult nikon_capture_group_write(void *opaque, hwaddr offset,
                                            uint64_t value, unsigned size,
                                            MemTxAttrs attrs)
{
    NikonCaptureGroupState *s = opaque;
    uint32_t mask;

    if (size != 4 || (offset & 3) ||
        !nikon_capture_group_mask(offset, &mask) || (value & ~mask)) {
        return nikon_capture_group_error(s, offset, size, true, value);
    }
    s->configuration[offset / 4] = value;
    for (unsigned i = 0; i < ARRAY_SIZE(cursor_offsets); i++) {
        if (offset == cursor_offsets[i]) {
            s->cursor_written |= 1u << i;
            break;
        }
    }
    return MEMTX_OK;
}

static const MemoryRegionOps nikon_capture_group_ops = {
    .read_with_attrs = nikon_capture_group_read,
    .write_with_attrs = nikon_capture_group_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4,
               .unaligned = false },
};

static void nikon_capture_group_reset(DeviceState *dev)
{
    NikonCaptureGroupState *s = NIKON_CAPTURE_GROUP(dev);

    group_cancel(s);
    s->last_input = 0;
    memset(s->committed, 0, sizeof(s->committed));
    memset(s->configuration, 0, sizeof(s->configuration));
    memset(s->commit_selected, 0, sizeof(s->commit_selected));
    memset(s->active, 0, sizeof(s->active));
    memset(s->results, 0, sizeof(s->results));
    s->cursor_written = 0;
    s->cursor = 0;
    s->active_valid = false;
    s->running = false;
    s->started = false;
    s->result_state = GROUP_RESULT_COLD;
    s->reported_error = false;
}

static void nikon_capture_group_unrealize(DeviceState *dev)
{
    NikonCaptureGroupState *s = NIKON_CAPTURE_GROUP(dev);

    s->stopping = true;
    nikon_capture_group_reset(dev);
    AIO_WAIT_WHILE(NULL, s->inflight);
}

static void nikon_capture_group_init(Object *obj)
{
    NikonCaptureGroupState *s = NIKON_CAPTURE_GROUP(obj);

    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, group_complete, s);
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
    memory_region_init_io(&s->memory, obj, &nikon_capture_group_ops, s,
                          "nikon.capture-group", CAPTURE_GROUP_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->memory);
    qdev_init_gpio_in_named(DEVICE(s), nikon_capture_group_commit_selected,
                           "commit-selected", 2);
    qdev_init_gpio_in_named(DEVICE(s), nikon_capture_group_commit, "commit", 2);
    qdev_init_gpio_in_named(DEVICE(s), nikon_capture_group_run, "run", 1);
    qdev_init_gpio_in_named(DEVICE(s), group_input_ready, "input-ready", 1);
    qdev_init_gpio_in_named(DEVICE(s), group_input_ready, "capacity-ready", 1);
    qdev_init_gpio_in_named(DEVICE(s), nikon_capture_group_automatic_rise,
                           "automatic-rise", 1);
}

static void nikon_capture_group_finalize(Object *obj)
{
    NikonCaptureGroupState *s = NIKON_CAPTURE_GROUP(obj);

    timer_free(s->timer);
    assert(!s->compute && !s->inflight);
}

static const Property group_properties[] = {
    DEFINE_PROP_LINK("pipeline", NikonCaptureGroupState, pipeline,
                     TYPE_NIKON_READOUT_PIPELINE, DeviceState *),
    DEFINE_PROP_LINK("capture", NikonCaptureGroupState, capture,
                     TYPE_NIKON_CAPTURE_CONTROL, DeviceState *),
};

static const VMStateDescription nikon_capture_group_vmstate = {
    .name = "nikon-capture-group",
    .unmigratable = true,
};

static void nikon_capture_group_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &nikon_capture_group_vmstate;
    dc->user_creatable = false;
    device_class_set_props(dc, group_properties);
    dc->unrealize = nikon_capture_group_unrealize;
    device_class_set_legacy_reset(dc, nikon_capture_group_reset);
}

static const TypeInfo nikon_capture_group_type = {
    .name = TYPE_NIKON_CAPTURE_GROUP,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonCaptureGroupState),
    .instance_init = nikon_capture_group_init,
    .instance_finalize = nikon_capture_group_finalize,
    .class_init = nikon_capture_group_class_init,
};

static void nikon_capture_group_register_types(void)
{
    type_register_static(&nikon_capture_group_type);
}

type_init(nikon_capture_group_register_types)
