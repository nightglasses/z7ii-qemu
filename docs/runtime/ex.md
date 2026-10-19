# Private EX composition

A complete decoded bundle attaches the original MAS, SLA and EX components.
MAS keeps at most two Cortex-A7 CPUs. SLA owns two additional Cortex-A7 CPUs,
its memory and its GIC. EX owns one Cortex-M4 CPU, its private ARMv7M memory,
SRAM, DMA and NVIC. EX memory is never mapped into the MAS system address space.
The complete immutable EX GBytes slice is supplied before realization; the
device retains its own reference. Raw `-bios` MAS input has neither private peer.

Official multi-thread TCG needs capacity for all five contexts while the MAS
domain still has two CPUs:

```sh
qemu-system-arm -machine z7ii-mas-diagnostic,bundle=<decoded-container> \
    -smp cpus=2,maxcpus=5 -accel tcg,thread=multi -nodefaults
```

The machine guards the MAS array bounds separately from execution capacity.
Adding a private EX CPU does not create a third MAS CPU or change either GIC's
domain CPU count. The existing source-qualified SLA bridge and memory links
remain independent from EX. VR and TPJ are retained bundle components; their
execution is not implied by this composition.

The source EX TxA task uses MAS SSI0 to clock EX SPI1's slave RX endpoint. RxA
uses SSI1 to clock EX SPI4's slave TX endpoint. Both source MAS resource records
contain the no-DMA sentinel `158`; these channels remain FIFO/IRQ driven. The
existing DMA connections for SSI2 through SSI4 are unchanged. The mature SSI
chip-select index identifies the endpoint, while original MCU software NSS
controls its SPI configuration; it is not a frame acknowledgement.

Original constructor `1020b980` supplies equal 16-byte configurations for
SSI0 and SSI1: master, 16-bit words, LSB first, CPOL0/CPHA1 and a 20 MHz
requested rate. The divider is computed from the guest's selected source
clock; machine code does not replace that source rate with a fixed value.
The RX/TX requests contain `80` hexadecimal beats, giving 256 bytes per
frame. Original IRQ records bind SSI0 RX/TX to 192/193 and SSI1 to 194/195.
The original EX slave configuration uses eight-bit words. The native endpoint
therefore receives an explicit 16-bit, LSB-first master clock contract and
performs two real EX byte clocks per MAS word, packing their actual replies.
Machine code performs no frame conversion or synthesized completion.

Four software-complementary physical pairs are explicit functional-board
assumptions, because no board netlist was recovered:

| Original EX producer/consumer | MAS producer/consumer | Role |
|---|---|---|
| GPIO B14 output | GPIO5 bit9 input | TX readiness |
| GPIO D12 output | GPIO6 bit15 input | Frame validation |
| GPIO E3 output | GPIO5 bit10 input | RX request |
| GPIO G6 input | GPIO6 bit12 output | RX acknowledgement |

Only real guest GPIO drives traverse these pairs. No power flag, ready bit,
reply frame or IRQ is supplied by machine code. The independent EX model owns
its C8 I2C-expander interrupt and source-backed GPIO pull configuration.

The source 17-pad input frontend is realized after EX and connected before its
own realization. Its explicit host-key profile publishes physical pad levels;
the guest performs scanning and debouncing. Resettable exit and the EX retained
input wrapper preserve actual supplied levels across global, MCU and RCC resets.
This does not assign a camera menu meaning to a host key or synthesize a touch
contact. Real original menu, peripheral and SPI-arm execution still requires a
combined-runtime witness after this composition has been compiled and tested.

The functional machine profile owns one deterministic EX board-input record in
`hw/nikon/machines/z7ii.c`. It applies the required ADC samples, expander pad
levels, GPIOD.13 level and power-good wires through the EX device's normal GPIO
and ADC input APIs. There are no machine-level `ex-*` override properties.
This keeps the synthetic board assumption in one place and prevents a command
line value from being mistaken for recovered camera state. Direct EX device
unit tests still drive named inputs to cover reset and disconnected-pin
semantics.

The independent SLA clone requires an external 128 KiB input before the package
ROM. The default keeps that range unavailable. An explicit machine option
`sla-boot-prefix=erased` supplies a counterfactual erased-FF input; an explicit
file supplies exactly 128 KiB of read-only bytes through the shared SLA parser.
Neither option copies code into the clone destination or changes guest page
tables. The erased profile is not recovered Nikon instructions and cannot
establish original execution if a CPU enters its bytes. The option requires a
bundle because raw MAS input has no private SLA domain. A combined run must
record the selected input profile separately from the source-qualified EX
endpoint and from any actual original GUI output.

