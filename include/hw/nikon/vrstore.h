/* Explicit native persistence for VR records. SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HW_NIKON_VRSTORE_H
#define HW_NIKON_VRSTORE_H

#include "qapi/error.h"
#include "qemu/typedefs.h"

typedef struct NikonVRStore NikonVRStore;
typedef void (*NikonVRStoreComplete)(void *opaque, uint64_t token, int result);

/* Both backends are explicit supplied inputs in the main AioContext. Body
 * addresses map to offsets relative to storage_base; metadata is a separate
 * exact 262-byte record in one explicitly supplied 512-byte backend sector.
 * The remaining supplied sector bytes are preserved. Writes and flushes on
 * both backends are
 * required before completion. This is a native storage policy, not FCU/ROM ABI.
 * The caller's slot must survive until completion or synchronous cancellation.
 */
bool nikon_vr_store_start(NikonVRStore **slot, GBytes *records,
                         BlockBackend *body, BlockBackend *metadata,
                         uint32_t storage_base, uint32_t storage_size,
                         uint64_t token, NikonVRStoreComplete complete,
                         void *opaque, Error **errp);
/* Per-record durable progress, and a flush-only prepare/final barrier. */
bool nikon_vr_store_record(NikonVRStore **slot, GBytes *record,
                          bool metadata_record, BlockBackend *body,
                          BlockBackend *metadata, uint32_t storage_base,
                          uint32_t storage_size, uint64_t token,
                          NikonVRStoreComplete complete, void *opaque,
                          Error **errp);
bool nikon_vr_store_flush(NikonVRStore **slot, BlockBackend *body,
                         BlockBackend *metadata, uint32_t storage_base,
                         uint32_t storage_size, uint64_t token,
                         NikonVRStoreComplete complete, void *opaque,
                         Error **errp);
/* Already issued writes can persist; cancellation does not promise rollback. */
bool nikon_vr_store_cancel(NikonVRStore **slot, uint64_t token);
uint64_t nikon_vr_store_token(const NikonVRStore *store);

#endif
