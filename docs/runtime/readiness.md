# GUI readiness gate

This page records the earlier cold-F4 execution without the healthy battery
peer. With the complete peer, BatCk becomes1 but the GUI stays idle; the
current [ordinary GUI investigation](guiupdate.md) joins the battery-warning
trigger and follows the unissued ordinary request. These are distinct execution
paths.

The cold-F4 mode1 continuation now reaches the original readiness condition
at MAS VA `1054045e` with return value0 from gate getter `10630d12`.
The branch skips the pending3/high-event publication. It is not blocked in
the earlier conditional record51 wait. No guest gate, event or task state was
changed to obtain this result.

All code and data addresses here are C1.70 MAS CPU VAs. The primary source is
the unchanged original MAS payload mapped at `10020000`, retained under
`work/runs/descriptor17-z7cross-mac1/`. Bounded source receipts and the
parent-owned read-only runtime probes are under `work/runs/readiness-20261026/`.
This is a control-flow result, not a GUI first-frame or complete-startup claim.

## Direct original execution

The preceding `work/runs/reproducible-20261026/mainline.json` records main
mode1, manager current3/target5/mode1/state2 and GUI class1/mode1 after the
record5 fill callback `10389dc2` and successful waiter return `10389e2e=1`.
Gate `b181a6ec` and pending3 `b201bf6c` remain0 in its bounded continuation.
The final CPU PCs are snapshots; they do not identify a blocked GUI task.

The separate `entry.json` probe enters `1053d732` from mode entry
`1053fe10`, with LR `10540431`. State word `b181b518` is0. It then reaches
the readiness condition at `1054045e`, with R0=0, gate0 and pending3=0.
Thus the helper's state5-only wait is excluded for this observed entry.
GUI class1 alone would not establish this: mode entry selects class1 at
`1053fe54`, before its remaining calls.

The separate first-policy `policy.json` probe records:

| Field at `10437d1c` | Value |
|---|---|
| Packet pointer | `b181d938` |
| Packet word0, event flags | `00010208` |
| Packet word1, decoded selector | 0 |
| Packet byte+8, selected action | 29 |
| Power word `b181a6d0` | 1 |
| Previous-state word `b181b3ec` | 6 |
| Configuration byte `b20d29b6`, index290 | 0 |
| Readiness gate before/after policy | 0/0 |

The separate `sequence.json` records three mode1 policy calls:

| Call | Flags | Selector | Previous state | Power | Return |
|---|---|---|---|---|---|
| 1 | `00010208` | 0 | 6 | 1 | 29 |
| 2 | `4020201e` | 1 | 6 | 1 | 29 |
| 3 | `40000000` | 0 | 0 | 1 | 8 |

Gate and configuration290 remain0 throughout these calls. The second packet
does supply flags bit1, but selector1 differs from the gate's selector34.
Return values alone do not establish the exact executed return branch.

The separate `guard.json` resolves call2. Execution reaches `104382e4`
with R10=1, power1 and previous state6. The comparison against34 fails at
`104382e8`, so the configuration290 getter and gate setter are bypassed.
The byte's observed0 is therefore not the executed cause of this rejection.
The same probe reaches return29 boundary `10437e60` with R1=1772 (`6ec`)
and byte `b181a630=1`: this is the final worker-state arm, after the
selector-specific branch. It is not an earlier return before the gate guards.
In `tail.json`, all three terminal bytes (`b23f6c35`, `b23f6ced`,
`b23f6ca5`) and transition word `b181a708` are0. The three state tests
are therefore false and raw byte `b23f6ca5` differs from2, selecting that
terminal return29. The separate first-policy reason capture in
`completion.json` is `6ac`, with byte `b181a630=0`: call1 returns29 at
the earlier readiness-byte check.

## Policy return boundaries

For main mode1 with flags `40` clear and previous state6, earlier policy
exits return9,15,0,3,4,5,18,6 or14. The exception is worker0 mode13/14/15:
it calls `10437c74` at `10438212`, which can return29 before the local gate
guards. With any other worker0 mode, an observed29 necessarily passes the
local flags check at `104382be`; the late gate branch itself need not open.

The shared return29 logging boundary `10437e60` exposes a concrete reason in
R1. Reason `6ac` means byte `b181a630` was0 at `1043837a`; `6ba` is the
configuration15141/saved-state special arm; `6ec` is the final worker-state
arm at `10438454`. A return29 through `10437c74` does not use this boundary.
This permits an exact read-only probe without guessing from selector values.

