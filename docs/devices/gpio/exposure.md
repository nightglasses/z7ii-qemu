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

### Status-notification correction and downstream boundary

The later [queue-turnover receipt](/evidence/input/turnover.json) resolves the
producer side. The original status driver polls an active-low change input on
EX PC0. Its previous unwired low level repeatedly admitted identical status
acquisitions and BatMg event29 reports. The [status endpoint](../expower/protocol.md)
now retains an external change until its four-byte acquisition and generation-
matched release, through normal I2C transactions. A separate SPI-DMA diagnostic
observed 11 successful waits out of 12 and actual ring-index progress; no
completion event or queue slot is supplied by the host.

With the correction, a post-window original snapshot observes PC0 high, a valid
class1 sample, and all16 class2 slots empty. A93.94-host-second original run
continues MAIN processing from52 to156 completed frames between its46.75 and
92.81-second samples, with zero processing errors and zero panic length. This
establishes bounded continuous processing and removes the observed congestion;
it does not establish indefinite stability or complete exposure control.

A separate unpaused M run supplies a complete negative detent through raw
contacts held for one host second per phase. MAS cumulative dial0 becomes−1,
but Tv/Av remain312/24 throughout the bounded observation. A separate read-only
breakpoint diagnostic joins report byte11, its52-byte normalized copy, the
input-worker event and the actual cumulative-add instruction. These are
distinct observations; breakpoint timing is not used as ordinary-input
acceptance.

The downstream source chain is MAS VA `10434378` → `1038b80c` → `1038b68c`
→ `1038b528`: the parser signs the wheel byte, the receiver synchronously
copies52 bytes to VA `b1f7a720`, event1 admits the input worker, and the worker
adds the delta to VA `b1f7a7f8`. This addition precedes the application callback
and exposure policy. An unchanged Tv alone therefore cannot identify failed
transport. The input-engine initialization and pause gates are separately
observed open in the button run; current engine3 is the source-named
`GuiLvEv1`. Its input ring also retires normally. The independent application
exposure path is `1029ee74` → `102a59e8` → `10299e3e(7,0,0)` → `1029d28a`,
then the active mode's Tv/Av index producer. Cumulative counts alone do not
establish that this separate queue and permission path consumed a delta.

MENU, arrows and OK now use the ordinary expander contacts and reach the MAS
normalized switch bits during held-key observations. A separate MENU-only
read-only diagnostic observes the classified action tuple`{28,0,0}`, final
action28 and dispatch to the current state0 object's actual handler
`104e6c1c`. The menu permission path at `1055396c` accepts it, and its synchronous
coordinator returns. State0 and the initialized GUI worker are not missing.

The subsequent request is deferred in a separate read-only diagnostic: GUI
flag42 is zero at `105b64d2`. The call at `105b64c6`
retains request `0x20000` at VA `b181dabc`, rather than immediately switching
workers, and sets flag135 at `b202be3c`. Twenty-two observations at
`1053eab4` find no record73, including after the request is retained. Neither
the `04100012` receiver nor the worker-switch entry is observed in that bounded
window. Callback registration returns zero, while backend state byte
`b23f6a80` advances from2 at the earlier breakpoints to3 in the post-window
snapshot. This locates the retained request; it
does not yet establish which backend prerequisite prevents completion.

The normal `04100012` control-result message produces record73,
which admits the retained switch request. Immediate and deferred paths both
require the manager to receive the old GUI worker's exit notification before
starting the Menu worker and its tree constructor `1044eca8`. No such control
result or exit notification is supplied by the host. Those earlier sessions
did not show a menu screen, and a longer continuation reached a distinct
unsupported176/336-byte image program. Electrical delivery, accepted menu
request, worker handoff, drawing and setting changes remain separate acceptance
levels.

The same diagnostic separately delivers a sub-command dial delta to
`102a59e8`. The common permission check returns1 at `102a5a32`, and two calls
reach the Av setter `1029b9d0` with delta1. Neither index-selection return nor
property publication is observed. Thus this observation narrows the setting
failure to the Av setter's independent guards, rather than proving a common
input-manager rejection. It does not establish command-dial0 delivery or a
changed exposure setting. MAIN advances24→107 with zero graph errors in the
session, which includes debugger pauses and is not timing acceptance.

