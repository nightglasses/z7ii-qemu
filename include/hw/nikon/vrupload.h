/* Source-bounded VR upload records. SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HW_NIKON_VRUPLOAD_H
#define HW_NIKON_VRUPLOAD_H

#include "qapi/error.h"

typedef struct NikonVRUpload NikonVRUpload;

/* A complete record stream supplied by the actual MAS component producer.
 * This stores received bytes; it does not implement the unknown resident ABI,
 * erase code flash, produce a SCI response, or qualify staged bytes as code.
 */
NikonVRUpload *nikon_vr_upload_new(uint32_t stream_size, Error **errp);
void nikon_vr_upload_free(NikonVRUpload *upload);
void nikon_vr_upload_cancel(NikonVRUpload *upload);

/* F5 begins a stream, F7 stores one complete 262-byte record, F8 observes it,
 * and FA seals a complete stream. Exact wire lengths are 152/268 bytes. No
 * padding bytes are invented or checked as protocol fields. Rejected frames
 * do not mutate the stream; repeated accepted records are rejected explicitly.
 */
bool nikon_vr_upload_receive(NikonVRUpload *upload, GBytes *packet, Error **errp);
uint32_t nikon_vr_upload_received(const NikonVRUpload *upload);
GBytes *nikon_vr_upload_last_record(NikonVRUpload *upload);
bool nikon_vr_upload_sealed(const NikonVRUpload *upload);
/* A sealed immutable source snapshot, with its metadata retained separately.
 * DATA qualification only; callers must independently decide storage policy.
 */
GBytes *nikon_vr_upload_records(NikonVRUpload *upload);
GBytes *nikon_vr_upload_body(NikonVRUpload *upload, GBytes **metadata,
                           Error **errp);

#endif
