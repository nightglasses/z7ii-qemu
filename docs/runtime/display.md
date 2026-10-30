# Display readiness producers

This page preserves earlier policy and event observations. Current normal
startup uses the [cold-neutral input](../inputs/nor/virtual.md); the
[readiness audit](readiness.md) and [current frontier](../validation/frontier.md)
supersede the checkpoints below. The slot3/event branch described here is
not established as a universal prerequisite for all GUI output.

Held-input checkpoint: normal F4 held before initial EX sampling
crosses main mode5, selects mode1 and starts GUI worker3/mode1, with BatCk
still2. Channel9's finite shared initial-count load now reaches the original
timer callback with status1. Fill engine0's720x480 zero fill to `bf309600`
now completes through the original callback and waiter; GUI gate and pending3
are still0 in the bounded follow-up. See the
[joined policy/input trace](policy.md). That checkpoint establishes no GUI first frame.

Released-input checkpoint: the full battery warning is now composited
into the actual1024x682 YUVA target through original IRQ129. The following
bounded run has no new graphics error or MAS data abort and manager completes
worker4→worker0. Worker0 stays class1/mode0 with periodic10 events; GUI worker
stays class4/mode0. Gate`b181a6ec` and pending3 remain0. The follow-up
[policy trace](policy.md) shows that original ROM initializes power gate
`b181a6d0=1` and previous state`b181b3ec=6`, then original cleanup/policy
clear them. The present mode5 branch returns29; the zero snapshot alone
does not identify a missing producer of1/6. An original36-byte event record
selects mode5 before the GUI/action route.
See [conversion.md](conversion.md) and`run srcover/` for joined
handoff/event receipts. This is a policy/readiness frontier, not another
graphics rejection or proof of final scanout.

Erased-input observation: the paired producer of pending slot 3 and event bit
`0x80000000` is in the GUI mode-entry function `1053fe10`. The single-thread
`icount` probes below stop earlier at battery-check status `b18651d8` and
event bit `0x200`. The [battery delivery comparison](battery.md) supersedes
that general startup diagnosis: the current multi-thread baseline receives
the report, publishes status 2 and advances past the wait. The later
[worker handoff](handoff.md) stops at a rear-display configuration handler's
null resource-table fault; no GUI entry or first frame is established.

All addresses here are C1.70 MAS domain VA. Function roles are inferred from
the original instructions and tables; numeric conditions and runtime values
below are directly checked. Original firmware bytes remain unchanged.

## The paired producer

`104bd5f6(index)` writes `1` to `b201bf60 + 4*index` at `104bd5fa`.
The concrete slot-3 call and adjacent publication are:

```text
1053fe10                    GUI mode entry
  1054045a -> 10630d12()     reads b181a6ec
  1054045e CBZ -> 1054048c   zero skips the entire publication block
  10630d1a(1)               writes the separate word b181a6f0
  1053e134(1, 0)
  10540470 -> 104bd5f6(3)   b201bf6c := 1
  10540474 -> 10634050()    wrapper b181c564 -> b181c560 -> handle 010001c2
  1054047c -> 10634088(..., 80000000)
  10540488 -> 10634088(10634030(), 200000)
```

The consumer is `1062eddc`: its `1062f104` branch requires the high event
bit, then `1062f10e -> 104bf8f8(3)` reads and clears slot 3. A nonzero value
reaches `1062f120 -> 10637b72(1) -> 104d7f16(1)` and the admission poller
`10637b54`. The later `104bf8ec(3)` also clears the slot.

The same high bit can accompany other pending slots. For example,
`10620720` tests `10630d12()` and input halfword `+20 & 0x400`, then calls
`104bd5f6(4)` at `10620734`. That publishes slot 4, not slot 3. A high-bit
producer alone therefore does not prove that the readiness branch is armed.

## How the GUI callback would become runnable

The GUI worker context is `b181d9b8`. Its ROM template has class/state `4`
at `123b79b8` and callback `105408fb` at `123b79bc` (Thumb pointer). The
runtime callback is at context `+112`; the class selector is byte `+108`.

After the worker is started through `103fabb2(ctx, mode)`, its generic body
`103fab3a` consumes event bit 1 at `ctx+32`, calls method `1053cfb8(mode)`,
then method `1053d2fa`. The first method selects class 0 and posts primary
event `0x10000`. The second drains primary events and calls `1053d0ec`:

```text
1053d0ec(flags & 10000)
  -> 1053d24e: 106ddbfc(b181d9b8, phase=2, 0)
  -> callback selected by ctx+108; class 0 selects 105408fa
  -> 10540916: 106ddbf8(ctx), reading ctx+40
  -> 1054091e: 1053fe10(mode)
  -> paired producer, subject to b181a6ec != 0
```

