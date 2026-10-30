# Median residual detail processing

The paired RAW12 model interprets C1.70 MAS bank words `73c..758` as a finite
median-residual correction stage. The software producer, packing, scalar bounds
and duplicate relationships are source-established. The name, field roles,
neighborhood, numerical scales and stage position are explicit functional
hypotheses. This optically monochrome model does not establish physical Nikon
detail processing, Bayer reconstruction or exposure fidelity.

## Producer and complete parameter group

Code and ROM addresses below are C1.70 MAS CPU virtual addresses. Register
offsets are hexadecimal, relative to board PA `20380000` or `203c0000`.
Accesses are aligned little-endian 32-bit transactions. Compact byte offsets,
table field indices and scalar ranges are decimal.

`102d3874` calls `102f78b0`, which invokes `10363a6c` and `102f734e`.
`10363a6c` selects one of four four-pointer directories at VA `11c0ae18`
using saturated normalized metadata byte6. A selector from metadata byte7 and
byte6 at VA `11b13b04` chooses a table within that directory. Each table has
nineteen 114-byte rows of57 signed16-bit fields. Normalized metadata byte24
selects adjacent rows in steps of6, with row indices clamped to18 and nearest
interpolation using denominator6. The model consumes the interpolated values
actually published through MMIO, without reading factory tables.

Call these source fields `q0..q56`. This article covers the coherent publication
of `q6..q28`, excluding the earlier pedestal/gain inputs and later independent
parameters. VA `102bf33c` selects compact state `P+382` as configuration entry13.
`102f734e` writes the group at compact offsets434..461. `102e49fe` publishes it
through `1036b49c`, `1036b4de`, `1036b4fc`, `1036b51e` and `1036b554`.

| Bank word | Fields, high to low | Qualification |
| --- | --- | --- |
| `73c` | zero byte, `q6`, `q7`, `q8` | `00060301` |
| `740` | zero upper half, `q10`, `q9` | zero |
| `744` | `q11`, `q12`, `q13`, `q14` | `64`, `4..31`, `6`, `0..32` |
| `748` | `q15`, `q16`, `q17`, `q18` | `4`, three equal values in8..16 |
| `74c` | zero byte, `q19`, `q20:u16` | `00040010` |
| `750` | zero byte, `q21`, `q22:u16` | zero |
| `754` | zero byte, `q23`, `q24:u16` | `001f0000` |
| `758` | `q25`, `q26`, `q27`, `q28` | `1f100804` |

For `744`, `1036b4de` preserves little-endian compact bytes439..442, whose
contents are respectively `q14,q13,q12,q11`. The low byte is a numerical
interpolated field, not a recovered enable bit. Its inspected source rows use
0,4,8,16,32, and interpolation can produce1. The observed M `40040600` versus
AUTO `40040601` therefore does not justify a two-value control whitelist.

The bounds are extrema across four directories, four families per directory
and nineteen rows per family. Convex interpolation with nearest rounding stays
within them. In every inspected family, `q12`, `q14` and `q16..q18` are
nondecreasing with row index; `q16=q17=q18` throughout interpolation. The fixed
relations `q11=1<<q13=64` and `q20=1<<q19=16` support a finite normalized-weight
hypothesis. They do not prove fixed-point units or hardware field names.
Admission uses the bounded scalar domain and all listed fixed values, rather
than a factory-row whitelist; not every admitted combination is claimed to
occur in firmware.

The following `75c..778` parameters originate in the same57-field source table
but have no implemented numerical equation in this stage. They retain strict
opaque-profile validation. Changing this group's scalar does not authorize
ignoring or inventing uses for those later fields.

## Functional numerical hypothesis

The pure `NikonPairedDetail` API represents the three changing roles as
`limit=q12`, `strength=q14` and `lane=q16=q17=q18`. The device validates every
fixed field, zero byte and duplicate before creating that representation.
There is no inferred enable bit and no omitted-group or all-zero fallback.

The stage follows the [interpolated denoise](noise.md) and precedes temporal
history, half-resolution WORK outputs, grid averaging and MAIN color output.
It reads an immutable radius-one3x3 neighborhood including the center,
replicating the nearest edge pixel. In the sample-times-four domain:

```text
s = RAW12 * 4
m = median of the nine neighbor samples * 4
d = clamp(m-s, -limit, limit)
correction = nearest(d * strength * lane / (64*16))
out14 = clamp(s+correction, 0, 16383)
out12 = min(4095, nearest(out14/4))
```

Every nearest divide uses signed ties away from zero. Normalization by64 and16,
the residual limit's sample units and the median neighborhood are chosen
functional meanings. They qualitatively follow the source trends: larger
limits admit greater corrections, and larger strength/common-lane values
increase the correction toward a robust local estimate. The maximum product
is `31*32*16=15872`, within signed32. The ratio is at most one half; the output
cannot overshoot the local median before rounding. Constants remain exact,
and an already monotone, row-constant edge is retained by this median stage.

Strength0 is a valid numerical endpoint. It still follows the same neighborhood
and arithmetic path, giving exactly zero correction. Synthetic callers and
qtest fixtures explicitly supply `limit4,strength0,lane8` with the complete
fixed packet. Zeroing the whole API structure or register group is invalid.
This distinguishes a legitimate zero scalar from treating unknown configuration
as identity processing.

The implementation reuses the two existing DPC/filtered scratch images.
All configuration and input validation and scratch allocations occur before
public outputs are modified. The stage does not change color transforms,
invent image content, own a timer or produce events.

## Publication, reset and cancellation

All eight words must have been published since reset before a job qualifies.
Writes of zero words count as publication but do not exempt missing words or
invalid values. Initial admission rejects incomplete packets, changed fixed
fields, nonzero reserved bytes, unequal lanes, limits outside4..31, strengths above32
and lanes outside8..16. Parameters are decoded into the existing job snapshot.

Online detail uploads follow
[online numerical uploads](pixels.md#online-numerical-uploads). A complete
changed packet advances the bank's history revision and clears future history
addresses; the owned snapshot may finish its DMA/IRQ but cannot register
history under the old revision. Stop cancels pending work and clears open
online rounds; reset also clears the words and publication coverage.
The stage relies on the existing input DMA, complete numerical output and
result publication path. Register writes never manufacture completion, IRQs
or input readiness.

## Falsifiable validation scope

Unit fixtures use an independent standard-library median and quotient/remainder
rounding reference. They compare complete half luma, residual, per-pixel grid,
MAIN Y and UV, including prior history and composition with the preceding
adaptive filter and denoise stages. Fixed positive/negative impulses anchor
the clipped signed half-way correction: `+/-31*32*16/1024` rounds to`+/-16` in
the sample-times-four domain. Other assertions cover replicated corners,
constant endpoints, each changing scalar's effect, zero strength, noise-energy
reduction, monotone edges and invalid configurations leaving outputs unchanged.

The ordinary-MMIO qtest fixtures submit positive/negative source-image impulses
and check changed MAIN and WORK results, output guards and IRQ acknowledgement.
Boundary fixtures check every word, duplicate lanes, reserved bits, malformed
scalars, absent/partial publication after reset, same-value retention,
future-snapshot publication, history revision changes and reset readback.
These are specified tests; native results and unchanged-firmware continuation
belong to separate integration receipts.

The fixtures can falsify this implementation or its declared finite properties.
They cannot establish physical Nikon semantics. A bound register manual,
numerical CPU consumer or discriminating before/after physical pixel measurements
could revise the role, scale and neighborhood hypotheses; GUI continuation
alone cannot validate them.
