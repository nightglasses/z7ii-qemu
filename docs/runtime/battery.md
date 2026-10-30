# BatCk delivery and the execution profile

Scope: the recorded source-graph build reproduces battery-result publication
with the unchanged complete bundle and multi-thread TCG. It is
not a missing frontend implementation on that baseline. The earlier
single-thread `icount` investigation found a real wait in that execution
profile, but overstated it as the general startup frontier.

EX code addresses `080xxxxx` and SRAM `200xxxxx` below are EX domain VA;
`10xxxxxx` code and `bxxxxxxx` data addresses are MAS domain VA. These are
guest-software observations and static call chains, not physical-camera timing
or a first-frame receipt.

## Who sends the result

EX must first finish its system handshake. The original source chain is:

```text
EX system descriptor, client 1, body type 2, body length 28
  -> 08033958 -> 08032000: enqueue, returned handle 0100
  -> 08031f28 / 08033a00: completed descriptor callback
  -> 08033a74: matching kind-2 handle posts lifecycle bit 20
  -> lifecycle dispatch 080332a8; bit 800 runs 08034034
  -> kind-3 descriptor, body length 106
  -> 08033b2c: matching kind-3 handle posts lifecycle bit 40
  -> 08032b80 -> 08032bb0 -> 08024c48
  -> owner word 20015dc8 := 1; battery task notified
  -> 080278fc(0), then 08025544: builds the battery report
  -> EX transport -> MAS client 4, 112-byte reassembled record
```

The MAS frontend and BatCk are separate services:

```text
102053e4 -> 102055e6: reassemble and dispatch the client record
  -> 1038a6aa(event=43090001): copy 112 bytes to b1d53c68
  -> 1038a6ec: post bit 10 to BatSv
  -> 1038a53a: bit 10 invokes registered callback b1810498
  -> 103f96da(event=43090010): copy to b23bd29c
  -> 103f9710: post bit 10 to BatCk
  -> 103f9576 -> 103f8ec6: classify and publish
  -> b18651d8 := 1 or 2; first-publication word b23bcd9c := 1
  -> callback broadcast 042d0001
  -> 104be772: post 200 to the waiting worker
```

The bit-`10` stores above are checked in original Thumb instructions; inferred
decompiler prototypes omit some register arguments. `43090001` is the
frontend event for the record, not proof of a particular power classification.
The current multi-thread result is status **2**, whereas a historical run
recorded status 1. Both are nonzero and release this startup wait.

## The concrete single-thread failure

Submission is not the missing condition: a focused probe observes the first
descriptor at `20013c20` accepted with handle `0100`. Its serialized first
frame begins `01 01 1c 00 ... 02 02 ...`. The failure is later, in EX transmit
worker `08034a18`:

```text
08034abe: arm a 256-byte SPI transfer
08034ae6: clear ACK flag at 20017d58
08034afe: enable the handshake input
08034b0a: 0802157c(event object 20017d90, mask=0c, result, timeout=10)
08034b0e: test the helper's Boolean result
08034b60: test ACK flag 20017d58
```

`0802157c` returns 1 only when the underlying RTOS event wait succeeds.
It uses mode 0 with mask `0x0c`. Completion callback `08034858` posts bit 4;
ACK callback `08034894` writes `20017d58=1` and posts bit 8. Without a
successful wait and ACK, the worker takes its retry path through `08034764`
and never completes that first descriptor to the system-handshake consumer.

At the return point, the first twelve single-thread `icount` attempts all
show helper result **0**, ACK **0**, and SPI4 TX DMA NDTR **142..192** bytes
remaining. The same system-frame header repeats. The failed wait's result
buffer is not interpreted as valid returned event flags.

With multi-thread TCG, the first corresponding system-frame attempt shows
helper result **1**, returned flags **0x0d**, ACK **1**, and TX NDTR **0**.
The subsequent kind-3 handshake, battery enable, report and MAS publication
are then observed. This identifies a timed SPI/ACK boundary sensitive to the
execution profile. It does not yet identify the precise CPU scheduling or
FIFO-refill delay responsible for the partial transfer under single-thread
TCG; no device timing code was changed to force that profile through.

## Controlled comparison with history

All current comparisons use `work/build/qemu/qemu-system-arm`, the same
`descriptor17-z7cross-mac1/decoded-container.bin`, the same built-in EX factory
and board inputs, and no descriptor override or peer injection.

| Execution profile, 60 host seconds | Battery frontend | BatCk final state |
|---|---|---|
| Single-thread TCG, `-icount shift=6,sleep=off` | no report | status 0, publication 0; manager state 2 |
| Single-thread TCG, no `icount` | no report in the window | status 0, publication 0; manager state 2 |
| Multi-thread TCG, no `icount` | one `43090001` / 112-byte record | status 2, publication 1; manager has advanced |

The last two runs change only the TCG thread setting. Separate native-start
runs, without a debugger connection or breakpoint during the measurement,
reproduce the single-thread-`icount` failure and the multi-thread delivery.
They are stopped through QMP after the window, then read through GDB. This
rules out a startup breakpoint as the sole explanation. Host-time windows do
not represent equal guest time or prove permanent unreachability.

The native multi-thread trace observes four system submissions, three
completion fanouts, one system kind-3 builder, one battery-report builder,
one MAS battery frontend entry and one BatCk classifier entry. Worker 0
executes the post-wait path at `104386d6`; it is no longer stuck at the
earlier battery wait.

The historical positive receipts are real positive executions:
`ex-battery-runtime/cadence-narrow/gdb.log` records the handshake and client-4
report; `post-battery-display1/gdb.log` records status 1 and the following
manager transition. Their retained launch scripts use:

- the older `work/build/runtime` executable;
- `-accel tcg,thread=multi`, with no `icount`;
- the former functional/NOR profile selectors, a descriptor-17 input file,
  and explicit ADC/expander values.

Those launches are not identical to the recent single-thread probes. The
current build nevertheless reproduces delivery with its current inputs when
run under multi-thread TCG, so restoring obsolete options or an old executable
is not required to recover this edge. Report contents and classification are
not asserted identical across those input profiles. The old positive logs do
not establish a GUI first frame, and their multi-breakpoint zero counts are
not used as absence evidence.

The normal composed-runtime baseline remains the multi-thread invocation in
[ex.md](ex.md). Trace-only single-thread/`icount` probes must retain their
execution-profile qualification instead of replacing that baseline.

## Current remaining boundary and receipts

In both current multi-thread windows the final manager snapshot is state 3,
current worker 0, target worker 4, mode `0x15`. The subsequent
[handoff investigation](handoff.md) observes worker 0 consume its stop bit,
then wait for display configuration completion on handle `01000160`, mask
`20`. Its request handler faults at `10481864` through a null rear-panel
resource table before invoking completion. The generic worker wrapper
therefore cannot reach its phase-3 acknowledgement. Pending slot 3 and its
later GUI gate remain zero; battery delivery alone does not establish GUI
entry or display readiness.

Raw material is in `run batck/`: per-run argv, `frontend`,
`submit` and `txwait` probes, native-start scripts and snapshots, bounded
`ex.dis` / `mas.dis`, and static MAS decompilations. The initial
`icount-frontend` EX snapshot failed because GDB selected the wrong CPU for
memory access; it is not EX-state evidence. Later EX reads explicitly select
`Hg5` and issue read-only RSP memory requests. The original 512 KiB EX listing
input is directly byte-equal to the bundle's EX slice at offset `0xd0`.

No firmware bytes, guest state, PC, packet, callback or IRQ were injected.
This investigation changes documentation only; native device tests were not
repeated and no original GUI-first-frame result is claimed.
