# Memory Domain Validation & Direct ROMD Transfers

The Four-Channel Copy Engine integrates direct address space qualification to ensure safe, high-speed DMA across heterogeneous memory regions.

```mermaid
flowchart TD
    Req["DMA Transfer Request\nSource: [SRC, SRC + LEN)\nDestination: [DST, DST + LEN)"] --> CheckSrc{"Source Region Direct?"}
    
    CheckSrc -->|"Yes (RAM or Direct ROMD)"| CheckDst{"Destination Region Direct & Writable?"}
    CheckSrc -->|"No (Hole or MMIO)"| FailSrc["Reject: Set Status = 1 (Bus Error)"]
    
    CheckDst -->|"Yes (Writable RAM)"| SplitChunks["Split at Section Boundaries\nMax 64 KiB Per Beat"]
    CheckDst -->|"No (ROMD, ReadOnly, Hole)"| FailDst["Reject: Set Status = 1 (Bus Error)"]
    
    SplitChunks --> Execute["Execute DMA Read/Write\nPaced by Virtual Clock"]
```

## Memory Region Classification Rules

1. **Source Regions**:
   - **System RAM**: Direct reads accepted.
   - **ROMD Firmware Sections**: Direct read access accepted. The copy engine bypasses `MemTxAttrs.memory` restrictions, enabling direct DMA copies from read-only flash/ROM representations into execution RAM.
   - **MMIO & Holes**: Strictly rejected; DMA does not generate peripheral side effects.
2. **Destination Regions**:
   - **System RAM**: Writable RAM accepted.
   - **ROMD & Read-Only Aliases**: Strictly rejected; flash images cannot be overwritten via the copy engine.
3. **Section Boundary Splitting**:
   - Transfers crossing multiple contiguous memory regions are automatically partitioned at section boundaries, ensuring accurate memory translation without reading past mapped extents.
