# First diagnostic execution of original Z7II C1.70 MAS

Recorded startup scope: [unchanged-firmware startup](/startup/first).
Original MAS executed and stopped on its first unsupported write. Complete boot,
arrival at `O0x178` and reconstruction comparison were not established.
This page records the bounded diagnostic run and the next device contract.

Machine commit: `4f9ccc6c9b5b66a02aace86a9a6c0dd77953db0a`; upstream:
`4fc49f46dc95d4a27de2509e7fceb2931e91faeb`. The reported executable was
`11.1.2 / v11.1.2-dirty`, including local machine integration. The integration
owner built and ran it on macOS ARM; this investigation inspected that run
rather than executing another one.

The input was the extracted `02_eg1840_mas_01700.payload.bin`, 38,239,096
bytes. Direct comparison found its content unchanged. This is a MAS component
payload, not the whole update or parent `O[0x208853c,0x228cf3c)` inner image.
See [origin and mapping](https://github.com/mashiro01/nightglass/blob/main/research/z7ii/mas-mapping.md)
and [nested images](https://github.com/mashiro01/nightglass/blob/main/research/z7ii/subimages.md).

The invocation used `-M z7ii-mas-diagnostic -accel tcg,thread=single -bios <payload>
-display none -serial none -monitor none -S -qmp unix:<socket>,server=on,wait=off
-D <log> -d in_asm,int,guest_errors`. Official QMP continued execution,
read stop state/registers and quit QEMU; process exit was0.

| Direct observation | Result |
|---|---|
| Initial state | prelaunch; PC10020000, CPSR400001d3; all general registers/SP0; upstream Cortex-A9 ARM/SVC/secure reset |
| Entry | Original bytes20 2f 00 ea; reached1002bc88, CP15 initialization, MPIDR selection and six-mode SP setup |
| First rejected transaction | CPU0 PC1002bda4, diagnostic PA04264000, WRITE32=21, MEMTX_ERROR |
| Exception/stop | Upstream Data Abort; QMP debug, running=false; final PC10103610, CPSR600001d7, SPb2ba8000, LR1002bdac |
| Host session |0.11494487500749528 seconds including control/exit; not firmware boot time or EXPEED performance |

All numeric addresses/values in the table are hexadecimal. Model assumptions
were one Cortex-A9, candidate MAS VA10020000 used as the diagnostic physical
load address, upstream reset, two static-copy destinations, a16KiB TTBR0
relocation window and six16KiB CPU0 stacks, bounded by the
[load profile](cpuboard.md). There was no BootROM, real loader, IRQ, timer,
IPC or handshake device. RAM copies, ready words and original loops were not
patched. Data Abort preceded asynchronous debug stop, so final PC differs
from the failing instruction.

## Next contract at 0x04264000 {#memc-contract}

The following is static analysis of bounded ARM/Thumb windows using the existing
Capstone decoder, separate from executed coverage. `O` is the MAS payload
offset; RAM2 VA is projected as `b2bb0000+(O-2428d80)` and was not reached.
See [copy/configuration source](https://github.com/mashiro01/nightglass/blob/main/research/z7ii/mas-hardware-configuration.md).

| Original consumer | Established software operation |
|---|---|
| ARM Obd9c..Obdac | Literal Obff0=04264000; WRITE32 base+0=21 at Obda4; literal Obff4=153b0902; WRITE32 base+20 at Obdac. No readback/ready predicate; the second write was not executed in this run |
| ARM Obdb0..Obdbc | Save SP, load04008f00 from Obff8; call O2b8→O914 (BX LR), then Od700. This temporary stack was unmapped; mode stacks cannot replace it |
| RAM2 b2bb0692..b2bb06aa | Literal O2429444 / VAb2bb06c4=04264000; read base+0, clear bit3, write; pass432 to b2bb06cc and save return at b1806f5c |
| RAM2 b2bb06ac..b2bb06bc | RMW base+0 to set bit3, reload b1806f5c and tail-call b2bb06cc. Reachability/callers remain unresolved; constant-zero reads would not implement this |
| RAM2 b2bb06cc..b2bb0712 | Align TTBR0 to16KiB, update low20 bits of64 words from+400 while preserving upper bits, maintain caches and DSB/ISB, return previous low20 of first entry. Under short descriptors this changes attributes for VA10000000..13ffffff, not proof of64MiB flash |
| From RAM2 b2bb0714 | WRITE16 10000aaa=aa,10000554=55,10000aaa=80. Compatible with AMD/CFI NOR commands; no exact controller, device or bit3 identity follows |

Direct literal search found only Obff0 and O2429444, not a complete access
inventory: computed addresses, MOVW/MOVT, indirect calls and other domains
can add consumers. Value21 sets bits0/5. Fields of153b0902, reset/read values,
side effects, IRQ, timing and physical binding remain unknown.

Od700..Od764 uses a different base, Od768=04281000: replace+2c bits16..24
with163, clear bit0 and+28 bit10; delay40,000 iterations; clear+2c bit1,
+28 bit11 and+2c bits4..6, delay8,000 and return. Od800 is a software delay,
not ready polling. The separate66-word configuration writes target
04280030/34/38 and five columns at04282000. Socionext pinctrl similarity
does not identify the first blocked device. See
[configuration analysis](https://github.com/mashiro01/nightglass/blob/main/research/z7ii/mas-hardware-configuration.md)
and the [vendor pinctrl submission](https://lkml.iu.edu/1811.2/01402.html).

Pinned QEMU [pflash_cfi02.c](https://github.com/qemu/qemu/blob/4fc49f46dc95d4a27de2509e7fceb2931e91faeb/hw/block/pflash_cfi02.c)
provides NOR unlock/program/erase/CFI machinery, reusable only after geometry,
width, banks, permissions and missing ranges are qualified. It cannot replace
bus-control registers. Pinned
[arm_sysctl.c](https://github.com/qemu/qemu/blob/4fc49f46dc95d4a27de2509e7fceb2931e91faeb/hw/misc/arm_sysctl.c)
and the [vendor M10V DTS](https://github.com/torvalds/linux/blob/ffc253263a1375a65fa6c9f62a893e9767fbebfa/arch/arm/boot/dts/socionext/milbeaut-m10v.dtsi)
do not match this binding. RTOS API matching does not recover SoC registers.

This historical boundary required RAM2 callers/restoration, NOR consumers,
the04008f00 stack extent and independent04281000 initialization to be
recovered. It authorized no generic regfile, forced ready or borrowed board
identity. Later progress is recorded in the [current frontier](../validation/frontier.md).
