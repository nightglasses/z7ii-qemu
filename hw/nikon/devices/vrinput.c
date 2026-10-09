/*
 * Exact external VR input spans and finite flash snapshot/overlay storage.
 * No resident table values, helper instructions or response bytes are supplied.
 * 8 KiB blocks/128-byte programming units are an explicit functional capability
 * matching the observed descriptors and public RX flash SDK, not chip identity.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/bswap.h"
#include "qemu/timer.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "hw/nikon/devices.h"
#include "hw/nikon/vrinput.h"
#include "hw/nikon/vrupload.h"
#include "migration/vmstate.h"
#include "system/address-spaces.h"
#include "system/reset.h"
#include "system/block-backend.h"

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

typedef enum VRUploadJob {
    VR_UPLOAD_SNAPSHOT,
    VR_UPLOAD_PREPARE,
    VR_UPLOAD_RECORD,
    VR_UPLOAD_FINAL,
} VRUploadJob;

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
    NikonVRUpload *upload;
    char *upload_origin;
    NikonVRStore *store;
    BlockBackend *upload_body_blk, *upload_metadata_blk;
    uint32_t upload_storage_base, upload_storage_size;
    uint64_t store_completed_token;
    int32_t store_error;
    NikonVRStoreComplete store_complete;
    void *store_opaque;
    bool upload_record_persistence, upload_prepared, upload_durable_final;
    uint32_t upload_durable_received, upload_job_received;
    uint64_t upload_job_token;
    VRUploadJob upload_job;
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

    if (!s->mapped || s->pending_span || s->store || !token || !span || !span->flash ||
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

bool nikon_vr_input_upload_begin(DeviceState *dev, uint32_t stream_size,
                               const char *origin, Error **errp)
{
    NikonVRInput *s = NIKON_VR_INPUT(dev);
    NikonVRUpload *upload;
    char *new_origin;

    if (s->store) {
        error_setg(errp, "VR upload cannot replace a persisting source snapshot");
        return false;
    }
    if (!origin || !*origin) {
        error_setg(errp, "VR staged input requires an explicit source origin");
        return false;
    }
    upload = nikon_vr_upload_new(stream_size, errp);
    if (!upload) {
        return false;
    }
    new_origin = g_strdup(origin);
    nikon_vr_upload_free(s->upload);
    g_free(s->upload_origin);
    s->upload = upload;
    s->upload_origin = new_origin;
    s->upload_prepared = s->upload_durable_final = false;
    s->upload_durable_received = 0;
    s->store_error = 0;
    return true;
}

static bool vr_input_store_frame(NikonVRInput *s, uint8_t command, Error **errp);
static void vr_input_store_cancel(NikonVRInput *s);

bool nikon_vr_input_upload_capture(DeviceState *dev, AddressSpace *source,
                                 Error **errp)
{
    NikonVRInput *s = NIKON_VR_INPUT(dev);
    uint8_t mode, cs, pointer[4], count[4], frame[268];
    uint32_t address, remaining, size;
    bool record = false;
    g_autoptr(GBytes) bytes = NULL;

    if (!source || !s->upload) {
        error_setg(errp, "VR upload capture needs a real source and begun stage");
        return false;
    }
    if (address_space_read(source, 0x11904, MEMTXATTRS_UNSPECIFIED,
                           pointer, 4) != MEMTX_OK ||
        address_space_read(source, 0x82008, MEMTXATTRS_UNSPECIFIED,
                           count, 4) != MEMTX_OK) {
        error_setg(errp, "VR upload capture state bus read failed");
        return false;
    }
    address = ldl_le_p(pointer);
    remaining = ldl_le_p(count);
    /* Identify a started command before rejecting its capture envelope.  The
     * command is meaningful only in an actual RX ring with a received byte;
     * an empty ring must not turn stale contents into a new record attempt.
     */
    if ((address == 0x1587c || address == 0x15990) && remaining < 276) {
        if (address_space_read(source, address, MEMTXATTRS_UNSPECIFIED,
                               frame, 1) != MEMTX_OK) {
            error_setg(errp, "VR upload RX command bus read failed");
            return false;
        }
        record = frame[0] == 0xf7;
    }
    if (address_space_read(source, 0x10f55, MEMTXATTRS_UNSPECIFIED,
                           &mode, 1) != MEMTX_OK ||
        address_space_read(source, 0x8c044, MEMTXATTRS_UNSPECIFIED,
                           &cs, 1) != MEMTX_OK) {
        error_setg(errp, "VR upload capture state bus read failed");
        goto rejected;
    }
    /* Source IRQ15 invokes 177C3 on a partial 276-byte receive before calling
     * the resident callback. 11904 is consumed RX; its R2 is selected TX.
     */
    if ((mode != 1 && mode != 2) || !(cs & 0x80) || remaining > 276 ||
        (address != 0x1587c && address != 0x15990)) {
        error_setg(errp, "VR upload requires guest update mode, high CS and "
                   "the actual consumed RX ring");
        goto rejected;
    }
    size = 276 - remaining;
    if (size != 152 && size != 268) {
        error_setg(errp, "VR upload DMA count does not describe a whole packet");
        goto rejected;
    }
    if (address_space_read(source, address, MEMTXATTRS_UNSPECIFIED,
                           frame, size) != MEMTX_OK) {
        error_setg(errp, "VR upload RX packet bus read failed");
        goto rejected;
    }
    if (frame[0] != 0xf5 && mode != 2) {
        error_setg(errp, "VR record/status capture requires actual guest mode 2");
        goto rejected;
    }
    if (s->store && frame[0] != 0xf8) {
        error_setg(errp, "VR update must poll until actual persistence completes");
        goto rejected;
    }
    if (s->upload_record_persistence && frame[0] != 0xf5 &&
        frame[0] != 0xf8 &&
        (!s->upload_prepared || s->store_error ||
         s->upload_durable_received != nikon_vr_upload_received(s->upload))) {
        error_setg(errp, "VR update needs durable preparation and prior records");
        goto rejected;
    }
    bytes = g_bytes_new(frame, size);
    if (!nikon_vr_upload_receive(s->upload, bytes, errp)) {
        goto rejected;
    }
    return !s->upload_record_persistence ||
           vr_input_store_frame(s, frame[0], errp);

