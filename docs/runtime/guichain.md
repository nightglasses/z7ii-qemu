# GUI chain

> **Research log, not living documentation.** This file records the EX/GUI
> reverse-engineering frontier in the order it was worked, so paragraphs that
> call something "the current blocker" describe the moment they were written
> and may be superseded. The resolved current state is in
> [`ex.md`](ex.md) (EX runtime and the cold-boot ladder) and
> [`../validation/native.md`](../validation/native.md); where this log and
> those pages disagree, the pages win.

This document records the unchanged-firmware boundary for the Z7II composed
GUI path and the single-run observation method as it stood at each stage of the
work. The raw GDB logs
remain under `work/`; this file records only stable facts and the next evidence
needed.

The machine interface is now intentionally smaller: a complete bundle installs
the built-in EX configuration and board-input record. Retired hypothesis-only
input overrides are not part of the machine interface.

## One-run causal trace

The parent runner uses one GDB session with breakpoints at each boundary:

```text
MAS app boot/service
  0x102017a0 -> 0x1020d41a -> 0x1020d5fa
MAS TX descriptor/FIFO
  0x10243468
EX RX completion and record parser
  0x08031e70 -> 0x080332f4
EX power record path
  0x08033328 -> 0x08032404
MAS power classifier/report
  0x1038a6aa -> 0x103f96da
provider lifecycle
  0x10502db8 -> 0x10502d0a -> 0x104d7e78
  -> 0x104dc804 -> 0x104db90e
LCD start
  0x1046bf0c
```

The passive probe is `work/runs/analysis-app-start/probe-full-chain-12480.gdb`.
It prints call arguments, link registers and record/payload headers without
writing guest memory or changing execution.

## Current observed boundary

The clean functional run reaches the firmware-owned event-4 path and then
enters EX stage 5. Its first blocking transaction family is a missing EX I2C0
slave:

```text
0x0802a740 -> 0x0802b40c / 0x0802b234
  address byte 0xd6 (7-bit 0x6b)
  setup/readback commands include 0x02, 0x04, 0x05, 0x06 and 0x07;
  several transfers require one-byte readback
  no slave or failed readback -> 0x2000ed90 = 0, setup gate fails

0x0802a75a -> 0x0802ad0c
  same address, final command 0x0b, one result byte
  this is reached only after the setup family succeeds
```

The same run also shows a periodic second service retrying another missing
slave:

```text
0x0802b6f0 -> 0x0802b898
  address byte 0x1a (7-bit 0x0d), command bytes 08 01, 32-byte receive
  no slave -> 0x0802b924 failure and retry
```

The client-4 battery report still reaches MAS and is classified as
`0x43090001` (off); this is downstream evidence, not a separate MAS command
producer gap. The power snapshot, `0x43090002`, provider admission and LCD start
remain zero-hit because the EX I2C gates do not complete. Once the 6b setup
result is made nonzero, `0x080276c8` additionally accepts only local battery
classes 1, 2 or 3; the current built-in raw sample 3500 produces class 0 and
is therefore not a power-on value.

## Runtime EX-pad pulse is not an additional input

The one-run chain probe was repeated with the former explicit-input experiment
(the values are now part of the built-in functional board record), and with a
real qtest runtime pulse on the
EX expander after the boot settled: pad 6 driven low, pad 0 pulsed low/high 48
times, pad 6 released high, pad 0 pulsed 48 more times. The machine rejected the
drive on a number of pads with the upstream `can't be driven externally`
diagnostic, exactly as expected when the pin is still an output.

Result (`work/runs/analysis-readout-gate/pad-pulse-024850`):

```text
MAS_TX            462
EX_RX              14
EX record parser    4   payload[0] = 0x01, 0x0d, 0x08, 0x0a
EX_SPECIAL          0
EX_POWER_BUILD      0
POWER_CLASSIFY      code 0x43090001 (off)
POWER_REPORT        code 0x43090010
DISPLAY_START       2   (r1 = 0x8 and 0x3, no provider bit)
F092_M_SET          0
F091_ADMISSION      0
PROVIDER_ADMISSION  0
MASTER_RESTART      0
```

The runtime EX-pad pulse therefore does not change the record set and does not
produce the power-on classification. The earlier `lcdnext-exio*` positive
control ran a different experiment build and its `EX_TX ... len=112` line is
not reproduced by the current functional profile, so it cannot be treated as
the missing supported input.

A separate observation: the previously captured `natural-right-snapshot1`
LCD surface (`head1.ppm`) is a valid 780x780 RGB screendump but is entirely
black (zero non-zero bytes). Surface existence is not GUI output.

## Battery report path

A focused one-run trace on the current functional profile (the same values are
now internal to the board record; no firmware patch and no forced GPIO) shows
the EX power path is **not** stuck at the
MAS-to-EX command producer. The observed ordered chain is:

```text
EX system mask800 producers 0x080322c0   pfc=1, pfd=1
EX system-kind3 builder     0x08034034
EX mask40 handler/producer  0x08032bb0 / 0x08024c48
EX snapshot                0x080278fc
EX battery builder         0x08025544   gate 0x2000e87c=1
EX TX worker               0x08034a18
MAS ingress 0x102055e6     client=0x04 class=0x01 len=0x70 (112)
MAS power module 0x1038a6aa  code 0x43090001
MAS battery classifier 0x103f8ec6
MAS report 0x103f96da       code 0x43090010
```

Evidence: `work/runs/analysis-readout-gate/bchain-030704`,
`work/runs/analysis-readout-gate/rwire-030851`,
`work/runs/analysis-readout-gate/exrep-031443`.

The report is emitted, received and classified on the current build. Its
classification is off because the EX I2C gates above have not completed; the
report is not evidence for a missing MAS command producer.

## New frontier: MAS power classification of the 112-byte record

The classifier `0x103f8ec6` runs on the stored record at `0xb23bd29c`
(copied from the MAS power object buffer `0xb1d53c68`). The decoded record
bytes on the current build are:

```text
+0   0x01                 +4  0x02 0x00 0x01 0x00
+36  0x28 0x23 0x00 0x00  (0x2328 = 9000)
+61  0x00                 (gate read by 0x103f9028)
```

The EX builder output buffer `0x20015fc0` carries only
`{+0=3, +4=0x00010002, +20=0x64, +24=0x2328}` and leaves `+61` zero. Changing
Changing the historical ADC experiment to 1000 or 4095 changed neither the EX
record nor the MAS classification (`0x43090001` in all cases).

The resolver is therefore the field that the MAS classifier reads at record
offset `+61` (and the neighbouring `0x103f8ae4` state), which the current EX
report never populates. This is a MAS-side decode/condition question, not the
MAS-to-EX command `0x12` path that the previous frontier note emphasised.

Sweep receipt: `work/runs/analysis-readout-gate/adc1000-031617`,
`work/runs/analysis-readout-gate/adc4095-031728`.

## Inside the classification: decode chain

The client-4 report is decoded by the MAS client callback chain
(`0x102055e6` -> client table `[0xb1887bf0]` -> handler `0x1042f9e5` ->
callback `0x1042fa29` / decode `0x1042faec`). The decode writes the power
record at `0xb25dac8c`:

```text
[r5+0]  = 1
[r5+4]  = input[4]
[r5+5..]= input[7,6,8,10,5,9,11,12,14,16,18,20]
[r5+36] = input[24] (u16)         -> 0x2328
[r5+28] = input[28]
[r5+40] = input[36], unless the state object 0xb1886e4c[0] overrides it
[r5+41] = input[37]
```

The classifier `0x103f8ec6` then reads `record[61]` at `0x103f9028` and
`0x103f8ae4`, and the state object `0xb23bcd9c` (`+8`, `+32`, `+56`). On the
current build the decoded record has `+61 = 0` and the classifier selects the
"off" configuration. The decode never writes `+61`; it comes from the raw
input record or the state object `0xb1886e4c` (`[r8]` / `[r8+1]` / `[r8+2]`).

The next probe should read the raw client-4 body fields
(`input[4,6,7,8,10,16,18,20,24,28,36,37]`) and the state object `0xb1886e4c`
at the decode, to identify which finite source the model must supply. The
command `0x12` MAS-to-EX path is not on this chain.

Evidence: `work/runs/analysis-readout-gate/dec-032201`,
`work/runs/analysis-readout-gate/src61-031911`,
`work/runs/analysis-readout-gate/cls-032035`.

## The exact EX power-state gate

The MAS classifier's record byte `+61` comes from the EX report, and the EX
builder writes it from the EX battery state at `0x20016040`:

```text
0x0802571e  ldrb r1,[r0,#24]        ; r0 = 0x20016040
            r1 == 0 -> report[61] = 0
            r1 == 1 -> report[61] = 1
            r1 == 2 -> report[61] = 0x11
```

The whole `+48..+111` block of the report (which contains `+61`) is written
only when `0x080256c0` sees `lr == 1`, where `lr` was loaded at `0x0802557a`
from `[0x20016040+16]`. On the current build:

```text
[0x20016040+16] = 0     -> the +48.. block is skipped, report[61] stays 0
[0x20016040+24] = 0     -> even if written it would be 0
```

The 60-byte state at `0x20016040` is written by the `0x02020045` handler
`0x08025268` (`0x08020552`, r0=`0x20016040`, r1=snapshot, r2=60) from the
snapshot that `0x080278fc` builds on the stack. That snapshot builder does
write `sp[16]=1` (`0x0802794a`) and `sp[24]=1/2` (`0x08027b4c/0x08027b52`) on
the normal path, yet the state observed at `0x08025574` has `+16=0` and
`+24=0` and the current values are `{+0=0x0005, +4=0x2328, +7=0x64}`. The
event-0x02020045 copy breakpoint `0x08025274` did not hit in the 70 s run, so
the exact route by which the snapshot reaches `0x20016040` (or the branch that
skips the `sp[16]/sp[24]` writes) is the remaining item to close.

So the finite missing input is whatever should set `0x20016040+16` and
`+24` in the EX battery state before the event-15 build. This is the single
edge between the working EX/MAS transport and the power-on classification.

## Root cause: the EX cold-start factory config gate

Tracing the `0x20016040` state upstream lands on the EX cold-start config
validator `0x080294b0`, called once from `0x08024caa`:

```text
0x080294b8  ldr r0,[r0]              ; r0 = 0x08004000
0x080294c2  adr r1, "MAKE"           ; 0x454b414d
0x080294c6  bl 0x0804b994            ; 4-byte compare
0x080294ca  cbz r0, 0x080294dc       ; equal -> configured path
0x080294cc  ...                      ; else f644[0]=1, f644[1]=0, f644[4]=0
```

`0x08004000` is inside the EX ROM (`memory_region_init_rom`, read-only), and
the extracted EX payload has zero at file offset `0x4000`, so the unchanged
image always takes the invalid branch: `0x2001f644[0]=1`. That flag is the
gate for the whole power snapshot:

```text
0x0802a5a0  ldrb r0,[0x2001f644]
0x0802a5a4  cbnz r0, 0x0802a5ac        ; invalid -> skip 0x08025d98()
0x0802a5a6  bl 0x08025d98
0x0802a5aa  strb r0,[0x2001fba6]       ; only for the configured path
```

`[0x2001fba6]` is exactly the flag the snapshot builder `0x080278fc` tests
with `cbz r2, 0x08027984`; the `sp[16]=1` / `sp[24]` writes live on the
configured path only. Hence `report[61]=0` and MAS classifies off.

Historical counterfactual: adding the old byte patches
`4000:4d;4001:41;4002:4b;4003:45` made `0x080294dc` (configured path) run,
but the guest then aborted because
the rest of the factory config block is absent. So a valid EX factory config
block at `0x08004000` is the required finite external input; the extracted
firmware does not contain it. Receipts:
`work/runs/analysis-readout-gate/make-034221`, `make2-034406`.

## EX factory config: built-in synthetic data

The EX config is an external factory parameter store, not part of the update
payload's meaningful image. Its recovered layout:

```text
region descriptor table 0x2000e79c (20-byte entries {id, base, 0, size, flags})
  id0 base 0x08004000 size 0x0e2c   magic "MAKE" + first parameter view
  id1 base 0x08004010 size 0x0e18   main config struct (validator argument)
  id2/3 0x08008000/0x08008010, id4 0x08008100, id5 0x08008300,
  id6 0x08008340, id7 0x0800c000, id8 0x0800c010
```

The configured path `0x08029418` reads `config[276..279]` and `config[528]`
into `0x2001f644[0..4]` (the ADC channel-enable record), and `0x080294f4`
loads the battery calibration bytes (`config+4..+72`, `+248..+296`, `+316`,
`+320`, ...) into the battery working state.

The current functional profile keeps the missing factory data in a read-only
`NikonEXConfigData` constant in `hw/nikon/machines/z7ii.c`.  At EX
realization it copies the immutable component and applies that data only to
the private functional flash image; the original payload remains unchanged.

Experiment result: the magic alone takes the configured path. With
`config[276] >= 1` (`0x4124:1`) the EX boots the configured path normally
(channel open, provider install, RX all hit). With `config[276] = 0`
(`0x2001f644[0] = 0`, the value required for the power snapshot) the EX stops
before the transport and never issues an ADC conversion, regardless of the
rest of the simulated config (all-zero, all-`0x01`, all-`0xff`).

Those earlier file experiments are retained as historical observations only.
The supported functional run now uses the built-in data and does not require
an external config file or byte-patch argument.

## ADC-init follow-up (temporary model trace)

A temporary trace of the EX ADC register writes (`reg 2` control, `reg 13`
channel) separates the three cases:

```text
erased (0x2001f644[0]=1):            6467 ADC writes, ch13=0x1, ch2 start, boots
configured + config[276]>=1 (f644=1): 7365 ADC writes, boots
configured + config[276]=0  (f644=0):    0 ADC writes, stalls before transport
```

So with `0x2001f644[0] = 0` the EX never initializes/touches ADC0 at all and
the battery handler loops in the channel-0 wait `0x08025d98` (100 iterations
in 180 s). The erased/`f644=1` boot initializes the ADC normally. This means
the config-content simulation is not sufficient: the file is external and now
injectable, but the `f644[0]=0` firmware branch never reaches the ADC
initialization that the wait depends on.

Next: identify what in the `f644[0]=0` boot path should perform the ADC
initialization, i.e. the code between boot and `0x08025d98` that the erased
path reaches but the configured path does not. This is a firmware state
question, not another config byte.

## The f644[0]=0 stall is a GPIOD.13 board input

`0x08025d98` is not an ADC wait; it is a board-line poll. It reads its GPIO
object `[0x20020090+40] = 0x40020c00` and `r1 = [obj+12] = 0x2000`, then calls
`0x08043acc` which returns `[0x40020c00+16] & 0x2000` (`0x40020c10` = GPIOD
IDR, bit 13).

