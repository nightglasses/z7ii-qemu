# GUI chain

> **Research log, not living documentation.** This file records the EX/GUI
> reverse-engineering frontier in the order it was worked, so paragraphs that
> call something "the current blocker" describe the moment they were written
> and may be superseded. The resolved current state is in
> [`ex.md`](ex.md) (EX runtime and the cold-boot ladder) and
> [`../validation/native.md`](../validation/native.md); where this log and
> those pages disagree, the pages win.

> The F091 interpretation below is superseded by [ptp.md](ptp.md): it is a PTP
> parameter subcommand behind an idle transport/event wait and session/mode gates,
> not a demonstrated internal cold-boot sender. `104c0f20` is a local callback
> broadcast. The independent readiness call at `1062f120` is the mainline lead.

> That lead is traced in [display.md](display.md): GUI mode entry produces
> slot 3 and bit `80000000`. [battery.md](battery.md) qualifies the earlier
> battery wait to the single-thread probes and reproduces report delivery on
> the current multi-thread baseline. Historical multi-breakpoint zero-hit
> claims below remain unsuitable as absence evidence.

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

### Why the EX report leaves `+61` zero

The EX builder `0x08025544` (`sub_8025544`) erases the 112-byte report at
`0x20015fc0` and then fills it. Its write of `+61` (`0x20015ffd`) is guarded:

```text
if (0x20016058 == 0)      -> skip, the byte keeps the erased 0
else if (0x20016058 == 1) -> report[+61] = 1
else                      -> report[+61] = 0 or 17
```

So the report can only carry the value the MAS classifier wants when
`0x20016058` is `1`. A cross-reference pass finds no static writer for
`0x20016058` (the accesses are `movw`/`movt`-based and unattributed), and a
write watchpoint over 120 s records exactly **one** write, the boot image's
initialisation of that byte to `0` (`pc=0x080038be`, the SRAM template
decompressor). The same watchpoint on `0x20015ffd` records only the same boot
zero.

The chain is therefore: `+61` needs `0x20016058 == 1`, and nothing in this image
ever sets that byte. This is the same shape as the id-130 publisher gate
(`0x2001fba6`, see `docs/runtime/ex.md`): a state byte that the compressed boot
template initialises to zero and no later code changes. The normal EX-to-MAS
power flow is missing that one setter, not a record type.

### The whole input block is never populated (900 s receipt)

The report builder reads a state block that starts after the 112-byte report.
A 900 s run under the icount base watches the four dwords at
`0x20016040..0x2001604f`:

```text
0x20016040  one write, pc=0x080038be (boot template), value 0
0x20016044  no write at all
0x20016048  no write at all
0x2001604c  no write at all
```

So the block is not "slow to fill": nothing in the EX image populates it. The
0x6b monitor read completes on this same build (see the 0x6b section) and still
does not touch the block.

Three independent gates of the power path are now inert in this image, all in
the same way:

| Gate | Consumers | State |
|---|---|---|
| `0x2001fba6` | id-130 publisher start (`sub_8027d48`) | template zero, never set |
| `0x20016058` + the `0x20016040..` block | EX report `+61`, MAS classifier | template zero, never set |
| `0x200138da` | the type-`0x12` record handler | written but never read |

The honest reading is that this EX build does not run the power classification
branch, so the provider lifecycle and LCD start are unreachable here regardless
of the transport, the configuration or the monitor lane. Further work should
either find an image whose boot template differs, or restate the acceptance
target to what this image demonstrably reaches.

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
producer. Event-6 cannot supply a mode-0 transfer.

**Correction: the mode-1 path does write SSI0/1.** Probing `0x1024325c` (the
helper that stores through the device pointer at `[a1+28]`) records
`datareg=0x2c0a0000` (TX channel 0) with 0x80-byte frames split into 0x20-byte
stores, and `datareg=0x2c0a0100` (channel 1) for the RX task argument
(`work/runs/ex-6b-ercd/probeMAS3.gdb.log`). The earlier sentence that the
mode-1 path produces no observed SSI0/1 write is withdrawn.

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

**Superseded in part (icount receipt).** With the model's address-phase
deferral (`7be58c8e`: the receive byte is clocked only after the guest clears
ADDR) and the icount time base described below, a 900 s run reaches the 0x6b
read and takes the *non-BTF* completion path:

```text
sub_802ae48 entered 10x with descriptor 0x2001fdcc, wire address 0xd6,
sub-address 0 1 3;
sub_8046cd0 entered for 0x2001fdcc with state +61 = 0x22, count +42 = 1,
+44 != 2  (the exact condition it tests).
```

Before the deferral fix this handler was never entered.  The BTF path
`sub_8046bfe` is not a dead end either: it runs for other descriptors
(`0x2001fea0`/`0x2001fe40`) at the same state `0x22` with counts 2..4.  So the
0x6b monitor read completes, and the earlier claim that RXNE-with-BTF can never
complete is withdrawn.

The report input block `0x20016040..58` and `report[+61]` still carry only the
boot template's zero, so the power classification is downstream of something
other than the 0x6b read.

### Probe time base

Original-firmware probes run with `-accel tcg,thread=single -icount shift=6`:
icount derives `QEMU_CLOCK_VIRTUAL` from executed instructions, so peripheral
byte timers advance with guest progress instead of wall clock, and `shift=6`
(64 ns per instruction) matches one instruction per cycle at the modelled
16 MHz HSI.  icount is incompatible with multi-threaded TCG.  The wall-clock
base is kept for A/B comparison (`work/runs/ex-6b-ercd/run-wallclock.sh`);
receipts across time bases must be compared by guest progress, not wall
seconds.

Two later harness changes keep the same time base but remove wall-clock cost:

- `-icount shift=6,sleep=off` (`work/runs/ex-6b-ercd/run-fast.sh`) stops QEMU
  from idling to keep virtual time equal to real time, so one wall second covers
  far more guest progress at the identical 64 ns per instruction.  A state that
  needed a 420 s throttled window arrives in about 10 s.
- Static "who references this" scans must cover unconditional `B`/`B.W` as
  well as `BL`/`BLX`: `work/brscan.py` does both, `work/blscan.py` only calls.
  Several earlier "no reference" results were `B`-only references (see the
  event-0x39 correction), while the EGL present wrapper stays unreferenced
  under both scanners.
- Execution coverage is measured with `-dfilter ADDR+SIZE -d exec,guest_errors`
  (`work/runs/ex-6b-ercd/run-df.sh`) and aggregated from the trace, because
  GDB execution breakpoints detach the QEMU stub in this configuration after
  their first hit (data watchpoints stay connected).  A zero-hit coverage line
  is a direct execution fact, not a probe artefact.

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
(`0x01`, `0x0D`, `0x08`, `0x0A`). The type-`0x12` record never arrives, but it
is **not** a boot dependency: its EX handler `0x08033328` only stores one
payload byte into `0x200138da`, and that byte has a single writer and no
readers. The earlier reading of this pass that blamed the missing `0x12` for
the power classification and LCD start is withdrawn (see
`docs/runtime/ex.md`). The classifier `0x1038A6AA` staying at zero hits is
therefore its own question, and the code-supported frontier remains the
client-4 report field at `+61` described in "New frontier: MAS power
classification of the 112-byte record" above.

## What the normal EX-to-MAS flow still lacks

Reading the verified pieces in boot order, with the evidence class of each
link:

| Stage | State | Evidence |
|---|---|---|
| EX cold boot, RTOS init, cyclic monitors created | runs | 27 objects created, `probeRTOS2` |
| EX power monitors publishing (ids 113/114/115) | runs | `probeHOST1` lane counts |
| EX host-command dispatcher (101/102/103/104/131) | runs | `probeHOST1` |
| EX record dispatcher `0x080332f4` | runs for 4 boot records | `{type,handler}` table `0x0804e15c` |
| MAS kernel and GUI app states 1..3 | runs | dispatcher `0x1020d41a` |
| MAS record TX to `0x2c0a0000`/`0x2c0a0100` | runs | `probeMAS3` (0x80-byte frames) |
| EX power report emitted and classified by MAS | **classification wrong** | EX builder leaves `+61` zero; classifier `0x103f8ec6` reads it |
| Provider lifecycle and LCD start | not reached | downstream of the classification above |

So the normal flow is not missing a record *type*; it is missing the EX report
field `+61` (and the `0x103f8ae4` state beside it) that the MAS classifier
needs, or the MAS-side decode of that field. That is the next thing to settle,
and it is a data/condition question, not a missing command.

## Display owner: the natural start is reachable, the EGL present is dead

### Corrected call scan (direct observation)

The earlier statement in this file that `sub_104db8ba` has no caller was a
scanning artefact.  A Thumb-2 `BL` is a 32-bit instruction that can start at
any 2-byte boundary; a 4-byte-aligned scan finds only half of them.  A
2-byte-aligned `BL`/`BLX` decode over the MAS image was validated against two
known call sites (`0x10569f4e -> sub_10569c76`, `0x10569eda -> sub_10568f1c`)
and reports:

| Target | Role | Call sites |
|---|---|---|
| `sub_104db8ba` | pre-restart display predicate | `0x104dc07c`, `0x104dc6de` |
| `sub_1046bf0c` | main/sub-unit command writer | `0x1044936c`, `0x1044a0c6`, `0x104db90e`, `0x104dc26a`, `0x104dc294`, `0x104dc2ce`, `0x104dc66c` |
| `sub_10568f60` | EGL present wrapper (`eglSwapBuffers` + window table) | none |
| `eglSwapBuffers` (`0x10680b8c`) | driver swap handler | only `0x10568f7c`, inside `sub_10568f60` |

So the natural panel start is reachable, and only the EGL present wrapper is
dead code.  This does not by itself make a frame: it moves the question from
"the start is unreferenced" to "what arms the display owner".

### Owner state machine (verified from code; refs in `docs/refmap/REFS.tsv`)

- `0x104dc804` is the display-owner lifecycle callback.  ROM method tables at
  `0x1079deb4`, `0x1079deec`, `0x1079df24` and `0x1079df5c` (0x38-byte rows
  from `0x1079deac`) hold `0x104dc805`, so the callback is registered rather
  than called by a `BL`.
- The callback reads the five lane state bytes at `0xB1D0D0BC + lane`.  Lane
  state 6 calls `0x104dc522`; lane state 7 calls `0x104dc46a`.
- `0x104dc522` switches on the owner counter `[ctx + 0x254]` through a
  28-entry TBB at `0x104dc54c`:

  | State | Target | Action |
  |---:|---|---|
  | 0 | `0x104dc568` | provider/commit probe |
  | 1 | `0x104dc5b4` | state-1 helper |
  | 5 | `0x104dc5c2` | state-5 helper |
  | 6 | `0x104dc5cc` | per-port setup, `0x104dae5a(1)`, `0x1046bf0c(sel 8, value 0)` at `0x104dc66c`, counter++ |
  | 2-4, 7-24 | `0x104dc5ba` | counter++ only |
  | 25 | `0x104dc690` | parks while `[0xB1D020A0]` (pending provider) is null |
  | 26 | `0x104dc6c0` | consumes the provider (`0x104dbb40`), clears the mailbox, calls `sub_104db8ba` |

  After state 26 the counter is reset to zero at `0x104dc7fc`, so the machine
  cycles.  `sub_104db8ba` itself writes `0x1046bf0c([0xB1808064], 8, 0)` at
  `0x104db90e` after its predicates `sub_1046f546(1)`, `sub_10470276`,
  `sub_10470214` and the display state read `sub_104810e6()` (global
  `0xB1D01800`).

### Provider admission (verified)

- `0x104d7e78` is the admission wrapper.  It returns 3 after ten bounded
  retries while the manager-ready word `[0xB1807FA8]` is zero.  With ready set
  it checks manager `[0xB1807FA0]` and the request code through `0x1051d6f6`
  (accepts 0..17, 32..33, 37, 48..49, 64..67, 80..89, 96..111, 128..159, 161,
  176..190, 192 and 208..216).
- On success it calls `0x104d7d7e(0, code)`, the provider domain dispatcher,
  which builds objects with `0x1051d506` and publishes through `0x104d7b6e`
  (event `0x42060005`) or `0x104d7c60` (event `0x42061005`).
- The ready word is written by `0x104d7f16`; its only reference is a tail
  branch from the internal display self-test at `0x10637b72`.  The manager word
  is written by `0x104d79ea`, reachable from the F092 handler `0x10502db8`
  (request byte at `+0x10`).  `0x104dcd16` is the owner "accepted mode change"
  method, and `0x104dce06` moves eligible lanes from state 4 to state 6.

### Runtime receipt (direct observation, icount shift 6, 420 s)

At the first app draw (`sub_10569c76`, `a1=0xb81849c0`, `a3=0x200020`):

```text
disp = *(u32*)(0xB180807C) = 0xB1D0D0BC
ctx+0x254            = 0        owner counter
ctx+0x1c8 / ctx+0x1c9 = 0 / 1   display mode 1
ctx+0x2c0..0x2c3      = 3 3 5 5
lanes ctx+0..4        = 0 0 0 0 0
```

Later in the same window the UI dispatcher takes `0x42010001` and draws
`720x480` at `0xbf309600`, which the machine log names `lcd-composition-base`.
**That surface stays empty in the later runs:** a RAM dump after 30 s and after
90 s of unthrottled shift-6 execution (`work/runs/ex-6b-ercd/run-dump2.sh`,
`probeFB4`/`probeFB5`) finds `0xbf309600`, `0xcb4a7000` (`display-buffer-base`)
and `0xbec5f200` (resource-57 record 0) entirely zero, while the app draw
`0x10569c76` executes 41 times in 300 s.  The single earlier sighting of the
`720x480` composition draw is therefore not reproduced; the app draw that does
run writes other surfaces, and no candidate panel surface holds pixels yet.
The owner callback `0x104dc804` (entry), the owner dispatch site `0x104dc9ea`,
the natural start `0x104db8ba` and every `0x1046bf0c` call site record zero
hits.  So the app already renders a panel-sized frame into the composition
buffer, the display owner never runs, and no provider is admitted.

### Reading (inference, with the next measurement named)

The owner is registered but idle (lanes 0).  The frame therefore does not need
`eglSwapBuffers`: the DMPX window surface is created from a caller pointer
(`eglCreateWindowSurfacePointerDMPX`, `0x106815a4`) and the app renders directly
into the composition buffer.  What is missing is the display-service request
that arms the owner and fills the provider mailbox.  Two candidates remain:

1. the F091/F092 request family that arms the owner (`0x104dcd16`) and sets
   ready/manager, and
2. an image provider object that would fill `0xB1D020A0`.

`work/runs/ex-6b-ercd/probeDMSVC.gdb` watched the F0 router `0x104c6b14`,
admission `0x104d7e78`, the ready setter `0x104d7f16`, the manager setter
`0x104d79ea`, the owner event method `0x104dcd16`, the owner callback
`0x104dc804`, the two service handlers `0x10502d0a`/`0x10502db8` and the
provider builder `0x1051d506` for 480 s.  **All nine are zero-hit.**  A 900 s
confirmation with the draw marker (`probeDMSVC2`) also records only the first
`512x512` app draw (`a3=0x200020`) and still no service, ready, manager, owner
or start hit.  The whole display-service envelope path is idle, so the request
is not produced at all; the question is which component produces it, not which
gate rejects it.

Run-to-run variation is real but bounded: the first app draw is always seen;
the later screen switch and the `720x480` composition-buffer draw appeared in
one 420 s window and not in the 900 s window.  Compare guest progress, not
wall seconds, and treat a zero-hit late-stage probe as "not observed", not
"proven absent forever".  The four-way gate below (router, admission, owner,
start) is what the next windows must break.

## The application stage ladder and the AppC_Msg table

### Static record (verified)

The ROM contains a service record at `0x123b2b50`:

```text
+0  0xB1818BC4   RAM context pointer
+4  0x1209CB48   "AppC_Msg" name pointer
+8  0
+12 1
+0x3C = 0x123B2B8C  first entry of a 25-entry table of Thumb pointers
```

The table itself is not referenced by any `BL` or 32-bit literal; it sits
directly after the record, so the record base plus `0x3C` is the only
code-supported way to find it (inference).  Entries:

```text
0 0x10204B8D  1 0x10204B97  2 0x10204BB5  3 0x10204BB9  4 0x1020D5DF
5 0x1020D5FB  6 0x1020D621  7 0x1020D629  8 0x1020D631  9 0x1020D63F
10 0x1020D67D 11 0x1020D6AD 12 0x1020D6BD 13 0x1020D6C3 14 0x1020D6C9
15 0x1020D76F 16 0x1020D82B 17 0x10204BD9 18 0x1020D841 19 0x1020D8BD
20 0x1020D8F7 21 0x1020D93D 22 0x1020D945 23 0x10204BBD 24 0x10204BDB
```

The dispatcher that indexes this table has not been identified; the table
entries, not the index rule, are the verified part.

### Stage functions (verified from code)

- `0x1020D41A(ctx, stage)` is the stage dispatcher.  Cases 1/2/3 run their
  setup blocks and then post `0x2000000` through `0x1020D364`.
- `0x1020D316(ctx, stage)` is the stage initializer:
  - stage 1 registers `sub_1020CA7A` through `0x103606F8`, creates the
    deferred-drain object (`0x102A7C18`) through `0x102A7C5C`, and writes five
    message templates into the deferred array: `(slot,cmd)` =
    `(0,0x03000003)`, `(1,0x03000005)`, `(6,0x03000007)`,
    `(7,0x03000008)`, `(2,0x06000003)`;
  - stage 3 registers the `"AppC_Lck"` lock at `ctx+0x388` and fills the
    deferred entry at `ctx+0x304` with `{ctx, 0x03000002, slot 4}` before
    calling `0x102A7C92`.
- Deferred queue: array base `0xB1819A80` (8 slots of 8 bytes), count at
  `0xB1819AE0`.  Push `0x102A7C92` copies the two words and signals through
  `0x1036074E`.  Drain `0x102A7C18(mask)` builds a 68-byte message from each
  set slot and links it at `ctx+32` through `0x10294342`; the slot's second
  word becomes message command `+16`.
