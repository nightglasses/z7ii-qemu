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

## Developer rules

- Follow [CONTRIBUTING.md](CONTRIBUTING.md) and the applicable nested `AGENTS.md`; keep detailed device/evidence rules there.
- Keep original firmware bytes unchanged. Report original execution, experimental/hypothesis execution, synthetic fixtures, reconstruction comparisons, complete boot and physical-camera evidence separately. A fixture PASS or console image does not prove original startup.
- Do not fabricate ready/status, events, replies, IRQs or frames to advance a guest. Unknown addresses, widths, bits and commands stay explicit errors; no broad read-zero/discard apertures or forced guest returns.
- Reuse QEMU CPU/TCG, QOM, SysBus, MemoryRegion, DMA, IRQ, clocks, timers, loader, GDB and trace APIs. Add only the smallest finite source-backed board/device contract, not a second execution framework or generic scheduler.
- Add scoped native qtest/unit red→green evidence for changed behavior, including reset, cancellation and negative boundaries. Run one applicable batch; do not repeat full validation without new changes or unresolved failures. Keep fixture success separate from unchanged-firmware progress.
- The parent agent owns scheduling, builds, QEMU/GDB runs, integration, independent review, CI/PR and history-preserving merge. Children handle isolated research/development only; no nested delegation. All agent model selections must explicitly use ONLY GPT-family models.
- Use one writer per isolated branch/worktree and respect assigned file ownership. A private WIP seed is context, not an accepted change set; commit only the assigned delta. Do not touch other worktrees or undo collaborators' changes.
- Use normal Git refs and small cohesive commits. No unrelated history edits, seed amendments or scratch/other-owner staging; no child push, external publication, dependency installation or firmware modification. Verify content directly; do not add research-pipeline self-designed hash gates.
- Keep the official `qemu/` submodule tracked content clean; Nikon sources belong to the parent repository. Put generated builds, extracted firmware, logs, sockets and debugger notes in ignored `work/`, never in upstream or commits.
- Assistant shell commands use `rtk` where applicable; product code invokes explicit tools directly. Build ownership, dependencies and native test selection are documented in [docs/build.md](docs/build.md) and [README.md](README.md#dependencies-and-build). Do not invent a parallel build/test workflow.
