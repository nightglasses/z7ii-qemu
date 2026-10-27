# Radial sampling and authored neutral curves

The paired pixel model decodes the complete six-word `724..738` packet and
applies its two changing signed coefficients to MAIN R/B sampling. It accepts
the source's omitted-table path and complete authored neutral-curve path. The producer,
packing, estimation arithmetic and optional-table publication path are source
facts. The R/B assignment, radial interpretation, direction, stage placement and
sampling operator are explicit functional hypotheses. This is a finite virtual
camera contract, not recovered Nikon lens calibration or silicon processing.

Code addresses below are C1.70 MAS CPU virtual addresses. Hexadecimal register
offsets are relative to board PA `20380000` or `203c0000`. Registers use aligned,
little-endian 32-bit accesses. Structure byte offsets, field indices and scalar
arithmetic are decimal unless prefixed with `0x`.

## Complete producer and field representation

VA `102bf33c` selects compact state `P+382` as configuration entry13.
VA `102e49fe` calls `1036b3ee(P+32)` for `724` and `1036b408(P+36)` for the
remaining five words. The later `1036b4de` publishes `744`, not this group.

| Word | Source and packing | Finite qualification |
| --- | --- | --- |
| `724` | high/low `q4/q5` of the separate57-field table | `0/270` |
| `728` | low `cx`, high `cy`, from compact418/420 | unsigned16 coordinates |
| `72c` | compact426/427/428 in bits16/8/0 | `00030205` |
| `730` | compact422/424 in high/low signed10 | each `-128..127`; reserved bits zero |
| `734` | compact429/430 in high/low byte | high5..8, low32..62; upper16 zero |
| `738` | compact431/432 in high/low byte | equal bytes in1..3; upper16 zero |

All sixteen selectable families with nineteen rows contain `q4=0,q5=270`.
The table's `q0..q3=300` instead feed the four pedestals at `718/71c`.
VA `102f734e` interpolates `q4/q5` into compact414/416. This does not establish
a physical name for `724`; the model retains the entire observed fixed pair.

VA `102f88c8` rounds original-root92 and88 by256 into compact418 and420,
respectively. VA `10274aa8` constructs those root coordinates from source
dimensions shifted by7 plus a coordinate bias; `10273338` performs the related
coordinate conversion. The low/high center order in `728` is therefore `cx/cy`.
Centers can include a bias and need not equal the exact RAW midpoint. Their
interpretation as the radial origin is a functional choice.

VA `102f8812` sets the intermediate signed16 coefficients to zero when its
intermediate byte28 is nonzero; otherwise it copies its input22/24. VA
`102f82ea` binds those inputs to normalized metadata26/28. The latter can be
smoothed over16 observations by `10363c88`, using original-root82/84.

Both the photo caller `1026676a` and movie caller `102c3d0c` use
`102b4cd8 -> 102b4ba6` to derive those root inputs. The estimator multiplies a
floating-point result by65536, rounds to an integer and clamps to `[-128,127]`.
Thus Q16 quantization and the signed range have CPU arithmetic support. Setter
`1036b408` masks each coefficient to10 bits. For example, `-1` is encoded as
`3ff`; `37f` is signed `-129` and is outside the finite range.

VA `102f88c8` fixes compact426/427/428 to3/2/5. VA `102f8b32` chooses the two
`734` bytes from mode-dependent tables; compact431/432 receive the same lookup
value indexed by saturated metadata7. The bounds above span the source tables,
including the alternate13-by8 mode table. The scalar box is a bounded domain;
not every admitted combination is claimed to occur in firmware. No physical
shift, mantissa or channel name is established for those fields.

## Optional curves and the authored neutral domain

VA `102f833c` constructs two17-item signed16 polynomial records. It evaluates
each of two three-coefficient polynomials at17 nonuniform source radii, then
arithmetic-shifts the result by intermediate byte34+1. The terms use a constant,
squared radius and cubed radius; the fixed-point radius unit is4096. Their
constant terms can depend on the head coefficients. Neither an additive nor a
replacement relationship with the hardware head coefficient is established for
nonzero records, so this model does not admit nonzero curves.

VA `102f8722` chooses the polynomial branches or calls `102f86e6`. The latter
explicitly writes zero into every halfword of both17-item records. It also sets
the polynomial constants to the retained head coefficients multiplied by16,
and the remaining four polynomial coefficients to zero. It does not subsequently
evaluate those constants into the records. This is a source-authored neutral
table domain with independently retained head coefficients; it is not evidence
that a nonzero head should be cleared when the records are zero.

VA `102f9a14` unconditionally packs those two contiguous records from state+4
into cache+9592/+9628, sets both publication flags14761/14762, and writes zero
padding after each17th signed halfword. Thus complete zero table publication is
a normal source path, independently of a NULL hardware publication path.

The actual publisher at `102e4bbe` reads configuration entry14. It skips a NULL
entry, and separately skips either NULL child pointer. When present,
`1036b476(handle,selector,pointer)` copies36 bytes through `1036ca74` to `1200`
or `1224`: each bank holds17 signed16 entries followed by one padding halfword.
VA `102bf33c` selects those pointers from a separate cache. Cache flags14761 and
14762 select cache+9592 and+9628; both flags zero make entry14 NULL.