- `0x10201854(stage)` is the AppC client fan-out: a ROM/RAM factory table at
  `0xB185DEF0` supplies six client factories, and each returned object's
  method `+4` is called with the stage value.  The factual meaning of the six
  clients is not yet identified.

### Measured stops (icount shift 6)

| Window breakpoints | Stages seen | Notes |
|---|---|---|
| stage dispatcher only | 1, 2, 3 | 420 s; no later event |
| dispatcher + message link + owner | 1, 2 | 420 s, `0x2000000`/`0x2000001` only |
| dispatcher + link + deferred push/drain + lifecycle (9) | 1, 2 | 420 s; defer pushes `(0,0x03000003)`, `(1,0x03000005)`, `(6,0x03000007)`, `(7,0x03000008)`, `(2,0x06000003)` |
| dispatcher + link + deferred push/drain + lifecycle (12) | 1, 2 | 600 s icount and 240 s wall clock, byte-identical |

The stage-3 deferred `0x03000002` message was **not observed** in any window
so far, because the windows that include the deferred probes stop at stage 2.
Probe-set size and SMP interleaving change how far a run gets; stage receipts
must be compared by stage, not by wall time or probe count.

### What this rules in and out

- The app's own message framework works: the deferred push, drain, queue link
  and the `0x2000000`/`0x2000001` round trip all execute at stages 1-2.
- The display-service client (`0x10463A02` init, `0x10463BD4` control,
  `0x10463E8E`/`0x10478F3C` send -> `0x104C0F96`) never runs in any window,
  and neither does the F0 router or the owner.
- So the missing display request is not the app's generic message framework;
  it is the client that would call `0x104C0F96` with a display class, and that
  client is not being initialized or driven.

## The stage-3 GUI client and the live-view event

### Client fan-out (measured + code)

`0x10201854(stage)` reads a six-entry factory table at `0xB185DEF0` and calls
each returned object's method `+4` with the stage.  One measured filling of the
table (probeCLIENT, 240 s, first stage 2):

```text
0xB185DEF0  0x1042F51D
0xB185DEF4  0x104375C1
0xB185DEF8  0x10763BED
0xB185DEFC  0x102017D5
0xB185DF00  0x10201801
0xB185DF04  0x1042F51D   (same factory as entry 0)
```

The second client (`0x104375C1`) returns the static object `0x104376FC`; its
method `0x104375C4` is the GUI client's stage handler.  It handles stage 2 and
stage 3; the stage-3 block initialises the GUI and DMP pieces in this order:

```text
0x104377B8  0x1044E304 "GuiMeEv1" (id 0x10007)
0x1073ED94  0x1053CF38 "GuiLvEv1" (id 0x1000A)
0x1043DCD0 "GuiMsEv1" (id 0x1000B)
0x1072E1AC  0x10634034  0x10634056  0x10634072   (DMP/graphics setup)
0x1053A9E4  0x1073D3B8  0x106558C8  0x1065385C  0x1058014C  0x1063D98E
0x103BC584(...) with short string arguments
```

`0x103FAB8A(obj, "GuiLvEv1")` registers the event object and `0x106DDC68`
binds its handler.  The event ids are `0x10007` (menu), `0x1000A` (live view)
and `0x1000B`; the strings are the ones the earlier `gui-event-producer` probes
looked for.

### Live-view start gate (verified code)

`0x1053DBA2(arg)` posts event `0x40001` to the object at `0xB181D9B8` through
`0x1062E474` only when all five conditions hold:

```text
sub_1061E364()            != 0   (panel/engine state machine, no events 113-115)
*(u32*)(0xB181D970+0x2C)  == 0   (0xB181D99C)
sub_10633910()            -> v3 = 15 if nonzero else 5
sub_10652C36(v3)          != 0
sub_104A9824()            == 0
```

Only three sites call it: `0x1053DC48`, `0x1053DCBE`, `0x1053DEAE`, all in the
GUI event-module.  It is event-driven, not periodic.

### Measured stop (icount shift 6, 420 s)

`probeGUIC` records the client-2 method for stages 1, 2 and **3**, and the
`GuiLvEv1` registration with `r0=2, r1=0xEC=236` at the stage-3 block (the same
arguments as the earlier wall-clock `GUILVEV_REGISTER` receipt).  The same
window records **no** F0 router, no owner callback and no `0x1046bf0c` start.

`probeLV` (420 s) watches the live-view gate `0x1053DBA2`, its five condition
return sites, its only three callers (`0x1053DC48`, `0x1053DCBE`, `0x1053DEAE`),
the event post `0x1062E474`, the owner and every `0x1046bf0c` call site.  **All
are zero-hit.**  The gate is not even entered, so the question is not which
condition fails; the GUI event that should call the handler has not arrived.

The handler containing the three call sites is `0x1053DC04`.  It takes a small
event descriptor `r4` (`[r4]` = mode 0/1, `[r4+1]` must be 1, `[r4+4]` a value
compared with 2000), checks the GUI mode byte at `0xB181D970+2`, and calls the
live-view gate when the pending flag at `0xB181D970+0x28` is set.  It is an
event handler, not periodic code.

### The event ids that drive it (verified code)

The caller is the GUI event dispatcher at `0x1053E19A`.  It fetches one event
per id through `0x104BDDE0(id, &data)` and calls the matching handler:

```text
0x3B (59) -> 0x1053DC04   (live-view handler; called at 0x1053EBC6)
0x32 (50) -> 0x1053EC74 -> 0x10632916(12); set event 59; 0x104E63BE;
                          clear event 59   (the only set-site for 59)
0x2F,0x30,0x31,0x33..0x3A,0x41.. -> other GUI handlers
```

So the live-view gate needs **GUI event 50 (0x32)**: the dispatcher translates
it into a one-shot event 59 in the next iteration.  A scan of the whole image
for `movs r0,#50` plus the kernel event-set call finds only this dispatcher
site, so event 50 is posted with a computed id (or by the event framework),
not a literal immediate.

### The event-50 producer (verified)

The GUI event post API is `0x104BD99E(id, value)`.  It stores the value word at
`0xB201C984 + 4*id` and sets the flag byte at `0xB201BDD0 + 4*id`; the get
`0x104BDDE0` returns the flag and clears it.  Scanning every `BL 0x104BD99E`
call site for its `r0` finds exactly one site that posts id 50:

```text
0x1062ED9E  movs r0,#50 ; bl 0x104BD99E
```

It sits inside `0x1062ECDE` and only runs after `0x1062ED6E` fetched event 50
and `sub_1062E9B4()` reported state 3 and then state 5 (`0x1062ED88` stores 1
at `[r6+4]` for state 3, `0x1062ED94` sets r5=1 for state 5).  So the live-view
start depends on the display-mode machine behind `0x1062E9B4` /
`0x1062E852` reaching state 5, which then reposts event 50.

`probeEV50` (420 s) records that entry twice but neither mode check
(`0x1062ED84`, `0x1062ED90`) nor the id-50 post (`0x1062ED9E`) runs; the
live-view gate and the owner stay zero-hit.  Because the `0x1062ED9E` post is
only reachable when event 50 was already present and the mode machine reported
5, the **initial** event-50 producer is a different `0x104BD99E` call site with
a computed id.  The next measurement either reads the mode machine
`0x1062E852`/`0x1062E9B4` at its nine call sites or narrows the 36 non-literal
post sites to the display one.

### Current frontier

The GUI client is initialised at stage 3 and registers the live-view event, but
GUI event 50 never arrives, so the live-view start gate never runs and the
display provider is never requested.  The frontier is now the producer of GUI
event 50 (and the `0x10007`/`0x1000A`/`0x1000B` family that feeds it), not the
AppC message framework and not EGL.

## The GUI mode block at 0xB181D970 is never written

### The block and its consumers (verified code)

| Offset | Meaning | Evidence |
|---|---|---|
| `+0` | mode request (`1`, `2`, `3`) | `0x1053FA1E` switches on it and maps `1 -> event 0x10002`, `2 -> 0x10003`, `3 -> 0x10009`, then clears it |
| `+2` | current mode byte | live-view handler `0x1053DC04` stores the event descriptor's first byte here at `0x1053DDA2`; the start branch requires `+2 == 3` |
| `+0x10` (dword) | `0x006B006A` static template value | read at the first draw |
| `+0x28` | pending flag | the handler calls the live-view gate only when it is nonzero |
| `+0x2C` | clear/gate input | the gate requires it to be `0` |

The GUI step dispatcher `0x105424E8` runs step 9 -> `0x10542486`, which first
calls `0x1053FA1E`.  While a mode request is pending that call returns 1 and
the GUI event dispatcher `0x1053E19A` is **skipped for that step**; it then
clears `+0`.  So `+0` is the hand-off from the mode-request producer to the GUI
event pass.

`sub_1062E9B4()` (the state read used by the event-50 handler) walks a five
entry RAM table at `0xB181AB54` (entry = `{u8 mode, pad, pad, pad, fn}`), calls
each `fn`, classifies through `0x106DDBE0`, and returns the first entry's mode
byte whose class is not 4.  The mode bytes are data, not computed.

### Measured (icount shift 6, 420 s)

- At the first app draw the block is `+0..+0xC = 0`, `+0x10 = 0x006B006A`,
  `+0x14.. = 0`, `+2 = 0`, `+0x28 = 0`.
- Watchpoints on `+0` (u32) and `+2` (u16) record **no write** in 420 s.
- `0x1053E19A` (GUI event dispatcher) is zero-hit; the only mailbox fetch seen
  is one `id=0x31 flag=0` from another caller.
- Watchpoints on the GUI event mailbox for ids 50 and 59 (`0xB201BE98`,
  `0xB201BECC`, data `0xB201CA4C`) are **zero-hit**: neither event flag is ever
  set to 1.
- `0x1062E9B4` (mode read) is zero-hit.

### Reading (inference)

The GUI display-mode machine is completely dormant: no mode request reaches
`0xB181D970`, so `+2` stays 0 (the start branch needs 3), no event 59/50 is
posted, and the live-view gate and the display provider are never requested.
The producer of the `+0` mode request is therefore the next target; it is the
bridge between the camera state (EX host commands / power classification) and
the MAS GUI.  The event-50 self-loop at `0x1062ED9E` cannot bootstrap it,
because it only runs after event 50 was already present.

This is consistent with the EX-side frontier in
[`../validation/frontier.md`](../validation/frontier.md): the display start is
not blocked by EGL, by the app message framework, or by the display owner's
own code, but by a camera-state transition that never reaches the MAS GUI.

## The GUI view-state chain (event 0x39 -> mode request -> live view)

### The writers of the mode request (verified code)

`0x105405A2` is GUI step 1 (`0x105424E8` case 1).  It reads the view state
`0xB1821DBC` through `0x1054315E` and writes the mode request byte:

```text
0x1054073A  if (sub_1063D32C(r7) != 0 && r6 == 0):
              set kernel event 135 = 1
              state = 0xB1821DBC; clear 0xB1821DBC to 0
              25 -> 3, 26 -> 2, 28 -> 1, else -> 0
              strb mode -> [0xB181D970]   (0x10540762/66/6C, clear at 0x1054075A)
```

The view state is set to **25** by `0x1053DF9E(desc)` at `0x1053E05C`, which
requires `sub_1063385A() != 0`, `desc[0] <= 1` and `desc[1] == 1`; it also
programs `0x10634088(.., 0x2000000)` on that path.  A second branch sets the
live-view mode byte directly (`0x1053DFC4: [0xB181D972] = 1`).

`0x1053DF9E` is called only from `0x1054265A`, the descriptor handler for GUI
**event 0x39 (57)**, which requires `desc[0] == 3` and `desc[1] == 1` and then
calls `0x1053E072`, `0x1053DF9E`, `0x10542DA6` and `0x10630B10` in order.
`0x1054265A` is called from the GUI event dispatcher `0x1053E19A` at
`0x1053EB8C` after fetching event 0x39 through `0x104BDDE0`.

### The GUI step driver does not run (measured, icount shift 6, 420 s)

`probeGUISTEP` watches the GUI step dispatcher `0x105424E8`, mode step
`0x105405A2`, the event-0x39 descriptor handler `0x1054265A`, the state-25
setter `0x1053DF9E`, the GUI event mailbox and the owner.  **All are zero-hit**
except one unrelated `GET id=0x31 flag=0`.  Event 0x39 is never fetched, so the
whole chain above never starts.

`0x105424E8` is referenced only by the ROM method table entry at `0x123B79C0`
next to `0x105425C5`/`0x105425F7` and `0x105408FB`/`4`; no `BL` or literal
reaches it.  The GUI descriptor around it is a ROM template:

```text
0x123B7680  0x01E002D0 0x01E002D0 0x64 0x10 0x11..0x1B   (720x480, mode ids)
0x123B7700  0x04120000..0x0412001E, 0x04120100..0x04120105
0x123B7904  0x006B006A ..                                 (runtime block template)
```

The `0x006B006A` at `0x123B7914` is exactly the runtime value seen at
`0xB181D980`, so the runtime mode block is a copy of this template.  The GUI
step machine is registered (table at `0x123B7978..0x123B79D0`) but no driver
invokes it.

### Conclusion

The chain from GUI event 0x39 to the live-view gate is fully mapped, and the
missing link is now unambiguous: **the GUI application's step machine is never
driven**.  That driver is the next target; it is upstream of every remaining
display gate, including the mode request, the live-view event, the display
provider and the owner start.  The display code itself is complete and idle.

## The GUI worker task exists in RAM but is not runnable

### The runtime object (direct observation, RAM dump at the first app draw)

A GDB `dump memory` of `0xB1800000..0xB1830000` at the first app draw
(`0x10569C76`) contains the runtime GUI object:

```text
0xB181D970  mode block (all zero; +0x10 = 0x006B006A from the ROM template)
0xB181D9B8  {0x010000ED, 0x103FAB3B}   event id + worker main
0xB181D9C0  0x1C 0x2000 1             priority / stack / state
0xB181D9D8  {0x010001BE, 0x10204DA9}   second event id + handler
0xB181D9E4  3
0xB181D9E8  method table copied from ROM 0x123B797C:
            0x1053CFA9 0x1053CFB9 0x1053D2FB 0x1053D357 0x1053D367
            0x1053D50F .. 0x1053D72F, 4, 0x105408FB, 0x105424E9,
            0x105425C5 0x105425C5 0x105425F7
```

The ROM table at `0x123B797C` is therefore copied into RAM at `0xB181D9E8`
(entry 17 is the step dispatcher `0x105424E9`).  The same dump contains five
copies of the worker main `0x103FAB3B` at `0xB181D8A8`, `0xB181D9BC`,
`0xB1824EA4`, `0xB1828068` and `0xB182810C`, each followed by
`0x1C`/`0x2000`/`1` and a `0x10204DA9` handler registration.

### The worker main (verified code)

`0x103FAB3A` is a noreturn task body:

```text
call  [ctx+48]                       (init method, may be null)
sub_103FAB24(ctx, 1, [ctx+44])       (calls the [ctx+36] hook)
loop:
    sub_103610DA(ctx+32, 1, &v)      -> sub_10104A14(*(u32*)(ctx+32), 1, 1, &v)
    sub_103610D2(ctx+32, 1)          -> sub_101043A4(*(u32*)(ctx+32), ~1)
    call [ctx+52]  (per-iteration step)
    sub_103FAB24(ctx, 2, [ctx+44])
    call [ctx+56]
    call [ctx+64]
```

So `*(u32*)(ctx+32)` is the RTOS wait handle and `[ctx+36]` the hook.  For the
GUI object at `0xB181D9B8`:

```text
ctx+0x00 = 0x010000ED  event id
ctx+0x04 = 0x103FAB3B  worker main
ctx+0x08 = 0x1C        priority
ctx+0x0C = 0x2000      stack size
ctx+0x18 = 0x010001BE  registration id
ctx+0x1C = 0x10204DA9  hook
ctx+0x20 = 0x010001BE  wait handle          <-- the worker blocks here
ctx+0x24 = 3
ctx+0x30 = 0x1053CFA9  (= method table entry 0, then entries 1..20)
```

The worker therefore waits on RTOS event handle `0x010001BE` (via
`0x10104A14`) and calls GUI method-table entries as its init/step bodies.

### CPU state at the same moment (direct observation)

Five vCPU threads are exposed by the QEMU gdbstub:

| Thread | CPU | PC at first draw | Meaning |
|---|---|---|---|
| 1.1 | MAS core 0 | `0x10569C76` | app UI draw (running) |
| 1.2 | MAS core 1 | `0x10107078` | RTOS scheduler idle loop `sub_10107060` (current-task slot `0xB2B41604[cpu] == 0`) |
| 1.3/1.4 | SLA cores | `0x10038A4C` / `0x10038A48` | SLA idle |
| 1.5 | EX | `0x0803DD96` | EX code |

### Reading

The GUI worker context is fully populated (worker main, callbacks, stack size),
but MAS core 1 has no runnable task: the GUI worker is either blocked on its
`a1+32` event or not yet created.  Nothing observed so far signals that event.
The next measurement is the worker's task context and its wait object, then the
signaler; that signal is the last software link before the mode request.

This also explains why every earlier probe in the chain was zero-hit without
any model diagnostic: the code is idle, not failing.

### The worker blocks on a kernel object, not on a model register

The wait is a microkernel object, not a device poll.  The worker's context field
`ctx+0x20` is filled by `0x10361082`:

```text
0x10361082: v = {0, 8, 0, 0}; *a1 = sub_10104414(v)   // SVC 6, create
0x103610DA: sub_10104A14(*(u32*)ctx_32, 1, 1, &v)     // wait
0x103610D2: sub_101043A4(*(u32*)ctx_32, ~1)           // clear
0x103610CC: sub_10104844(*(u32*)ctx_32)               // the give a task would use
```

`sub_10104414`, `sub_10104A14`, `sub_101043A4` and `sub_10104844` are one-SVC
thunks (`SVC 6` at `0x10104424` and `0x10104A24`), so the wait object belongs to
the guest kernel; the model sees only the handle.  A watchpoint on `ctx+0x20`
(`0xB181D9D8`) catches the create storing `0x010001BE` from PC `0x1036109e` and
nothing else, so the value is a fresh kernel handle, not a device status word.

