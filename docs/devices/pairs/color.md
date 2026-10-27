# Functional CFA color

The paired numerical operator can consume an explicitly encoded Bayer mosaic
from actual RAW12 samples. It does not reconstruct the physical Nikon sensor's
spectral response, CFA phase, demosaicing or calibration. The existing mono
profile remains a distinct numerical input encoding.

The separate [recovered CPU conversion](conversion.md) records an exact
firmware RGB8-to-YUV software path, including signed shifts, quantization and
output packing. Its selector1 layout is distinct from paired selector2 NV12;
it does not establish the ASIC's RAW-domain stage order or precision.

## Input and ownership

`NikonPairedPixelConfig.encoding` selects mono, RGGB, GRBG, GBRG or BGGR.
CFA input width and height must be positive, even and at most4096. The caller
supplies the complete RAW ROI as contiguous host-endian12-bit samples and
adjusts its encoding for the ROI origin. No RGB side buffer supplies pixels.
MAIN crop coordinates may be odd; interpolation still sees the full RAW ROI.
Invalid encodings, dimensions, samples or existing numerical controls reject
before modifying any output.

The lane number at ROI coordinate `(x,y)` is
`(encoding-1) XOR ((y&1)*2+(x&1))`, where R, Gr, Gb and B are0,1,2 and3.
This is the explicit virtual CFA convention, not a recovered physical phase.
The provider must model its fixed sensor response independently of guest
white-balance controls. The consumer neither inverts live gains nor ignores
them to force neutral output.

## Virtual sensor transport

The functional C1.70 CMOS input interprets P6 RGB images as virtual RGGB and
P5 images as the separate mono diagnostic encoding. It emits real LE16 samples,
not an RGB side channel. Fixed `red-response` and `blue-response` properties
scale R/B sensor samples in Q12 (1..4096); both green responses are unity4096.
The properties are configured before realization and are independent of live
guest controls. Unity is the default. A declared virtual reference illuminant
may supply unequal responses, without claiming recovered Nikon calibration.

Completed input carries encoding and frozen transfer metadata alongside its
actual samples. The transfer is either linear RAW12 or an explicit lookup
codec containing the actual 256 unsigned16 values captured from the producer's
committed MMIO table. Neither metadata record contains RGB pixels.
Capture-work reduction chooses complete2×2 CFA tiles and preserves phase,
then performs the existing real packed-RAW DMA. Odd CFA input/output dimensions
reject. Paired reads that RAM, adjusts phase for the `44` ROI origin and
qualifies numerical history by encoding as well as source epoch and address.
The metadata cannot supply replacement pixels. Reset, stop, input ownership
and successful output publication retain the existing lifecycle contracts.

For the lookup codec, paired first reads and unpacks the real RAW12 DMA ROI.
Every consumed code must equal exactly one knot of a strictly increasing table
in `[0,4095]`.
Repeated, decreasing, out-of-domain or unknown transfer descriptions reject;
an in-range code between knots also rejects. There is no nearest-code fallback
or interpolation. The matched index reconstructs linear RAW12 as
`(index*257)>>4`, before any filter, history, grid, white balance or MAIN color
operation. The packed DMA bytes remain encoded and unchanged.

This is an explicit virtual producer/consumer codec contract, not a recovered
Nikon decompander. Forward encoding discards the source low byte by indexing
with `sample>>8`. Reconstruction chooses endpoint-aligned values `index*257`
in the declared16-bit domain, preserving the P6 channel expansion convention;
it cannot recover arbitrary original16-bit samples. The transfer is independent
of live white balance. Existing paired257-knot gain curves are not relabeled
as inverse tables. WORK history additionally requires the exact same codec and
transfer table, encoding, source epoch and destination addresses. An old
companded or differently quantized history cannot silently enter this domain.

