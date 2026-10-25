# Cached state prefixes on retained graphics

The normal GUI's216/344-byte A8 image now completes through original IRQ129.
The model handles the independently optional cached matrix and color-transform
disable prefixes on the retained image body, using the existing rasterizer and
sampler. All21 transfer native subtests pass. Original execution completes ten
prefixed images and later work, then reaches a176/368-byte RGBA clear targeting
bec5f200. Application bytes change to01/03 and primary request0, but GUI gate,
pending3 and display-ready remain0; no GUI first frame is established. The
later[target model](target.md) crosses that176/368 record0 restriction; see
the[current frontier](../../validation/frontier.md).

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
That checkpoint captured but did not accept the packet. The subsequent
[producer/ownership validation](target.md) now generalizes the initialized
target and verifies record0 output through the original IRQ.

Receipts: `run imageprefix/`, including native red/prefix/
dispatch-red/final logs; original baseline, unsuccessful green and final argv,
traces, snapshots, raw streams/indirect data; source projections and
producer/packet/IRQ/next-field joins. Original firmware bytes remain unchanged.

## Independent prefixes on retained rectangle records

After the timing extra-feature model, the original first uncovered graphics
submission is CPU1 PC106ee3c6, WRITE32 PA201810e0=1,
`transfer-stream-truncated`. The complete buffers in
`final-stream{0,1}.bin` are1232/1416 bytes,
not incomplete DMA input. They contain six existing720x480 identity SRC
rectangles into PA bec5f200, paint0, modes8/8 and keys10009..1000e:

| Records | x | y | width | height |
|---|---|---|---|---|
|1|136|6|112|40|
|2|594|6|100|38|
|3..6|90|402|36|36|

The first setup record has43 words, containing only the active matrix and
existing inline path. Each later record has53 words, with a ten-word cached
matrix before that same43-word body. The first draw has57 words without an
extra color-disable prefix; each later draw has59 words, including its
two-word01848004=0 prefix. The single ending is00081004=1. Thus setup is
43+5*53=308 words and draw is57+5*59+2=354 words, both already even.

The trace directly joins all six records to the same MAS context VA b1800c1c:
1068bf1e flushes106b581c(context,0x17f,2) at1068c00c, then calls1068bd90
at1068c2e4 with the matching coordinates. The first flush emits neither
matrix nor color-disable commands. The second flush explicitly joins
106b55a2 to bank1/header0041f046 at LR106b5627, then bank0/header01848004,
payload0 at LR106b5709. Each1068bd90 then emits the mandatory identity
matrix from ROM VA1078926c and its explicit color-disable before drawing,
and ends by marking dirty bits0x21. Dirty masks1 and0x20 independently
control the cached prefixes; absence is not a missing active matrix or
permission to enable color transforms.

The previous retained dispatcher required01848004 as the first draw header,
so a body beginning directly0115c004 fell through to LUT length validation.
Its rectangle parser also assumed53 setup words and59 draw words per record.
The finite parser now consumes each retained record as independently optional
cached matrix(10 words), mandatory active setup(43 words), optional color
disable(2 words), mandatory draw(57 words). Both full matrix headers, the
active identity matrix, zero color-transform state, paint/blend, path/key
sequence, shared target, geometry, ending and padding remain checked.
Initialized rectangle packets keep their existing complete prefix grammar.

Only a previously established720x480 RGBA profile admits retained work. The original
complete stream lengths remain authoritative for overlap and backing aliases,
including unused cached bytes; full720x480 target preflight, deferred mapping,
output-before-IRQ and reset/unrealize behavior are unchanged. The assumption
about unused overwritten matrix payloads having no eager numeric side effect
remains the one stated above. Per-record prefix independence is source-backed
functional inference; this trace observes no prefixes on record1 and both on
records2..6, rather than every possible combination on physical hardware.

The original `/nikon/transfer/rectangle-prefixes` fixture defines33 variants:
all four uniform combinations, mixed per-record combinations, the exact
six-rectangle grammar,256 records, raw unused matrix payloads, immutable
commands, absent/reset/failed context, changed headers/active state/color/key/
target/scratch, truncation/padding/ending, backing alias, map loss and
cancellation. Full output, overlapping paint order, slot tail and guards are
compared with an independent rectangle reference. Native execution and the
next original frontier require the parent integration receipts; defining a
fixture is not evidence of passing validation or a GUI first frame.

