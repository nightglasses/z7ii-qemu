# EX startup, status checks and host transport

This article preserves bounded reverse-engineering observations and their corrections. It is supporting analysis, not the current boot-status page. Use the [GUI chain guide](guichain.md) for the current interpretation and [startup boundary](../validation/frontier.md) for accepted progress. Zero-hit results from historical multi-breakpoint probes are not absence evidence.

## Current bundle stop: EX 0x4f register 0x23 readback {#current-bundle-stop-ex-0x4f-register-0x23-readback}

A QMP `info registers -a` sample on the current bundle binary after 80 seconds
of host execution shows MAS CPU0/CPU1 in the kernel idle loop
(`0x10107090`/`0x10107094`) and the SLA CPUs at
`0x10038a48`/`0x10038a64`. The EX CPU is not idle: it returns to
`0x08047596` inside `sub_8047584`, the I2C SR1 wait for RXNE (bit 6) or
STOPF (bit 4).

A hardware-breakpoint probe at `sub_80442c4`
(`run ex-i2c-entry/gdb.log`) records the active transfer as
`a2=0x9e` (0x4f write address), `a3=0x23`, `a4=1`, `a5=0x20008928`,
`a6=1`, `a7=1000`, called from `sub_80387e8` at `0x08038857`. The same probe
shows the 0x22 three-byte read from `sub_802fdf0`; the 0x6b/0x0d setup is not
reached. A second probe at `sub_8047584`
(`run ex-i2c-wait/gdb.log`) records `base=0x40005400`,
`buf=0x20008928/0x20008929`, `count=1`, and `SR1=0` on every sample.

`sub_80387e8` writes the expected value to 0x4f register `0x23` with
`sub_804472c`, then reads it back three times. The read side was the blocker:
the firmware saw neither RXNE nor STOPF. A `hbreak` at `0x08047192`
(`run ex-i2c-sr1b/gdb.log`) records SR1 `0x46` (ADDR+RXNE+BTF)
immediately after both the 0x22 (`0x45`) and 0x4f (`0x9f`) read addresses.
The HAL's ADDR wait then saw `0x44`, but the subsequent RXNE/STOPF wait saw
`0`. The receive byte was filled and then cleared between the address and the
RXNE wait.

### Fix and new stop {#fix-and-new-stop}

`ex_peripheral_write` now treats a CR1 STOP as an observable completion: it
preserves RXNE/BTF and the received byte, sets STOPF, and clears ADDR; the
last DR read retires RXNE/BTF. The native
`/arm/nikon/ex/stop-preserves-rx` fixture covers the STOP-before-DR-read
sequence. With that change the 0x4f readback completes.

A hardware-breakpoint probe on the new binary
(`run ex-i2c-fixed/gdb.log`, `run ex-setup-ret/gdb.log`,
`run ex-setup-stop/gdb.log`) records `SETUP_B40C` at `0x0802b40c` with
descriptor bytes `0,0,1,1,0,0`, `sub_802b014` mode 0 write `0` repeated, the
0x6b write returning `0`, and the STOP/wait returning `0`. No `READ_RET` at
`0x0802b074` and no compare at `0x0802b0d6` are reached.

The 0x6b **read** (`sub_802ae48`) is the current stop. A hardware-breakpoint
probe (`run ex-6b-read/gdb.log`, `run ex-6b-ret/gdb.log`,
`run ex-6b-sr2/gdb.log`, `run ex-6b-464c/gdb.log`) records the
read entry with handle `0x2001fdcc`, base `0x40005400`, SR2 `0`, state
`0x20`; `sub_804464c` is entered with the same SR2/state, runs its post-loop
setup, writes CR1 ACK/START, and does not return to `0x0802ae78` within the
bounded window. No `0xd7` read address appears.

The active RAM vector table (`VTOR=0x20000000`) maps IRQ31/33 to
`0x080201fd`; the dispatcher `sub_80201fc` calls the table at `0x20022d70`,
and the IRQ31 entry `0x08041065` tail-calls the callback `0x0802fce1` ->
`sub_802fce0`. That callback processes the handle with `sub_8043bd8` and
signals `0x2001fe9c` only when the handle state is `32`; otherwise it
returns while the IRQ remains asserted. The callback leaves STOPF set
(SR1 `0x10`); the model now clears ADDR/STOPF on the SR1-then-SR2 read, and
`/arm/nikon/ex/stop-preserves-rx` covers it. A follow-up probe records
`WAIT_ENTRY` at `sub_802b628` (`r0=3`, `r1=1`) eight times, so the setup does
reach the read wait; the missing `0x0802ae78` is a probe limitation. The
traced IRQ31 callback uses handle `0x2001fea0` and event `0x2001fef4`, while
the 0x6b read waits on object `0x2001fdc0` (handle `0x2001fdcc`). The 0x6b
completion event is not that traced callback. The 0x6b driver callbacks are
`sub_802af08`/`sub_802aee8`, registered by `sub_802b128` on the handle
`0x2001fdcc`. `sub_802af08` runs `sub_8043bd8`/`sub_8043e38` and, when the HAL
state is `32`, calls `sub_8021556(0x2001fdc0)` (event id `0x25`); the probe
observes `DRV_READY`/`DRV_SIGNAL`. The read wait
`sub_802b628(0x2001fdc0, 3, 1)` still does not return to the compare, so the
next target is the RTOS event wait (`sub_804d698`) mask/bit and its one-tick
timeout, not the monitor. The setup never reaches mode 1/3 or the readback
equality; the 0x0d/SSI frontier remains downstream. No machine input or forced
status is added.

## 0x6b read: where the original execution actually stops {#_0x6b-read-where-the-original-execution-actually-stops}

New disassembly of the original EX image fixes three prior
misreadings and moves the 0x6b frontier from "the RTOS wait does not return" to
"the receive path takes the BTF branch".

**Superseded in part (icount receipt).** With the model's address-phase
deferral (`7be58c8e`: the receive byte is clocked only after the guest clears
ADDR) and the icount time base described below, a 900 s run reaches the 0x6b
read and takes the *non-BTF* completion path:

