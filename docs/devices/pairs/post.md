# Finite post-color processing

The paired operator consumes four complete source-shaped parameter groups for
MAIN color processing. These controls operate on actual computed twelve-bit
YUV planes, before NV12 chroma averaging and byte conversion. WORK, grid and
pre-color temporal history retain their existing data contracts. No parameter
write creates a frame, result, completion, IRQ or buffer release.

## Source boundaries

The following addresses are MAS C1.70 virtual code addresses. Register offsets
are hexadecimal within either paired bank; compact byte offsets are decimal.

| Complete register group | Producer and packing | Dynamic numerical fields |
| --- | --- | --- |
| `b38..b58`, nine words | `102e57a6` passes configuration21+84, compact780, to `1036bcde` | `b58` high/low unsigned16 |
| `b94..bbc`, eleven words | `1036c02c`, `1036c0c0`, `1036c12a`, `1036c094` publish C, weights and adjacent controls | C and complementary `bac` low/high unsigned16 |
| `d20..d9c`, thirty-two words | Configuration40+28, compact924, is the110-byte input of `1036c260` | `d28` high/low byte |
| `dc0..dec`, twelve words | Configuration40+166 and its related scalar setters, including `1036c7a6` | `dd4`, `dd8` |

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

Only C and the five dynamic words B58, BAC, D28, DD4 and DD8 leave the explicit
calibration image. Every other word in the four groups must still match that
complete declared virtual calibration profile. Those values are finite-profile
qualifiers, not claims that the firmware always publishes constant values.
Unknown nonmatching controls remain blocking. The calibration file must leave
interpreted slots zero; it supplies no replacement guest register writes.

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
   neighbor sum `s` and the selected Q8 strength `k`, use
   `c + round((4*c-s)*k/1024)`. Strength zero is algebraically neutral.
3. D28 supplies an axial mean-blend strength followed by a diagonal mean-blend
   strength. Each pass uses `c + round((s-4*c)*strength/128)`. The second pass
   reads the completed first pass. Each strength is0..31; both apply to all
   three planes.
4. Let `n=s-4*c` for axial neighbors, `T=16*DD8`, and `w=DD4`. Add
   `round(n*max(0,4*T-abs(n))*w/(512*T))` to the center. This keeps the mean
   residual rational until the final correction. Constants and residuals at
   or beyond the threshold are unchanged; smaller residuals receive graded
   smoothing. DD4 is3..18 and DD8 is5. There is no invented omitted-group or
   zero-strength bypass.
5. Write MAIN Y bytes using the existing twelve-to-eight-bit conversion.
   Average the four actual U and V values in each output2x2 block before the
   same conversion and interleave U then V.

Intermediate storage comprises three actual MAIN planes. After all samples are
computed, the existing RAW-sized scratch is reused for the immutable passes.
Every allocation and input/configuration check precedes output modification.
The existing4096 dimension bound applies; no source RGB side channel is used.
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
spatial passes from the pre-color grid, covering asymmetric chroma, borders,
hard steps, weak texture and all dynamic strengths. Existing matrix fixtures
now distinguish chroma-domain C from the removed RGB transform. Invalid fields
must leave every output unchanged. Integration fixtures use real P5 acquisition,
packed RAW DMA and complete guest-side parameter publication, compare MAIN Y/UV
including pitch guards, and cover missing groups, opaque mismatches, reset,
changed-configuration cancellation and equal-write preservation. Native results
and unchanged-firmware progress are reported separately by the integration owner;
the fixture definitions do not establish physical camera color or complete boot.
