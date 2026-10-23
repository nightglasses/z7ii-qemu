# Multi-Domain Completion Architecture

The completion controller supports multi-domain operation across both the primary MAS system domain and the private SLA domain.

## Encoded Domain Selectors

The 3-bit selector in bits [14:12] of `COMP_REQ` designates the target subsystem domain:

| Domain Index | Subsystem Role | Submission Path | Hardware Action |
|---|---|---|---|
| `0` | Base Peripheral Engine | MAS / SLA early init | Updates domain 0 applied state |
| `1` | Image Processing Engine | MAS runtime | Updates domain 1 applied state |
| `2` | Sensor Interface Bus | SLA early init | Updates domain 2 applied state |
| `3` | Display & Scanout Controller | MAS runtime | Updates domain 3 applied state |
| `4` | Software Bypass (MAS) / Hardware (SLA) | Conditional | SLA submits `0xA1`; MAS bypasses |
| `5..7` | Extended Acceleration Domains | MAS runtime | Updates corresponding domain state |

## SLA Private Controller Mapping

The SLA machine maps a private instance of `TYPE_NIKON_COMPLETION` at physical address `0x04287000`:
- **Private Address Space**: The SLA instance resides inside the SLA memory space (`sla-memory`), completely isolated from the MAS system bus.
- **Dedicated Interrupt**: Connects to the SLA GIC-400 distributor at SPI 218 (`250 - 32`), ensuring that SLA startup sequences execute without triggering MAS CPU interrupts.
