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

The [battery comparison](battery.md) reproduces the EX system
handshake, client-4 report and BatCk publication with this multi-thread baseline.
Single-thread/`icount` probes instead fail a timed EX SPI/ACK condition before
the first system completion; their missing battery update is scoped to that
execution profile and must not replace the composed-runtime baseline.

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
`0x12` record. This is separate from the TL4 coprocessor boundary, which the
guest reaches and leaves through its own timeout branch.

**Correction: type `0x12` is not a demonstrated boot requirement.** The
dispatcher table at `0x0804e15c` does map `0x12` to the handler `0x08033328`,
but that handler only stores one payload byte into `0x200138da` and updates a
period (`sub_80214fe(0x2000f..., 16)`, `sub_8020670(8, 17)`). A cross-reference
pass over the EX image shows `0x200138da` has **one writer (that handler) and no
readers at all**, so the delivered byte is consumed by nothing. The earlier
sentence in this section that tied the missing `0x12` to the MAS power
classification and LCD start was an inference from the lane narrative, not a
code-supported dependency; it is withdrawn. The `0x12` producer is therefore a
dead data feed in this image, not the gate on the first GUI frame.

### Power dispatch and transport are running

With corrected function-entry probes (a Thumb pointer such as `0x08037CCD`
executes at `0x08037CCC`; probing the odd address misses the entry), the
power-on chain in the current bundle is measured end to end on one boot pass:

| Tick | Event |
|---|---|
| `0x378` | `0x08038426` sets state `0x200159A4 + 0x0d` to `1` |
| `0x390` | `0x080380AE` sets `+8` to `1` |
| `0x38B` | dispatcher `0x0803825C` takes bit `0x1000` and enters handler `0x08037CCC` |
| `0x396` | transport services `0x0803459C` (RX) and `0x08034A18` (TX) start |
| `0x397` | producer `0x08038124` posts the service and clears `+8`/`+0x0d` |

The MAS power classifier entry `0x1038A6A8` is also entered (once per window),
so both domains do run their power code. Earlier statements that the transport
services stay suspended, or that the classifier never runs, were probe-address
artifacts and are withdrawn.

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
  (`hw/nikon/components/exboot.c`).
- The earlier reset loop also included status `14`: the board profile drove
  the seven declared power-good wires (`PA10`, `PB1`, `PB15`, `PB11`, `PB12`,
  `PF3`, `PG2`) low, and the firmware treats all of them low as a rail fault.
  It logs `20001`, records status `14` in `0x2003a000` and writes AIRCR
  `SYSRESETREQ` (`0x8042d74`). The profile asserts those wires high while
  `power_good` holds, so the strap check at `sub_8037f80` returns healthy.
- The board input record is published at attach time instead of after the
  first 100 ms tick, because its levels are wires rather than later inputs, so
  the early strap check sees the declared levels on the first boot pass.

### Qualification record: no runtime writer

The state calculator `0x0802866C` reads its first record at `0x2001FA90` and
decides from `+0x38` and `+0x78`:

- `+0x38 == 0` returns `2`; `+0x38 == 4` returns `3`; other values fall
  through to `+0x78`, and `+0x78 == 0` returns `1`.

An all-zero record therefore still yields `2`, not `0`. The measured output
byte `0x2001FBAE` is `0`, so the publisher that stores the calculator result
never runs; the missing record is not the only gap.

Two write probes agree with that. A write watchpoint on the record word
`0x2001FA90` fires once in a 65 s window, and only at the boot zero-fill
(`0x080038BE`); no service path ever populates the fields. A breakpoint on the
service-record entry `0x0802AB20` records no hit at all.

The next target is therefore the caller that computes and publishes the power
state (the setter chain around `0x0802937C`), not the record contents.

### The publisher is never entered

Static callers recovered from the Thumb `BL` encoding of the EX image:
`0x0802866C` (calculator) is called from `0x080285FA`, `0x08028162` and
`0x0802892E`; the setter `0x0802937C` from `0x0802816E`, `0x08028932` and
`0x0802A9C6`; the service-record entry `0x0802AB20` from `0x08028454`.

At `0x08028150` the publisher always reaches the calculator: when the gate
helper `0x08028200` returns zero it takes `r0 = 2` and jumps back to
`0x0802815E`, which still calls `0x08028162`. A 50 s window records no hit at
`0x08028200`, `0x08028162` or `0x0802816E`, so the containing function is
never entered at all. The missing link is that caller, not the record, the
gate helper or the calculator.

