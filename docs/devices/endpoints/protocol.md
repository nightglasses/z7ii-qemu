# Capture endpoint and global parameters

This finite device retains source-identified configuration at board PAs
`[2006a200,2006ad84)`. The interval contains the global packet at
`2006a200..2006a370` and ten endpoint slots beginning at `2006a400`, with
stride`100`. Only the words listed below accept aligned little-endian32-bit
accesses. The intervening holes and unimplemented status words return an
explicit transaction error; the interval is not a read-zero/discard aperture.

## Source and scope

The original MAS call chain `1023c998` configures these endpoint/global groups
through wrappers `1020c8b2..1020c8f0` and `1020c862..1020c89e`. Narrow original
source projections and the read-only pointer-table snapshot are retained in
`run sensorprofile/`. The latter covers MAS VA `b18069cc`;
the table for endpoint offset14 starts at `b18069d4`. Each table contains
ten pointers and the next table begins28 hex bytes later. The actual pointers
establish the base, stride and NULL entries, rather than an inferred broad
register bank.

The source was reached under an explicitly declared diagnostic module input:
resource80 version15 and structural bodies for other selected module records.
That continuation is separate from normal-startup acceptance, a normal menu,
or recovery of factory module parameters. No firmware bytes or guest software
state are patched to reach this device.

## Endpoint words

Offsets in this table are relative to each endpoint's base. Endpoint numbers
are0 through9; other indices are outside the contract.

| Offsets | Accepted bits | Original producer |
|---|---|---|
| `14` | Full32-bit | `1020b142`: unmasked word OR twice another word |
| `18`, `4c` | `ff` | `1020b198`, bytes at parameter offsets0/1 |
| `24`, `48` | `ff` | `1020b142`, two separate bytes |
| `3c` | `7` | `1020b16a` RMW bits2:1; `1020b232` RMW bit0 |
| `40`, `80` | Full32-bit | `1020b16a`, two word parameters |
| `44` | `ff` | `1020b16a`, byte parameter |
| `50`, `60` | Full32-bit | `1020b1da`, either selected parameter set |
| `54/58/5c`, `64/68/6c` | Full32-bit, only endpoints2/3/4/9 | `1020b1da`, non-NULL secondary pointers |

Offset14 is not limited to value3 or normalized boolean fields: the source
uses complete unmasked words, so high payload bits are retained. The4c
producer reads the byte at parameter+1; it does not add one to a byte value.
The6 sparse words are absent at endpoints0/1/5/6/7/8 because the original
table pointers are NULL, and accesses there remain errors.

