# CComp GPIO peer

## Source-backed electrical request and dispatch

The C1.70 MAS CComp request object at MAS VA `b18a7928` selects GPIO6 bit8,
whose data register is PA `04282018`. Producers `1024229c` and `102422c4`
raise this output and arm a one-tick software alarm. Alarm callback
`10242124` lowers the output; it does not clear the pending software request.

The acknowledgement object at MAS VA `b18a792c` selects GPIO5 bit7, routed
through EXIU input0 to GIC ID65 on a rising edge. Original callback `102422ec`
uses the current software state: state1 dispatches `03030024`, state2
dispatches `03030025`. Both states use the same complete electrical request
pulse. Their different callbacks are guest software policy, not two distinct
commands on the GPIO wire. The source receipts are retained in
`run sensorprofile-20261024/`.

These observations establish who requests and handles an acknowledgement.
They do not recover a physical CComp implementation, its processing latency,
or any external data-processing result associated with the handshake.

## Finite virtual peer

`TYPE_NIKON_CCOMP` is a SysBus device without MMIO. It has one named input
`request` and one named output `ack`. The machine connects GPIO pad104
(GPIO6 bit8) to `request`, and `ack` to GPIO external-level87 (GPIO5 bit7).
The existing GPIO5 bit7 → EXIU0 → GIC65 route remains responsible for the
interrupt. The peer never reads guest RAM, selects software state, calls an
ISR or callback, writes a guest event, or depends on battery/resource inputs.

| State | Input or timer | Result |
| --- | --- | --- |
| Idle | Request rising edge | Retain an incomplete request |
| Request high | Request falling edge | Schedule service after 1000 virtual ns |
| Waiting | Service timer expires | Raise ACK; schedule its falling edge after 1000 virtual ns |
| ACK high | Width timer expires | Lower ACK and return idle |
| Waiting or ACK high | New request rising edge | Cancel outstanding timer, lower ACK, discard overlapping pulse |
| Discarding overlap | Request falling edge | Return idle without completion |

The 1000 ns service delay, 1000 ns ACK width and overlap-cancellation policy
are explicit **functional conventions**, not recovered physical timing or
firmware-mandated rejection behavior. There is no speculative command queue
or additional completion for an overlap. A previously latched EXIU request
remains the interrupt controller's responsibility; lowering ACK does not
erase it.

Requiring the request's falling edge before scheduling ACK is also a finite
model choice. The recovered callback can lower request itself, so source alone
does not establish that physical ACK always follows the alarm's falling edge.
ACK end time is anchored to the scheduled start plus its width; a large
virtual-clock step may deliver both edges and leave ACK low with EXIU pending,
without stretching the pulse to one width after a late callback.

A repeated publication of the same line level has no effect. A held-high
request cannot finish; elapsed host time does not run virtual timers. Reset
invalidates the outstanding transaction, cancels either timer phase and
lowers ACK while preserving the actual request line level. A residual
falling edge after reset therefore cannot finish the old transaction: a new
complete rising/falling pulse is required. Unrealize cancels the timer,
lowers ACK and frees the timer; no delayed completion survives teardown.

This peer expresses only the recovered pulse/ACK service contract. It does
not assert that any unknown physical CComp payload processing succeeded.

## Native validation scope

The `nikon` qtest executable registers `/nikon/ccomp/pulses`,
`/nikon/ccomp/cancellation` and `/nikon/ccomp/iar`. The pulse fixtures use real
GPIO direction/data writes and observe GPIO5 bit7, EXIU0 pending/W1C and
the board's GIC65 input. They cover repeated state1/state2-style reuse,
delay/width boundaries, duplicate levels, missing output direction,
incomplete requests, host-time inactivity, overlaps and reset in all three
active phases, followed by a new complete pulse. A single clock step crossing
both timer phases also verifies a low final ACK and retained EXIU pending.

The IAR fixture runs a self-written secure ARM program that configures the
actual GIC, emits a GPIO pulse, records its real IAR65 result and performs
EXIU W1C and GICC EOI. Secure configuration is performed by that synthetic
CPU program because ordinary qtest transactions are nonsecure. These are
synthetic device-contract checks, not original Nikon execution, complete
startup or physical-camera evidence. The parent obtained the old-model red
(ACK remained low), then all3 CComp and all5 GPIO cases passed in separate
native selections. Receipts: `native-ccomp-red.log`, `native-ccomp-green.log`
and `native-gpio-green.log` under `run sensorprofile-20261024/`.

## Original-firmware observation

The normal20-second `ccompnormal` run uses no descriptor32 input and retains
the existing declared inputs, healthy battery UART and unchanged firmware.
Original callback `102422ec` executes twice; the state2 branch dispatches
the CComp task's bit20. Original `10394b44` then posts CSEQEV01
(`b186832c`) bit10, and the wait returns1 at `1039a3c4`. The ID54 producer
`10395ac0` is not reached. Property6433 contains only ID68, and QMP remains
running with the head1 error-GUI console available. This verifies software
consumption of the finite handshake, not unknown CComp payload processing.
