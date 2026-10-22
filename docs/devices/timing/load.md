# Shared value and channel commit

The later [capture-control correction](../sensor/control.md) resolves the next
mask stop recorded below. The new original boundary is a buffer clear at
VA36d59800 / PA62059800; timing receipts below retain their fixed scope.

## Current divider and load contract

The byte at Bchannel+4 is a divisor-minus-one field, not an opcode selecting
periodic, one-shot or free-running execution. The old numeric0/1/31 mode split
is removed. All code addresses below are original C1.70 MAS VA; A/B denote
the board PA windows `2c010000` / `2c011000`.

The complete low setter `1036e7d4` has two direct callers: configuration
`10371704` and compare configuration `103718aa`. Both form the same20-byte
packet and reset the channel before programming it. The low setter writes
the packet's **unsigned byte+8** to Bchannel+4, word+4 to period, and optionally
stages word+0 at B+4 before submitting `1 << channel` at B+8 under lock
`b18063a8`. Optional capture/compare setup follows the initial-count load.
No load operation selects one-shot behavior or directly starts the channel.

The higher configurator `102e7ce4` computes `50 * duration / divisor`, tries
divisors1/50/250, and stores `divisor - 1` in that byte. It loads the complement
of the calculated delay when the count fits, otherwise uses a bounded repeating
period. Generic `10371704` copies the raw byte on its direct branch; its other
branch supplies49 decimal. `102e8630` also supplies raw3 for a background
counter, while `1038326a` supplies raw1 for capture setup. These independent
producers disprove interpreting the numeric values as execution modes.

| Source encoding | Counter divisor | Initial-count submission |
| --- | --- | --- |
| `00` | 1 | Supported |
| `01` | 2 | Supported before optional capture is armed |
| `03` | 4 | Supported; separately sourced background-counter value |
| `31` | 50 | Supported; generic default and current channel7 request |
| `f9` | 250 | Supported; duration configurator's third choice |
| Remaining unsigned-byte values | value+1 | Same finite raw-byte interface |
| Values above `ff` | Unestablished bits | Rejected without mutation |

The complete byte interface supports all256 encodings without a list of
startup-specific values. Physical behavior for every encoding has not been
measured; the divisor interpretation is source-backed functional semantics,
and the unchanged nominal3GHz input clock remains an explicit virtual choice.
No new address, channel, capture mode or external signal is introduced.

B+4 stages one raw32 operand. B+8 consumes it for exactly one of the twelve
configured, stopped, reset-deasserted basic channels, with global reset clear,
capture/compare disabled and count<=period. This rule is independent of divider.
Zero count, terminal count and period `ffffffff` are included; period+1 uses
64-bit arithmetic. Invalid submission preserves the old counter and staged
operand for a valid retry. A successful load consumes staging once and changes
no enable, pending cause or IRQ. Channel/global/device reset cancels staging.
Multi-channel masks, absent staging, unconfigured/reset/running channels,
enabled optional features, invalid widths and out-of-period counts still fail.

Counters and compare/capture samples advance at input-clock ticks divided by
value+1. Internal retained source ticks preserve a partial divider cycle across
gate pause/resume and same-value divider writes. A stopped divider change keeps
whole counts and clears fractional divider phase, a functional reprogramming
convention; running changes and armed-capture divider changes reject. At
period+1 the counter wraps, latches the existing threshold/compare pending bit
and continues. Subsequent wraps use the full period, not the initial operand.
The original channel7 caller stops its timer through `10371a3c` after its waiter
returns; the device does not manufacture a one-shot stop for raw31. Existing
W1C, reset cancellation, compare gates and capture-profile bounds remain.

## Native checks and original continuation

Parent-owned receipts are under `work/runs/timingmatrix-20261022/`. The same
unchanged-bundle normal-request run fails its original channel7 submission
assertion before the change (`red.py`, `red.stderr`). The new native divider/load
matrix also fails against the old model, reading0 instead of2900 after the
raw1 load (`native-load-red.log`). The earlier `native-red.log` has a mistyped
test selector and is not a runnable device-red result.

