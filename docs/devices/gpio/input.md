# Mechanical and GPIO pad input

[IRQ and event dependency audit](/devices/gate/dependencies). The complete
original GPIO getter at `10385d76` reads the object's PDR address, shifts by
its pin byte and returns bit0. The fixed TOOL source
`57546e1cbed483ac82dff7246c5d8dccbad02279`,
`research/z7ii/qemu-rtos-startup-contract.md:808..814`, follows the original
logical lookup, constructor candidate and whole COPY0 object table. Service
`60` (hexadecimal, 96 decimal) selects object `b180399c`, source O`237d930`.
Its data address `04282018` and pin3 describe port6 bit3; its external bit12,
IRQ77 and wrapper `10386075` agree with the existing EXIU software route.
Neither a captured live object nor a physical external peer is established.

The saved whole GPIO contract already requires input/alternate pad reads to
consume supplied external levels, with pulls used for floating inputs. The
previous device had only pad outputs: every input read was derived from its
pull configuration or virtual low. The finite `external-level` qdev input
now supplies one line for each of the existing twelve ports and sixteen
pins, index `port*16+pin`.
Publication of either level, including explicit low, marks that pad driven;
unpublished pads retain the prior pull/virtual-low profile. A driven level
overrides pulls. GPIO output mode continues to expose the guest output latch
and takes precedence over an external input, an explicit functional
convention rather than an electrical contention model. Alternate mode uses
the explicitly supplied level and does not invent a peripheral provider.

External publications update the same `pad` outputs used by PDR readback and
the existing GPIO-to-EXIU connections. EXIU therefore samples those actual
levels through its established polarity/edge/mask/W1C law, and the upstream
GIC/guest handles any admitted IRQ. There is no host callback, task flag,
timer, key event or request completion. Reset clears the guest GPIO
configuration and latch but preserves the external publications; it cannot
invent a transition in the external provider state. Resetting direction or
function can still change the resolved pad level, just as their guest writes
can. This diagnostic board has fixed
topology: the interface does not specify hot-unplug or high-impedance changes.

In the original default table port6 direction`1780` has pin3 configured as
input and pull-enable`8c80` leaves pin3 floating. With no external publication
its value remains virtual low. The later private SLA peer supplies the actual
modeled handshake through this pad; an arbitrary fixed ready level is not a
substitute for that peer. Physical polarity/pad wiring and electrical reset
remain unmeasured.

The sole builder owns C qtests and native registration. Focused cases should
cover explicit low overriding a pull-up; driven high readback; output-latch
precedence with input updates retained for a later direction change;
alternate selection; first/last pad indexing; provider state across reset;
and existing EXIU12/GIC77 edge, mask and selected ACK through port6 pin3.
Keep existing strict MMIO width/hole/high-field refusal checks. These are
synthetic interface cases, not original peer or whole-boot acceptance. This
initial interface design did not itself establish a native/test result.

## Whole source-family audit

The C1.70 MAS source audit covers all 232 GPIO descriptors and 31 direct
`10385d76` read-helper call sites in 29 functions. The descriptor table is copied
from ROM VA `1239c430` to RAM VA `b180249c`, with 56-byte records; the larger
COPY0 owner starts at ROM VA `12399f94` and RAM VA `b1800000`. A read-only runtime
join filters the service registry at VA `b18aedc8` by object PDR addresses in
PA `[04282000,04282030)` and finds 187 registered GPIO objects. This counts
initialized objects, not distinct physical pads or proved accessory roles.

| Input class | Source-established pad/service | Finite provider and boundary |
| --- | --- | --- |
| EX handshake | Services 86/87/108; P5.9/P5.10/P6.15 | Existing dynamic EX peer; no static ready substitution |
| CComp acknowledgement | Service 84; P5.7 | Existing request/ACK peer, with cancellation and IRQ effects |
| SLA handshake | Service 96; P6.3 | Existing private-domain peer |
| SD insertion | Service 91; P5.14 | Explicit high means absent; separate SD transport remains unimplemented |
| Other inspected inputs | Services 5/33/36/83/95/98/106/107/143/159/169 | Source retains getters and direction/pull state; no unproved accessory identity or static presence is assigned |

