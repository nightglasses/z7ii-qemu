# Four I²C interfaces and asynchronous bus transactions

## Held TX batch completion

The original combined acceleration read sets the driver HOLD flag for its
selector TX and waits for COMP before submitting RX. ISR`1036d686` requires
COMP1 with zero software remaining count and no error; callback`10370f3e`
stores that ISR snapshot and signals the waiter. It independently retains
HOLD when driver+44 is set. FIFOempty2 alone does not satisfy this contract.

Both local and asynchronous controller paths now report COMP only after the
last accepted WRITE byte empties a FIFO batch, retaining the bus until restart
or HOLD release. An address-only empty held TX still follows the existing
configured starvation timeout. A newly filled batch must clock its bytes before
another COMP. Releasing HOLD after an acknowledged COMP produces STOP without
replaying completion. Native coverage includes16-byte then2-byte held batches,
no early COMP during pending bytes, repeated START/RX, timeout and reset/cancel.
Original unchanged execution now completes the acceleration status and sample
reads; see [acceleration](../acceleration/protocol.md).

The controller now has an explicit asynchronous board endpoint alongside the
official synchronous I2CBus. They are mutually exclusive: attachment requires
an idle controller and an empty local bus, then closes that bus to local slave
devices. Detaching does not restore a local-bus fallback. Neither controller
path accesses a global DMA or guest address space; FIFO bytes and clocks belong
to each individual controller instance.

The public `NikonI2CAsyncOps` seam submits START, WRITE, READ, NACK and STOP with
an individual nonzero token. START carries the unshifted seven-bit address and
the receive direction in bit7. WRITE holds its FIFO head until that byte has
actually been clocked. READ carries zero and publishes only an actual returned
byte. A final returned byte is followed by an explicit master NACK; the endpoint
does not add another implicit NACK. STOP completion releases active bus state,
and non-HOLD transfer COMP is generated only after that real STOP completes.
After the last accepted WRITE byte drains a held TX batch, COMP is generated
while the bus and endpoint remain held. It is independent of STOP.
Phase completion means a physical functional bus action, not a TPJ application
acknowledgement. `NIKON_I2C_NAK=0x100` is separate from all valid bytes, including
an actual returned `ff`.

Address, transmitted-byte and received-byte failures raise the existing NAK
event and request STOP without reporting successful COMP. RX FIFO full suspends
further byte requests until the guest pops data. HOLD preserves a selected
endpoint and allows a same-address repeated START after the previous action has
completed. Direction changes and repeated START cannot replace an accepted
pending action. Releasing HOLD while address or data is pending does not skip
that action. Existing TX starvation timeout raises TIMEOUT and then submits
STOP; it does not invent a reply deadline for a disconnected remote endpoint.

Backpressured submissions retry only after an actual endpoint readiness event.
Accepted operations remain pending across disconnect and do not clock twice
after reconnect. FIFO clear and reset cancel the timer, invalidate the token
before notifying the endpoint, and discard the controller's queued bytes. Late
responses cannot complete a replacement operation. Matching-owner detach also
invalidates callbacks while preserving unfinished bus state until guest cancel.
The model performs no synchronous host wait. Its nominal byte budget remains
nine SCL clocks; a final asynchronous byte uses eight plus the explicit one-
clock NACK. STOP has a declared one-clock functional setup budget. These budgets
are model timing conventions, not Nikon hardware measurements.

The bound regular TPJ source profile uses I2C0 at `2c018000`, resource100,
IRQ214 (`d6`) and ISR `1036d5fd`. Constructor `103db654` selects logical I2C0;
the normal `103dbcec` transaction writes command10 then reads two bytes, or
command11 then reads seven bytes, at literal address55. The address is passed
directly to ADDR and is not shifted. The NDS source's normal command handlers
produce the corresponding status and seven-byte sequence independently. These
source correspondences identify the protocol; they do not prove an original
cross-CPU transaction has completed.

The peer's optional GPIO frontend carries actual source roles: resourceAA is
MAS GPIO10[13] (control), and resource63 is GPIO6[6], EXIU15/IRQ80 (notification).
The control publication requires a real DDR output and GPIO EPCR selection.
Floating reset or an input-mode output latch does not publish a driven level.
Reconnect queries the current pad direction and function; it cannot replay an
old driven level after the pad has become an input or alternate function.
An incoming physical notification updates the real external GPIO input and
normal EXIU path. Neither an I2C ACK nor a timer manufactures a notification,
and the control wire is not assigned an unsupported reset meaning.

