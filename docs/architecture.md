# Z7II QEMU architecture

This document describes the composed Z7II machine and its separate execution
providers. Solid arrows show implemented composition, data flow or device
signals, including explicitly selected functional assumptions. Dashed arrows
show optional external integration or runtime/physical joins still unverified;
their labels distinguish the two. Neither kind of arrow proves original
firmware startup or physical-camera fidelity. Historical runtime receipts are
identified separately from the current implementation.

## Machine composition

```mermaid
flowchart TD
  Input["Decoded five-component container<br/>EX / TPJ / MAS / VR / SLA"]
  Parser["Bundle parser<br/>identity, bounds, CRC"]
  Machine["z7ii-mas-diagnostic<br/>QEMU ARM process"]
  Input --> Parser --> Machine

  subgraph Mas["MAS domain"]
    Mas0["CPU0 Cortex-A7<br/>primary startup"]
    Mas1["CPU1 Cortex-A7<br/>secondary init / RTOS wait"]
    MasRTOS["MAS RTOS<br/>SVC / IRQ / task scheduler"]
    Mas0 --> MasRTOS
    Mas1 --> MasRTOS
  end

  Machine --> Mas0
  Machine --> Mas1
  Machine --> Sla["Private SLA domain<br/>2 Cortex-A7 cores, own memory / GIC"]
  Machine --> Ex["Private EX domain<br/>1 Cortex-M4 core, own memory / NVIC"]
  MasRTOS <--> Bridge["MAS / SLA bridge<br/>bounded peer address-space views"]
  Sla <--> Bridge
  Ex <--> SPI["EX SPI connection"]
  SPI <--> MasRTOS

  Parser -. "separate launch; not instantiated by bundle" .-> VR["nikonvr<br/>QEMU RX62N process"]
  Parser -. "separate launch; not instantiated by bundle" .-> TPJ["TPJ NDS32 runner<br/>GNU simulator process"]
  MasRTOS -. "explicit peer / frontend / socket attachment" .-> Wire["Typed peer transport<br/>SPI / I2C / GPIO"]
  Wire -. "optional external peer" .-> VR
  Wire -. "optional external peer" .-> TPJ
```

The bundle is validated as a whole before component publication. Only MAS,
SLA and EX CPUs are instantiated in the composed ARM machine. VR and TPJ
records are retained by the parser; execution requires separate providers and
explicit transport attachment. The GNU TPJ runner exists under `sim/nds32`,
built through `make tpj`; it is not a QEMU NDS32 machine or an automatically
started bundle CPU. See [NDS32 execution](startup/system.md) and
[peer transport](devices/peer.md).

`-bios` with a raw MAS component and the `nikonsla`, `nikonex`, `nikonvr`
machines are isolated diagnostic profiles. They do not replace the bundle
path. With two MAS cores, the composed machine has five ARM CPUs; MTTCG needs
`-smp cpus=2,maxcpus=5` to reserve capacity for private SLA/EX cores.

## CPU and board parameters

```mermaid
flowchart TD
  Reset["Virtual reset / selected loader"] --> MasCPUs["MAS Cortex-A7 cluster<br/>1 or 2 cores; default 2"]
  MasCPUs -->|CPU0| Primary["Initial task<br/>VA 0x10103bc8"]
  MasCPUs -->|CPU1| Secondary["Per-core init<br/>secondary scheduler / idle"]
  Primary --> Tasks["Guest RTOS<br/>kernel prelude, PKGB and service tasks"]
  Secondary --> Tasks

  Timer["Generic timer<br/>CNTFRQ 48 MHz model"] -->|"PPIs; secure PPI 29 observed"| GIC["MAS GIC<br/>256 IRQs, revision-1 view"]
  Devices["Device IRQ outputs"] -->|SPIs| GIC
  GIC -->|IRQ| MasCPUs
  CBAR["CBAR 0xf8100000"] --> MasCPUs

  SlaCPU["SLA Cortex-A7 x 2"] <--> SlaMem["Private SLA memory / MMU"]
  SlaGIC["Private SLA GIC"] --> SlaCPU
  ExNVIC["Private EX NVIC"] --> ExCPU["EX Cortex-M4 x 1"]
```

CPU0 creates and starts the initial task. CPU1 may remain in a scheduler or
wait boundary before joining a role task; an empty successor is not a missing
ready producer. CPU choices, reset state and board values remain classified
in the [CPU evidence matrix](startup/cpu_board.md). This is a functional TCG
model, not a model of cache latency, branch prediction or silicon pipelines.

## Memory and DMA ownership

```mermaid
flowchart TD
  ROMD["MAS / SLA ROMD"] --> Loader["Guest loader and memcpy"]
  Loader --> Zero["Selector-zero arena<br/>PA 0xb0d00000..0xb1100000<br/>4 MiB"]
  Loader --> One["Selector-one arena<br/>PA 0xb0900000..0xb0d00000<br/>4 MiB"]
  Loader --> DDR["Separate storage classes<br/>kernel, private objects, modules,<br/>code, graphics and pixels"]
  ROMD -->|"functional loader copies"| LoaderViews["Bounded loader RAM<br/>PA 0x50200000 / 0x50000000 / 0x50080000"]
  Zero --> Bus["QEMU address spaces / DMA"]
  One --> Bus
  DDR --> Bus
  DisplayRAM["Display buffer RAM<br/>PA 0xcb4a7000..0xcc63b000"] --> Bus
```

