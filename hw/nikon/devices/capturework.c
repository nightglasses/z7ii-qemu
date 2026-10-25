/* C1.70 functional grayscale stream: disabled corrections, geometry, packing.
 * Input association, nearest sampling and LUT arithmetic are hypotheses;
 * register producers, buffer geometry and completion routing are source-backed.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/bswap.h"
#include "qemu/timer.h"
#include "migration/vmstate.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/sysbus.h"
#include "hw/nikon/devices.h"
#include "hw/nikon/readoutpipeline.h"
#include "hw/nikon/sensor.h"
#include "hw/nikon/statistics.h"
#include "system/address-spaces.h"
#include "system/dma.h"

OBJECT_DECLARE_SIMPLE_TYPE(NikonCaptureWork, NIKON_CAPTURE_WORK)

typedef struct CaptureGeometry {
    uint32_t rows, columns, output_rows, output_columns, enable;
} CaptureGeometry;

typedef struct CapturePacking {
    /* PA20069514..30, selector2 fields below, and128 packed LUT words. */
    uint32_t words[8], buffers[18], table[128];
} CapturePacking;

/* Stride/controls, bank/start/count, two four-address sets, extension. */
static const unsigned buffer_offsets[] = {
    0x208, 0x214, 0x218, 0x224, 0x23c, 0x240, 0x244, 0x248, 0x24c,
    0x250, 0x254, 0x258, 0x25c, 0x260, 0x264, 0x268, 0x26c, 0x280,
};

struct NikonCaptureWork {
    SysBusDevice parent_obj;
    DeviceState *capture, *parameters, *readout, *pipeline;
    QEMUTimer *timer;
    bool selected[6], valid[3], running[4], active;
    uint32_t correction;
    CaptureGeometry geometry;
    CapturePacking packing;
    unsigned cursor;
    uint64_t last_input, completed, rejected, cancelled, blocked, bytes;
    NikonCaptureInput input;
    uint8_t *input_data;
};

static void capture_work_cancel(NikonCaptureWork *s)
{
    if (s->active) {
        timer_del(s->timer);
        s->active = false;
        s->cancelled++;
    }
}

static void capture_work_select(void *opaque, int n, int level)
{
    NIKON_CAPTURE_WORK(opaque)->selected[n] = !!level;
}

static void capture_work_commit(void *opaque, int n, int level)
{
    NikonCaptureWork *s = opaque;
    bool changed = false;
    bool *selected = &s->selected[3 * n];

    if (!level) {
        return;
    }
    if (selected[0]) {
        uint32_t correction = 0;

        nikon_capture_configuration(s->capture, 0x500, &correction);
        changed |= !s->valid[0] || s->correction != correction;
        s->correction = correction;
        s->valid[0] = true;
    }
    if (selected[1]) {
        CaptureGeometry g = { 0 };

        nikon_capture_configuration(s->capture, 0x1d00, &g.rows);
        nikon_capture_configuration(s->capture, 0x1d04, &g.columns);
        nikon_capture_configuration(s->capture, 0x1e78, &g.output_rows);
        nikon_capture_configuration(s->capture, 0x1e7c, &g.output_columns);
        nikon_capture_configuration(s->capture, 0x1f00, &g.enable);
        changed |= !s->valid[1] || memcmp(&s->geometry, &g, sizeof(g));
        s->geometry = g;
        s->valid[1] = true;
    }
    if (selected[2]) {
        CapturePacking p = { 0 };
        bool valid = true;

        for (unsigned i = 0; i < ARRAY_SIZE(p.words) - 1; i++) {
            valid &= nikon_capture_parameters_configuration(s->parameters,
                0x14 + 4 * i, &p.words[i]);
        }
        /* Source policy0 omits the shared9530 pair; it has no grayscale
         * arithmetic here and cannot be a mandatory input qualification.
         */
        nikon_capture_parameters_configuration(s->parameters, 0x30,
                                                &p.words[7]);
        for (unsigned i = 0; i < ARRAY_SIZE(buffer_offsets); i++) {
            valid &= nikon_readout_configuration(s->readout,
                NIKON_READOUT_BUFFERS, buffer_offsets[i], &p.buffers[i]);
        }
        if (p.words[0] & 4) {
            for (unsigned i = 0; i < ARRAY_SIZE(p.table); i++) {
                valid &= nikon_capture_parameters_configuration(s->parameters,
                    0x100 + 4 * i, &p.table[i]);
            }
        }
        if (!s->valid[2] || memcmp(&s->packing, &p, sizeof(p))) {
            changed = true;
            s->cursor = p.buffers[6];
        }
        s->packing = p;
        s->valid[2] = valid;
    }
    if (changed) {
        NikonCaptureInput input;

        capture_work_cancel(s);
        /* A configuration epoch cannot replay an already delivered frame. */
        if (nikon_readout_pipeline_input(s->pipeline, &input)) {
            s->last_input = input.generation;
        }
    }
}

