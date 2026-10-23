# Captured RGBA stream initialization variant

The later normal path's self-contained856/1088-byte inline rectangle family
is implemented separately in [rectangles](../devices/transfer/rectangles.md).
Later progress is tracked in the [current startup frontier](../validation/frontier.md).
The older captures and profiles below retain their scope.

The 232-byte setup and 416-byte draw captured in
`work/runs/descriptorfunctional-20261022/` describe the existing finite RGBA
clear profile with one early state variation. They do not require a new fill
program: independent comparison finds only draw word 25 changing from `0` to
`8`, outside the already dynamic pointer operands. Both versions overwrite
that same register byte with `8` before either draw trigger.

## Input and original builders

The read-only source is unchanged Z7II C1.70 MAS payload
`work/runs/descriptor17-z7cross-mac1/02_eg1840_mas_01700.payload.bin`, loaded at
MAS VA `10020000`. All code addresses below are original MAS VAs. Static
decompilation receipts, a finite word decoder/profile comparison and its
results are retained under `work/runs/streamsemantics-20261022/`.
The capture's `stream.json` identifies source0 `bd0dff90`, 58 words, and
source1 `bd15ff90`, 104 words. It records a fault at `201810e0`; that fault
snapshot alone does not establish graphics completion or a first frame.

- `106453cc` appends a command header and operand to one of two command
  buffers; it is the common scalar emitter, not a pixel producer.
- `106454b2` emits target/context initialization. At `10645562` it emits setup
  header `00691004` with `80`; it emits draw `01a3f004` from the context's
  width/height at `context+88/+8c`, and related format/blend state.
- `10645b2e` builds the paired indirect-path records: setup header `0080f026`
  at `10645be4`, five following words, and draw header `00a0f02e` at
  `10645c12`, six following words. It selects one of two path slots and toggles
  the slot at `10645c28`.
- `1068bd90` is the original clear-program builder. It reads context width and
  height, configures state, calls the two mode setters described below, then
  loops through path bands using `10645b2e` at `1068be90`. These static calls
  identify the generating family; this investigation did not trace a complete
  runtime call stack to assign every captured word to a unique invocation.

## Observed fields and their meaning

Indices are zero-based 32-bit little-endian words. Registers here are command
register indices, not host MMIO addresses.

| Captured field | Value | Interpretation and limit |
|---|---|---|
| setup 11–20 | header `0041f046`, diagonal `3f800000` | Identity float32 transform. |
| setup 21/22 and 25/26 | headers `0004f004`/`0002f004`, pointers `bcca7a10`/`bcca7b50` | Indirect coordinate/segment operands. Their pointed-to bytes are not contained in the three supplied capture files. |
| setup 23/24 and 27/28 | `0005f004,d0000005`; `0003f004,10000005` | Existing profile's float-coordinate, indirect five-segment path setup. |
| setup 42/56 and draw 87/98 | `00010001`, `00010002` | Two matching path keys. |
| draw 8/9 | `01a2f004,0` | Format0, interpreted as RGBA8888 by the existing source-backed profile. |
| draw 10/11 | `01a3f004,02b00400` | Width1024, height688. |
| draw 32/33 | `01a4f004,bf309600` | Destination base, same as existing profile. |
| draw 44–46 | `00c0f00e,03ff0000,02af0000` | Clip bounds consistent with x0..1023 and y0..687. |
| draw 75/76 | `0185f004,ff000000` | Packed paint; existing RGBA interpretation writes bytes `00 00 00 ff`. |
| draw 77/78 | `01863004,108` | Existing fixed render state. |
| draw 79/80 and 90/91 | `00a0f02e,bcca7f50`; `00a0f02e,bcec3f70` | Path addresses match setup 34 and 48. |
| draw 88/89 and 99/100 | `01a61004,1` twice | Draw triggers, after final mode initialization. |