`0x103FAB24` is the hook relay (`v = *(u32*)(ctx+36); return v ? v(phase, arg) : 0`)
and `ctx+36` holds `0x10204DA9`; `ctx+44` holds `3`, which the relay passes as the
argument at every phase.  The hook is a notification path, not a wait.

### The frame-slot table shows create/make-current without present (measured)

`0x10569C76` does not draw pixels: it builds a descriptor for the caller's buffer
and hands it to `0x10568BDE`, which creates an EGL window surface for that buffer
(`sub_106B260C([0xB1800A08], width, height, format, attrs, ...)`).  Surviving
frames live in a 0x50-entry table of 24-byte slots at **`0xB1CE1BCC`**:

```text
slot[i] +0  valid/kind      0x10568BDE sets it after a successful create
        +4  buffer pointer
        +8  width<<16|height and format bytes
        +12 EGL display        0xB1800A0C
        +16 EGL surface        the value eglSwapBuffers takes
        +20 owner flag         0xB1800A00 / 0xB18009FC bookkeeping
0x10569018(i)  slot i valid and i < 0x50
0x10568F60(i)  PRESENT: 0x10680B8C([0xB1800A0C], slot[i]+16)   == eglSwapBuffers
0x10568EA4(i)  RELEASE: 0x1068039C(display, slot[i]+12),
               0x1067F2A8(display, slot[i]+16), 0x106B254A(...), clear slot
```

Coverage over 120 s unthrottled shift 6 (`run-df.sh probeFRAME`) answers "why is
`eglSwapBuffers` never called":

| Function | Role | Executions |
|---|---|---|
| `0x10568BDE` | create window surface for a frame buffer | 8 |
| `0x10568F1C` | `eglMakeCurrent` wrapper | 21 |
| `0x10569018` | slot validity helper | 25 |
| `0x105693E2` | neighbouring EGL helper | 1 |
| **`0x10568F60`** | **present slot -> `eglSwapBuffers`** | **0** |
| `0x10568EA4` | release slot | 0 |

So the app creates surfaces and makes them current, but never presents and never
releases: slots are allocated and left behind.  The present wrapper is the only
caller of `eglSwapBuffers` in the image (`0x10568F7C`), and it has no `BL`, no
pointer literal and no computed reference (2-byte-aligned Thumb scan plus dword
scan of the whole component, including the inner image at `0xB1000000`).
`eglDrawFrameModeDMP` and `eglCopyBuffers` have no caller either.

### The display-service start is entered but its admission loop never runs

Provider admission `0x104D7E78` is called from exactly one place inside the
display service: the poller `0x10637B54`, which walks a request-code table

```text
0x10637b54  push {r4, lr}
0x10637b56  ldr  r4, =0xB181D380
0x10637b58  ldr  r0, =unk_1094828C
0x10637b5a  ldr  r1, [r4, #0x58]          cursor in the request-code table
0x10637b5c  ldrb r0, [r0, r1]
0x10637b5e  bl   0x104D7E78               provider admission(code)
0x10637b62  ldr  r0, [r4, #0x58]; add #1; str r0, [r4, #0x58]
0x10637b68  cmp  r0, #0xF; bcc 0x10637B70; movs r0,#0; str r0,[r4,#0x58]
0x10637b70  b    0x10637454
```

The poller is called from `sub_1062EDDC`, the display-service start, right after
the ready setter thunk `0x10637B72` (which tail-branches to `0x104D7F16`):

```text
0x1062f102  cmp  r4, #0; bge 0x1062F1A0      <-- argument gate
0x1062f106  bl   0x1062E9B4
0x1062f10e  bl   0x104BF8F8 (r0=3); cbz r0, 0x1062F12E
0x1062f116  bl   0x106957B4; bl 0x10637B48
0x1062f120  bl   0x10637B72                  <-- ready
0x1062f124  bl   0x10637B54                  <-- admission poller
0x1062f130  bl   0x104BF8F8 (r0=4); cbz r0, 0x1062F140
0x1062f136  bl   0x10637B54                  <-- admission poller again
0x1062f142  bl   0x104BF8F8 (r0=0); cbz r0, 0x1062F16E
```

Measured, 90 s unthrottled shift 6 (`probeDMSVC3`):

| Address | Role | Executions |
|---|---|---|
| `0x1062EDDC` | display-service start | 1 |
| `0x10637B54` | admission poller | **0** |
| `0x104D7E78` | admission | 0 |
| `0x104D7F16` | ready setter | 0 |
| `0x1053D2FA` | GUI method 2 (calls the start) | 0 |
| `0x104DC804` | display owner | 0 |
| `0x1046BF0C` | banks start | 0 |

So the display service is *entered* once and then leaves before ready/admission:
the argument gate `r4 >= 0` or one of the `0x104BF8F8` probes short-circuits it,
and none of the seven `BL` callers of `0x1062EDDC` executes at all
(`0x10437A74`, `0x1043DE7C`, `0x1044E58A`, `0x1053D300`, `0x10656952`,
`0x1070725A`, `0x1073EFEC`, all zero).  The single entry is therefore a
fall-through or a branch from the preceding block, not a call; that discrepancy
is recorded rather than explained, and it is the next measurement.

Whichever branch it is, the consequence is the same: ready, admission, the
provider mailbox, the owner and the bank start are all downstream of a gate that
this boot never opens.  This is the concrete display-side equivalent of the
EX template-zero gates in [`../validation/frontier.md`](../validation/frontier.md).

### RETRACTED: "the display state machine stops at state 1 because the mode is 2"

> **Retracted.** This object belongs to the `CARD0` (SD-card) subsystem, not to
> the display; see "Correction: the 0x1042Cxxx/0x1042Dxxx cluster is the
> device-subsystem dispatcher" below.  The measurements are accurate, the
> attribution is not.  The section is kept only as a record of that object's
> behaviour.

Original (mis-attributed) section title: "The display state machine stops at
state 1 because the mode is 2 (measured)".

The display state machine is `0x1042D086(obj, new_state)`.  It stores the new
state at `obj+29`, notifies `[obj+20]` and `[obj+16]` with
`([obj+56], old_state, new_state)`, and then advances by itself:

```text
case 1:  obj+32 = 0
         if (*(u8*)(obj+28) == 1) { new_state = 2; continue; }   <-- arming advance
         else if (obj+40) recurse(obj, 6)
case 2:  mode == 1 -> 0x1042D05C(obj, ...)      mode == 2 -> LABEL_14
case 3:  obj+48 == 0 -> new_state = 4
case 4/5: obj+32 != 0 -> new_state = 6
```

`obj+28` is the **display mode**.  A watchpoint run (`probeMODE3`) catches both
mode writes and both object builds:

```text
MODE1-W#1  pc=0x1042D1DC  val=0x2  lr=0x1042D2BA    object 0xB209862C mode := 2
OBJ2-BUILD#1 pc=0x1042D1F4        lr=0x1042E9BC    object 0xB185DE6C built
MODE2-W#1  pc=0x1042D1DC  val=0x2  lr=0x1020F8DC    object 0xB185DE6C mode := 2
```

and a `-d cpu` capture shows both objects entering the machine with
`new_state = 1`:

```text
0x1042D086  a1=0xB209862C  a2=1  lr=0x1042D2BA
0x1042D086  a1=0xB185DE6C  a2=1  lr=0x1020F8DC
```

Both therefore take `case 1`, find `mode == 2`, and stop.  State 2 - the only
state that posts the attach event - is never reached.

Mode can be written two ways:

| Setter | Behaviour | Executions (45 s) |
|---|---|---|
| `0x1042D72E(obj)` | `0x1042D1DA(72*idx + base, 2)` - hardcoded 2 | 2 |
| `0x1042CCA0(obj, a2, a3)` | `if (a2 == 0) mode = 2 else mode = 1` | **0** |

So the only mode that ever gets written is the teardown/idle mode 2, and the
selector that would pick mode 1 (`a2 != 0`) is never called.

The request handler `0x1042E7FA` agrees: its single invocation is

```text
a1=0xB185DE64  a2=0  a3=1  a4=0x1042E7FB  lr=0x1042D0A8
```

`a3 == 1` is the "disable" request and `a2 == 0` makes its `if (a2)` branch
false, so the handler is a no-op.  Request type `2` - the one whose
`case 2` posts event 0 and arms the display - is never issued.

### Panel observer: the composition buffer is now scannable (model addition)

The model had no way to observe the composition buffer, so every earlier
statement about pixels was a RAM dump.  `hw/nikon/devices/panel.c`
(`TYPE_NIKON_PANEL`, `nikon-panel-functional`) now samples the guest-owned
composition region on a virtual-clock cadence and publishes it to a QEMU
console:

```text
properties   base (0xbf309600), width 1024, height 688, skip-rows 6,
             interval-ns 33333333
source       guest RAM only, mapped read-only through address_space_map
format       the transfer device's documented RGBA8888 program
             (1024x688 with six inactive top rows)
publish      qemu_graphic_console_create + qemu_console_resize +
             composite into qemu_console_surface()->image +
             qemu_console_update_full, on sampled-content change only
```

It adds **no** guest register, IRQ, completion or status, and never writes guest
memory: the guest cannot observe the observer.  The machine instantiates it next
to the `z7ii.lcd-composition` region.

Receipts:

- `tests/unit/nikon/panel.c` — the RGBA8888 -> `0xAARRGGBB` conversion is a pure
  byte reorder (alpha copied, no premultiplication): 2 tests, TAP `ok`.
- A live run with `-qmp` and QMP `screendump` produces a `1024x682` P6 PPM from
  the console (measured 20 s and 60 s after boot), and its content is all black:
  the guest-owned composition buffer is still zero, which matches every earlier
  RAM dump.

So the observer works and immediately confirms the negative: the firmware never
writes a pixel into the composition buffer in this boot.

### Key assistance does not drive the chain (measured)

The model already maps seventeen host keys onto the EX GPIO pads
(`hw/nikon/devices/input.c`).  A 60 s assisted run (`work/runs/ex-6b-ercd/
run-assist.sh`) pressed `F1..F5` and `1..3` in four rounds at 8/16/24/32 s
through QMP `input-send-event`, with a `-dfilter` over the display frontier and
a panel screendump at the end:

| Address | Role | Executions |
|---|---|---|
| `0x10569C76` | app draw | 27 |
| `0x10568F60` | EGL present wrapper | **0** |
| `0x1053E19A` | GUI event dispatcher | **0** |
| `0x104DC804` | display owner | **0** |
| `0x1046BF0C` | banks start | **0** |
| `0x104BD99E` | mailbox post | **0** |
| `0x105A6906` | frame queue submit | **0** |
| `0x104C6B14` | service request router | **0** |
| `0x10502D0A` | F091 handler | **0** |
| `0x1009ACA0` | SLA readout ISR | **0** |

and the panel frame stays entirely black.

The input path itself is verified: `hw/nikon/devices/input.c` now logs every key
that reaches a pad, and the same run records

```text
nikon-input: pad=0  ex-pad=0x0a down active-low level=0
nikon-input: pad=0  ex-pad=0x0a up   active-low level=1
nikon-input: pad=1  ex-pad=0x11 down active-low level=0
nikon-input: pad=12 ex-pad=0x6e down active-low level=0
```

so the host keys do drive the seventeen EX pads.  The negative is on the EX
side: it receives the pad levels but does not turn them into a host command that
MAS would act on.  The EX key profile is therefore a working assistance channel
whose consumer is inactive, not a broken channel.  The remaining
assistance candidate is a host command injected on the MAS host-command lane
itself, which needs the frame protocol that this investigation has not yet
recovered.

### The only display messages posted are channel teardowns (measured)

The event handler wrapper `0x10299A80` has no `BL` caller and no pointer
literal; a `-d cpu` capture shows all three of its calls come from the single
site `0x1042C5D6` inside `sub_1042C550`:

```text
0x1042c5d0  ldrb r0, [r5, r4]     ; channel from a 6-entry table
0x1042c5d2  mov  r2, r7           ; payload pointer
0x1042c5d4  mov  r1, r6           ; event code
0x1042c5d6  bl   0x10247FB4       ; post (wrapper -> dispatcher -> 0x104BF4AE)
0x1042c5da  adds r4, #1
0x1042c5dc  cmp  r4, #6           ; loop over six channels
```

and `r6` (the event) is compared with `5` just before: when it *is* 5, the code
first runs a per-channel disable helper chosen by channel class
(`0/1 -> 0x1042C922`, `2/3 -> 0x1042E630`, `4-6 -> 0x1042D70C`), and may then
skip the post when that helper returns 1.  So **event 5 is the teardown path**.

All three captured messages are teardowns:

```text
(2, 5, 0xb185de5c)   (5, 5, 0)   (6, 5, 0)
```

The boot therefore posts only "disable channel" notifications; the attach event
`(0 or 1, 0)` that arms the display service is never posted anywhere in the
window.

The post API `0x10247FB4(channel, event, ptr)` has exactly four call sites in
the image, and coverage over 40 s shows only the teardown one runs:

| Poster | Function | Runs |
|---|---|---|
| teardown loop (event 5) | `0x1042C550` | 1 |
| `0x1042D028` | forwards `(arg3, arg2, arg1+17)` | 0 |
| `0x1042D4B6` | forwards `(arg4, arg1, 0)` after `0x1042D794` | 0 |
| `0x1042E744` | forwards `(kind, arg1, ptr)` after `0x1042EA50` | 0 |

So the display message producers that could attach or arm a channel exist and
are simply never invoked; the only producer that runs is the teardown loop.

### The display event channel never delivers the arming event (measured)

The function that gives the missing context-2 object is the display event
handler `0x104BF4AE(channel, event, ptr)`.  It stores `event` in a per-channel
table and switches:

```text
case 0/1:                               channel 0 and 1 are the arming channels
    switch (event)
      case 0:                           <-- the arming event
          if (0x10631F9E(...)) {
              0x10634088(0x10634050(), 0x20000);   give context 2
              0x10634088(0x10634030(), 0x200000);  give context 1
          }
          ...
      case 2/5:  0x10634088(0x1063406C(), 256);     give context 3
      case 3:    ... give context 1 with 8
case 2:   switch (event) ... give context 1 with 4 / give context 3
```

A `-d cpu` capture of the handler over 45 s unthrottled gives **three** entries
in total:

```text
channel(r0)=2  event(r1)=5  ptr=b185de5c   lr=0x10299AA4   (dispatching)
channel(r0)=5  event(r1)=5  ptr=0
channel(r0)=6  event(r1)=5  ptr=0
```

All three carry event `5`; the only event ever delivered on channel 2 takes the
`give context 3` branch, and channels 5/6 are outside the switch.  **Channel 0
or 1 with event 0 is never delivered**, so the arming path - and therefore the
context-2 give - cannot run.  That matches the give histogram exactly: context 3
is given once, contexts 1 and 2 not at all through this handler.

The caller is a message dispatcher (`0x10299A80 -> 0x10360882`), so the display
arming waits on an *event message* that this boot never produces.

### The RTOS kernel calls resolve and the two blocking objects are never given

The `r12` descriptor each stub loads is also the value the kernel indexes with.
In `0x10107624` the dispatch computes `sl = r1 >> 16`, bounds it against
`0xFFFF807E`, and jumps through the table base `0x10127428`; because `sl` is a
sign-extended value in `0xFFFF80xx`, the effective entries land *below* that
base, in the ROM table at `0x101074D8..0x101074E8`:

```text
create  0x802C0100  ->  0x10110D34
give    0x802E0200  ->  0x101110EC
clear   0x802F0200  ->  0x1011131C
wait    0x80300500  ->  0x10111410
```

Coverage over 45 s unthrottled shift 6 (`run-df.sh probeKCALL`) shows these are
the live RTOS paths, not a dead table:

| Kernel call | Handler | Executions |
|---|---|---|
| create | `0x10110D34` | 568 |
| **give** | `0x101110EC` | **173965** |
| clear | `0x1011131C` | 7915 |
| **wait** | `0x10111410` | **33276** |

The kernel therefore signals constantly.  What it never signals is the two
objects this chain needs.  A `-d cpu` capture of 87293 give calls in 25 s (83
distinct objects, `probeGIVE`) gives the object histogram:

```text
0x01000008  83433      0x01000020   1696      0x01000007    726
0x0100003c    343      0x010000a9    330      0x010000ee    257
0x010001c4     68      0x0100014a     56      ...
```

and the two objects that block the chain are absent from it:

| Object | Role | Gives in 25 s |
|---|---|---|
| `0x010001BE` | GUI worker wait object | **0** |
| `0x010001C2` | display-service gate object | **0** |
| `0x010001C1` / `0x010001C3` / `0x010001C4` | neighbouring display objects | 3 / 1 / 68 |

So the blocker is now a pair of *specific missing kernel gives*, not a stalled
kernel, not a dead device and not firmware data: sibling display objects of the
same family are given, and these two never are.  The wait on a never-given
object returns with no flags, and `0` flags is exactly the `r4 == 0` the display
gate needs to be negative.

### The guest kernel lives in the MAS image and its SVC path is healthy

The wait/create/give stubs are ARM, not Thumb, and they pass a **descriptor
pointer in `r12`** before `SVC 6`:

```text
0x10104414  push {r4}; add r4, sp, #arg_0; push {lr}
0x10104420  ldr r12, =0x802C0100     create
0x10104424  svc 6
0x10104848  ...                      ldr r12, =0x802E0200   give
0x10104A14  ...                      ldr r12, =0x80300500   wait
0x101043A8  ...                      ldr r12, =0x802F0200   clear/mask
```

The reset path installs the vector base before any task runs:

```text
0x1002bd20  ldr r0, =0x10100000
0x1002bd24  mcr p15, 0, r0, c12, c0, 0     ; VBAR = 0x10100000
```

so the exception vector table is at VA `0x10100000` in the MAS image.  Coverage
over 45 s unthrottled shift 6 (`run-df.sh probeVEC3`):

| Address | Role | Executions |
|---|---|---|
| `0x10100000` | vector-table block start | 0 (entered at `0x10100008`) |
| `0x10100008` | SVC vector (`ldr pc,[pc,#0x14]` -> `0x10103560`) | **417421** |
| `0x1010000c` / `0x10100100` | other vector slots | 0 |

