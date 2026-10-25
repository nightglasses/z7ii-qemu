# Finite RGBA to YUV105 and scaled YUVA107 conversion

The original224/1736-byte pair now completes through IRQ129 with the same
unchanged bundle and explicit functional descriptor input. It reads a
separate1024-wide RGBA composition buffer, not the720x480 warning target.
The next pair has the same lengths and now also completes:720x480 RGBA is
scaled to1024x682 packed YUVA107. Its176/1552-byte warning-source SRC_OVER
continuation now completes too. The following bounded original run has no
graphics rejection or MAS data abort; the manager returns from warning
worker4 to worker0 with GUI readiness still unarmed.

Later normal-GUI execution also exercises an inherited temporary paint mode2.
The [mode overwrite model](../devices/transfer/mode.md) now accepts all four
source-backed0/2/4/6 combinations before exact final2 in both initialized
profiles and retained SRC_OVER. All20 transfer native subtests pass; the
[current frontier](../validation/frontier.md) records later original progress.

## Original command contract

All code/buffer addresses below are MAS VAs. Raw56/434-word commands are in
`run imagetransfer-20261023/`, observed at entry`106ee04a`,
LR`106eeab9`, descriptor`b545e79c`, command sources`bd0dff90/bd15ff90`.
Unlike the older small byte-LUT fixture, this is a full image transaction.

- Setup begins with identity, then a float32 path matrix with diagonal
  `(1,-1,1)` and y translation688. Inline signed32 words are
  `(0,0,1024,682,-1024)`, segments`02 07 09 07 00`, key10001. The transformed
  rectangle covers logical rows6..688. Opaque setup37 must echo draw421.
- Draw source155=`bf309600`, format156=`001b0100`, allocated source
  size152=`040002b0` (1024x688), active158=`040002aa` (1024x682).
- The fixed f24 sampling matrix is
  `(00350000,0,0,0,00b58064,003f0241,0,0,003f0000)`.
  It selects x directly and reverses/scales y at pixel centers. With logical
  y=`physical_y+6.5`, its y coefficients are exactly
  `m4=-98404/67108864`, `m5=66113/65536`; multiply the result by688 and floor.
  Over the682 physical rows, source indices remain in0..687.
- Destination format1a2=105, extent1a3=1024x688, Y base1a4=`cc18b000`,
  interleaved full-height UV base1b5=`cc235800`. Their delta is1024x682.
  Only682 rows per plane are stored; alignment rows are not extra DMA output.
- Mode184 low byte2 samples the image without paint modulation. Blend
  selector1a0=0 is SRC. Primary table upload is256 entries`entry[i]=i*16`;
  the secondary upload is the exact captured32-entry configuration.
  Conversion registers1c1..1c9 and all remaining state/headers/endings are
  fixed. Different table values are unsupported in this profile.

The original builder`106b55a2` calls`10646360→10646500` for destination
low format bits greater than4; the latter packs signed halfword coefficient
fields into the observed conversion registers. It is not an f24 matrix.
Read-only producer/decode receipts are in
`run yuvmodel-20261023/{yuvbuilder,yuvcoeff,nextcommands}.json`.

