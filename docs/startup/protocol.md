# Functional model for original startup

The machine follows firmware software protocols with explicitly declared
virtual reset values and completion timing. Historical strict-unknown-state
receipts remain tied to their original commits, not relabeled as evidence
for the functional model. Scope:
[system control/clock](https://github.com/mashiro01/nightglass/issues/330) and
[RTOS startup dependencies](https://github.com/mashiro01/nightglass/issues/331).

## Completion and system-control contracts

Completion PA04287000 uses one coherent virtual reset: request, done,
IRQ-enable and applied-domain words are0, representing an inactive virtual
domain with no completion and notifications disabled. After original software
writes its request and CMD=a1, an official QEMU BH applies start/stop and
sets done; the guest IRQ-enable controls actual GIC SPI250. Offset+c returns
applied state, with other bits reserved0 for this profile. Status reads do
not complete requests. Original W1C and GIC EOI remain guest-owned; no host
RTOS flags or handler calls are introduced. Physical reset, domain identities
and latency remain unrecovered. The obsolete unknown-reset path is removed.

System control PA04281000 retains three control words for each of the original
nine PLL records, six stop-bit groups and SET/CLEAR aliases. Reset+28=ffe,
+2c=73,+30=3 is the virtual stopped/output-gated convention. Six gate-bank
states reset0, meaning their gates are not stopped in this profile. These
are not measured Nikon/BootROM resets. Software must still perform RMW,
dependency checks, stop and output-gating operations. The model does not
clear other groups or interpret gate-bank4 offset48 as PLL lock.

Each output uses upstream
[Clock](https://github.com/qemu/qemu/blob/4fc49f46dc95d4a27de2509e7fceb2931e91faeb/include/hw/core/qdev-clock.h)
and [virtual timer](https://github.com/qemu/qemu/blob/4fc49f46dc95d4a27de2509e7fceb2931e91faeb/include/qemu/timer.h)
APIs. Clearing group stop schedules10us virtual lock; output becomes nominal
48MHz only after software also clears output gating. Stop cancels the timer
and output. Frequency and timing are functional assumptions, not derived
from multiplier163. There is no independent lock-ready register or unbound
consumer, and no physical performance claim.

Aligned little-endian32-bit access is limited to known configuration/control/
gate interfaces. Ten configuration words00..24 retain full original STR/LDR
values; the original getter, not the device, ORs its companion mask.
Configuration routing to physical rate is not decoded. Unsupported fields,
offsets, widths and commands return MEMTX_ERROR with CPU/PC/address.
See the [pinned control contract](https://github.com/mashiro01/nightglass/blob/943873c/research/z7ii/qemu-system-control-contract.md);
same-family IP comparison does not identify Nikon silicon.

## Historical execution and native scope

Code857ce78 built natively. Its original MTTCG run exited0 after a debug stop,
with the38,239,096-byte payload directly unchanged and O178 stillfe ff ff ea.
CPU0 crossed PC1002d70c/READ32 PA0428102c; its next unsupported transaction
was PC10024c9c/WRITE32 PA20023000=1, original SDI/DDR reset assert.
CPU1 remained in early release wait; SMP release and first dispatch were
not claimed.

All45 functional-pll-smoke-first synthetic cases passed: six bank masks,
functional reset, PLL initial state/start writes/10us callback, retained
MEMC/NOR rejection, GIC/timer and the then-strict completion contract.
Later completion reset built and passed60 contracts, including default request,
actual domain3 state changes, GIC250 start/stop/ACK/EOI, reset and unsupported
profiles. This did not establish original arrival at that completer.

MEMC, PLL/gate, completion, timestamp and SDI do not serialize state.
Upstream VMStateDescription.unmigratable and DeviceClass.vmsd reject migration/
snapshots rather than silently losing requests, clocks or applied state.
The synthetic clock-reset case verifies QMP migration rejection.

Fixed40f6b3b passed60 fixtures. Fixed45ae03b built without unpublished MXIC
and passed exactly three checks: migration/reset, pending arbitration reset
and completed arbitration reset. Earlier62-case runs included unpublished
MXIC and are experiments, not full-suite evidence for45ae03b. No new original
execution was performed for that review correction.

Script45ae03b accidentally made second-entry checks unreachable. Its three
results still establish migration rejection and both arbitration-reset cases,
not a second reset entry. Script0cf5f1c removed the unreachable duplicate,
restoring cont, final-loop wait and R2=0 verification. Exactly clock-reset and
completion-reset then passed independently rebuilt C45ae03b; prior receipts
were not replaced. Current startup is documented in the
[frontier](../validation/frontier.md).
