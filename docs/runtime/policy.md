# Worker0 mode and readiness policy

The current [healthy-battery continuation](guiupdate.md) is a separate run:
with cold F4 and BatCk1 it remains in worker0, without the warning-driven GUI
entry recorded below. Its policy still processes events. The following mode1
GUI result belongs to the earlier no-peer/BatCk2 execution.

The released-input run returns from warning worker4 to worker0 with main
mode5 and selector29. The follow-up now crosses that boundary using the
existing normal F4 input, held before initial EX sampling: original MAS selects
**main mode1**, starts **GUI worker3/mode1**, and now completes the finite
channel9 initial-count transaction through its original timer callback.
The new record5 fill now completes through its original callback and waiter.
Readiness stays unarmed in the bounded continuation. BatCk remains2;
this transition needs no battery
peer. No old-worker exit acknowledgement is pending. The paired pending3
and high-event-bit producer remains described in [display.md](display.md).

Code/data addresses `10xxxxxx/12xxxxxx/bxxxxxxx` below are C1.70 MAS VA;
`080xxxxx/200xxxxx` are EX VA. MMIO addresses are explicitly board PA.
The unchanged bundle and explicit
[functional descriptor](../inputs/nor/functional.md) are the same as the
completed [warning composition](conversion.md). Debugger probes are read-only;
input experiments use normal QMP key events. No guest software state is
injected, and no complete normal startup is established.

## The original producers of 1 and 6

Separate single-write-watchpoint runs observe initialization and later stores:

| Cell | Original initial value and source | Later original store |
|---|---|---|
| `b181a6d0` | 1, copied from ROM `123b4664` | `10630ab8(0)`, store `10630abc`, caller LR `10438b91` |
| `b181b3ec` | 6, copied from ROM `123b5380` | `10694f90(0)`, store `10694f94`, caller LR `1043882b` |

Both initial writes stop at PC `1002be9c`, immediately after the loader's
`STM` at `1002be98`. The copy begins at ROM `12399f94` and RAM `b1800000`.
The first cell is cleared by worker0 cleanup `10438aea` while transitioning
to warning mode15 (`r4=00200015`). The second is consumed by policy
`104387a6`: when the previous-state getter returns6 and `1062facc(6)` is
true, it publishes the saved state, or0 when that saved state is6. The live
store has main mode5 and selector29.

Consequently the final values0/0 do not mean those values were never produced.
They are not evidence that a missing asynchronous producer must restore1/6.
The previously identified readiness branch still has those numeric guards;
its reachability must be evaluated with the current mode and control flow.

## The current mode5 branch

`10694f36()` reads current main mode `b181b3d8`. Its setter
`106953e6(mode,a2)` writes mode, auxiliary word `b181b3e4` and change flag
`b181b3dc=1`; callers include `1048da74` through alias `1036089e`.

The joined original policy probe records mode5 at entry `10437d1c`, followed
by the mode5 condition at `10437e14` and return29 at `10437e6e`. State getters
`104bf49c(0/1)` return5/2. Thus the first branch's `a<2 || b==0 || b==1`
readiness rejection is false; these two entries are not both unready.
With `10630750()` false (`b181a6a8=0`), the branch consults warning selector
`10437b7a`; the joined probe at `10437ed0` directly observes its return29,
followed by policy return29. That selector tests bounded configuration/warning
conditions and otherwise returns29. Slot identities and the physical meaning
of main mode5 are not inferred from their numeric values.

This differs from the main-mode1/2/3 route into the later GUI/action selector.
Worker0 continues receiving events; supplying another periodic event does not
change the already observed main-mode selection.

## The record selecting mode5

An original application event **`02000002`** supplies this36-byte record:

```text
01000000 08000000 08001000 01010000 00000000 01000000
00000000 00000000 00000000
```

`1020ca7a` copies the record to app context+348. The case's branch at
`1020cbd6` uses shared call tail `1020d268`; it invokes `10247ae4` through
context+20. The live event is `b1f7fd28`, record `b1f7fd48`, application
context `b1818bd0`, callback context `b1819848`.

The following sequence is joined in original execution:

```text
02000002 record
  -> 10247ae4 -> 10247c62
  -> 102a6970(mainapp,3,normalized,0)
  -> 10204bda -> 1020dad2
  -> 1020d9e0(app,05020101,normalized)
  -> 1036089e(5,0,0) -> 1048da74 -> 106953e6
```

Record flags `word+4=8` satisfy `flags & 18(hex)`. `10247c62` maps record
byte20 values2/3 to normalized type1, and other values to type2. This run's
byte20 is1, so type2 is selected. `10247ae4` also sets normalized word+4=1;
normalized byte+8 is0. The other padding bytes are stack residue, not fields.
For event `05020101`, `1020d9e0` sees normalized type2, app context+349=0
and normalized word+4 nonzero, and therefore publishes main mode5.

