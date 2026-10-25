# Source-bound image transactions

The existing dual-bank device implements guest RAM input, two ten-layer ports,
42 graphic objects, retained packed tables, Pixman composition and QEMU console
publication. It adds no overlapping MMIO device. Physical LCD/ISP identity and
an original GUI menu receipt remain unverified.

Evidence is the saved ec048f native instruction/P-code index and the finite
`qemu-rtos-startup/run display/{bank,completion,registers}/` graphs. They contain
249 complete bank entries and 7,778 unique instructions; the register graph
retains 121 original literal READ records across 59 owners, each with consuming
instruction, literal address, payload offset and original word. The complete
14-owner `operations.json` supplies the packed tables and object arrays. No
fresh native disassembly was used to implement these contracts.

## Register domains and packets

Offsets below are bank-relative. Actual PC-relative words distinguish unit0
at 20140000, main at 20141000 and ports at 20142000/20143000. The original
seven-word objects B1801250/B180126C specify register displacements 0/4000
and table displacements 0/10000. The original runtime capture independently
confirmed those fields, installed callback1046efa9 and event handles70/6f;
this object receipt does not establish a submitted frame.

| Original consumer | Exact effect |
|---|---|
| C3C2/C4C0 | Unit0 config0200, packed geometry0204/0600, scalar0240/0250/0604 |
| C62C/C68A | Unit0 numeric config0400 and scalar0404/0408 |
| C6CA/C8EA | Unit0 packet0430..0478, omitting0454, and inverse getter |
| CA80/CBE4 | Main single-halfword1204, packed14-bit timing1308, visible height1328/width132c, distinct timing/control1400 |
| C1DA/C296 | Coefficients at selector addresses04a0/04d0/1490/14f0 or port2840/3840 |
| D6E8 family | Port layout2200, dimensions2204, coordinate2208, four two-bit lane selectors2210 |
| D48A/D568 | Ten dimensions2400..2424; first primary2440, further primary2480..24a0; secondary24c0..24e4; byte strides2500..2524/2540..2564; positions2580..25a4; enable25c0 |
| D6D0 | Per-port overlay update2604/3604: read, write1 unless the complete value is3 |
| Second port | The same complete arrays plus1000 |
| 104d94b6/104dda26 | Layouts0..6 from formats11..17; layout0 uses four primary bytes, others two; layouts5/6 have a separate plane |
| CFB6/D060 and104dd230 | Forty-two records: position1800, extent1900, two retained six-bit fields1a00, three color bytes1b00 |
| D0D2/D060 | 1c00/1c04 are the42-entry enable bitmap, **not output pointers**; 1c08 selects an entry |
| BF0C/BF58/BFAA/BFFA | Selector8 accesses main1200: start1/11, pause2, stop0, busy3; other selectors have independent sub-unit commands |

Abbreviated consumers are in the1046xxxx function family. Unit0 config0200
cannot start a main transaction; main1204 is not two dimensions; main1400 is
not an encoding enum. Layer0 uses the separately supplied2440 primary pointer,
while other layers use the parallel pointer array. The model preserves this
original asymmetry.

The two direct D6D0 callers commit dirty configuration for at most ten layers,
clear each layer dirty flag, request the per-port update and then clear the
port dirty flag. The finite model retains only the observed guest command bit0
with a cold-zero reset. It does not manufacture value3, an event, an interrupt
or completion. No firmware producer of values2/3 was found; the physical
meaning of bit1 remains outside this contract. The fixed WonderLantern Z7
reference independently places a retained two-bit word at the same port
offset, corroborating the register family but not expanding the Z7II guest
write set.

D060 proves bitmap semantics by testing1c00 for indices below32 and1c04 for
the remaining entries. D0D2 alone merely writes two arbitrary packet words
and cannot establish a pointer. The model performs no invented output DMA.

## Packed-table apertures

Existing SysBus indices0..4 retain banks and B000/D000/E000 globals. New
indices5..8 expose the following unit0 ranges; indices9..12 expose the same
ranges plus10000 for the second object's original table displacement. Machine
glue owns these mappings.

