# NDS32 native system execution

Custom implementations reside in `sim/nds32/`, `include/gdb/nds32system.h`, and `cmake/nds32.cmake`.
The official Andes provider remains pinned and read-only; CMake connects the native GNU build graph within an ignored source view.
No new ISA decoder is added; instruction execution, finite address mapping, exception unwinding, and virtual events continue using the GNU simulator.

## Architectural basis

- [AndeStar ISA v1.3](https://download.paulk.fr/coreboot/mt8173-spm/documentation/isa/AndeStar_ISA_v1.3.pdf), IRET pages 76–77, MFSR/MTSR pages 124/135, SETEND/SETGIE pages 162/163.
- [AndeStar SPA V2 v1.9](https://www.scribd.com/document/980985425/AndeStar-SPA-V2-UM026-V1-9), §4.2, §4.4, §9.3; this is a public mirror of the vendor manual; camera implementation still requires independent proof.
- [Andes Priority Application Note](https://www.andestech.com/wp-content/uploads/%E5%9C%A8%E6%99%B6%E5%BF%83%E5%B9%B3%E5%8F%B0%E8%BF%90%E8%A1%8C%E5%85%B7OSC%E7%9A%84FreeRTOS.pdf), pages 4–8.
- [Linux v5.17 NDS32 bitfield](https://github.com/torvalds/linux/blob/v5.17/arch/nds32/include/asm/bitfield.h) and [native IRQ driver](https://github.com/torvalds/linux/blob/v5.17/drivers/irqchip/irq-ativic32.c).

Transition 0→1 saves IPC/IPSW; 1→2 additionally saves P_* and P0/P1; 2→3 preserves prior registers, saving only OIPC and raising INTL.
IRET restores the corresponding tier based on INTL prior to instruction entry. Exceptions use GNU `sim_engine_restart`, so faulting instructions do not continue writing destination registers.
IRQs are selected at instruction boundaries based on GIE, mask, fixed or 2-bit programmable priority, and CPL; inputs can only be altered by actual devices.
Writing pending on level inputs cannot negate an external signal; edge latches are cleared by W1C.
The original four `standby wake_grant` instructions use explicit consent and wake inputs; external IRQs wake consented cores even when masked.
Without consent, execution does not advance to the next instruction; waiting phases process only GNU virtual events.

## TPJ functional configuration and boundaries

Original complete sources confirm: 25 vector slots, 23 IRETs, MASK/PEND using `0xffff`, IVB writes zero;
no PRI, CTRL, TRIGGER, or MASK2/PEND2 consumers. Vector count does not prove hardware NIVIC/version identity.
`--nds32-system` explicitly selects a functional configuration: 16-input IVIC v1, fixed priority, no shadow SP, physical addressing, little-endian;
this does not prove Nikon chip model, original BootROM, or reset values. Original software handles its own SP, COPY, ZERO, and IRQ enablement.

System mode no longer creates 64 MiB RAM by default. Callers must provide finite ROM/RAM and bind native read/write callbacks to each active device region via `nds32_system_attach()`.
Missing callbacks or rejected width/address combinations raise bus exceptions rather than returning zero or ACKs. Device regions are non-executable.
Callback values are copied by the device; caller opaque lifetimes persist until `sim_close()`.
Software interrupt outputs exceeding six inputs must be explicitly bound by callers without guessing board wiring.
`nds32_system_run()` provides finite execution quantums via GNU virtual instruction/event ticks; this is not a physically measured clock model.
Enabling MMU translation explicitly stops execution; unmodeled MMUs are not faked as identity maps.

## Integration and verification

The build owner provisions `NDS_UPSTREAM`, `native_file(path, content)`, and read-only source views.
`cmake/nds32.cmake` requires each upstream seam to match uniquely, linking `system.o`, native boundary hardware, and decoder hooks.
`configure.ac` utilizes existing `SIM_AC_OPTION_HARDWARE`; the build owner regenerates configure/config.in via compatible Autoconf,
compiling with native GNU configure/Make without editing official dependencies or using Python wrappers.

`tests/unit/nds/system.c` generates synthetic instructions using GNU opcode macros, executing IRET, IRQ selection, and bus exceptions through the authentic decoder.
Assertions cover nested recovery, overflow return, priority/CPL, level and edge lifecycles, authentic callback reads, and fault unwinds.
The sole build owner completed native compilation and linking of official providers, `system.o`, `libsim.a`, and `run` with `WITH_HW=1`.
ELF-less synthetic fixtures pass standard GNU `--endian=little`, with five test suites executing exit 0:
nested IRET, IRQ priority/CPL, finite MMIO and fault unwinds, privileged SR accesses, standby consent/wake.
Raw receipts reside in the build owner's ignored `work/validation/gui/ndsendiancheck.log`; original TPJ has not asserted completed boot.

## Native process entry

`sim/nds32/runner.c` employs BFD, the GNU simulator, and POSIX non-blocking socket/poll without extra executors or wrappers.
Complete ELF entry points are zero; each allocatable segment must be loadable, non-empty, VMA=LMA, non-overlapping, and fully cover `[0, e000)`.
The source owner confirmed existing artifacts satisfy this via GNU readelf; `0xe000` is the TPJ length in the update package, not proven ROM capacity.
Loaded ROM is read-only and executable; original software performs RAM COPY/ZERO and stack setup without host pre-copying or SP injection.

Transport shares `include/hw/nikon/wire.h` with the QEMU machine; the runner provides independent sockets for external touch controllers.
`--save FILE` exports complete and idle NOR state; re-importing preserves true write outcomes without mutating input files.
Completions use single-tick GNU events representing functional timing hypotheses rather than measured device frequencies. Missing backings, out-of-bounds accesses, and unknown commands remain busy and report actual boundaries.

`--transform FILE` accepts 24-byte external records: six little-endian input halfwords followed by six true output halfwords.
Requests to `53002` must match inputs before native events write record outputs and assert `53001` ready.
Without matching records, the device will not set ready, copy inputs, or invent transformation algorithms.

`--snapshot FILE` format: `hex_address decimal_width hex_value`, admitting only proven readable fields.
Busy flags, flash read latches, flash sums, and transformation ready states cannot be injected via this file; data origin remains provider responsibility.
Inputs for `6001c..21`, `50014/54/94`, `72024`, `721c0`, `80050` follow proven ISR consumers;
inactive events, default-zero read/write configuration, and `71010` idle-high notifications represent functional cold-start conventions, not physical reset proofs.
Notification wires output only after the guest writes `71010`; initial backings do not emit premature ready/high levels.
Level IRQs clear after the guest clears hardware flags without host intervention in RAM flags.

`--limit TICKS` bounds instruction/event budgets for initial observations, reporting actual PC and sleep state; functions stand-alone without sockets.
The device emits PC, address, width, and cause upon encountering the first missing producer or invalid field.
Normal touch input uses shared typed I2C START/WRITE/READ/NACK/STOP, matching MAS address `55`, cmd10/read2, cmd11/read7.
Hardware fields drive authentic ISRs and callbacks; READ accesses only `60040` output written by the guest without synthesizing command responses.
See [Touch Contract](../devices/touch/protocol.md) for source evidence and wiring hypotheses.
SPI CLOCK retains original tokens pending authentic binding without borrowing I2C or EX frames; `720xx` remains configuration/status/buffer space.

`tests/unit/nds/tpj.c` utilizes synthetic external files covering NOR read latch, incomplete payloads, count-1/NOR programming,
6-halfword external outputs, original event level deassertion, exact reply tail bounds, and missing-producer/register-hole rejection.
The sole build owner compiled and linked the runner, executing endpoint fixtures with exit code 0.
The initial limited probe of original ELF stopped at offset `0xb3fe` (`bnes38` in original COPY helper) within the first 4096-tick quantum.
The unknown signal 0 in that log arose from an incorrect enum in the host budget callback: official `SIM_SIGNONE` is 64 rather than literal 0.
Correcting the stop signal leaves runner non-zero fault checks intact without altering guest PC or skipping COPY.
`tests/unit/nds/budget.c` independently validates dual-budget stop/continue, ensuring non-zero native stops are not accepted as budget completions.
The budget-fix fixture exited 0; original code proceeded to `0x0b22` reading `41011`, reporting absent external flash backing.
Typed I2C, real-time NOR checksum/save, explicit update snapshots, and `50018` count/IRQ/ACK groups executed six fixture suites exit 0.
With explicit `--update`, the complete ELF passed external NOR verification at `0x0ca4`, advancing to `0x0a966` reading `80042`.
Subsequent work completed the `80043` request / `80042` completion at `0x0a94a`, 11 channel configurations, 384 halfword stream configurations, and `80216` self-clearing requests,
preserving notification edges in execution quanta and halting at native events after the first boundary. Nine focused fixtures exited 0;
original code proceeded to `0x0a588` WRITE8/PA `801a0`, halting after a single genuine bus fault (receipts in ignored `work/tpjcontrollerqualified.log` and `work/tpjcontrolleroriginal.log`).
Source analysis covers succeeding 4-byte `801a0..a3`, 9-page `801ea/801e0` sensing setups, and three `60003` bit 4 predicates.
These fields preserve guest configuration or matching command IRQ causes without synthesizing ADC samples, scan counts, 720 ready, or contact points.
Twelve fixture suites for normal sampling exited 0 (`work/tpjsamplesqualified.log`). Normal processing and explicit quiescence in pinned `3935ace9`
completed 16 native fixtures exit 0, preserving 11 negative boundaries and fault assertions (`work/tpjfreezemodenative.log`).
A full-source run with 14 startup input records exited naturally within a 2,000,000 tick budget at PC `0x8ec8` (`normal9/capture2/reference2/BUFFER4/quiesce1`) without boundary violations (`peer-transport/work/runtime/touch/startup.log`).
Wire commands and guest replies remained zero in that run; completed boot and functional menu operation are not claimed.
