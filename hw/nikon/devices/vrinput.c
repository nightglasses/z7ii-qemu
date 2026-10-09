/*
 * Exact external VR input spans and finite flash snapshot/overlay storage.
 * No resident table values, helper instructions or response bytes are supplied.
 * 8 KiB blocks/128-byte programming units are an explicit functional capability
 * matching the observed descriptors and public RX flash SDK, not chip identity.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qapi/error.h"
#include "hw/core/qdev-properties.h"
#include "hw/nikon/devices.h"
#include "hw/nikon/vrinput.h"
#include "migration/vmstate.h"
#include "system/address-spaces.h"
#include "system/reset.h"

OBJECT_DECLARE_SIMPLE_TYPE(NikonVRInput, NIKON_VR_INPUT)
OBJECT_DECLARE_SIMPLE_TYPE(NikonVRSpanLoader, NIKON_VR_SPAN)

typedef struct VRInputSpan {
    NikonVRInput *input;
    MemoryRegion storage, alias;
    GBytes *snapshot;
    char *origin;
    uint64_t base, length, padding;
    NikonVRInputQualification qualification;
    bool flash, modified;
} VRInputSpan;

struct NikonVRInput {
    DeviceState parent_obj;
    MemoryRegion memory, missing;
    MemoryRegion *parent_memory;
    GPtrArray *spans;
    QEMUTimer *timer;
    VRInputSpan *pending_span;
    uint8_t *pending_data;
    uint64_t pending_token, pending_offset;
    uint32_t pending_length;
    NikonVRFlashComplete complete;
    void *complete_opaque;
    bool erase, access_failed, reported_error, mapped;
    /* Explicit host storage interface, also usable by native qtest. */
    uint64_t destination, source_address, length, token, delay_ns;
    uint64_t completed_token;
    uint32_t result;
};

struct NikonVRSpanLoader {
    DeviceState parent_obj;
    NikonVRInput *input;
    char *file, *origin, *qualification;
    uint64_t guest_base, exact_size;
    bool flash;
};

static MemTxResult vr_input_error(NikonVRInput *s, uint64_t address,
                                 uint64_t value, unsigned size, bool write)
{
    s->access_failed = true;
    nikon_access_error(&s->reported_error, address, size, write, value,
                       "VR external input");
    return MEMTX_ERROR;
}

static MemTxResult vr_missing_read(void *opaque, hwaddr offset, uint64_t *value,
                                   unsigned size, MemTxAttrs attrs)
{
    /* Invalid output for a failed transaction, never a supplied zero byte. */
    *value = 0;
    return vr_input_error(opaque, NIKON_VR_EXTERNAL_BASE + offset, 0, size, false);
}

static MemTxResult vr_missing_write(void *opaque, hwaddr offset, uint64_t value,
                                    unsigned size, MemTxAttrs attrs)
{
    return vr_input_error(opaque, NIKON_VR_EXTERNAL_BASE + offset,
                          value, size, true);
}

static MemTxResult vr_span_write(void *opaque, hwaddr offset, uint64_t value,
                                 unsigned size, MemTxAttrs attrs)
{
    VRInputSpan *span = opaque;

    /* Mutations require a real storage request, never raw guest stores. */
    return vr_input_error(span->input, span->base + offset - span->padding,
                          value, size, true);
}

static MemTxResult vr_span_read(void *opaque, hwaddr offset, uint64_t *value,
                                unsigned size, MemTxAttrs attrs)
{
    VRInputSpan *span = opaque;
    const uint8_t *data = memory_region_get_ram_ptr(&span->storage);
    unsigned i;

    *value = 0;
    if (offset < span->padding || size > span->length ||
        offset - span->padding > span->length - size) {
        return vr_input_error(span->input,
                               span->base + offset - span->padding,
                               0, size, false);
    }
    for (i = 0; i < size; i++) {
        *value |= (uint64_t)data[offset + i] << (8 * i);
    }
    return MEMTX_OK;
}

