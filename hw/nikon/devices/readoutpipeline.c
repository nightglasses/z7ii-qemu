/* Bounded functional C1.70 readout-control to CMOS producer/consumer join.
 * The profile mapping is not a physical sensor or pixel-format claim.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/sysbus.h"
#include "hw/nikon/cmos.h"
#include "hw/nikon/readoutpipeline.h"
#include "hw/nikon/sensor.h"
#include "hw/nikon/statistics.h"
#include "system/address-spaces.h"
#include "system/dma.h"
#include "migration/vmstate.h"

OBJECT_DECLARE_SIMPLE_TYPE(NikonReadoutPipeline, NIKON_READOUT_PIPELINE)

typedef struct MainGeometry {
    uint32_t rows, columns, output_rows, output_columns, enable;
} MainGeometry;

struct NikonReadoutPipeline {
    SysBusDevice parent_obj;
    DeviceState *readout;
    DeviceState *cmos;
    DeviceState *capture;
    uint64_t active_generation;
    uint64_t cmos_generation;
    bool submitted;
    bool input_high;
    bool input_run;
    bool input_sync;
    uint32_t completion_cause;
    uint64_t requests;
    uint64_t accepted;
    uint64_t completed;
    uint64_t rejected;
    uint64_t cancelled;
    uint64_t stale;
    uint64_t input_notifications;
    NikonReadoutC170Mapping mapping;
    NikonCaptureInput input;
    uint8_t *input_data;
    NikonCaptureInput main_input;
    uint8_t *main_data;
    uint64_t main_generation;
    bool prefer_main;
    bool main_selected[4], main_valid[2], main_running[3], input_main;
    uint32_t correction, main_width, main_height;
    MainGeometry geometry;
    uint64_t main_token, input_generation;
    uint64_t main_completed, main_rejected, main_cancelled;
};

bool nikon_readout_pipeline_input(DeviceState *dev, NikonCaptureInput *input)
{
    NikonReadoutPipeline *s = NIKON_READOUT_PIPELINE(dev);

    *input = s->input;
    return input->generation != 0;
}

bool nikon_readout_pipeline_main_input(DeviceState *dev,
                                      NikonCaptureInput *input)
{
    NikonReadoutPipeline *s = NIKON_READOUT_PIPELINE(dev);

    *input = s->main_input;
    return input->generation != 0;
}

static void main_input_cancel(NikonReadoutPipeline *s)
{
    if (s->main_token) {
        nikon_cmos_cancel(s->cmos);
        s->main_token = 0;
    }
    if (s->input_main) {
        s->input = (NikonCaptureInput) { 0 };
    }
    s->main_input = (NikonCaptureInput) { 0 };
    g_clear_pointer(&s->main_data, g_free);
}

static void main_input_select(void *opaque, int n, int level)
{
    NIKON_READOUT_PIPELINE(opaque)->main_selected[n] = !!level;
}

static void main_input_commit(void *opaque, int n, int level)
{
    NikonReadoutPipeline *s = opaque;
    bool changed = false;

    if (!level) {
        return;
    }
    if (s->main_selected[2 * n]) {
        uint32_t correction = 0;

        nikon_capture_configuration(s->capture, 0x500, &correction);
        changed |= !s->main_valid[0] || s->correction != correction;
        s->correction = correction;
        s->main_valid[0] = true;
    }
    if (s->main_selected[2 * n + 1]) {
        MainGeometry g = { 0 };

        nikon_capture_configuration(s->capture, 0x1d00, &g.rows);
        nikon_capture_configuration(s->capture, 0x1d04, &g.columns);
        nikon_capture_configuration(s->capture, 0x1e78, &g.output_rows);
        nikon_capture_configuration(s->capture, 0x1e7c, &g.output_columns);
        nikon_capture_configuration(s->capture, 0x1f00, &g.enable);
        changed |= !s->main_valid[1] || memcmp(&s->geometry, &g, sizeof(g));
        s->geometry = g;
        s->main_valid[1] = true;
    }
    if (changed) {
        main_input_cancel(s);
    }
}

static void main_input_run(void *opaque, int n, int level)
{
    NikonReadoutPipeline *s = opaque;

    s->main_running[n] = !!level;
    if (!level) {
        main_input_cancel(s);
    }
    if (!n) {
        s->input_run = !!level;
        if (!level) {
            s->input_sync = false;
        }
    }
}

static bool main_input_ready(const NikonReadoutPipeline *s)
{
    return s->main_running[0] && s->main_running[1] && s->main_running[2] &&
           s->main_valid[0] && s->main_valid[1] &&
           !nikon_capture_data_pending(s->capture);
}

static void main_input_begin(NikonReadoutPipeline *s)
{
    const MainGeometry *g = &s->geometry;
    Error *err = NULL;
    uint32_t width = g->output_columns >> 16;
    uint32_t height = g->output_rows >> 16;

    if (!main_input_ready(s)) {
        return;
    }
    if (s->correction || g->enable != 0x11 ||
        ((g->rows | g->columns | g->output_rows | g->output_columns) & 0xffff) ||
        !width || !height || width > 4096 || height > 4096 ||
        g->output_rows > g->rows || g->output_columns > g->columns ||
        (uint64_t)width * height > 4 * 1024 * 1024) {
        s->main_rejected++;
        qemu_log_mask(LOG_GUEST_ERROR, "nikon main input profile unsupported\n");
        return;
    }
    /* Finite inline identity/geometry stream; no invented raw RAM buffer. */
    s->main_token = nikon_cmos_acquire(s->cmos, width, height, &err);
    if (!s->main_token) {
        s->main_rejected++;
        qemu_log_mask(LOG_GUEST_ERROR, "nikon main input rejected: %s\n",
                      error_get_pretty(err));
        error_free(err);
        return;
    }
    s->main_width = width;
    s->main_height = height;
}