### The publisher is host-command case 9

The publisher `0x08028098` has a single `BL` caller, `0x0802800C`, which is
case index `9` of the byte dispatch table at `0x08027EB2`
(`tbb [pc, r1]`, bytes `92 95 98 7a 9b a7 19 19 aa ad`). The enclosing
function is the host-command dispatcher `0x08027E58`
(`push {r3..r7, lr}`), the handler installed on the message object
`0x2001F664`; it has no `BL` caller of its own.

So the power-state publisher runs only when the dispatcher receives the
class that maps to index `9`, and that message is never dispatched in the
current run. The next step is to decode the `r1`-class derivation and find
which host id yields class `9`.

### The publisher's class is host id 130

The `tbb` at `0x08027EAE` indexes `r1 = id - 121` for ids `121..130`, so
case index `9` is host id `130`. The dispatcher derives the id through
`sub_80270DC(msg)`, and the mapped case calls the publisher.

A 60 s window at the dispatcher entry logs the ids it actually takes:
`101`, `102`, `103`, `131`, `1114` and `1115` (`0x65`, `0x66`, `0x67`,
`0x83`, `0x45A`, `0x45B`). Host id `1130` (`0x46A`, the timer form of `130`)
never appears.

The software-timer record table at `0x08051444` uses 20-byte records
(`callback`, `period`, 8-character name): record 0 is
`0x08027D40 / 500 / "BatCmCy1"`, record 1 is `0x08027D38 / 250 / "BatC..."`.
The next target is the callback record that should post id `130`, and why it
does not run.

### The id-130 timer exists but never fires

The callback that posts host id `1130` is record 2 of the timer table: entry
`0x08026988` (`movw r0, #1130`), period `50`, name `BatCmCy3`.

Creation is complete. A breakpoint on the timer constructor `0x08026DF8` shows
all sixteen indices created early in the boot (order `0,4,3,5,6,7,8,9,a,b,d,c,
e,f,2,1`), including index `2`. The constructor receives the record's callback,
period and name and creates the timer in the block at `0x2001FC2C`.

Execution is not: a breakpoint on `0x08026988` records no hit in 45 s, while
the lane does dispatch timer ids `1114`/`1115`. So the remaining question is
why this created timer never delivers its callback, which is the last link
before the power-state publisher.

### Timer blocks carry identical structure

At tick `0x55a` the timer control blocks at `0x2001FC2C + index*20` read:

| Index | field 0 | callback | period | name |
|---|---|---|---|---|
| 1 | `0x1a` | `0x08027D38` | `0xfa` | `BatCmCy2` |
| 2 | `0x19` | `0x08026988` | `0x32` | `BatCmCy3` |
| 3 | `0x0d` | `0x08027D30` | `0x14` | `BatCmCy4` |
| 4 | `0x0c` | `0x08027D28` | `0x05` | `BatCmCy5` |

Every block holds the expected callback, period and name, so the record was
installed correctly. Field 0 is a countdown that differs per index; index 2
reads `0x19` and index 1 `0x1a`, while the two timers that do dispatch
(`1114`/`1115`) read `0x0c`/`0x0d`. The difference to explain is the
callback delivery, not the record installation.

### Cyclic dispatch site

The callback for timer id `1114` returns to `0x0804AB77`, inside the RTOS
cyclic-expiry path: `0x0804AB5A` updates the object (`0x0804AC5C`,
`0x0804AC8C`), then loads `r0 = [obj]`, `r1 = [obj+8]` and calls the callback
with `blx r1`.

So the dispatch code is common and works for indices 3/4. Index 2's callback
is installed in its block but never reached, which points at the expiry-list
membership or object state maintained by `0x0804AC5C`/`0x0804AC8C`.

### The id-130 cyclic object was never started

A memory search for the callbacks finds both the record block and the RTOS
cyclic object. At tick `0x4e7`:

| Object | `[0]` | `[4]` | `[8]` | `[12]`/`[16]` | `[20]` | `[24]` |
|---|---|---|---|---|---|---|
| id 130 (`0x200229A8`) | `0x08026989` | `0` | `0x32` | `0`/`0` | `0x270` | `0` |
| id 114 (`0x20022738`) | `0x08027D29` | `1` | `5` | `0x20022654`/`0x200226E4` | `0x4EC` | `0x0804AB5B` |

