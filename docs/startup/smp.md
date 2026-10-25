# Original two-core entry and the next clock boundary

This observation qualifies multicore entry after [MEMC initialization](memc.md).
The two-core model built and its synthetic WFE/DSB/SEV handshake passed.
Original MTTCG observed CPU1 after WFE while CPU0 still failed its first clock
read at0428102c. Original magic release, kernel dispatch, tick and GUI were
not established.

Machine942de88 used upstream4fc49f46dc95d4a27de2509e7fceb2931e91faeb.
Native macOS ARM build, synthetic tests and original observations were
completed without instruction/PC changes or host magic injection. The original
38,239,096-byte MAS input remained directly equal in both receipts; entry
was20 2f 00 ea and O178 remainedfe ff ff ea.

## Software contract and bounded mappings

The original configuration producer gives TSysPrNum=2, TKerPrNum=2 and
TTimPeriod=1. Descriptor+4 is the reload function O260 written at O308;
+c is stop O254. CPU0 Od848 writes04008ffc=babebabe and executes DSB/SEV.
The nonzero-selector path performs WFE at Obfa4, DMB, reads/compares, clears
the word and returns to common entry. See the
[pinned RTOS contract](https://github.com/mashiro01/nightglass/blob/aa759231295a8b20662ca35f3d313b1bff7035b2/research/z7ii/qemu-rtos-startup-contract.md).
These are software defaults and flow, not physical core/CBAR measurements.

The profile creates two upstream Cortex-A7 cores at candidate reset PC10020000,
ARM state, retaining upstream MPIDR, indices, WFE/SEV, exceptions and ordering.
A15MPCore uses num-cpu2/num-irq256 and
[upstream VExpress wiring](https://github.com/qemu/qemu/blob/4fc49f46dc95d4a27de2509e7fceb2931e91faeb/hw/arm/vexpress.c);
each secure timer uses PPI29. CPU identity, CBAR1d000000 and physical loading
are [model assumptions](../devices/memc/protocol.md). Single-core fixtures
explicitly select -smp1.

Only cold-zero [04008ffc,04009000) was added for the magic word; host code
does not set it. CPU1 SVC SP isb2b84000. The entry/table/cache PUSH closure
is8+8+max(8,56)=72 bytes, so only[b2b83fb8,b2b84000) is mapped.
The selector<<14 stride does not justify another16KiB stack. See the
[pinned72-byte derivation](https://github.com/mashiro01/nightglass/blob/b75f77140fa05d5d5e75e3a32ed0d58961ebc906/research/z7ii/qemu-rtos-startup-contract.md).

## Two original observations

The historical thin runner used official QEMUMachine and recorded argv,
per-core registers and direct input comparisons. HMP over QMP is
[stateless](https://github.com/qemu/qemu/blob/4fc49f46dc95d4a27de2509e7fceb2931e91faeb/qapi/misc.json):
cpu-index must be supplied in each call; a previous cpu1 command is not a
persistent selection.

| Run | Observed result (addresses/values hexadecimal) |
|---|---|
| original-two-core-942de, single-thread TCG | Both CPUs existed; CPU0 faulted before CPU1 scheduling. CPU1 PC10020000/SP0 does not prove WFE execution |
| original-two-core-mtcg-first, MTTCG | CPU1 PC1002bfa8, the DMB after WFE1002bfa4; SPb2b84000,R0=04008ffc,R2=babebabe,CPSR200001d3. No subsequent magic read/clear/common entry was observed; its72-byte stack was not yet consumed |
| CPU0 in both | PC1002d70c READ32 PA0428102c, MEMTX_ERROR; Data Abort; final PC10100010,CPSR600001d7,SPb2ba8000,LR1002d714 |
| Completion in both | QMP debug/running=false, process exit0, original payload unchanged; host sessions about0.071s/0.274s, not boot times |

The observed CPU1 PC is compatible with the original wait path; it does not
prove CPU0 produced magic, SMP initialization or first dispatch. CPU0 had not
executed the later magic writer.

## Validation scope and next contract

Native incremental build exited0. startup-single-smoke passed a four-byte
ARM loop; all15 startup-device-smoke contracts passed, including shared-word
edge faults; startup-smp-r2 passed the synthetic two-core handshake. Only
guest instructions wrote/cleared flags and magic using actual WFE/DSB/SEV.
The first synthetic attempt failed because HMP CPU selection was incorrectly
treated as persistent; the observation API was corrected without bypassing
device assertions.

At this checkpoint, the unresolved contract remained04281000+2c initial state
and the surrounding clock registers. Public M10V/SC2000 CLKSEL offsets28/2c
do not justify a Nikon PLL reset value. Later devices require their own
write/status/IRQ/ACK contracts; the software path O294→Thumb O1e0034 does
not authorize skipping O178. Later progress is in the
[current frontier](../validation/frontier.md).
