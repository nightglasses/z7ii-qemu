# DMA Channel Reconfiguration & Lifecycle

The Four-Channel Copy Engine supports channel reconfiguration, sticky control preservation, and sequential multi-transfer workflows.

```mermaid
sequenceDiagram
    participant FW as Camera Firmware / RTOS
    participant DMA as Copy Channel (0x04251010 + ch * 0x30)
    participant GIC as GIC SPI 45..48

    Note over FW,DMA: Transfer 1: Staging & Trigger
    FW->>DMA: Set COUNT, SRC, DST, CFG
    FW->>DMA: Write CTRL (+0x18) = 0x11109003 (Trigger)
    DMA->>DMA: Transfer bytes via virtual clock pacing
    DMA->>DMA: Status (+0x20) = 0x8 (Success)
    DMA->>GIC: Assert SPI line
    FW->>GIC: Handle IRQ
    FW->>DMA: Write Status (+0x20) = 0x0 (Clear Status)
    DMA->>GIC: Deassert SPI line

    Note over FW,DMA: Transfer 2: Reconfiguration
    FW->>DMA: Preserve sticky bits, write new COUNT, SRC, DST
    FW->>DMA: Write CTRL (+0x18) = 0x11109003 (Trigger)
    DMA->>DMA: Transfer second block
```

## Channel Reconfiguration Rules

1. **Sticky Control Bit Preservation**:
   - The setup method modifies only bits matching mask `0x00F09110` using ARM Bit Field Insert (`BFI`) instructions.
   - Sticky global mode bits (such as bit 28 and bit 24) are preserved across sequential transfers.
2. **Reconfiguration While Idle**:
   - Reconfiguring channel registers while the channel is idle (`status == 0` or after a completed transfer) is fully supported.
   - Modifying channel configurations while a DMA transfer is actively in flight is strictly prohibited and returns `MEMTX_ERROR`.
3. **Cancellation Semantics**:
   - Clearing low bit 2 while maintaining bit 28 cancels the active channel transfer. Cancellation sets error status `0x1` and stops virtual timer progression without corrupting target RAM.