The parent subsequently records an old-model behavioral red at the first
valid no-prefix START in `run timingextra/prefix-red.log`.
After integration, `prefix-green.log` passes the complete applicable transfer
batch:25 subtests in2.10s, including the33-variant prefix fixture. These are
synthetic native receipts. The original1232/1416 rejection described above
remains the observed baseline; a new unchanged-firmware integration run and
its next boundary are still pending, and no GUI first frame is established.

## Shared keys across intervening A8 jobs

The first integration of prefix parsing still rejects the same1232/1416
grammar, now at board PAs bd0e0620/bd160860, with
`transfer-rectangle-context-not-established`. This is a parser advance only;
that batch has not completed. The failure exposes the old rectangle-only
cross-batch key gate, not a missing original producer.

`10645c3c` increments the common context's private+184, wrapsfffe to1,
emits matching bank1/85 and bank0/a6 keys and then1a6=1. The global
initializer106454b2 resets private+184; the helper is shared by rectangles,
paths and images. The integrated original trace directly joins:

| Trace line | Original operation |
|---|---|
|899510|Global initialization00691004=80 in MAS context VA b1800c1c|
|900215..903490|Initialized six rectangles produce10001..10006|
|903740 /903760|1280/1560 START, then original IRQ129|
|904547 /904808 /904828|A8 key10007,216/344 START, IRQ129|
|905558 /905563|Image producer LR106b40d7 calls10645c3c and emits10008|
|905813 /905833|176/336 START, IRQ129|
|906478 /906483|Rectangle LR1068bead calls10645c3c and emits10009|
|910021|1232/1416 START, followed by the context rejection|

There is no intervening global initialization after line899510. The two A8
jobs legitimately consume7/8 and preserve the same720 profile. The old device
instead cleared rectangle_key after either successful A8 job because only
rectangle execution published that field. Its next-key gate therefore could
not admit the firmware's normal10009 start.

The corrected model removes that obsolete field and its execution output.
Retained rectangles reuse the existing successfully established720x480 RGBA
profile. The complete body still supplies and validates its own target;
matching a historical target is not required for this family. First keys
must remain0001xxxx with low16 in1..fffe, and every record must follow the
batch's sequence with setup/draw echo and existing wrap. Repeated or skipped
legal keys across batches are admitted. No hardware key tracker or new
context store is added; cold/reset/failed and successful105/107 operations
cannot supply the required720 profile. Common geometry/clip retention is a
finite source-backed functional inference, not a physical GPU measurement.

The updated prefix fixture has39 variants. Its first case establishes six
rectangles, executes normal A8 jobs with keys7/8 and then submits retained
key9. Other additions cover legal cross-batch key skip/reuse, matched but
nonsequential internal keys, and105/107 context loss. The prior continuation
fixture now checks an actual key-echo mismatch and permits legal replay.
`context-red.log` records SKIP_KEY failing under the old gate. That run's
loop mistakenly began at NONE after A8_CHAIN was inserted before it, so it
did not execute A8_CHAIN and cannot establish that chain's red or green
result. After correcting the loop, `a8chain-red.log` records the first A8_CHAIN
case failing at its final retained START. Its baseline is a reconstructed
old-key contract: optional per-record prefix parsing is retained and only the
former rectangle_key state/publication contract is restored. It is not a run
against the unmodified earlier full device. Restoring the final implementation,
`native-transfer-final.log` passes all25 transfer subtests in2.11s, executing
all39 prefix variants. These native receipts are synthetic.

The final original run in `run timingextra/validated*` keeps
unchanged firmware and the declared functional inputs. Over110 host seconds,
QMP reports running and no uncovered access is recorded. Four1232/1416 STARTs
are observed, later graphics continue and a2976/3448 rectangle batch also
completes. This separates the actual original continuation from raw matrix
combinations covered only by native fixtures.

The same run observes timing channel4 result1 and W1C status0. Original
410a1805/status9 reaches1039578c and returns through1039a408; shared bitmap
bit4 is then clear, leaving0x20. Those are original timing/event observations,
not injected readiness. The final application bytes are01/03; manager
current/target/mode/state are3/5/1/2 and BatCk is00000001. GUI gate, pending3
and display-ready remain0. A bounded run without an uncovered access and
ongoing graphics do not establish a displayed GUI first frame.
