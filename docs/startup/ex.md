# EX complete component functional profile

The `nikonex` machine takes the original decoded five-component container with
`-bios`. `nikon_bundle_component(..., NIKON_COMPONENT_EX)` supplies an owned
immutable 512 KiB slice. All container validation finishes before EX is realized;
the component is copied once into read-only flash, without altering its vectors,
instructions, descriptors, compressed data or padding. The ordinary emulator
requires that input. An empty image is allowed only under qtest.

## Reset and memory evidence

The fixed original provider is `00_ex1840_013000.bi.payload.bin` from the decoded
Z7II C1.70 container. [The EX region evidence](https://github.com/mashiro01/nightglass/blob/90a37d0a7d27e9b82f38e65a8bb834c946e13834/research/z7ii/ex-regions.md)
binds its 512 KiB extent, little-endian vector words, `SP=0x20000c08` and Thumb
reset pointer `0x080002b5`. Two independent absolute references and the original
scatter table support the software flash address `0x08000000`. This is a
functional physical address choice; the board's actual aliases remain unknown.
The profile aliases flash at address zero for Cortex-M reset vector fetch. It
does not synthesize a BootROM or claim that an out-of-package ROM was recovered.

The guest reset routine copies `0x1c4` bytes from flash into `0x20000000`, then
writes VTOR. Its original dispatcher at `0x08003804` runs the seven descriptors:

| Source | Destination | Length | Original handler |
|---|---|---:|---|
| `0x08000f7c` | `0x2003c000` | `0x26d8` | COPY `0x08003894` |
| `0x08051ba8` | `0x2000d000` | `0x1e10` | unpack `0x08003838` |
| `0x080528e4` | `0x2003f000` | `0x850` | COPY `0x08003894` |
| unused | `0x20000408` | `0x800` | ZERO `0x080038b0` |
| unused | `0x20000c08` | `0x200` | ZERO `0x080038b0` |
| unused | `0x2000ee10` | `0x141fc` | ZERO `0x080038b0` |
| unused | `0x2003a000` | `0xc30` | ZERO `0x080038b0` |

The emulator performs none of those initialization operations. SRAM is the
finite `0x20000000..0x20040000` software envelope supported by the original
initialization records and startup upper-bound constant. This capacity is an
explicit functional assumption, not a part-number identification. The separate
SOURCE `src/ex_physical` ROM/COPY research views contain selected recovered
functions; they are not a complete reset image and are not the input to this
machine.

The profile instantiates upstream `ARMv7M` with Cortex-M4, VFP/DSP defaults,
four NVIC priority bits, bitband aliases and 97 external vector slots. The
`0x1c4` copied vector extent determines the slot envelope; it does not prove
that all 97 interrupts exist physically. BASEPRI, CPACR, FPCCR, VTOR and exception
entry/return are executed by upstream QEMU. The concrete CPU model is a software
assumption supported by the original Thumb/v7-M/FPU register accesses.

## Peripheral protocol

Peripheral addresses and the reset group were inspected together in the
original `0x080001e0..0x08000f7c` startup paths using native GNU ARM objdump;
runtime descriptor evidence additionally identifies the three I2C bases and
IRQ31/32 routing. [ST RM0090](https://www.st.com/resource/en/reference_manual/rm0090-stm32f4xx-reference-manual-stmicroelectronics.pdf)
is the public protocol reference. Matching addresses establish the software
interface used by this profile, not the physical MCU identity.

| Group | Address | Behavior |
|---|---|---|
| RCC | `0x40023800` | Explicit defined registers; oscillator enable/cancel, PLL input validation, delayed ready bits, conditional SWS switching, reset lines |
| PWR | `0x40007000` | Voltage-scale control and status, writable backup control fields |
| RTC | `0x40002800` | DBP/WPR protection, clock-dependent INIT/RSF/write-window synchronization, prescaled BCD calendar, leap dates, alarm/wakeup EXTI interrupts, backup registers |
| flash interface | `0x40023c00` | Byte/halfword/word cache/latency control; W1C status; programming operations fault |
| GPIO A-I | `0x40020000 + 0x400*port` | Upstream GPIO state machine restricted to the common F4 `0x00..0x27` layout; explicit F4 reset settings |
| SYSCFG / EXTI | `0x40013800` / `0x40013c00` | Guest EXTICR pad selection, rising/falling edge detection, software requests, masks, pending W1C, grouped NVIC lines |
| USART 1-6 | standard F4 bases | Upstream STM32 USART, real `serial_hd(0..5)` character backends, receive/transmit IRQ state |
| timers 1-14 | standard F4 bases | Upstream STM32 counters/interrupt lifecycle; shared update IRQs combine through upstream OR-IRQ devices |
| ADC 1-3 / common | `0x40012000 + 0x100*adc` / `0x40012304` | Configuration, external sample conversion, resolution/alignment, EOC/overrun, real DMA/IRQ; no fabricated reset samples |
| SPI 1-3 | `0x40013000`, `0x40003800`, `0x40003c00` | Separate transmit/receive state, real SSI master transactions, external slave clocks, RXNE/TXE/overrun, IRQ and DMA requests |
| I2C 1-3 | `0x40005400 + 0x400*channel` | Real I2CBus START/address/data/STOP, actual slave ACK/NACK, SR1/SR2 address-clear sequence and event/error IRQ |
| DMA 1-2 | `0x40026000 + 0x400*controller` | Eight streams each, real private-address-space bus beats, widths/increments/counts, half/complete/error status and W1C IRQ |

All profile MMIO is little-endian and permits 1/2/4-byte accesses that stay
within one defined word. Unknown profile accesses report CPU, PC, address,
direction and width and return a failed memory transaction. No broad register
bank is filled with ready values. Upstream GPIO is exposed through a bounded
alias, excluding L4-only analog registers. Its current F4-common semantics are
reused rather than reimplemented.

Upstream `stm32_rcc.c` only implements enables/resets and cannot produce the
original oscillator or SWS transitions. Upstream `stm32f2xx_spi.c` explicitly
lacks interrupt/DMA support and a CPU slave endpoint; it additionally transfers
on DR reads. These gaps require the independent EX protocol state machines.
The generic `stm32f405_soc.c` was not used because its unimplemented-device
apertures would silently let missing peripherals return zero.

The functional timing assumptions are a 16 MHz reset HSI, a 10 MHz board HSE
consistent with the original reset PLL arguments, and a 100 microsecond
oscillator settling time. RCC derives CPU/SysTick clocks from the guest PLL and
AHB divider. The reused peripheral timers currently use the fixed reset clock;
APB clock-rate accuracy and physical oscillator drift are outside this profile.
Clock enables retain the guest's register state; electrical clock gating is not
modeled. DMA double-buffer mode fails with transfer-error status; ordinary and
circular single-buffer beats are implemented. Flash erase/program, SPI CRC,
ten-bit I2C, RTC timestamp/tamper input and subsecond calibration/shift, and
electrical signal timing are not claimed. Nonzero RTC shift/calibration requests
fault. RTC uses a 32 kHz functional LSI or 32.768 kHz LSE; its calendar advances
from guest-provided values using the actual prescaler and selected clock. The
backup calendar persists across EX system reset; RCC.BDRST resets it.

The saved complete 1269-function native inventory was queried once for all
direct peripheral references. It confirms application references to the advanced
and small timer groups (`0x400100xx`, `0x400104xx`, `0x400018xx..0x400020xx`,
`0x400140xx..0x400148xx`) and ADC common control `0x40012304`; those complete
timer groups are instantiated before running. Upstream ADC was inspected and
rejected because it fabricates an incrementing sample value. The EX ADC waits
for `nikon_ex_adc_input()` to supply an external 12-bit sample for the selected
regular channel. Multichannel/injected/continuous conversion timing is not
claimed by the current single regular conversion profile.

## Component wiring

`include/hw/nikon/ex.h` exposes the isolated processor as `TYPE_NIKON_EX`.
`nikon_ex_set_firmware()` validates the complete slice and reset vectors before
realization. `nikon_ex_memory()` exposes only the private EX address space; MAS
does not see EX SRAM by accidentally sharing numerically equal addresses.
Guest AIRCR.SYSRESETREQ schedules a local EX recursive reset through a bottom
half; it does not reset the other processor domains in a combined machine.

`nikon_ex_spi_bus()` and `nikon_ex_i2c_bus()` are ordinary QEMU buses for real
attached peers. `nikon_ex_spi_transfer()` accepts a real external clocked word
only when guest SPE is enabled and MSTR is clear. It returns the queued guest
transmit word and places the received word into EX RX state. An inactive or
unfilled slave returns a pulled-high idle word; it does not invent an ACK,
message or successful command. `nikon_ex_gpio_input()` returns a physical
port/pin input line and drives the normal GPIO/EXTICR/EXTI path. Button/pad and
MAS transport assignments must come from recovered consumers, not guessed
MENU mappings or direct calls into firmware GUI routines.

## Validation status

The native C qtest fixture covers delayed oscillator/PLL switching and cancel,
GPIO edge to grouped NVIC IRQ/W1C, DMA real SRAM copying/IRQ clear, disconnected
I2C NACK, peripheral reset and RTC protection, delayed INIT handshake, timed wakeup
and leap-day rollover. The shared builder's binary at
`work/build/nikon/qemu-system-arm` passed the first five cases against EX source
`fbc2cf0` (1.05 seconds). The RTC case is pending the next shared build.

The first actual original-container run used that same binary and source. It
entered at `0x080002b4`, reached `0x08020084` through the guest's original scatter
dispatcher, and reported `SP=0x20000c08`, `VTOR=0x20000000`. RAM at `0x2000d000`
held unpacked words `0x08041dd1, 0x08041dd1, 0x08041dcb, 0x08041dd9`; COPY views
at `0x2003c000` and `0x2003f000` contained Thumb instructions. All inspected ZERO
destinations held zero. This proves original reset and initialization execution,
not full application readiness. Continuing naturally exposed RTC WPR at PC
`0x080395ae` and SSR at `0x08044fb2`, causing a real failed transaction and
HardFault lockup. Original RTC drivers `0x08045004`, `0x080491dc`, `0x08044eb4`
and `0x080450a4` establish the whole protection/INIT/alarm group added next.
Runtime logs and GDB command files stay under ignored `work/ex/`. No vendor
firmware, generated native object, or temporary GDB command file is tracked.
