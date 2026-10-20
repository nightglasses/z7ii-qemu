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

Windows on the complete bundle after the fixes listed under removed fault
layers:

- the thirteen-step RTOS initialisation chain enters `0x804b808` once and does
  not re-run; the tick is monotonic over 60,000 increments
  (`0x7cf, 0xf9f, 0x176f, ... 0x98ab`);
- the host command dispatcher takes more than 16,000 messages in 240 s with
  ids `101`, `102`, `103`, `131`, `1114` and `1115`;
- software timer callbacks post more than 10,000 messages in 240 s and timer
  ids 114/115 reach their handlers;
- the monitor lane runs 30,000 `0x0d` service entries and repeated `0x6b`
  equality edges without a sanitiser write;
- resets: one cold-boot escalation in 600 s, then continuous operation. A
  90 s window enters the reset vector twice in total, the attach entry and
  that single escalation.

The remaining single reset is the firmware's own cold-boot management ladder,
and it is a one-shot design rather than a model defect. The reload tick posts
the verdict handler (`0x08032379`, mask `0x00200000` in the `{mask, handler}`
table at `0x0804DF64`) from inside its `stage == 1` branch; the verdict arms
the block at `0x20013d18` and records reason `2`. The block's parameters are
host-supplied: the type-`0x0a` record's payload word at `+4` (`0x010a0a01`:
enable, period `10`, timeout `10`, flag) is copied into `0x20013bc2` at tick
`0x3da` by the record handler `0x08033418`, so the period and timeout come from
the MAS record lane rather than from EX constants. The arm also emits one record through
`sub_8033b48`; that record's builder sets `[0x200138ea + 4] = 1`, and the
verdict's own re-arm guard reads that same byte. The guard can therefore never
fire before the first arm, and no code path was found or observed that
completes the cycle: a write watchpoint on the done byte `0x20013d1d` over
60 s sees only the tick's expiry store (`0x8032cc6`) before the reset, and the
block layout has no other store offset. On expiry the verdict logs `20002` and
always escalates by reason (`1`/`2`/`3` produce status `12`/`15`/`16`), so the
armed cycle is fatal by construction.

The arm therefore happens at most once per SRAM lifetime: the post-reset pass
does not re-run the block initialisation that sets the stage byte, and a 100 s
post-reset window contains no verdict invocation at all. Whether the real
camera's host answers the management cycle inside its ten-invocation window is
not established; the composition deliberately does not synthesize such a
reply, so the observed behaviour is the unchanged firmware's own cold-boot
retry rather than an emulated handshake.

### MAS record lane: the missing power-record type

The EX message dispatcher at `0x080332F4` walks the `{type, handler}` table at
`0x0804E15C` and tail-calls the handler for the incoming record's first byte.
Only four records reach it in a 120 s window, all within the first millisecond
of guest time:

| Type | Handler | Meaning in this lane |
|---|---|---|
| `0x01` | `0x080333D8` | init record |
| `0x0D` | `0x080333B4` | service record |
| `0x08` | `0x08033398` | service record |
| `0x0A` | `0x08033418` | management-cycle descriptor; payload `+4` = `0x010a0a01` |

The boot payloads read out at the dispatcher are a version handshake and the
management descriptor:

```text
0x01  len 1c   01 02 00 00 "01.70.02"
0x0d  len 1c   0d 02 00 00 "01.70.02"
0x08  len 05   08 00 00 00 0a 00 00 00 00 00 00 00 34 00 00 00
0x0a  len 08   0a 00 00 00 01 0a 0a 01 00 00 00 00 ...
```

The power-record handler `0x08033328` is selected by type `0x12`, and that
type never arrives. The `0x6b` setup gate is not the cause: the EX cell at
`0x2000ED90` reads `1` at every dispatched record. The MAS TX engine itself
keeps running (`0x10243468` is hit repeatedly), so the lane is not dead at the
transport; the EX simply has no further records to dispatch.

The next producer to identify is therefore the MAS-side builder of the type
`0x12` record; its absence is what keeps the EX power-record path, the MAS
power classification and LCD start at zero hits. This is separate from the TL4
coprocessor boundary, which the guest reaches and leaves through its own
timeout branch.

### Power qualification state at run time

The missing record shows up directly in the EX RAM. At the 1200th `0x0d`
service (tick `0x2659`) the state output byte `0x2001fbae` is `0`, the
qualification record that feeds it (`0x2001fa90`) is sixteen zero bytes, and
the sequence/program/fault cells read `2`/`0`/`0` with the last applied class
`1`. The state calculator `0x0802866C` therefore has no record to interpret;
it is not failing a threshold. That state is what the EX reports and what the
MAS classifies as `0x43090001` (off).

Two explanations remain, and the current evidence does not choose between
them: either the MAS-side qualification-record producer is missing from the
composition, or it is gated by a real external input (for example a power
switch position) that this board record does not drive. Distinguishing them
needs the MAS condition around the sender, not another EX-side measurement.

The MAS GUI-app dispatcher `0x1020D41A` is not that gate: states `1`, `2` and
`3` all run, and the state-3 branch at `0x1020D51E` is a release/unbind
teardown path, so reaching it is phase completion rather than GUI progress.

### Removed fault layers

- The first fault was a precise data bus error (`CFSR = 0x8200`,
  `BFAR = 0x50060800`). The descriptor reaching that address is built in
  `sub_8023e36` and driven through `sub_8044c40`/`sub_8044cd2`; the window is
  the hardware random number generator, modelled by
  [the RNG device](../devices/exrng/protocol.md).
- The second was a usage fault (`INVSTATE`) with stacked `PC = 0x0802af00` or
  `0x08000000`, i.e. calls through non-thumb pointers. The reset vector only
  relocates the vector table; a boot loader produces the descriptor image at
  `0x2000d000` from the scatter table at `0x080038cc`. The loader and its two
  decoders run at cold boot and on every MCU reset
  ([`exboot.c`](../../hw/nikon/components/exboot.c)).
- The earlier reset loop also included status `14`: the board profile drove
  the seven declared power-good wires (`PA10`, `PB1`, `PB15`, `PB11`, `PB12`,
  `PF3`, `PG2`) low, and the firmware treats all of them low as a rail fault.
  It logs `20001`, records status `14` in `0x2003a000` and writes AIRCR
  `SYSRESETREQ` (`0x8042d74`). The profile asserts those wires high while
  `power_good` holds, so the strap check at `sub_8037f80` returns healthy.
- The board input record is published at attach time instead of after the
  first 100 ms tick, because its levels are wires rather than later inputs, so
  the early strap check sees the declared levels on the first boot pass.
