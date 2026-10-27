# Finite image contexts

The image command family supports A8 and straight RGBA textures in the retained
720 × 480 RGBA context. Exact command state, source format, bounded geometry,
target ownership and complete memory preflight remain mandatory. The A8
execution receipts below are historical checkpoints; the RGBA extension has
separate fixture coverage and does not itself establish original completion.

The normal GUI path now completes the 224/472-byte pair and seven subsequent
176/336-byte image pairs through original IRQ129. At that checkpoint the next
rejection is840/1040; the later [mixed path model](stroke.md) handles that
family. See the [current frontier](../../validation/frontier.md). GUI first
frame remains unverified.

The later [retained-prefix extension](prefix.md) supports independently optional
cached matrix and color-transform-disable prefixes:176/336,216/336,176/344 and
216/344 share the same retained body and target context. All21 transfer subtests
pass; original execution completes ten216/344 jobs and later graphics work.

## Observed commands and producer

Code addresses are unchanged C1.70 MAS VAs; buffers below are board PAs.
Two original baseline runs reproduce byte-identical224/472-byte buffers at
`bd0dff90/bd15ff90`. The old dispatch recognizes their shared global prefix
as a rectangle-clear batch and rejects CPU1 PC `106ee3c6`, WRITE32 PA
`201810e0=1`, with `transfer-rectangle-count-not-supported`. The submission
is complete; the message reflects the wrong parser's length formula.

After initialization, the body matches the existing [A8 image family](../../runtime/image.md):

| Field | Observed value |
|---|---|
| Setup prefix | `691004=80`, then identity `41f046` matrix write |
| Draw prefix | Existing35-word format0,720×480, full-clip initialization |
| Setup body |44 words, including a second path matrix write |
| Draw body |83 words, with no trailing alignment word needed |
| Inline path | Segments `02 07 09 07 00`; signed32 `(0,0,50,36,-50)` |
| Path matrix | Diagonal `(1,-1,1)`, translation `(283,478)` |
| Source | A8 at `b97a8280`, width64/height40,2560 bytes |
| Sampling matrix | f24 `(.015625,0,-4.421875,0,-.02500009536743164,11.949951171875,0,0,1)` |
| Paint / blend | `ff000000`, mode0e modulation, SRC_OVER |
| Target / key | `bec5f200`, matched class1 key `10001` |

The path covers `[283,333) × [442,478)` in direct buffer rows. Physical-memory
reads capture the full source and target preimage. The source has578 nonzero
alpha samples,466 fully opaque. The target preimage contains342352 alpha0,
2128 alpha204 and1120 alpha255 pixels; it is not an opaque canvas.

Trace joins `10686d50 → 106b4b28 → 106b4064`. The wrapper calls `106454b2`
at `10686ddc`, emits dirty state through `106b581c` at `10686eea`, then calls
the image producer at `10686efa`. The initializer resets its software key and
emits format/geometry/clip; `106b55a2` flushes pending matrix state. The image
producer emits its own path matrix through `106462b0`, f24 sampling through
`106b41d2`, source extent/format through `106b4150`, and inline rectangle through
`106b4064 → 10646324`. Both matrix writes address the same register range;
the finite executor uses the explicit image matrix following cached state.
The [overwrite analysis](overwrite.md) later confirms that the unused first
payload may differ from identity without affecting that path.

## Finite state combinations

Image dispatch precedes the broader clear-prefix dispatch, but exact image
validation remains mandatory. The initialized224/472 form checks the complete
35-word global prefix, setup enable and first full matrix-write header. Its
unused nine-word payload is fully overwritten before latching; the explicit
final body matrix retains all numeric checks. It then shares
the44/83-word body validator and existing rasterizer/sampler with the retained
44/84-word body, whose84th draw word must be zero padding. The optional retained
prefixes are validated separately as described in[prefix.md](prefix.md).
Geometry, clip, format,
source type, inline segment grammar, echoed scratch operand, keys and endings
remain checked. No generic command VM or permissive register store is added.