static void nikon_readout_pipeline_request(void *opaque, int n, int level)
{
    NikonReadoutPipeline *s = opaque;
    NikonReadoutRequest request;
    NikonReadoutC170Mapping mapping;
    Error *err = NULL;

    if (!level) {
        return;
    }
    s->requests++;
    if (s->active_generation) {
        s->rejected++;
        return;
    }
    if (!nikon_readout_request(s->readout, &request) ||
        !nikon_readout_c170_mapping(s->readout, request.generation, &mapping,
                                    &err)) {
        qemu_log_mask(LOG_GUEST_ERROR, "nikon readout request rejected: %s\n",
                      err ? error_get_pretty(err) : "no owned generation");
        error_free(err);
        s->rejected++;
        return;
    }

    s->active_generation = request.generation;
    s->mapping = mapping;
    s->completion_cause = mapping.completion_cause;
    /*
     * Ready means the provider owns this immutable request, including while
     * waiting for input. Otherwise a new marker can replace its generation.
     */
    if (!nikon_readout_publish_ready(s->readout, request.generation)) {
        s->active_generation = 0;
        s->stale++;
        return;
    }
    s->accepted++;
}

static void nikon_readout_pipeline_input_rise(void *opaque, int n, int level)
{
    NikonReadoutPipeline *s = opaque;
    NikonReadoutC170Mapping *m = &s->mapping;
    bool rising = level && !s->input_high;
    NikonCmosSubmission submission;
    Error *err = NULL;

    s->input_high = !!level;
    if (!rising || s->submitted || s->main_token) {
        return;
    }
    if (!s->active_generation) {
        main_input_begin(s);
        if (s->main_token) {
            s->prefer_main = false;
        }
        return;
    }
    /* One real provider serializes both domains. A fresh TG alternates eligible
     * main/auxiliary work; busy edges never become acquisition credits.
     */
    if (s->prefer_main && main_input_ready(s)) {
        main_input_begin(s);
        if (s->main_token) {
            s->prefer_main = false;
            return;
        }
    }
    /*
     * Functional input association: an owned request consumes a fresh TG
     * boundary. Busy/earlier edges are not queued. The existing CMOS engine
     * still owns timed chunks, actual DMA and completion/error/cancellation.
     */
    submission = (NikonCmosSubmission) {
        .width = m->width,
        .height = m->height,
        .bytes_per_pixel = 2,
        .stride = m->stride,
        .destination = m->destination,
        .format = NIKON_CMOS_FORMAT_FUNCTIONAL_C170,
        .transport_seed = s->active_generation ^ m->format,
        .exposure_ns = 1,
        .readout_ns = 1,
    };
    s->cmos_generation = nikon_cmos_submit(s->cmos, &submission, &err);
    if (!s->cmos_generation) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "nikon readout generation %" PRIu64
                      " destination=0x%08x span=0x%x rejected: %s\n",
                      s->active_generation, m->destination, m->span,
                      error_get_pretty(err));
        error_free(err);
        nikon_readout_release(s->readout, s->active_generation);
        s->active_generation = 0;
        s->rejected++;
        return;
    }
    s->input_sync = s->input_run;
    s->submitted = true;
    s->prefer_main = true;
}

