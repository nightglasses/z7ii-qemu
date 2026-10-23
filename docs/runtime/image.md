# Inline A8 image modulation

The unchanged C1.70 MAS submits a176/336-byte pair after the five gray
rectangles. It draws a red warning icon into the retained720x480 RGBA buffer.
The finite model processes the guest's inline geometry and alpha image, then
publishes ordinary IRQ129. It does not substitute a host icon or publish a
final display frame.

The later normal-GUI [initialized image model](../devices/transfer/image.md)
adds full224/472 initialization, explicit validated target selection and
temporary paint-mode preservation. Original execution completes that pair and
seven176/336 continuations through IRQ129. The older warning-path receipts
below retain their own scope; the [current frontier](../validation/frontier.md)
records later progress through the [mixed path model](../devices/transfer/stroke.md).

## Original operands and producer

All code and buffer addresses in this section are MAS VAs. Full original
streams and a bounded source window are in
`work/runs/textureboundary-20261022/`. Submission entry is`106ee04a`,
caller LR`106eeab9`, descriptor`b545e79c`, command sources
`bd0e5820/bd1676f0`, counts44/84 words, path key`1008b`.

- Setup register3=5 and register5=`80000005` select five inline signed32
  coordinates. Control`0000000a` carries two segment words, yielding
  `02 07 09 07 00` plus zero padding; control`00010022` carries five coordinate
  words`(0,0,72,72,-72)`. These are coordinate integers, not float bit patterns.
- The column-major float32 path matrix has diagonal`(1,-1,1)` and
  translation`(112,378)`. The rectangle covers buffer x112..184/y306..378.
- Draw registers158/152=`00500048`: width80, height72. Register156 low
  format18 selects A8; source155=`b97a6c00`. The5760 bytes contain the
  firmware's alpha mask for a circular exclamation icon, with padding in the
  80-pixel row width. No text/font/icon is supplied by the host.
- Mode184 low byte0e modulates sampled image by paint185=`ff4848f0`, packed
  little-endian RGBA (`R=240,G=72,B=72,A=255`). Blend selector1 is SRC_OVER.
  Target remains`c175d600`, format0 and inherited720x480 extent/clip.
- Sampling registers10c..114 use signed f24, not IEEE float32. Decode sign
  bit23, exponent bits22..16 biased63, and16 fraction bits. The nine values are
  approximately`(.012500048,0,-1.3999939,0,-.013888955,5.25,0,0,1)`.

Original builder`106b4150` emits source size/format;`10646212` emits the
source address and fixed image state. `106b41d2` converts matrix coefficients
into the f24 representation. `106b5198` selects image/paint mode3, whose
`10645c86` packing yields low byte0e with inherited bit3. `106b4064` calls
`10646324`, which emits the inline rectangle coordinate words; its two raw
control payloads must not be interpreted as register commands. Read-only
decompilation receipts are in`work/runs/texturemodel-20261023/producer*.json`.

The fixed [Z7 path decoder](https://github.com/WonderLantern/z7-qemu/blob/97abf0b81cd3211f8374a38987a0beaf4549b90f/qemu/nikon-z7-gpu.h#L162-L198)
corroborates datatype bits31..30=2 as signed32. Its
[inline decoder](https://github.com/WonderLantern/z7-qemu/blob/97abf0b81cd3211f8374a38987a0beaf4549b90f/qemu/nikon-z7-gpu.h#L482-L516)
consumes op2 payloads, and its
[image sampling](https://github.com/WonderLantern/z7-qemu/blob/97abf0b81cd3211f8374a38987a0beaf4549b90f/qemu/nikon-z7-gpu.h#L304-L439)
supplies A8 white RGB/alpha, mode14 modulation, row-major normalized sampling
and nearest/clamp behavior. This is a functional reference, not recovered
Z7II silicon interpolation precision.

## Implemented finite contract

The exact44/84-word command family retains all fixed headers, state, ending
and padding. Variable fields are the affine path/sampling matrices, five
bounded integer coordinate words, matching nonzero opaque operand, matched
class1 key, equal source extents, source address and opaque paint. The initialized
extension also permits an explicit RAM target; retained images must match the
successfully established target. The later mixed path model also uses this
target-matched profile. Only this
five-segment closed inline H/V path family and A8 source with dimensions1..256 are
accepted. Perspective/singular/nonfinite transforms, unsupported types/modes,
nonopaque source paint and incomplete packets remain errors. The later
[SRC_OVER extension](../devices/transfer/over.md) supports destination alpha0..255.
The magnitude/count bounds are functional work limits, not silicon limits.

`nikon_transfer_image_parse` validates context, packet/source/target ranges,
command overlaps and effective RAM aliases. Exact integer-to-float conversion
within±4096 reuses the existing affine path rasterizer. At deferred execution,
the image is read through DMA, coverage and premultiplied modulation are built
in scratch buffers, and the complete target mapping is checked before any
write. The shared path executor applies SRC_OVER and IRQ129 only after work
succeeds. Late map/data errors, reset and unrealize preserve output.

Nearest sampling uses pixel centers and clamps to the image bounds:
`tx=floor(clamp(width*(m0*x+m1*y+m2),0,width-1))`, and analogously for y.
The accepted sampler is affine with final row`(0,0,1)`. Cairo coverage and
integer rounding are functional approximations. The image-quality command
value remains fixed; acceptance does not claim physical filter precision.

## Native and original-execution evidence

`work/runs/texturemodel-20261023/red.log` records valid synthetic image
rejection by the preceding model. `verifiedgreen.log` passes one executable,
all10 transfer cases. The new image fixture covers alpha0/64/128/255,
distinct color channels, row width versus rectangle width, f24 mapping,
vertical inversion, edge clamping, full target/guards, delayed input reads,
repeat/context, malformed fields and sampling, unsafe ranges/aliases,
map loss, reset and cancellation. Intermediate logs retain a test-load
comparison correction and an incorrect f24 literal in the added flip oracle;
neither is an accepted native receipt.

With the same unchanged bundle and constructed descriptor input, the original
pair completes through IRQ129 at`106ee3ca`. Full memory comparison observes
3751 changed pixels:3365 solid paint and386 partial-alpha edge pixels,
all inside`[112,184)×[306,378)`. Every pixel outside that patch equals the
previous text/border frame, and all alpha remains255. `frame.bin` is actual
guest memory. The PNG recipe only encodes those bytes, reversing rows for a
separate preview using the fixed Z7 convention; final Z7II scanout is unproven.

The subsequent pair is224/1736 bytes,56/434 words, source buffers
`bd0dff90/bd15ff90`. It reads the separate1024-wide RGBA composition buffer
`bf309600` and targets YUV format105 at`cc18b000/cc235800`, with1024x688
aligned extent and a1024x682 inline path. It is not evidence that the rendered
720x480 warning has been scanned out. Full decoding/state and next abort are
in the same model receipt directory; a separate entry capture is in
`work/runs/imagetransfer-20261023/`. See the
[startup frontier](../validation/frontier.md).
This YUV105 [conversion](conversion.md) now completes through IRQ129 as well.
The next same-length pair also completes720x480 scaling into YUVA107. The
176/1552-byte warning-source SRC_OVER continuation now also completes and
produces the warning in the actual YUVA target. The subsequent bounded run
returns to worker0 with no new graphics rejection.
