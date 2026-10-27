# Adaptive monochrome filtering

The paired pixel operator interprets the C1.70 MAS filter packet as a bounded
adaptive monochrome filter. Its parameter representation and software producer
are source-established; the filter equation, control meanings, intermediate
precision and position in the pixel pipeline are explicit functional hypotheses.
This contract does not claim Nikon denoising fidelity or establish GUI progress.

## Producer and parameter representation

Code addresses in this section are C1.70 MAS CPU virtual addresses. Register
offsets are hexadecimal, relative to paired bank PA `0x20380000` or
`0x203c0000`. Register accesses are aligned little-endian 32-bit words.

`0x102d3874` calls `0x102f7d2e`, which calls `0x102f7a6c` to construct the
packet. The latter selects one of thirteen tables through the pointer array
at ROM VA `0x11a9874c`. Each table has 55 rows of 29 unsigned 16-bit values.
The input index at context pointer `[82]+3` selects adjacent rows in steps of
two; odd indices interpolate their midpoint with nearest rounding. Indices
beyond the table clamp to its last row. `0x1036448e` constructs this index
through a clamped lookup of a converted input parameter. It is a changing
calculation input, rather than a register enum or a buffer address.

The configuration publisher `0x102e49fe` passes root configuration entry 12 to
`0x102e4914`. That pointer refers to compact state offset 98. The publisher
calls `0x1036af16` for control offset `0x594`, then `0x1036af3a` for three
24-byte records at compact offsets 208, 232 and 256. Each record has twelve
unsigned 16-bit values, packed high-half first into six interleaved words:

| Pair | Three bank word offsets | Compact record elements |
| --- | --- | --- |
| 0 | `0x598`, `0x59c`, `0x5a0` | 0, 1 |
| 1 | `0x5a4`, `0x5a8`, `0x5ac` | 2, 3 |
| 2 | `0x5b0`, `0x5b4`, `0x5b8` | 4, 5 |
| 3 | `0x5bc`, `0x5c0`, `0x5c4` | 6, 7 |
| 4 | `0x5c8`, `0x5cc`, `0x5d0` | 8, 9 |
| 5 | `0x5d4`, `0x5d8`, `0x5dc` | 10, 11 |

The inspected tables duplicate each parameter across all three records and
both halves. Their first four parameters are at most 16383, with the third
at most 15. `0x102f7960` fixes elements 8 and 9 to zero and elements 10 and 11
to 16. `0x1036aed8` publishes the neighboring `0x588` scalar as `14/14`.
These observations support a finite 14-bit parameter convention; they do not
recover the hardware's arithmetic units or Bayer/channel identities.

The inspected producer constructs `0x594` as either zero or six from two
equal booleans. A separate value from the same interpolated row reaches
compact offset 380, then `0x1036b12a` publishes it at bank offset `0x608`.
This scalar is zero or one in all inspected tables. Its software production
does not prove an enable or bypass meaning.

For example, table 7 at VA `0x11b0f026` produces `0x1a6a` at index 21 by
interpolating rows 10 and 11, and `0x17c7` at index 22 from row 11. Each
record is then `[v,v,50,50,1,1,50,50,0,0,16,16]`, in decimal except `v`.
Consequently the changing first parameter is published at all of `0x598`,
`0x59c` and `0x5a0`; accepting one changed register constant would omit the
packet relationship.

## Functional operator

The pure API represents the finite monochrome parameters as
`NikonPairedFilter`: `enabled`, `refine`, `clipped`, and unsigned scalars
`retain`, `base`, `radius`, `slope`. The scalar limits are 16383, except
`radius`, whose limit is 15. A disabled filter requires every scalar and both
other booleans to be zero. `nikon_paired_filter_valid` is shared with device
admission. Wire completeness, equality between duplicated records, and the
zero-offset/unity-strength tail are device responsibilities; the pixel API
does not interpret register words or calibration files.

The chosen wire interpretation maps `0x594=0/6` to `refine=false/true` and
`0x608=0/1` to `clipped=false/true`. Neither zero control disables the filter.
The first four record parameters become `retain`, `base`, `radius`, `slope`.
This mapping is a declared functional choice, not recovered silicon semantics.

The filter operates after the existing five-tap spatial stage and before
temporal history, half-resolution work outputs, the grid and main color output.
For each RAW12 center sample, use the 14-bit intermediate `s = sample * 4`.
One normal pass reads the four axial neighbors at the selected radius. A
refined operation performs a horizontal two-neighbor pass, then a vertical
two-neighbor pass. Both passes read immutable input images; the second pass
reads the first pass's rounded RAW12 output. Coordinates outside the image
replicate the nearest edge sample.

