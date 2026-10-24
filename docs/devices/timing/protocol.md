# Twelve-channel functional timing controller

Current extensions are the [divider/load contract](load.md),
[capture](capture.md), [compare](compare.md) and [finite extra-feature
budget](extra.md). The last supersedes the historical unsupported enable38/
result40 boundary on channels0..4 and records its explicit functional assumptions.

This document freezes the source1 checkpoint. The later [external capture extension](capture.md) separately records its interface, native checks and next original dependency; earlier receipts below keep their original scope.

The current [divider/load contract](load.md) supersedes the historical opcode
interpretation below. Bchannel+4 is the source's eight-bit divisor-minus-one
field: raw31 means divide by50, not one-shot. All256 byte encodings use the
same configured/stopped basic-channel initial-count submission, scaled counts
and periodic wrap; raw1 no longer has a special once-only threshold. The
normal original run now crosses all seven channel7 waits and stops on a
capture-control mask write at PA20068000. Twelve focused native cases pass;
source joins, precise limits and receipts are in that current contract.

## Shared value and channel commit

The finite [initial-count load](load.md) now accepts the original channel9
shared-value/one-channel submit and reaches its original callback with status1.
The next unchanged-firmware stop is a new fill destination at PA20070010.
The extension records source semantics, virtual assumptions and native receipts
separately from the historical checkpoints below.