static const MemoryRegionOps vr_missing_ops = {
    .read_with_attrs = vr_missing_read,
    .write_with_attrs = vr_missing_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
};

static const MemoryRegionOps vr_span_ops = {
    .read_with_attrs = vr_span_read,
    .write_with_attrs = vr_span_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
    .impl = { .min_access_size = 1, .max_access_size = 8, .unaligned = true },
};

static VRInputSpan *vr_find_span(NikonVRInput *s, uint64_t address, uint64_t size)
{
    unsigned i;

    if (!size || address > UINT32_MAX || size > UINT64_C(0x100000000) - address) {
        return NULL;
    }
    for (i = 0; i < s->spans->len; i++) {
        VRInputSpan *span = g_ptr_array_index(s->spans, i);

        if (address >= span->base && size <= span->length &&
            address - span->base <= span->length - size) {
            return span;
        }
    }
    return NULL;
}

static bool vr_flash_address(uint64_t base)
{
    /* Five original descriptors plus the separately consumed header block. */
    return base == 0xffff0000 || base == 0xffff2000 || base == 0xffff6000 ||
           base == 0xffff8000 || base == 0xffffa000 || base == 0xffffc000;
}

bool nikon_vr_input_bind(DeviceState *dev, uint64_t guest_base, GBytes *bytes,
                         const char *origin,
                         NikonVRInputQualification qualification,
                         bool flash_block, Error **errp)
{
    NikonVRInput *s = NIKON_VR_INPUT(dev);
    VRInputSpan *span;
    g_autofree char *name = NULL;
    size_t size = bytes ? g_bytes_get_size(bytes) : 0;
    unsigned i;

    if (!s->mapped || !size || !origin || !origin[0] ||
        (qualification != NIKON_VR_DATA_PROFILE &&
         qualification != NIKON_VR_ORIGINAL_CODE)) {
        error_setg(errp, "VR span needs a realized provider, exact bytes, "
                   "source origin and explicit qualification");
        return false;
    }
    if (guest_base < NIKON_VR_EXTERNAL_BASE || guest_base > UINT32_MAX ||
        size > UINT64_C(0x100000000) - guest_base) {
        error_setg(errp, "VR span exceeds the external 32-bit address domain");
        return false;
    }
    if (flash_block && (size != NIKON_VR_FLASH_BLOCK ||
                        !vr_flash_address(guest_base))) {
        error_setg(errp, "VR flash input needs a complete source-observed "
                   "8 KiB block snapshot");
        return false;
    }
    for (i = 0; i < s->spans->len; i++) {
        VRInputSpan *other = g_ptr_array_index(s->spans, i);

        if (guest_base < other->base + other->length &&
            other->base < guest_base + size) {
            error_setg(errp, "VR external span conflicts with source '%s'",
                       other->origin);
            return false;
        }
    }
    span = g_new0(VRInputSpan, 1);
    span->input = s;
    span->base = guest_base;
    span->length = size;
    span->padding = guest_base & 0xfff; /* Existing RX TCG page alignment. */
    span->qualification = qualification;
    span->flash = flash_block;
    name = g_strdup_printf("vr.input.%u", s->spans->len);
    if (!memory_region_init_rom_device(&span->storage, OBJECT(s), &vr_span_ops,
                                       span, name, size + span->padding, errp)) {
        object_unparent(OBJECT(&span->storage));
        g_free(span);
        return false;
    }
    memcpy((uint8_t *)memory_region_get_ram_ptr(&span->storage) + span->padding,
           g_bytes_get_data(bytes, NULL), size);
    span->snapshot = g_bytes_ref(bytes);
    span->origin = g_strdup(origin);
    memory_region_init_alias(&span->alias, OBJECT(s), name, &span->storage,
                              span->padding, size);
    memory_region_add_subregion_overlap(&s->memory,
                                        guest_base - NIKON_VR_EXTERNAL_BASE,
                                        &span->alias, 1);
    g_ptr_array_add(s->spans, span);
    return true;
}