rejected:
    if (s->upload_record_persistence && record) {
        /* MAS applies status 2 to the current chunk, including a short F7.
         * Suppress an old in-flight completion as well as its eligibility.
         * Issued disk writes are not rolled back; a new F5 starts a new stage.
         */
        vr_input_store_cancel(s);
        s->store_error = -EINVAL;
        s->upload_prepared = s->upload_durable_final = false;
    }
    return false;
}

GBytes *nikon_vr_input_upload_snapshot(DeviceState *dev, const char **origin)
{
    NikonVRInput *s = NIKON_VR_INPUT(dev);

    *origin = s->upload_origin;
    return s->upload ? nikon_vr_upload_records(s->upload) : NULL;
}

static void vr_input_store_done(void *opaque, uint64_t token, int result)
{
    NikonVRInput *s = opaque;
    NikonVRStoreComplete complete = s->store_complete;
    void *consumer = s->store_opaque;

    s->store_complete = NULL;
    s->store_opaque = NULL;
    s->store_completed_token = token;
    s->store_error = result;
    if (s->upload_job != VR_UPLOAD_SNAPSHOT) {
        if (!result) {
            if (s->upload_job == VR_UPLOAD_PREPARE) {
                s->upload_prepared = true;
            } else if (s->upload_job == VR_UPLOAD_RECORD) {
                s->upload_durable_received = s->upload_job_received;
            } else {
                s->upload_durable_final = true;
            }
        } else {
            s->upload_prepared = s->upload_durable_final = false;
        }
    }
    if (complete) {
        complete(consumer, token, result);
    }
}

