# Initialized RGBA rectangle targets

The normal GUI's 176/368-byte indirect SRC_OVER rectangle now accepts an
explicit, validated RGBA target, rather than a resource57 record7 address.
The complete transfer batch passes 22 subtests. Unchanged original execution
completes the record0 rectangle and two subsequent jobs, then stops at a
224/1648-byte YUVA107 clear with a different temporary paint mode. GUI gate,
pending3 and display-ready remain zero; no GUI first frame is established.

## Direct observations and ownership

Code addresses below are unchanged C1.70 MAS VAs; command/data and resource
addresses are board PAs. The old model reproduces CPU0 PC `106ee3c6`, WRITE32
PA `201810e0=1`, `transfer-stream-program-not-supported`. The complete pair
is at `bd0dff90/bd15ff90`. Its indirect segments are `02 07 09 07 00`, float32
coordinates `(0,0,720,480,-720)`, identity matrix, paint `ff000000`, SRC_OVER
selector1 and matched key `10001`. Against the single-path fingerprint, the
only fixed-field difference is draw word33: `bec5f200` rather than `c175d600`.

The original resource57 table at VA `123b73ec` has record0
`{1,004b0000,b181d384}` and record7 `{1,004b0000,b181d38c}`. Read-only runtime
reads confirm their pointer cells contain `bec5f200` and `c175d600` respectively.
Record0 owns `[bec5f200,bf10f200)`; this job uses its first `151800` bytes,
`[bec5f200,bedb0a00)`. The board's existing exact record0 MemoryRegion and
backing are unchanged. This is not the composition buffer `bf309600`, nor
does its ownership establish a display identity. See [storage](../../runtime/storage.md).

The producer chain is `10643cba -> 1064abe8 -> 1068c39c -> 1064609a`:

* The path wrapper calls `1064abe8(context,path,2,0,5376)` at `10643ce8`.
* `1064abe8` invokes global initialization `106454b2` at `1064aec0`, then
  sets up the filled path through `1068c39c` at `1064aed2`.
* `1068c39c` gets dimensions from the surface at `context+40`, emits the
  blend/mode state and calls `1064609a(context,0,...)` at `1068c42e`.
* `1064609a` copies the eight-word template at VA `1078950c`. Its four
  headers write `184` mask2, `1a0` mask2, `1a2` mask9 and `1a4` mask15.
  For this branch, the last payload comes from
  `106d57ac(*(private+204))`, then the whole template is appended at
  `106460fc`. It is not a hardcoded record address.
* `106d57ac` returns allocation+8 when cached, otherwise fills that field
  from allocation+0. The runtime allocation object at VA `bc012f50` has
  cached address `bec5f200`, logical extent `151800` and stride `b40`.
  The context is `b1800c1c`, private `bc01e4b0`, surface `bc013040`;
  surface+136/+140 contain 720/480, and private+204 points to that object.

Trace and memory agree on those objects. In the final normal run the target
helper returns `bec5f200` before this START, and the paint writer receives
`0185f004/ff000000` from LR `1064af21`. The command's complete initialization
sets format0, extent720x480, full clip and color-transform disable before the
sole draw trigger. There is no software branch requiring record7 for this
grammar. Successful old record7 execution was an observation, not a target
identity requirement.

## Finite model and assumptions

Only this initialized single-path family's target operand becomes variable.
It must be four-byte aligned, wholly within the 32-bit address domain and
covered by one direct writable RAM span for `720*480*4` bytes. All command,
coordinate and segment extents remain checked for range, raw overlap and
effective backing alias with the whole output. Parse and deferred execution
both validate the captured operand and current mappings.

Complete initialization may replace a prior target or establish a cold
context. Only successful output publishes the actual target in the existing
RGBA720 context; ACK retains it. A later path/image continuation must match
that address. A foreign continuation, failed parse/execution, reset or
unrealize clears or cancels context as before. Address0 remains legal and
distinct from the out-of-domain no-context sentinel. No second context store
or resource-slot whitelist is introduced.

The exact headers, all other state/zeros, identity matrix, geometry/clip,
paired scratch value, key/endings, closed positive rectangle and existing
paint/compositing limits stay checked. Initial mode0/8 remains allowed only
before the mandatory final8. At that checkpoint the older 232/416 two-path
composition profile retained its existing target and geometry; the later
finite extension below removes that address fingerprint. Neither change generalizes
unobserved surface formats or command grammars.

The finite inference is that equal explicit initialization has the same
meaning for any independently validated target span. Target equality for
retained work and four-byte alignment are conservative functional contracts,
not recovered physical address restrictions. Cairo coverage and straight
RGBA integer SRC_OVER keep the [existing assumptions](over.md). Physical GPU
precision and display ownership remain unestablished. No firmware bytes,
guest PC, events, returns, readiness cells or supplied GUI image are changed.

## Validation and next stop