static void capture_work_run(void *opaque, int n, int level)
{
    NikonCaptureWork *s = opaque;

    if (s->running[n] != !!level) {
        NikonCaptureInput input;

        if (nikon_readout_pipeline_input(s->pipeline, &input)) {
            s->last_input = input.generation;
        }
    }
    s->running[n] = !!level;
    if (!level) {
        capture_work_cancel(s);
    }
}

static bool capture_work_ram(uint64_t address, uint64_t bytes)
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

static bool capture_work_output(NikonCaptureWork *s, unsigned *bits,
                                unsigned *width, unsigned *height,
                                unsigned *row_bytes, uint32_t *destination)
{
    const CapturePacking *p = &s->packing;
    const CaptureGeometry *g = &s->geometry;
    const uint32_t *b = p->buffers;
    unsigned format = p->words[0] & 3;
    unsigned bank = b[4] >> 1;

    /* Zero feature flags are the only implemented correction profile. The
     * geometry stage is grayscale nearest sampling, not recovered Bayer DSP.
     */
    if (s->correction || g->enable != 0x11 || (g->rows & 0xffff) ||
        (g->columns & 0xffff) || !(g->rows >> 16) || !(g->columns >> 16) ||
        g->output_rows > g->rows || g->output_columns > g->columns ||
        (g->output_rows & 0xffff) || (g->output_columns & 0xffff) ||
        (p->words[0] & ~7u) || format > 2 ||
        p->words[1] != g->output_columns || p->words[2] != g->output_rows ||
        p->words[3] || p->words[4] || p->words[5] || p->words[6] != 2 ||
        b[1] != 1 || b[2] != 1 || b[3] || (b[4] & ~2u) || bank > 1 ||
        b[5] != 1 || b[7] > 3 || b[6] > b[7] || s->cursor > b[7] ||
        b[8] || b[17]) {
        return false;
    }
    *width = p->words[1] >> 16;
    *height = p->words[2] >> 16;
    *bits = format == 0 ? 16 : format == 1 ? 14 : 12;
    if (!*width || *width > 4096 || !*height || *height > 4096 ||
        (*width % (format == 1 ? 4 : format == 2 ? 2 : 1))) {
        return false;
    }
    *row_bytes = *width * *bits / 8;
    if (b[0] != ROUND_UP(*row_bytes, 16)) {
        return false;
    }
    uint64_t span = (uint64_t)b[0] * *height;

    /* Preflight the complete selected ring, including overlap and RAM edges. */
    for (unsigned i = 0; i <= b[7]; i++) {
        uint64_t address = b[9 + bank * 4 + i];

        if (!capture_work_ram(address, span)) {
            return false;
        }
        for (unsigned j = 0; j < i; j++) {
            uint64_t other = b[9 + bank * 4 + j];

            if (address < other + span && other < address + span) {
                return false;
            }
        }
    }
    if (p->words[0] & 4) {
        for (unsigned i = 0; i < 256; i++) {
            unsigned value = (p->table[i / 2] >> (16 * (i % 2))) & 0xffff;

            if (value >= (1u << *bits)) {
                return false;
            }
        }
    }
    *destination = b[9 + bank * 4 + s->cursor];
    return true;
}

static void capture_work_complete(void *opaque)
{
    NikonCaptureWork *s = opaque;
    unsigned bits, width, height, row_bytes;
    uint32_t destination;

    s->active = false;
    if (!capture_work_output(s, &bits, &width, &height, &row_bytes,
                              &destination)) {
        s->rejected++;
        return;
    }
    g_autofree uint8_t *row = g_malloc(row_bytes);
    const CapturePacking *p = &s->packing;

    for (unsigned y = 0; y < height; y++) {
        unsigned sy = (uint64_t)y * s->input.height / height;
        uint32_t accumulator = 0;
        unsigned available = 0, column = 0;

        for (unsigned x = 0; x < width; x++) {
            unsigned sx = (uint64_t)x * s->input.width / width;
            uint32_t value = lduw_le_p(s->input.data +
                                       sy * s->input.stride + 2 * sx);

            if (p->words[0] & 4) {
                unsigned index = value >> 8;

                value = (p->table[index / 2] >> (16 * (index % 2))) & 0xffff;
            } else {
                value >>= 16 - bits;
            }
            accumulator |= value << available;
            available += bits;
            while (available >= 8) {
                row[column++] = accumulator;
                accumulator >>= 8;
                available -= 8;
            }
        }
        if (dma_memory_write(&address_space_memory,
                              (uint64_t)destination + y * p->buffers[0],
                              row, row_bytes, MEMTXATTRS_UNSPECIFIED) !=
            MEMTX_OK) {
            s->rejected++;
            return;
        }
    }
    /* The owned stream was copied, geometrically sampled and packed into real
     * guest buffers. Never substitute input-sync/statistics for these causes.
     */
    uint32_t front[] = { 0x18, 0 };
    uint32_t paired[] = { 0, 0x80 };

    nikon_capture_publish(s->capture, NIKON_CAPTURE_FIRST_EVENT,
                          front, 2, &error_abort);
    nikon_capture_publish(s->capture, NIKON_CAPTURE_THIRD_EVENT,
                          paired, 2, &error_abort);
    s->completed++;
    s->bytes += (uint64_t)row_bytes * height;
    s->cursor = (s->cursor + 1) % (p->buffers[7] + 1);
}

