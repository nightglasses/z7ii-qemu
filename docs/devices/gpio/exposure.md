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
