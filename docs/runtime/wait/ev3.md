# EV3 producer boundary

This receipt records a fresh read-only run of the unchanged Z7II C1.70 MAS
payload. It isolates the current GUI-phase event producer and separates it from
the earlier EV88 wait.

## Run contract

- Payload: `02_eg1840_mas_01700.payload.bin`, 38,239,096 bytes, unchanged.
- Machine: `z7ii-mas-diagnostic`, two MAS Cortex-A7 CPUs, multi-thread TCG.
- Observer: GDB remote breakpoint at the event-set wrapper and a read-only
  watchpoint on the EV3 control-block pattern field.
- The observer wrote no event bits, IRQ state, ready cells, task fields, or
  device state. Debugger files remain ignored under `work/`.

## Direct producer observation

The event-set wrapper `0x103610cc` was entered on MAS CPU1 with:

```text
r0              0xb185e12c
*(uint32_t *)r0 0x01000003
r1 (mask)       0x00000002
lr              0x10200ceb
```

The handle cell therefore resolves to event ID `0x01000003` (EV3). This
mask `0x2` call has wrapper LR `0x10200ceb`; it is not a call from
`0x10200d06`. The later read-only
`work/research/guiresume/graphicsextent1/gdb.log:105–118` separately records `0x10200d06` with r1=`0x1` and wrapper LR `0x10200d0b` for mask `0x1`.
These historical peer EV3 setters publish masks `0x1`/`0x2`, not the GUI
mask `0x10` produced by MAS VA `0x10200c34`. The original register facts above
are unchanged; the earlier mask2-to-`10200d06` function binding is withdrawn.
At wrapper entry the EV3 control block was:

```text
0xb2c55848: 0xb2c33008 0xb2c33290 0x01000003 0x00000000
0xb2c55858: 0x00000008 0x00000000 0x00000000 0x00000000
```

The linked waiters are guest task objects; the host did not add them. After
letting the guest continue, the same control block reached pattern `0x5` in a
separate read-only snapshot. This is consistent with the peer producer setting
the event and the waiter state advancing.

## Wait setup versus producer

The first write observed at the kernel event routine `0x101111c8` had:

```text
value 0x00000002
lr    0x1011ea2c
```

That write belongs to the wait setup path. It is not the producer. The current
wait task `0xb2c33368` later contains event ID `0x01000003`, output
`0xb51bb508`, requested pattern `0x10`, and mode `1`; its control block is
`0xb2c55848`. The separate CB `0xb2c568e8` has ID `0x01000088` (EV88) and is
not the current GUI-phase producer target.

## Model boundary

This run proves the guest-side producer edge:

```text
historical peer setter (wrapper LR 0x10200ceb)
        -> event-set wrapper 0x103610cc
        -> EV3 handle cell 0xb185e12c
        -> event ID 0x01000003, mask 0x2
```

It does not prove the physical peer-ready/reply source before the setters.
The next required evidence is the original guest GPIO/bridge/USIO path to
these distinct calls, not a mask2 binding to `0x10200d06`. QEMU must keep
that path source-bound and must not inject EV3, an IRQ, a ready task, or a synthetic GUI frame. No QEMU model change is
justified by this receipt.

## Complete-bundle revalidation before bridge ISR correction

Two bounded eight-second observations (`work/research/guiresume/verified1/`
and `verified2/`) use the unchanged complete C1.70 decoded bundle, not the
raw-MAS configuration above. Both use `boot-shim=functional`,
`-smp cpus=2,maxcpus=5`, and `-accel tcg,thread=multi`. Their shared rebuilt
native binary SHA256 is
`13158f95a0c3313ee9d177f50ed1d55ce9a5a317a67e3bab106736fab71fc764`.
The receipts identify base revision `f02f0c43cd4da62b3a469717accd86e03d540f0c`,
local source hashes and the uncommitted changes; this is not a pristine
revision build. GDB uses explicit AAPCS and denies guest memory/register
writes and function calls. All guest progress is natural.

Both runs capture GUI initialization phases 1 and 2, the GUI message task,
two EV88 wait/return pairs, and 14 COPY submissions with no first DMA fault.
The final three functional loader extents at PA `0x50200000`, `0x50000000`
and `0x50080000` compare byte-exactly with their guest source ranges, including
the later `0x3a50`-byte overwrite of the second extent. They are not the first
uncovered COPY destinations in this configuration; this does not recover
physical provider ownership or identify a framebuffer.

In `verified2`, MAS enters wrapper VA `0x103610da` with handle
`0xb185e12c`, requested mask `0x10` and ordinary LR `0x1020046f`.
The observed wrapper sets an infinite timeout and mode `1` before invoking
the kernel. The stopped task `0xb2c33368` retains EV3, output `0xb51bb508`,
mask `0x10` and mode `1`; EV3's stopped pattern is `0x1`, not the earlier
raw-MAS receipt's `0x5`. The captured peer setter calls publish masks `0x1`
and `0x2`, not `0x10`. Neither GUI phase 3 nor the graphics transfer producer
is captured in these scopes. The transfer/first-frame gate remains failed;
this is not proof that no producer exists outside the observed interval.

