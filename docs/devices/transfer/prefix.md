# Cached state prefixes on retained A8 images

The normal GUI's216/344-byte A8 image now completes through original IRQ129.
The model handles the independently optional cached matrix and color-transform
disable prefixes on the retained image body, using the existing rasterizer and
sampler. All21 transfer native subtests pass. Original execution completes ten
prefixed images and later work, then reaches a176/368-byte RGBA clear targeting
bec5f200. Application bytes change to01/03 and primary request0, but GUI gate,
pending3 and display-ready remain0; no GUI first frame is established.

## Direct observations and upstream producers

Code addresses are unchanged C1.70 MAS VAs; command/data buffers are board PAs.
The baseline reproduces216/344 at bd0e0490/bd1605b0, rejecting CPU1 PC106ee3c6,
WRITE32 PA201810e0=1, `transfer-stream-truncated`. Against the preceding
capture, only the paired opaque scratch operand changes, from bcec3f70 to
bcca7f50. This is the same complete grammar, not a byte-identical replay.

Setup has54 words: a ten-word cached matrix write followed by the existing
44-word inline image body. Draw has86 words:01848004=0 followed by the existing
84-word retained image body, including ending/padding. Captured body operands:

| Field | Value |
|---|---|
|Active matrix|float32 diagonal(1,-1,1), translation(616,44)|
|Inline path|02 07 09 07 00; signed32(0,0,38,36,-38)|
|A8 source/extent|b97ac500,40x48|
|Target/paint|bec5f200 / ff000000|
|Temporary/final low-byte mode|8 / 0e|
|Key|10007 in both streams|

10686d50 calls the conditional global initializer106454b2 at10686ddc, then
flushes dirty state with106b581c(context,0x17f,2) at10686eea before calling
106b4b28 at10686efa. Within106b55a2, dirty mask1 converts nine cached fixed
values through the pointer stored at private+984 and emits0041f046. Dirty
mask0x20 independently reads private+488 and emits01848004 with Boolean
payload0 or01000000. These are the same cached-state producers established
by the earlier [matrix](overwrite.md) and [paint-state](paint.md) investigations.

The baseline trace joins cache emission to10645a66 with bank1/header0041f046,
LR106b5627, and color disable to106453cc with bank0/header01848004/payload0,
LR106b5709. The image producer later calls106462b0, whose nine-value loop
emits a second0041f046 to the same bank/register range. The trace observes
this second vector write before6=1 and the paired1a6=1 draw trigger.

The complete setup has matrix headers at words0 and10, path latch at40/41,
and key85 at51/52. There is no command between the two full matrix writes.
Draw's disable is at0/1; its only1a6 trigger is at81/82. Thus every cached
matrix value is replaced before latching, while the color-transform disable
remains active through drawing independently of the low-byte modes. The
[fixed Z7 decoder](https://github.com/WonderLantern/z7-qemu/blob/97abf0b81cd3211f8374a38987a0beaf4549b90f/qemu/nikon-z7-gpu.h)
corroborates register replacement, byte enables and latch/draw points; this
software reference is not a physical Z7II measurement.

## Finite combinations and assumptions

The shared image parser separates the optional prefixes from the unchanged
44/84-word retained body. Dirty masks1 and0x20 are independent, so the whole
source-backed retained family is accepted:

| Cached matrix | Color disable | Setup/draw bytes |
|---|---|---|
|Absent|Absent|176/336|
|Present|Absent|216/336|
|Absent|Present|176/344|
|Present|Present|216/344|

The existing initialized224/472 form remains separate and validates its full
global prefix. A retained prefix does not establish context: its target must
match a successfully initialized720x480 RGBA profile. Image initialization or
a supported clear can establish that profile; ACK retains it. Cold, foreign,
failed and reset contexts remain errors. Reset/unrealize cancel deferred work.

The optional matrix header must write the complete41..49 range. Its nine raw
payload words are unused; the second matrix still passes the existing finite
affine validation. The optional color pair must be exactly01848004=0. Enabled
color transforms, unknown masks/bits, partial or repeated prefixes and an
intervening latch are not admitted. Original command source addresses/lengths
remain intact for range/alias checks, including the skipped prefix bytes.
Sampling, paint, prior-mode8/10/12/14 validation, keys, source extent, ending,
padding, complete target mapping and actual output-before-IRQ are unchanged.

The explicit functional assumption is that completely replaced matrix data
has no eager numeric side effect. Raw NaN/Inf/projective values can therefore
appear only in that unused prefix. Active matrices keep all checks. Disabled
color-transform meaning and existing Cairo/compositing/sampling precision
remain software-backed functional conventions, not measured silicon fidelity.
There is no general command interpreter, guest patch or supplied GUI bitmap.

## Native validation and original continuation

The new216/344 native case fails against the old model with MEMTX_ERROR.
After the correction, the new case covers77 variants: all four prefix forms
crossed with all four prior modes, each of nine raw matrix slots with four bit
patterns, independent whole-target/guard reference pixels, repeat and immutable
capture, initial-image/clear contexts, cold/foreign/reset/failed contexts,
header/flag/active-matrix/sample/key/latch/truncation/padding errors, source
overlap with the unused matrix prefix, mapping/input loss and cancellation.

An initial integration attempt exposed a dispatch collision: the established
224/1688 LUT program also has an f24 header at draw35. That attempt stops at
the early LUT and its broad GDB snapshot fails; it is not A8 continuation
evidence. A focused native collision case is red before correction. The final
dispatch retains exact full-image initialization length, bounds retained image
draws by the84-word body plus two-word prefix, and keeps the longer retained
converter family ahead of the shared f24 header. The real LUT fixture with
that shared header must execute and ACK. Final applicable
`nikon_transfer_unit` passes all21 subtests in1.38s. These native results and
the initial unsuccessful integration remain separately scoped receipts.

The final unchanged original run keeps the prior declared descriptor17 TEMP,
descriptor18/resource12, acceleration(0,0,1024), RNG0, cold F4, five-CPU MTTCG
and normally paced9600 healthy UART inputs. Ten216/344 jobs each enter original
IRQ129 before the next START, followed by176/336 images and further conversions,
composites, clears and paths. There are69 handler entries from the first new
prefixed image until the next error. Final application bytes are01/03, primary
request0, BatCk00010001 and GUI gate/pending3/display-ready0. These are actual
snapshot values; their change is not proof of a displayed frame. QEMU and peer
remain alive until controlled cleanup, and UART has no protocol rejection.

The next first error is CPU0 PC106ee3c6, WRITE32 PA201810e0=1,
`transfer-stream-program-not-supported`, on176/368 at bd0dff90/bd15ff90.
Its indirect segments are02 07 09 07 00 and float32 coordinates
(0,0,720,480,-720), identity matrix, opaque black SRC_OVER and key10001.
The fixed-field comparison with the existing single-clear profile has one
difference: draw target word33 is bec5f200 rather than the historical c175d600
restriction. The existing temporary mode8 allowance already matches this job.
Generalizing this target and the associated context/output contract needs the
next producer/ownership validation; this packet is captured but not accepted.

Receipts: `work/runs/imageprefix-20261023/`, including native red/prefix/
dispatch-red/final logs; original baseline, unsuccessful green and final argv,
traces, snapshots, raw streams/indirect data; source projections and
producer/packet/IRQ/next-field joins. Original firmware bytes remain unchanged.