The SD join is confirmed by service-map VA `120659c0`, object VA `b1803884`,
pin byte 14 and PDR PA `04282014`. Before the correction, its undriven,
pull-disabled low level falsely meant inserted. The board now publishes high
for this absent slot through `external-level`; it does not change the generic
cold state of other GPIO pads. See [storage](../storage/protocol.md#insertion-before-link-training).

The runtime audit also prevents a blanket pull-up workaround. Services 5 and
33 already read high through guest-selected pulls; service 159/P10.2 reads low
where the storage power-feedback consumer expects low. Some descriptors select
alternate mode. Unknown roles do not establish cover, lens, HDMI, headphone or
write-protect detection. Those identities require their actual callers and
providers; the audit is not evidence for setting every undriven pin high.

Six native GPIO cases pass, including the new absent-card case across reset,
pull changes and output/input direction changes. The existing IRQ fixture now
expects both its driven ACK bit and the independent absent-SD bit in whole-port
readback. Historical five-case results above are not relabeled as this run.

## Maintained photo/movie selector

The complete-bundle input frontend owns EX I2C expander `0x22` pad0. High
selects photo live view; low selects movie live view. A new frontend starts
high. F13 toggles this maintained contact once per press; release and keyboard
repeat leave it unchanged. These host key and initial-position choices are a
functional input profile, not recovered physical keyboard wiring. F1–F12 and
1–5 retain the separate seventeen direct EX GPIO scan inputs. F4/EX C5 is not
the photo/movie selector.

The C1.70 source chain uses EX VA `0803E52A..0803E53A` to sample expander
bit0 into logical input37; `0803D950..0803D9B2` inverts it into aggregate
byte83. VA `0803D65E` copies that record after a four-byte header, making
the field byte87 of the 124-byte transport report. A PowMg edge snapshot may
also feed the aggregate through the same input's source history. Its callback
VA `0803805C` receives event `020D0008`, calculates the changed 24-bit pattern
and supplies the flag word consumed by VA `08037ED8`. This is original input
processing, not an endpoint-synthesized report.

MAS VA `104343AA`, inside parser `10433EBC`, reads byte87 into normalized
u16 offset38. VA `1038B47E` carries this scalar into the 372-byte input packet
at offset124, with its changed indicator at128. VA `1029EB8A` maps scalar1
to true and submits typed setting3; the setting engine derives setting4 and
dispatches its dependent policy handler VA `1033E6C4`, which publishes property
5772 after selecting its effective value. This policy is conditional, not an
unconditional byte copy. `1035963C` consumes5772 for capture config bytes0/24;
`102324CC` later combines bytes0/24/263 to select the capture family.

A separate unchanged-firmware diagnostic drove only this external pad high
through QEMU's existing input-line API: normalized scalar and5772 became0,
and the source-named policy became `100` / `LvStill`. With the former low input,
they were1 and `800` / `LvMovie`. Application main_mode remained1 in both
profiles. This directly distinguishes the selector from the lens's50 command
and the F4-selected application branch. Expander bit10 is another field:
report byte83 becomes a normal switch mask, not this scalar. The mapping must
not be copied from a different body without checking its original consumers.

The board's obsolete static pad0 publication is removed, leaving one external
driver. Reset retains both the maintained position and the held-key latch;
reset exit republishes the level without inventing a key transition. Unrealize
unregisters the input handler and releases the expander line to high impedance.
The existing expander supplies resolution, latching and interrupts; original
EX/MAS code owns filtering, messages, setting derivation and capture policy.

The two native input subtests pass in 0.54 seconds. They cover all seventeen
direct-pad receivers and the maintained selector's default, toggle, repeat,
release, reset ordering, unchanged direct pads, unregister and recreation.
The selector fixture observes its electrical output; original executions
separately establish its expander and firmware connection. Normal live view,
shutter capture and file saving are separate acceptance boundaries.

## Mode dial ADC input

The frontend supplies the maintained eight-position mode dial through the
named `mode-adc` output. Its level is a raw12-bit ADC count, not a Boolean;
the board connects it to EX ADC1 channel2 (`adc` input index2). The former
static channel2 provider is removed so the dial has one owner. QOM
`mode-position` accepts integer0..7 before or after realization, initially7.
F14 advances one position and F15 reverses one, wrapping at the ends. Only
a fresh press turns the dial. Reset retains both its mechanical position and
held-key latch, and republishes the count after receiver reset. Unrealize
disconnects its ADC source with level−1.

The original EX VA `0803DE88` reads ROM VA `0804EF84`=2 and resolves it through
VA `08040314` and table VA `0804D9A8` to configuration ID129 hexadecimal.
The original initialized ID127..12A records bind ADC1 channels0..3. VA
`0803E2B0..0803E35C` reads this ADC descriptor, extracts the16-bit value after
shifting two bits and classifies the following inclusive ranges. The chosen
integer midpoints are explicit virtual mechanical positions, not recovered
resistor values or voltages.

| Position | Raw ADC interval | Supplied count | EX logical value | MAS setting0 |
| --- | --- | --- | --- | --- |
|0|0..230|115|0|0|
|1|231..716|473|1|1|
|2|717..1233|975|2|2|
|3|1234..1779|1506|3|3|
|4|1780..2334|2057|6|9|
|5|2335..2874|2604|5|8|
|6|2875..3398|3136|4|7|
|7|3399..4095|3747|10|4|

EX VA `0803E428` owns logical-input4 filtering. VA `0803D78C` puts the
result in aggregate byte11; the four-byte transport header makes it report
byte15. MAS VA `1043438E` reads that byte into normalized u16 offset32;
VA `1038B47E` carries it to the372-byte input packet at offset100 with changed
indicator104. VA `1029EB8A → 102A60FC` submits typed setting0 with the mapping
above. This preserves the original mode and GUI policy. Human mode names and
their original-execution effects require separate verification; the public
interface deliberately names mechanical positions rather than guessing labels.

## Command-dial contacts

Two finite contact sequencers share the existing direct GPIO outputs. Dial0
uses EX F11/F14 (raw host F8/F9); dial1 uses EX E7/E13 (raw host F6/F7). The
first pin is bit1 and the second bit0. A positive detent produces raw levels
`11 → 10 → 00 → 01 → 11`; a negative detent reverses that sequence. The first
transition occurs after5ms, and every later transition also waits5ms. This
virtual input timing is a functional assumption to give the unchanged EX
polling loop time to sample each phase, not a measured rotary-encoder rate.
Time advances the contacts only; it never emits a report, setting, GUI update
or guest interrupt.

Write-only QOM integer actions `dial0-step` and `dial1-step` accept+1 or−1
for one detent, or0 to cancel that dial's active and queued sequence. F16/F17
submit dial0 positive/negative turns; F18/F19 submit dial1 turns. Repeat and
release do not submit another turn. Each dial has its own eight-detent FIFO,
including its active turn. Full queues reject another action without changing
the admitted sequence or deadline. Actions require a realized frontend.

The sequencer owns both pins until it finishes or is cancelled. A changed raw
F6..F9 key cancels the corresponding sequence and hands both contacts back to
the held raw-key states. A detent request while either of its raw keys is held
is rejected. Explicit cancellation also restores those raw levels. Reset
cancels both queues and timers; sequenced contacts return to idle11 while raw
held contacts retain their level. Unplug cancels the timers before releasing
the pads. This single-owner rule prevents the two input interfaces from
silently fighting over a contact.

Original EX VA `0803E364` reads dial0: state2→0 increments its signed counter,
and0→2 decrements it. VA `0803E3BC` reads dial1: state0→1 increments and1→0
decrements. Both counters saturate at±127. The GPIO helper chain
`0803CE34 → 08043ACC` reads raw IDR without inversion. VA `0803DF98` snapshots
the counters at RAM VA `200186D1/200186D2` to aggregate offsets43/44 and clears
the source counters. VA `0803D710` accumulates signed values at report aggregate
offsets7/8; after the transport header these are report bytes11/12.

MAS VA `10434378/1043437E` sign-extends those bytes into normalized offsets16/20.
VA `1038B47E` places them at input-packet offsets84/88. VA `102A59E8` combines
them with the auxiliary counters at92/96 and negates the second sum, then
dispatches through VA `1029D28A` or `1029D05A`. Original settings4455/4456 can
reverse or exchange their use. In the selected exposure-related contexts,
VA `1029BA78` and `1029B9D0` submit the firmware's own typed settings rather
than interpreting a contact as a fixed shutter or aperture assignment.
Front/rear naming and GUI exposure behavior remain original-execution checks.

The expanded native fixture exercises all eight ADC positions, invalid mode
values, held/reset behavior, timed four-phase traces in both directions,
independent concurrent dials, admission during an active turn, queue capacity
and reuse, explicit/raw cancellation, held-key arbitration, reset and unplug.
These synthetic electrical checks do not establish original mode acceptance,
physical encoder timing, aperture movement or shutter operation. In particular,
the [finite virtual iris](../lens/protocol.md#optical-fields-and-calibration)
processes its own bounded targets and busy state; static maximum-aperture
fields are not user-selected exposure values. Six native input groups and
eighteen lens groups pass separately. Original mode switching and the remaining
exposure-input boundary are reported in the
[continuous receipt](/evidence/continuous/capture.json).
