# EXIU pad interrupt functional model

This stage continues [unchanged-firmware startup](/startup/first). Code `bbbf191aa179578174492a21b6d02be4a45deba9` built natively against QEMU `4fc49f46dc95d4a27de2509e7fceb2931e91faeb`; thirteen focused synthetic checks passed. The unchanged MAS passed the preceding EXIU initializer stop and next stopped at CPU1 `PC102dc38a / READ32 PA2c020000`. Neither a valid NV header nor a menu is established.

The original bounded protocol is fixed in [EXIU research, 715d941](https://github.com/mashiro01/nightglass/blob/715d941/research/z7ii/qemu-exiu-contract.md). Its 672 bytes/278 continuous Thumb instructions include registration, initializer, configuration, common ISR and sixteen wrappers. Saved native coverage is only 0/166 registration bytes, 0/14 initializer bytes and 310/492 runtime bytes; other sections remain original-byte candidates anchored by actual access or data, not fully recovered native functions.

The COPY0 object table has 232 records of56 bytes. Sixteen enabled interrupt objects, indices84..99, specify external bits0..15, GIC IRQ65..80, original wrapper addresses and PDR sources: GPIO5 pins7..15 and GPIO6 pins0..6. These are software routing records, not identified physical keys or wires. The initializer actually writes SRCSEL0, W1-clear`ff`, then MASK`ff`; the latter leaves upper eight channels unmasked rather than preserving reset MASK`ffff`.

The [Socionext-authored candidate header](https://github.com/killsmm/m10v_mono/blob/cebff0686425b739bf63e87c911591ab30b03c1a/ca7_rtos/target/cpu_tkernel/basefw/MILB_API/MILB_Header/include/Arm/_exiu.h#L322-L331) has the same eight register offsets, at a different base. Its [external/software selection implementation](https://github.com/killsmm/m10v_mono/blob/cebff0686425b739bf63e87c911591ab30b03c1a/ca7_rtos/target/cpu_tkernel/basefw/MILB_API/Project/DeviceDriver/ARM/src/dd_exiu.c#L285-L319) and [polarity/detection implementation](https://github.com/killsmm/m10v_mono/blob/cebff0686425b739bf63e87c911591ab30b03c1a/ca7_rtos/target/cpu_tkernel/basefw/MILB_API/Project/DeviceDriver/ARM/src/dd_exiu.c#L377-L457) support the IP protocol candidate. The independent [fixed upstream Linux driver](https://github.com/torvalds/linux/blob/8934827db5403eae57d4537114a9ff88b0a8460f/drivers/irqchip/irq-sni-exiu.c#L40-L120) supplies W1-clear, mask/trigger semantics, including the fact that a still-active level cannot be cleared. Its 32-input SynQuacer instance does not identify this Nikon device.

| Offset | Implemented software interface |
|---|---|
| 00 | Sixteen mask bits; 1 suppresses the corresponding GIC line |
| 04 | External-source selection0 only; software selection fails explicitly |
| 08 | Request=`latched pending & ~mask`, an explicit functional convention |
| 0c | Raw request=`latched pending`, an explicit functional convention; no selected original consumer |
| 10 | Write-one-clear; active level requests immediately reassert |
| 14 | Low/falling0, high/rising1 polarity |
| 18 | Level0, edge1 detection |
| 1c | Software input remains unsupported: persistence/clear semantics are unproved |

Only aligned word accesses and bits0..15 are supported. Status writes, clear-register reads, software input, unknown width/offset/high bits return `MEMTX_ERROR` and retain the first-stop record. Virtual reset is all masked, high polarity, edge detection, no pending requests. GPIO reset gives unconnected input-low pads; subsequent pad levels come from the existing latch/direction/function/pull model, not a constant EXIU input. These resets and exact raw/request filtering are declared board assumptions. They are not hardware reset measurements.

GPIO now publishes standard QEMU pad GPIO outputs. The board connects the sixteen original software routes to standard controller inputs and GIC outputs; no host ISR, task flag or key event is supplied. A real pad transition to the selected polarity latches an edge even when masked. Configuration changes never synthesize edges. Level requests latch and remain clearable only after the active level drops. Reads do not generate requests. The stateful controller explicitly disallows migration/snapshot through official unmigratable VMState.

Native build exit0 and registration reverse-apply check exit0 are recorded for the integrated source. `run startup/exiu-bbbf191/result.json` records ten synthetic checks: edge/mask/latch/ack, level clear/reassert, all sixteen GPIO pad routes, actual GIC IAR65/EOI, configuration without false edge, and five refusal cases. `exiu-extra-bbbf191/result.json` adds falling edges, actual IAR80/EOI and a true QMP reset/second guest entry. These thirteen checks use exact C`bbbf191`, upstream `QEMUMachine`, guest MMIO and genuine GIC acknowledgment; they are synthetic device verification, not camera firmware boot. Later source wrapping and the additional fixture registration do not constitute another original run.

`run startup/original-exiu-bbbf191/receipt.json` binds exact C`bbbf191`, pinned upstream, launch arguments, reset/final registers and direct full-input byte equality. QEMU exited0 after0.243576541 seconds of host observation, debug-stopped; all38,239,096 MAS bytes were unchanged and `whole_boot_verified=false`. The first uncovered access is `qemu.log:6031`: CPU1 `PC102dc38a`, read32`2c020000`. Final CPU1 is already in the original Data Abort path, so its abort LR is not a caller edge. CPU0 finalPC`10100008` is an original SVC vector context; it does not certify NV validity, defaults or PKGB completion. Existing older failed-prefix receipts remain intact.

The next whole-device dependency is `2c020000`. Physical pad providers, key topology, electrical polarity, actual reset and software-source behavior remain unbound. This model reduces only covered software risks; it does not prove factory NV/calibration, BootROM/flash health or EXPEED timing/performance.
