# Nikon firmware emulation on QEMU

This repository contains a diagnostic QEMU machine for unchanged Nikon Z7II C1.70 firmware components. It adds Nikon machines, devices, peers, and native tests beside a pinned upstream QEMU checkout. The machine is intended to make firmware execution and device contracts observable while each assumption is recorded and bounded.

The project is a software model of selected firmware interfaces. It does not claim a recovered BootROM, a complete board flash, a physical sensor or display specification, or real camera performance. The embedded Z7II machine's complete bundle composition installs the bounded loader contract internally; unchanged firmware has not yet naturally reached its first GUI frame. Unknown register fields, access widths, commands, and addresses remain errors until a source-backed contract and a focused test exist. Functional-model acceptance and physical-camera fidelity are separate results.

## Supported machine types

This repository is organized as a Nikon firmware family emulator. The first
complete camera target is the Z7II; the other machine types are reusable
component or software-domain profiles and do not claim a complete camera boot.

| Machine type | Domain and CPU | Current role | Status |
|---|---|---|---|
| `z7ii-mas-diagnostic` | Z7II MAS, Cortex-A7 SMP | Main camera startup, peer wiring, capture, display, and storage integration | Primary development target; original firmware has not reached a verified GUI first frame |
| `nikonsla` | SLA software domain, Cortex-A7 | Standalone decoded SLA loading and private peripheral contracts | Supported standalone profile; not a complete camera board |
| `nikonex` | EX component, Cortex-M4 | Independent EX firmware and device contract tests | Supported component profile |
| `nikonvr` | VR software profile, RX62N | Independent RX software and peripheral tests | Supported software profile |

New Nikon bodies or component domains should add a named machine under
`hw/nikon/machines` and a focused qtest before they are described as supported.

## Firmware input modes

The combined Z7II run is the target for system-level startup. Use the
`bundle=` machine property with a validated decoded container to load the five
component records (`EX`, `TPJ`, `MAS`, `VR`, and `SLA`) into one composed machine:

```sh
work/build/qemu/qemu-system-arm \
  -M z7ii-mas-diagnostic,bundle=/absolute/path/to/decoded-container.bin \
  -smp cpus=2,maxcpus=5
```

A complete bundle installs the bounded loader contract and connects
the MAS/SLA notification endpoints before either bridge is realized, and
joins readout-control requests to the existing CMOS timing/DMA
engine. That join validates the finite C1.70 mapping, writes only the submitted
primary buffer and publishes generation-checked completion. An explicit
PGM/PPM source is used when configured; otherwise the bundle path selects
the deterministic CMOS provider. It does not inject guest event objects, task
state or a prefilled UI. Missing physical-camera evidence, exact Bayer fidelity
or full GPU precision does not block functional implementation. Raw `-bios`
remains the isolated MAS input mode.