The SVC dispatcher at `0x10103560` extracts the SVC immediate, subtracts 4, and
jumps through the table at `0x10103594`; `SVC 6` therefore reaches
**`0x10107624`**, which reads `MPIDR` (per-CPU), indexes a per-CPU current-task
slot at `[0x1010711C]`, and finally dispatches through a syscall table pointer
loaded at `0x101077E0` (`ldr pc,[ip, sl, lsl #2]`).

Reading: the guest kernel is ordinary guest code inside the MAS image with an
identity-mapped ROM vector base; it executes hundreds of thousands of syscalls
per window and schedules the tasks that do run.  A `0` reply from the
display-gate query is therefore kernel/object state produced by this firmware,
not a missing device model and not a decode defect.

### The transfer engine is only configured, never used (measured)

Watchpoints over the classic transfer register block at `0x20180000` for 60 s
unthrottled shift 6 (`probeXFER2`) record **no write at all** to source
`+0xC00`, destination `+0xC04`, control `+0xC10` or start `+0xC18`, and the
earlier stream watch records no `0x10D0..0x10EC` descriptor and no `0x01000000`
kick.  The only guest writes in that window are the setup latch
`+0x1214 <- 0x0100FFFF` / `0xFFFF` from `0x106EE3C8`, i.e. exactly the
initialization the model documents (`setup_values[7]`).

So the transfer stage is configured and then never asked to move a byte: no
classic job, no stream job, therefore no completion and no IRQ129 in this boot.

### The display service arms behind a guest-kernel reply

`-d cpu` with `-dfilter 0x1062EDDC+0x3C4` logs the full register state of every
executed block inside the function, which turns the branch question into a
direct reading (`work/runs/ex-6b-ercd/probeCPUF`, 60 s unthrottled):

```text
pc=1062eddc r4=b181d8a4 r0=10437a6f lr=10437a79   entry, called from 0x10437a74
pc=1062edf4 r4=ffffffff r0=b181c564               r4 <- r8 = -1
pc=1062ee02 r4=00000000 r0=00000000               r4 <- result of sub_106340D4
... r4 stays 00000000 through every block ...
pc=1062f102 r4=00000000 r0=00000000 lr=1063280f   cmp r4,#0 ; bge taken
```

Static confirmation: inside `0x1062EDDC..0x1062F1A0` only two instructions write
`r4` - `0x1062EDE8 mov r4,r8` (`r8 = -1`) and `0x1062EDFC mov r4,r0` - and the
path between them is straight-line, so `r4` at the gate is always the return of
`sub_106340D4(sub_10634050(), -1, 0)`.

`sub_106340D4` returns an **out-parameter**, not a constant (IDA's first
decompile is misleading):

```text
0x106340d4  push {r1-r7, lr}; mov r4, r0 (context pointer); mov r6, r2; mov r7, r1
0x106340d8  r0 = 0xB181C55C; ... classify the context (id 1/2/3)
0x10634100  movs r0, #0x57; bl nullsub_248      (trace hook 87)
0x10634114  ldr r0, [r4]                        (*context = the kernel object word)
0x10634116  mov r3, r6; mov r1, r7; add r2, sp, #0xC
0x1063411a  bl sub_10361116                     (SVC 6 wrapper, writes the out word)
0x10634122  ldr r0, [sp, #0xC]; pop {r1-r7, pc}  returns the kernel's out value
```

The three display contexts are ordinary guest RAM objects holding kernel
handles (dump of the same run):

```text
0xB181C558  0x010001C1   0xB181C55C  0xB181C558     context 1
0xB181C560  0x010001C2   0xB181C564  0xB181C560     context 2
0xB181C568  0x010001C3   0xB181C56C  0xB181C568     context 3
```

so the arming block is gated on a **microkernel reply** for handle
`0x010001C2`, not on a device register and not on firmware data.  In this boot
the reply word is `0`, the guard takes the `bge` exit, and ready/admission never
run.  Handle `0x010001BE` (the GUI worker's wait object) belongs to the same
allocator range, which is why the two frontiers are structurally identical.

Nothing here may be faked: the reply is produced by the guest kernel for its own
object, so the next measurement is what operation the wrapper issues and which
state of the object makes the reply negative.

### Measured EGL usage map: setup only, never present (120 s, unthrottled)

The EGL dispatch table at `0x1239A9F8` holds 49 `{name, handler}` pairs (first
pair at `0x1239AA08`, table header `{0, 0x1209BEB0, 0x1209BCD4, 1}`).  Coverage
of every present-capable entry and the setup entries, 120 s unthrottled shift 6:

| EGL entry | Handler | Executions |
|---|---|---|
| `eglChooseConfig` | `0x1067DBDA` | 12 |
| `eglGetConfigAttrib` | `0x1067E414` | 8 |
| `eglCreateWindowSurfacePointerDMPX` | `0x106815A4` | 12 |
| `eglMakeCurrent` | `0x10680426` | 19 |
| `eglCreateWindowSurface` (plain EGL) | `0x1067EA28` | 0 |
| `eglCreatePbufferSurface` | `0x1067EE3A` | 0 |
| `eglDestroySurface` | `0x1067F2A8` | 0 |
| `eglQuerySurface` | `0x1067F332` | 0 |
| `eglWaitGL` / `eglWaitNative` | `0x10680AFE` / `0x10680B12` | 0 / 0 |
| **`eglSwapBuffers`** | `0x10680B8C` | **0** |
| `eglCopyBuffers` | `0x10680CFE` | 0 |
| `eglDrawFrameModeDMP` | `0x106810BE` | 0 |
| `eglQueryFrameDMP` | `0x10681164` | 0 |
| `eglGetProcAddress` | `0x10680F60` | 0 |

So the application selects a config, creates a DMPX window surface per frame and
makes it current, and then stops: no present, no copy, no frame-mode draw, no
surface destroy, and no run-time `eglGetProcAddress` at all.  Twelve surface
creations against nineteen `eglMakeCurrent` calls show surfaces being reused and
re-bound rather than queued for presentation.

**Pointer parity matters here:** the table stores Thumb pointers, so every
handler is odd.  A `-dfilter` range at the odd address matches no translated
block and silently reports zero; these numbers use the even instruction
addresses.  The earlier zero-hit EGL probes used even addresses, which is why
they agree.

`eglSwapBuffers` itself (`0x10680B8C`) is a validator: it checks display `1|2`,
`sub_106B161A(display+24, surface)`, `nullsub_273(surface)`, display states
`12448/12449/12450` and `*(u32*)(surface+96) == 12437`, then either calls
`0x105A6906(display, cfg, surface, ..., mode, 0x8100, 1, 0x3038)` (the queue
submit) or `0x1067C99E(surface, 1)` (the commit).  `0x105A6906` records zero
executions while `0x1067C99E` runs 46 times, from other call sites in the
driver, so the driver's commit helper is exercised but no frame is ever queued
for the display through the swap path.

### The event bus is silent in the same windows (measured, two time bases)

The same runs watch the mailbox primitives: `0x104BD99E` (post) executes **0**
times while `0x104BDDE0` (get) executes once, in 180 s at shift 6 both with and
without `sleep=off`.  The GUI's external entry `0x1053CF66` (registered as the
first entry of `0xB202F728`) also executes **0** times, and so do the dispatch
helpers it would call (`0x104E61A8`, `0x104E6400`, `0x104E619A`).

The two-mode A/B is the control for the harness change: at 180 s the throttled
and unthrottled runs give the same counts (`0x1053CFA8` 1, `0x104BD99E` 0,
`0x104BDDE0` 1, `0x105424E8` 0, draw 43 vs 41), so `sleep=off` changes wall cost,
not guest behaviour at this depth.

A QMP key injection (`run-key.sh`, `F1` -> EX pad `0x0a`) leaves every count
unchanged: no post, no step dispatch, no owner, no bank start in the following
35 s.

### Consequence for the present chain

The publication path this build has is `0x10568BDE` (surface per frame) ->
`0x10568F60` (present) -> `eglSwapBuffers` -> transfer program -> IRQ129 ->
display buffer, and it stops at the second step: nothing in the image reaches
the present wrapper, and no event is posted that could reach it.  Frames are
created and abandoned, which is what the zero present/release counters and the
zeroed panel surfaces show.

### The init method registers the application callback table

`[ctx+48]` is the worker's init method, i.e. method-table entry 0
`0x1053CFA8` = `sub_104E5F48(0x1053CF66)` then `sub_10583730(...)`.  Those two
calls are the only place the GUI publishes itself to the rest of the image:

```text
0x104E5F48  0xB202F728..0xB202F78C <- 24 GUI service entry points
            (0x104E65FC(0x1053CF66), 0x1058C544, 0x105AF230, 0x105C0118 x2,
             0x104E7038, 0x104E703C, 0x104E72F4, 0x10542E28, 0x104E1BDC,
             0x105B5650, 0x104E9072 x3, 0x106589EC, 0x104EA300,
             0x10585BCC x2, 0x104E9D7C, 0x10656174, 0x104EAA40, ...)
0x10583730  0x10361034(~0xB2030060, 516)   clear the 516-byte request queue
0x1053CF66  (cmd == 4 && HIWORD(arg) == 16) -> 0x104E61A8(0),
            0x104E6400(arg), 0x104E619A(...)   the external event entry
```

So the GUI is reachable from outside only through the table at `0xB202F728` and
the `0x1053CF66` callback registered as its first entry.

### Coverage says exactly which of these run (unthrottled shift 6, 300 s)

A `-dfilter`/`-d exec` run (`work/runs/ex-6b-ercd/run-df.sh probeLONG`) over the
frontier addresses plus the ROM range of the GUI framework gives:

| Address | Meaning | Executions in 300 s |
|---|---|---|
| `0x10569C76` | app UI draw | 41 |
| `0x1053CFA8` | worker init method (entry 0) | 1 |
| `0x10542E28` | GUI service entry called by the init table | 1 |
| `0x105424E8` | step dispatcher (entry 17) | 0 |
| `0x10542486` / `0x105429D4` | step states 9 / 11 | 0 |
| `0x1053FA1E` | mode-request consumer | 0 |
| `0x1053E19A` | GUI event dispatcher | 0 |
| `0x1053DBA2` | live-view gate | 0 |
| `0x10695450` | event-0x39 poster | 0 |
| `0x104DC804` | display owner callback | 0 |
| `0x104D7E78` | provider admission | 0 |
| `0x1046BF0C` | banks start | 0 |

`0x105424E8` switches on its first argument and reaches the mode-request path
through only two arms:

```text
case 9  -> 0x10542486 : sub_1053FA1E(a1) -> on success 0x1053F42A(5,a1),
                         0x1053E19A(a1), 0x1053EE72(5), 0x1053F8C6(5),
                         0x104BDDE0(23/24) -> 0x1061E778(15, 0/1), 0x1053F158(5)
case 11 -> 0x105429D4 : sub_104E6402(a1), then sub_1053FA1E() and 0x10543442/
                         0x10543414/0x1054349E
```

Both call `sub_1053FA1E`, which is the only consumer of the mode-request word
at `0xB181D970`; it maps request 1/2/3 to `0x10002/0x10003/0x10009` and clears
the word.  The mode request therefore cannot be produced or consumed while the
worker's kernel object is not given.

### Reading

The remaining link is a single kernel-object give.  Every display gate sits
downstream of the step dispatcher, the step dispatcher is only reachable through
method-table entry 17, and entry 17 is only called after the worker's wait
returns.  Nothing executed in a 300 s unthrottled window gives that object, and
no code in the image references `ctx+0x20` outside the GUI methods themselves, so
the give is an external event delivered through the GUI callback table or the
kernel, not a device register the model can report.

## The GUI event-0x39 setter and the SLA readout

### Event 0x39 has exactly one poster (verified)

The GUI mailbox poster for event `0x39 (57)` is `0x10695450(a1, a2)`:

```text
if (a1 > 3 || a2 > 1) return;
ctx = *(u32*)0x1069570C          // 0xB181B3D4
ctx[0] = a1; ctx[1] = a2          // descriptor bytes
if (*(u8*)0xB181B5BC & 2) 0x106DDCA4()
r1 = ctx; r0 = 57; 0x104BD99E(r0, r1)   // post GUI event 0x39
0x10634030(); 0x10634088(.., 0x80)
```

The event-`0x39` handler `0x1054265A` requires `desc[0] == 3 && desc[1] == 1`,
so the producer must call `0x10695450(3, 1)`.  That function has **no `BL`
caller and no pointer literal**: it is invoked indirectly, from a callback the
caller has not been identified for yet.  Its context at `0xB181B3D4` is a mode
descriptor (runtime dump: `[0]=0, [1]=0, +0x28 = 0x13, +0x2C = 6`,
`+0x44 = 0x10204DB5`).

### The SLA readout initializes but never completes (direct observation, 420 s)

`probeSLAREAD` watches the SLA result-readout cohort:

| Address | Role | Result |
|---|---|---|
| `0x10093CEC` | SLA mode2 dispatcher | **hit** |
| `0x1009ACE2` | readout registration | **hit** |
| `0x1009AD52` | readout initialization | **hit** |
| `0x1009ACA0` | readout ISR / guest callback | zero-hit |
| `0x104DC804` | MAS display owner | zero-hit |

So the SLA readout path is initialized (the same evidence as
`docs/devices/readout/protocol.md`) but no request completion ever reaches its
ISR.  The model deliberately does not fabricate that callback.

### Combined frontier

Three independent "last links" are now measured and none is a device-model bug:

1. MAS GUI worker blocked on RTOS event `0x010001BE` (MAS core 1 idle).
2. The only GUI event-`0x39` poster (`0x10695450`) is never invoked.
3. The SLA readout initializes but its completion ISR never fires.

All three are "the firmware waits for an event that no reachable producer
sends".  The next step is to identify the caller of `0x10695450` (likely the
app-service interface at `0xB1819848` or an EX command lane) and to decide
whether the SLA readout completion requires a sensor/readout input that the
container does not contain.

## The inner image is copied, never started; the 0x39 setter has no reference

### The MAS inner image (direct observation)

The container's MAS component carries an embedded inner image at payload offset
`0x208853C` (2,117,120 bytes, `O[0x208853C,0x228CF3C)`).  The loader is
`0x104A132C`:

```text
0x10368B76(5)                                   prepare
copy(dst=0xB1000000, src=0x120A853C, len=*(u32*)0x122ACF3C = 0x204A00)
copy(dst=0x04008000, src=0x120A853C, len=64)     early handoff (vector table)
0x103610CC(0xB1807A90, 4)                       signal
```

(`0x103A3B66` is the copy helper; source `0x120A853C`, destination
`0xB1000000`, size `0x204A00`, handoff destination `0x04008000`.)

A RAM dump at the first app draw (`probeINNER`) shows the guest **did** copy it:
`0xB1000000` holds the same ARM vector table (`18 F0 9F E5` x5,
`FE FF FF EA`, then the handler table `0xB1000060`, `0xB10D0A08`, ...) and
1,879,425 of the 2,113,536 bytes are non-zero.

The image is not the GUI app: it contains none of the GUI strings
(`GuiLvEv1`, `GuiMeEv1`, `AppC_Msg`).  Its reset handler `0xB1000060` is only a
register-zeroing prologue.

A 420 s probe (`probeINNERSTART`) watches `0xB1000000` (vector base) and
`0xB1000060` (reset handler): **both are zero-hit**, while the app draws once.
So the inner image is resident but never entered at its own entry.

### The event-0x39 producer IS reachable through a veneer (corrected)

**Correction.** The earlier "no reference" receipt below was produced by a scan
that only looked for `BL`/`BLX`.  The image also uses unconditional `B`/`B.W`
tail branches, and those carry the reference:

```text
0x103608AA  B.W 0x1048DB00      (veneer)
0x1048DB00  B.W 0x10695450      (4-byte thunk -> the event-0x39 poster)
```

`0x103608AA` has ten `BL` callers: `0x1020CD34`, `0x1020E17A`, `0x1020E19C`,
`0x1020F772`, `0x10246928`, `0x1024697C`, `0x102477FE`, `0x1024781E`,
`0x10247E26`, `0x10247EA0`.  So the poster is not dead code; it is posted
through that veneer.  A rescan of the EGL present wrapper `0x10568F60` with the
same `B`-aware scanner still finds **zero** references, so that conclusion
stands.

Coverage of the ten callers' functions (45 s unthrottled):

| Function | Runs | Contains a post site |
|---|---|---|
| `0x1020CA7A` | 26 | yes (`0x1020CD34`) |
| `0x1020E0E8` | 2 | yes (`0x1020E17A`, `0x1020E19C`) |
| `0x1020F6F2` | 1 | yes (`0x1020F772`) |
| `0x102468C4`, `0x1024692E`, `0x10247DEA`, `0x10247E3C` | 0 | yes |

so three of the posting functions *do* run, yet the veneer (`0x1048DB00`) and
the poster (`0x10695450`) never execute: the call is skipped on a conditional
path.

The condition at the busiest site is explicit:

```text
0x1020cd0a  ldrb r0,[r4,#1] ; cmp r0,#7 ; bne 0x1020CC34        (needs 7)
0x1020cd10  ldrb.w r1,[r5,#0x330]                                state byte
            state 1 -> post (0,1)   state 2 -> (2,1)
            state 3 -> post (3,1)   state 4 -> (1,1)   other -> (0,1)
0x1020cd34  bl 0x103608AA                                        post event 0x39
```

The event-0x39 handler needs the pair `(3, 1)`, i.e. `[ctx+0x330] == 3` **and**
`[ctx+1] == 7`.  The GUI live-view path is therefore gated on an application
context state, not on missing code.

### Which AppC message classes actually reach that post (measured)

The posting function `0x1020CA7A` is the AppC stage handler.  A `-d cpu` capture
shows 21 entries in 45 s, always with `a1 = 0xB1818BD0` (the AppC_Msg context at
`0xB1818BC4 + 0xC`) and a varying payload pointer, all from caller
`0x10294318`.

Its message switch is a TBB at `0x1020CC6E` on
`*(u32*)(payload + 0x10) - 0x03000000` with ten cases.  Coverage of the switch
block and its case targets over 45 s:

