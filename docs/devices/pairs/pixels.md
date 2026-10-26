# Monochrome paired pixel processing

The Z7II paired model consumes completed RAW12 DMA buffers and produces main
Y/UV pixels, work history/residual planes and a packed grid before publishing
completion. It supports an explicitly selected monochrome calibration profile
for C1.70 MAS. The address joins and software request paths are source-backed;
the filters, temporal arithmetic, gain scales, output encodings and event
cadence below are
finite functional choices, not recovered Nikon silicon processing or factory
calibration. This contract does not establish normal GUI startup or physical
camera accuracy.

The implementation is in `hw/nikon/devices/pairs.c` and
`hw/nikon/devices/pairedpixels.c`; their interfaces are
`include/hw/nikon/paired.h` and `include/hw/nikon/pairedpixels.h`.
The RAW producer is `hw/nikon/devices/capturework.c`.
[Request admission and pool ownership](requests.md) describes the original
software joins; this page defines the pixel operator and its lifecycle.

Addresses, register offsets, register values and masks below are hexadecimal.
MAS code addresses are CPU virtual addresses (VA); device bases and buffer
addresses are board physical addresses (PA).
Register offsets are relative to PA `20380000` or `203c0000`, each covering
`5e00` bytes. Ranges exclude their upper end. Device accesses are aligned,
little-endian 32-bit words; dimensions, byte counts and arithmetic constants are
decimal unless written with `0x`.

## Source binding and functional boundary

C1.70 MAS VA `10369faa` writes `10=1` and then `08=1` to start a selected
paired bank. VA `102e49fe` publishes the configuration through typed setters.
The important source-established joins are:

| Producer | Established software behavior |
| --- | --- |
| `102e5826`, `1036a268` | Publish the source origin and extent at `44/48` |
| `102bec2e`, `102beb6a`, `1036a56c`, `1036a672` | Publish two four-slot RAW address groups and pitch at `30c` |
| `102beaa8`, `1036a82a` | Publish two four-slot grid groups at `398..3b4` and pitch at `394` |
| `102be8a0`, `1036ab28`, `1036ab54` | Publish the cropped geometry and work/main address groups |
| `102e57a6`, `1036bb78`, `1036beaa`, `1036c02c` | Publish the A, B and C coefficient layouts |
| `1036bb04` | Publish three unsigned 16-bit gains and four boolean control bytes at `9cc/9d0/9d4` |
| `1036bb68` | Publish either of two 516-byte curve records at `2890` and `2a94` |
| `103650c0`, `10364bd8`, `1036c862` | Select a cyclic feedback record, construct its scalars and publish the complete packet at `df4..e10` |
| `102e4ce4`, `102e4d06`, `1036bf88`, `1036bfae`, `1036ca74` | Skip null table pointers; copy selected tables into `1400..1d37` |
| `10369fc4`, `1036a4fa` | Request finish at `0c` and read the result word at `118` |
| `102be68c` | Subscribe to raw events `1402`, comprising `2`, `400` and `1000` |

These setters establish representation and ownership, not the physical meaning
of every field. In particular, `300=0` is not proof of a 16-bit input format:
`102beb6a` can force its format field to zero for the enabled path. The model
uses the actual RAW producer's bit depth and stride instead.

The chosen operator accepts grayscale RAW12 samples, applies impulse, spatial
and temporal filtering, then applies up to two Q10 gain curves, independent
Q8 RGB gains, bounded LFSR noise and live A/C Q10 and B Q9 matrices to the main image. Its work
planes are half-resolution luma and residual; its grid contains area means.
Those numerical meanings are hypotheses made explicit here so they
can be tested and replaced when a discriminating consumer or physical record
becomes available. They are not arbitrary gains inferred from the opaque tables.

## Explicit calibration input

The machine property `paired-calibration` selects a binary file; the paired
device receives it through its `calibration` property. The file is exactly
24,064 bytes: the complete relative register interval `[0,0x5e00)`, in
little-endian 32-bit words. File byte zero corresponds to bank offset zero.

Interpreted configuration words must be zero in the file because guest writes
provide their live controls, geometry, addresses and matrix coefficients.
`paired_live_word` defines this complete set; each range below advances in
four-byte words and includes both endpoints:

