# Source-bound image transactions

The existing dual-bank device implements guest RAM input, two ten-layer ports,
42 graphic objects, retained packed tables, Pixman composition and QEMU console
publication. It adds no overlapping MMIO device. Physical LCD/ISP identity and
an original firmware frame remain unverified.

Evidence is the saved ec048f native instruction/P-code index and the finite
`qemu-rtos-startup/work/display/{bank,completion,registers}/` graphs. They contain
249 complete bank entries and 7,778 unique instructions; the register graph
retains 121 original literal READ records across 59 owners, each with consuming
instruction, literal address, payload offset and original word. The complete
14-owner `operations.json` supplies the packed tables and object arrays. No
fresh native disassembly was used to implement these contracts.

## Register domains and packets

Offsets below are bank-relative. Actual PC-relative words distinguish unit0
at 20140000, main at 20141000 and ports at 20142000/20143000. The original
seven-word objects B1801250/B180126C specify register displacements 0/4000
and table displacements 0/10000. These initial values are not an observed
runtime receipt.

| Original consumer | Exact effect |
|---|---|
| C3C2/C4C0 | Unit0 config0200, packed geometry0204/0600, scalar0240/0250/0604 |
| C62C/C68A | Unit0 numeric config0400 and scalar0404/0408 |
| C6CA/C8EA | Unit0 packet0430..0478, omitting0454, and inverse getter |
| CA80/CBE4 | Main single-halfword1204, packed14-bit geometry1308, distinct timing/control1400 |
| C1DA/C296 | Coefficients at selector addresses04a0/04d0/1490/14f0 or port2840/3840 |
| D6E8 family | Port layout2200, dimensions2204, coordinate2208, four two-bit lane selectors2210 |
| D48A/D568 | Ten dimensions2400..2424; first primary2440, further primary2480..24a0; secondary24c0..24e4; byte strides2500..2524/2540..2564; positions2580..25a4; enable25c0 |
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

Main start snapshots the supported packet. It requires nonzero1308 geometry
and a supplied layer or enabled object. An accepted request produces busy3
and a virtual timer at a named nominal48MHz pixel service rate. This is not
measured EXPEED speed, sensor cadence or vblank. Pause/stop cancel the request.
No geometry, absent providers or a software ready3 write produces a frame.

Layers DMA-read their actual primary and secondary normal-RAM rows, preserving
padding. Whole extents reject zero, wrapping, ROM, MMIO and absent planes. A
16M-pixel budget limits host allocation and is not a camera hardware limit.
All reads and composition finish before console publication; a bad final
layer cannot publish an earlier partial frame. Failure and cancellation keep
the last completed surface.

Named virtual component profiles are layout0 lane-routed RGBA; layout1/4
RGB565; layout2 ARGB1555; layout3 ARGB4444; layout5 RGB565+A4; layout6 RGB565+A8.
Controlbit4 reverses byte order; controlbit28 selects layout0 alpha. Source
proves footprints and lane selectors, **not packed component identities**.
Pixman composes premultiplied layers in port/layer order. Actual enabled
object records then render clipped rectangles from their position and extent.
Their three bytes use a named virtual RGB order. The two six-bit fields remain
neutral retained values, not guessed alpha, rotation or format. These named
conventions support functional output and need later source/hardware refinement.

After actual composition the bank lazily creates a QEMU graphic console and
publishes the exact Pixman image. Initialization, disconnected sensor, RAM
allocation or elapsed time alone creates no frame. Refresh shows the last
completed transaction. Successful publication supplies named virtual bit16
through the original route/mask/W1C protocol. Original1046bc6c reads/ACKs
pending;1046bcb0 calls object+10;1046efee installs literal callback1046efa9;
1046efa8 translates rawbits16/20 and calls real guest event APIs. Host code
never writes guest flags, object metadata or runtime handler tables.

This implements guest-buffer/object-record→composition→console output. It
does not establish the original sensor join, physical scanout IP or an
original menu receipt. CMOS input remains separate until its source-bound
controller consumer is recovered. IPUD/sample services are not wired to pixels
by name.

## Native validation

`image.c` exports `nikon_image_add_tests(bios)` to the sole builder. Eight
no-TCG cases check exact PPM pixels and padded input; all seven layouts;
cancellation preserving the surface; a bad final layer preserving the whole
surface without IRQ; two ports/shared IRQ; independent register domains;
a bitmap-selected object; and every packed-table family, plane, bank and both
object displacements. They use the mature GIC fixture and valid virtual-clock
advances without `-S`. Synthetic tests do not prove a naturally submitted
original frame. Corrected compilation/execution await actual builder receipts.