- GPIOD.13 clear for 5 polls -> `0x08025d98` returns 1 -> `0x2001fba6 = 1` ->
  battery handler takes `0x0802a5f6` (no cold init), transport never starts.
- GPIOD.13 set -> returns 0 -> `0x2001fba6 = 0` -> main path, ADC init, and
  the SPI transport runs.

The firmware configures GPIOD.13 as an input (`0x40020c00` MODER bit 13 = 00),
so it is an external board line, not an ADC channel. The model leaves it
undriven (low), which is why the configured `f644[0]=0` path stalls.

Counterfactual (temporary board drive, reverted): replaying
`nikon_ex_gpio_input(ex, 3, 13) = 1` lets the configured `f644[0]=0` boot
reach channel-open and the SPI RX; `work/runs/analysis-readout-gate/gpd13-*`.
With the drive plus the earlier file-based synthetic block, the EX transport is reached but the
client-4 battery report still does not form in the 150 s window, so the board
line is necessary but not yet sufficient.

This is the kind of finite board input represented by the built-in functional
record. It holds EX GPIOD.13 high and replays the declared level across EX GPIO
mode/reset activity. The earlier explicit-input run reached channel-open and
repeated EX RX/transport activity, proving that the named board input removes
this specific stall. It does not, by itself, produce the client-4 battery report or
the power-on event: the event dispatcher still observes codes `0x01/0x02/0x03`,
`0x05`, `0x1c`, and `0x1d`, while the battery-builder event/call remains
unreached. Thus GPIOD.13 is necessary for this configured path but not a
complete GUI or power contract. The remaining boundary is the source of the
power-builder event, not another implicit ready/status fabrication.

## Post-GPIOD.13 frontier: owner publication is still timer/state gated

The configured-input run advances farther than the original undriven-input
run. A passive trace reaches the system-kind-2 next-frame and
completion handlers (`0x08033a00`, `0x08033a74`), the kind-3 gate/builder
(`0x08032c38`, `0x08034034`), and the owner flag setter (`0x08024c48`). The
local event-5 handler (`0x08024dec`) also runs.

It still does not reach the snapshot/publication chain:

```text
event 4 -> 0x08025854 -> 0x080278fc -> 0x08027ce8(0x02020045)
        -> local event 15 -> 0x08025544 -> client-4 report
```

The event-5 handler takes its no-deferred-work branch because the associated
state word at `0x20015dcc` remains zero; no event-4 dispatch, snapshot callback,
or `0x08028091/0x08027d00` publication-timer callback is observed. The EX
software timer object at `0x2001f9cc` is registered with callback
`0x08028091`, but the current run only shows registration and no later arm or
callback. This moves the next investigation from board GPIO/config values to
the source-backed state transition that arms the publication timer or sets the
deferred-work word. No event, timer expiry, snapshot, report, or GUI status is
being synthesized.

## Correction: event 4 has a local firmware producer

The earlier statement that event 4 had no local producer was incorrect. A
full static closure found the callback/table path that selects it:

```text
EX event 3
  -> 0x08024d5c[index 3] -> 0x08024dd0
  -> 0x08027e22 -> queue commands 101 and 102
  -> command object +16 == 102 -> 0x08027efe -> 0x0802a888
  -> qualified 0x2001fa50 entry ([entry+4] == 1)
  -> 0x0802a95e callback(0x0202004a)
  -> 0x08025185 table index 9 -> event code 4
  -> 0x0802551c -> EX event 4
```

This is EX-internal firmware behavior, not an external event-4 injection
contract. The same image contains the event-3 producer: the lower-level
service callback at `0x08034168` calls `0x08025c68`, whose `0x08025c68` /
`0x08025c6a` sequence selects event code 3 and enqueues it through
`0x0802551c`. The current configured run is therefore missing the earlier
service callback/data-ready transition that reaches `0x08034168`, plus any
entry qualification needed by `0x0802a888`.

The GPIOD.13 result remains valid only as the early configured-path gate; it
is not the final event-4 trigger. No event is synthesized by the model.

## Follow-up: GPIOD.13 is only the early gate

A direct GPIO-read trace of the configured run shows GPIOD.13 (`0x40020c10`,
mask `0x2000`) is read exactly in the five-poll wait at `0x08025d98`. After
that wait the EX firmware does not read this line again before the repeated
owner/event-5 cycles. This rules out a later GPIOD.13 edge as the missing
publication trigger.

The event queue consumed by `0x08024d5c` is registered at `0x08024c5e` with
the local setter `0x0802551c`. A direct-call-only scan of that setter was
misleading: it found no direct `movs r0,#4` call site, but it missed the
callback/table path above. The configured run consequently dispatches
`1/2/3/5/0x1c/0x1d` and never reaches the event-3 -> command-102 transition;
event 4 is produced only after that indirect service callback and the
`0x2001fa50` entry qualification. This is a stronger boundary than “one more
GPIO value”: GPIOD.13 is only the early gate, while the missing upstream edge
is the service/data-ready callback that reaches `0x08034168`.

## MAS ingress and EX power-I2C follow-up

A passive MAS trace on the clean functional profile (`work/runs/analysis-readout-gate/masdrop5-063852`) separates the earlier “dropped-frame” suspicion from the power report. The ingress loop at `0x10205406` saw 25 candidate frames in the first window; every observed client id was below `0x12` (including the accepted `client=0x04` report), so no high-client rejection carried the missing power transition. The client-4 report arrived repeatedly as `class=0x02, len=0x70` and reached both `0x1038a6aa` and `0x103f96da`.

At those callbacks the report header/body bytes began:

```text
01 00 00 00 02 00 01 00 00 00 00 00 ...
```

The MAS flags word remained `0x10754d09`; a watchpoint on `0xb1800c80` saw only the boot initialization write (PC `0x1002be9c`) and no later update. Thus the known power-on bit (bit 19) is not being suppressed by an unconsumed high-client frame; it simply never becomes set in this run.

The EX I2C power peers were also traced temporarily on the configured path. The firmware performed only the established qualification transaction on register `0x23` (write `0x88`, followed by repeated readbacks). There was no later read of the auxiliary `0x1033` profile or any uninitialized status register. This removes “missing default expower readback” as the current evidence-backed cause; the production model was restored unchanged after the trace.

One additional GPIO read is not an external power input. `0x0802b87c` reads GPIOE.9 (`0x40021000`, mask `0x0200`) from the qualification loop at `0x08026680`; the configured firmware drives that pin as an output and the observed IDR is zero. A temporary high-drive experiment could not change it once the guest selected output mode and did not produce event 4. E9 therefore remains an internal accessory/qualification result, not a new `ex-*` input contract.

The remaining boundary is now precise: the configured EX path reaches
owner/event-5 work while event 4 is never enqueued because the event-3
service/data-ready callback has not reached `0x08034168`, and the
`0x2001fa50` entry qualification has not completed. The MAS power report is
then consumed as “off.” Adding expower defaults or another GPIO level would
be fabrication without that upstream callback contract.

## Static service key for the missing callback

The callback is not an unbound function pointer. The EX mode-indication module
initializes at `0x08032278`, obtains service slot `0x00010007` through
`0x08042258`, and passes that slot into `0x080322f8`, which registers the
module callback/mask state. The descriptor callback path then reaches
`0x080322a2 -> 0x0803411c -> 0x08034168` when that service publishes its
data/ready transition. This gives the current outer boundary a concrete
software name: the provider of service key `0x10007` has not delivered the
mode-indication input. With the initialized service base at `0x2000d174`,
`0x08042258(0x10007)` resolves to slot `0x2000d200`; the next static target is
the firmware/provider code that populates that slot. The provider's physical
source is not yet proven, so no new GPIO or protocol property is added.

## Queue-level closure

The EX event queue itself has no hidden local bypass. The queue object consumed by
the dispatcher is `0x20015e90`; the only static call to its enqueue primitive
`0x08041cac` is from the event setter `0x0802551c`, which allocates a node and
writes the event code at node offset `+16`. The other `0x08041cac` call sites
enqueue unrelated objects (`0x20016120` and `0x2001f664`). A full EX image scan
therefore finds no second producer capable of placing code `4` into this queue.
This closes the queue-level branch: event4 is firmware-produced after the
event-3 service callback and command 102; it must not be injected as an
external event. The unresolved frontier is the lower-level service/data-ready
input and callback registration that should reach `0x08034168`, not a second
event queue or an omitted direct GPIO/timer call.

## Synthetic factory-config branch: additional GPIO qualification (not a new input contract)

To separate the missing event-4 producer from the earlier factory-config
experiment, an ignored counterfactual run supplied `cfg-all1-f6440.bin` and
temporarily drove the EX input pins without changing the tracked model.  In
that branch the firmware enters a different battery qualification loop before
the normal snapshot gate:

```text
0x080270f0 -> GPIO base 0x40020800, mask 0x0020  (port-C pin 5)
0x0802b87c -> GPIO base 0x40021000, mask 0x0200  (the documented port-E.9 output)
```

The first read is an undriven input in the current model (`IDR=0x0900`), so the
qualification function returns the failure branch.  Driving that line high
temporarily changes the readback (`IDR=0x0920`) and passes this one predicate;
the later port-E.9/ADC qualification still does not enqueue EX event 4 or reach
the snapshot/publication chain.  The port-E.9 line is guest-configured as an
output and remains the existing internal qualification result, not a host
power-good input.

This is hypothesis execution only: the supplied config image is synthetic, the
temporary pin drive was reverted, and no `ex-gpioc5` property is being added.
The accepted input frontier is the built-in synthetic configuration and board
record; the earliest
complete missing edge is still the lower-level service/data-ready callback
transition that feeds event 3 and then command 102; event 4 itself is already
present in the firmware.

## Static refinement: adjacent EX service-record candidate

The `0x10007` slot itself still has only one literal consumer in this image:
the mode-indication subscription at `0x08032278`. An adjacent EX framework
path does expose a concrete internal provider candidate. The startup routine
at `0x080341e8` obtains internal service-record index 7 through
`0x08042244`, then initializes that record at `0x080342d4`; the initializer
calls `0x08042288` with callback `0x0803459d` and the record-7 pointer:

```text
record index 7 -> 0x080342d4 -> callback 0x0803459d
                    (generic record/service framework)
```

This does not prove that `0x0803459d` is the physical source, nor justify a
host callback or event injection. It narrows the static task to the state
feeding record 7 and the framework operation that publishes it to service
slot `0x2000d200`; the next evidence must come from those original writes.

## Static closure of the power-on boundary (2026-10-17)

The power/event path has now been checked statically from its earliest gate;
the generic MCU receive callback above must not be treated as the proven
missing power input.  The exact chain is:

```text
factory config[1][541] @ 0x0800422d
        |
        v
0x08032c40..0x08032c5a  -- gate for the power predicate
        |
        v
0x08037f80  -- ON iff P0.10, P1.1 and P6.2 all read low
        |
        v
0x08032c3c  -- debounce (three true polls)
        |
        v
firmware event 0x02020047 -> event code 3 -> command 102
        |
        v
0x0802a888 -> 0x08025185 -> event code 4
```

The unchanged update image has an erased config window: `0x0800422d` is zero,
so `0x08032c5a` skips `0x08037f80` entirely.  This is an external factory
configuration input, not a missing firmware instruction.  Once that byte is
enabled, the board-level power predicate reads three GPIO inputs (P0.10,
P1.1, P6.2); the model's pad id `0x62` is P6.2.  The predicate is low-active
and debounced.  GPIOD.13 remains a separate early configured-startup gate; it
is not the final power/event-4 input.

The battery ADC/expower qualification is a downstream condition for the power
snapshot/report (thresholds are read by the battery module at
`0x0802768c`), not an event-4 injection point.  After the GPIO predicate and
qualification succeed, event 3, command 102, and event 4 are all produced by
the EX firmware itself.  There is no basis for synthesizing event 4 or for
adding a generic MAS 256-byte frame to the power contract.

The static scan supporting this closure is recorded in
`work/runs/analysis-ex-power-adc/receipt.md`; it covers the GPIO test tables,
the config table at `0x2000e79c`, the event setter/queue, and the battery
threshold path.  Earlier notes that described the unresolved boundary as a
generic service/data-ready callback remain useful for the adjacent service
framework, but are not evidence that this callback is the power-on input.

## Built-in synthetic configuration data

The missing factory block is represented by a read-only `NikonEXConfigData`
constant in `hw/nikon/machines/z7ii.c`.  The complete bundle composition
applies it automatically to the private EX flash image at realization.  There
is no config file, byte-patch argument, profile-version selector, guest event,
ADC result, GPIO edge or report supplied by this mechanism.

The object is explicitly synthetic: it supplies only the statically required
`MAKE` marker, deterministic calibration seed bytes, configured-path fields,
and gate byte `0x0800422d`.  The guest still performs validation, GPIO
polling, ADC conversion, debounce, event generation and report construction.

### Offset correction for earlier synthetic files

The EX resource table has two adjacent views: id0 begins at `0x08004000`,
while id1 (the main config pointer passed to `0x08029418` and
`0x08032c4e`) begins at `0x08004010`.  Therefore a file copied at id0 offset
`0x4000` must place `config[276]` at file offset `0x124` (payload offset
`0x4124`) and `config[541]` at file offset `0x22d` (payload offset `0x422d`).
Some earlier ignored `work/runs/ex-config-sim/cfg-*` files placed those fields
at the id0-relative offsets without the `0x10` view adjustment; they are not
valid evidence for the main-config values.  The built-in profile uses the
corrected id1 mapping.

## Local validation correction: event 4 is produced and consumed

The post-cleanup functional run was instrumented again with the built-in
configuration and board-input contract. It observed the firmware-owned prefix:

```text
0x08034168 -> 0x08034198 -> 0x08025c68
EVENT_SET 0x3
EVENT_SET 0x4
0x08025854 (event-4 handler)
0x08027e6c (event dispatcher)
```

Therefore the earlier “event 4 is absent” wording is not valid for the current
model. `GPIOC.5` (`0x40020800`, mask `0x20`) was also mapped by a direct read
trace; its default floating-high model value makes the qualification helper
return zero and continue. No new `ex-gpioc5` input is justified.

The remaining observed stop is downstream of event-4 consumption. The EX
state byte at `0x2001f5da` still follows `0 -> 5 -> 1`; it never reaches the
value `4` required by the battery-transition routine at `0x08026b6c`. Snapshot
calls at `0x080278fc` consequently retain `state19=0` and `state42=0`, and no
`0x08032404` power-builder call is observed. MAS continues to classify the
valid client-4 report as `0x43090001` (off). This is an internal EX
state-machine/qualification boundary, not permission to synthesize event 4 or
add another board-level input.