The existing [finite RGBA contract](../devices/transfer/protocol.md#finite-rear-lcd-rgba-composition-clear)
requires actual segment bytes `02 07 09 07 00` and float32 coordinates
`(0,6,1024,682,-1024)` before accepting the indirect operands. Those bytes
describe a closed filled rectangle, x0..1024, y6..688; they are a separate
DMA-read requirement, not verified merely by matching these two streams.

## Why the old exact fingerprint rejects it

`check.py` compares every fixed setup/draw field, retaining only the existing
dynamic operands. Setup matches completely; draw word25 is `8` where the
original fixed fingerprint implicitly expected zero. Word24 is `01841004`.
The same header occurs at word59, followed by the already-required operand8
at word60. There is no draw trigger between these two low-byte writes.

The original builder supplies the reason. Let `s = *(a1+1c)`:

```text
10645c86: s[c0] = argument;
          emit 01841004, (s[bf] ? 8 : 0) | (argument << 1) | !!s[bc]
10646062: s[bf] = argument;
          emit 01841004, (argument ? 8 : 0) | (s[c0] << 1) | !!s[bc]
```

Offsets `bf/bc` are bytes; `c0` is a word. The first setter preserves the
software shadow's boolean bit3 while changing another field. The clear
builder calls it with zero at `1068bdee`, then calls the second setter with
one at `1068be22`, forcing bit3. Thus an inherited shadow value can explain
the early8. This is software-shadow recomposition, not a hardware-register
read/modify/write. The exact earlier call that established the captured
shadow value was not traced here.

The byte-lane interpretation is corroborated by the fixed public
[Z7 command decoder](https://github.com/WonderLantern/z7-qemu/blob/97abf0b81cd3211f8374a38987a0beaf4549b90f/qemu/nikon-z7-gpu.h#L487-L502):
header bits12..15 independently select the four bytes, bits16 upward select
the register, and op4 keeps the register while op6 advances across operands.
Accordingly `01841004` writes only register184's low byte. Other captured
writes to184 select byte2 (word12), byte1 (word26), and byte3 (word55).
Word59 again selects byte0. Both triggers therefore see low byte8 whether
word25 was0 or8. The public implementation corroborates this interpretation;
it is not Nikon physical-silicon documentation.

## Smallest supported contract

Accept draw word25 only as0 or8 within the existing exact 232/416 profile,
while retaining word60=8, command order, all other masks/fields, both triggers,
format, geometry, color, destination, path keys, indirect path checks, DMA
ranges and overlap rejection. The renderer can use its existing finite RGBA
clear semantics because the differing byte is overwritten before drawing.
Do not accept arbitrary register184 values or relax the rest of the program.

The producer investigation changes no device implementation and runs no guest or native
tests. It establishes a bounded parser-acceptance rationale, not execution
acceptance. Unknowns remain the full graphics ISA, physical timing and cache
effects, the captured indirect operand contents until read separately, the
earlier software-shadow producer, and the downstream scanout/first-frame
outcome. No IDA database, firmware, guest state, hardware source or tests were
modified by this investigation.

## Integrated model and original-code follow-up

The subsequent parent integration changes only the finite fingerprint to
accept initial word25=0/8. The new initial8 fixture fails on the old model;
the updated complete transfer unit batch passes all six cases, retaining
unknown-initial-bit and final-mode rejection, exact pixels/guards, IRQ/ACK and
initial8 map-loss/reset/unrealize coverage. Receipts are
`work/runs/streammodel-20261022/{red,green}.log`.

The unchanged bundle runs with the same explicit constructed descriptor17
input. At entry `106ee04a`, the232/416-byte streams have initial8, segments
`02 07 09 07 00` and coordinates `(0,6,1024,682,-1024)`. Thus this run also
reads the pointed-to bytes absent from the earlier fault capture. Active
first/last target samples become `00 00 00 ff` and original IRQ129 handler
`106ee3ca` is entered. The next original submission, not this accepted pair,
causes the later data abort.

### 720x480 single-path profile

The new descriptor reuses source addresses `bd0dff90/bd15ff90`, but word counts
are44/92, hence176/368 bytes. Both streams are complete: setup ends with a
NOP, draw word88 triggers once and words90/91 form its normal ending. Decoding
retained in `work/runs/streamnext-20261022/semantics.json` shows format0,
mode8, paint `ff000000`, state108 and a single path key10001. It omits the
earlier second band and changes extent/clip to720x480/719x479 and target to
`c175d600`, resource57 record7. The declared RGBA extent is
`[c175d600,c18aee00)`, length151800 hex, within that record's backing slot.

A separate original-code entry probe measures the **current** indirect bytes:
segments `02 07 09 07 00`, float32 coordinates `(0,0,720,480,-720)`.
The target rectangle is therefore full720x480, not inferred from reused
pointers. Caller LR=`106eeab9`; the original dispatcher `106eea34` calls
`106ee04a(a1+36,...)` at `106eeab4`. These observations and its static
decompilation are retained in `work/runs/streamboundary-20261022/`.

The preceding length dispatcher recognized draw416 and1648; other lengths
went to the LUT parser. Draw368 consequently failed its224/1688-byte length
check with `transfer-stream-truncated`. That diagnostic denoted an unsupported
profile, not missing DMA bytes.

The new finite table now recognizes exactly176/368, validates the one path,
uses the existing direct-memory/overlap checks and writes precisely
`[c175d600,c18aee00)` through the existing deferred RGBA executor. The ending
operand draw91=1 is fixed, not the two-path profile's dynamic second operand.
That extension alone did not accept stateful continuation. Its new fixture fails
on the preceding model, then the complete transfer batch passes seven cases;
see [the native receipt](../validation/native.md#rgba720x480-single-path-clear).

In the unchanged-bundle run with the same functional input, the target's first
sample changes from zero to `00 00 00 ff`, its last sample also reads opaque
black, and the tail sample stays zero. Original IRQ129 handler `106ee3ca` is
entered. Accepted command/IRQ/next-fault receipts are retained under
`work/runs/singleclear-20261022/`. Its first exploratory probe attempted to read
an unmapped address below record7's slot and failed in GDB; those prefixed
logs are excluded from the completion-to-fault result.

### Gray rectangle and inherited context

The subsequent descriptor contains42/58 words,168/232 bytes, at
`bd0e0040/bd160100` (immediately after the preceding buffers' captured bytes).
Setup lacks the initial register69 write and selects path key10002. Draw
sets target `c175d600`, low mode byte8, paint `ffb4b4b4`, state108 and one
trigger; both streams have normal ending commands. It omits full format,
target extent and clip initialization. Decoding is retained as
`work/runs/singleclear-20261022/{decode.py,nextcommands.json}`.

An entry-only read at `106ee04a`, caller LR=`106eeab9`, captures actual
segments `02 07 09 07 00` and float coordinates `(96,86,528,308,-528)`.
This describes a centered gray rectangle `[96,624) × [86,394)` inside the
preceding720x480 target. The dispatch record begins bytes `01 0a`: case0a of
the original dispatcher `106eea34`. Captured command bytes match those from
the completion-to-fault run. Receipts are
`work/runs/singleboundary-20261022/{stream.json,entry.json}`; that probe later
receives an asynchronous debug stop on EX and is used only for its prior
entry/operand reads, not another MAS abort claim.

The new model records a finite known720x480 context only after successful
execution of the full initialization. ACK and the exact gray continuation
retain it. Reset/unrealize, rejected complete programs, processing failure or
completion of another graphics profile invalidate it. Canceled/failed
initialization never establishes it. It is a validity bit for the known fixed
state, not a general graphics register file or an implicit default format.

Exact168/232 tables validate target, color, state, mode, path key/echo and
ending. DMA still preflights the full target and both indirect arrays and
rejects overlap/unbacked ranges. The executor maps the complete strided span,
then writes528 pixels on308 rows with720-pixel stride, changing precisely
`[96,624) × [86,394)` to bytes `b4 b4 b4 ff`. Other pixels remain unchanged.
Native red/green and context lifecycle coverage are recorded in
[validation](../validation/native.md#rgba-gray-continuation-context).

The unchanged bundle with the same input now produces gray first/last
rectangle samples while side/next-row samples remain opaque black, and enters
original IRQ129. No firmware, input, guest event or software status is changed
to achieve this step. The accepted gray-completion/next-abort probe is
`work/runs/graycontext-20261022/{probe.log,snapshot.json,qemu.stderr}`.

### Captured131-path batch

Before the path extension, the first later MAS abort is store `106ee3c6` to `201810e0`,
`lr_abt=106ee3ce`, `DFAR_S=201810e0`, `DFSR_S=1808`. The descriptor now
contains5372/7208 words,21488/28832 bytes, sources `bd0e00f0/bd1601f0`.
Full command decoding finds131 setup records and131 draw triggers with keys
10003..10085, all paint `ff000000`, target `c175d600` and retained format,
extent and clip. Setup uses varying3x3 transforms and variable coordinate/
segment arrays. Both streams end normally; the preceding dispatcher falls into
the LUT length guard and reports `transfer-stream-truncated`.

Entry capture at `106ee04a`, caller LR=`106eeab9`, reads all131 indirect
arrays and transforms. Coordinate counts range10..128 and segment counts6..37;
the observed segment vocabulary is00/02/04/0a. Path10003 has18 coordinates/
10 segments, transform scale27/36
and translation200/350.3232421875; its segment bytes are
`02 04 04 04 04 04 04 04 04 00`. Path10004 has62 coordinates/21 segments
including code0a, with the same scale and x translation217.41552734375.
The [original-producer investigation](path.md) confirms MOVE/LINE/QUAD/CLOSE,
nonzero winding, direct buffer y, black SRC_OVER and the six-line battery
communication warning. A bounded Cairo A8 path module now processes the
original arrays, preserving the gray background and retaining context across
completion/ACK. Full-batch staging, bounds and effective backing checks prevent
partial output or unsafe DMA aliases. Decode and raw
bytes are in `work/runs/graycontext-20261022/`; passive entry/operand reads are
in `work/runs/graypaths-20261022/`.

The final original-code execution produces25958 changed gray-panel pixels,
including11874 solid black and14084 fringe pixels, at buffer bounds
`[200,605) × [105,378)`. All alpha stays opaque and outside-panel pixels remain
black. It enters original IRQ129, then faults on a different complete pair:
824/1112 bytes,206/278 words, sources `bd0e54e0/bd167290`, five paths with
keys10086..1008a. Their identity-transform arrays are rectangles using
`02 07 09 07 00`: side strips2x308 at622/86 and96/86, horizontal strips528x2
at96/86 and96/392, and72x72 at112/306. The first four paints are `ffe6e6e6`,
the last `ffb4b4b4`, with even-odd operand `00040002`. Relative H/V and mixed
paints were unsupported at that checkpoint; draw1112 consequently hit the LUT
length guard. The extended path model now supports relative H/V, even-odd and
ordered opaque paints. The original824/1112 pair completes through IRQ129:
3328 border pixels become light gray and the72x72 patch is gray, with all other
pixels exactly matching the previous glyph frame. The old dedicated gray
continuation branch has been replaced by this shared path processor.

The subsequent176/336-byte pair has44/84 words, key1008b. It has
inline segment/coordinate payloads, matrix diagonal `(1,-1,1)` with translation
`(112,378)`, image source `b97a6c00`, extent register `00500048`, paint
`ff4848f0` and mode0e. The signed coordinate words `(0,0,72,72,-72)` suggest a
72x72 rectangle. [Image analysis and implementation](image.md) establish an
80x72 A8 source, f24 nearest/clamp sampling and paint modulation. This pair now
completes through original IRQ129, changing3751 icon pixels and preserving all
pixels outside the72x72 patch. The subsequent224/1736-byte pair reads RGBA source
`bf309600` and targets YUV105 planes `cc18b000/cc235800`; that separate contract
was rejected with `transfer-stream-truncated` at that checkpoint. The finite
[YUV105 conversion](conversion.md) now completes it through original IRQ129,
with source/guards unchanged and both698368-byte planes fully written. The
next same-length pair changes source to720x480 `bec5f200`, output107 at
`cb82b000/cb980000`, path scaling and filter selectors; it now also completes
through original IRQ129. The176/1552-byte pair now samples the warning
surface using SRC_OVER and retained107 state through IRQ129 too. Its packed
output is independently checked against the source image. The subsequent
bounded run has no new graphics rejection or MAS data abort; manager returns
from warning worker4 to worker0, with GUI readiness still unarmed.
Current native, frame
comparison, first battery report and next-fault receipts are retained in
`work/runs/coloredpaths-20261022/`; a separate read-only next-entry/source-window
capture is in `work/runs/textureboundary-20261022/`.
Current A8 red/green, icon frame and next-abort receipts are in
`work/runs/texturemodel-20261023/`; the next pair's entry capture and an
independent battery-alarm read are in `work/runs/imagetransfer-20261023/`.
The current YUV105 native/completion/next-abort records are in
`work/runs/yuvmodel-20261023/`; a passive next107 entry capture is in
`work/runs/yuvnext-20261023/`.
Current107 red/green, complete planes and next-fault receipts are in
`work/runs/yuva107-20261023/`.
SRC_OVER native/frame/handoff and worker0 policy-event receipts are in
`work/runs/srcover-20261024/`.

Current native/final execution receipts, captured frame and reference-direction
preview are `work/runs/pathmodel-20261022/`; passive five-path capture and
paired command comparison are `work/runs/pathboundary-20261022/`. The PNG
preview reverses captured rows per the fixed Z7 presentation convention and
does not establish Z7II physical scanout. No host font or UI pixels are
substituted into guest memory.

Manager remains4/5/15/2, panel=`b7f54a00`, GUI gate/pending3=0. Target physical
display identity, downstream scanout and first-frame acceptance remain open.
