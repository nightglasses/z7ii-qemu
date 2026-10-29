# Sensor static readout and geometry bounds

This document covers sensor-specific static readout geometry and timing bounds
for the Z7II CMOS sensor model. See the [full-size static readout contract](../readout/static.md)
for the complete multi-unit capture pipeline.

## Sensor dimensions and timing

- **FX full sensor format**: 8320 horizontal samples, 5504 active rows (plus up
  to 32 margin rows for optical black lines).
- **Stride and span**: Row stride is 16,640 bytes (256-byte aligned). Active span
  is 91,586,560 bytes (87.34 MiB); full extended span is 92,119,040 bytes
  (87.85 MiB).
- **Pixel depth and CFA**: 16-bit LE Bayer CFA samples.
- **Acquisition timing**: Deterministic 1 ns exposure plus 1 ns per at-most-4096-byte
  chunk without crossing a row boundary.

## Bounds enforcement

- The CMOS buffer capacity `CMOS_MAX_BUFFER_BYTES` and `sensor.c` span check are
  bounded to 144 MiB (`144U * 1024 * 1024`), covering capture pool kind 16
  (`0x54900000`, 136.2 MiB) with strict writable RAM verification.
- Internal stream acquisition dimensions are bounded to width <= 8448, height <= 5632,
  and total pixels <= 48 MP (`48 * 1024 * 1024`).