`10299614` manages five workers. Its entry table starts at `b1811e5c`
with 12-byte rows `{getter, context, initialized}`; row 3 is the GUI worker.
It first collects phase-1 notifications for every row, then starts row 0.
After row 0's phase-2 notification, manager state `b1811e58=2` waits for a
phase-4 handoff request `{target_index, mode}`. It stops the old worker,
waits for phase 3, then starts the target through
`102996ee -> 10360872 -> 103fabb2`.

The observed notifications are phase 1 for indices `0,2,3,4,1`, followed by
phase 2 for index 0. All five initialized cells become 1. The final manager
state is 2, current index `b1811e50=0`, target `b1811e51=5` (the sentinel),
and there is no phase-4 request in this window. The GUI class remains 4 and
its mode word at `b181d9e0` remains zero. Thus initialization has completed;
the missing transition is the handoff from worker 0.

## The earlier observed wait in worker 0

Worker 0 has context `b181d8a4`. Its first method `104378a8` returns and
the loop `10437a6e` receives the initial primary event. Class-0 callback
`10438768(2)` then calls `104384c8`, which selects class 1 and performs the
startup preparation. Its final block is:

```text
if (10630b40() == 1) {             // byte b181a63a
    106ef35a(&status);            // 103f9a22: status := byte b18651d8
    while (status == 0) {
        1063d9ec(100);
        104386c4: 106340d4(1063406c(), 200, ffffffff);
        104386ca: 106ef35a(&status);
    }
    ... status 1/2 handling ...
    10630b3a(2);                  // advances b181a63a only after the wait
}
```

The wait uses wrapper `b181c56c`, handle cell `b181c568=010001c3`, mask
`0x200`, timeout `0xffffffff`. A single-breakpoint probe observes that entry
with status 0 and stage 1. It then removes the entry breakpoint and arms
only return address `104386c8`: there is no return before the 60-host-second
window ends. Final status is still 0, stage still 1, and manager state still 2.

The battery-check publisher is `103f8ec6`. It builds the classified record
through `103f8c1a`, sets first-publication word `b23bcd9c`, and stores status
2 at `103f8f30` or status 1 at `103f8f58`. On first publication it calls
`103f8f92 -> 103f9d94(callbacks=b23bcd1c, event=042d0001, ...)`.
Callback `104be772` translates precisely that event into
`10634088(1063406c(), 200)`. Registration occurs at
`104bf284 -> 106ef362`; the live callback array contains `104be773`.

The firmware strings `BatCkMtx`, `BATCKF1` and `BatckLL` identify this service
as battery checking, rather than a display register. In a separate
single-breakpoint run its dispatcher `103f9576` receives only `0x10000` and
`0x20000`. It receives none of the data/update flags `8`, `0x20` or `0x10`:

- Flag `0x10` directly calls `103f8ec6`.
- Flag `8`, or flag `0x20` with input record byte 0 equal to 19, calls
  `103f9350`, which can classify measurements and call `103f8ec6`.
- In this run, first-publication word `b23bcd9c` and input flags at
  `b23bd320` remain zero, as does the status byte.

The measured gap in this single-thread profile is battery-result publication
before the mode handoff. [battery.md](battery.md) now traces it to the EX
transmit worker's failed 10-tick completion/ACK wait, and reproduces delivery
on the current multi-thread baseline. Historical delivery is therefore not
contradicted by the scoped negative observations below.

## The later mode-entry gate

Even after the handoff, the paired producer requires `b181a6ec != 0`.
`10630d0a(value)` writes this word at `10630d0e`. Its identified call is
`10438324 -> 10630d0a(1)` inside `10437d1c`, called by `104387a6` at
`104387ee`. Necessary local conditions on that branch include input bit 1,
`10630ab0()` (`b181a6d0`) nonzero, `10694f88()` (`b181b3ec`) equal to 6,
selector `a1[1]` equal to `0x22`, and `1063c826(0x122)` nonzero.
Other earlier guards in that function still apply.

The live gate word is zero. This is a separate later condition, not an
observed false gate test in `1053fe10`: the current wait prevents reaching
that function. The selector/configuration meanings and whether this gate must
open on the ordinary GUI startup path remain unverified.

## Receipts and limits

`run startgate/` retains `argv.txt`, `runprobe.py`, the
`manager`, `waits`, `battery` and `block` GDB scripts and logs, static IDA
results in `static.json`, and bounded GNU objdump listings/call searches.
Each accepted dynamic probe uses one hardware breakpoint at a time. The
earlier `producer` multi-breakpoint run is exploratory and its zero-hit
counts are not needed for the conclusions above.

All runs use the complete unchanged decoded bundle, single-thread TCG and
`-icount shift=6,sleep=off`. They read registers/memory and do not inject a
packet, write guest memory, invoke guest code, or change a PC. These are
bounded host-time observations, not proof of permanent unreachability or a
GUI-first-frame receipt. No device behavior changed in this investigation.
