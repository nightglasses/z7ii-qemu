# Machine and device implementation rules

- Follow the pinned upstream QEMU C style and QOM/SysBusDevice APIs; reuse existing devices/state machines.
- Nikon code belongs in this repository's hw/nikon/. machines/ owns model-specific address layouts, CPUs, RAM and IRQ wiring; devices/ owns common functional protocols. Do not place project device changes in the official qemu submodule.
- Keep only the pinned official dependency in qemu/. Project devices, headers, tests and build entries stay outside it. Native integration uses the ignored generated source graph; do not restore old dependency paths, compatibility entry points or tracked generated files.
- Maintained filenames avoid hyphens; prefer one clear word. Update callers when renaming and remove obsolete filename compatibility paths.
- Common devices/headers use Nikon/nikon names. Device names describe established function, not body names, MMIO address numbers or inferred physical SoC/IP identity.
- Machines map absolute device addresses; diagnostics use actual mapped addresses. Public boundaries live in include/hw/nikon/devices.h.
- Explicitly identify evidence/assumptions for CPU model, reset, address space, image placement, entry, modes and core wiring. Do not present VA as verified PA.
- State endian, width, reset and side effects for known MMIO. Unknown accesses record CPU/PC/address/direction/width and remain blocking.
- Do not bypass startup dependencies through broad read-zero/write-discard apertures or automatic ready/IRQ/handshakes.
- Simplify image operations and other peripherals according to actual software protocols; document the scope/effect. Complete physical detail is not required initially.
- Do not commit factory code arrays, firmware or build products, or create another CPU/ISA decoder.