The two 4 MiB arenas are separate `MemoryRegion` owners. The selector-one
firmware range check extends to `0xb1000000`; the modeled arena ends at
`0xb0d00000`. Under `functional`, loader RAM covers PA `0x50200000`
(`0x27800` bytes), `0x50000000` (`0x57e30`) and `0x50080000` (`0x69e70`).
Historical experimental receipts compare these loader extents byte-exactly.
Physical backing/provider ownership and framebuffer identity remain unresolved.
Display-buffer RAM provides storage only; guest code owns pointers, descriptors
and pixels. These finite regions are not an unknown-address RAM fallback.

## Runtime pipelines

### Boot and task pipeline

```mermaid
sequenceDiagram
  participant R as Reset / selected loader
  participant C0 as MAS CPU0
  participant C1 as MAS CPU1
  participant K as Guest RTOS
  participant B as MAS / SLA bridge
  participant N as Guest producer VA 0x10200c34
  participant G as GUI waiter EV3 mask 0x10

  R->>C0: load MAS and reset
  R->>C1: reset / secondary startup path
  C0->>K: create and start initial task
  C1->>K: per-core init and scheduler wait
  C0->>K: select service / PKGB tasks
  Note over B,G: Historical notification experiment, no GUI acceptance
  B-->>N: modeled notification reaches guest handler
  N->>G: guest sets EV3 mask 0x10
  G->>K: observed natural wait return in that experiment
```

Historical peer setters publish EV3 masks `0x1`/`0x2` (wrapper LRs
`0x10200d0b`/`0x10200ceb`); these do not satisfy the GUI mask `0x10` wait.
Strict/default bridge notifications reject PA `0x2013e000`.
`functional-gui` selects `boot-shim=functional` and enables both MAS/SLA
notification endpoints before realization. It also attaches the functional
readout pipeline below. Notification wiring remains a functional hypothesis,
not a recovered physical bank/reset/IRQ contract. Historical experimental wait
returns do not prove complete boot under the latest profile.

