# Original-firmware startup frontier

## Normal display request accepted; partial-alpha SRC_OVER frontier

The latest unchanged-bundle runs cross normal display-request admission with
healthy battery data, explicit descriptor17 resource19/21, descriptor18 resource57
and declared raw acceleration axes`(0,0,1024)`. The original device initializes
through I2C2/address1d. Source-backed held-TX completion lets the original driver
submit RX and publish the direction contribution through its ordinary consumers.

Original execution joins`103d4e7a`→direction notification→`10350aa6(input8)`→
`1020f7c0`→application state2→`103f2540`. An initial return4 was traced to
resource19's erased signed threshold -1 versus measured0. Two complete original
Z7 TEMP defaults, supplied independently through explicit NOR input, both make
that call return0. Application remains state2, primary request state1, BatCk1
and power1. No guest event, pointer, readiness mask or return value is injected.

The [MXIC read-view correction](../devices/config/view.md) passes eight native
configuration cases after an actual old-model red. The unchanged original
initializer now programs selectors, draft fields/slots, completes its timed
transaction and returns at`103d1fb2` without clearing TRVSW1.

The [timing divider/load correction](../devices/timing/load.md) now covers the
complete source byte field and its initial-count combinations. Raw31 is
divisor-minus-one for50, not a one-shot opcode. Twelve native cases pass after
recorded original/native reds, including all256 encodings,12 channels, scaled
expiry/recurrence, count boundaries, divider phase, optional features and reset.
Original channel7 receives its IRQ/status1 through the original dispatcher,
crosses all seven waits in`102d6f7c` and reaches its return sequence.

The [capture-control mask/enable correction](../devices/sensor/control.md)
normalizes the source's complemented unit mask and implements paired12-bit
reset/enable commands, independent RMW/IRQ masks and adjacent finite controls.
Four native cases pass after original/native reds. Original`10239740(2eb)`
now reaches its return sequence, with reset bitmap`d40`, enabled bitmap`2bf`
and all eight pending words0. No completion or software event is supplied.

The [kind40 allocation and mapping](../runtime/kind40.md) are now joined:
record15 converts PA`62059800` to VA`36d59800`; original section descriptor
`62014426` translates that VA back to the same PA. Only the owned`1c8000`-byte
allocation is backed, with no physical VA mirror or whole-class expansion.
All19 native memory cases pass after an actual old-model red. In unchanged
execution the original clear returns to`10238364`.
The [resource11/12 source-stream join](../runtime/rows.md) now backs both
complete destination capacities and accepts explicit primary NOR files.
Original validation/loading and all eight layouts produce a byte-exact
`1c8000`-byte output containing2640 empty rows under the declared functional
input. All24 memory qtests pass after an actual native red.
The [SYS configuration audit](../devices/sysconfig/runtime.md) then completes
all three full-word ports`04280064/68/6c`, with25 memory qtests passing after
an actual old-model red. Original execution crosses`103c5d6e` and joins the
`b18a7718 != 0` branch at`102d9448` → `102fea1e(1,1)` → `102974de`.
The [router audit](../devices/router/runtime.md) corrects all five00..24
set/clear pairs and identifies all three write1 request producers and IRQ
consumers. The [finite timing model](../devices/router/timing.md) now adds
explicit resynchronization/fence hypotheses and a30Hz virtual input;23 SLA
plus25 memory qtests pass. This cadence and the request-to-cause assignments
are functional assumptions, not physical measurements.

In final-binary original execution, IRQ87 reaches original callback102d6b6a
with raw80/10, and waits return1 at102d94a2/102d9510. Application bytes are
03/02, primary request state11, power0 and BatCk-containing word00010001.
At that router checkpoint the first rejection is CPU0 PC`1020a832`, WRITE32
PA`2006800c=1`. The [capture run/stop profile](../devices/sensor/runstop.md)
now covers all11 source-selected unit bits with separate low-level/upper
quiescence causes, shared4/8 and independent cancellation lifetimes. All25
sensor qtests pass after native reds. Original code crosses that store and
later GUI wait4000 is released by original callback104be418(05040005).
The [audio format profile](../devices/audio/format.md) now accepts the complete
source-shaped format/port/divider fields and block programming;15 native
audio/DMA/HDMAC cases pass. Original1036da82 constructs AUMD0d820000 by RMW
and returns to102dea1c. Block programming and masks then execute. The
[HDMAC reload model](../devices/hdmac/reload.md) now covers all8 combinations
of RC/RS/RD at bits23:21. An applicable21-case native batch, focused5-case
HDMAC boundary run and3-case reload guard run pass after the old-model red.
Original102dc41e accepts both input descriptors, the original start arms
selectors5/6, and1036dd3a input enable returns to102dea86. The initializer
reaches its return sequence102dea90. The [Audio observation profile](../devices/audio/monitor.md)
now supports the54/58 and5c/60 getters under explicit last-observed/packing
assumptions;22 applicable native cases pass after the old-model SS3 red.
Original execution crosses the input getter and amplitude consumer. The
first rejection at that Audio-only checkpoint is CPU1 PC`106ee3c6`, WRITE32 PA`201810e0=1`,
reason`transfer-stream-truncated`, in paired graphics submission106ee04a.
This is a parser frontier, not proof of genuinely truncated guest data.
The captured submission is856/1088 bytes at PAbd0dff90/bd15ff90, with
720x480 geometry fields. Its unrecognized dispatch falls back to fixed LUT
length validation; see the [transfer frontier](../devices/transfer/protocol.md).
The [inline rectangle model](../devices/transfer/rectangles.md) now supports
the whole self-contained signed32/SRC family. Its initial 14-case batch passes,
including variable batches, colors/alpha, key wrap, exact pixels and guarded
32-bit RAM/alias boundaries. Original856/1088 START is followed by entry to
original IRQ129 handler106ee3ca. The subsequent host IOTLB assertion is now
[bound and fixed](../startup/ram.md): original PC `10634fbe`, VA `38f661c8`
maps to PA `bed661c8` in `z7ii.resource57-record0`; its independently mapped
backing had the wrong page offset. Exact aliases preserve all physical bounds.
The RAM-only 43-case batch passes, including real TCG reads and writes at 39
addresses over reset. The [continuation model](../devices/transfer/continuation.md)
now retains the initialized profile and next key across ACK, completing the
2120/2368-byte pair's ten rectangles with keys `10005..1000e`. All 15 transfer
unit cases pass; unchanged execution receives original IRQ129 for that pair.
The current rejection is CPU1 PC `106ee3c6`, WRITE32 PA `201810e0=1`,
`transfer-stream-program-not-supported`, on complete 176/368-byte buffers at
PA `bd0dff90/bd15ff90`. Fixed fields match the existing record7 SRC_OVER
profile except partial-alpha paint `cc000000`; indirect arrays are not yet
validated and partial-alpha compositing remains unsupported. Input sample
transfer, audio completion and GUI first frame remain unverified.
The capture notification mappings have native coverage only in this run;
the observed0c is followed by disable14 before its queued fence completes.
No2c/30 router execution is claimed; their contract is native-tested only.
Latest receipts are in `rectanglecontinuation-20261023`; `ramalignment-20261023`,
`graphicscommands-20261023`,
`audiolevels-20261023`,
`hdmacflags-20261023`,
`audioformat-20261023`, `captureflow-20261022`,
`routerfunctional-20261022`,
earlier `router-20261022`,
`sysconfig-20261022`, `streamstorage-20261022` retain their own checkpoints.
The UART peer rejects no protocol request. The current final GDB snapshot
completes with BatCk `00010001` and gate/pending3/display-ready zero.
Request admission and TG boundaries do not establish scanout or a GUI first frame. The earlier
432/616-byte graphics pair belongs to the erased-TEMP failure transition and is
not the current accepted-request frontier.

TEMP/acceleration records remain qualified cross-model ROM candidates;
the resource12 empty list is a separately declared functional hypothesis.
Physical board selection,
calibration, raw acceleration units and conversion latency remain functional
limits. See [guiupdate](../runtime/guiupdate.md), parent-owned`guinormal-20261022`
receipts and`temp-comparison.json` for the full joins and prior comparisons.

## Complete finite battery protocol checkpoint

The [UART peer](../runtime/batterypeer.md) now implements the complete14-call
original battery transport inventory together, with an explicit26-byte
identity/status/metrics profile and source-backed9600-baud byte pacing.
The unchanged class1 original run returns0 from the full query routine
at08023df0, then processes72 complete requests over20hostseconds without
an unsupported command. Original EX ends at state2/substate0/diagnostic0,
charge100; the actual MAS BatCk word is1. GUI gate and pending3 remain0.
These are separate receipts from the earlier no-peer cold-F4 run and do not establish
GUI first-frame/scanout. Native coverage also includes controls25/26 and
the optional14/12 echo; see [batteryprotocol.md](../runtime/batteryprotocol.md).

## Healthy battery plus held-input checkpoint

The complete peer has now also run with the unchanged bundle, constructed
descriptor and cold F4 input held continuously. BatCk becomes1 and remains1;
EX ends at state2/substate0/diagnostic0 with charge100. The manager remains
current0/target5/mode0/state2 and the GUI stays idle, class4/mode0. A targeted
policy trace confirms worker0 continues processing events. The earlier
warning-path GUI entry below is not the result of this healthy combined run.