MAS has a separate `mas-boot-prefix` option with the same strict, erased or
exact-file choices. The two options invoke the existing shared input parser
independently and map distinct immutable ROMD storage in their own address
spaces. A file supplied for one domain is not reused for the other domain.
The original MAS selector-2 transfer reads `10000000..10100000` into `baa00000`:
only its first 128 KiB is external; the following `e0000` bytes come from the
original MAS package. This option maps only that missing input range, keeps
guest writes under the existing MEMC rejection contract, and never preloads
the destination or page tables. MAS input is also valid with raw `-bios`; an
SLA input still requires the complete bundle.

## Host command lane

The EX firmware is a uT-Kernel application with a message-driven command lane.
`sub_8027e58` is the host-command dispatcher: a handler installed on the
message object `0x2001f664` in the `BatCmAl1` task. It reads the id from
`msg+0x10` (`sub_80270dc` subtracts 1000, so timer ids arrive as `1000+id`; the
`msg+0x14 == -1` sentinel maps to id 999) and selects a handler with two
byte-offset tables for ids 101..119 and 121..130 plus compare tiers for 120 and
131..138. Any other id takes the default path at `0x8027ee4`, which logs
`19013` and returns the message.

| Source | Ids | Notes |
|---|---|---|
| event | 101..111, 126, 129, 132..134, 136..138 | posted by lane callbacks and the host/protocol handler `sub_8024d5c` |
| timer | 112..116, 118..125, 130, 135 | 16 software timers created by `sub_8026df8` from the record table at `0x08051444`; periods 1..500 ms |
| none | 117, 127, 128 | no case at all; id 128 is timer-driven but has no handler, so its 500 ms record always reaches the default path |

The monitor lane is inside this dispatch, not beside it. Handlers 101, 102,
104, 107, 114/124, 115, 116, 119, 120 and 126 call the `0x6b` committers
(`sub_802b234`/`sub_802b40c`) and/or the `0x0d` service (`sub_802b6f0`). The
shared sequence state is `0x2001f5da` (0 idle, 3 report, 5 powered, 6 bay-1
done), the program state `0x2001f5b2` (0 idle, 1 armed, 2 dirty), the fault
byte `0x2001f5db` and the last applied class `0x2001f5dc`. The board class
bytes `0x2001f644..0x2001f647` are written only by `sub_8029418` from outside
the host-id set.

### Media presence inputs

The presence poll `sub_8026718` reads one GPIO pin through the pin registry:
`0x2001fe24` resolves to **GPIOE bit 9** (present = 0, absent = 1) and the
firmware drives **GPIOB bit 2** as the media rail (`sub_802ba44`, BSRR
`0x00000004` set and `0x00040000` reset). A second raw input, GPIOC bit 0,
gates the periodic `0x0d` refresh. Both pin handles come from the boot
registrar over the identity table at `0x0804d9b0`, and every cell involved is
zeroed BSS, so a missing detect read never blocks startup: the poll simply
keeps sampling, and ten consecutive low samples arm the insert path (rail on,
`0x6b` commit, `0x0d` read, event `0x0202004d`).

The machine does not drive these pins. With the GPIO reset level (0) the guest
takes the present branch; with the pin high it stays absent and silent. No
physical board meaning is claimed for the pins beyond what the image does.

### Measured state of the lane

Hardware-breakpoint windows on the complete bundle (240 s unless noted):

- only ids 101 and 102 dispatch, about 2.5 times per second, and software timer
  callbacks do post at the same order of rate;
- the T-Kernel timer initializer `0x0804b808` (`current_time = 0`, timer queue
  re-init, SysTick reload `49999`) runs about once per second as part of the
  whole thirteen-step initialization chain (return addresses
  `0x0804b5b5..0x0804b615`), so the RTOS time base never accumulates;
- the MCU reset vector `0x080002b4` is re-entered at the same cadence with the
  reset stack pointer, so the guest is looping through complete resets rather
  than running on;
- the fault that drives those resets is a bus fault whose stacked PC is
  `0x080001d6` with `LR = 0xfffffffd` (an exception-return value), i.e. the
  return path of the `BASEPRI` lock helpers `0x080001c4`/`0x080001d2`.

That last item is the current frontier: the reset loop, not the monitor lane,
is what keeps the periodic timers from running. The next probes are the
`BASEPRI` helper callers, the bus-fault status word (CFSR/BFAR), and the AIRCR
`SYSRESETREQ` writer at `0x08000606` that the fault path reaches.