The former [tracker #480](https://github.com/mashiro01/nightglass/issues/480)
dependency is closed: `0x42062002` is firmware-local and type-5 traffic is
service negotiation. [#482](https://github.com/mashiro01/nightglass/issues/482)
and [#485](https://github.com/mashiro01/nightglass/issues/485) are unblocked.

### Readout, image, display and storage

```mermaid
flowchart TD
  Guest["Guest readout configuration<br/>and PA 0x2006b400 = 1"] --> Readout["readout-control<br/>owned request generation"]
  Readout -->|"functional-gui only"| Pipeline["readoutpipeline.c<br/>validate finite C1.70 mapping"]
  Image["Explicit PGM/PPM or<br/>opt-in deterministic source"] --> CMOS["CMOS timing / DMA engine"]
  Pipeline --> CMOS
  CMOS -->|"timed primary-buffer DMA"| RAM["Submitted writable guest RAM"]
  Pipeline -->|"accepted generation ready"| Ready["readout ready at PA 0x2006b3c0"]
  CMOS -->|"done / error / cancel signals"| Pipeline
  Pipeline -->|"current generation only"| Completion["Readout completion observation<br/>no inferred GIC line"]
  Capture["capture-control / SDES<br/>independent finite contracts"] -. "capture-to-readout join unverified" .-> Readout

  RAM -. "original producer join unverified" .-> Process["Processing endpoint PA 0x24070000<br/>accepts supplied complete buffers"]
  Supplied["Explicit complete input buffers"] --> Process
  RAM -. "original display packet / commit unverified" .-> Compose["Image-bank compositor<br/>guest packet and supported RAM layers"]
  DisplaySources["Supplied guest packet / RAM layers"] --> Compose
  Process -. "original consumer join unverified" .-> Compose
  Compose -->|"complete transaction"| Console["Functional QEMU console publication"]
  RAM -. "original capture/save join unverified" .-> Save["NVMe / BlockBackend storage"]
  StorageRequests["Guest storage requests / attached medium"] --> Save
```

The implemented profile maps `3e8/3ec/3f0` to primary destination/stride/span,
validates the secondary destination/stride without writing it, and accepts
only format pairs `00400040` and `00010001`. Functional samples are two bytes;
width is stride/2 and height is span/stride. The CMOS engine performs actual
timed DMA. Busy requests are rejected, stale generations cannot complete,
and cancellation/reset stop pending work. Strict/default profiles leave this
join disconnected. No physical pixel format, sensor timing, secondary-plane
producer, GIC route or downstream ISP/display join is inferred. See the
[readout mapping](devices/readout/profile.json),
[controller contract](devices/sensor/protocol.md#buffer-selectors-and-readout-parameters)
and [pipeline fixtures](../tests/qtest/nikon/cmos.c).

Image-bank composition and console publication already exist independently;
the missing acceptance is the original guest's natural producer/commit path,
not the existence of a functional compositor. The diagnostic frame descriptor
is opt-in and is not wired as a CMOS-to-display handoff. See
[image transactions](devices/banks/image.md) and
[GUI acceptance](startup/display.md).

### Processor and graphics execution

```mermaid
flowchart TD
  MMIO["MAS MMIO"] --> Processor["Request-only processor<br/>functional: PA 0x2c040000 / 0x04294000"]
  Processor -. "no execution provider connected" .-> Execution["Processor execution / ready / replies unresolved"]

  MMIO --> Streams["Graphics frontend PA 0x20180000<br/>owned immutable paired streams"]
  Streams --> Validate["Finite LUT family validation<br/>224 / 1688 bytes, operands / geometry / formats"]
  RAM["Guest RAM"] -->|"128 x 128 one-byte input"| LUT["Timed functional LUT executor"]
  Validate --> LUT
  LUT -->|"32 x 32 one-byte output"| Output["Descriptor-selected destination RAM"]
  LUT -->|"successful output commit"| Done["IRQ129 and post-completion<br/>status58 read with bit26 clear"]
  MMIO --> Copy["Ordinary byte-copy DMA"]
  RAM --> Copy
  Copy -->|"actual copied bytes"| CopyOut["Guest destination RAM"]
  Copy -->|completion| IRQ130["IRQ130"]
  Output -. "original scanout commit unverified" .-> Frame["Original GUI first frame absent"]
  MMIO --> Scalars["D100 / D200 / C0A0 retained scalars<br/>no event or completion side effect"]
```

The request-only processor retains the actual 16 KiB guest bootstrap and has
no execution provider. Historical experimental runs naturally time out; that
is not processor success or an SLA execution route.

The graphics frontend captures `8*units64` bytes with a functional 128-KiB
bound per stream. The supported program family requires the full 224/1688-byte
pair and validates its fingerprint, geometry, formats, 12-bit LUT entries,
backing and non-overlapping operands. A virtual timer samples the 128x128
input at `(4x,4y)`, applies the two LUTs and writes the 32x32 output before
IRQ129. Offset `0x58` is readable as zero only after successful completion;
other status states, fences, IRQ131 and other command families remain
unsupported. Reset cancels pending work. This is a finite functional law,
not recovered physical GPU precision or a screen rendering claim. See
[graphics transfer](devices/transfer/protocol.md) and its
[qtest](../tests/qtest/nikon/transfer.c) /
[unit fixtures](../tests/unit/nikon/transfer.c).

The 29-fixture/247-subtest receipt and the two full-stream original-bundle
experiments in [native receipts](validation/native.md) predate this executor.
Those runs retained pending streams and produced no IRQ129 or frame; their
results are historical, not measurements of current execution. No new
original-firmware result is claimed by these diagrams.

D200 zero and C0A0 bit0 are bounded retained scalar contracts. C010 and
PA `0x2c012000` remain strict errors. Static Thumb recovery places the
`1046bfxx` image-start route under `tftdbg`; it is not natural-startup evidence.

## Current implementation boundaries

| Pipeline | Implemented contract / recorded evidence | Remaining boundary |
|---|---|---|
| CPU startup | MAS CPU0/CPU1 roles observed; private SLA/EX instantiated | Physical CPU/board parameter qualification |
| External components | Standalone RX VR and GNU NDS32 TPJ providers; typed peer transport | Explicit launch/wiring; no automatic five-component CPU integration |
| Loader COPY | Two 4 MiB arenas; bounded 500*/502* RAM under functional | Physical backing/provider identity |
| EV3 | GUI mask0x10 producer; historical experimental wait return; profile enables notifications | Strict PA2013e000; physical notification contract unqualified |
| Processor | MAS-only request/bootstrap storage; historical natural timeout | Execution/ready/reply provider |
| Graphics | Full paired capture, finite LUT execution, output DMA, IRQ129 and post-completion status58 read | Other programs/formats, fences, IRQ131, other status semantics and original scanout join |
| Copy/composition | Byte-copy DMA IRQ130; supplied-layer composition and console publication | Natural original producer/commit chain |
| Display scalars | D100/D200/C0A0 finite contracts | C010, PA2c012000 and physical meanings |
| CMOS/readout | Opt-in finite mapping, timed primary DMA and generation-checked completion; rejection/cancel/reset fixtures | Strict profile disconnected; secondary output, physical formats/timing, GIC and downstream joins |
| GUI acceptance | Observation fixture explicitly reports missing evidence as `UNMET` | Natural source pixels, guest commit, completion and first console frame; interaction separately |

Hardware models may produce causal status, IRQ and scanout through finite
state machines. They must not force guest-software outcomes by writing guest
event/status state, invoking callbacks or ISRs, manipulating PC/task state,
or supplying a prefilled, fixed or host-drawn UI. Fixture success, unchanged
firmware progress, complete boot and physical evidence remain separate.
