# Paired immutable color filtering

The finite model consumes the complete source-shaped `900..9c4` parameter group
and the independent zero at `9d8` in both paired banks. It computes two bounded
same-color neighborhood passes followed by a median-residual pass on actual RAW
pixels. The packing and interpolation bounds are established from the C1.70 MAS
software producers. The pixel equations, directional interpretation, units and
stage order are explicit functional hypotheses, not recovered silicon behavior
or physical-camera image-quality calibration. Native fixtures and original
firmware execution require separate validation; this page does not claim a new
original-firmware startup milestone.

## Producer and complete packet

Offsets below are hexadecimal byte offsets from paired-bank PA `0x20380000` or
`0x203c0000`. Registers are aligned little-endian 32-bit words. C1.70 MAS VA
`0x102e49fe` publishes configuration15 from a compact structure at byte492:

| Publisher VA | Input relative to byte492 | Register offsets | Established action |
| --- | --- | --- | --- |
| `0x1036b63a` | 0 | `900..950` | Packs the first block |
| `0x1036b830` | 80 | `954/958` | Packs six scalars from the separate57-field family |
| `0x1036b866` | 88 | `95c..9b0`, `9d8` | Packs the second block and two source zeros |
| `0x1036ba6a` | 170 | `9b4` | Packs duplicate one-byte scalars |
| `0x1036baa8` | 172 | `9b8..9c4` | Packs the tail and a source zero |
| `0x1036baea` | 182 | `9c8` | Publishes an independent57-field/pedestal result |

Upstream VA `0x102d3874`/`0x103626fe` selects a336-byte directory at
VA `0x11c0ae58 + 336*d`. `0x103622d4` chooses a selector and tuning family;
the98-field table pointer is directory dword `17+8*selector+2*family`. There are
four directories, four selectors and four families. Each table contains19 rows
of98 signed16-bit fields,196 bytes per row. VA `0x10361756` interpolates adjacent
rows using normalized metadata24 with denominator6, clamping the row index at18.
The model accepts the convex scalar bounds below, rather than enumerating any
specific exposure-mode packet. No factory table or firmware array is included.

The two blocks have the same wire layout. Their bases are `900` and `95c`;
all offsets in the following table are relative to that base. Pair notation is
high16/low16. Shift notation lists bytes from most to least significant.

| Relative offset | Established containers | Functional role |
| --- | --- | --- |
| `00` | q0/1 or q45/46 | Common base/slope |
| `04/08` | q2..5 or q47..50 | Axial negative and positive base/slope pairs |
| `0c` | q6..9 or q51..54 | Four axial shift scalars |
| `10` | q10 or q55, remaining bits zero | Axial radius scalar |
| `14/18` | q11..14 or q56..59 | Diagonal negative and positive base/slope pairs |
| `1c` | q15..18 or q60..63 | Four diagonal shift scalars |
| `20` | q19 or q64, remaining bits zero | Diagonal radius scalar |
| `24` | zero byte, q20..22 or q65..67 | Three lane strengths R/G/B |
| `28..4c` | Twenty masked11-bit containers per block | Source-fixed zero qualifiers; nonzero rejected |
| `50` | q43/44 or q88/89 | Negative/positive correction limits |

The observed source extrema cover all64 tables and all19 rows. Convex
interpolation preserves these bounds; the checks do not claim every Cartesian
combination corresponds to a factory row.

| Scalars | First-block bounds | Second-block bounds |
| --- | --- | --- |
| Common base/slope | 0..400 / 0..300 | 0..480 / 0..600 |
| Axial negative base/slope | 0..100 / 0..500 | 0..480 / 0..800 |
| Axial positive base/slope | 0..120 / 0..300 | 0..540 / 0..500 |
| Diagonal negative base/slope | 0..80 / 0..800 | 0..500 / 0..650 |
| Diagonal positive base/slope | 0..120 / 0..300 | 0..320 / 0..620 |
| Axial shifts | 2..4, 2..3, 2..4, 2..4 | 2..4, 2, 3..4, 2..4 |
| Diagonal shifts | 2..4, 2, 2, 2 | 2..4, 2, 2..4, 2..4 |
| Lane strengths | Each0..31, G=B | Each0..31, G=B |
| Correction limits | Each0..4095, negative≤positive | 5..100 / 9..150, negative≤positive |

In each diagonal pair, positive base≤positive slope. The first block's two
radius scalars equal the second block's axial scalar, each either0 or2; the
second block's diagonal scalar is0. Neither source value is called an enable.