### Adjacent qualification checks (negative result)

Static inspection of the state-machine body also identifies a GPIOE.9 read at
`0x0802b87c` (`0x40021000`, mask `0x0200`). It is reached only from the
`state==3` branch at `0x0802a602`; the current functional run never enters
that branch. The ADC classifier at `0x0802a6f6` produces the expected classes
for the supplied raw sample ranges (`<1032` -> 4, `1032..1380` -> 3,
`>=3148` -> 0), but changing those samples does not change the global state
sequence or produce the power builder. These are downstream qualification
observations, not evidence for another top-level external input.

### ADC1 classification is not the boot switch

The EX firmware's selected battery sample is the raw 12-bit value at
`0x2001f5c6`; the current configured path reads ADC1 channel 1. The classifier
at `0x0802a580` stores only a local class byte at `0x2000ed8c`:

```text
raw >= 3148       -> class 0
2728..3147        -> class 1
1381..2727        -> class 2
1032..1380        -> class 3
raw < 1032        -> class 4
```

This is a battery/power qualification result, not a percentage and not a
direct boot command. The downstream qualification routine additionally checks
the raw sample against `3819`, the EX state bytes, and the global stage. In the
functional runs, ADC1 values in the low, middle and high ranges changed the
local class (`4`, `3`, `0` respectively), but the global stage still followed
`0 -> 5 -> 1`; event 4 was still produced and consumed, and the power-snapshot
builder was still not called. Therefore ADC1 is a required qualification input
after the I2C setup succeeds, but changing it alone cannot open the GUI path.

The event dispatcher has a separate table entry for event `0x81` (table entry
129 at `0x08027eae`), which calls `0x08029f38` and can write global stage `3`.
The current run never receives `0x81`; the only static producer is the generic
incoming-message callback at `0x08025016 -> 0x08028b38`, where the message
supplies the event byte. This is a distinct message/service path, not the ADC
classifier and not permission to inject an event.

## Immediate post-event-4 stop: an unmodeled EX I2C endpoint family

The next stop after the firmware-owned event-4 handler is now identified by a
focused trace. At `0x0802a740`, the EX state machine first calls the setup
helpers `0x0802b40c` and `0x0802b234`; they submit a sequence of I2C operations
through descriptor `0x2001fda6`. The final status read at `0x0802a75a` calls
`0x0802ad0c(0x2001f654)` through descriptor `0x2001fdcc`. Source-derived fields
are:

```text
I2C controller       0x40005400 (EX I2C0)
write address byte   0xd6      (7-bit endpoint 0x6b)
setup commands       0x02, 0x04, 0x05, 0x06, 0x07 (several one-byte reads)
final command        0x0b, one result byte
```

The unchanged functional model has only the EX I2C devices at `0x22`, `0x4f`
and `0x10`. With no `0x6b` endpoint, the setup helpers return zero and write
`0x2000ed90 = 0`; the final status operation is not a successful completion and
`0x0802a77e` falls back to stage 1. This is the concrete first reason the run
does not enter the power snapshot path. It is not an ADC1 class decision, and
it occurs after event 4 has already been produced and consumed.

The endpoint identity and transaction shape are firmware-derived, but the
device contents are not recoverable from the update image. The initial
uncommitted experiment attached only an acknowledged `0x0b` read and was
insufficient; the current functional profile instead uses the finite command
family and bounded responses recorded in the checkpoint below. The remaining
termination issue is in the generic EX I2C asynchronous controller path, not
permission to use an unbounded or address-specific read-zero aperture.

This supersedes the earlier broad wording that called the post-event-4 stop a
generic service callback. The service/event path is still an adjacent software
framework, but the first concrete missing external transaction on the current
functional run is the `0x6b` I2C endpoint above.

## Clean-source ordering and the adjacent `0x0d` service

The clean-source run (`work/runs/analysis-readout-gate/clean-gate2-074950`)
separates the two transactions instead of treating every I2C failure as one
power signal:

```text
stage 5 -> 0x0802a740 -> 0x0802b40c / 0x0802b234
  I2C0 address byte 0xd6 (7-bit 0x6b), setup/readback command family
  failure writes 0 to 0x2000ed90 before the final 0x0b transaction

then -> 0x0802a75a -> 0x0802ad0c
  same endpoint, command 0x0b, firmware request parameter r3 = 1
  no slave -> no 0x0802ad92 status commit

periodic service -> 0x0802b6f0 -> 0x0802b898
  I2C0 address byte 0x1a (7-bit 0x0d), command bytes 08 01,
  receive path clocks four bytes; 0x08043e38 returns completion code 0x20
  no slave -> 0x0802b924 failure and the service retries
```

The `0x0d` parser is firmware-owned and classifies the fourth received byte as follows:
bits 0..1 become output field 0, bits 2..4 equal to one set field 1, and
`a9/aa`, `c9/ca`, `e9/ea` select output classes 1, 2 and 3 respectively;
other values select class 0 and set the error marker. These response values
are not present in the update image, so they remain an unmodeled external
device contract rather than a permissible zero-filled reply.

The endpoint experiment also exposed a generic controller-model issue:
`ex_i2c_fill()` previously prefetched beyond the firmware's finite receive
length. The model now bounds each transaction (one byte for `0x6b`, four for
`0x0d`, and the short expander reads) and preserves the STM32 two-byte
pipeline. Native device tests cover those boundaries. A guest trace confirms
that `0,0,0,a9` reaches the EX data register and produces the expected
`f64c=1/0/1/0` status fields; the remaining gap is the later qualification
state transition, not another guessed slave byte.

Thus the current external-input boundary is two finite I2C slave contracts
(`0x6b` setup/readback plus `0x0b`, and `0x0d/{08,01}`), while the exact
payload bytes still require an explicit reconstruction profile. The unchanged
firmware has not reached the power snapshot or GUI in this baseline.

The setup response constraints are recoverable even without a factory dump. The
EX helper at `0x0802b014` validates the one-byte read pairs from the `0x6b`
setup sequence: mode 0 accepts a value at most `0xa0`; mode 1 compares the low
six bits and requires the result at most `24`; mode 3 compares the low five bits
and requires the result at most `25`. The firmware passes mode values 0, 1 and 3
at the call sites. The `0x6b` setup registers are a register file: the firmware
writes one byte and reads it back, so the model must echo the written value
rather than return a fixed constant. A separate `0x0b` status register can
return `1` (or `2`), the only values accepted at `0x0802a77e`. This is finite
protocol reconstruction, not a generic read-zero aperture; the one-byte I2C
completion bound and a native write/read regression are required.

## Bundle device models (checkpoint 2026-10-17)

The complete bundle composition attaches two finite I2C slave models from
`hw/nikon/devices/expower.c`:

* `0x6b` is a register-file responder: the setup commands store the written
  byte and return it on the next read (the `0x0802b014` equality predicate),
  while command `0x0b` returns the ready value `1` and zero for the
  controller's bounded prefetch.
* `0x0d` accepts the observed `08 01` and `a9 00` requests and returns a
  bounded four-byte response whose fourth byte is the explicit class
  register: class 0 `0x00`, class 1 `0xa9`, class 2 `0xc9`, class 3 `0xe9`
  (`0x0802b6f0`). The default is class 1; the `class` QOM property is a
  functional board input and reset preserves it. The leading bytes are
  reserved. The firmware's separate completion field is `0x20` (32), not a
  32-byte receive length.

The native `/nikon/ex/power-devices` test exercises both addresses, write/read
equality for the setup registers, the `0x0d` class table and reset-preserved
class, reset state, command validation and bounded response lengths. The earlier guest checkpoint reached the `0x6b` setup/final
transaction and the `0x0d` four-byte response but observed a zero destination
buffer and `fba6`; the register-file correction is a model change and needs a
fresh runtime receipt before claiming the setup now passes. This checkpoint
proves the finite device contracts and their integration, not a complete
original-firmware boot.

## Current clean-profile frontier: internal qualification object, not event injection

The preceding historical event-4 note must not be applied to the current
clean profile. Static disassembly of `0x0802b7da..0x0802b826` shows that the
service copies the fourth received byte (`sp+7`) into `0x2001fe21`, then parses
that field. Therefore the source-backed functional response is `0,0,0,0xa9`,
not `0xa9,0,0,0`. With that correction, `f64c` becomes `1/0/1/0`, but the
clean profile still does not reach event 3: it remains at the later stage-5
qualification loop. The earlier byte-order conclusion was a static-analysis
error and is retired.

For that experimental later frontier, the exact qualification failure is
visible in `0x0802a888`/`0x0802aa30`: the callback gate reads
`0x2001fa50+4 == 0` while `f644[0] == 0`.

That word is not an unmodeled GPIO or I2C reply. During EX initialization,
`0x08026d84..0x08026db2` copies the event/timer object at
`0x2001f9f4` into `0x2001fa50`; the source field `0x2001f9f4+4` is zero. A
hardware watch on both the source and destination over the clean run saw only
the startup zeroing/copy and no later nonzero write. The registered callback
object (`0x20007b34`, events `0x65/0x66`) never dispatches the battery event
that would populate this qualification state. Therefore the remaining gap is
an internal EX service/state transition whose enabling condition is still to
be identified statically; synthesizing event 4 or forcing `f50+4` would violate
the unchanged-firmware boundary.

## Fresh clean-run correction: event 3 is reached; mode 0 blocks the timer path

The last paragraph above is historical and is superseded by the current clean
trace (`seq-baseline-012640`, `svc-dump-013846`, `arm4-013617`). The service
key `0x00010007` callback is delivered repeatedly (`0x080322a2`, `r0=1`), and
the event-3 producer/builder at `0x08034168` runs. Its event `0x66` is then
dispatched to the qualification handler at `0x0802a888`; this is not an
absent-provider or absent-event-3 failure.

The next gate is the EX state byte at `0x2001f598+39` (`0x2001f5bf`). The
handler at `0x080272a4` treats values 1, 2 and 3 as ADC-channel selectors;
for those values it samples the corresponding channel and arms the registered
timer object `0x2001f9cc` via `0x08020a40`. The clean run observes the byte as
zero on every cycle. A write trace sees only the startup clear at
`0x080038be`; there is no later mode publication and no arm of `0x2001f9cc`.

Static dispatch identifies the writer: the EX event-0x16 handler at
`0x08028ae8` writes `r0` to `0x2001f5bf`, stores the companion fields, and
publishes event 110. The event-0x16 path is entered from the internal EX
command/event dispatcher (`0x08024d08`); it is not a direct GPIO or I²C
register read. No event-0x16 delivery occurs in the clean run. Consequently
the currently proven missing edge is an internal firmware event/state
transition, not another unmodeled external device reply. Do not inject event
0x16, force mode 1/2/3, or fabricate event 4 as a machine input. The next
analysis target is the producer of the dispatcher record consumed by
`0x08024d08`, with the external I²C/GPIO contracts held fixed.

### Configuration counterfactual check

The built-in profile's auxiliary view was temporarily filled with `0x01`
(`aux1-020323`) while keeping the same high GPIOD.13 and finite I²C inputs.
The run still reached event3, but `0x20014074..0x20014080` remained all zero,
`0x2001f5bf` remained zero, and no event-0x16 handler entry occurred. This
does not support the hypothesis that the missing mode publication is caused
by an erased auxiliary byte-fill. A temporary primary mode value of `1`
instead left the valid startup path before the trace point; it is not a valid
replacement for the proven profile value `5`. Both experiments were reverted
and are not model inputs.

## Current external-input boundary: ADC0 is proven; the next gap is a MAS message

The configured path's channel-0 wait is now covered by the built-in functional
input. Static code at `0x08026bfc` accepts the sampled halfword at
`0x2001f598+46` only when it is at least `3819` (and the associated state byte
is valid). The functional profile therefore supplies ADC1 channel 0 at the
finite raw count `4095`; this is a synthetic full-scale board-rail value, not a
claimed factory measurement.

With that input, a lightweight guest trace reaches the firmware-owned prefix:

```text
event 3 -> command 0x66 -> qualification handler
```

The state byte at `0x2001f5bf` nevertheless remains zero, so the timer path is
not armed and the snapshot/power builder is not called. The next candidate is
not another analog threshold: event `0x81` is dispatched by the table entry at
`0x08027eae`, and its only static producer is the incoming-message callback
`0x08025016 -> 0x08028b38`.

The corresponding message buffer at `0x20015e46` is cleared during EX startup
(`0x080038be`) and had no later guest write in the focused run. The EX command
queue repeatedly contained only internal command codes `1`, `2`, `3`, `5`,
`0x1d` and `7`; the incoming-message branch was never entered. This proves a
missing MAS-to-EX message on the current run, but not its payload or physical
cause. No event `0x81`, mode value, timer completion, or GUI-ready state is
therefore synthesized by the model.

## Static closure of the mode/event edge: an EX service notification is missing

The internal mode edge can now be placed one layer lower without inventing an
event. During EX initialization, `0x0802ea42` resolves service key `0x00010000`
and `0x0802ecec` registers callback `0x0802f485` (`0x0802f484` body) on the
notification object at `0x200140dc`, with a `0x003fffff` mask. Its bit dispatch
contains the power-relevant branch:

```text
notification bit 7 (mask 0x80)
  -> 0x0802ea70
  -> parse record 0x20014074
  -> type 0 / type 10 path
  -> 0x08024d08, enqueue dispatcher code 22 (0x16)
  -> 0x08024f88 -> 0x08028ae8
  -> write mode byte 0x2001f5bf and publish event 110
```

The static producer audit is decisive for the boundary: all direct local
notification sends (`0x080214fe`) whose target is this same object
`0x200140dc` use masks `1`, `0x1000`, `0x2000`, `0x4000`, `0x8000`,
`0x40000`, or `0x200000`; there is no local `0x80` send in the EX image. The
other service object at `0x200138f8` is active in the clean run and dispatches
`0x4000`, `0x20`, `0x800`, `0x1000`, `0x40`, `0x80000`, and `0x10000`, but that
does not deliver bit 7 to the power object. A focused runtime trace confirms
the power callback sees only `mask=0x1` while those adjacent service masks are
processed.

Therefore the remaining edge is a notification delivered into the EX power
service object from outside this local producer set (cross-module/peer/IRQ
path still to be identified). It is not justified to model it as event 0x16,
to force mode `1/2/3`, or to add another GPIO/ADC/I2C value. The next static
target is the producer of the service-key `0x00010000` notification or its
transport ingress; the existing finite board inputs remain held fixed.

## Source-graph closure of the bit-7 sender split

The fixed EX source graph at `nightglass-firmware` revision `27b40fdc7b`
separates the missing bit from the already observed local masks. The callback
at `0x0802f484` tests bit 7 at `0x0802f4b4` and dispatches
`0x0802ea70`; that handler uses object base `0x20014038`, so its input record
is `0x20014074`. The direct local sends to the power notification object
`0x200140dc` (`0x20014038 + 0xa4`) are:

```text
0x0802eb0a  mask 0x00002000
0x0802ee60  mask 0x00001000
0x0802f31e  mask 0x00004000
0x0802f3f0  mask 0x00008000
0x0802fb30  mask 0x00040000
```

There is no source call that sends `0x80` to this object. A separate source
site at `0x08033b1c` does send `0x80`, but its literal target is the adjacent
service object `0x200138f8`. Thus bit 7 is present in the EX image and belongs
to another service object; it is not a missing GPIO/ADC/I2C value and it is
not evidence that the power callback should be forced.

The service-record candidate is also concrete but not yet a producer proof:
startup `0x080341e8` resolves record index 7, calls initializer `0x080342d4`,
and registers the Thumb callback `0x0803459d` (body `0x0803459c`). The callback
collects its framework state and calls `0x0803423c`; the source graph does not
show that callback emitting bit 7 or populating `0x20014074`. The next decisive
probe is therefore the adjacent-object bit-7 callback and the cross-object /
peer / IRQ path that could deliver a notification into `0x200140dc`, together
with the first guest write to `0x20014074` and the call/return state around
`0x0803459c` and `0x0803423c`.

## Dynamic trace receipt: first writes and the remaining trigger

The dynamic receipts below need a build-provenance boundary. The older probe
scripts invoked `work/build/runtime/qemu-system-arm`, whose timestamp predates
the current machine source, and used a retired profile/property schema. The
current source has no startup profile selectors; the NOR protocol and complete
bundle composition are now the default paths. `frame-contract` remains an
explicit qtest-only aperture. A clean rebuild of
`work/build/qemu/qemu-system-arm` was required before rerunning the path. Those
older receipts remain historical observations, but they are not current-build
startup proof.

A current-build natural run using the accepted properties recorded repeated
`0x1020b86c` calls with `r0=0xb1d1a8a4`, `r1=0x64`, and `state=2`; the real TX
task later entered with `state=0`. The same run had no `0x102437f0` mode-0 hit
and no `0x2c0a0000`/`0x2c0a0100` write. This reproduces the scheduler/data-path
stop on the current binary without treating the stale dynamic receipts as a
current firmware run.

The historical ignored run `work/runs/ex-battery-runtime/trace-notif7/` used
an older functional bundle and descriptor with GDB breakpoints at
`0x080214fe`, `0x0802f484`, `0x0802ea70`, `0x08033b1c`, `0x0803459c`, and
`0x0803423c`. It watched byte writes at `0x20014074` and word writes at the
adjacent record/object fields. The first observed writes were startup clears at
`0x080038be`, followed by object initialization at `0x080422de`/`0x080422e2`:
`0x200140dc=0x12`, `0x200140e0=0x080214d9`, `0x200138f8=0x19`, and
`0x200138fc=0x080214d9`. There was no non-zero write to `0x20014074`.

The run entered `0x0802f484` for masks `0x1` and `0x10000`, entered
`0x0803459c` once, and repeatedly entered `0x0803423c`. It did not enter
`0x0802ea70` or the static bit-7 producer `0x08033b1c`; the generic notification
send breakpoint saw no `mask=0x80` send to either watched object. Therefore the
dynamic evidence closes the startup/record-initialization branch but does not
claim a runtime producer for `0x20014074`: the current functional input never
reaches the adjacent-service compare that guards `0x08033b1c`, and the external
service-record/peer/IRQ ingress remains the blocking edge.

A follow-up trace also covered the direct `[r4+0x3c]` stores in the EX service
status code (`0x0802aa8c`, `0x0802aab4`, `0x0802aa98`, and `0x0802aab8`) and
the service-record entry `0x0802ab20`. The only hit was the status entry
`0x0802aa30` with `r0/r4=0x2001fa50`; its `+0x3c` field is therefore
`0x2001fa8c`, in the qualification table, not the power record base
`0x20014038`. None of those direct stores or `0x0802ab20` reached
`0x20014074`. This eliminates the adjacent qualification writer as the current
bit-7 producer and leaves the MAS-to-EX receive/record ingress as the next
addressable boundary.

The transport trace then watched the EX RX DMA and both MAS SSI data registers.
EX arms DMA at `0x080425a4` with `NDTR=0x100`, source peripheral
`0x4001300c`, destination `0x20017b76`, and count `0x100`; the only later
`NDTR` transition was the reset-to-zero write at `0x080434a4`. There was no
`NDTR` decrement, no non-zero receive-buffer write, and no guest data write to
MAS SSI0 `0x2c0a0000` or SSI1 `0x2c0a0100`. MAS did reach a higher-level client
receiver for tag `0x10`, but no 112-byte client-4 frame callback was observed.

The complete reachable EX `[r4+0x3c]` writer set was also instrumented for
address ownership. It produced many unrelated targets (`0x20017cc4`,
`0x2001fedc`, `0x2001fe7c`, `0x2001f3bc`, and others), but no
`r4=0x20014038` and no target `0x20014074`. The original transport watchpoint
run cannot be used to claim that EX never started: GDB hit an internal error
after downgrading the receive-buffer watchpoint at `0x20017b76`, and its log
stopped before the service-start chain.

A follow-up probe without RAM watchpoints captured the actual EX start chain in
`work/runs/ex-battery-runtime/ex-start-probe/gdb.log`. It saw the power
completion path (`0x0803813a -> 0x08037eac -> 0x08021418 -> 0x0802138a`),
then `0x08034210`/`0x0803422a` with `r0=1`, followed by RX start
`0x08034580`, TX start `0x08034a04`, RX worker `0x0803459c`, and TX worker
`0x08034a18`. The earlier apparent absence was therefore a probe failure, not
an EX task gate.

The MAS-side task gate is now identified. At constructor `0x1020b980`, the
runtime state bytes are `0xb1808958=0`, `0xb180895a=0`, and `+4=1`. RX task
`0x1020b65a` gates its path on the second byte; TX task `0x1020b7ba` gates its
path on the first. The live task code sends a readiness predicate through
`0x10385d76`; a failed predicate returns the state to zero, while a successful
predicate briefly advances it to state two before the cleanup path runs.
Watchpoints on both state bytes show repeated `0 -> 1 -> 2 -> 1 -> 0` cycles
from the MAS state handlers (`0x1020b83a/0x1020b854/0x1020b8a6/0x1020b8fa`
and `0x1020b69a/0x1020b6f2/0x1020b720`). Neither SSI data register is written
during those cycles. The missing prerequisite is therefore the MAS SSI
readiness/transport handshake that should stabilize these task states, before
any master FIFO producer can clock EX.

The readiness predicate's input object is now concrete. A one-shot live probe
at the gate call (`0x1020b842`, LR `0x1020b82f`) reported `r4=0xb1808958`
and `r0=0xb180376c`. The object begins
`{0x56, 0x09, 0x04282014, 0x04282114, ...}`: its `+8` pointer is the MAS
GPIO5 PDR at `0x04282014`, and its `+4` byte selects bit `9`. The predicate
therefore evaluates `(*(uint32_t *)0x04282014 >> 9) & 1`; the observed word
was zero, so this gate correctly returned not-ready. This is a GPIO input
object, not a service-record or MAS-RAM status slot.

The physical/software producer is the EX peer. The board wiring connects EX
GPIOB14 (`0x40020400`) to MAS GPIO5 bit9; the EX RX worker at `0x0803459c`
arms the 256-byte DMA and raises B14 on its receive-armed path (the transition
is immediately before its `0x080345e4` continuation), providing the SSI0
side's EX-RX-ready signal. The paired SSI1 side uses EX TX worker
`0x08034a18`, which raises E3 through the `0x08043ade` GPIO helper at
`0x08034afe`; EX GPIOE3 (`0x40021000`) drives MAS GPIO5 bit10/EXIU3. It
lowers E3 at `0x08034b1a` after the real DMA wait.
The same probe captured the guest-owned producers. RX entered `0x080345e4` with
`GPIOB14 base=0x40020400`, mask `0x4000`, and DMA stream `0x40026410`, proving
the B14/SSI0-ready write. TX called dequeue `0x08034678` with queue state
`0x20021860`; its registered callback `0x08031f28` returned the real message
path, then `0x08034abe` submitted `0x100` bytes from `0x20017d9a` and
`0x08034afe` raised E3 (`GPIOE base=0x40021000`, mask `0x8`). E3 was released
at `0x08034b1a` after the DMA wait. EX start/dequeue and both B14/E3 producers
are therefore proven active. The unresolved edge is now on the MAS side: no
SSI0/1 master clock/FIFO write was observed, so the MAS-to-EX transaction still
does not feed the armed EX endpoints.

The post-arm MAS trace closes the immediate readiness question. With B14 high,
the `0xb180376c` object returned bit9=1; the paired object `0xb18037a4`
returned bit10=1 from the same GPIO5 PDR (`0x601` has both bits set). The gate
returned `1`, and a state watch caught `0xb1808958` advancing to state `2` at
`0x1020b854`; the existing state receipt then shows the normal `2 -> 1 -> 0`
cleanup. FIFO watchpoints at `0x2c0a0000` and `0x2c0a0100` remained silent
through the companion 30-second run.

The reason the successful readiness return still produces no master write is
visible in the live task body. After the state-2 setup, `0x1020b88e` loads the
callback field at `0xb1808974` (`r4+0x1c`) and branches around the event-6
emission when it is null. The post-arm gate trace sampled `cb=0`; therefore the
task performs readiness setup, skips `0x1020b60e`'s event path, and falls back
to state 1 without reaching an SSI FIFO producer. The next MAS-side boundary is
the TX task writer at `0x1020b7c8`; the next MAS-side boundary is why that
registered task misses the state-2 window, not the EX B14/E3 handshake.

A dedicated watch on `0xb1808974` ran through the EX start, repeated B14/E3
arming, and TX DMA submissions without a single write. The reachable
registration chain is `EXPRTSND/EXPRTRCV` descriptors at `0xb18abfac` and
`0xb18abfc8`, flags `0x60000013`, then `0x1020148c -> 0x1020b93e` from LR
`0x10200729`. The entry receives `r3=0`, but that argument is not the callback
setter: `0x103bf1ec` ignores incoming `r3` and stores its `r1` argument into
the registered object at `r0+4`.

The direct `caller-10200729` receipt captured `r0=0xb18abfac`,
`r1=0xb18abfc8`, `r2=0x60000013`, `r3=0`, and `lr=0x10200729` at
`0x1020b93e`. The preceding startup sequence at `0x102006e0` is only
`BL 0x10201488; LDR r1,[r0]; MOVS r0,#1; BLX r1`; it does not write a
callback argument into `r3`. `0x10201488` returns `0xb18089cc`, while method
`0x1020148c` fetches records 94/95 and calls `0x1020b93e`. The observed zero
is therefore an inherited register value at the vtable callsite, not a hidden
callback parameter supplied by the descriptor provider.

IDA now resolves the upstream method precisely. The vtable method
`0x1020148c` is entered from the call at `0x10200726`, which explains the
observed LR `0x10200729`; when called with `a1=1` it obtains resource records
94 and 95 through `0x103d3dea` (`table_base[0] + 28*index`) and tail-calls
`0x1020b93e(r0, r1)`. That function has only two explicit arguments. It uses
`r3` for the preceding `0x103606b6` descriptor setup, but leaves `r3`
unchanged before both `0x103bf1ec` calls. The observed `r3=0` is therefore an
inherited register value, not a supplied callback parameter. The actual state
field writer is the later TX task body, not this registration helper.

The actual registration calls observed for the MAS tasks were:

| helper | object | r1 stored as task callback | r2 | r3 |
|---|---:|---:|---:|---:|
| `0x103bf1ec` | `0xb1d1a8a4` | `0x1020b7bb` (TX task) | `0x1020ba14` | `0` |
| `0x103bf1ec` | `0xb1d1ab08` | `0x1020b65b` (RX task) | `0x1020ba28` | `0` |

The other `0x103bf1ec` callers are generic object registrars, not evidence that
the MAS state callback is being filled. For example, source-backed caller
`0x1021f140` invokes the helper three times on `0xb21507c0`, `+0xc`, and
`+0x18`, registering callbacks `0x1021e809`, `0x1021ecdf`, and `0x1021ece7`.
Those objects are outside the `0xb1808958` state pair. This explains why the
wide caller set contains many `r3=0` registrations without producing a write
to `0xb1808974`.

Both task entry callbacks are registered, while the separate state callback
field `0xb1808974` remains zero because the TX task is not entering during the
state-2 window. `0x1020b7ba` writes `b1808974=1` only when
`b1808958==2`; `0x1020b7e8` then consumes and clears that field before queuing
event 6. A 30-second dynamic window saw eight state-2 stores with `cb=0` and
no task entry in those windows; the TX task entered once from the kernel
trampoline (`lr=0x1011ba00`) only after state had returned to 0. The blocker is
therefore scheduler/readiness timing, not an unknown callback writer.

The registration and wakeup boundary is now explicit. `0x1020b93e` first
creates the MAS event object at `0xb1808964` through `0x10361082`, then calls
`0x103bf1ec` for the TX and RX task descriptors. That helper packages the
object pointer and callback into an SVC-6 request (`R12=0x80580100`), stores
the returned task handle at the descriptor base, and leaves the callback at
`+4`. After readiness, `0x1020b85a` sets the event bit through
`0x103610d2`, issues the TX-handle scheduler operation through `0x103bf224`
(SVC-6 `R12=0x805b0100`), and applies the task delay/scheduling parameter
through `0x103bf214` (SVC-6 `R12=0x805a0200`). Source-backed callers use this
second argument as elapsed-time milliseconds, not a priority level. The state loop's
`0x10361116` wait is another SVC-6 wrapper (`R12=0x80300500`), while
`0x103dc538(5)` is a separate SVC thunk. The MAS code therefore relies on the
kernel scheduler/flag wakeup to enter `0x1020b7ba`; it does not call that task
pointer directly. The task entry is present but arrives after the state-2
window, localizing the next gap to SVC-managed dispatch timing and flag
wakeup.

The transport callbacks are a separate, working chain. The same
`0x1020b93e` call builds worker descriptors with `0x103606b6`, assigning
`0x1020b90a` (TX) and `0x1020b78e` (RX) as their callbacks. The worker loop at
`0x10360698` waits through `0x103610f8` and invokes the stored callback with
the returned event mask. `0x1020b90a` fans masks 2/4/8 into
`0x1020b7e8`, which is why the state-2 transition and EX transport activity
are observed. This proves the EX-to-MAS transport callback path is alive; the
remaining missing edge is specifically the later SVC task wakeup that should
enter `0x1020b7ba`.

