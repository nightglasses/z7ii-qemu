---
layout: home

hero:
  name: "Nikon Z7II Emulation"
  text: "Diagnostic Firmware Handbook"
  tagline: "Software models, verified device contracts, and execution evidence for unchanged Nikon Z7II (C1.70) firmware on QEMU."
  actions:
    - theme: brand
      text: "System Architecture"
      link: "/architecture"
    - theme: alt
      text: "Peripheral Contracts"
      link: "/devices/gate/protocol"
    - theme: alt
      text: "Address Reference Map"
      link: "/refmap/README"

features:
  - icon: 🎯
    title: "Zero Firmware Modifications"
    details: "Executes original byte sequences without patches or guest outcome injection. Unknown MMIO addresses, widths, and commands strictly return MEMTX_ERROR."
  - icon: 🧩
    title: "Multi-Domain System Graph"
    details: "Integrates Cortex-A7 SMP (MAS, SLA), Cortex-M4 (EX), Renesas RX62N (VR), and Andes NDS32 (TPJ) under native QEMU SysBus and QOM."
  - icon: 🔬
    title: "Rigorous Evidence Discipline"
    details: "Every register field, interrupt line, and execution milestone is explicitly partitioned into observed, verified, inferred, or hypothesis."
  - icon: 🗺️
    title: "Traceable Address Reference Map"
    details: "Unified 1:1 address index connecting firmware virtual addresses, physical MMIO registers, GIC interrupts, and runtime execution receipts."
---

## Handbook Navigation

This technical handbook documents the complete reverse-engineering and emulation effort for the Nikon Z7II digital camera system running unchanged C1.70 firmware components on QEMU.

### Core Sections

1. **[Architecture & Design Principles](/architecture)**
   - System CPU topologies, memory maps, inter-core communication, build infrastructure, and strict evidence criteria.
2. **[Startup Sequence](/startup/first)**
   - Firmware loader contracts, reset vectors, dual-core SMP parking, and coprocessor execution domains (`MAS`, `SLA`, `EX`, `VR`, `TPJ`).
3. **[Peripheral Contracts](/devices/gate/protocol)**
   - Over 45 source-backed peripheral state machines: memory controllers, DMA engines, interrupt dispatchers, serial transports (I2C, SPI, USIO), and CMOS sensor readout.
4. **[Runtime Subsystems](/runtime/policy)**
   - OS kernel services, memory allocation, battery authentication and UART peer protocol, graphics composition pipeline, and display readiness gates.
5. **[Address Reference Map](/refmap/README)**
   - Master address database (`REFS.tsv`), end-to-end execution chain graphs (boot, provider, display, input, readout), and active research frontiers.
6. **[Verification & Test Matrix](/validation/budgets)**
   - Native C qtests, timing budgets, synthetic fixture contracts, and boundary receipts.

---

### Quick Launch

To run the composed Z7II virtual machine with a validated five-component container:

```sh
work/build/qemu/qemu-system-arm \
  -M z7ii-mas-diagnostic,bundle=/absolute/path/to/decoded-container.bin \
  -smp cpus=2,maxcpus=5
```