Return8 has an earlier family of routes through `104381d0`, where R1=`631`
identifies `105af196()!=0`, word `b181a708==0`, previous state other than6
and worker0 mode outside10/16/17/13/14/15. R1=`640` identifies nonzero
properties6401/6402 with word `b181a708==0` and previous state other
than6; R1=`64f` identifies the saved action8 arm. A later route branches
from `10438448` to `10438460`: it
requires byte `b181a630!=0`, saved action29/8, worker0 mode other than17,
at least one of the three worker-state tests below, and `b181a708==0`.

| Worker-state getter | Exact underlying test |
|---|---|
| `10633970` -> `103e5ec8` | Byte `b23f6c35` is1,2 or3 |
| `106335b4` -> `103e6d46` | Byte `b23f6ced` is nonzero |
| `10633910` -> `103e6d6a` | Byte `b23f6ca5` is nonzero |
| `10633928` -> `103e52c0` | Returns raw byte `b23f6ca5` |

When worker0 mode is17, all three tests are false, or `b181a708!=0`, the
late arm returns29 unless raw byte `b23f6ca5==2`, which returns0. The state
bytes' physical meaning is not established by these numeric predicates.

Policy wrapper `104387a6` translates action8 into a GUI worker request
through `1062e474`: request family `100000` resolves to GUI context
`b181d9b8` through `1053cf34`. The action8 request does not call the gate
setter. GUI dispatch can therefore occur with gate0, as observed. This does
not yet establish whether another readiness or first-frame publication is
required on that ordinary dispatch path.

## Publication and its upstream producer

`1053fe10` calls `10630d12` at `1054045a`; the getter reads word `b181a6ec`.
The zero branch at `1054045e` skips the entire publication block. With a
nonzero gate, the block calls `10630d1a(1)`, `1053e134(1,0)`, then
`104bd5f6(3)` and posts high bit `80000000` through `10634050`, followed by
`200000` through `10634030`. Setter `10630d1a` writes the separate word
`b181a6f0`; it does not produce the gate.

The zero branch is a forward skip to the common tail. `tail.json` directly
reaches its return instruction `1054059e`, with gate0 and pending3=0.
The saved return PC is `1053d253`, joining the dispatcher at `1053d252`.
Mode entry has therefore completed without this publication. The attempted
`completion.json` stop at `10540922` was invalid: `1054091e` is a tail
branch after restoring LR, and `10540922` belongs to another switch arm.
That timeout supplies no evidence of a later wait.

## Ordinary GUI loop after mode entry

At `1053d252`, the dispatcher always runs phase9, selecting class1 callback
`105424e8 -> 10542486`. Getter `1053fa1e` handles word `b181d970` only
when it is1,2 or3; otherwise phase9 continues into its record processing.
The word is0 in the observed continuation. The dispatcher then runs any
phases selected by its flags and always phase11 before returning to the
ordinary loop `1053d2fa`.

`loop.json` reaches the actual wait call `1053d312` with wrapper
`b181c55c`, mask `ffbbffff` and timeout `80` hex. Its return at
`1053d316` supplies flags `84` hex. Thus the loop runs and wakes; neither
an indefinite GUI wait nor mode-entry deadlock explains this observation.
The timeout comes from `10633f26`, which selects the minimum remaining
deadline among47 software timers. This receipt does not identify which
producer posted each returned event bit.

For unchanged class1, flags `84` select these handlers:

| Dispatch order | Source branch | Handler |
|---|---|---|
| Always | `1053d258`, phase9 | `10542486`, record processing |
| Flags `&4` | `1053d274`, phase5 | `105413d6`, changed-state flags and updates |
| Always | `1053d2ee`, phase11 | `105429d4`, update/timer maintenance |

Flag `80` has no separate optional phase in `1053d0ec`; it does not mean
phase9 is conditional on that bit. Source-backed producers include GUI
entry's event `10000` at `1053d034`, exit event1 in `1053d356`, and
conditional events4/8000 in mode entry's common tail. This is not an
exhaustive producer list.

