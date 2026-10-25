# Functional descriptor17 input

Functional emulation can use an **explicit constructed parameter image** with
unchanged original firmware. Recovering physical factory calibration is a
separate objective. Consumer code establishes layouts, ranges, arithmetic and
ordering constraints without uniquely determining the factory numbers.

The existing `descriptor17-input` property supplies the image to external NOR.
Original code still checks `ENDM`, copies records, translates fields, publishes
callbacks and drives hardware. This input does not supply guest readiness,
events, return values, task state or a frame. Omitting the property keeps erased
NOR; see [descriptor.md](descriptor.md).

## First bounded experiment

The functional-input experiment is identified by
`run descriptorfunctional/`. `build.py` starts with the qualified
Z7 display seed and changes **only** range `d700:e300`. The added ID34 is a
functional hypothesis, not a recovered Nikon record:

| Field | Selected value | Basis/classification |
|---|---|---|
| Raw grid, `+1c/+20` | 30 by 20 | Virtual selection within original fixed-buffer bounds |
| Aggregate grid, `+24/+28` | 6 by 5 | Nonempty selection within original 8-by-8 bounds |
| Rectangle tables | Tile `(x,y)` covers `[5x,5x+4] × [4y,4y+3]` | Ordered inclusive rectangles covering all 600 raw cells |
| First coefficient table | Edge -24, interior 0 | Virtual two-level log weights; original mapper scales by 4 |
| Second coefficient table | Uniform 0 | Virtual equal log weights; later processing unvalidated |
| All other ID34 words | Explicitly 0 | Unvalidated numeric hypotheses, not recovered or universally neutral defaults |

Inactive table slots are zero. The matrix is not skipped using zero dimensions,
and unrelated erased records are not replaced. The builder checks bounds and
full raw-grid coverage, and rejects negative cases for excessive width, empty
height and an out-of-range endpoint. Its expected derived image follows the
retained 83 original scalar assignments and active matrix transformation.

## First original-firmware result

The existing build and complete unchanged bundle run with multi-thread TCG,
without `icount`. The file is supplied only through `descriptor17-input`.
The accepted sequential probe uses one hardware breakpoint at a time:

```text
1021796a mapper entry: resource34 b7f57700, dimensions 30,20,6,5
return to original loader 103c07a8:
  all 628 derived bytes match the original assignment map
  rear-panel pointer b7f54a00
later first data abort:
  instruction 106ee3c6: STR r1,[r0,r2]
  r0=10e0, r1=1, r2=20180000
  lr_abt=106ee3ce, DFAR_S=201810e0, DFSR_S=1808
  device reason transfer-stream-program-not-supported
  manager current 4, target 5, mode 15, state 2
```

The earlier erased-ID34 partial seed overruns the mapper and remains at manager
current/target 5/5, state 0. The constructed input advances execution through
mapper return and to a later worker state. Under the original manager contract,
current 4/state 2 indicates the old-worker handoff completed and target worker
4 reached its running state. This is not a GUI first frame; the GUI gate and
pending slot 3 remain zero at this stop. No device behavior was changed to
accept the rejected stream.

A separate single-breakpoint capture reproduces the same fault and manager
state. Its original descriptor at `b545e79c` supplies stream sources
`bd0dff90/bd15ff90` and 32-bit word counts `58/104`, hence complete lengths
232/416 bytes. `setup.bin`, `draw.bin` and `stream.json` retain these guest-
produced bytes for the next device investigation. An initial capture assumed
the descriptor was in `r4`; that assumption failed. The retained context shows
the descriptor in `r5`, which the accepted stream capture reads explicitly.

`probe.log`, `snapshot.json`, `derived.bin`, `expected.bin`, `argv.txt` and
`qemu.stderr` retain the accepted result. The earlier `exploratory-*` attempt
confirmed mapper return but retained an internal GDB breakpoint and stopped
again in the loader; it is excluded from the later-stop claim. The corrected
probe explicitly deletes each internal breakpoint before arming the next.
Owned QEMU/GDB processes are terminated after collection.

## Graphics-model follow-up

The rejected232/416-byte pair is the existing RGBA clear with initial draw
word25=8 rather than0. Original setters preserve that software-shadow bit,
then word60 overwrites the same byte with8 before drawing. After admitting
only that finite variant, the unchanged bundle with this **same descriptor
image** submits matching indirect path data, clears the active surface and
enters original IRQ129 handler `106ee3ca`. No input or firmware change was
needed for this step.

The subsequent boundary is a complete176/368-byte, single-path RGBA clear to
`c175d600`,720x480, with measured coordinates `(0,0,720,480,-720)`.
The finite single-path extension now executes that exact profile and the
unchanged firmware enters original IRQ129 again. The input image is unchanged.
The subsequent168/232-byte continuation selects gray `ffb4b4b4`, the same
target and measured path `(96,86,528,308,-528)`. It omits full format/extent/clip
initialization. Bounded retained context now permits that exact continuation;
the unchanged firmware writes the gray rectangle and enters original IRQ129.
The subsequent21488/28832-byte pair contains131 original glyph outlines.
[Original ROM/outline analysis](../../runtime/path.md) identifies the battery
communication warning; the finite affine/nonzero path model now renders that
text into the same target and reaches original IRQ129. The input image remains
unchanged. The subsequent824/1112-byte pair contains five gray rectangles:
four two-pixel border strips and a72x72 patch, with relative H/V segments and
mixed `ffe6e6e6`/`ffb4b4b4` paints.
Relative H/V, even-odd filling and ordered opaque paints now process this pair
through original IRQ129 too. The subsequent176/336-byte inline A8 path now
also completes through IRQ129, adding the red icon with paint `ff4848f0`.
The subsequent224/1736-byte pair now also completes: a separate1024-wide RGBA
source `bf309600` and YUV105 targets `cc18b000/cc235800`. The next pair has
the same lengths,720x480 source `bec5f200`, scaling and YUVA107 targets
`cb82b000/cb980000` and now completes through original IRQ129. The following
176/1552-byte pair uses warning source`c175d600`, SRC_OVER and inherited107
target state and now completes too. Its actual packed output contains the
scaled warning. Manager subsequently returns to0/5/0/2 with zero GUI gate/
pending3, without another graphics rejection in the bounded window.
See [stream semantics](../../runtime/stream.md) and the
[current frontier](../../validation/frontier.md) for the distinct native and
original-code execution receipts. No first frame is established.

## Continuing the functional route

The first profile validates **initialization progress**, not complete
calibration or all later arithmetic. Scalar hypotheses must be checked as
execution reaches their consumers, especially divisors, indices, limits and
selectors. Additional records need finite constraints and original-execution
evidence; they are not covered by an automatic all-zero fallback.

The176/1552-byte continuation now has source-backed finite processing and
native coverage. The next measured boundary is worker0's class1 policy/event
loop; GUI readiness remains unarmed, rather than another graphics fault.
Completion follows successful modeled processing; acknowledging an unknown
stream alone remains insufficient. The [battery classification](../../runtime/compatibility.md)
is a separate upstream boundary from graphics completion.

An authentic capture remains valuable for fidelity and replacing hypotheses.
It is optional for continuing functional emulation, and its absence does not
justify bypassing the original parser or scheduler.