Persistent parameter data can be supplied separately through the explicit
[`descriptor17-input` / `descriptor18-input` properties](docs/inputs/nor/descriptor.md). A
[constructed functional profile](docs/inputs/nor/functional.md) uses unchanged
firmware to parse a hypothetical virtual-camera configuration; recovering a
physical factory image is a separate fidelity objective. The current experiment
passes ID34 initialization, RGBA clears, a gray panel and131 original glyph
outlines through original IRQ129. Those glyphs render the battery communication
warning into guest memory. Five subsequent gray rectangles also complete;
an inline A8 image submission adds the red warning icon through IRQ129.
The224/1736-byte conversion from a separate1024-wide RGBA buffer to YUV105
also completes, followed by720x480 scaling into1024x682 YUVA107. The current
176/1552-byte continuation now composites the warning surface with SRC_OVER
using retained107 context. The following bounded run has no graphics error
and returns from warning worker4 to worker0; GUI readiness remains unarmed.
The [current policy trace](docs/runtime/policy.md) crosses main mode5 using
the existing F4 input held before initial EX sampling. Original code selects
main mode1 and starts GUI worker3/mode1 while BatCk remains2. The finite
[channel9 initial-count load](docs/devices/timing/protocol.md) now reaches
the original timer callback with status1. The subsequent720x480 zero fill to
resource57 record5 at `0xbf309600` now completes through the original IRQ,
callback and waiter; readiness remains unarmed in the bounded continuation.
The [reproducible RNG input](docs/inputs/ex/rng.md) and
[finite UART battery peer](docs/runtime/batterypeer.md) now support the complete
finite battery command inventory, including status/metrics, configuration/control
and the conditional echo. Unchanged EX accepts authentication and normal class1
query continuation through this peer.
Source-backed9600-baud byte pacing now sustains normal UART polling; original
EX reaches charge100/diagnostic0 and MAS BatCk becomes1. See the
[protocol inventory](docs/runtime/batteryprotocol.md) for the modeled scope.
Joining that healthy peer with cold F4 changes the earlier warning path:
BatCk remains1, worker0 keeps processing events, and GUI stays idle rather
than entering the earlier warning-driven mode1. The [ordinary GUI investigation](docs/runtime/guiupdate.md)
captures the actual divergence: the no-peer battery report sets properties6401/6402,
and6401=1 selects GUI action8; the healthy report clears both properties and
skips that branch. Input lock80 and its delivered value0 are derived policy state, not a
missing readiness notification.
The subsequent [normal-request continuation](docs/runtime/guiupdate.md)
supplies complete acceleration/TEMP records and explicit raw sensor samples.
Unchanged firmware initializes the acceleration device through I2C2, completes
the direction contribution and selects application state2. Both qualified TEMP
candidates make the normal display request return0 and enter primary state1,
with BatCk1 and power1. The [MXIC read-view correction](docs/devices/config/view.md)
now completes the full configuration transaction with active readback selected.
The [timing divider/load model](docs/devices/timing/load.md) now covers the
complete source byte/load interface; raw31 means divide-by50. Original channel7
crosses all seven waits. The [capture-control model](docs/devices/sensor/control.md)
now completes complemented-mask and paired reset/enable initialization. The
[kind40 allocation](docs/runtime/kind40.md) now has exact source-backed RAM;
the original mapping from VA `0x36d59800` to PA `0x62059800` and complete clear
are verified. The [resource11/12 producer join](docs/runtime/rows.md) now covers
both target capacities and explicit primary NOR input. Original code loads and
converts all eight row layouts, with the complete2640-row output verified.
The [SYS configuration family](docs/devices/sysconfig/runtime.md) now supports
all three full-word ports `0x04280064/68/6c`; all25 memory qtests pass.
The [router timing model](docs/devices/router/timing.md) now covers all three
requests and a virtual TG input under explicit functional hypotheses. All23
SLA and25 memory qtests pass. Original IRQ87 callbacks release both subsequent
waits and application state advances to03/02; the next first rejection is
the capture-control stop request at that checkpoint. The complete
[capture run/stop model](docs/devices/sensor/runstop.md) now covers all11 unit
bits, separate low-level/upper quiescence causes and shared4/8 lifetime;
all25 sensor qtests pass. Original execution crosses that store and a later
GUI completion callback. The [audio format profile](docs/devices/audio/format.md)
now covers the complete source-shaped format/port/divider fields and block
programming, with15 native audio/DMA/HDMAC cases passing. The subsequent
[HDMAC reload model](docs/devices/hdmac/reload.md) covers all8 RC/RS/RD
combinations and passes21 applicable native cases plus focused boundary/guard
checks. Original setup crosses both input descriptor stores/starts, sets audio
input enable and reaches the microphone initializer's return sequence.
The subsequent [Audio observation profile](docs/devices/audio/monitor.md)
supports both paired getters under explicit last-observed/packing assumptions;
all22 applicable Audio/DMA/HDMAC cases pass. The original input getter and
amplitude conversion now return. At that Audio-only checkpoint the next rejection is CPU1 WRITE32
`0x201810e0=1`, PC `0x106ee3c6`, with `transfer-stream-truncated` in the
graphics parser. Input selectors5/6 still lack a sample producer; actual sample
transfer and audio completion remain unverified.
The [inline rectangle model](docs/devices/transfer/rectangles.md) now handles
that856/1088-byte family, including variable counts/colors, SRC alpha clearing,
key wrap and strict RAM/alias boundaries. All14 transfer unit cases pass.
Unchanged original execution submits the four rectangles and receives IRQ129.
The [RAM backing fix](docs/startup/ram.md) resolves the subsequent host IOTLB
assertion in resource57 record0 while preserving exact physical boundaries;
all 43 applicable memory/transfer native cases pass. Original execution now
reaches a 2120/2368-byte, ten-rectangle continuation without the earlier
initialization prefix. It is rejected by the current graphics parser; GUI
gate/pending3/display-ready remain zero and no GUI first frame is established.
The earlier432/616-byte
pair belongs to the erased-TEMP failure continuation. No GUI first frame is
claimed from accepted request admission.
The earlier [readiness trace](docs/runtime/readiness.md), without the peer,
observes selector1 skipping the selector34 gate; GUI mode entry reaches its
return with gate0 and pending3=0.
Final scanout and a GUI first frame remain unverified.

