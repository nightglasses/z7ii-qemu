# Axial residual filtering

The paired model interprets the complete eight-word `0x75c..0x778` suffix of
the interpolated 57-field packet as a bounded axial residual filter. Its wire
packing and source-family ranges are established from the original producer.
Its equation, units and placement in the image pipeline are explicit functional
hypotheses. This model does not claim recovery of the ASIC algorithm, physical
image quality, complete startup or recording behavior.

## Source and wire contract

The [packet producer](filter.md#next-interpolated-packet) is C1.70 MAS CPU
VA `0x102d3874 → 0x102f78b0 → 0x10363a6c/0x102f734e`. The configuration
publisher joins root entry 13 through `0x102bf33c`, `0x102e49fe` and
`0x1036b4de`. The suffix represents fields q29 through q50; it is separate
from the preceding radial/detail fields and from the `0x594` adaptive packet.

Offsets below are hexadecimal, relative to paired bank PA `0x20380000` or
`0x203c0000`. Accesses are aligned little-endian 32-bit words. Scalar ranges
and byte values in the table are decimal; packed constants have a `0x` prefix.

| Word | Fields, most significant first | Complete finite contract |
| --- | --- | --- |
| `0x75c` | q29/q30, two u16 | base 50–300; fixed 30 |
| `0x760` | q31/q32, two u16 | slope 60–4095; cap 16–4095 |
| `0x764` | q33/q34, two u16 | weight 100–1024; fixed 4095 |
| `0x768` | q35/q36/q37/q38, four u8 | precision 8–10; fixed 31/12/14 |
| `0x76c` | q39/q40/q41/q42, four u8 | fixed 4; dose 0–15; zero/zero |
| `0x770` | q43–q46 | exact `0x08080808` |
| `0x774` | q47/q48 | exact `0x00000808` |
| `0x778` | q49/q50 | exact `0x0af00020` |

In the inspected source tables, slope, cap, weight and dose increase with the
row index; precision and base decrease. Interpolation permits graded dose
values, so the implementation does not reduce it to an enable bit or whitelist
the previously observed zero and one. Every field is validated, including the
fixed scalars, zero bytes and packed constants. All eight words must have been
written in the current device reset epoch. An omitted or partially published
packet rejects even when its available words happen to match the profile.

These eight interpreted calibration-file words must be zero; original guest
writes supply them. A changed word or first write of a previously unwritten
word cancels owned work and invalidates numerical history through the existing
configuration lifecycle. Equal writes to a completely published packet preserve
active work and history. Whole-device reset clears coverage and words. None of
these configuration writes produces a numerical completion or an IRQ.

## Finite numerical operator

The public scalar representation is `NikonPairedAdaptive`. It runs after detail
and before temporal history. For mono it uses the full image. For CFA it uses
each existing same-color phase plane independently, preserving the color
separation described in [functional CFA color](color.md). Inputs are immutable
during a pass; edges replicate the nearest sample on that plane.

For a RAW12 center `c` and its four axial neighbors, define:

```text
n = left + right + up + down - 4*c
t = min(cap, base + nearest(slope*c / 4095))
w = weight*dose
r = 16*(1 << precision)
correction = nearest(clamp(n, -4*t, 4*t)*w / (4*(r+w)))
output = clamp(c + correction, 0, 4095)
```

Signed ties round away from zero. The residual `n/4` stays rational until the
final division; there is no intermediate integer axial-mean rounding. Signed
64-bit intermediates cover the complete supported ranges. The positive
retention `r` makes the correction a bounded fraction of the clipped residual.
Dose zero gives zero correction through the same equation. Positive doses
produce a graded response whose quantization depends on the actual pixels.
The synthetic neutral scalar tuple is
`{base=50,slope=60,cap=16,weight=100,precision=10,dose=0}`. A neutral wire
fixture may select other valid positive scalars with dose zero; it still must
publish and validate the complete eight-word packet.

All six dynamic scalars participate in the equation. WORK means/residuals and
grid means derive from its actual output through the existing temporal path.
MAIN then uses the existing live color controls, matrices and NV12 conversion.
The pass alternates the existing image scratch buffers; it introduces no
per-frame scheduler, hidden input, new completion cause or ownership shortcut.

## Validation scope

Synthetic unit cases compare MAIN, WORK and per-pixel grid values against an
independent rational oracle, including signed half ties, fractional axial
residuals, borders, constants, noisy inputs, all six dynamic scalars and doses
0 through 15. A CFA case compares four independently filtered phase planes.
Invalid scalar ranges leave all outputs unchanged.

Native integration fixtures use actual nonuniform 64×36 PGM input, CMOS and
RAW12 DMA, then check MAIN pixels and padding, WORK/residuals, a packed grid
cell and the existing masked IRQ/W1C path. Separate cases exercise malformed
words, absent/partial coverage, equal versus changed writes, reset cancellation
and preservation versus invalidation of previously published history. These are
fixture contracts, not evidence of a physical-camera equation or unchanged
firmware progress. The integration owner records native execution and original
firmware observations separately.