The working timer carries state `1`, list links, a next-expiry tick and a code
pointer; the id-130 object carries state `0`, no links, no expiry and no code
pointer. It was created but never started or linked, so the cyclic dispatcher
cannot see it.

This is the classification result: the gap is not a missing MAS producer, not
an unmodelled external input and not a device contract. It is an EX-side
cyclic object that the firmware creates and never starts.

### Cyclic start API and its callers

The start/stop API is `0x08020AB0`; its `BL` callers sit in the power module:
`0x080339DA`, `0x08033BA4`, `0x08033C2A`, `0x08033CC6`, `0x08033D50`,
`0x08033E10`. The 16-timer creation wrapper `0x08026DF8` is called from
`0x08026D12..0x08026D30`.

The id-130 object is created but not started, so the next check is which
object each of those six start calls covers and whether the id-130 object is
skipped by a condition inside the power module.

### `0x08020AB0` is the cyclic start path (earlier withdrawal was wrong)

The six call sites pass `0x20013924` (five) and `0x200139A0` (one). Those are
not the objects but 20-byte RAM descriptors: `0x08020A5A` builds a create
record, calls the create API `0x0804BF38`, and writes the allocated object
index back into the descriptor's first halfword. `0x08020AB0` reads `*a1`
(that index) and finishes with `BL 0x0804D388`, the start writer that sets the
object's state bit and arms its timer. So `0x08020AB0` is the stop/configure/
start wrapper, and the earlier "not the cyclic start path" withdrawal is
reversed.

Two more facts complete the chain:

- the battery monitors have their own wrapper,
  `0x08029264(idx) -> 0x08020AB0(0x2001FC2C + 20*idx, handler)`. The 16
  descriptors come from the ROM record table at `0x08051444` (`BatCmCy1..G`;
  the catalogue is in the EX config document), and all 36 call sites carry a
  literal index.
- the literal index set is `{0,1,3,4,6,7,8,9,10,11,12,13,14,15}`.
  **Index 2 (`BatCmCy3`, id 130) appears in no start site; the only code that
  can select it is `0x08027D48`.**

### Why the id-130 timer never starts

Host id `131`, when `0x2001F5B6` is `9` or `10`, clears `0x2001F5BD` and calls
`0x08027D48`, the monitor selector:

```text
if (battery_state[0] == 1) { start(1); v = 2; }   /* BatCmCy2 + BatCmCy3 */
else                       {           v = 0; }   /* BatCmCy1 */
start(v); start(4); start(3);
```

So `battery_state[0]` (`0x2001FBA6`) is the single boolean that decides whether
the id-130 publisher timer exists at all. In this image **nothing ever sets it
to 1**:

- static: the only direct store to byte `0x2001FBA6` is the battery-state reset
  `0x08027174`, which writes `0`. Every other reference loads the block base
  and touches `+1`, `+0x12`, `+0x14`, `+0x29`, `+0x2C`, `+0x33`, `+0x34`.
- runtime: a write watchpoint on `0x2001FBA6` over 120 s records one write,
  the boot clear to `0`, and `0x08027D48` is entered once with
  `battery_state[0]=0` and `0x2001F5B6=0x0a`.

Consequently the selector always starts descriptor 0 (BatCmCy1), 4 and 3. That
matches the lane: ids `113`, `114` and `115` are published (113 as `1113`,
114/115 as `1114`/`1115`), while `130`/`135` never are, and the dispatcher only
takes inbound ids `101`, `102`, `103`, `104` and `131`.

The remaining question is closed: **the id-130 publisher is unreachable in this
EX image because its start is gated on a battery-variant byte that the firmware
never sets.** It is not a missing MAS producer, not an unmodelled input, not a
device contract and not a configuration value.

### The RTOS start writer leaves the id-130 object untouched

Write watchpoints on the two objects' state words (`+4`) show:

- `0x2002273C` (id 114) is written `1` repeatedly by `0x0804D404`
  (`lr=0x0804D3FD`), the RTOS path that starts/links a cyclic object;
- `0x200229AC` (id 130) is written only by the boot zero-fill
  (`0x080038BE`); `0x0804D404` never touches it.

So the start routine exists and runs, and the id-130 object is simply not
among the objects it is asked to start. The remaining step is the caller that
selects the objects (`0x0804D404`'s caller chain) and its condition.