Offset3c bit0 is an independent low bit, not an established enable or run
flag. `1023c57a` toggles a software-record byte, invokes `1020c8be` to
reconfigure, writes that low bit through `1020c8f0`, then calls
`1020bfaa(8)` at the existing capture-control command address `20068100`.
This sequence suggests parameter selection but does not prove a physical
bank identity. This parameter device retains all three bits independently
and performs no action on their transitions. The separate
[statistics coordinator](../statistics/protocol.md#endpoint-ring-completion-and-first-read-mode)
uses bit0 as an explicit finite address-set hypothesis and publishes a ring
notification only after successful owned DMA; configuration alone cannot
justify that event.

`1020b1b0` performs aligned32-bit MMIO reads at70/74/78/7c. It narrows
the first two results to bytes in its software output buffer and retains the
other two as words. No producer for those results was identified. They remain
errors for reads and writes;
configuration cannot manufacture them.

## Global packet

Offsets here are relative to PA `2006a000`, matching the original source.
Every aligned word from200 through370 has a direct configuration store.
Accepted masks bound software-produced bit positions, not physical register
legality or unobserved correlations among fields.

| Offsets or record form | Accepted bits | Producer |
|---|---|---|
| `200` | `ffff00ff` | `1020af18`, swapped halfword and byte |
| `204` | Full32-bit | `1020af18` |
| `208/20c/210/220/260/264` | `ffff` | `1020af18` |
| `214/218/21c`, `224..258` | `0fffffff` | `1020af18`, packed12+16 bits |
| `25c` | `00ffffff` bit union | `1020af18`, byte or shifted halfword with low1 |
| `268/26c/278` | `ffff` | `1020aea8` |
| `270/274` | `0fffffff` | `1020aea8` |
| `27c` | Full32-bit | `1020afb4` |
| `280` | `ff` | `1020afb4` |
| `284/294/298/29c/2a0/2a8/2ac/2b0/2b4` | `03ffffff` | `1020afb4`, packed10+16 bits |
| `288/290/2a4/2b8` | `ffff` | `1020afb4` |
| `28c` | `7fffffff` | `1020afb4`, packed15+16 bits |
| Four record bases `2bc/2d4/2ec/304`: base, base+4 | Full32-bit, `00ffffff` | `1020b056` |
| Four record bases `2c4/2dc/2f4/30c`: base/+4/+c, base+8 | `ffff`, `03ffffff` | `1020aed6` |
| `31c` | `ff` | `1020b094` |
| Three row bases `320+1c*r`: base | Full32-bit | `1020b094` |
| Same row: +4/+8/+10/+14 | `ffff` | `1020b094` |
| Same row: +c | `03ffffff` | `1020b094` |
| Same row: +18 | `07ffffff` for row1; `03ffffff` for rows0/2 | `1020b094`, shift11 versus10 |

For `1020af18`, a zero leading parameter writes204 and skips its other
stores. Parameter byte84 equal to1 writes25c as `(u16(parameter+86)<<8)|1`;
otherwise25c receives that byte. Only byte84 equal to0 writes260/264 from
the two separate halfwords at parameter+90/+88. The model's25c mask is the
union of those source-produced bit positions, without assigning physical
meaning to every possible combination. Likewise a disabled record in
`1020b056` writes only its first word. These are guest-side store decisions,
not hardware commands to erase the remaining retained words. Each accepted
MMIO store changes only that word; no hidden batch clearing is added.

The original global status helper `1020af08`/wrapper `1020c876` reads
`3e0/3e4/3e8/3ec`. The consumer `10238a62` selects one of these four words.
They are externally published read-only results from the complete-bundle
[statistics coordinator](../statistics/protocol.md). They remain errors before
publication or after reset. Other gaps from374 through3fc remain errors;
no configuration write supplies a result.

## State and event boundary

Known configuration reads return the last accepted word. Cold reset and
unrealize clear retained configuration under an explicit functional-zero
profile; physical reset values and universal physical readability are not
claimed. RMW at endpoint3c is directly observed. Unknown widths, alignment,
offsets and upper bits are rejected before mutation. There is no compatibility
layout or fallback for holes.

The device has no IRQ, timer, DMA or request output. Configuration
values are never interpreted as readiness or capture completion. Original
start/stop/commit traffic remains at the separate capture-control interface:
`1023c998` subsequently uses `1020c802` and `20068100/104`, while endpoint
switching uses `1020bfaa(8)`. Those consumers do not turn retained endpoint
or global words into new hardware events. The coordinator separately
snapshots selected commits, executes DMA and publishes scalar values;
staging/commit/stop do not erase the last successful scalar package.

The later [kind18 grid investigation](../../runtime/grid.md) connects the
original endpoint1 address sets at`2006a550/560` to PA`5d170000`, with stride
`12000` at`2006a580`, six allocated units and normal shared commit/run.
This establishes the configured output's ownership; it does not recover the
statistics arithmetic or add DMA/completion to this configuration device.
The separate board RAM's cold-zero convention is not endpoint-produced data.
The later [capture data audit](../../runtime/capturedata.md) adds the entire
source-declared neighboring pool family, traces queued results and corrects
two possible inferences: configured word48 is count-minus1 and word80 is
unit stride in this family, while global result selector0 does not identify
endpoint0 or establish a scalar value. Numerical production remains outside
this parameter/result sink. The coordinator's scalar arithmetic is an explicit
functional assumption, independent of the source-established selector ordering.

## Native validation scope

The new `/arm/nikon/sensor/capture/endpoints` fixture has a scoped old-model
red in `native-endpoints-red.log`: the initial valid `2006a414=80000003`
reads back0 because the old model has no endpoint mapping. This checks the
raw-word requirement rather than only the observed value3.

The complete endpoint fixture covers all10 slots, all known words, five data
patterns, NULL sparse entries, per-bit upper-mask rejection, all selector/low
bit combinations, independent endpoints, reset, invalid widths/alignment,
aperture boundaries, and absence of DMA, readout requests or capture status.
The separate global fixture covers all93 words with independent expected
mask tables and four patterns, checks the one widened row1 field, skips
implicit clearing when204 is zero, verifies every gap and status location,
and checks reset and absence of data/event effects. Both fixtures use HMP's
`address_space_read` result to assert real transaction errors at unknown
status locations; qtest readl's zero-filled error buffer is not treated as a
successful hardware result. The parent obtained separate endpoint and global
old-model reds, then the complete `nikon_sensor` batch passes all28 subtests
in7.48 seconds (`native-endpoints-green.log`).

## Diagnostic continuation

With unchanged firmware and the same incomplete version-declaration input,
`endpointsdiagnostic` crosses the former `2006a414` rejection. Its first
uncovered transaction is CPU0 PC`10209efe`, WRITE32 PA`2006be00=0`.
QMP reports debug/stopped and no display console yet. Original writer
`10209ef6` expands eight halfwords and two bytes into ten consecutive
configuration words, with the destination bank selected by its object+12.
The complete bank-selection contract is the next source boundary; this device
does not extend its aperture to that unrelated range. This diagnostic result
does not qualify the incomplete module body as normal GUI startup input.

The independent `endpointscounts` diagnostic changes four timing fields in
that synthetic input. Original arithmetic produces object+148=35190,
object+156=4096 and object+160=1000; the final timer packet independently
reads back4095 at MAS VA`b2b369c2` and35189 at`b2b369cc`, confirming the
minus1 encodings. It reaches the same `2006be00` frontier. These named
timing choices validate the arithmetic/encoding relation; the other module
tables remain incomplete. The exact changes and comparisons are retained in
`counts-input.json` and `endpointscounts-proof.json` with the run receipts.