The [wire-only battery tool](docs/runtime/batterytool.md) is built as
`work/build/qemu/tools/nikon/nikon-battery` by the normal source graph, with
native arithmetic, range, cancellation and CLI fixtures. It derives first-auth
replies and provides the complete finite protocol described above; unknown
commands and fields remain explicit errors.

The raw `-bios mas-component.payload.bin` form is a focused MAS-only diagnostic
input. It is useful for isolating MAS startup and RTOS behavior, but it is not a
replacement for the combined bundle path. `nikonsla`, `nikonex`, and `nikonvr`
are standalone component or software-domain machines used for isolation and
contract tests; they do not replace the composed Z7II machine.

This repository starts after Nikon package decoding. Its input contract is the
complete decoded five-component container; outer-package decryption, signature
verification, and component decoding belong to the upstream firmware intake
pipeline and are outside this QEMU project. The bundle loader refuses partial
or ambiguous component input before any component becomes visible to a machine.

## Scope and boundaries

- QEMU can and must implement complete finite deterministic hardware behavior for unchanged firmware. A functional model may implement CMOS/readout/service/graphics state machines, DMA, IRQs and scanout without claiming exact physical behavior. Only direct guest-software outcome injection is forbidden: writing guest event or status state to force success, invoking guest callbacks or ISRs, manipulating the guest PC or task state, or presenting a prefilled, fixed or host-drawn UI.
- The primary ARM machine is `z7ii-mas-diagnostic`. It supports both the raw MAS diagnostic input and the validated five-component bundle. A raw MAS payload is not a complete update package. `nikonsla` is a separate ARM software-domain machine; `nikonex` and `nikonvr` cover the component profiles listed above.
- CPU reset, ROM loading, RAM, MMIO, DMA, GIC, timers, storage, serial, input, processing, and display paths are implemented only where their firmware-facing contracts are known.
- CMOS input is an independent, guest-configured transport endpoint. It accepts mode, crop, scale, format, byte geometry, buffer rotation, exposure, period, readout timing, and opaque source fields; it preserves unknown words and can sample an explicitly supplied binary PGM/PPM image into the guest-configured buffers. The deterministic byte source is available to the complete bundle and explicit fixtures. This is a functional transport contract, not a recovered sensor specification.
- Eye proximity input is a separate I2C device contract. It never supplies image rows, CMOS completion, ISP data, or a display frame. Its register access can be exercised independently; measurement values, physical wiring, and an interrupt producer remain unbound.
- SDES channel/global control, `capture-control`, and `readout-control` retain their source-backed MMIO fields, command/status publication, buffer descriptors, and request boundaries. The complete bundle joins a bounded readout mapping to CMOS primary-buffer DMA and generation-checked completion; capture/SDES and downstream ISP/display joins remain independent boundaries.
- The display path composes supplied guest buffers and can publish a QEMU console image after a completed transaction. A rendered image is not evidence of the original camera menu or LCD.
- The 8x8 fixture is transport-only. It checks packet and buffer delivery through the declared seam; it is not a CMOS, ISP, display, or complete-camera-startup fixture.
- Storage uses QEMU's mature NVMe and `BlockBackend` paths with an explicitly attached medium. It does not assert a particular card, filesystem image, capacity, or physical bus implementation.
- Unsupported transactions return `MEMTX_ERROR`, record the first offending CPU/PC/address/direction/width, and request a QEMU debug stop. An architectural exception or an instruction fetch outside mapped memory can stop through upstream ARM handling before the asynchronous request is processed.

