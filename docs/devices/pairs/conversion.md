# Recovered CPU color conversion

The C1.70 MAS firmware contains a complete CPU RGB8-to-YUV conversion at
VA `0x102B3B82`. Its selector1 output, integer arithmetic and byte ordering can
be recovered independently of the paired ASIC. This page records that software
contract and several recovered coefficient generators. It does not establish
the paired RAW selector2 pixel equations, physical CFA or calibrated color
response. The implemented finite ISP remains described in [color](color.md)
and [pixels](pixels.md).

All addresses below are MAS CPU virtual addresses. Descriptor offsets are
decimal byte offsets unless explicitly prefixed with `0x`; multibyte fields
are little-endian. Equations summarize source behavior rather than reproduce
firmware bytes or disassembly.

## CPU pixel equations

The converter accepts its destination only when descriptor byte0 is `1`.
Each source pixel has three unsigned 8-bit lanes, denoted `R`, `G` and `B`
according to their roles in the recovered equations. For each pixel it computes:

```text
Y = floor((77*R + 150*G + 29*B) / 256)
U = 128 + floor((-43*R - 85*G + 128*B) / 256)
V = 128 + floor((128*R - 107*G - 20*B) / 256)
```

The chroma calculation uses arithmetic right shifts at
VA `0x102B3C78`, `0x102B3C88`, `0x102B3C92` and `0x102B3C96`.
Consequently a negative numerator rounds toward negative infinity. Replacing
that operation with signed C division, which truncates toward zero, changes
the result. Luma uses a right shift without a rounding bias.

For two horizontally adjacent pixels, the converter writes their separate
`Y0,Y1` bytes and the following two chroma bytes:

```text
UV[0] = floor((U0 + U1 + 1) / 2)
UV[1] = floor((V0 + V1 + 1) / 2)
```

Thus U precedes V, and the per-pixel chroma values are quantized before the
pair average. Averaging the signed numerators first is not an equivalent
implementation. The final pair average rounds a half upward.

For arbitrary RGB8 input, luma is in `0..255`, U in `0..255` and V in `1..255`.
The source narrows chroma to a byte before averaging; those bounds mean no
overflow or clipping is needed for this input domain. The unequal V coefficient
sum is intentional in this transcription: it is `1`, not `0`. Nevertheless,
every gray input `R=G=B` in `0..255` produces U=V=128 and Y equal to that input.

## Layout and descriptor traversal

The source consumes six bytes per horizontal pair and writes two bytes to
each destination plane. It processes `floor(source.u16[3]/2)` pairs on each of
`source.u16[4]` rows. An odd last pixel is not processed by this CPU routine;
a bounded reference should either reproduce that omission explicitly or
require even width. UV is written on every row, so this is a horizontal 2:1,
full-height chroma layout, not the half-height UV layout admitted for paired
selector2.

The exact pointer recurrence is useful without assigning unproved field names:

```text
S = source.u32_at_16 + source.u16[0] * source.u16[6]
Y = destination.u32_at_20
    + destination.u16_at_2 * destination.u16_at_16
UV = destination.u32_at_24
     + destination.u16_at_4 * destination.u16_at_16

before each row:
    S += 3 * source.u16[5]
    Y += destination.u16_at_14
    UV += destination.u16_at_14
for each horizontal pair:
    consume S[0..5]; S += 6
    write Y[0..1]; Y += 2
    write UV[0..1]; UV += 2
```

The pre-row additions also apply to the first row. They must not be silently
reinterpreted as conventional post-row pitches. At the end, VA `0x102B3D0C`
calls the cache-maintenance helper with the Y base and a span derived from
`UVbase - Ybase + destination.u16_at_4 * destination.u16_at_8`.
The converter then returns `1`; a destination selector other than1 returns
`0` without entering the pixel loop.

## Source selection and metadata join

VA `0x102C23F0` selects the CPU converter when its third argument is nonzero
or the availability byte at VA `0xB1864DFC` is zero. Otherwise it publishes
the descriptor pointers, submits operation4 to worker14 and waits on event64
with a timeout argument of3000. That alternate path does not expose its pixel
arithmetic in this wrapper.

There are two recovered callers:

* VA `0x102C8C12`, at its call VA `0x102C8C3A`, explicitly passes third
  argument1, forcing the CPU route for that branch.
* VA `0x1055EAC8` constructs a20-byte source descriptor and a28-byte destination
  descriptor, sets destination byte0 to1, then calls VA `0x103CF9D8`.
  That wrapper selects software according to its mode and resource checks
  before calling VA `0x102C23F0`.

