# Original filled glyph paths

The captured 131 paths support a finite **original-outline, solid-color,
nonzero-winding fill** implementation. They correspond to the firmware's
battery communication warning, not evidence of a normal ready screen. Use the
guest's segment/coordinate arrays; a host font or prewritten text image would
replace the observed producer rather than execute its graphics contract.

## Scope and receipts

Direct inputs are `work/runs/graypaths-20261022/{entry.json,setup.bin,draw.bin}`
and `work/runs/graycontext-20261022/nextcommands.json`. They record original
MAS submission at VA `106ee04a`, LR `106eeab9`, source buffers `bd0e00f0` and
`bd1601f0`, setup21,488 and draw28,832 bytes. The unchanged C1.70 MAS payload
is `work/runs/descriptor17-z7cross-mac1/02_eg1840_mas_01700.payload.bin`, base
VA `10020000`. Addresses below are original MAS VAs, not physical registers.

`work/runs/pathsemantics-20261022/` retains `check.py/check.json`, five original
function decompilations and the inspected fixed public `z7-gpu.h`. The check
validates coordinate consumption, segment counts, finite arrays, repeated
glyph correspondence, ROM string locations and actual command operands. It
does not render, run a guest or modify IDA.

## Segment and transform semantics

All captured segment codes are absolute: 187 close (`00`), 187 move (`02`),
824 line (`04`) and 1,729 quadratic (`0a`). They consume exactly 8,938 float
coordinates across all 131 paths. The meanings agree with the fixed
[Z7 path implementation](https://github.com/WonderLantern/z7-qemu/blob/97abf0b81cd3211f8374a38987a0beaf4549b90f/qemu/nikon-z7-gpu.h#L162-L235)
and [Khronos OpenVG path definitions](https://registry.khronos.org/OpenVG/specs/openvg-1.1.pdf):

| Code | Coordinates consumed | Meaning |
|---|---:|---|
| `00` | 0 | Close current contour to its move point. |
| `02` | 2 | Move to `(x,y)`; start a contour. |
| `04` | 2 | Straight line to `(x,y)`. |
| `0a` | 4 | Quadratic Bézier: control `(qx,qy)`, endpoint `(x,y)`. |

The nine command matrix registers `41..49` are column-major:

```text
w  = m43*x + m46*y + m49
x' = (m41*x + m44*y + m47) / w
y' = (m42*x + m45*y + m48) / w
```

Every captured matrix has diagonal `(27,36,1)`, zero shear/perspective and
varying translations. The first path has translation `(200,350.3232421875)`;
the six successive line baselines decrease by approximately48 to110.323.
The first outline has a T-shaped top bar at positive glyph y. These facts
identify logical positive y as upward for the text.

**Target-memory rows and displayed rows must remain distinct.** The fixed
[Z7 edge transform](https://github.com/WonderLantern/z7-qemu/blob/97abf0b81cd3211f8374a38987a0beaf4549b90f/qemu/nikon-z7-gpu.h#L61-L72)
uses the matrix directly; its rasterizer writes row `iy` without a height-y
flip. Its separate [presentation code](https://github.com/WonderLantern/z7-qemu/blob/97abf0b81cd3211f8374a38987a0beaf4549b90f/qemu/nikon-z7-gpu.h#L455-L477)
then reads source row `H-1-y` for a top-down console/PPM. Consequently a Cairo
mask used for target DMA should preserve the direct transformed y convention;
any display inversion belongs to the independently established presentation
or scanout interpretation. This reference implementation does not prove Z7II
physical panel wiring or orientation.

## Fill rule and drawing state

Do not infer a fill rule from draw state `108` or setup state `2100`.
The stronger original-producer chain is:

1. `1068c5ae` obtains fill-rule enum from state, then at `1068c6ee` calls
   `10645b2e` with argument2 equal to `(rule == 6401)`; decimal6401 is `1901`.
2. `10645b2e` packs that boolean into bit8 of the final block operand at
   `10645bdc`; the six-operand command `00a0f02e` writes registers `a0..a5`.
3. Every captured block has final operand `00040102`: registera5 bit8=1.
   Khronos defines `1901` as `VG_NON_ZERO`, versus `1900` `VG_EVEN_ODD`.

Thus this capture selects **nonzero winding**. The fixed Z7 renderer's
scanline algorithm also fills where winding is nonzero, but it does not branch
on the even-odd selection; that implementation alone would be insufficient
evidence for other captures. Setup register65 bit8 selects fill. Its bits13:12
receive the low two quality flags in `1068c5ae`; `2100` must not be mistaken
for an even-odd/nonzero selector.

Direct command observations for all131 draws:

- register184 low byte8: solid paint path, rather than texture sampling;
- register185=`ff000000`: packed little-endian RGBA, black with opaque alpha;
- register186=`108`: fixed observed state; individual bit meanings remain
  unverified and need not be generalized;
- register1a0 blend selector bits19:16=1, matching `SRC_OVER` in the
  [fixed Z7 blend implementation](https://github.com/WonderLantern/z7-qemu/blob/97abf0b81cd3211f8374a38987a0beaf4549b90f/qemu/nikon-z7-gpu.h#L305-L333);
- destination format0 and address `c175d600`, distinct keys `10003..10085`;
- no new register1a3 extent write: this submission inherits the established
  720×480 target context. An isolated zero-initialized decoder cannot invent
  that inherited extent from these draw bytes alone.

The original `vgDrawGlyph` export entry at table VA `1239b090` pairs string
`1209c3e4` with Thumb function `10644523`. Its wrapper `10644522` calls
`10644dac`, which resolves glyph state, builds a transform, then reaches path
drawing via `1064c25e → 1064c170 → 1068c5ae`. This is an original glyph/path
producer route; no captured runtime stack in these files proves every glyph
took this exact route.

## Text correspondence and unknown font identity

The six original UTF-16LE strings occur consecutively in MAS ROM:

| MAS VA | Text | Nonspace glyphs |
|---|---|---:|
| `1109ecce` | This battery cannot be used. | 24 |
| `1109ed08` | It does not communicate | 20 |
| `1109ed38` | properly with this camera. | 23 |
| `1109ed6e` | To use the camera safely, | 21 |
| `1109eda2` | choose a battery designated | 24 |
| `1109edda` | for use in this camera. | 19 |

Those line counts equal the captured baseline groups and total131. Assigning
characters in this order gives24 unique characters; every repeated character
has exactly identical segment bytes and coordinate arrays, with zero
mismatches. This is strong static identification of the text, beyond visual
guessing. It does not identify the precise font family, a font file, glyph
index/cmap mapping, or the font record's ROM provenance. A scoped standard
sfnt-directory search found no validated embedded TrueType/OpenType object;
that is not a proof that the updater lacks fonts or encoded font data.

## Finite implementation boundary

Use the known target/context, original DMA arrays, bounded counts, exact
supported command states and byte masks. Convert each quadratic to an exact
cubic for Cairo (`C1=P0+2/3*(Q-P0)`, `C2=P2+2/3*(Q-P2)`), or use an equivalent
maintained path rasterizer. Select winding fill and preserve contour
orientation. Cairo may produce an A8 coverage mask; apply the original solid
RGBA source with SRC_OVER to the existing target. It must not choose a host
font, substitute strings, clear each glyph's bounding rectangle, or replace
the entire existing gray background.

Reject unsupported segments/states, nonfinite matrices/coordinates, count
mismatches, invalid contour sequences, missing inherited context, unsafe
DMA spans/aliases and incomplete streams before writing target memory.
Preflight all131 paths and output accesses before committing output; retain
reset/unrealize cancellation. Native fixtures should distinguish winding from
even-odd, quadratic from line approximations, holes/contour direction, direct
buffer y from display flip, and partial coverage over a nonblack destination.

The minimal functional model need not reproduce the reference's particular
4×4 supersampling or physical silicon coverage. Cairo antialiasing, rounding,
edge inclusion and compositing precision can differ; keep pixel-exact Nikon
claims separate. No screenshot, physical-camera capture, first-frame proof or
normal ready-state acceptance is supplied by this static research.

## Implemented bounded execution

The integrated model now accepts the captured protocol family with1..256
paths,41 setup words and55 draw words per path plus exact ending/alignment.
It requires the previously completed720x480 context, fixed target/format/state,
opaque SRC_OVER, either nonzero or even-odd selector, echoed opaque operands
and sequential keys. Relative horizontal07 and vertical09 segments consume
one scalar each in object space, then apply the affine transform. Other
relative segment codes remain unsupported.
Work bounds are64 segments and256 coordinate words per path; finite affine
matrix/input magnitudes are capped4096 and transformed coordinates8192.
These are functional limits, not recovered silicon capacities.

`hw/nikon/devices/path.c` uses [Cairo path/fill APIs](https://cairographics.org/manual/cairo-cairo-t.html)
to produce per-path A8 coverage with GRAY antialiasing and1/16-pixel
tolerance. The packet/DMA layer validates the entire batch, including
effective RAM backing aliases with the output. At deferred execution it
revalidates inputs, composites each opaque paint in command order into a
premultiplied RGBA scratch overlay, maps/prechecks the full target, then
applies that overlay with integer rounding. The later [SRC_OVER extension](../devices/transfer/over.md)
supports destination alpha0..255. Nonopaque source paint, unsupported segments/states,
perspective/nonfinite data and malformed/unsafe ranges remain errors.
Reset/unrealize and any late error leave the destination unchanged.

Old-parser red, independent backing-alias red and final nine-case native green
are recorded under `work/runs/pathmodel-20261022/`. With unchanged firmware
and the same explicit functional input, all131 original outlines render into
guest memory before original IRQ129. `render.json` records25958 changed panel
pixels, including11874 black and14084 fringe pixels; the six ROM strings are
visually readable in the captured buffer. `frame.bin` is the raw DMA output;
`buffer.png` encodes its direct rows and `preview.png` reverses them per the
fixed Z7 presentation convention. Neither PNG establishes a physical Z7II
scanout or normal ready screen.

The subsequent824/1112-byte batch has five even-odd relative
H/V rectangles and mixed gray paints: four light border strips and a72x72
background patch. Its complete commands and indirect arrays are retained in
`work/runs/pathboundary-20261022/`. The extended model processes them through
original IRQ129: all3328 border pixels become `ffe6e6e6`, the5184 patch pixels
are `ffb4b4b4`, and every other pixel matches the previous glyph frame.
The former168/232 gray rectangle now uses this same path processor; its
obsolete dedicated table/executor branch has been removed. Native red/green,
full-frame comparison and next abort are in `work/runs/coloredpaths-20261022/`.
The subsequent176/336-byte submission changes to an inline path and image
sampling state. The [finite A8 model](image.md) now completes it through
original IRQ129 too. A separate RGBA-to-YUV105 [conversion](conversion.md)
also completes, followed by720x480 scaling to YUVA107. The next176/1552-byte
SRC_OVER continuation also completes using retained107 state, producing the
scaled warning in the actual packed target. See the
[startup frontier](../validation/frontier.md).
[Battery classification](compatibility.md) remains a separate software issue;
successful warning rendering does not establish normal startup.