static bool vr_input_store_frame(NikonVRInput *s, uint8_t command, Error **errp)
{
    g_autoptr(GBytes) record = NULL;
    bool accepted;

    if (command == 0xf8) {
        return true;
    }
    if (s->upload_job_token == UINT64_MAX) {
        s->store_error = -EOVERFLOW;
        s->upload_prepared = s->upload_durable_final = false;
        error_setg(errp, "VR native storage request token space exhausted");
        return false;
    }
    s->upload_job_token++;
    s->store_complete = NULL;
    s->store_opaque = NULL;
    s->store_error = 0;
    s->store_completed_token = 0;
    if (command == 0xf7) {
        s->upload_job = VR_UPLOAD_RECORD;
        s->upload_job_received = nikon_vr_upload_received(s->upload);
        record = nikon_vr_upload_last_record(s->upload);
        accepted = nikon_vr_store_record(&s->store, record,
            s->upload_job_received == 262, s->upload_body_blk,
            s->upload_metadata_blk, s->upload_storage_base,
            s->upload_storage_size, s->upload_job_token,
            vr_input_store_done, s, errp);
    } else {
        s->upload_job = command == 0xf5 ? VR_UPLOAD_PREPARE : VR_UPLOAD_FINAL;
        if (command == 0xf5) {
            s->upload_prepared = s->upload_durable_final = false;
            s->upload_durable_received = 0;
        }
        accepted = nikon_vr_store_flush(&s->store, s->upload_body_blk,
            s->upload_metadata_blk, s->upload_storage_base,
            s->upload_storage_size, s->upload_job_token,
            vr_input_store_done, s, errp);
    }
    if (!accepted) {
        s->store_error = -EINVAL;
        s->upload_prepared = s->upload_durable_final = false;
    }
    return accepted;
}

NikonVRUploadProgress nikon_vr_input_upload_progress(DeviceState *dev)
{
    NikonVRInput *s = NIKON_VR_INPUT(dev);

    return (NikonVRUploadProgress) {
        .durable_received = s->upload_durable_received,
        .prepared = s->upload_prepared,
        .sealed = s->upload && nikon_vr_upload_sealed(s->upload),
        .durable_final = s->upload_durable_final,
        .busy = s->store != NULL,
        .error = s->store_error,
    };
}

bool nikon_vr_input_upload_status(DeviceState *dev, uint8_t *status)
{
    NikonVRInput *s = NIKON_VR_INPUT(dev);

    if (!s->upload_record_persistence || !s->upload || !s->upload_prepared ||
        s->store || s->store_error) {
        return false;
    }
    if (nikon_vr_upload_sealed(s->upload)) {
        if (!s->upload_durable_final) {
            return false;
        }
        *status = 3;
    } else {
        if (s->upload_durable_received != nikon_vr_upload_received(s->upload)) {
            return false;
        }
        *status = 2;
    }
    return true;
}

static void vr_input_store_cancel(NikonVRInput *s)
{
    if (s->store) {
        nikon_vr_store_cancel(&s->store, nikon_vr_store_token(s->store));
    }
    s->store_complete = NULL;
    s->store_opaque = NULL;
}

void nikon_vr_input_upload_cancel(DeviceState *dev)
{
    NikonVRInput *s = NIKON_VR_INPUT(dev);

    vr_input_store_cancel(s);
    s->upload_prepared = s->upload_durable_final = false;
    s->upload_durable_received = 0;
    if (s->upload) {
        nikon_vr_upload_cancel(s->upload);
    }
}

bool nikon_vr_input_upload_persist(DeviceState *dev, BlockBackend *body,
                                 BlockBackend *metadata, uint32_t storage_base,
                                 uint32_t storage_size, uint64_t token,
                                 NikonVRStoreComplete complete, void *opaque,
                                 Error **errp)
{
    NikonVRInput *s = NIKON_VR_INPUT(dev);
    g_autoptr(GBytes) records = s->upload ?
        nikon_vr_upload_records(s->upload) : NULL;

    if (!s->mapped || s->pending_span || !records) {
        error_setg(errp, "VR persistence requires idle realized input and a "
                   "sealed actual record snapshot");
        return false;
    }
    if (s->upload_record_persistence) {
        error_setg(errp, "VR per-record profile persists each packet and final "
                   "barrier directly");
        return false;
    }
    if (!nikon_vr_store_start(&s->store, records, body, metadata, storage_base,
                              storage_size, token, vr_input_store_done, s,
                              errp)) {
        return false;
    }
    s->store_complete = complete;
    s->store_opaque = opaque;
    s->upload_job = VR_UPLOAD_SNAPSHOT;
    s->store_completed_token = 0;
    s->store_error = 0;
    return true;
}

