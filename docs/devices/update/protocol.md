# Functional SPI update peer

`hw/nikon/peers/update.c` implements the explicit `nikon-update-peer` profile.
It owns a NikonPeer frontend and one `nikon-vr-input` per-record storage input.
It is independent of `nikon-vr-peer`, the original SCI5 peripheral, MCU IRQs,
and the external resident slots at VR VA `fffc8000`. It never provides slot
bytes, hooks a JSR, writes TDR, rearms guest DMA, or claims original execution.

The software evidence is the complete MAS update family at
`103AA134..103AA464`, its input producer `103AA2C8`, callback
`103AA1DA..103AA2BC`, and physical CS/DMA transfer `103BC266..103BC2C8`.
The source emits F5/F8/FA control packets of 152 bytes and F7 packets of
268 bytes. F7 contains a 262-byte record and a zero/negative-payload-sum tail;
the four-byte header is excluded from that sum. The first record is metadata
with prefix `04 01 04 00 00 01`; subsequent records carry a little-endian
destination and 256 body bytes. Metadata is not a destination address.

The MAS callback only consumes response byte 0. After F5 and each F7, it polls
F8 for 2 before advancing its local chunk cursor. After FA it polls for 3.
The bound SPI source configuration uses 16-bit little-endian words and
LSB-first bytes. A word `00f7` therefore clocks F7 followed by 00. The endpoint
also accepts actual 8-bit clocks without changing the byte stream.

CS is an actual SELECT signal with line 0: low begins a packet, high ends it.
GPIO signals are not silently treated as CS. At CS-low the endpoint latches
current native status before receiving any command, because SPI is full
duplex. Status 1 means native preparation/I/O is incomplete or invalidated;
only `nikon_vr_input_upload_status()` can supply 2 or 3. The source original
low-CS ISR writes byte 0 equal to 1 at VR VA `fff17d59`, and the MAS F8
consumer continues polling for non-success values. Using 1 for this native
busy state is an explicit functional policy, not a reconstructed resident
implementation. The remaining unconsumed response bytes are explicit idle
ones (`ff`, or `ffff` per full word). This is a functional wire input
assumption, not asserted original response metadata or an acknowledgement.

The endpoint accumulates at most 268 actual clocked bytes. Only CS-high with
exactly 152 or 268 bytes submits a packet to the already qualified vrupload
parser and vrstore BlockBackend producer. CS-high itself never emits a reply.
F5 starts actual backend flushes. Each F7 is validated and actually writes
one complete metadata record or addressed body record, then flushes both
backends. Only successful completion exposes 2 to a later CS interval.
FA validates the complete sealed stream and flushes both backends before 3
is eligible. F8 only observes; it does not manufacture completion or mutate
storage. A latched status cannot change halfway through a packet.

Wrong command/length/checksum/record bounds invalidate progress. A rejected
F7 cannot reuse the previous record's eligible 2. Errors produce no success
status; this profile does not guess meanings for the source's FC/FD/FE failure
categories. A new valid F5 is required to restart. Cancel, reset, disconnect
and detach suppress pending completion and discard partial frames. Already
issued storage writes can persist; no rollback is promised. An inactive or
unsupported clock remains uncompleted until cancelled and is never replayed
as a clock in a later CS interval. Tokens retain the existing 64-bit peer
identity, and the shared transport retains its duplicate/in-flight checks.

No observer, new scheduler, timer, or second packet parser is introduced.
The standard QEMU main loop performs BlockBackend AIO. MAS's own F8 poll
transactions observe the resulting state. The exclusive input owner prevents
QMP begin/cancel or guest-RAM capture from replacing the native stream; readonly
progress remains available. Device reset keeps the attachment and requires
F5 again. Both the peer and input must outlive the endpoint; unrealize cancels
jobs, detaches the endpoint and releases its input ownership. The busless
device registers with the standard Resettable hierarchy and is unmigratable.

## Explicit construction

`include/hw/nikon/update.h` exposes `nikon_update_peer_create(owner, peer,
input, stream_size, origin, errp)`. The owner, peer, realized input, exact full
record-stream length, and source origin are mandatory. The input must have
`upload-record-persistence=true` and two explicit writable backends: a body
image of its exact declared storage size, and one 512-byte metadata sector.
Only the actual 262 metadata bytes are written; its 250 supplied padding
bytes are preserved. No media file is synthesized by the endpoint.

For native machine construction, `nikon_vr_input_create_store(owner, memory,
body_backend, metadata_backend, storage_base, storage_size, errp)` configures
these properties before realization and returns the realized input. It uses
the standard qdev drive properties and cleans up references on failure.
This matters because the existing external-span `nikon_vr_input_create()`
already returns a realized device and cannot be configured afterwards.
Both constructors require an explicit memory root; the storage constructor
can use a private root without adding any input region to the CPU's bus.

The equivalent QOM devices are:

```text
-device nikon-peer,id=wire,chardev=wire
-device nikon-vr-input,id=input,upload-body-drive=body,
        upload-metadata-drive=metadata,upload-storage-base=0xfff00000,
        upload-storage-size=819200,upload-record-persistence=true
-device nikon-update-peer,id=update,peer=/machine/peripheral/wire,
        input=/machine/peripheral/input,stream-size=838662,origin=CALLER_ORIGIN
```

The line breaks above describe device arguments, rather than a shell command.
The caller supplies the two drive definitions and chardev. For the original
3200 body records, the full stream is 3201 * 262 = 838662 bytes. Other finite
test streams explicitly state their size. Storage mapping is native policy;
it is not an FCU register implementation or permission to execute received
bytes. `origin` is a source assertion and does not authenticate the stream.
No input becomes ORIGINAL_CODE through receipt or persistence.

A machine hosting this profile must choose it explicitly instead of the
original SCI peer. A standalone RX service may keep its CPU stopped with
`-S`; it still requires the complete container selected by that machine's
normal `-bios` contract. This does not pretend the original RX boot crossed
the absent `fffc8008` input. A host can instead pass an explicitly private
memory root to the existing input factory when wiring a native service.
Machine/GUI wiring belongs to its owner; this module adds no automatic
machine fallback or shared-address alias.

## Qualification scope

One new native qtest sends real peer envelopes with tokens above 32 bits,
actual CS edges, and 8/16-bit byte streams. It checks complete F5/F7/F8/FA
progress, reads actual metadata/body files after durable records, validates
the preserved sector padding, rejects malformed F7 without an old 2, and
checks cancellation, partial-frame reset, disconnect and unchanged original
SCI/DMA registers. A subprocess rejects a second endpoint trying to own the
same input after acquiring a different peer frontend, exercising failed
realize cleanup. Ordinary QOM unrealize/re-realize also rejects attachment to
a peer with an incoming request, releases the input and state claims, and
allows attachment after the original request is cancelled. The exact same
QOM profile and public APIs serve production
construction; there is no test-only packet command. Only synthetic bytes are
used. The sole builder compiled/linked the atomic frontend and executed this
focused case with exit0 in1.23s; its ignored receipt is
`run vrupdatenativereceipt.log`. The previous five input, nine peripheral,
and three storage/staging cases were not rerun by this group.
