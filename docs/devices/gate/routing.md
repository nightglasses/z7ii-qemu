# Control2010 Event Gate and Inflow Routing

The Control2010 Gate controller at physical address `0x20100000` provides an inflow admission barrier and interrupt routing across three 32-bit pending event domains (96 channels total).

```mermaid
flowchart TD
    subgraph ExternalInflow ["96 External Provider Lines"]
        D0["Domain 0 Lines [0..31]"]
        D1["Domain 1 Lines [32..63]"]
        D2["Domain 2 Lines [64..95]"]
    end

    subgraph AdmissionGate ["Global Gate (0x20100004)"]
        GateCtrl{"Gate Bit 0\n0 = Closed\n1 = Open"}
    end

    subgraph BankedDomains ["Three Banked Status & Mask Domains"]
        direction TB
        subgraph Dom0 ["Domain 0 (Bits 0..31)"]
            M0["Mask (0x08)"]
            R0["Raw Pending (0x14)"]
            A0["W1C ACK (0x20)"]
        end
        subgraph Dom1 ["Domain 1 (Bits 32..63)"]
            M1["Mask (0x0C)"]
            R1["Raw Pending (0x18)\nBit 0: Stop Confirmation"]
            A1["W1C ACK (0x24)"]
        end
        subgraph Dom2 ["Domain 2 (Bits 64..95)"]
            M2["Mask (0x10)"]
            R2["Raw Pending (0x1C)"]
            A2["W1C ACK (0x28)"]
        end
    end

    ExternalInflow --> GateCtrl
    GateCtrl -->|"Admitted if Open"| BankedDomains
    Dom0 -->|"Masked Output"| Aggregator["IRQ Aggregator"]
    Dom1 -->|"Masked Output"| Aggregator
    Dom2 -->|"Masked Output"| Aggregator
    Aggregator -->|"Single Wire"| GIC["GIC SPI 102"]
```

## Global Admission Gate (`0x20100004`)

- **Bit 0**: Master event inflow gate.
  - `0`: Gate closed. New external rising edges are dropped. Existing pending state and active mask settings are preserved.
  - `1`: Gate open. External input edges are admitted into the three raw pending domains.
- **Provider Tracking**: The controller monitors all 96 external provider line levels continuously, even when the gate is closed. Lines held active during closure do not synthesize spurious edges upon reopening.

## Banked Domain Register Map

All registers require strictly aligned 32-bit accesses.

| Domain | Mask Register | Raw Pending Register | W1C Acknowledge Register |
|---|---|---|---|
| **Domain 0** (Channels 0..31) | `0x08` | `0x14` | `0x20` |
| **Domain 1** (Channels 32..63) | `0x0C` | `0x18` (Bit 0 = Stop Done) | `0x24` |
| **Domain 2** (Channels 64..95) | `0x10` | `0x1C` | `0x28` |

### Acknowledgment Protocol
- Firmware ISR reads the raw status register (`0x14`, `0x18`, or `0x1C`) and writes back `1` to the corresponding bit in the W1C ACK register (`0x20`, `0x24`, or `0x28`) to clear the pending flag.
- The GIC SPI 102 line remains asserted as long as any unmasked pending bit in any of the three domains remains active.
