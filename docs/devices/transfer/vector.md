# Ordered solid vector batches

The finite vector walker accepts inline SRC rectangles and indirect SRC_OVER
fills and strokes in one ordered submission. It reuses the existing path
rasterizer and normalized straight-RGBA compositor. Image sampling remains a
separate contract; this is not a general GPU command interpreter.

## Original command evidence

An unchanged C1.70 MAS run, after the virtual lens startup exchanges, submits
744 setup bytes and 920 draw bytes. The setup contains a 43-word rectangle,
a 53-word rectangle, a 41-word fill and a 48-word stroke, followed by one zero
alignment word. The draw side contains the two rectangle bodies, a color-state
transition, a 57-word fill, a 55-word stroke and the normal two-word ending.
Recognition follows those command records, not these total byte lengths.

The four keys advance from `1003e` through `10041`. The common board-physical
RGBA destination is `bec5f200`; the two scratch operands are `bcca7f50` and
`bcec3f70`, echoed between the streams. Scratch storage remains an opaque
paired operand, as in the [rectangle contract](rectangles.md).

Both rectangles replace `[2,82) x [304,352)` with transparent RGBA using SRC.
The fill and stroke use the same captured outline and identity affine matrix.
The fill paint is black with alpha128; the stroke is opaque gray, with half
width1 and a tangent extent giving miter limit approximately4. The outline
has bounds `[3,81] x [305,351]`; the stroke's outer bounds match the cleared
rectangle. This describes observed geometry, not an inferred icon or text.

The existing original producer joins remain applicable: rectangle clears use
`10643a9a ->1064aace ->1068bf1e ->1068bd90 ->10646324`; solid paint passes
through `1068b8ae ->106453cc`, and the fill/stroke builders are `1068c5ae`
and `1068c50c`. These are MAS virtual code addresses. This submission's
individual upstream calls were not separately traced.

## State and path semantics

A stroke may inherit the preceding path matrix and segment count, or carry
the complete 19-word path prefix and 24-word draw-state prefix. A complete
prefix replaces the current matrix before the path latch. Inline rectangles
retain their mandatory active identity matrix; an earlier unused cache matrix
may be omitted or overwritten. The existing color-transform-disable write
is accepted only in its known positions and with value0.

The captured segments add relative QUAD `0b` to the existing relative H/V
outline. Its control point and endpoint both use the current point before
the operation. The finite decoder consistently handles the absolute/relative
MOVE, LINE, H, V and QUAD pairs `02/03`, `04/05`, `06/07`, `08/09`, `0a/0b`.
CLOSE is `00` and restores the contour's starting current point. Cubic, arc,
relative CLOSE and unknown segment values remain errors.

The relative-bit interpretation is corroborated by the pinned
[Z7 path decoder](https://github.com/WonderLantern/z7-qemu/blob/97abf0b81cd3211f8374a38987a0beaf4549b90f/qemu/nikon-z7-gpu.h#L175-L230).
The current Z7II capture directly exercises relative QUAD and H/V; the other
paired forms are a finite functional extension based on that source, not a
separate original-run or physical-camera observation.

All solid paints retain their complete RGBA value, including alpha0..255.
SRC replaces every covered rectangle pixel, so transparent SRC erases old
alpha. Path fill and stroke use SRC_OVER, so alpha0 preserves the destination.
The equations and rounding convention are those in [RGBA over](over.md).
Cairo coverage and stored-channel-space mixing remain functional assumptions.

## Atomic execution and bounds

The same walker validates the entire batch at admission and deferred execution.
Keys advance in operation order with the existing `fffe ->1` wrap and maximum
256 operations. Full grammar, matched scratch/key fields, ending, padding,
affine limits, path counts, fill rules and stroke styles remain checked.

One backed 720x480 RGBA extent is shared by all operations. Command streams
and indirect coordinates/segments retain their raw and effective-backing
alias checks against the output. Indirect data are read again at deferred
execution. A private copy of the actual destination receives the ordered
operations; only a fully validated, rendered batch is copied back. A late
invalid stroke or lost mapping therefore cannot leave earlier SRC clears in
guest RAM. Reset and unrealize cancel work, and IRQ129 follows successful
commit through the existing scheduler and guest-owned acknowledgement.

## Validation scope

The parent obtained an actual native red result for
`/nikon/transfer/vector-batch`: the old model returned MEMTX_ERROR for the
first valid mixed submission. The added fixture compares the complete output
against a hand-specified absolute-QUAD outline plus an independent RGBA
reference, and checks literal center, straight-edge and empty-corner pixels.
It covers omitted cache state, independent stroke matrix replacement,
transparent and partial-alpha paints, inherited state, wrapped keys,
immutability, repeats, aliases, late rejection, mapping loss and cancellation.

`/nikon/transfer/relative-paths` compares complete fill and stroke masks for
absolute and relative forms under an affine transform, and rejects unknown
segments and a relative control point outside the finite coordinate bound.
The parent then ran the complete `nikon_transfer_unit` batch: all 28 subtests
passed in 2.45 seconds, including all 19 vector-batch variants and the relative
path checks. The curated receipt below records their scope.
An initial fixture-only failure came from signed extension in a literal pixel
assertion; an explicit `uint32_t` cast corrected the assertion without changing
the rendered pixels or product implementation.

These are synthetic fixtures, separate from the original execution below.

## Original execution

With the fixed lens's corrected operating state, the unchanged C1.70 bundle
passes the captured mixed batch and binds a 1024×682 status GUI. The former
request to extend the lens is absent. The complete cold-neutral profile uses
the selected blank 8 GiB card, paced healthy UART battery, held F4/EX C5 input,
five-CPU MTTCG and the declared 2×2 grayscale scene. No debugger accesses occur
inside the 60-host-second observation window.

QMP is running at 15 and 30 seconds, then debug-stopped at 45 and 60 seconds.
Paired completion advances through 0, 36, 76 and 76 jobs; the new stop is the
dynamic paired field at offset `598`, not a graphics rejection. Lens packets
complete 843 times without rejection. The capture policy remains source-named
`LvMovie` (`800`), not movie recording. Head0 remains unbound, the scene
background is black and GUI readiness remains zero. This establishes a rendered
status GUI without the former lens warning, not normal live view or photo/movie
capture acceptance. The [curated receipt](/evidence/transfer/vector.json)
records the native and original scopes separately.