The [ordinary GUI investigation](../runtime/guiupdate.md) now captures the
actual policy divergence in a one-variable no-peer comparison. The original
`RxBattInfo` receiver writes properties6401/6402 to1 for the no-peer error
report and to0 for the healthy report. The no-peer normalized report has
selector byte7=0 and status byte11=5; healthy has status0. The third no-peer
policy call reads6401=1 and requests GUI action8 with reason640; the healthy
run skips this branch and returns29/reason6ec. Thus the earlier GUI entry is
an error-report path, not a missing mandatory readiness event.

The verified property storage is **word** `b2551f40` / `b2551f44`.
Earlier diagnostics read `b20d2894 + key` bytes, which are not these properties
and cannot establish their values or lifetime. The corrected no-peer final
words are1/1, healthy0/0. The ordinary non-warning request route remains under
investigation.

A source-backed public request route runs from application method `10247d3e`
through `10247e3c`, queue `103ef318`, state machine `103e7312`, then completion
`04160003` and public event `0410000b`. The healthy `appdisplay` trace does
not enter either request method or the state machine; its state byte
`b23f6a80` remains0. The completion callback receives other events, but no
`04160003`. This distinguishes an unissued request from a submitted operation
waiting for completion; the method's legitimate caller is still unresolved.

The separate input-unlock message `02000006` has also been sourced: property6922
must become1 and the one-shot word `b1818f88` must be0. Its property producer
belongs to `CaptureService` / `capseq`, not an established generic boot-ready
contract. The healthy `serviceready` trace sees no entry into that capture
state machine or its callback, and property6922 remains0. This observation does
not justify fabricating capture readiness to reach GUI.

Input lock word `b1811f0c=80` is present in both runs, including the no-peer
run that enters GUI. Application message `02000004` is delivered with value0;
the original worker0 wrapper derives it from its initial saved-state6 marker
and publishes it through `05040016`. This is not an independently missing
ready notification. No input lock, job state or GUI event was injected.
Gate, pending3 and display-ready remain0 in the healthy run; its sampled
surface words remain0 and no GUI first frame or scanout has been verified.

Parent-owned receipts are in `work/runs/displayhealthy-20261027/`: the20-second
baseline, targeted policy and notification traces, `healthyhistory` /
`warninghistory` comparison, and final read-only
snapshots. The released-F4 healthy comparison selects main mode5 rather than
held-F4 mode1, but still leaves worker0 active and GUI idle. Numeric main mode
alone does not establish a GUI request.

## Earlier held-input checkpoint without the battery peer

With the same unchanged bundle and functional descriptor, existing QMP F4
input is held from the start of execution. Original EX samples GPIOC5 low;
MAS record36[20]=2 normalizes to type1 and original code selects main mode1.
Manager reaches current3/target5/mode1/state2, GUI worker class1/mode1, while
BatCk remains2. This crosses the released-input worker0 mode5 boundary below
without a battery peer or any guest software-state injection.

The finite [shared timing load](../devices/timing/protocol.md) now stages
ffffcd37 at PA2c011004 and consumes mask200(hex) at2c011008 for channel9.
The original period isfffffffe, so its first threshold is13000 ticks away.
After native red→green (eight timing cases), unchanged original execution
reaches callback102eb748 through the original ISR with r1=1. No callback,
guest software state or event was injected.

The preceding rejection was MAS CPU1 PC102dbba2, WRITE32 PA20070010=1,
`fill-request-profile-not-supported`. Engine0 fields are control0, mask11(hex),
flags1, pattern/dimension/parameter0, width=stride=b40(hex), count1e0(hex),
destinationbf309600. This requests1,382,400 zero bytes (720x480x4). Existing
finite geometry matches, but accepted720x480 destinations arebec5f200 and
c175d600; bf309600 is outside that contract. Original caller10389e08 invokes
102dba60 at10389e24 and registers completion callback10389dc2. Which resource
owns this destination, and its completion/IRQ path, are the next investigation.
The finite extension now adds only this exact destination to the existing
engine0 tuple. Three native fill cases pass after the recorded old-model red.
Original execution reaches callback10389dc2 with event41050c01 and saved
status1; waiter1038a3c6 returns1 at10389e2e. Active first/last words are0,
while the adjacent tail word atbf45ae00 remainsff000000.

`work/runs/timingpreset-20261025/frontier.json` retains the joined input,
initializer, callback and fill-register capture; `frontier-qemu.stderr`
records the historical rejection. The new
`work/runs/reproducible-20261026/mainline.json` joins the accepted fill and
25-host-second continuation without a new uncovered transaction. GUI gate
and pending3 remain0, manager remains
current3/target5/mode1/state2. Final scanout and a GUI first frame remain
unverified. Earlier `work/runs/policy-20261025/coldkey*` and `coldrecord*`
retain their historical timing rejection. The [readiness probe](../runtime/readiness.md)
now reaches its gate condition with selector1, which skips the selector34
setter; configuration290 is not read on that path. Mode entry reaches its
return at1054059e with gate0/pending3=0, so this is a conditional publication
skip rather than a blocked mode-entry wait. The final host-interrupted EX PC
is not a new fault. See [fill.md](../runtime/fill.md) and [policy.md](../runtime/policy.md).

The separate [reproducible battery peer](../runtime/batterypeer.md) now passes
authentication, declared class1 0c/0e initialization and finite zero-control
command02 through unchanged EX without an in-transaction debugger pause.
Command02 returns0 with reply02060004; the next request is020b0009,
TX4/RX14/timeout100 ticks, which decodes state data. That historical boundary
is now covered by the complete finite protocol above. That peer is not present
in the cold-F4 mainline receipt above; neither run establishes full battery
operation, physical identity or a GUI first frame.

## Functional-input checkpoint: warning composited; worker0 policy/event loop

The explicitly constructed [descriptor17 profile](../inputs/nor/functional.md)
keeps the original bundle unchanged and uses the existing external-input
property. It retains the qualified Z7 panel records and supplies a hypothetical
ID34 with raw dimensions 30/20, aggregate dimensions 6/5, bounded tiling
rectangles and explicit numeric assumptions. The original mapper returns to
`103c07a8`; all 628 derived bytes match the original assignment map.

The first run reaches manager **current 4, target 5, mode 15, state 2**, beyond
the erased-input worker-0 exit boundary below, then rejects a232/416-byte
graphics pair at `201810e0` with `transfer-stream-program-not-supported`.
The [stream investigation](../runtime/stream.md) identifies an overstrict
fingerprint: only initial draw word25 differs,0 versus8. The original clear
builder preserves software-shadow bit3 there, then overwrites the same byte
with8 at word60 before either trigger. The finite model now admits exactly
those two initial values, retaining every other field and DMA/path check.

The focused native transfer batch passes after a recorded red fixture. With
the unchanged bundle and same constructed input, original code submits the
initial8 variant, whose indirect path bytes match the existing1024x688 clear.
Active first/last surface samples become `00 00 00 ff`, then original IRQ129
handler `106ee3ca` is entered. That pair is no longer the startup boundary.

The subsequent complete pair is176/368 bytes,44/92 words, using format0 RGBA,
mode8, paint `ff000000`, one path key `10001` and one draw trigger. Its target
is resource57 record7 at `c175d600`, extent720x480, clip719/479. A focused
read at original submission entry `106ee04a` observes path segments
`02 07 09 07 00` and float coordinates `(0,0,720,480,-720)`, hence a full
rectangle. Caller LR=`106eeab9` matches `106eea34`'s submission call at
`106eeab4`. The finite model now accepts this separate exact profile through
the existing DMA validation, deferred RGBA executor and completion path.

The new single-path fixture fails on the previous parser, then the native
transfer batch passes all seven cases. In the unchanged-bundle functional run,
the720x480 target's first/last samples change to `00 00 00 ff`, its tail sample
stays zero, and original IRQ129 handler `106ee3ca` is entered. No input or
firmware change was required for this step.

The subsequent pair is168/232 bytes,42/58 words, at source addresses
`bd0e0040/bd160100`. It targets the same `c175d600`, writes mode8/state108,
selects paint `ffb4b4b4`, path key10002 and one draw trigger, with normal
ending commands. A focused entry probe measures segments `02 07 09 07 00`
and float coordinates `(96,86,528,308,-528)`: the gray rectangle covers
x96..624/y86..394 inside the720x480 surface. Caller LR remains `106eeab9`.
This shorter pair omits full format, target extent and clip initialization.
The finite model now retains the known720x480 context after successful
execution, across ACK, gray continuation and supported path filling. Incompatible
completed profiles, rejected
complete programs, processing failure and reset/unrealize invalidate it.
Cold, canceled or failed initialization cannot establish the required state.

The new gray fixture fails on the preceding parser and the complete native
transfer batch then passes all eight cases. In the unchanged-bundle functional
run, original code submits the gray pair with the measured path above. First
and last rectangle samples become `b4 b4 b4 ff`, while left/right and next-row
samples remain `00 00 00 ff`; original IRQ129 handler `106ee3ca` is entered.

The subsequent pair is21488/28832 bytes,5372/7208 words, sources
`bd0e00f0/bd1601f0`. Full decoding identifies131 setup path records and131
draw triggers, keys10003..10085. Every paint is `ff000000`, target `c175d600`,
with retained format/extent/clip and varying 3x3 transforms. The first path
has18 coordinate words and10 segment bytes; its transform scales27/36 and
translates200/350.3232421875. The next has62 coordinate words and21 segments,
including code0a. The [glyph investigation](../runtime/path.md) confirms
MOVE/LINE/QUAD/CLOSE semantics, direct buffer y, nonzero winding and SRC_OVER.
ROM strings, baseline grouping and identical repeated outlines identify the
six-line battery communication warning. It is an error-display path.