Unchanged original execution observed nonzero `730` and all eighteen words
`1200..1244` zero; a later original execution confirmed actual writes to that
zero group. These observations and the source producer establish the need for
both paths. They do not establish the physical polynomial sampling operator.

The finite model admits completely unwritten, cold-zero curve registers, or
two fully published nine-word curves whose entire34 signed entries and both
padding halfwords are zero. Touching either slot requires both slots complete;
a single full slot is not accepted as omission. Every nonzero halfword remains
unsupported, including signed negative entries and padding. The opaque input
file must leave these interpreted slots zero. The full `734/738` representation
and bounds remain required. No nonlinear operator, displacement, selector or
enable bit is inferred to widen this neutral domain. Actual MAIN computation
continues through the head-coefficient sampling equation below.

## Numerical functional hypothesis

For a destination RAW coordinate `(x,y)`, select `k=red` or `k=blue` and use:

```text
sx_Q16 = x*65536 + (x-center_x)*k
sy_Q16 = y*65536 + (y-center_y)*k
```

This is the chosen coordinate interpretation of `1+k/65536`, not a claim about
physical correction direction. At snapshot, subtract the RAW ROI origin at
`44` from the unsigned wire center. The resulting signed center is independent
of the later MAIN crop. It is bounded by the wire unsigned16 range and the
supported RAW origin0..4095, giving `[-4095,65535]`. The coefficient range keeps
the coordinate products safely within signed64.

MONO MAIN samples R and B independently from the immutable pre-color image,
then applies their existing quad correction. G uses the original sample.
CFA MAIN samples the R/B phases from the immutable pre-color mosaic, retains
both green phases, and applies the existing per-site quad correction followed
by bilinear demosaicing. No RGB side buffer is introduced. The additional
scratch image contains RAW12 samples.

Sampling replicates the nearest border on the requested lattice. Mono spacing
is one RAW pixel; CFA spacing is two, preserving the requested phase for every
encoding. Bilinear weights use full Q16 RAW coordinates and an unsigned64 sum;
round once to nearest with half ties up. The CFA sum is bounded by
`4095*2^34`. Coefficient0 follows the same arithmetic, exactly preserving
integer sampling and the previous zero-coefficient output. No zero-structure
enable or fallback is introduced.

WORK luma/residual, grid and temporal history remain pre-color. This stage
does not alter their values, input generations, slots, timing or event routing.
All allocations and validation precede public output modification. MAIN output
still requires the normal successful RAW DMA and processing publication path.

## Publication and falsifiable validation

All six prefix words must be written since reset, including a zero coefficient.
Each optional slot has an independent ordered nine-word publication round.
Writing its first word starts a fresh round, even if unchanged; later words
must arrive consecutively. Missing, duplicate or out-of-order writes cannot
borrow coverage from the previous round. A fresh header restarts malformed
coverage. Reset clears the registers, round counters and prior-publication flag.

Optional writes are staged and do not cancel owned work. An active job retains
its frozen head-coefficient computation, including during incomplete or
unsupported new table uploads. Completion retains every existing memory and
peer fence but does not requalify live optional tables. A valid complete zero
publication is numerically unchanged and preserves that job. Changed prefix
values still use the existing cancellation path. These MAIN-only inputs preserve
pre-color numerical history.

A warm partial publication holds an existing AwaitInput request and its real
unconsumed RAW until both slots complete. Initial partial coverage, malformed
order and a complete nonzero group reject new admission. Completing a valid
group retries only an existing AwaitInput request; it cannot arm processing or
reuse a consumed RAW generation. No reset, write or rejection supplies a
completion, slot release, event or ready bit.

Pure fixtures compare complete MAIN output against an independent floating
coordinate oracle, with every CFA encoding, R/B separately, negative/zero/
positive coefficients, extreme and fractional centers, border clipping, quad
clipping and odd MAIN crop. Fixed vectors change a measured step sample from
112 to89 or116 for opposite coefficients; WORK/grid remain unchanged. Invalid
scalar/center tests require untouched outputs.

Ordinary-MMIO fixtures use actual64-by36 mono or64-by72 CFA transport, an odd
RAW ROI and odd MAIN crop. They verify changed dark/bright edge samples,
padding, completion and IRQ acknowledgement. Other fixtures cover every wire
class, signed encoding, omitted and complete authored-zero publication,
every cold prefix and every missing new-round word, nonzero words/padding,
unchanged-write retention, frozen completion, warm staging, changed-prefix
cancellation and reset. In particular, the nonzero-head edge fixtures also
publish both complete zero curves, so accepting those curves cannot silently
turn off the actual radial calculation.
These are specified tests; native results and unchanged-firmware continuation
are reported separately by integration.

A register manual, CPU pixel consumer or discriminating physical measurement
could falsify the radial/R-B interpretation or reveal the missing curve-index
contract. GUI continuation alone cannot establish those physical meanings.