static void vr_host_operation(Object *obj, const char *operation, Error **errp)
{
    NikonVRInput *s = NIKON_VR_INPUT(obj);
    bool accepted;

    if (!strcmp(operation, "upload-begin")) {
        if (s->length > UINT32_MAX) {
            error_setg(errp, "VR upload stream length exceeds 32-bit bounds");
            return;
        }
        nikon_vr_input_upload_begin(DEVICE(s), s->length,
                                   s->upload_origin, errp);
        return;
    } else if (!strcmp(operation, "upload-capture")) {
        nikon_vr_input_upload_capture(DEVICE(s), &address_space_memory, errp);
        return;
    } else if (!strcmp(operation, "upload-cancel")) {
        nikon_vr_input_upload_cancel(DEVICE(s));
        return;
    } else if (!strcmp(operation, "upload-persist")) {
        nikon_vr_input_upload_persist(DEVICE(s), s->upload_body_blk,
                                     s->upload_metadata_blk,
                                     s->upload_storage_base,
                                     s->upload_storage_size, s->token,
                                     NULL, NULL, errp);
        return;
    } else if (!strcmp(operation, "erase")) {
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
        error_setg(errp, "unknown VR host storage/staging operation");
        return;
    }
    if (!accepted) {
        s->result = NIKON_VR_FLASH_REJECTED;
    }
}

static bool vr_input_get_busy(Object *obj, Error **errp)
{
    NikonVRInput *s = NIKON_VR_INPUT(obj);

    return s->pending_span || s->store;
}

static bool vr_input_get_upload_sealed(Object *obj, Error **errp)
{
    NikonVRInput *s = NIKON_VR_INPUT(obj);

    return s->upload && nikon_vr_upload_sealed(s->upload);
}

static bool vr_input_get_upload_prepared(Object *obj, Error **errp)
{
    return NIKON_VR_INPUT(obj)->upload_prepared;
}

static bool vr_input_get_upload_durable_final(Object *obj, Error **errp)
{
    return NIKON_VR_INPUT(obj)->upload_durable_final;
}

static char *vr_input_get_upload_origin(Object *obj, Error **errp)
{
    NikonVRInput *s = NIKON_VR_INPUT(obj);

    return g_strdup(s->upload_origin ?: "");
}

static void vr_input_set_upload_origin(Object *obj, const char *origin,
                                       Error **errp)
{
    NikonVRInput *s = NIKON_VR_INPUT(obj);

    if (s->upload && nikon_vr_upload_received(s->upload)) {
        error_setg(errp, "VR staged input origin cannot change after receipt");
        return;
    }
    g_free(s->upload_origin);
    s->upload_origin = g_strdup(origin);
}

static void vr_input_get_upload_received(Object *obj, Visitor *v,
                                         const char *name, void *opaque,
                                         Error **errp)
{
    NikonVRInput *s = NIKON_VR_INPUT(obj);
    uint64_t received = s->upload ? nikon_vr_upload_received(s->upload) : 0;

    visit_type_uint64(v, name, &received, errp);
}

static void vr_input_get_persist_error(Object *obj, Visitor *v,
                                      const char *name, void *opaque,
                                      Error **errp)
{
    NikonVRInput *s = NIKON_VR_INPUT(obj);

    visit_type_int32(v, name, &s->store_error, errp);
}