| Index | PA and size | Original operation |
|---|---|---|
| 5 | 24040000,200 | DAAE/DB82: two banks of three32-entry12-bit planes |
| 6 | 24042000,200 | D8FA/D9EC: the same packed-plane extent |
| 7 | 24045000,400 | DC44/DD72: two banks of three33-entry8-bit planes |
| 8 | 24046000,200 | DE54/DEC2 and DF28/DF9A: two banks of two single33-entry tables |

Access is aligned32-bit. Two entries occupy bits0..11/16..27 or bits0..7/16..23.
Three-plane8-bit tails contain one low byte. Single-table tails preserve the
original LDRH value including its upper byte. Holes and bits outside each
exact packed mask are rejected. Writes retain actual values and generate no
event or guessed shader transform. E004 requests banks with bits2/10 while
E0BA observes effective banks with bits16/24. BFFA and each complete table
setter guard access to the effective bank while the corresponding engine
reports status3. This supports a double-buffer interpretation. The named
functional timing law commits enabled requested banks only at successful
frame publication, preserving effective state on writes, cancellation and
fault. Sub-unit start requests become status3 only after a supplied frame has
completed; initialization alone cannot report a running frame producer.

## Functional image laws

Main start requires nonzero visible width132c/height1328 within the allocation
budget and arms status1. Each virtual frame
boundary snapshots the live supported packet and validates every enabled
provider. A transaction with no enabled layer completes status and IRQ but
creates neither a console nor host pixels. This is the bounded command100
sequence: original setters write enable25c0=0 and main start1200=11, while the
command cannot return to issue its later object-enable command until the main
completion path runs. Invalid enabled input still fails without completion.

A transaction with at least one enabled source reads actual providers and
publishes only a complete image. Successful publication reports active3;
scanout continues until pause/stop. This follows the original live setter
chains DD988A→DD354→D6E8→95EC→DD2A8→D48A/C354 and D942A→DD20E→D0D2,
which update layers/bitmaps without another BF0C master start. The boundary
and timer use a named nominal48MHz service rate over the visible pixels. This is not measured
EXPEED speed, sensor cadence or vblank. An armed device may await real input;
absent providers and software ready3 writes cannot produce a frame. Invalid
live input preserves the previous complete surface while later updates can
recover without a restart.

Each started port composes its layers in the pre-scale2204/3204 canvas, then
places the scaled canvas at2208/3208. Original D9848/D988A/D8BA serialize
horizontal a/b and vertical e/f at2814/2818 (second port plus1000), with
enable2810 and a retained five-bit phase. DD354 forms the even pre-scale
extent from viewport extent multiplied by b/a and f/e; source layer positions
are already relative to this pre-scale canvas. The model consequently scales
the canvas by a/b and e/f using mature Pixman's inverse fixed-point transform.
Disabled ratios mean unity; enabled zero ratios reject the complete frame.
Nearest sampling is an explicit functional convention. The original phase is
retained but its physical filter interpretation is unverified; it does not
justify shifting already cropped RAM pointers or inventing an output format.

Horizontal viewport origin uses the original signed16 coordinate domain.
D96D4 loads vx0 with LDRSH and bounds vx1 rather than vx0; DD354/D95EC form
the relative layer position from signed D.x−V.x. A valid second-unit example
is V=(-2,0,8,8), D=(0,0,8,8): the pre-scale canvas is10x8, with its8x8 layer
at x2. Treating encoded origin FFFE as65534 incorrectly hides that layer.
The model passes the signed destination to Pixman, which clips it and advances
the source through the same inverse scale. The primary RAM pointer remains
the already adjusted cropped pointer and keeps its full original row stride.
The vertical14-bit coordinate retains its prior interpretation: D96D4's
unsigned initial vy0 bound rejects negative values, so this change does not
invent a negative-Y hardware encoding.