```text
sub_802ae48 entered 10x with descriptor 0x2001fdcc, wire address 0xd6,
sub-address 0 1 3;
sub_8046cd0 entered for 0x2001fdcc with state +61 = 0x22, count +42 = 1,
+44 != 2  (the exact condition it tests).
```

Before the deferral fix this handler was never entered.  The BTF path
`sub_8046bfe` is not a dead end either: it runs for other descriptors
(`0x2001fea0`/`0x2001fe40`) at the same state `0x22` with counts 2..4.  So the
0x6b monitor read completes, and the earlier claim that RXNE-with-BTF can never
complete is withdrawn.

The report input block `0x20016040..58` and `report[+61]` still carry only the
boot template's zero, so the power classification is downstream of something
other than the 0x6b read.

### Probe time base {#probe-time-base}

Original-firmware probes run with `-accel tcg,thread=single -icount shift=6`:
icount derives `QEMU_CLOCK_VIRTUAL` from executed instructions, so peripheral
byte timers advance with guest progress instead of wall clock, and `shift=6`
(64 ns per instruction) matches one instruction per cycle at the modelled
16 MHz HSI.  icount is incompatible with multi-threaded TCG.  The wall-clock
base is kept for A/B comparison (`run ex-6b-ercd/run-wallclock.sh`);
receipts across time bases must be compared by guest progress, not wall
seconds.

Two later harness changes keep the same time base but remove wall-clock cost:

- `-icount shift=6,sleep=off` (`run ex-6b-ercd/run-fast.sh`) stops QEMU
  from idling to keep virtual time equal to real time, so one wall second covers
  far more guest progress at the identical 64 ns per instruction.  A state that
  needed a 420 s throttled window arrives in about 10 s.
- Static "who references this" scans must cover unconditional `B`/`B.W` as
  well as `BL`/`BLX`: `run brscan.py` does both, `run blscan.py` only calls.
  Several earlier "no reference" results were `B`-only references (see the
  event-0x39 correction), while the EGL present wrapper stays unreferenced
  under both scanners.
- Execution coverage is measured with `-dfilter ADDR+SIZE -d exec,guest_errors`
  (`run ex-6b-ercd/run-df.sh`) and aggregated from the trace, because
  GDB execution breakpoints detach the QEMU stub in this configuration after
  their first hit (data watchpoints stay connected).  A zero-hit coverage line
  is a direct execution fact, not a probe artefact.

Static facts (read from the original bytes, load base `0x08000000`, container
offset `0xd0`, length `0x80002`):

- `sub_802AE48` is the 0x6b read helper. It clears flag bits 0/1, calls the
  read-start routine at `0x0802AE74`, and only on a zero return executes the
  event wait at `0x0802AE86`. A non-zero start returns immediately through
  `sub_802B5F4` without ever waiting.
- `sub_804464C` (read) and `sub_80449B4` (write) contain no `BL`/`BLX`. They
  validate the descriptor state byte (`+0x3d == 0x20`), poll `SR2` bit1 for a
  free bus, program `CR1` (`PE`, `POS`, `ACK`/`START`) and `CR2` interrupt
  enables, then return. They never touch `SR1` or `DR`.
- Byte movement is interrupt driven. `sub_802B128` installs the EV handler
  `sub_802AF08` and the ER handler `sub_802AEE8` through the software interrupt
  numbers at `0x2001FDA4`/`0x2001FDA5`; the EV handler drives `sub_8043BD8`,
  which reads `SR1`/`SR2` and dispatches on the phase byte `+0x3e`.
- `sub_802B014` retries three times. Success requires masked equality between
  the written byte and the read-back byte plus the range gate (`<= 0xa0` for
  command 0, `& 0x3f <= 0x18` for 1, `& 0x1f <= 0x19` for 3).
- The receive completion path is `sub_8046CD0` (RXNE with BTF clear): it reads
  `DR`, clears the interrupt enables and sets state `0x20`, which lets
  `sub_802AF08` signal flag bit0. The RXNE-with-BTF path `sub_8046BFE` leaves
  state `0x22`/phase `0x40` for a one-byte request, and the following STOPF is
  not handled in that phase, so the request can never complete.

Measured on the complete bundle (`0x6b` wire address `0xd6`, register `0`,
one byte, timeout argument `1`):

- the read start is entered with descriptor `0x2001fdcc` and reaches its
  bus-free exit `0x080446A0` (`6` hits in 240 s), so the bus is not stuck;
- the RXNE handler takes `0x08046BFE` six times for descriptor `0x2001fdcc`
  and never `0x08046CD0`, i.e. `SR1` carries RXNE together with BTF;
- event waits on the flag object at `0x2001FDC0` (id `0x25`, `waiptn=3`)
  return `E_OK` immediately (`0x080215D4`, `r0=0`), so the RTOS wait is not the
  blocker; a previous probe that saw no read-side wait call was observing the
  write helper's wait site `0x0802AEDA`, not `0x0802AE86`;
- the RTOS tick word at `0x20022D58` stays near `0x1ee` while the retry loop
  runs, so guest time is not advancing during the loop. This is a separate
  time-base observation, not a completion signal.

The RTOS wrappers match the local T-Kernel 1.00.01 sources: `sub_804D698` is
`_tk_wai_flg`, `sub_804CFFC` `_tk_set_flg`, `sub_804BE34` `_tk_clr_flg`,
`sub_804AD00` `eventflag_cond`, `sub_804AD90` `gcb_make_wait_with_diswai`. The
timeout argument is in milliseconds (`TMO_POL=0`, `TMO_FEVR=-1`), the wait mode
is a hardcoded `TWF_ORW` inside `sub_80215BC`, bit0 means completion, bit1 means
error, and `sub_802B628` maps them to `0`/`1`, with `3` for a failed wait. The
flag id is not a literal: `sub_8021514` stores the id returned by the T-Kernel
object allocator, so `0x25` depends on the boot allocation order.

Model boundary, now resolved: the controller used to feed the receive FIFO at
address match, so a one-byte reader saw RXNE with BTF and the firmware never
reached its completion path. Retiring that speculative byte at the `ACK` write
was tried and reverted, because the original guest then stopped before the 0x6b
traffic. The receive shift-byte correction times the shift byte instead: one byte
at address match, the acknowledged byte after nine `CCR`-scaled periods of the
APB1 clock. Clearing ACK cancels the shift for a one-byte receiver with POS
clear; STOP, START and reset cancel either receive sequence.

