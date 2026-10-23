# Control2010 Runtime Packed Configuration

The Control2010 controller maintains a bank of sparse 32-bit registers for packed subsystem parameters, filter coefficients, and input aperture routing.

```mermaid
flowchart TD
    subgraph ConfigRegisters ["Control2010 Parameter Bank (0x20100000)"]
        R30["Opaque Word 0 (0x30)\nBit 0: Clear/Assert\nFull 32-bit RMW"]
        R34["Opaque Word 1 (0x34)\nFull 32-bit RMW"]
        PackedF["Packed Field Group 0\n0xF4, 0x108 (Mask: 0x000FFF00)"]
        PackedW["Packed Field Group 1\n0xF8..0x104, 0x120..0x12C (Mask: 0xFFFFFFF0)"]
        PackedB["Packed Field Group 2\n0x10C (Mask: 0xFF0FFF00)"]
        InApertures["Input Apertures\n0x2000, 0x2100 (Bits [3:0]: Mode 0/1)"]
    end
```

## Register Layout

All accesses require aligned 32-bit operations.

| Offset | Register Name | Width | Legal Mask | Description |
|---|---|---|---|---|
| `0x030` | `GATE_CFG_WORD0` | 32-bit | `0xFFFFFFFF` | Opaque 32-bit configuration word. Firmware performs bitwise OR/BIC operations. |
| `0x034` | `GATE_CFG_WORD1` | 32-bit | `0xFFFFFFFF` | Opaque 32-bit configuration word. |
| `0x0F4`, `0x108` | `GATE_PACKED_A` | 32-bit | `0x000FFF00` | Subsystem filter coefficients. |
| `0x0F8` - `0x104` | `GATE_PACKED_B` | 32-bit | `0xFFFFFFF0` | Channel parameter arrays. |
| `0x10C` | `GATE_PACKED_C` | 32-bit | `0xFF0FFF00` | Overlapping configuration word. |
| `0x120` - `0x12C` | `GATE_PACKED_D` | 32-bit | `0xFFFFFFF0` | Channel parameter arrays. |
| `0x2000`, `0x2100` | `GATE_IN_APERTURE`| 32-bit | `0xFFFFFFF1` | Input selector ports (bits [3:0] support mode values `0` and `1`). |

### Protocol Rules
- **State Preservation**: Writing to any packed register preserves unrelated fields outside the legal mask.
- **Access Restrictions**: Offsets outside the specified aperture or non-32-bit accesses trigger `MEMTX_ERROR`.
- **Reset**: Device reset initializes all configuration and packed parameter registers to `0x00000000`.
