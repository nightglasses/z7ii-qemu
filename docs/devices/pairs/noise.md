# Interpolated noise reduction

The paired monochrome RAW12 model interprets the separate C1.70 MAS
`660..678` scalar packet as a bounded edge-preserving denoise operation.
Its software producer, packing and finite scalar relationships are
source-established. Field roles, sample units, neighborhood and arithmetic
are explicit functional hypotheses. This is not a reconstruction of Nikon's
physical denoising algorithm, and fixture results do not establish startup.

## Source and representation

All code and ROM addresses below are C1.70 MAS CPU virtual addresses. Bank
offsets are hexadecimal, relative to board PA `20380000` or `203c0000`.
Transactions are aligned, little-endian 32-bit words. Compact packet byte
offsets and scalar ranges are decimal.

`102d3874` calls `103626fe`, then `103622d4` and `103620e6`. The producer
selects one of four 84-pointer directories at VA `11c0ae58`, with the directory
selected by saturated normalized metadata byte6. A selector obtained from
metadata byte7 and byte6 chooses four possible table columns; root input byte114
chooses within that group. Each selected table has nineteen 42-byte rows of
21 signed16-bit fields. Metadata byte24 selects adjacent rows in steps of6;
the producer interpolates each field with denominator6 and nearest rounding,
clamping row indices to18. The denominator is the interpolation scale:
there is no additional division or multiplication by6 in the pixel model.

The first twenty interpolated fields `c0..c19` populate compact state `P+68`
through `P+92`; field20 is internal state. `103646fe` sets `P+94=0`.
`102e49fe` publishes root configuration entry10 using `1036adac`:

| Bank word | High to low fields |
| --- | --- |
| `660` | `c0:u16`, `c1:u16` |
| `664` | `c2:u12` in bits27..16, `c3:u12` in bits11..0; other bits zero |
| `668` | `c4:u8`, `c5:u8`, `c6:u8`, `c7:u8` |
| `66c` | `c8:u8`, `c9:u8`, `c10:u8`, `c11:u8` |
| `670` | `c12:u8`, `c13:u8`, `c14:u8`, `c15:u8` |
| `674` | `c16:u8`, `c17:u8`, zero byte, `c18:u8` |
| `678` | `c19:u16`, zero byte, `P94:u8` |

`P91/P95` are padding. In particular, `REV16(u16(P+88)) <<16` publishes
`c16` before `c17` at `674`. Adjacent `648..65c` has a separate producer
`1036b29a`; `67c` is published separately by `1036ae6c`. Neither belongs to
this numerical packet or its admission range.

The inspected four directories, sixteen table families per directory and
nineteen rows per table establish these bounds and relationships:

| Fields | Inspected range or relationship | Functional role chosen here |
| --- | --- | --- |
| `c0` | zero | Fixed family qualification only |
| `c1` | 7..103 | Base difference tolerance |
| `c2/c3` | 1..43 / 3..45; `c3=c2+2`, both at most `c1` | Lower/upper transition offsets |
| `c4/c5` | 3..17 / 1..10; `c4>=c5` | Axial/diagonal weights |
| `c6/c7` | 0..12; equal | Common tolerance bias |
| `c8..c11` | 8..31; all equal | Common center retention weight |
| `c12..c15` | zero | Fixed family qualification only |
| `c16/c17/c18` | 3/2/1 | Fixed family qualification only |
| `c19`, `P94` | zero | Fixed family qualification only |

The ranges are extrema observed across the source ROM rows, not recovered
silicon operating limits. The producer's convex interpolation with nearest
rounding stays within those extrema for every normalized index0..108 and
preserves the listed equalities and inequalities. Production admission uses
that bounded interpolated domain; it does not impose a whitelist of observed
rows or infer that every combination within the domain was emitted by firmware.

Within every inspected family, `c1..c7` are nondecreasing with row index and
`c8..c11` are nonincreasing. The same root byte114 selects tables used by
`102f64ee` to emit XMP luminance/color noise-reduction controls. These facts
support the direction of a denoise hypothesis: growing tolerance and weaker
center retention at higher indices. They do not recover individual hardware
field names, physical units, Bayer channels or the pixel equation.

## Finite numerical contract