| Block | Role | Executions |
|---|---|---|
| `0x1020CC6A` | the TBB itself | 4 |
| `0x1020CCA6` | one case block | 1 |
| `0x1020CCB8` | another case block | 1 |
| `0x1020CC78` (`0x03000000`) | case block | 0 |
| `0x1020CC94` (`0x03000001`) | case block | 0 |
| `0x1020CD0A` | the state test that precedes the event-0x39 post | **0** |

A `-d cpu` capture at the TBB records the delivered class directly: the single
observed execution carries `r0 = 7`, i.e. message code **`0x03000007`** (case 7,
whose block is `0x1020CCA6`).

The path to the state test is a different one: `0x1020CCB8` (a first-switch case
block) feeds the nested switch `0x1020CAAA`, whose case 4 is `0x1020CCA0`, which
branches to the second TBB at `0x1020CCC2` (switch on
`*(u32*)(payload+0x10) - 0x04000000`, 11 cases, TBB at `0x1020CCD0`).  Its
`0x04000000` case is `0x1020CCDC`; from there `cbz [r5+0x148]` falls through to
`0x1020CD0A`.  Coverage shows the whole tail cold:

| Block | Role | Executions |
|---|---|---|
| `0x1020CCA0` | nested case 4 | 0 |
| `0x1020CCC2` | second TBB | 0 |
| `0x1020CCDC` | `0x04000000` case | 0 |
| `0x1020CD0A` | state test before the post | 0 |

So the handler does receive messages - but the delivered class is
`0x03000007`, and the branch that reaches the state test is never selected.
The event-0x39 chain is gated on the application message class delivered to
`0x1020CA7A`, one level above the `[ctx+0x330] == 3` / `[ctx+1] == 7` pair
documented above, and on the nested class it would pass down.

### The `0x03000004` class is produced by a translator that never runs

The class the event-0x39 branch needs is produced by a translator,
`0x1020E98A(in_ctx, code, payload)`: it maps incoming codes to outgoing ones via
`0x1020D364(ctx, out_code, 0, 0, payload)`, and the literal pool at
`0x1020EA5C` holds the outgoing codes it emits:

```text
0x1020EA5C  0x05000007  0x04100019  0x0410000E  0x04000001  0x03000004
```

Read from the switch: incoming `0x04100001` (68157441) produces outgoing
**`0x03000004`** (`50331652`); `0x0410000E`/`0x0410000F` produce `0x04000002`/
`0x04000003`; `0x04100027`/`0x04100028`/`0x04100036`/`0x0410003E` produce the
`0x05000001`/`0x05000002` family.

Coverage over 40 s:

| Function | Role | Executions |
|---|---|---|
| `0x1020E98A` | code translator | **0** |
| `0x1020D364` | outgoing post helper | 19 |

and the translator's only reference is a `B.W` thunk at `0x10204D4C`
(`mov r1,r3; b.w 0x1020E98A`) sitting inside the AppC handler-thunk block at
`0x10204D3A..0x10204D60` (a run of four-byte `movs rX,#n; bx lr` stubs).  So the
producer of `0x03000004` exists, is wired into the AppC handler table, and is
still never selected: the message class that would select it never arrives.

This is one level above the `0x03000007` measurement: that class reaches the
switches of `0x1020CA7A`, while the translator needs `0x04100001` traffic first.

### The display "enable" dispatcher runs with unsupported request codes

The display module is described by the ROM table at `0x118AF660`, which holds the
teardown op `0x1042C5E4` at `+0x0C` and the enable op `0x1042CB82` at `+0x24`
followed by a run of further ops (`0x1042CE67`, `0x1042CE97`, `0x1042CEC3`, ...)
and the panel strings `CARD0`/`CARD1`.  Coverage over 40 s:

| Op | Role | Executions |
|---|---|---|
| `0x1042C5E4` | teardown loop | 5 |
| `0x1042CB82` | enable dispatcher | **2** |
| `0x1042BFCA` | table user A | 0 |
| `0x1042C1F0` | table user B | 0 |

So the enable dispatcher is *not* dead - it runs - and it switches on
`*(u32*)(request + 16)`:

```text
23       -> 0x1042C8C4(idx, 1)
24/25    -> 0x1042C8C4(idx, 2 / 3)
26       -> 0x1042CD62(idx)
29       -> 0x1042CD76(idx)
20       -> 0x1042C87C(idx, 1)   -> 0x1042CCD6   (the selector that can pick mode 1)
other    -> default, returns
```

A `-d cpu` capture records three entries from the same indirect call site
(`0x1020F8D8`), with request pointers `0xB51BB538` (twice) and `0xB209FA24`.  A
gdb peek at those addresses in a fresh run shows `[req+16]` holding `4` (and a
pointer value in the other), i.e. **request codes outside the supported 20..29
set**, so all three take the default branch and neither `0x1042C87C` nor the
mode selector ever runs.

Together with the mode watch this closes the display-mode question: the only
mode write that happens is the hardcoded `mode = 2` from `0x1042D72E`, and the
enable requests that this boot issues carry codes the handler does not accept.

### The display-service walk runs and skips the ready branch (measured)

The display module function `0x104BDB42(request_code)` does run (once per
window).  A full register trace of its single execution (`probeDM2`, filter
`0x104BDB42+0x200`) shows it walking a list of service sub-codes:

```text
entry       r0 = 0x04030034
items       0x04030002  0x04030003  0x0403000a  0x04030008  0x04030009
            0x04030019  0x0403001a  0x0403000d  0x0403000e  0x0403000f  0x04030015
```

and it **never executes the ready-setter call at `0x104BDC64`**.  The guard is
explicit:

```text
0x104bdc34  ldr  r0, =0x04030002
0x104bdc36  adds r0, #0x18            r0 = 0x0403001a
0x104bdc38  cmp  r5, r0               r5 = the current service item
0x104bdc3a  bne  0x104BDC68            skip the whole ready path
```

so the ready setter (thunk `0x10637B72` -> `0x104D7F16`) is reached only when the
walk's current item is exactly **`0x0403001A`**, and this boot's walk does not
satisfy that at the comparison point.

Related callers found with the `B`-aware scanner:

| Target | Callers |
|---|---|
| `0x10637B72` (ready thunk) | `0x104BDC64` (this walk), `0x1062F120` (service start) |
| `0x104D7E78` (admission) | `0x10502D38` (**F091 handler `0x10502D0A`**), `0x10637B5E` (poller) |
| `0x104DB8BA` (natural bank start) | `0x104DC07C`, `0x104DC6DE` (owner states) |
| `0x1046BF0C` (bank command) | the seven previously documented sites |

So there are two independent routes into the display envelope - the service walk
and the F091 handler - and both are measured cold in this boot for different
reasons: the walk skips its `0x0403001A` iteration, and no F091 request arrives.

### The display service request table maps codes to handlers (structure verified)

The service codes live in a three-word-entry table in ROM around
`0x119DB400..0x119DBA00`:

```text
{request-class word, request code, handler}

0x119DB438  0x040F0020  0x0403001A  0x107387B9
0x119DB444  0x040F0014  0x04030018  0x1073873F
0x119DB9E4  0x04090017  0x04030034  0x1073885D
0x119DB9F0  0x04090032  0x04030035  0x1073873F
```

so `0x0403001A` is routed to `0x107387B8` and `0x04030034` to `0x1073885C`.
A `-d cpu` capture of the walk shows its caller is `0x107392EE` (LR
`0x107392F3`) and that `r5` at the ready guard is the **request code**
`0x04030034`, not a loop item:

```text
pc=104bdbfe r5=04030034   pc=104bdc34 r5=04030034   pc=104bdc68 r5=04030034
```

so the guard `cmp r5, 0x0403001A` is asking "is this request 0x0403001A?" and the
boot's request is `0x04030034`.  The ready setter is therefore reached only for
display-service request **`0x0403001A`**, and this boot never issues it.

### The display event is broadcast twice and arrives as 0x04030034 (measured)

The display service propagates its state changes through two levels of
registered callbacks:

```text
0x1052E3AE(code, arg)   iterates a 4-entry table at 0xB2B2CB7C
                        and calls each handler(code, arg)
0x107392D6(code, arg)   iterates a 16-entry table at 0xB2989C40 and does the same
```

A `-d cpu` capture over 45 s records exactly one entry at each level:

