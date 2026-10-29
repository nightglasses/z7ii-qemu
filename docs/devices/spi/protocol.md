# Eleven-channel SPI functional profile

The asynchronous board endpoint uses the same FIFO, frame timer and interrupt
state as in-process SSI. After the nominal master frame deadline, the device
submits the unconsumed FIFO head with a monotonically changing transaction
token. A backpressured endpoint can retry on an explicit readiness notification;
an accepted request remains busy until an actual peer response completes that
token. There is no host busy wait, automatic reply, polling completion or retry
timer. Only completion consumes TX, publishes the received word and sets the
normal transfer-done interrupt. Thus an attached but offline peer cannot return
the disconnected SSI zero as a successful response.

Disable, port close and board reset cancel the timer and invalidate the active
token before notifying the endpoint. A late reply cannot consume a replacement
transaction. Each device instance owns its own token and pending state. Disable
preserves FIFO contents under the original register contract; port close and
board reset flush them. Board attachment is configured only while disabled, and
the endpoint's operations and opaque object must remain alive until the SPI
device has been detached by its matching owner. Runtime detach invalidates and
cancels the active token, retains unfinished busy state, and never switches the
port to disconnected SSI success. Later transactions also remain offline until
a real endpoint is attached again. Endpoint callbacks run on QEMU's device
thread under the BQL, including callbacks originating from a CharBackend.

This seam covers a master word transaction, not a substitute for external slave
clocks. An asynchronous request is transport framing, not a claim that a remote
guest has accepted a Nikon application message. Source-backed CS, readiness and
slave state must be implemented by the actual endpoint before it completes a
word.

The client that drives port 4, its job descriptor and the exact bytes that port
puts on the wire are recorded in [runtime/transport.md](../../runtime/transport.md).

The saved native SPI DMA initialization now binds the whole three-port family.
`103705a8` copies the 88-byte policy returned by `10207462` into `b1d4ff84`;
`103705cc` binds DMA resources only in `12a..131`. The original EX transmit
task `1020b7ba` uses logical0/channel0 and receive task `1020b65a` uses
logical1/channel1. Both have resource `158`, the unbound sentinel, and their
mode1 call goes through `10370a14 → 10370d92 → 10243468` (FIFO). The table binds
SPI2,3,4 to HDMAC bank0 channel pairs0/1,2/3,4/5, with request pairs7/8,9/10,11/12
respectively. The original `102dc33e` whole bank/channel initializer establishes
those resource-to-controller records; they are not inferred from resource
numbers alone.

Those three ports receive an explicit board `dma` binding. CTRL bit400 gates
two demand wires: enabled TX space and enabled RX data. Existing HDMAC performs
the real bus transactions, accepts one demand beat, and rechecks FIFO state;
SPI contains no DMA copy engine. The original VR channel4 uses halfword data at
TX `+0` and RX `+4`, so those two widths are supported while a source-bound DMA
port selects CTRL400. Unbound ports retain the DMA rejection. Disable/reset
deassert demand, reads consume actual RX, and completed external frames free
actual TX space. No demand depends on a fabricated peer reply. The focused DMA
fixture checks both RAM-to-MOSI and MISO-to-RAM through the actual existing
HDMAC, including suspension before SPI enable and while a peer response waits.

The focused native test file exercises the real CharBackend transport with
ordinary Unix sockets and its shared `wire.h` envelope. It holds a response
while virtual time advances, checks the actual MOSI word and transaction token,
sends fragmented MISO input, cancels and rejects late responses after disable
and close, and interleaves two independently connected ports. An attached peer
without a chardev stays busy without RX or transfer completion. These are
synthetic host endpoint transactions, not EX or VR firmware compatibility proof.
The sole builder compiled and linked the coherent SPI/transport/machine model,
then all eight focused `/arm/nikon/spi` cases passed in 2.18 seconds. Its actual
`run validation/gui/peernative.log` in the validation worktree records zero
failed tests. This includes real socket response timing, fragments, cancellation,
late tokens, independent peers, offline waiting and real HDMAC data. The only
reported compiler warning was the fixture entry's missing forward prototype;
that declaration is now supplied without changing runtime behavior.

