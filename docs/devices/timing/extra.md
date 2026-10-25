# Finite extra-feature timing

## Source and observed caller

All code addresses are C1.70 MAS VA. Register addresses below are board PA;
A/B are `2c010000` / `2c011000`, with channel n at `100*(n+1)`.
The source exposes the extra feature on channels0..4, independently of compare.
Low setter `1036e996` writes boolean enable to Bchannel+38, the second
packet word to Bchannel+3c, then ORs8 into Achannel control. Its disable
branch writes enable0. The adjacent `1036e9d6` setter and `1036e9e6` getter
access configuration3c and result40. These interfaces establish the register
geometry and full-word configuration, not the physical result production rule.
Offset34 has no recovered consumer and remains unsupported.

The read-only original probe captures this chain:
`1039a3b2 -> 10394ec8 -> 10371828 -> 10371704 -> 1036e996`.
The one-shot wrapper `10371828` sets the extra argument to1 and installs the
primary callback. Generic `10371704` supplies divider49 on its non-direct
branch and encodes period minus one; helper `10371a96` converts nonzero N
to `[1,N]` and zero to `[0,1]`. The observed channel is4, callback
`1039578d` (Thumb VA), packet `[1,1]`, and requested period `ff2687e4`.
This is a basic timer, not the `103718aa` compare/PWM branch. That separate
branch can also configure extra, so the model must not require compare enable.