| Level | Caller | Code |
|---|---|---|
| `0x1052E3AE` | `0x10509FDC` (inside the display service start `0x10509F9E`) | `0x04090017` |
| `0x107392D6` | `0x1052E3C4` (the outer broadcast's `blx`) | `0x04030034` |

So the service-start event `0x04090017` reaches the display subscriber as
request **`0x04030034`** - the code that the walk at `0x104BDB42` receives and
that fails the `0x0403001A` guard.  The translation happens in the per-subscriber
adapter between the two broadcasts (same caller site, different `r0`); the
router table entry for `0x0403001A -> 0x107387B8` exists in ROM, so the arming
event is a different outer event that this boot never emits.

### The arming request is the F091 handler's IPC and never leaves (measured)

The display service event codes are IPC ids, and the arming one is
`0x040F0020`:

```text
sub_10502CBC(v):  sub_104C0F20(0x040F0020, {v, 0, 0x10502CAE})   then waits 1000 ms
sub_10502E7C(..): sub_104C0F20(0x040F0000, {0x105031AC})         then waits 3000 ms
```

`0x040F0020` is the router-table class word whose display request is
`0x0403001A` - the code the ready guard requires.  Both senders have callers in
the F0-handler block:

| Sender | Callers |
|---|---|
| `0x10502CBC` | `0x10502D20` (inside **F091 handler `0x10502D0A`**), `0x10502F4E` |
| `0x10502E7C` | `0x10502F30` |

and coverage over 45 s shows the whole F0 block cold:

| Function | Role | Executions |
|---|---|---|
| `0x10502CBC` | send `0x040F0020` | 0 |
| `0x10502E7C` | send `0x040F0000` | 0 |
| `0x10502D0A` | F091 handler | 0 |
| `0x10502DB8` | F092 handler | 0 |

So the display arming path is: **F091-class request -> handler `0x10502D0A` ->
IPC `0x040F0020` -> display service -> request `0x0403001A` -> ready
(`0x104D7F16`) and admission (`0x104D7E78`)** - and the first link never fires.
The same handler also calls admission directly (`0x10502D38`), so one inbound
F091 would arm both halves at once.

Consolidated event receipt for a 60 s window: the display service broadcasts
exactly one outer event (`0x04090017`, twice) and the subscribers receive exactly
one code (`0x04030034`); `0x040F0020` is never broadcast.

### The service request plumbing, from the class table down to the handler

The request path has three layers, all verified structurally:

```text
class table 0x119E1E84..  rows whose handlers are 0x107722E5/EB/E3
   -> router 0x104C6B14(request)
        v2 = *(u16*)(request+12)            the service code
        sub_104C6AB4(request) == 0x2001     validate
        v4 = sub_104C6CC4(v2)               look up the handler row
        if [0xB188BC7C]() || v2 == 0xF001   dispatch via sub_104C6A80(v4, request)
        else                                reject via sub_1063C4FC(request)
   -> handler row from the table at 0x119E4820
```

The handler table is 28-byte entries `{code, 0, 0, 0, handlerA, handlerB, handlerC}`:

```text
0x119E4820  code 0xF079   handlers 0x104B2F72 0x104B2F84 0x104B2F70
0x119E483C  code 0xF091   handlers 0x10502CAA 0x10502D0A 0x10502CA8
0x119E4858  code 0xF092   handlers 0x10502D82 0x10502DB8 0x10502D80
0x119E4874  code 0xF093   handlers 0x10502E12 0x10502F10 0x10502E10
0x119E4890  code 0xF094
```

so `0xF091`'s middle handler is exactly the `0x10502D0A` measured cold above, and
`0xF092`'s is `0x10502DB8`.  The router is itself installed in the class-2
envelope table (its three call sites are `0x107722E6`, `0x10772302`,
`0x1077230E`), and the send side is `0x104C0F20` with 51 call sites.

`0xF091`/`0xF092` appear **only** in this handler table: there is no `movw`
immediate for either code anywhere in the image and neither appears in the EX
component, so the sender addresses the row through the table rather than by a
literal.

A 120 s unthrottled window shows the whole request layer idle, not just the two
display rows:

| Address | Role | Executions (120 s) |
|---|---|---|
| `0x104C6B14` | service request router | **0** |
| `0x10502D0A` | F091 handler | 0 |
| `0x10502DB8` | F092 handler | 0 |
| `0x10502E12` | F093 handler | 0 |
| `0x10502CAA` | F091 sibling | 0 |

So no class-2 service request reaches MAS at all in this boot.  The **send**
side is idle too: a `-d cpu` capture of the send API `0x104C0F20` over 45 s
records **zero** entries, even though the image has 51 static call sites for it.
So MAS's host-facing service bus is dormant in both directions: nothing sends a
request and nothing routes one.

That is the remaining link between the display service and its host, and it is
the same conclusion the EX side reaches from the other direction (its
host-command table carries neither 113 nor 131).  The local display events still
flow - they use in-process callback tables (`0x1052E3AE`, `0x107392D6`), not this
bus - but the host-facing service envelope is completely quiet.

### Correction: the 0x1042Cxxx/0x1042Dxxx cluster is the device-subsystem dispatcher

The op dispatcher `0x1020F876` does not drive the display.  Its tables are the
**system-command and subsystem tables**, and they are named in the ROM:

```text
table A 0x118AED0C  28-byte entries keyed by eSyscCommandId, e.g.
  0  eSyscCommandId_Create2nd              00 0e
  1  eSyscCommandId_InitializeFastBoot     0f 0c 0b 01 05 08 04
  2  eSyscCommandId_StartTaskFastBoot      01
  3  eSyscCommandId_Initialize1st          03
  4  eSyscCommandId_StartTask1st           08 03 01
  5  eSyscCommandId_Initialize2nd          0d 0e 05 02 10 04 09 0a
  6  eSyscCommandId_StartTask2nd           00
  7..11 BSShutdown / BSShutdownCancel / ...
  18 eSyscCommandId_AppFirstStartComplete  02 0a 04
  19 eSyscCommandId_PowerOnBoot            03 01
  20 eSyscCommandId_CardInsert             03
  21 eSyscCommandId_CardRemove             03
  22..29 CardMountComplete/CardMountError/CardUnmount*/CardFormat*
```

and table B (`0x118AEC40`, 12-byte entries) maps each sub-op index to a getter
that returns a subsystem descriptor; the descriptor's first word is the
subsystem handler:

| idx | descriptor | handler | name string |
|---|---|---|---|
| 0 | `0x118AF5CC` | `0x1042BACC` | - |
| 1 | `0x118AF66C` | `0x1042C5E4` | - |
| 2 | `0x118AF7F4` | `0x1042E948` | - |
| **3** | `0x118AF684` | **`0x1042CB82`** | **`CARD0`** |
| 4 | `0x118AF6B0` | `0x1042D32A` | - |
| 5 | `0x118AF77C` | `0x1042DCFA` | `ML_L...` |
| 6 | `0x118AF780` | `0x1042DE40` | `ML_LENS` |
| 7 | `0x118AF758` | `0x1042D9D8` | `F_LENS` |
| 8 | `0x118AF734` | `0x1042D866` | `EXTSB` |
| 9 | `0x118AF7C8` | `0x1042E432` | - |
| 10 | `0x118AF7A4` | `0x1042E246` | `NwBoard` |
| 11..16 | `0x118AF834..` | `0x1042ECD2..` | - |

**So the earlier "display mode / display enable" reading was a mis-attribution.**
`0x1042CB82` is the `CARD0` (SD-card) subsystem: its `20..29` switch is exactly
`eSyscCommandId_CardInsert/CardRemove/CardMount*/CardUnmount*/CardFormat*`, its
`op = 20` branch (`0x1042C87C -> 0x1042CCD6 -> 0x1042CCA0`) is card-insert
handling, and the `mode` byte at `+0x1C` written with `2` by `0x1042D72E`
belongs to that card object, not to the display.  `0x1042D866` is `EXTSB`, also
not the display.

The display side is the owner/admission/provider path documented earlier
(`0x104DC804`, `0x104D7E78`, `0x1046BF0C`); that part of the frontier is
unaffected by this correction, and the earlier statements about the display mode
are withdrawn.

### The display op dispatcher and its tables (structure verified)

`0x1020F876(request, a2)` is the op dispatcher.  It reads `op = *(u32*)(request+16)`
and uses two ROM tables:

```text
table A  0x118AED0C  28-byte entries: {op id, name pointer, up to 8 sub-op bytes}
                     terminator 0x11; only entries with entry[0] == op are valid
table B  0x118AEC40  12-byte entries: {index, name pointer, getter stub}
```

The op-4 entry is `{4, 0x120A62A8, bytes 08 03 01 11}`, i.e. sub-ops
**8, 3, 1**; table B resolves them to the getter stubs at `0x1042D7F8`,
`0x1042C770`, `0x1042C54C`.  For each sub-op the dispatcher calls
`handler = *getter(); handler(request)`.

Measured: the only handler reached from this site in the window is
`0x1042CB82` (three calls, all from `0x1020F8D8`), and it receives the same
`op = 4`, which its `20..29` switch does not cover, so it returns without acting.
**Correction:** `0x1042CB82` is the `CARD0` (SD-card) handler, resolved from the
subsystem descriptor names - it is not a display handler (see the correction
section below).

The getter stubs themselves are two-instruction returns whose literal pools the
current IDB has not typed as code, so the exact getter -> descriptor mapping is
still open; the measured callee identity (`0x1042CB82`) is what matters here.

### Historical scan receipts (superseded by the correction above)

`0x10695450` had been searched for as:

| Search | Result |
|---|---|
| 2-byte-aligned Thumb `BL`/`BLX` targets | none |
| ARM `BL`/`BLX` immediate targets | none |
| 32-bit literal, both alignments, whole image | none |
| `TBB`/`TBH` table entries landing on it | none |
| 32-bit value in RAM `0xB1800000..0xB3000000` (GDB `find`) | none |
| 32-bit value in the inner image dump `0xB1000000..0xB1204000` | none |

None of those searches covered `B`/`B.W`, which is where the reference is.

`0x10695750`, the display-mode dispatcher that would call the callback table at
`0xB181B3D4+0x44` (`0x10204DB5` twice), has the same historical footprint and
should be re-scanned with the `B`-aware scanner before any conclusion.

### Reading

The two remaining candidates for the live-view start are (a) a producer whose
address is computed at run time in a way none of the scans see, and (b) dead
code left in this container image.  The scans above are exhaustive for the
static forms the MAS code actually uses elsewhere (literal pointers, direct
`BL`, jump tables), and neither the ROM, the main RAM window nor the inner
image holds the address, so the practical conclusion is: **the supplied
image has no reachable producer for GUI event 0x39**.  If that is the case, the
view-state-25 -> mode-request -> live-view branch cannot start from this image
alone, exactly like the EX template-zero gates in
[`../validation/frontier.md`](../validation/frontier.md).

This is a firmware-data boundary, not a device-model gap; fabricating the
event would be a synthetic stimulus, not an unchanged-firmware receipt.

## Reachability summary (unchanged container, 420 s windows)

| Link | State | Evidence |
|---|---|---|
| MAS app boot, stages 1..3 | runs | `0x1020D41A` stage receipts |
| app UI draw `0x10569C76` | runs (41x in 300 s) | `-dfilter` coverage |
| composition surface `0xbf309600` | stays zero | RAM dump at 30 s and 90 s |
| AppC message framework (defer/queue) | runs | `probeDEFER` |
| GUI worker task | registered, blocked on RTOS event `0x010001BE` | RAM dump + CPU 1 idle |
| GUI worker init (method 0) `0x1053CFA8` | runs once | `-dfilter` coverage |
| GUI step dispatcher `0x105424E8` (method 17) | never runs | `-dfilter` coverage, 300 s |
| step states 9/11 `0x10542486`/`0x105429D4` | never run | `-dfilter` coverage |
| mode-request consumer `0x1053FA1E` | never runs | `-dfilter` coverage |
| GUI event dispatcher `0x1053E19A` | never runs | `probeGUIEV` |
| GUI event `0x39` | never posted | mailbox watchpoints zero |
| event-`0x39` poster `0x10695450` | reachable via `0x103608AA -> 0x1048DB00`; never posted (callers skip it) | `B.W`-aware scan + coverage |
| mode request `0xB181D970+0` | never written | u32/u16 watchpoints zero |
| live-view gate `0x1053DBA2` | never entered | `probeLV` |
| display owner `0x104DC804` | never runs | all windows |
| provider admission `0x104D7E78` | never runs | `probeDMSVC` |
| SLA readout init | runs | `0x10093CEC`/`0x1009ACE2`/`0x1009AD52` |
| SLA readout ISR/completion | never fires | `0x1009ACA0` zero-hit |
| MAS inner image copy | runs (RAM matches payload) | `probeINNER` |
| inner reset `0xB1000060` | never entered | `probeINNERSTART` |

The shape is consistent: every display-side module is initialised or
registered, then waits for an event no reachable producer sends.  With the
unchanged container and no external stimulus, the first GUI frame is therefore
not reachable in this model; the missing inputs are the camera-state/sensor
events documented above, and producing them would be a synthetic stimulus,
which the objective excludes.

## The SysC ladder IS drained - and two earlier "never runs" claims were tool artefacts

### Correction

Breakpoint-based probes in this log reported that the `SysC_Msg` message box's
drain handler `sub_102050C8` and its pump body `0x103D3D78` never executed, and
the whole task-start ladder was therefore stuck in the queue.  That is **wrong**.
The probes were run with several hardware breakpoints and watchpoints in the same
GDB session, and this target silently stops honouring them once the debug
register budget is spent; the later breakpoints simply never fire, which reads
exactly like "the code never runs".

The reliable instrument is QEMU's own execution filter, which costs nothing per
hit:

```text
-accel tcg,thread=single -icount shift=6 -dfilter 0x1020F876+4,0x102050C8+4,0x10205120+4 -d exec
```

Unperturbed, 40 s: 6 posts (`0x10205120`), 6 drains (`0x102050C8`), 11 dispatches
(`0x1020F876`), interleaved as post -> drain -> dispatch.  A 60 s run of the same
kind gives 44 pump-body executions.  So the message box works: every queued
record is taken and dispatched.

### What the same instrument says about the rest of the chain

60 s, no GDB attached:

| function | role | executions |
|---|---|---|
| `0x1020F876` | SysC dispatcher | 10 |
| `0x10569C76` | app graphic-resource create | 47 |
| `0x1044F6E0` | camera-control device state machine | 0 |
| `0x104D7E78` | display provider admission | 0 |
| `0x10568F60` | EGL present | 0 |

So the boot does drain and dispatch its ladder, the application does create its
graphic resources, and the display side is still untouched.  Every earlier
"zero" in this log that came from a multi-breakpoint GDB session has to be
re-measured with `-dfilter` before it is believed.

### Reliable timeline, 60 s, no GDB attached

`-dfilter` only, so every number below is a real execution count of the original
firmware:

| address | role | executions |
|---|---|---|
| `0x10201804` | SysC boot-ladder entry | 3 |
| `0x1020F876` | SysC dispatcher | 10 |
| `0x1020F8D0` | controller-handler call site | 4 |
| `0x1044E362` | camera-control task-table entry #1 | 1 |
| `0x1044F6E0` | camera-control device state machine | 0 |
| `0x1054BA98` | GUI message handler | 77 |
| `0x10569C76` | app graphic-resource create | 49 |
| `0x104D7E78` | display provider admission | 0 |
| `0x104DC804` | display owner | 0 |
| `0x10568F60` | EGL present | 0 |
| `0x106EE04A` | GPU/resource-manager config | 5 |
| `0x104C0F20` | host-facing service send | 0 |

Reading: the boot ladder runs and dispatches (10 dispatches, 4 controller calls),
the application is alive (77 GUI messages, 49 graphic resources), the GPU
resource manager is configured (5), and the display slice is completely idle -
no admission, no owner, no present - while the host-facing send API is never
used.

### What still has to be re-measured

These earlier claims came from multi-breakpoint GDB sessions and are **not**
trustworthy yet; each needs a `-dfilter` count or an unperturbed dump:

- the display provider query reply being `0` for handle `0x010001C2`
  (the handle value itself *is* present in RAM at 120 s: `0xB181C560`,
  `0xB1D6AD44`, `0xB2C4AAE8`, `0xB2C4AB54`, `0xB2C59030`, `0xB2C61E38`);
- "the worker blocks on kernel object `0x010001BE`" (handle present at
  `0xB181D9D8`, `0xB1D6A620`, `0xB2C3F75C`, ...);
- every "no reference" scan that used a breakpoint to decide reachability.

The rule for this workspace is now: reachability questions are answered with
`-dfilter`; GDB breakpoints are only used to read a value at a known execution,
with at most two or three armed at a time.

### The display start runs late, and its reply is a flags word

`-dfilter` over 40 s with three addresses, read in log order:

```text
0x10361082  object-create helper          586 executions
0x1062EDDC  display service start           1 execution, after all 586
0x10637B54  provider-admission caller       0
```

so the display service start is not an early victim of a missing object - it runs
*after* every object the boot creates.  A single-breakpoint read at the
instruction following its query (`0x1062EE0C`) returns `r4 = 0`.

That `r4` is not a handle: the code immediately tests it as a bitfield
(`LSLS R0, R4, #31; BEQ` then `LSLS R0, R4, #2; BPL`) and skips whole blocks when
the bits are clear.  So the display service asks for a **state/capability word**
and receives 0.

180 s unperturbed, same instrument:

| address | role | executions |
|---|---|---|
| `0x1062EDDC` | display service start | 1 |
| `0x10637B54` | provider admission caller | 0 |
| `0x104D7E78` | provider admission | 0 |
| `0x104DC804` | display owner | 0 |
| `0x10568F60` | EGL present | 0 |
| `0x1046BF0C` | display banks start | 0 |
| `0x10569C76` | app graphic-resource create | 45 |
| `0x1020F876` | SysC dispatcher | 13 |

Running three times longer changes nothing downstream of the display start, so
the missing input is the word that start queries, not timing.

### Instrument budget: one hardware breakpoint per run

Two independent observations show this target honours roughly **one** hardware
breakpoint or watchpoint per GDB session: with a watchpoint armed on the display
context global, a breakpoint on the query reply never fires, while either alone
works.  Every multi-instrument probe earlier in this log therefore has an unknown
subset of its breakpoints silently inactive.

Rules adopted here:

- execution counts and reachability: `-dfilter` only (unlimited, unperturbed);
- a value at a known execution: a **single** hardware breakpoint or watchpoint
  per run;
- ordering between two functions: `-dfilter` on both entries and compare log
  line numbers, never two breakpoints.

### What the display start actually waits on

Static read of the helper chain behind that `r4`:

```text
sub_10634050()      = the constant 0xB181C564
sub_106340D4(a1, a2) maps a1 to 1/2/3 by comparing against 0xB181C55C,
                     0xB181C55C+8 and 0xB181C55C+0x10, logs through
                     nullsub_248(87, index, a2, 0), and calls
sub_10361116(obj, ..)  -> handle = *(u32 *)obj; sub_10104A14(handle, 1, ..)
                       which is the one-SVC microkernel wait
```

so the display service does not "query a reply" - it **waits on its context
object** and then tests the returned event word as a bitfield (bit 0 at
`0x1062EE10`, bit 29 at `0x1062EE22`).  The value read there is 0, i.e. no
event.

The context object itself is created: a watchpoint on `0xB181C560` catches the
store of handle `0x010001C2` from PC `0x1036109E` (the generic
`sub_10361082` object-create helper), and `sub_10634050()` returns
`0xB181C564`, the handle slot eight bytes into that block.

So the remaining dependency is one specific event: whatever should set bit 0 /
bit 29 of the display context's event word.  Every documented display step -
provider admission, ready setter, owner, bank start, present - is downstream of
that single event.

### The cold half of the firmware, measured with one instrument

60 s each, `-dfilter` only, no GDB.  Everything below is a real execution count:

| address | role | executions |
|---|---|---|
| `0x1054BA98` | GUI message handler | 77 |
| `0x10569C76` | app graphic-resource create | 49 |
| `0x1020F876` | SysC dispatcher | 10-13 |
| `0x106340D4` | display context wait | 10 |
| `0x1052E3AE` | display event broadcast | 1 |
| `0x10637B54` | provider admission caller | 0 |
| `0x10502D0A` | F091 display service handler | 0 |
| `0x10502CBC` | arming IPC send inside F091 | 0 |
| `0x104BDC38` | arming guard (`cmp r5,#0x0403001A`) | 0 |
| `0x1053E19A` | GUI event dispatcher | 0 |
| `0x105424E8` | GUI step | 0 |
| `0x104BD99E` | GUI event setter | 0 |
| `0x104C6B14` | service request router | 0 |
| `0x104C0F20` | host-facing service send | 0 |
| `0x104C0F96` | service deliver | 0 |

The application half is warm - messages are handled, graphic resources are
created - and every activation path is cold: no GUI event is ever set, no GUI
dispatcher step runs, no service request is routed or sent, the F091 display
handler never runs, so no arming IPC is sent, the display context never receives
an event, and admission / owner / bank start / present all stay at zero.

That is the natural-boot answer in its current, reliably measured form: this
bundle's application never leaves its own initialisation-and-message phase,
because nothing ever raises the first GUI event.

### The AppC path runs, and still raises no GUI event

60 s, `-dfilter` only:

| address | role | executions |
|---|---|---|
| `0x10294318` | AppC message link | 22 |
| `0x1020CA7A` | AppC stage handler | 20 |
| `0x103608AA` | event-0x39 veneer caller | 0 |
| `0x10695450` | event-0x39 poster | 0 |
| `0x10478F3C` | AppC send wrapper | 0 |

so AppC messages are delivered and staged twenty times, and the event-0x39 post
inside the stage handler is never taken.  That matches the gate read out earlier:
the post needs the message's class byte to be 7 and the application context state
at `ctx+0x330` to be 1..4.

A single watchpoint on that state byte (`0xB1818EF4`, from the `LDRB.W R1,[R5,#0x330]`
at `0x1020CD10` with `ctx = 0xB1818BC4`) records **no write at all** in 60 s, so
the byte is never advanced from its initial 0 by any code in this boot.

### The chain, stated once, with every link measured

```text
app messages + graphic resources          warm   (77, 49)
AppC stage handler                        warm   (20)
AppC send wrapper                         0      -> nothing leaves the app
GUI event setter                          0      -> no GUI event is ever raised
GUI event dispatcher                      0
service router / send / deliver           0      -> no service request exists
F091 display handler                      0      -> no arming IPC is sent
display context wait                      10     -> always replies "no event"
provider admission / owner / bank / present 0
GPU resource-manager config               5      -> configured, never submits
application context state byte            0 writes
```

The whole display and service half of this firmware is downstream of a GUI event
that never gets raised, and the state byte the one posting site tests is never
written by anything.

### Correcting the state-byte address, and what actually gates the post

An earlier paragraph put the application state byte at `0xB1818EF4`
(`ctx = 0xB1818BC4` plus `0x330`).  That is wrong.  A single breakpoint on the
state setter `sub_1020DEAE` shows the context it is called with:

```text
SETTER ctx=0xb1818bd0 a2=0x2 a3=0x4 a4=0x3 lr=0x102a6ab9
```

so the context is **`0xB1818BD0`**, the same value the docs already quote for the
`AppC_Msg` argument, and the bytes the setter writes are
`ctx+0x32C`, `ctx+0x330` (when `a2 == 0`), `ctx+0x332` (`a2 == 1`) and
`ctx+0x334` (`a2 == 2`).  The gate byte that `LDRB.W R1,[R5,#0x330]` at
`0x1020CD10` reads is therefore **`0xB1818F00`**.

Measured:

- the setter runs once, with `a2 = 2`, so it writes `ctx+0x334 = 3`
  (`0xB1818F04`) - not the gate byte;
- a single watchpoint on `0xB1818F00` records **no write at all** in 60 s;
- a single breakpoint on the gate instruction `0x1020CD10` **never fires**, so
  the stage handler never even reaches that test.

The setter itself is entry 28 of a 64-entry Thumb handler table at
`0x123B2B8C`; `-dfilter` shows entry 0 running 3 times, entry 4 four times,
entry 7 zero and entry 28 once, so the table is live and this boot simply calls
the entries its message classes select.

### Kernel task primitives, counted

60 s, `-dfilter`:

| wrapper SVC | role | executions |
|---|---|---|
| `0x101044FC` | create task (`0x80010100`) | 4 |
| `0x10104964` | start task (`0x80030200`) | 5 |
| `0x10104358` | set task priority | 17 |
| `0x101048DC` | post to a message queue | 106 |
| `0x1010471C` | wait on a message queue | 231 |

so task creation and starting are both happening; the message plumbing is busy.
The application's own event posting, by contrast, is completely absent.

### The stage handler is called twenty times with the same synthetic message

A single breakpoint on the stage-handler entry `0x1020CA7A`, printing the message
fields, records twenty identical invocations:

```text
MSG msg=0xb1818bd0 b0=0x1e b1=0 cmd=0x1 p0=0 p1=0
MSG msg=0xb1818bd0 b0=0x1e b1=0 cmd=0x1 p0=0 p1=0
... (twenty times)
```

The message is the application context itself, its class byte (`+1`) is 0 rather
than the 7 the GUI posting site requires, and its command word (`+0x10`) is 1,
which the handler's `- 0x03000000` switch sends straight to the default case.

So the application is not processing real AppC messages at all - it is re-entering
its stage handler with one constant record - and the AppC send wrapper
`0x10478F3C` never executes (measured 0).  Nothing in this boot produces an AppC
message, therefore nothing selects the class-7 path, therefore no GUI event is
raised and the entire display half stays cold.

That is the current, reliably measured answer to "why does no frame appear
naturally": the application's inbound message channel is empty, so its state
machine never advances past its initial stage.

### The AppC publisher chain, and why nothing ever publishes

The delivery wrapper `sub_10478F3C` has exactly one caller, and the chain up to
it is short:

```text
sub_10463A02(a1)         build the message at 0xB2A5070C and publish it
  -> sub_10463E8E(msg)   read class = *(u16 *)(msg+6), look it up with
                         sub_10463D70(class); if no subscriber: drop
                         otherwise deliver with sub_10478F3C(subscriber, msg)
```

60 s, `-dfilter`, every address in that chain:

| address | role | executions |
|---|---|---|
| `0x10463A02` | AppC post API | 0 |
| `0x10463A40` | publisher A | 0 |
| `0x10463B60` | publisher B | 0 |
| `0x10463E8E` | publish / class lookup | 0 |
| `0x10463D70` | subscriber lookup | 0 |
| `0x10478F3C` | deliver to subscriber | 0 |

So the AppC subsystem is not failing - it is never entered.  Both publishers are
registered as callbacks in a table at `0x12425700`:

```text
0x12425700  0x00000001
0x12425704  0x104639E1   0x000183DA
0x1242570C  0x10463A41   0x000101DA
0x12425714  0x10463B61   0x00010FDA
0x1242571C  0x10463BD5   0x00000000
```

i.e. publisher A is the handler for message class `0x83DA`, publisher B for
`0x101DA`, and a third handler for `0x10FDA`.  Nothing in this boot sends a
message with any of those classes, so none of the publishers ever runs.

### The answer, in one sentence

The application runs, handles messages and creates graphic resources, but its
inbound AppC channel never carries a real message - the classes that would drive
its publishers (`0x83DA`, `0x101DA`, `0x10FDA`) are never sent - so its stage
handler only re-enters with one constant record, the class-7 GUI message that
would raise the first GUI event is never selected, and everything downstream
(service router, F091 display handler, arming IPC, display context event,
provider admission, owner, bank start, present, GPU submission) stays cold.

### The operation (AutoOperation) module is cold too

The published Z7 model raises its first UI by injecting one MENU **AutoOperation**
call into the application object from the app's idle checkpoint - not by waiting
for an internal event - so the same module is the natural next suspect here.

60 s, `-dfilter`:

| address | role | executions |
|---|---|---|
| `0x10522EC8` | operation dispatcher (`op` switch) | 0 |
| `0x1052329C` | its first in-module caller | 0 |
| `0x10526B7E`, `0x10527378`, `0x1052830C` | other in-module callers | 0 |

So no operation is ever dispatched in this boot: keys reaching the pad cannot
become operations because the operation module never runs, which is consistent
with every key experiment in this log producing nothing.

See [`../reference/z7-assisted-boot.md`](../reference/z7-assisted-boot.md) for the
reference recipe and which of its properties transfer.

### Poking one AutoOperation tick

Following the reference method (see `../reference/z7-assisted-boot.md`), one
operation tick was invoked from outside with the call harness, with the execution
filter counting the aftermath:

```text
ASSIST_CALL entry=0x1052329d reply=0 status=returned     (the AO poller)
```

Counts afterwards, same run:

| address | role | executions |
|---|---|---|
| `0x1052329C` | AO poller (our call) | 1 |
| `0x10522EC8` | AO dispatcher, one per operation id | 130 |
| `0x1020CA7A` | AppC stage handler | 30 (20 without the poke) |
| `0x1054BA98` | GUI message handler | 67 |
| `0x104BD99E` | GUI event setter | 0 |
| `0x1053E19A` | GUI event dispatcher | 0 |
| `0x10695450` | event-0x39 poster | 0 |
| `0x10637B54` | provider admission caller | 0 |
| `0x10568F60` | EGL present | 0 |
| `0x1044F6E0` | camera-control state machine | 0 |

So the operation module can be driven from outside - the poller evaluates every
operation id, 130 dispatcher calls - and it still raises no GUI event, because no
switch state is present for any operation to act on.  The next input the firmware
wants is therefore the switch state itself, not another tick.

Note what this also shows: an assisted call can move numbers in the warm half
(67 GUI messages, 30 stage entries) while the measured cold half stays exactly at
zero, so "nothing changed" is a real result and not an artefact of the poke.

### AutoOperation internals read so far

- `sub_1052329C` is the poller: `for (id = 0; id < 0x40; id++) sub_10522EC8(id);`
- `sub_10522EC8(id)` is the per-operation decision: it takes the operation's
  state block `off_1094AA24[id]`, picks one of three field groups depending on
  the mode globals (`0xB202C1F0`, `0xB202C170`, `0xB202C17C`), reads
  `*(u32 *)(block + {4,16,28} + 4 * index)`, and switches on the operation id.
  It also consults `0xB202BE70` and `0xB181DA91`.
- `off_1094AA24` is a table of RAM pointers (op 0 -> `0xB181DDBC`,
  op 1 -> `0xB1821374`, ...).  In a 25 s dump each block looks like
  `{0, 0x00FFFFFF x9, 0x0000000N, pointer}` - i.e. mostly "unset" markers with a
  small per-operation value at `+0x28` (1 for op 0, 8 for op 1).

So an operation becomes active when its state block says so; the switch state
that would set those words arrives through the key path, which is the thing the
EX-side investigation is now looking for.

### The EX key path is alive - the assumption that keys never arrive was wrong

Static map of the EX side (base `0x08000000`, from the EX image): pad table
`0x0804EF8F` (17 pad codes), GPIO read `0x08043ACC`, three scanners
`0x0803DD00` / `0x0803E000` / `0x0803E51C`, debounce `0x0803E428`, key
publication `0x0803E12C` (event ids `0x02030083`, `0x0203008C`, `0x02030081`),
consumer = switch manager `0x0803D290`.

60 s, `-dfilter`, EX addresses:

| address | role | executions |
|---|---|---|
| `0x0803DDBC` | "SwichCy8" timer callback | 9645 |
| `0x0803E098` | EX event dispatcher | 9786 |
| `0x0803DD00` | scanner A (6 pads) | 146650 |
| `0x0803E000` | scanner B (6 pads) | 861 |
| `0x0803E12C` | key publication | 9551 |
| `0x0803D290` | switch manager (consumer) | 10530 |
| `0x080325B8` | application-cycle caller in the static chain | 0 |

So the EX firmware scans pads, debounces, publishes key events and merges them
in its switch manager on every cycle.  Keys are not being lost inside the EX.
Whatever is missing sits between that merged state and the MAS - the next
question for the EX-side analysis.

### Method caveat: the execution filter only counts TB starts

`-dfilter` reports a TB only when that exact address starts a TB, so an address
*inside* a function reads 0 no matter how often the code runs.  Re-checking the
one address in the cold list that was not a function entry:

```text
0x104BDC38 (arming guard, inside sub_104BDB42)   filter: 0
0x104BDB42 (its function entry)                  filter: 1
```

so the guard's function does run once.  All the other "0" entries in the tables
above were verified to be real function entries, and therefore stand.

### The EX key path runs, but its report to MAS never fires

Static work on the EX image (base `0x08000000`) found one and only one
off-chip forwarder for the merged switch state:

```text
0x0803D1CC  LSLS R0,R4,#0x1F   ; event bit 1 of the 40-unit "SwMngCyc" timer
0x0803D1D4  LDR R1,[R0,#4]  / CBZ skip     ; 0x20011670 must be non-zero
0x0803D1D8  LDR R1,[R0,#0xC]/ CBZ skip     ; 0x20011678 must be non-zero
0x0803D1DC  LDR R1,[R0]     / CBZ skip     ; 0x2001166C must be non-zero
0x0803D1F2  BL 0x0803D5FC                  ; the forwarder
```

and `0x0803D5FC` copies the 120-byte merged state to `0x200115F4`, fills a
24-byte record descriptor at `0x200115D8` and calls the transport hook
`*(0x20011490)`.

Measured with `-dfilter`:

| address | role | executions |
|---|---|---|
| `0x0803D290` | switch manager (merges key records) | 9989 |
| `0x0803D5FC` | switch-state forwarder to MAS | **0** |

So the EX scans, debounces and merges key state, and never reports it to the
MAS.  The static reading is that `0x20011670` and `0x20011678` have no writer
anywhere in the image, i.e. the forwarder's gate is never satisfied; the
transport hook `0x20011490` is installed only by `0x0803CEF0`, which has no
reference in the ROM either.  Both would have to come from the decompressed init
record.

### Caveat: EX RAM is not readable through the emulator's debug paths

EX RAM lives in the EX device's own address space (`ex.address-space`, SRAM at
`0x20000000`, 256 KiB).  It is **not** part of the machine address space, so:

- `pmemsave` at `0x20011xxx` silently returns zeros (a dump of that region is
  meaningless, not evidence);
- gdb cannot read it from any thread: `Cannot access memory at address 0x20011670`
  for all five vCPUs, because the gdbstub resolves memory in the system space.

Consequences for this workspace: EX memory values must either come from a
purpose-built debug hook inside the device, or not be claimed at all.  Execution
counts for EX code addresses are unaffected and remain valid.

### Reading EX RAM with guest code, and what it shows

EX SRAM is unreachable from the debugger, but the original firmware can read it
for us: call the EX image's own `LDR R0,[R0]; BX LR` gadget (`0x08000646`) with
the address in `r0` and return to an existing `B .` loop (`0x0800025E`) through a
hardware breakpoint.  The read is performed by EX instructions, not by the
debugger.

```text
0x2001148C channel                        2
0x20011490 transport hook                 0x08032001   installed
0x20015DD4 peer lane hook                 0x08032001   same function
0x2001166C gate flag                      1
0x20011670 gate flag                      1
0x20011678 gate flag                      1
0x20011688 already-sent                   0
0x200114E6 merged switch state            0
0x2001155E last-sent mirror               0
0x2000E738 transport token                0xFFFF  idle
```

**This corrects the static prediction.**  The analysis expected `0x20011670` /
`0x20011678` to have no writer and the forwarder to be dead by those flags.  They
are both 1, the transport hook is installed and the transport is idle, so the
forwarder's own gate is satisfied.

What is actually missing is one level up:

| address | role | executions |
|---|---|---|
| `0x0803D044` | switch-manager registration | 31 |
| `0x0803D094` | "SwMngCyc" timer callback | **0** |
| `0x0803D180` | switch-manager event handler (starts the timer at `0x0803D22C`) | **0** |
| `0x0803D5FC` | switch-state forwarder | **0** |
| `0x080325B8` | EX application cycle (event bit `0x02` in the table at `0x0804DF68`) | **0** |

The handler starts its own timer, and the handler is driven by event bit 2 of the
EX application cycle.  That cycle never runs, so the switch manager is never
enabled, its timer never ticks, and no key state is ever reported to the MAS.

### The EX waits for one lane frame that the MAS never sends

Inbound EX traffic, 60 s, `-dfilter`:

| address | role | executions |
|---|---|---|
| `0x08027E58` | inbound host-command dispatcher (0x65..0x8A) | 65 |
| `0x0802A57C` | 0x65 handler | 30 |
| `0x0802A888` | 0x66 handler | 29 |
| `0x08032000` | `McuCm` lane enqueue (EX -> peer) | 52 |
| `0x08031F28` | `McuCm` send task | 37 |
| `0x080332A8` | `McuMg` mask dispatcher | 1 |

and on the McuMg side:

| address | role | executions |
|---|---|---|
| `0x080322F8` | McuMg registration (timers "McuMgCyc"/"McuMgCy2") | 7 |
| `0x080332F4` | McuMg message router | 1 |
| `0x080341B4` | "McuMgCyc" 10-tick timer callback | **0** |
| `0x0803350C` | the only poster of event bit 0x02 | **0** |
| `0x080325B8` | EX application cycle (bit 0x02 handler) | **0** |

The router `0x080332F4` dispatches inbound messages by their payload's first byte
through the table at `0x0804E15C`; id `0x04` is the only path to
`0x0803350C`, which is the only poster of the McuMg bit `0x02`.  So the EX
application cycle starts only when the peer sends a frame whose payload begins
with `0x04`.

Measured: the lane carries the 0x65/0x66 host commands (30 + 29) and the EX
enqueues 52 frames the other way, but **no frame with payload id 0x04 ever
arrives** - the router runs exactly once, and the McuMg timers never tick.

So the two MCUs are in a mutual wait in this boot: the MAS never sends the EX the
frame that enables its key/switch cycle, and the EX therefore never reports key
state back.  Both sides are otherwise alive - the lane transports frames in both
directions and the EX send task runs.

### Where the bootstrap actually closes

```text
MAS app starts, but its own AppC publish chain never runs (measured 0)
  -> nothing on the MAS ever raises an app event
  -> the MAS-side sender that would emit the McuMg "id 0x04" frame never runs
  -> EX application cycle never starts, key/switch cycle never enables
  -> EX reports no key state, so the MAS gets no input even if a key is pressed
  -> no GUI event, no service request, no display arming, no GPU submission
```

Both ends are waiting on the other, and the first move belongs to the MAS
application, which never leaves its initial stage.

### The McuMg object is alive and simply never receives its message

Read through the EX gadget (firmware reads its own SRAM):

```text
0x200138D4  0x00010001      module id/mode word
0x200138F8  25              object queue handle
0x200138FC  0x080214D9      its event task body (0x080214D8)
0x20013924  8               "McuMgCyc" timer object
0x20013938  9               "McuMgCy2" timer object
0x20013A68  0               the 106-byte id-4 message buffer: never written
```

and the MAS side of the same lane is busy:

| address | role | executions (60 s) |
|---|---|---|
| `0x10243468` | MAS TX descriptor/FIFO to the EX | 2320 |
| `0x10243400` | MAS TX neighbour | 1764 |
| `0x1034FBAE` | MAS lane enqueue | 7577 |
| `0x104C0F20` | host-facing service send | 0 |

So: the EX McuMg object, its queue and its event task all exist, the lane
transports thousands of MAS frames, and the one message this module needs
(payload id `0x04`) is never among them.

### There is no relay in the MAS image, and the inner image is not it either

A whole-image search of the MAS side (base `0x10020000`) for a sender of the EX
`McuMg` ladder frames found none:

- the MAS<->EX link is the "ExMcu" module (`0x10206180` dispatcher,
  `0x1020624E` init, comm state machine `0x1020B7E8`, frame assembly `0x1020BDB6`);
- the frame assembly produces an 8-byte header + payload (<=240 B) + 8-byte tail
  and carries **no command-id byte** and no `0x6A` size;
- the ExMcu payload descriptor (`0xB1D1A878`, length at `+0x3A`, pointer at
  `+0x40`) is written only by the comm init and the debug CLI;
- the sysc command area (`0x1020E000..0x10214000`) makes **zero** calls into the
  ExMcu module, and the sysc controller table has no ExMcu controller;
- the ids the EX router expects (`{1,4,6,8,0xA,0xC,0xD,0x10}`) are a subset of
  the MAS *sysc* command ids, i.e. the EX side is a mirror of the MAS ladder, and
  the mirror relay is absent from the image.

The other place such a relay could have lived - the 2 MiB MAS inner image that is
copied to `0xB1000000` and never started - is a **Bluetooth stack** (OBEX, GATT,
`IpcuDriver.c`, `Arm926IpcuService.c`), not an EX relay.  So that hypothesis is
closed too.

### Poking the EX cycle does nothing either

Calling the EX `McuMg` 10-tick timer callback `0x080341B4` eight times from
outside (EX vCPU, registers only, returning to the image's own `B .` loop) returns
0 every time and changes nothing downstream:

```text
0x0803D094 (SwMngCyc timer cb)  0
0x0803D180 (switch-manager handler) 0
0x0803D5FC (switch-state forwarder) 0
0x0803350C (event bit 0x02 poster)  0
0x080325B8 (EX application cycle)   0
```

so the cycle is not waiting on a tick; it is waiting on the peer frame.

### Where that leaves the natural-boot question

Both MCUs boot into a mutual wait that neither image can break from the inside:
the EX application cycle needs one lane frame that no code in the MAS image
sends, and the MAS application's own message chain is never fed, so it never
emits that frame.  This matches the published Z7 model's own conclusion that
these firmwares do not reach a UI unassisted.

Two open possibilities remain, both narrowing the problem rather than blocking it:

1. the EX `McuCm` peer is not the MAS - "McuCm"/"McuMg" only say "MCU"; the SLA
   component has not been searched for this relay;
2. the relay belongs to a stage the MAS never reaches, which is the assisted-boot
   territory the reference project occupies (see `../reference/z7-assisted-boot.md`).

### Z7-style forcing on this firmware: what each poke actually moved

Following the reference model's method (call original routines from a checkpoint),
each candidate was poked from outside and the downstream counters measured with
`-dfilter` in the same run.

| poke | immediate effect | downstream |
|---|---|---|
| MAS AutoOperation poller `0x1052329C` | 130 operation evaluations | GUI event setter 0, dispatcher 0, poster 0 |
| MAS display provider admission `0x10637B54` x24 | admission `0x104D7E78` runs 24 times | ready setter `0x104D7F16` 0, owner 0, banks 0, present 0 |
| EX `McuMg` 10-tick timer callback `0x080341B4` | returns 0 each time | switch-manager timer 0, forwarder 0, cycle 0 |

So the routines themselves are callable and they do run, and none of them
produces a frame: each one stops at the next input that the boot never supplies.
The admission path is a good example - it admits on demand, but the ready setter
and owner that follow belong to the display service's own state machine, which is
still waiting on its context event.

The practical consequence is that "imitate the reference" on this firmware needs
a real input at the *bottom* of the chain, not a call in the middle: the
reference's ladder works because its firmware exposes a single app object that
every call can advance; this firmware exposes no such object reachable without
the missing peer frame.

### SLA: what the image does and does not contain

The SLA component (`4_eg1840_sla_01700.bin`, 8,834,368 B = 0x86CD40) loads at
**0x10020000**, file offset 0, one segment, 7488 functions.  Verified against
`docs/startup/sla.md`: the ARM vector table sits at `0x10034100` (`18f09fe5
da0300ea ...`) and the ROM section descriptors at `0x10030400`.

It carries a **service directory**: 159 records of `{u16 service_id, name}` from
`0x10871124`, including

| id | name |
|---|---|
| `0x0304` | `ExMcuService` |
| `0x0429` | `ExMcuCommandServ` |
| `0x0800` | `BExMcuProtocolSer` |

and the directory base `0x1087103C` is referenced exactly once in the whole
image, from `0x10298628`, inside a table of 16-bit pairs at `0x10298600`.

That is a **directory entry, not an implementation**: the MAS image carries the
same three names, so their presence proves the SLA knows the services, not that
it hosts them.

Two searches for the frame builder came back negative:

- no function in `0x10020000..0x10700000` contains both a `#0x6A` size-like
  immediate and a `#0xF0` mask (the shape the MAS ExMcu assembly uses); the six
  functions that contain both use `0x6A` as a struct offset or an unrelated
  constant;
- the SLA's long pointer tables (`0x10038DFC`, `0x10169E48`, `0x103AF550`,
  `0x10865D74`, `0x1086B3B8`) do not match the 159-entry service directory, so
  there is no obvious parallel service-handler table to index into it.

So the relay has not been found in the MAS image, in the never-started inner
image (a Bluetooth stack), or in the SLA by shape.  Before spending more on
protocol archaeology, the cheaper discriminator is on the EX side: the EX
`McuMg` router ran exactly once early in the boot, so *some* peer frame did
arrive.  Identifying which frame that was says who the real peer is.

### The one frame the EX does receive is a version announcement

The EX `McuMg` router `0x080332F4` runs exactly once per boot, so its single
invocation can be captured.  With one hardware breakpoint on the router (armed
before the guest starts) and the EX read gadget for the memory:

```text
ROUTER hit=1 msg=0x20002108 lr=0x08031F17
  msg+0  28            message length
  msg+4  0x20011964    payload pointer
  msg+8  0
  msg+12 0x20017B28

payload  {0x00000513, 0x372E3130, 0x32302E30, 0}
bytes    13 05 00 00  30 31 2E 37  30 2E 30 32
         id=0x13 len=0x05  "01.7"      "0.02"
```

So the peer does talk to the EX, and what it sends once is a **version
announcement**: id `0x13`, followed by the string `01.70.02` - the firmware
version.  The router's table (`0x0804E15C`) handles ids
`{1,4,6,8,0xA,0xC,0xD,0x10}` only, so this frame is not a ladder command and is
dropped without effect.

That refines the earlier statement: the lane is not silent, and the peer is not
absent - the peer sends exactly one thing, a version string, and never any of
the ladder ids the EX application cycle waits for.

