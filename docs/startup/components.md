# Five-component input and execution domains

Issue [395](https://github.com/mashiro01/nightglass/issues/395). This document defines the five firmware component records in the decoded container and maps each to its corresponding execution domain and hardware architecture.

## Component breakdown and target domains

| Component | Target Architecture / Machine | Load Base / Entry Point | Role in System |
|---|---|---|---|
| `MAS` | Cortex-A7 Dual-Core SMP (`z7ii-mas-diagnostic`) | VA `0x10020000` / `0x10020000` | Main application system, UI rendering, device coordination |
| `SLA` | Cortex-A7 Dual-Core (`nikonsla`) | VA `0x10020000` / `0x10020000` | Sub-system coordination, optical path, and sensor capture |
| `EX` | Cortex-M4 (`nikonex`) | Base `0x08000000` / Vector `0x08000004` | Power management, battery authentication, lens drive, GPIO inputs |
| `VR` | Renesas RX62N (`nikonvr`) | Flash `0xfff80000` / `0xfffffffc` | Vibration reduction and gyro sensor processing |
| `TPJ` | Andes NDS32 (`tpj`) | Base `0x00000000` / Vector `0x00000000` | Touch panel controller and panel digitizer interface |

## Integration rules

1. **Unchanged Payloads**: Byte streams from the decoded bundle are fed directly into the respective emulator domains without tampering or byte patches.
2. **Explicit Interfaces**: Communication between domains (e.g. MAS-SLA bridge, EX battery UART peer, SPI transports) must be mediated by bounded hardware models (SysBus, CharBackend, IRQs) rather than shared memory shortcuts.
3. **Refusal on Ambiguity**: Bundles missing any mandatory record or failing header size checks are rejected prior to machine realization.