Layers DMA-read their actual primary and secondary normal-RAM rows, preserving
padding. Whole extents reject zero, wrapping, ROM, MMIO and absent planes. A
16M-pixel budget limits host allocation and is not a camera hardware limit.
All reads and composition finish before console publication; a bad final
layer cannot publish an earlier partial frame. Failure and cancellation keep
the last completed surface.

Named virtual component profiles are layout0 lane-routed RGBA; layout1/4
RGB565; layout2 ARGB1555; layout3 ARGB4444; layout5 RGB565+A4; and the older
layout6 RGB565+A8 convention outside the bounded YUVA profile below.
Controlbit4 reverses byte order; controlbit28 selects layout0 alpha. Source
proves footprints and lane selectors, **not packed component identities**.
Pixman composes premultiplied layers in port/layer order. Actual enabled
object records then render clipped rectangles from their position and extent.
Their three bytes use a named virtual RGB order. The two six-bit fields remain
neutral retained values, not guessed alpha, rotation or format. These named
conventions support functional output and need later source/hardware refinement.

## Format17 YUVA provider

The unchanged original run in `run readinessnormal-20261023/banks.json`
submits unit1 port0 with control `00030006`, lanes `02000301`, a 1024x682
layer, primary PA `cb4a7000`, secondary PA `cb5fc000`, and byte strides
2048/1024. The same run's `banks-head1.ppm` contains the old decoder's stripes.
These PAs are the earlier [transfer107 output](../transfer/clearmode.md),
whose finite producer writes `[Y0,Y1,U,V]` per horizontal pair and separate
per-pixel A8. Interpreting each two-byte primary word as RGB565 discards that
pair structure and treats chroma bytes as unrelated RGB pixels.

The saved original MAS source projections in that directory separate facts:
`104dd354` maps software format17 to layout6; `104d94b6` and `104dda26`
compute two primary bytes and one secondary byte per pixel, preserve their
independent strides, and adjust cropped plane pointers. `1046d6e8` serializes
the layout, control fields and four two-bit lane selectors into the port
packet. The source and original packet identify a producer/consumer profile;
they do not establish physical identities for every control flag, a generic
layout6 format, or another lane permutation.

The finite model consequently decodes only control `00030006` with lanes
`02000301` as paired YYUV plus A8. Each pair shares U/V but has independent
Y and alpha. Limited-range integer BT.601 conversion, clipping to RGB8,
and alpha premultiplication use the same functional color convention as
the existing transfer107 inverse; they are not measured hardware colorimetry
or bit-exact GPU precision. Pixman retains the existing OVER composition.
Two-pixel width, a four-byte primary pointer and a four-byte primary stride
are conservative complete-pair boundaries. Unknown controls in the layout6
bit16/17 family, different lanes, odd widths and incomplete pair alignment
reject the whole frame instead of silently selecting RGB565. The earlier
layout6 profile without those bits and the other layouts remain unchanged.

This uses the existing unit/port/layer arrays, independent plane addresses,
normal-RAM row validation and DMA, with no resource-address whitelist or
invented output DMA. All enabled layers finish before publication; a bad
later provider preserves the previous surface and supplies no new completion.
Live updates, cancellation, reset and cadence retain their existing contract.
At this checkpoint the decoder preserved the old interpretation of1308 as
780x780 output. The following source analysis separates it from the original
visible-area fields; a layer extent alone is not the output-size authority.

## Main visible area and timing

`10448f92` calls `10448e20` with a rear/EVF selection derived from the original
request; the rear branch calls `10448d12(packet,0)` before `1046ca80` at
`10448efa`. That producer writes packet+22/+24 as780/780, packet+32 as1155,
and packet+46/+48 as682/1024. The setter serializes these distinct fields to
main1308,130c,1328 and132c respectively. Its inverse `1046cbe4` reconstructs
the same offsets. The visible fields are unsigned halfwords loaded without a
14-bit mask, unlike1308; their accepted write masks are therefore `ffff`.
The fixed rear producer does not read resource24/25/27 to choose these values.
Those resources and their qualified input bodies remain unchanged.