bool nikon_vr_input_load(DeviceState *dev, uint64_t guest_base,
                         const char *path, uint64_t exact_size,
                         const char *origin,
                         NikonVRInputQualification qualification,
                         bool flash_block, Error **errp)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(GBytes) bytes = NULL;
    char *data = NULL;
    size_t size;

    if (!path || !path[0] || !exact_size ||
        exact_size > NIKON_VR_EXTERNAL_SIZE) {
        error_setg(errp, "VR input file requires an explicit bounded exact size");
        return false;
    }
    if (!g_file_get_contents(path, &data, &size, &error)) {
        error_setg(errp, "Cannot read VR input '%s': %s", path, error->message);
        return false;
    }
    bytes = g_bytes_new_take(data, size);
    if (size != exact_size) {
        error_setg(errp, "VR input '%s' has %zu bytes, expected %" PRIu64,
                   path, size, exact_size);
        return false;
    }
    return nikon_vr_input_bind(dev, guest_base, bytes, origin, qualification,
                               flash_block, errp);
}

bool nikon_vr_input_is_original_code(DeviceState *dev, uint64_t address,
                                    uint64_t size)
{
    VRInputSpan *span = vr_find_span(NIKON_VR_INPUT(dev), address, size);

    return span && !span->modified &&
           span->qualification == NIKON_VR_ORIGINAL_CODE;
}

bool nikon_vr_input_describe(DeviceState *dev, uint64_t address, uint64_t size,
                            NikonVRInputInfo *info)
{
    VRInputSpan *span = vr_find_span(NIKON_VR_INPUT(dev), address, size);

    if (!span || !info) {
        return false;
    }
    *info = (NikonVRInputInfo) {
        .guest_base = span->base,
        .exact_size = span->length,
        .origin = span->origin,
        .qualification = span->modified ? NIKON_VR_DATA_PROFILE :
                                          span->qualification,
        .flash_block = span->flash,
        .modified = span->modified,
    };
    return true;
}

GBytes *nikon_vr_input_snapshot(DeviceState *dev, uint64_t guest_base)
{
    VRInputSpan *span = vr_find_span(NIKON_VR_INPUT(dev), guest_base, 1);

    return span && span->base == guest_base ? g_bytes_ref(span->snapshot) : NULL;
}

static void vr_abort(NikonVRInput *s)
{
    timer_del(s->timer);
    s->pending_span = NULL;
    s->complete = NULL;
    s->complete_opaque = NULL;
    g_clear_pointer(&s->pending_data, g_free);
}

bool nikon_vr_input_cancel(DeviceState *dev, uint64_t token)
{
    NikonVRInput *s = NIKON_VR_INPUT(dev);

    if (!s->pending_span || token != s->pending_token) {
        return false;
    }
    vr_abort(s);
    return true;
}

static void vr_flash_finish(void *opaque)
{
    NikonVRInput *s = opaque;
    VRInputSpan *span = s->pending_span;
    NikonVRFlashComplete complete = s->complete;
    void *complete_opaque = s->complete_opaque;
    uint64_t token = s->pending_token;
    uint8_t *target;
    uint32_t i;

    if (!span) {
        return;
    }
    target = (uint8_t *)memory_region_get_ram_ptr(&span->storage) +
             span->padding + s->pending_offset;
    if (s->erase) {
        memset(target, 0xff, s->pending_length);
    } else {
        for (i = 0; i < s->pending_length; i++) {
            target[i] &= s->pending_data[i];
        }
    }
    span->modified = true;
    memory_region_flush_rom_device(&span->storage,
                                   span->padding + s->pending_offset,
                                   s->pending_length);
    vr_abort(s); /* Clear request before invoking the actual producer consumer. */
    s->completed_token = token;
    s->result = NIKON_VR_FLASH_OK;
    if (complete) {
        complete(complete_opaque, token, NIKON_VR_FLASH_OK);
    }
}