The new record0 native case returns MEMTX_ERROR against the old model. The
reused SRC_OVER fixture now exercises 49 variants per target at `c175d600`,
`bec5f200`, `0`, `800004` and `ffeae800`; the last active span ends exactly
at 4GiB. The four new targets contribute 196 variants. Coverage includes
the actual full-screen black/mode8 case, 64 source/destination alpha pairs,
independent whole-buffer reference pixels, repeat and retained drawing,
replacement of a prior target, foreign context, alignment, backed 4GiB
overflow, short/unmapped/read-only target, raw and backing aliases,
immutable capture, late indirect corruption/map loss, reset and unrealize.
Nonzero targets compare the entire `4b0000` slot plus both guards; address0
uses the exact active span and a suffix guard so it does not cover command
RAM. Failed and reset work cannot establish a retained context.

The final applicable `make check MTESTARGS=nikon_transfer_unit` passes all
22 subtests in 1.12s. The normal original run uses the preceding declared
TEMP/descriptor18/resource12, acceleration `(0,0,1024)`, RNG0, cold F4,
five-CPU MTTCG and normally paced 9600-baud healthy UART profiles. Original
IRQ129 follows the newly accepted 176/368, then 232/416 and one 224/1648
submission. The next 224/1648 START rejects before completion. Final
application bytes are `01/03`, primary request0, BatCk `00010001`, and GUI
gate/pending3/display-ready0. The peer rejects no protocol request; QEMU
and peer stay alive until controlled cleanup.

A separate read-only debugger-paused run captures the full record0 slot
before START and at entry to original IRQ129. All 345600 active pixels then
equal `000000ff` bytes; 336004 pixels change and the entire remaining slot
tail matches its preimage. The captured command and indirect data match the
new full-screen case. This isolated output receipt is separate from normal
UART timing: the normal run's later final record0 is all zero, not an isolated
post-rectangle image. That later content does not negate or replace the
before/IRQ comparison, and neither receipt establishes scanout.

The next first error is again CPU0 PC `106ee3c6`, WRITE32 PA `201810e0=1`,
`transfer-stream-program-not-supported`. Complete buffers are at
`bd0dff90/bd15ff90`, with format107, 1024x688 allocation, an inline clear of
1024x682 rows starting at y6, and planes `cb4a7000/cb5fc000`. Comparison with
the existing clear family, respecting its dynamic fields and LUTs, finds
only draw word66 differs: temporary `01841004=0` instead of8; final word389
still writes8 before drawing. That checkpoint leaves the packet rejected;
the later[clear mode model](clearmode.md) traces both writers and crosses it.

Receipts: `run singletarget/`, including original baseline and
ownership probe, source projections, native red/focused/final batch, final
normal argv/traces/snapshot/streams and producer/IRQ joins. An intermediate
green snapshot script read a stale baseline filename and failed; its trace
is separately scoped, and the successful final run is the snapshot receipt.

## Two-path target extension

The 232/416-byte pair has the same `1a4` mask15 target write at draw word33.
The `1064609a -> 106d57ac` source above takes that payload from the current
allocation; neither helper branches on resource57 record5 or the previously
observed PA `bf309600`. The complete initialization, 1024x688 RGBA extent,
identity matrix, two matched scratch operands, path keys and endings remain
fixed and independently checked. Only the target payload becomes an operand;
the two-path geometry and opaque-black paint do not become generic paths.

The whole `1024*688*4 = 2b0000`-byte allocation must be four-byte aligned,
remain in the 32-bit PA domain, and resolve to one direct writable RAM span.
All command, coordinate and segment extents must be disjoint from that whole
allocation both as PAs and as resolved backing. This deliberately includes
the first six rows even though output touches only rows6..687. Deferred
execution revalidates mappings and indirect inputs against the captured
command pair. Output starts at target+`6000`, writes 682 opaque-black rows
and leaves the six preceding rows, trailing backing and guards unchanged.
The existing context publication rule recognizes only 720x480 output;
successful two-path output therefore clears any prior RGBA720 continuation
context, and cannot establish one even at the same address.

This is a source-backed functional inference: identical explicit initialization
has the same meaning at another independently validated span. It does not
establish a new buffer's firmware ownership, a physical GPU address rule or
display identity. The original-startup observation remains the earlier
`bf309600` execution; this proactive extension alone claims no new original
startup progress and adds no memory, readiness or event injection.

The native `/nikon/transfer/rgba-pair-targets` fixture has 76 variants across
`bec5f200`, `0`, `800004` and `ffd50000`; the last complete extent ends
exactly at 4GiB. Its first alternate target is the old-model red case.
Coverage compares the complete mapped backing against an independent row
reference, including top rows and guards, and checks repeat, immutable
captured command bytes, alignment, 32-bit overflow, unbacked/short/read-only
spans, a read-only fragment in either the top rows or final pixel, raw and
backing aliases confined to top rows, late map/input loss, changed indirect
path, reset and unrealize. A successful 720 initialization followed by the
232/416 pair cannot authorize retained720 drawing. The historical clear
fixture's target+4 rejection is replaced by a genuinely short-span target.
Native run receipts and acceptance remain owned by the integration batch.

The parent integration batch confirms the scoped red in
`run timingextra/rgba-red.log`: the old model rejects the first
valid alternate target at START with MEMTX_ERROR. With the final 76-variant
fixture and target operand implementation, `make check
MTESTARGS=nikon_transfer_unit` passes all 24 transfer subtests in 2.13s;
the receipt is `run timingextra/rgba-green.log`. These are
native synthetic protocol checks, not additional original-firmware startup
or scanout evidence.