## Formatted media and natural setting changes

The later diagnostic identifies the Av setter's actual guard: property6405 at
MAS VA `b2551f50` is8, so bit3 causes `10299ef4` to reject adjustment through
`1029b014`. The other sampled guards, properties6424/6916/5772, are1/0/0.
The storage chain `104970e2` → `105077ea` → `10225b38` translates raw storage
flag`40000` into property6405 bit3. VA `105062de` sets that raw flag if either
storage object's state is2 or3; an absent slot's state0 alone does not set it.
State2 can follow filesystem/path check error`043a0088`, while state3 also
occurs during cancellation/unmount. These states must not all be labeled an
unformatted card.

A fresh normal8GiB MBR/FAT32 slot0 input removes that rejection. The board
continues to use its normal BlockBackend, NVMe queues, DMA and original guest
filesystem. No host folder serves guest filesystem operations. The next
source-produced512-byte [two-dimensional copy](../fill/protocol.md#source-produced-two-dimensional-copy)
is now executed as actual DMA, allowing the original consumer to continue.

In the subsequent unpaused original run, ordinary mode-position and dial
contacts settle in M/A/S/P and change actual settings:

| Operation | Original value before → after |
| --- | --- |
| M main dial | Tv517:312 →304 |
| M sub-command dial | Av518:24 →80 |
| A sub-command dial | Av518:80 →88 |
| S main dial | Tv517:304 →296 |
| P main dial | Program shift514:0 →−2; index20 →19 |

The M screen displays1/6400 andF3.2. MAIN completions rise26→139 between the
32.03 and82.85-host-second observations, with QMP running at every control
sample and post-window panic length0. The initial Av24 is outside the explicit
F2.8 lens range; normal permission/strategy selection can clamp or skip rows,
so these results do not imply one table index per detent.

The lens runtime retains relative aperture/target0 and no new aperture
commands during these changes. The actual request is an AE output, not a
direct property518 mirror: `10210886` publishes the rational from
`b2114fb8` through its output to `b25ef0a8`; `10233570` then passes it through
the iris packet to `10398206`. Its576/192 value remains Av3 and equals the
previous target, explaining why the later comparison sends no new command37.
The source includes range/qualification checks and a legal retain-old-value
branch, but this session does not identify which AE branch held the value.
The next discriminating producer is `10213280` and its exposure context
offsets11493/11784/11904/11676/11632, together with globals
`b185f634/b185f664`. Actual iris application and optical exposure therefore
remain unaccepted; a missing motor command alone does not prove a lens
transport fault. MENU naturally advances the old GUI worker
to internal4, Menu to1 and its backend to4, clearing the retained request.
It initially reaches a separate176/328-byte normal-image grammar boundary.
After that mode is implemented, an original repeat advances to a168/232-byte
path-state boundary, with138 MAIN completions and zero diagnostic append bytes.
Worker handoff is established; a rendered menu tree and navigation are not yet
established.

A separate session at the normal-image revision changes M settings but does
not settle a requested A transition by84.36 host seconds. QMP remains running,
no unsupported access is reported and the class2 queue is empty, but the
exception/watchdog diagnostic append buffer contains303181 bytes. Its text
and flags were not captured, so the cause is unresolved. The subsequent
successful mode transitions do not erase this stability failure. The
[curated receipt](/evidence/input/turnover.json) records both windows and the
source-established diagnostic-buffer semantics, separately from native checks.

After the source-backed path-state and scaled105 layout corrections, a fresh
121.40-host-second original window completes the full input sequence without
an unsupported access. It displays the Playback Menu and changes the row and
category highlights through ordinary inputs. Paired MAIN jobs increase19→242;
periodic graph-error samples are0, QMP remains running at the final query, and
post-window exception/watchdog flags and diagnostic append length are0.
The image below is original scanout, not a host-rendered menu:

![Original Playback Menu](/evidence/input/menu.png)

The runner's requested-action labels include intended submenu/return actions;
they are not evidence that those actions completed. The last screen still
shows the Playback Menu. Other categories, actual setting changes inside
menus and return to live view require separate acceptance. The earlier
mode-transition failure and unchanged iris application target remain open.
