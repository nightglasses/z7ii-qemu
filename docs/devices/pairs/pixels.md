# Paired pixel processing

The Z7II paired model consumes completed RAW12 DMA buffers and produces main
Y/UV pixels and work history/residual planes before publishing
completion. It supports an explicit virtual calibration profile with mono or
[CFA color input](color.md) for C1.70 MAS. The address joins and software request paths are source-backed;
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
| `10364894`, `103646fe`, `102f734e`, `1036b35a` | Construct and publish four Q9 gain/black-offset/pedestal lanes and a shared bound at `708..720` |
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
and temporal filtering, then applies four-lane affine color correction,
up to two Q10 gain curves, independent
Q8 RGB gains, bounded LFSR noise, A Q10/B Q9 conversion and
[signed-chroma C processing](post.md), bounded MAIN processing and a
[completely published uniform luma gain](luma.md) to the main image. Its work
planes are half-resolution luma and residual. An enabled grid is a separately
produced packed12 confidence-map input, never a paired output.
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
0c, 84, 88, 8c, f0
40, 44, 48, 4c, 50, 94, 98, 9c, a0, a8
b8, bc, c0, c4, c8, cc, d4, d8, dc, e0, e4
f8, fc, 100, 104, 108, 10c, 110, 11c, 120, 124, 128
12c, 130, 134, 138, 13c, 140, 144, 204, 300, 3d8, 420, 424
218..234, 30c..32c, 394..3b4, 400..410, 428..470, 488..4c8
594, 598..5dc, 608, 660..678, 708..778, b00..b10, b64..b74, b94..ba4
1200..1244
900..9c4, 9cc, 9d0, 9d4, 9d8, df4..e10, 2890..2c94
b3c..b4c, b58, bac, d28, dd4, dd8
1900..197c
1a08..1a8c
```

Every reserved or inaccessible file word must also be zero. A wrong length,
unreadable file, nonzero interpreted slot or nonzero reserved slot fails device
realization. The file is a declared virtual profile, not a recovered
physical-camera calibration dump.

The fixed virtual lens selects a different complete opaque tuple from the
earlier absent-endpoint profile. The historical [lens receipt](/evidence/lens/session.json)
stops after eight jobs when `70c` changes. The live affine operator below
replaces exact matching for `708..720`; these file slots must now be zero.
Controls `700/704` have the independent affine/mode contract below; their input
file slots must also be zero, and the guest supplies them through MMIO. The complete
`724..778` groups now have separate [radial](radial.md), [detail](detail.md) and
[axial filter](adaptive.md) contracts. Their interpreted file slots must be
zero. A different numeric gain never authorizes unrelated mode bits,
coordinate groups or coefficients.

The live [uniform luma tables](luma.md) and [saturation curves](saturation.md)
have their own ordered-publication and frozen-job contracts. Their slots must
be zero in the calibration file; source table writes supply the actual values.

## Four-lane color correction

**Source facts.** C1.70 MAS VA `102e49fe` publishes config[13], root byte52,
through `1036b322`, `1036b35a` and related setters. It consumes at least109
bytes at pointer P. The relevant fields are:

| Register | High half | Low half |
| --- | --- | --- |
| `708` | gain at P+6 | gain at P+12 |
| `70c` | gain at P+18 | gain at P+24 |
| `710` | offset at P+8, signed13 | offset at P+14, signed13 |
| `714` | offset at P+20, signed13 | offset at P+26, signed13 |
| `718` | pedestal at P+10 | pedestal at P+16 |
| `71c` | pedestal at P+22 | pedestal at P+28 |
| `720` | zero | shared bound at P+30 |

Gain and pedestal fields are unsigned16-bit containers. VA `10364894`
constructs three gains using rounded division by512 and saturation at16383;
`103646fe` writes them to compact offsets388/394/400/406, duplicating the
middle gain. It writes the negative input black level to offsets390/396/402/408.
VA `102bf33c` selects compact+382 as P. Thus the changing low half of `70c`
is a normal calculated gain, not an incomplete static configuration.

VA `10363a6c` selects interpolation tables. VA `10363bc0` produces four
pedestals in context[82]+40/42/44/46; all304 selectable rows contain300 for
these fields. Its context[82]+48 bound is:

```text
min(32767, round(((16383 - black_level) * shared_scale
                 + 64 * sum(four_pedestals)) / 256))
