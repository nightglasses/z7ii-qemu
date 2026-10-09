/* Native block overlay producer for source-bounded VR records.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "qemu/main-loop.h"
#include "qemu/iov.h"
#include "system/block-backend.h"
#include "hw/nikon/bundle.h"
#include "hw/nikon/vrstore.h"

struct NikonVRStore {
    NikonVRStore **slot;
    BlockBackend *body_blk, *metadata_blk;
    GBytes *body, *metadata;
    struct iovec iov;
    QEMUIOVector qiov;
    BlockAIOCB *acb;
    QEMUBH *finish_bh;
    uint64_t token;
    uint32_t offset;
    unsigned phase;
    bool cancelled;
    int result;
    NikonVRStoreComplete complete;
    void *opaque;
};

static void vr_store_free(NikonVRStore *store)
{
    qemu_bh_delete(store->finish_bh);
    g_clear_pointer(&store->body, g_bytes_unref);
    g_clear_pointer(&store->metadata, g_bytes_unref);
    blk_unref(store->body_blk);
    blk_unref(store->metadata_blk);
    g_free(store);
}

static void vr_store_submit(NikonVRStore *store);

static void vr_store_finish(void *opaque)
{
    NikonVRStore *store = opaque;
    NikonVRStoreComplete complete = store->complete;
    void *consumer = store->opaque;
    uint64_t token = store->token;
    int result = store->result;

    /* BlockBackend retires its in-flight AIO after calling the I/O callback.
     * Release backend references and notify the consumer after that callback.
     */
    *store->slot = NULL;
    vr_store_free(store);
    if (complete) {
        complete(consumer, token, result);
    }
}

static void vr_store_done(void *opaque, int result)
{
    NikonVRStore *store = opaque;

    store->acb = NULL;
    if (store->cancelled) {
        return;
    }
    if (!result && ++store->phase < 4) {
        vr_store_submit(store);
        return;
    }
    store->result = result;
    qemu_bh_schedule(store->finish_bh);
}

static void vr_store_submit(NikonVRStore *store)
{
    BlockBackend *blk;
    GBytes *bytes;
    size_t size;

    while (store->phase < 2 &&
           !(store->phase ? store->body : store->metadata)) {
        store->phase++;
    }
    if (store->phase >= 2) {
        blk = store->phase == 2 ? store->metadata_blk : store->body_blk;
        store->acb = blk_aio_flush(blk, vr_store_done, store);
        return;
    }
    blk = store->phase ? store->body_blk : store->metadata_blk;
    bytes = store->phase ? store->body : store->metadata;
    store->iov.iov_base = (void *)g_bytes_get_data(bytes, &size);
    store->iov.iov_len = size;
    qemu_iovec_init_external(&store->qiov, &store->iov, 1);
    store->acb = blk_aio_pwritev(blk, store->phase ? store->offset : 0,
                                &store->qiov, 0, vr_store_done, store);
}

static bool vr_store_validate(NikonVRStore **slot, BlockBackend *body,
                              BlockBackend *metadata, uint32_t storage_base,
                              uint32_t storage_size, uint64_t token,
                              Error **errp)
{
    uint64_t perm, shared_perm;

    if (!slot || *slot || !token || !body || !metadata ||
        body == metadata || !storage_size ||
        storage_size > UINT64_C(0x100000000) - storage_base) {
        error_setg(errp, "VR persistence needs an idle job slot, nonzero token, "
                   "distinct backends and an explicit bounded guest mapping");
        return false;
    }
    if (blk_get_aio_context(body) != qemu_get_aio_context()) {
        error_setg(errp, "VR body backend is outside the main AioContext");
        return false;
    }
    if (blk_get_aio_context(metadata) != qemu_get_aio_context()) {
        error_setg(errp, "VR metadata backend is outside the main AioContext");
        return false;
    }
    if (!blk_is_available(body) || !blk_is_available(metadata)) {
        error_setg(errp, "VR persistence backend is unavailable: body=%d metadata=%d",
                   blk_is_available(body), blk_is_available(metadata));
        return false;
    }
    if (!blk_is_writable(body) || !blk_is_writable(metadata)) {
        error_setg(errp, "VR persistence backend is unwritable: body=%d metadata=%d",
                   blk_is_writable(body), blk_is_writable(metadata));
        return false;
    }
    if (blk_getlength(body) != storage_size) {
        error_setg(errp, "VR body backend size is %" PRId64 ", expected %" PRIu32,
                   blk_getlength(body), storage_size);
        return false;
    }
    /* BlockBackend lengths are sector units. The exact 262-byte source
     * record occupies one explicit sector; its supplied padding is preserved.
     */
    if (blk_getlength(metadata) != BDRV_SECTOR_SIZE) {
        error_setg(errp, "VR metadata backend size is %" PRId64 ", expected %d",
                   blk_getlength(metadata), BDRV_SECTOR_SIZE);
        return false;
    }
    blk_get_perm(body, &perm, &shared_perm);
    if (!(perm & BLK_PERM_WRITE)) {
        error_setg(errp, "VR body backend requires held write permission");
        return false;
    }
    blk_get_perm(metadata, &perm, &shared_perm);
    if (!(perm & BLK_PERM_WRITE)) {
        error_setg(errp, "VR metadata backend requires held write permission");
        return false;
    }
    return true;
}

