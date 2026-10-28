# Finite post-color processing

The paired operator consumes four complete source-shaped parameter groups for
MAIN color processing. These controls operate on actual computed twelve-bit
YUV planes, before NV12 chroma averaging and byte conversion. WORK and
pre-color temporal history retain their existing data contracts. The separately
produced kind25 confidence grid is a read dependency. No parameter
write creates a frame, result, completion, IRQ or buffer release.

## Source boundaries

The following addresses are MAS C1.70 virtual code addresses. Register offsets
are hexadecimal within either paired bank; compact byte offsets are decimal.

| Complete register group | Producer and packing | Dynamic numerical fields |
| --- | --- | --- |
| `b38..b58`, nine words | `102e57a6` passes configuration21+84, compact780, to `1036bcde` | `b3c/b40` starts, `b44` steps, `b48` extents, `b4c` origins and `b58` strengths |
| `b94..bbc`, eleven words | `1036c02c`, `1036c0c0`, `1036c12a`, `1036c094` publish C, weights and adjacent controls | C and complementary `bac` low/high unsigned16 |
| `d20..d9c`, thirty-two words | Configuration40+28, compact924, is the110-byte input of `1036c260` | `d28` high/low byte |
| `dc0..dec`, twelve words | Configuration40+166 and scalar setters `1036c744/7a6/7e2/818/828` | Complete qualified family below |

`1037f624` computes both B58 magnitudes from normalized state and interpolated
tables. The ordinary branches clamp to0..256. The alternate low-half lookup
at `11acc8ea`, indices20..212, also stays within0..256. Neither component is a
boolean or an enumeration of the few values observed in startup.

`103801c8` constructs two complementary weights at compact850/852. Each is
within the selected0..256 profile and their sum must be256. `10380232` creates
C with first row/column `[1024,0,0]` and a variable bottom-right two-by-two
matrix. The source establishes that layout and coefficient packing; interpreting
that matrix as a centered chroma transform is a functional hypothesis. The old
RGB-domain C transform is removed rather than retained as a second behavior.

`103612ec` interpolates a43-field, nineteen-row unsigned16 family. Its fields1/2
become compact929/930 and D28's two bytes. `103611d4` can replace the second
component with a product of modifiers saturated to five bits. Both components
are bounded0..31; a changed value is not a new wire command.

`102f7e14` selects one of four52-byte table descriptors at `11c0b6f8`.
`102f8224` chooses two neighboring rows from the descriptor+36 nineteen-row,
six-byte table, with normalized byte24 divided by6. Bytes4/5 are interpolated
into compact1126/1127 and published as DD4/DD8. All76 inspected rows bound the
first field to3..18 and fix the second to5. Interpolation legitimately produces
intermediate values absent from individual rows. The source does not establish
their physical names, a filter radius, an enable bit or an ASIC equation.

### Complete DC0..DEC family

Configuration40 points to compact896. The normal publisher `102e563a` passes
its+166/+176/+182 records and+228 scalar tail to the setters above. Their full
twelve-word representation is:

| Word | Source representation | Admitted source domain |
| --- | --- | --- |
| DC0 | compact1062 high byte,1063 low byte | base10..200, slope0..4 |
| DC4 | compact1064 high byte,1065 low byte | base10..200, same slope as DC0 |
| DC8 | compact1066 | 0..31 |
| DCC | compact1067 | 0 |
| DD0 | compact1068/1069/1070 in bits16/8/0 | `080401` |
| DD4/DD8 | compact1126/1127 | 3..18 /5 |
| DDC | compact1072 unsigned16 in bits8..23,1076 low byte | scalar0..360, low byte0 |
| DE0 | compact1074 unsigned16 in bits8..23,1077 low byte | scalar0..511, low byte0 |
| DE4 | compact1128 high byte,1129 low byte | high128, low64 or128 |
| DE8 | compact1078/1079/1080 in bits16/8/0 | high1, remaining equal1 or3 |
| DEC | compact1081 high byte,1082 low byte | equal0 or16 |