Code/native/fixture recipe `51b8e37ee709040fdc7328a53362fc6fc7fef3b6` implements the finite timing/control protocol at2c010000/2c011000, including an independent direct-source1 virtual counter profile. The [fixed whole source contract](https://github.com/mashiro01/nightglass/blob/69c9345438b35c459f569b3bc50bf66af8012a72/research/z7ii/qemu-multichannel-timing-contract.md) exports12 original objects/register-pointer tables, IRQ172..183 and setup/callback callers. Saved-native coverage remains partial; bounded original-byte methods are not all accepted native CFGs. The high caller's3,000,000,000/period arithmetic supports a timing role. No source/destination/descriptor producer exists in this evidence. The manufacturer XDMA layout differs and is rejected; this module performs no DMA memory copy.

## Source interfaces and named virtual assumptions

Global A+0 is the original1/0 reset-like pulse, A+4 the original1 configuration. Global B+0 is the guest's shared12-bit channel bitmap; B+c accepts the original initializer0. Every channel is at+100+100*n in each window. A+0 retains the supported basic control0/1; A+4/+8 support their zero-disabled basic configuration. A+10 contains generated pending status, acknowledged by original ISR read/writeback. B+0 is channel reset, B+4 opcode, B+8 period−1, B+c current-counter getter. Unknown globals, A+c/+14 and unsupported nonzero optional mode/feature operations fail rather than form a regfile. Original setup's capability-specific optional enable0 writes are accepted only at their actual pointer-backed offsets/channels; absent capabilities remain unavailable. Shared B+4/+8 supports only the initial-count load described above.

The virtual profile explicitly assumes zero reset/configuration/bitmap/status, A reset1 assertion cancelling all channels, channel reset1 cancelling and clearing its own state, and master/bitmap/local control gates. Counters are not started until the guest supplies a period. Gate disable freezes the current count and cancels its timer; reenabling resumes it. Period programming clears elapsed/completed state and schedules a fresh interval only when all gates allow it. One-shot completion remains latched until period/reset reconfiguration. There is no input edge or PWM waveform provider.

Opcode31 is modeled as **one-shot**, opcode0 as **periodic**, and opcode1 as a **free-running direct source**. These meanings, threshold status bit0 and ISR **W1C** are named functional conventions; the numeric opcode1 hardware meaning is not established. Periodic expiry wraps and can report again after acknowledgement; one-shot freezes at period+1. Direct-source1 continues counting and exposes the low32 count bits. It reports its configured threshold once; acknowledgement or gate pause/resume does not rearm that report. Period programming or reset clears the report state. No read creates an event. Pending state drives the actual channel GIC IRQ; the host never calls a guest callback or sets a software event flag.

The complete activation state uses the existing master, shared bitmap, local control, reset and programmed-period gates. Supported configuration is checked before writes mutate it; changing a running opcode, unsupported capture enable, invalid bitmap bits and unsupported shared submissions reject while retaining prior supported state. No unsupported feature value is retained for a later global enable to activate. Optional capture/compare/PWM requests and other status causes remain unavailable. In particular accepting source1 does not produce capture results or external edges.

A separate upstream Clock uses **nominal3GHz**, matching the software arithmetic scalar only as a virtual board input. Counts derive from elapsed virtual nanoseconds, never read-count increments. Encoded periodFFFFFFFF is expanded in64 bits to4,294,967,296 ticks, approximately1.431655765 seconds at this nominal clock; it is neither zero nor a proven hardware infinite mode. QEMUTimer events round up to nanoseconds and check actual elapsed ticks. This is not a physical frequency, clock-source identity or EXPEED benchmark. The module reuses pinned QEMU [Clock](https://github.com/qemu/qemu/blob/4fc49f46dc95d4a27de2509e7fceb2931e91faeb/include/hw/core/clock.h), [QEMUTimer](https://github.com/qemu/qemu/blob/4fc49f46dc95d4a27de2509e7fceb2931e91faeb/include/qemu/timer.h), MemoryRegion/SysBus and IRQ APIs; state is explicitly unmigratable. MMIO operations are aligned32-bit words; unknown widths/holes/requests fail.

## Current exact acceptance and next original dependency

Incremental native51b8e37 built with exit0; upstream checkpatch reports0 errors/0 warnings. All18 copied board source/header files were checked by direct byte equality against the frozen checkout, recorded in `work/issue331/timing-source1-51b8e37/upstream-source-equality.json`.

`work/issue331/timing-source1-51b8e37/result.json` binds exact native and recipe51b8e37 plus upstream4fc49f46. **24 timing fixtures PASS** in2.313630292 seconds: the previous13 and11 source1 tests. New checks exercise MAX without premature status, continuing count after a once-only threshold acknowledgement, bitmap/master/local pause and resume, reprogram rearming, reset cancellation, real GIC IRQ177/EOI, and invalid opcode/active opcode change/capture/bitmap requests retaining prior opcode1, MAX period and bitmap. Existing opcode0/31 behaviors remain independently checked.

`work/issue331/full-synthetic-51b8e37/result.json` binds the same exact native/recipe to **339 complete local macOS synthetic fixtures PASS**,15.567963958 seconds. The list is checked for unique names before any guest runs and includes exactly six ring fixtures,23 bridge lifecycle fixtures and24 timing fixtures. This is the requested whole-suite regression after the duplicate-ring harness repair and bridge late-lifecycle fixes; it is not Linux CI, physical timing or original firmware boot proof. GitHub Linux jobs remain blocked by account billing at this stage.

Changed-stage original `work/issue331/original-timing-source1-51b8e37/{receipt.json,qemu.log}` binds exact native51b8e37, pinned upstream QEMU4fc49f46/version11.1.2, all model assumptions and complete launch arguments. QEMU exits0 after0.1938659999286756 seconds of host observation; wholebootfalse and complete original payload direct equalitytrue. It crosses the prior opcode1 write rejection and stops at **CPU1 PC1036e8fc /WRITE32 PA2c011610=1**, unsupported optional capture enable. Its DFSR1808/DFAR2c011610 is the bus rejection. This does not prove original counter activation, expiry or capture results. CPU0's concurrent final PC1010e7b8 is not an independently established higher-task outcome. The whole capture input/result/consumer protocol is the next dependency; it is not accepted as a bare enable word.

The preceding original228 and HBP228 raw receipts remain unchanged. HBP captured the actual20-byte channel5 packet `[0,FFFFFFFF,1,1,0]`: offset+c is reset pulse1; the trailing shared-submit flag+10 is0. Its encoded operation1 is the direct high-caller branch, not the distinct branch generating31. MAX can arise from a sentinel or zero period minus one; it is not hardware-infinite proof. That observation ended with debugger cleanup failure after valid capture, not a new successful whole original run.

## Historical initial timing stage

Incremental native codea435cda built with exit0. `work/issue331/timing-a435cda/result.json` records13 focused synthetic PASS: one-shot expiry/count/W1C, periodic second expiry, bitmap pause, pending reset, real channel11 GIC IRQ183/EOI, local stop, actual second-entry system reset, unknown opcode/optional capability/shared-submit/counter-write/width rejection. The final document stage wraps one macro initializer line without changing functional C. No whole old suite or physical timing result is attributed to these fixtures.

Original `work/issue331/original-timing-a435cda/{receipt.json,qemu.log}` binds the full exact C ref, QEMU4fc49f46 v11.1.2, explicit timing/Audio/alternate-bank assumptions, QEMUexit0,0.195035333s host observation, wholebootfalse and complete original payload direct equality true. CPU1 executes wrapper1020103a (log6498), after the previous timing initializer, then the first uncovered transaction is **PC10294c34 /READ32 PA04251000** (6518), followed by DFSR1008/DFAR04251000 (6558). This proves original initialization progress, not original timer callbacks, the whole enclosing constructor/phase, or menus. Synthetic clock/IRQ checks remain separately scoped.

CPU0 concurrently has an IRQ snapshot at10100018 with R0=0, resource pointerb185ed14 and LR103bf9ac. That snapshot alone does not prove valid NV contents, default restoration, a parser return or complete unwind. The expanded original record trace remains available for a separate source/line join. The previous180/07 receipts remain untouched.

The next four-channel family at04251000 is followed as a whole source/configuration/IRQ protocol. Unknown operations remain visible, and original O178, code, contexts and IRQ handlers stay unchanged.