static NikonVRStore *vr_store_new(NikonVRStore **slot, BlockBackend *body,
                                 BlockBackend *metadata, uint64_t token,
                                 NikonVRStoreComplete complete, void *opaque)
{
    NikonVRStore *store = g_new0(NikonVRStore, 1);

    store->slot = slot;
    store->body_blk = body;
    store->metadata_blk = metadata;
    blk_ref(body);
    blk_ref(metadata);
    store->token = token;
    store->complete = complete;
    store->opaque = opaque;
    store->finish_bh = qemu_bh_new(vr_store_finish, store);
    *slot = store;
    return store;
}

bool nikon_vr_store_start(NikonVRStore **slot, GBytes *records,
                         BlockBackend *body, BlockBackend *metadata,
                         uint32_t storage_base, uint32_t storage_size,
                         uint64_t token, NikonVRStoreComplete complete,
                         void *opaque, Error **errp)
{
    g_autoptr(GBytes) body_bytes = NULL;
    g_autoptr(GBytes) metadata_bytes = NULL;
    size_t body_size;
    const uint8_t *data;
    uint32_t destination;
    NikonVRStore *store;

    if (!vr_store_validate(slot, body, metadata, storage_base, storage_size,
                            token, errp)) {
        return false;
    }
    if (!records) {
        error_setg(errp, "VR persistence needs a real record snapshot");
        return false;
    }
    body_bytes = nikon_vr_body(records, &metadata_bytes, errp);
    if (!body_bytes) {
        return false;
    }
    data = g_bytes_get_data(records, NULL);
    destination = ldl_le_p(data + 262 + 2);
    body_size = g_bytes_get_size(body_bytes);
    if (destination < storage_base || body_size > storage_size ||
        destination - storage_base > storage_size - body_size) {
        error_setg(errp, "VR record destinations exceed the supplied native "
                   "body storage range");
        return false;
    }
    store = vr_store_new(slot, body, metadata, token, complete, opaque);
    store->body = g_steal_pointer(&body_bytes);
    store->metadata = g_steal_pointer(&metadata_bytes);
    store->offset = destination - storage_base;
    vr_store_submit(store);
    return true;
}

bool nikon_vr_store_record(NikonVRStore **slot, GBytes *record,
                          bool metadata_record, BlockBackend *body,
                          BlockBackend *metadata, uint32_t storage_base,
                          uint32_t storage_size, uint64_t token,
                          NikonVRStoreComplete complete, void *opaque,
                          Error **errp)
{
    const uint8_t *data;
    uint32_t destination = storage_base;
    NikonVRStore *store;

    if (!vr_store_validate(slot, body, metadata, storage_base, storage_size,
                            token, errp)) {
        return false;
    }
    if (!record || g_bytes_get_size(record) != 262) {
        error_setg(errp, "VR durable progress needs one exact source record");
        return false;
    }
    data = g_bytes_get_data(record, NULL);
    if ((metadata_record && memcmp(data, "\x04\x01\x04\x00\x00\x01", 6)) ||
        (!metadata_record && (data[0] != 4 || data[1] != 1))) {
        error_setg(errp, "VR durable record shape is invalid");
        return false;
    }
    if (!metadata_record) {
        destination = ldl_le_p(data + 2);
        if (destination < storage_base || storage_size < 256 ||
            destination - storage_base > storage_size - 256) {
            error_setg(errp, "VR durable record exceeds the supplied body range");
            return false;
        }
    }
    store = vr_store_new(slot, body, metadata, token, complete, opaque);
    if (metadata_record) {
        store->metadata = g_bytes_ref(record);
    } else {
        store->body = g_bytes_new_from_bytes(record, 6, 256);
        store->offset = destination - storage_base;
    }
    vr_store_submit(store);
    return true;
}

bool nikon_vr_store_flush(NikonVRStore **slot, BlockBackend *body,
                         BlockBackend *metadata, uint32_t storage_base,
                         uint32_t storage_size, uint64_t token,
                         NikonVRStoreComplete complete, void *opaque,
                         Error **errp)
{
    NikonVRStore *store;

    if (!vr_store_validate(slot, body, metadata, storage_base, storage_size,
                            token, errp)) {
        return false;
    }
    store = vr_store_new(slot, body, metadata, token, complete, opaque);
    vr_store_submit(store);
    return true;
}

bool nikon_vr_store_cancel(NikonVRStore **slot, uint64_t token)
{
    NikonVRStore *store = slot ? *slot : NULL;

    if (!store || store->token != token) {
        return false;
    }
    *slot = NULL;
    store->cancelled = true;
    if (store->acb) {
        blk_aio_cancel(store->acb);
    }
    vr_store_free(store);
    return true;
}

uint64_t nikon_vr_store_token(const NikonVRStore *store)
{
    return store->token;
}
