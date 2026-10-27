# Exposure controls and acceptance

The normal body-input path selects P/S/A/M and delivers quadrature detents to
the original EX/MAS firmware. A changed mode label or received-detent counter
does not establish exposure control. Acceptance requires the original setting
producer to change the intended value, followed separately by any applicable
lens command. No host write sets these guest values.

Addresses below are C1.70 MAS virtual addresses; property identifiers, indices,
units and table lengths are decimal. Register-style addresses are hexadecimal.

## Source-established setting units

Property517 at `b25755f0` is shutter Tv. VA `10766be4` sets that property through
`103e37dc` and identifies the operation as `SetTvByCameraLsb`. Property518 at
`b25755f4` is aperture Av; VA `107667e0` identifies its setter as
`SetAvByCameraLsb`. The two `CApex_ConvApexValue` paths at `107666c0` and
`10766b44` normalize through `103a2814` with denominator24. Therefore:

```text
Tv = signed_property517 / 24
ideal exposure seconds = 2^(-Tv)
Av = signed_property518 / 24
F number = 2^(Av / 2)
```

For example, Tv312 is13EV, ideally1/8192 second (the usual display label is
1/8000). Av72 is3EV, approximatelyF2.8; Av96 is4EV, F4. These are decoded
settings, not proof of optical integration or scene-brightness simulation.

The first122-row table at `b18122fc`, corresponding to ROM `123ac290`, uses
16-byte rows. Normal indices13..121 give `Tv=(index-43)/6`. Its first13 rows
select special shutter choices; indices3..12 carry explicit long-exposure
seconds and must not use the exponential formula. The second91-row table at
`b1812a9c`, ROM `123aca30`, gives `Av=(index-12)/6`. Each row's final word is a
dynamic permission flag: one detent may skip rows, so index±1 is not required.

## Mode and dial policy

Normal mode positions0/1/2/3 select P/S/A/M. In the corresponding manual-dial
contexts, property5667 is29/30/31/32. With the default reverse/swap policies
4455/4456/4372 equal to zero, the source admits:

| Position | Context | Dial0 | Dial1 |
| --- | --- | --- | --- |
| P /0 | 29 | Program shift514 | Automatic aperture policy |
| S /1 | 30 | Tv517 | Automatic aperture policy |
| A /2 | 31 | Automatic shutter policy | Av518 |
| M /3 | 32 | Tv517 | Av518 |

The first dial's default positive direction raises Tv. MAS negates the second
route, so its positive direction lowers Av. Policy4455 reverses direction;
4456 can swap routes, including a distinct A-only option. Endpoint settings
and dynamic permission flags can legitimately leave a detent unchanged.

P's program-shift table is separate: ROM `123acfe0` contains41 choices in
sixth-EV units. VA `103371b6` publishes the result as property5753 with rational
denominator6. P acceptance should observe514 and the program-line result,
not require the manual Tv/Av caches to change together.

## Lens target is a separate consumer

The aperture dispatch cache at `b23ed5bc` has a state byte and an absolute-Av
rational at byte8. Its denominator is signed16 at the pair's start; its numerator
is signed32 at byte4. The remaining halfword is not part of the denominator.
The last submitted target at `b1868448` has the same representation, followed
by a reference pair. A requested Av can be bounded by the selected lens range
before the original strategy decides whether to issue an iris command.

For the explicit F2.8 virtual lens, absoluteAv3 corresponds to relative
position0; absoluteAv4 corresponds to192 in the lens's1/192EV units. A property
change alone does not prove that target was submitted, completed or used to
expose pixels. Observe the normal request mask, UART command, lens runtime and
actual target separately. The [lens contract](../lens/protocol.md) defines
finite travel and reporting; optical scene exposure remains an independent
acquisition capability.

## Bounded original-input diagnosis

The [full-resolution execution receipt](/evidence/color/capture.json) separates
the successful AUTO scene test from the ongoing M input diagnosis. A normal M
session settles at property context 32, but one queued negative dial0 detent
does not change the observed MAS cumulative count or Tv setting over about
45.6 host seconds. Holding each raw contact phase for one host second also
fails to deliver a count in a separate session; increasing the dwell alone is
therefore not a justified model correction.

A further independent stock GPIO trace observes both complete negative sequences
on EX GPIOF IDR, masked by `0x4800`: `4800 → 4000 → 0000 → 0800 → 4800`.
The three intermediate intervals contain respectively 12, 18 and 6 IDR reads
for the queued input, and 247, 634 and 517 reads for the slower raw contacts.
Those reads do not identify the caller or selected mask. A separate read-only
hardware breakpoint at EX VA `0803e3b0` reaches the original negative count-store
instruction with new state 2 and signed value −1. The raw counter is sampled
before the store, so this establishes decoding and the intended write, not
successful downstream delivery.

The source chain continues through EX VA `0803df98`, which snapshots and clears
the raw counts at EX SRAM `200186d1..d2` into `200186a3..a4`. The registered
callback at `20018454` normally points to VA `0803d291`; it returns early if
byte `2001148d` is 1. That stop byte is 0 in the separate trace run's final
snapshot. The callback aggregates through VA `0803d710` into `200114ed..ee`
and sends the normal input report. The exact source condition at VA `0803d296`
is `BEQ 0803d2d2` after comparing that byte to 1. Older reference-map labels
reversed this condition; they are corrected alongside this observation.

A subsequent independent breakpoint chain captures the same negative input
through snapshot bytes `ff 00` and VA `0803d766`'s aggregate result −1. The
outgoing report contains dial0 byte `ff`, but VA `0803d68a` observes submission
return `ffff`. The normal send guards are all satisfied: words `2001166c`,
`20011670`, `20011678` and `20011688` are 1, byte `2001168c` is 13, and token
`2000e738` is idle `ffff`. The negative MAS normalization breakpoint does not
hit during this bounded instrumented window.

Source VA `08032000` selects class2's write index at `2001176c` and slot
`200117e4 + 4 * index`. At `08032092`, a nonzero selected slot branches to the
unlock/return path, retaining default `ffff` without enqueueing or notifying
the consumer. `ff00` instead means the descriptor channel is outside the
registered count; `ff01` is an invalid type. The source's type1 queue has
priority over type2.

The next independent queue probe observes type2 read/write indices both 6,
with selected slot `200117fc` occupied by BatMg descriptor `20015f18`:
channel 4, type 2, length 8, body at `20015e66` beginning with status kind
`0a`. Type1 is empty. This confirms the occupied-slot explanation for that
input submission. A later post-window snapshot has type2 read/write indices
5/4, a registered provider, stop = 0 and active = 1. The consumer has made
progress; the observations establish congestion, not permanent stoppage.

The original McuTx service starts SPI-DMA and at VA `08034b0a` AND-waits for
both completion bits 4 and 8, with timeout 10 ticks. The later snapshot has
not observed that transaction's bit8 callback; without the matching HAL state
and wait result, it does not identify a missing hardware completion. The next
discriminating evidence is the wait return at `08034b0e`, returned flags and
the actual SPI HAL record at SRAM `20017eac`, together with normal producer
rates. Final zero input counters alone cannot distinguish failed submission
from later normal consumption and clearing. No input cadence, IRQ, queue slot
or exposure value is forced to bypass this missing evidence.