A counterfactual branch check separates event-6 from the SSI producer. An
ignored GDB probe set `0xb1808974=1` only at the state-2 check before
`0x1020b88e`, solely to exercise the existing event-6 branch. It produced
repeated `0x1020b60e` records with `r0=6`, while both `0x2c0a0000` and
`0x2c0a0100` watchpoints stayed silent. This is not a firmware input or a
registrant; it only proves event-6 enqueues a record and does not directly
clock either SSI FIFO. The legal producer path remains the missing SVC task
wakeup into `0x1020b7ba`.

A stronger scheduler-substitution probe exercised the registered task body
itself. At the real state-2 callback check (`0x1020b88a`), the probe redirected
PC to the registered Thumb entry `0x1020b7bb` and returned to the original
handler. The firmware then emitted event 5 (`0x1020b60e(r0=5)`) from the task,
followed by event 6 (`0x1020b60e(r0=6)`) from `0x1020b7e8`; `b1808974` was
set and then consumed as expected. Both SSI watchpoints remained silent. This
executes the real task and event-6 code, but substitutes for the missing SVC
context switch, so it proves the event-6 downstream behavior without claiming
that the scheduler wakeup is fixed.

The scheduler timing was isolated with one additional perturbation. The
original state handler passes delay parameter `100` to the `0x805a0200` service.
With only that GDB argument changed to `0`, the real SVC-created task entered
from its kernel trampoline while `state=2` and executed the real writer at
`0x1020b7c8`; the SSI0/SSI1 watchpoints still stayed silent. This identifies
the original failure as task-delay/wakeup timing, while keeping the delay change
explicitly outside the firmware evidence.

IDA closes the SVC wrapper semantics behind that timing. `0x103bf1ec` reaches
the `0x80580100` task-create SVC, `0x103610d2` reaches `0x802f0200` event-set,
`0x103bf224` reaches `0x805b0100` task resume, `0x103bf214` reaches
`0x805a0200` task delay, and `0x10361116` reaches `0x80300500` event wait.
The source-backed caller `0x1021f240` resumes a task and then passes
`(elapsed-26000)/1000` to the delay wrapper, confirming that the second
argument is elapsed milliseconds. MAS's `0x1020b86c` therefore puts the TX
task into a 100 ms scheduling delay after its state-0 setup; the worker's
state-2 transition can complete before that task is eligible to run.

The legal producer question remains negative even after that scheduling edge is
removed counterfactually. The unmodified run records eight state-2 stores with
no TX task entry (`mas-task-window/gdb.log`). A separate probe changed only
the delay argument at `0x1020b86c` to zero; the real task entered at state 2,
wrote `b1808974` at `0x1020b7c8`, and executed the existing event-6 path, but
both `0x2c0a0000` and `0x2c0a0100` watchpoints stayed silent
(`mas-priority-fifo/gdb.log`). The event-6/task-body probe likewise saw event
codes 5 and 6 without an SSI write. This separates the scheduler miss from
the still-missing guest mode-0 data producer; changing the delay is not a
firmware receipt and cannot justify a QEMU event or FIFO injection.

The timer-cadence question is narrower than the SVC delay value. IDA shows
`0x805a0200` as a kernel scheduling service; the wrapper does not read the
architectural counter or program a Generic Timer register. The current board
contract sets `CNTFRQ=48 MHz`, wires secure physical timer PPI29 through the
upstream GIC, and has read-only receipts with real PPI29 IAR/EOI cycles. That
is evidence that the timer/IRQ plumbing is alive, but it does not establish
the RTOS tick conversion used by this task or prove that QEMU stretches the
100 ms delay. The only direct causal evidence remains the guest's delay
argument and the zero-delay counterfactual; a guest tick-count receipt around
the SVC service would be required before changing the Generic Timer model.

The IDA MCP timer audit now closes the guest-side millisecond base. The
`0x1002012c` initializer writes the external counter frequency as 48 MHz;
`0x100200d4` writes `CNTFRQ=0x2dc6c00`, clears the architectural timer state,
and `0x10020234` starts `CNTP_TVAL=0xbb80` (48000 ticks). The reload helper
`0x10020260` writes the same 48000 value, while `0x100202bc` reads the timer
value and returns `(48000 - TVAL) * 1000 / 48`. That elapsed-millisecond
conversion is consumed by `0x10119f54` and `0x10119ffc`. It matches QEMU's
48 MHz contract and gives a source-backed 1 ms guest time base, but the
`0x805a0200` SVC wrapper does not call this converter or read the counter. A
natural receipt tying the MAS delay to successive timer values is still needed
before attributing the 100 ms window to a QEMU cadence error.

The natural mode-0 search remains empty: the standalone
`0x102437f0` watch again recorded zero hits over a ten-second natural run, and
the direct-xref audit still has no
mode-0 caller bound to channel 0 or 1. The zero-delay run executes the real TX
task but still produces no SSI0/1 write. Thus the current QEMU hardware model
has no source-backed reason to clock MAS SSI0/1 or to add a peer-side producer.

The unit interpretation is backed by an unrelated source-backed caller at
`0x1021f240`: it resumes one task with `0x103bf224`, then calls
`0x103bf214` with `(elapsed-26000)/1000`; for a second task it passes the
remaining elapsed milliseconds divided by `1000`. This is the same SVC pair and
rules out treating MAS's `100` as a priority field.

The device-side SPI contract closes the FIFO boundary independently. FIFO
progress comes from guest word accesses and the common guest ISR; the host does
not write software objects, enable bits or event flags. Since `0x1020b60e`
only appends the event record and the verified event-6 path produced no
`0x2c0a0000/0x2c0a0100` access, event-6 is not an SSI producer. Any eventual
master clock must come from a later guest MMIO transaction after the MAS
task/delay boundary.

## Static and dynamic SSI producer boundary

IDA MCP closes the guest-side register path. The common driver entry
`0x10243468` dispatches on command byte `[a2+0x10]`; its mode-0 branch calls
`0x102437f0`. That engine reaches `0x102438d0` for TX data-register writes and
`0x1024320c` for RX data-register reads; both use the device pointer at
`[a1+0x1c]`, so a real mode-0 command can reach a guest SSI data register.
This is the genuine firmware MMIO producer shape, rather than an event-ring
side effect.

The MAS state task does not construct that mode. `0x1020b7e8` prepares the
command object at `0xb1d1aac4`, stores `mode=1` at `0x1020b884`, and calls
`0x10370a14`; the RX task at `0x1020b686` follows the same mode-1 setup path.
The mode-1 branch does not enter the mode-0 engine; the loaded MAS IDB instead
dispatches it to `0x10243380`, a synchronous transfer loop that calls
`0x1024325c`. That helper's TX branch writes through the device pointer at
`[a1+0x1c]` to the SSI data register and tests `[a1+0x1c]+0x14` for
TX-FIFO-full. Mode 1
therefore has its own data-register producer; the earlier wording that called
it configuration-only is incomplete. The event-6 call at `0x1020b89e` is
downstream of the mode-1 call and is not itself the producer. This is a static
decompile observation; no runtime mode-1 hit has been recorded yet.

The source-backed generic data sender `0x10367fe0` does build a mode-0 command
and calls `0x10370a14`, so the image contains a legal data path. Its selector is
initialized by `0x10367ec0` as
`b18a7808 = sub_10368b0a(7)`. The new unmodified receipt
(`work/runs/ex-battery-runtime/mas-channel-receipt/gdb.log`) reads that selector
as channel `8`, i.e. object `0x2c0a0800`; it also reads MAS TX channel `0` and
RX channel `1`, with every observed MAS command still `mode=1`. The earlier
`ssi-api-probe` channel-6 calls belong to another configuration object and are
not this generic sender. Across these receipts there is no mode-0 call or
write to `0x2c0a0000/0x2c0a0100`.

The mode-0 producer question is therefore closed for the known direct-call
set: the generic path is bound to SSI8. The MAS power task reaches SSI0/1
through the mode-1 transfer engine, which is itself a direct data-register
producer. Event-6 cannot supply a mode-0 transfer. The remaining question is
why the mode-1 path produces no observed SSI0/1 write; it is not evidence of
an omitted direct call. The next probe must break at `0x1020b88a` (mode-1
call) and `0x10243380`, then watch the device pointer's data register.

The direct-xref audit remains valid for the mode-0 set. `0x10370a14` has nine
direct callers. The MAS RX/TX callers are the only channel-1/channel-0 pair
and both pass mode 1. The mode-0 callers `0x10367fe0` and `0x10368096` use the
selector initialized as resource 7 (runtime channel 8); the other callers use
mode 1/2 on resources 2, 4, 6 or 8, or a separate mode-1 object. No direct
caller combines mode 0 with channel 0 or 1. The mode-1 callers are themselves
direct FIFO producers, so the missing SSI0/1 write must be explained by
whether `0x1020b88a`/`0x10243380` execute and reach `0x1024325c`, not by an
untraced mode-0 caller.

A minimal unmodified runtime probe placed only one breakpoint at
`0x102437f0`. Over its ten-second window the target exited normally and the
mode-0 engine had zero hits (`work/runs/ex-battery-runtime/ssi-mode0-watch/gdb.log`).
This independently confirms that the absence of an SSI0/1 mode-0 hit is not a
side effect of the higher-volume configuration probes.

The remaining BLX and function-table audit does not reopen that boundary. The
two MAS task callbacks are ordinary transport notifications: `0x1020b7e8`
loads `b1808968` and calls it at `0x1020b80a` before the TX mode-1 setup;
`0x1020b686` loads `b1808988` and calls it at `0x1020b76c` only after the RX
path has accepted a completed frame. `sub_1020555a` registers
`sub_102054ee` into `b1808968` through `sub_1020bc82`, and registers
`sub_102053e4` into `b1808988` through `sub_1020bc88`. The first callback only
refreshes the selected external provider state (`sub_10205640` /
`sub_1020545c`); the second copies the received record in 240-byte chunks and
calls `sub_102055e6`. Neither callback calls `sub_10370a14`, writes a
`0x2c0a0000/0x2c0a0100` data register, or raises an SSI completion.

The callback table at `b1887bf0` is used by those record/transport helpers
(`sub_102054fe` and `sub_102055e6`). IDA's data-reference search found no
table entry pointing at `0x10370a14`, `0x10243468`, or `0x102437f0`; the
only references to `0x10370a14` remain its nine direct callers. The common ISR
`0x102435ec` likewise has no static call reference: it snapshots and masks
the peripheral pending word, then routes callback types 1/2/4 through
`0x103610cc` or drains RX data through `0x1024325c`. It never writes TX FIFO
data. The SSI initializers (`0x1020b980` -> `0x103706c4` -> `0x10243130`)
only copy the source configuration into the MMIO register object and enable
the IRQ mask; they do not construct a producer. This closes the indirect
BLX/IRQ/function-table path for SSI0/1.

The minimum source-backed QEMU boundary is therefore the existing finite EX
SSI peripheral: `z7ii_attach_ex()` attaches MAS `spi0`/`spi1` to EX SPI1 RX
channel 0 and SPI4 TX channel 3 via `nikon_ex_spi_slave()`. It consumes only
actual guest master words and returns only the EX device's current response;
it does not write MAS software objects, readiness flags, events or IRQs. The
focused `/nikon/ex/spi-word` fixture exercises the same 16-bit, LSB-first
master-to-8-bit-slave conversion and real EX DMA path. No additional host
producer is source-backed until an original guest write to SSI0/1 is observed.

This also orders the EX service-record boundary. RX worker `0x0803459c` and
validator `0x0803423c` accept a service record only after a 256-byte frame
passes its length, sequence and checksum checks; the callback then reports the
validated buffer. The current run can enter the validator while the DMA window
is empty, but it never obtains a valid frame and never reaches a record
producer. Therefore the unresolved service-record ingress is downstream of
the missing SSI0 mode-0 data transfer, rather than an independent board input
that can be filled in safely.

The adjacent helpers on this provider path do not fill that field either. The
source-backed implementation of `0x103606b6` copies its inputs and stores the
descriptor pair at object offsets `+0x24/+0x28`, then calls `0x10361082` with
the object base plus `0x20`; `0x10361082` writes its allocator result to that
passed object's first word. Neither helper writes a structure `+0x1c` callback.
This removes the descriptor/resource setup branch as the missing state-callback
registrant; the remaining writer must be a separate registration path.

The source-backed `0x102xxx` `+0x1c` writer set narrows to four unrelated
static shapes after the exact task writer was recovered. `0x10208abc` stores
incoming `r1` to `[r0+0x1c]`, then sets `[r0+0x10]=2`; its sibling
`0x10208a3a` stores the input at `+0x18` and clears `+0x10`. Both use the
`0xb1806654` lookup table and `0x2c01500c` resource, not the EXPR task
descriptors. The dedicated runtime watch did not hit this static function, so
it is not the current MAS state producer.

A focused candidate run then broke on all five nearby entries
(`0x10208abc`, `0x10208a3a`, `0x10209f4a`, `0x10240ed4`, and `0x102c9c8e`)
through EX start, repeated B14/TX DMA activity, and the normal MAS readiness
cycle. None executed, and `0xb1808974` remained unwritten. These static
`+0x1c` writers are therefore outside the currently reached producer path; the
remaining gap is dispatch into the registered MAS TX task.

The other nearby writers are record/housekeeping paths: `0x10209f4a` copies a
source record field at `+0x120` into a destination `+0x1c` alongside fields
`+0x18/+0x20/+0x24/+0x28/+0x2c`; `0x10240ed4` writes a halfword into an output
record at `+0x1c`; and `0x102c9c8e` increments a resource counter at `+0x1c`.
They are not evidence of the MAS state callback. The next IDA step is bounded
caller analysis for the SVC-managed scheduler/transport dispatch that should
enter `0x1020b7ba`, with event-flag wakeup and returned task handles as the
next dynamic checkpoints.

## Static qualification state: what is and is not an external input

The state calculator at `0x0802866c` does not read a new GPIO or ADC register.
It reads the first qualification record in the EX table at `0x2001fa90` and
uses its associated status fields to return state `1`, `2`, or `3`. The setter
at `0x0802937c` stores that result in `0x2001fbae`, which is the value consumed
by the snapshot builder at `0x080278fc` and ultimately report byte `61`.

The record fields are populated by the EX service-record path (`0x0802ab20`)
from an incoming 64-byte record; they are not constants that the machine can
legitimately write into EX RAM. Static analysis therefore narrows the missing
boundary to the producer of that service record/notification. It does not yet
identify a new physical pin or a particular I2C byte. Until that producer is
identified, changing the finite board profile would be guessing and would
duplicate firmware state transitions.