## Architecture and startup flow

The detailed CPU, memory, device, and runtime pipeline diagrams are in
[`docs/architecture.md`](docs/architecture.md). The parameter matrix and
evidence classification are maintained in
[`docs/startup/cpu_board.md`](docs/startup/cpu_board.md).

```mermaid
flowchart TD
  Input["Raw MAS payload or decoded bundle"] --> Loader["Selected loader / virtual reset"]
  Loader --> MAS["MAS Cortex-A7 x 2<br/>guest RTOS / MMIO / DMA"]
  Loader -->|"bundle only"| SLA["Private SLA Cortex-A7 x 2"]
  Loader -->|"bundle only"| EX["Private EX Cortex-M4"]
  MAS <--> Bridge["MAS / SLA bridge<br/>notifications opt in"]
  SLA <--> Bridge
  EX <--> SPI["EX SPI connection"]
  SPI <--> MAS

  MAS --> Readout["readout-control"]
  Readout -->|"complete bundle only"| CMOS["Finite readout mapping<br/>CMOS timed DMA / generation completion"]
  Source["Explicit image or<br/>bundle deterministic input"] --> CMOS
  CMOS --> RAM["Submitted primary guest buffer"]
  MAS --> Graphics["Finite paired graphics LUT executor<br/>actual output, IRQ129, completed status58 read"]
  GraphicsRAM["Guest-selected graphics input RAM"] --> Graphics
  Graphics --> Output["Descriptor-selected output RAM"]
  MAS --> Copy["Ordinary byte-copy DMA / IRQ130"]
  Copy --> CopyRAM["Guest-selected copy destination RAM"]
  MAS --> Banks["Image-bank compositor<br/>guest packet / supported RAM layers"]
  DisplayRAM["Supplied guest RAM layers"] --> Banks
  Output -. "natural scanout join unverified" .-> Banks
  RAM -. "natural display producer join unverified" .-> Banks
  Banks --> Console["Completed transaction<br/>functional QEMU console"]
  RAM -. "original capture/save join unverified" .-> Storage["NVMe / BlockBackend<br/>explicit attached medium"]
  MAS --> Processor["Request-only processor / bootstrap store"]
  Processor -. "no execution provider" .-> Pending["Execution / ready / replies unresolved"]
  MAS --> Unknown["Unsupported transaction"]
  Unknown --> Stop["MEMTX_ERROR / diagnostic debug stop"]
```

Solid arrows show implemented functional contracts, including declared model
assumptions; dashed arrows identify unverified runtime joins or absent
providers. The bundle instantiates MAS, SLA and EX CPUs in one ARM process.
VR uses a separate RX QEMU profile, and TPJ has a separate GNU NDS32 runner
under `sim/nds32`; neither is automatically started by the bundle loader.
External peers require explicit transport attachment.

The readout join validates the primary destination, stride, span and the two
accepted C1.70 format pairs before timed CMOS DMA. Secondary ranges are
validated but not written. Busy requests, stale generations, cancellation and
reset preserve ownership boundaries. This is a functional mapping, not a
recovered sensor format, timing law, GIC route or CMOS-to-ISP/display path.