With that model the original bundle reaches, for the first time, the read
helper's event wait `0x0802AE86`, the setup comparison `0x0802B0D6` and the
success exit `0x0802B0E4` for commands 0, 1 and 3 and for later repeats in a
300 s window. The read-back equality path therefore runs without a fabricated
value. `/nikon/ex/i2c-single-byte-read` records the controller-side sequence:
`SR1` `0x42`, then `0x40`, then RXNE without BTF across a stepped clock. The
same probe also reaches the periodic `0x0d` status service at `0x0802B6F0`, so
the monitor lane now runs its setup and its status cycle on the original
image. The structured contract is in
[the monitor page](../devices/expower/protocol.md).

### Two-byte receiver and POS

The EX-private little-endian controller at PA `0x40005400` also implements
the two-byte receive sequence specified by ST's
[RM0090 master receiver N=2 and CR1.POS description](https://www.st.com/resource/zh/reference_manual/DM00031020.pdf).
With POS set, clearing ACK NACKs the second byte. Clearing ADDR by reading
SR1 then SR2 releases the first byte; the CCR/APB1-derived timer clocks the
second byte and raises BTF. ACK may be cleared immediately before or after
ADDR release. The finite receive budget includes already buffered bytes,
so two DR reads cannot clock a third byte. BTF clears after the first pop,
RXNE after the second; STOP preserves buffered data and reset cancels a
pending shift.

The native `/arm/nikon/ex/i2c-pos-two-byte-read` fixture originally observed
SR1 `0x40` instead of required `0x44`. Both ACK-clear orders now pass using
distinct real expander bytes, as do drain and pending-shift reset checks.
The complete 19-subtest EX batch passes. These are controller fixtures;
they do not establish the cause of an original-firmware wait, menu operation,
physical timing, or acceptance of a modified firmware package.

## MAS mode-1 producer: retire the watchpoint evidence {#mas-mode-1-producer-retire-the-watchpoint-evidence}

The "no `0x2c0a0000`/`0x1024...` FIFO write" item is an instrumentation
artifact. `0x10370A14` has nine code callers and no data references; the mode-1
engine `0x10243380` pumps through `0x1024325C`, and `0x102436EC` (called from
the RX branch) is a second writer to the same `device+0x00` data register with
the same `SR` bit4 gate. Existing receipts under `run ex-battery-runtime/`
record the worker callback storing state `2->1` with `lr=0x10370a7b`, the
channel-1 context stepping `tx_index` `0 -> 32 -> ... -> 128`, and the final
object at `tx_index=128/128` with a shifting model FIFO. Fifteen probe scripts
set MMIO watchpoints on `0x2c0a0000`/`0x2c0a0100` and none reported a hit,
although channel 1 provably receives 128 pushes per transfer; MMIO data
watchpoints in this gdbstub are the unreliable instrument, so those silent
receipts no longer count as evidence. Channel 0 has still never been measured
with a trustworthy probe; read the context object `0xb1802090` instead.

## Monitor lane: what the image does, after the structure pass {#monitor-lane-what-the-image-does-after-the-structure-pass}

The `0x6b` lane is a leaf sink. The IMAGE's own command space is small and now
recorded in [the monitor contract](../devices/expower/protocol.md):

- **17 wire sites, all one byte.** A bounded decode over Code
  `0x08000000..0x08080000` finds exactly 17 `0xD6` immediates, in six functions
  (`sub_802AD0C`, `sub_802ADAC`, `sub_802AF38`, `sub_802B014`, `sub_802B234`,
  `sub_802B40C`). Every site passes length 1 and timeout 1. There is no `0xD7`
  in this lane: the receive direction comes from the `0x400` control bit and
  the interrupt handler sets bit0 of the address byte.
- **Two committers, two orders.** `sub_802B234` writes `6,0,1,2,3,4,5,7`;
  `sub_802B40C` writes `0,1,2,3,4,5,7,6`. Payloads live at `0x2001fda6+cmd` and
  are seeded `00 80 a0 00 00 8d 33 00 cd 06`.
- **Only `0`, `1`, `3` are verified**, with masks `0xff`/`0x3f`/`0x1f` and gates
  `<=0xa0`/`<=0x18`/`<=0x19`. `0x02`, `0x04`, `0x05`, `0x06`, `0x07` are
  write-only; `0x0b`, `0x10`, `0x11` and (as status) `0x01` are read-only.
- **A persistent mismatch is not an abort.** After three attempts the firmware
  writes `0x01 = 0xc0` and returns success; only a transfer-level error aborts,
  and that path re-initialises the whole lane through `sub_802B170`. The
  separate 300 s probe records equality hits only, with no mismatch edge and no
  sanitiser write, so the model passes the equality path and not the sanitiser.
- **The status class is not a control input.** Eight handlers copy the `0x0d`
  class into the snapshot byte `0x2001f5dc`; the only consumer, `sub_8029A84`,
  compares class with snapshot and, on a change, reprograms `0x6b`. No per-class
  branch, GPIO write or rail change exists. The `0x6b` commit is gated by the
  transaction-valid dword `0x2001f650`, not by the class.
- **"mode 1/3" was a misnomer.** The `1` and `3` arguments of `sub_802B014` are
  monitor sub-register indices, not transfer modes. Nothing in this lane reaches
  a program transfer: the `0x6b` command handler's record transfer is a separate
  framed output (`sub_80249A8` -> `sub_8024308`), and the multi-byte EX lane that
  does exist talks to a different device (`0x70`) through `0x2001fea0`.

Where the frontier actually sits now:

1. The host-command dispatch `sub_8027E58` (message object `0x2001f664`, ids
   101..124) and the 16 software timers at `0x08051444`; the monitor lane is one
   handler among many, and the others (`sub_802A57C`, `sub_802A888`,
   `sub_802A03C`, `sub_8029A84`, `sub_8029BE0`, `sub_8026B04`) carry untested
   state.
2. The unmodelled side of the lane: the detect-line GPIO guards (`sub_802B6D4`,
   `sub_802B87C`), the rail object `0x2001fe28`, and the 10-sample present /
   absent debounce in `sub_8026718`. These read external inputs this model does
   not drive, so a probe should record the guards rather than assume a branch.
3. `0x2001fdaE/fdaf` (`cd 06`) are seeded but never transmitted; their consumer
   is unresolved, and `0x10`/`0x11` replies are modelled as zero because no
   firmware path writes those indices, which satisfies the health predicate but
   is not a recovered device identity.

## EX host lane: dispatch map and the reset loop {#ex-host-lane-dispatch-map-and-the-reset-loop}

Three static audits plus four hardware-breakpoint windows close the "what runs
on the EX core" question for the monitored bundle.

Dispatch (static, module `0_ex1840_013000.bi.bin`): `sub_8027e58` reads the id
from `msg+0x10` after `sub_80270dc` maps timer ids `1000+id` and the
`msg+0x14 == -1` sentinel to 999, then selects a handler with two byte-offset
tables (101..119, 121..130) plus compare tiers for 120 and 131..138. The 16
software timers come from the record table at `0x08051444`
(`{cb, period_ms, name[8], id}`, created by `sub_8026df8`). Ids 117, 127 and 128
have no case: id 128 is timer-driven, so its 500 ms record always lands on the
default path at `0x8027ee4` (log `19013`). Id 111's only producer
(`sub_8028090`) has no code reference, and `sub_8027dbe`'s producer is
unresolved.

Monitor-lane membership (static): 101, 102, 104, 107, 114/124, 115, 116, 119,
120 and 126 reach `sub_802b234`/`sub_802b40c` and/or `sub_802b6f0`. The shared
state cells are `0x2001f5da` (sequence), `0x2001f5b2` (program: 0 idle, 1 armed,
2 dirty), `0x2001f5db` (fault), `0x2001f5dc` (last class), `0x2001f5a1..a4`
(presence debounce) and `0x2001f64c/f64e/f650` (status raw/class/valid).

Presence path (static, pin identity from the LZ-decoded descriptor image at
`0x2000d000`): `0x2001fe24` = GPIOE bit 9, present = low; the rail is GPIOB
bit 2 driven by `sub_802ba44`; a second raw input is GPIOC bit 0. Ten
consecutive low samples trigger the insert sequence: rail on, `0x6b` commit,
`0x0d` read, snapshot update, event `0x0202004d`. The boot probe
`sub_8026674` samples the same pin up to ten times and cannot fail. Because the
whole state block is zeroed BSS, the first poll is always "absent".

Measured (bundle, 240 s windows): only ids 101 and 102 dispatch (about 2.5/s);
software timer callbacks post at a comparable rate; `0x0804b808`
(`current_time = 0`, timer queue re-init, SysTick reload `49999`) runs about
once per second together with the whole thirteen-step init chain (return
addresses `0x0804b5b5..0x0804b615`); and the MCU reset vector `0x080002b4` is
re-entered at the same cadence with the reset stack pointer.

The reset loop is therefore the current blocker, not the monitor lane. Fault
window: a bus fault at stacked PC `0x080001d6` with `LR = 0xfffffffd` — the
exception-return path of the `BASEPRI` lock helpers `0x080001c4`/`0x080001d2`
(`mrs BASEPRI` / `msr BASEPRI`). The AIRCR `SYSRESETREQ` writer that ends the
cycle is at `0x08000606` (VECTKEY `0x05fa`, request bit 2). Next: dump
CFSR/BFAR at the fault, identify the caller that enters the helper with an
exception-return LR, and check whether the guest runs the helper from
unprivileged thread mode.

Byte audit of the monitor file (static, exhaustive immediate and literal scan):
regs 2, 4, 5, 6, 7 have no reader and no expected-value table anywhere in the
image; reg 6 is transmitted only by `sub_802b40c` (never by `sub_802b234`);
reg 7's default is `0x00`, not `0x80`; `0x2001fdae/fdaf` (`cd 06`) have no
reader and must not carry behaviour. Reg `0x0b` states 4 and 6 are the only
exit actions (final commit and finish), which stay unreachable while the model
answers 1/2 and the lane loops in reset.

## The reset loop's cause: an unmapped 0x5006 window {#the-reset-loop-s-cause-an-unmapped-0x5006-window}

Fault window result (bundle, hardware breakpoints). The first BusFault entry
already carries `CFSR = 0x8200` — precise data bus error with a valid fault
address — and `BFAR = 0x50060800`. The stacked frame is
`PC = 0x080001d6`, `LR = 0x080202f9`, `R1 = 0xe000ef00` (NVIC STIR). So the
access to `0x50060800`, not the `BASEPRI` return path, is what raises the
fault; the stacked PC belongs to the return of the lock helper that follows the
software-trigger write.

Static: `0x50060800` occurs exactly once in the image (literal at
`0x08023fb8`). `sub_8023e36` loads it into a stack descriptor
(`str r0,[sp]`) and submits it through `sub_8044c40`/`sub_8044cd2`, which is
the closest thing to a driver contract for that window. Nothing else in the
image encodes a second `0x5006xxxx` constant, so this is one window, not a
family.

The machine maps flash `0x08000000`, SRAM `0x20000000..0x20040000`, the
`0x40000000` peripheral window and the `0xe0000000` system region. The fault is
therefore a real model gap and the reason the guest re-enters the reset vector
about once per second: every pass re-initializes `current_time`, the timer
queue and SysTick, so the software timers never accumulate.

Next: identify the `0x5006` window from the original software contract (its
descriptor use in `sub_8044c40`/`sub_8044cd2` is the only handle) and model it
explicitly — or keep it as an explicit unmapped fault. A read-zero or
discard aperture would hide the fault being measured and is not acceptable
under this repository's rules. Two candidate readings that the data does not
distinguish yet: a vendor SRAM window used as a descriptor ring, or a
peripheral block whose first register sits at `+0x800`.

### Reset loop, second layer {#reset-loop-second-layer}

Modelling the `0x5006` window as
[the channel engine device](../devices/exrng/protocol.md) removes the
precise bus error. The loop continues through a second layer: a usage fault
(`CFSR = 0x20000`, `INVSTATE`) with stacked `PC = 0x0802af00` and
`LR = 0x08020225`, i.e. the RTOS dispatcher `sub_80201fc` executed `blx` on an
even word loaded from the table at `0x20022d70`.

Two measurements narrow it down:

- the normal dispatcher path carries odd thumb pointers — IRQ31 selects
  `0x08041065`, and the first four table words are `0x8041995`, `0x80410f1`,
  `0x80411d5`, `0x804114d`;
- the image does contain the correct odd pointer `0x0802aee9` (literal at
  `0x0802b16c`, inside the handler installer `sub_802b128`), and it contains no
  `0x0802af00` constant at all. So the even entry was computed at runtime, not
  loaded from a table.

Next probe: break at the registrar `sub_80419a4` and print each `(id, handler)`
pair; whichever call passes an even handler writes the offending entry. A RAM
watchpoint on `0x20022d70 + 4*irq` would be the direct alternative, but data
watchpoints in this gdbstub have been unreliable before, so the registrar
breakpoint is the trustworthy route.

### Reset loop closed: boot loader and RNG {#reset-loop-closed-boot-loader-and-rng}

Two model gaps explain this recorded reset loop. Both corrections have scoped
fixture coverage:

1. **Boot data segment.** `0x080002b4` only relocates the vector table. The
   descriptor image at `0x2000d000` comes from a boot loader walking the
   sixteen-byte records at `0x080038cc`. Entries: LZ image
   `0x08051ba8 -> 0x2000d000` (`0x1e10`, decoder `0x08003838`), zero fills for
   `0x20000408`, `0x20000c08`, `0x2000ee10` (`0x141fc`) and `0x2003a000`, and
   two diagnostic images (`0x2003c000`, `0x2003f000`) through an unimplemented
   loader `0x08003894`. Decoding the LZ stream offline reproduced exactly
   `0x1e10` bytes ending at `0x080528e1`, and the recovered descriptors match
   the pin table (`pad 18 -> GPIOB 0x40020400 mask 4`, `32 -> GPIOC mask 1`,
   `41 -> GPIOC mask 0x200`, `64 -> GPIOE mask 1`, `73 -> GPIOE mask 0x200`).
   Without the image, original code read zeroed descriptors and branched
   through non-thumb pointers: usage faults (`CFSR = 0x20000`, INVSTATE) at
   stacked `PC = 0x0802af00` and `0x08000000`.

2. **`0x50060800` is the hardware RNG.** The record path clears CR bits 2/3,
   clears SR bits 5/6 and later sets CR bit 2 — the STM32F4 RNG layout. It is
   now a real register block (control, status with data-ready, data word with
   host randomness); every other offset stays an explicit error.

Measured after both fixes (220 s bundle window): **no bus fault, no usage
fault**, and the reset cadence dropped from about once per second to about once
per minute. The remaining reset is deliberate: `0x8042800` writes AIRCR
`SYSRESETREQ` with no fault pending, reached from the T-Kernel path around
`0x80427xx` (the same call site observed earlier with `lr = 0x8042805`).

Next frontier: identify that deliberate reboot caller and what condition it
tests — it is now the only thing standing between the original image and a
continuous EX run. Everything the monitor lane needs (I2C shift timing, 0x6b
register file, 0x0d status, boot data, RNG) is in place.

### The remaining reboot: a boot-time status check {#the-remaining-reboot-a-boot-time-status-check}

The deliberate reset is now localised. `sub_80426b8` walks a record array whose
base is `*(0x080429b0)`; for each entry it loads the first word and compares it
with 99 (`0x080427c0 ldr r0,[r6]`, `0x080427c2 cmp r0,#99`,
`0x080427c4 bne 0x08042800`), and the mismatch path reaches
`0x08042800 bl 0x08049088`, the AIRCR `SYSRESETREQ` writer. Two further
conditions in the same function (`0x080427d2 cmp fp,#1` after
`0x080427ce bl 0x080478c4`) also branch there.

Measured at the compare: the record is at `0x2003a000` and its first word is
`0x0000000e`, not 99, on every pass. The next words are `0x00082c21` and the
ASCII text `init` / `task` (`0x74696e69` / `0x6b736174`), so this is the
diagnostic block rather than a transfer descriptor. That block is zero-filled
by the boot loader on every reset (`0x2003a000`, length `0xc30`), so the value
is written again during the same boot.

One nearby path shows the intended contract: `sub_8042e90` writes 99 into
`0x2003a000` (`0x08042e9a movs r0,#99`, `0x08042e9c str r0,[r1]`) and then
calls `sub_804cd74` (the reboot machinery) followed by `sub_80426b8` one
argument later. So 99 is the "clean" marker the check expects, and something
during bootstrap is leaving 14 behind instead.

Diagnostic evidence: the log writer `sub_802146c` records a single entry
`id = 17003, value = 0` before the reboot. Its id and the meaning of the
status word are the next static targets: find the writers of `0x2003a000`
beyond the two known ones (the fault reporter at `0x08020862` stores 3, and
the clean marker above stores 99) and decode what 14 represents.

### The reboot is a strap check failure (code 14) {#the-reboot-is-a-strap-check-failure-code-14}

A hardware-watchpoint run on `0x2003a000` names the writer directly:
`0x08042d86 str r5,[r0]` in `sub_8042d70`, with `r5 = 0xe`. The handler is
entered at `0x08042d74` from `0x08032c76`, so the caller passes the code
explicitly:

```
8032c5a bl 0x8037f80        ; strap check
8032c5e cbz r0, 0x8032c82   ; healthy -> clear counter
8032c60 ldrb r0,[r4,#3]     ; failure counter
8032c68 cmp r0,#3
8032c6e movw r0,#20001      ; log id 20001
8032c7a movs r0,#14
8032c7c bl 0x8042d74        ; fatal(14)
```

and the fatal path writes AIRCR `SYSRESETREQ`. So the loop is: strap check
fails three times in a row, the firmware logs `20001`, records 14 and reboots.

`sub_8037f80` fails when `(sub_8038a98() & 3) == 3` **and**
`(sub_8038bbc() & 4) == 4`. Both helpers walk descriptor tables
(`*(0x8038ad8)` and `*(0x8038adc)`), read each pin through `sub_8043acc`
(descriptor `+8` GPIO base, `+12` mask) and compare it with a byte from a ROM
table, building a match bitmap. Measured: set A has six pins, all reading 0;
set B has four pins reading 1, 1, 0, 1. The failure condition is therefore a
*specific match pattern* between live GPIO levels and the ROM table, not a
missing value.

Next: resolve the two descriptor tables and the ROM expected bytes, then decide
whether the machine must drive those straps (explicit input) or whether the
comparison's meaning rules out that pattern by construction.

### The strap check is a pull-up input test {#the-strap-check-is-a-pull-up-input-test}

The two descriptor tables are resolved. `sub_8038a98` uses the pointer array at
`0x2001f224` with expected bytes at ROM `0x0805138c` (six entries), and
`sub_8038bbc` uses `0x2001f214` with `0x0805135c` (four entries). Every expected
byte is `00`, and the ROM record's first word is the pad number.

| Set | Pad | GPIO | Mask | PUPDR | Model read |
|---|---|---|---|---|---|
| A | `0x0a` | A `0x40020000` | `0x400` | pull-up | 0 |
| A | `0x11` | B `0x40020400` | `0x0002` | pull-up | 0 |
| A | `0x1f` | B `0x40020400` | `0x8000` | pull-up | 0 |
| A | `0x1b` | B `0x40020400` | `0x0800` | pull-up | 0 |
| A | `0x1c` | B `0x40020400` | `0x1000` | pull-up | 0 |
| A | `0x53` | F `0x40021400` | `0x0008` | pull-up | 0 |
| B | `0x25` | C `0x40020800` | `0x0020` | pull-up | 1 |
| B | `0x61` | G `0x40021800` | `0x0002` | pull-up | 1 |
| B | `0x62` | G `0x40021800` | `0x0004` | pull-up | 0 |
| B | `0x56` | F `0x40021400` | `0x0040` | pull-up | 1 |

All ten pads are configured as inputs with pull-up (MODER 0, PUPDR 1), and the
comparison treats a read of 0 as a match. The failure fires when A0 and A1
match and B2 matches, i.e. **PA10, PB1 and PG2 all read low**.

That is not what a pulled-up, undriven pin does on real hardware. The pinned
GPIO model already implements it correctly: `disconnected_pins` defaults to
`0xffff`, and a disconnected input with `is_pull_up` reads 1. So some model
path is *driving* these pads low (they are connected rather than floating) —
either the pad frontend or the board input record.

Model fix, in order of preference:

1. leave the ten pads disconnected so the pull-up applies (matches the register
   configuration the firmware itself wrote);
2. only if the board profile genuinely names them, drive the levels the profile
   declares — never invent a strap value to dodge the check.

With those three pins reading 1, the match pattern changes, `sub_8037f80`
returns 0, the failure counter clears and `fatal(14)` never runs.

All ten strap pads are host-key frontend pads:
`nikon_input_gpio_ids = {0x0a, 0x11, 0x1f, 0x25, 0x1b, 0x47, 0x4d, 0x5b, 0x5e,
0x61, 0x62, 0x6d, 0x6e, 0x6f, 0x1d, 0x53, 0x1c}` covers A0..A5 and B0..B2;
only `0x56` (PF6) is outside it. The frontend drives `!down` (`nikon_input_event`,
`nikon_input_publish`, `nikon_input_unrealize`), i.e. **1 when the key is up**, so
an idle board should read high and the check should not match at all.

Measured 0 on those pads, while MODER 0 and PUPDR 1 are what the firmware wrote.
So either the frontend line never reaches the GPIO model (wiring/reset ordering
between the frontend realise, the machine reset that replays pads, and the GPIO
cold reset) or the replay path overwrites it. That single discrepancy is what
still drives `fatal(14)`; next probe: read the frontend pad line and the GPIO
IDR at the same moment for `0x0a`, `0x11` and `0x62`.

### Power-good polarity was the fatal-14 trigger {#power-good-polarity-was-the-fatal-14-trigger}

The QOM query named the driver: `disconnected-pins` on GPIOA/B/C/F/G had exactly
the strap bits cleared (`PA10`, `PB1`, `PB15`, `PB11`, `PB12`, `PF3`, `PG2`,
`PC5`), with `pins-connected-high = 0`. That is `z7ii_ex_input_tick`, which
declared those seven pins as power-good wires and drove them **low** every
millisecond.

The firmware treats exactly that pattern as a rail fault: `sub_8037f80` fails,
three failures log `20001`, `sub_8042d74` records `14` in `0x2003a000` and
writes AIRCR `SYSRESETREQ`. Driving the declared wires according to
`power_good` (high while good) makes the check return 0 for every sample in a
200 s window and removes the `14` path.

What appears next is a second ladder: the state block at `0x20013d18` reports
`+5 == 1` and `+6 == 2`, and `0x8032378` logs `20002` and records status `15`
(`+6 == 1/2/3` maps to `12`/`15`/`16`). Next question: what fills `+5`/`+6` on a
clean boot — most likely a reset-reason decoder reading RCC control/status or
the diagnostic block, which would mean the ladder is being fed by the very
reset it causes.

### The second ladder is a handshake countdown, not a fault {#the-second-ladder-is-a-handshake-countdown-not-a-fault}

A watchpoint on the state block word `0x20013d1c` (bytes `+4..+7`) names every
writer in one boot:

```
0x80325b0  word 0x01000000  (+7 = 1)
0x80323d6  word 0x01020000  (+6 = 2)   reason recorded
0x8032cc8  word 0x01020100  (+5 = 1)   escalation armed
0x80038be  word 0             boot loader zero fill
```

Two functions form the ladder.

`0x8032378` is the verdict routine (`r4 = state block 0x20013d18`):

```
[r4+5] != 1            -> 0x80323a4 (no verdict)
[r4+5] == 1            -> log 20002 with [r4+6]
[r4+6] == 1/2/3        -> fatal(12) / fatal(15) / fatal(16)
```

`0x80323c8` records the reason: `bl 0x8033b48`, `[r4+3] = 1`, `[r4+2] =
*(0x20013bc8)`, `[r4+6] = 2`.

`0x8032c84` runs the countdown:

```
if ([r4] == 1) { if (--[r4+1] == 0) { [r4+1] = *(r4-0x151); bl 0x80214fe; } }
if ([r4+3] == 1) { if (--[r4+2] == 0) { [r4+3] = 0; [r4+5] = 1; ... } }
```

So the block arms a countdown (`+3 = 1`, `+2` loaded from `0x20013bc8`) and, when
it expires, sets `+5 = 1`; the verdict routine then sees reason 2 and escalates
to `fatal(15)`. That is a **power-on handshake that never completes**, not a
memory or transfer fault — the same shape as the earlier monitor-lane waits,
one layer up.

Next probe: log `[r4]`, `[r4+1]`, `[r4+2]`, `[r4+3]`, `[r4+5]`, `[r4+6]` at
`0x80323c8` (arm) and `0x8032cbe`/`0x8032cc8` (expiry) to get the countdown
length, and step into `bl 0x8033b48` to see which handshake was armed. The
countdown value at `0x20013bc8` is the concrete deadline to compare against the
lane it waits for.

### Why the handshake is armed: the submit returns an error {#why-the-handshake-is-armed-the-submit-returns-an-error}

Arming is not unconditional. At `0x80323c8` the block is empty
(`+0=1, +1=0x0a, +2=0, +3=0, +5=0, +6=0`) and the value returned by
`bl 0x8033b48` is **`0xffff0301`**. The countdown then loads `0x20013bc8 = 0x0a`
and the trace shows it stepping `0xa, 0x9, 0x8, ... 0x5` with `+3 = 1`, so ten
ticks later `+5` becomes 1 and `fatal(15)` fires.

`sub_8033b48` builds a descriptor (`command 11`, one copied status byte, three
callbacks) and submits it through `sub_8033958`, which is a thin indirect
dispatch:

```
sub_8033958: if (desc == 0) return 0xff; else
             r0 = desc; r1 = *(0x200138d4 + 12); bx r1
```

So the service behind `0x200138d4` returned `0xffff0301` and the caller armed a
ten-tick retry/handshake instead of treating the reply as success.

Next: identify the registered handler behind `0x200138d4 + 12`, decode
`0xffff0301`, and find which model-provided value it rejects. That handler is
the last thing between the original image and a continuous EX run — every fault
layer below it (RNG, boot data, strap/power-good polarity) is now closed.

### The failing service is the diagnostic log ring {#the-failing-service-is-the-diagnostic-log-ring}

`sub_8033958` dispatches through `*(0x200138d4 + 12)`; the watchpoint on
`0x08033966` names that pointer as **`0x08032000`**, which is the diagnostic
log-ring module. It was called with four descriptors in the observed window:

```
cmd 0x1c  value 0x200139da
cmd 0x6a  value 0x200139fa
cmd 0x05  value 0x20013bb8
cmd 0x01  value 0x200138ea
```

`sub_8032000` starts with a capacity check:

```
r0 = desc[0];        r1 = *(0x20011754)
if (r0 >= r1) { log 11001 with (idx, count, value); return 0xff00 | ... }
```

**Correction to the first reading:** the capacity is initialised. A watchpoint
on the byte sees `0x8031cec` write `cap = 0x12` (18) before any dispatch, and
three subsequent dispatches carry indexes `1`, `0x10`, `6`, all below the
capacity, so the check passes. The zero is therefore not the cause.

What is still open is where `0xffff0301` comes from: `sub_8032000` has more
error paths after the capacity check, and the caller (`sub_8033b48`) composes
its own status. Next: single-step the ring module for the `cmd 0x1c` call (the
first one) and record which branch returns the error, instead of assuming the
capacity gate.

### Correction: `0xffff0301` is a packed ring ack, not a plain error {#correction-0xffff0301-is-a-packed-ring-ack-not-a-plain-error}

Reading `sub_8032000` to its returns changes the earlier interpretation. The
success path stores the descriptor and returns

```
r5 = (counter & 0xffff) | (kind << 8)      ; 0x803209e
```

so `kind = 3`, `counter = 1` yields `0x0301`. The `0xffff` half comes from the
caller (`sub_8033b48`), which combines the ack with its own high word, and the
value observed at `0x80323c8` (`0xffff0301`) is therefore the enqueue ack, not a
transport failure.

That also matches the caller's own branch:

```
0x8033b8a  r0 = ack - 0xFF00 - 0xFF
0x8033b90  beq 0x8033b96      ; only an exact 0xffff takes the retry path
0x8033b92  [r5+4] = 1         ; otherwise: success marker
0x8033b96  [r5+4] = 0xf0, [r5+5] = 10, log 20011   ; 10-tick retry arm
```

With `0x0301` the success branch is taken, so the retry arm at `0x8033b96` is
**not** what raises `fatal(15)`.

The latch is armed by the other function: `0x80323c8` records reason `2` in
`[r4+6]`, sets `[r4+3] = 1` and loads the countdown from `0x20013bc8` (`0xa`);
the 1 ms tick at `0x8032c84` decrements it and sets `[r4+5] = 1` when it
expires, after which the verdict routine at `0x8032378` escalates. So the next
question is what that recording path is waiting for and who calls it, not
whether the enqueue succeeded.

### The latch reads a "cmd 11 already submitted" marker {#the-latch-reads-a-cmd-11-already-submitted-marker}

The condition cell is `0x200138ee`, and the watch names its only writer:
`0x8033b94` inside `sub_8033b48` (`strb r6,[r5,#4]` with `r5 = 0x200138ea`).
So the marker means "the ring entry with index 11 has been submitted".

The verdict path is then exact:

```
0x80323aa  r0 = 0x200138ea; r1 = [r0+4]        ; the marker
0x80323ae  cbz r1, 0x80323c4                   ; not submitted yet -> submit + arm
0x80323b0  pop; log 20017                      ; already submitted -> nothing
0x80323c4  bl 0x8033b48                        ; submit index 11, sets the marker
0x80323d0  [r4+2] = [0x20013bc8] (= 0xa)       ; countdown
0x80323d2  [r4+6] = 2                          ; reason 2
0x80323d4  ...
```

`sub_8033b48` has exactly **one** caller: this arm path. So in this image the
marker can only become set from inside the arm, which means the first pass
always takes the arm branch, submits index 11 and arms reason 2 with a ten-tick
countdown. Nothing in the image clears `[r4+3]`/`[r4+6]` afterwards — the only
other writer of that word is the tick at `0x8032c9a`, which decrements a
separate reload byte `[r4+1]` (watched stepping `0x0a -> 0x09 -> ...`).

Two facts therefore frame the next step:

- the block holds two independent counters, `[r4+1]` (periodic reload, `0xa`)
  and `[r4+2]` (the ten-tick escalation countdown);
- the stage bytes `[r4+0] = 1` and `[r4+7] = 1` are written early
  (`0x803247a`/`0x803247c` and `0x80325b0`), and the arm requires `[r4+0] == 1`.

So the question is no longer "which service failed" but "which boot stage is
supposed to advance `[r4+6]`/`[r4+3]` before the ten ticks elapse". Next probe:
watch the four bytes `0x20013d18`..`0x20013d1b` (stage + counters) through a
whole boot, and break at the caller of the verdict routine to see its period.

### The EX domain now runs; one ladder reset remains {#the-ex-domain-now-runs-one-ladder-reset-remains}

State after the RNG window, the boot scatter loader, the power-good polarity
fix and publishing the board record at attach time (all on `main`, PRs
the separately scoped EX startup observations):

- the T-Kernel module initialisation runs **once** (`0x804b808` entered a
  single time in a 180 s window), so the old re-init loop is gone;
- the tick advances monotonically: `0x7cf, 0xf9f, 0x176f, ... 0x98ab` for
  60,000 increments, one increment per SysTick;
- the host command lane is alive and varied: ids `101`, `102`, `103`, `131`,
  `1114`, `1115` (timer ids 114/115 now reach their handlers), more than
  16,000 dispatches and 10,000 timer posts in a 240 s window;
- the monitor lane keeps running: 30,000 `0x0d` service entries and repeated
  `0x6b` equality edges in the same window.

One deliberate reset remains, and it is the ladder described above:

```
ARM  (0x80323c4)  tick 0x2aa2
FATAL(15) at 0x8042d86, return address 0x80323a5, tick 0x51b2
MCU reset
```

so the arm happens about 10,000 ticks after boot and the escalation about
10,000 ticks after the arm (the ten-unit countdown is therefore driven by a
1 kHz divisor, not by SysTick directly). After that reset the observed windows
show no second arm, but the wall time available after the reset was not always
enough to reach the same tick count, so "does the second boot re-arm" is the
open measurement. It needs a steady run that spans at least ~25,000 ticks after
the reset with a single reset breakpoint (the multi-breakpoint runs detach
early in this gdbstub).

### The ladder is a cold-boot transient {#the-ladder-is-a-cold-boot-transient}

A 600 s window with a single breakpoint shows exactly one escalation:

```
FATAL n=1  code=0xf  return 0x80323a5  tick 0x51b4
```

and a 420 s window with three breakpoints shows the block initialisation and the
arm only on the cold boot:

```
BLOCK_INIT n=1  tick 0x3e0
ARM        n=1  tick 0x2aa4
RESET      n=2                      ; the escalation, then nothing further
```

After that reset the second boot runs the rest of the window (minutes of wall
time, tens of thousands of ticks) **without re-initialising the block and
without re-arming**, so the escalation is a one-time cold-boot behaviour in this
model rather than a periodic loop.

The open question is resolved: the block is armed by a *boot step*, posted
once, and the escalation is fatal by construction. The mechanism, from single
breakpoint and write-watchpoint windows:

- the step table at `0x0804E160` maps step codes to handlers
  (`01→080333D8, 04→0803350C, 06→08033498, 08→08033398, 0A→08033418,
  0C→080333F8, 0D→080333B4, 10→08033438, 12→08033328, 20→0803347C`) and its
  step `0A` handler copies an eight-byte descriptor into `0x20013B54` and
  posts mask `0x00010000` to the event block `0x200138F8`;
- that mask selects handler `0x08032451` in the same `{mask, handler}` table
  (`0x0804DF64`) as the verdict, and `0x08032450` is the block initialisation.
  A single hardware breakpoint on it fires once, at tick `0x3DD`, from the
  dispatcher;
- the verdict is posted by the reload tick whenever `stage == 1`; the first
  post arms and records reason `2`, and the arm reloads the ten-invocation
  countdown from the descriptor word (`0x010A0A01` at `0x20013B54`, copied at
  tick `0x3DA`);
- the done byte `0x20013D1D` has exactly one observed writer, the tick expiry
  at `0x8032CC6` (60 s write watchpoint). On expiry the verdict always
  escalates by reason (`1/2/3 → 12/15/16`), so a completed cycle is fatal by
  construction, not just the armed one;
- the verdict's skip guard is `[0x200138EA+4]`, and the arm's own record
  builder `sub_8033B48` sets that byte to `1` (`0x8033B92`). The guard can
  never fire before the first arm, so the first cold-boot post always arms;
- after the reset neither the step-`0A` copy nor the block initialisation is
  reached again. A 100 s post-reset window contains no verdict invocation and
  no block-initialisation entry; a 90 s window enters the MCU reset vector
  twice in total (attach entry, escalation).

The remaining "why" is which cold-only gate keeps the boot step from re-running
after our RAM reset (a reset-cause distinction or a boot-stage flag); the
observable behaviour is already "one deliberate cold-boot reset, then
continuous" and the bundle deliberately synthesizes no management reply. The
resolved write-up is in `docs/runtime/ex.md` on `main`.

Accepted criteria for the continuous-run goal, measured on this build:

| Criterion | Evidence |
|---|---|
| initialisation chain runs once | `0x804b808` entered once in 180 s |
| monotonic time base | tick `0x7cf, 0xf9f, ... 0x98ab` over 60,000 increments |
| periodic software timers dispatch | ids `1114`, `1115` and 10,000 timer posts |
| monitor lane stable | 30,000 `0x0d` services, repeated `0x6b` equality edges |
| no periodic reset | one cold-boot escalation then continuous in 600 s |