The pure `NikonPairedDenoise` API receives only decoded, qualified scalars:
`threshold=c1`, `low=c2`, `high=c3`, `axial=c4`, `diagonal=c5`,
`bias=c6=c7`, `retain=c8=c9=c10=c11`. Fixed fields remain validated at the
device boundary; they are not relabeled as enable bits. The device does not
read ROM tables or replace the actual MMIO values with a selected factory row.

The operator runs after the existing [adaptive filter](filter.md) and before
temporal blending, half-resolution work outputs, grid averaging and MAIN color
output. This placement and an optically monochrome intermediate are functional
choices. For every center sample, read immutable radius-one axial and diagonal
neighbors, replicating the nearest pixel at image boundaries. Define:

```text
s = RAW12 * 4
L = threshold + low + bias
U = threshold + high + bias

gate(d) = 1                    if abs(d) <= L
          (U-abs(d))/(U-L)      if L < abs(d) < U
          0                    if abs(d) >= U

A = mean over four axial neighbors of gate(neighbor*4-s)*(neighbor*4-s)
D = mean over four diagonal neighbors of gate(neighbor*4-s)*(neighbor*4-s)
correction = nearest((axial*A + diagonal*D)/(retain+axial+diagonal))
out14 = clamp(s+correction, 0, 16383)
out12 = min(4095, nearest(out14/4))
```

Keep the neighborhood means and gates rational until the correction divide.
All nearest divisions use signed ties away from zero. The implementation uses
integer gate numerators over `U-L=2`, combining the two means into one divide;
the largest conservative numerator is
`4*2*16380*(17+10)=3538080`, within signed32. The existing DPC scratch image
holds the complete result, so no additional image allocation or in-place
neighbor feedback is introduced.

Constant inputs remain exact. Neighbors beyond `U` contribute no correction,
so this stage preserves sufficiently large edges already present at its input;
it does not undo the earlier spatial stage's blur. Positive center retention
makes the result a convex local blend before rounding. This finite mapping
responds to the whole accepted scalar family rather than admitting particular
values such as `660=15`, guessing a bit0 enable or ignoring the packet.

All-zero parameters are invalid. Synthetic fixtures explicitly publish the
finite tuple `threshold7, low1, high3, axial3, diagonal1, bias0, retain31`.
Its unrounded correction magnitude is at most `4*8/35 < 1` in the
sample-times-four domain, and the rounded correction is at most1. It quantizes
back to the original RAW12 sample through the same operator. This is an
explicit numerical fixture input, not an unknown hardware bypass or an omitted
configuration fallback.

## Publication and lifecycle

All seven words must have been written since device reset before a job can
qualify. Writing a zero word records its publication; it does not exempt that
word's value or any missing word from validation. Each admitted group satisfies
all listed bounds, equalities, fixed values and zero bits. Partial groups,
unknown fixed-field changes and inconsistent duplicate scalars reject before
output publication. The decoder admits a bounded self-consistent scalar domain;
it does not claim that every admitted combination occurs in a factory table.

A job snapshots the decoded parameters with its existing pixel configuration.
Unchanged parameter writes retain the running job. A changed NR word uses the
existing configuration cancellation and history invalidation path. Reset
cancels pending work and clears both the seven words and their publication
mask. Only completed input DMA and numerical processing publish existing
outputs/results/IRQ causes; parameter writes do not create completion events.

## Falsifiable validation

The synthetic unit reference independently enumerates axial/diagonal offsets
and uses quotient/remainder nearest division. It compares complete half luma,
residual, per-pixel grid, MAIN Y and UV, including prior history and execution
after the existing adaptive filter. Fixed low-contrast step vectors anchor
nonidentity output, exact lower/upper gate boundaries, the half-weight transition
and positive/negative half-way correction rounding. Other cases check constant
endpoints, noise-energy reduction, large-edge preservation, every dynamic
scalar's effect and invalid configurations leaving all outputs unchanged.

The qtest fixtures publish synthetic groups through ordinary MMIO and source
input. They check changed MAIN/WORK pixels, result/IRQ acknowledgement, incomplete
and zero groups, unknown fixed fields, duplicate mismatches, unchanged writes,
changed-write cancellation and reset. These are specified validation targets;
native execution and unchanged-firmware continuation are recorded separately
by the integration owner.

These observations can falsify the implementation or its declared functional
properties. They cannot prove physical Nikon fidelity. A numerical CPU consumer,
a register manual bound to this packet, or discriminating physical before/after
pixel measurements could revise the field-role and equation hypotheses. A
successful GUI continuation alone cannot establish their physical correctness.
