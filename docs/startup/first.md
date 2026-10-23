# Z7II C1.70 original MAS first diagnostic execution

2026-10-08; [Run original Z7II QEMU machine and recover boot device dependencies](https://github.com/mashiro01/nightglass/issues/326). **The original MAS firmware has executed natively; the first uncovered write was explicitly rejected and stopped. Full system startup, reaching `O0x178`, and comparison against reconstructed versions remain unverified.** This document records the diagnostic baseline and the next hardware contract without implementing devices.

Pinned machine commit is `4f9ccc6c9b5b66a02aace86a9a6c0dd77953db0a`, upstream QEMU is `4fc49f46dc95d4a27de2509e7fceb2931e91faeb` (`v11.1.2-dirty`, including local machine integration). Native execution ran on macOS ARM; raw receipts and logs are in ignored `work/issue326/r4-4f9/{receipt.json,qemu.log}`.

Input is Nightglass ignored `work/z7ii-1.70/02_eg1840_mas_01700.payload.bin`, 38,239,096 bytes; receipts confirm identical contents before and after execution. It is an unpacked MAS component payload, not a full update package or nested `mas-inner` at `O[0x208853c,0x228cf3c)`.

Invocation arguments: `-M z7ii-mas-diagnostic -accel tcg,thread=single -bios <payload> -display none -serial none -monitor none -S -qmp unix:<short_path>/qmp.sock,server=on,wait=off -D <run>/qemu.log -d in_asm,int,guest_errors`. The control client resumes execution via QMP, reads stop status and registers, and terminates QEMU with exit code 0.

| Direct Observation | Result |
|---|---|
| Initial state | `prelaunch`; PC `0x10020000`, CPSR `0x400001d3`, all GPRs and SP zero; ARM/SVC/secure, default Cortex-A9 reset state |
| Original entry | Log records first instruction `20 2f 00 ea`, entering `0x1002bc88` to perform CP15 initialization, MPIDR selector, and six-mode SP setup |
| First rejected transaction | CPU0, PC `0x1002bda4`, diagnostic PA `0x04264000`, write, 4 bytes, value `0x21`; `MEMTX_ERROR` |
| Exception and halt | Upstream ARM takes Data Abort; QMP stop reason/status is `debug`, running=false; final PC `0x10103610`, CPSR `0x600001d7`, SP `0xb2ba8000`, LR `0x1002bdac` |
| Host session duration | Receipt `wall_seconds=0.11494487500749528` (includes orchestration and shutdown); does not reflect firmware boot time or EXPEED performance |

Initial reproduction assumptions remain documented: single-core Cortex-A9; candidate MAS program VA `0x10020000` mapped as diagnostic physical load address; upstream default CPU reset; two static copy destinations, a 16 KiB TTBR0 migration window, and six CPU0 stack slots (16 KiB each), mapped strictly according to [README](../../README.md). There is no BootROM, physical loader, IRQ, timer, IPC, or handshake device; RAM copy contents are not prefilled, ready magics are not set, and original self-loops are unpatched. Because the faulting transaction triggers an exception before asynchronous debug stop processing, the final PC differs from the fault-originating PC.

## Next contract at `0x04264000`

The following represents finite static analysis of original bytes, separate from the execution range reached above. Capstone was used to disassemble ARM/Thumb windows without running the guest, executing Ghidra, or altering inputs. `O` denotes MAS payload offset; RAM2 VA follows static projection `0xb2bb0000+(O-0x2428d80)`.

| Original Consumer | Verified Software Contract |
|---|---|
| ARM `O0xbd9c..0xbdac` | `O0xbff0=0x04264000` loaded as base; `O0xbda4` writes `base+0=0x21`; `O0xbff4=0x153b0902` loaded, `O0xbdac` writes value to `base+0x20`. Both are 32-bit STR; entry contains no readback or ready check. Latter unexecuted. |
| ARM `O0xbdb0..0xbdbc` | Saves original SP; loads temporary SP `0x04008f00` from `O0xbff8`; calls `BL O0x2b8→O0x914` (BX LR); then calls `O0xd700`. Temporary stack window unmapped; cannot be replaced by six-mode stacks. |
| RAM2 Thumb VA `0xb2bb0692..0xb2bb06aa` | Literal `O0x2429444` / RAM2 VA `0xb2bb06c4` points to `0x04264000`; reads `base+0`, clears bit 3, writes back, passes `0x432` to `0xb2bb06cc`, saves return value to `0xb1806f5c`. |
| RAM2 Thumb VA `0xb2bb06ac..0xb2bb06bc` | Reads `base+0` → sets bit 3 → writes back; reads saved value from `0xb1806f5c`, tail calls `0xb2bb06cc`. This is genuine RMW consumption; registers cannot be assumed zero. |
| RAM2 Thumb VA `0xb2bb06cc..0xb2bb0712` | Fetches 16 KiB table base from TTBR0; modifies lower 20 bits of 64 words from `+0x400`, preserves high bits, flushes caches with DSB/ISB, returns original low 20 bits. Represents page table attribute transition for VA `0x10000000..0x13ffffff`. |
| Adjacent RAM2 VA `0xb2bb0714`+ | 16-bit write `0x10000000=0xaa`, `0x10000002=0x55`, `0x10000000=0x90`, reading `0x10000000` and `0x10000004`. CFI query sequence for parallel NOR flash; base confirmed as `0x10000000`. |
| Uncovered entry `O0xd700` | ARM routine at `O0xd700`; 11 candidate basic blocks ending with BX LR. Calls helper `O0xd820` 4 times. |

## Model implementation criteria

1. **Aperture `0x04264000`**: Single-word read/write support at offset 0 and 0x20. Preserves guest writes; clears bit 3 on RMW. Other offsets and widths return `MEMTX_ERROR`.
2. **Temporary Stack**: Provision exact RAM at `[0x04008ea8, 0x04008f00)` (88 bytes) to satisfy temporary SP requirements.
3. **Evidence Discipline**: Do not fabricate CFI query replies or widen NOR mappings without explicit receipts.
