# Bounded grid pixel model

The type9 numerical core consumes a tight NV12 image and the complete grid of
40-byte little-endian records. It produces a separate tight NV12 image. It does
not schedule graph work, perform DMA, or generate completion events. The caller
supplies the actual source dimensions independently of the grid coverage.

## Record evidence

The original constructor at `10404354` stores four coordinate pairs in the order
top-left, top-right, bottom-left, bottom-right at byte offsets 0 through 28. Each
coordinate occupies the low 23 bits; the remaining nine bits are clear. The tag
is at byte offset 32, not in the final word at offset 36. The final word remains
uninterpreted and need not be zero.

The coordinate scaling and offsets in `10407A00` and `104078EE` support Q8
coordinates. The finite model decodes the low 23 bits as a signed two's-complement
integer. The constructors `1040DF30`, `104093C4`, and `10408E4E` use 64-pixel
horizontal cells and split each 64-pixel vertical cell into eight record rows.
Thus record origins are `(64*column, 8*row)`.

Non-final columns carry tag `0x306`. The final-column construction at `10408A50`
chooses `e = ceil(log2(max(8, remaining_width)))`, with `3 <= e <= 6`, and stores
tag `0x300 | e`. Its right corners are extrapolated to the corresponding
power-of-two extent when the remainder is not a power of two. Consequently the
last cell's interpolation denominator is `2^e`, while its origin still advances
by 64 pixels. A fixed 64-pixel denominator for every column is incorrect.

An original 1376-pixel-wide capture has 22 columns and 120 rows: 105600 bytes.
Its 2520 non-final records have tag `0x306`, and its 120 final-column records have
tag `0x305`, matching the 32-pixel remainder. This is evidence for the record
family, not a table whitelist.

## Finite numerical definition

For local destination coordinates `u=x%64`, `v=y%8`, let `w=2^e`. Each source
coordinate is the rounded value of

```
(TL*(w-u)*(8-v) + TR*u*(8-v) + BL*(w-u)*v + BR*u*v) / (8*w)
```

The result remains Q8. Signed rounding is nearest with ties away from zero.
Luma uses bilinear sampling at this coordinate. Coordinates outside the source
replicate the nearest edge pixel. Pixel interpolation rounds to nearest; its
nonnegative ties round upward. These interpolation, rounding, signed encoding,
and edge choices are explicit functional hypotheses rather than recovered
physical calibration behavior.

Each interleaved UV pair is sampled at the mapped even destination coordinate.
The same Q8 luma coordinate is interpreted as Q9 in the half-resolution chroma
plane, preserving fractional precision when dividing the source coordinate by
two. Chroma siting is an explicit hypothesis. U and V are independently sampled;
the implementation does not assume monochrome input.

All source and destination dimensions must be even and between 2 and 4096.
Image lengths must match their tight NV12 sizes exactly. Column count must equal
`ceil(dst_width/64)`; row count must cover the destination height, with at most
512 rows. Every supplied grid record, including rows outside the output crop,
must pass its coordinate-bit and column-specific tag checks before any output
store. The record buffer must contain the full grid and may have trailing bytes.
Output overlap with either source pixels or consumed records is rejected.
Failure leaves the output untouched.

The independent unit vectors cover identity, translation, fractional coordinates
with nonconstant chroma, a non-affine four-corner map, cropping, cell boundaries,
signed edge clamping, coordinate rounding, final extents 8/16/32/64, and
non-power-of-two remainders. Negative vectors cover malformed dimensions,
lengths, coordinate high bits, tags, padding mistaken for a tag, invalid unused
records, and overlapping buffers. Fixture validation is separate from original
firmware execution and physical-camera fidelity.

## Hardware abstraction layer and compute pipeline

Heavy pixel transformation operators are abstracted via `NikonComputePipeline`
(`include/hw/nikon/compute_pipeline.h`), decoupling mathematical definitions
from execution backends:
- `NIKON_COMPUTE_BACKEND_CPU`: reference scalar / SIMD algorithm implementation,
  guaranteeing exact deterministic results across all host architectures.
- `NIKON_COMPUTE_BACKEND_METAL`: native macOS Apple Silicon Metal compute
  shader driver (`hw/nikon/devices/compute_metal.m`) performing GPU-parallel
  affine grid NV12 warping and RGGB Bayer CFA demosaicing with zero-copy UMA
  shared memory buffers.
- `NIKON_COMPUTE_BACKEND_DIRECTML`: reserved slot for Windows DirectML and
  HLSL compute shaders.
- `NIKON_COMPUTE_BACKEND_VULKAN`: reserved slot for cross-platform Vulkan.

The compute pipeline is integrated into `nikon_gate_work_compute`
(`hw/nikon/devices/gatework.c`), automatically delegating geometric warp operations
to the host GPU when running on Apple Silicon. The unit fixture `nikon_compute_unit`
validates lifecycle management, backend selection, Bayer CFA demosaicing color responses,
and warp parameter boundaries under native TAP protocols, including direct
bit-for-bit equivalence checks between Metal GPU output and CPU reference.
