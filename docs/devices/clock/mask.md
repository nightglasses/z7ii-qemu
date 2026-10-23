# Word-Mask Configuration Register

The word-mask register at offset `0x034` (`0x04281034`) provides whole-word configuration masking for peripheral sub-blocks.

```mermaid
flowchart TD
    In["Caller Mask (R1) & Flag (R2)"] --> CheckMask{"Mask == 0xFFFFFFFF?"}
    CheckMask -->|"Yes"| EarlyRet["Return 0 (No MMIO Operation)"]
    CheckMask -->|"No"| Lock["Acquire OS Mutex B1CF1130"]
    Lock --> Read["Read current 32-bit word from 0x04281034"]
    Read --> CheckFlag{"Flag Non-Zero?"}
    CheckFlag -->|"Yes (Clear)"| BIC["new = old & ~mask"]
    CheckFlag -->|"No (Set)"| OR["new = old | mask"]
    BIC --> Write["Write new 32-bit word to 0x04281034"]
    OR --> Write
    Write --> Unlock["Release OS Mutex & Return"]
```

## Hardware Specification

| Offset | Physical Address | Register Name | Width | Access | Default |
|---|---|---|---|---|---|
| `0x034` | `0x04281034` | `CLK_WORD_MASK` | 32-bit | R/W | `0x00000000` |

### Protocol Rules
- **Access Constraint**: Aligned 32-bit transactions only. Unaligned or sub-word operations return `MEMTX_ERROR`.
- **Atomic Modification**: Firmware encapsulates read-modify-write sequences within an OS mutex (`0xb1cf1130`). When flag is non-zero, the mask is cleared via bit-clear (`BIC`); when flag is zero, the mask is asserted via bitwise OR.
- **Independence**: `CLK_WORD_MASK` is independent of the stop banks (`+0x038..+0x04c`) and bit-control registers (`+0x058..+0x05c`). SET/CLEAR aliases do not affect this register.
- **Reset**: System reset clears the configuration word to `0x00000000`.