```text
0c, 84, f0
40, 44, 48, 4c, 50, 94, 98, 9c, a0, a8
b8, bc, c0, c4, c8, cc, d4, d8, dc, e0, e4
f8, fc, 100, 104, 108, 10c, 110, 11c, 120, 124, 128
12c, 130, 134, 138, 13c, 140, 144, 204, 300, 3d8, 420
218..234, 30c..32c, 394..3b4, 400..410, 428..470, 488..4c8
b00..b10, b64..b74, b94..ba4
9cc, 9d0, 9d4, df4..e10, 2890..2c94
```

Every reserved or inaccessible file word must also be zero. A wrong length,
unreadable file, nonzero interpreted slot or nonzero reserved slot fails device
realization. The file is a declared virtual profile, not a recovered
physical-camera calibration dump.

The RAW ring addresses are live descriptor fields. Source VA `102bec2e`
publishes descriptor byte 52 into either four slots at `218..224` or
`228..234`, through `1036a56c`. VA `102beb6a` publishes the same descriptors
through `1036a672`, at `310..31c` or `320..32c`. The shared software selection
byte also reaches root configuration byte 39 and bank register `fc` through
`102c0304` and `1036a3da`. The finite model interprets `fc=0/1` as selection of
the primary/secondary address group, retaining four phases per batch. Source
root bytes 42/43 separately produce `108=3` and `10c=0`; a second group does
not establish eight independent processing phases.

For the selected group, all four front RAW addresses must equal their back
counterparts: `218..224/310..31c` for group 0, or `228..234/320..32c` for group
1. Each group may name independent buffers. Actual completed RAW generation,
RAM coverage, backing aliases and overlap are checked through the selected
addresses before DMA. A group switch does not create a new input generation
or allow replay. Third/fourth back groups `330..34c` remain zero, and `fc>1`
rejects. Inactive RAW/grid/main staging is retained without executing it or
cancelling active work. Its complete memory/profile checks apply when selected.

The related output rings follow the same software selection byte but retain
their own plane types. VA `102beaa8` and setter `1036a82a` publish the two grid
groups. VA `102be8a0` and setter `1036ab54` publish both work and main plane
groups. In the inspected work mode, the producer explicitly fills both work
groups from the same descriptors. The supported relationships are:

| Plane | Primary four slots | Secondary four slots | Finite relationship |
| --- | --- | --- | --- |
| Grid | `398..3a4` | `3a8..3b4` | Independent, complete selected group |
| Work luma | `430..43c` | `450..45c` | Required slot-for-slot mirror |
| Work residual | `440..44c` | `460..46c` | Required slot-for-slot mirror |
| Main Y | `488..494` | `4a8..4b4` | Independent, complete selected group |
| Main UV | `498..4a4` | `4b8..4c4` | Independent, complete selected group |

Each grid group has four addresses; the main group has eight across its two
planes. A partially published selected group rejects. Y is not equated with UV,
nor luma with residual. Work groups remain required mirrors because the inspected
producer unconditionally fills both from the same descriptors; independent work
history surfaces are not inferred. Every selected plane receives full span,
RAM/backing and overlap checks. Aliases do not generate additional bytes, phases
or events. Independent selection is a functional interpretation of the shared
software selector, not a physical register specification.
The gap `3b8..3c0`, independent pointer `3c8` and status words `3cc..3d4` are
not extra grid rings and are not generalized by this contract.

For processing, every accessible opaque configuration word must equal the
selected file word, including low fields such as `304`, `308`, `414` and `424`.
Live interpreted words are checked or decoded separately. The 36-table interval
`[1400,1d38)` contains 22 tables of 16 words followed by 14 tables of 17 words,
590 words in total. Coverage is checked per table since reset:

- If any word of a table was written, every word of that table must have been
  written, including expected zero words. Partial publication rejects.
- A wholly unwritten table is accepted only when every word of that table in
  the selected profile is zero. A nonzero profile table cannot be omitted.

The latter case models the original null-pointer skip as an explicit profile
with cold zero table registers. It does not fill missing RAM data, claim an
unpublished nonzero table or generate a completion. Register equality with the
selected file still applies. Opaque table values constrain profile admission;
this model does not execute their unrecovered silicon equations. Without a
selected file, requests can be admitted but pixel processing rejects.