The subsequent `record.json` repeats the completed entry and actual `84`
wake, with a remaining timeout of `72` hex, then reaches phase9's first
record92 check at `1053f444`. Getter `104bdde0(92, &record)` returns0,
the record pointer at `[SP+18]` hex is0, pending-update word `b181d9b0`
is0 and display-ready word `b1807fa8` is0. The getter returns and consumes
the pending marker, rather than a transport success/error convention.
The absent-record branch goes directly through `1053f544` to `1053f89a`;
if the pending-update word remains0 there, `1053f8a0` returns without
taking this update route. This is one concrete missing update input in
this observed phase9 call, not proof that all GUI update routes are absent.
The sender of record92 and the producer of this update word are the next
source boundary; neither should be supplied as a fabricated guest event.

First-frame admission remains unresolved. The separate admission routine
`104d7e78` requires ready word `b1807fa8!=0`, additional mode/owner checks
and a supported selector. Setter `104d7f16` produces the ready word; the
previously traced pending3/high-bit route is one caller chain, not a proven
requirement for every ordinary GUI update. The observed `84` wake and
completed mode entry do not establish admission or physical scanout.

The identified original code caller of gate setter `10630d0a` is
`10438324`, inside worker0 policy `10437d1c`. After surviving its earlier
returns, the source requires main mode1/2/3, input flag `40` clear, then:

| Instruction boundary | Necessary condition |
|---|---|
| `104382be` / `104382c0` | Input flags `& 2` is nonzero |
| `104382c6` | Getter `10630ab0`: power word is nonzero |
| `104382ce` | Getter `10694f88`: previous state equals6 |
| `104382e4` / `104382e8` | Selector equals34 (`22` hex) |
| `10438320` | Getter `1063c826(290)`: configuration byte is nonzero |
| `10438324` | Call `10630d0a(1)`, storing gate at `10630d0e` |

Worker0 packet builder `10437924(flags)` writes flags to `b181d938` and
clears the selector at `b181d93c`. Only flags bit1 invokes worker phase3,
whose class1 callback `10438ba2` calls `10438d72`. That function stores the
input decoder's selector at packet+4 (`10438d96` or `10438da0`). Phase11
then calls policy through `104387a6`, using the same packet. Therefore
main mode1 selected by the system report does not itself supply this input
event bit or selector34.

The normal selector decoder consumes a376-byte input queue record through
`1053bbda(0)`, `1053cf0e` and `1053b578`. Getter `103c6c32` reads record+24;
ROM table row `109458b0` maps mask `20000000` to selector34. Other active
bits and combination handling can alter the final selector. The physical
control behind that record bit is not established here.

Configuration getter `1063c826` reaches `103c09e8`, reading
`b20d2894 + index`. Initialization `103c0a64` copies the original template
at `118b02ac`; its byte+290 (`118b03ce`) is0. The resource8 restoration
failure arm also leaves this byte0. The generic setter exists, but its
identified direct callers do not establish an index290 producer of1.
The observed zero has a source-backed default; it is not evidence of a
missing asynchronous peripheral write.

The power and previous-state lifetime is separately established in
[policy.md](policy.md): original ROM starts them at1/6. Worker0 cleanup
`10438b8c -> 10630ab8(0)` clears power. Policy `10438826 -> 10694f90`
consumes previous state6, writing saved state or0. Gate admission therefore
has a finite opportunity before original cleanup and state consumption.
The later GUI snapshot0/0 does not mean initial1/6 were absent.

## Conditional wait and verification boundary

Before publication, `1054042c` calls `1053d732`. Only state
`b181b518==5` causes a wait at `1053d768`, using event wrapper `b181c56c`,
mask `80000000` and timeout `ffffffff`, returning at `1053d76c`.
One concrete producer is callback `104be90a`, event `0410002f`: non-null
payload byte0=1 posts that bit at `104beffc`; byte0=0 posts `40000000`
instead. This event maps to record51 and caches its eight-byte payload at
`b181c5e8`. Its physical sender remains outside this scoped trace. The
observed state0 excludes this wait as the present dependency.

To establish the first executed false policy guard, use one read-only
breakpoint at a time: capture packet and gate cells at `10437d1c`, then
capture R7(flags)/R10(selector) at `104382be` if that branch is reached.
Continue through the table's guard boundaries to identify the actual skipped
condition. `10438dbc` exposes the selector just after decoding, with R4 as
packet pointer; `1053cf0e` and return `1053cf26` can join that selector to
the original input record. Runtime evidence is still needed before assigning
a physical input meaning or treating the gate branch as required by every
ordinary GUI startup route. Do not inject a selector, configuration byte,
readiness flag or event to make the branch pass.