static void main_input_event(NikonReadoutPipeline *s, unsigned event)
{
    size_t size = (size_t)s->main_width * s->main_height * 2;
    uint32_t causes[] = { 1, 0 };
    uint64_t token = s->main_token;
    g_autofree uint8_t *data = NULL;

    if (event == NIKON_CMOS_PIPELINE_DONE &&
        !nikon_cmos_completed(s->cmos, token)) {
        s->stale++;
        return;
    }
    s->main_token = 0;
    if (event != NIKON_CMOS_PIPELINE_DONE) {
        s->main_cancelled += event == NIKON_CMOS_PIPELINE_CANCEL;
        s->main_rejected += event == NIKON_CMOS_PIPELINE_ERROR;
        return;
    }
    data = g_malloc(size);
    if (!nikon_cmos_copy_frame(s->cmos, token, data, size)) {
        s->main_rejected++;
        return;
    }
    g_free(s->main_data);
    s->main_data = g_steal_pointer(&data);
    s->main_input = (NikonCaptureInput) {
        .generation = ++s->main_generation,
        .width = s->main_width, .height = s->main_height,
        .encoding = nikon_cmos_encoding(s->cmos),
        .stride = s->main_width * 2, .data = s->main_data, .synchronized = true,
    };
    s->input = s->main_input;
    s->input.generation = ++s->input_generation;
    s->input_main = true;
    nikon_capture_publish(s->capture, NIKON_CAPTURE_FIRST_EVENT,
                          causes, ARRAY_SIZE(causes), &error_abort);
    s->main_completed++;
    s->input_notifications++;
}

