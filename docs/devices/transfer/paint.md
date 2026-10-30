# Disabled color transform in mixed path batches

The normal GUI's840/1048-byte mixed fill/stroke packet now completes through
original IRQ129. The model recognizes the optional color-transform disable
write at each full path's dirty-state seam and preserves the shared RGBA
profile after successful rectangle clears. All20 transfer native subtests pass.
That run's next original boundary is a224/1736-byte YUVA107 conversion with
temporary paint mode2, subsequently handled by the [mode overwrite model](mode.md).
GUI first frame remains unverified.

## Direct facts and producer chain

Code addresses are unchanged C1.70 MAS VAs; command buffers are board PAs.
The preceding matrix-overwrite run captured840/1048 at bd0e0490/bd1605b0.
This task's baseline repeats the grammar at bd0e02f0/bd1603d0, rejecting CPU1
PC106ee3c6, WRITE32 PA201810e0=1, `transfer-stream-truncated`. Setup contains
three41+29-word fill/stroke pairs. Draw contains the earlier55+31-word pairs
plus `01848004=0` at first fill words24/25 and the normal two-word ending.
It has262 words rather than260. This is not missing guest data: dispatch used
the old fixed-position rule word50 and fell through to LUT length validation.

The actual producer is the ordinary cached-state flush, not a new submission
or completion request:

| Stage | Original source evidence |
|---|---|
| Previous clear |1068bd90 ends with106b581c(context,0x21,1), marking matrix and color-transform state dirty after forcing its own clear state.|
| Glyph draw |10644f62 calls1064c170 at106451a6 for a path-backed glyph.|
| Flush before path |1064c170 sets the glyph matrix, marks matrix dirty(mask1), then calls106b581c(context,0x17f,2) at1064c1c0.|
| Dirty filter |106b581c mode2 routes to106b55a2; it intersects private+136 with the supplied mask.|
| New write |When dirty mask0x20 is set,106b55a2 reads private+488 and emits01848004 with0 or01000000 at106b5704.|
| Execution |The glyph producer then emits fill paint/path and, if selected and positive width, stroke paint/path; matched keys and1a6 triggers follow.|

The baseline trace directly joins the new write with context b1800c1c,
bank0, header01848004, payload0, LR106b5709. The enclosing flush's LR is
1064c1c5. The final trace also observes preceding dirty marks0x21/mode1 and
glyph flushes0x17f/mode2. Snapshot private bc01e4b0 has private+488=0.
Clearing the dirty bit explains why the extra command need occur only once
in the three-glyph batch.