Target `1a4` is now an explicit operand: four-byte aligned, wholly below4GiB,
and covered by one writable direct RAM span for the720×480 RGBA plane. All
command/source spans, raw overlaps and effective backing aliases with output
are preflighted. Source dimensions 1..4096 with at most 65,536 texels, bounded
inline coordinates, finite
affine path/sample matrices, opaque paint and SRC_OVER retain their existing
limits. Unknown formats, states and unsafe operands remain errors.

The retained context records the successful target address, instead of a
record7-only boolean. Retained image commands must match it; full initialization
may select a different independently validated target. The later
[mixed path model](stroke.md) also validates paths against that target.
No-context uses a value outside the
32-bit address domain, preserving legal target0. ACK retains context; rejection,
execution failure, reset, unrealize or a different successful program clears
or replaces it. Context and IRQ publish only after output execution succeeds.
The later [shared-context correction](paint.md) also permits a successfully
initialized or continued rectangle clear to establish this same RGBA profile;
target matching and failure/reset invalidation still apply.

The first image-only implementation crosses224/472, then finds temporary
draw-body word29 changed from8 to14 in the next176/336 pair. `10646062`
sets paint bit3 while preserving `2*private192` and boolean `private188`;
`10645c86` later writes the final image mode. For final mode0e, the finite
profile accepts temporary8/10/12/14 and still rejects bit0/other bits and final
mode changes. All four prior-mode combinations are tested together.

