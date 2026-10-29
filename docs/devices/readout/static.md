# Full-size static readout contract

This document defines the C1.70 static full-size RAW readout contract for the
Z7II CMOS and readout pipeline, transitioning from liveview/preview auxiliary
readout to full-size static frame readout upon shutter press.

## Original firmware evidence

### Shutter trigger and capture dispatch

The capture sequence is initiated by user shutter button actuation and decoded
through the multi-processor event pipeline:

1. **Front button decoding**: The EX subsystem receives button state changes and
   packages them into a 124-byte input frame delivered via SSI1 to MAS. The frame
   handler at MAS VA `0x104343c6` decodes shutter button transitions into the
   internal state buffer at `0xb25eae30`.
2. **Input snapshotting**: The command 7 input snapshot handler at MAS VA
   `0x10433ebc` reads `0xb25eae30` into snapshot word `0xb25eaeac`.
3. **Shutter execution**: The shutter request executor at MAS VA `0x10764958`
   initiates capture via `0x103ba6ea` and entry point `0x103ba164`, establishing
   a capture deadline of up to 60,000 ms (`0xea60`) for complete readout.

### Descriptor derivation and programming

Static readout parameters are derived and programmed by the C1.70 firmware
pipeline:

1. **Chunking derivation**: Derivation chunking is computed from the 10-byte ROM
   table at `0x118e52f4` (with chunk index `0x88 == 2`) at MAS VA `0x102c0304`
   and `0x102c0966`.
2. **Descriptor builder**: MAS VA `0x1023869e` computes the transport descriptor,
   deriving the row stride, buffer span, format pair (`0x00400040` /
   `0x00010001`), and pulling double-indirect buffer pointers.
3. **Register programming**: Parameter writers at `0x1020c7c6` and `0x1020a3da`
   write descriptor fields to the readout parameter bank at `0x2006b000`
   (`0x2006b3c0..0x2006b3fc`).
4. **Mode selection**: Handler `0x1020a2ae` packs mode byte `0xb26b489e` into
   `0x2006b394[23:16]`.
5. **Interrupt callback**: IRQ94 callback `0x102391b2` for completion event
   `0x10000000` checks bit 3 (`0x08`) of raw0 for mode 0, or bit 5 (`0x20`) for
   mode 1, and posts event bit 29 (`0x20000000`) upon completion. Mode 0 and mode
   1 alternate as ping-pong buffer destinations.

## Geometry and memory bounds

### FX full-frame resolution and sensor margins

- **Active resolution**: 8256 × 5504 (45.44 MP FX format).
- **Physical sensor row width**: 8320 samples per row LE16, providing 64 margin
  and optical black columns (32 columns per side).
- **Row stride**: `8320 * 2 = 16,640` bytes (`0x4100`), satisfying 256-byte
  alignment (`stride & 0xff == 0`).
- **Sensor lines**: 5504 active FX lines (up to 5536 lines including vertical
  optical black lines).
- **DX crop format**: 5408 × 3600 effective, stride = 11,008 bytes (5504
  samples), span = 39.63 MiB.

### Buffer span calculation

| Mode / Format | Width (samples) | Height (lines) | Stride (bytes) | Span (bytes) | Span (MiB) |
|---|---|---|---|---|---|
| FX Active | 8320 | 5504 | 16,640 (`0x4100`) | 91,586,560 | 87.34 MiB |
| FX Extended OB | 8320 | 5536 | 16,640 (`0x4100`) | 92,119,040 | 87.85 MiB |
| DX Crop | 5504 | 3600 | 11,008 (`0x2b00`) | 39,628,800 | 37.79 MiB |

### Memory pools

Memory pool extraction from C1.70 ROM table `0x11982938` establishes:

- **Capture pool kind 16**: Base address `0x54900000`, size `0x8834000` (142,819,328
  bytes = 136.2 MiB), containing 4 buffer units of `0x220d000` (34.05 MiB).
- **Override pools**: Mode 2/4 pools include kind 44 (`0x62700000`, 1.21 GB /
  `0xdbb1d000`, 390.8 MB) and kind 46 (`0xe2616400`, 201 MB / `0x736a4000`, 306 MB).

### Model boundary relaxation

The functional model enforces finite source-backed boundaries:

- **Span ceiling**: The span validation ceiling in `sensor.c` and
  `CMOS_MAX_BUFFER_BYTES` in `cmos.c` is expanded to 144 MiB (`144U * 1024 * 1024`),
  fully covering capture pool kind 16 (136.2 MiB) and the FX full-frame span
  (87.34 MiB – 87.85 MiB), while strictly preserving `nikon_cmos_writable` RAM
  preflight checks.
- **Dimension checks**: Dimensions are bounded to width <= 8448, height <= 5632,
  and total pixels <= 48 MP (`48 * 1024 * 1024`).

## RAW readout execution and delivery

The readout pipeline executes full-size static frame acquisition:

1. **Request snapshot**: On `0x2006b400 = 1`, the committed descriptor fields
   (`0x2006b3e8..0x2006b3fc`) are snapshotted into `NikonReadoutC170Mapping`.
2. **Provider reservation**: Provider ready is published to the readout control
   block, locking the active generation.
3. **CMOS DMA transfer**: The CMOS engine transfers the frame in timed 4096-byte
   chunks directly into the target guest RAM address space via DMA.
4. **Completion signaling**: Upon reaching `DONE`, the pipeline raises IRQ94 with
   raw0 bit 3 (mode 0) or bit 5 (mode 1), signaling the firmware consumer.

## File save boundary

File saving transitions from the capture completion consumer to host storage:

1. **Firmware save workers**:
   - Save request worker at MAS VA `0x1044b300` (command `0x42091001`).
   - Sector callback at MAS VA `0x1047e768`.
   - Block I/O bridge at MAS VA `0x104d5a3a`.
   - File object write path `0x10567d8e -> 0x105671ba -> 0x105bea78`.
2. **Host save device**: `nikon-save` is instantiated under `/machine/save`,
   providing a synchronous host-side handoff contract (`request -> commit / fail /
   cancel`). Requests carry token, guest source address, byte length, and format
   (RAW/JPEG). Commits record durable storage completion without fabricating media.

## Native verification receipts

- **Unit tests**:
  - `tests/unit/nikon/cmos.c`: `test_static_fullsize_acquire` covers full-size
    8320 × 5504 acquisition and validates dimension boundaries (8448 × 5632 ceiling).
  - `tests/unit/nikon/save.c`: covers save lifecycle, token validation, commit,
    failure, and cancellation.
- **QTest integration**:
  - `tests/qtest/nikon/cmos.c`:
    - `test_pipeline_static_fullsize`: tests complete 8320 × 5504 (91.58 MiB)
      readout into kind 16 pool at `0x54900000`, verifying accepted request,
      DMA completion, generation matching, and IRQ94 mode 0 completion.
    - `test_pipeline_rejections`: tests that spans exceeding 144 MiB are rejected.
  - `tests/qtest/nikon/save.c`:
    - `test_cold_handoff`: verifies cold reset properties on standalone device.
    - `test_machine_save`: verifies realized `/machine/save` device on the Z7II
      machine with idle state.