To construct a specialization from unchanged firmware, stop at that rejection,
read the source-declared retained words, copy only opaque words into the
little-endian image and zero the interpreted/reserved slots listed above.
Restart cold with the explicit file. This read-only capture defines which
parameter tuple the approximate operator accepts; it neither recovers the
silicon equations nor installs any values into guest MMIO or RAM.

The supported path also requires this complete scalar control tuple. A listed
range means that every listed word has the stated value.

| Relative offsets | Required values |
| --- | --- |
| `40`, `4c`, `50`, `94`, `98`, `a0`, `a8` | `5`, `1`, `0`, `0`, `400`, `3`, `4` |
| `84`, `f0` | `1`, `8e` |
| `b8`, `bc`, `c0`, `c8`, `cc` | `1`, `1`, `0`, `0`, `0` |
| `d4`, `d8`, `dc`, `e0`, `e4` | `0c060202`, `9`, `30300`, `158`, `0` |
| `f8`, `fc`, `100`, `104`, `108`, `10c`, `110` | `1`, `0 or 1`, `1`, `0`, `3`, `0`, `0` |
| `11c`, `120`, `124`, `128`, `12c`, `130`, `134`, `138`, `13c`, `140`, `144` | `1`, `1`, `1`, `0`, `1`, `0`, `0`, `0`, `0`, `0`, `0` |
| `300`, `3d8`, `404`, `408`, `410`, `420`, `428`, `42c` | `0`, `2`, `1`, `1`, `1`, `1`, `1`, `3` |
| `b14`, `b60`, `b90` | `80000000` each |
| `330..34c` | Zero each |

Other accessible fields retain their register contract; retaining a word does
not imply that an arbitrary processing mode is supported. Pixel production
requires the normal `84=1`, `f0=8e` controls; the cold or stop configuration
cannot produce an image merely because its other fields match.