```

VA `102f78b0` calls that producer and then `102f734e`, which copies these
five outputs to compact+392/398/404/410/412. This closes their relationship
to `718/71c/720`. The formula supports an additive pedestal and an upper
bound interpretation; it does not establish the physical per-pixel order.

VA `102f8812` fixes P[0]=1, producing bit8 of `700`. VA `102f8ba6`
constructs two mutually exclusive mode booleans, published by `102f88c8`
as bit0 of `700/704`. The ordinary producer emits all three states:
`101/0` when either source flag is enabled and the ROM selector permits it;
`100/0` when both flags are clear under that selector; and `100/1` when
disabled or excluded by the selector. Bit1 fields and other control bits are
zero. Gain, offset, pedestal and bound production is independent of these
booleans, so the numerical affine stage follows bit8 in all three states.
The two mutually exclusive low bits also select radial polynomial/curve
production; they are not treated as affine enable or recovered white balance.

Without an explicit polynomial override, mode00 zeroes both optional curves
while retaining their head coefficients; mode01 also zeroes the heads. The
override path supplies a separate complete packet, so curve contents cannot
generally be inferred from one mode bit. The existing zero-optional-table
profile and bounded head-coefficient arithmetic remain required. Unknown bits,
simultaneous modes and low-bit controls without affine bit8 reject. Entirely
zero controls and numeric fields are a separate
synthetic neutral convention used by standalone fixtures.

**Finite functional hypothesis.** For each filtered monochrome RAW12 sample,
the model computes four lanes:

```text
lane[i] = clamp(round((mono + signed13_offset[i]) * gain[i] / 512)
                + pedestal[i], 0, min(shared_bound, 4095))