The parameter setter10646b50 handles enum0x1170, accepts one scalar, stores
its Boolean value at private+488 and marks dirty0x20 when changed (10647090..a4).
Getter10647606 returns that field for0x1170 at10647952. The adjacent0x1171
case accepts eight values and marks dirty0x40;106b55a2 emits their packed
coefficient vector only when the enable is nonzero. These enums correspond to
VG_COLOR_TRANSFORM and its values in the [Khronos OpenVG1.1 specification](https://registry.khronos.org/OpenVG/specs/openvg-1.1.pdf).
That joins the high-byte flag to the color-transform stage, independently of
the low-byte paint mode. The public API match is a software semantic inference,
not identification of a physical GPU.

The fixed [Z7 decoder](https://github.com/WonderLantern/z7-qemu/blob/97abf0b81cd3211f8374a38987a0beaf4549b90f/qemu/nikon-z7-gpu.h)
interprets header bits12..15 as byte enables. Thus01848004 writes register184
with maskff000000;01841004 writes mask000000ff. The new write is not bit3 of
the low byte and is not overwritten by later low-byte mode8. Under the disabled
color-transform profile it leaves the existing fill/stroke colors unchanged.
This related implementation corroborates the encoding, not physical precision.

## Shared context and finite contract

The initialized clear and initialized image both validate the same35-word
format0,720x480 geometry/full-clip prefix. The clear prefix and each rectangle
explicitly disable the high-byte color transform. Its later path consumers
set their own blend, target and low-byte paint mode. The same original context
continues across those submissions; no new global initialization is emitted.

The first parser-only correction therefore exposed a second real issue:
the model discarded rgba720_target after a successful clear, causing
`transfer-stream-context-not-established` on the same840/1048 family. The
finite clear executor now publishes its validated target as the shared RGBA
profile only after successful writes. Initial and retained clear batches both
establish it; ACK retains it. Rejection, execution failure, reset and unrealize
retain the existing invalidation rules. This does not establish YUVA107 state,
nor permit paths to use a different target.

The path grammar now admits zero or one exact01848004=0 pair at draw word24
of each full fill record, before its repeated paint state. The parser validates
the remaining55 logical words unchanged and consumes57 physical words when
the pair is present.29/31-word inherited strokes are unchanged. Any permitted
mix of these forms works within the existing256-operation bound, independent
of one captured packet length. Dispatch uses the setup family's005ef00e header
instead of a draw operand whose position changes.

Enabled01000000, other payloads/byte masks/registers, duplicate or misplaced
writes and incomplete records remain explicit errors. Enabled color transforms
require a separately validated coefficient/arithmetic contract; they are not
silently bypassed. The existing profile represents disabled color transform
when the optional write is absent. Both accepted initializing families set it
to zero, and supported path/image operations preserve that profile. All active
matrix, stroke, DMA alias/range, key, target, ending and lifetime checks remain.
Cairo coverage, stored-channel compositing and the software-to-register mapping
remain functional conventions, not measured hardware fidelity.

## Validation and next original boundary

The new840/1048 fixture fails on the old model with MEMTX_ERROR. After parsing
is fixed, a clear-to-mixed-path fixture independently reproduces the context
rejection. Both are retained as native red receipts. Final applicable
`nikon_transfer_unit` passes all20 subtests in0.81s. The mixed-path case now
has51 variants, including one/all optional writes,128 pairs, late enabled state,
wrong mask/payload/position, truncation/ending, initialized and retained clears,
foreign/reset/failed clear contexts, immutable capture, repeat, all output and
guard bytes, late map loss, reset and unrealize. These are synthetic receipts.

The final unchanged original run keeps the prior declared descriptor17 TEMP,
descriptor18/resource12, acceleration(0,0,1024), RNG0, cold F4, five-CPU MTTCG
and normally paced9600 healthy UART inputs. It completes three840/1048 packets
through original106ee3ca IRQ129, plus later496/672 paths,176/336 images and
224/1736 and176/1552 conversions/composites. There are26 original handler
entries from the first new mixed packet until the next rejection. Byte lengths
and completions are actual trace joins; no host-rendered UI or injected event,
PC, readiness or firmware return is used.

That run's next first error is CPU0 PC106ee3c6, WRITE32 PA201810e0=1,
`transfer-convert-program-not-supported`. Its complete224/1736 buffers are
bd0dff90/bd15ff90. The scaled107 fixed-field comparison has exactly one
non-operand difference: draw word74, the01841004 payload, is2 instead of0;
later draw word92 is the known final mode2. Source is bf309600 and output
planes are cb82b000/cb980000. Those addresses are operand observations; their
admission and the earlier mode's overwrite/producer semantics are joined by the
later [mode analysis](mode.md). Matrix, scaled geometry and fixed LUT fields match the existing
profile; the new rejection is not attributed to them.

Final GUI gate/pending3/display-ready remain0, BatCk00010001, application03/02
and primary request11. QEMU and peer are alive until controlled cleanup, and
the peer has no protocol rejection. Receipts in `run paintmask/`
retain baseline red, parser-only green/context rejection, final original run,
native red/context-red/focused/final logs, source projections, raw streams,
snapshots and progress/producer/next-field joins. Normal startup progress and
physical fidelity/complete GUI remain separate claims.
