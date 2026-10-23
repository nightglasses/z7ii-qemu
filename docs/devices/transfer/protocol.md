# Graphics packet transfer

## Current normal-startup frontier

The [inline rectangle model](rectangles.md), [RAM backing fix](../../startup/ram.md)
and [context continuation](continuation.md) now cross both the 856/1088 and
2120/2368-byte checkpoints. Original IRQ 129
follows each pair. The device retains the initialized format/clip profile and
expected key across ACK, validates the whole next batch and publishes updated
state only after successful pixel execution.

The [RGBA SRC_OVER extension](over.md) validates the next 176/368-byte
indirect rectangle and implements paint `cc000000` over the observed transparent
target, plus shared path/image destination-alpha handling. Original IRQ129
follows five further pairs, ending with224/376. The later
[image context model](image.md) recognizes224/472 as fully initialized A8
sampling, retaining a validated target and accepting the complete temporary
paint-mode combinations before final mode0e. All17 transfer unit cases pass.
Original execution completes224/472 plus seven176/336 pairs through IRQ129.
The next CPU1 WRITE32 PA `201810e0=1`, original PC `106ee3c6`, rejects with
`transfer-path-batch-count-not-supported` on complete840/1040-byte buffers at
`bd0e0540/bd160aa0`. The next state/operand family needs producer analysis.
GUI readiness is zero and no first frame is claimed. The analysis below
records earlier checkpoint receipts.

At the [Audio observation checkpoint](../audio/monitor.md), unchanged
firmware's first uncovered access was CPU1 MAS VA106ee3c6, WRITE32 board
PA201810e0=1. Original submitter106ee04a programs64-bit unit counts6b/88:
setup856 bytes at PAbd0dff90, draw1088 bytes at PAbd15ff90. Both complete
buffers are saved in `work/runs/audiolevels-20261023/final-stream0.bin` and
`final-stream1.bin`; `verified.json` binds the actual submit metadata. The
trace includes both writer-entry and store instruction PCs, since later TCG
blocks enter at106ee3c6. Earlier snapshots using only106ee3c4 metadata are
partial buffers, not complete submissions.

Both streams contain720x480 geometry fields. Draw word50 is0, so the earlier
path dispatch predicate does not select this family;1088 matches no other
supported draw-size dispatch. The fallback LUT parser requires224/1688 and
therefore reports`transfer-stream-truncated`. The transport has read the
submitted extents; this diagnostic does not establish genuinely truncated
guest commands. The producer and finite SRC-clear contract are now recorded
in [rectangles](rectangles.md). The earlier rejected submission itself does
not establish IRQ129, rendered output or a GUI first frame.
The older supported profiles and receipts below retain their own scope.

## Existing transfer contracts

The original C1.70 producer `VA106eeb5c` fills a packet consumed by
`106ee148`. The latter writes source and destination addresses at offsets
`c00/c04`, secondary addresses at `c34/c38`, geometry at `c08/c0c`, control at
`c10`, and starts with `c18=1`. Control bit3 selects the alternate producer
which supplies byte extent at `c20` and zero extension/padding at `c24/c28`.
Its extent is computed from the two dimensions and the numeric byte factor;
neither a color name nor a framebuffer address is assumed. For ordinary
single-plane packets with equal geometry and equal numeric format selectors,
the corresponding byte factors are `4/3/2/2/2/1`. The unconverted transfer
copies the resulting contiguous byte extent. The original selector function
`106eeaf0` maps only format `1c` to selector5. Its ordinary command also
supplies a secondary address pair at `c34/c38`; the same producer's alternate
byte-stream equivalent gives that second plane the same width*height byte
extent (`width*height*8 >> 3`). This source-backed second plane is copied
after the first and completes as one packet, without interpreting either
plane's colors. Both extents are validated before any bytes are written.
Contiguous rows follow the
producer's dimension-only packet; unlike an imported surface's software
stride, that command has no independent row-pitch register. Rotation, format
conversion, unequal geometry and unbound secondary-plane formats remain
outside this implemented protocol rather than being silently treated as copy.

`106ee4e4` registers IRQ129/130/131 using the original literal words at
MAS O`6ce584/588/58c` (`106ee3cb/106ee3d7/106ee3e3`). The respective Thumb
handlers `106ee3ca/106ee3d6/106ee3e2` write zero to `20181020/20180c18/2018001c`
and dispatch software notification masks `1/4/2` to the same continuation.
The registration and kernel user-handler table consumer form the static
binding; the guest has not yet run these handlers in the original boot.

The shared continuation `106ee4c6` keeps notification values 2 or 4 and maps
other values to 1, then sends the first packet word through
`106d4ffe -> 105a65a4`. The corresponding query
`106ee436 -> 106d5034 -> 105a6626` uses the original 20-byte record at
`VA10788f00` (MAS O`768f00`): words `0,7,21,0,ffffffff` in hex. The query
replaces record+c with its output pointer. These are real guest event-handle
and wait/clear consumers; the device never fills their RAM or returns a
host-generated event flag. IRQ130's original handler supplies mask4, which
the subsequent command3 consumer explicitly checks.