At this pre-correction boundary, the next source-bound question was the
producer/preconditions for EV3 mask `0x10` at caller VA `0x1020046a`, rather
than another loader RAM window or an injected wake. Darwin's upstream `qemu_get_thread_id()` falls back to
`getpid()`, so identical QMP thread IDs cannot disprove the requested MTTCG
mode. Separate HMP `cpu N` and `info registers` calls also do not preserve
CPU selection; these observations use GDB's all-thread register snapshot
instead of treating five repeated CPU0 reports as independent cores.

## Producer and bridge configuration frontier

Original-byte disassembly plus bounded read-only observations locate the
mask `0x10` producer at MAS VA `0x10200c34`. It calls two callbacks, then
`0x10387e36` and `0x1038825e`, before calling the event-set wrapper at
`0x10200c56` with EV3 handle `0xb185e12c` and mask `0x10`. This recovers the
actual call boundary, not accepted reconstruction source or physical wiring.

`producer1` and `producer2` reach this task and both callback returns. The
first precondition returns; the second waits in `0x102de69e` through wrapper
`0x103610da`, handle `0xb1806f44`, mask `0x4`, LR `0x102de6c9`. It is a
separate bridge event, not EV3 or EV88. In `producer3` the MAS and SLA ISRs
both receive raw `0x40`, while their original `LSLS #26` branches require
raw bit5 (`0x20`) to set flag4. The model's configuration bit was wrong.

After the [bridge correction](../../devices/bridge/protocol.md), two final
8-second complete-bundle observations (`final1` and `final2`) catch both
ISRs' flag4 paths and the MAS guest setter with mask `0x4`; the configuration
wait returns naturally. These dirty-worktree runs share native SHA256
`f2512544ef07e70b703574fbb52e2a491bc5d43386ec17de55cf84ca030f2dbc`.
They then reject the original word read at PA `0x2013e000` in both domains.
The MAS getter is `0x102ddda4`, its caller return `0x10387eff`; the SLA
getter returns to `0x10060db7`. Bankmask2 selects this read from base
`0x20136000`. The original publisher uses base `0x20136008`, but its
relationship to both banks' getter values and reset state is not yet proven.

A paired-word reflection experiment produced a natural EV3 `0x10` setter
and return from its consumer wait, but then rejected the bank0 read at
`0x20136000`. That experiment was withdrawn: its cold-word reflection and
bank binding cannot establish the real notification contract. Its ignored
`work/research/ev3producer/mailbox_candidate.patch` and `mailbox1` receipt
must not be presented as progress from the maintained model.

Final runs do not observe EV3 `0x10`, the transfer producer or a first frame.
Their COPY and loader-byte gates fail before those submissions, unlike the
older 14-COPY observations above. The next evidence is the real producer,
bank routing and reset law for the getter/publisher ports, not injected
flags, ready values, a NOR writer for the abort path, or longer waiting.

## Notification-profile experiment and current strict boundary

The newer [notification-mask study](../../devices/bridge/protocol.md) separates
source-supported set/clear intent from unproved firstword/bank/reset/IRQ laws.
The default-off `notification` property allows those laws to be explored as
explicit functional hypotheses; it is not accepted physical recovery or a
replacement for the withdrawn ordinary-word experiment.

Before the I2C fix, this profile lets the original producer set EV3 `0x10`
and its consumer return, then reaches an ADDR write rejected because control
`0x0b3e` includes divisors. The independent low-byte mode correction adds no
endpoint or ACK. Two fresh eight-second observations (`flagsi2c1/2`) then
reach GUI phase3 entry with 11 and 13 captured COPY submissions respectively;
all three loader extents compare byte-exactly in both. Their binary SHA256 is
`3848f3146a462f0649e586d548d1a264f1424949c17a67be24979eb8d06fdae9`.
The source worktree is dirty, and the enabled notification assumptions are
part of each experiment's identity, not evidence of hardware behavior.

Both experiments first reject MAS WORD write `0` to PA `0x2c04000c`, PC
`0x1020b4da`; the subsequent NOR abort-path write is not the first cause.
Neither reaches the child constructor, graphics transfer producer/completion
or a natural first frame. Their entry-only hooks did not qualify phase3
completion or rendering.

Six later read-only [startup observations](../coprocessor.md), with the same
binary and explicit experimental profile, capture 13 COPY requests each and
all three byte-exact extents. Additional hooks in the last two observe the
GUI component phase3 routine returning1 before the controller abort; indexed
callback returns and actual guest-written pointers corroborate its setup.
This is not a global initialization completion, graphics transfer or frame.

`flagsdefault2` uses the same binary with both notification properties false
and still rejects the two domains' reads at `0x2013e000`. It captures zero
COPY submissions and fails its loader-byte checks. The strict frontier is
therefore unchanged. Closing the notification laws remains necessary before
promoting the experimental path; interpreting the next `0x2c04` interface
also requires a bounded device contract, not zero-write discard.

