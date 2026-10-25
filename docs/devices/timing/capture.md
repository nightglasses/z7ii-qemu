# Gated external capture profile

The later [divider audit](load.md) supersedes this checkpoint's source1 timing
interpretation. The current narrow capture profile still requires raw1 at
Bchannel+4, but that byte now means divisor2. Samples use the divided wrapping
counter; there is no once-only source1 threshold. The additional native capture
case verifies counts300/600 after two200ns active intervals at the declared
nominal3GHz input, separated by a paused interval, plus reset cancellation.
Historical fixed-binary receipts below retain their original timing semantics.

Native C and initial fixture recipe `19adb80700992ee8dd63d8409d85c9f194b4a154` extend the separate source1 counter checkpoint. Corrected qtest recipe is `5a822117f9472e0d749c3dbc077d9c7de1864ade`; its changes affect only external-input observation. The [fixed whole capture contract](https://github.com/mashiro01/nightglass/blob/b7a5bd1656bff8798beb197e811c389650018161/research/z7ii/qemu-multichannel-timing-contract.md) preserves original70B enable method1036e8ea and28B result reader1036e930, channel5/6 pointer geometry and the high callback branches. Historical partial saved-native coverage remains partial, rather than a claim of accepted whole firmware CFGs.

The prior exact51b8 original run stopped at CPU1 PC1036e8fc, writing1 to2c011610. R5=1 is the actual first optional input word. Its second word0 is a static high-caller join, not an independently captured live word. The original low method enables the optional word, reads A+8/A+0, clears A+8 bit5 and sets A+0 bits1/2 for modeflag0. A nonzero modeflag instead sets bit5; its hardware event meaning remains unbound. The result reader reads B+18 into output+4, then B+14 into output+0. No original instruction in that method supplies the capture event itself.

## Named functional semantics and bounds

Two named qdev input signals, `capture[0]` and `capture[1]`, belong to channels5/6. The board leaves both disconnected at initial low. Their physical source, wiring, edge meanings and capture-status assignments are unknown. The model defines an explicit **modeflag0, source1** convention:

| Interface | Virtual behavior |
| --- | --- |
| B+10 enable0/1, channels5/6 only | Disables/enables sampling; enable1 requires a programmed source1 counter before state changes |
| A+0 bits0/1/2 | Counter gate/rising sampling gate/falling sampling gate; other channels retain their basic bit0 boundary |
| A+8 modeflag | Only0 supported; nonzero bit5 mode fails before modifying configuration |
| Input rising/falling edge | When sampling is enabled, its gate is set and the real source1 counter is running, latch low32 elapsed count to B+14/B+18 respectively |
| Pending bits1/2 | Rising/falling events; actual IRQ177/178 outputs follow pending state, acknowledged by existing W1C |
| Result read | Returns the last sampled count, initially0; it never samples input or produces pending state |

Bits1/2 reach the original primary high-caller callback (`status & 2F`). Bit4 belongs to a different callback slot populated by the separate PWM-like capability and is not invented as this capture event. The model still does not invoke any guest callback or set a guest flag itself.

Input levels change only through the external qdev interface. Gate/enable/configuration changes do not create an edge. Sampling disable preserves prior samples and pending state for normal acknowledgement. Counter gate disable freezes elapsed ticks and suppresses sampling. Channel/global/system reset clears capture enable, samples and pending state, while preserving the external pad level. Period reprogramming clears samples and capture pending state as an explicit new-timebase convention. Enabling a high pad after reset or reconfiguration therefore does not fabricate a rising edge.

Both inputs use the same independent nominal3GHz input Clock, not a physical frequency assertion. The divided period+1 threshold remains64-bit. An enabled capture configuration rejects divider changes before mutation, including while bitmap-paused. Unsupported capture values/modes/capabilities/results writes, shared submissions while capture is enabled and other features fail. No unknown input, packing, capture timer, peer or waveform provider is silently added.

The implementation reuses upstream [named qdev GPIO inputs](https://github.com/qemu/qemu/blob/4fc49f46dc95d4a27de2509e7fceb2931e91faeb/include/hw/core/qdev.h), Clock, QEMUTimer and IRQ APIs. External-input fixtures use the official [QEMUQtestMachine](https://github.com/qemu/qemu/blob/4fc49f46dc95d4a27de2509e7fceb2931e91faeb/python/qemu/machine/qtest.py) and [qtest pin/clock protocol](https://github.com/qemu/qemu/blob/4fc49f46dc95d4a27de2509e7fceb2931e91faeb/system/qtest.c). They inject input edges, never result/status words or guest software flags. Device state remains explicitly unmigratable.

## Exact focused checks and original progress

Incremental native19adb80 builds successfully; checkpatch reports0 errors/0 warnings. `run issue331/timing-capture-focused-summary.json` records **29 executing ARM checks** under recipe19adb80 and **six corrected official qtest input checks** under recipe5a82211. The29 successful returns are preserved as the first29 PASS lines of the original partial aggregate log, and their generated ARM cases plus runner AST are verified identical to the corrected recipe. The qtest report `run issue331/timing-capture-inputs-fixed/result.json` binds native19adb80 and recipe5a82211, six PASS in0.4111132089747116 seconds. Checks cover disconnected inputs, actual edge-sampled counts, IRQ output/W1C, two-channel isolation, disable and gate behavior, configuration without synthetic edges, reset retaining pad level and period reprogramming clearing samples. Executing secure ARM separately verifies IRQ177/IAR/EOI. That focused summary checks the350-case registry's unique names and six ring entries without attributing a full-suite run to it.

The subsequent requested complete local smoke run is independently recorded in `run issue331/full-capture-350-19adb80/result.json`: **350/350 PASS**,16.355878499918617 seconds, native19adb80700992ee8dd63d8409d85c9f194b4a154 and exact recipe5a822117f9472e0d749c3dbc077d9c7de1864ade. It records the complete unique fixture list and verifies all18 board source/header files against the native upstream overlay by direct byte equality. The full suite includes the six qtest inputs,35 timing/capture checks,23 bridge checks and six unique ring checks. All guests exit0. This is macOS local validation, not Linux CI or original firmware execution. The previous full339 receipt remains native51b8; no earlier focused, failed or original receipt is retagged.

The first qtest attempt failed at its Group0 IAR assertion. Official qtest memory requests use `MEMTXATTRS_UNSPECIFIED` with secure=false, which the mature GIC treats as Non-secure; its Group0 accesses cannot be used as the executing secure ARM test. The corrected fixture observes the actual named SysBus IRQ output and consumes official asynchronous IRQ notifications before queued replies. GIC/device C was not changed for this correction. Initial raw partial aggregate and failed fixture artifacts remain at `run issue331/timing-capture-19adb80`; they are not rewritten as successful corrected runs.

Changed-stage original `run issue331/original-timing-capture-19adb80/{receipt.json,qemu.log}` binds native19adb80, pinned upstream4fc49f46/QEMU11.1.2, future runner assumptions and exact command/filter. QEMU exits0 after0.1949202079558745 seconds of host observation; complete original payload direct equalitytrue and wholebootfalse. It crosses the prior capture-enable rejection and stops at **CPU1 PC10361044 /WRITE32 PA b80d1f80=0**, an uncovered RAM access beyond the current first application allocation. This is the next whole allocation/object dependency; it is not repaired by allowing a single word.

The original run contains no external capture stimulus and therefore proves neither sampled results, physical input wiring, complete enclosing phase nor menus. Earlier51b8/228 original and debugger receipts remain untouched.