Upstream `1039a3b2` obtains `500 * *(b1868ae4_pointer+288)` from
`102cb65e(42)`, with32-bit multiplication. `10395bc4(...,7)` replaces its
local64-bit value with the nonnegative difference between the `103a30f0`
getter and saved baseline7, clamped to0 when bit31 is set. The caller checks
their unsigned ordering and passes the low32 remaining difference to the
wrapper. It then waits on `80000000` in the object
at `b186832c`, finally calling `10371a3c` to stop the timer. The captured raw
delay is preserved: with divider50 and the nominal3GHz clock it requires about
71.35 virtual seconds. No clamp, instant expiry or guest arithmetic correction
is introduced. The field producer is `102cb648 -> 103a47a2(31) -> 103bfaa2
-> 102486f6`. Initializer `102e9bf0` invokes it after stopping three timing
channels. It installs the resource payload without a body check or default
selection; selector42 directly reads payload+288 and multiplies by500.
The integration snapshot confirms MAS VA pointer `b7f55900`, raw `ffffffff`
and scaled `fffffe0c`. The declared descriptor17 input's complete phase2
resource31 body is erased; its source range and cross-model limits are recorded
in the [input contract](../../inputs/nor/descriptor.md#resource31-timing-parameters).
No normal raw value has been recovered or substituted. `102cb65e(42)` is a
parameter getter, not a clock read.

Low ISR `1036ea8a` reads Achannel+10 and writes the value back. Higher
dispatcher `10371672` maps events `410a1801..0c` to channels0..11;
`status & 2f` calls the primary callback and `status & 10` calls the distinct
compare callback. Channel4 therefore uses event `410a1805`, board IRQ176.
Callback `1039578c` ignores status and calls `103610cc(b186832c)`, which
reaches the normal event setter `10104844`. Neither that callback nor control
bit3 proves the hardware's terminal status encoding. No host calls these guest
methods or writes their software event.

## Functional assumptions and finite contract

The single-shot producer motivates a finite **period budget**, not an
unbounded register file. The following choices are explicit functional
assumptions, not recovered physical behavior:

* B+38 retains boolean enable; B+3c retains budget N; B+40 reports completed
  periods, initially0. The budget is armed only with enable1 and Acontrol bit3.
  N0 is staged/unarmed and cannot complete. Basic control9 and compare control8
  use their existing independent run gates.
* Each actual elapsed divided period increments result, bounded by N. Existing
  basic bit0 or compare bit4 still reports elapsed periods. Reaching N also
  latches bit3, stops the counter and deasserts compare output. Thus terminal
  basic status is9 and compare status18. Enable/control/configuration remain
  readable after stopping. The result direction, terminal bit and stopping
  behavior require future hardware evidence for physical qualification.
* W1C clears pending causes without rearming. Reads/configuration/enable never
  create an IRQ. Enable1 (including repeated1), configuration writes and period
  programming reset result/rearm; enable/config preserve partial-period phase,
  while period programming resets it and clears feature pending bits1..4.
  Existing basic pending bit0 remains independently acknowledged.
* Master/shared/local pauses retain source ticks, divider phase and result.
  Extra disable preserves result/configuration and resumes the ordinary
  basic/compare periodic profile if its other gates permit. Reset cancels the
  timer and clears all extra state/pending without completion. Unrealize has
  the same cancellation boundary, including objects retained before finalize.

The implementation uses existing QEMU clocks, timers and channel IRQs. It never
multiplies period, divider and N together: a64-bit period duration suffices,
elapsed periods are bounded by remaining budget before updating the32-bit
result. Configuration accepts the complete uint32 field, including zero and
`ffffffff`. Unsupported channels, widths, boolean values, control bits,
result writes and offset34 reject before mutation. Initial-count submission
with extra enabled remains unsupported, including stopped/exhausted channels;
the known producers load before arming optional features.

## Native evidence

`run timingextra/native-red.log` records the old model's actual
failure on enable1 readback in `/arm/nikon/timing-extra/budget`. After the change,
`make check MTESTARGS=nikon_timer` initially passes all19 subtests in4.31s
(`native-green.log`). Independent review then identifies missing unrealize
cancellation; `unrealize-red.log` reproduces the retained IRQ. With the reset
hook applied to unrealize, `native-final.log` passes all20 subtests in4.64s.
Eight new cases cover all five capable channels, N1/N3,
real elapsed-period IRQs/result progression, basic/compare terminal status and
output, gate/phase retention, zero staging, rearm independent of W1C, disable,
channel/global/system reset, unrealize/realize reuse, negative accesses,
initial-load rejection and
period/budget32-bit limits. The fixture is a synthetic ARM loop, not original
startup or physical clock evidence. Source/probe records remain in the same
ignored receipt directory.

## Unchanged-firmware continuation and limit

The `final` run preserves the previous complete bundle and explicit TEMP,
descriptor18/resource57, resource12, acceleration `(0,0,1024)`, fixed RNG0,
cold F4, five-CPU MTTCG and normally paced9600-baud healthy UART profiles.
No guest bytes, event cells or callbacks are modified. The110-host-second
window ends with one first uncovered access: CPU1 PC `106ee3c6`, WRITE32
PA `201810e0=1`, `transfer-stream-truncated`. The complete new command pair
is1232/1416 bytes at PA `bd0e0620` / `bd160860`.

That graphics rejection globally stops virtual execution **before** channel4's
long period elapses. Snapshot control is9, divider31, period `ff364cc3`,
phase `23b824`, extra enable/configuration1/1, result0 and pending0.
There is no observed `410a1805` dispatch, `1039578c` callback or wait return
at `1039a408` in this initial run. It establishes enable/configuration
acceptance only; the final run below separately reaches the terminal path.
Application bytes are01/03, primary request0, BatCk `00010001` and GUI gate,
pending3 and display-ready0. The UART peer rejects no request. This is not
a GUI first frame, physical-clock qualification or complete boot receipt.

After the parallel [optional rectangle-prefix extension](../transfer/prefix.md),
`integration` advances parsing of that same1232/1416-byte pair to the existing
`transfer-rectangle-context-not-established` guard, CPU0 at the same START.
It does not complete the pair. Channel4 still has enable/configuration1/1,
result0 and pending0; no original terminal callback is observed. Resource31's
erased field is an input dependency distinct from this graphics context guard.

The corrected shared720 context then permits the original A8 keys7/8 followed
by rectangle9. Final `validated` execution runs110 host seconds without any
uncovered access; QMP remains running before its read-only snapshot. Original
dispatch `10371672(410a1805,9)` reaches `1039578c`, which publishes the
normal event. Waiter return `1039a408` has R0=1. The guest's ordinary stop
clears channel4's shared enable (final bitmap20). Extra result40=1, pending0,
control9 and extra enable/configuration1/1 remain readable. This verifies
that original software consumes the finite model's completion, not that its
status encoding or result direction match physical hardware.

The last timer period is `ff3468ba` plus one, with divider50. Resource31's
field remains erased. Final manager is current3/target5/mode1/state2,
application01/03, primary0, BatCk1 and GUI gate/pending3/display-ready0.
Graphics continues beyond the former1232/1416 pair. There is no new uncovered
access or identified blocked task within this window; the remaining work is
the software readiness/first-frame route. The UART peer rejects no request.
