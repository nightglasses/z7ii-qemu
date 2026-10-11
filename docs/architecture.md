# Z7II QEMU architecture

This document describes the current composed Z7II machine, its standalone
component profiles, and the evidence boundary for each hardware pipeline. A
solid edge means the QEMU model has a bounded contract and a focused test. A
dashed edge means the firmware or hardware join is observed only partially.

## Machine composition

```mermaid
flowchart TD
  Input[Decoded five-component container\nEX / TPJ / MAS / VR / SLA]
  Parser[Bundle parser\nidentity, bounds, CRC]
  Machine[Z7II composed machine\nz7ii-mas-diagnostic]

  Input --> Parser --> Machine

  subgraph Mas[MAS domain]
    Mas0[CPU0 Cortex-A7\nprimary startup]
    Mas1[CPU1 Cortex-A7\nsecondary init / RTOS wait]
    MasRTOS[MAS RTOS\nSVC / IRQ / task scheduler]
    Mas0 --> MasRTOS
    Mas1 --> MasRTOS
  end

  subgraph Private[Private component domains]
    Sla[SLA domain\n2 × Cortex-A7]
    Ex[EX domain\n1 × Cortex-M4]
  end

  Machine --> Mas
  Machine --> Sla
  Machine --> Ex
  Machine -. VR record profile .-> VR[VR component\nstandalone RX62N profile]
  Machine -. TPJ record .-> TPJ[TPJ component\nrecord loaded; no standalone CPU machine]

  MasRTOS --> Shared[Bridge / peer / shared contracts]
  Sla --> Shared
  Ex --> Shared
```

The bundle path is the system-level input. `-bios` with a raw MAS component,
`nikonsla`, `nikonex`, and `nikonvr` are isolated diagnostic profiles. They are
useful for narrowing a contract and do not replace the composed machine.

## CPU and board parameters

```mermaid
flowchart TD
  Reset[Virtual reset]
  Reset --> MasCPUs[MAS CPU cluster\nCortex-A7 × 2 maximum]
  MasCPUs -->|CPU0| Primary[Initial task\n0x10103bc8]
  MasCPUs -->|CPU1| Secondary[Per-core init\nsecondary scheduler / idle]
  Primary --> Prelude[Kernel prelude\nplatform callback]
  Prelude --> Tasks[PKGB and service tasks]
  Secondary --> Tasks

  MasCPUs --> CBAR[CBAR\n0xf8100000]
  MasCPUs --> Counter[CNTFRQ\n48 MHz model]
  MasCPUs --> GIC[GIC\n256 IRQs, revision-1 view]
  GIC --> Timer[Timer PPIs\nsecure PPI 29 observed]
  GIC --> SPIs[Device SPIs\nrouter, USIO, completion]

  SlaCPU[SLA CPU domain\nCortex-A7 × 2] --> SlaGIC[Private SLA GIC / MMU]
  ExCPU[EX CPU domain\nCortex-M4 × 1] --> ExIRQ[EX NVIC / IRQ]
```

CPU0 and CPU1 have different startup responsibilities. CPU0 creates and starts
the initial task; CPU1 can remain in a scheduler or event-wait boundary before
joining a role task. A CPU1 empty successor is therefore not a default-ready
failure. CPU model details remain separated into observed values, functional
assumptions, and unresolved silicon identity in the CPU evidence matrix.

## Memory and DMA ownership

```mermaid
flowchart TD
  ROMD[MAS / SLA ROMD]
  Loader[Guest loader and memcpy]
  ROMD --> Loader

  subgraph Arenas[Dedicated download arenas]
    Zero[Selector-zero\n0xb0d00000..0xb1100000\n4 MiB]
    One[Selector-one\n0xb0900000..0xb0d00000\n4 MiB]
  end
  Loader --> Zero
  Loader --> One

  subgraph DDR[Declared storage classes]
    Kernel[Kernel / BSS\n0xb1800000 +]
    Private[Private objects]
    Modules[Modules / code]
    Graphics[Graphics / pixels]
  end
  Loader --> DDR

  ROMD -. later source .-> Unknown[Destination 0x50200000\nowner unresolved]
  Zero --> Copy[COPY / IPUD / RMU]
  One --> Copy
  DDR --> Copy
```

The two 4 MiB download arenas are separate QEMU `MemoryRegion` owners. The
selector-one firmware range check extends to `0xb1000000`, but the proven model
owns the exact four-MiB arena `[0xb0900000, 0xb0d00000)`. The later
`0x50200000` COPY destination is kept as an error boundary until its MMU,
board decode, and provider contract are recovered.

## Runtime pipelines

### Boot and task pipeline

```mermaid
sequenceDiagram
  participant R as Reset / loader
  participant C0 as MAS CPU0
  participant C1 as MAS CPU1
  participant K as RTOS scheduler
  participant P as Peer / event producer
  participant G as GUI waiter EV3

  R->>C0: load MAS and reset
  R->>C1: release secondary core
  C0->>K: create and start initial task
  C1->>K: per-core init and scheduler wait
  C0->>K: select service / PKGB tasks
  P-->>G: role0 callback sets EV3
  G->>K: wake condition becomes eligible
```

The EV3 setter edge is runtime-observed. The peer-ready, bridge, GPIO, and
USIO path before that callback remains a separate evidence boundary.

### Image pipeline

```mermaid
flowchart TD
  Sensor[CMOS input contract]
  Capture[capture-control]
  Readout[readout-control]
  DMA[DMA / buffer rotation]
  ISP[Processing / ISP boundary]
  Frame[Framebuffer descriptor]
  Scanout[Display scanout]
  Save[Storage save]

  Sensor --> Capture
  Capture --> Readout
  Readout -. real C1.70 producer .-> DMA
  DMA -. source-backed frame .-> ISP
  ISP -. real callback / descriptor .-> Frame
  Frame -. first frame .-> Scanout
  Frame -. save request .-> Save
```

The CMOS, processing, display, and storage devices each have isolated tests.
The complete original-firmware image path is not accepted until the runtime
producer, descriptor geometry, completion event, scanout, and save edge are all
observed.

## Current frontier

| Pipeline | Current evidence | Next boundary |
|---|---|---|
| CPU startup | CPU0 initial task and PKGB roles observed | CPU/board parameter matrix |
| Selector-zero/one COPY | Both 4 MiB arenas modeled and tested | `0x50200000` owner and provider |
| EV3 | Role0 callback to setter observed | Peer-ready/bridge precondition |
| CMOS/readout | Device contracts and fixtures exist | Original firmware producer and DMA completion |
| Display | GUI acceptance test reports explicit `UNMET` | Real framebuffer and scanout callback |

Every unknown remains an explicit fault or receipt. The emulator does not
advance a guest PC, synthesize an event, prefill a framebuffer, or convert a
transport fixture into a camera specification.