The byte-stream model snapshots a complete packet, validates the complete
source and destination ranges against actual directly backed memory, and
uses QEMU DMA to copy bytes. Each DMA transaction is bounded by the current
effective source and destination MemoryRegion sections. In particular, a short
ROMD alias must switch to its adjacent RAM or ROMD backing, not read hidden
bytes from the aliased ROM device. Each timer batch rechecks direct access and
destination writability; losing either stops the packet without a successful
completion or an MMIO read. Its virtual timer raises IRQ130 only after all
bytes have been written. `c18=0` acknowledges the IRQ, or cancels a transfer
before its next byte batch. Address/configuration registers remain the
original packet values; internal progress is separate. A failed DMA never
raises a successful completion. Unknown commands, addresses, layouts and
access widths fail through the common access diagnostic.

Zero reset state, a nominal 48MHz clock, at-most-4096-byte scheduling batches and
cancel-before-next-batch semantics are functional-board timing/reset
assumptions. They are not measurements of the Nikon transfer engine.
The interrupt ACK address and mask binding have source evidence; the level
completion timing is a functional model of that software-visible protocol.
The complete initializer `106edfd6` writes `1020=0,1248=1,c18=0,1c=0`,
registers the three handlers, then writes `34=0,c1c=8000,151c=1,155c=1`.
Those finite configuration fields are retained. The later `106ee04a`
configuration also retains `1214=ffff` or `0100ffff` (the original boolean
branch) and `1298=0`. This admits its empty-table setup, not an upload job or
an arbitrary configuration aperture. Nonzero optional table lengths instead
supply source/count/start fields at `10d4/10dc/10d0` and
`10e4/10ec/10e0`. Their owned request frontend is described below;
the finite paired executors below own processing and IRQ129 completion.
A single accepted setup field still raises no IRQ. The `c1c` bit15 enable
interpretation is a functional convention grounded in the installed value;
clearing that field suppresses the modeled completion IRQ. It is not an
independently measured silicon register definition.

The source file registers `nikon-transfer-functional`, a 0x2000 SysBus MMIO
region with IRQ outputs 129,130,131 in that order. Board mapping is
`PA20180000`, subject to the original page-table identity mapping evidence;
machine/header/Meson integration belongs to the shared integration owner.

## Owned graphics stream requests and finite LUT executor

The original `106ee04a` writes two independent source/count/start triples at
`10d4/10dc/10d0` and `10e4/10ec/10e0`; start is1. The
`graphicsextent1` probe establishes that descriptor extents56/422 count
32-bit words, while port counts28/211 count 64-bit units. The complete pair is
therefore224/1688 bytes (`8*count`), not the historical56/422-byte prefixes.
The frontend requires explicit `1214/1298` configuration, validates each
complete direct-memory source, and snapshots it immutably across effective
MemoryRegion boundaries.

The original wait path `106ee1f8` (cases 8/10) additionally writes `1214` with
bit24 (`0x01000000`) while both streams are still pending, then spins on bit26
of register `58`.  That write is a caller fence request, not a configuration
change: the frontend accepts it during a pending stream and keeps the retained
`1214` value.  Completion itself stays timer-driven and is reported through
`58`, so the spin exits only after the stream actually completes.

The LUT C1.70 program family executes. Three unchanged-firmware jobs
were observed with the same command-header fingerprint and different operand
addresses/table values. The accepted profile is:

- stream0 is exactly224 bytes and stream1 exactly1688 bytes;
- the setup/draw command headers at setup words0/22/36/42 and draw
  words0/10/87/344/355/396/408/415 must match the observed LUT family;
- setup word37 and draw word409 select the same readable input address;
  draw word397 alone selects the writable output address;
- setup word39 encodes128-byte input stride, setup word40 is128x128, and
  draw word11 is32x32;
- draw word414 input format2 and draw word403 output format`01000000` are the
  only supported format pair;
- draw words88..343 are256 unsigned 12-bit primary-LUT entries, and words
  356..387 are32 unsigned 12-bit secondary-LUT entries.

The physical graphics result is not recovered. This is an explicit functional
profile, not a generic processor or imported Z9 ISA: each output byte `(x,y)`
samples input byte `(4x,4y)`, looks it up in the256-entry table, selects the
32-entry table with primary bits11..7, and rounds that 12-bit value to8 bits.
The operation reads a128x128 one-byte source and writes only the descriptor-
selected32x32 one-byte destination. It does not write the command streams,
input, scratch-looking operands, status RAM, callbacks, tasks, PCs, or a fixed
menu image.

