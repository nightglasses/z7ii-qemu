/*
 * Guest-configured CMOS frame transport.
 *
 * The saved C1.70 observations stop before a sensor receiver, pixel format,
 * buffer descriptor or completion producer is identified.  This device keeps
 * those words observable and only runs after the guest supplies a complete
 * transport configuration. An explicit PGM/PPM image is sampled as the input;
 * deterministic bytes remain only for fixture-provider tests.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qapi/error.h"
#include "qemu/timer.h"
#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "migration/vmstate.h"
#include "system/address-spaces.h"
#include "system/dma.h"
#include "hw/nikon/cmos.h"
#include "hw/nikon/devices.h"

OBJECT_DECLARE_SIMPLE_TYPE(NikonCmosState, NIKON_CMOS)

#define CMOS_CHUNK 4096
#define CMOS_MAX_BUFFER_BYTES (64ULL * 1024 * 1024)
#define CMOS_UNKNOWN_WORDS 8

enum {
    CMOS_MODE = 0x000,
    CMOS_WIDTH = 0x004,
    CMOS_HEIGHT = 0x008,
    CMOS_CROP_X = 0x00c,
    CMOS_CROP_Y = 0x010,
    CMOS_CROP_WIDTH = 0x014,
    CMOS_CROP_HEIGHT = 0x018,
    CMOS_SCALE_X = 0x01c,
    CMOS_SCALE_Y = 0x020,
    CMOS_FORMAT = 0x024,
    CMOS_BYTES_PER_PIXEL = 0x028,
    CMOS_STRIDE = 0x02c,
    CMOS_BUFFER_COUNT = 0x030,
    CMOS_BUFFER_INDEX = 0x034,
    CMOS_DEST0 = 0x038,
    CMOS_DEST1 = 0x03c,
    CMOS_DEST2 = 0x040,
    CMOS_DEST3 = 0x044,
    CMOS_EXPOSURE_LO = 0x048,
    CMOS_EXPOSURE_HI = 0x04c,
    CMOS_PERIOD_LO = 0x050,
    CMOS_PERIOD_HI = 0x054,
    CMOS_READOUT_LO = 0x058,
    CMOS_READOUT_HI = 0x05c,
    CMOS_START = 0x060,
    CMOS_CANCEL = 0x064,
    CMOS_STATUS = 0x068,
    CMOS_IRQ_MASK = 0x06c,
    CMOS_IRQ_STATUS = 0x070,
    CMOS_IRQ_ACK = 0x074,
    CMOS_FRAME_COUNT = 0x078,
    CMOS_ACTIVE_BUFFER = 0x07c,
    CMOS_BAYER = 0x080,
    CMOS_BIT_DEPTH = 0x084,
    CMOS_PACKING = 0x088,
    CMOS_DMA_SOURCE = 0x08c,
    CMOS_DMA_DESTINATION = 0x090,
    CMOS_TRANSPORT_SEED = 0x094,
    CMOS_UNKNOWN_BASE = 0x100,
};

typedef struct NikonCmosConfig {
    uint32_t width, height;
    uint32_t crop_x, crop_y, crop_width, crop_height;
    uint32_t scale_x, scale_y;
    uint32_t format, transport_seed, bytes_per_pixel, stride, buffer_count;
    uint32_t destination[4];
    uint64_t exposure_ns, period_ns, readout_ns;
} NikonCmosConfig;

struct NikonCmosState {
    SysBusDevice parent_obj;
    MemoryRegion regs;
    qemu_irq irq;
    qemu_irq pipeline_event[NIKON_CMOS_PIPELINE_EVENTS];
    QEMUTimer *timer;
    NikonCmosConfig configuration, active;
    uint32_t phase, status, irq_mask, irq_status;
    uint32_t row, column, output_width, output_height;
    uint32_t active_buffer, unknown_writes, mode;
    uint32_t bayer, bit_depth, packing, dma_source, dma_destination;
    int64_t next_frame_time;
    uint32_t unknown[CMOS_UNKNOWN_WORDS];
    uint64_t frame_count, generation;
    char *image_path;
    uint8_t *image_data;
    gsize image_size;
    uint32_t image_width, image_height, image_channels;
    bool reported_error;
    bool fixture_provider;
};

static bool nikon_cmos_token(const uint8_t *data, gsize size, gsize *offset,
                             char *token, size_t token_size)
{
    gsize start, length;

    while (*offset < size) {
        if (g_ascii_isspace(data[*offset])) {
            (*offset)++;
        } else if (data[*offset] == '#') {
            while (*offset < size && data[*offset] != '\n') {
                (*offset)++;
            }
        } else {
            break;
        }
    }
    if (*offset == size) {
        return false;
    }
    start = *offset;
    while (*offset < size && !g_ascii_isspace(data[*offset]) &&
           data[*offset] != '#') {
        (*offset)++;
    }
    length = *offset - start;
    if (!length || length >= token_size) {
        return false;
    }
    memcpy(token, data + start, length);
    token[length] = 0;
    return true;
}

static bool nikon_cmos_load_image(NikonCmosState *s, Error **errp)
{
    g_autofree char *contents = NULL;
    gsize size, offset = 0, pixels;
    char magic[3], width[16], height[16], maxval[16];
    g_autoptr(GError) local_err = NULL;
    uint64_t parsed_width, parsed_height, parsed_maxval;
    unsigned channels;

    if (!s->image_path || !s->image_path[0]) {
        return true;
    }
    if (!g_file_get_contents(s->image_path, &contents, &size, &local_err)) {
        error_setg(errp, "CMOS image '%s' cannot be read: %s", s->image_path,
                   local_err->message);
        return false;
    }
    if (!nikon_cmos_token((const uint8_t *)contents, size, &offset, magic,
                          sizeof(magic)) ||
        (strcmp(magic, "P5") && strcmp(magic, "P6")) ||
        !nikon_cmos_token((const uint8_t *)contents, size, &offset, width,
                          sizeof(width)) ||
        !nikon_cmos_token((const uint8_t *)contents, size, &offset, height,
                          sizeof(height)) ||
        !nikon_cmos_token((const uint8_t *)contents, size, &offset, maxval,
                          sizeof(maxval)) ||
        qemu_strtou64(width, NULL, 10, &parsed_width) ||
        qemu_strtou64(height, NULL, 10, &parsed_height) ||
        qemu_strtou64(maxval, NULL, 10, &parsed_maxval) ||
        !parsed_width || !parsed_height || parsed_width > UINT32_MAX ||
        parsed_height > UINT32_MAX || parsed_maxval != 255) {
        error_setg(errp, "CMOS image must be binary PGM/PPM (P5/P6), 8-bit, "
                   "with valid dimensions");
        return false;
    }
    while (offset < size && g_ascii_isspace(((uint8_t *)contents)[offset])) {
        offset++;
    }
    channels = !strcmp(magic, "P6") ? 3 : 1;
    if (parsed_width > SIZE_MAX / parsed_height ||
        parsed_width * parsed_height > SIZE_MAX / channels) {
        error_setg(errp, "CMOS image dimensions overflow the source buffer");
        return false;
    }
    pixels = parsed_width * parsed_height * channels;
    if (offset > size || pixels > size - offset) {
        error_setg(errp, "CMOS image payload is truncated");
        return false;
    }
    g_free(s->image_data);
    s->image_data = g_memdup2((uint8_t *)contents + offset, pixels);
    s->image_size = pixels;
    s->image_width = parsed_width;
    s->image_height = parsed_height;
    s->image_channels = channels;
    return true;
}

static bool nikon_cmos_get_provider_bound(Object *obj, Error **errp)
{
    return NIKON_CMOS(obj)->fixture_provider;
}

static bool nikon_cmos_get_fixture_provider(Object *obj, Error **errp)
{
    return NIKON_CMOS(obj)->fixture_provider;
}

static void nikon_cmos_set_fixture_provider(Object *obj, bool value,
                                            Error **errp)
{
    NikonCmosState *s = NIKON_CMOS(obj);

    if (s->phase == NIKON_CMOS_EXPOSING ||
        s->phase == NIKON_CMOS_READING) {
        error_setg(errp, "cannot change CMOS provider while capture is active");
        return;
    }
    s->fixture_provider = value;
}

static char *nikon_cmos_get_image(Object *obj, Error **errp)
{
    return g_strdup(NIKON_CMOS(obj)->image_path ? NIKON_CMOS(obj)->image_path : "");
}

static void nikon_cmos_set_image(Object *obj, const char *value, Error **errp)
{
    NikonCmosState *s = NIKON_CMOS(obj);

    if (s->phase == NIKON_CMOS_EXPOSING || s->phase == NIKON_CMOS_READING) {
        error_setg(errp, "cannot change CMOS image while capture is active");
        return;
    }
    g_free(s->image_path);
    s->image_path = g_strdup(value && *value ? value : NULL);
    g_clear_pointer(&s->image_data, g_free);
    s->image_size = 0;
    s->image_width = s->image_height = s->image_channels = 0;
}

bool nikon_cmos_provider_bound(DeviceState *dev)
{
    return NIKON_CMOS(dev)->fixture_provider;
}

static bool nikon_cmos_writable(hwaddr address, uint64_t length)
{
    while (length) {
        MemoryRegionSection section = memory_region_find(get_system_memory(),
                                                         address, length);
        uint64_t span;
        bool writable;

        if (!section.mr) {
            return false;
        }
        span = int128_get64(section.size);
        writable = span && span <= length &&
                   section.offset_within_address_space == address &&
                   memory_region_is_ram(section.mr) &&
                   memory_access_is_direct(section.mr, true,
                                           MEMTXATTRS_UNSPECIFIED) &&
                   !section.readonly;
        memory_region_unref(section.mr);
        if (!writable) {
            return false;
        }
        address += span;
        length -= span;
    }
    return true;
}

static void nikon_cmos_irq(NikonCmosState *s)
{
    qemu_set_irq(s->irq, !!(s->irq_status & s->irq_mask));
}

static void nikon_cmos_phase(NikonCmosState *s, enum NikonCmosPhase phase)
{
    s->phase = phase;
    s->status = phase;
}

static void nikon_cmos_event(NikonCmosState *s, uint32_t event)
{
    int output = -1;

    s->irq_status |= event;
    nikon_cmos_irq(s);
    if (event == NIKON_CMOS_EVENT_DONE) {
        output = NIKON_CMOS_PIPELINE_DONE;
    } else if (event == NIKON_CMOS_EVENT_ERROR) {
        output = NIKON_CMOS_PIPELINE_ERROR;
    } else if (event == NIKON_CMOS_EVENT_CANCEL) {
        output = NIKON_CMOS_PIPELINE_CANCEL;
    }
    if (output >= 0) {
        qemu_set_irq(s->pipeline_event[output], 1);
        qemu_set_irq(s->pipeline_event[output], 0);
    }
}

static void nikon_cmos_fail(NikonCmosState *s)
{
    timer_del(s->timer);
    s->generation++;
    nikon_cmos_phase(s, NIKON_CMOS_ERROR);
    nikon_cmos_event(s, NIKON_CMOS_EVENT_ERROR);
}

void nikon_cmos_cancel(DeviceState *dev)
{
    NikonCmosState *s = NIKON_CMOS(dev);

    timer_del(s->timer);
    s->generation++;
    nikon_cmos_phase(s, NIKON_CMOS_IDLE);
    nikon_cmos_event(s, NIKON_CMOS_EVENT_CANCEL);
}

static uint8_t nikon_cmos_sample(const NikonCmosState *s, uint32_t x,
                                 uint32_t y, uint32_t channel)
{
    uint32_t sx = s->active.crop_x +
        (uint64_t)x * s->active.crop_width / s->output_width;
    uint32_t sy = s->active.crop_y +
        (uint64_t)y * s->active.crop_height / s->output_height;
    if (s->image_data) {
        uint32_t ix = MIN(sx, s->image_width - 1);
        uint32_t iy = MIN(sy, s->image_height - 1);
        size_t index = ((size_t)iy * s->image_width + ix) *
                       s->image_channels;

        return s->image_data[index + MIN(channel, s->image_channels - 1)];
    }

    uint32_t value = s->active.transport_seed ^ (uint32_t)s->frame_count;

    value += sx * 0x45d9f3bU;
    value ^= sy * 0x27d4eb2dU;
    value += channel * 0x9e3779b9U;
    value ^= (x + y) * 0x7f4a7c15U;
    return (uint8_t)(value >> ((channel & 3) * 8));
}

static void nikon_cmos_schedule(NikonCmosState *s, uint64_t delay)
{
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    if (now < 0 || delay > INT64_MAX - now) {
        nikon_cmos_fail(s);
        return;
    }
    timer_mod(s->timer, now + delay);
}

static void nikon_cmos_step(void *opaque)
{
    NikonCmosState *s = opaque;
    uint8_t bytes[CMOS_CHUNK];
    uint32_t row_bytes = s->output_width * s->active.bytes_per_pixel;
    uint32_t count;
    hwaddr destination;
    MemTxAttrs attrs = MEMTXATTRS_UNSPECIFIED;

    if (s->phase == NIKON_CMOS_EXPOSING) {
        nikon_cmos_phase(s, NIKON_CMOS_READING);
        nikon_cmos_schedule(s, s->active.readout_ns);
        return;
    }
    if (s->phase != NIKON_CMOS_READING) {
        return;
    }
    count = MIN((uint64_t)row_bytes - s->column, sizeof(bytes));
    destination = (hwaddr)s->active.destination[s->active_buffer] +
                  (uint64_t)s->row * s->active.stride + s->column;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t pixel = s->column + i;
        uint32_t x = pixel / s->active.bytes_per_pixel;
        uint32_t channel = pixel % s->active.bytes_per_pixel;

        bytes[i] = nikon_cmos_sample(s, x, s->row, channel);
    }
    attrs.memory = true;
    if (!nikon_cmos_writable(destination, count) ||
        dma_memory_write(&address_space_memory, destination, bytes, count,
                         attrs) != MEMTX_OK) {
        nikon_cmos_fail(s);
        return;
    }
    s->column += count;
    if (s->column == row_bytes) {
        s->column = 0;
        s->row++;
    }
    if (s->row == s->output_height) {
        nikon_cmos_phase(s, NIKON_CMOS_DONE);
        s->frame_count++;
        s->active_buffer = (s->active_buffer + 1) % s->active.buffer_count;
        int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

        s->next_frame_time = s->active.period_ns &&
            s->active.period_ns <= INT64_MAX - now ?
            now + s->active.period_ns : 0;
        nikon_cmos_event(s, NIKON_CMOS_EVENT_DONE);
    } else {
        nikon_cmos_schedule(s, s->active.readout_ns);
    }
}

static bool nikon_cmos_prepare(NikonCmosState *s, Error **errp)
{
    NikonCmosConfig *c = &s->configuration;
    uint64_t output_width, output_height, row_bytes, span, total;
    unsigned i;

    if (!c->width || !c->height || !c->crop_width || !c->crop_height ||
        c->crop_x >= c->width || c->crop_y >= c->height ||
        c->crop_width > c->width - c->crop_x ||
        c->crop_height > c->height - c->crop_y) {
        error_setg(errp, "CMOS mode and crop must be explicitly configured");
        return false;
    }
    if (!nikon_cmos_load_image(s, errp)) {
        return false;
    }
    if (s->image_data && (s->image_width != c->width ||
                          s->image_height != c->height)) {
        error_setg(errp, "CMOS image dimensions (%ux%u) must match configured "
                   "sensor dimensions (%ux%u)", s->image_width,
                   s->image_height, c->width, c->height);
        return false;
    }
    if (!c->scale_x || !c->scale_y || !c->bytes_per_pixel ||
        c->bytes_per_pixel > 16 || !c->buffer_count || c->buffer_count > 4 ||
        !c->stride || !c->exposure_ns || !c->readout_ns) {
        error_setg(errp, "CMOS transport geometry and timing are incomplete");
        return false;
    }
    output_width = ((uint64_t)c->crop_width * c->scale_x) >> 16;
    output_height = ((uint64_t)c->crop_height * c->scale_y) >> 16;
    if (!output_width || !output_height || output_width > UINT32_MAX ||
        output_height > UINT32_MAX) {
        error_setg(errp, "CMOS scale produces an empty or oversized frame");
        return false;
    }
    row_bytes = output_width * c->bytes_per_pixel;
    total = row_bytes * output_height;
    if (row_bytes > UINT32_MAX || !total || total > CMOS_MAX_BUFFER_BYTES ||
        c->stride < row_bytes) {
        error_setg(errp, "CMOS stride or frame extent is invalid");
        return false;
    }
    span = (output_height - 1) * (uint64_t)c->stride + row_bytes;
    if (span > UINT32_MAX) {
        error_setg(errp, "CMOS stride span exceeds the address space");
        return false;
    }
    for (i = 0; i < c->buffer_count; i++) {
        if (c->destination[i] > UINT32_MAX - span ||
            !nikon_cmos_writable(c->destination[i], span)) {
            error_setg(errp, "CMOS destination buffer is not writable RAM");
            return false;
        }
    }
    if (c->exposure_ns > INT64_MAX || c->readout_ns > INT64_MAX ||
        c->period_ns > INT64_MAX) {
        error_setg(errp, "CMOS virtual timing overflows");
        return false;
    }
    s->output_width = output_width;
    s->output_height = output_height;
    return true;
}

static bool nikon_cmos_begin(NikonCmosState *s, Error **errp)
{
    int64_t now;

    if (!s->fixture_provider) {
        error_setg(errp, "CMOS frame provider is not bound");
        return false;
    }
    if (s->phase == NIKON_CMOS_EXPOSING || s->phase == NIKON_CMOS_READING) {
        error_setg(errp, "CMOS frame transport is busy");
        return false;
    }
    if (!nikon_cmos_prepare(s, errp)) {
        return false;
    }
    now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    if (now < 0 || s->configuration.exposure_ns > INT64_MAX - now) {
        error_setg(errp, "CMOS exposure time overflows the virtual clock");
        return false;
    }
    if (s->next_frame_time && now < s->next_frame_time) {
        error_setg(errp, "CMOS frame period backpressure is active");
        return false;
    }
    timer_del(s->timer);
    s->active = s->configuration;
    s->row = s->column = 0;
    s->generation++;
    s->status = 0;
    nikon_cmos_phase(s, NIKON_CMOS_EXPOSING);
    nikon_cmos_schedule(s, s->active.exposure_ns);
    return true;
}

bool nikon_cmos_submit(DeviceState *dev, const NikonCmosSubmission *submission,
                       Error **errp)
{
    NikonCmosState *s = NIKON_CMOS(dev);
    NikonCmosConfig config = { 0 };

    if (!submission || submission->format != NIKON_CMOS_FORMAT_FUNCTIONAL_C170) {
        error_setg(errp, "CMOS submission format is unsupported");
        return false;
    }
    config.width = submission->width;
    config.height = submission->height;
    config.crop_width = submission->width;
    config.crop_height = submission->height;
    config.scale_x = 1U << 16;
    config.scale_y = 1U << 16;
    config.format = submission->format;
    config.transport_seed = submission->transport_seed;
    config.bytes_per_pixel = submission->bytes_per_pixel;
    config.stride = submission->stride;
    config.buffer_count = 1;
    config.destination[0] = submission->destination;
    config.exposure_ns = submission->exposure_ns;
    config.readout_ns = submission->readout_ns;
    s->configuration = config;
    return nikon_cmos_begin(s, errp);
}

static void nikon_cmos_start(NikonCmosState *s)
{
    Error *err = NULL;

    if (!nikon_cmos_begin(s, &err)) {
        error_free(err);
        nikon_cmos_fail(s);
    }
}

static MemTxResult nikon_cmos_error(NikonCmosState *s, hwaddr offset,
                                    unsigned size, bool write, uint64_t value,
                                    const char *reason)
{
    nikon_access_error(&s->reported_error, s->regs.addr + offset, size, write,
                       value, reason);
    return MEMTX_ERROR;
}

static uint32_t *nikon_cmos_word(NikonCmosConfig *c, hwaddr offset)
{
    switch (offset) {
    case CMOS_WIDTH: return &c->width;
    case CMOS_HEIGHT: return &c->height;
    case CMOS_CROP_X: return &c->crop_x;
    case CMOS_CROP_Y: return &c->crop_y;
    case CMOS_CROP_WIDTH: return &c->crop_width;
    case CMOS_CROP_HEIGHT: return &c->crop_height;
    case CMOS_SCALE_X: return &c->scale_x;
    case CMOS_SCALE_Y: return &c->scale_y;
    case CMOS_FORMAT: return &c->format;
    case CMOS_TRANSPORT_SEED: return &c->transport_seed;
    case CMOS_BYTES_PER_PIXEL: return &c->bytes_per_pixel;
    case CMOS_STRIDE: return &c->stride;
    case CMOS_BUFFER_COUNT: return &c->buffer_count;
    case CMOS_DEST0: return &c->destination[0];
    case CMOS_DEST1: return &c->destination[1];
    case CMOS_DEST2: return &c->destination[2];
    case CMOS_DEST3: return &c->destination[3];
    default: return NULL;
    }
}

static MemTxResult nikon_cmos_read(void *opaque, hwaddr offset,
                                   uint64_t *value, unsigned size,
                                   MemTxAttrs attrs)
{
    NikonCmosState *s = opaque;
    uint32_t *word;

    if (size != 4 || (offset & 3)) {
        return nikon_cmos_error(s, offset, size, false, 0, "cmos-access-width");
    }
    word = nikon_cmos_word(&s->configuration, offset);
    if (word) {
        *value = *word;
    } else if (offset == CMOS_MODE) {
        *value = s->mode;
    } else if (offset == CMOS_BUFFER_INDEX) {
        *value = s->active_buffer;
    } else if (offset == CMOS_EXPOSURE_LO || offset == CMOS_EXPOSURE_HI ||
               offset == CMOS_PERIOD_LO || offset == CMOS_PERIOD_HI ||
               offset == CMOS_READOUT_LO || offset == CMOS_READOUT_HI) {
        uint64_t v = offset == CMOS_EXPOSURE_LO || offset == CMOS_EXPOSURE_HI ?
            s->configuration.exposure_ns : offset == CMOS_PERIOD_LO ||
            offset == CMOS_PERIOD_HI ? s->configuration.period_ns :
            s->configuration.readout_ns;
        *value = offset & 4 ? v >> 32 : v;
    } else if (offset == CMOS_STATUS) {
        *value = s->phase;
    } else if (offset == CMOS_IRQ_MASK) {
        *value = s->irq_mask;
    } else if (offset == CMOS_IRQ_STATUS) {
        *value = s->irq_status;
    } else if (offset == CMOS_FRAME_COUNT) {
        *value = s->frame_count;
    } else if (offset == CMOS_ACTIVE_BUFFER) {
        *value = s->active_buffer;
    } else if (offset == CMOS_BAYER) {
        *value = s->bayer;
    } else if (offset == CMOS_BIT_DEPTH) {
        *value = s->bit_depth;
    } else if (offset == CMOS_PACKING) {
        *value = s->packing;
    } else if (offset == CMOS_DMA_SOURCE) {
        *value = s->dma_source;
    } else if (offset == CMOS_DMA_DESTINATION) {
        *value = s->dma_destination;
    } else if (offset >= CMOS_UNKNOWN_BASE &&
               offset < CMOS_UNKNOWN_BASE + CMOS_UNKNOWN_WORDS * 4) {
        *value = s->unknown[(offset - CMOS_UNKNOWN_BASE) / 4];
    } else {
        return nikon_cmos_error(s, offset, size, false, 0,
                                "cmos-register-unbound");
    }
    return MEMTX_OK;
}

static MemTxResult nikon_cmos_write(void *opaque, hwaddr offset,
                                    uint64_t value, unsigned size,
                                    MemTxAttrs attrs)
{
    NikonCmosState *s = opaque;
    uint32_t *word;

    if (size != 4 || (offset & 3)) {
        return nikon_cmos_error(s, offset, size, true, value,
                                "cmos-access-width");
    }
    word = nikon_cmos_word(&s->configuration, offset);
    if (word) {
        if (s->phase == NIKON_CMOS_EXPOSING || s->phase == NIKON_CMOS_READING) {
            return nikon_cmos_error(s, offset, size, true, value,
                                    "cmos-configuration-busy");
        }
        *word = value;
    } else if (offset == CMOS_MODE) {
        if (s->phase == NIKON_CMOS_EXPOSING || s->phase == NIKON_CMOS_READING) {
            return nikon_cmos_error(s, offset, size, true, value,
                                    "cmos-configuration-busy");
        }
        s->mode = value;
    } else if (offset == CMOS_EXPOSURE_LO || offset == CMOS_EXPOSURE_HI ||
               offset == CMOS_PERIOD_LO || offset == CMOS_PERIOD_HI ||
               offset == CMOS_READOUT_LO || offset == CMOS_READOUT_HI) {
        uint64_t *target = offset == CMOS_EXPOSURE_LO || offset == CMOS_EXPOSURE_HI ?
            &s->configuration.exposure_ns : offset == CMOS_PERIOD_LO ||
            offset == CMOS_PERIOD_HI ? &s->configuration.period_ns :
            &s->configuration.readout_ns;
        if (offset & 4) {
            *target = (*target & UINT32_MAX) | (value << 32);
        } else {
            *target = (*target & 0xffffffff00000000ULL) | (uint32_t)value;
        }
    } else if (offset == CMOS_START) {
        if (value != 1) {
            return nikon_cmos_error(s, offset, size, true, value,
                                    "cmos-start-command");
        }
        nikon_cmos_start(s);
    } else if (offset == CMOS_CANCEL) {
        if (value != 1) {
            return nikon_cmos_error(s, offset, size, true, value,
                                    "cmos-cancel-command");
        }
        nikon_cmos_cancel(DEVICE(s));
    } else if (offset == CMOS_IRQ_MASK) {
        s->irq_mask = value & (NIKON_CMOS_EVENT_DONE | NIKON_CMOS_EVENT_ERROR |
                               NIKON_CMOS_EVENT_CANCEL);
        nikon_cmos_irq(s);
    } else if (offset == CMOS_IRQ_ACK) {
        s->irq_status &= ~value;
        nikon_cmos_irq(s);
    } else if (offset == CMOS_BAYER) {
        s->bayer = value;
    } else if (offset == CMOS_BIT_DEPTH) {
        s->bit_depth = value;
    } else if (offset == CMOS_PACKING) {
        s->packing = value;
    } else if (offset == CMOS_DMA_SOURCE) {
        s->dma_source = value;
    } else if (offset == CMOS_DMA_DESTINATION) {
        s->dma_destination = value;
    } else if (offset >= CMOS_UNKNOWN_BASE &&
               offset < CMOS_UNKNOWN_BASE + CMOS_UNKNOWN_WORDS * 4) {
        /* Preserve unknown source fields and expose their write count. */
        s->unknown_writes++;
        s->unknown[(offset - CMOS_UNKNOWN_BASE) / 4] = value;
    } else {
        return nikon_cmos_error(s, offset, size, true, value,
                                "cmos-register-unbound");
    }
    return MEMTX_OK;
}