static VRInputSpan *vr_flash_validate(NikonVRInput *s, uint64_t token,
                                     uint64_t destination, uint32_t length,
                                     uint64_t delay_ns, Error **errp)
{
    VRInputSpan *span = vr_find_span(s, destination, length);
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    if (!s->mapped || s->pending_span || !token || !span || !span->flash ||
        !delay_ns || delay_ns > INT64_MAX - now) {
        error_setg(errp, "VR flash request needs idle storage, a token, "
                   "supplied block bounds and a finite positive duration");
        return NULL;
    }
    return span;
}

static void vr_flash_start(NikonVRInput *s, VRInputSpan *span, uint64_t token,
                           uint64_t destination, uint32_t length,
                           uint64_t delay_ns, NikonVRFlashComplete complete,
                           void *opaque)
{
    s->pending_span = span;
    s->pending_token = token;
    s->pending_offset = destination - span->base;
    s->pending_length = length;
    s->complete = complete;
    s->complete_opaque = opaque;
    timer_mod(s->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + delay_ns);
}

bool nikon_vr_input_erase(DeviceState *dev, uint64_t token, uint64_t destination,
                         uint64_t delay_ns, NikonVRFlashComplete complete,
                         void *opaque, Error **errp)
{
    NikonVRInput *s = NIKON_VR_INPUT(dev);
    VRInputSpan *span = vr_flash_validate(s, token, destination,
                                         NIKON_VR_FLASH_BLOCK, delay_ns, errp);

    if (!span) {
        return false;
    }
    s->erase = true;
    vr_flash_start(s, span, token, destination, NIKON_VR_FLASH_BLOCK,
                    delay_ns, complete, opaque);
    return true;
}

bool nikon_vr_input_program(DeviceState *dev, AddressSpace *source,
                           uint64_t token, uint64_t destination,
                           uint64_t source_address, uint32_t length,
                           uint64_t delay_ns, NikonVRFlashComplete complete,
                           void *opaque, Error **errp)
{
    NikonVRInput *s = NIKON_VR_INPUT(dev);
    VRInputSpan *span;
    g_autofree uint8_t *data = NULL;
    const uint8_t *target;
    uint32_t i;

    if (!source || !length || length > NIKON_VR_FLASH_BLOCK ||
        destination % NIKON_VR_FLASH_PROGRAM_UNIT ||
        length % NIKON_VR_FLASH_PROGRAM_UNIT || source_address > UINT32_MAX ||
        length > UINT64_C(0x100000000) - source_address) {
        error_setg(errp, "VR program requires a real source address space and "
                   "bounded aligned 128-byte programming units");
        return false;
    }
    span = vr_flash_validate(s, token, destination, length, delay_ns, errp);
    if (!span) {
        return false;
    }
    data = g_malloc(length);
    if (address_space_read(source, source_address, MEMTXATTRS_UNSPECIFIED,
                            data, length) != MEMTX_OK) {
        error_setg(errp, "VR flash source bus read failed");
        return false;
    }
    target = (uint8_t *)memory_region_get_ram_ptr(&span->storage) + span->padding +
             destination - span->base;
    for (i = 0; i < length; i++) {
        if ((target[i] & data[i]) != data[i]) {
            error_setg(errp, "VR program cannot set an unerased flash bit");
            return false;
        }
    }
    s->pending_data = g_steal_pointer(&data);
    s->erase = false;
    vr_flash_start(s, span, token, destination, length, delay_ns, complete, opaque);
    return true;
}

static void vr_host_operation(Object *obj, const char *operation, Error **errp)
{
    NikonVRInput *s = NIKON_VR_INPUT(obj);
    bool accepted;

    if (!strcmp(operation, "erase")) {
        accepted = nikon_vr_input_erase(DEVICE(s), s->token, s->destination,
                                       s->delay_ns, NULL, NULL, errp);
    } else if (!strcmp(operation, "program")) {
        if (s->length > UINT32_MAX) {
            error_setg(errp, "VR program length exceeds its bounded interface");
            return;
        }
        accepted = nikon_vr_input_program(DEVICE(s), &address_space_memory,
                                         s->token, s->destination,
                                         s->source_address, s->length,
                                         s->delay_ns, NULL, NULL, errp);
    } else if (!strcmp(operation, "cancel")) {
        if (!nikon_vr_input_cancel(DEVICE(s), s->token)) {
            error_setg(errp, "VR cancel token does not match a pending request");
        }
        return;
    } else {
        error_setg(errp, "VR host storage operation must be erase/program/cancel");
        return;
    }
    if (!accepted) {
        s->result = NIKON_VR_FLASH_REJECTED;
    }
}