The finite affine path model now processes that batch with the same unchanged
bundle/input. Native old-parser red and backing-alias red turn green; the final
transfer batch passes all nine cases. Actual guest memory contains the warning:
25958 changed gray-panel pixels,11874 solid black and14084 coverage-edge pixels,
ink bounds `[200,605) × [105,378)` in buffer rows. Outside-panel pixels remain
black and all target alpha is opaque. Original IRQ129 handler `106ee3ca` runs.

The subsequent pair is824/1112 bytes,206/278 words, sources
`bd0e54e0/bd167290`, keys10086..1008a, with five draw triggers. Entry reads
confirm identity transforms and rectangle segments `02 07 09 07 00`:
two2x308 side strips at `(622,86)` and `(96,86)`, two528x2 strips at `(96,86)`
and `(96,392)`, and a72x72 patch at `(112,306)`. The first four paints are
`ffe6e6e6`, the final one `ffb4b4b4`, with even-odd operand `00040002`.
The extended path processor now supports relative H/V, even-odd filling and
ordered opaque colors. The focused native old-model red becomes green, with
nine transfer cases passing. The unchanged bundle and same input complete
all five paths through original IRQ129. A full-frame comparison verifies
3328 light-gray border pixels and5184 gray patch pixels; all other pixels
exactly match the previous131-path frame. The patch already had that gray
background, so only3328 pixels change. No firmware or input bytes were changed.

The accepted completion-to-fault probe observes the later first MAS data abort
at original store `106ee3c6`, `PA201810e0`, width4/value1:
`lr_abt=106ee3ce`, `DFAR_S=201810e0`, `DFSR_S=1808`.
The subsequent pair is176/336 bytes,44/84 words. It has key1008b, target
`c175d600`, paint `ff4848f0`, mode low byte0e rather than08, image source
`b97a6c00` and image extent register `00500048` (72/80). Setup now embeds
segments `02 07 09 07 00` and signed words `(0,0,72,72,-72)` after control
commands, rather than the preceding indirect float arrays. The affine matrix
has diagonal `(1,-1,1)` and translation `(112,378)`. These observations suggest
a72x72 image-painted rectangle over the cleared patch. The
[image model](../runtime/image.md) now processes its80x72 A8 source, inline
signed32 coordinates and f24 sampler through original IRQ129. The red icon
changes3751 pixels (3365 solid,386 partial alpha), while every pixel outside
the72x72 patch matches the prior frame. The focused old-model red becomes
green with all10 native transfer cases, including bounds, alias, late failure,
reset and cancellation checks.

The subsequent pair is224/1736 bytes,56/434 words, sources
`bd0dff90/bd15ff90`, still caller LR=`106eeab9` at entry`106ee04a`.
It submits source`bf309600`, source format100, allocated1024x688 extent and
active1024x682 geometry. Destination format105 uses target`cc18b000` and
second plane`cc235800`, separated by1024×682 bytes, with aligned target
extent1024x688. Setup has identity initialization followed by a path matrix
diagonal`(1,-1,1)` and y translation688; inline signed32 coordinates are
`(0,0,1024,682,-1024)`, key10001. Draw mode2, SRC selector0, LUT/state and
two-plane mapping are now covered by the finite
[YUV105 conversion](../runtime/conversion.md). The input is a different buffer
from the720x480 warning target. All11 native transfer cases pass after the
valid old-model red, including independent color/row oracles and full output,
alias, late failure and cancellation boundaries. Unchanged firmware with the
same explicit input completes this pair through original IRQ129. Its source
is black RGB and remains unchanged; both698368-byte targets become Y16/UV128,
with their neighboring guards unchanged. This does not establish warning
presentation or a final display commit.

The next pair has the same224/1736 lengths and command sources.
It changes source to resource57 record0 at`bec5f200`,720x480, and inline words
to`(0,0,720,480,-720)`. Path scales become1.422210693359375 and
-1.4208221435546875, with y translation687.99993896484375. Destination
format107 uses`cb82b000/cb980000`, delta`2*1024*682`, aligned1024x688 extent.
Sample f24 words46/54 change to`0034fff8/003efffe`, and filter/state selectors
153/143 change1→7. The finite scaled model now accepts exactly this captured
state, including partial path coverage, all8-bit alpha, SRC edge preservation
and packed Y0/Y1/U/V plus separate alpha. Old-model red and independent review
precede a green12-case native transfer batch, with every packed/alpha output
byte, independent sampler/color checks, edge RGB retention and safety/reset/
cancellation boundaries. The unchanged bundle/input completes through IRQ129.
The full1382400-byte source is transparent zero and stays unchanged; primary
output contains Y16/UV128 and the full alpha plane is0. Both guards are intact.

The next pair is176/1552 bytes,44/388 words, sources
`bd0e0070/bd160660`, key10002. It uses the720x480 warning surface`c175d600`
and the same scaled sampler/107 targets, changes blend selector1a0 to SRC_OVER
and target1a5 to1, and omits full geometry initialization. It needs retained
107 context and a bounded compositor, now implemented. Both captured streams
decode completely; the former`transfer-stream-truncated` came from falling
through to the older LUT parser's fixed224/1688-byte requirement. Native
old-model red becomes green with13 transfer cases, including all source/old
alpha combinations, tabulated color results, context lifecycle and negative
boundaries. Independent review checks fields/masks, context and32-bit bounds.
Unchanged original execution completes through IRQ129. Every packed output
byte matches independent source sampling and conversion; alpha is within1 of
analytic coverage. The actual output visibly contains the battery warning.

The subsequent60-host-second window has **no new graphics error or MAS data
abort**. Manager becomes0/5/0/2. A separate joined probe observes worker4's
phase4 request`{0,0}`, phase3 exit acknowledgement and worker0's phase2 start.
Worker0 then returns from its first event wait with flags10012; no stuck-wait
claim is made for that call. In a further30-second event-return probe it
settles at class1/mode0, stage2, receiving40000000 once and10 repeatedly.
GUI worker remains class4/mode0. The subsequent [policy trace](../runtime/policy.md)
observes original ROM initialize power gate`b181a6d0=1` and previous state
`b181b3ec=6`, then original cleanup/policy clear both. Main mode5, selected
by application event02000002's36-byte record, takes the mode5 branch and
returns29. This is not a missing producer of the initial1/6 values.
Panel=`b7f54a00`, GUI gate and pending slot3 remain0, BatCk remains2.
The joined producer is EX system-report helper08033118: word20013960 bit0
is0, so it encodes raw106-byte report[28]=1. The entire106-byte EX output
matches the subsequent MAS input. Constructor10431380 preserves that value
as record36[20], which selects normalized type2 and main mode5. This is a
different report from BatCk's112 bytes. The source is low-active GPIOC5:
the existing F4 input is released/high, so its original sampled bit is0.
A later normal QMP F4-down reaches actual C5 IDR low, but the bounded run
does not resample or publish a changed system report. Original EX requires
two update bytes before dispatching PowMg1000; the live values areAC=1/B1=0.
The captured9-byte control request is all zero: it uses the direct B1-set
branch and does not register a28-byte reply. A later B1=0 can therefore be
consumption of that one-shot update. The next lead is the control request's
sender and held-GPIO startup timing, plus timely battery authentication and later-command
semantics. The physical camera control name, physical display
identity, downstream scanout and a GUI first frame remain unestablished.

The accepted probe uses one hardware breakpoint at a time, after correcting an
exploratory retained internal breakpoint. Actual input recipe, argv, expected
and observed mapper outputs and the later stop are retained in
`work/runs/descriptorfunctional-20261022/`. Follow-up native/execution receipts
are `work/runs/streammodel-20261022/`, complete next-pair capture/decoding is
`work/runs/streamnext-20261022/`, and its current indirect operands/caller are
`work/runs/streamboundary-20261022/`. Current red/green, stream/IRQ/fault and
next-pair decoding receipts are in `work/runs/singleclear-20261022/`; the next
rectangle's read-only entry capture is `work/runs/singleboundary-20261022/`.
The latter captures its operands before an asynchronous debug stop on EX; it
does not supply another MAS frontier receipt. An earlier singleclear probe
tried to read just below record7's mapped slot and failed in the debugger;
`exploratory-*` is excluded from the completion-to-fault claim. Original
firmware and the constructed input remain unchanged. Factory-data recovery is
optional for continuing this functional route.

Current native and accepted gray-completion/next-abort receipts, full next-pair
bytes and command decoding are `work/runs/graycontext-20261022/`. The first
green attempt exposed a signed-extension bug in the test's comparison only;
`fixturecomparison.log` records it, and the corrected `green.log` passes.
The next131-path batch's separate read-only entry capture, all131 indirect
arrays and transforms are in `work/runs/graypaths-20261022/entry.json`, with
caller LR=`106eeab9` and command bytes matching the main fault capture.
Coordinate counts range10..128 and segment counts6..37; observed segment
codes are00/02/04/0a. No additional external data was needed to capture them.

Current implementation/native receipts, final original completion/next fault,
full framebuffer and preview recipe are `work/runs/pathmodel-20261022/`.
The earlier successful pre-alias-check execution is retained as `prealias-*`;
the unprefixed records use the final checked build. The passive five-path entry
capture and all its indirect arrays are `work/runs/pathboundary-20261022/`.
The buffer preview is not a scanout receipt: it reverses captured rows using
the fixed Z7 presentation convention and does not establish Z7II physical
orientation. Final display publication and GUI gate/pending3 remain unproven.

