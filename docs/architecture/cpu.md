# Z7II CPU and board compute architecture

This diagram separates the generic Arm Cortex-A7 MPCore structure from the
Z7II-specific values observed in C1.70 runtime traces or selected as explicit
QEMU functional assumptions. It does not identify the Nikon SoC die or claim
that every optional Cortex-A7 block is present in the camera.

## CPU cluster and internal units

```mermaid
flowchart TD
  subgraph Cluster[Arm Cortex-A7 MPCore cluster]
    subgraph Core0[Core 0 / CPU0]
      IF0[Instruction fetch]
      BP0[Branch prediction]
      DEC0[ARM / Thumb decode]
      INT0[Integer register file\nALU / shifter / multiply]
      LSU0[Load-store unit\nalignment / ordering]
      NEON0[NEON / VFP\nSIMD and floating point]
      L1I0[L1 instruction cache]
      L1D0[L1 data cache]
      TLB0[Instruction/data TLBs]
      MMU0[MMU and stage-1 translation]
      CP150[CP15 system control\nTTBR / TTBCR / SCTLR / DACR]
      Debug0[Debug / PMU interface]
      IF0 --> BP0 --> DEC0
      DEC0 --> INT0
      DEC0 --> LSU0
      DEC0 --> NEON0
      IF0 --> L1I0
      LSU0 --> L1D0
      L1I0 --> TLB0
      L1D0 --> TLB0
      TLB0 --> MMU0
      CP150 --> MMU0
      Debug0 -. observability .-> DEC0
    end

    subgraph Core1[Core 1 / CPU1]
      IF1[Instruction fetch]
      BP1[Branch prediction]
      DEC1[ARM / Thumb decode]
      INT1[Integer execute]
      LSU1[Load-store]
      NEON1[NEON / VFP]
      L1I1[L1 instruction cache]
      L1D1[L1 data cache]
      TLB1[Instruction/data TLBs]
      MMU1[MMU and stage-1 translation]
      CP151[CP15 system control]
      Debug1[Debug / PMU interface]
      IF1 --> BP1 --> DEC1
      DEC1 --> INT1
      DEC1 --> LSU1
      DEC1 --> NEON1
      IF1 --> L1I1
      LSU1 --> L1D1
      L1I1 --> TLB1
      L1D1 --> TLB1
      TLB1 --> MMU1
      CP151 --> MMU1
      Debug1 -. observability .-> DEC1
    end

    SCU[Snoop control unit\ncoherency and interrupts]
    L2[L2 cache / controller\nimplementation-dependent]
    GICCPU[GIC CPU interface]
    Timer[Generic timer and counter]
    ACP[Coherency / external interconnect\nboard-dependent]

    L1D0 --> SCU
    L1D1 --> SCU
    SCU --> L2
    L2 --> ACP
    SCU --> GICCPU
    Timer --> GICCPU
  end

  ACP --> Memory[DDR / SRAM / ROM / MMIO]
  GICCPU --> IRQ[GIC distributor and board IRQ fabric]
  CP150 --> MMU[Guest page tables\nTTBR0 / TTBCR / section descriptors]
  MMU --> Memory
```

The generic blocks above come from the Cortex-A7 MPCore programming model. The
firmware-facing Z7II values currently used by the project are:

| Area | Current value | Evidence class |
|---|---|---|
| MAS cores | 2 Cortex-A7 software participants | C1.70 `TSysPrNum`/`TKerPrNum` and runtime SMP traces |
| CPU0 role | Initial task creation, kernel prelude, service-task scheduling | Direct runtime trace |
| CPU1 role | Per-core initialization, scheduler/event wait, later role task | Direct runtime trace |
| CBAR | `0xf8100000` | Firmware/runtime contract used by the model |
| GIC | 256 IRQ lines, revision-1 active-view compatibility | Firmware RMW behavior and GIC receipt |
| Generic counter | `CNTFRQ=48 MHz` | Runtime machine contract |
| Secure timer | PPI 29 observed in the MAS domain | IRQ trace |
| MMU | ARMv7-A stage-1 translation with guest-created L1 sections | TTBR/PTE receipts |
| Debug | GDB stub and hardware watchpoints used for observation | Tooling evidence, not guest hardware identity |

The exact silicon part number, CPU PLL frequency, cache sizes, external memory
controller timing, and physical interconnect are still unresolved. The public
SC2000 four-core and 650/660 MHz references are not used as Z7II facts.

## CPU startup and ownership

```mermaid
sequenceDiagram
  participant R as Reset
  participant C0 as CPU0 / MAS primary
  participant C1 as CPU1 / MAS secondary
  participant K as RTOS scheduler
  participant S as Shared memory / GIC

  R->>C0: reset at MAS entry
  R->>C1: secondary release path
  C0->>S: initialize shared kernel objects
  C0->>K: create and start initial task
  C1->>S: per-core setup and cache/MMU state
  C1->>K: scheduler/event wait boundary
  C0->>K: select service and PKGB tasks
  K->>S: task state, event flags, IRQ/SVC handoff
```

An empty CPU1 successor is a valid startup/affinity boundary. It must not be
repaired by returning a synthetic task, writing a ready bit, or manufacturing
an interrupt. The current next CPU investigation is the board owner behind the
`0x50200000` descriptor-driven COPY, not the CPU1 scheduler itself.

## References

- [Arm: The top five things to know about Cortex-A7](https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/the-top-5-things-to-know-about-cortex--a7)
- [Arm Cortex-A7/A15 MPCore documentation](https://developer.arm.com/documentation/ddi0503/i/hardware-description/cortex-a15-a7-mpcore-test-chip)
- [Project CPU/board evidence matrix](../startup/cpu_board.md)