Graphics stream counts 28/211 are 64-bit units, yielding full 224/1688-byte
snapshots. The finite LUT executor validates the supported program family,
reads a 128x128 input and writes a 32x32 result before raising IRQ129. Finite
RGBA profiles also clear surfaces and fill bounded affine MOVE/LINE/QUAD/CLOSE
outlines using Cairo coverage, retained720x480 context and black SRC_OVER.
No host font or replacement text is used. The post-completion offset `0x58`
read reports bit26 clear; other status semantics, fences, IRQ131 and unsupported
program families remain explicit boundaries. Ordinary
byte-copy IRQ130 and image-bank composition/console publication are separate
implemented paths. No graphics output buffer is presumed to be a screen.

The MAS request-only processor at PA `0x2c040000` plus bootstrap view
`0x04294000` still has no execution provider. Historical original experiments
naturally timed out there; earlier full-stream receipts also predate the LUT
executor and retain pending graphics work. See [runtime evidence](docs/runtime/coprocessor.md)
and [native receipts](docs/validation/native.md) for those historical results.
[The startup frontier](docs/validation/frontier.md) states what the supplied EX
component demonstrably reaches and which gates it cannot reach at all.
No new original-firmware GUI result is implied by the current source graph.
D100/D200/C0A0 retain bounded scalar state with no event/completion side effect;
C010 and PA `0x2c012000` remain strict boundaries. The `1046bfxx` image-start
chain belongs to the registered `tftdbg` command, not natural startup.

The [detailed architecture](docs/architecture.md) records memory ownership,
CPU/IRQ wiring, notification hypotheses and the remaining producer/consumer
joins. Complete boot and a naturally produced original GUI first frame remain
unverified. The guest owns callbacks, event objects, task state, display
packets and save decisions.

## Dependencies and build

The `qemu/` submodule is the official QEMU source pinned to `v11.1.2` (`4fc49f46dc95d4a27de2509e7fceb2931e91faeb`). Keep that checkout clean; the project-owned source is under `hw/nikon`, `include/hw/nikon`, `tests/qtest/nikon`, and `tests/unit/nikon`.

Install CMake 3.24 or newer, a C compiler, Make, Meson/Ninja, `pkg-config`, GLib, pixman, Cairo 1.16 or newer, and the Python packages required by the upstream QEMU build. Then run:

```sh
git submodule update --init --recursive
make -j8
make check
make bench
```

The root Makefile asks CMake to compose an ignored source view and then invokes QEMU's normal configure and generated build. The default executables are `work/build/qemu/qemu-system-arm` and `work/build/qemu/qemu-system-rx`. Build directories and source views can be moved without changing the source tree:

```sh
make configure \
  CONFIGURE_FLAGS='--target-list=arm-softmmu,rx-softmmu --enable-tcg \
    --disable-docs --disable-tools --disable-user --disable-guest-agent --disable-werror'
make BUILD_DIR=/absolute/path/to/build -j8
```

No firmware is downloaded by the build. Keep extracted images, logs, sockets, and debugger scripts in the ignored `work/` directory.

## Run the original MAS model

Use an extracted MAS component payload. The machine also accepts synthetic diagnostic images used by qtests, but a synthetic image does not represent original firmware behavior.

```sh
mkdir -p work/runs/r1
work/build/qemu/qemu-system-arm \
  -M z7ii-mas-diagnostic \
  -accel tcg,thread=single \
  -bios /absolute/path/to/mas-component.payload.bin \
  -display none -serial none -monitor none -S \
  -gdb unix:work/runs/r1/gdb.sock,server=on,wait=off \
  -qmp unix:work/runs/r1/qmp.sock,server=on,wait=off \
  -D work/runs/r1/qemu.log -d guest_errors,int
```