Current five-path native/completion/frame/next-fault receipts are in
`work/runs/coloredpaths-20261022/`; the next inline packet and a bounded raw
image-source window are retained in `work/runs/textureboundary-20261022/`.
The latter is only an entry capture, not processing acceptance. A first-report
MAS capture also observes unknown battery typeff and mainfault1, with software
override disabled; the normalizer produces fault1 before BatCk becomes2.
See [battery classification](../runtime/compatibility.md) for the upstream EX
conditions and the distinction between report arrival and usable power.
The separate original EX probe in `work/runs/batteryhandshake-20261022/`
observes three21-byte initialization requests expecting29-byte replies, each
returning transport4, followed by error8 on structure`2001fa50`. This narrows
the battery issue to a concrete transport exchange. The subsequent work below
identifies the event-bit2 producer and consumed reply fields; no battery-success
data is injected.
The current A8 implementation/native/frame/next-abort records are in
`work/runs/texturemodel-20261023/`; passive next-pair submission and alarm
captures are in `work/runs/imagetransfer-20261023/`. The latter directly
observes EX`08023768`, LR`0804ab53`, TX count21/21 and RX count0/29, with
driver record selecting USART1 at EX PA`40011000`. Static source registration
names that callback's timer`BatCmAl2`. It signals event4, producing transport4.
The present machine connects USART1 to`serial_hd(0)` and these runs use
`-serial none`; no external battery peer supplies receive bytes. A valid peer
also needs the random-dependent29-byte challenge response described in
[battery compatibility](../runtime/compatibility.md); framing/XOR alone is
insufficient. No supported peer key source is established in current evidence.

YUV105 model/native/completion/source/plane/next-abort receipts are
`work/runs/yuvmodel-20261023/`; the next107 submission/full source is in
`work/runs/yuvnext-20261023/`. Current107 native/plane/next-abort and complete
176/1552-byte command decoding are in`work/runs/yuva107-20261023/`.
Current SRC_OVER native/frame/completion and joined manager/worker0 event
receipts are in`work/runs/srcover-20261024/`. The host-only
[authentication reconstruction](../runtime/authentication.md) now checks the
field/ladder algorithm against an independent affine oracle. The bounded
Z7II/Z6II/Z7 inventory finds shared public constants, not an identified peer
private scalar or runnable responder. Algorithm reconstruction, secret-source
recovery, original EX validation and normal startup remain distinct results.
The subsequent [wire-seed analysis](../runtime/seed.md) proves at most2^31
first-scalar seed-equivalence classes. The initial four-thread search recovers
a representative in18.589 seconds. A subsequent fresh UART challenge is
recovered in1077.040 seconds with12 threads,1.603 million candidates/s and
1,726,661,050 candidates tried. Independent Python G recomputation matches
all17 bytes. Public H supplies the tag without a battery private scalar.
The actual29-byte reply is sent through upstream USART1/chardev; original
transport returns0, original validator returns1, state+4 becomes1 and EX
enters query0c. All vCPUs were paused during host computation: this is
**timing-assisted original acceptance**, not a real-time responder or complete
battery initialization. No guest-private seed/nonce is read and no guest
status/event/return is supplied. No0c reply is sent; three0c requests are
followed by a fresh authentication cycle. The next boundary is the finite
identity/class fields and0e acknowledgement, alongside response latency.
The inspected Z72 responder
reads guest nonce RAM and the Z8 path supplies a synthetic report, so neither
establishes a wire-only peer. Current host recovery/reply receipts are in
`work/runs/seednative-20261024/` and `work/runs/batteryaccept-20261025/`.

## Erased-input baseline: rear-panel table fault during worker exit

The [battery delivery comparison](../runtime/battery.md) reproduces the EX
system handshake and 112-byte report on the current source-graph build with
`-accel tcg,thread=multi`, without `icount`, obsolete profile selectors,
descriptor overrides or guest-state injection. Native-start runs with no
debugger during the 60-host-second measurement observe one MAS battery
frontend entry and one BatCk publication. Final `b18651d8=2` and
`b23bcd9c=1`; worker 0 passes the wait at `104386c4`.

The prior general diagnosis was too broad: the absent report is reproduced
under single-thread TCG, including the `icount` probes below. At EX
`08034b0e`, twelve attempts to send the first system frame fail the 10-tick
mask-`0c` event wait, retain ACK `20017d58=0`, and leave TX DMA NDTR
142..192. Multi-thread TCG completes that frame with a successful wait,
ACK 1 and NDTR 0. The historical positive logs also used multi-thread TCG.
The CPU/FIFO timing mechanism responsible for the single-thread partial
transfer is not yet established; this is not a missing BatCk frontend.

The [handoff investigation](../runtime/handoff.md) identifies the later state-3
boundary. Manager current worker 0 is exiting before target worker 4 starts in
mode `15`. Worker 0 consumes stop bit 1, enters cleanup, and waits at
`10578d24` for handle `01000160`, mask `20`. The rear-display request carries
callback `1057d2c5`, which should post that completion; the generic wrapper
`103fab3a` should subsequently send manager phase 3.

The handler instead reaches `104493c8 -> 104819ac(5) -> 10481824(5)` with
panel-table cell `b1808508=0`. The halfword load at `10481864` accesses VA
`11e` and takes the first MAS data abort (`lr_abt=1048186c`, `DFAR_S=11e`,
`DFSR_S=5`) before command completion. Resource 24 (`HAIMENMONI`) should
populate the table through `102051b4 -> 10448c3a -> 1048178c`; current
descriptor17 is erased and its footer reads `ffffffff`. No ID-24 lookup is
observed in the focused 60-second window. A return probe at `10205226` reads
0 from the mask-5, timeout-5000 wait on handle `010000f2`; that failure skips
the resource lookup and installation. The [resource investigation](../runtime/resource.md)
captures raw RTOS return `ffce0000` and the direct `ENDM` comparison:
expected `454e444d`, actual `ffffffff`, comparator result -1 and validator
result 0. Loader phases 1, 2 and 3 all run; ID24/27 callback registration is
present, with publication masks still zero. This is an external-resource
publication boundary, not a missing battery report or an unconsumed stop event.

An explicitly qualified Z7 cross-model descriptor17 input makes the mask-5
wait succeed and installs the `HAIMENMONI` pointer `b7f54a00` through original
code. That experiment subsequently enters the ID34 translator with erased
dimensions and overruns its fixed array before worker startup. Its manager
remains state 0/current 5/target 5. It does not establish completion of the
state-3 handoff on the erased-input baseline, or provide Z7II factory data.

The subsequent [ID34 field trace](../runtime/id34.md) identifies an immediate
`+774` parameter value, 83 mapper scalar assignments and the active coefficient
matrix; later grid processing also needs bounded raw dimensions, ordered
rectangle coordinates and a second coefficient table. Fresh official
[Z6II/Z7 package comparison](../inputs/nor/id34.md) confirms identical Z6II
loader schema and the Z7 display-default bytes, while Z7's ID34 registry span
differs. No usable original ID34 was recovered. This is a static/input-source
refinement, with no new startup success or device-contract change.

The next producer follow-up confirms that `+774` is negated into parameter
`40000405`, while parameter `40000406` receives 6; this is not an EX command.
A fresh read-only stop at mapper entry observes those values `1/6` with erased
source `ffffffff`, the panel pointer installed, dimensions still erased and
manager `5/5/0`. Identified resource reloads read the same external NOR;
the ordinary settings-persistence table excludes ID17/34. The ROM survey's
296 geometry windows remain unqualified, and later phase-3 records still have
unvalidated bodies. This narrows input acquisition to a traceable descriptor17
capture without claiming another boot transition. Receipt:
`work/runs/id34producer-20261022/`.

Pending slot 3 and the later GUI gate remain zero, and no GUI first frame
is claimed. Receipts: `work/runs/batck-20261021/` and
`work/runs/handoff-20261021/` and `work/runs/descriptor-20261021/`.

### Earlier single-thread battery wait and the readiness producer

The [display producer investigation](../runtime/display.md) identifies the
paired producer: GUI mode entry `1053fe10` calls `104bd5f6(3)` at `10540470`,
then publishes event bit `80000000` at `1054047c`. Its later local condition
is `b181a6ec != 0`.

The single-thread original-bundle probe stops earlier. The five-worker manager
has collected all initialization notifications, started worker 0, and waits in
state 2 for a mode handoff; the GUI worker remains class 4. Worker 0 is inside
`104384c8`, waiting at `104386c4` on handle `010001c3`, mask `200`, with
battery-check status `b18651d8=0` and startup stage `b181a63a=1`. A probe
with one breakpoint at a time observes the wait entry and no return in 60 host
seconds. Battery publisher `103f8ec6` should store status 1/2 and broadcast
`042d0001`; registered callback `104be772` posts the awaited event. BatCk's
dispatcher only receives startup flags `10000` and `20000` in the focused
window, without the data/update flags needed to classify and publish a result.

This locates the software condition in that execution profile, not the
multi-thread baseline. The zero-valued later gate is not an observed rejection
in the unentered GUI producer. Receipts: `work/runs/startgate-20261021/`.

### F091 is a PTP input, not a demonstrated boot sender

The [F091/PTP investigation](../runtime/ptp.md) resolves the upstream chain:
PTPCMD validates the outer operation through `10463e8e` and the `119e1e74`
table, then validates its subcommand through `104c6b14`. On success,
`10463f88 -> 10478faa -> 104c6b9c -> 104c6a92` selects execution handler
`10502d0a`. F091 is a parameter subcommand of outer operations `94fd..94ff`.
The task and callbacks are installed, but the task waits at MAS VA `1046383a`
in state 0 on event mask `1`; no transport callback or command dispatch is
observed. Session word `b188bc6c` and F001-controlled mode word `b188bc80`
both read zero. Those are later gates, not measured rejection events.

