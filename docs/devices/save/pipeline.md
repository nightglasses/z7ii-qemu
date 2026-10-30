# File save pipeline and storage boundary

This document defines the C1.70 file save control and data boundary for the
Nikon Z7II, tracing captured image buffers through the RTOS file writing queue,
the FAT32 filesystem layer, the Block I/O bridge, and into the NVMe storage
controller.

## Data and control flow overview

```
[Captured Frame Buffer] (Kind 16: 0x54900000, 91.58 MiB RAW / JPEG)
         |
         v
MAS 0x10567d8e -> 0x105671ba (File write API)
         |
         v
MAS 0x105bea78 (Builds 208-byte packet, opcode 13)
         |
         v
RTOS Queue 0xb1808a14 (File save request queue)
         |
         v
MAS 0x1044b300 (Save worker, command 0x42091001)
         |
         v
MAS 0x1044b270 (De-multiplexer)
         |
         v
MAS 0x1047109c -> 0x104666fe (FAT32 filesystem layer)
         |
         v
MAS 0x104655c2 (Cluster / sector allocation)
         |
         v
MAS 0x10464d6c / 0x10464e92 (Sector buffer writes)
         |
         v
MAS 0x1047e768 (Sector callback)
         |
         v
MAS 0x104b7a4c -> MAS 0x104d5a3a (Block I/O bridge)
         |
         v
MAS 0x105143a0 (Calculates sector count, checks 512B alignment)
         |
         v
NVMe MMIO 0x2a900000 (`nikon.storage`)
         |
         v
GPEX PCI Host + NVMe Controller (QEMU upstream)
         |
         +-----------------------------+
         |                             |
         v                             v
DMA Sector Write to Media       INTx / MSI Interrupts
   (`slot0-drive`)             (IRQ237 / IRQ242..246)
         |                             |
         +--------------+--------------+
                        |
                        v
        `/machine/save` (`nikon-save`)
             State Machine Checkpoint
          (REQUESTED -> COMMITTED)
```

## Original firmware evidence

### 1. Producer buffer and format

- **RAW buffer source**: Captured RAW frames reside in capture pool kind 16 at
  PA `0x54900000` (136.2 MiB capacity). A full-frame FX LE16 image spans
  8320 × 5504 samples = 91,586,560 bytes (87.34 MiB) with 16,640-byte row
  stride.
- **Format representation**: Format is identified as `NIKON_SAVE_FORMAT_RAW`
  (`0xb101`) or `NIKON_SAVE_FORMAT_JPEG` (`0x3801`).

### 2. File write packet and RTOS queue

1. **File object write API (MAS VA `0x10567d8e`)**: Called by image encoding and
   metadata builders with a write struct on the stack:
   - `[sp, #0]`: write flag (`1`).
   - `[sp, #4]`: buffer pointer (`0x54900000`).
   - `[sp, #8]`: length (aligned to 8 bytes).
   - Return value compared against success code `0x043a0080`.
2. **Packet builder (MAS VA `0x105bea78`)**: Formats a 208-byte packet on the
   stack:
   - Data buffer address at `[sp, #40]`.
   - Data byte length at `[sp, #52]`.
   - Opcode `13` (`0xbc`) stored at `[sp, #188]`.
   - Calls `0x105be866` to enqueue the packet to RTOS message queue `0xb1808a14`.

### 3. FAT32 filesystem dispatch

1. **Save command worker (MAS VA `0x1044b300`)**: Dequeues packets matching
   command ID `0x42091001` (`-0xbdf6efff`), extracts buffer and length from
   fields `+4` and `+12`, and forwards to `0x1044b270`.
2. **Handle lookup and dispatch (`0x1044b270`)**: Special handle `0x7fff0000`
   calls callback `[0xb1808a90]`. Standard handles look up context in
   `0xb1d27d38` and invoke `0x1047109c`.
3. **Cluster allocation loop (MAS VA `0x1047109c`)**: Iteratively calls
   `0x104666fe(handle, buffer, length, &bytes_written)`.
4. **Cluster worker (MAS VA `0x104655c2`)**: Allocates clusters within the FAT32
   table, splits large transfers into 512-byte sector chunks, and delegates to
   sector workers `0x10464d6c` and `0x10464e92`.
5. **Sector callback (MAS VA `0x1047e768`)**: Invoked per sector batch to track
   filesystem metadata updates.

### 4. Block I/O bridge and NVMe driver

1. **Block I/O bridge invocation (MAS VA `0x104b7a4c`)**: Calls bridge entry
   point MAS VA `0x104d5a3a`.
2. **Alignment and sector calculation (MAS VA `0x105143a0`)**:
   - Fetches block device context from `0xb18012d4`.
   - Reads sector size from `[r0, #12]` (`512`).
   - Computes sector count: `count = length / 512` (`udiv r5, r3, r1`).
   - Checks remainder: `length % 512 == 0` (`mls r1, r1, r5, r3`).
   - Checks bounds against total medium capacity at `[r0, #24]`.
3. **NVMe submission**: Formats NVMe write commands (opcode 1) and writes to
   the controller doorbell registers at `0x2a900000`.

### 5. Media execution and interrupts

1. **NVMe host (`nikon.storage`)**: Maps DBI at `0x2a900000`, config at
   `0x2a800000`, memory BAR at `0x2a810000`, PHY at `0x2b200000`, and platform
   registers at `0x2b100000`.
2. **BlockBackend transfer**: The upstream NVMe controller transfers data from
   guest RAM directly into the drive image attached to `slot0-drive`.
3. **Interrupt routing**: Upstream controller raises INTx/MSI interrupts,
   routed via `aggregate` OR-gate to shared GIC IRQ237, and per-queue lines
   to IRQs 242, 243, 244, and 246.

## Host handoff contract (`nikon-save`)

The host device at `/machine/save` tracks the complete lifecycle:

| Phase | State | Trigger | Next allowed |
|---|---|---|---|
| 0 | `IDLE` | Boot / reset | `REQUESTED` |
| 1 | `REQUESTED` | `request-token` written with valid source, length, format | `COMMITTED`, `FAILED`, `CANCELLED` |
| 2 | `COMMITTED` | `commit-token` matches pending token | Reset to `IDLE` |
| 3 | `FAILED` | `fail-token` matches pending token, error != 0 | Reset to `IDLE` |
| 4 | `CANCELLED` | `cancel-token` matches pending token | Reset to `IDLE` |

- **Zero-media isolation**: The handoff contract maintains observable state
  without manufacturing synthetic images or fabricating guest completions.
- **QOM control and observability**: Exposes `requests`, `commits`, `failures`,
  `token`, `source`, `length`, `format`, `error`, and `phase` as read-only
  properties. Exposes writable setters `request-source`, `request-length`,
  `request-format`, `request-token`, `commit-token`, `fail-token`, and
  `cancel-token` for verifiable integration test execution.