## The EX system report behind the record

The producer is not an invented application record. The joined original run
captures EX helper `08033118`, called by system-report builder `08034034`
with LR `08034099`. Its input registers are `(80,8e81,0,40b)` (hex), matching
the four words at EX RAM `20013958`. Thus the helper's `r2` is the word at
`20013960`, and its **bit0 is clear**.

Original instructions `08033120..0803312a` encode that bit as raw system
report byte28: set bit produces2; clear bit follows `0803326e` and produces1.
The report buffer is EX RAM `200139fa`, length106. Captured immediately after
the helper returns at `08034098`, its entire106 bytes exactly match the
subsequent original MAS parser input. In particular raw byte15=1 and byte28=1.
This is a time-joined EX→MAS system-report observation, separate from the
112-byte BatCk battery report.

MAS dispatch slot1 uses parser `1043124c` with this input and length106.
Constructor `10431380` clears a36-byte record at `b25db614`, maps raw input
bytes12..25 to record flag bits0..13, and preserves raw byte28 when it is
1/2/3 as record byte20 (otherwise0). Thus raw byte15=1 produces flags bit3,
and raw byte28=1 produces the observed record byte20=1.

The remaining original transport/application chain is:

```text
EX word20013960 bit0=0
  -> 08033118: system106[28]=1
  -> MAS1043124c -> 10431380: record36[20]=1, flags bit3
  -> descriptor callback10206576: event03040002
  -> 102062dc: copy record to b2b3a300
  -> 1039fab2 -> 10204e30 -> 1020e374: event02000002
  -> normalized type2 -> main mode5 -> worker0 selector29
```

`102062dc` and `1039fab2` are separately observed callbacks of the generic
parser/dispatch path `102055e6`, with caller LR `1020562d`; this is not a
direct call from the former to the latter. `1020e374` copies36 bytes into
its68-byte application message and publishes event02000002. None of these
copy/forward layers alters record byte20.

## The external GPIO condition

Original copy/sampling instructions and independent access-watch runs close
the remaining producer chain:

```text
EX GPIOC.5 IDR bit5, active low
  -> 08038bbc four-input sampler -> 2001f334 bit0
  -> 08037ccc/08037d00 -> 20015b7c bit0
  -> snapshot20015a60+20 -> report state20013960 bit0
  -> system106[28] -> MAS record36[20] -> main mode
```

Sampler table `2001f214[0]` points to descriptor `2000df8c`: resource37,
EX GPIO MMIO base PA `40020800`, pin mask20(hex). Original ROM row `0805135c` compares
that GPIO's normalized read against0 and assigns output mask1. At sampler
return site `08039418`, the GPIOC IDR is3936(hex), so C5 reads high and the
returned sample mask is0. This is why the original source bit is0. The
additional OR branch in `08037ccc` requires byte `200159b0=1`; the observed
byte is0 and that branch does not supply a competing bit in this run.

`080393f8` performs sampling; `08038c8c`/`08038c64` return bytes
`2001f334/335`, combined into the scratch word at `20015b7c`. The original
copy path publishes the56-byte snapshot and then the system report. Access
watches include same-value zero stores, rather than inferring a missing
write from a change-only watchpoint.

The existing [input frontend](../devices/input/protocol.md) already connects
its pad3 to GPIO ID25(hex), C5. Its host binding is **F4**. Cold realization
publishes released/high levels. Thus the current mode5 input has a concrete
electrical source; changing a software gate or injecting a system report is
unnecessary. The original table does not name the physical camera control,
and this GPIO sampling is not a direct battery-authentication result.

In the late-input experiment, upstream QMP `input-send-event` supplies F4
down after initial mode5 selection. The frontend logs pad3/EX-pad25 level0.
At the end of the25-host-second window, actual GPIOC IDR is3916(hex), proving
C5 went low, but sampled byte `2001f334` remains0, both snapshot/report flags
remain0, and main mode remains5. No changed system report reaches the parser
breakpoint. EXTI IMR is000200c9(hex), with line5 disabled; no EXTI5 interrupt
can be inferred from this input. The next question is when original EX
resamples this level, including its held-input startup behavior. This negative
window does not prove permanent unreachability or a broken EXTI model.
A separate held-F4 plus normal QMP `system_reset` experiment likewise has
no changed report in its bounded window; it does not yet establish whether
the post-reset sampler ran. Neither result supports injecting a sample or
forcing a worker event.

Original resampling has a more precise synchronization condition:
`08038124` requires both bytes `200159ac==1` and `200159b1==1`. Only then
does `0803813a` publish mask1000(hex) to event object `20015a14` and clear
both bytes. Dispatcher `0803825c` uses table `0804e304`; its1000 row at
`0804e384` selects `08037ccc`, which invokes `080393f8` to sample GPIOs.
Late-input `key.json` directly observes **AC=1, B1=0**. The immediate false
condition is the second update flag, rather than delivery of the F4 level.