R = lane[0]; G = round((lane[1] + lane[2]) / 2); B = lane[3]
```

Signed rounding is nearest with ties away from zero. Offset-before-gain,
direct RAW12 units, lane-to-RGB order and placement before the two curves
are declared choices. Neither the actual Bayer phase nor the physical
RAW14 arithmetic is recovered. This monochrome source does not establish
color-sensor demosaicing. The existing Q8 RGB gains, noise and matrices follow
the curves. Work/grid/residual planes remain pre-color; a complete affine
upload supplies future work and preserves the current immutable job and history.
Reset clears the configuration and cancels pending work through the existing
device lifecycle. No completion is posted without successful RAW consumption,
numerical processing and all output writes.

The native vectors use unequal gains, signed offsets, unequal pedestals,
clipping and subsequent curves; qtests check actual DMA pixels, padding,
result flags, retained writes, numerical publication, reset and negative
encodings. [Affine evidence](/evidence/paired/affine.json) separates those
fixtures from the unchanged-firmware observation.

The [adaptive filter](filter.md) now interprets the complete dynamic packet at
`594`, `598..5dc` and `608`. Its source-established interpolation and packing
are separate from the declared bounded numerical equations. Every interpreted
file slot must be zero; complete guest publication supplies the actual values.
The [capture-mode contract](modes.md) separately defines continuous/chunked
scheduling and independently enabled WORK output. Neither extension permits
opaque calibration changes or supplies a missing MAIN stream consumer.

The RAW ring addresses are live descriptor fields. Source VA `102bec2e`
publishes descriptor byte 52 into either four slots at `218..224` or
`228..234`, through `1036a56c`. VA `102beb6a` publishes the same descriptors
through `1036a672`, at `310..31c` or `320..32c`. The shared software selection
byte also reaches root configuration byte 39 and bank register `fc` through
`102c0304` and `1036a3da`. The finite model interprets `fc=0/1` as selection of
the primary/secondary address group, retaining four phases per batch. Source
root bytes 42/43 separately produce `108=3` and mode-dependent `10c`; a second group does
not establish eight independent processing phases.

For the selected group, all four front RAW addresses must equal their back
counterparts: `218..224/310..31c` for group 0, or `228..234/320..32c` for group
1. Each group may name independent buffers. Actual completed RAW generation,
RAM coverage, backing aliases and overlap are checked through the selected
addresses before DMA. A group switch does not create a new input generation
or allow replay. Third/fourth back groups `330..34c` remain zero, and `fc>1`
rejects. Inactive RAW/grid/main staging is retained without executing it or
cancelling active work. Its complete memory/profile checks apply when selected.

The source descriptor builders at MAS VA `102bec2e/102beb6a` and publishers
`1036a56c/1036a672` copy the addresses without a uniqueness assertion. That
alone does not establish hardware alias behavior. The finite model permits
different sequential phases to name exactly the same PA and complete RAW span:
an owned job reads only its current phase, and each later use still requires
a genuinely newer completed generation from the actual physical RAW slot.
ACKs, kicks and elapsed time cannot replay that generation. Current and future
phase checks still reject partial input overlaps, different-PA backing aliases,
input/output conflicts, missing RAM and peer conflicts. Complete front/back
publication remains required. This temporal read/read reuse is a declared,
falsifiable ownership hypothesis, not an address exception or hardware proof.

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
nor luma with residual. Enabled work groups remain required mirrors because the inspected
producer fills both from the same descriptors; independent work
history surfaces are not inferred. Disabled WORK planes have no DMA or history
access and do not cancel active MAIN work when their unused pointers change.
Every enabled selected plane receives full span,
RAM/backing and overlap checks. Aliases do not generate additional bytes, phases
or events. Independent selection is a functional interpretation of the shared
software selector, not a physical register specification.
The gap `3b8..3c0`, independent pointer `3c8` and status words `3cc..3d4` are
not extra grid rings and are not generalized by this contract.

For processing, every accessible opaque configuration word must equal the
selected file word, including low fields such as `304`, `308` and `414`.
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
| `11c`, `120`, `124`, `128`, `12c`, `130`, `134`, `138`, `13c`, `140`, `144` | `1`, `1`, `1`, `0`, grid enable0/1, `0`, `0`, `0`, `0`, `0`, `0` |
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

The device requires `1 <= W,H <= 4096`, positive even main
dimensions and a main rectangle contained in the ROI. The completed RAW frame
must have an even width, dimensions at most 4096, bit depth 12, enough pixels
for the ROI and a stride equal to `30c`, at least `frame_width*12/8` bytes.
Packed input samples use consecutive low-order 12-bit fields in the byte
stream. The device snapshots the ROI from actual guest RAM before scheduling
processing; later producer overwrites cannot change that snapshot.

Phase `p` starts at zero and advances modulo four only after successful output.
The next input must match `0x310+0x10*fc+4p` and have a generation greater than that
producer slot's last consumed generation. The same phase selects these ports:

| Output | Address | Stride | Active row bytes and rows |
| --- | --- | --- | --- |
| Main Y | `0x488+0x20*fc+4p` | `4c8` | `OW`, `OH` |
| Main UV | `0x498+0x20*fc+4p` | `4c8` | `OW`, `OH/2` |
| Work luma | `0x430+0x20*fc+4p` | `470` | `2*ceil(W/2)`, `ceil(H/2)` |
| Work residual | `0x440+0x20*fc+4p` | `470` | Same as work luma |
| Grid input, when `12c=1` | `0x398+0x10*fc+4p` | `394` | `3*GW/2`, `GH` |

The work address aliases at `450+4p` and `460+4p` must equal the selected luma
and residual addresses. UV must immediately follow the full pitched Y extent;
residual must immediately follow the full pitched work-luma extent.

Every target is four-byte aligned, has stride at least its active row size and
fits writable direct RAM without 32-bit address wrap. The full pitched extents
of all four phases are preflighted, including RAW and grid buffers not yet consumed.
RAW phases cannot overlap. Targets cannot overlap any RAW extent or another
target plane. Exact same-plane aliases across phases are allowed for main and
work outputs. Grid reads may alias other reads; every grid extent must remain
disjoint from every selected-phase output. Overlap checks include shared
RAM backing, so different PA aliases cannot bypass these rules. A peer bank's
active job or unacknowledged `1000` result also protects its target extents.

Only active row bytes are written. Row padding and unused pool tails retain
their previous contents. In the C1.70 address join, input phases select kind16
units in order 1/2/3/0; kind31 work and kind26 main select two alternating units,
while kind25 grid selects four distinct units. Their source-declared allocation
sizes do not become inferred active image dimensions.

### Confidence-map input

C1.70 MAS VA `102643e2` supplies unit8's kind25 descriptors to
`102cd202`/`102cd176`, which enqueue them through registry queue+144. Paired
VA `1026405a` consumes that same queue through `102cd2ba`. This establishes the
read direction at `398..3b4`; the former paired60×34 area-mean output is removed.
The observed `b48.high16/low16` maximum indices59/39 imply a60×40 map. The model
decodes variable `GW/GH` as those fields plus one, bounded to4096 with an even
width for packed12. Pitch comes from `394`, independently of RAW dimensions.
Grid RAM may be readonly; output RAM must be writable.

The public `nikon_paired_grid_frame` descriptor carries address, dimensions,
pitch and a completed generation. Positive publication is permitted only after
all producer DMA; generation zero withdraws that address before overwrite, and
NULL invalidates the producer epoch. The device retains at most eight address
descriptors, sufficient for two four-phase views. Slots remain stable within
an epoch, including after withdrawal; a ninth distinct address rejects until
NULL starts a new pool epoch. Per-bank address and epoch consumption prevents
replay. A matching fresh descriptor is required
before taking either the grid or RAW snapshot. Nonzero RAM is never readiness.
Withdrawal after admission leaves the owned immutable copy valid.

The board's `grid-ready` input queries the linked gate's published descriptor
through `nikon_gate_plane_output`. It passes that descriptor to the same public
API; it never sets a paired pending cause. Reset invalidates retained descriptors.
Final preflight again rejects aliases between grid reads and any output writes,
including shared backing aliases and protected peer-bank outputs.

The local coordinates below are a declared virtual interpretation of source
producer VA `102be6b0` and setter VA `1036bcde`, including their factor16:

```text
gx_Q22 = b3c + 16 * (RAW_X - b4c.high16) * b44.high16
gy_Q22 = b40 + 16 * (RAW_Y - b4c.low16)  * b44.low16
```

`RAW_X/Y` include the source ROI origin and MAIN crop. The starts are unsigned
Q22 and the unsigned16 steps have effective Q18 scale. Clamp each coordinate to
the grid edge, then bilinearly interpolate its four actual twelve-bit samples
with one final nearest rounding. Grid dimensions, starts, steps and origins are
live bounded numerical fields. The remaining `b38/b50/b54` controls must match
the complete explicit calibration tuple; no unknown control variant is bypassed.
The scalar map modulates the existing B58 residual exactly once as specified in
the [post-color contract](post.md). Its confidence meaning and coordinate scale
are functional hypotheses, not recovered silicon arithmetic.

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
respectively. Disabled channels preserve the preceding RGB sample. The source
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
is enabled and yields zero color output; omission does not mean a zero gain.

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
4. **Grid input.** Snapshot and decode the independently completed confidence
   map described below. It does not alter WORK/history or produce a paired DMA.
5. **Main color, curves and gains.** Apply the four-lane affine correction above
   to each cropped mono sample, or replicate mono into RGB for the synthetic
   neutral packet. For each resulting channel sample `m`, process enabled
   curve 0 then enabled curve 1. At each stage, set `i=m>>4`, `f=m&15` and
   `gain=(knot[i]*(16-f)+knot[i+1]*f+8)/16`. Replace `m` with
   `clamp12(round(m*gain/1024))` before the next curve, so the second stage
   indexes the first stage's result. Then replace each enabled channel with
   `clamp12(round(m*channel_gain/256))`.
6. **Noise.** Apply the bounded three-channel LFSR operation defined below.
7. **Main matrix conversion.** Apply A in RGB with Q10 division, then B with Q9
   division. A clamps to `[0,4095]`; B clamps luminance to that range and
   signed chroma to `[-2048,2047]`. C acts on
   signed chroma after B, preserving luminance, and the complementary BAC
   weights blend the original and transformed chroma. Each division rounds
   symmetrically to nearest, with ties away from zero. Add2048 to the chroma
   components and clamp before the bounded MAIN [post-processing](post.md).
   Convert to eight bits with `min((value+8)/16,255)`. Main Y stores every pixel;
   UV stores the rounded mean of each 2-by-2 block's U and V, interleaved U first
   (NV12).

Curves and RGB gains affect main color output only. Work luma/residual are
computed from the filtered image before these stages. Curve and gain
multiplications use the same symmetric nearest rounding and 12-bit clamp as
the matrix stages; their stage order is part of this finite operator, not a
claim about the physical processing pipeline.

A and C contain signed 13-bit coefficients; B contains signed 12-bit
coefficients. A/B pack coefficient 0 in the first word, then high/low halfword
pairs. C uses low/high pairs. A's first word permits only its coefficient bits;
B/C's first word must be the full sign extension of coefficient 0. Remaining
words reject bits outside the two coefficient fields. C additionally requires
the source-shaped identity first row/column; its remaining2×2 coefficients act
on chroma. The former RGB-domain C interpretation is removed. The fixed Q10/Q9 scales,
matrix order and saturation are functional interpretations of the recovered
coefficient layouts, not a claim of identical physical arithmetic.

Work luma and residual are little-endian 16-bit words holding RAW12-range
values. Input grid pairs `a,b` are packed into three bytes:
`a.low8`, `(a>>8) | ((b&15)<<4)`, `b>>4`. Thus each 60-sample grid row contains
90 bytes. Paired reads those bytes without modifying payload or padding.
NV12, these work meanings and confidence modulation are explicit finite
formats; the full physical meaning of work mode `42c=3` remains unresolved.

Prior work RAM is trusted only after this bank successfully published that
same physical luma/residual address pair in the current source epoch. Merely
nonzero RAM, another bank's output or a new calibration configuration cannot
seed history. Reusing alternating phase aliases can reuse such established
history; a source epoch change makes the old contents numerically ineligible.

The implementation preserves the complete scalar law while omitting proven
identities. Zero-strength refinement, unity curves/gains and exact A/C identity
are qualified once per job; noise still steps for every enabled channel.
Integer CFA interpolation reduces the generic Q17 expression to exact one-,
two- or four-tap averages with one final rounding. Zero neighborhood residuals
contribute no correction. For nonzero color taps, the validated threshold is
at most2360. A small reciprocal table uses `m=ceil(2^32/d)`: for unsigned
`n<2^32`, `(n*m)>>32` can overestimate `floor(n/d)` by at most one. Comparing
`q*d>n` corrects that possible excess before restoring signed nearest-away
rounding. The rounding numerator fits21 bits. Independent fixtures continue
to execute full scalar divisions, filters, bilinear sampling and color stages;
these optimizations do not widen the numerical profile or change DMA/events.

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
| Matching fresh RAW, valid whole profile, no pending result or peer conflict | Snapshot input/history and submit one host computation after 1 ms of virtual time |
| Successful operator and every output DMA row | Consume that generation, record history, advance phase, add `OW*OH` to `118`, publish `1002` |
| All four phases successfully read | Publish the functional `400` four-read watermark, reset the traversal mask and retain cyclic admission |
| `fc=0/1` | Retain a staged view selection; preserve an owned snapshot and its phase/count state |
| Any live front/back RAW address write while awaiting input | Cancel an owned snapshot and stage a new address publication, including same-value writes |
| New-view RAW address write | Stage the next view without cancelling the current immutable job |
| `10=1` while awaiting input, with both complete staged address views published and no stop | Commit immediately if idle, or after the current job publishes or is cancelled |
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

The QEMU thread pool computes pixels and packs output bytes from a detached,
immutable job: decoded parameters, RAW samples and qualified history are copied
before submission. Workers never read guest RAM, access device state, perform
DMA or raise an IRQ. One aggregate job per bank bounds outstanding ownership,
including cancelled work that has not returned. Its fixed numerical stages use
at most twelve pool tasks at a time:

| Stage | Independent work | Join before |
| --- | --- | --- |
| `0` prepare | Validate immutable samples, allocate private outputs, scan exact noise seeds | Any pixel processing |
| `1` prefix | Twelve ordered passes, each with up to three row bands per CFA lattice or four MONO bands | Next prefix pass, then temporal history |
| `2` temporal | Disjoint bands of complete 2×2 blocks | Neighboring radial reads |
| `3` radial | CFA rows read one immutable mosaic and write distinct corrected pixels; MONO skips this stage | MAIN color |
| `4` color | Up to twelve even-row MAIN bands | Spatial post-processing |
| `5` post | Each of five ordered passes uses up to four row bands per active channel | Next pass, then UV averaging |
| `6` pack | Disjoint Y/UV bands and actual LE16 WORK bytes | Final result availability |

PREFIX gathers the four CFA lattices, then joins DPC, spatial, filter,
optional vertical refinement, denoise, detail, adaptive, two color blocks,
color tail and final scatter in that order. Each pass reads the complete
immutable preceding plane and writes disjoint rows. Neutral lanes keep their
input pointer. Scatter joins before releasing its private scratch. The MONO
path uses the same pass bodies without gather/scatter. Color-block reciprocal
tables are prepared once per job and shared read-only by its bands.

Each MAIN band starts with the three LFSR states reached by scanning exactly
its number of preceding pixels. Enabled channels alone step, and each band
retains the original 2×2, `dy`, `dx` traversal. No noise state is reset at a band
boundary. Each post band reads the complete immutable input plane for its
current pass and writes disjoint rows. All bands join before swapping the
active channel's input and scratch; neutral channels do not swap. This preserves
cross-band neighborhoods and all five scalar pass boundaries. The two dead RAW
intermediates become two channel scratch planes; only the third scratch needs
another MAIN-sized allocation. The synchronous
pixel interface composes these same stage bodies and copies results only after
the complete private operation succeeds.

Only main-AioContext callbacks submit the next fixed stage, after every child
of the current stage returns. Workers neither submit nor wait on other pool
work, so the same graph can run with a single pool worker. Final completion
schedules the existing virtual-clock publication; a paused virtual clock does
not publish an already computed result. The 1 ms interval is an earliest
submission time, not a promise of completion within 1 ms or a physical frame rate.

Before publication, the main thread checks the bank's cancellation token, phase
and unconsumed RAW generation, current whole-batch memory/overlap constraints,
peer ownership and downstream gate epoch/capacity. Only then can actual DMA and the
existing completion transition run. Source withdrawal still permits an immutable
snapshot to finish, but cannot establish history for a newer source epoch.
Cancellation or reset invalidates the task and releases logical ownership;
every submitted child retains the private aggregate until the last callback,
then discards its result without DMA, consumed-generation updates or completion events. Unrealize
disables admission and drains these callbacks before device resources are freed.
This separation removes long pixel computation from the BQL callback. It does
not by itself establish the cause of missed host controls or normal GUI startup.

A supported but occupied MAIN input can admit the next immutable paired
computation into that same single aggregate. It cannot admit a second aggregate.
When computation finishes, the complete memory, alias, peer and ownership
preflight still runs. BUSY retains the private ready result without DMA, RAW
consumption, history, phase movement or pending bits. A genuine gate input
release pulses an internal `input-capacity` GPIO, which schedules a virtual-clock
recheck after the current MMIO transaction. It is not a guest completion cause.
The paused clock cannot publish the retained result.

Normal successful MAIN consumption changes the gate generation but preserves
its input epoch. A waiting paired job can publish into the then-current free
generation of that same epoch. Stop, reset, committed input binding or topology
changes revoke the epoch; the old result fails the ordinary ownership check.
Malformed current memory or peer ownership also rejects, even while capacity
is occupied. This one-result precomputation and epoch law are explicit functional
pipeline hypotheses, not recovered hardware queue depth or latch timing.

Address publication is distinct from initial `10/08` admission. Both front
(`218..234`) and back (`310..32c`) views have four slots, with a separate
coverage mask per group. `fc` reads back the staged selection; the live selector
used by an admitted job changes only after a complete publication and `10`.
Changing `fc` to another live view starts a fresh eight-word publication for
that target. The existing job keeps its immutable input, parameters and outputs;
further admission waits for the new view to be committed. A same-value `fc`
write preserves staged coverage and any already accepted commit. Values above
one reject without changing state.

A write to a live RAW address still cancels an owned snapshot and starts a
barrier, including a same-value write. Writes to the next view do not cancel the
old job. An incomplete publication and its `10` kick do not commit; supplying
the missing word alone also does not commit. A later `10` is required. An
accepted commit waits only for the current job to publish or be cancelled,
not for the old four-phase traversal to wrap. Further RAW writes to its target
invalidate the queued commit and begin another publication requiring all eight
words and a new kick. No commit creates input, DMA or an event. Partial coverage
also survives initial `08`, so an incomplete initial publication cannot evade
this barrier by starting a request.

A commit preserves the bank-global phase, traversal mask, additive result,
pending results, consumed generations and eligible numerical history. The
`400` watermark counts four real sequential phase reads even if the live view
changes between them. C1.70 MAS VA `102637c2` consumes `400` as a refill latch,
without checking selector or group identity. VA `10268d16..10268d62` obtains
four descriptors, toggles its selection, calls `102c05ee` and issues `10`
without `08`. VA `102c0650..102c067a` writes `fc` through `1036a434` before
publishing the addresses. These calls establish live refill and selection;
deferring its latch to one job boundary and retaining the global phase/count
are finite functional choices. No source establishes that all four reads in a
watermark must use one selector. Whole-profile, memory, alias, peer and gate
checks still run before new work and before old-job publication. Changing a
staged selector cannot reinterpret that old job's output ownership.

The cyclic-ring interpretation replaces the earlier unconditional four-read
pause hypothesis. C1.70 MAS VA `102637c2` handles `400` by setting the retry
latch at VA `b18648c0`; it does not issue a stop. The action at VA `10268ba4`
calls `1026405a` for four descriptors. A zero return at `10268d20` preserves
that latch and skips RAW publication and its `10` kick. The nearby `10268d10`
sets a local flag after independent WORK publication; it is not an
unconditional kick after a failed RAW dequeue. A read-only breakpoint capture
observed this zero-return path after a real `1402` completion while RAW
production continued. Requiring a new kick after every four reads closed the
model's retry loop prematurely.

Continued reuse of an unchanged ring is an explicit functional hypothesis,
not a recovered physical interpretation of `400`. Every frame still requires
a newer successful RAW generation, acknowledged pending results and available
downstream capacity. No ACK, elapsed time or kick creates a frame. An incomplete
publication remains blocked until a later complete commit. Duplicate `10`
while initially armed, `08` while already awaiting input and malformed controls
remain errors.

Read-only diagnostic properties count successful frames (`completed`), rejected
attempts (`rejected`), cancelled active jobs (`cancelled`), peer-conflict attempts
(`blocked`) and successfully written active bytes (`bytes`). They are cumulative
device-lifetime diagnostics retained across device reset. Per successful frame,
the byte count adds `OW*OH*3/2 + ceil(W/2)*ceil(H/2)*4 + 3060`, excluding padding.
These counters do not measure physical frame rate or electrical throughput.
The read-only QOM string `runtime` reports each bank's request enum
(`0` quiescent, `1` armed, `2` awaiting input, `3` drain pending), active admitted
job, phase, traversal/publication masks, publication flags and consumed generations,
plus the four actual RAW slots, source epoch and `inflight` aggregate count.
Per-bank `compute` is `0` before host submission or after release, `1` while a
task is queued/running and `2` after its callback has returned a result for
virtual-clock publication. Unlike MMIO `18`, `active` describes an admitted job,
including its host computation. `stage` reports the fixed stage above (`7` done),
and `remaining` counts children awaiting their Aio callbacks. `postpass` reports
the current post pass; `prefixpass` reports the twelve-pass prefix cursor.
`capacity-wait` marks a computed result retained for the
real gate input, and `capacity-pending` marks its virtual recheck timer. Reading these fields
performs no work or events.
The same string distinguishes the live `selected` view from the `staged` `fc`
readback; `commit` is its queued target, or `-1` if no complete commit is pending.

The original repeated worker's early `42190804` notification is software:
`102e4724` sends it after admission, before waiting for results. This device
does not manufacture that callback. Its IRQ pending bits instead traverse the
original IRQ99/100 handlers and callback consumers.

The supported stop sequence is `84=0`, `f0=fc400`, then `54=1`, `60=1`, `6c=1`,
`78=1` in order while a request exists. The first step cancels owned scheduled
work. Only after all stop commands are accepted does the bank become quiescent
and publish aggregate stop detail `014a0080`. Misordered or malformed stop
commands reject. Stop acknowledgement is separate from a successful frame.
The first stop step and normal drain request discard a queued view commit and
return the staged `fc` readback to the live selector. A kick during the stop
sequence rejects. Reset clears both selectors to zero and invalidates any
queued commit before releasing old work.

A changed strict control, topology or uninterpreted profile word cancels an
active job. Staged `fc`, inactive RAW/grid/main pointers, dedicated request
controls and the source-confirmed numerical uploads below retain their separate
rules. RAW/main/grid pointer and selection changes preserve pre-color history;
source-epoch withdrawal still prevents obsolete history from being reused.
First publication of an opaque table word changes coverage and cancels work
even when its value is zero. Numerical tables instead follow their complete
publication contracts. Rewriting an already covered word equally preserves work.
These writes
preserve already published pixels and pending results; they do not synthesize
a replacement completion. Device reset preserves external GPIO levels and
does not turn a retained high level into a new rising event.

The later [EX/input receipt](/evidence/input/turnover.json) records the async
ownership change separately from unchanged-firmware progress. All 13 direct
paired lifecycle cases and 36 pixel arithmetic cases pass. A normal queued
input session observes both dials, naturally selects M/A/S/P, and continues
MAIN processing before a separate RGBA texture rejection. Tv/Av publication
and visible menus are not established by that session. The whole CMOS qtest
batch is not reported as passing: interrupted macOS runs expose a dummy-CPU
shutdown wait in upstream `pause_all_vcpus`. No upstream source or guest state
is changed to hide that validation limit.

### Online numerical uploads

The running-request path at MAS VA `102c04c6` rebuilds configuration through
`102c0b68`, calls `102e55fa/102e560e`, then `102e49fe` with its initialization
arguments zero. The path skips the initial-start branch. Its following setters
perform plain packing/stores without a stop, drain or hardware-completion wait:

| Packet | Setter VA | Bank-relative store order |
| --- | --- | --- |
| Noise | `1036c862` | `df4,df8,dfc,e00,e0c,e04,e08,e10` |
| RGB gain | `1036bb04` | `9d0,9d4,9cc` |
| Radial coefficients | `1036b3ee/1036b408` | `724,728,72c,730,734,738` |
| Denoise | `1036adac` | Seven words `660..678` |
| Adaptive spatial filter | `1036af16/1036af3a/1036b12a` | `594`, then three channels of six interleaved words at `598..5dc`, then `608` |
| MAIN affine | `1036b35a` | Seven words `708..720` |
| Detail | `1036b49c/1036b4de/1036b4fc/1036b51e/1036b554` | Eight words `73c..758` |
| Axial correction | `1036b576` | Eight words `75c..778` |
| Color filtering | `1036b63a/1036b830/1036b866/1036ba6a/1036baa8` | 51 words: `900..950,954,958,95c..9b0,9d8,9b4,9b8..9c4` |
| A and B matrices, independently | `1036bb78/1036beaa` | Five words each: `b00..b10` and `b64..b74` |
| C matrix, weights and tail | `1036c02c/1036c0c0/1036c12a/1036c094` | Eleven words `b94..ba4,bac,bb0..bbc,ba8` |
| Post smoothing | `1036c260` | 32 words `d20..d9c` |
| Post edge family | `1036c744/1036c7e2/1036c828/1036c7a6/1036c818` | Twelve words `dc0..dd0,ddc,de0,de8,dec,dd4,dd8,de4` |
| Confidence strength | `1036bcde` | `b58` only; preceding grid layout words retain strict cancellation |
| Two gain curves, independently | `1036bb68 →1036ca74` | 129 words each: `2890..2a90` and `2a94..2c94` |

Word ranges in this table include both ends and advance by four bytes. These
are 17 finite coverage groups. Existing uniform MAIN luma, saturation and optional
radial tables keep their specialized ordered-publication and dual-slot validity
checks under the same immutable-owner rule. The independent mode words
`700/704`, grid layout, `9c8`, `3c4.bit15` and uninterpreted config40 fields are
not numerical exemptions. No supported numerical value range is widened.

A later running kick is separate: `102c0530 →102e5488 →1036a49a`.
The noise producer deliberately advances its parameter index modulo 64.
Consequently a changed noise mask, RGB enable or radial coefficient is not
source evidence of a cancellation request. In a separate unchanged fixed-M
diagnostic, 33 actual active cancellations in 25.55 host seconds all came from
this former changed-word policy, predominantly `df8`, then `9cc` and `730`;
no explicit stop or live-RAW-address cancellation was recorded in that scope.
This diagnostic completed 41 jobs from 72 numerical computations. It is not
a controlled host-rate benchmark.

The finite model retains the current token, decoded parameters and private
RAW/history/grid until that job finishes. These packets qualify future
admission. The first store starts a fresh coverage round, every member is
required, and an incomplete round retains actual unconsumed input without
publishing completion. All groups must finish before another job is admitted;
ordinary full-profile validation still rejects malformed completed values.
Readback exposes actual staged words. A completed upload retries existing input
through the normal admission predicate, without a new frame or event.
Cold construction retains its existing qualification rules; an already opened
round continues to require every member across a drain/rearm boundary.

The completion-time whole-batch memory/alias check uses the owned whole
numerical configuration and re-decodes only current topology. It never invokes
future numerical/profile qualification; partially uploaded fields cannot reject
an otherwise completed old computation. Current addresses, RAM backing, pending
ownership and downstream generation are still checked. A partial round survives
drain into quiescent state and can finish there before rearming; stop/reset
discard its coverage and cancel owned work. Live-address invalidation retains
its separate cancellation rule.

Changed complete pre-color filter, denoise, detail, axial and color-filter
uploads invalidate future history qualification. A separate bank/job history
revision prevents an older task from restoring that history when it later
finishes. Abandoning a partial round cannot discard its changed-history state.
The old private input/history still computes and publishes normally. MAIN-only
uploads preserve pre-color history. Source epochs remain separate from parameter
revisions, and format/layout changes retain their strict cancellation boundary.

The per-job latch and whole-packet admission boundary are functional hypotheses,
not recovered silicon timing. They are falsifiable through old-output/new-output
fixtures, each missing member of the first three packets, first/middle/last
members of the remaining groups, curve bitmap boundaries, malformed staging,
reset, explicit stop and drain/rearm. This corrects the old numerical-update-aborts-work hypothesis;
it does not by itself prove sustained original-firmware retirement.

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
of `df4/e04/e08/e0c` are reserved and reject when nonzero. Work data remains
pre-color; grid input is independently owned. With shift4 and zero offset, gain255
bounds the added term to about 16 RAW12 units; other shifts/offsets have their
explicit configuration-dependent bound.

Seed, stepping order, bit slice, normalization, stage placement and limit
semantics remain functional assumptions, not recovered physical arithmetic.
Every polynomial cycle is computed through the same operator. Noise state is
job-local: a cancelled or rejected job cannot advance another job's state,
publish pixels or produce completion. A complete online upload supplies future
snapshots and preserves current work and pre-color history. No physical-camera
capture is required to execute this declared model; such evidence could
distinguish it from the actual hardware algorithm.

## Failure and validation scope

Missing calibration, partial opaque-table or curve coverage, an omitted nonzero
opaque table, nonzero curve padding, unsupported control/gain/matrix encodings,
wrong RAW format, inconsistent geometry, bad pitch, RAM holes, readonly outputs,
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
history, impulse/spatial filtering, crop geometry, confidence interpolation, gains, curves, matrices
and invalid inputs. The paired cases in `tests/qtest/nikon/cmos.c` exercise actual RAW DMA,
MAIN/WORK targets and grid inputs, phase aliases, table publication boundaries, memory preflight,
backpressure, retained results, matrix encoding, cancellation/reset and source
history epochs. These synthetic fixtures test the declared model. Their verdicts
and unchanged-firmware execution receipts belong to the accepted validation
record. The [pixel receipt](/evidence/imagerequest/pixels.json) records the
accepted 51 CMOS, 23 SLA and 8 numerical unit subtests, including retained-ring
continuation, real non-unit color results and same-value publication
cancellation. Native fixtures do not establish complete startup. Effective RAM
backing-alias guards were independently reviewed; the historical qtest board had
no decoder seam to inject a second alias. The new public-device grid fixture
adds a backing alias; its native verdict remains separately required.

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
cancellation. The RAW generations kept increasing through sampled four-read
pauses in that earlier model; those observations alone did not prove liveness.
Head1 binds a 1024×682 status GUI; the prior SD warning is replaced by the
original absent/non-CPU-lens notice. Head0 and normal live view remain
unverified. All graph ports are inactive at the snapshot, so parameter
publication and GUI readiness zero do not justify inventing downstream work.

The profiles must remain distinct. With corrected signal levels and slot0
omitted, a separate 120-second run stays at 12 completed jobs while fresh RAW
continues: bank0 waits at the four-read pause without a new continuation kick.
This was a liveness boundary in the earlier unconditional-pause model. The
source analysis above motivates the current bounded cyclic interpretation.
With slot0 explicitly inserted and the complete storage error-consumer model,
the later 60-second run reaches 108 paired jobs, 287,426,880 payload bytes and
114,095,520 main pixels while the original NVMe driver reads its first sector.
The [storage receipt](/evidence/storage/presence.json) records that integrated
result. Neither one progressing window nor a status console establishes stable
normal live view across all input profiles.

The grid-consumer fixtures additionally cover actual packed12 read ownership,
missing/withdrawn/repeated generations, epoch restart, readonly grid RAM,
backing aliases, immutable worker snapshots and zero/full confidence laws.
The numerical fixture's former pre-color output observations are now independent
scalar reference scratch, checked against actual half/residual and MAIN outputs.
No replacement intermediate-output API is provided. These new cases require
native integration results; they do not establish unchanged-firmware acceptance.