The fixed [Z7 source sampler and SRC blend](https://github.com/WonderLantern/z7-qemu/blob/97abf0b81cd3211f8374a38987a0beaf4549b90f/qemu/nikon-z7-gpu.h#L304-L439)
corroborates normalized f24 image sampling, direct memory rows and six-row
alignment. Its [YUV writer](https://github.com/WonderLantern/z7-qemu/blob/97abf0b81cd3211f8374a38987a0beaf4549b90f/qemu/nikon-z7-gpu.h#L244-L301)
uses limited-range BT.601 and averages per-pixel chroma across each horizontal
pair. That functional convention is used here, as with the existing YUV
clear; physical Nikon coefficient/filter/LUT precision is not established.
Accepting the captured table state does not implement arbitrary LUT programs.

## Finite implementation and safety

`nikon_transfer_convert_parse` accepts exactly56/434 words and the captured
geometry, sampler, format and table configuration. Variable fields are source
and destination addresses, matched class1 key and echoed nonzero opaque
operand, plus the overwritten temporary mode0/2/4/6 at draw word74. Exact final
mode2 and both mode-write headers remain checked. This profile initializes its
complete state and does not depend on
the previous720x480 context. Completion invalidates that different context.

All command/source/output spans must be directly backed, contiguous and
32-bit bounded. The source extent is2818048 bytes; both output extents are
698368 bytes. Guest-address overlaps and effective backing aliases involving
either destination are rejected, including planes at distinct addresses
sharing the same underlying RAM. The generalized alias check also retains
the earlier path/image protections.

At deferred execution, the full RGBA source is read via DMA. Alpha0 and255
are supported: transparent pixels become black for this SRC-to-no-alpha
transaction; opaque pixels use their submitted RGB. Partial alpha is an
explicit unsupported boundary. The fixed f24 row expression is evaluated
exactly with integers; each pair is converted into scratch Y/UV storage using
the declared BT.601 convention. Both full targets are mapped and checked
before either is written. Late failure, reset or unrealize yields no partial
output or successful completion. Only successful work raises IRQ129.

## YUV105 validation

`run yuvmodel-20261023/red.log` shows rejection of a valid synthetic
transaction on the preceding parser. `green.log` passes all11 native transfer
cases. The new fixture uses seven known RGB/YUV colors, differing adjacent
pixels and source rows, independently tabulated chroma, a floating-point
sampling oracle, transparent pixels, delayed source mutation, partial-alpha
rejection, bad LUT/sampler/format/key/geometry, wrong plane delta, overflow,
source/command overlaps, backing aliases, map loss, reset and cancellation.
It checks every output byte, guards and tail. Two earlier fixture compile
logs concern declaration order and a platform math-header enum collision;
they are not the device red/green receipt.

Independent read-only review verifies every fixed captured field, the682
integer source rows against the f24 expression, SRC alpha0/255 semantics,
chroma pairing and mapping/alias boundaries. In the separate original run,
the source has black RGB and remains byte-identical across processing.
Both complete output planes become Y16/UV128, with neighboring guards
unchanged, before original IRQ129 handler`106ee3ca`. `source.bin`, `y.bin`,
`uv.bin` and `render.json` retain the actual memory result.

## Scaled YUVA107 contract

The subsequent224/1736-byte pair changes source to
`bec5f200`, allocated/active720x480, inline coordinates to
`(0,0,720,480,-720)`, and uses path scales approximately1.4222/-1.4208.
Destination format becomes107 at`cb82b000/cb980000`, separated by
`2*1024*682` bytes. This is the packed Y0/Y1/U/V primary and separate alpha
family, with source filter/state selectors changing1→7 and slightly changed
f24 coefficients. All other fixed state and both LUTs remain the same.
The same parser now qualifies exactly these two complete profiles; mixed
fields, arbitrary sizes/transforms/LUTs and unsupported format values reject.

The107 source extent is1382400 bytes, primary output1396736 bytes and alpha
output698368 bytes. The target remains logical1024x688 with physical rows6..687.
The path matrix has exact float bits`3fb60b00/bfb5dd80/442bffff` for x scale,
y scale and y translation. Its rectangle reaches x1023.99169921875 and starts
at y6.00531005859375. Cairo rasterizes this same finite path on1024x688; the
first physical row and final column have partial coverage. Prior720x480
path callers explicitly retain their own canvas dimensions.

Sampler f24 words46/54 become`0034fff8/003efffe`. For physical x/y, exact
nearest source indices are evaluated without floating rounding:

```text
sx = floor(131064*(2*x+1)*720 / (2048*131070))
sy = floor((2*66113*1024-98404*(2*y+13))*480 / (1024*131070))
```

All1024 columns and682 rows stay within0..719 and0..479. Selector7 is accepted
as captured state; the functional nearest/clamp convention follows the fixed
Z7 renderer. It does not establish the physical filter kernel. The inspected
[Z8 pixel pipeline](https://github.com/YCX81/z8-qemu/blob/f56290e3f4d5ca8fe52e9528b5ea6a8e163a12dd/qemu/nikon-z9-pixels.h)
instead handles RAW/IMPRSZ; it is not evidence of this OpenVG sampler.

For107, source and existing destination alpha may have any8-bit value. The
model reads both old planes into scratch before processing. At each pixel,
`sa=src_alpha*coverage`, `da=dst_alpha*(255-coverage)`; straight RGB is the
rounded weighted sum divided by`sa+da`, or zero when that sum is zero. Output
alpha is`round((sa+da)/255)`. This is SRC with coverage, retaining old color
and alpha only where the path is partial. The old pair is decoded through
the declared BT.601 convention before blending, and the new pair averages
its two chroma samples. Both full writable mappings and backing independence
are checked before either copy. Source and neighboring memory are preserved.
Successful107 processing now retains this exact context for the continuation
below. This does not preserve arbitrary GPU register programs.

## YUVA107 validation and current stop

`run yuva107-20261023/red.log` records valid scaled-profile rejection
before implementation. `green-final.log` passes all12 native transfer cases.
The new case tests seven colors with0/64/128/255 alpha, an independent floating
sampler oracle, every packed/alpha output byte, transparent SRC edge retention,
white old-target/blue-source edge RGB mixing, delayed source mutation, bad
geometry/sampler/format/filter/LUT/plane layout/ranges, effective aliases,
map loss, reset and unrealize. `green-color.log` exposed an incorrect test
assumption that adding1 RGB adds1 Y; the tabulated Y oracle was corrected,
without changing device arithmetic. Independent read-only review checks all
captured fields, all sampler indices, layout, blend and safety boundaries.

The unchanged bundle and same functional descriptor input then reach original
IRQ129 handler`106ee3ca`. The actual1382400-byte source is all-zero transparent
RGBA and remains byte-identical. The full packed output contains698368 bytes
each of Y16 and UV128; all698368 alpha bytes are0. Both neighboring guards
remain unchanged. `render.json` and captured planes retain that result; it
does not imply the warning has yet been composited into these planes.

## YUVA107 SRC_OVER continuation

The next pair is **176/1552 bytes**,44/388 words, command sources
`bd0e0070/bd160660`, key10002. It samples the existing720x480 warning surface
`c175d600` using the same scaled sampler into the same107 output planes,
but changes blend selector1a0 to SRC_OVER and target1a5 to1, and omits the
full target geometry initialization. Both LUT configurations remain present.
This complete stream formerly fell through to the older LUT parser's fixed
224/1688-byte requirement and faulted as`transfer-stream-truncated`; the
captured bytes were never syntactically truncated. It is now a separate exact
profile, with setup/draw offsets12/45 relative to the full107 field schema.
The256-entry identity and32-entry secondary tables are resubmitted. Register
192 writes four words; the following`01963004` writes register196's low16
bits, retaining its previously established upper bits and register197.
The model validates that exact header/mask, not a generic shorter table upload.

The inherited geometry, clip, format upper bits and conversion state are
known from a successfully completed full107 profile. Explicit source/target
addresses, nonzero matched opaque operand and class1 key are revalidated.
Cold, pending/canceled/failed initialization cannot establish the context.
ACK preserves it; successful107 continuations retain it. Another completed
profile, rejected complete program, late execution failure, reset or unrealize
clears it, together with the incompatible720x480 path context.

For SRC_OVER, integer weights are`sa=As*coverage*255` and
`da=Ad*(65025-As*coverage)`. Straight RGB is the rounded weighted sum, alpha
is`round((sa+da)/65025)`. The SRC branch uses the same scale with
`da=Ad*255*(255-coverage)`, preserving its prior result. The maximum RGB
numerator including rounding is4,236,541,312, within32-bit unsigned range.
All previous full-span, backing-alias, deferred-read and atomic-write checks
also cover this continuation; unknown blend/sampler/LUT/mask state rejects.

`run srcover-20261024/red.log` records the previous model rejecting a
valid initialized continuation. `green-final.log` passes all13 native transfer
cases. The new case uses independently tabulated white-over-blue YUV/alpha
for source alpha0/64/128/255 and old alpha0/128/255, checks every output byte
with bounded edge-coverage tolerance, then tests repeated completion, cold/
failed/canceled/reset/incompatible initialization, rejection invalidation,
malformed fields, aliases, deferred mutation, late map loss, reset and unrealize.
An initial fixture helper-name compile failure is retained separately as
`fixturebuild.log`; it is not the behavioral red. Independent review verifies
the captured fields/masks, lifecycle and integer bounds.

The unchanged bundle and identical functional descriptor then submit this
profile and enter original IRQ129 handler`106ee3ca`. The actual source is the
previous battery warning, all alpha255, and is unchanged. Both output planes
and guards are captured. `pixels.py/json` independently compare all1,396,736
packed bytes with a floating sampler, declared BT.601 and pair chroma; alpha
differs by at most1 from analytic rectangle coverage. `composition.png` decodes
the actual packed/alpha memory rows and visibly contains the original warning;
no host text, font or UI painting participates.

## Following original execution boundary

The60-host-second completion/fault window reaches no new graphics error or
MAS data abort. The final manager is current0/target5/mode0/state2, versus
the preceding current4/mode15. A separate one-hardware-breakpoint-at-a-time
probe observes phase4 request`{0,0}` while current4, phase3 from worker4,
then phase2 from worker0. Both exit acknowledgements now complete. Worker0
re-enters its event loop at`10437a86`, wrapper`b181c55c`, mask`ffbbffff`,
timeout`ffffffff`; its first wait returns flags`10012`, so that particular
wait is not claimed to be blocked. `settle.json` retains the joined sequence.

`idle.json`/`idle-summary.json` extend this with a30-second return probe:
worker0 stays class1/mode0, stage2; after10012 it receives40000000 once and
10 repeatedly. GUI worker remains class4/mode0. `10437924` maps10 to callback
phase1 and always calls phase11; class1 router`10438ba2` sends phase11 to
policy`104387a6→10437d1c`. The subsequent [policy trace](policy.md) shows
that original ROM supplied power gate1 and previous state6, and original
cleanup/policy consumed them. Current main mode5, selected from an original
36-byte application record, takes its own branch and returns29. Restoring
the final0/0 snapshot is not a demonstrated repair. No worker-exit
acknowledgement remains pending in this window.

The final readiness gate and pending3 remain0, BatCk remains2, and panel
pointer remains`b7f54a00`. A snapshot of unrelated runnable CPU PCs is not a
new graphics failure or proof that the GUI has started. The remaining lead
is the worker0 policy/event route to GUI readiness, alongside the unresolved
natural battery peer and subsequent command semantics. Physical panel scanout
has not been observed.

The composition-buffer preview is not a final display commit.
