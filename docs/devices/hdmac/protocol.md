# Paired HDMAC functional DMA model

Review correction: exact C`7b4327d74677dae537e4834e4533508448a37bea` built natively and passed22 HDMAC fixtures at `work/issue331/hdmac-state-7b4327d/result.json`. No original run was repeated for this correction; the original execution evidence below remains exact C`84c9e02`, not relabeled as7b.

CA, CB, source/destination changes while EB is armed now share a pure candidate preflight, including width/address alignment. DE enable preflights all armed channels before mutating control or timers. Rejected changes preserve the old state. New soft and external-request negative sequences arm unaligned byte addresses with DE off, then attempt a word-width update: both fail and monitor readback verifies original CA/CB/SA/DA. A positive sequence fixes the addresses before changing width and then completes a real word copy.

Every DMA operation carries a generation. Cancel, reset or a new operation changes it; the callback checks both generation and running state immediately after each synchronous bus read/write, before handling the result or continuing. A genuine DMA write to its own global DE0 retains SS2/EB-clear and never emits CI completion. A two-beat DMA write to its own CA clears EB on the first beat and never issues the second beat. These are real bus side effects, not a blanket ban on MMIO sinks. The five new regressions and17 prior HDMAC contracts passed against exact7b, with old failure/passing receipts preserved.

