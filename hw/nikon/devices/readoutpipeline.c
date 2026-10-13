/* Bounded functional C1.70 readout-control to CMOS producer/consumer join.
 * The profile mapping is not a physical sensor or pixel-format claim.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/nikon/cmos.h"
#include "hw/nikon/readoutpipeline.h"
#include "hw/nikon/sensor.h"
#include "migration/vmstate.h"

OBJECT_DECLARE_SIMPLE_TYPE(NikonReadoutPipeline, NIKON_READOUT_PIPELINE)

struct NikonReadoutPipeline {
    DeviceState parent_obj;
    DeviceState *readout;
    DeviceState *cmos;
    uint64_t active_generation;
    uint64_t requests;
    uint64_t accepted;
    uint64_t completed;
    uint64_t rejected;
    uint64_t cancelled;
    uint64_t stale;
};

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
        request.completion_marker != 1 ||
        !nikon_readout_c170_mapping(s->readout, request.generation, &mapping,
                                    &err)) {
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
        error_free(err);
        s->rejected++;
        return;
    }
    s->active_generation = request.generation;
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
        if (nikon_readout_publish_completion(s->readout, generation)) {
            s->completed++;
        } else {
            s->stale++;
        }
    } else {
        s->cancelled++;
    }
}

static void nikon_readout_pipeline_reset(DeviceState *dev)
{
    NikonReadoutPipeline *s = NIKON_READOUT_PIPELINE(dev);
    bool active = s->active_generation != 0;

    s->active_generation = 0;
    if (active) {
        nikon_cmos_cancel(s->cmos);
    }
}

static void nikon_readout_pipeline_realize(DeviceState *dev, Error **errp)
{
    NikonReadoutPipeline *s = NIKON_READOUT_PIPELINE(dev);

    if (!s->readout || !object_dynamic_cast(OBJECT(s->readout),
                                            TYPE_NIKON_READOUT_CONTROL) ||
        !s->cmos || !object_dynamic_cast(OBJECT(s->cmos), TYPE_NIKON_CMOS)) {
        error_setg(errp, "readout pipeline requires readout and CMOS links");
    }
}

static void nikon_readout_pipeline_init(Object *obj)
{
    NikonReadoutPipeline *s = NIKON_READOUT_PIPELINE(obj);

    qdev_init_gpio_in_named(DEVICE(obj), nikon_readout_pipeline_request,
                            "request", 1);
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
    device_class_set_props(dc, nikon_readout_pipeline_properties);
    device_class_set_legacy_reset(dc, nikon_readout_pipeline_reset);
}

static const TypeInfo nikon_readout_pipeline_type = {
    .name = TYPE_NIKON_READOUT_PIPELINE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(NikonReadoutPipeline),
    .instance_init = nikon_readout_pipeline_init,
    .class_init = nikon_readout_pipeline_class_init,
};

static void nikon_readout_pipeline_register_types(void)
{
    type_register_static(&nikon_readout_pipeline_type);
}
type_init(nikon_readout_pipeline_register_types)