The original SLA receive helper at `1005fb6a` and transmit helper at
`1005fba2` read IDR at offset28, OR bit1 and write it back. IER/IDR are one
write-strobe pair controlling IMR, rather than independent stored registers.
Both read as zero in this functional profile; reading either changes neither
IMR nor ISR. The [AMD UG1087 IER reference](https://docs.amd.com/r/en-US/ug1087-zynq-ultrascale-registers/Intrpt_Enable-I2C-Register)
and [IDR reference](https://docs.amd.com/r/en-US/ug1087-zynq-ultrascale-registers/Intrpt_Disable-I2C-Register?contentId=rVK_Cp5TVhjmCqPgmBReMg)
define their interrupt fields as write-only strobes and document the single
mask relationship. Read-as-zero is a declared functional readback convention,
not a measured Nikon silicon value. The dedicated four-port `strobes` fixture
checks the original IDR read/OR/write sequence, unchanged mask reads, no ISR
events and the paired enable/disable effect.

`tests/qtest/nikon/i2c.c` supplies nine focused native fixtures. They use actual
Unix CharBackend sockets and typed forty-byte `wire.h` envelopes, exercise
every bus phase, delayed/fragmented replies, address/write/read NAK, reset and
late tokens, eighteen-byte FIFO backpressure, HOLD starvation timeout, four
offline controllers, two interleaved peers, source control/notification GPIO
and reconnect without stale output publication,
and the genuine synchronous AT24C target. The normal command10/11 fixtures use
synthetic byte arrays to verify the two/seven-byte transaction shapes; those
arrays are not camera response data. These fixtures await the sole builder;
no new pass or original application success is claimed here.

This stage implements the four original controller bases `2c018000..2c01b000` and guest IRQs214..217 for [RTOS and multicore startup](/startup/smp). The original initializer's IDR write `2c018028=2ff` is interrupt masking, not FIFO acknowledgement. Original geometry, configuration, request/cancel and distinct ISR variants are recorded in [the fixed whole-protocol research](https://github.com/mashiro01/nightglass/blob/2f11242/research/z7ii/qemu-i2c-contract.md). Its1160B original-byte candidate coverage is distinct from complete accepted native CFG coverage; only the initializer had previously been dynamically observed.

The virtual board starts each controller idle: CR/address/count/ISR/pause/timeout0, IMR2ff, empty16-byte TX/RX FIFOs and independent nominal48MHz Clock. These are functional assumptions, not Nikon reset or rate measurements. Word-only MMIO follows the [Linux v6.12 Cadence register interface](https://github.com/torvalds/linux/blob/v6.12/drivers/i2c/busses/i2c-cadence.c#L21-L118) and the AMD register/protocol references linked in the research. SR derives bus/FIFO/direction state; IER/IDR change one mask, and ISR events persist until W1C. Reads do not create transfer completion.

Each controller owns an official [I2CBus](https://github.com/qemu/qemu/blob/4fc49f46dc95d4a27de2509e7fceb2931e91faeb/hw/i2c/core.c#L106-L182), Fifo8, Clock and virtual-time timer. An address request schedules actual upstream start/send/recv/end actions; an empty bus returns address NACK. Byte frames use9 virtual SCL cycles and the candidate divisor22×(divA+1)×(divB+1). Raw MAS diagnostics leave these buses empty. The complete-bundle composition attaches the qualified eye and acceleration endpoints on I2C2. Synthetic tests may attach upstream `at24c-eeprom`; its bytes are not camera data.

Normal7-bit master TX/RX is supported. TX TRANS_SIZE returns unsent-byte-count minus one (empty0); RX TRANS_SIZE is remaining expected bytes, and slave counts describe queued FIFO state. These are the standard candidate conventions, not proof of Nikon's variant encoding. RX full waits for guest pop/rearm rather than dropping data. Empty DATA read reports RXUNF and returns a named underflow value0; full DATA write reports TXOVF and preserves existing bytes. FIFO clear cancels work and clears counts/FIFOs; W1C remains separate. HOLD retains a selected endpoint, permits same-address repeated START and stops when released after completion. Configured TX starvation timeout is `(TIMEOUT+1)` virtual SCL cycles; zero disables that assumption. No external slave master is invented.

Unbound operations fail: original CR bit7 extension, other monitor-mode ADDR requests,10-bit master addressing, changed endpoint during a held transaction, unsupported widths/offsets and illegal active configuration changes. The finite model permits low-byte control modes `0x3e/0x3f` as ordinary master transactions; this exception does not recover physical monitor behavior or the Nikon variant encoding. Other CR SLVMON combinations remain rejected at ADDR. Idle slave configuration does not implement an external slave-controller endpoint. State migration/snapshots are explicitly blocked with upstream unmigratable VMState.

Native C `9f453e8c91b88fe4d389663266789f4691f709dc` built successfully and14 focused synthetic checks passed in `run startup/i2c-9f453e8/result.json`. Its immutable original receipt is `run startup/original-i2c-9f453e8/receipt.json`: QEMUexit0,0.240677s host interval, debug stop, complete payload direct-content unchanged. Log6111 first records CPU0 PC`102a7d94`, READ32 PA`12e00004`;6113/6116 record DataAbort/DFSR1008. CPU1 had passed the four IDR initializer writes; its final PC`103b05d2` does not establish phase completion. The original run did not attach the synthetic EEPROM, modify original code, supply a valid external record, or reach a menu.

Follow-up C `059c985af743bb5a5a61629481a48e24d1936ba6` adds active direction-change preflight and source formatting. Its separate incremental native build succeeded;15 focused checks passed in `run startup/i2c-059c985/result.json`, including the new pending-request mutation rejection. Existing original9f receipt remains unchanged and is not relabeled as059. Checks cover all four controllers, real bus NACK, attached-endpoint TX/RX/repeated START, mask/W1C, GIC217 IAR/EOI, FIFO errors/clear, timeout, cancellation, real second-entry board reset, quiet slave state and unsupported variants. These device checks do not prove original application transactions or real camera endpoints.

At that earlier original run, the independent dependency was the external record scanner over `[12e00000,12e40000)`. Its absent payload backing and empty/ENDM semantics belonged to a separate finite storage profile; they were not an I²C response or a valid factory-data result.

Review found two transaction-boundary defects in the initial implementation: releasing HOLD during a pending address phase could report COMP without executing upstream START; changing TIMEOUT while an acknowledged empty TX was waiting could leave the previous deadline armed. C `2d070f1f1919c0f2efb18d468b2c1624f57a19fc` fixes both generically. HOLD release retains pending-address work; only an addressed, completed TX may complete directly. Mutable waiting timeout deletes the old timer and reschedules from the current virtual instant, or stays waiting when disabled. Failed address selection must still produce the real NACK.

That exact C built incrementally with exit0 and passed18 focused checks in `run startup/i2c-state-2d070f1/result.json`: the original15 plus pending-HOLD-release/empty-bus NACK, acknowledged timeout disable/re-enable, and replacement of an old deadline by a longer one. The latter two use an official EEPROM only in the fixture, observe real DATA after ACK, and wait past the old deadline without TIMEOUT/STOP before testing a newly armed deadline. No unchanged original run was repeated or relabeled. The binary includes the separately documented finite record aperture; these18 checks concern I²C and do not establish record parsing or original application transfer success.

## MAS I2C2 eye sensor endpoint

The saved MAS source selects logical channel 3, maps it to hardware I2C2 at
`2c01a000`, and uses seven-bit address `0x38`. The complete-bundle machine now
attaches one ordinary upstream I2C slave at that address. Raw MAS diagnostic
runs intentionally leave the bus empty so existing NACK tests can attach
their own fixtures.

The endpoint implements the source-compatible RPR-0521RS register contract:
system/mode/ALS/proximity/interrupt and offset controls are writable;
proximity, ALS and offset data are readable little-endian registers; and the
manufacturer ID register returns `0xe0`. Register pointers and sequential
transfers use upstream I2C callbacks, and unqualified register pointers NACK.
Reset restores the public ID and zero measurement state. No measurement
provider, GPIO interrupt, ready flag, frame trigger, or image data is
synthesized; cold-zero readings remain an explicit board input until a
source-qualified producer is recovered.

Implementation is `hw/nikon/devices/eyesensor.c`; the machine obtains the
controller's real upstream bus through `nikon_i2c_bus()`. The native ARM build
links this endpoint successfully. A complete C1.70 bundle machine initializes
with the endpoint attached; a bounded 120-second original run still stops in
the separate SLA/EX domain fault loop (CPU3 IFAR/DFAR zero), so this change
does not claim GUI completion.

## Mode qualification with nonzero divisors

A read-only original observation in the opt-in bridge notification experiment
catches MAS PC `0x1036d492` writing ADDR `0x38` at `0x2c01a008`, with control
readback `0x0b3e`. The old ADDR guard compared the entire control word against
`0x3e/0x3f`, mistakenly treating the existing divisor bits8..15 as mode bits.
The guard now compares only the low byte. It preserves the same two finite
mode exceptions; no new monitor operation, endpoint, ACK or reply is added.

The native `/arm/nikon/i2c/clock-divisors` regression first fails on the old
guard (`0 != 4` for NACK status), then passes. All four empty raw-MAS buses
accept TX/RX modes with both divisor fields varied and report actual upstream
address NACK, never successful completion. An unsupported `0x26` mode with
those divisors still produces no transaction. Full native and original-run
scope is recorded in [validation](../../validation/native.md); progress under
the bridge experiment is not default-profile or physical-hardware acceptance.
