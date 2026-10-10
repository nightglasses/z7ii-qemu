# EX system reset

The original EX snapshot at source
`fb176bdc934c38dcd0df3133a5bd00b29280d167` ended at `0804909e`, with
LR `08042805`, xPSR `0100000f`, R0 `e000ed0c`, R1 `05fa0304` and R2
`05fa0004`. The saved read-only receipt is
`work/worktrees/local-gdb-observers/work/gui/providers/receipt.json`; its
complete register and SCB reads are in the adjacent `gdb.log`. CFSR and HFSR
were zero. The snapshot followed 58.905961 seconds of virtual execution and
was terminated normally by QMP; that duration is not a measured reset latency.

Original EX instructions `08049088..080490a0` perform DSB, preserve AIRCR
PRIGROUP, write the `05fa` key and SYSRESETREQ, perform DSB, and wait in a NOP
loop. LR binds the call at `08042800` in diagnostic collector `080426b8`.
The original vector entry 15 selects `080202c9`; xPSR's exception number 15
places the observed request in SysTick context. This is a software system-reset
request, not evidence of a current HardFault. The sequence also matches the
official [CMSIS 5.9.0 SystemReset implementation](https://github.com/ARM-software/CMSIS_5/blob/5.9.0/CMSIS/Core/Include/core_cm4.h),
without asserting a Nikon library identity.

The diagnostic reason at EX RAM `2003a000` and the current EX stack were not
captured. In particular, R4 equal to 8 is not a captured reason: the collector
uses an eight-item loop. The existing old saved native inventory binds
`08042d74`'s reason pointer through ROM literal `08042da8 = 2003a000`.
Reason-writer and timer-callback coverage from that older inventory is partial;
no external power, peer, or missing input is established as this request's cause.

The independently established model defect was the local reset boundary.
In fixed official QEMU `4fc49f46dc95d4a27de2509e7fceb2931e91faeb`,
`hw/core/qdev.c:device_reset_child_foreach` traverses child buses, not QOM
children. `hw/arm/armv7m.c:armv7m_realize` realizes the CPU without a bus;
NVIC and SysTick are separate SysBus devices. The ARMv7M container has no reset
handler that forwards to those QOM children. Thus resetting only the container
does not retire a CPU reset or clear its NVIC/SysTick state. The same official
file explicitly documents the separate CPU-reset requirement in
`armv7m_load_kernel`.

The local AIRCR bottom half now synchronously uses official `run_on_cpu` to
perform the existing MCU reset on its owning CPU thread under MTTCG. It resets
NVIC, non-secure SysTick, the existing MCU peripherals, and finally calls
official `cpu_reset`, which obtains SP and PC from the original vector memory.
No register, stack, PC, return value or firmware instruction is patched. Normal
board reset also resets the bus-less core in the EX Resettable exit phase,
after the normal device hold phases. SRAM, supplied GPIO levels, independent
I2C peers and other CPU domains retain their existing reset contracts.

`tests/unit/nikon/exreset.c` is a synthetic native full-system fixture. It uses
one real EX and one independent official ARMv7M domain under the qtest
accelerator, with vector-only synthetic images. The separate fixture domain
uses unique RAM migration names and the official M-profile board reset API;
it does not instantiate a second board's singleton EX storage. It checks the
keyed NVIC request through the real GPIO/BH path, owning
CPU SP/PC/xPSR and masks, NVIC enable/pending and VTOR, SysTick registers, repeated
requests, normal board reset, unchanged second-domain state during local reset,
and retained SRAM/external GPIO level. It does not execute original firmware.
The official incremental ARM build and unit link passed. The focused
`/nikon/ex/local-system-reset` case passed in0.57s with exit0; its receipt is
`work/exresetownernativereceipt.log`. Production reset code is exactly the
fixed `f05d96bc` source; subsequent changes only repair the native fixture's
public IRQ include and independently rooted DeviceState memory ownership.
The earlier duplicate singleton-RAM and non-device RAM-owner failures remain
in `work/exresetnativereceipt.log` and `work/exresetdomainsnativereceipt.log`.
No successful original reboot or GUI startup is claimed by this source change.

The next already-planned original snapshot needs EX RAM `2003a000`'s diagnostic
record, the current stack, complete SCB `e000ed00..e000ed3f`, and available
MSP/PSP/CONTROL/PRIMASK/BASEPRI/FAULTMASK fields to establish the initiating
software cause separately from reset retirement. No additional VM was started
for this investigation.