VA `0x1055EB62` dispatches the latter construction from image metadata
`u16_at_6`: values2 and5 call VA `0x1055EAC8`, while value3 calls another
converter. These metadata values must not be confused with destination
selector1. The builder obtains the source address from metadata `u32_at_12`,
Y/UV addresses from `u32_at_32` and `u32_at_36`, and dimensions from the
four `u16` fields at offsets46,48,50 and52. It derives aligned values using
`align16(3*metadata.u16_at_46)` and `align16(metadata.u16_at_46)`.
This is a positive source join to an image-format dispatch, not a claim that
every paired MAIN image passes through this CPU function.

## Fixed arithmetic references

These values follow directly from the equations; they are reference vectors,
not a physical-camera measurement or an unchanged-firmware execution receipt.
The U/V columns show the per-pixel values before horizontal averaging.

| RGB8 input | Y | U | V |
| --- | ---: | ---: | ---: |
| `(0,0,0)` | 0 | 128 | 128 |
| `(255,255,255)` | 255 | 128 | 128 |
| `(255,0,0)` | 76 | 85 | 255 |
| `(0,255,0)` | 149 | 43 | 21 |
| `(0,0,255)` | 28 | 255 | 108 |
| `(1,0,0)` | 0 | 127 | 128 |

A horizontal red255/blue255 pair writes Y bytes `(76,28)` and UV bytes
`(170,182)`. The red1 case distinguishes negative arithmetic-shift flooring
from truncation toward zero. The red/blue pair also checks byte order and
quantization before averaging. A software reference can use these fixed
vectors, gray ramps and nonconstant adjacent pixels without claiming paired
ASIC equivalence.

## Recovered coefficient generation

CPU generation of coefficients is separately recoverable even when the ASIC
application of those coefficients is not. The following routines establish
parameter arithmetic and layout.

| MAS VA | Recovered software behavior | Limit of the conclusion |
| --- | --- | --- |
| `0x10380232` | Maps control byte34 to `d=0` for255, otherwise `d=byte-128`; selects ROM row `clamp(round_div(u16(state82+22)*d,256)+12,0,24)` from VA `0x11ACEE1C`, composes its2×2 matrix with Q10 identity through VA `0x1038040A`, and writes compact bytes832..848 as `[1024,0,0; 0,m00,m01; 0,m10,m11]`. | Establishes a first-axis identity and a2×2 coefficient submatrix. Signed chroma domain, offsets, stage order and pixel clipping require a separate consumer join. |
| `0x103801C8` | Writes compact u16 fields850 and852 as complementary weights summing256 within the admitted parameter domain. One branch copies `state82.u16_at_22` and its complement; other branches use the magnitude of signed `state82.s16_at_10`, or its shifted half-range, selected by explicit metadata flags. | Supports complementary fixed-point coefficient generation, not the choice of which two pixel signals are blended. |
| `0x10364894` | Selects a two-u16 row using `min(state84.byte3,108)`, performs successive products divided by512 through VA `0x10380388`, and saturates generated values at `0x3fff`. It writes state84 u16 fields34,36 and38. | Recovers gain generation and saturation. The per-pixel channel placement and application order remain separate questions. |

Here `round_div` denotes the original helper VA `0x10380388`, not an
unspecified host-language rounding operation. For its ordinary non-overflow
branch and positive denominator it rounds signed values to nearest with ties
away from zero; it also has a zero-denominator return and an overflow-avoidance
branch. An equivalent parameter oracle must preserve those branches rather
than substitute floating-point rounding.

For VA `0x10364894`, writing `D(n)=round_div(n,512)` and
`sat(n)=min(n,0x3fff)`, the recovered nonnegative-domain chain is:

```text
r = sat(D(row[0] * context.u16_at_88))
b = sat(D(row[1] * context.u16_at_92))
state84.u16_at_34 = sat(D(context.u16_at_94 * r))
state84.u16_at_36 = sat(D(context.u16_at_94 * context.u16_at_90))
state84.u16_at_38 = sat(D(context.u16_at_94 * b))
```

## Boundary of replacement

The selector1 CPU conversion can be implemented as a source-equivalent,
independent RGB8 software reference. The coefficient generators can likewise
be compared directly against recovered integer formulas. Neither result
licenses replacing the [paired numerical contract](pixels.md) with those
equations while calling it recovered hardware behavior.

The paired RAW codec seam, CFA interpolation, RAW12 matrix normalization,
half-height UV reconstruction, curve input domains and ordering, and spatial
post-processing kernels retain the explicit functional choices in
[color](color.md), [pixels](pixels.md), [saturation](saturation.md) and
[post-processing](post.md). To narrow those choices, the next discriminating
evidence is a joined consumer or controlled input/output comparison for the
same format and stage: signed low-amplitude colors distinguish rounding,
adjacent unequal colors distinguish chroma quantization order, and row-varying
colors distinguish vertical sampling. A software fallback for selector1 alone
cannot resolve selector2's vertical rule or prove a physical demosaicing kernel.
