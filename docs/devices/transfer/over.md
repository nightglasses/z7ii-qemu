# Indirect RGBA paths and SRC_OVER

The normal GUI path's 176/368-byte pair now completes through original IRQ129.
It fills a bounded indirect rectangle with semitransparent black, then retains
the record7 context for subsequent path/image work. At that checkpoint, the next
unchanged-firmware rejection is a separate224/472-byte pair. The later
[image context model](image.md) crosses it; see the [current frontier](../../validation/frontier.md).
No GUI first frame is established.

The later [target contract](target.md) removes the record7-only restriction
from this fully initialized family. A validated explicit720x480 RGBA target
may establish or replace context; retained commands must match it. Original
record0 execution and an isolated whole-slot before/IRQ comparison now verify
that extension. The observations and16-case receipt below retain their scope.

## Observed data and producer

Code addresses are unchanged C1.70 MAS VAs; data addresses below are board PAs.
The complete setup/draw buffers at `bd0dff90/bd15ff90` select format0,
720×480, identity transform, full clip, target `c175d600` and SRC_OVER
(`1a0[19:16]=1`). Draw word25=8 is an already accepted shadow bit3.
Paint word76 is `cc000000`: the existing little-endian RGBA convention gives
black with alpha204. The indirect inputs are captured independently:

| Input | Board PA | Contents |
|---|---|---|
| Segment bytes | `bcca7af0` | `02 07 09 07 00` |
| Float32 coordinates | `bcca79b0` | `91, 158, 537, 164, -537` |
| Target before submission | `c175d600` | All 345600 RGBA pixels zero |

MOVE, relative H/V/H and CLOSE describe `[91,628) × [158,322)`, or 88068
pixels. Physical-memory GDB reads establish these contents, rather than
inferring geometry from pointer equality. The old parser rejects the valid
pair at CPU1 PC `106ee3c6`, WRITE32 PA `201810e0=1`, with
`transfer-stream-program-not-supported`.

The traced register185 writer `106453cc` receives `0185f004/cc000000`,
caller LR `1064af21`. The producer chain is `1064abe8 → 1068b8ae` at
`1064af1c`. The solid-paint branch of `1068b8ae`, paint type6912, passes
paint-object+12 unchanged at `1068b97a`. This is a filled indirect path,
distinct from the inline SRC clear producer `1068bd90`.

## Finite functional contract

Only the existing single-path 176/368 family gains variable packed paint
and a positive closed axis-aligned float rectangle. Exact command grammar,
identity transform, format, full clip, matched keys, echoed operands,
scratch fields and endings remain checked. The five segments remain fixed.
Coordinates use the existing path validator's finite ±4096 input and ±8192
transformed bounds; fractional, clipped and wholly offscreen rectangles are
valid. Coordinate alignment, complete RAM extents, raw overlaps and effective
backing aliases of all four inputs with the target are checked before DMA.
The older 232/416 double-path shape and opaque black paint stay fixed. The
single-path target is now checked as described in[target.md](target.md).

The shared rasterizer supplies Cairo A8 coverage. For source alpha As,
coverage M and destination alpha Ad, define integer weights:

```
a = As * M
s = a * 255
d = Ad * (65025 - a)
t = s + d
Ao = round(t / 65025)
Co = round((Cs * s + Cd * d) / t)
```

Zero effective source alpha preserves all destination bytes. Colors are
stored as straight RGBA. The [ordered vector walker](vector.md) applies each
solid path directly to its private destination using these equations, including
paint alpha0..255. Image batches retain their premultiplied scratch overlay;
for that input the source numerator is `Cs * M * 65025`. They accept destination
alpha0..255 and normalize the result. Image modulation still requires opaque
paint; A8/edge coverage may produce a partially transparent overlay.

The [fixed Z7 implementation](https://github.com/WonderLantern/z7-qemu/blob/97abf0b81cd3211f8374a38987a0beaf4549b90f/qemu/nikon-z7-gpu.h#L304-L435)
corroborates packed RGBA, selector1 and straight-alpha SRC_OVER. The equations
also follow the [W3C source-over definition](https://www.w3.org/TR/2024/CRD-compositing-1-20240321/#porterduffcompositingoperators_srcover).
These are functional references, not physical Nikon measurements. In particular,
the observed black sample cannot independently establish arbitrary colored
paint packing/association. Straight colored RGBA, stored-channel-space mixing,
nearest integer rounding and Cairo edge coverage are explicit finite-model
assumptions, validated synthetically; physical GPU precision remains unknown.

Commands are captured immutably. Indirect path data and mappings are
revalidated at deferred execution; the full mask/overlay is prepared before
output writes. Late failure, reset and unrealize publish neither output nor
completion. Successful execution retains the existing RGBA720 context; IRQ129
and original ACK/continuation paths are unchanged. No guest event or UI is
injected.

## Validation and next stop

The new native valid case fails against the old model with MEMTX_ERROR.
The applicable complete `nikon_transfer_unit` batch then passes all16 subtests
in1.21s. The new case has35 variant types, including64 combinations of source
and destination alpha, independent rational reference pixels, colored paints,
fractional/clipped/offscreen rectangles, repeated blending, gray-context
continuation, geometry/state/data rejection, aliases, immutable command capture,
late indirect corruption, map loss, reset and cancellation. Whole output
allocations and guards are compared. Existing path and A8 image cases also
exercise partially transparent destinations. Independent read-only review
found no concrete defect in arithmetic, validation or lifecycle behavior.

With unchanged firmware, the prior descriptor/TEMP, resource12, RNG0,
acceleration, cold F4 and normally paced healthy UART inputs, original execution
now records the following START → original IRQ129 sequences:

| Setup/draw bytes | Result |
|---|---|
| 176/368 | IRQ129 |
| 5576/7488 | IRQ129 |
| 656/888 | IRQ129 |
| 176/336 | IRQ129 |
| 224/376 | IRQ129 |
| 224/472 | Rejected |

The final record7 canvas has257532 transparent pixels,74966 black alpha204
pixels and13102 pixels with higher alpha from subsequent drawing. For example,
the first and last rectangle corner pixels become `d2d2d2ff`; neighboring
outside pixels remain zero. This is a final buffer after multiple jobs, not an
isolated post-176/368 snapshot or scanout evidence.

The new first rejection remains CPU1 PC `106ee3c6`, WRITE32 PA `201810e0=1`,
now `transfer-rectangle-count-not-supported`, for complete224/472-byte buffers
at `bd0dff90/bd15ff90`. Its indirect/inline operands and state sequence require
the next producer analysis; the diagnostic alone does not prove a malformed
guest count. Final BatCk is `00010001`; GUI gate/pending3/display-ready remain
zero. The UART peer rejects no protocol request. Audio input samples,
physical fidelity and GUI first frame remain unverified.

Ignored receipts are in `run rgbaover/`: original red/green
traces, physical input/preimage/canvas captures, source projections, complete
next buffers, native red/green logs, `path-observed.json`,
`paint-producers.json` and `verified-progress.json`.