static bool vr_input_get_busy(Object *obj, Error **errp)
{
    return NIKON_VR_INPUT(obj)->pending_span != NULL;
}

static bool vr_input_get_access_failed(Object *obj, Error **errp)
{
    return NIKON_VR_INPUT(obj)->access_failed;
}

static bool vr_input_get_original_code(Object *obj, Error **errp)
{
    NikonVRInput *s = NIKON_VR_INPUT(obj);

    return nikon_vr_input_is_original_code(DEVICE(s), s->destination, s->length);
}

static void vr_input_reset(DeviceState *dev)
{
    NikonVRInput *s = NIKON_VR_INPUT(dev);

    vr_abort(s);
    s->access_failed = s->reported_error = false;
    s->completed_token = 0;
    s->result = NIKON_VR_FLASH_OK;
    /* Flash contents persist across reset; input snapshots remain immutable. */
}

static void vr_input_realize(DeviceState *dev, Error **errp)
{
    NikonVRInput *s = NIKON_VR_INPUT(dev);

    if (!s->parent_memory) {
        s->parent_memory = get_system_memory();
    }
    memory_region_add_subregion(s->parent_memory, NIKON_VR_EXTERNAL_BASE,
                                &s->memory);
    s->mapped = true;
    qemu_register_resettable(OBJECT(dev));
}

static void vr_input_unrealize(DeviceState *dev)
{
    NikonVRInput *s = NIKON_VR_INPUT(dev);

    qemu_unregister_resettable(OBJECT(dev));
    vr_abort(s);
    memory_region_del_subregion(s->parent_memory, &s->memory);
    s->mapped = false;
}

static void vr_input_init(Object *obj)
{
    NikonVRInput *s = NIKON_VR_INPUT(obj);

    s->spans = g_ptr_array_new();
    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, vr_flash_finish, s);
    memory_region_init(&s->memory, obj, "vr.external", NIKON_VR_EXTERNAL_SIZE);
    memory_region_init_io(&s->missing, obj, &vr_missing_ops, s, "vr.missing",
                          NIKON_VR_EXTERNAL_SIZE);
    memory_region_add_subregion(&s->memory, 0, &s->missing);
    object_property_add_bool(obj, "busy", vr_input_get_busy, NULL);
    object_property_add_str(obj, "operation", NULL, vr_host_operation);
    object_property_add_bool(obj, "access-failed", vr_input_get_access_failed,
                              NULL);
    object_property_add_bool(obj, "original-code", vr_input_get_original_code,
                              NULL);
    object_property_add_uint64_ptr(obj, "completed-token", &s->completed_token,
                                    OBJ_PROP_FLAG_READ);
    object_property_add_uint32_ptr(obj, "result", &s->result, OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(obj, "destination", &s->destination,
                                    OBJ_PROP_FLAG_READWRITE);
    object_property_add_uint64_ptr(obj, "source-address", &s->source_address,
                                    OBJ_PROP_FLAG_READWRITE);
    object_property_add_uint64_ptr(obj, "length", &s->length,
                                    OBJ_PROP_FLAG_READWRITE);
    object_property_add_uint64_ptr(obj, "token", &s->token, OBJ_PROP_FLAG_READWRITE);
    object_property_add_uint64_ptr(obj, "delay-ns", &s->delay_ns,
                                    OBJ_PROP_FLAG_READWRITE);
}