The final `make check MTESTARGS=nikon_timer` passes one executable/all12 cases
(`native-validated.log`). Its raw-byte matrix checks all256 encodings distributed
across all12 channels, load readback, scaled first expiry, real IRQ/W1C and a
second full-period expiry. Additional coverage includes fractional pause/resume,
same-divider writes, running/out-of-byte rejection, counts0/period, periods0/1/
`ffffffff`, invalid-submit retry, capture sampling through its named input,
compare timing, existing optional-feature rejection, pending preservation and
channel/global/system reset cancellation. Intermediate fixture errors at an
exact deadline and a zero-nanosecond qtest clock step are recorded separately;
they do not establish device regressions.

Post-change `green` and `next` original runs keep the same bundle, TEMP type3
paired NOR inputs, raw acceleration `(0,0,1024)`, fixed RNG0, healthy class1 UART
peer and held cold F4. Only the final GDB snapshot pauses execution. The
targeted TCG trace joins channel7 `10371704(7,b18a7670)` with LR `102d6fbd`,
the actual load `fffff45b`, period `fffffffe` and raw divider31. Its remaining
2980 counter ticks require149000 nominal input ticks, about49.67us at the
declared virtual clock. Original ISR dispatch reaches `10371672` with
`410a1808/status1`; the source callback `102d6e92` publishes the original event
flag. The trace crosses all seven channel7 waits in `102d6f7c` and reaches
its return sequence at `102d7232/102d7234`.
No host callback or software event is supplied. Source initial delays in that
sequence are2980/2980/380/1980/380/9980/80 counter ticks.

The next first uncovered access is CPU1 PC `1020a514`, WRITE32 board PA
`20068000=fffffd40`, `capture-control-access-or-reset-value-not-supported`.
The joined original call is `10239740` at `10239756` → tail wrapper `1020c042`
→ `1020a50c(b1806814,fffffd40,0)`, LR `1023975b`. This is the complement of
selected mask `2bf`; the current capture-control decoder allows only `fff`
at this offset and rejects the upper bits. The complete mask/enable interface
and its consumers are the next dependency, not another timing opcode.

BatCk1, power1, application bytes02/01 and primary request state1 remain;
the UART peer rejects no request. GUI gate, pending3, display-ready and sampled
surface words remain0. No GUI first frame, scanout, whole boot or physical
camera clock behavior is verified.

## Historical channel9-only checkpoint

The earlier finite model accepted the original channel9 initial-count load. High
caller10371704 receives channel9, configb1868834 and LR102e7daf. Runtime
configurator102e7ce4 supplies `~13000`; ROM124027c8 contains flag1, zero
configuration and callback102eb749. Setter1036e7d4 resets/stops the channel,
programs numeric mode0 and periodfffffffe, then locks b18063a8, writes
ffffcd37 to shared B+4 at1036e812 and mask200(hex) to B+8 at1036e81a.
The getter1036e86c reads the selected channel's B+c. Elapsed accumulation
102e822c combines overflow*(period+1)+count, supporting an up-counter load:
fffffffe+1 minus ffffcd37 leaves13000 ticks to the first threshold.

At that checkpoint B+4 staged one raw32 operand; B+8 consumed it only for one configured,
stopped, reset-deasserted channel in numeric mode0, without capture/compare,
with count<=period and global reset deasserted. Invalid submission preserves
the prior counter and staged operand for a valid retry. Staging alone starts
nothing; submission changes no enable, status or IRQ. Any channel reset or
device/global reset cancels staging, a conservative functional constraint.
Existing gates start and pause the real QEMUTimer. Initial load is once-only;
subsequent mode0 expiry wraps to zero rather than reloading the operand.
Auto-reload behavior is not established by the recovered source.

The original wrapper's Bchannel+4 values0/49/249 are divisor-1, derived from
divisors1/50/250. The existing virtual opcode labels below are not recovered
hardware meanings. That historical extension admitted numeric0 only and did not add
divider or optional-feature behavior. Nominal3GHz remains a virtual assumption.

`work/runs/timingpreset-20261025/red.log` records the old model failing the
initial-count assertion. `green.log` records all eight native timing cases
passing, including expiry/IRQ/W1C, pause/resume, illegal submits, staged and
running reset cancellation, and preservation of pending IRQ. The unchanged
bundle with the same functional descriptor and normal cold F4 input reaches
original callback102eb748 with r1=1 through the original ISR; no callback or
software event is injected. `frontier.json` retains the joined run. The next
actual error is fill engine0 at PA20070010, described in the
[current startup frontier](../../validation/frontier.md).