A separate path at `1062f120` calls the display ready setter without F091. It
requires event bit `80000000` and pending slot 3 (`b201bf6c`). A focused window
observes one poll return with flags zero, slot 3 zero, and no high-bit/readiness
branch. Their producer and earlier battery-result wait are now resolved above.
The existence of the optional PTP handler does not establish that cold boot
should send it a command.

### Earlier baseline used for the F091 investigation

On 2026-10-21 the [VR investigation](../startup/vr-domain.md) was deferred for
the GUI objective. Its original resident-slot stop remains explicit. The
experimental SPI4 ready-bit peer changes pacing without activating the measured
display points; that does not qualify the complete VR protocol or rule out a
later dependency.

A fresh complete-bundle run on the rebuilt source graph, without a VR peer,
supplied resident spans, debugger writes or forced guest calls, records:

| Domain VA | Entry | Trace entries in 45 host seconds |
|---|---|---:|
| MAS `0x1020d41a` | app dispatcher, positive control | 3 |
| MAS `0x1020d51e` | app state-3 teardown branch | 1 |
| MAS `0x10370a14` | SPI transport, positive control | 1294 |
| EX `0x0802b6f0` | monitor service, positive control | 52 |
| MAS `0x104c0f20` / `0x104c6b14` | local callback broadcast / PTP subcommand router | 0 / 0 |
| MAS `0x10502d0a` / `0x10502cbc` | F091 handler / arming IPC | 0 / 0 |
| MAS `0x104d7e78` / `0x104dc804` | provider admission / display owner | 0 / 0 |
| MAS `0x10568f60` / `0x105a6906` | present wrapper / frame queue submit | 0 / 0 |
| MAS `0x1046bf0c` | display banks start | 0 |

The run used single-thread TCG, `-icount shift=6,sleep=off`, and narrow
`-d exec,nochain` filters. These are function-entry trace counts in a host-time
window, not virtual-time performance or a proof of unreachability. Receipt:
`work/runs/vrtriage-20261021/{arm.argv.txt,arm.exec.log,arm.stderr,summary.txt}`.
The same batch rechecked standalone original VR: its first uncovered access
remains RX VA `0xfff00167` reading `0xfffc8008`.

Read-only MAS IDB inspection confirms that the Thumb wrappers at
`0x107722e4`, `0x10772300` and `0x1077230c` pass `r1` as the request to router
`0x104c6b14`. The router reads the service code at request `+12`, validates the
request and performs a table lookup. F091's row at MAS VA `0x119e483c` contains
handler pointer `0x10502d0b`; the handler calls `0x10502cbc(1)` before provider
admission. Wrapper decoding and the table getter are retained in
`work/runs/vrtriage-20261021/static.json`.

That upstream dispatch is now identified in the PTP note above. In particular,
`104c0f20` iterates eleven local callbacks; it is not itself an external PTP
transmit function, so its zero count cannot establish that every host-facing
transport is idle. Neither absent immediate literals nor zero hits establish
that the firmware lacks every possible display producer. Earlier absolute
claims of an unreachable first frame or a model complete for this boot are
withdrawn.

## Earlier EX and display observations

The sections below retain their recorded windows and source analysis for the
supplied EX component (C1.70, container offset `0xd0`, length `0x80002`). Their
negative observations do not extend the current checkpoint into a proof that
other modes or longer runs cannot make progress.

A fixture PASS does not establish anything here; see
[`native.md`](native.md) for the synthetic contract level.  Physical-camera
behaviour stays outside this model.

## Reached by the unchanged firmware

All rows are observed on the bundle with the probe time base below.

| Stage | Evidence |
|---|---|
| EX cold boot, RTOS init, 27 cyclic objects created | breakpoints on `0x0804bf38`/`0x0804d388`; object pool `0x200224f0` |
| power monitors publishing ids 113/114/115 | **not reproduced** in the current 600 s window: dispatcher `0x08027e58` takes only `0x65`/`0x66` (round 12); the older `1113/1114/1115` receipt is not current |
| host commands 101/102 dispatched | same dispatcher, ids `0x65/0x66` in the current window; 103/104/131 come from earlier windows |
| EX record dispatcher `0x080332f4` handles the four boot records | `{type,handler}` table `0x0804e15c` |
| MAS record TX writes `0x2c0a0000`/`0x2c0a0100` | `0x1024325c` with `datareg=0x2c0a0000/0x2c0a0100`, 0x80-byte frames |
| **0x6b monitor read completes** | `sub_802ae48` 10x on `0x2001fdcc`, wire address `0xd6`; `sub_8046cd0` entered with `+61=0x22`, `+42=1` |
| MAS kernel idle and GUI app states 1..3 | dispatcher `0x1020d41a`, state-3 branch `0x1020d51e` |

## Not reachable in this image

Each gate is a state byte that the compressed SRAM template initialises to zero
and that no later code writes.  The three are independent, so none of them is
an artefact of the other two.

| Gate | Consumer | Verification |
|---|---|---|
| `0x2001fba6` | id-130 publisher start, `sub_8027d48`; also the detailed path of the report-input builder `sub_80278fc` | only write is the boot template's zero; selector always takes the `v = 0` branch |
| `0x20016058` (and the `0x20016040..` block) | EX report `+61`, MAS power classifier | four dwords watched for 900 s: one boot-template write, three never written; producer interface identified in "EX report-input producer" below |
| `0x200138da` | type-`0x12` record handler | written by the handler, never read |

## EX report-input producer (round 12)

The `0x20016040..` block has a concrete producer, so it is not "written by
nothing":

```text
host dispatcher 0x8027e58, case 'q' (id 113)
  -> sub_80278fc(1)                     build a 60-byte payload
     (payload from 0x2001faa4/faa8/faab/fabd/fabe/fac8/fb08/fbb8/fbb9;
      detailed path gated by 0x2001fba6)
  -> dispatch 0x02020045 to slot 0x2001f5fc
  -> 0x8025268: memcpy(0x20016040, payload, 60)   (0x8020552)
  -> 0x802551c(15)
```

Sibling handlers fill other report fields for the `0x02020041`/`0x02020042`
families (`0x8025120` writes `0x2001607c`/`0x200160b8..c4`, `0x8025184` writes
`0x200160c8..d2`).  The report builder `sub_8025544` then consumes the block
(`+0/+1/+2/+4/+8...`) and `+61` is guarded by block byte 24
(`0x20016058`).

Measured in a 600 s icount window: the host dispatcher only takes ids `0x65`
(101) and `0x66` (102) - 60+ alternating calls; ids `113`/`114`/`115` never
arrive, so `sub_80278fc` is never called, the dispatch never happens, and the
block keeps only the boot-template zero (`pc=0x80038be`).  The missing link is
the EX-internal power-monitor command 113, not a register or DMA writer.

### The BatCmCy monitor table and its publishers (round 13)

The table at `0x8051444` holds 20-byte cyclic-monitor entries
`{callback, period, name[8], slot}`:

| Entry | Callback | Publishes | Period | Slot |
|---|---|---:|---:|---:|
| BatCmCy1 | `0x8027d40` | `0x459` (1113) | 500 | `0x71` |
| BatCmCy2 | `0x8027d38` | `0x46f` (1135) | 250 | `0x87` |
| BatCmCy4 | `0x8027d30` | `0x45b` (1115) | 20 | `0x73` |
| BatCmCy5 | `0x8027d28` | `0x45a` (1114) | 5 | `0x72` |
| BatCmCy6 | `0x80268e4` | `0x468` (1128) | 500 | `0x80` |
| BatCmCy7 | `0x8029c78` | `0x45c` (1116) | 500 | `0x74` |
| ... | `0x8029c70`/`0x8029a7c`/... | `0x45e` (1118), ... | | |

Publisher API: `0x8028b70(id)` -> allocate `0x80268ec` -> queue post
`0x8041cac` (the queue handle is loaded at `0x8028b94`, separate from the
transport dispatcher `0x8027e58`).

Measured at shift 6: the `1114` and `1115` publishers fire repeatedly (23
publishes in 600 s), while `1113` and `1128` are zero-hit in 600 s **and** in a
900 s window.

The reason is not the period.  A dump of the RTOS cyclic object pool
(`0x200224f0`, 48-byte objects) taken at the 10th `1114` tick shows:

| Object | Descriptor | Callback | State | Period | Links | Expiry |
|---|---|---|---|---|---|---|
| idx0 / object 11 | `0x2001FC2C` | `0x8027D41` (1113) | **0** | 500 | none | `0x271` |
| idx4 / object 12 | `0x2001FC7C` | `0x8027D29` (1114) | **1** | 5 | linked | `0x510` |
| idx3 / object 13 | `0x2001FC68` | `0x8027D31` (1115) | **1** | 20 | linked | `0x515` |
| idx5 / object 14 | `0x2001FC90` | `0x80268E5` (1128) | **0** | 500 | none | `0x271` |

The descriptors map index -> object correctly (`0x2001FC2C` -> 11,
`0x2001FC68` -> 13, `0x2001FC7C` -> 12).  Started objects carry state 1, list
links and the dispatch code pointer `0x804AB5B`; the 1113 and 1128 objects have
state 0 and no links.  So they are **created but not started**, which is the
same shape as the id-130 object in `docs/runtime/ex.md`.

