# Nightglass QEMU

Nikon firmware emulation and debugging built on upstream QEMU. This private repository maintains Nikon machine/device additions and the small integration needed to build them against a pinned upstream release. It does not implement a CPU emulator or a second simulation framework.

The initial target is Nikon Z7II C1.70. Existing camera emulation projects inform device models and tracing. A user-supplied post shows a claimed Nikon Z9 QEMU run reaching a camera error screen; implementation and launch details are unavailable, so that case is a useful lead rather than a reproducible baseline.

Upstream baseline: QEMU `v11.1.2`, commit `4fc49f46dc95d4a27de2509e7fceb2931e91faeb`. Build dependencies and original firmware remain in ignored `work/`. Firmware recovery tools remain in [Nightglass](https://github.com/mashiro01/nightglass), and maintained firmware source remains in [Nightglass Firmware](https://github.com/mashiro01/nightglass-firmware).

The first runnable milestone is loading unchanged original MAS instructions into an explicitly documented diagnostic machine and observing real execution or the first missing hardware contract. CPU/reset/load assumptions must remain visible. Subsequent work recovers the actual loader, mappings, interrupts, timer and interprocessor protocol until the original firmware reaches its normal service state. Rebuilt firmware is then compared in the same model.

The update package contains five components, not a complete board flash/BootROM dump. Model coverage, successful original boot, rebuilt behavior and real-camera acceptance are separate results. Unknown device behavior is recorded as a stop point; original code and the `O0x178` self-loop are preserved.

Issues are managed through [the Nightglass delivery map](https://github.com/mashiro01/nightglass/issues/67). No firmware bytes, generated binaries or private credentials are committed. CI will build the pinned QEMU integration and synthetic tests; it will not download camera firmware or call models.

## MAS diagnostic machine

The implementation for [运行 Z7II 原版 QEMU machine 并恢复启动设备依赖](https://github.com/mashiro01/nightglass/issues/326) is `hw/arm/z7ii.c`. It uses the pinned upstream ARM CPU, MemoryRegion, ROM loader, reset, TCG, exception handling and GDB stub. The current [MEMC/A7 diagnostic layer](docs/memc-a7-diagnostic.md) adds bounded MEMC profiles, a 48-byte temporary stack, and upstream GICv2/generic timer under explicit board hypotheses. IPC, boot handshake and display remain uncovered. Linux is not required: use the upstream native macOS build dependencies and ARM system TCG.

With a clean checkout of the pinned upstream revision and an empty build directory:

```sh
scripts/build-qemu.sh /absolute/path/to/qemu-11.1.2 /absolute/path/to/work/build/mas-diagnostic
```

The script modifies only upstream `hw/arm/Kconfig`, `hw/arm/meson.build` and the new `hw/arm/z7ii.c`, then verifies that `-M help` exposes the machine. The QOM type uses the upstream `arm_machine_interfaces` target registration, as existing ARM machines do. Later source edits can be copied into that checkout and rebuilt with `ninja -C /absolute/path/to/work/build/mas-diagnostic qemu-system-arm`; do not rerun initial integration on a dirty checkout. This repository does not fetch firmware or install dependencies.

Run the extracted **MAS component payload**, not the complete Nikon update file, with an unused output directory. The locally confirmed C1.70 input is `02_eg1840_mas_01700.payload.bin`, 38,239,096 bytes (`0x2477b78`); the machine also accepts smaller synthetic diagnostic images. The nested `mas-inner` at parent offsets `[0x208853c,0x228cf3c)` belongs to a separate candidate `0xb1000000` mapping and must not be substituted at this entry.

```sh
# Run from this repository; choose another short unused name for later runs.
mkdir -p work
mkdir work/r1
/absolute/path/to/work/build/mas-diagnostic/qemu-system-arm \
  -M z7ii-mas-diagnostic -accel tcg,thread=single \
  -bios /absolute/path/to/02_eg1840_mas_01700.payload.bin \
  -display none -serial none -monitor none -S \
  -gdb unix:work/r1/gdb.sock,server=on,wait=off \
  -qmp unix:work/r1/qmp.sock,server=on,wait=off \
  -D work/r1/qemu.log -d guest_errors,int
```

Use an ARM-capable GDB to connect to the socket, inspect reset registers and use `hbreak *0x10020178` before continuing. A hardware breakpoint observes the original self-loop without changing its bytes. The original C1.70 entry word is `20 2f 00 ea`; `O0x178` remains `fe ff ff ea`. QMP `query-status` and `query-cpus-fast`, GDB registers and the upstream `-d int` log distinguish a stopped access, an architectural exception, a wait and a running loop. `-d in_asm,cpu` may be added for a short controlled trace; it produces substantially more output.

For a bounded original run with preserved receipts, use the existing upstream Python library through the thin entry below. It uses a short temporary QMP socket, records the upstream launch command, reset/final registers and first-stop log, and directly compares payload bytes before and after. Choose a new output directory each time. `--seconds` bounds host observation, not modeled boot time; a timeout or process exit is not boot completion.

```sh
PYTHONPATH=/absolute/path/to/qemu-11.1.2/python \
  /absolute/path/to/work/build/mas-diagnostic/pyvenv/bin/python \
  scripts/run-original-mas.py \
  /absolute/path/to/work/build/mas-diagnostic/qemu-system-arm \
  /absolute/path/to/02_eg1840_mas_01700.payload.bin \
  work/original-r6 --machine-revision <actual-machine-Git-ref>
```

Keep UNIX socket names short: [macOS bounds `sockaddr_un.sun_path` at 104 bytes](https://github.com/apple-oss-distributions/xnu/blob/main/bsd/sys/un.h#L74-L78), so absolute worktree paths containing full Git revisions can exceed it. The relative socket paths above avoid that problem. [The first original MAS run](docs/original-mas-first-run.md) stopped at the uncovered write to `0x04264000`. [The MEMC-layer run](docs/original-mas-memc-run.md) accepts both initialization stores and passes the early stack PUSH. [The current two-core run](docs/original-mas-smp-run.md) records CPU1 at the WFE continuation under MTTCG, while CPU0 still fails its read from `0x0428102c` at PC `0x1002d70c`. Native build, 42 single-core device contracts and a genuine two-core synthetic rendezvous passed. The [clock-stop bank layer](docs/clock-gates-diagnostic.md) supports shared SET/CLEAR mask semantics with unknown reset bits; its original run preserves the same PLL read failure. Whole-camera boot and original RTOS/timer service remain unverified. Use `--tcg-thread multi` with the bounded runner for a separate MTTCG observation; default single-thread scheduling may stop CPU0 before CPU1 runs.

| Initial condition | Evidence or explicit diagnostic assumption |
|---|---|
| Two Cortex-A7 cores by default; CBAR `0x1d000000`, CNTFRQ 48 MHz | Original configuration producers set `TSysPrNum=2/TKerPrNum=2`; this supports the diagnostic software model, not a physical CPU identification. CPU type and CBAR remain hypotheses. Upstream GICv2 and generic timer supply genuine per-core IRQ state, including secure PPI 29; no guessed ready bits. Small synthetic tests explicitly select `-smp 1`. |
| Reset PC `0x10020000`, ARM state, raw ROMD image of exactly the input length | MAS entry relative branch, VBAR/absolute literals support the candidate program mapping. Placing it in QEMU physical space is an assumption; neither BootROM nor the real loader is present. PC is set in the reset callback. Guest writes enforce MEMC WEOFF and explicitly fail unsupported NOR operations. |
| MEMC `0x04264000`, MODE0/TIM0 only; temporary stack `[0x04008ed0,0x04008f00)` | Same-IP binding hypothesis with source-defined x16/PAGE/WEOFF semantics; only the current MODE `0x21/0x29` and TIM `0x153b0902` profiles are supported. Register reads require guest initialization. Temporary RAM is exactly the evidenced 48-byte consumption, initialized to zero for this diagnostic run. |
| Shared release RAM `[0x04008ffc,0x04009000)`; CPU1 SVC prefix stack `[0xb2b83fb8,0xb2b84000)` | Original CPU0 produces magic with STR/DSB/SEV; secondary WFE/read/clear stays guest code. RAM starts cold at zero and the host never writes magic. The CPU1 prefix consumes exactly a 72-byte lower bound; further CPU1 stack extent remains uncovered. A selector stride of 16 KiB does not authorize 16 KiB of new per-core RAM. |
| Six clock-stop views at `0x04281038..0x0428104c`, SET/CLEAR aliases at +`0x100`/+`0x200` | Original six-pair table and helpers plus preserved same-family source support the diagnostic mask semantics. Reset supplies no known bits; guest masks determine only selected bits and reads of any unknown word fail. PLL, actual gate outputs and the external counter remain unsupported. |
| Completion request/done/enable at `0x04287000`, command at +`0x10`, SPI250 | The [completion layer](docs/completion-diagnostic.md) models observed request profiles, deferred result production, guest-written IRQ enable and W1C acknowledgement. Reset fields and complete domain status remain unknown; no implicit initial IRQ-disable value, host RTOS flag or physical timing is supplied. Original execution has not reached this device. |
| Zero-initialized RAM `0xb1800000 + 0xaedb8`, `0xb2bb0000 + 0x105c` | MAS literals at `O0xc018..0xc030` describe two copy destinations and lengths. The unchanged guest must perform both copies; host code does not prefill them. No third zero-length copy creates RAM. |
| Zero-initialized RAM `0xb2b74000 + 0x4000` | Guest code at `O0x270..0x290` copies the translation table and then writes TTBR0. The machine does not enable the MMU or manufacture translations. |
| Six CPU0 banked stack slots, each `0x4000` bytes below its top | Tops from literals `[O0xbfd8,O0xbff0)`: `b2bb0000/b2ba8000/b2ba0000/b2b98000/b2b90000/b2b88000`. Slot extent is a bounded hypothesis suggested by selector displacement `selector << 14`, not a proven physical RAM capacity. Gaps remain uncovered. |
| All other addresses fail | A low-priority trap returns `MEMTX_ERROR`; the first guest access records CPU, PC, address, direction, width and write value, then requests QEMU's debug stop. The invalid read output is not a successful zero response. Debugger probes do not count as guest accesses. |

Unknown transactions can trigger an upstream architectural abort before the asynchronous VM stop is processed; inspect both the recorded offending PC and post-stop registers. Instruction fetches outside executable memory and CPU exceptions remain governed by upstream TCG/ARM and may appear in the exception log rather than the MMIO callback. The machine does not promise an automatic stop on every CPU exception. A GDB breakpoint on the vector entry is available when diagnosing one.

The static evidence is documented in [MAS mapping](https://github.com/mashiro01/nightglass/blob/main/research/z7ii/mas-mapping.md), [hardware configuration](https://github.com/mashiro01/nightglass/blob/main/research/z7ii/mas-hardware-configuration.md), [translation source](https://github.com/mashiro01/nightglass/blob/main/research/z7ii/mas-translation-source.md) and [nested images](https://github.com/mashiro01/nightglass/blob/main/research/z7ii/subimages.md). Upstream API reference: [ARM PC semantics](https://github.com/qemu/qemu/blob/4fc49f46dc95d4a27de2509e7fceb2931e91faeb/target/arm/cpu.c#L59-L70), [loader](https://github.com/qemu/qemu/blob/4fc49f46dc95d4a27de2509e7fceb2931e91faeb/include/hw/core/loader.h#L55-L72), [memory transaction API](https://github.com/qemu/qemu/blob/4fc49f46dc95d4a27de2509e7fceb2931e91faeb/include/system/memory.h#L292-L351), [VM stop request](https://github.com/qemu/qemu/blob/4fc49f46dc95d4a27de2509e7fceb2931e91faeb/include/system/runstate.h#L138-L141).

This is a diagnostic machine for unchanged instruction execution under stated assumptions. A build or a stop at `O0x178` does not establish whole-camera boot. Successful execution can only reduce software risks covered by this model; it does not prove flash, BootROM, calibration or physical hardware health, or measure EXPEED performance. Actual build/run commands, exit codes, stop registers and missing hardware contracts belong in ignored per-run receipts before any stronger claim.
