# Original MAS crosses MEMC initialization

This observation qualifies early memory-controller initialization before
[multicore startup](smp.md).
Original MAS crossed two MEMC writes and the first temporary-stack PUSH, then
rejected READ32 at diagnostic PA0428102c. Complete boot, O178, original
RTOS/GIC/timer initialization and reconstruction comparison were unverified.

Machine commit40fb318220fa48aae235029c73f0cd06d2bcc431 used upstream
4fc49f46dc95d4a27de2509e7fceb2931e91faeb. The integration owner completed
native macOS ARM build, original execution and synthetic tests. This page
records that run, not an additional execution. The extracted MAS input was
38,239,096 bytes and remained directly byte-equal before/after execution;
original instructions, entry and O178 were unchanged.

Invocation: `-M z7ii-mas-diagnostic -accel tcg,thread=single -bios <payload>
-display none -serial none -monitor none -S -qmp unix:<socket>,server=on,wait=off
-D <log> -d in_asm,int,guest_errors`. QMP continued, inspected the debug stop
and quit; process exit0 is not evidence of original boot.

| Direct observation | Result (addresses/values hexadecimal) |
|---|---|
| Reset | prelaunch; PC10020000, CPSR400001d3, general registers/SP0; upstream Cortex-A7 ARM/SVC/secure reset |
| MEMC MODE0 | CPU0 PC1002bda4 WRITE32 PA04264000=21, accepted-profile |
| MEMC TIM0 | CPU0 PC1002bdac WRITE32 PA04264020=153b0902, accepted-profile |
| Continuation |100202b8→10020914, original BX LR, then1002d700; the observed PUSH succeeded, not proof that the entire48-byte window was consumed |
| Next rejection | CPU0 PC1002d70c (MAS Od70c), READ32 PA0428102c; MEMTX_ERROR/uncovered-address. Diagnostic value0 is invalid transaction output, not valid zero readback |
| Exception/stop | Data Abort; DFSR1008, DFAR0428102c; QMP debug/running=false; final PC10100010,CPSR600001d7,SPb2ba8000,LR1002d714 |
| Host session |0.10279012494720519 seconds including control/exit; not boot time or physical performance |

One Cortex-A7, CBAR1d000000, CNTFRQ48MHz, A15MPCore256 IRQs, the MEMC layout
and cold-zero48-byte temporary RAM remained
[diagnostic assumptions](../devices/memc/protocol.md). Accepted writes do not
identify Nikon silicon, physical reset or NOR. NOR writes, clock, IPC and
BootROM were absent at this checkpoint.

The rejected read is the first04281000 initializer read. Later RMW and
software delays were not executed. The [earlier static analysis](first.md#memc-contract)
separates this block from MEMC; real field/reset/side-effect evidence was still
needed. The final PC differs from the transaction PC because Data Abort
precedes asynchronous stop.

The device-smoke-40fb synthetic receipt records13 fixtures, each exit0:
MODE/TIM retention and WEOFF RMW, uninitialized reads, width/offset/profile
rejection, NOR setup/protection/missing backend, temporary-stack edges and
secure timer PPI29/IAR/active/EOI/CNTFRQ/CBAR behavior. These do not prove
original RTOS, complete flash writes or SMP/IPC. See the
[current frontier](../validation/frontier.md) for later execution.