Nearest/clamp sampling at pixel centers, Cairo coverage, straight RGBA output
and integer compositing precision remain the existing functional assumptions,
corroborated by the [fixed Z7 source](https://github.com/WonderLantern/z7-qemu/blob/97abf0b81cd3211f8374a38987a0beaf4549b90f/qemu/nikon-z7-gpu.h#L304-L439).
Target-matched retained context is a conservative software-family contract.
These choices do not establish physical GPU precision or arbitrary state
combinations. Firmware bytes remain unchanged; the model does not inject
software events, alter PC or set readiness fields.

## RGBA texture modulation

An unchanged C1.70 MAS GUI submission supplies another complete 176/336-byte
pair. Its first rejection is at MAS VA `106ee3c6`, committing board PA
`201810e0=1`, with `transfer-image-program-not-supported`. Compared with the
existing image grammar, the only fixed-field mismatch is draw word 13:
`001b0100` instead of the A8 word `001b0018`. Its height also exceeds the old
256-per-dimension work limit. The source bytes and addresses remain guest
operands, rather than an address whitelist or a replacement host graphic.

| Operand | Captured value |
|---|---|
| Source format / storage | `156=001b0100`, packed four-byte RGBA |
| Source active / allocation extent | `158=152=001001e0`, 16 × 480 |
| Source span | Board PA `b2d23380..b2d2ab80`, 30,720 bytes |
| Inline coordinates | `(0,0,3,480,-3)`, segments `02 07 09 07 00` |
| Path matrix | Diagonal `(1,-1,1)`, translation `(64,480)` |
| Sampling matrix | `(0.0625,0,-4,0,-0.0020833611488342285,1,0,0,1)` |
| Paint / final mode / blend | `ffffffff`, `0e`, SRC_OVER |
| Target / key | Board PA `bec5f200`, matched retained key `10005` |

The path covers `[64,67) × [0,480)`. Pixel-center nearest sampling selects
source columns 0..2 and reverses source rows 479..0; the other source columns
are padding in the submitted row width. The packet alone does not identify
the texture's GUI role or prove its pixel contents.

The original MAS producer at VA `106b4150` emits the active and allocated
extents from image fields `+52/+54` and `+56/+58`. Register `156` combines the
base format at `+28`, bytes `+84 << 16` and `+85 << 8`, and two independent
flag bits. Thus `0100` is format/state, not a byte pitch. The existing RGBA
conversion profile also uses `001b0100` with four-byte source storage. The
fixed [Z7 reference's pixel reader](https://github.com/WonderLantern/z7-qemu/blob/97abf0b81cd3211f8374a38987a0beaf4549b90f/qemu/nikon-z7-gpu.h#L262-L275) treats low formats `0`/`100` as packed
RGBA, `18` as white RGB plus source alpha, and mode `0e` as per-channel paint
modulation. These software joins support the finite extension; they do not
recover the physical meaning of every source flag.

The parser accepts only the complete words `001b0018` and `001b0100`; other
formats or flag combinations still reject. Both use the existing five-segment
inline path, opaque paint, affine f24 sampler, final mode `0e`, SRC_OVER and
initialized or target-matched retained context. Initialized and independently
optional cached-matrix/color-disable prefixes share this body validation.
Source storage is exactly `width × height` bytes for A8 and four times that
for RGBA. All bytes participate in the existing backing, command-overlap,
destination-overlap and effective-alias checks before execution.

The common work budget remains 65,536 texels, with a separate 4096 bound on
each dimension. This allows bounded strips and other aspect ratios rather
than only the captured 16 × 480 tuple. RGBA scratch input is therefore at
most 256 KiB. These are functional resource limits, not recovered hardware
capacities. Straight RGBA, nearest/clamp filtering and integer rounding
remain explicit functional assumptions. Each sampled RGB channel is
modulated by the corresponding paint channel; source alpha and path coverage
produce a premultiplied overlay for the existing SRC_OVER executor.

The added native `rgba-image` fixture checks initialized and retained packets,
alternate backed targets,
both optional prefixes, all four temporary paint modes, a synthetic texture
with the captured strip geometry, horizontal/vertical work limits, channel
modulation, transparent and partial-alpha pixels, fractional coverage,
clamping and inversion. It compares the complete output and guards against
an independent rational-coordinate oracle with literal colored anchors.
Negative cases cover unknown formats/flags, dimensions and texel budgets,
RGBA's four-byte span, address wrap, command overlap, backing alias, missing
or foreign context, input/map loss, reset and cancellation. Deferred source
changes and immutable admitted command bytes are checked separately.
The valid initialized RGBA fixture rejects against the previous model. The
complete `nikon_transfer_unit` executable now passes 29 subtests in 2.57 host
seconds, including the 35 RGBA-image variants above. This is native fixture
acceptance, not a new original menu-screen or physical-color claim. Separate
read-only review finds no blocking issue in the arithmetic, span checks or
context lifetime. A new original-firmware run with a standard external FAT32
input first reaches the fill engine's copy-mode boundary, before this texture
submission; it does not establish original execution of the new RGBA path.

## Validation and next stop

The following observations describe the earlier A8 implementation and its
historical next stop.

The new valid initialized-image fixture fails against the old model with
MEMTX_ERROR. A second native red reproduces retained mode14 rejection after
initialization. The final complete `nikon_transfer_unit` batch passes all17
subtests in0.99s. Its new40 variants cover actual geometry/sampling, temporary
modes, target0/alternate targets, retained matching, cold/foreign/reset/failed
context, fixed-record7 path rejection, full pixels/guards, malformed prefixes,
formats/keys, backed4GiB crossing, aliases, deferred source changes,
immutable commands, map/input loss, reset and unrealize. Independent read-only
review finds no concrete defect.

An intermediate unchanged-firmware run isolates the first image: its whole
720×480 output exactly matches an independent black/A8 SRC_OVER calculation
from captured source and preimage. Exactly578 pixels change, within inclusive
x288..327/y444..475. The final run then receives original IRQ129 after224/472
and all seven176/336 continuations, reaching complete840/1040-byte buffers at
`bd0e0540/bd160aa0`. The next CPU1 PC `106ee3c6`, WRITE32 PA `201810e0=1`,
rejects with `transfer-path-batch-count-not-supported`. Its complete grammar,
upstream producer and state combination need the next analysis; this diagnostic
does not establish a malformed guest count.

Inputs remain the declared descriptor/TEMP/resource12, RNG0, acceleration,
cold F4 and normally paced healthy UART profiles. Final BatCk is `00010001`;
GUI gate/pending3/display-ready remain zero, and the peer rejects no request.
Guest-memory pixels and handler entry do not establish scanout or a first frame.

Ignored receipts: `run guiimage/`, including original `red`,
`probe`, intermediate `initialized`, final `green`, physical source/preimage
and output captures, complete next buffers, source projections, native
red/mode-red/final logs, `initialized-pixels.json` and `verified-progress.json`.