All unmentioned upper bits are zero. `103612ec` interpolates fields38..42 of
the43-field family into1062..1066, by the same adjacent-row division by6 used
for D28. `103622d4` selects among four descriptor directories, four selector
families and four input variants, each with nineteen rows. Across those1216
rows, both base fields span10..200, the two slope fields are equal within0..4,
and the final field spans0..31. These are bounded interpolation domains, not a
list of accepted startup values. The same function fixes1067..1070 to0/8/4/1.

`102f819a` produces a signed saturated control at state2111 from three table
and normalized-state modifiers. `102f7f68` adds128, clamps to an unsigned8
index, and selects one16-byte row from the current descriptor's256-row table.
Row+4 supplies DDC. Four complete tables contain1024 rows in total. The same
row supplies the related DE4/DE8/DEC controls. Their only source tuples and DDC
scalar bounds are:

| DE4 low | DE8 middle/low | DEC high/low | DDC scalar |
| --- | --- | --- | --- |
|64|1/1|0/0|0..360|
|128|1/1|0/0|0..235|
|128|3/3|16/16|4..100|

`102f812c` constructs three modifiers from normalized state. `102f7f68`
multiplies them, rounds their product divided by16384 and saturates to9 bits
to produce DE0. When original-root byte117 is zero, that producer explicitly
sets the modifiers to zero. `102f809c` fixes the low bytes of DDC/DE0 to zero
and DE4's high byte to128. DE4's low byte is64 when row byte2 is1, otherwise128.
The source proves packing, arithmetic, ranges and these dependencies. It does
not identify a physical threshold, radius, mixing unit or hardware kernel.

Grid coordinates/dimensions, C, B58, BAC, D28 and the complete DC0..DEC family leave the explicit calibration
image and are decoded. Every other word in the four groups must still match
the complete declared virtual calibration profile. Unknown nonmatching controls
remain blocking. The calibration file must leave interpreted slots, including
all twelve DC0..DEC words, zero; it supplies no replacement guest writes.

## Numerical hypothesis

All divisions round to nearest with signed ties away from zero. Each spatial
pass reads an immutable plane, replicates edge samples and clamps its final
sample to0..4095. The following equations are selected functional laws, not
recovered silicon algorithms.

1. Apply A to actual RGB samples with the existing Q10 operation. Apply B in
   Q9, preserving signed chroma. Clamp Y to0..4095 and U/V to-2048..2047.
   C must preserve Y and have no luma/chroma cross terms. Calculate its signed
   two-dimensional Q10 chroma transform. With `o=BAC.low`, `t=BAC.high`, use
   `round((o*original + t*C(original))/256)` for each chroma component, then add
   the2048 center and clamp. Y is unchanged. Identity C therefore has no effect
   for any legal complementary weights. The association of low with original
   and high with transformed is explicit hypothesis.
2. B58 supplies a Y strength and a shared U/V strength. With center `c`, axial
   neighbor sum `s`, the selected Q8 strength `k`, and the bilinearly sampled
   confidence `m`, use `c + round((4*c-s)*k*m/(1024*4095))`. The enabled kind25
   map is an immutable packed12 input described in the [pixel contract](pixels.md).
   A zero map or zero strength is neutral; a full4095 map reproduces the prior
   unmodulated equation exactly. With grid control `12c=0`, that unmodulated
   equation applies. This is a declared confidence-residual law, not a recovered
   interpretation of R73 or the physical local-correction algorithm.
3. D28 supplies an axial mean-blend strength followed by a diagonal mean-blend
   strength. Each pass uses `c + round((s-4*c)*strength/128)`. The second pass
   reads the completed first pass. Each strength is0..31; both apply to all
   three planes.