static void nikon_readout_pipeline_event(void *opaque, int n, int level)
{
    NikonReadoutPipeline *s = opaque;
    uint64_t generation;
    bool input_sync;

    if (!level) {
        return;
    }
    if (s->main_token) {
        main_input_event(s, n);
        return;
    }
    if (!s->active_generation || !s->submitted) {
        return;
    }
    if (n == NIKON_CMOS_PIPELINE_DONE &&
        !nikon_cmos_completed(s->cmos, s->cmos_generation)) {
        s->stale++;
        return;
    }
    generation = s->active_generation;
    input_sync = s->input_sync;
    s->input_sync = false;
    s->active_generation = 0;
    s->cmos_generation = 0;
    s->submitted = false;
    if (n == NIKON_CMOS_PIPELINE_DONE) {
        NikonReadoutC170Mapping *m = &s->mapping;
        uint8_t *data = g_malloc(m->span);

        /* Snapshot only completed DMA, before another request can reuse RAM. */
        if (dma_memory_read(&address_space_memory, m->destination, data,
                            m->span, MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
            g_free(data);
            if (!s->input_main) {
                s->input.generation = 0;
            }
            nikon_readout_release(s->readout, generation);
            s->rejected++;
            return;
        }
        if (!nikon_readout_publish_completion(s->readout, generation)) {
            g_free(data);
            s->stale++;
            return;
        }
        g_free(s->input_data);
        s->input_data = data;
        s->input_main = false;
        s->input = (NikonCaptureInput) {
            .generation = ++s->input_generation,
            .width = m->width, .height = m->height,
            .encoding = nikon_cmos_encoding(s->cmos),
            .stride = m->stride, .data = data,
            .synchronized = input_sync,
        };
        /* Shared acquisition remains qualified by actual auxiliary DMA.
         * Independent main acquisition uses the same input namespace.
         */
        uint32_t causes[] = {
            s->completion_cause | (input_sync ? 1U : 0), 0,
        };
        Error *err = NULL;

        if (nikon_capture_publish(s->capture, NIKON_CAPTURE_FIRST_EVENT,
                                  causes, ARRAY_SIZE(causes), &err)) {
            s->completed++;
            s->input_notifications += input_sync;
        } else {
            error_free(err);
            s->stale++;
        }
    } else {
        if (!s->input_main) {
            s->input.generation = 0;
        }
        nikon_readout_release(s->readout, generation);
        s->cancelled++;
    }
}

static void nikon_readout_pipeline_cancel(void *opaque, int n, int level)
{
    NikonReadoutPipeline *s = opaque;

    if (level && s->submitted) {
        nikon_cmos_cancel(s->cmos);
    } else if (level && s->active_generation) {
        nikon_readout_release(s->readout, s->active_generation);
        s->active_generation = 0;
        s->cancelled++;
    }
    if (level) {
        s->input_sync = false;
        if (!s->input_main) {
            s->input.generation = 0;
        }
    }
}

static void nikon_readout_pipeline_reset(DeviceState *dev)
{
    NikonReadoutPipeline *s = NIKON_READOUT_PIPELINE(dev);
    uint64_t generation = s->active_generation;
    bool submitted = s->submitted || s->main_token;

    s->active_generation = 0;
    s->cmos_generation = 0;
    s->submitted = false;
    s->input_sync = false;
    s->main_token = s->input_generation = s->main_generation = 0;
    s->input_main = s->input_run = false;
    s->prefer_main = false;
    memset(s->main_selected, 0, sizeof(s->main_selected));
    memset(s->main_valid, 0, sizeof(s->main_valid));
    memset(s->main_running, 0, sizeof(s->main_running));
    s->input = (NikonCaptureInput) { 0 };
    s->main_input = (NikonCaptureInput) { 0 };
    g_clear_pointer(&s->input_data, g_free);
    g_clear_pointer(&s->main_data, g_free);
    if (generation) {
        nikon_readout_release(s->readout, generation);
    }
    if (submitted) {
        nikon_cmos_cancel(s->cmos);
    }
}

static void nikon_readout_pipeline_realize(DeviceState *dev, Error **errp)
{
    NikonReadoutPipeline *s = NIKON_READOUT_PIPELINE(dev);

    if (!s->readout || !object_dynamic_cast(OBJECT(s->readout),
                                            TYPE_NIKON_READOUT_CONTROL) ||
        !s->cmos || !object_dynamic_cast(OBJECT(s->cmos), TYPE_NIKON_CMOS) ||
        !s->capture || !object_dynamic_cast(OBJECT(s->capture),
                                            TYPE_NIKON_CAPTURE_CONTROL)) {
        error_setg(errp, "readout pipeline requires readout, CMOS and capture "
                   "links");
    }
}

static void nikon_readout_pipeline_init(Object *obj)
{
    NikonReadoutPipeline *s = NIKON_READOUT_PIPELINE(obj);

    qdev_init_gpio_in_named(DEVICE(obj), nikon_readout_pipeline_request,
                            "request", 1);
    qdev_init_gpio_in_named(DEVICE(obj), nikon_readout_pipeline_cancel,
                            "cancel", 1);
    qdev_init_gpio_in_named(DEVICE(obj), nikon_readout_pipeline_event,
                            "cmos-event", NIKON_CMOS_PIPELINE_EVENTS);
    qdev_init_gpio_in_named(DEVICE(obj), nikon_readout_pipeline_input_rise,
                            "automatic-rise", 1);
    qdev_init_gpio_in_named(DEVICE(obj), main_input_run, "input-run", 3);
    qdev_init_gpio_in_named(DEVICE(obj), main_input_select, "commit-selected", 4);
    qdev_init_gpio_in_named(DEVICE(obj), main_input_commit, "commit", 2);
    object_property_add_uint64_ptr(obj, "active-generation",
                                   &s->active_generation, OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(obj, "requests", &s->requests,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(obj, "accepted", &s->accepted,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(obj, "completed", &s->completed,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(obj, "rejected", &s->rejected,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(obj, "cancelled", &s->cancelled,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(obj, "stale", &s->stale,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(obj, "input-notifications",
                                   &s->input_notifications, OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(obj, "main-completed", &s->main_completed,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(obj, "main-rejected", &s->main_rejected,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(obj, "main-cancelled", &s->main_cancelled,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(obj, "main-input-generation",
                                   &s->main_input.generation,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint32_ptr(obj, "main-input-width",
                                   &s->main_input.width, OBJ_PROP_FLAG_READ);
    object_property_add_uint32_ptr(obj, "main-input-height",
                                   &s->main_input.height, OBJ_PROP_FLAG_READ);
    object_property_add_uint32_ptr(obj, "main-input-stride",
                                   &s->main_input.stride, OBJ_PROP_FLAG_READ);
}

static const Property nikon_readout_pipeline_properties[] = {
    DEFINE_PROP_LINK("readout", NikonReadoutPipeline, readout,
                     TYPE_NIKON_READOUT_CONTROL, DeviceState *),
    DEFINE_PROP_LINK("cmos", NikonReadoutPipeline, cmos,
                     TYPE_NIKON_CMOS, DeviceState *),
    DEFINE_PROP_LINK("capture", NikonReadoutPipeline, capture,
                     TYPE_NIKON_CAPTURE_CONTROL, DeviceState *),
};

static const VMStateDescription nikon_readout_pipeline_vmstate = {
    .name = TYPE_NIKON_READOUT_PIPELINE,
    .unmigratable = true,
};

static void nikon_readout_pipeline_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = nikon_readout_pipeline_realize;
    dc->vmsd = &nikon_readout_pipeline_vmstate;
    dc->unrealize = nikon_readout_pipeline_reset;
    device_class_set_props(dc, nikon_readout_pipeline_properties);
    device_class_set_legacy_reset(dc, nikon_readout_pipeline_reset);
}

static const TypeInfo nikon_readout_pipeline_type = {
    .name = TYPE_NIKON_READOUT_PIPELINE,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NikonReadoutPipeline),
    .instance_init = nikon_readout_pipeline_init,
    .class_init = nikon_readout_pipeline_class_init,
};

static void nikon_readout_pipeline_register_types(void)
{
    type_register_static(&nikon_readout_pipeline_type);
}
type_init(nikon_readout_pipeline_register_types)