### The EX also transmits, and the first message is a short id-1 record

The `McuCm` channel enqueue `0x08032000` was captured at its first two calls
(one hardware breakpoint armed before the guest starts):

```text
hit 1  r0=0x20013C20  r2=0x1C  lr=0x08033EE6   (caller in the McuMg module)
hit 2  r0=0x20015F18  r3=0x20015E36  lr=0x08025486   (caller in the 0x08025xxx record module)
```

reading `r0+0` gives `0x001C0001`, i.e. `{u16 id = 1, u16 length = 0x1C}` — the
same 28-byte size as the incoming version frame.

So the lane carries a short exchange at boot: the peer announces `id 0x13`
(`"01.70.02"`), the EX enqueues an `id 1` record of the same length, and after
that the ladder ids never appear in either direction.

That is the shape of a version handshake that completes and then goes quiet,
which is why every later stage waits forever.

### Answer to "why is the ladder never sent"

Every running image was checked for a sender of the EX ladder frames.

| image | loaded at | result |
|---|---|---|
| MAS (`0x10020000`, Cortex-A7 x2) | runs | no sender: the ExMcu module's frame assembly has no command-id byte, its payload descriptor is written only by comm init and the debug CLI, the sysc layer never calls into it, and there is no ExMcu controller |
| EX (`0x08000000`, Cortex-M4) | runs | receives one version announcement (`id 0x13`, `"01.70.02"`), replies `id 1`, then waits |
| MAS inner image (`0xB1000000`) | copied, never started | a Bluetooth stack (OBEX/GATT/`IpcuDriver.c`), not a relay |
| SLA (`0x10020000` diagnostic, Cortex-A7 x2) | runs | carries the service directory naming `ExMcuService` / `ExMcuCommandServ` / `BExMcuProtocolSer`, but no 106-byte frame builder of the expected shape and no parallel service-handler table |
| VR (`3_vr1840`, Renesas **RX**) | **not instantiated** | modelled as its own machine `nikonvr`, not part of `z7ii-mas-diagnostic` |

So the ladder ids the EX waits for have exactly one candidate source left: a
component this machine does not run.  The five-vCPU topology is
two MAS Cortex-A7, two SLA Cortex-A7 and one EX Cortex-M4, and the bundle's
fifth component is a Renesas RX MCU image that exists only in the separate
`nikonvr` board.

That closes the natural-boot question in a measurable way: the EX key/switch
cycle is enabled by a peer frame that none of the components this machine runs
can produce, and the MAS image provably lacks the relay.  The firmware is not
failing - it is waiting for the peer that this machine does not instantiate.

### The VR link is wired, and the VR cannot boot

Following the repo's documented composition rule (ARM and RX are separate QEMU
targets; the full chain must use the peer transport, not a mixed-ISA process),
the camera and the VR were run as two processes with the existing peer devices:

```sh
qemu-system-rx  -machine nikonvr -bios <bundle> -display none -nodefaults \
  -chardev socket,id=s0,path=/tmp/nikon-vr.sock,server=on,wait=off \
  -device nikon-peer,id=wire,chardev=s0 \
  -device nikon-vr-peer,id=vrpeer,peer=wire,peripherals=/machine/vr-peripherals

qemu-system-arm -machine z7ii-mas-diagnostic,bundle=<bundle> ... \
  -chardev socket,id=p0,path=/tmp/nikon-vr.sock \
  -device nikon-peer,id=wire0,chardev=p0 \
  -device nikon-spi-peer,peer=/machine/peripheral/wire0,spi=/machine/spi4,\
gpio=/machine/gpio,pin=177
```

The transport works and the camera process runs, but nothing downstream moves
(60 s: display admission 0, GUI dispatch 0, event-0x39 poster 0, AO dispatcher 0,
EX ladder poster 0, EX forwarder 0, present 0).  The reason is on the VR side:

```text
nikonvr: loaded-component functional assumptions: ... no cold BootROM,
         fixed exception table or peer ACK supplied
nikon: uncovered access pc=0xfff00167 address=0xfffc8008 (read)  -> MEMTX_ERROR
nikon: uncovered access pc=0x00000000 address=0x00000000
nikon: uncovered access pc=0xffffff7c address=0xffffff7c
```

`0xfffc8008` is a 4-byte slot inside the VR's **package-external resident
interval** `[0xfffc8000, 0x100000000)` (`NIKON_VR_EXTERNAL_BASE/SIZE`).  The
bootstrap at `fff00167` reads that slot and JSRs through it before entering the
application; the component supplies neither the value nor the target, and the
model deliberately maps nothing there (explicit `MEMTX_ERROR`, never an erased
default).  The whole interval is `0x38000` bytes at the very top of the VR
address space - i.e. that MCU's resident ROM, which is not part of the firmware
update package.

So the VR guest never reaches its peripheral init (`fff001f8`/`fff005bd`), SCI5
is never configured, the MAS SPI4 clocks get no answer, and the camera stays
exactly where it was.  The documented way to supply such bytes is
`-device nikon-vr-input` plus an exact `nikon-vr-span` naming a real,
source-qualified file; fabricating the slot is explicitly rejected.

The VR path is therefore closed until a real dump of the VR resident interval
exists.  See `docs/startup/vr-domain.md` for the full record.

### MAS-side SPI4 protocol: first capture

The peer transport makes the guest's SPI4 traffic observable without touching the
model: run the camera with `nikon-spi-peer` on `/machine/spi4` and read the
40-byte peer envelope from the chardev (`work/runs/ex-6b-ercd/cap-vr.py`).

Without answering any clock request, the guest asserts select, clocks exactly
one byte `0xD0`, then cancels and retries - 14 times in 40 s.  So `0xD0` is the
first byte of the VR request and the driver will not continue until that byte is
answered.

Answering each clock with a diagnostic `0xFF` (capture side stays genuine guest
traffic; the reply is the harness's) makes the guest clock a full **76-byte
transfer** per select.  The first four bytes of the transfers observed:

```text
d0 10 00 00     f2 18 55 f0     f2 10 00 00     f1 52 00 00
f2 90 55 00     f2 10 00 00     f2 90 55 58     d4 1c e3 00
```

i.e. a fixed 4-byte command word followed by 72 further clocks, which is the
request/response window the driver expects the peer to fill.

The MAS-side stack behind that traffic was located by climbing from the SPI
driver (`0x10243xxx`) through the byte-stream transport (`0x10370A14`,
`0x10367FE0`, `0x10368096`) to the protocol client module `0x102FE6xx`:

| address | role |
|---|---|
| `0x102FE630(a1, a2, ..)` | sends a 4-byte command: `*a2` with its top byte replaced by `(a1 != 0)` |
| `0x102FE6AC(addr16, len, buf)` | bounded memory read/write: 2-byte address, chunked to 0x100, total capped at 0x900 |
| `0x10367FE0` | transport write, chunk 0xC0 |
| `0x10368096` | transport write+read |

So the VR link is an addressed command/data protocol rather than a plain frame
stream, and the 76-byte transfer is its request+response window.

### The VR command word matches a MAS RAM table

The captured first command word `d0 10 00 00` (little-endian `0x000010D0`) does
not exist as a constant in the MAS image, but it does exist in MAS RAM at
`0xB182A44C`, inside a table of 16-byte records:

```text
0xB182A410  d9 10 00 00  00*8  01 00 00 00
0xB182A420  d6 10 00 00  00*8  02 00 00 00
0xB182A430  cd 10 00 00  00*8  03 00 00 00
0xB182A440  d0 10 00 00  00*8  04 00 00 00
0xB182A450  d3 10 00 00  00*8  03 00 00 00
0xB182A460  2e 11 00 00  00*8  02 00 00 00
0xB182A470  2d 11 00 00  00*8  01 00 00 00
0xB182A480  2c 11 00 00  00*8  00 00 00 00
0xB182A490  2f 11 00 00  ...
```

i.e. `{u32 word, 8 zero bytes, u32 index}` with words such as `0x10D9`, `0x10D6`,
`0x10CD`, `0x10D0`, `0x10D3`, `0x112E`, `0x112D`, `0x112C`, `0x1254`, `0x1258`.

The frame builders `0x102FE574` / `0x102FE64E` compute their buffer as
`*(ctx+0x14) + 0xCD9A0 + 0x20 + index*16` (and `0xCADD0`, `0xAF` stride for the
other table), so the 76-byte transfer is assembled from these RAM tables rather
than from image constants - which is why the command words cannot be found by
searching the firmware image.

That gives the decoding path: the command word is a table entry, the transfer is
one command plus a fixed 72-byte window, and `-device`-free RAM dumps already
contain both the tables and the assembled frames.

### Frame-table follow-up: negative, and what it means

Following the two builder formulas
(`*(ctx+0x14) + 0xCD9A0 + 0x20 + index*16` and the `0xAF`-strided table at
`0xCADD0`) into RAM does **not** locate assembled frames:

- `*(0xB18A77A0)` is `0xB7D00000`, and `0xB7D00000 + 0xCADD0` /
  `+0xCD9A0` are entirely zero, both at 20 s and during an active SPI4 capture
  (dumped with QMP `pmemsave` while `cap-vr.py` was answering clocks);
- so the base in those formulas is not that global, and the command words are not
  read out of a persistent RAM table.

What the capture does establish, deterministically (identical with `0x00` and
`0xFF` replies):

- every transfer is one chip-select window of exactly **76 clocked bytes**;
- exactly **one 4-byte group** per window is non-zero, the remaining 72 bytes are
  `0x00`, i.e. the guest clocks a 4-byte command and then dummy bytes to read a
  72-byte window back;
- the command groups seen are `d0 10 00 00`, `f2 18 55 f0`, `f2 10 00 00`,
  `f1 52 00 00`, `f2 90 55 00`, `f2 90 55 58`, `d4 1c e3 00`.

That matches `sub_102FE630`'s shape exactly - a 4-byte command written as three
payload bytes plus a flag byte in the top position - but the command values do
not decode into any table found so far, so the next step is to read the *caller*
of `sub_102FE630` / the argument `*a2` at runtime rather than to guess it from
RAM.

### Runtime argument capture of the transport: blocked again by instrumentation

Attempting to read the transport arguments (`sub_10367FE0` entry: buffer pointer,
length, caller) while the SPI4 peer client answers clocks gave no transport call
at all inside 50 s; the peer only ever reported chip-select *deassert* signals:

```text
CAP select level=1 events=1
CAP select level=1 events=2
...
```

while the same client without GDB attached receives a full sequence of clock
requests and 76-byte windows.  So attaching the debugger changes the SPI4
activity in this configuration, exactly like the earlier breakpoint-coverage
problems - the capture path and the GDB path do not compose for this question.

Practical conclusion: the remaining protocol work should stay **static**, in the
`0x102FExxx` client module (frame builder `0x102FE1EA`, senders `0x102FE574`,
`0x102FE64E`, the multi-chunk writer `0x1030621E`, the addressed reader/writer
`0x102FE6AC`) plus the captured 76-byte windows as ground truth, instead of
trying to observe the transport live.

### The captured SPI4 traffic is not the BodyVr record builder

Static reading of the BodyVr request builder `0x102FE1EA` (4942 bytes of
decompiled code, 21 operation cases) shows a clear record format:

```text
81 00 <len> <data...>      written into the packet buffer at 0xB2B37590
...
0xB2B375B8 (buffer + 0x28)  per-record byte counts, e.g. 5, 7, 4
```

i.e. `0x81` is a write-style command, followed by a sub-address, a length byte
and the data, with a parallel length table at `+0x28`.

But that buffer is **all zero** in a normal run *and* during an active SPI4
capture (`pmemsave` of `0xB2B37000..0xB2B38000` while the peer was answering
clocks: 22 non-zero bytes in the page, none at the buffer).  So the 76-byte
windows captured on `/machine/spi4` are produced by a different client, and the
BodyVr record builder never runs.

That reopens one question rather than closing it: **which MAS client owns SPI4**.
The answer is static and cheap: the channel table at `0x1239C040` gives each
channel its MMIO base, threshold, ISR entry and clock ids, and the driver's
open/transfer API (`0x10243044`, `0x10243130`, `0x10243468`, `0x10243542`) is
reached only from `0x1037xxxx`; climbing from those callers names the clients.

### Three different senders on the same transport, and which one is unknown

Climbing the transport from its singleton handle (`0xB18A7808`, referenced once,
from the literal pool at `0x10368138`) gives the frame shapes of the clients that
use it:

| builder | frame prefix | buffer | status |
|---|---|---|---|
| `0x102FE1EA` (BodyVr records) | `0x81 00 <len> <data>` | `0xB2B37590`, length table at `+0x28` | never runs (buffer stays zero, also during capture) |
| `0x10368096` (transport write+read) | `0x82 <b0> <b1> <data>` | `0xB2B375CA`, receive window `0xB2B3774A` | shape known, not yet tied to a channel |
| captured on `/machine/spi4` | `0xD0 10 00 00`, `0xF2 ..`, ... | unknown | deterministic, reply-independent, 76-byte window |

The transport descriptor is `{tx_buf, rx_buf, 4, length}` with a 0xC0 chunk
limit, and `sub_10385D50(handle, level)` guards it as a mutex.

So the repository's documented statement - MAS SPI4 is consumed by the VR SCI5 -
is still the best hypothesis for the 0xD0 traffic, but none of the builders found
so far produces it.  The next static step is to enumerate every client that
reaches `sub_10370A14` (the transfer start) and map each to its channel by the
buffer it uses; the client whose buffer carries the `0xD0` command is the VR
protocol implementation.

### Capture-time RAM search: one command word matches a table, the rest do not

A 106 MB `pmemsave` of the MAS kernel and private regions taken *during* an
active SPI4 capture (peer client answering clocks) was searched for the captured
command patterns:

| pattern | hits in 106 MB of MAS RAM |
|---|---|
| `d0 10 00 00` | one, at `0xB182A44C` |
| `f2 18 55` | none |
| `f2 90 55` | none |

The single hit is inside a table of 16-byte records at `0xB182A40C`, which holds
values such as `0x125F`, `0x1260`, `0x1261`, `0x1262`, `0x10D9`, `0x10D6`,
`0x10CD`, `0x10D0`, `0x10D3`, `0x112E`, `0x112D`, `0x112C`, `0x112F`, `0x1254`,
`0x1258`, `0x124D` with small indices beside them - an event/state table, not a
command list.

So the first captured command word coincides with an entry in that table, while
the later command groups (`f2 18 55 f0`, `f2 90 55 00`, `f2 90 55 58`) exist
nowhere in RAM as bytes, i.e. they are produced on the fly (computed or encoded)
rather than copied from storage.

That is the state of the protocol work: command *shape* and *window size* are
known, the *values* are computed, and the producing client is still unidentified.
The next static step is the one already planned - map every client that reaches
`sub_10370A14` to the channel it uses, so the owner of the `0xD0`/`0xF2` traffic
is named.

### Isolating the SPI4 client by differential execution

Instead of guessing, run the camera twice with `-dfilter` over a wide code range -
once without any peer and once with the SPI4 peer client answering clocks - and
diff the sets of executed addresses.  Everything unique to the second run is on
the SPI4 client path.  The method works with no debugger attached, so it does not
suffer from the instrumentation perturbation seen earlier.

Result, 30 s per run: **57 addresses execute only when the peer is attached**,
clustered as:

| cluster | functions | size |
|---|---|---|
| `0x10306xxx` | `sub_103069DC`, `sub_10306DE8`, `sub_10306FB0` | 820, 214, 254 |
| `0x10307xxx` | `sub_10307FB4` | 48 |
| `0x10368xxx` | `sub_10368150`, `sub_1036820A`, `sub_10368AEC` | 86, 132, 14 |
| `0x1037xxxx` | `sub_10370242`, `sub_103708C4`, `sub_10370A14`, `sub_10370E84`, `sub_10370EE8` | 178, 336, 138, 50, 86 |

So the SPI4 client is the `0x10306xxx`/`0x10307xxx` module, and the `0x1037xxxx`
functions are the transport it drives.  `sub_103069DC` (the largest) is a service
handler that pumps `MEMORY[0xB26B4C9C]` states 4/5/6 and obtains buffers through
`sub_10298BEA(0, id, size)` - the frame assembly lives in this module.

This also explains why the earlier builders were dead ends: they belong to other
transports, not to the SPI4 client.

### SPI channel numbering, and which handles belong to which client

The transport keeps one 104-byte context per channel at `0xB1808E20 + 104*N`, and
each context's `+0xC` points at an 0x58-byte channel descriptor.  Read from a
capture-time RAM dump, the descriptors say:

| descriptor field | meaning |
|---|---|
| `+0x00` | IRQ / peripheral id (`0xE9`, `0xEA`, ... step 1) |
| `+0x0C` | channel number `N` |
| `+0x10` | `1 << N` |
| `+0x18`/`+0x1C` | clock ids (`0xC1/0xC0`, `0xC3/0xC2`, ... step 2) |
| `+0x1C` | MMIO base `0x2C0A0000 + N*0x100` |

So **firmware channel N is the model's `spi<N>` port** - the mapping is exact, no
off-by-one.

The transport handle read from the same dump is the channel number, and the known
clients hold:

| client | handle global | value |
|---|---|---|
| `0x10367FE0` / `0x10368096` (record writer) | `0xB18A7808` | 8 |
| `0x1036EB3C` (generic transfer wrapper) | `0xB1807B10` | 6 |
| `0x10383388` (long-timeout transfer) | `0xB18A78E9` | 2 |

so none of the already-identified clients drives channel **4**; each loads its
channel byte from its own context (`LDRB R0,[R4,#k]`) rather than from a global,
which is why the owner still has to be found by its context struct rather than by
a constant.

### Multi-peer observation does not work

Attaching a peer to several channels at once, or to a channel without the
chip-select GPIO link, yields **no clock requests at all** (0 on `spi0`, `spi1`,
`spi4`), while the same single peer on `spi4` with `gpio=/machine/gpio,pin=177`
receives the full sequence.  So the peer only enters its asynchronous path when
the select link is present, and one CS pin per run is what this model supports -
multi-channel capture needs each channel's own CS pin, which is known only for
channel 4.