4. Apply the complete-family neighborhood kernel below.
5. Let `n=s-4*c` for axial neighbors, `T=16*DD8`, and `w=DD4`. Add
   `round(n*max(0,4*T-abs(n))*w/(512*T))` to the center. This keeps the mean
   residual rational until the final correction. Constants and residuals at
   or beyond the threshold are unchanged; smaller residuals receive graded
   smoothing. DD4 is3..18 and DD8 is5. There is no invented omitted-group or
   zero-strength bypass.
6. Write MAIN Y bytes using the existing twelve-to-eight-bit conversion.
   Average the four actual U and V values in each output2x2 block before the
   same conversion and interleave U then V.

For step4, let `b-/b+` be the high bytes of DC0/DC4, `s` their equal low byte,
`t=DC8`, `a=DDC>>8`, `m=DE0>>8`, `w=DE4.low`, `r=DE8.low` and `d=DEC.low`.
At each center `c`, define:

```text
L- = 16*b- + round(s*c/16)
L+ = 16*b+ + round(s*c/16)
A = sum of four axial differences at radius r, each clamped to [-L-, L+]
D = sum of four diagonal differences at radius r, each clamped to [-L-, L+]
R = (16-d)*A + d*D
correction = round(R*(31-t)*a*m*w / (64*31*256*512*(128+w)))
output = clamp(c + correction, 0, 4095)
```

This chooses the bases/slopes as asymmetric difference limits, DC8 as retention,
DDC/DE0 as Q8/Q9 factors, DE4 as complementary blend weights, and DE8/DEC as
neighborhood/mix controls. Every one of those numerical roles, scales and the
stage placement is an explicit functional hypothesis. The arithmetic keeps
both four-neighbor means and the final blend rational until one signed divide.
The numerator is below2^49 and denominator below2^36; signed64 is sufficient.
Zero amount, zero modifier or retention31 is algebraically neutral through the
same computation, without an omitted-group or enable shortcut. Constant planes
remain constant and a neutral gray axis remains centered under this shared
kernel. Source-family qualification is mandatory even for a neutral result.

Intermediate storage comprises three actual MAIN planes. After all samples are
computed, the existing RAW-sized scratch is reused for the immutable passes.
Every allocation and input/configuration check precedes output modification.
There are five immutable spatial passes; after the odd final pass its scratch
plane is copied back before NV12 publication. The existing4096 dimension bound
applies; no source RGB side channel is used.
These operators preserve constants but do not promise that an arbitrary gray
edge remains colorless, or that clipping preserves the image's global mean.

## Publication and validation

All64 words across the four groups must have been explicitly written since
reset, including zero-valued words. A first write changes publication coverage
even if its retained value was already zero. It cancels an active immutable job;
a changed covered value also cancels. Equal, already-covered writes preserve
the job. MAIN-only post-color changes preserve pre-color history qualification.
Whole-model reset clears all four coverage masks. Normal RAW ownership,
preflight, output DMA, pending backpressure and completion publication remain
the existing paired transaction's responsibility.

Synthetic pure fixtures independently compose centered C/BAC and exact rational
spatial passes from independent pre-color reference scratch, covering asymmetric chroma, borders,
hard steps, weak texture, all dynamic fields and all three complete tail
families. They use independent coordinate enumeration and division/remainder
rounding for the full five-pass oracle. Existing matrix fixtures
now distinguish chroma-domain C from the removed RGB transform. Invalid fields
must leave every output unchanged. Integration fixtures use real P5 acquisition,
packed RAW DMA and complete guest-side parameter publication, compare MAIN Y/UV
including pitch guards, gray and colored edges, and cover every cold DC0..DEC
prefix, malformed/reserved fields, tuple relations, opaque mismatches, reset,
changed-configuration cancellation and equal-write preservation. Native results
and unchanged-firmware progress are reported separately by the integration owner;
the fixture definitions do not establish physical camera color or complete boot.