The interpreted fields above do not cover every opaque program. The
[cycle-selected noise records](#cycle-selected-noise-records) have a declared
numerical model; all remaining opaque words still constrain profile admission.

## Geometry, phases and memory

Let `W/H` be the source ROI extent and `OW/OH` the main output extent:

| Field | Decode |
| --- | --- |
| Source origin | `44.high16 = x0`, `44.low16 = y0` |
| Source extent | `48.high16 = W`, `48.low16 = H` |
| Main rectangle | `40c.low16 = x`, `400.high16 = y`, `40c.high16 = OW`, `400.low16 = OH` |
| Consistency fields | `9c = H-1`, `c4 = W`, `204 = (H<<16)\|W` |

The device requires `60 <= W <= 4096`, `34 <= H <= 4096`, positive even main
dimensions and a main rectangle contained in the ROI. The completed RAW frame
must have an even width, dimensions at most 4096, bit depth 12, enough pixels
for the ROI and a stride equal to `30c`, at least `frame_width*12/8` bytes.
Packed input samples use consecutive low-order 12-bit fields in the byte
stream. The device snapshots the ROI from actual guest RAM before scheduling
processing; later producer overwrites cannot change that snapshot.

Phase `p` starts at zero and advances modulo four only after successful output.
The next input must match `0x310+0x10*fc+4p` and have a generation greater than that
producer slot's last consumed generation. The same phase selects every output:

| Output | Address | Stride | Active row bytes and rows |
| --- | --- | --- | --- |
| Main Y | `0x488+0x20*fc+4p` | `4c8` | `OW`, `OH` |
| Main UV | `0x498+0x20*fc+4p` | `4c8` | `OW`, `OH/2` |
| Work luma | `0x430+0x20*fc+4p` | `470` | `2*ceil(W/2)`, `ceil(H/2)` |
| Work residual | `0x440+0x20*fc+4p` | `470` | Same as work luma |
| Grid | `0x398+0x10*fc+4p` | `394` | 90 bytes, 34 rows |

The work address aliases at `450+4p` and `460+4p` must equal the selected luma
and residual addresses. UV must immediately follow the full pitched Y extent;
residual must immediately follow the full pitched work-luma extent.

Every target is four-byte aligned, has stride at least its active row size and
fits writable direct RAM without 32-bit address wrap. The full pitched extents
of all four phases are preflighted, including RAW buffers not yet consumed.
RAW phases cannot overlap. Targets cannot overlap any RAW extent or another
target plane. Exact same-plane aliases across phases are allowed for main and
work outputs; grid phases must remain distinct. Overlap checks include shared
RAM backing, so different PA aliases cannot bypass these rules. A peer bank's
active job or unacknowledged `1000` result also protects its target extents.

Only active row bytes are written. Row padding and unused pool tails retain
their previous contents. In the C1.70 address join, input phases select kind16
units in order 1/2/3/0; kind31 work and kind26 main select two alternating units,
while kind25 grid selects four distinct units. Their source-declared allocation
sizes do not become inferred active image dimensions.

## Numerical operator and formats

All working samples are integers in `[0,4095]`. Edge neighbors replicate the
ROI edge. The following equations define the functional model, including its
intentional precision limits.

The source-packed main-color gain fields are:

| Field | Decode and accepted encoding |
| --- | --- |
| `9cc` | Four independent boolean bytes: global bit 24, R bit 16, G bit 8, B bit 0; all other bits zero |
| `9d0` | Unsigned 16-bit R gain in the high half and G gain in the low half |
| `9d4` | Unsigned 16-bit B gain in the low half; high half zero |

A channel is gained only when both its own bit and the global bit are set.
The global/R/G/B bytes come from source-record byte offsets 12/6/7/8,
respectively. Disabled channels preserve the replicated mono sample. The source
family contains unity gain values of 256 (`0x100`); interpreting these as Q8
multipliers is a functional choice. Changing an enable byte with a unity gain therefore remains
a valid numerical configuration rather than an opaque-profile mismatch.

Each curve record has 257 unsigned 16-bit gain knots followed by a zero upper
halfword in its last 32-bit word. Table 0 occupies `2890..2a90`; table 1 occupies
`2a94..2c94`, both inclusive. Guest publication of all 129 words enables that
curve. A wholly omitted curve is disabled; any partial publication or nonzero
last upper halfword rejects. Coverage resets with the device. These curves
are interpreted live fields and must be zero in the calibration file, unlike
the 36 opaque tables described above. A completely published zero-valued curve
is enabled and yields zero mono output; omission does not mean a zero gain.

The source family includes flat curves of 1024 (`0x400`). The model interprets
knots as Q10 gains at input levels `0,16,...,4096`, not as replacement pixel values.
This gain interpretation, interpolation and ordering remain numerical choices;
the recovered publication sizes and packing do not prove the silicon equation.

1. **Impulse suppression.** Inspect the center and its four axial neighbors.
   If the neighbor range exceeds 64, preserve the center. Otherwise replace it
   with the five-sample median only when the center differs from that median by
   more than 1024.
2. **Spatial filtering.** From the impulse-filtered image, compute
   `(4*center + left + right + up + down + 4)/8` with integer division.
3. **Temporal filtering.** Divide the image into clipped 2-by-2 blocks and round
   each block mean to nearest. With valid prior luma, let `d` be the absolute
   difference between the current pre-temporal mean and prior luma. If
   `d <= 256`, use Q8 old weight
   `(64*(256-min(prior_residual,256))+128)/256`; otherwise use zero. Blend each
   current sample as `(current*(256-weight)+prior_luma*weight+128)/256`.
   Store the resulting block mean as luma and `d` as residual. Without prior
   luma, the weight and residual are zero.
4. **Grid.** Partition the entire filtered ROI into 60-by-34 cells using floor
   boundaries `floor(i*extent/cells)`. Each cell contains its rounded area mean,
   before the color matrices and independently of the main crop.
5. **Main curves and gains.** For a cropped mono sample `m`, process enabled
   curve 0 then enabled curve 1. At each stage, set `i=m>>4`, `f=m&15` and
   `gain=(knot[i]*(16-f)+knot[i+1]*f+8)/16`. Replace `m` with
   `clamp12(round(m*gain/1024))` before the next curve, so the second stage
   indexes the first stage's result. Replicate the resulting mono sample into
   RGB, then replace each enabled channel with
   `clamp12(round(m*channel_gain/256))`.
6. **Noise.** Apply the bounded three-channel LFSR operation defined below.
7. **Main matrix conversion.** Apply A then C with Q10 division, then B with Q9
   division. Each matrix stage rounds symmetrically to nearest, with ties away
   from zero. A/C outputs clamp
   to `[0,4095]`. B adds 2048 to its second and third channels before clamping.
   Convert to eight bits with `min((value+8)/16,255)`. Main Y stores every pixel;
   UV stores the rounded mean of each 2-by-2 block's U and V, interleaved U first
   (NV12).

Curves and RGB gains affect main color output only. Work luma/residual and the
grid are computed from the filtered image before these stages. Curve and gain
multiplications use the same symmetric nearest rounding and 12-bit clamp as
the matrix stages; their stage order is part of this finite operator, not a
claim about the physical processing pipeline.

A and C contain signed 13-bit coefficients; B contains signed 12-bit
coefficients. A/B pack coefficient 0 in the first word, then high/low halfword
pairs. C uses low/high pairs. A's first word permits only its coefficient bits;
B/C's first word must be the full sign extension of coefficient 0. Remaining
words reject bits outside the two coefficient fields. The fixed Q10/Q9 scales,
matrix order and saturation are functional interpretations of the recovered
coefficient layouts, not a claim of identical physical arithmetic.

Work luma and residual are little-endian 16-bit words holding RAW12-range
values. Grid pairs `a,b` are packed into three bytes:
`a.low8`, `(a>>8) | ((b&15)<<4)`, `b>>4`. Thus each 60-sample grid row writes
90 bytes. NV12, these work meanings and grid averaging are explicit finite
formats; the full physical meaning of work mode `42c=3` remains unresolved.

Prior work RAM is trusted only after this bank successfully published that
same physical luma/residual address pair in the current source epoch. Merely
nonzero RAM, another bank's output or a new calibration configuration cannot
seed history. Reusing alternating phase aliases can reuse such established
history; a source epoch change makes the old contents numerically ineligible.

## Work, completion and cancellation

The RAW producer withdraws a slot with generation zero before overwriting it,
then supplies a nonzero generation only after every packed RAW DMA row succeeds.
`nikon_paired_frame(NULL)` withdraws all source slots and advances the source
epoch. An already snapshotted job survives source withdrawal; it cannot establish
history for a later source epoch. No scene-provider notification, acquisition
acknowledgement or elapsed timer alone supplies a completed RAW buffer.

| Input or condition | Implemented transition |
| --- | --- |
| `10=1` while quiescent | Arm one request |
| `08=1` while armed | Await input; reset phase, batch and per-request result counters |
| Matching fresh RAW, valid whole profile, no pending result or peer conflict | Snapshot input/history and schedule one job after 1 ms of virtual time |
| Successful operator and every output DMA row | Consume that generation, record history, advance phase, add `OW*OH` to `118`, publish `1002` |
| All four phases successfully read | Publish the functional `400` four-read watermark and pause batch admission |
| `10=1` while awaiting input, with an unchanged ring or all four addresses republished, and no pending result, active job or stop | Release the batch pause, clear seen/refill flags and retry real input |
| Premature `10=1` while awaiting input | Accept a no-op kick; preserve request and all work/freshness state |
| `0c=1` while awaiting input | Enter drain pending; stop admitting new jobs |
| Drain has a successful publication, no active job and all pending bits acknowledged | Become quiescent and publish final bit `2` once |
| Whole-device reset | Cancel owned work and clear request, configuration, table coverage, results, histories, source slots and pending state |

Pending status is read at `24` and acknowledged by W1C at the same offset.
Mask bit one at `e8` inhibits the corresponding IRQ; IRQ level is
`pending & ~mask`. A nonzero pending word prevents the bank from processing the
next phase. Both W1C and subsequent real RAW publication can retry eligible
work. Register `18` is a virtual boolean: one while awaiting input or drain,
zero while armed or quiescent. Result `118` is an additive main-pixel work count,
not a recovered sensor statistic. VA `102be68c` subscribes to `1402`
(`2 | 400 | 1000`), and the original initialization uses IRQ mask `ffffff71`
(`~8e`). That establishes the consumer's event classes, not their physical
production cadence. The 1 ms latency, successful-frame summary `1002` and
four-read watermark `400` are finite conventions tied to actual work, not
physical timing measurements. Pixel processing does not invent `4` or `8`
events to advance a consumer.

Batch continuation is distinct from initial `10/08` admission. The original
helper VA `1036a49a` writes only `10=1`, independently of whether descriptor
replacement succeeded. The model tracks writes to each of `310..31c` and
`320..32c` independently while awaiting input. A subsequent `10` kick checks
only the selected group's four-bit refill mask and may reuse its unchanged ring
(refill mask zero), or accept all four republished addresses (mask `f`). A
partial update, masks `1..e`, remains paused. Neither the fourth address write
nor W1C alone resumes processing. The kick must also observe acknowledged
pending bits, no active job and no stop in progress. It retains the logical
request, next phase, additive result, consumed generations and eligible numerical
history; it clears the batch seen flags and only the selected refill mask.
Staged partial refills of the other group survive this operation. A changed
`fc` after any successful phase and before the four-read pause rejects without
changing selection, preventing a batch from splicing two groups. Selection at
an empty-batch boundary remains subject to real RAW freshness and full preflight.

Retained-ring continuation is a bounded functional choice: the recovered call
path does not establish a hardware requirement to rewrite a stable ring after
every four reads. Reusing addresses does not replay old pixels. Every selected
slot still requires a newer completed RAW generation, and every admitted job
repeats whole-batch profile, memory and overlap preflight.

VA `10268d10` can also issue that kick after a failed four-descriptor dequeue.
If continuation conditions are incomplete, the kick is accepted without
advancing a phase, replaying pixels, resetting the request or producing an
event. Such a kick is not latched as permission to resume after later address
writes or acknowledgements: another kick must observe the complete conditions.
A duplicate `10` in the initial armed state, `08` while already awaiting input
and malformed control values remain errors.

Read-only diagnostic properties count successful frames (`completed`), rejected
attempts (`rejected`), cancelled active jobs (`cancelled`), peer-conflict attempts
(`blocked`) and successfully written active bytes (`bytes`). They are cumulative
device-lifetime diagnostics retained across device reset. Per successful frame,
the byte count adds `OW*OH*3/2 + ceil(W/2)*ceil(H/2)*4 + 3060`, excluding padding.
These counters do not measure physical frame rate or electrical throughput.
The read-only QOM string `runtime` reports each bank's request enum
(`0` quiescent, `1` armed, `2` awaiting input, `3` drain pending), active timer
job, phase, batch/refill masks, pause/publication flags and consumed generations,
plus the four actual RAW slots and source epoch. Unlike MMIO `18`, its `active`
field describes an admitted timer job. Reading it performs no work or events.

The original repeated worker's early `42190804` notification is software:
`102e4724` sends it after admission, before waiting for results. This device
does not manufacture that callback. Its IRQ pending bits instead traverse the
original IRQ99/100 handlers and callback consumers.

The supported stop sequence is `84=0`, `f0=fc400`, then `54=1`, `60=1`, `6c=1`,
`78=1` in order while a request exists. The first step cancels owned scheduled
work. Only after all stop commands are accepted does the bank become quiescent
and publish aggregate stop detail `014a0080`. Misordered or malformed stop
commands reject. Stop acknowledgement is separate from a successful frame.

A changed retained configuration word cancels an active job, except for inactive
RAW/grid/main address staging and the dedicated `0c`, `f0` and `84` controls.
It also invalidates its bank's
numerical-history ownership unless it belongs to the main-only noise group
`df4..e10`, RAW pointer groups `218..234/310..32c`, or main/grid target pointers
`488..4c4/398..3b4`, or bounded selection `fc`. These target/source-address,
selection and noise changes preserve pre-color work history; actual source-epoch
withdrawal still prevents old history from being reused. First publication of a table/curve word also changes its
coverage state and cancels work even when its value is zero. Rewriting an
already covered word with the same value preserves work. These writes
preserve already published pixels and pending results; they do not synthesize
a replacement completion. Device reset preserves external GPIO levels and
does not turn a retained high level into a new rising event.

## Cycle-selected noise records

MAS VA `102e563a` passes configuration index 40 plus 188 bytes to `1036c862`.
That setter consumes a complete 40-byte packet: three 12-byte records followed
by one 32-bit word. A separate configuration pointer at software byte offset 200
can reach the same setter. The source establishes these exact transfers:

| Input fields | Bank-relative output | Packing |
| --- | --- | --- |
| Records 0/1/2, byte offset 0 | `df8`, `dfc`, `e00` | Each full 32-bit word copied unchanged |
| Records 0/1/2, byte offset 4 | `e0c` | Three unsigned 16-bit values shifted by 16/8/0 and ORed; values above 255 overlap |
| Records 0/1/2, byte offsets 6/7/8 | `df4`, `e04`, `e08` | Each byte triplet packed at shifts 16/8/0 |
| Final word at packet byte offset 36 | `e10` | Rotate right by 16 |

In the observed original continuation, index 21 at VA `b3c87c74` minus 696
and index 40 at VA `b3c87d3c` minus 896 identify the same caller block
`b3c879bc`. The 40-byte packet is therefore at VA `b3c87df8`. Its first
three operands change from `f8200000/e8040002/e8080004` to
`f8040000/e8040001/e8040002` after the first four computed phases.

The actual producer is VA `10364bd8`, called through `103650aa` after
`102d3874` binds the output block. VA `103650c0` reads the low six bits of a
counter at `*(context+236)+468`, then advances it modulo 64 on every call.
The selected 48-byte row at ROM VA `11b07e9c + 48*index` supplies words 6..8
to this packet, and words 9..11 to a second parameter packet. Thus the
operand change is a deliberate cyclic sequence, independent of RAW intensity.
A root configuration flag at byte 150 can instead supply explicit operand
words. The model does not replace this producer or its counter.
The second packet, at caller byte 1184, is bound through configuration index
50 and reaches the same setter in the selected bank-1 branch. Both banks use
the same generic numerical interpretation; it is not a second opcode family.

The 384 operands in that table contain 64 unique masks. Each passes the same
algebraic test: for `p(x) = x^32 + bitreverse32(mask)`,
`x^(2^32-1) mod p = 1`, and `x^((2^32-1)/q) mod p != 1` for every prime
factor `q` in `3, 5, 17, 257, 65537`. This identifies primitive degree-32
polynomials, strongly supporting a Galois LFSR interpretation rather than
independent floating-point coefficients or per-record opcodes. The native
operator performs this mathematical validation, without a mask whitelist.

Scalar generation is also source-established. Four selected parameter
families each contain 19 rows of 14 unsigned halfwords. Their inspected rows
all contain `[1,1,1,4,4,4,0,0,0,1020,1020,14,21,21]`. The producer interpolates
adjacent rows with denominator 6 and nearest rounding. The first nine fields
supply record bytes 6, 7 and 8; fields 9/10 supply the final two halfwords.
Fields 11..13 are multiplied by a separate per-channel gain-table byte,
divided by 128 with nearest rounding and saturated to an unsigned byte before
being stored at record byte 4. Configuration byte 1 equal to 2 or 7 forces
record 1/2 byte 6 to zero. A separate fixed-scalar path uses `4/8/8` in the
three byte-4 fields, while retaining byte-6 values of one. It is not a proven
hardware bypass.

The finite numerical interpretation names record bytes 6/4/7/8
`enabled/gain/shift/offset`, and the final halfwords negative/positive limits.
Those names, unlike the packet construction above, are hypotheses. The model
uses three independent unsigned 32-bit states initialized to one per job:

```text
state = (state >> 1) XOR ((0 - (state & 1)) AND feedback)
r = clamp((state >> 21) - 1024, -negative_limit, positive_limit)
d = round_away_from_zero(r * gain / 1024)
d = round_away_from_zero((d + offset) / 2^shift)
RGB12[channel] = clamp12(RGB12[channel] + d)
```

This operation runs after RGB gain and before matrix A. Enabled channels step
once per main output pixel, in 2×2 chroma-block, row-within-block,
column-within-block order. Disabled channels neither step nor alter pixels.
Limits are unsigned 10-bit values; gain/offset are unsigned bytes, shift is
0..15 and enabled is boolean. A nonzero feedback must be primitive even for a
disabled channel; a zero feedback is permitted only while disabled. Top bytes
of `df4/e04/e08/e0c` are reserved and reject when nonzero. Work and grid data
remain pre-color results. With the observed shift 4 and zero offset, gain 255
bounds the added term to about 16 RAW12 units; other shifts/offsets have their
explicit configuration-dependent bound.

Seed, stepping order, bit slice, normalization, stage placement and limit
semantics remain functional assumptions, not recovered physical arithmetic.
Every polynomial cycle is computed through the same operator. Noise state is
job-local: a cancelled or rejected job cannot advance another job's state,
publish pixels or produce completion. Changing this group cancels in-flight
work but preserves already established pre-color history. No physical-camera
capture is required to execute this declared model; such evidence could
distinguish it from the actual hardware algorithm.

## Failure and validation scope

Missing calibration, partial opaque-table or curve coverage, an omitted nonzero
opaque table, nonzero curve padding, unsupported control/gain/matrix encodings,
wrong RAW format, inconsistent geometry, bad pitch, RAM holes, readonly memory,
address wrap and forbidden overlap reject without publishing completion or
consuming input freshness. An already pending result is backpressure, not an
excuse to overwrite its outputs. A failed RAW producer publishes no fresh frame.
The completion path repeats memory/alias preflight before output DMA.

Numerical validation occurs before writing guest outputs. If an actual output
DMA fails after earlier rows were written, those writes are not rolled back;
the job still publishes no completion, consumes no generation and establishes
no history. This distinction prevents a partial memory transfer from becoming
a successful frame. Unknown MMIO accesses and commands remain explicit errors.

`tests/unit/nikon/pairedpixels.c` contains arithmetic fixtures for constants,
history, impulse/spatial filtering, crop/grid geometry, gains, curves, matrices
and invalid inputs. The paired cases in `tests/qtest/nikon/cmos.c` exercise actual RAW DMA,
all three target families, phase aliases, table publication boundaries, memory preflight,
backpressure, retained results, matrix encoding, cancellation/reset and source
history epochs. These synthetic fixtures test the declared model. Their verdicts
and unchanged-firmware execution receipts belong to the accepted validation
record. The [pixel receipt](/evidence/imagerequest/pixels.json) records the
accepted 51 CMOS, 23 SLA and 8 numerical unit subtests, including retained-ring
continuation, real non-unit color results and same-value publication
cancellation. Native fixtures do not establish complete startup. Effective RAM
backing-alias guards were independently reviewed; the present qtest board has
no decoder seam to inject a second alias, so that case is not claimed as an
executed alias fixture.

The later [noise and selected-group receipt](/evidence/imagerequest/noise.json)
records 54 CMOS and 10 numerical subtests, alongside 23 SLA cases. It adds
independent hard-coded LFSR output goldens, all three color channels, a valid
primitive polynomial outside the inspected table, cancellation/reset and
malformed feedback/scalar fields. Real RAW tests switch between independent
source/main/grid groups, retain the shared work surfaces, reject a mid-batch
selection change and preserve staged refill/history correctly.

In an unchanged 120-host-second cold-neutral run with explicit PGM, paired
profile and media, before the later signal-level correction, sequential QMP
observations at 30/60/90/120 seconds count
50/104/120/132 successful jobs. Actual payload reaches 351,299,520 bytes and
result `118` reaches 139,450,080 main pixels, with no paired rejection or
cancellation. The RAW generations keep increasing through sampled four-read
pauses; those pauses are normal backpressure, not evidence of a frozen worker.
Head1 binds a 1024×682 status GUI; the prior SD warning is replaced by the
original absent/non-CPU-lens notice. Head0 and normal live view remain
unverified. All graph ports are inactive at the snapshot, so parameter
publication and GUI readiness zero do not justify inventing downstream work.

The profiles must remain distinct. With corrected signal levels and slot0
omitted, a separate 120-second run stays at 12 completed jobs while fresh RAW
continues: bank0 waits at the four-read pause without a new continuation kick.
This is an unresolved liveness boundary, not permission to clear the pause.
With slot0 explicitly inserted and the complete storage error-consumer model,
the later 60-second run reaches 108 paired jobs, 287,426,880 payload bytes and
114,095,520 main pixels while the original NVMe driver reads its first sector.
The [storage receipt](/evidence/storage/presence.json) records that integrated
result. Neither one progressing window nor a status console establishes stable
normal live view across all input profiles.
