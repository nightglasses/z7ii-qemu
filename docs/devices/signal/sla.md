# SLA Domain Signal Control Specification

The SLA execution domain maps an identical instance of the Five-Route Signal Controller into its private physical address space, sharing the same hardware register geometry and ISR dispatch sequence.

## Memory Mapping in SLA Domain

| Aperture | Address Range | Size | Access | Description |
|---|---|---|---|---|
| MMIO 0 | `0x2B000000 - 0x2B00001B` | 28 bytes | Aligned 32-bit | Seven signal control and status registers |
| MMIO 1 | `0x28000000 - 0x28000003` | 4 bytes | Aligned 32-bit (WO) | Opaque configuration port (written with `0x0F`) |

## SLA Boot Initialization Sequence

The SLA initialization method (`0x1008989E` - `0x100898B4`) performs the following deterministic sequence:
1. **Opaque Config**: Writes `0x0000000F` to `0x28000000`.
2. **Gate Enable**: Writes `0x0000003C` to `0x2B000004` (enabling sampling gates for routes 0, 1, 2, 3, 4).
3. **Interrupt Registration**: Registers the common Thumb ISR at `0x100897E5` for SPI 237 and unmasks the interrupt in the SLA GIC distributor.

## Operational Contracts
- **Isolation**: The SLA controller instance maintains completely independent internal latches, debounce timers, and pending states from the MAS instance.
- **Interrupt Routing**: Routed directly to the private SLA GIC distributor SPI 237 without cross-domain leakage.