The current functional smoke run also retains the separately documented early
COPY fault (`source=0x107a4f34`, destination `0x00000000`). That loader/startup
fault is independent of the EX power qualification edge and must not be used as
evidence for a missing power input.

## Current bundle stop: EX 0x4f register 0x23 readback

A QMP `info registers -a` sample on the current bundle binary after 80 seconds
of host execution shows MAS CPU0/CPU1 in the kernel idle loop
(`0x10107090`/`0x10107094`) and the SLA CPUs at
`0x10038a48`/`0x10038a64`. The EX CPU is not idle: it returns to
`0x08047596` inside `sub_8047584`, the I2C SR1 wait for RXNE (bit 6) or
STOPF (bit 4).

A hardware-breakpoint probe at `sub_80442c4`
(`work/runs/ex-i2c-entry/gdb.log`) records the active transfer as
`a2=0x9e` (0x4f write address), `a3=0x23`, `a4=1`, `a5=0x20008928`,
`a6=1`, `a7=1000`, called from `sub_80387e8` at `0x08038857`. The same probe
shows the 0x22 three-byte read from `sub_802fdf0`; the 0x6b/0x0d setup is not
reached. A second probe at `sub_8047584`
(`work/runs/ex-i2c-wait/gdb.log`) records `base=0x40005400`,
`buf=0x20008928/0x20008929`, `count=1`, and `SR1=0` on every sample.

`sub_80387e8` writes the expected value to 0x4f register `0x23` with
`sub_804472c`, then reads it back three times. The read side was the blocker:
the firmware saw neither RXNE nor STOPF. A `hbreak` at `0x08047192`
(`work/runs/ex-i2c-sr1b/gdb.log`) records SR1 `0x46` (ADDR+RXNE+BTF)
immediately after both the 0x22 (`0x45`) and 0x4f (`0x9f`) read addresses.
The HAL's ADDR wait then saw `0x44`, but the subsequent RXNE/STOPF wait saw
`0`. The receive byte was filled and then cleared between the address and the
RXNE wait.

### Fix and new stop

`ex_peripheral_write` now treats a CR1 STOP as an observable completion: it
preserves RXNE/BTF and the received byte, sets STOPF, and clears ADDR; the
last DR read retires RXNE/BTF. The native
`/arm/nikon/ex/stop-preserves-rx` fixture covers the STOP-before-DR-read
sequence. With that change the 0x4f readback completes.

A hardware-breakpoint probe on the new binary
(`work/runs/ex-i2c-fixed/gdb.log`, `work/runs/ex-setup-ret/gdb.log`,
`work/runs/ex-setup-stop/gdb.log`) records `SETUP_B40C` at `0x0802b40c` with
descriptor bytes `0,0,1,1,0,0`, `sub_802b014` mode 0 write `0` repeated, the
0x6b write returning `0`, and the STOP/wait returning `0`. No `READ_RET` at
`0x0802b074` and no compare at `0x0802b0d6` are reached.

The 0x6b **read** (`sub_802ae48`) is the current stop. A hardware-breakpoint
probe (`work/runs/ex-6b-read/gdb.log`, `work/runs/ex-6b-ret/gdb.log`,
`work/runs/ex-6b-sr2/gdb.log`, `work/runs/ex-6b-464c/gdb.log`) records the
read entry with handle `0x2001fdcc`, base `0x40005400`, SR2 `0`, state
`0x20`; `sub_804464c` is entered with the same SR2/state, runs its post-loop
setup, writes CR1 ACK/START, and does not return to `0x0802ae78` within the
bounded window. No `0xd7` read address appears.

The active RAM vector table (`VTOR=0x20000000`) maps IRQ31/33 to
`0x080201fd`; the dispatcher `sub_80201fc` calls the table at `0x20022d70`,
and the IRQ31 entry `0x08041065` tail-calls the callback `0x0802fce1` ->
`sub_802fce0`. That callback processes the handle with `sub_8043bd8` and
signals `0x2001fe9c` only when the handle state is `32`; otherwise it
returns while the IRQ remains asserted. The callback leaves STOPF set
(SR1 `0x10`); the model now clears ADDR/STOPF on the SR1-then-SR2 read, and
`/arm/nikon/ex/stop-preserves-rx` covers it. A follow-up probe records
`WAIT_ENTRY` at `sub_802b628` (`r0=3`, `r1=1`) eight times, so the setup does
reach the read wait; the missing `0x0802ae78` is a probe limitation. The
traced IRQ31 callback uses handle `0x2001fea0` and event `0x2001fef4`, while
the 0x6b read waits on object `0x2001fdc0` (handle `0x2001fdcc`). The 0x6b
completion event is not that traced callback. The 0x6b driver callbacks are
`sub_802af08`/`sub_802aee8`, registered by `sub_802b128` on the handle
`0x2001fdcc`. `sub_802af08` runs `sub_8043bd8`/`sub_8043e38` and, when the HAL
state is `32`, calls `sub_8021556(0x2001fdc0)` (event id `0x25`); the probe
observes `DRV_READY`/`DRV_SIGNAL`. The read wait
`sub_802b628(0x2001fdc0, 3, 1)` still does not return to the compare, so the
next target is the RTOS event wait (`sub_804d698`) mask/bit and its one-tick
timeout, not the monitor. The setup never reaches mode 1/3 or the readback
equality; the 0x0d/SSI frontier remains downstream. No machine input or forced
status is added.

## 0x6b read: where the original execution actually stops

2026-10-19. New disassembly of the original EX image fixes three prior
misreadings and moves the 0x6b frontier from "the RTOS wait does not return" to
"the receive path takes the BTF branch".

Static facts (read from the original bytes, load base `0x08000000`, container
offset `0xd0`, length `0x80002`):

- `sub_802AE48` is the 0x6b read helper. It clears flag bits 0/1, calls the
  read-start routine at `0x0802AE74`, and only on a zero return executes the
  event wait at `0x0802AE86`. A non-zero start returns immediately through
  `sub_802B5F4` without ever waiting.
- `sub_804464C` (read) and `sub_80449B4` (write) contain no `BL`/`BLX`. They
  validate the descriptor state byte (`+0x3d == 0x20`), poll `SR2` bit1 for a
  free bus, program `CR1` (`PE`, `POS`, `ACK`/`START`) and `CR2` interrupt
  enables, then return. They never touch `SR1` or `DR`.
- Byte movement is interrupt driven. `sub_802B128` installs the EV handler
  `sub_802AF08` and the ER handler `sub_802AEE8` through the software interrupt
  numbers at `0x2001FDA4`/`0x2001FDA5`; the EV handler drives `sub_8043BD8`,
  which reads `SR1`/`SR2` and dispatches on the phase byte `+0x3e`.
- `sub_802B014` retries three times. Success requires masked equality between
  the written byte and the read-back byte plus the range gate (`<= 0xa0` for
  command 0, `& 0x3f <= 0x18` for 1, `& 0x1f <= 0x19` for 3).
- The receive completion path is `sub_8046CD0` (RXNE with BTF clear): it reads
  `DR`, clears the interrupt enables and sets state `0x20`, which lets
  `sub_802AF08` signal flag bit0. The RXNE-with-BTF path `sub_8046BFE` leaves
  state `0x22`/phase `0x40` for a one-byte request, and the following STOPF is
  not handled in that phase, so the request can never complete.

Measured on the complete bundle (`0x6b` wire address `0xd6`, register `0`,
one byte, timeout argument `1`):

- the read start is entered with descriptor `0x2001fdcc` and reaches its
  bus-free exit `0x080446A0` (`6` hits in 240 s), so the bus is not stuck;
- the RXNE handler takes `0x08046BFE` six times for descriptor `0x2001fdcc`
  and never `0x08046CD0`, i.e. `SR1` carries RXNE together with BTF;
- event waits on the flag object at `0x2001FDC0` (id `0x25`, `waiptn=3`)
  return `E_OK` immediately (`0x080215D4`, `r0=0`), so the RTOS wait is not the
  blocker; a previous probe that saw no read-side wait call was observing the
  write helper's wait site `0x0802AEDA`, not `0x0802AE86`;
- the RTOS tick word at `0x20022D58` stays near `0x1ee` while the retry loop
  runs, so guest time is not advancing during the loop. This is a separate
  time-base observation, not a completion signal.

The RTOS wrappers match the local T-Kernel 1.00.01 sources: `sub_804D698` is
`_tk_wai_flg`, `sub_804CFFC` `_tk_set_flg`, `sub_804BE34` `_tk_clr_flg`,
`sub_804AD00` `eventflag_cond`, `sub_804AD90` `gcb_make_wait_with_diswai`. The
timeout argument is in milliseconds (`TMO_POL=0`, `TMO_FEVR=-1`), the wait mode
is a hardcoded `TWF_ORW` inside `sub_80215BC`, bit0 means completion, bit1 means
error, and `sub_802B628` maps them to `0`/`1`, with `3` for a failed wait. The
flag id is not a literal: `sub_8021514` stores the id returned by the T-Kernel
object allocator, so `0x25` depends on the boot allocation order.