static void vr_input_finalize(Object *obj)
{
    NikonVRInput *s = NIKON_VR_INPUT(obj);
    unsigned i;

    timer_free(s->timer);
    for (i = 0; i < s->spans->len; i++) {
        VRInputSpan *span = g_ptr_array_index(s->spans, i);

        memory_region_del_subregion(&s->memory, &span->alias);
        object_unparent(OBJECT(&span->alias));
        object_unparent(OBJECT(&span->storage));
        g_bytes_unref(span->snapshot);
        g_free(span->origin);
        g_free(span);
    }
    g_ptr_array_free(s->spans, true);
    memory_region_del_subregion(&s->memory, &s->missing);
}

static const VMStateDescription vr_input_vmstate = {
    .name = TYPE_NIKON_VR_INPUT,
    .unmigratable = true, /* Sparse source metadata/request lifecycle not serialized. */
};

static void vr_input_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = vr_input_realize;
    dc->unrealize = vr_input_unrealize;
    dc->vmsd = &vr_input_vmstate;
    dc->hotpluggable = false;
    device_class_set_legacy_reset(dc, vr_input_reset);
}

DeviceState *nikon_vr_input_create(Object *owner, MemoryRegion *memory,
                                  Error **errp)
{
    DeviceState *dev;

    if (!owner || !memory) {
        error_setg(errp, "VR input factory requires an explicit owner and memory root");
        return NULL;
    }
    dev = qdev_new(TYPE_NIKON_VR_INPUT);

    NIKON_VR_INPUT(dev)->parent_memory = memory;
    object_property_add_child(owner, "vr-input", OBJECT(dev));
    if (!qdev_realize(dev, NULL, errp)) {
        object_unparent(OBJECT(dev));
        object_unref(OBJECT(dev));
        return NULL;
    }
    object_unref(OBJECT(dev));
    return dev;
}

static void vr_span_realize(DeviceState *dev, Error **errp)
{
    NikonVRSpanLoader *s = NIKON_VR_SPAN(dev);
    NikonVRInputQualification qualification;

    if (!s->input || !s->qualification) {
        error_setg(errp, "VR span loader requires its input provider and qualification");
        return;
    }
    if (!strcmp(s->qualification, "data")) {
        qualification = NIKON_VR_DATA_PROFILE;
    } else if (!strcmp(s->qualification, "original-code")) {
        qualification = NIKON_VR_ORIGINAL_CODE;
    } else {
        error_setg(errp, "VR span qualification must be data or original-code");
        return;
    }
    nikon_vr_input_load(DEVICE(s->input), s->guest_base, s->file, s->exact_size,
                        s->origin, qualification, s->flash, errp);
}

static const Property vr_span_properties[] = {
    DEFINE_PROP_LINK("input", NikonVRSpanLoader, input, TYPE_NIKON_VR_INPUT,
                     NikonVRInput *),
    DEFINE_PROP_STRING("file", NikonVRSpanLoader, file),
    DEFINE_PROP_STRING("origin", NikonVRSpanLoader, origin),
    DEFINE_PROP_STRING("qualification", NikonVRSpanLoader, qualification),
    DEFINE_PROP_UINT64("guest-base", NikonVRSpanLoader, guest_base, 0),
    DEFINE_PROP_UINT64("exact-size", NikonVRSpanLoader, exact_size, 0),
    DEFINE_PROP_BOOL("flash-block", NikonVRSpanLoader, flash, false),
};

static void vr_span_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = vr_span_realize;
    dc->hotpluggable = false;
    device_class_set_props(dc, vr_span_properties);
}

static const TypeInfo vr_input_types[] = {
    {
        .name = TYPE_NIKON_VR_INPUT,
        .parent = TYPE_DEVICE,
        .instance_size = sizeof(NikonVRInput),
        .instance_init = vr_input_init,
        .instance_finalize = vr_input_finalize,
        .class_init = vr_input_class_init,
    }, {
        .name = TYPE_NIKON_VR_SPAN,
        .parent = TYPE_DEVICE,
        .instance_size = sizeof(NikonVRSpanLoader),
        .class_init = vr_span_class_init,
    },
};

DEFINE_TYPES(vr_input_types)