Each pass computes the following equations. All constants here are decimal;
`nearest` rounds signed ties away from zero.

```text
residual = mean(neighbor_samples * 4) - s
threshold = min(16383, base + nearest(slope * s / 16383))
if clipped:
    residual = clamp(residual, -threshold, threshold)
correction = nearest(residual * (16383 - retain) / 16383)
out14 = clamp(s + correction, 0, 16383)
output = min(4095, nearest(out14 / 4))
```

Neighbor sums divide exactly in this intermediate domain. Radius zero and
`retain=16383` are identity operations. At fixed residual limits, decreasing
`retain` increases smoothing; increasing `base` or `slope` expands the allowed
correction. This behavior agrees qualitatively with the table trends, but
does not establish their original physical purpose. Threshold conversion,
edge treatment, pass order and the meaning of `0x608` remain hypotheses.

The largest product is `16380 * 16383`, within signed 32-bit range. One bounded
scratch image separates input and output. Allocation or validation failure
occurs before any public output is changed. The pure operator owns no timers,
DMA, events or register state; device snapshot, changed-write cancellation,
reset and completion remain device responsibilities.

## Validation scope and limits

The numerical fixtures compare complete work luma, residual, per-pixel grid,
Y and UV output against an independent quotient/remainder reference, including
execution with prior history. Fixed non-degenerate step vectors additionally
anchor the reference: an unclipped radius-one pass produces
`[128,1152,2943,3967]`, refinement produces `[256,1792,2304,3839]`, and a
clipped threshold of four produces `[1,513,3582,4094]` in each row.

The fixture also checks constant and impulse input, a noise energy reduction,
all four `refine/clipped` combinations, independent parameter effects, border
replication, radius zero, full retention, endpoint samples, rounding, and
invalid configurations leaving outputs unchanged. These are synthetic numerical
requirements, not evidence of a completed original startup. Native verdicts
and any unchanged-firmware continuation belong to the integration validation.

The fixed [Z7 ISP implementation](https://github.com/WonderLantern/z7-qemu/blob/97abf0b81cd3211f8374a38987a0beaf4549b90f/qemu/nikon-z7-isp.h)
does not implement proprietary denoising/calibration operators. The fixed
[Z8 implementation](https://github.com/RdWing/z8-qemu/blob/9881d71053dacadbb81d2b352533ccf6a52cd604/qemu/nikon-z8-lab.c#L14069)
likewise leaves parameter LUT and denoising equations unsupported. They provide
no recovered equation for this packet. A numerical consumer, register manual,
or discriminating physical input/output measurement could revise this bounded
functional model; no such physical-fidelity claim is made here.

## Separate interpolated parameter family

The later original movie boundary at bank `660..678` belongs to another packet,
not to this filter's six pairs. C1.70 MAS `102d3874 → 103626fe → 103622d4 →
103620e6` selects two rows from nineteen 42-byte records and interpolates 21
signed16-bit fields with denominator6. The first 20 populate compact offsets
68..94; field20 is internal state. Four directories and mode branches select
the record family. Setter `1036adac` consumes this 28-byte compact packet through
root config[10]. With compact base P and decimal byte offsets, its packing is:

| Bank word | Packed fields |
| --- | --- |
| `660` | `u16(P+68)<<16 \| u16(P+70)` |
| `664` | `(u16(P+72)&0xfff)<<16 \| (u16(P+74)&0xfff)` |
| `668` | High to low bytes: P76, P77, P78, P83 |
| `66c` | High to low bytes: P79, P84, P80, P85 |
| `670` | High to low bytes: P81, P86, P82, P87 |
| `674` | High to low bytes: P89, P88, zero, P90 |
| `678` | `u16(P+92)<<16 \| P94`; bits15..8 zero |

P91/P95 are padding. Adjacent `648..65c` belongs to the separate setter
`1036b29a`; `67c` comes from `1036ae6c`. They are not one admission range.
The observed `660:20→18` change means two scalars `[0,32]→[0,24]`, not a new
register enum. The selected tables establish continuous values and packing,
but no recovered numerical consumer establishes the image operator. These
words therefore retain opaque-profile validation rather than being accepted
and ignored or relabeled as this filter's coefficients.