An independent consumer, `104dae5a -> 104dd5ae -> 1046ca80`, copies one of
18 fixed56-byte ROM profiles from MAS VA `1079ed90`. Their1308 pairs include
525/525 with1328/132c=480/720;625/625 with576/720;750/750 with720/1280;
1125/1125 with1080/1920; and2250/2250 with2160/3840. Two rows instead have
562/563 while still declaring1080/1920. This rules out interpreting1308's
two fields as horizontal and vertical visible dimensions. Its role as a
pair of vertical timing counts is a strong inference; exact physical field,
blanking and cadence semantics remain unverified.130c carries a separate
timing scalar, including1155 in the fixed rear profile.

The normal20-host-second original run's read-only `geometry.json` in
`run moduleinput-20261024/` observes unit1 main1308=`030c030c`,
130c=`483`,1310/1314=`3`,1318=`707`,131c=`60`,1320/1324=`0`,
1328=`2aa` and132c=`400`. Together with the setter/producer and the distinct
port viewport, this supports the finite rule: compose the output at the
explicit132c width and1328 height, while retaining1308 and the other timing
words separately. It neither guesses a maximum layer extent nor substitutes
1024 for a known address or timing value.

All console allocation, start validation and nominal pixel-service budgeting
use the explicit visible extent. Missing, zero or over-budget visible fields
cannot publish or complete. Live visible changes retain the existing frame
boundary, clip the independently sized port canvas through Pixman, and publish
only after every enabled provider succeeds. Unknown upper bits reject before
changing the retained visible field. Reset clears both fields; pause cancels
pending publication. There is no fallback to the former1308 layout. The
48MHz service law remains a functional convention and does not derive a
physical refresh rate from the recovered timing counts.

After actual composition the bank lazily creates a QEMU graphic console and
publishes the exact Pixman image. An empty transaction supplies completion but
does not create or refresh a console. Initialization, disconnected sensor, RAM
allocation or elapsed time alone creates no frame. Refresh shows the last
completed image transaction. Successful main completion supplies paired raw
cause bit1 and semantic bit16 through the original route/mask/W1C protocol.
The observed `0011f1f1` mask inhibits bit16 but not bit1, so the cause reaches
IRQ111; original1046bc6c reads/ACKs the complete pending word,1046bcb0 calls
object+10,1046efee installs literal callback1046efa9, and1046efa8 translates
rawbits16/20 and calls real guest event APIs. Host code
never writes guest flags, object metadata or runtime handler tables.

The board names this QEMU device `display`. Consumers use QMP `screendump`
with `device: "display"` and `head: 0` or `1`, rather than assuming console
index0. Mature QEMU preserves console numbering after display initialization;
an existing monitor text console can retain index0 when this lazy graphical
console is first created. The explicit endpoint selects the actual completed
bank surface without creating an early placeholder or changing its dimensions.
These fixtures use `-nodefaults`, retaining libqtest's explicit QMP socket.
Otherwise an unused default parallel VC creates a fixed text console even
when `-serial none` was requested. Upstream device-based console lookup visits
that text object and tries to read its absent `device` property. Suppressing
unused default devices avoids this upstream limitation through the supported
launch contract; the camera display model and pixel assertions are unchanged.

This implements guest-buffer/object-record→composition→console output. It
does not establish the original sensor join, physical scanout IP or an
original menu receipt. CMOS input remains separate until its source-bound
controller consumer is recovered. IPUD/sample services are not wired to pixels
by name.

## Native validation

`image.c` exports `nikon_image_add_tests(bios)` to the sole builder. Existing
no-TCG cases check exact PPM pixels and padded input; all seven layouts;
cancellation preserving the surface; a bad final layer preserving the whole
surface without IRQ; two ports/shared IRQ; independent register domains;
a bitmap-selected object; and every packed-table family, plane, bank and both
object displacements. The additional refresh case checks both scaling axes,
padded input, a live buffer change without master restart, invalid live ratios
preserving the last surface, recovery and pause cancellation. They use the
mature GIC fixture and valid virtual-clock
advances without `-S`. Synthetic tests do not prove a naturally submitted
original frame.

