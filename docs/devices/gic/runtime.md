# Whole upstream GIC version matching the original initialization

This continues [QEMU: recover IRQ timer RTOS and interprocessor startup dependencies](https://github.com/mashiro01/nightglass/issues/331). Earlier GICv2 receipts remain unchanged. A targeted original run with the same `cf6250c` C and runner `ecfa5c4`, `run issue331/original-gic-initialize-ecfa5c4`, enables only official GIC trace events: log228 writes Distributor ISACTIVER0=`20000000`,232 enables29,234 writes CPU0 EOIR=`1d`,251 shows the real upstream timer asserting PPI29. There is no IAR acknowledgement29 before the separate `04282000` debug stop. The original initialization uses read/modify/write EOIR but discards its low12bits; the upper read is architecturally unsupported and remains the upstream warning/return behavior.

The [fixed upstream active-register write path](https://github.com/qemu/qemu/blob/4fc49f46dc95d4a27de2509e7fceb2931e91faeb/hw/intc/arm_gic.c#L1349-L1371) permits software active writes only for revision2. The [EOI path](https://github.com/qemu/qemu/blob/4fc49f46dc95d4a27de2509e7fceb2931e91faeb/hw/intc/arm_gic.c#L857-L912) returns when no acknowledged IRQ supplies a running priority. Thus the original active29→EOI29 initialization can leave a v2 IRQ active before its first real timer event. This is a model-version mismatch candidate, not proof of the physical Nikon GIC revision.

The canonical board now selects the whole mature upstream `arm_gic` revision1 with security extensions, num-cpu matching the board and256 total IRQs. Revision1 retains its read-only active-register behavior; no host IRQ clear or guest instruction change is used. Distributor/CPU interfaces remain at the guest-produced CBAR+1000/+2000. CPU EL2 capability remains unchanged: configuring the device directly avoids A15MPCore's automatic coupling of CPU EL2 to the GIC virtualization option, which [requires revision2](https://github.com/qemu/qemu/blob/4fc49f46dc95d4a27de2509e7fceb2931e91faeb/hw/intc/arm_gic_common.c#L216-L227). GIC virtualization interfaces are absent in this diagnostic profile, rather than aliasing unsupported addresses.

## Revision contract

The revision choice is a functional compatibility result, not a recovered Nikon
part number. The original SLA initializer reads, ORs bit29, and writes the
Distributor range `0x300..0x303` (`GICD_ISACTIVER0`, the active view for PPI29),
then reads and writes the CPU interface EOI path at offset `0x10`. The original
trace records the write value `0x20000000` and subsequently observes a real
secure timer PPI29 acknowledgement and EOI. Therefore the write must not create
an active PPI29 state that suppresses the first timer delivery.

The ARM GIC architecture specification defines the same distinction: the
`GICD_ISACTIVERn` range starts at `0x300 + 4*n`, is read-only active state in
GICv1, and is read/write Set-active state in GICv2 ([IHI 0048B.b, sections
4.3.9 and Table 4-13](https://documentation-service.arm.com/static/5f8ff196f86e16515cdbf969?token=)).
The upstream QEMU implementation follows that distinction: revision1 reads
the active state but the write path only mutates state for revision2
([arm_gic.c](https://github.com/qemu/qemu/blob/4fc49f46dc95d4a27de2509e7fceb2931e91faeb/hw/intc/arm_gic.c#L1048-L1071),
[write path](https://github.com/qemu/qemu/blob/4fc49f46dc95d4a27de2509e7fceb2931e91faeb/hw/intc/arm_gic.c#L1349-L1377)).

The focused `/nikon/sla/secure-tick` fixture executes this exact read/OR/write
sequence against the board's real upstream GIC, checks that the active view
remains zero after the write, then observes IRQ29 through IAR and EOI. This
locks the model contract without injecting an IRQ or adding a private active
register implementation. It does not establish the physical controller's
silicon revision; an on-device identification read or equivalent hardware
evidence would be required for that claim.

Normal IRQ/FIQ and official timer lines use the same [upstream A15MPCore PPI wiring](https://github.com/qemu/qemu/blob/4fc49f46dc95d4a27de2509e7fceb2931e91faeb/hw/cpu/a15mpcore.c#L95-L115): physical30, virtual27, hypervisor26, secure29. CPU generic timers remain upstream devices, and guest registration, enable, acknowledgement and EOI remain necessary. This version choice does not by itself establish a second original tick.

Incremental native build exited zero. Four focused tests in `run issue331/gic-v1-first/result.json` passed: secure physical timer/PPI29 with real IAR/EOI, the original active-write/EOI-before-IAR shape followed by a real timer acknowledgement, completion SPI250/EOI and completion polling. Other historical fixtures are not retroactively relabeled as revision1 validation.

Original fixed `666084f3661baf8e9717548393a1976018dc7550` in `run issue331/original-gic-v1-666084f` exited zero at the still-uncovered GPIO store `PC10200f0a / WRITE32 PA04282000 / ffff0000`, wall0.335829s, direct payload unchanged. Its official GIC/exception log now records two real CPU0 timer services: lines252/255 IRQ→IAR29,261 timer level0,268 EOI29; lines294/298/301 next timer level1→IRQ→IAR29,303 level0,310 EOI29. These are actual original IRQ entry/acknowledgement/deassertion/EOI cycles, not a synthetic tick or host handler call. The [original clear callback reload protocol](https://github.com/mashiro01/nightglass/blob/e76c05c/research/z7ii/qemu-rtos-startup-contract.md) supplies separate static instruction evidence; this event-only run does not fabricate a dynamic `O260` instruction trace. Menus, IRQ250 original task-mode use and physical tick performance remain unverified.