The power module has two wrappers around the start API:
`0x8029264(idx)` (ends in `0x804D388`, start) and `0x80292BC(idx)` (stop).
Both are called with index trios `(0,4,3)` from `0x8027D50..0x8027D94`, gated by
the state byte `[0x2001F598+37]`; the last transition for index 0 therefore
leaves it stopped while indices 3/4 remain started.  The `1113` -> host-113
translation is consequently still unobserved, and with it the report-input
builder.

### Which start site runs (round 15)

A 300 s probe with `lr` at the start wrapper shows that every observed start is
`idx=3` followed by `idx=4`, both with `lr=0x802A921`/`0x802A929`: the periodic
re-arm tail at `0x802A918` starts only slots 3 and 4.  The selector that also
starts slot 0 (`0x8027D48`, the host-131 route) has **zero hits** in the same
window, and `START-WRAP idx=0x0` has zero hits.  The host dispatcher carries
only `0x65`/`0x66` in the current windows, so the host-131 route that would
start slot 0 never runs.

So the 1113 absence is a *missing host-131/power transition*, not a period
effect and not a start-API failure: slots 3/4 are re-armed every cycle by
`0x802A918`, while slot 0 is started only by `0x8027D48`, which the current
windows never reach.

### The MAS host-command table does not contain 113 or 131 (round 16)

The MAS periodic host-command descriptor table at `0x10949704` (20-byte
entries `{timeout, flag, handler, cmd, period}`) lists
`0x63 0x65 0x66 0x67 0x68 0x69 0x6a 0x7b 0x7c 0x7d 0x8f 0x90 0x91 0x94`
with handlers `0x105b53xx..0x105b54xx`.  The two ids that matter here, `0x71`
(113) and `0x83` (131), are **not in the table** (no dword occurrence in the
surrounding `0x10949600..0x10949a00` window either).  So they are not periodic
MAS host commands; if they are ever sent, it is from an event path that has not
been identified.

That closes the "MAS sends 113/131 periodically" hypothesis: the EX waits for
event-driven commands that the current MAS image never emits on the observed
lane.

Consequences: the provider lifecycle and LCD start are downstream of the power
classification, which is downstream of a block this image never fills.  The
transport, the factory configuration and the monitor lane are all ruled out as
causes by direct measurement.

## MAS display owner (direct observation, 420 s icount shift 6)

The MAS side has its own, nearer gate, and it is not `eglSwapBuffers`.  The
display-owner callback `0x104dc804` is registered in the ROM method tables at
`0x1079deac..`, and its state dispatcher `0x104dc522` can reach the natural
panel start `sub_104db8ba` (state 26) and the mask-8 start at `0x104dc66c`
(state 6).  A 2-byte-aligned Thumb `BL` scan finds both call sites, correcting
the earlier "unreferenced" reading that came from 4-byte alignment.

What is idle is the owner itself: at the first app draw the owner context
`0xB1D0D0BC` has all five lane bytes zero, owner counter `+0x254` zero, and
mode `+0x1c9` one, while the app already draws `720x480` into
`0xbf309600` (the machine's `lcd-composition-base`).  The owner callback, the
dispatch site, the natural start and every `0x1046bf0c` call site have zero
hits in that window.  Frame publication therefore needs a display-service
request that arms the owner and admits a provider (`0x104d7e78`, manager-ready
`0xB1807FA8`, manager `0xB1807FA0`, events `0x42060005`/`0x42061005`), not an
EGL swap.  `docs/runtime/guichain.md` carries the full chain and the next
measurement (`probeDMSVC`); the power-classification link above stays a
correlation, not the proximate cause of the idle owner.

The same run narrows the MAS side one step further: the GUI mode block at
`0xB181D970` is never written (u32 `+0` and u16 `+2` watchpoints, zero hits),
the GUI event mailbox never sets ids 50 or 59, the GUI event dispatcher
`0x1053E19A` does not run, and the mode read `0x1062E9B4` is never called.
So the camera-state transition that should request a display mode never reaches
the MAS GUI.  What is still missing is a *producer* (an EX host command or
power-state transition), not display code or model behaviour; both the EX gates
above and this GUI mode request are on the same unexplained path.

The producer chain is now mapped in `docs/runtime/guichain.md`: GUI event
`0x39` -> `0x1054265A` -> view state `25` (`0x1053DF9E`) -> GUI step 1
(`0x105405A2`) -> mode request `3` -> event `0x10009` -> live view.  A 420 s
probe shows the GUI step dispatcher `0x105424E8` and every node of that chain
zero-hit, so the GUI application's step machine is registered in the ROM table
at `0x123B7978..0x123B79D0` but nothing drives it.  The runtime GUI worker
context at `0xB181D9B8` is fully populated and blocks on RTOS event
`0x010001BE`; at the same moment MAS core 1 sits in the scheduler idle loop
(`0x10107078`) while core 0 runs the app.  The missing signal for that worker
is the single upstream gate for the first frame.

Two further receipts bound the same edge:

- The embedded MAS inner image (`O[0x208853C,0x228CF3C)`, 2.1 MiB) **is copied**
  by the guest to `0xB1000000` (the RAM dump matches the payload head), but
  neither the vector base `0xB1000000` nor the reset handler `0xB1000060` is
  ever entered in 420 s.  It contains no GUI strings.
- The only poster of GUI event `0x39` (`0x10695450`) has **no reference** in
  any static form (Thumb/ARM `BL`/`BLX`, 32-bit literals, `TBB`/`TBH` tables),
  and the address appears in neither the RAM window `0xB1800000..0xB3000000`
  nor the inner-image dump.  The same holds for the mode dispatcher
  `0x10695750`.

If no runtime-computed producer exists outside those scans, this image has no
reachable producer for the live-view branch, which would make the first frame a
firmware-input boundary rather than a device-model gap.  Fabricating the event
would be a synthetic stimulus, not an unchanged-firmware receipt.

The overall shape is *registration without a driver*: app stages 1..3 and the
720x480 draw into `0xbf309600` run; the GUI worker (event `0x010001BE`), the
GUI event dispatcher, the only event-`0x39` poster, the mode request at
`0xB181D970`, the live-view gate, the display owner and the provider admission
are all idle; the SLA readout initializes but never completes; the inner image
is copied but never entered.  With the unchanged container and no external
stimulus the first GUI frame is outside the reachable set, and every remaining
input is of the "camera state / sensor readout" kind that the objective
forbids synthesizing.

## Probe time base

Original-firmware probes run with `-accel tcg,thread=single -icount shift=6`:
icount derives `QEMU_CLOCK_VIRTUAL` from executed instructions, so peripheral
byte timers advance with guest progress instead of wall clock, and `shift=6`
(64 ns per instruction) matches one instruction per cycle at the modelled
16 MHz HSI.  icount is incompatible with multi-threaded TCG.  Compare receipts
across time bases by guest progress, never by wall seconds.

## Re-running the 0x6b receipt

```sh
work/runs/ex-6b-ercd/check-6b.sh            # 900 s run, asserts the markers
work/runs/ex-6b-ercd/check-6b.sh --log NAME # assert an existing log only
```

Expected markers in the log:

```text
6BREAD hdr desc=0x2001fdcc r1=0xd6 ...
CD0 desc=0x2001fdcc st=0x22 cnt=0x1 f44=0xffff0000
```

The model dependency is the I2C address-phase stretch: a receive byte is
clocked only after the guest clears ADDR, and no path may deliver data while
`SR1.ADDR` is set.  Those two laws are guarded by
`tests/qtest/nikon/ex.c::i2c_single_byte_read`.

### The app renders, but no candidate panel surface holds pixels (round 17)

Coverage with `-dfilter`/`-d exec` over an unthrottled shift-6 window shows the
app draw `0x10569c76` executing 41 times in 300 s while every display gate stays
cold.  RAM dumps of `0xbf309600` (composition), `0xcb4a7000` (display buffer)
and `0xbec5f200` (resource-57 record 0) after 30 s and 90 s are **all zero**, so
the app's repeated draws do not land on a panel surface in this model.

The consequence for this file is the same one the GUI chain records: the
remaining link is not a missing pixel copy inside the model but a missing
request that never reaches the display owner, and that request is downstream of
the GUI worker's guest-kernel wait.

## MAS display frontier: the publication request has no producer

The MAS side of the first-frame chain is now measured end to end.  The runtime
detail lives in [`../runtime/guichain.md`](../runtime/guichain.md); this section
keeps the acceptance-relevant summary and the evidence boundary.

### What the application actually does (measured)

| Stage | Address | Executions |
|---|---|---|
| choose config | `0x1067dbda` | 12 |
| get config attrib | `0x1067e414` | 8 |
| create DMPX window surface | `0x106815a4` | 12 |
| make current | `0x10680426` | 19 |
| `eglSwapBuffers` | `0x10680b8c` | **0** |
| `eglCopyBuffers` | `0x10680cfe` | **0** |
| `eglDrawFrameModeDMP` | `0x106810be` | **0** |
| `eglDestroySurface` | `0x1067f2a8` | 0 |
| `eglGetProcAddress` | `0x10680f60` | 0 |

The per-frame surface creation path `0x10569c76 -> 0x10568bde` runs (8 times in
120 s) and `eglMakeCurrent` follows, so the app creates and binds a surface per
frame.  The present wrapper `0x10568f60` - the only caller of `eglSwapBuffers`
in the image - has no `BL`, no pointer literal and no computed reference, so
frames are created and abandoned.  This is the direct answer to "why the app
never calls `eglSwapBuffers`/`eglCopyBuffers`/`eglDrawFrameModeDMP`".

### The transfer stage exists and is configured but never given a job

The display engine window at `0x20180000` is initialized by the guest
(`0x106EDFD6` once, config `0x106EE04A` 3 times) and its setup latch is written
with exactly the model's documented values (`0x20181214 <- 0x0100ffff`, then
`0xffff`, from `0x106EE3C8`).  No stream descriptor (`0x10d0..0x10ec`) and no
`0x01000000` kick is ever written, so **IRQ129 cannot fire** in this boot and
the "swap -> transfer stream -> IRQ129 -> display buffer" receipt has no
producer.