The separate57-field table directory is16 consecutive little-endian32-bit
pointers at VA `0x11c0ae18`. Across all16 tables and19 rows, q51..56 are fixed
at `(3000,400,16,128,128,48)`, giving `954=0x0bb80190` and
`958=0x10808030`. These two complete words are mandatory fixed qualifiers.
No independent dynamic numerical role was recovered, so no artificial numerical
use is assigned to them.

The tail's complete qualification is:

| Offset | Established packing/range | Functional role |
| --- | --- | --- |
| `9b0`, `9d8` | Source-fixed zero | Fixed qualifiers |
| `9b4` | q90<<8\|q91; equal scalars0 or1, upper16 zero | Duplicate precision selector |
| `9b8` | q92<<8\|q93; q92=0..130, q93=0..31, upper byte zero | Negative limit and strength |
| `9bc` | q94<<16\|q95; q94=2..4, q95=0..200, upper byte zero | Shift and positive limit |
| `9c0` | Source-fixed zero | Fixed qualifier |
| `9c4` | q96<<8\|q97; q96=8..16, q97=4, upper16 zero | Weight and fixed divisor |

`9c8` is outside this98-field packet and its publication mask. Its producer uses
the separate57-field q4/q5 and a pedestal contribution. It remains under the
existing strict opaque-profile check. It is neither mirrored to `e10` nor
silently ignored: the existing numerical LFSR noise limits are independently
published at `e10`. Recovering a numerical role for `9c8` needs a source-backed
connection to an observable consumer.

## Functional equations

The two blocks run after the existing adaptive stage and before temporal history,
WORK/grid publication and MAIN color processing. Each block reads an immutable
image. CFA processing uses each half-resolution phase plane separately; it never
averages R, Gr, Gb and B samples together. Its strengths are R/G/G/B. MONO uses
`round((R+G+B)/3)` as a shared luminance strength, so all three source strengths
participate without inventing hidden RGB input planes.

For `s=4*sample`, the common threshold is
`base + round(slope*s/16383)`. Each branch adds its negative or positive
`base + round(slope*s/16383)` according to the sign of a neighbor residual `d`.
Axial direction order is left/right/up/down; diagonal order is
upper-left/upper-right/lower-left/lower-right. Radius is `1+radius_scalar`.
Borders replicate the nearest same-plane sample.

For threshold `T`, a neighbor contributes zero when `T=0` or `abs(d)>=T`;
otherwise it contributes `round(d*(T-abs(d))/T)`. Its directional weight is
`2^shift`. Divide each branch's weighted sum by its total weight, average the
two branch means, then multiply by lane strength/31. Clamp the resulting
correction to the block's negative/positive limits. Add it to `s`, clamp to
0..16383 and round/clamp back to RAW12. The second block reads the completed
first-block image.

The tail reads an immutable radius-one3×3 median including the center. Clamp
`4*median-s` to `[-q92,q95]`, then add
`round(residual*q93*q96/(31*4*2^q94*2^q90))` to `s`. Clamp and round as above.
The repeated q91 is validated and q97 is the fixed4 divisor. All signed rounding
uses nearest, ties away from zero. The maximum tail ratio is1; the block gates
and residual limits bound local corrections. Constants remain constant and
sufficiently large edges receive zero gated block correction. These properties
and noise-energy fixtures are falsifiable model tests, not a universal proof of
image-quality improvement.

Zero strengths and zero thresholds execute these same equations. An explicit
synthetic neutral packet uses zero thresholds/strengths, legal shifts (all2
except the second axial third shift3), first limits0/0, second limits5/9,
tail precision0, limits0/0, strength0, shift2 and weight8, together with every
fixed qualifier. There is no omitted-packet or all-zero-packet identity path.

## Publication and validation

Each bank records all50 words `900..9c4` and the independent `9d8` write since
reset, including zero and same-value writes. All51 publication bits are required
before snapshot/processing. Other source groups, especially `9c8`, retain their
own contracts. Every snapshot carries the decoded scalar family with the input
and output geometry. A changed relevant write cancels an in-flight transaction
and invalidates temporal history through the existing paired lifecycle. An
equal write on an already-published word preserves both. Reset cancels pending
work and clears the registers, publication coverage and history. No status,
completion event, IRQ or frame is fabricated for missing or invalid parameters.

Scoped fixtures are in `tests/unit/nikon/pairedpixels.c` and
`tests/qtest/nikon/cmos.c`. They cover an independent rational pixel oracle,
positive/negative fixed tail results, source-variable field effects, all CFA
encodings with colored detail, constant/edge/noise properties, invalid pure
configurations, normal RAW→MAIN/WORK/grid transport, register boundaries,
every missing publication word, changed/equal-write cancellation, reset and
history invalidation. These fixtures are implementation tests; their execution
verdict and any unchanged-firmware result are owned by the integration receipt.