Identified producers include `080380a8` for the6-byte input path (setsAC and
calls the synchronization gate), and `080371a4` for the28-byte input path
(copies28 bytes and setsB1 at `080371b6`). Initialization path `08038420`
also setsB1 and calls the gate. A later source capture qualifies this branch:
`refresh-entry.json` observes `080383f8`, LR `08024d4f`, event04050024 and
**nine zero input bytes**. Its input[8]==0 branch directly setsB1 and calls
the synchronization gate. Only nonzero input[8] registers the28-byte callback
through `08024bf8` and queues command33. No such registration is observed in
the bounded baseline. Thus late B1=0 may be a consumed one-shot update; it
does not establish a missing28-byte battery reply. The next lead is who sends
this control request and whether another update or an earlier held GPIO level
is required for the intended startup mode.

## Receipts and limits

`work/runs/policy-20261025/` retains `power` and `state` watchpoint runs,
`policy` and `selector` entry/condition/return, `message`, `producer`, `record` and
`recordevent`, `recordsource`, `recordsend`, `recordinput`, `parser`,
`systeminput` and `systemsource` joined captures, plus `systemflag`,
`snapshotflag`, `inputflag`, `sampleflag`, `pin` and `key` input diagnostics.
`systemsource.json` records
the exact106-byte cross-domain equality. The first `parser` attempt tried to
read a non-pointer return register and failed; the accepted capture records
the returned descriptor pointer, not an application event ID. Each accepted probe holds one hardware
breakpoint/watchpoint at a time and deletes it before the next stage.
The earlier `recordevent` attempts using a reused register or a different
shared-tail predecessor failed; they do not support the event identity.
The accepted `recordevent.json` reads the saved original event pointer from
the function stack and joins it to the same normalization and mode store.
No guest memory, instruction, PC, event, task or function return was changed.
No maintained device behavior changed, so the preceding13-case native
transfer receipt retains its own scope; it was not rerun as a substitute for
these original-execution observations.

## Held input crosses the mode5 boundary

`coldkey.py` pipelines normal QMP `cont` and F4-down requests on one socket,
then attaches a read-only debugger. The attach capture is still before main
mode selection (`ffffffff`) and before the EX sample. The subsequent original
worker0 policy entry has main mode1 and EX sampled bit0=1. Later manager
state is current3/target5/mode1/state2; GUI class/mode is1/1 and the previous
worker0 has class4. BatCk is still2. This is an observed usable input timing
for this constructed board profile, not proof that every mode1 route requires
C5 or that F4 names the physical camera power control.

`coldrecord.json` joins the original application record and mode decision:
record flags remain8, record byte20 is **2**, normalized type is **1**, and
`106953e6` receives mode1. This closes the previously predicted normalization
route in the cold-input run. Its EX raw106-byte cache snapshot was zero, so
the earlier full EX106-byte equality receipt retains its released-input scope.

That checkpoint's next failure was MAS CPU0 at `1036e812`, WRITE32 PA `2c011004`
with `ffffcd37` (signed -13001), rejected by the preceding timing contract. Original
`10371704` is called with channel9, config `b1868834`, LR `102e7daf`;
channel object `b1806460` has byte+4=9. `1036e7d4` prepares a shared value
write followed by `2c011008 = 1<<9`, not a global enable bit. That model stopped
at the first write; the subsequent commit is established from original code,
not claimed as an accepted transaction.

The original ROM template at `124027c8` has flag1, zero configuration words
and callback `102eb749`. Runtime configurator `102e7ce4` changes the config's
word+12 to -1 and word+16 to `~13000=ffffcd37` before this initialization.
Thus the rejected value is generated by original code, not a recovered ROM
constant or a host input. That rejection required a separate finite shared
value/submit contract; accepting arbitrary writes or treating the word as enable
would have bypassed the boundary. GUI readiness and pending3 are still0;
no final scanout or GUI first frame is established.

The follow-up finite [timing contract](../devices/timing/protocol.md) now loads
that initial count for the stopped channel and lets normal gates, virtual time
and the original ISR deliver callback102eb748 with status r1=1. The joined
`work/runs/timingpreset-20261025/frontier.json` then observes fill engine0's
720x480 zero-fill request tobf309600, initially rejected at PC102dbba2,
WRITE32 PA20070010=1. The later [record5 extension](fill.md) now completes
that exact request through the original IRQ/callback/waiter. Its25-host-second
continuation has no new uncovered transaction, but manager remains3/5/1/2 and
gate/pending3 remain0. This is the current
[startup frontier](../validation/frontier.md); the precise next readiness
producer is not yet established. Original firmware bytes remain unchanged.
