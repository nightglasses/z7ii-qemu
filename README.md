# Nightglass QEMU

Nikon firmware emulation and debugging built on upstream QEMU. This private repository maintains Nikon machine/device additions and the small integration needed to build them against a pinned upstream release. It does not implement a CPU emulator or a second simulation framework.

The initial target is Nikon Z7II C1.70. Existing camera emulation projects inform device models and tracing. A user-supplied post shows a claimed Nikon Z9 QEMU run reaching a camera error screen; implementation and launch details are unavailable, so that case is a useful lead rather than a reproducible baseline.

Upstream baseline: QEMU `v11.1.2`, commit `4fc49f46dc95d4a27de2509e7fceb2931e91faeb`. Build dependencies and original firmware remain in ignored `work/`. Firmware recovery tools remain in [Nightglass](https://github.com/mashiro01/nightglass), and maintained firmware source remains in [Nightglass Firmware](https://github.com/mashiro01/nightglass-firmware).

The first runnable milestone is loading unchanged original MAS instructions into an explicitly documented diagnostic machine and observing real execution or the first missing hardware contract. CPU/reset/load assumptions must remain visible. Subsequent work recovers the actual loader, mappings, interrupts, timer and interprocessor protocol until the original firmware reaches its normal service state. Rebuilt firmware is then compared in the same model.

The update package contains five components, not a complete board flash/BootROM dump. Model coverage, successful original boot, rebuilt behavior and real-camera acceptance are separate results. Unknown device behavior is recorded as a stop point; original code and the `O0x178` self-loop are preserved.

Issues are managed through [the Nightglass delivery map](https://github.com/mashiro01/nightglass/issues/67). No firmware bytes, generated binaries or private credentials are committed. CI will build the pinned QEMU integration and synthetic tests; it will not download camera firmware or call models.

