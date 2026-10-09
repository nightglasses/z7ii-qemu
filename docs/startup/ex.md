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

The actual guest unpack return at `0x08003890` reports source end
`0x080528e1` and destination end `0x2000ee10`. Thus the compressed stream
consumes component offsets `[0x51ba8, 0x528e1)`; three padding bytes precede
the next COPY source at `0x528e4`. The later COPY handler's source pointer is
not an unpack-consumption measurement.

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
IRQ31/32 routing. [ST RM0090](https://www.st.com/resource/en/reference_manual/dm00031020-stm32f10x-microcontrollers-stmicroelectronics.pdf)
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
| timers 1-14 | standard F4 bases | QEMU Clock/QEMUTimer counter, update and CC1-4 scheduling; upstream OR-IRQ combines the six shared advanced/small timer lines |
| ADC 1-3 / common | `0x40012000 + 0x100*adc` / `0x40012304` | Configuration, external sample conversion, resolution/alignment, EOC/overrun, real DMA/IRQ; no fabricated reset samples |
| SPI 1-4 | `0x40013000`, `0x40003800`, `0x40003c00`, `0x40013400` | Separate transmit/receive state, real SSI master transactions, external slave clocks, RXNE/TXE/overrun with DR/SR clear sequence, IRQ and DMA requests |
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
AHB divider. Timer clocks follow RCC APB1/APB2 enables and dividers, including
the doubled timer clock when the APB divider exceeds one. Gating freezes the
counter phase. Other peripheral clock enables retain their register state;
electrical clock gating is not modeled for those devices. Physical oscillator
drift is not modeled. DMA double-buffer mode fails with transfer-error status; ordinary and
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

The original MCU transport uses two separate slave channels. RX initialization
`0x080343a4` queries SPI logical index 0 through `0x08040364`. The original table
at `0x0804dab8` yields handle `0x73`; `0x08021188` resolves that handle through
the pointer table at `0x2000ee10`. An actual original execution stopped before
the first RTC WPR write observed descriptor `0x2000e480`, whose base word is
`0x40013000` (SPI1). TX initialization `0x08034784` queries logical index 4,
whose handle `0x76` resolves descriptor `0x2000e4a4`, base `0x40013400` (SPI4).
The respective RCC APB2 reset/enable bits 12 and 13 independently agree.
Both original initializers configure slave mode, 8-bit words, LSB first,
CPOL=0/CPHA=1 and software NSS selected internally. The QEMU SSI word interface
carries complete numeric words; electrical bit-clock timing is not modeled.

Original RX worker `0x0803459c` submits 256-byte DMA reception into
`0x20017b76`, announces a request using its GPIO descriptor at state offset
`0x18`, waits for the original completion event, disables SPI and drains DR.
Validator `0x0803423c` checks the length against byte `0xfc * 240`, sequence
bytes `0xfc/0xfd`, and the 256-byte additive checksum against `0xff`. Successful
validation changes the handshake GPIO and calls the registered callback with
event `0x42010001` and the received buffer. These instructions execute in EX;
the model neither constructs frames nor generates that event or handshake.
The saved 1269-function inventory lacks the RX worker entry, so the worker
evidence additionally uses bounded native GNU objdump over the original bytes.

The original registry and runtime descriptors bind the transport pads below.
Offsets refer to RX state `0x20017b28` or TX state `0x20017d40`, rather than
the processor's physical GPIO register layout.

| Consumer | Logical GPIO | State offset | Physical pad | Original role |
|---|---:|---:|---|---|
| RX | `0x3c` | `0x14` | D12 | Validation result: clear after reception, set after a valid frame |
| RX | `0x1e` | `0x18` | B14 | Reception armed: set after submitting DMA, clear after completion |
| RX | `0x15` | `0x1c` | B5 | SPI1 AF5 data input |
| RX | `0x05` | `0x20` | A5 | SPI1 AF5 clock input |
| TX | `0x66` | `0x20` | G6 | Rising-edge peer input; original callback signals completion bit `0x08` |
| TX | `0x43` | `0x24` | E3 | Transmission request: set while waiting, then clear |
| TX | `0x45` | `0x28` | E5 | SPI4 AF5 data output |
| TX | `0x42` | `0x2c` | E2 | SPI4 AF5 clock input |

TX worker `0x08034a18` waits for its source event and dequeues a message before
constructing a 256-byte frame with additive checksum `0xff`. It arms transmit
DMA through `0x08045898`, clears completion bits `0x0c`, raises E3, and waits
up to ten scheduler ticks for DMA completion bit `0x04` or G6 callback bit
`0x08`. It then lowers E3 and follows the original timeout/completion recovery
paths. GPIO initialization `0x080348d0` supplies mode word `0x10110000` for
G6; the original HAL configures EXTI rising edges. The model does not raise
G6 in response to the request. Matching these roles to MAS requires its own
transport consumers, including direction and startup phase.

After EX realization, `nikon_ex_spi_slave(ex, channel, master_bus, cs, errp)`
attaches an ordinary upstream SSI peripheral to a source-proven master bus.
Channels are zero based: 0 for MCU RX and 3 for MCU TX. The returned endpoint is
owned by that bus. Every actual SSI word enters `nikon_ex_spi_transfer()`, causing
the real EX register/DMA/NVIC lifecycle. No NSS GPIO is guessed because this
original transport uses internal software NSS. `nikon_ex_gpio_connect_output()`
connects a physical pad to a real peer input while retaining the EX internal
EXTI route. The MAS master-bus identities and matching handshake pads must be
bound from its own consumers before combined-machine connection.

## Validation status

The external I/O peer uses the 24-bit PCAL6524 register protocol, without claiming
the unidentified board's part number. Original `0x080305a4` reads three bytes from
wire address `0x44` through `0x0802fdf0`; the runtime handle at `0x2001fe94` selects
I2C1 (`0x40005400`), hence seven-bit address `0x22`. Original `0x0802fef0` and
`0x0802ff9c..0x0803016a` establish the complete configuration group: three input,
output, polarity and direction bytes; six drive and edge bytes; input latch,
pull enable/select, interrupt mask/status/clear, live input and pin output
configuration. The upstream eight-bit PCA9554 and LED PCA9552 do not implement
this group. `hw/nikon/devices/exio.c` therefore uses upstream I2CSlave directly.

The protocol reference is [NXP PCAL6524, sections 6 and 7](https://www.nxp.com/docs/en/data-sheet/PCAL6524DS.pdf).
The peer implements register-group cycling and full auto-increment, electrical
input/output resolution, latch, polarity, pull direction, edge/level interrupts
and physical reset. Reserved commands NACK. Debounce uses actual P0_0 clock
edges, including the initial nine-cycle qualification, without inventing an
external oscillator. Drive strength is stored; analog current is not modeled.

Floating inputs resolve high when neither an external driver nor configured pull
drives them; this is an explicit electrical functional assumption. The selected
original polling mode's IRQ selector at `0x08051738` is `0x33`, which the actual
registry maps to NVIC26, the TIM11 CC1 service. It is not GPIO ID `0x1a`, nor
NVIC6. The peer INT remains unconnected in the original profile. Only the empty
qtest fixture wires INT to A0 to verify GPIO/EXTI/NVIC propagation; that synthetic
wire establishes no physical board mapping. The 24 named `pad` inputs accept low/high/release
as 0/1/-1 and carry no camera button names. No initial switch pattern or power
event is synthesized. `nikon_ex_io(ex)` returns the realized bus-owned peer for
external input connection; the separate `pad-out` group reports output drive or
high impedance. Cold board reset resets its registers; EX AIRCR
resets only the MCU and preserves the independent peer. Its active-low `reset`
input resets the peer and makes it NACK until released. Physical RESET wiring
remains unconnected rather than assigning an undocumented EX output.

The original snapshot completion is distinct from a camera key. At `0x08030740`,
request selector 1 and a non-null callback enter one of six slots at `0x2001ffcc`
and signal IoExpSvc flag `0x20`. Outer dispatcher `0x080304ac` tail-branches to
`0x0803042c`, where load `0x08030478` and BLX `0x08030488` consume that same bank
and pass current/previous three-byte snapshots to the callback. The separate
flag `0x10` bank at `0x2001ffb4` is not this request. `0x080380a8` unconditionally sets power
state `0x200159a4 + 8` to 1, copies both snapshots and derives falling bits as
`(~current & previous) & 0xffffff`, then calls `0x08038124`. No particular pad
pattern gates that store. `0x08038124` waits for bytes `+8` and `+13` to both equal
1, signals the original power descriptor with `0x1000` and clears both bytes.
The resulting `0x08037ccc` processing has no early return based on an input bit;
its final `0x08037eac` invokes the registered callback with event `0x04060002`.
The model supplies the I2C peer, not any of these guest flags or callbacks.

Original HAL `0x080442c4` uses BTF and drains two received bytes after STOP.
The EX controller retains a two-byte DR/shift-register receive pipeline, exposes
RXNE/BTF from occupancy and preserves the received bytes after STOP. The new
native fixture checks this original three-byte finish in addition to cyclic
register writes, AI across holes, external input, edge/latch/read-clear,
reset-held NACK and reserved-command NACK. The six existing native cases passed
with EX source `16bc110`; the focused IO case passed with `0afe966` in the shared
ARM snapshot `56a99b9`. IO reset publishes electrical levels in Resettable exit,
after GPIO reset hold, preserving the first real falling edge. That synthetic
fixture does not establish that the original power condition has been satisfied.

The complete selected-mode tables come from original offsets `0x51738..0x517d7`,
not from a GPIO-number interpretation of the RAM state:

| Original address | Content / consuming source |
|---|---|
| `0x08051738` | IRQ selector `0x33`; `0x08030664..0x0803068a` resolves, installs callback and enables it |
| `0x0805173c` | Snapshot event ID `0x020d0001` |
| `0x08051740` | Request callback event ID `0x020d0004` |
| `0x08051744` | IRQ callback `0x08030409` (Thumb) |
| `0x08051748` | Twelve-mode words: `0,0,0,0,1,1,0,1,1,0,0,0`; copied to IO state `+0xc0` |
| `0x08051778` | Same twelve-mode words; copied to `+0xf0`, consumed to register the selected timer |
| `0x080517a8` | Twelve-mode words: `0,0,0,1,0,0,1,0,0,0,0,0`; consumed by configuration update |

Actual cold initialization `0x080303f6` selects mode 8. Its copied timer-enable
word is one. Registry pointer `0x2000f330` selects table `0x0804de7c`, whose byte
`[0x33]` at `0x0804deaf` is `0x1a`, hence NVIC26. IRQ callback `0x08030408`
disables/clears that timer through the original HAL and signals IoExpSvc bit 8.
The dispatcher at `0x080304fa` then reads the three-byte snapshot and rearms
the timer at `0x08030522`. This is periodic guest polling; no physical PCAL INT
pad can be inferred from that selector.

An actual original run with shared ARM snapshot `56a99b9` completed the I2C
transactions, producing current/previous input bytes `03 00 00` with CFSR zero.
It reached the idle PendSV scheduler and the original power condition still had
bytes `+8=0`, `+13=1`. TIM11 contained CEN=1, DIER=2, PSC=49, ARR=65535 and
CCR1=20033. The old upstream timer only raises update interrupts when DIER.UIE
is set, stores CCR without scheduling comparison, and returned unbounded CNT
`0x0148db97`. Those behaviors cannot deliver this original CC1-only producer.
The observer was then closed; no firmware state was changed to bypass the wait.

The independent timer profile uses upstream QEMU Clock, QEMUTimer and OR-IRQ.
It schedules bounded CNT, CC1-4 and update events, buffered PSC/ARR/CCR reload,
up/down/center counting, repetition and one-pulse stop. TIM1/8 split update,
compare, trigger/COM and break interrupts. Defined capture, trigger, break,
output and DMA-request endpoints carry real supplied events. No board timer
DMA or external waveform is invented; no such physical connection is established
by this profile. Encoder mode and nonzero capture filters fail rather than
silently approximate an absent sampled input. Complementary output dead time,
internal trigger cross-routing and analog output behavior are not modeled.
Register capabilities distinguish the advanced, general, basic and small timers;
unsupported offsets fail with the actual CPU/PC/mapped address diagnostic.
The first original run of qualified timer snapshot `d07400b` exposed an actual
SMCR read at PC `0x08045b4a`, address `0x40014808`, before TIM11 starts. Clock
initializer `0x08045b30` unconditionally reads/writes SMCR and clears mask
`0xff77` before selecting the internal source. A single inspection of the whole
saved timer HAL family also shows OC1 initializer `0x080499f8` unconditionally
reading/writing CR2 before CCMR1 and CCR1. The base initializer `0x080498fc`
guards RCR with the advanced-instance checks; channel setup selects CCMR1/2
from the requested channel. Therefore the software small-timer profile exposes
stateful CR2 and the implemented SMCR clock/slave state as well. This is an
explicit extension from the chosen reference chip's small-timer register map,
supported by the original consumer; it does not identify a physical part.
The earlier capability rejection produced CFSR `0x8200`, preserved as a failed
run receipt. No comparison callback or SPI readiness was claimed for that run.
The new native timer case checks the CC1-only path, overflow, APB clock changes
and gating, PSC reload, split/shared IRQs, repetition/one-pulse and real capture.
Its result is pending the sole builder's qualified snapshot.

The native C qtest fixture covers delayed oscillator/PLL switching and cancel,
GPIO edge to grouped NVIC IRQ/W1C, DMA real SRAM copying/IRQ clear, disconnected
I2C NACK, peripheral reset and RTC protection, delayed INIT handshake, timed wakeup
and leap-day rollover. The shared builder's binary at
`work/build/nikon/qemu-system-arm` passed all six cases against immutable EX
source `c54ef00` (1.14 seconds), after the shared whole ARM build passed.

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

The subsequent complete original run used that qualified `c54ef00` binary.
RTC initialization completed; the observed calendar advanced to `00:01:08`,
RCC.BDCR selected enabled LSE, and SCB.CFSR was zero. SysTick CTRL was `7`,
LOAD was `49999`, and both the HAL and RTOS tick counters advanced. The CPU
executed the original PendSV scheduler; this is actual exception/thread
execution rather than a host jump to a worker.

The startup task's saved return is `0x0802137d`: it has passed the RTC bit-2
wait and now waits for bit 1 of the original `CDSCFLAG` object. McuRxSvc and
McuTxSvc exist with original entry pointers `0x0803459d` and `0x08034a19`,
but remain suspended until this phase completes. Power callback
`0x08021418` signals bit 1 for event `0x04060002`; event `0x04060001` instead
runs shutdown callbacks. The original dispatch table maps power-service bit
`0x1000` to `0x08037ccd`, whose end emits `0x04060002`. Its producer
`0x08038124` requires bytes `+0x08` and `+0x0d` of state `0x200159a4` both
equal one. The observed bytes are zero and one respectively. No host event,
task resume, GPIO assignment or memory write bypasses this condition. This
receipt qualifies reset, unpack, RTC and scheduler execution, while leaving
transport readiness dependent on the real source event and connected peers.

Debugger reads crossing reserved RCC offset `0x1c` and RTC offset `0x3c`
produced diagnostic failed transactions in the temporary application log;
those reads were observer mistakes, not original guest accesses. Subsequent
observations use only defined register offsets.
