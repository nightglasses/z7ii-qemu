# Nikon machine and functional device structure

Nikon implementations are separate from the official QEMU submodule. Maintained filenames avoid hyphens, preferring a single clear word; renaming does not modify established QOM types or function prefixes. The first camera machine is the Z7II; existing `z7ii-mas-diagnostic` startup entry points, CPU assumptions, firmware placement, access gates, and IRQ connections remain preserved. Generic devices use Nikon QOM types; legacy device paths and deprecated QOM names have been removed.

`hw/nikon/machines/z7ii.c` handles the Z7II CPU, memory layout, firmware loading, and device wiring. `hw/nikon/devices/` implements functional protocols; MEMC, clock control, and request-completion state machines are also decoupled from the machine. Public types and essential interfaces reside in `include/hw/nikon/devices.h`. Absolute MMIO layouts are defined exclusively in the machine, while generic devices obtain fault addresses from the actively mapped MemoryRegion. QEMU's QOM, SysBus, MemoryRegion, Clock, IRQ, timer, and BH primitives serve as the sole execution framework.

Address numbers do not designate physical IP identities. The following names reflect currently implemented software protocols; distinct registers and event semantics remain independent without merging state machines for uniform naming.

| Original Module | Current Source File | Protocol Scope |
|---|---|---|
| control2010 | `gate.c` | Configuration, event masks, external gates, and deferred stop acknowledgment |
| control2014 | `banks.c` | Two-object configuration, two pending banks, and tiered IRQs |
| control2016 | `dispatcher.c` | Byte registers, sub-pending state, and top-level IRQ routing |
| control20201 | `sampler.c` | One group of external data sampling and independent events |
| control20202 | `staging.c` | Staged configuration, request, and event state |
| control20203 | `parameters.c` | Parameter configuration and independent external events |
| control20204 | `table.c` | Configuration tables and three groups of external data sampling |
| control20205 | `shared.c` | Shared physical parameter words and external events |
| control2038 | `pairs.c` | Dual-bank configuration, masks, and pending status |
| control2406 | `status.c` | Finite configuration and explicit unavailable request states |

These identifiers do not substantiate Nikon SoC model designations, physical reset defaults, or complete hardware capabilities. Original addresses, observations, hypotheses, failure notes, and historical execution identities across existing protocol documents are strictly preserved. Refactored type names and log prefixes do not promote historical receipts into new build verifications.

Pinned QEMU `v11.1.2` resides in the official `qemu/` Git submodule; upstream licenses, Git history, and nested dependencies are maintained. Nikon source files belong to `hw/nikon`, `include/hw/nikon`, and `tests/qtest/nikon` in this repository. CMake generates an ignored native build source graph along with three Kconfig/Meson entry points, keeping the upstream working tree clean. The root `Makefile` subsequently invokes official configure and Make/Meson/Ninja; `make check` and `make bench` execute the Nikon native C qtest pipeline. See [build.md](build.md) for detailed dependencies and ownership. There is no fork, patch, code duplication, or project-specific Python scheduler. Moving dependency structures does not claim completed original boot.

The event router module originates from pinned commit `a955007714bead0742fc6ee1f1be2602d75faf14`, currently named `router.c`. Complete source code and 13 original synthetic fixture receipts from that pinned milestone are retained as historical evidence, with its three SysBus IRQs wired to GIC86/87/88 on the Z7II machine. Physical native builds remain owned by the integration phase; no additional constructor or machine compatibility shims exist.
