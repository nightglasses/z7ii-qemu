# SLA Domain Event Gate Mapping

The SLA execution domain maps an instance of the Control2010 Event Gate controller at physical aperture `0x20100000` to manage coprocessor event inflow and sensor pipeline interrupts.

## Memory Mapping and IRQ Routing

- **Base Aperture**: `0x20100000 - 0x20102928` (little-endian aligned 32-bit registers).
- **Interrupt Vector**: Connected directly to the SLA GIC-400 distributor at SPI 102 (IRQ 134).
- **Isolation**: Operates inside the SLA private address space (`sla-memory`), independent of the MAS instance.

## SLA Method Reference Map

| Method Address | Operation | Hardware Effect |
|---|---|---|
| `0x100943CE` | Service Registration | Registers service 146 object at `0xC10012C4`, registers Thumb ISR for IRQ 102 at `0x10094339`. |
| `0x1009453A` | Gate Inflow Open | RMW on `0x20100004`: clears low 2 bits and sets bit 0 (opens external event inflow). |
| `0x1009458A` | Input Aperture Configuration | RMW on `0x20102000` / `0x20102100`: configures input selection modes. |
| `0x100949B2` | Gate Inflow Close | Writes `0` to `0x20100004` (initiates 20 µs drain, latches internal stop confirmation). |
| `0x100949A2` | Mask Configuration | Configures the 3 banked domain masks at `0x0C`, `0x10`, and `0x14`. |
| `0x10094C7A` | Opaque Word Configuration | RMW operations on `0x030` and `0x034`. |
| `0x10094CD2` | Packed Parameter Setup | Writes packed filter coefficients and channel parameters (`0xF4` - `0x12C`). |
| `0x10094F18` | Descriptor Mutation | Emits 13-word configuration blocks for sensor staging. |