The additional `/arm/nikon/spi/select` focused fixture uses source GPIO11[1]
(`pin=177`, resource AE) through the peer's public GPIO link. Floating reset and
an input-mode high PDR latch produce no SELECT. Setting the actual DDR output
produces SELECT high; the masked PDR write produces SELECT low. EPCR alternate
mode stops publication of later latch changes. Its frame assertions cover the
actual socket envelope with SELECT kind, line0, bits0 and token0. This added case
awaits the sole builder; the eight-case pass above does not cover it.

This layer continues [unchanged-firmware startup](/startup/first) and [RTOS and multicore startup](/startup/smp). It uses the [fixed original-byte whole SPI contract](https://github.com/mashiro01/nightglass/blob/e27bf43/research/z7ii/qemu-spi-protocol-contract.md), including the actual `10242fda` initialization anchor, eleven original objects and their two IRQ wrappers, and 23 adjacent methods. The bounded original Thumb analysis covers 2,324 bytes / 964 instructions with guarded jump tables and preserved unknown transfers. These functions were absent from the saved native listing: this is explicitly original-byte diagnostic analysis, not a claim of complete native-function recovery.

The original objects supply `2c0a0000 + channel*100`, IRQ pairs `193/192` through `213/212`, and FIFO-depth consumers at `+48`. Open writes `+60=0`; close/init write `+60=1`. Setup stops and polls EN, programs configuration and watermarks, then real guest transactions push/pop word data and service FIFO progress through the common ISR `102435ec`. The ISR reads pending, W1C acknowledges it and intersects the guest enable mask; overflow follows its original error/event path. No host writes the software objects, enables them, calls their handlers or sets their event flags. [Original whole-method evidence](https://github.com/mashiro01/nightglass/blob/e27bf43/research/z7ii/qemu-spi-protocol-contract.md).

The protocol candidate is the Socionext-authored [fixed SPI register header](https://github.com/killsmm/m10v_mono/blob/cebff0686425b739bf63e87c911591ab30b03c1a/ca7_rtos/target/cpu_tkernel/basefw/MILB_API/MILB_Header/include/Peri/_spi.h). It agrees on TX/RX `00/04`, CTRL/AUX `0c/10`, FIFO status `14`, interrupt enable/status/clear `20/24/28`, EN `3c`, nine-bit depth `48` and watermarks `4c`. Its three channels and base `1e800000` differ, and its `54..ff` are reserved; Nikon's eleven channels and `60/64` extensions remain a version hypothesis. It does not establish Nikon reset values, physical FIFO capacity or a connected endpoint. The [fixed manufacturer driver](https://github.com/killsmm/m10v_mono/blob/cebff0686425b739bf63e87c911591ab30b03c1a/ca7_rtos/target/cpu_tkernel/basefw/MILB_API/Project/DeviceDriver/Peripheral/src/dd_spi.c#L1936-L1964) supplies the divider relation `input / (2*(DIV+1))`; the original Nikon DIVENABLE field is preserved independently.

`hw/nikon/devices/spi.c` implements one SysBus device per channel using upstream [SSI](https://github.com/qemu/qemu/blob/4fc49f46dc95d4a27de2509e7fceb2931e91faeb/hw/ssi/ssi.c#L156-L168), [Fifo32](https://github.com/qemu/qemu/blob/4fc49f46dc95d4a27de2509e7fceb2931e91faeb/include/qemu/fifo32.h), Clock and QEMUTimer. Word accesses move FIFO entries; status reflects occupancy and a pending frame; completed master frames produce received words, watermark/done events, and genuine GIC input state. Interrupt W1C clears latched events. Access counts never advance virtual time or complete a future word. A master frame on an empty upstream SSI bus receives zero, the explicit disconnected MISO-low endpoint, not a device ID or a command acknowledgement.

Synchronous master frames retain the intended serial-clock cadence. The first
word is scheduled from the current virtual time; each immediately following
word is scheduled from the preceding intended deadline. A late main-loop timer
callback can therefore process already elapsed word deadlines without adding
host scheduling delay to every serial period. Empty FIFO, disable, close,
reset and cancellation discard that cadence. Completion from an asynchronous
peer also discards it because external backpressure, rather than the local
clock, determines when the next word can begin.

## Shared sensor-client ownership boundary

The expanded cold-neutral timing input exposes a separate SPI8 boundary in
unchanged C1.70 execution. MAS VA `10243244` discards a received word only after
checking RXEMPTY at PA `2c0a0814`. A temporary, error-only diagnostic captured
the exact rejection and the preceding32 RX-pop/EN/configuration operations;
successful accesses produced no output and no guest state was changed.

At the first empty pop, the helper's local progress is0 and goal5, with saved
status `0c` indicating RX data. Shared owner MAS VA `b1802350` already holds
progress5/goal5, while the controller is disabled with empty TX/RX. The retained
history contains five actual pops, then EN=0 at `102436d2`, followed by the
resumed old helper's pop. There is no intervening reset. All retained operations
report CPU0. This establishes overlapping software activations using stale
local transaction state; it does not identify the draining activation's upper
caller by itself or establish a physical empty-read return value.

The faulting route is source-established:

```text
capseq event800 ->103976b0 ->1026dbb6 ->102d9bd2 ->102d9a2c(1)
 ->102fe1ea(4) ->1030621e ->10367fe0 ->10370a14
 ->10243468 ->102437f0 ->1024320c
```

It requires sensor operation byte `b1868510` in{6,8} and eligibility byte
`b18685a0` equal0, then updates the sensor property batch before publishing the
normal frame profile. These are ordinary operation checks, not a lock. The
complete writer, wrapper and low-level start paths contain no mutex, dispatch
disable, IRQ mask or busy admission check. Mode0 polls completion/overflow/
cancellation without a finite timeout; the common ISR drains RX only for mode3.
SPI8 has no DMA consumer in this binding.

A later bounded stack diagnostic identifies the independent CAPTG worker's
handler at MAS VA `1026c66e`. Its event10/state8 branch calls
`102d9ace ->102d9a2c(0) ->102fe1ea(3)`, while another branch calls
`102d9780 ->102d93a2 ->102fe1ea(3)`. Both reach the same writer through
`1030621e` and use owner `b1802350`, TX staging `b2b375ca` and RX staging
`b2b3774a`. CAPSEQ and CAPTG have separate task IDs, identical nominal public
priority8 and CPU0 affinity. No forgotten mutex was found along either complete
writer/start path. The VD callback accepts sensor operations6/8 before prepare
has finished and can post CAPSEQ event800. The sensor eligibility byte is a
hardware-bypass selector, not an acquired bus lock. These observations establish
the second caller; they do not justify adding an invented TG readiness gate.

The pre-error stopped snapshot also resolves CaptureService's apparent wait.
Its task `0100001b` is READY at internal priority9, with dispatcher event39 bit1
still queued. CPU0 runs CAPTG `010000ba` at priority7, polling received progress1
of7 at MAS VA `10243226`; CAPSEQ `01000096` really waits on its dispatcher.
The kernel chooses the lower priority number. CaptureService therefore cannot
dequeue while the higher-priority CAPTG spins without yielding. Its saved event
wait fields belong to an earlier suspension and are not a current wait. Its
saved PC `10111300` is inside flag-post rescheduling, after posting the CAPTG
request, rather than inside a blocking wait.

The same diagnostic records161232 SPI8 status observations after a sampled
word deadline, with a maximum lateness of5058600 virtual nanoseconds. The first
three are26600/73600/83600ns late, still TX5/RX0/EN1. This directly establishes
that the timer-only model can expose stale hardware progress to the poller.
It does not establish that this gap explains every historical interleaving or
recover a physical sensor reply. The diagnostic only recorded bounded host
state and original RAM; it did not modify guest memory or invoke guest code.

Returning a retained or zero word would hide stale ownership and could regress
the shared progress counter. A device-side transaction queue would not repair
the overwritten guest descriptor and staging buffer. Both remain unsupported
repairs. The functional correction reconciles elapsed serial work at its real
device boundary; original-firmware acceptance remains separate from that native
contract. See the [curated pressure and ownership receipt](/evidence/runtime/grid.json).

## Observing elapsed synchronous work

A validated PIO MMIO operation first reconciles synchronous master words whose
sampled deadlines have already elapsed. It uses the existing SSI transfer,
FIFO, interrupt and timer paths, not a synthesized received value. Repeated
accesses before the deadline do no work. A genuinely empty data-register read
still fails. Unsupported widths, registers and configuration values fail before
reconciling work. W1C and disable take effect after already elapsed words;
reset and cancellation invalidate the active token.

Each observation processes at most the TX count present on entry, bounded by
the32-word FIFO. A guard covers ordinary timer delivery as well as MMIO delivery,
so nested IRQ/DMA callbacks cannot recursively shift or consume newly supplied
words without bound. Asynchronous peers retain their actual request/response
boundary, and slave mode still requires external clock input.

DMA-enabled MMIO retains timer-driven arbitration. QEMU's device reentrancy
guard prevents a synchronous HDMAC beat from revisiting the SPI FIFO inside an
outer SPI access; HDMAC already defers that demand through its existing BH.
Shifting an entire overdue batch inside that guarded access could overflow RX
before the real DMA consumer can run. The model therefore skips MMIO catchup
while CTRL400 and the SPI guard are active. It keeps the guard enabled and
retains ordinary timer/HDMAC interleaving. The near-full fixture starts with31
actual RX words, queues32 TX words plus5 DMA-fed words, and checks all68 ordered
MOSI/MISO words through real HDMAC arbitration. Reset with deferred demand must
cancel it without another transfer. This scope is independent of the observed
PIO-only SPI8 caller.

ClockPreUpdate reconciles elapsed words while the old input period is still
available; ClockUpdate only starts eligible pending work. An active word retains
its sampled deadline even if the input then stops. A successor cannot start
without a clock. This ordering is the explicit atomic-word timing hypothesis,
not a recovered sub-word physical clock model.

## Source-selected input clocks

The board connects each controller's `spi-clock` input to the clock device.
C1.70 MAS VA `10370c32` queries selector5 through `103b0540`, reading PA
`04281014` with mask `00880008`. The common selector nibble at bits20..23
declares80 MHz for8 and100 MHz for9. Channel8 instead uses bits16..19:
8 declares80 MHz,9 declares100 MHz and A declares61.53 MHz. These are the
original driver's declared frequencies, not measured physical PLL outputs.
An unrecognized selector supplies no clock; queued words wait for a supported
source. Unconnected controllers, including standalone fixtures and private
components, retain their declared nominal48 MHz profile. The connected MAS
board does not fall back to that rate for an unknown selector.

The divider remains `input / (2*(DIV+1))` when enabled. A word samples its
clock on start; a source change or stop affects its successor. This atomic-word
choice is a functional timing assumption. The native selected-clock fixture
checks all three declared rates, a stopped source, restart and changes while
two words are queued. Older48 MHz receipts below retain their historical scope.

The unchanged-firmware EX transport exposed the prior scheduling error. With
the same 48 MHz, 16-bit, divide-by-two guest configuration, its first 256-byte
descriptor was split when an EX receive window ended with NDTR96: only 160
bytes had arrived. Individual nominal two-microsecond words had accumulated
roughly millisecond host callback delays because every next timer used
`now + period`. With intended-deadline scheduling, all128 words complete in
one EX DMA arm, NDTR reaches zero, the firmware receives its battery report,
and startup naturally reaches the graphics transfer producer. This is an
unchanged-firmware progression receipt, not an LCD-frame completion claim.

The system-linked `/nikon/spi/late-callback-cadence` unit test advances the
virtual clock across four frame deadlines before running timers once. The old
implementation leaves three TX words queued; the corrected implementation
drains all four into RX and raises the ordinary transfer-done cause. The same
fixture checks idle restart, disable and reset boundaries. The focused native
`/arm/nikon/spi` batch passes all nine subtests with the corrected device.

## Explicit external slave clocks

`nikon_spi_slave_clock()` accepts one externally supplied word only when the
port is released, EN is set, CTRL master mask `20` is clear, the guest's slave
request `64=1` is present and the supplied width exactly matches AUX bits8..12
plus one. Register offsets and masks here are hexadecimal; bit indices and word
counts are decimal. An outstanding internal/async shift cannot use this path.
The call neither advances virtual time nor supplies a periodic clock or peer.
The board and external producer own every requested clock edge.

When the caller supplies `transmitted`, acceptance requires an actual TX FIFO
word and returns that word masked to the configured width. A NULL pointer
explicitly means that the external master does not sample outgoing data. This
permits RX-only operation without manufacturing a TX word or assigning an
unrecovered idle-line value. Source C1.70 MAS VA `10383388` constructs such a
request: null TX pointer/count and a real RX buffer for21000 halfwords. Low
driver `10243468` leaves TX count zero for null-TX slave requests; only the
master branch derives a dummy TX count from the receive count. Its DMA setup
calls `102436de`, whose original instructions write `64=1` only when CTRL's
master bit is clear. These facts support receive-only clocking, rather than
requiring a fabricated FIFO entry to deliver the42-byte streaming family.
This API does not select a logical serial mapping or implement that lens stream.

An accepted clock consumes a queued TX word when present and captures the real
incoming word unless AUX mask `8` selects RX discard. Existing FIFO watermarks,
pending bits, HDMAC demand and IRQ routing then follow their ordinary paths.
Draining an actual TX FIFO word produces its existing transfer-done cause;
RX-only clocking does not invent a TX completion or a receive-request length.
The guest retains `64` until it changes that request. No guest callback, RAM
packet, serial completion count or application acknowledgement is supplied.

If RX capture is required and the RX FIFO is full, the call returns false
before either FIFO changes. The external producer retains the unaccepted word;
this is an explicit transaction/backpressure convention, not a claim that a
physical SPI slave can stop an already clocking master. A refused transaction
does not latch a fabricated receive-overflow event. RX discard can still send
real TX data while RX is full; empty TX with RX discard is no work and refuses.
All refusal paths preserve the caller's output word, FIFOs and IRQ/DMA levels.
Disable, close/reset, master mode, width mismatch and absent slave request also
refuse. Existing master timing and asynchronous-peer completion are unchanged.

The system-linked native fixtures exercise actual MMIO configuration and the
public clock API:16/32-bit word masking, time-only silence,21 RX-only halfwords,
watermark IRQ and DMA demand, full-RX refusal without TX loss, RX-discard
transmission, reset/recovery and disabled/master/width/request boundaries.
These are synthetic device transactions. Their validation is separate from the
earlier master receipts and does not establish original lens compatibility.

The sole builder obtained a focused negative result with an API stub that
refuses every external clock: `/nikon/spi/slave/word` fails its first accepted
word assertion. With the implementation above, `nikon_spi_unit` passes all five
subtests: the retained master-cadence case and four slave cases. The build
reported no new errors. This verifies the finite FIFO/clock interface and its
refusal boundaries, not the lens packet format, board channel mapping, original
firmware consumption or a connected physical lens.

| Virtual-board input | Modeled behavior and boundary |
|---|---|
| Eleven independent 32-word FIFOs | Depth is a positive declared parameter; count, empty/full and overflow are derived from actual queue state. Real physical capacity is unmeasured. |
| Independent 48 MHz input | QEMU virtual time drives frame duration: AUX length plus one, enabled public divider or explicit no-divider bypass, with configured master-delay ticks. This is a nominal model, not an EXPEED performance estimate. |
| Board reset: asserted, empty, configuration zero | Channel `60=1` cancels the timer, clears FIFOs/EN/pending/slave-request and deasserts IRQ; it preserves configuration/enable mask so guest close/reopen remains coherent. `60=0` releases it. These reset side effects are explicit assumptions. |
| Common pending to first IRQ | Both original wrappers reach one ISR. All enabled pending events route to the first original IRQ `193+2*channel`; the second line stays disconnected. TX/RX physical routing is unproven. |
| Explicit external slave clocks only | Master uses upstream SSI; slave `64=1` never manufactures clocks/data. An explicit clock provider can move real FIFO words through the API above; an unconnected slave keeps waiting. |

CTRL/AUX support the actual constructed fields, including Nikon's AUX bit2 that the older header marks reserved. DMA requests on unbound ports fail; source-bound SPI2..4 use the demand wires above. EN includes the original channel8 selector exception. CS/polarity/framing configuration is retained; no physical chip-select topology or device response is invented on the disconnected bus. Unknown offsets, widths, reset commands and configuration bits return `MEMTX_ERROR` with the normal first-access stop. The device explicitly rejects migration/snapshot through upstream unmigratable VMState. The [whole protocol's exact field and unknown boundaries](https://github.com/mashiro01/nightglass/blob/e27bf43/research/z7ii/qemu-spi-protocol-contract.md) remain applicable apart from the now source-bound DMA path described above.

Native incremental build exited zero at C `2e1a2b62fdc99d0ab999fe7b022aaef3fa4e2916`. Eleven focused synthetic fixtures passed against that same C: seven in `run startup/spi-fixtures-first/result.json`, then three in `spi-fixtures-more/result.json`, and a separate genuine QMP reset/second guest entry in `spi-board-reset-first/result.json`. They verify timed disconnected master transfer/readback, slave waiting, pending reset cancellation and preserved configuration, all eleven depths, genuine GIC IRQ193 acknowledge/W1C/EOI, actual RX FIFO overflow/ack, and width/hole/DMA/reset-command rejection. These bytes are synthetic test instructions, not recovered firmware; this is not a full-suite rerun or endpoint compatibility proof.

The unchanged MAS run `run startup/original-spi-2e1a2b6/receipt.json` records the same complete C ref, QEMU exit zero, direct full-input comparison unchanged, and host wall time 0.202172 seconds. Its new first stop is **CPU0 PC `10360f88`, WRITE32 PA `b7f6a0ec ← 0`**, an uncovered RAM consumer. This MTTCG run reaches the other CPU's memory dependency before CPU1 enters the filtered SPI initialization root; it therefore does **not** prove all eleven original channel initializations or any original external SPI transfer passed. The older `50714df` first SPI-access failure remains unchanged.

The new `--trace-pkgb` uses upstream comma-separated `-dfilter` to observe the original candidate chain, with no guest modifications. In this run, `qemu.log` lines 5161–5166 show CPU1 at `10200684`, argument1, `R5/R6=1`, `R7=118afa78`, `R8=13`, `R9=8`, `R10=1`, `R11=2`, SVC stack `b51bb578`, LR `10200ccb`. Lines 5355–5360 show its descriptor `b1802060` at `10200694`; lines 5367–5372 show entry `10201010`, argument1 and handler pointer `10201011`, SVC stack `b51bb570`, LR `1020069b`. This connects the [fixed phase1/role1/slot1 descriptor+4 chain](https://github.com/mashiro01/nightglass/blob/f0b2efb/research/z7ii/qemu-rtos-startup-contract.md) to actual execution. It proves constructor entry, not phase completion, external peripheral success, a menu or whole-camera boot. The next work closes the full RAM allocator/object producer rather than adding a single writable word.
