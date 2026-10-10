# Repository guide

| Path | Purpose |
|---|---|
| [README.md](README.md) | Public scope, build, launch, workflow, architecture, and acceptance levels |
| [CONTRIBUTING.md](CONTRIBUTING.md) | Contribution and review process |
| [hw/nikon/AGENTS.md](hw/nikon/AGENTS.md) | Machine and device implementation rules |
| [docs/AGENTS.md](docs/AGENTS.md) | Evidence, protocol, and model-boundary rules |
| [docs/layout.md](docs/layout.md) | Machine, device, peer, and public-interface layers |
| [docs/build.md](docs/build.md) | Upstream dependency and native source-graph build details |
| [docs/startup/](docs/startup/) | Loader, reset, memory, and original-firmware observations |
| [docs/devices/](docs/devices/) | Device contracts, IRQ behavior, timing, and unsupported accesses |
| [docs/inputs/](docs/inputs/) | Explicit external-input and erased-state profiles |
| [docs/runtime/](docs/runtime/) | Guest runtime, allocation, wait, and transport boundaries |
| [docs/validation/](docs/validation/) | Focused validation scope and native receipts |
| [tests/qtest/nikon/](tests/qtest/nikon/) | QEMU integration fixtures |
| [tests/unit/nikon/](tests/unit/nikon/) | Device and protocol unit fixtures |

Keep the official `qemu/` submodule clean. Put generated builds, extracted firmware, logs, sockets, and debugger notes in ignored `work/`.