Both operand extents must be contiguous directly backed memory, the source
must be readable, the destination writable, and their ranges must not overlap.
Address arithmetic is checked before access. Unsupported fingerprints,
lengths, formats, geometry, LUT widths, unbacked/MMIO ranges, overflow and
source/destination overlap reject the second START explicitly, discard both
owned requests and raise no success IRQ. Validation completes before any
output write; an execution-time input/map failure preserves the destination.
A successful timer execution writes the full1024-byte result, consumes the
pair and raises IRQ129. `1020=0` retains the observed ACK/deassert behavior.
After successful completion, a word read of offset `58` returns zero for
the observed bit26-clear path; reads before completion remain errors. IRQ131,
fences, replies and other graphics programs remain unsupported. The
ordinary byte-copy engine and IRQ130 remain separate.

## Finite black YUV422 rectangle clear

An additional original C1.70 job was captured after an explicit EX ADC/pad
input experiment in `work/runs/lcdnext-type2-transfer2`. Its stream extents
are224/1648 bytes (port counts28/206), rather than the LUT pair's224/1688.
The firmware and its command buffers were only read by the debugger. This
observation establishes the submitted commands, not a completed LCD boot or
the electrical input values of a physical camera.

The command header interpretation is corroborated by
[WonderLantern's Z7 decoder at97abf0b](https://github.com/WonderLantern/z7-qemu/blob/97abf0b81cd3211f8374a38987a0beaf4549b90f/qemu/nikon-z7-gpu.h#L482):
the low three bits select the operation, bits3..10 encode count minus one,
bits12..15 select writable register bytes, and the high16 bits select the
register. That project's mode8 clear, YUV422 two-plane layout and six-row
alignment treatment corroborate the functional interpretation across models;
they do not establish Z7II silicon fidelity. This implementation validates a
single finite command fingerprint rather than importing that renderer.

The accepted setup is an identity-transformed filled rectangle with path
register65=`100`, extent1024x688, signed integer coordinates `(0,6)`, width1024
and height682. The draw must retain the captured command order, masks,
matrix/color-conversion state, clip `(0..1023,0..687)`, source-blend state,
mode8, zero clear color, output format`105`, extent1024x688 and draw trigger.
The setup/draw path key must agree. Both table upload headers and locations
are fixed, and all256/32 table entries must be unsigned12-bit values. Those
values are otherwise ignored because this accepted mode never samples the
source. The source-looking path key is not read as texture memory.

Register1a4 selects the Y plane and1b5 the interleaved UV plane. Their address
delta must equal1024*682 (`aa800`), as in the original capture. The functional
model maps the rectangle's six-row aligned coordinate origin onto the exact
682-row physical plane; it writes only698368 bytes per plane. Limited-range
BT.601 black encoding (Y16 and U/V128) is an explicit functional assumption,
not a measured physical-camera color result. Both entire target planes must
be directly backed writable memory, with no address overflow, overlap with
one another or with either submitted command stream. Backing aliases that
map both planes onto overlapping host storage are also rejected.

Both targets are validated and mapped before either is changed. The existing
deferred stream timer then fills both planes and raises IRQ129; the original
`1020=0` ACK deasserts it. Reset or unrealize before that timer executes
cancels the job without writes or completion. Unsupported lengths, headers,
fields, colors, formats, geometry, plane deltas, ranges and overlap fail
explicitly with no target changes or successful IRQ. Execution-time loss of
writability is revalidated before modification. The LUT profile remains
separate and unchanged. The selected targets are graphics surfaces; scanout
still requires the guest's actual display commit.

The synthetic `/nikon/transfer/yuv-clear` unit case assembles register packets,
uses changed LUT contents, checks every written Y/UV byte and surrounding
guards, IRQ129 delay/ACK, unchanged command streams, reset/unrealize
cancellation and execution-time map loss. Near-miss header/count, truncation,
format, rectangle geometry, mode, color, key, oversized LUT, address overflow,
plane overlap/delta, MMIO targets and command-stream overlap preserve both
destinations and never read MMIO. This is fixture coverage, not natural-boot
acceptance.

The corrected SPI cadence lets unchanged C1.70 progress farther through this
same producer. Its first three submissions are the existing224/1688-byte LUT
profile. The fourth is the existing224/1648-byte format105 clear. The fifth is
another224/1648-byte clear whose setup stream is byte-identical to the fourth;
only draw words9/379 and the two destination addresses differ. Both format
words are107. Its primary destination is `cb4a7000`, its secondary destination
is `cb5fc000`, and their exact delta `155000` is
`2 * 1024 * 682` bytes. These are passive original-firmware observations at
the correct `10020000` MAS VMA, not guest state injection.

The format-emission sites at `106454e2/106454ea` and
`106460fa/106460fc` independently produce the two operands from the current
surface. The second path emits the secondary destination at `1064610e/1064611c`
for low format bits5 or7; `1067c620/1067c624` selects format107 and a16-bit
primary element width. WonderLantern's fixed Z7 implementation
corroborates the remaining finite layout: format107 stores two pixels as
`Y0,Y1,U,V` in the primary plane and uses a separately addressed one-byte
alpha sample per pixel. [Z7 pixel access and extent
model](https://github.com/WonderLantern/z7-qemu/blob/97abf0b81cd3211f8374a38987a0beaf4549b90f/qemu/nikon-z7-gpu.h#L259-L326).
The shared command encoding and the Z7II's observed address delta make this a
bounded cross-model functional profile; they do not claim identical physical
graphics silicon.

For the observed mode8/color-zero job, format107 writes repeating limited-range
BT.601 black bytes `10,10,80,80` in hexadecimal and zero to the full8-bit alpha
plane. Primary and secondary extents are respectively1396736 and698368 bytes,
must be adjacent in that order, and are both validated and mapped before either
is changed. Format `01000107`, which the Z7 reference treats as packed4-bit
alpha, remains unsupported because the Z7II did not submit it. Mismatched
format operands, wrong plane delta, overlap, range loss and every prior command
fingerprint boundary still fail without completion. This clear advances the
firmware through its own IRQ129/wait path; it is not itself a scanout or a host
rendered frame.

The extended yuv-clear fixture fails at the valid107 START against the prior
device and passes after this finite executor is present. It covers both exact
formats, full output bytes and guards, the expanded-primary overflow and
command-stream overlap boundaries, reset, unrealize, backing alias and map
loss. All five transfer unit cases and all seven focused `/arm/nikon/transfer`
qtests pass in the incremental native build. In the corresponding unchanged-
firmware run the old `201810e0` format error is absent; after the fifth graphics
completion the next explicit boundary is CPU0 `PC1046d6da`, READ32
`PA20146604`. That is the display bank1 port0 scalar at bank-relative offset
`2604`, directly downstream of graphics-buffer initialization.

## Finite rear-LCD RGBA composition clear

After the source-backed bank `2604` value was retained, unchanged C1.70
naturally submits a sixth complete graphics pair at `106ee3c6`. Stream0 is
exactly232 bytes (29 port units) and stream1 is exactly416 bytes (52 units).
The setup creates two indirect filled paths with keys `00010001` and
`00010002`. Both use segment bytes `02 07 09 07 00` and five float32
coordinates `(0,6,1024,682,-1024)`. The draw selects format0 RGBA8888,
1024x688 extent, target `bf309600`, mode8, packed little-endian paint
`ff000000`, state `108`, and triggers both keys once. This is passive
original-firmware evidence at the correct `10020000` MAS VMA, not a synthetic
boot input.

Command decoding and the interpretation of indirect paths, format0 RGBA image
storage, mode8 clears and packed little-endian RGBA paint are corroborated by
[WonderLantern's fixed Z7 implementation at97abf0b](https://github.com/WonderLantern/z7-qemu/blob/97abf0b81cd3211f8374a38987a0beaf4549b90f/qemu/nikon-z7-gpu.h#L158-L179)
and its [draw path](https://github.com/WonderLantern/z7-qemu/blob/97abf0b81cd3211f8374a38987a0beaf4549b90f/qemu/nikon-z7-gpu.h#L306-L451).
The model imports none of that project's memory whitelist, generic renderer or
console probe. Cross-model agreement supports this finite command meaning; it
does not claim identical Z7/Z7II physical graphics silicon.

The executor accepts only the observed232/416-byte command ordering and every
fixed header, mask, register value, path key, format, extent, mode, color,
state and pair of draw triggers. The two dynamic indirect-path operands must
match between setup and draw. Their shared segment and coordinate addresses
must be complete readable direct-memory ranges containing exactly the path
above. Unknown opcodes or fields, near-match programs, other formats or
geometry, truncated streams, bad path input, unbacked ranges, 32-bit overflow,
or overlap among command streams, path inputs and output reject before a
successful IRQ.

Resource57 table record5 establishes the exact backing slot
`[bf309600,bf7b9600)`. The draw declares the complete1024x688 target extent
`[bf309600,bf5b9600)`, length `2b0000`, and preflights that whole range. Unlike
the two-plane YUV profile, format0 has no six-row `y_bias`: path y=6..688 writes
only rows6 through687. Successful execution maps and writes RGBA bytes
`00 00 00 ff` to `[bf30f600,bf5b9600)`, exactly `2aa000` bytes. It leaves the
top alignment rows `[bf309600,bf30f600)`, slot tail
`[bf5b9600,bf7b9600)` and both neighbors unchanged. Reset or unrealize before
the deferred timer cancels without output or IRQ; execution-time mapping loss
also preserves the surface. Completion uses the existing IRQ129 and original
`1020=0` ACK path.

The target is an upstream primary/HAIMENMONI rear-LCD composition surface, not
the final scanout and not EVF output. The model does not present these bytes or
copy them into the `cb4a7000` display arena. That next consumer/commit remains
firmware-produced and is the next natural execution boundary after this
profile. Synthetic unit coverage verifies every active output byte, untouched
top rows, tail and guards, strict field/path/format/range/overlap rejection,
opaque-operand echo/nonzero/distinct rules, destination preservation,
execution-time map loss and reset/unrealize cancellation. The memory qtest
independently checks the exact resource57 slot boundaries and reset retention.
These fixtures do not claim unchanged-firmware GUI or LCD completion.

The functional allocation bound is128 KiB per stream, not recovered silicon
capacity. Odd command-stream sources, oversized counts, zero/missing-count
starts, MMIO/unbacked command sources, duplicate starts and pending operand
changes fail. The six stream ports are word-write-only; neighboring fields
remain errors. Named `stream-request[0..1]` outputs are host-facing ownership
signals, not GIC connections. `nikon_transfer_stream_request()` returns an
immutable, reference-counted byte snapshot with source, `units64` count and a
nonzero ownership token. Reset/unrealize cancel pending or scheduled work and
invalidate tokens without reusing them; held GBytes snapshots remain readable.
Rejected and executed pairs likewise invalidate current tokens. Read-only QOM
pending/token/SHA256 properties describe the currently owned bytes.

Read-only watchpoints locate the first-header writer at `10645418` and a
template-copy writer at `10360f5c`; `106453cc/10645454` corroborate word-pair
append and bulk-copy production. Those observations support the finite packet
fingerprint only. They do not establish a processor ISA or physical GPU
fidelity, and Z9's different command encoding is not transplanted.

### Initial mode-byte variant

The constructed descriptor17 run supplies the same232/416-byte RGBA program
with draw word25=8 instead of0. Header word24=`01841004` writes register184's
low byte. Original MAS setter `10645c86` preserves software-shadow bit3 while
changing another field; the clear builder `1068bd90` calls it with0 at
`1068bdee`, then calls `10646062(...,1)` at `1068be22`. The latter emits the
already-required word59/60 pair, overwriting the same low byte with8 before
either draw trigger. The [producer investigation](../../runtime/stream.md)
records the original addresses and fixed public decoder corroboration.

The finite fingerprint therefore accepts only0 or8 at word25. Word60 must
still be8; all other fixed fields, ordering, path data, geometry, DMA bounds
and overlap checks remain unchanged. The existing executor performs the same
RGBA clear and deferred IRQ129. No arbitrary register program is accepted.

The new initial8 fixture fails against the old parser, then the complete
`nikon_transfer_unit` batch passes: one executable, six registered test cases.
It checks both initial values' full pixels and guards, rejects initial18 and
final word60=0, and exercises initial8 with map loss, reset and unrealize.
Receipts: `work/runs/streammodel-20261022/{red,green}.log`.

With unchanged firmware and the same explicit functional input, the captured
indirect paths match the model, the surface's active first/last samples become
`00 00 00 ff`, and original IRQ129 handler `106ee3ca` is entered. The next
176/368-byte program initially fails the LUT length guard, motivating the
finite single-path extension below.

### Finite720x480 single-path clear

The model also accepts the captured176-byte setup/368-byte draw pair with
format0 RGBA8888, extent720x480, clip719/479, mode8, paint `ff000000`, state108,
one path key `10001` and one trigger. Target `c175d600` is fixed to resource57
record7; its physical display identity is unestablished. The shared DMA/path
validation requires segments `02 07 09 07 00` and float32 coordinates
`(0,0,720,480,-720)`. The path operand at setup34 must be nonzero and echo at
draw80. Draw91 is the fixed ending operand1, not a second dynamic path operand.
All other headers, fields, ordering and zeros match this separate finite table.
Initial draw25 is0/8, with draw60=8 still mandatory. Crossed profile lengths,
other formats/geometry/clips/colors/targets, bad endings, unknown state and
invalid path/range/overlap reject without completion.

The existing deferred RGBA executor writes `00 00 00 ff` to precisely
`[c175d600,c18aee00)`,151800 hex bytes, without a six-row offset. Resource57's
record7 slot ends at `c1c0d600`; its remaining tail is untouched. Native
fixtures compare every output byte, both guard regions and the full slot tail,
reject changed fields, setup/draw truncation and bad indirect data, and retain
the IRQ/ACK, reset, unrealize and execution-time map-loss checks for initial8.
The new case fails against the previous model, then the complete transfer unit
batch passes one executable/seven cases. Receipts are
`work/runs/singleclear-20261022/{red,green}.log`.

The unchanged bundle with the same explicit functional input submits the pair;
its target first/last samples change from cold zero to `00 00 00 ff`, the tail
sample remains zero, and original IRQ129 handler `106ee3ca` is entered.
The subsequent168/232-byte pair selects gray paint `ffb4b4b4` and a
centered rectangle `(96,86,528,308,-528)` on the same target. It omits full
format/extent/clip initialization and uses the bounded continuation below.

### Finite gray continuation and retained context

Successful completion of the full720x480 profile establishes a finite known
context: format0, target `c175d600`, extent720x480, clip719/479 and its remaining
fixed state. This is represented by one validity bit, not a generic register
bank. Completion publishes it only after successful DMA execution; ACK and
ordinary stream ownership changes retain it. Successful gray continuation
retains it too. Reset/unrealize, rejected complete programs, execution failure
or completion of the LUT/YUV/1024-wide profiles invalidate it. A canceled or
failed initialization cannot establish context. Busy or malformed transport
writes still fail before execution; they do not synthesize a context.

The observed gray packet is168/232 bytes, target `c175d600`, paint `ffb4b4b4`,
mode8/state108, key10002, one trigger and normal ending/NOP. Its arrays are
`02 07 09 07 00` and `(96,86,528,308,-528)`. It now uses the shared path
processor below, including its field, topology, DMA and context checks;
the former dedicated gray fingerprint and executor branch were removed.

The path executor maps/prechecks the complete target before writing.
For this input it writes528 pixels on308 rows with720-pixel stride;
only `[96,624) × [86,394)` changes to bytes `b4 b4 b4 ff`. Side gaps, other
rows, target tail and neighbors remain unchanged. It raises IRQ129 after
successful work through the existing deferred completion path. This extends
finite guest-produced graphics processing; it does not publish a console frame.

The new gray fixture fails on the previous parser, then the complete transfer
unit batch passes one executable/eight cases. It compares the entire target,
guards/tail, delayed IRQ and ACK, context retention across ACK/repeat, cold and
wrong-context rejection, reset/canceled/failed initialization, malformed fields
and DMA operands, execution-time map loss and pending reset/unrealize. It also
checks context invalidation after a rejected program or failed execution.
Receipts: `work/runs/graycontext-20261022/{red,green}.log`. The intervening
`fixturecomparison.log` is a test signed-extension comparison error, corrected
by storing the loaded word as uint32 before comparison; it is not a pixel defect.

With unchanged firmware and the same explicit functional input, real target
samples change from black to gray, the side/next-row samples stay black, and
original IRQ129 handler `106ee3ca` is entered. The subsequent pair is
21488/28832 bytes with131 transformed black paths, keys10003..10085, on the
same target. Complete decoding and all131 indirect paths are retained;
the bounded path profile below now processes it.

### Finite transformed opaque paths

The [original glyph evidence](../../runtime/path.md) establishes absolute
CLOSE/MOVE/LINE/QUAD segments00/02/04/0a, column-major affine matrices, direct
target-memory y, nonzero winding from registera5 bit8 and black SRC_OVER.
The131 paths correspond to six ROM strings warning that the battery cannot
communicate with the camera; they are not a normal ready screen.

The packet profile accepts1..256 paths, each exactly41 setup words and55 draw
words, one final two-word ending and zero alignment padding where needed.
All fixed fields retain their observed values: format0/target `c175d600`,
state108, mode8 and blend selector1. Paint may be any opaque packed RGBA;
operand `00040102` selects nonzero and `00040002` selects even-odd fill.
Count/descriptors, matrix, DMA operands and matched sequential keys vary.
The known720x480 context is mandatory and retained after successful completion.
Counts are bounded to64 segments/256 float coordinates per path; these and
coordinate/matrix magnitudes4096/transformed8192 are functional work limits,
not recovered silicon capacities. Perspective/singular/nonfinite transforms,
other segments, malformed contours/counts, unsupported states, nonopaque source
paint, missing context and incomplete packets remain errors. Destination
alpha0..255 is supported by the later [SRC_OVER extension](over.md).

`devices/path.c` validates topology and converts quadratic to exact cubic
control points. Relative horizontal07 and vertical09 each consume one scalar,
update the object-space position and then apply the affine matrix. Cairo A8
WINDING or EVEN_ODD/OVER coverage uses GRAY antialiasing and
1/16-pixel tolerance. It never selects a font or imports text. Transfer code
preflights the full target, packet ranges and all indirect arrays; it rejects
guest overlap and effective RAM-backing aliases with the target. Read-only
arrays may be reused by multiple glyphs. At deferred execution it revalidates
data, composites each path/paint in command order into a premultiplied RGBA
scratch overlay, maps/prechecks the full target, then applies SRC_OVER
with integer rounding. Any late
bad path, map loss, reset or unrealize prevents output/completion. No partial
glyph batch is committed. Coverage precision is a functional approximation.

The new synthetic path fixture distinguishes transformed geometry, quadratic
interior from its bounding box, nonzero contour direction/holes, partial
coverage over gray, clipping, late bad data and cancellation. Old-parser red
and nine-case transfer green are in `work/runs/pathmodel-20261022/`; the
independent backing-alias review produced `aliasred.log` before its fix and
`finalgreen.log` afterwards. `buildinclude.log` is an initial missing math-header
compile failure, not the red fixture receipt. Cairo is an explicit Z7II Meson
dependency and the official QEMU submodule is unchanged.

The final unchanged-bundle functional run produces25958 changed panel pixels,
including11874 solid black and14084 coverage-edge pixels, and enters original
IRQ129. All target alpha remains opaque and pixels outside the gray panel
remain black. Captured `frame.bin` is guest memory, not a scanout commit.
`preview.png` merely reverses rows following the fixed Z7 presentation
convention; it does not prove Z7II panel orientation or a visible first frame.
The subsequent824/1112-byte pair has five relative H/V rectangles, even-odd operand
`00040002`, and mixed `ffe6e6e6`/`ffb4b4b4` paints. It now completes through
original IRQ129, preserving all pixels outside the five paths. Native coverage
adds same-direction even-odd holes, relative H/V, overlapping ordered paints,
distinct RGB channels, invalid alpha and relative topology. The focused
nine-case batch passes; receipts are `work/runs/coloredpaths-20261022/`.
The subsequent176/336-byte packet uses the finite image extension below.

### Inline signed32 path and A8 image modulation

The [image evidence](../../runtime/image.md) identifies a44/84-word packet,
five inline signed32 coordinates, relative H/V segments, an80x72 A8 source,
f24 sample matrix and mode14 modulation by opaque packed RGBA paint. The
model accepts this exact header/state family in the retained720x480 context;
variable dimensions are bounded1..256 and both extent writes must match.
Only the known inline five-segment path, affine matrices, opaque paint,
matched class1 keys and nonzero echoed operands are supported. Other image
formats, modes, perspective, indirect geometry in this family and nonopaque
source paint stay explicit errors. Destination alpha0..255 is supported.
The later [initialized image extension](image.md) also validates the full
224/472 initialization and retains the explicit target rather than a record7
boolean. Retained176/336 images require that target; fixed record7 path batches
still require `c175d600`. Temporary paint modes8/10/12/14 are accepted before
the final mode0e write; unknown bits and final-mode changes remain errors.

`nikon_transfer_image_parse` validates the entire target, command and image
ranges/overlaps and effective backing aliases. At deferred execution it reads
the current alpha source, reuses the affine path rasterizer and builds an
image-modulated premultiplied overlay. `nikon_path_image` decodes the f24
matrix, samples at pixel centers with functional nearest/clamp behavior and
preserves direct buffer y. The shared path executor validates output mapping,
commits normalized RGBA SRC_OVER and raises IRQ129. Late errors, reset and
cancellation leave the target unchanged. No console frame or guest callback
is injected.

The synthetic fixture covers alpha/color channels, stride, transforms, flip,
clamping, malformed/unsafe inputs, backing aliases, delayed data reads, map
loss and reset/unrealize. The focused native batch passes all10 cases after
old-model red. Unchanged firmware then renders the red icon:3751 changed
pixels,3365 solid and386 alpha edges; all outside-patch pixels match the prior
frame. Receipts: `work/runs/texturemodel-20261023/`. The subsequent224/1736-byte
RGBA-input/YUV105-output pair uses the finite conversion below; see the
[current frontier](../../validation/frontier.md). Physical raster/filter
precision and final scanout remain separate, unverified properties.

### Finite RGBA input to YUV105 and scaled YUVA107 planes

The [conversion evidence](../../runtime/conversion.md) establishes56/434-word
commands, a1024x688 source with fixed f24 sampler, inline integer rectangle
covering logical rows6..688, SRC mode2, output105, a256-entry identity primary
table and exact captured32-entry secondary configuration. The profile fixes
all sampler, conversion, LUT, state, geometry and ending fields. Only image/
output addresses, matched class1 key and echoed opaque operand vary. Other
table values remain unsupported. A second exact224/1736-byte profile accepts
720x480 scaling into107; it does not allow arbitrary mixed state.

`nikon_transfer_convert_parse` checks2818048 source bytes and698368 bytes
per Y/UV plane, exact plane delta, complete direct spans, wrap, overlap and
effective backing aliases. The generalized alias helper also serves prior
path/image profiles. At deferred execution, source alpha must be0 or255;
transparent pixels become black and opaque pixels retain RGB for the captured
SRC-to-no-alpha operation. The exact fixed f24 row expression selects source
rows687..0. Functional limited-range BT.601 conversion and pairwise chroma
averaging use the fixed Z7 convention; physical programmable LUT/matrix
precision remains unverified. The known tables qualify this bounded state,
not general LUT execution.

The complete image is read and both planes computed in scratch memory before
either output is mapped for writing. Both maps and their backing independence
must succeed before the two copies. Late failure, reset or unrealize preserves
both outputs and publishes no IRQ. Success raises IRQ129, with the prior
720x480 continuation context invalidated by the new full initialization.

Native old-model red becomes green with all11 transfer cases. The new
fixture checks seven colors, independent sampling and chroma oracles, every
plane byte, alpha, source/command/plane aliases, bad fields/tables/ranges,
delayed input, map loss and cancellation. Unchanged firmware then converts
the separate black RGBA source to full Y16/UV128 planes through original
IRQ129; it leaves source and guards unchanged. Receipts:
`work/runs/yuvmodel-20261023/`.

The107 profile reads1382400 source bytes and writes1396736 packed Y0/Y1/U/V
bytes plus698368 separate alpha bytes, with exact plane delta155000(hex).
The captured float path matrix rasterizes a1024x688 mask whose active rows
begin at6; first-row/final-column coverage is partial. Its fixed f24 sampler
selects720x480 source pixels at target centers using exact integer ratios.
Selector7 uses the declared functional nearest/clamp convention. Source and
old-target alpha0..255 are supported. SRC coverage preserves the old target
where coverage is partial; old packed YUV is converted to RGB before blending.
Both targets are read into scratch, and both writable maps/alias checks must
succeed before either commit. Successful107 completion retains this finite
state for the SRC_OVER continuation below.

`work/runs/yuva107-20261023/` records old-model red and all12 native cases
passing, including partial alpha, edge color/alpha retention and previous
safety/cancellation boundaries. Unchanged firmware completes this pair through
IRQ129 with source/guards preserved, packed Y16/UV128 and alpha0. The next
176/1552-byte packet switches to the warning source and SRC_OVER while omitting
full target initialization. This exact continuation now processes using the
successful107 context. It validates the shorter192 upload and low16-bit196
write, both fixed LUTs, sampler, blend, layout and dynamic operand matches.
The same preflight/deferred DMA and atomic two-plane commit checks apply.
SRC_OVER uses`sa=As*coverage*255`, `da=Ad*(65025-As*coverage)` for straight
RGB and rounded alpha. The largest rounded RGB numerator fits32-bit unsigned.

ACK and successful107 continuation preserve the context; cold or unsuccessful
initialization does not establish it. A different completed profile, rejected
complete stream, late failure, reset or unrealize clears it. Native old-model
red and all13 transfer cases passing are in`work/runs/srcover-20261024/`,
including source/old alpha combinations, every packed byte, repeat, lifecycle,
mask/field/range/alias rejection and late cancellation. Original unchanged
execution completes through IRQ129 and its actual packed buffer contains the
warning. Independent full-frame and alpha-coverage checks pass. The subsequent
bounded run returns from worker4 to worker0 with no new graphics error; see
the conversion evidence for the remaining policy/readiness boundary.

The native tests in `tests/qtest/nikon/transfer.c` check data movement,
unchanged packet addresses, delayed completion, IRQ130, and cancellation. They
also submit synthetic firmware-shaped LUT packets for byte-exact output,
IRQ129 ACK, unknown/truncated/range/overlap/format rejection, destination
preservation and reset cancellation; no original firmware bytes are fixtures.
`tests/unit/nikon/transfer.c` additionally checks byte-exact crossing of a
short ROMD alias into adjacent RAM and ROMD, untouched destination tails,
whole-packet MMIO rejection without side effects, and loss of ROMD direct
access or destination writability before a timer batch. The boundary test
failed its byte comparison on the old implementation before passing with
section-bounded DMA. An additional empty-configuration test checks both
1214 values, retained readback, reset, unknown-bit/neighbor/table-start
rejections and absence of all three IRQs after time advances. It failed on
the old first word write before passing with the finite setup extension.
The stream-submission case additionally checks complete56/422-word synthetic
snapshots (transport and executor coverage, not original-byte fixtures),
independent ownership, caller-memory mutation isolation, MMIO preflight without
reads, unknown fields/counts, byte-exact functional LUT execution, IRQ129 ACK,
reset/unrealize cancellation and token invalidation. The
ROMD test also checks section-exact stream capture across both adjacent backing
types. The old frontend failed the initial10d4 word write before these tests
passed. These are synthetic device tests, not GUI boot evidence.

The corrected stream-submission test uses counts 28/211 and compares full
224/1688-byte snapshots. `work/research/z9reference/extent-red.log` records
the actual old-length failure (`56 != 224`), then `extent-green.log` passes
all four transfer unit cases. The added `stream-extent-boundaries` case checks
both banks, incomplete setup, zero/invalid starts, unsupported widths/read
directions/neighbors, the exact 128-KiB limit and over-limit counts, and a full
extent reaching adjacent MMIO without any MMIO read or IRQ. Reset/unrealize
cancellation and ownership isolation remain tested. The parent native receipt
`extent-nativecheck.log` passes 29 fixtures / 247 subtests; it is not GUI boot.
Two pre-executor parent original-bundle `extentfixed1/2` observations match the
full producer-bounded stream inputs but retain pending requests and no wait
return, IRQ129 or frame; see
[runtime evidence](../../runtime/coprocessor.md).

Remaining work is active, not accepted as a completed GUI: physical color
fidelity, programs/formats/geometry outside the finite LUT, clear, path and A8 profiles,
IRQ131, other status58 states, fences,
processing replies and final scanout remain unsupported. The selected graphics
destination is not presented as a screen. Console output must consume the
guest's actual scanout commit and layout.