static const MemoryRegionOps nikon_cmos_ops = {
    .read_with_attrs = nikon_cmos_read,
    .write_with_attrs = nikon_cmos_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl = { .min_access_size = 4, .max_access_size = 4 },
};

static void nikon_cmos_reset(DeviceState *dev)
{
    NikonCmosState *s = NIKON_CMOS(dev);

    timer_del(s->timer);
    s->generation++;
    memset(&s->configuration, 0, sizeof(s->configuration));
    memset(&s->active, 0, sizeof(s->active));
    s->phase = NIKON_CMOS_IDLE;
    s->status = 0;
    s->irq_mask = s->irq_status = 0;
    s->row = s->column = s->output_width = s->output_height = 0;
    s->active_buffer = s->unknown_writes = 0;
    s->mode = 0;
    s->bayer = s->bit_depth = s->packing = 0;
    s->dma_source = s->dma_destination = 0;
    s->next_frame_time = 0;
    memset(s->unknown, 0, sizeof(s->unknown));
    s->frame_count = 0;
    s->reported_error = false;
    nikon_cmos_irq(s);
}

static void nikon_cmos_init(Object *obj)
{
    NikonCmosState *s = NIKON_CMOS(obj);

    memory_region_init_io(&s->regs, obj, &nikon_cmos_ops, s,
                          "nikon-cmos", NIKON_CMOS_REGS);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
    qdev_init_gpio_out_named(DEVICE(obj), s->pipeline_event,
                             "pipeline-event", NIKON_CMOS_PIPELINE_EVENTS);
    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, nikon_cmos_step, s);
    object_property_add_uint32_ptr(obj, "phase", &s->phase, OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(obj, "frame-count", &s->frame_count,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint32_ptr(obj, "active-buffer", &s->active_buffer,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint32_ptr(obj, "unknown-writes", &s->unknown_writes,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_bool(obj, "provider-bound",
                             nikon_cmos_get_provider_bound, NULL);
    object_property_add_bool(obj, "fixture-provider",
                             nikon_cmos_get_fixture_provider,
                             nikon_cmos_set_fixture_provider);
    object_property_add_str(obj, "image", nikon_cmos_get_image,
                            nikon_cmos_set_image);
}

static void nikon_cmos_finalize(Object *obj)
{
    NikonCmosState *s = NIKON_CMOS(obj);

    timer_free(s->timer);
    g_free(s->image_path);
    g_free(s->image_data);
}

static const VMStateDescription nikon_cmos_vmstate = {
    .name = TYPE_NIKON_CMOS,
    .unmigratable = true,
};

static void nikon_cmos_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &nikon_cmos_vmstate;
    device_class_set_legacy_reset(dc, nikon_cmos_reset);
}

static const TypeInfo nikon_cmos_type = {
    .name = TYPE_NIKON_CMOS,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonCmosState),
    .instance_init = nikon_cmos_init,
    .instance_finalize = nikon_cmos_finalize,
    .class_init = nikon_cmos_class_init,
};

static void nikon_cmos_register_types(void)
{
    type_register_static(&nikon_cmos_type);
}

type_init(nikon_cmos_register_types)