This stage continues [运行 Z7II 原版 QEMU machine 并恢复启动设备依赖](https://github.com/mashiro01/nightglass/issues/326). Exact C`84c9e020000519fba01529414309cb24bfcaf4e8` built natively against fixed QEMU`4fc49f46dc95d4a27de2509e7fceb2931e91faeb`, passed seventeen focused synthetic checks and ran unchanged MAS to the next CPU1 access: `PC1036d334 / WRITE32 PA2c018028=2ff`. Same-run CPU0 later aborted on a separate `12e00004` source access. Menu/whole boot, valid NV data and original DMA transfers are not established.

The [bounded original HDMAC contract, e90a97e](https://github.com/mashiro01/nightglass/blob/e90a97e/research/z7ii/qemu-hdmac-contract.md) contains700 bytes/281 instructions, two banks/eight channels each, sixteen36-byte objects and sixteen register-pointer tables. Historical native coverage is26/94 and296/606 bytes; remaining method sections are bounded original-byte candidates. Original selector argument0 chooses`2c020000`, nonzero chooses`2c030000`. Bootstrap sets priority PR28 and DE31 in each global. The original object table joins bank0 to IRQ150..157 and bank1 to IRQ142..149, and real guest ISR wrappers/callbacks; host code never invokes those callbacks.

The [Socionext-authored header](https://github.com/killsmm/m10v_mono/blob/cebff0686425b739bf63e87c911591ab30b03c1a/ca7_rtos/target/cpu_tkernel/basefw/MILB_API/MILB_Header/include/Peri/_hdmac.h#L10-L65) supplies the matching global and eight×16-byte register layout, at different reference addresses. The [fixed v6.12 Linux driver](https://github.com/torvalds/linux/blob/v6.12/drivers/dma/milbeaut-hdmac.c#L21-L53) independently gives CA/CB fields; [its transaction/acknowledgement](https://github.com/torvalds/linux/blob/v6.12/drivers/dma/milbeaut-hdmac.c#L114-L185) is a software/IP reference, not a QEMU device or Nikon chip/reset binding. The [author count computation](https://github.com/killsmm/m10v_mono/blob/cebff0686425b739bf63e87c911591ab30b03c1a/ca7_rtos/target/cpu_tkernel/basefw/MILB_API/Project/DeviceDriver/Peripheral/src/dd_hdmac0.c#L836-L838) gives bytes=(BC+1)×(TC+1)×2^TW. [Field/status constants](https://github.com/killsmm/m10v_mono/blob/cebff0686425b739bf63e87c911591ab30b03c1a/ca7_rtos/target/cpu_tkernel/basefw/MILB_API/include/dd_hdmac0.h#L238-L320) support width0/1/2, fixed/increment addresses and SS error/completion meanings.

The model uses two finite SysBus instances: global+0 and eight channels at+10+index×10, with CA/CB/SA/DA at+0/+4/+8/+c. Aligned word accesses only. Virtual reset clears all fields/status/timers, with DE off and no pending IRQ. Global PR chooses fixed low-index or rotating arbitration among virtual-time-ready requests; cursor starts0 and advances after a serviced channel. This is an explicit functional arbitration policy, not measured hardware concurrency or throughput.

DE and EB arm a request; IS0 additionally requires software ST. Nonzero IS remains armed with no completion while no DREQ producer is connected. Implemented transfers support non-reload byte/halfword/word beats, fixed or incrementing source/destination, block/burst/demand field encodings0..2 and ordinary non-wrap beat types. Channel PB and global DH1 suspend the same request; DH0 resumes the bank. Protection/pitch fields, reload, wrapped bursts, DH2..15, DS and transform encodings remain explicitly unsupported when requested. TT must be0, width/mode3 are refused; unknown registers/widths/reserved fields fail. Unsupported requests cannot become successful linear copies.

An independent official Clock48MHz provides one nominal tick per configured beat. A software request waits that many virtual ticks, then its finite transfer runs through official `dma_memory_read/write` in one event. External requests accept one beat per event and recheck their actual wire. Other CPUs do not observe an intermediate streaming timing model. Sources and sinks are the existing real board address space, not debugger writes or private successful memory. Each beat honors fixed addresses and width; source/destination bus failure produces SS3/4, address wrap SS1, success SS5. CI gates successful IRQ and EI gates errors. Completion clears EB and end counters; error/cancel retain the request counter fields. This readback convention and DE-disable/EB-clear cancellation→SS2 are explicit functional assumptions; physical partial-transfer count/retry details remain unverified. Real bus writes completed before an error are not rolled back. Reset and cancel remove pending timer callbacks.

CB readback preserves guest configuration and device-produced SS. Writes may preserve existing SS or clear it, never create arbitrary nonzero completion. The original ISR's CB&~`1f0000` clears SS/CI/EI and deasserts the real GIC line. No read callback manufactures completion, source data or IRQ. Stateful devices use official unmigratable VMState.

`work/issue331/hdmac-84c9e02/result.json` records twelve checks: real word copy/ack, fixed source, disconnected external wait, cancel-before-copy, both bank/channel7 geometries, causal source/destination bus errors and five refusals. Unknown DMA endpoints retain the board debug stop; tests then read CB through the monitor to verify SS3/4 instead of expecting guest continuation across that stop. An earlier `hdmac-d333da0` attempt preserved the incorrect harness expectation and failed on the first unknown source endpoint; its raw logs are not a passing batch. `hdmac-extra-84c9e02/result.json` adds five checks: fixed versus rotating arbitration, actual GIC IAR150/EOI, byte/halfword counts and true QMP reset/second entry. All seventeen successful checks bind exact C`84c9e02`. Later C line wrapping and fixture registration do not imply another native/original run.

The original receipt `work/issue331/original-hdmac-84c9e02/receipt.json` records QEMU exit0, host0.243680167 seconds, full38,239,096-byte input equality, debug stop and `whole_boot_verified=false`. First uncovered log entry6074 is CPU1 `PC1036d334 / WRITE32 PA2c018028=2ff`; lines6075..6078 show its Data Abort with DFSR1808/DFAR2c018028. Lines6080..6083 then show CPU0's Data Abort with DFSR1008/DFAR12e00004. The once-only shared uncovered-access logger does not list the second CPU's access separately; absence of that second MMIO line is not success. Final CPU0 PC10100010/LR102a7d9c is an abort context, not proof of valid NV data. Neither exception is bypassed.

Next dependencies are the complete `2c018000` device and CPU0's independently bound source/consumer near`12e00000`. Hardware request endpoint wiring, protection transactions, real reset and physical DMA timing remain unverified. The model does not measure EXPEED performance or establish flash/BootROM/calibration health.

## Issue397 lifecycle extension and remaining dependencies

[QEMU：整批闭合 DMA 与音频的启动协议](https://github.com/mashiro01/nightglass/issues/397) audits this class together with copy, fill and Audio against the saved contracts. The author [pause/resume implementation](https://github.com/killsmm/m10v_mono/blob/cebff0686425b739bf63e87c911591ab30b03c1a/ca7_rtos/target/cpu_tkernel/basefw/MILB_API/Project/DeviceDriver/Peripheral/src/dd_hdmac0.c#L635-L705) writes PB1, then PB0/EB1; [bank halt/resume](https://github.com/killsmm/m10v_mono/blob/cebff0686425b739bf63e87c911591ab30b03c1a/ca7_rtos/target/cpu_tkernel/basefw/MILB_API/Project/DeviceDriver/Peripheral/src/dd_hdmac0.c#L1049-L1065) writes DH1/0. These source operations justify a preserved-request lifecycle, without establishing Nikon pause status7, interrupt or EB-clear timing.

The named functional model holds remaining beats, addresses, EB, existing SS and the unelapsed nominal timer delay. PB, DH and a deasserted DREQ are conjunctive gates: clearing one cannot override another. Holding does not emit status or an IRQ. If a real DMA bus write changes PB or DH, the accepted beat commits once and the next beat waits. Cancel/reset still invalidate the generation and cannot resume a stale request. Completion and error IRQs retain the existing causal bus rules. CA configuration changes during an active transfer still reject, except PB and EB lifecycle operations; global enable retains the all-channel preflight.

Native test content `tests/qtest/nikon/dma.c` supplies seven synthetic cases: all sixteen channel/IRQ geometries, both-bank halt, held cancel/reset, self-MMIO PB/DH writes and an Audio FIFO freeing a real DREQ while PB remains asserted. Registration, compilation and execution belong to the build owner; this document does not claim those new tests have run. All older receipts above retain their original code identities. No original payload execution is repeated for this extension.

The first integrated native run compiled but failed the first resume destination check (`work/validation/gui/dma.log` in the build owner's `native-qemu-source` worktree). Its command passed `-S` with the qtest accelerator. Upstream `system/cpus.c` disables the virtual clock when pausing CPUs, and `util/qemu-timer.c` advances qtest time through timer lists that honor that enable state. The fixture now leaves the qtest virtual clock enabled; qtest does not execute guest instructions. Resume and reentrant-start checks advance to the next actual timer deadline, while held/canceled-state observations still advance a positive interval and Audio completion spans its real repeated clock events. This corrects the fixture's timer eligibility rather than changing device status or transfer semantics. Native validation of this child remains the sole build owner's task.

Unbound reload/wrap/protection/DS/transform semantics are future protocol work, rather than counted as additional devices. Disconnected peripheral requests require a real producer or declared external input. The only currently bound Audio wires are bank1 selectors3/7; input5 has no samples. No source joins these DMA signals to the separate event/EXIU interfaces, so no frame-completion signal is inferred.

## Private component DMA

HDMAC accepts a standard pre-realize `memory` link to a `MemoryRegion`, following
the Fill execution-domain interface. An unset link uses MAS system memory.
Each realization owns a fresh heap `AddressSpace` over its selected root; all DMA source
reads, destination writes and read-only checks use that same root. A real MMIO
FIFO remains a peripheral transaction in that domain, including its demand
line transitions and reentrant cancel/pause behavior. No RAM-only restriction
is imposed on HDMAC endpoints. Read-only destinations report SS4 instead of
accepting QEMU's ignored ROM write; genuine read and write errors retain SS3/4.

An official MemoryListener tracks effective read-only spans in the selected
address space, including permissions inherited from aliases or containers and
live topology changes. This avoids ascending into another root's address space.
The destination guard checks the complete beat before issuing the bus write.
A halfword or word crossing writable RAM into ROM is rejected
before changing either span. MMIO translation is checked byte by byte without
performing bus accesses; the actual peripheral write retains its original width.

Unrealize first cancels every channel and invalidates its generation using the
existing reset lifecycle, then retires the owned address space through QEMU's
`address_space_destroy_free()`. Its heap storage remains valid until QEMU's RCU
callback finishes, independently of device finalization or a new realization.
It does not
free the linked board memory. Normal reset retains the memory link and clears
only HDMAC state. Timer callbacks cannot survive device removal or move a beat
into another component's root. The eight channels, 32 request inputs, existing
IRQ outputs and per-beat external-demand rules remain unchanged; port-to-bank,
request-selector and IRQ binding is supplied by each component's board source.

`tests/qtest/nikon/hdmac.c` provides four independent native cases: real shared
file-backed private RAM at the same PA as a MAS sentinel, selected unmapped and
read-only endpoints, real Audio MMIO FIFO side effects through its own root,
and all 31 external request selectors with held/resumed beats plus cancel and
reset. The fixture uses standard QOM links and real MemoryRegions without
production aliases or fabricated completion. Compilation and actual execution
remain the sole builder's responsibility; source provision is not a PASS claim.

The complete SLA driver family `1005f644..1005f8aa` preserves the same register
geometry and lifecycle. Its setup writes IS/BT/BC/TC into CA at bits24/20/16/0
and TT/TW/SA/DA/SP/DP/RL into CB at bits28/26/25/24/23/22/21. The entire SPI
consumer pair `1005c70e..1005c7fa` sets BT=BC=SP=DP=RL=0, TT=2, TW=1,
TC=halfword-count-minus-one. RX fixes SA; TX fixes DA. The optional opposite
fixed address uses the same preserved SA/DA field. The start family sets CI/EI
and EB, clears ST for external requests, and uses ST with IS=0 for software
requests. All of these configurations are supported by the existing HDMAC
model; mode4's CTRL400 belongs to the SPI endpoint rather than HDMAC global
control. No transfer transform or reload mode has been inferred.

SLA COPY records `C1004db8` contain the sixteen driver objects: bank0 handles
12a..131 / IRQ150..157 and bank1 handles132..139 / IRQ142..149. The register
pointer table `C1004cb4..C1004da4` gives the actual two physical banks2c020000
and2c030000. Eleven SPI consumer records at `C1007238`, stride68hex, provide
TX selector `3+2*port` and RX selector `4+2*port`, port0..10. These selectors
all fit the existing request namespace. Connecting each SPI producer to both
banks is a board functional routing assumption, distinguished from the
directly recovered selector and IRQ records; the shared HDMAC device neither
guesses this wiring nor emits demand or completion on behalf of a producer.

### Native execution receipts

The sole builder compiled and linked the device and independent native fixture.
All four cases passed across their focused runs: private RAM and read-only
destination in `work/hdmacprivatefirst.log`, all31 request selectors with actual
pending-timer cancel/reset in `work/hdmacrequestsfinalreceipt.log` (0.17 seconds),
and the real selected Audio FIFO in `work/hdmacfifocapacityfinalreceipt.log`
(0.69 seconds). The first receipt contains passing private/read-only TAP cases
followed by a failed FIFO case; the whole first run is not reported as PASS.
Those paths
belong to the canonical `qemu-submodule` worktree. Earlier failed FIFO receipts
remain separate: one used an incorrect relative address for an already mapped
MemoryRegion root; another submitted two beats into the reset one-stage Audio
FIFO. The passing fixture uses its real mapped addresses and configures the
established sixteen-stage profile. Device error results and success assertions
were retained throughout.

The separate SLA owner fixture also passed its entire eleven-port TX/RX group
using private HDMAC banks, real SPI FIFO/demand wires, actual
SSI input and a distinct MAS sentinel, recorded in
`work/slaspidmafinalreceipt.log`. Its native duration is 0.71 seconds; the
0.85-second tool wall time is not the native test duration. These fixtures establish
functional address-space and protocol isolation, not original SLA boot, a
completed GUI or physical DMA timing. No older passing case was rerun solely
to relabel it as a newer receipt.

### Native boundary and retirement regressions

`tests/unit/nikon/memory.c` links the actual ARM system library and initializes
QEMU with its qtest accelerator and a `none` machine. It creates real devices
and independent MemoryRegion roots without production test aliases. The
fixture roots live under QEMU's standard objects container, so link properties
resolve a real canonical path. It asserts both the selected QOM root and an
actual source bus read before submitting the boundary DMA request. Earlier
native runs failed with source SS3 because their detached QOM owners produced
an empty link path and selected system memory; they did not exercise the
destination guard and are retained as failed receipts.
The boundary case submits aligned halfword and word beats across writable RAM into
read-only RAM, read-only aliases and read-only containers, checks SS4/EB-clear
and verifies every destination byte remains unchanged. Live permission changes
allow a real copy after removing protection and reject it again after restoring
protection. The retirement case repeatedly queues an old-domain DMA timer,
unrealizes both HDMAC and Fill, realizes them against the other real root before
draining RCU, then performs actual copy and fill operations after retirement.
It verifies the old timer was canceled and the old root remains unchanged;
device finalization also precedes the final RCU drain.

The sole builder compiled and linked the actual full ARM system-library unit.
Both new error families passed across their focused runs. The six boundary
combinations and their live permission transitions passed in
`work/memoryunitrootreceipt.log`; that run subsequently aborted in the
lifecycle fixture and is not a whole-run PASS. Its actual cause was attempting
`qdev_realize()` on a device whose QOM unrealize had retained its parent bus.
The corrected fixture uses the standard `realized=true` property to re-realize
that same attached device. The focused lifecycle run passed in
`work/memoryunitlifecyclereceipt.log`, exit0, 0.79 seconds. It exercised all32
alternate-root retirements, real copy/fill after prior RCU callbacks, canceled
old timers, and finalization before the last deferred callbacks. These paths
belong to the canonical `qemu-submodule` worktree. Earlier source-SS3 and bus
reattachment failures remain preserved separately; no passing boundary case or
prior four-group HDMAC fixture was repeated to relabel its receipt. These are
synthetic correctness results, not original boot or GUI evidence.