The crop case uses the second object's native8-byte alignment, a supplied
RAM crop pointer with48-byte row stride, signed leading clipping, trailing
clipping after a live origin change, and actual QMP head1 pixels. The existing
ports case also checks actual second-head pixels rather than inferring output
from its IRQ alone.

Baseline sole-builder receipt: all eight preceding image cases passed in3.83 seconds,
with the exact RGB,8×8 extent, object, cancellation, fault and packed-table
assertions retained. Tested device source is `ef0ba27`; the named board endpoint
is `43ea19b`; fixture `4c1db31` selects the device/head and suppresses unused
defaults. The preserved log is
`native-qemu-source/run validation/gui/imagenativefinal.log`.
The earlier default-console failure and upstream text-property abort are
closed by the supported fixture launch contract. No naturally submitted
original frame or camera measurement follows from this native receipt.
The sole builder independently compiled the continuous/affine extension from
`d31386c` and observed all nine cases passing, including refresh, in
`qemu-submodule/run scanoutfinal.log`; that receipt is separate from the prior
eight-case baseline. The sole builder compiled the signed-origin change;
focused crop/ports/refresh checks exited0 in1.11/1.10/1.11s, respectively,
recorded in `run displaycropreceipt.log`, `run displayportsreceipt.log` and
`run displayrefreshreceipt.log`. Processing's unchanged exact-byte,
EOF and cancellation receipts are not rerun for this display change.

The YUVA fixtures add independent tabulated black/white/red/green/blue
vectors with unequal Ys and alpha0/64/128/255, multiple pairs and rows with
padding, both units and ports and layers0/1/4/9, actual head pixels, live
independent plane changes, pause/reset cancellation, and a full 1024x682
frame in independently chosen RAM. Unknown lanes/control, odd width,
unaligned pair pointer/stride, short color/alpha stride and MMIO planes
must preserve the preceding complete surface without completion, followed
by recovery. These synthetic fixtures do not themselves establish an
additional original menu or physical scanout receipt.

Parent native red (`native-yuva-red.log`) exercised the new fixture against
the old decoder and failed its pixel assertion, 32 versus64. The complete
image batch then passed all14 subtests in4.41s (`native-image-green.log`).
Both receipts are in `run readinessnormal-20261023/`.

The separate unchanged-firmware `color` run in that directory used the same
declared inputs, healthy UART peer and20 host seconds without an in-transaction
debugger pause. QMP published `display` head1 at780x780; head0 had no console.
`color-head1.ppm` shows guest icons and the error dialog with corrected colors.
This establishes an original error-GUI frame under the finite input/device
profiles, not a normal menu, complete boot or physical-camera color receipt.
No firmware bytes, guest readiness fields or guest pixels were injected.

The visible-area fixture programs the source-shaped1308=`030c030c` alongside
1328=682/132c=1024, and compares every output pixel, including columns780..1023.
All earlier image fixtures now supply their explicit visible fields. The
new case also checks a live512x342 output clipping a1024x682 port canvas,
high-bit write rejection, missing/zero/over-budget extents preserving the
complete prior surface without IRQ, recovery, pause and reset. The parent's
old-model `native-visible-red.log` in `run moduleinput-20261024/`
rejects the expected1024x682 PPM length2095120 against the old780x780 length
1825215. That synthetic red is separate from the original register snapshot
and from any subsequent corrected original-run frame.

The parent whole-image batch passes all15 subtests in4.61s
(`moduleinput-20261024/native-image-green.log`). The separate `visible`
unchanged-firmware run retains the same normal inputs and20 host seconds:
QMP `display` head1 now publishes1024x682, with the complete guest error
dialog and right-side icons in `visible-head1.ppm`. It adds no descriptor32
input and retains the resource80 error. This verifies the corrected visible
extent, not normal-menu startup or a physical refresh-rate measurement.
The shared sensor-to-display fixture also programs its explicit8x8 visible
fields; `/arm/nikon/sensor/display-buffer` passes in0.79s in the separate
`nikon_sensor` executable (`native-display-buffer-green.log`).