Synthetic native fixtures publish a nonlinear table and its four-times-scaled
variant through normal MMIO, then check actual encoded RAW, decoded WORK,
neutral and primary MAIN colors, and fixed attenuation followed by ordinary WB.
An uncommitted later table write cannot change the completed frame's transfer.
Negative cases exercise repeated/decreasing/out-of-range and incomplete tables,
non-knot RAM codes, reset and cancellation. These are fixture contracts; native
execution and original-firmware receipts remain separate.

The former P6-to-gray conversion destroyed color before RAW packing. Copying
that gray into all four lanes and applying unequal live WB gains explains the
observed purple output before scanout. Changing UV order or subtracting an
extra display bias would mask that input-contract mismatch. Mono test inputs
remain useful for numerical diagnostics but do not model a color camera's
neutral spectral response under arbitrary white balance.

## Numerical contract

1. Split the actual mosaic into four half-resolution phase planes. Run the
   existing impulse suppression, spatial, adaptive, denoise, [detail](detail.md)
   and [axial residual](adaptive.md) operations, followed by the
   [two color-filter blocks and median tail](colorfilter.md), on
   each plane independently. Neighborhoods therefore contain only one phase;
   their edge samples replicate the nearest sample of that phase.
2. Compute each2×2 mosaic block's scalar mean and the existing history
   deviation/weight. History adds the same signed delta to all four samples:
   `round((prior_mean-current_mean)*weight/256)`, followed by12-bit clamping.
   It does not independently blend color samples toward a monochrome mean.
   WORK luma/residual and grid means use this pre-color mosaic. Saturation can
   change a block's final mean; WORK reports that actual clamped mean.
3. Apply [coefficient-only radial sampling](radial.md) on each immutable
   same-color lattice, then the live four-lane gain/offset/pedestal/limit operation to each actual
   sample of its own lane, preserving the two greens through this operation.
4. Bilinearly interpolate R and B on their phase lattices. At a measured green
   site, preserve that green; at R/B sites, average the two independently
   interpolated greens. Replicate borders on each same-phase lattice. Integer
   interpolation rounds to nearest with positive ties upward.
5. Apply the two gain curves, RGB gains, noise and A/B matrices, followed by
   [signed-chroma C/BAC](post.md) and the [saturation curve](saturation.md),
   followed by bounded MAIN processing, then the
   [completely published uniform luma gain](luma.md) and NV12 output
   conversion. Chroma is centered at2048 in12-bit space, averaged
   over each2×2 output block and normalized to8-bit interleaved U then V.

The phase split, filter placement, common history delta, border policy,
demosaicing and arithmetic are finite functional hypotheses. The source-backed
register packing and actual RAW/output DMA remain separate contracts described
in [paired pixels](pixels.md). No completion, IRQ, buffer release or timing
behavior is introduced by this pure numerical helper.

Scratch space is bounded by the existing4096×4096 input limit. The operator
uses two RAW-sized scratch buffers; CFA filtering adds three reusable
quarter-size phase buffers and the existing optional adaptive-filter scratch.
Three cropped MAIN-sized Q12 planes hold the actual computed YUV for spatial
post-processing. They supply no hidden RGB source, and are allocated before
outputs change. The RAW scratch is reused only after every MAIN input has
been consumed.

## Validation scope

The [full-resolution original-firmware validation](../../validation/color.md)
separately records detailed photo input, 20 normally acquired dynamic images,
neutral gray, actual scanout latency, numerical color limits and remaining
exposure-control/startup boundaries. Native fixtures below remain distinct
from that execution evidence.

Synthetic unit cases cover four encodings, all crop-origin parities and
same-phase borders; unequal fixed sensor responses with non-unit white balance
that recover a neutral image; unequal RGB values and history that preserves
chroma; a hand-derived colored edge; and invalid encodings, odd input sizes and
out-of-range samples leaving every output unchanged. These fixtures exercise
the declared numerical contract. They do not establish original-firmware
startup, physical color accuracy or photograph/recording acceptance. Native
execution results are recorded by the integration owner separately.
