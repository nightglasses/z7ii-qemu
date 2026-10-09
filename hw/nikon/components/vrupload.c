/* Finite input stream consumer, not a replacement resident ROM service.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "hw/nikon/bundle.h"
#include "hw/nikon/vrupload.h"

enum {
    VR_RECORD_SIZE = 262,
    VR_RECORD_BODY = 256,
    VR_CONTROL_PACKET = 152,
    VR_UPLOAD_PACKET = 268,
};

struct NikonVRUpload {
    GByteArray *records;
    GBytes *sealed_records;
    uint32_t stream_size;
    uint32_t body_base;
    bool active;
};

NikonVRUpload *nikon_vr_upload_new(uint32_t stream_size, Error **errp)
{
    NikonVRUpload *upload;

    if (stream_size < 2 * VR_RECORD_SIZE || stream_size % VR_RECORD_SIZE) {
        error_setg(errp, "VR upload requires an exact complete record stream");
        return NULL;
    }
    upload = g_new0(NikonVRUpload, 1);
    upload->stream_size = stream_size;
    upload->records = g_byte_array_new();
    return upload;
}

void nikon_vr_upload_cancel(NikonVRUpload *upload)
{
    upload->active = false;
    upload->body_base = 0;
    g_byte_array_set_size(upload->records, 0);
    g_clear_pointer(&upload->sealed_records, g_bytes_unref);
}

void nikon_vr_upload_free(NikonVRUpload *upload)
{
    if (!upload) {
        return;
    }
    g_byte_array_unref(upload->records);
    g_clear_pointer(&upload->sealed_records, g_bytes_unref);
    g_free(upload);
}

static bool vr_upload_record(NikonVRUpload *upload, const uint8_t *record,
                             Error **errp)
{
    uint32_t count = upload->records->len / VR_RECORD_SIZE;
    uint32_t body_base = upload->body_base;
    uint64_t body_count = upload->stream_size / VR_RECORD_SIZE - 1;

    if (upload->records->len == upload->stream_size) {
        error_setg(errp, "VR upload exceeds its exact stream size");
        return false;
    }
    if (count && !memcmp(record, upload->records->data +
                         upload->records->len - VR_RECORD_SIZE,
                         VR_RECORD_SIZE)) {
        error_setg(errp, "VR upload repeats the last accepted record");
        return false;
    }
    if (!count) {
        if (memcmp(record, "\x04\x01\x04\x00\x00\x01", 6)) {
            error_setg(errp, "VR upload metadata record is invalid");
            return false;
        }
    } else {
        uint32_t address = ldl_le_p(record + 2);

        if (count == 1) {
            body_base = address;
        }
        if (record[0] != 4 || record[1] != 1 ||
            body_count * VR_RECORD_BODY > UINT64_C(0x100000000) - body_base ||
            address != body_base + (count - 1) * VR_RECORD_BODY) {
            error_setg(errp, "VR upload destination record is discontinuous "
                       "or exceeds the 32-bit address domain");
            return false;
        }
    }
    /* Commit only after the entire record has passed its bounds checks. */
    g_byte_array_append(upload->records, record, VR_RECORD_SIZE);
    upload->body_base = body_base;
    return true;
}

bool nikon_vr_upload_receive(NikonVRUpload *upload, GBytes *packet, Error **errp)
{
    size_t size;
    const uint8_t *data = g_bytes_get_data(packet, &size);

    if (size < 4 || data[1] ||
        (data[0] != 0xf5 && data[0] != 0xf7 &&
         data[0] != 0xf8 && data[0] != 0xfa) ||
        size != (data[0] == 0xf7 ? VR_UPLOAD_PACKET : VR_CONTROL_PACKET) ||
        lduw_le_p(data + 2) != (data[0] == 0xf7 ? 0x108 : 0x10)) {
        error_setg(errp, "VR upload packet has an unknown command or wire shape");
        return false;
    }
    if (data[0] == 0xf5) {
        nikon_vr_upload_cancel(upload);
        upload->active = true;
        return true;
    }
    if (data[0] == 0xf8 && (upload->active || upload->sealed_records)) {
        return true;
    }
    if (!upload->active || upload->sealed_records) {
        error_setg(errp, "VR upload needs an active unsealed stream");
        return false;
    }
    if (data[0] == 0xf7) {
        uint8_t sum = 0;
        unsigned i;

        for (i = 4; i < VR_UPLOAD_PACKET; i++) {
            sum += data[i];
        }
        if (data[266] || sum) {
            error_setg(errp, "VR upload payload checksum failed");
            return false;
        }
        return vr_upload_record(upload, data + 4, errp);
    }
    if (upload->records->len != upload->stream_size) {
        error_setg(errp, "VR upload cannot seal an incomplete stream");
        return false;
    }
    {
        g_autoptr(GBytes) records = g_bytes_new(upload->records->data,
                                               upload->records->len);
        g_autoptr(GBytes) metadata = NULL;
        g_autoptr(GBytes) body = nikon_vr_body(records, &metadata, errp);

        if (!body) {
            return false;
        }
        upload->sealed_records = g_steal_pointer(&records);
    }
    upload->active = false;
    return true;
}

uint32_t nikon_vr_upload_received(const NikonVRUpload *upload)
{
    return upload->records->len;
}

GBytes *nikon_vr_upload_last_record(NikonVRUpload *upload)
{
    if (upload->records->len < VR_RECORD_SIZE) {
        return NULL;
    }
    return g_bytes_new(upload->records->data +
                       upload->records->len - VR_RECORD_SIZE, VR_RECORD_SIZE);
}

bool nikon_vr_upload_sealed(const NikonVRUpload *upload)
{
    return upload->sealed_records != NULL;
}

GBytes *nikon_vr_upload_records(NikonVRUpload *upload)
{
    return upload->sealed_records ? g_bytes_ref(upload->sealed_records) : NULL;
}

GBytes *nikon_vr_upload_body(NikonVRUpload *upload, GBytes **metadata,
                           Error **errp)
{
    *metadata = NULL;
    if (!upload->sealed_records) {
        error_setg(errp, "VR upload has no sealed source snapshot");
        return NULL;
    }
    return nikon_vr_body(upload->sealed_records, metadata, errp);
}
