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

struct NikonReadoutPipeline {
    SysBusDevice parent_obj;
    DeviceState *readout;
    DeviceState *cmos;
    DeviceState *capture;
    uint64_t active_generation;
    uint32_t completion_cause;
    uint64_t requests;
    uint64_t accepted;
    uint64_t completed;
    uint64_t rejected;
    uint64_t cancelled;
    uint64_t stale;
    NikonReadoutC170Mapping mapping;
    NikonCaptureInput input;
    uint8_t *input_data;
};

bool nikon_readout_pipeline_input(DeviceState *dev, NikonCaptureInput *input)
{
    NikonReadoutPipeline *s = NIKON_READOUT_PIPELINE(dev);

    *input = s->input;
    return input->generation != 0;
}

static void nikon_readout_pipeline_request(void *opaque, int n, int level)
{
    NikonReadoutPipeline *s = opaque;
    NikonReadoutRequest request;
    NikonReadoutC170Mapping mapping;
    NikonCmosSubmission submission;
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

    submission = (NikonCmosSubmission) {
        .width = mapping.width,
        .height = mapping.height,
        .bytes_per_pixel = 2,
        .stride = mapping.stride,
        .destination = mapping.destination,
        .format = NIKON_CMOS_FORMAT_FUNCTIONAL_C170,
        .transport_seed = request.generation ^ mapping.format,
        .exposure_ns = 1,
        .readout_ns = 1,
    };
    if (!nikon_cmos_submit(s->cmos, &submission, &err)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "nikon readout generation %" PRIu64
                      " destination=0x%08x span=0x%x rejected: %s\n",
                      request.generation, mapping.destination, mapping.span,
                      error_get_pretty(err));
        error_free(err);
        s->rejected++;
        return;
    }
    s->active_generation = request.generation;
    s->mapping = mapping;
    s->completion_cause = mapping.completion_cause;
    if (!nikon_readout_publish_ready(s->readout, request.generation)) {
        s->active_generation = 0;
        s->stale++;
        nikon_cmos_cancel(s->cmos);
        return;
    }
    s->accepted++;
}

static void nikon_readout_pipeline_event(void *opaque, int n, int level)
{
    NikonReadoutPipeline *s = opaque;
    uint64_t generation;

    if (!level || !s->active_generation) {
        return;
    }
    generation = s->active_generation;
    s->active_generation = 0;
    if (n == NIKON_CMOS_PIPELINE_DONE) {
        NikonReadoutC170Mapping *m = &s->mapping;
        uint8_t *data = g_malloc(m->span);

        /* Snapshot only completed DMA, before another request can reuse RAM. */
        if (dma_memory_read(&address_space_memory, m->destination, data,
                            m->span, MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
            g_free(data);
            s->input.generation = 0;
            nikon_readout_release(s->readout, generation);
            s->rejected++;
            return;
        }
        g_free(s->input_data);
        s->input_data = data;
        s->input = (NikonCaptureInput) {
            .generation = generation, .width = m->width, .height = m->height,
            .stride = m->stride, .data = data,
        };
        if (nikon_readout_publish_completion(s->readout, generation)) {
            uint32_t causes[] = { s->completion_cause, 0 };
            Error *err = NULL;

            if (nikon_capture_publish(s->capture, NIKON_CAPTURE_FIRST_EVENT,
                                      causes, ARRAY_SIZE(causes), &err)) {
                s->completed++;
            } else {
                error_free(err);
                s->stale++;
            }
        } else {
            s->input.generation = 0;
            s->stale++;
        }
    } else {
        s->input.generation = 0;
        nikon_readout_release(s->readout, generation);
        s->cancelled++;
    }
}

static void nikon_readout_pipeline_cancel(void *opaque, int n, int level)
{
    NikonReadoutPipeline *s = opaque;

    if (level && s->active_generation) {
        nikon_cmos_cancel(s->cmos);
    }
    if (level) {
        s->input.generation = 0;
    }
}

static void nikon_readout_pipeline_reset(DeviceState *dev)
{
    NikonReadoutPipeline *s = NIKON_READOUT_PIPELINE(dev);
    uint64_t generation = s->active_generation;

    s->active_generation = 0;
    s->input = (NikonCaptureInput) { 0 };
    g_clear_pointer(&s->input_data, g_free);
    if (generation) {
        nikon_readout_release(s->readout, generation);
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
