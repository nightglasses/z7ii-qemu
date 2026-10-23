# Dual-Core ARM Exception Mode Stacks

The Nikon EXPEED architecture provisions dedicated 16 KiB physical stack spaces for each of the six ARM Cortex-A7 execution modes, banked independently across CPU Core 0 and CPU Core 1.

```mermaid
flowchart TD
    subgraph Core0Stacks ["Core 0 Mode Stacks (Stride: 0x4000)"]
        direction TB
        C0_UND["UND Mode Top: 0xB2BB0000 (16 KiB)"]
        C0_SVC["SVC Mode Top: 0xB2BA8000 (16 KiB)"]
        C0_ABT["ABT Mode Top: 0xB2BA0000 (16 KiB)"]
        C0_FIQ["FIQ Mode Top: 0xB2B98000 (16 KiB)"]
        C0_IRQ["IRQ Mode Top: 0xB2B90000 (16 KiB)"]
        C0_SYS["SYS Mode Top: 0xB2B88000 (16 KiB)"]
        
        C0_UND --- C0_SVC --- C0_ABT --- C0_FIQ --- C0_IRQ --- C0_SYS
    end

    subgraph Core1Stacks ["Core 1 Mode Stacks (Offset: -0x4000)"]
        direction TB
        C1_UND["UND Mode Top: 0xB2BAC000 (16 KiB)"]
        C1_SVC["SVC Mode Top: 0xB2BA4000 (16 KiB)"]
        C1_ABT["ABT Mode Top: 0xB2B9C000 (16 KiB)"]
        C1_FIQ["FIQ Mode Top: 0xB2B94000 (16 KiB)"]
        C1_IRQ["IRQ Mode Top: 0xB2B8C000 (16 KiB)"]
        C1_SYS["SYS Mode Top: 0xB2B84000 (16 KiB)"]
        
        C1_UND --- C1_SVC --- C1_ABT --- C1_FIQ --- C1_IRQ --- C1_SYS
    end
```

## Stack Layout & Top Calculations

Each mode stack provides 16 KiB (`0x4000` bytes) of dedicated descending stack memory:

$$\text{Stack Top}(\text{mode}, \text{core}) = \text{Base Top}(\text{mode}) - (\text{core} \times 0x4000)$$

| Execution Mode | Base Top (CPU 0) | Base Top (CPU 1) | Size | Primary Usage |
|---|---|---|---|---|
| **Undefined (`UND`)** | `0xB2BB0000` | `0xB2BAC000` | 16 KiB | Undefined instruction traps |
| **Supervisor (`SVC`)** | `0xB2BA8000` | `0xB2BA4000` | 16 KiB | System call wrappers, OS kernel calls |
| **Abort (`ABT`)** | `0xB2BA0000` | `0xB2B9C000` | 16 KiB | Prefetch and Data Abort exception handlers |
| **FIQ (`FIQ`)** | `0xB2B98000` | `0xB2B94000` | 16 KiB | Fast interrupt service routines |
| **IRQ (`IRQ`)** | `0xB2B90000` | `0xB2B8C000` | 16 KiB | Peripheral interrupt dispatching & ISR entry |
| **System (`SYS`)** | `0xB2B88000` | `0xB2B84000` | 16 KiB | Initial bootstrap task execution stack |

## Inter-Core Isolation & Safety

1. **SMP Stride**: The 16 KiB per-core offset prevents stack corruption when both cores handle simultaneous interrupts (e.g. Core 0 processing GIC SPI while Core 1 handles SGI).
2. **Context Preservation**: During hardware interrupt entry, the CPU switches to IRQ mode, saves context into its private IRQ stack, acknowledges GIC IAR, and transitions to SVC mode for high-level RTOS service execution.
