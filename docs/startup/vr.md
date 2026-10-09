# VR RX peripheral protocol

The `nikonvr` CPU profile uses existing RX TCG and EXTB. Its chip part number
has not been proved. Register names and bit positions are checked against the
[official Renesas RX Driver Package](https://github.com/renesas/rx-driver-package/blob/master/source/r_bsp/r_bsp_vx.xx/r_bsp/mcu/rx64m/register_access/ccrx/iodefine.h),
retrieved on 2026-10-09. That reference is a register protocol reference; it is
not evidence that Nikon used an RX64M. SYSTEM and RTC are separate devices.

The observed RSPI1 address `d0140`, three-bit ADADC and sampling registers at
`890e0` are additionally checked against the
[official RX65N register header](https://github.com/renesas/rx-driver-package/blob/master/source/r_bsp/r_bsp_vx.xx/r_bsp/mcu/rx65n/register_access/ccrx/iodefine.h),
retrieved on 2026-10-09. This matches peripheral capabilities and still does
not identify the Nikon part. MTU3a extended clock selection is described in
tables 24.7–24.9 of the [Renesas RX65N/RX651 hardware manual](https://www.renesas.com/en/document/mah/rx65n-group-rx651-group-users-manual-hardware).

The original bootstrap at `fff001f8` initializes PORT/MPC and at `fff005bd`
initializes CMT, MTU, SCI1/5, eight DMA channels, DTC and ADC. The final ICU
routine at `fff009a4` programs priorities, masks and selectable interrupt
sources. The model maps these finite register groups individually and rejects
unknown offsets, widths and unsupported phases through `MEMTX_ERROR`.

PORT stores direction, output, peripheral selection, open drain, pull and drive
configuration. PIDR is produced by GPIO output or a real external pad, with a
named low input assumption when nothing drives the pad. PCR can pull an
unconnected pad high. External producers use the `pad` input GPIO list; pad
levels are exposed on the separate `pad-out` output list, as required by QEMU's
named GPIO contract. MPC PWPR protects pin function writes. P47 configured by
the guest as IRQ15 supplies the original SCI5 select interrupt; P17 supplies
the application's IRQ7 input. Both follow
the real select line, the selected edge/level mode, IR, IER and IPR. ICUb stores
256 priorities and group masks. Upstream ICUa cannot represent this layout:
its IPR storage has only 142 entries and four DMA selectors. CMT reuses the
existing upstream Renesas timer implementation and drives vectors 28/29.

The machine supplies an explicitly named **functional 60 MHz peripheral
clock**. This is not a measured Nikon hardware frequency. CMT, MTU/TPU internal
clocks and master SCI/RSPI transfer timing use this input. SCI5 uses external
SSI clock frames. SCI10's synchronous master clocks use SMR, BRR and CKE;
its transmit timer cannot create RDRF when its SSI bus has no attached peer.
Writing configuration or enabling DMA cannot create a received SCI byte.

The original MAS SPI4 traffic is consumed by VR **SCI5**, not by RSPI0. Direct
observations in the application are SCI5 at `8a0a0`, SMR `80`, external-clock
SCR CKE `2`, receiver DMA0 from `8a0a5`, sender DMA1 to `8a0a3`, and DMRSR0/1
values 84/85. `nikon_vr_peripherals_attach_spi()` attaches a normal upstream
SSI slave to the MAS SPI bus. Its chip-select input drives P47. Each actual
clock frame returns a byte previously written by the guest to TDR and offers
the incoming byte to RDR. RDRF/overrun, TX empty/end, group requests and DMA
activation result from those transfers. The model does not parse opcode 18,
262-byte records or manufacture byte 2 acknowledgements. The guest owns
consumption, record validation and reply generation.

The normal DMA mode implements original `DMTMD=2001/2101`, source/destination
increment or decrement/fixed modes, each actual address-space read/write,
remaining count, disable-on-completion, DTIF/ESIF and interrupt enables. An
empty enabled TDR produces its genuine initial request; subsequent requests
follow external SCI clocks or completed RSPI master frames. Failed memory
transactions preserve the remaining count and produce an error instead of a
successful beat.

DTC activation requires DTCST, the matching DTCER and IER, and an actual
peripheral request. It reads the vector pointer and full-address descriptor
from guest RAM and performs each actual address-space transaction. Normal
and repeat modes implement byte/word/longword size, address updates, repeat
area reload, chain activation, DISEL and WBDIS. WBDIS preserves the guest
descriptor and DTCER rather than silently completing it. All peripheral
requests use a QEMU bottom half after their register callback returns, so
DMA/DTC never reenters the MMIO region which raised the request. Repeated
timer activations retain their count; ordinary ready requests coalesce while
pending. Nested requests from MMIO writes are processed after the current
DMA/DTC transaction returns.
Unsupported descriptor phases or failed bus transactions report errors.

The complete original extracted body contains the DTC vector table at body
offset `c7c00`, copied by the guest to `39c00`. Vectors 108, 109 and 212 point
to guest descriptors `160fc`, `1611c` and `1613c`. The first two implement
sixteen longword repeat transfers between RSPI2 SPDR and guest buffers,
followed by a chained byte write to SPCR; the last performs a byte SPCR write
with WBDIS. These values are evidence for the implemented protocol and are
not embedded firmware rules in the device. The bit semantics are checked
against the [Renesas DTC migration guide](https://www.renesas.com/en/document/apn/rx65n-h8sx1668-dtc-migration-guide-h8sx1668-rx65n-rev100),
R01AN6775EJ0100 revision 1.00, 2023-03-27.

RSPI0/1/2 have independent upstream SSI buses and transfer timers, sequence
commands, TX/RX holding registers, status and DMA/DTC events. SPDCR determines
the one-to-four-frame buffer size. The original RSPI2 SPDCR `23` waits for
four longword frames, clocks every frame and exposes the completed receive
buffer to the guest's DTC transfers. With no peripheral
attached, upstream SSI returns zero: this is an explicit functional MISO idle
wire, not a sensor value or a VR acknowledgement. Other unconnected serial
inputs do not receive unsolicited bytes.

MTU1/2/3/4/6/7 and TPU0–5 own running counters, compare registers, status flags,
counter clear and overflow events. TSTRA/TSTRB/TPUA.TSTR start and stop their
respective channels. Selectable A/B events use the guest's actual ICU selector
values and write protection; selector 9 is MTU1B. MTU TCR2=1 selects clock/2.
External clock modes advance only on the named `timer-clock` GPIO input, in
channel order MTU1,2,3,4,6,7,TPU0,1,2,3,4,5. Original TPU5 TPSC=7 consequently
does not advance while its external clock wire is disconnected.

The three observed ADC channels accept real 12-bit `analog` GPIO input values;
negative input disconnects a channel. No conversion completes until every
selected channel is connected. ADST starts a conversion, ADADS selects value
addition, and original ADADC=5 samples sixteen times. Result registers are
read-only and unavailable until produced. ADIE produces selected B event 64;
continuous scan repeats actual sampling. Each sampling step uses an explicitly
named **functional 1 microsecond settling interval**, not a silicon timing
measurement. No analog endpoint is attached by this component profile.

The native fixture `tests/qtest/nikon/peripherals.c` checks MPC locking,
GPIO/mux readback, actual IRQ15 edges and acknowledgement, mature CMT start/
stop, RSPI-clock-driven transmit/receive DMA counts and failed bus reads,
MTU compare/stop events, selector protection, external TPU clock edges and ADC
input-dependent sixteen-sample completion.
Additional cases check a repeat DTC chain and WBDIS writeback suppression,
SCI10 transmit DMA with an absent receive peer, and RSPI2 four-frame buffering.
It contains a synthetic component container and no Nikon executable code.
It is separate from the acceptance run using the complete original decoded
container. Passing a synthetic register fixture does not establish an
original guest-produced MAS/VR reply.

The complete application was scanned for peripheral base loads and their
effective accesses, together with the bootstrap. The resulting consumer
groups are implemented as separate regions:

| Consumer | Register window | Source evidence |
| --- | --- | --- |
| PORT/MPC | `8c000–8c1ff` | Bootstrap `fff001f8`; SCI1 P17 and SCI5 P47 mux/input consumers |
| ICUb | `87000–87a01` | Bootstrap `fff009a4`; IRQ7/15, groups BL0/AL0, DMA selectors and selectable A/B vectors |
| DMA0–7 | `82000–82204` | SCI5 RX/TX 84/85, SCI1 RX 60, SCI10 RX/TX 104/105 and RSPI requests |
| DTC | `82400–8240f` | Application `00016a69`; original RAM descriptors for vectors 108/109/212 |
| SCI1 / SCI5 | `8a020–8a02d` / `8a0a0–8a0ad` | Application `0002f34b` / original MAS transport |
| SCI10 | `d0040–d005f` | Application `00017373`; synchronous master, DMA6/7, FIFO reset command |
| CMT | `88000–8800f` | Bootstrap CMT initialization; upstream timer vectors 28/29 |
| TPU0–5 | `88100–8816f` | Application `0002f0ba` TPU5 external clock and effective TCNT `88166` |
| MTU1/2/3/4/6/7 | `c1200–c12ff`, `c1380–c139f`, `c1400–c141f`, `c1a00–c1aff` | Bootstrap counter setup; application compare consumers and MTU4 selected vector 212 |
| ADC channels 0–2 | `89000–890e2` | Bootstrap sixteen-sample addition; application result consumers |
| RSPI0 / 1 / 2 | `d0100–d0120`, `d0140–d0160`, `d0300–d0320` | Application SPI initialization and RSPI2 DTC sensor transport |

Each window admits only named registers with their actual access width; a
window is not a claim that every offset is implemented. SYSTEM/RTC consumers
are owned by the separate Renesas devices, including keyed software reset at
`800c2`. Application loads of `80000` in the arithmetic helper near `00020818`
are numeric constants, not register accesses.

## Remote SCI5 endpoint

`nikon-vr-peer` is a thin native frontend with `peer` and `peripherals` QOM
links. The common `nikon-peer` CharBackend carries CLOCK, SELECT and GPIO
signals; this adapter contains no firmware packet parser. SELECT line 0 is
the physical P47/IRQ15 level. GPIO line ids are RX `port * 8 + bit`; machine
wiring must map those ids to the corresponding physical peer pads. Only PDR
outputs with PMR selecting GPIO are published. Output levels rejected by
transport backpressure remain unpublished and retry on transport readiness;
the next publication uses the current physical level.

The public `nikon_vr_peripherals_clock()` accepts one eight-bit token and
retains it while the guest is unarmed. The existing QEMU bottom half waits for
actual active select, synchronous SCI5 CKE=2, TE/RE, an enabled RX DMA request
84, the source's non-inverted LSB-first SCMR format, a clear receive holding
register and a byte actually present in TDR.
Register writes and DMA completion wake that bottom half. It clocks the
existing SCI frame exactly once, drains the actual RX/TX DMA requests, and
only then invokes the completion callback with the captured guest TDR byte.
CS edges, empty TDR and unarmed DMA cannot create replies. Transport reply
backpressure retains the captured byte and retries sending, without another
SCI clock or DMA beat.

The adapter accepts source-required eight- and sixteen-bit wire beats. MAS
constructor `103BC13C` supplies configuration byte `+1=0`; native `103706C4`
passes it to `10243170`, which clears CTRL bit 2 (`MSB1ST`). MAS uses
little-endian halfword DMA, so a sixteen-bit beat clocks the low byte first.
VR bootstrap `fff00755` writes SMR `80`, and `fff00758` writes SCMR `72`.
SCMR SDIR=0 means LSB-first transmission/reception in the
[Renesas SCI migration guide](https://www.renesas.com/en/document/apn/h8h8s-rx-mcus-h8s-rx-migration-guide-serial-communications-interface-rev100),
R01AN4046EJ0200 revision 2.00, page 15. Both ends therefore agree without
bit reversal. A sixteen-bit request performs two independently armed SCI5
frames, low then high, and replies with `low | (high << 8)`. Cancellation
between them retains the first byte's actual RX/TX DMA effects and cancels
the remaining clock; no first byte is replayed.

The extracted original application programs a 276-byte hardware window:
`000176b0` writes DMA1 DMCRA at `82048`; the high-select ISR restores DMA1
count 276 at `00017dce`; it restores DMA0 count 276 at `00017e05` only on
the ordinary path, while update mode 2 delegates that rearm to the resident
callback. The low-select ISR
at `00017d6a–00017da7` supplies the guest TX buffer and enables SCR `f2`.
These counts can cover the MAS 268-byte transfer, but do not prove an original
completed packet. In particular, a final sixteen-bit beat still needs two
actual TDR bytes; an empty second frame waits instead of receiving padding.

Chip select is independently guest-owned: MAS GPIO resource `ae` is port 11
pin 1, set low at `103BC296` and high at `103BC2BA` around `10370A14`.
It must drive SELECT line 0 through machine wiring; SPI enable is not a
substitute for that physical wire.

`nikon_vr_peripherals_cancel()` removes the matching pending callback. Effects
of a frame already clocked are retained. Adapter reset, incoming CANCEL,
disconnect and detach cancel the shared incoming token without sending a
reply. The normal SPI select input starts inactive high, as in the local SSI
slave; disconnect does not fabricate a new select edge.
The busless frontend explicitly registers its Resettable object with QEMU's
system reset hierarchy and unregisters it on unrealize. Merely defining a
device reset hook does not attach a busless device to that hierarchy.

The immutable dependency is the actual native8 PASS snapshot
`f8078bf944ef0867736223365a2ece21ba111735`; the endpoint uses shared peer API
`9398300` on `3439c35`/`964a354`. A ninth native case uses the standard qtest
socket helper and the unchanged transport envelope. It checks GPIO levels,
real select IRQ, unarmed and inactive-select waits, cancel/reset/disconnect,
guest-written TDR bytes `a5`, `5a`, `77`, and received bytes reaching RAM via
DMA. It also verifies sixteen-bit low/high ordering and cancellation while
the second byte waits for real TDR refill. Qtest manually programs the
synthetic guest registers and does not
execute Nikon's ISR. Sole-builder qualification passed the focused ninth case
after the busless reset registration repair (`8819090`), in 0.83 seconds with
exit 0; the preceding eight cases passed in the original batch. The receipt is
`work/vrpeerresetreceipt.log` in the canonical `qemu-submodule` worktree.

The complete original image remains strictly stopped at bootstrap
`fff00167`, reading the four-byte external function-pointer slot `fffc8008`.
Bootstrap performs `JSR` through that value at `fff00169` before entering
application `00018583`. This endpoint supplies neither that data slot nor
target instructions, and its synthetic fixture is not an original VR ACK
receipt.

## Explicit external input and flash storage

`nikon-vr-input` now supplies a sparse container for the loaded VR domain's
external interval `[fffc8000,100000000)`. `nikon-vr-span` loads one exact file
span with an explicit `input` link, `guest-base`, `exact-size`, `origin`, and
`qualification=data|original-code`. The C API accepts the same exact `GBytes`
without a file. Overlap, missing origin, unsupported qualification, empty input,
and 32-bit overflow are errors. No table pointers, signatures, padding bytes,
resident instructions, or default erased snapshot are generated. Known spans
use standard QEMU ROM devices and exact aliases; holes and direct guest stores
return `MEMTX_ERROR`.

Origin and original-code qualification are caller declarations of provenance,
not cryptographic authentication. `nikon_vr_input_is_original_code()` queries
that declaration over an exact supplied range. QEMU's generic `MemTxAttrs`
does not identify instruction fetches: this metadata is not a TCG execute
permission. Copy/alias consumers must carry the declaration explicitly.

`flash-block=true` separately authorizes a mutable overlay only for a complete
8192-byte snapshot at the five source descriptor addresses `ffff0000`,
`ffff2000`, `ffff6000`, `ffff8000`, `ffffa000`, or the independently consumed
header block `ffffc000`. Smaller exact immutable spans remain possible, including
genuine header bytes; a signature alone is insufficient to initialize a mutable
block. Bootstrap's configuration readers probe tails at `ffff60fc`, `ffff80fc`,
and `ffffaafc`, then conditionally copy entire `100`, `100`, and `b00` byte
records. Application `0001768f` separately reads `ffffc004–ffffc007`.

The storage producer accepts a supplied block erase or aligned 128-byte-unit
program request, captures actual source-bus bytes, rejects an attempt to set an
unerased bit, and commits only at the caller's finite positive virtual duration.
These block/program units are an explicit functional capability matching the
descriptors and [Renesas's fixed public RX65N SDK header](https://github.com/renesas/rx-driver-package/blob/bcc409d331b0bff68d4b09bf6fc41376f75d42bc/source/r_flash_rx/r_flash_rx_vx.xx/r_flash_rx/src/targets/rx65n/r_flash_rx65n.h),
not silicon identification or measured timing. Missing blocks are not created.
Cancel/reset abort uncommitted requests without completion; committed bytes
persist across reset. The original `GBytes` snapshot stays unchanged, while a
modified span ceases to qualify as original code. ROM-device flushes invalidate
translated code after mutations. Sparse source metadata and pending operations
are explicitly unmigratable.

This is a storage producer API and an explicit host QOM storage interface; it
does not supply the unknown resident ABI, FCU registers, IRQ, or peer bytes.
The new native cases use only synthetic whole-page data and cover exact holes,
qualifications, bounds/overlap rejection, real erase/program readback, reset,
cancel, source-bus errors, and preservation of the original snapshot. All five
passed focused qualification: the sole builder compiled/linked
production input source `1706171` and qualified all five focused cases after
fixture corrections `9401ec3`/`b6be0d8`. External-span, flash-overlay,
flash-cancel-bounds and external-conflict exited 0 in 0.52/0.66/0.52/0.60
seconds (`work/vrinputscalar0receipt.log` through
`work/vrinputscalar3receipt.log`); external-rejection exited 0 in 1.44 seconds
(`work/vrinputrejectionreceipt.log`). These receipts belong to the canonical
`qemu-submodule` worktree and qualified source snapshot `615a8a94d05dd44986b2fab28a66f3dc1ab7231c`.
The existing nine peripheral cases were not rerun or changed by this group.

## Bounded update record staging

The source-confirmed upload path is MAS `1039CCDC` → `10393E36` →
`103AA2C8`, storing the total stream length, source pointer and local cursor
in `B23C4780`. `103AA3C4` copies at most hexadecimal `106` (262 decimal)
bytes from source plus cursor into a 268-byte F7 packet: `f7 00 08 01`,
payload, `00`, and the negative payload byte sum modulo 256. Header bytes
are excluded from this sum. The cursor is local state, not a transmitted
flash offset. Partial final chunks leave unspecified tail bytes in this
producer; this staging profile accepts only complete component streams,
whose lengths are multiples of 262, and invents no partial padding.
The 152-byte F5/F8/FA control packets carry `00 10 00` after their command.
Their unused trailing bytes are not interpreted as fields.

The receiver owns two distinct rings. Application `000175d1` uses base
`10e19`; DMA1 DAR is SCI5 TDR `8a0a3`, its initial count is 276, and its
TX buffers are `15654` and `15768` with current pointer at `118fc`.
DMA0 SAR is SCI5 RDR `8a0a5`; its RX buffers are `1587c` and `15990`,
with current receive pointer at `11900`. Parser `000177c3` saves the consumed
RX pointer at `11904` and advances `11900` to the other ring entry.
At `00017cc9`, R6 is `10e1a`: byte `317[R6]` is last command `10f57`.
F5 instead sets byte `315[R6]`, update mode `10f55`, to 1. The main loop
observes it at `00017e90`, quiesces its ordinary work, sets mode 2 at
`00017e9e`, and calls resident slot +0. On high CS, IRQ15 computes
`15654 + 276 * RAM[10f53]`, stores it at `118fc`, and calls resident slot
+4 with R1=2 and R2=this **TX** buffer. The incoming frame is separately
available through `11904`; R2 is not an incoming-record argument.

`vrupload.c` now stores an exact stream of received 262-byte records, preserving
the first metadata record `04 01 04 00 00 01` and the following `04 01` /
little-endian destination / 256-byte body records. It checks packet shape,
the actual checksum, bounded contiguous addresses, and exact total size
before sealing an immutable snapshot. It reuses `nikon_vr_body()` to validate
the complete snapshot and separate metadata from body. Failed frames do not
advance receipt; duplicate destination records are explicitly rejected rather
than silently interpreted as progress. F8 observes staging without producing
a status byte. FA seals only a complete stream. These are DATA input snapshots,
not original-code qualification or a claim about resident flash operations.

The concrete functional entry is `nikon_vr_input_upload_begin()` with exact
stream length and declared origin, then `nikon_vr_input_upload_capture()` on
the VR AddressSpace. Capture reads actual guest mode `10f55`, CS P47 PIDR
`8c044`, consumed RX pointer `11904`, and DMA0 remaining count `82008`.
It requires high CS, the consumed ring bounds, and an actual 152/268-byte
window. F5 may be captured in mode 1; data/status/finish require mode 2.
The explicit QOM operations are `upload-begin`, `upload-capture`, and
`upload-cancel`, with `length` and `upload-origin`, plus readonly
`upload-received`/`upload-sealed`. These provide a usable host functional
profile and an API for a future source-qualified resident consumer; they do
not intercept a JSR, modify pointers, rearm DMA, or change IRQ/SCI output.
`nikon_vr_input_upload_snapshot()` exposes the sealed bytes and origin to
the actual persistence producer. Reset/cancel discard staging, while an
already returned immutable snapshot remains owned by its caller.

MAS callback `103AA1DA` confirms why receipt alone cannot acknowledge an
upload: after F7 it requests F8; F8 byte 0 equal to 2 advances the local
cursor, FF requests a resend, FC/FD/FE fail, and other values poll again.
After FA, F8 byte 0 equal to 3 finishes successfully. No such byte is
generated by staging. Flash erase ordering, interpretation of the metadata
body, and resident +0/+4/+8 effects remain independent source requirements.
The focused synthetic staging fixture manually supplies mode, RX bytes and
DMA count; it verifies strict capture, address/checksum rejection, sealing
and reset/cancel. It does not execute the original ISR or establish an
original firmware upload receipt.

## Explicit native persistence profile

The addressed source record sequence starts at `fff00000`, advances by 256
bytes per body record, and contains 3200 body records (819200 bytes) for the
supplied original component. Thus its exclusive body end is `fffc8000`.
The first 262-byte metadata record is separate; bytes `04 00 00 01` in its
prefix are not a program destination. This is the concrete range used by the
native persistence profile. A caller must supply the entire backing file and
declare its guest base/size explicitly; neither missing data nor an initial
erased image is generated. Synthetic tests use the same bounded range with
explicit synthetic bytes and a shorter record stream.

`vrstore.c` now consumes the actual sealed records via `nikon_vr_body()` and
standard BlockBackend AIO. It retains the complete metadata on a separate
explicit 512-byte backend sector (262 source bytes plus preserved supplied
padding) and maps contiguous body bytes at the offset computed
from the parsed record destination minus the caller's storage guest base.
It checks distinct writable available backends, exact backing lengths,
32-bit mapping bounds, and the full destination range before issuing either
write. It requires the main AioContext. The sequence is metadata write, body
write, metadata flush, body flush; only four successful operations produce a
durable completion. The final notification runs in a BH after the BlockBackend
callback retires, so backend references and buffers outlive all in-flight I/O.
The snapshot, metadata, body, and backend references remain owned by the job
until completion or synchronous cancellation.

The input provider's concrete public entry is
`nikon_vr_input_upload_persist()`. Standalone QOM uses explicit
`upload-body-drive` and `upload-metadata-drive` block links plus
`upload-storage-base`/`upload-storage-size`; `upload-persist` submits the
sealed source with the existing nonzero `token` property. `busy` remains true
through both flushes and the completion BH. `persist-completed-token` and
`persist-error` identify the finished native operation, with result zero only
meaning success for the matching completed token. A missing backend, unsealed
source, read-only backend, busy operation, or invalid destination is an error.
Reset, unrealize and `upload-cancel` synchronously cancel the active job and
suppress notification; already issued writes can remain and are not rolled
back. Sealed GBytes returned to another consumer remain independently owned.

This is an explicit native block overlay policy. It does not implement FCU
erase/program sequencing, upgrade DATA inputs to original instructions,
replace the loaded guest's ROM mapping, or populate its resident slots.
It is separate from the finite 8 KiB/128-byte flash-operation capability.
The standard backend may be a caller-configured raw image or QEMU overlay;
the device does not invent a disk format or migrate supplied snapshots.

The complete MAS callback family `103AA1DA..103AA2BC` reads only response
byte 0 (`103AA1E2`), including stages 17/18/19/1A. It does not consume
response bytes 1..151 as addresses or flash metadata. F5 completion requests
F8; F7 completion in state 3 requests F8/state 2. F8/state 2 accepts status value `02`
as chunk completion, advances the local cursor by the saved chunk size, and
requests the next F7 or FA. FF resends F7, FC/FD/FE terminate with failure,
and other values poll. FA completion requests F8/state 6; status value `03` then finishes
successfully. The callback's guards at record offsets `14`/`18` are visible,
but their external increment mechanism has not been joined, so no retry count
is asserted. Native storage completion is an eligible persistence result for
a separately chosen functional service; this provider does not emit either
status byte or write the selected TX buffer, rearm RX DMA, or return through
an unknown resident callee. Real SCI replies still require an actual producer
to supply bytes through the source-confirmed serial path.

The focused synthetic persistence case submits a short addressed stream to
an explicitly supplied 819200-byte body image and 512-byte metadata sector,
waits for real native I/O with a bounded host deadline, verifies both files
after shutdown, opens a new instance, and rejects a stream crossing the body
boundary before either write. Untouched body bytes and full metadata are
checked. The sole builder compiled/linked production `155e36b` and passed
only this focused case in 1.25 seconds with exit 0; its receipt is
`work/vrstoresectorreceipt.log` in the canonical `qemu-submodule` worktree.
Staging independently passed with fixture `0d40bc3` in 0.44 seconds with
exit 0 (`work/vruploadstagingdeclaredreceipt.log`). The already qualified
external-input five and peripheral nine were not rerun.

QEMU's `block.c:bdrv_co_getlength()` returns sector-rounded lengths, always
multiples of `BDRV_SECTOR_SIZE`. The metadata backend therefore contains one
explicit 512-byte sector, rather than pretending the block length can be 262.
Only the actual 262-byte record is written; the remaining 250 supplied bytes
are preserved and tested. No padding bytes are taken as firmware metadata.

The explicit `upload-record-persistence=true` profile connects capture to
per-record native storage. It requires the same two writable exact backends
and explicit guest mapping. This is a functional storage policy, rather than
a claim about the unknown resident ROM's flash programming order. The MAS
source at `103AA1DA..103AA2BC` polls F8 after F5 and after every F7, and advances
the local cursor only on response byte 0 equal to 2. Waiting until the whole
stream is sealed before persisting would deadlock that producer. After FA,
the same source polls F8 until byte 0 equals 3. No other response byte carries
a destination or cursor in this consumer; destinations come from the actual
addressed 262-byte records.

F5 starts a real flush of both supplied backends and marks native preparation
only on success. Each F7 first passes the complete wire checksum and record
validation. The first record writes all 262 metadata bytes; each subsequent
record writes its 256 body bytes at its own little-endian address within the
explicit mapping. Both backends are then flushed. The durable byte count
advances only after those actual operations succeed. F8 can be captured while
I/O is pending and leaves the job unchanged. FA requires all preceding
records to be durable, validates the entire sealed snapshot, and starts a
final flush of both backends. F8 remains valid after sealing. An I/O failure,
out-of-range record, pending job, or unprepared stage cannot supply an eligible
status value. A rejected F7 invalidates preparation until a new F5; the
previous record's eligible 2 cannot acknowledge MAS's current chunk. Cancel
and reset suppress pending completion, clear progress,
and preserve any writes already issued; they do not promise rollback.

`nikon_vr_input_upload_cancel()` exposes the same synchronous stage/job
cancellation used by QOM and device reset. A native consumer can use the
begin/capture/progress/status/cancel API without relying on QMP commands.
`nikon_vr_input_upload_progress()` reports those native facts. The read-only
`upload-eligible-status` property and `nikon_vr_input_upload_status()` make 2
available after preparation or a durable record and 3 after the final durable
barrier. A query with no eligible byte fails instead of returning a made-up
zero. These queries never transmit a byte, write the selected TX ring, rearm
DMA, change SCI registers, or raise an IRQ. Source IRQ15's low-CS path at
`FFF17D59` overwrites selected TX byte 0 with 1; its unknown mode-2 resident
callback receives selected TX in R2, while incoming RX is at pointer 11904.
Consequently native storage eligibility alone cannot establish the original
reply or replace that callback.

The new synthetic per-record case captures the full F5/F7/F8/FA family from
the source state and buffers, waits with a bounded host deadline for native
I/O, checks metadata and the entire body image after individual records,
checks final eligibility, and verifies reset/cancel preserve durable bytes.
The upload fixtures declare only the exact required mode byte, consumed-RX
pointer and both 276-byte rings in their synthetic startup ZERO table. The
production machine continues to map RAM from the input's real startup table;
it has no added RAM alias. The sole builder compiled/linked exact production
and fixture `3eecfaa` (RX build session 38012, exit 0), then passed only this
focused case in 0.81 seconds with exit 0. Its receipt is
`work/vrrecordprogressreceipt.log`, canonical snapshot
`d2d94c46c0c56a083197e5595918d150e6b0e3a0`. Prior staging and persistence
passes were retained, and the external-input five and peripheral nine were
not rerun. These results do not claim original firmware boot or an original
peer response.