static void vr_input_get_upload_status(Object *obj, Visitor *v,
                                      const char *name, void *opaque,
                                      Error **errp)
{
    uint8_t status;

    if (!nikon_vr_input_upload_status(DEVICE(obj), &status)) {
        error_setg(errp, "VR upload has no eligible durable response byte");
        return;
    }
    visit_type_uint8(v, name, &status, errp);
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
    nikon_vr_input_upload_cancel(dev);
    s->access_failed = s->reported_error = false;
    s->completed_token = 0;
    s->result = NIKON_VR_FLASH_OK;
    s->store_completed_token = 0;
    s->store_error = 0;
    s->upload_job_token = 0;
    /* Flash contents persist across reset; input snapshots remain immutable. */
}

static void vr_input_realize(DeviceState *dev, Error **errp)
{
    NikonVRInput *s = NIKON_VR_INPUT(dev);

    if (s->upload_body_blk || s->upload_metadata_blk ||
        s->upload_record_persistence) {
        if (!s->upload_body_blk || !s->upload_metadata_blk ||
            s->upload_body_blk == s->upload_metadata_blk) {
            error_setg(errp, "VR persistence needs distinct explicit body and "
                       "metadata drives");
            return;
        }
        if (blk_set_perm(s->upload_body_blk,
                         BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE,
                         BLK_PERM_CONSISTENT_READ, errp) ||
            blk_set_perm(s->upload_metadata_blk,
                         BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE,
                         BLK_PERM_CONSISTENT_READ, errp)) {
            return;
        }
    }
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
    vr_input_store_cancel(s);
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
    object_property_add_bool(obj, "upload-sealed", vr_input_get_upload_sealed,
                              NULL);
    object_property_add_bool(obj, "upload-prepared", vr_input_get_upload_prepared,
                              NULL);
    object_property_add_bool(obj, "upload-durable-final",
                              vr_input_get_upload_durable_final, NULL);
    object_property_add_str(obj, "upload-origin", vr_input_get_upload_origin,
                            vr_input_set_upload_origin);
    object_property_add(obj, "upload-received", "uint64",
                        vr_input_get_upload_received, NULL, NULL, NULL);
    object_property_add_str(obj, "operation", NULL, vr_host_operation);
    object_property_add_bool(obj, "access-failed", vr_input_get_access_failed,
                              NULL);
    object_property_add_bool(obj, "original-code", vr_input_get_original_code,
                              NULL);
    object_property_add_uint64_ptr(obj, "completed-token", &s->completed_token,
                                    OBJ_PROP_FLAG_READ);
    object_property_add_uint32_ptr(obj, "result", &s->result, OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(obj, "persist-completed-token",
                                    &s->store_completed_token,
                                    OBJ_PROP_FLAG_READ);
    object_property_add_uint32_ptr(obj, "upload-durable-received",
                                    &s->upload_durable_received,
                                    OBJ_PROP_FLAG_READ);
    object_property_add(obj, "upload-eligible-status", "uint8",
                        vr_input_get_upload_status, NULL, NULL, NULL);
    object_property_add(obj, "persist-error", "int32",
                        vr_input_get_persist_error, NULL, NULL, NULL);
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
    vr_input_store_cancel(s);
    nikon_vr_upload_free(s->upload);
    g_free(s->upload_origin);
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

static const Property vr_input_properties[] = {
    DEFINE_PROP_DRIVE("upload-body-drive", NikonVRInput, upload_body_blk),
    DEFINE_PROP_DRIVE("upload-metadata-drive", NikonVRInput, upload_metadata_blk),
    DEFINE_PROP_UINT32("upload-storage-base", NikonVRInput, upload_storage_base, 0),
    DEFINE_PROP_UINT32("upload-storage-size", NikonVRInput, upload_storage_size, 0),
    DEFINE_PROP_BOOL("upload-record-persistence", NikonVRInput,
                     upload_record_persistence, false),
};

static void vr_input_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = vr_input_realize;
    dc->unrealize = vr_input_unrealize;
    dc->vmsd = &vr_input_vmstate;
    dc->hotpluggable = false;
    device_class_set_props(dc, vr_input_properties);
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