static void capture_work_rise(void *opaque, int n, int level)
{
    NikonCaptureWork *s = opaque;
    NikonCaptureInput input;

    if (!level || s->active || !s->valid[0] || !s->valid[1] || !s->valid[2] ||
        !s->running[0] || !s->running[1] || !s->running[2] || !s->running[3]) {
        return;
    }
    if (nikon_capture_front_pending(s->capture)) {
        s->blocked++;
        return;
    }
    if (!nikon_readout_pipeline_input(s->pipeline, &input) ||
        input.generation == s->last_input) {
        return;
    }
    s->last_input = input.generation;
    if (!input.synchronized) {
        return;
    }
    if (!input.width || !input.height ||
        (uint64_t)input.width * input.height > 4 * 1024 * 1024) {
        s->rejected++;
        return;
    }
    g_free(s->input_data);
    s->input_data = g_memdup2(input.data, (size_t)input.stride * input.height);
    s->input = input;
    s->input.data = s->input_data;
    s->active = true;
    timer_mod(s->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000000);
}

static void capture_work_reset(DeviceState *dev)
{
    NikonCaptureWork *s = NIKON_CAPTURE_WORK(dev);

    capture_work_cancel(s);
    memset(s->selected, 0, sizeof(s->selected));
    memset(s->valid, 0, sizeof(s->valid));
    memset(s->running, 0, sizeof(s->running));
    s->cursor = 0;
    s->last_input = 0;
    s->input = (NikonCaptureInput) { 0 };
    g_clear_pointer(&s->input_data, g_free);
}

static void capture_work_realize(DeviceState *dev, Error **errp)
{
    NikonCaptureWork *s = NIKON_CAPTURE_WORK(dev);

    if (!s->capture || !s->parameters || !s->readout || !s->pipeline) {
        error_setg(errp, "capture work requires capture, parameters, readout "
                   "and pipeline links");
    }
}

static void capture_work_init(Object *obj)
{
    NikonCaptureWork *s = NIKON_CAPTURE_WORK(obj);
    DeviceState *dev = DEVICE(obj);

    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, capture_work_complete, s);
    qdev_init_gpio_in_named(dev, capture_work_select, "commit-selected", 6);
    qdev_init_gpio_in_named(dev, capture_work_commit, "commit", 2);
    qdev_init_gpio_in_named(dev, capture_work_run, "run", 4);
    qdev_init_gpio_in_named(dev, capture_work_rise, "automatic-rise", 1);
    object_property_add_uint64_ptr(obj, "completed", &s->completed,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(obj, "rejected", &s->rejected,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(obj, "cancelled", &s->cancelled,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(obj, "blocked", &s->blocked,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(obj, "bytes", &s->bytes, OBJ_PROP_FLAG_READ);
}

static void capture_work_finalize(Object *obj)
{
    NikonCaptureWork *s = NIKON_CAPTURE_WORK(obj);

    timer_free(s->timer);
    g_free(s->input_data);
}

static const Property capture_work_properties[] = {
    DEFINE_PROP_LINK("capture", NikonCaptureWork, capture,
                     TYPE_NIKON_CAPTURE_CONTROL, DeviceState *),
    DEFINE_PROP_LINK("parameters", NikonCaptureWork, parameters,
                     TYPE_NIKON_CAPTURE_PARAMETERS, DeviceState *),
    DEFINE_PROP_LINK("readout", NikonCaptureWork, readout,
                     TYPE_NIKON_READOUT_CONTROL, DeviceState *),
    DEFINE_PROP_LINK("pipeline", NikonCaptureWork, pipeline,
                     TYPE_NIKON_READOUT_PIPELINE, DeviceState *),
};

static const VMStateDescription capture_work_vmstate = {
    .name = TYPE_NIKON_CAPTURE_WORK,
    .unmigratable = true,
};

static void capture_work_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = capture_work_realize;
    dc->unrealize = capture_work_reset;
    dc->user_creatable = false;
    dc->vmsd = &capture_work_vmstate;
    device_class_set_props(dc, capture_work_properties);
    device_class_set_legacy_reset(dc, capture_work_reset);
}

static const TypeInfo capture_work_type = {
    .name = TYPE_NIKON_CAPTURE_WORK,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonCaptureWork),
    .instance_init = capture_work_init,
    .instance_finalize = capture_work_finalize,
    .class_init = capture_work_class_init,
};

static void capture_work_register_types(void)
{
    type_register_static(&capture_work_type);
}
type_init(capture_work_register_types)