The `-S` option leaves the CPUs paused for reset inspection. Use the upstream GDB stub for registers and memory, and QMP for `query-status`, `query-cpus-fast`, resume/stop, and device inspection. Use a short per-run socket path on macOS. For a focused one-core fixture, pass `-smp 1`; use the machine default when observing the original two-core handshake.

Record the input hash, command line, QMP status, GDB registers, first stopped access, and exit status in the run directory. A bounded stop, a debugger breakpoint, or a process exit is an observation; it is not proof of complete boot.

## QEMU workflow

1. **Define the input.** Keep the original component bytes unchanged and record their length and digest. Synthetic fixtures must identify their generated payload and purpose.
2. **Start at reset.** Confirm the reset PC, CPU state, loader range, and initial RAM assumptions before adding a device response.
3. **Follow the bus.** Let the guest perform MMIO, DMA, copies, cache-visible handshakes, IRQ acknowledgement, and task setup. Add only finite, source-backed register ranges and values.
4. **Exercise the contract.** Add or update a native qtest/unit fixture for normal transactions, reset, cancellation, invalid widths, absent inputs, and boundary addresses. Keep transport, processing, display, and storage fixtures independent.
5. **Run unchanged firmware.** Re-run the same payload and capture the next natural stop. A new stop location is progress only when the preceding guest work and all required acknowledgements are visible in the receipt.
6. **Classify the result.** Report model coverage, fixture success, original-firmware progress, and physical-camera validation as separate outcomes.

Detailed contracts and observations live under [`docs/`](docs/), with the source layout in [`docs/layout.md`](docs/layout.md) and build ownership in [`docs/build.md`](docs/build.md). Device pages state the exact mapped range, accepted widths, reset assumptions, interrupt behavior, and unsupported cases.

## Tests and acceptance

`make check` runs the Nikon qtest suite and native unit fixtures in the configured QEMU build. `make bench` runs the native performance fixture. Focused runs can be selected through the upstream generated test target, for example:

```sh
make check MTESTARGS='nikon_usio nikon_vr'
make check MTESTARGS='nikon --test-args="-p /arm/nikon/processing/complete"'
```

The suite covers synthetic ARM execution, reset and loader checks, RAM and bus boundaries, device state machines, real DMA copies, GIC IRQ delivery, peer transport, processing buffers, display composition, and storage against an attached block backend. Fixtures contain no private credentials and do not require original firmware bytes.

Use these acceptance levels when reporting a result:

| Level | Evidence | What it establishes |
|---|---|---|
| Build | Clean submodule and successful `make` | The configured source graph compiles and links. |
| Fixture | Focused qtest/unit receipt with exit status | The declared synthetic contract works for the tested inputs and boundaries. |
| Original firmware | Unchanged payload, command, stop PC/access, and registers | The guest reached the recorded point under the stated virtual assumptions. |
| Display or storage result | Completed transaction plus QMP pixels or backend byte checks | The modeled endpoint produced the checked output. It does not identify the physical camera path. |
| Physical camera | Independent hardware procedure and receipt | Hardware behavior beyond this QEMU model. |

Do not turn an unknown-field stop into a successful default. Do not describe the 8x8 transport fixture, a synthetic frame, or a QEMU console image as complete camera startup.

## Repository map

- [`hw/nikon/machines`](hw/nikon/machines): machine composition, CPU domains, memory maps, and IRQ wiring.
- [`hw/nikon/devices`](hw/nikon/devices): finite QOM/SysBus, PCI, DMA, input, processing, display, and storage models.
- [`hw/nikon/components`](hw/nikon/components): validated component-container views and loaders.
- [`include/hw/nikon`](include/hw/nikon): public device and machine interfaces.
- [`tests/qtest/nikon`](tests/qtest/nikon) and [`tests/unit/nikon`](tests/unit/nikon): native functional and boundary fixtures.
- [`docs/`](docs): source-backed protocols, startup observations, validation receipts, and model limits.

Contributions should keep the upstream submodule untouched, add a focused contract and test for each new behavior, and state the evidence boundary in the corresponding documentation.