### The display service arms behind a guest-kernel reply

`0x10637B54` is the only caller of provider admission `0x104D7E78`; it is called
from the display-service start `0x1062EDDC`, right after the ready-setter thunk
`0x10637B72` (tail branch to `0x104D7F16`).  A full register trace of the
executed path shows the guard is reached with `r4 == 0` and exits before that
block, so ready, admission, the provider mailbox, the owner and the bank start
are all skipped in this boot.

`r4` is the reply of a microkernel query (`sub_106340D4` -> `sub_10361116`,
`SVC 6`) for the display context object whose handle is `0x010001C2`.  The guest
kernel is ordinary guest code inside the MAS image - VBAR `0x10100000`, SVC
vector `0x10100008` executing 417421 times in 45 s, `SVC 6` dispatching through
`0x10103594` to `0x10107624` - so the `0` reply is this firmware's own state,
not a modelling artefact.

### Withdrawn: "the display mode never leaves 2"

This section was a mis-attribution and is withdrawn.  The object whose `+0x1C`
byte is written to `2` belongs to the `CARD0` (SD-card) subsystem, resolved from
the named tables: table A at `0x118AED0C` is keyed by `eSyscCommandId`
(`CardInsert 20`, `CardRemove 21`, `CardMount*` 22..25, `CardFormat*` 26..29,
`StartTask1st 4`, ...), and table B at `0x118AEC40` maps subsystems to
descriptors whose names are `CARD0`, `ML_LENS`, `F_LENS`, `EXTSB`, `NwBoard`.
`0x1042CB82` is the card handler, not a display one.

The display side keeps the frontier documented above (owner `0x104DC804`,
admission `0x104D7E78`, banks start `0x1046BF0C`, kernel gives
`0x010001C2`/`0x010001BE`); the display-mode statements below are retracted.

### Historical (withdrawn) display-mode section

The display state machine `0x1042D086` advances from state 1 to state 2 - the
only state that posts the attach event - only when `mode(obj+28) == 1`.  Both
display objects (`0xB209862C`, `0xB185DE6C`) enter with `new_state = 1` and mode
`2`, and the two mode writers measured in the window are:

| Setter | Behaviour | Runs |
|---|---|---|
| `0x1042D72E` | `0x1042D1DA(obj, 2)` hardcoded | 2 |
| `0x1042CCA0(obj, a2, a3)` | `a2 == 0 ? 2 : 1` | **0** |

So the firmware writes only the idle mode, and the selector that could write
mode 1 is never called.  The display request handler `0x1042E7FA` is likewise
called once with `a3 = 1` (disable) and `a2 = 0`, which its `if (a2)` makes a
no-op; request type 2 (attach) is never issued.

### The missing dependency is two specific kernel gives (measured)

The RTOS kernel call table resolves and is active: `create 0x10110D34`,
`give 0x101110EC`, `clear 0x1011131C`, `wait 0x10111410`, with 173965 gives and
33276 waits in 45 s.  Capturing the give calls themselves (87293 calls, 83
distinct objects, 25 s) shows which objects this boot signals, and the two the
chain needs are absent:

| Object | Role | Gives |
|---|---|---|
| `0x010001BE` | GUI worker wait object | **0** |
| `0x010001C2` | display-service gate object | **0** |
| `0x010001C1` / `0x010001C3` / `0x010001C4` | neighbouring display objects | 3 / 1 / 68 |

A wait on a never-given object returns no flags, and no flags is exactly the
`r4 == 0` that keeps the display-service arming block skipped.  So the frontier
is a pair of missing kernel gives inside the unchanged firmware, not a stalled
kernel, a dead device or missing bundle data.

### The chain in order, with each link's measured state

| # | Link | State | Evidence |
|---|---|---|---|
| 1 | AppC message class delivered to `0x1020CA7A` | only `0x03000007` observed | `-d cpu` at the TBB, `r0 = 7` |
| 2 | class that reaches the state test (`0x1020CD0A`) | never selected | coverage 0 for `0x1020CCA0`, `0x1020CCC2`, `0x1020CCDC`, `0x1020CD0A` |
| 3 | code translator `0x1020E98A` (`0x04100001 -> 0x03000004`) | never runs | coverage 0; only ref is a `B.W` thunk at `0x10204D4C` |
| 4 | event-0x39 poster `0x10695450` | reachable, never posted | veneer `0x103608AA -> 0x1048DB00 -> 0x10695450`, 10 callers, none reaching it |
| 5 | GUI event dispatcher / live-view gate | never runs | coverage 0 |
| 6 | display arming request | only start event `0x04090017` -> `0x04030034`; needs `0x0403001A` | two-level broadcast capture + `cmp r5,#0x0403001A` guard |
| 7 | device-subsystem dispatcher `0x1042CB82` (`CARD0`) | runs, gets `op = 4` | 3 entries; `20..29` switch takes default; not a display handler |
| 8 | display event `(0|1, 0)` (arming) | never posted | only `(2,5) (5,5) (6,5)` observed |
| 9 | kernel give of `0x010001C2` / `0x010001BE` | never happens | 87293 give calls, 83 objects, both absent |
| 10 | ready `0x104D7F16` / admission `0x104D7E78` | never runs | coverage 0 (gated on the never-given object) |
| 11 | EGL present `0x10568F60` | unreachable | no `BL`/`B`/literal reference under both scanners |
| 12 | transfer job / IRQ129 | never issued | no `0xC00..` register writes, no stream kick |

Every link is measured cold, and the gates are all *upstream* of anything a
device model could supply: message classes the boot does not deliver, a service
request (`0x0403001A`) that nothing issues, and two kernel objects nothing
signals.

### Where this meets the EX host lane

The display arming chain now ends at a *service request* rather than at a device:
F091-class request -> handler `0x10502D0A` -> IPC `0x040F0020` -> display service
-> request `0x0403001A` -> ready `0x104D7F16` + admission `0x104D7E78`.  The
handler never runs, and the display service's only broadcast in a 60 s window is
its start event `0x04090017` (delivered as `0x04030034`), never `0x040F0020`.

That is the same shape as the EX side of this file: the EX host-command table at
`0x10949704` carries neither 113 nor 131, and the EX lanes observed here carry
only `0x65`/`0x66`.  Both halves therefore wait on an inbound service request
that this boot does not deliver, which is the next measurement named in
[`../runtime/guichain.md`](../runtime/guichain.md) - find the producer of the
F091-class request rather than any further device behaviour.

### The host-facing service bus is idle in both directions

The class-2 envelope path (`class table 0x119E1E84.. -> router 0x104C6B14 ->
handler table 0x119E4820`) is dormant in this boot:

| Address | Role | Executions |
|---|---|---|
| `0x104C0F20` | request send API (51 static call sites) | **0** / 45 s |
| `0x104C6B14` | request router | **0** / 120 s |
| `0x10502D0A` | F091 handler (display arming sender) | 0 |
| `0x10502DB8` / `0x10502E12` | F092 / F093 handlers | 0 / 0 |

Local display events do flow, but through in-process callback tables
(`0x1052E3AE` with 4 subscribers, `0x107392D6` with 16), not through this bus.
So MAS sends and receives no host-facing service request at all, and the display
arming request `0x040F0020` (routed to display code `0x0403001A`) is never
issued by anything.

### Observability added: a panel scanout model (step 1 of the assisted path)

The model now includes `nikon-panel-functional` (`hw/nikon/devices/panel.c`),
which samples the guest-owned composition region `0xbf309600` (1024x688
RGBA8888, six inactive top rows) on a virtual-clock cadence and publishes a QEMU
console surface.  It reads guest RAM only - no register, IRQ, completion or
status is added - so it changes nothing the guest can observe.

Receipts: `tests/unit/nikon/panel.c` (2 TAP tests, the conversion is a pure byte
reorder) and a live `-qmp` + `screendump` run that yields a 1024x682 P6 PPM.

The first scanned frame is entirely black, which is the same negative every RAM
dump showed: the unchanged firmware writes no pixel into the composition buffer
in this boot.  The observer is therefore a prerequisite for the assisted path in
[`../runtime/guichain.md`](../runtime/guichain.md), not evidence of a frame yet.

### Assisted-path status (measured)

- Panel scanout observer: added, unit-tested, and a QMP `screendump` yields a
  1024x682 PPM.
- Camera image input profile: documented in
  [`../inputs/camera/image.md`](../inputs/camera/image.md); with an image
  configured the readout-pipeline counters stay 0 because the guest never issues
  the readout request.
- EX key assistance (F1..F5, 1..3 in four rounds through QMP): the app keeps
  drawing (27 times), but present, GUI dispatcher, owner, banks start, mailbox
  post, frame queue, service router, F091 handler and the SLA readout ISR all
  stay at 0, and the scanned frame stays black.

