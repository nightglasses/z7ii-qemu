# Inline RGBA rectangle batches

The normal GUI path's 856/1088-byte submission contains four integer rectangle
clears. The new finite model handles the whole self-contained batch family,
including variable count, coordinates, colors, target and sequential keys.
The unchanged original now receives IRQ 129 for this submission. The later
[RAM backing fix](../../startup/ram.md) resolves the host TLB assertion recorded
below. The later [context continuation](continuation.md) also completes ten
rectangles in 2120/2368 bytes without this family's initialization prefix.
The later [SRC_OVER extension](over.md) crosses the partial-alpha checkpoint;
see the [current frontier](../../validation/frontier.md). No GUI first frame
is established.

## Direct evidence

Code addresses are unchanged C1.70 MAS VAs; buffer addresses are board PAs.
The captured setup/draw buffers are bd0dff90/bd15ff90. Original 106ee04a
converts word counts 214/272 into 64-bit units6b/88 and starts the two streams.
Earlier dispatch did not recognize 1088 or the inline path shape and fell back
to the 224/1688-byte LUT parser, producing`transfer-stream-truncated`.

The original call chain is 10643a9a→1064aace→1068bf1e→1068bd90.
The public rectangle-clear wrapper obtains the current context; 1064aace
checks positive extents, and 1068bf1e clips to the surface and packs the
context's four color components. 1068bd90 emits the clear state and path bands.
Its 10646324 helper appends inline segment bytes`02 07 09 07 00` and signed32
coordinates `x,y,width,height,-width`. 10645b2e emits paired scratch operands;
10645c3c assigns matching setup 85/draw a6 keys and triggers register 1a6.
Keys increment from 1 through fffe and then wrap to 1.

The producer trace measures these four 1068bd90 invocations, each with return
address 1068c2e9; stream coordinates and positive height 48 agree:

| x | y | width | height | paint | target |
|---|---|---|---|---|---|
| 128 | 2 | 124 | 48 | 00000000 | bec5f200 |
| 266 | 2 | 104 | 48 | 00000000 | bec5f200 |
| 436 | 2 | 153 | 48 | 00000000 | bec5f200 |
| 195 | 60 | 328 | 48 | 00000000 | bec5f200 |

The setup starts with 691004/80 and contains four 53-word records. Draw has
35 initialization words, 57 words per clear plus a two-word transition between
clears, and a two-word ending. Eight-byte alignment adds zero padding where
needed: setup bytes = `4*round_up(2+53*n,2)`, draw bytes =
`4*round_up(35+59*n,2)`. Scratch values bcca7f50/bcec3f70 alternate and echo
between streams; keys 10001..10004 match one-to-one. Both transforms are
identity; format 0, geometry 720x480 and clip 719/479 are explicit.

1068bd90 writes packed paint to 185 and emits 01a04004=0 for SRC. In contrast,
the prior opaque SRC_OVER path writes that register byte as 1. The fixed
[Z7 decoder](https://github.com/WonderLantern/z7-qemu/blob/97abf0b81cd3211f8374a38987a0beaf4549b90f/qemu/nikon-z7-gpu.h#L304-L333)
corroborates the blend selection in 1a0[19:16]. Thus zero alpha here clears
covered destination color and alpha; it is not an SRC_OVER no-op. This related
software reference is not Nikon physical-silicon documentation.

## Finite contract and assumptions

The recognizer uses the self-contained initialization shape and 720x480
geometry, not one hardcoded total length. All fixed headers, masks, state,
transforms, segments, ending and padding must match the recovered family.
Count 1..256 uses the existing functional work bound. Coordinates and colors
are operands; positive integer rectangles must lie fully inside the submitted
720x480 context. Other transforms, geometry, formats and blending remain
explicit errors. Guest clipping is upstream; malformed out-of-bounds commands
are not silently clipped by the model.

All target operands must agree on one aligned 32-bit RAM extent. The complete
`0x151800`-byte image must be directly writable without 32-bit address overflow.
Raw and effective backing alias with either command stream are rejected.
Every record is validated before any write, including late keys/coordinates;
execution revalidates at the deferred timer before mapping the destination.
Writes replace all four RGBA bytes only inside each rectangle, in submission
order. Neighboring pixels and allocation tail remain untouched. Reset or
unrealize cancels queued work; execution-time mapping loss emits no successful
completion. IRQ 129 is raised only after actual execution, using the existing
guest-owned ACK path.

Exact integer half-open coverage and packed RGBA byte interpretation are
functional conventions. Scratch slots are paired opaque operands: the model
does not emulate physical GPU intermediate storage or expose invented scratch
data. It does not implement a generic GPU command VM or a physical rasterizer.
This self-contained profile does not establish the old c175d600 RGBA720 or
YUVA107 continuation contexts; successful execution invalidates those scoped
context flags instead of applying them to bec5f200.

The [continuation extension](continuation.md) now retains this family's own
initialized profile and expected next key after successful execution. It is
separate from those older scoped flags and supports omitted initialization
only with a valid retained profile.

## Native validation and original continuation

Against the old parser, the new native submission returns MEMTX_ERROR instead
of the expected success. The final `nikon_transfer_unit` batch passes all 14
cases. The new case contains 40 variants: counts 1/2/3/4/5/256, count overflow,
transparent and partial-alpha SRC, overlapping colors, key wrap, every output
byte and guards, malformed fields/padding/endings, late invalid operands,
target range/alignment/readonly/alias boundaries, immutable captured commands,
mapping loss, reset and unrealize. A real RAM region crossing 4 GiB proves that
the 32-bit end check rejects otherwise backed output without touching any byte.

The first integration attempt exposed a dispatch collision: the earlier 32x32
LUT program also has reset header 1a11. Its geometry now distinguishes the
families; a separate real LUT submission with that shared header must complete
and ACK before the rectangle variants. An independent reviewer found the
32-bit output-end gap; it was corrected and the complete applicable batch
rerun. Final read-only review found no remaining concrete defect. These are
synthetic validation results, separate from original execution.

The final unchanged-bundle run keeps the prior descriptor/TEMP/acceleration,
RNG 0, cold F4 and normally paced healthy UART peer. It crosses 856/1088 START
and enters original IRQ 129 handler 106ee3ca afterwards. Further original clear
generation is observed. The peer reports no protocol rejection before QEMU
terminates. Then host QEMU aborts in `accel/tcg/cputlb.c:1080`,
`tlb_set_page_full`, on `!(iotlb & ~TARGET_PAGE_MASK)`.

This rectangle-only run's next stop was a host RAM/IOTLB page-alignment
invariant. The subsequent [RAM investigation](../../startup/ram.md) binds the
access to original PC `10634fbe`, VA `38f661c8`, PA `bed661c8` in
`z7ii.resource57-record0`. It confirms and fixes the board backing offset while
preserving exact physical boundaries. Those later receipts supersede the
unbound-address hypothesis; no firmware or QEMU upstream source was changed,
and no assertion was disabled.

Receipts remain in `work/runs/graphicscommands-20261023/`: red/producer runs,
source projections, `native-red.log`, `native-final.log`, final trace/stderr,
`preassert-progress.json` and `verified-progress.json`. The failed first
integration and failed host-debugger attempts retain their own scope. The
planned final GDB snapshot did not run after host abort; prior GUI gate/state
values are not relabeled as this run's final observations.
