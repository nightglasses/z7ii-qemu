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

This layer continues [Issue 326](https://github.com/mashiro01/nightglass/issues/326) and [Issue 331](https://github.com/mashiro01/nightglass/issues/331). It uses the [fixed original-byte whole SPI contract](https://github.com/mashiro01/nightglass/blob/e27bf43/research/z7ii/qemu-spi-protocol-contract.md), including the actual `10242fda` initialization anchor, eleven original objects and their two IRQ wrappers, and 23 adjacent methods. The bounded original Thumb analysis covers 2,324 bytes / 964 instructions with guarded jump tables and preserved unknown transfers. These functions were absent from the saved native listing: this is explicitly original-byte diagnostic analysis, not a claim of complete native-function recovery.

The original objects supply `2c0a0000 + channel*100`, IRQ pairs `193/192` through `213/212`, and FIFO-depth consumers at `+48`. Open writes `+60=0`; close/init write `+60=1`. Setup stops and polls EN, programs configuration and watermarks, then real guest transactions push/pop word data and service FIFO progress through the common ISR `102435ec`. The ISR reads pending, W1C acknowledges it and intersects the guest enable mask; overflow follows its original error/event path. No host writes the software objects, enables them, calls their handlers or sets their event flags. [Original whole-method evidence](https://github.com/mashiro01/nightglass/blob/e27bf43/research/z7ii/qemu-spi-protocol-contract.md).

The protocol candidate is the Socionext-authored [fixed SPI register header](https://github.com/killsmm/m10v_mono/blob/cebff0686425b739bf63e87c911591ab30b03c1a/ca7_rtos/target/cpu_tkernel/basefw/MILB_API/MILB_Header/include/Peri/_spi.h). It agrees on TX/RX `00/04`, CTRL/AUX `0c/10`, FIFO status `14`, interrupt enable/status/clear `20/24/28`, EN `3c`, nine-bit depth `48` and watermarks `4c`. Its three channels and base `1e800000` differ, and its `54..ff` are reserved; Nikon's eleven channels and `60/64` extensions remain a version hypothesis. It does not establish Nikon reset values, physical FIFO capacity or a connected endpoint. The [fixed manufacturer driver](https://github.com/killsmm/m10v_mono/blob/cebff0686425b739bf63e87c911591ab30b03c1a/ca7_rtos/target/cpu_tkernel/basefw/MILB_API/Project/DeviceDriver/Peripheral/src/dd_spi.c#L1936-L1964) supplies the divider relation `input / (2*(DIV+1))`; the original Nikon DIVENABLE field is preserved independently.

`hw/nikon/devices/spi.c` implements one SysBus device per channel using upstream [SSI](https://github.com/qemu/qemu/blob/4fc49f46dc95d4a27de2509e7fceb2931e91faeb/hw/ssi/ssi.c#L156-L168), [Fifo32](https://github.com/qemu/qemu/blob/4fc49f46dc95d4a27de2509e7fceb2931e91faeb/include/qemu/fifo32.h), Clock and QEMUTimer. Word accesses move FIFO entries; status reflects occupancy and a pending frame; completed master frames produce received words, watermark/done events, and genuine GIC input state. Interrupt W1C clears latched events. Reads alone do not shift data or create completion. A master frame on an empty upstream SSI bus receives zero, the explicit disconnected MISO-low endpoint, not a device ID or a command acknowledgement.

Synchronous master frames retain the intended serial-clock cadence. The first
word is scheduled from the current virtual time; each immediately following
word is scheduled from the preceding intended deadline. A late main-loop timer
callback can therefore process already elapsed word deadlines without adding
host scheduling delay to every serial period. Empty FIFO, disable, close,
reset and cancellation discard that cadence. Completion from an asynchronous
peer also discards it because external backpressure, rather than the local
clock, determines when the next word can begin.

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

| Virtual-board input | Modeled behavior and boundary |
|---|---|
| Eleven independent 32-word FIFOs | Depth is a positive declared parameter; count, empty/full and overflow are derived from actual queue state. Real physical capacity is unmeasured. |
| Independent 48 MHz input | QEMU virtual time drives frame duration: AUX length plus one, enabled public divider or explicit no-divider bypass, with configured master-delay ticks. This is a nominal model, not an EXPEED performance estimate. |
| Board reset: asserted, empty, configuration zero | Channel `60=1` cancels the timer, clears FIFOs/EN/pending/slave-request and deasserts IRQ; it preserves configuration/enable mask so guest close/reopen remains coherent. `60=0` releases it. These reset side effects are explicit assumptions. |
| Common pending to first IRQ | Both original wrappers reach one ISR. All enabled pending events route to the first original IRQ `193+2*channel`; the second line stays disconnected. TX/RX physical routing is unproven. |
| No external slave clock or peer | Master uses upstream SSI; slave `64=1` records its external request but does not manufacture clocks or data. It keeps waiting until guest cancellation/timeout. |

CTRL/AUX support the actual constructed fields, including Nikon's AUX bit2 that the older header marks reserved. DMA requests on unbound ports fail; source-bound SPI2..4 use the demand wires above. EN includes the original channel8 selector exception. CS/polarity/framing configuration is retained; no physical chip-select topology or device response is invented on the disconnected bus. Unknown offsets, widths, reset commands and configuration bits return `MEMTX_ERROR` with the normal first-access stop. The device explicitly rejects migration/snapshot through upstream unmigratable VMState. The [whole protocol's exact field and unknown boundaries](https://github.com/mashiro01/nightglass/blob/e27bf43/research/z7ii/qemu-spi-protocol-contract.md) remain applicable apart from the now source-bound DMA path described above.

Native incremental build exited zero at C `2e1a2b62fdc99d0ab999fe7b022aaef3fa4e2916`. Eleven focused synthetic fixtures passed against that same C: seven in `run issue331/spi-fixtures-first/result.json`, then three in `spi-fixtures-more/result.json`, and a separate genuine QMP reset/second guest entry in `spi-board-reset-first/result.json`. They verify timed disconnected master transfer/readback, slave waiting, pending reset cancellation and preserved configuration, all eleven depths, genuine GIC IRQ193 acknowledge/W1C/EOI, actual RX FIFO overflow/ack, and width/hole/DMA/reset-command rejection. These bytes are synthetic test instructions, not recovered firmware; this is not a full-suite rerun or endpoint compatibility proof.

The unchanged MAS run `run issue331/original-spi-2e1a2b6/receipt.json` records the same complete C ref, QEMU exit zero, direct full-input comparison unchanged, and host wall time 0.202172 seconds. Its new first stop is **CPU0 PC `10360f88`, WRITE32 PA `b7f6a0ec ← 0`**, an uncovered RAM consumer. This MTTCG run reaches the other CPU's memory dependency before CPU1 enters the filtered SPI initialization root; it therefore does **not** prove all eleven original channel initializations or any original external SPI transfer passed. The older `50714df` first SPI-access failure remains unchanged.

The new `--trace-pkgb` uses upstream comma-separated `-dfilter` to observe the original candidate chain, with no guest modifications. In this run, `qemu.log` lines 5161–5166 show CPU1 at `10200684`, argument1, `R5/R6=1`, `R7=118afa78`, `R8=13`, `R9=8`, `R10=1`, `R11=2`, SVC stack `b51bb578`, LR `10200ccb`. Lines 5355–5360 show its descriptor `b1802060` at `10200694`; lines 5367–5372 show entry `10201010`, argument1 and handler pointer `10201011`, SVC stack `b51bb570`, LR `1020069b`. This connects the [fixed phase1/role1/slot1 descriptor+4 chain](https://github.com/mashiro01/nightglass/blob/f0b2efb/research/z7ii/qemu-rtos-startup-contract.md) to actual execution. It proves constructor entry, not phase completion, external peripheral success, a menu or whole-camera boot. The next work closes the full RAM allocator/object producer rather than adding a single writable word.