Model boundary, now resolved: the controller used to feed the receive FIFO at
address match, so a one-byte reader saw RXNE with BTF and the firmware never
reached its completion path. Retiring that speculative byte at the `ACK` write
was tried and reverted, because the original guest then stopped before the 0x6b
traffic. The accepted change (PR #115) times the shift byte instead: one byte
at address match, the acknowledged byte after nine `CCR`-scaled periods of the
APB1 clock, and cancellation on a cleared ACK, STOP, START or reset.

With that model the original bundle reaches, for the first time, the read
helper's event wait `0x0802AE86`, the setup comparison `0x0802B0D6` and the
success exit `0x0802B0E4` for commands 0, 1 and 3 and for later repeats in a
300 s window. The read-back equality path therefore runs without a fabricated
value. `/nikon/ex/i2c-single-byte-read` records the controller-side sequence:
`SR1` `0x42`, then `0x40`, then RXNE without BTF across a stepped clock. The
same probe also reaches the periodic `0x0d` status service at `0x0802B6F0`, so
the monitor lane now runs its setup and its status cycle on the original
image. The structured contract is in
[the monitor page](../devices/expower/protocol.md).

## MAS mode-1 producer: retire the watchpoint evidence

The "no `0x2c0a0000`/`0x1024...` FIFO write" item is an instrumentation
artifact. `0x10370A14` has nine code callers and no data references; the mode-1
engine `0x10243380` pumps through `0x1024325C`, and `0x102436EC` (called from
the RX branch) is a second writer to the same `device+0x00` data register with
the same `SR` bit4 gate. Existing receipts under `work/runs/ex-battery-runtime/`
record the worker callback storing state `2->1` with `lr=0x10370a7b`, the
channel-1 context stepping `tx_index` `0 -> 32 -> ... -> 128`, and the final
object at `tx_index=128/128` with a shifting model FIFO. Fifteen probe scripts
set MMIO watchpoints on `0x2c0a0000`/`0x2c0a0100` and none reported a hit,
although channel 1 provably receives 128 pushes per transfer; MMIO data
watchpoints in this gdbstub are the unreliable instrument, so those silent
receipts no longer count as evidence. Channel 0 has still never been measured
with a trustworthy probe; read the context object `0xb1802090` instead.

## Monitor lane: what the image does, after the structure pass

The `0x6b` lane is a leaf sink. The IMAGE's own command space is small and now
recorded in [the monitor contract](../devices/expower/protocol.md):

- **17 wire sites, all one byte.** A bounded decode over Code
  `0x08000000..0x08080000` finds exactly 17 `0xD6` immediates, in six functions
  (`sub_802AD0C`, `sub_802ADAC`, `sub_802AF38`, `sub_802B014`, `sub_802B234`,
  `sub_802B40C`). Every site passes length 1 and timeout 1. There is no `0xD7`
  in this lane: the receive direction comes from the `0x400` control bit and
  the interrupt handler sets bit0 of the address byte.
- **Two committers, two orders.** `sub_802B234` writes `6,0,1,2,3,4,5,7`;
  `sub_802B40C` writes `0,1,2,3,4,5,7,6`. Payloads live at `0x2001fda6+cmd` and
  are seeded `00 80 a0 00 00 8d 33 00 cd 06`.
- **Only `0`, `1`, `3` are verified**, with masks `0xff`/`0x3f`/`0x1f` and gates
  `<=0xa0`/`<=0x18`/`<=0x19`. `0x02`, `0x04`, `0x05`, `0x06`, `0x07` are
  write-only; `0x0b`, `0x10`, `0x11` and (as status) `0x01` are read-only.
- **A persistent mismatch is not an abort.** After three attempts the firmware
  writes `0x01 = 0xc0` and returns success; only a transfer-level error aborts,
  and that path re-initialises the whole lane through `sub_802B170`. The
  separate 300 s probe records equality hits only, with no mismatch edge and no
  sanitiser write, so the model passes the equality path and not the sanitiser.
- **The status class is not a control input.** Eight handlers copy the `0x0d`
  class into the snapshot byte `0x2001f5dc`; the only consumer, `sub_8029A84`,
  compares class with snapshot and, on a change, reprograms `0x6b`. No per-class
  branch, GPIO write or rail change exists. The `0x6b` commit is gated by the
  transaction-valid dword `0x2001f650`, not by the class.
- **"mode 1/3" was a misnomer.** The `1` and `3` arguments of `sub_802B014` are
  monitor sub-register indices, not transfer modes. Nothing in this lane reaches
  a program transfer: the `0x6b` command handler's record transfer is a separate
  framed output (`sub_80249A8` -> `sub_8024308`), and the multi-byte EX lane that
  does exist talks to a different device (`0x70`) through `0x2001fea0`.

Where the frontier actually sits now:

1. The host-command dispatch `sub_8027E58` (message object `0x2001f664`, ids
   101..124) and the 16 software timers at `0x08051444`; the monitor lane is one
   handler among many, and the others (`sub_802A57C`, `sub_802A888`,
   `sub_802A03C`, `sub_8029A84`, `sub_8029BE0`, `sub_8026B04`) carry untested
   state.
2. The unmodelled side of the lane: the detect-line GPIO guards (`sub_802B6D4`,
   `sub_802B87C`), the rail object `0x2001fe28`, and the 10-sample present /
   absent debounce in `sub_8026718`. These read external inputs this model does
   not drive, so a probe should record the guards rather than assume a branch.
3. `0x2001fdaE/fdaf` (`cd 06`) are seeded but never transmitted; their consumer
   is unresolved, and `0x10`/`0x11` replies are modelled as zero because no
   firmware path writes those indices, which satisfies the health predicate but
   is not a recovered device identity.

## EX host lane: dispatch map and the reset loop

Three static audits plus four hardware-breakpoint windows close the "what runs
on the EX core" question for the monitored bundle.

Dispatch (static, module `0_ex1840_013000.bi.bin`): `sub_8027e58` reads the id
from `msg+0x10` after `sub_80270dc` maps timer ids `1000+id` and the
`msg+0x14 == -1` sentinel to 999, then selects a handler with two byte-offset
tables (101..119, 121..130) plus compare tiers for 120 and 131..138. The 16
software timers come from the record table at `0x08051444`
(`{cb, period_ms, name[8], id}`, created by `sub_8026df8`). Ids 117, 127 and 128
have no case: id 128 is timer-driven, so its 500 ms record always lands on the
default path at `0x8027ee4` (log `19013`). Id 111's only producer
(`sub_8028090`) has no code reference, and `sub_8027dbe`'s producer is
unresolved.

Monitor-lane membership (static): 101, 102, 104, 107, 114/124, 115, 116, 119,
120 and 126 reach `sub_802b234`/`sub_802b40c` and/or `sub_802b6f0`. The shared
state cells are `0x2001f5da` (sequence), `0x2001f5b2` (program: 0 idle, 1 armed,
2 dirty), `0x2001f5db` (fault), `0x2001f5dc` (last class), `0x2001f5a1..a4`
(presence debounce) and `0x2001f64c/f64e/f650` (status raw/class/valid).

Presence path (static, pin identity from the LZ-decoded descriptor image at
`0x2000d000`): `0x2001fe24` = GPIOE bit 9, present = low; the rail is GPIOB
bit 2 driven by `sub_802ba44`; a second raw input is GPIOC bit 0. Ten
consecutive low samples trigger the insert sequence: rail on, `0x6b` commit,
`0x0d` read, snapshot update, event `0x0202004d`. The boot probe
`sub_8026674` samples the same pin up to ten times and cannot fail. Because the
whole state block is zeroed BSS, the first poll is always "absent".

Measured (bundle, 240 s windows): only ids 101 and 102 dispatch (about 2.5/s);
software timer callbacks post at a comparable rate; `0x0804b808`
(`current_time = 0`, timer queue re-init, SysTick reload `49999`) runs about
once per second together with the whole thirteen-step init chain (return
addresses `0x0804b5b5..0x0804b615`); and the MCU reset vector `0x080002b4` is
re-entered at the same cadence with the reset stack pointer.

The reset loop is therefore the current blocker, not the monitor lane. Fault
window: a bus fault at stacked PC `0x080001d6` with `LR = 0xfffffffd` — the
exception-return path of the `BASEPRI` lock helpers `0x080001c4`/`0x080001d2`
(`mrs BASEPRI` / `msr BASEPRI`). The AIRCR `SYSRESETREQ` writer that ends the
cycle is at `0x08000606` (VECTKEY `0x05fa`, request bit 2). Next: dump
CFSR/BFAR at the fault, identify the caller that enters the helper with an
exception-return LR, and check whether the guest runs the helper from
unprivileged thread mode.

Byte audit of the monitor file (static, exhaustive immediate and literal scan):
regs 2, 4, 5, 6, 7 have no reader and no expected-value table anywhere in the
image; reg 6 is transmitted only by `sub_802b40c` (never by `sub_802b234`);
reg 7's default is `0x00`, not `0x80`; `0x2001fdae/fdaf` (`cd 06`) have no
reader and must not carry behaviour. Reg `0x0b` states 4 and 6 are the only
exit actions (final commit and finish), which stay unreachable while the model
answers 1/2 and the lane loops in reset.

## The reset loop's cause: an unmapped 0x5006 window

Fault window result (bundle, hardware breakpoints). The first BusFault entry
already carries `CFSR = 0x8200` — precise data bus error with a valid fault
address — and `BFAR = 0x50060800`. The stacked frame is
`PC = 0x080001d6`, `LR = 0x080202f9`, `R1 = 0xe000ef00` (NVIC STIR). So the
access to `0x50060800`, not the `BASEPRI` return path, is what raises the
fault; the stacked PC belongs to the return of the lock helper that follows the
software-trigger write.

Static: `0x50060800` occurs exactly once in the image (literal at
`0x08023fb8`). `sub_8023e36` loads it into a stack descriptor
(`str r0,[sp]`) and submits it through `sub_8044c40`/`sub_8044cd2`, which is
the closest thing to a driver contract for that window. Nothing else in the
image encodes a second `0x5006xxxx` constant, so this is one window, not a
family.

The machine maps flash `0x08000000`, SRAM `0x20000000..0x20040000`, the
`0x40000000` peripheral window and the `0xe0000000` system region. The fault is
therefore a real model gap and the reason the guest re-enters the reset vector
about once per second: every pass re-initializes `current_time`, the timer
queue and SysTick, so the software timers never accumulate.

Next: identify the `0x5006` window from the original software contract (its
descriptor use in `sub_8044c40`/`sub_8044cd2` is the only handle) and model it
explicitly — or keep it as an explicit unmapped fault. A read-zero or
discard aperture would hide the fault being measured and is not acceptable
under this repository's rules. Two candidate readings that the data does not
distinguish yet: a vendor SRAM window used as a descriptor ring, or a
peripheral block whose first register sits at `+0x800`.

### Reset loop, second layer

Modelling the `0x5006` window as
[the channel engine device](../devices/exrng/protocol.md) removes the
precise bus error. The loop continues through a second layer: a usage fault
(`CFSR = 0x20000`, `INVSTATE`) with stacked `PC = 0x0802af00` and
`LR = 0x08020225`, i.e. the RTOS dispatcher `sub_80201fc` executed `blx` on an
even word loaded from the table at `0x20022d70`.

Two measurements narrow it down:

- the normal dispatcher path carries odd thumb pointers — IRQ31 selects
  `0x08041065`, and the first four table words are `0x8041995`, `0x80410f1`,
  `0x80411d5`, `0x804114d`;
- the image does contain the correct odd pointer `0x0802aee9` (literal at
  `0x0802b16c`, inside the handler installer `sub_802b128`), and it contains no
  `0x0802af00` constant at all. So the even entry was computed at runtime, not
  loaded from a table.

Next probe: break at the registrar `sub_80419a4` and print each `(id, handler)`
pair; whichever call passes an even handler writes the offending entry. A RAM
watchpoint on `0x20022d70 + 4*irq` would be the direct alternative, but data
watchpoints in this gdbstub have been unreliable before, so the registrar
breakpoint is the trustworthy route.

### Reset loop closed: boot loader and RNG

Two model gaps were the whole loop, and both are now fixed with fixtures on
`main` (PR #121).

1. **Boot data segment.** `0x080002b4` only relocates the vector table. The
   descriptor image at `0x2000d000` comes from a boot loader walking the
   sixteen-byte records at `0x080038cc`. Entries: LZ image
   `0x08051ba8 -> 0x2000d000` (`0x1e10`, decoder `0x08003838`), zero fills for
   `0x20000408`, `0x20000c08`, `0x2000ee10` (`0x141fc`) and `0x2003a000`, and
   two diagnostic images (`0x2003c000`, `0x2003f000`) through an unimplemented
   loader `0x08003894`. Decoding the LZ stream offline reproduced exactly
   `0x1e10` bytes ending at `0x080528e1`, and the recovered descriptors match
   the pin table (`pad 18 -> GPIOB 0x40020400 mask 4`, `32 -> GPIOC mask 1`,
   `41 -> GPIOC mask 0x200`, `64 -> GPIOE mask 1`, `73 -> GPIOE mask 0x200`).
   Without the image, original code read zeroed descriptors and branched
   through non-thumb pointers: usage faults (`CFSR = 0x20000`, INVSTATE) at
   stacked `PC = 0x0802af00` and `0x08000000`.

2. **`0x50060800` is the hardware RNG.** The record path clears CR bits 2/3,
   clears SR bits 5/6 and later sets CR bit 2 — the STM32F4 RNG layout. It is
   now a real register block (control, status with data-ready, data word with
   host randomness); every other offset stays an explicit error.

Measured after both fixes (220 s bundle window): **no bus fault, no usage
fault**, and the reset cadence dropped from about once per second to about once
per minute. The remaining reset is deliberate: `0x8042800` writes AIRCR
`SYSRESETREQ` with no fault pending, reached from the T-Kernel path around
`0x80427xx` (the same call site observed earlier with `lr = 0x8042805`).

Next frontier: identify that deliberate reboot caller and what condition it
tests — it is now the only thing standing between the original image and a
continuous EX run. Everything the monitor lane needs (I2C shift timing, 0x6b
register file, 0x0d status, boot data, RNG) is in place.

### The remaining reboot: a boot-time status check

The deliberate reset is now localised. `sub_80426b8` walks a record array whose
base is `*(0x080429b0)`; for each entry it loads the first word and compares it
with 99 (`0x080427c0 ldr r0,[r6]`, `0x080427c2 cmp r0,#99`,
`0x080427c4 bne 0x08042800`), and the mismatch path reaches
`0x08042800 bl 0x08049088`, the AIRCR `SYSRESETREQ` writer. Two further
conditions in the same function (`0x080427d2 cmp fp,#1` after
`0x080427ce bl 0x080478c4`) also branch there.

Measured at the compare: the record is at `0x2003a000` and its first word is
`0x0000000e`, not 99, on every pass. The next words are `0x00082c21` and the
ASCII text `init` / `task` (`0x74696e69` / `0x6b736174`), so this is the
diagnostic block rather than a transfer descriptor. That block is zero-filled
by the boot loader on every reset (`0x2003a000`, length `0xc30`), so the value
is written again during the same boot.

One nearby path shows the intended contract: `sub_8042e90` writes 99 into
`0x2003a000` (`0x08042e9a movs r0,#99`, `0x08042e9c str r0,[r1]`) and then
calls `sub_804cd74` (the reboot machinery) followed by `sub_80426b8` one
argument later. So 99 is the "clean" marker the check expects, and something
during bootstrap is leaving 14 behind instead.

Diagnostic evidence: the log writer `sub_802146c` records a single entry
`id = 17003, value = 0` before the reboot. Its id and the meaning of the
status word are the next static targets: find the writers of `0x2003a000`
beyond the two known ones (the fault reporter at `0x08020862` stores 3, and
the clean marker above stores 99) and decode what 14 represents.

### The reboot is a strap check failure (code 14)

A hardware-watchpoint run on `0x2003a000` names the writer directly:
`0x08042d86 str r5,[r0]` in `sub_8042d70`, with `r5 = 0xe`. The handler is
entered at `0x08042d74` from `0x08032c76`, so the caller passes the code
explicitly:

```
8032c5a bl 0x8037f80        ; strap check
8032c5e cbz r0, 0x8032c82   ; healthy -> clear counter
8032c60 ldrb r0,[r4,#3]     ; failure counter
8032c68 cmp r0,#3
8032c6e movw r0,#20001      ; log id 20001
8032c7a movs r0,#14
8032c7c bl 0x8042d74        ; fatal(14)
```

and the fatal path writes AIRCR `SYSRESETREQ`. So the loop is: strap check
fails three times in a row, the firmware logs `20001`, records 14 and reboots.

`sub_8037f80` fails when `(sub_8038a98() & 3) == 3` **and**
`(sub_8038bbc() & 4) == 4`. Both helpers walk descriptor tables
(`*(0x8038ad8)` and `*(0x8038adc)`), read each pin through `sub_8043acc`
(descriptor `+8` GPIO base, `+12` mask) and compare it with a byte from a ROM
table, building a match bitmap. Measured: set A has six pins, all reading 0;
set B has four pins reading 1, 1, 0, 1. The failure condition is therefore a
*specific match pattern* between live GPIO levels and the ROM table, not a
missing value.

Next: resolve the two descriptor tables and the ROM expected bytes, then decide
whether the machine must drive those straps (explicit input) or whether the
comparison's meaning rules out that pattern by construction.

### The strap check is a pull-up input test

The two descriptor tables are resolved. `sub_8038a98` uses the pointer array at
`0x2001f224` with expected bytes at ROM `0x0805138c` (six entries), and
`sub_8038bbc` uses `0x2001f214` with `0x0805135c` (four entries). Every expected
byte is `00`, and the ROM record's first word is the pad number.

| Set | Pad | GPIO | Mask | PUPDR | Model read |
|---|---|---|---|---|---|
| A | `0x0a` | A `0x40020000` | `0x400` | pull-up | 0 |
| A | `0x11` | B `0x40020400` | `0x0002` | pull-up | 0 |
| A | `0x1f` | B `0x40020400` | `0x8000` | pull-up | 0 |
| A | `0x1b` | B `0x40020400` | `0x0800` | pull-up | 0 |
| A | `0x1c` | B `0x40020400` | `0x1000` | pull-up | 0 |
| A | `0x53` | F `0x40021400` | `0x0008` | pull-up | 0 |
| B | `0x25` | C `0x40020800` | `0x0020` | pull-up | 1 |
| B | `0x61` | G `0x40021800` | `0x0002` | pull-up | 1 |
| B | `0x62` | G `0x40021800` | `0x0004` | pull-up | 0 |
| B | `0x56` | F `0x40021400` | `0x0040` | pull-up | 1 |

All ten pads are configured as inputs with pull-up (MODER 0, PUPDR 1), and the
comparison treats a read of 0 as a match. The failure fires when A0 and A1
match and B2 matches, i.e. **PA10, PB1 and PG2 all read low**.

That is not what a pulled-up, undriven pin does on real hardware. The pinned
GPIO model already implements it correctly: `disconnected_pins` defaults to
`0xffff`, and a disconnected input with `is_pull_up` reads 1. So some model
path is *driving* these pads low (they are connected rather than floating) —
either the pad frontend or the board input record.

Model fix, in order of preference:

1. leave the ten pads disconnected so the pull-up applies (matches the register
   configuration the firmware itself wrote);
2. only if the board profile genuinely names them, drive the levels the profile
   declares — never invent a strap value to dodge the check.

With those three pins reading 1, the match pattern changes, `sub_8037f80`
returns 0, the failure counter clears and `fatal(14)` never runs.

All ten strap pads are host-key frontend pads:
`nikon_input_gpio_ids = {0x0a, 0x11, 0x1f, 0x25, 0x1b, 0x47, 0x4d, 0x5b, 0x5e,
0x61, 0x62, 0x6d, 0x6e, 0x6f, 0x1d, 0x53, 0x1c}` covers A0..A5 and B0..B2;
only `0x56` (PF6) is outside it. The frontend drives `!down` (`nikon_input_event`,
`nikon_input_publish`, `nikon_input_unrealize`), i.e. **1 when the key is up**, so
an idle board should read high and the check should not match at all.

Measured 0 on those pads, while MODER 0 and PUPDR 1 are what the firmware wrote.
So either the frontend line never reaches the GPIO model (wiring/reset ordering
between the frontend realise, the machine reset that replays pads, and the GPIO
cold reset) or the replay path overwrites it. That single discrepancy is what
still drives `fatal(14)`; next probe: read the frontend pad line and the GPIO
IDR at the same moment for `0x0a`, `0x11` and `0x62`.

### Power-good polarity was the fatal-14 trigger

The QOM query named the driver: `disconnected-pins` on GPIOA/B/C/F/G had exactly
the strap bits cleared (`PA10`, `PB1`, `PB15`, `PB11`, `PB12`, `PF3`, `PG2`,
`PC5`), with `pins-connected-high = 0`. That is `z7ii_ex_input_tick`, which
declared those seven pins as power-good wires and drove them **low** every
millisecond.

The firmware treats exactly that pattern as a rail fault: `sub_8037f80` fails,
three failures log `20001`, `sub_8042d74` records `14` in `0x2003a000` and
writes AIRCR `SYSRESETREQ`. Driving the declared wires according to
`power_good` (high while good) makes the check return 0 for every sample in a
200 s window and removes the `14` path.

What appears next is a second ladder: the state block at `0x20013d18` reports
`+5 == 1` and `+6 == 2`, and `0x8032378` logs `20002` and records status `15`
(`+6 == 1/2/3` maps to `12`/`15`/`16`). Next question: what fills `+5`/`+6` on a
clean boot — most likely a reset-reason decoder reading RCC control/status or
the diagnostic block, which would mean the ladder is being fed by the very
reset it causes.

### The second ladder is a handshake countdown, not a fault

A watchpoint on the state block word `0x20013d1c` (bytes `+4..+7`) names every
writer in one boot:

```
0x80325b0  word 0x01000000  (+7 = 1)
0x80323d6  word 0x01020000  (+6 = 2)   reason recorded
0x8032cc8  word 0x01020100  (+5 = 1)   escalation armed
0x80038be  word 0             boot loader zero fill
```

Two functions form the ladder.

`0x8032378` is the verdict routine (`r4 = state block 0x20013d18`):

```
[r4+5] != 1            -> 0x80323a4 (no verdict)
[r4+5] == 1            -> log 20002 with [r4+6]
[r4+6] == 1/2/3        -> fatal(12) / fatal(15) / fatal(16)
```

`0x80323c8` records the reason: `bl 0x8033b48`, `[r4+3] = 1`, `[r4+2] =
*(0x20013bc8)`, `[r4+6] = 2`.

`0x8032c84` runs the countdown:

```
if ([r4] == 1) { if (--[r4+1] == 0) { [r4+1] = *(r4-0x151); bl 0x80214fe; } }
if ([r4+3] == 1) { if (--[r4+2] == 0) { [r4+3] = 0; [r4+5] = 1; ... } }
```

So the block arms a countdown (`+3 = 1`, `+2` loaded from `0x20013bc8`) and, when
it expires, sets `+5 = 1`; the verdict routine then sees reason 2 and escalates
to `fatal(15)`. That is a **power-on handshake that never completes**, not a
memory or transfer fault — the same shape as the earlier monitor-lane waits,
one layer up.

Next probe: log `[r4]`, `[r4+1]`, `[r4+2]`, `[r4+3]`, `[r4+5]`, `[r4+6]` at
`0x80323c8` (arm) and `0x8032cbe`/`0x8032cc8` (expiry) to get the countdown
length, and step into `bl 0x8033b48` to see which handshake was armed. The
countdown value at `0x20013bc8` is the concrete deadline to compare against the
lane it waits for.

### Why the handshake is armed: the submit returns an error

Arming is not unconditional. At `0x80323c8` the block is empty
(`+0=1, +1=0x0a, +2=0, +3=0, +5=0, +6=0`) and the value returned by
`bl 0x8033b48` is **`0xffff0301`**. The countdown then loads `0x20013bc8 = 0x0a`
and the trace shows it stepping `0xa, 0x9, 0x8, ... 0x5` with `+3 = 1`, so ten
ticks later `+5` becomes 1 and `fatal(15)` fires.

`sub_8033b48` builds a descriptor (`command 11`, one copied status byte, three
callbacks) and submits it through `sub_8033958`, which is a thin indirect
dispatch:

```
sub_8033958: if (desc == 0) return 0xff; else
             r0 = desc; r1 = *(0x200138d4 + 12); bx r1
```

So the service behind `0x200138d4` returned `0xffff0301` and the caller armed a
ten-tick retry/handshake instead of treating the reply as success.

Next: identify the registered handler behind `0x200138d4 + 12`, decode
`0xffff0301`, and find which model-provided value it rejects. That handler is
the last thing between the original image and a continuous EX run — every fault
layer below it (RNG, boot data, strap/power-good polarity) is now closed.

### The failing service is the diagnostic log ring

`sub_8033958` dispatches through `*(0x200138d4 + 12)`; the watchpoint on
`0x08033966` names that pointer as **`0x08032000`**, which is the diagnostic
log-ring module. It was called with four descriptors in the observed window:

```
cmd 0x1c  value 0x200139da
cmd 0x6a  value 0x200139fa
cmd 0x05  value 0x20013bb8
cmd 0x01  value 0x200138ea
```

`sub_8032000` starts with a capacity check:

```
r0 = desc[0];        r1 = *(0x20011754)
if (r0 >= r1) { log 11001 with (idx, count, value); return 0xff00 | ... }
```

**Correction to the first reading:** the capacity is initialised. A watchpoint
on the byte sees `0x8031cec` write `cap = 0x12` (18) before any dispatch, and
three subsequent dispatches carry indexes `1`, `0x10`, `6`, all below the
capacity, so the check passes. The zero is therefore not the cause.

What is still open is where `0xffff0301` comes from: `sub_8032000` has more
error paths after the capacity check, and the caller (`sub_8033b48`) composes
its own status. Next: single-step the ring module for the `cmd 0x1c` call (the
first one) and record which branch returns the error, instead of assuming the
capacity gate.

### Correction: `0xffff0301` is a packed ring ack, not a plain error

Reading `sub_8032000` to its returns changes the earlier interpretation. The
success path stores the descriptor and returns

```
r5 = (counter & 0xffff) | (kind << 8)      ; 0x803209e
```

so `kind = 3`, `counter = 1` yields `0x0301`. The `0xffff` half comes from the
caller (`sub_8033b48`), which combines the ack with its own high word, and the
value observed at `0x80323c8` (`0xffff0301`) is therefore the enqueue ack, not a
transport failure.

That also matches the caller's own branch:

```
0x8033b8a  r0 = ack - 0xFF00 - 0xFF
0x8033b90  beq 0x8033b96      ; only an exact 0xffff takes the retry path
0x8033b92  [r5+4] = 1         ; otherwise: success marker
0x8033b96  [r5+4] = 0xf0, [r5+5] = 10, log 20011   ; 10-tick retry arm
```

With `0x0301` the success branch is taken, so the retry arm at `0x8033b96` is
**not** what raises `fatal(15)`.

The latch is armed by the other function: `0x80323c8` records reason `2` in
`[r4+6]`, sets `[r4+3] = 1` and loads the countdown from `0x20013bc8` (`0xa`);
the 1 ms tick at `0x8032c84` decrements it and sets `[r4+5] = 1` when it
expires, after which the verdict routine at `0x8032378` escalates. So the next
question is what that recording path is waiting for and who calls it, not
whether the enqueue succeeded.

### The latch reads a "cmd 11 already submitted" marker

The condition cell is `0x200138ee`, and the watch names its only writer:
`0x8033b94` inside `sub_8033b48` (`strb r6,[r5,#4]` with `r5 = 0x200138ea`).
So the marker means "the ring entry with index 11 has been submitted".

The verdict path is then exact:

```
0x80323aa  r0 = 0x200138ea; r1 = [r0+4]        ; the marker
0x80323ae  cbz r1, 0x80323c4                   ; not submitted yet -> submit + arm
0x80323b0  pop; log 20017                      ; already submitted -> nothing
0x80323c4  bl 0x8033b48                        ; submit index 11, sets the marker
0x80323d0  [r4+2] = [0x20013bc8] (= 0xa)       ; countdown
0x80323d2  [r4+6] = 2                          ; reason 2
0x80323d4  ...
```

`sub_8033b48` has exactly **one** caller: this arm path. So in this image the
marker can only become set from inside the arm, which means the first pass
always takes the arm branch, submits index 11 and arms reason 2 with a ten-tick
countdown. Nothing in the image clears `[r4+3]`/`[r4+6]` afterwards — the only
other writer of that word is the tick at `0x8032c9a`, which decrements a
separate reload byte `[r4+1]` (watched stepping `0x0a -> 0x09 -> ...`).

Two facts therefore frame the next step:

- the block holds two independent counters, `[r4+1]` (periodic reload, `0xa`)
  and `[r4+2]` (the ten-tick escalation countdown);
- the stage bytes `[r4+0] = 1` and `[r4+7] = 1` are written early
  (`0x803247a`/`0x803247c` and `0x80325b0`), and the arm requires `[r4+0] == 1`.

So the question is no longer "which service failed" but "which boot stage is
supposed to advance `[r4+6]`/`[r4+3]` before the ten ticks elapse". Next probe:
watch the four bytes `0x20013d18`..`0x20013d1b` (stage + counters) through a
whole boot, and break at the caller of the verdict routine to see its period.

### The EX domain now runs; one ladder reset remains

State after the RNG window, the boot scatter loader, the power-good polarity
fix and publishing the board record at attach time (all on `main`, PRs
#119/#121/#122/#123):

- the T-Kernel module initialisation runs **once** (`0x804b808` entered a
  single time in a 180 s window), so the old re-init loop is gone;
- the tick advances monotonically: `0x7cf, 0xf9f, 0x176f, ... 0x98ab` for
  60,000 increments, one increment per SysTick;
- the host command lane is alive and varied: ids `101`, `102`, `103`, `131`,
  `1114`, `1115` (timer ids 114/115 now reach their handlers), more than
  16,000 dispatches and 10,000 timer posts in a 240 s window;
- the monitor lane keeps running: 30,000 `0x0d` service entries and repeated
  `0x6b` equality edges in the same window.

One deliberate reset remains, and it is the ladder described above:

```
ARM  (0x80323c4)  tick 0x2aa2
FATAL(15) at 0x8042d86, return address 0x80323a5, tick 0x51b2
MCU reset
```

so the arm happens about 10,000 ticks after boot and the escalation about
10,000 ticks after the arm (the ten-unit countdown is therefore driven by a
1 kHz divisor, not by SysTick directly). After that reset the observed windows
show no second arm, but the wall time available after the reset was not always
enough to reach the same tick count, so "does the second boot re-arm" is the
open measurement. It needs a steady run that spans at least ~25,000 ticks after
the reset with a single reset breakpoint (the multi-breakpoint runs detach
early in this gdbstub).

### The ladder is a cold-boot transient

A 600 s window with a single breakpoint shows exactly one escalation:

```
FATAL n=1  code=0xf  return 0x80323a5  tick 0x51b4
```

and a 420 s window with three breakpoints shows the block initialisation and the
arm only on the cold boot:

```
BLOCK_INIT n=1  tick 0x3e0
ARM        n=1  tick 0x2aa4
RESET      n=2                      ; the escalation, then nothing further
```

After that reset the second boot runs the rest of the window (minutes of wall
time, tens of thousands of ticks) **without re-initialising the block and
without re-arming**, so the escalation is a one-time cold-boot behaviour in this
model rather than a periodic loop.

The open question is resolved: the block is armed by a *boot step*, posted
once, and the escalation is fatal by construction. The mechanism, from single
breakpoint and write-watchpoint windows:

- the step table at `0x0804E160` maps step codes to handlers
  (`01→080333D8, 04→0803350C, 06→08033498, 08→08033398, 0A→08033418,
  0C→080333F8, 0D→080333B4, 10→08033438, 12→08033328, 20→0803347C`) and its
  step `0A` handler copies an eight-byte descriptor into `0x20013B54` and
  posts mask `0x00010000` to the event block `0x200138F8`;
- that mask selects handler `0x08032451` in the same `{mask, handler}` table
  (`0x0804DF64`) as the verdict, and `0x08032450` is the block initialisation.
  A single hardware breakpoint on it fires once, at tick `0x3DD`, from the
  dispatcher;
- the verdict is posted by the reload tick whenever `stage == 1`; the first
  post arms and records reason `2`, and the arm reloads the ten-invocation
  countdown from the descriptor word (`0x010A0A01` at `0x20013B54`, copied at
  tick `0x3DA`);
- the done byte `0x20013D1D` has exactly one observed writer, the tick expiry
  at `0x8032CC6` (60 s write watchpoint). On expiry the verdict always
  escalates by reason (`1/2/3 → 12/15/16`), so a completed cycle is fatal by
  construction, not just the armed one;
- the verdict's skip guard is `[0x200138EA+4]`, and the arm's own record
  builder `sub_8033B48` sets that byte to `1` (`0x8033B92`). The guard can
  never fire before the first arm, so the first cold-boot post always arms;
- after the reset neither the step-`0A` copy nor the block initialisation is
  reached again. A 100 s post-reset window contains no verdict invocation and
  no block-initialisation entry; a 90 s window enters the MCU reset vector
  twice in total (attach entry, escalation).

The remaining "why" is which cold-only gate keeps the boot step from re-running
after our RAM reset (a reset-cause distinction or a boot-stage flag); the
observable behaviour is already "one deliberate cold-boot reset, then
continuous" and the bundle deliberately synthesizes no management reply. The
resolved write-up is in `docs/runtime/ex.md` on `main`.

Accepted criteria for the continuous-run goal, measured on this build:

| Criterion | Evidence |
|---|---|
| initialisation chain runs once | `0x804b808` entered once in 180 s |
| monotonic time base | tick `0x7cf, 0xf9f, ... 0x98ab` over 60,000 increments |
| periodic software timers dispatch | ids `1114`, `1115` and 10,000 timer posts |
| monitor lane stable | 30,000 `0x0d` services, repeated `0x6b` equality edges |
| no periodic reset | one cold-boot escalation then continuous in 600 s |

## Measured app-state progression (latest pass)

The MAS GUI app dispatcher at `0x1020D41A` takes `r1` as the state and `r0` as
the context pointer (`0xB1818BD0`). A 60 s window reaches states `1`, `2` and
`3`, and the state-3 branch at `0x1020D51E` executes (`lr=0x1020D42B` from the
dispatcher). The deeper one-run marker `0x1020D5FA` stays at zero, so it
belongs to a later sub-step rather than to the state-3 entry.

In the same pass the EX record lane delivers only the four boot records
(`0x01`, `0x0D`, `0x08`, `0x0A`) and never the type-`0x12` power record, and
the MAS power classifier `0x1038A6AA` stays at zero hits. Both lanes therefore
stop before the power classification that feeds the provider lifecycle and LCD
start.
