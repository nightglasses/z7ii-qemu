# Mixed path fills and strokes

The 840/1040-byte submission contains three fill/stroke pairs. The finite model
now handles this mixed grammar and the source-backed solid stroke styles,
reusing Cairo coverage and existing RGBA compositing. Original execution
advances through differently grouped mixed batches and subsequent graphics work.
The next boundary at that checkpoint was a new856/1088-byte rectangle
submission, now handled by the [matrix overwrite correction](overwrite.md).
See the [current frontier](../../validation/frontier.md); first frame remains
unverified.

## Observed data and producer

Code addresses are unchanged C1.70 MAS VAs; command/data addresses are board PAs.
Two original baseline runs reproduce840/1040 at `bd0e0540/bd160aa0`, rejecting
CPU1 PC `106ee3c6`, WRITE32 PA `201810e0=1`, with
`transfer-path-batch-count-not-supported`. The previous parser divides the
whole setup by41 words and requires55 draw words for each path. Here each
pair is41+29 setup words and55+31 draw words; three pairs plus the final
two draw words give210/260 words exactly. This is not a malformed guest count.

All six paints are opaque black `ff000000`, target `bec5f200`, SRC_OVER,
keys `10009..1000e`. Physical reads capture two indirect outlines:

| Path | Coordinates PA / float32 words | Segments PA / bytes |
|---|---|---|
| First | `bcca5170` /26 | `bcca56d0` /14, MOVE/LINE/CLOSE |
| Second and third | `bcca4f20` /44 | `bcca57b0` /16, MOVE/LINE/QUAD/CLOSE |

Fill matrices scale by `(28.79998779296875,36)` and translate by x
`166.59063720703125`, `187.125`, `201.76718139648438`, common y
`13.062515258789062`. Each following stroke repeats the same indirect operands
and inherits that matrix. Setup65 changes `2100 → 2000`, clearing the fill
bit8; stroke setup lacks a new matrix and segment-count register63 write.
The stroke payload `5af01e` supplies zero options/phase, half width
`0.0555555559694767` and tangent miter extent `0.09622504562139511`.
Cap/join fields are zero. The outlines resemble three glyphs; no string or
font is substituted, and this receipt does not establish their UI meaning.

The producer chain is `10644f62 → 1064c170`: the first iterates guest glyph
records, calls the latter at `106451a6`, and advances glyph positioning.
`1064c170` calls `1068c5ae(...,2,0)` at `1064c204` for fill, then
`1068c50c` at `1064c23c` and `1068c5ae(...,1,0)` at `1064c254` for stroke.
Original trace observes those fill/stroke entries with matching caller LRs.
`1068c50c` emits four5a..5d words and65 cap/join bits; disassembly binds
5c to half the stroke width and5d to the half-width-scaled miter extension.
`1068c5ae` chooses fill bit8 and emits the shared draw trigger/key.

The [fixed Z7 decoder](https://github.com/WonderLantern/z7-qemu/blob/97abf0b81cd3211f8374a38987a0beaf4549b90f/qemu/nikon-z7-gpu.h#L93-L150)
independently interprets these fields as object-space strokes, cap0/1/2
butt/round/square and join0/1/2 miter/round/bevel. It corroborates the functional
interpretation, not physical Z7II precision.

## Finite execution contract

The parser walks complete41/55 fill or29/31 stroke records, rather than
inferring a uniform count from total length. A batch starts with a full fill;
stroke may inherit only the latest matrix and register63 count inside that
batch. Its explicit segment descriptor must agree with that count. Full fills
validate the target against the established720×480 RGBA profile and keep the
same target throughout the batch. Thus the earlier record7-only path limitation
is removed; foreign or absent target context still rejects.

At most256 operations are accepted. Class1 keys advance in command order with
`fffe → 1` wrap. Both complete grammars, echoed scratch fields, final ending,
zero alignment padding, formats, opaque paints, fill rules and finite affine
inputs stay checked. Whole-plane writable RAM, four-byte target/coordinate
alignment,32-bit extents, command overlaps and effective backing aliases are
validated before output. Indirect arrays are read/revalidated at deferred
execution. A late invalid record/path or lost mapping leaves output unchanged.
Reset, unrealize, IRQ/ACK and context publication retain their existing lifetime.

Solid strokes support all nine cap/join combinations. Radius and tangent
extent must be finite in `[0,4096]`; zero radius produces no coverage. Dash
options/phase, unknown style bits, cap/join3 and other command variants remain
errors. The rasterizer permits open stroke contours and separate MOVE subpaths;
the existing fill topology rules remain unchanged. It sets width `2*radius`
and miter limit `hypot(radius,extent)/radius`, or1 for zero radius.
The affine CTM at stroke time scales the pen in object space while preserving
the already constructed device-space path, following the
[Cairo stroke contract](https://www.cairographics.org/manual/cairo-cairo-t.html#cairo-set-line-width).

Cairo GRAY coverage, curve tolerance, integer overlay rounding and normalized
SRC_OVER remain functional approximations. The within-batch inheritance,
strict target match and work limits are conservative finite contracts, not a
claim that all physical GPU command combinations are supported. No firmware
bytes, software events, PC or readiness values are injected by the model.

## Validation and new frontier

The new valid mixed fixture fails against the old parser with MEMTX_ERROR.
The complete applicable `nikon_transfer_unit` batch passes19 subtests in1.23s.
The mixed case has36 variants:1/2/3/128 pairs and rejected129, ordered colors,
nonuniform scale, wrapped keys, zero width, repeat/immutable capture, context,
malformed state/data, aliases, backed4GiB crossing, map loss, reset and unrealize.
Entire output allocations and guards are compared. A separate mask case tests
all nine cap/join combinations on open lines, analytic whole-mask butt/square
boundaries, round endpoint pixels, three corner styles, miter truncation,
multiple open contours and invalid parameters. Independent review identified
the initial cap/join test gap; the final expanded tests and implementation pass
review. `native-compile.log` records a corrected test-header macro collision,
not an accepted red or green receipt.

Original MTTCG batching differs once execution advances. Neither post-fix run
submits the exact840/1040 pair as one packet. Both instead complete mixed
280/352 and1680/2072 packets, then984/1328 fill paths and subsequent image and
conversion jobs through original IRQ129. The matched-trace run records29
completions from its first mixed packet to the next rejection. Synthetic
three-pair coverage establishes840/1040 grammar acceptance separately; it is
not described as a byte-identical original replay.

That run's new first rejection is PC `106ee3c6`, WRITE32 PA `201810e0=1`,
`transfer-rectangle-program-not-supported`, complete856/1088 buffers at
`bd0dff90/bd15ff90` (CPU1 in the matched run, CPU0 with extra glyph tracing).
Its first setup matrix differs from the clear family's identity restriction;
the next matrix write is identity. The later [producer analysis](overwrite.md)
establishes that the first write is overwritten before use. Equal byte lengths do not equate it with
the earlier accepted856/1088 packet.

The descriptor/TEMP/resource12, RNG0, acceleration, cold F4 and healthy UART
profiles remain declared inputs. Final BatCk is `00010001`; gate/pending3/
display-ready remain zero. The peer rejects no request. Final guest-memory
pixels include subsequent jobs and do not establish scanout or first frame.
Receipts are ignored in `work/runs/guipaths-20261023/`: baseline `red`/`probe`,
post-fix `green`/`matched`, indirect arrays, source projections, native logs and
`matched-progress.json`/`verified-progress.json`.
