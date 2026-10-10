/* Source-bounded frame save handoff; no file or image is synthesized.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "hw/nikon/save.h"
#include "migration/vmstate.h"

struct NikonSaveState {
    DeviceState parent_obj;
    uint32_t phase;
    uint64_t requests, commits, failures, token;
    uint32_t source, length, error;
    uint32_t format;
};

static NikonSaveState *save_state(DeviceState *dev, Error **errp)
{
    if (!dev || !object_dynamic_cast(OBJECT(dev), TYPE_NIKON_SAVE) ||
        !qdev_is_realized(dev)) {
        error_setg(errp, "save handoff requires a realized nikon-save device");
        return NULL;
    }
    return NIKON_SAVE(dev);
}

static bool save_token(NikonSaveState *s, uint64_t token, Error **errp)
{
    if (s->phase != NIKON_SAVE_REQUESTED) {
        error_setg(errp, "save handoff is not pending (phase=%u)", s->phase);
        return false;
    }
    if (!token || token != s->token) {
        error_setg(errp, "save handoff token does not match the pending request");
        return false;
    }
    return true;
}

bool nikon_save_request(DeviceState *dev, const NikonSaveRequest *request,
                        Error **errp)
{
    NikonSaveState *s = save_state(dev, errp);

    if (!s) {
        return false;
    }
    if (!request || !request->token || !request->source || !request->length) {
        error_setg(errp, "save request needs nonzero token, source and length");
        return false;
    }
    if (request->format > NIKON_SAVE_FORMAT_JPEG) {
        error_setg(errp, "save request format is outside the source-bounded enum");
        return false;
    }
    if (s->phase == NIKON_SAVE_REQUESTED) {
        error_setg(errp, "save handoff already has a pending request");
        return false;
    }
    s->phase = NIKON_SAVE_REQUESTED;
    s->requests++;
    s->token = request->token;
    s->source = request->source;
    s->length = request->length;
    s->format = request->format;
    s->error = 0;
    return true;
}

bool nikon_save_commit(DeviceState *dev, uint64_t token, Error **errp)
{
    NikonSaveState *s = save_state(dev, errp);

    if (!s || !save_token(s, token, errp)) {
        return false;
    }
    s->phase = NIKON_SAVE_COMMITTED;
    s->commits++;
    return true;
}

bool nikon_save_fail(DeviceState *dev, uint64_t token, uint32_t error,
                     Error **errp)
{
    NikonSaveState *s = save_state(dev, errp);

    if (!s || !save_token(s, token, errp)) {
        return false;
    }
    if (!error) {
        error_setg(errp, "save failure needs a nonzero source error code");
        return false;
    }
    s->phase = NIKON_SAVE_FAILED;
    s->failures++;
    s->error = error;
    return true;
}

bool nikon_save_cancel(DeviceState *dev, uint64_t token, Error **errp)
{
    NikonSaveState *s = save_state(dev, errp);

    if (!s || !save_token(s, token, errp)) {
        return false;
    }
    s->phase = NIKON_SAVE_CANCELLED;
    return true;
}

bool nikon_save_status(DeviceState *dev, NikonSaveStatus *status,
                       Error **errp)
{
    NikonSaveState *s = save_state(dev, errp);

    if (!s) {
        return false;
    }
    if (!status) {
        error_setg(errp, "save status output is required");
        return false;
    }
    status->phase = s->phase;
    status->requests = s->requests;
    status->commits = s->commits;
    status->failures = s->failures;
    status->token = s->token;
    status->source = s->source;
    status->length = s->length;
    status->format = s->format;
    status->error = s->error;
    return true;
}

static void save_reset(DeviceState *dev)
{
    NikonSaveState *s = NIKON_SAVE(dev);

    s->phase = NIKON_SAVE_IDLE;
    s->requests = s->commits = s->failures = s->token = 0;
    s->source = s->length = s->error = 0;
    s->format = NIKON_SAVE_FORMAT_UNKNOWN;
}

static void save_init(Object *obj)
{
    NikonSaveState *s = NIKON_SAVE(obj);

    object_property_add_uint64_ptr(obj, "requests", &s->requests,
                                    OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(obj, "commits", &s->commits,
                                    OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(obj, "failures", &s->failures,
                                    OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(obj, "token", &s->token, OBJ_PROP_FLAG_READ);
    object_property_add_uint32_ptr(obj, "source", &s->source,
                                    OBJ_PROP_FLAG_READ);
    object_property_add_uint32_ptr(obj, "length", &s->length,
                                    OBJ_PROP_FLAG_READ);
    object_property_add_uint32_ptr(obj, "format", &s->format,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint32_ptr(obj, "error", &s->error,
                                    OBJ_PROP_FLAG_READ);
    object_property_add_uint32_ptr(obj, "phase", &s->phase,
                                   OBJ_PROP_FLAG_READ);
}

static const VMStateDescription save_vmstate = {
    .name = TYPE_NIKON_SAVE,
    .unmigratable = true,
};

static void save_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Nikon source-bounded frame save handoff";
    dc->vmsd = &save_vmstate;
    device_class_set_legacy_reset(dc, save_reset);
}

static const TypeInfo save_type = {
    .name = TYPE_NIKON_SAVE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(NikonSaveState),
    .instance_init = save_init,
    .class_init = save_class_init,
};

static void save_register(void)
{
    type_register_static(&save_type);
}
type_init(save_register)