So the observer and the image input are in place, and the remaining assistance
must be a host command injected on the MAS host-command lane, which requires the
frame protocol that has not been recovered here.

### Why the natural (unassisted) path produces no frame: the two-sided answer

Both sides are now measured with their own receipts, and they meet in the middle:

**MAS side.** The application performs EGL setup and abandons each frame's
surface (create 8, present 0, release 0).  The only present wrapper
(`0x10568F60`) has no `BL`, `B`/`B.W` or pointer reference.  The frame-queue
submit `0x105A6906` never runs, the transfer engine is configured but never
given a job, and the display owner, provider admission and banks start are all
zero-hit.  Arming needs display request `0x0403001A`, which needs IPC
`0x040F0020` from the F091 handler `0x10502D0A`, and the host-facing service bus
is idle in both directions (send `0x104C0F20` 0 in 45 s, router `0x104C6B14` 0
in 120 s).

**EX side.** The EX receives external input: the seventeen host keys drive its
GPIO pads and `hw/nikon/devices/input.c` logs every transition
(`nikon-input: pad=0 ex-pad=0x0a down active-low level=0`, ...).  It still emits
only host commands `0x65`/`0x66`, and its host-command table `0x10949704`
carries neither 113 nor 131.

**The meeting point.** MAS's display arming is downstream of an inbound service
request that only a host command from the EX (or an internal MAS module) would
produce, and neither producer exists in this boot.  The EX is not stuck on a
missing device input either - it is driven - it simply does not convert that
input into the commands MAS waits for.

So the natural-path answer is not "the model lacks a device": the model now
observes the panel, feeds an image, and delivers host keys.  It is that the two
firmware domains in this boot never exchange the request that would start the
display.

### The SLA readout cohort stops after init as well (measured, 60 s)

The same window over the SLA result-readout cohort shows only the initialisation
running:

| Address | Role | Executions |
|---|---|---|
| `0x1009AD52` | readout initialization | 1 |
| `0x10093CEC` | mode2 dispatcher | 1 |
| `0x1009AD7E` | paired request submission | **0** |
| `0x1009AE04` | paired request stop | 0 |
| `0x1009AE6C` | full configuration writes | 0 |
| `0x1009AF8C` | three fullword setters | 0 |
| `0x1009ACA0` | readout ISR | 0 |

So the SLA initialises the interface and never submits a request either.  That
closes the last candidate for a model-side fix in the image path: the SLA
readout-control `readout-request` line is unconnected in this machine, but
connecting it would change nothing because the guest never raises it.

### The graphics window is the SMAPH-F resource-manager block (identified)

The MAS image carries the DMP/OpenVG driver (`VG_DMP_*`, `VG_DMPX_*`, `SMAPH-F`,
`rm_gpuif_wait_interrupt`, `int_cause & RM_HWIF_INT_GPU`, `BGraphicService`).
Its register accessors fix the aperture:

```text
sub_106EE430(a1)  return *(u32 *)(a1 + 0x20180000)      // read
sub_106EE3C4(a1, v)  *(u32 *)(a1 + 0x20180000) = v      // write
sub_106EE1F8(cause, ctx)  rm_gpuif_wait_interrupt       // 0x58 cause word
```

so the "transfer/display engine window" this model already owns at `0x20180000`
is the GPU's register file, not a separate DMA block. The command word format
the model already fingerprints for the LUT draw

```text
0x01408004  op = 4, reg = 0x0140, mask = bit15, count = 1
0x01a3f004  op = 4, reg = 0x01a3, mask = 0xf,   count = 1, r[0x1a3] = w|h<<16
0x00a6f004  op = 4, reg = 0x00a6, mask = 0xf,   count = 1
```

is exactly the packed command format a published Z7 model documents for the
same IP (`op = header & 7`, `count = ((header >> 3) & 255) + 1`, per-byte write
mask in bits 12..15, base register index in bits 16..23, two 512-word register
banks). Two independent derivations agree, so the model's existing LUT profile
is a narrow slice of one command family, and the *fill/stroke/textured* families
are unimplemented rather than absent from the hardware.

### Still no pixels after 120 s (measured)

`pmemsave` of the kernel, graphics and pixels RAM regions at 120 s unthrottled
shift 6 (74,448,896 + 14,680,064 + 17,825,792 bytes), scanned for image-like
pages (a page counts when more than 200 of its 1024 words are non-zero):

| Region | Pages | Largest structured cluster |
|---|---|---|
| `0xb1800000` kernel | 536 | `0xb18a2000`, `0xb2b75000`, `0xb21dd000` - 1024 distinct words per page, i.e. random-state buffers, not pixels |
| `0xbb300000` graphics | 4 | `0xbc010000..0xbc013000` - object records (float `1.0`, small ints, pointers) |
| `0xbc100000` pixels | 1 | `0xbd160000` - 315 distinct words, too sparse for a surface |

Nothing in the dump has the row-structured colour field a rendered UI would
leave. The measurement matches the mechanism: no GPU command stream is
submitted, so no renderer - modelled or missing - has anything to execute.

### Renderer hypothesis: no observed command producer

A runtime sweep points the panel observer at every surface the firmware could be
drawing into, through QMP `qom-set` on `/machine/panel` (base, width, height,
skip-rows), then takes a `screendump` of each.  At 20 s all of them are black:

| Surface | Geometry | Pixels |
|---|---|---|
| `0xbf309600` composition | 720x480 and 1024x688 | all black |
| `0xcb4a7000` display buffer 0/1 | 1280x720 | all black |
| `0xbec5f200` resource-57 record 0 | 1024x688 | all black |
| `0xbf10f200` record 1 | 960x540 | all black |
| `0xb81849c0` first app draw target | 512x512, 480x720, 720x480 | all black |

and the app draw target's neighbours are not empty either: the frame-slot table
`0xB1CE1BCC` has eight live entries with ~2 KiB descriptors, so what the app
allocates is bookkeeping, not a rendered image.

The render blocks are not being driven: GDB watchpoints on the IPUD window
`0x24020000` (`+0x04`, `+0x20`) and the processing window `0x24070000` (`+0x04`)
record **no writes** in 45 s.

The reference project's own scope list is the confirmation: it implements
"selected GPU paths, strokes, bitmap sampling, alpha and LCD composition" and
leaves "complete GPU/OpenVG implementation" outside its verified scope.  This
model has the transport, transfer, fill, copy, lanes and banks blocks, but no
renderer that turns the firmware's UI draw calls into pixels.

A broader functional render engine would still need actual guest commands.
These black surfaces and idle windows do not establish that implementing one
alone would unblock the boot. The upstream startup/command producer remains
the investigation target; the panel observer can expose any resulting pixels.

### Evidence boundary

- Not observed in these windows: EGL present, frame queueing, a transfer job,
  IRQ129, the display owner, provider admission and the bank start.
- The recorded missing guest-kernel reply (handle `0x010001C2`) and cold present
  path have not been traced to a missing device contract that would unlock them.
- Fabricating either (injecting a swap, a frame slot, ready/admission, a stream
  kick or a kernel reply) would be a synthetic stimulus, not an
  unchanged-firmware receipt, so it is excluded here.

## Conclusion: no frame producer observed in the recorded windows

The recorded first-frame chain is cold upstream of display admission. The
current task is to trace the independent firmware startup conditions described
in the PTP note, rather than infer from an idle PTP handler that no producer
can exist.

### The answer to the original question

In these windows the application does not call `eglSwapBuffers`, `eglCopyBuffers`
or `eglDrawFrameModeDMP`. Static scanning found that the only present wrapper
(`0x10568F60`, whose body is the sole call site of `eglSwapBuffers`
`0x10680B8C`) has no reference of any kind - no `BL`, no `B`/`B.W`, no pointer literal, in the ROM,
the RAM window or the inner image.  The app performs EGL setup
(`eglChooseConfig` 12, `eglGetConfigAttrib` 8, `eglCreateWindowSurfacePointerDMPX`
12, `eglMakeCurrent` 19) and then abandons each frame's surface: the frame-slot
table at `0xB1CE1BCC` records create (8) but **present 0** and **release 0**.

### Why no alternative path publishes

| Alternative | State |
|---|---|
| frame queue submit `0x105A6906` | 0 executions |
| transfer descriptor / stream / kick | never written |
| IRQ129 | unreachable without a job |
| display owner `0x104DC804` | 0 executions |
| provider admission `0x104D7E78` | 0 executions |
| banks start `0x1046BF0C` | 0 executions |
| kernel give `0x010001C2`, `0x010001BE` | absent from 87293 give calls |

### The PTP arming route is idle in these windows

The PTP route sends display-service request `0x0403001A` through outer event
`0x040F0020`, broadcast by `0x10502CBC` from F091 handler `0x10502D0A`. No F091
request was observed in those windows: `0x104C0F20` local broadcast and
`0x104C6B14` PTP router both have zero entries. The previous
claim that no bundle component contains `0xF091` was incorrect: the MAS handler
table contains it at `0x119e483c`. Finding no immediate-code producer does not
exclude a table-driven or computed request. The upstream dispatch is now traced
to PTPCMD, whose earlier event wait remains unsatisfied. The separate
`1062f120` readiness call means this PTP route is not the only software path.

### What this means for the objective

No original-firmware GUI frame is established. Continue tracing the actual
startup producer and the conditions that gate it. A missing request is an
observation to explain, not evidence that the model is complete or that only a
different image could progress. Any future device change needs a finite guest
contract and native validation; directly injecting a guest request, event or
present call would remain a separate hypothesis experiment.
