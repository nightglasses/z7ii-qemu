# Z7II decoded bundle CPU and board parameters

This matrix distinguishes unchanged C1.70 software facts, selected QEMU profile values, and unknown physical-camera parameters. The composite machine executes MAS, SLA and EX from the complete decoded container. VR is a separate RX machine; TPJ has no executable target yet.

## Composite capacity

`-smp cpus=2,maxcpus=5` creates two MAS Cortex-A7 CPUs, two private SLA Cortex-A7 CPUs and one private EX Cortex-M4 CPU. Only MAS count is controlled by `cpus`; `maxcpus` reserves upstream MTTCG execution capacity for embedded domains. Five virtual CPUs do not establish five physical camera cores.

Each domain retains a separate address space and interrupt controller. SLA uses local MP affinities 0/1 and its GIC's `first-cpu-index` maps the actual global QEMU indices. EX has no A-profile MPIDR/GIC membership.

## Parameter matrix

| Parameter | MAS | SLA | EX | Evidence level |
|---|---|---|---|---|
| CPU model | Cortex-A7 | Cortex-A7 | Cortex-M4 | Functional profile; exact physical core/part remains unbound |
| Domain CPU count | 2 by default, maximum 2 | 2 embedded | 1 embedded | MAS C1.70 config says TSysPrNum=2/TKerPrNum=2; SLA/EX counts are declared model topology |
| Local MP affinity | Upstream CPU index 0/1 | Explicit 0/1 | Not A-profile | Firmware consumes MPIDR low selectors; no physical cluster topology proof |
| Loaded entry | 0x10020000, ARM | 0x1002bcd4, ARM | Original SP/Thumb vector words | Component execution profile, not recovered cold BootROM |
| CBAR | 0xf8100000 | 0xf8100000 | None | Named functional input; source consumes CBAR+0x1000/+0x2000 |
| GIC/NVIC | GIC revision 1, 256 IRQs | Private GIC revision 1, 256 IRQs | NVIC, 97 IRQs, 4 priority bits | Source-backed interfaces with declared upstream version/count profile |
| Security | GIC security enabled, virtualization disabled | Same | ARMv7-M reset | Physical initial security state remains unknown |
| Generic timer | CNTFRQ=48 MHz, secure PPI 29; PHYS30/VIRT27/HYP26 | Same | SysTick uses ARMv7-M clocks | 48 MHz firmware frequency literal, not CPU MHz |
| ROM | 0x10020000, C1.70 size 0x2477b78 | 0x10020000, size 0x86cd40 in private memory | 0x08000000, 512 KiB | Component source bytes and current loaded-address profile |
| RAM | Six source storage classes totalling 186 MiB plus bounded scratch areas | Source COPY/table/BSS/arena/stack ranges | 0x20000000, 256 KiB | Functional backing; no installed DDR geometry or population claim |
| Clock | Upstream generic timer + device functional clocks | Same | Initial HSI16 MHz, refclk2 MHz; RCC-driven timers | Model clocks; physical CPU/DDR/PLL rates remain unknown |

## Reset, cache and page tables

Current diagnostic prelaunch CPSR 0x400001d3 is upstream Cortex-A7 reset state. The model sets only entry PC, reset-cbar, cntfrq and domain capacity. It does not pre-write SCTLR, ACTLR, TTBR0/1, TTBCR, DACR, VBAR, cache state, secure-bank state, RTOS flags or task objects. Unchanged firmware produces those values.

The MAS table is copied by the guest from its 16 KiB source table to 0xb2b74000. SLA guest copies its 16 KiB table to 0xc18e4000. These are source/runtime contracts; the virtual cold-zero RAM and mapped backing remain model choices. Table walks must preserve each domain's physical owner rather than using equal numerical addresses as a cross-domain alias.

CPU0 creates and starts the initial MAS task; nonzero software selectors perform per-core initialization and join the guest scheduler. A secondary CPU with no selected task is not by itself evidence of a missing core, failed reset, or defective scheduler.

## Physical fidelity boundary

Public Socionext SC2000 documents list four Cortex-A7 cores and 650 or 660 MHz depending on document revision. No current evidence binds that part to Z7II/EXPEED 6. Those figures cannot replace MAS's two software participants or its 48 MHz timer reference. Public RAM candidate specifications also do not establish Z7II installed capacity, enabled channels, clock or available bandwidth.

A functional QEMU run cannot measure camera CPU throughput, cache latency, DDR bandwidth, ISP utilization or thermal limits. Optimizing firmware performance requires original hardware counters/measurements and board-specific clock/DDR evidence; emulation is for control flow, protocol and correctness analysis.

## Evidence and next work

Current project sources: `hw/nikon/machines/z7ii.c`, `sla.c`, `ex.c`; [SLA startup](sla.md), [GIC runtime contract](../devices/gic/runtime.md), [bundle execution](../runtime/ex.md), and [component boundary](components.md).

Before changing a physical parameter, obtain original firmware/loader receipts for MIDR/MPIDR/CBAR and CP15 reset state, clock/DDR initialization registers, and full bundle domain/event routing. Exact silicon identity, cold BootROM, physical core count, DDR channels/capacity and CPU/DDR clocks remain open. No model behavior is changed by this audit.
