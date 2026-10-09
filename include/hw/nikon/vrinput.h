/* Explicit external inputs for the loaded VR domain. GPL-2.0-or-later */
#ifndef HW_NIKON_VRINPUT_H
#define HW_NIKON_VRINPUT_H

#include "hw/core/qdev.h"
#include "system/memory.h"
#include "hw/nikon/vrstore.h"

#define TYPE_NIKON_VR_INPUT "nikon-vr-input"
#define TYPE_NIKON_VR_SPAN "nikon-vr-span"
#define NIKON_VR_EXTERNAL_BASE UINT64_C(0xfffc8000)
#define NIKON_VR_EXTERNAL_SIZE UINT64_C(0x38000)
#define NIKON_VR_FLASH_BLOCK 8192
#define NIKON_VR_FLASH_PROGRAM_UNIT 128

typedef enum NikonVRInputQualification {
    NIKON_VR_DATA_PROFILE,
    NIKON_VR_ORIGINAL_CODE,
} NikonVRInputQualification;

typedef enum NikonVRFlashResult {
    NIKON_VR_FLASH_OK,
    NIKON_VR_FLASH_REJECTED,
} NikonVRFlashResult;

typedef struct NikonVRInputInfo {
    uint64_t guest_base, exact_size;
    const char *origin; /* Borrowed until provider destruction. */
    NikonVRInputQualification qualification;
    bool flash_block, modified;
} NikonVRInputInfo;

typedef struct NikonVRUploadProgress {
    uint32_t durable_received;
    bool prepared, sealed, durable_final, busy;
    int error;
} NikonVRUploadProgress;

typedef void (*NikonVRFlashComplete)(void *opaque, uint64_t token,
                                   NikonVRFlashResult result);

DeviceState *nikon_vr_input_create(Object *owner, MemoryRegion *memory,
                                  Error **errp);
/* Exact spans only. Source origin/qualification are explicit caller assertions,
 * not authentication or an instruction-fetch permission enforced by MemTxAttrs.
 * flash_block requires a complete 8 KiB snapshot at a source-observed block.
 */
bool nikon_vr_input_bind(DeviceState *dev, uint64_t guest_base, GBytes *bytes,
                         const char *origin,
                         NikonVRInputQualification qualification,
                         bool flash_block, Error **errp);
bool nikon_vr_input_load(DeviceState *dev, uint64_t guest_base,
                         const char *path, uint64_t exact_size,
                         const char *origin,
                         NikonVRInputQualification qualification,
                         bool flash_block, Error **errp);
bool nikon_vr_input_is_original_code(DeviceState *dev, uint64_t address,
                                    uint64_t size);
/* Query provenance for an exact covered range; copy/alias users must carry it. */
bool nikon_vr_input_describe(DeviceState *dev, uint64_t address, uint64_t size,
                            NikonVRInputInfo *info);
/* Immutable original snapshot, independent of the current mutable overlay. */
GBytes *nikon_vr_input_snapshot(DeviceState *dev, uint64_t guest_base);

/* A storage producer, not the unknown resident ABI or an FCU register model.
 * delay_ns is an explicit functional duration; completion never raises an IRQ
 * or supplies a peer byte. Program reads the actual guest source address space.
 * Reset/cancel abort an uncommitted request without a completion callback.
 */
bool nikon_vr_input_erase(DeviceState *dev, uint64_t token, uint64_t destination,
                         uint64_t delay_ns, NikonVRFlashComplete complete,
                         void *opaque, Error **errp);
bool nikon_vr_input_program(DeviceState *dev, AddressSpace *source,
                           uint64_t token, uint64_t destination,
                           uint64_t source_address, uint32_t length,
                           uint64_t delay_ns, NikonVRFlashComplete complete,
                           void *opaque, Error **errp);
bool nikon_vr_input_cancel(DeviceState *dev, uint64_t token);

/* Explicit functional record staging. Capture reads the actual consumed RX
 * pointer (11904), DMA0 remaining count, CS pad and guest mode from source.
 * It never substitutes for +0/+4/+8 or writes DMA/IRQ/SCI response state.
 */
bool nikon_vr_input_upload_begin(DeviceState *dev, uint32_t stream_size,
                               const char *origin, Error **errp);
bool nikon_vr_input_upload_capture(DeviceState *dev, AddressSpace *source,
                                 Error **errp);
/* Synchronous cancellation suppresses completion, with no rollback promise. */
void nikon_vr_input_upload_cancel(DeviceState *dev);
GBytes *nikon_vr_input_upload_snapshot(DeviceState *dev, const char **origin);
bool nikon_vr_input_upload_persist(DeviceState *dev, BlockBackend *body,
                                 BlockBackend *metadata, uint32_t storage_base,
                                 uint32_t storage_size, uint64_t token,
                                 NikonVRStoreComplete complete, void *opaque,
                                 Error **errp);
NikonVRUploadProgress nikon_vr_input_upload_progress(DeviceState *dev);
/* Eligible source-defined F8 byte-0 value, after actual durable operations.
 * False means no byte is available. This query does not transmit a SCI byte.
 */
bool nikon_vr_input_upload_status(DeviceState *dev, uint8_t *status);

#endif
