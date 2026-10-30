# Battery and external-input startup profiles

These observations qualify a specific input and source revision. They do not describe a new execution or replace the [current startup boundary](frontier.md). Earlier negative observations retain their original scope; later corrections are stated beside the affected interpretation.

## Complete finite battery protocol checkpoint {#complete-finite-battery-protocol-checkpoint}

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

## Healthy battery plus held-input checkpoint {#healthy-battery-plus-held-input-checkpoint}

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

Parent-owned receipts are in `run displayhealthy/`: the20-second
baseline, targeted policy and notification traces, `healthyhistory` /
`warninghistory` comparison, and final read-only
snapshots. The released-F4 healthy comparison selects main mode5 rather than
held-F4 mode1, but still leaves worker0 active and GUI idle. Numeric main mode
alone does not establish a GUI request.

## Earlier held-input checkpoint without the battery peer {#earlier-held-input-checkpoint-without-the-battery-peer}

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

`run timingpreset/frontier.json` retains the joined input,
initializer, callback and fill-register capture; `frontier-qemu.stderr`
records the historical rejection. The new
`run reproducible/mainline.json` joins the accepted fill and
25-host-second continuation without a new uncovered transaction. GUI gate
and pending3 remain0, manager remains
current3/target5/mode1/state2. Final scanout and a GUI first frame remain
unverified. Earlier `run policy/coldkey*` and `coldrecord*`
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

## Functional-input checkpoint: warning composited; worker0 policy/event loop {#functional-input-checkpoint-warning-composited-worker0-policy-event-loop}

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
`run descriptorfunctional/`. Follow-up native/execution receipts
are `run streammodel/`, complete next-pair capture/decoding is
`run streamnext/`, and its current indirect operands/caller are
`run streamboundary/`. Current red/green, stream/IRQ/fault and
next-pair decoding receipts are in `run singleclear/`; the next
rectangle's read-only entry capture is `run singleboundary/`.
The latter captures its operands before an asynchronous debug stop on EX; it
does not supply another MAS frontier receipt. An earlier singleclear probe
tried to read just below record7's mapped slot and failed in the debugger;
`exploratory-*` is excluded from the completion-to-fault claim. Original
firmware and the constructed input remain unchanged. Factory-data recovery is
optional for continuing this functional route.

Current native and accepted gray-completion/next-abort receipts, full next-pair
bytes and command decoding are `run graycontext/`. The first
green attempt exposed a signed-extension bug in the test's comparison only;
`fixturecomparison.log` records it, and the corrected `green.log` passes.
The next131-path batch's separate read-only entry capture, all131 indirect
arrays and transforms are in `run graypaths/entry.json`, with
caller LR=`106eeab9` and command bytes matching the main fault capture.
Coordinate counts range10..128 and segment counts6..37; observed segment
codes are00/02/04/0a. No additional external data was needed to capture them.

Current implementation/native receipts, final original completion/next fault,
full framebuffer and preview recipe are `run pathmodel/`.
The earlier successful pre-alias-check execution is retained as `prealias-*`;
the unprefixed records use the final checked build. The passive five-path entry
capture and all its indirect arrays are `run pathboundary/`.
The buffer preview is not a scanout receipt: it reverses captured rows using
the fixed Z7 presentation convention and does not establish Z7II physical
orientation. Final display publication and GUI gate/pending3 remain unproven.

Current five-path native/completion/frame/next-fault receipts are in
`run coloredpaths/`; the next inline packet and a bounded raw
image-source window are retained in `run textureboundary/`.
The latter is only an entry capture, not processing acceptance. A first-report
MAS capture also observes unknown battery typeff and mainfault1, with software
override disabled; the normalizer produces fault1 before BatCk becomes2.
See [battery classification](../runtime/compatibility.md) for the upstream EX
conditions and the distinction between report arrival and usable power.
The separate original EX probe in `run batteryhandshake/`
observes three21-byte initialization requests expecting29-byte replies, each
returning transport4, followed by error8 on structure`2001fa50`. This narrows
the battery issue to a concrete transport exchange. The subsequent work below
identifies the event-bit2 producer and consumed reply fields; no battery-success
data is injected.
The current A8 implementation/native/frame/next-abort records are in
`run texturemodel/`; passive next-pair submission and alarm
captures are in `run imagetransfer/`. The latter directly
observes EX`08023768`, LR`0804ab53`, TX count21/21 and RX count0/29, with
driver record selecting USART1 at EX PA`40011000`. Static source registration
names that callback's timer`BatCmAl2`. It signals event4, producing transport4.
The present machine connects USART1 to`serial_hd(0)` and these runs use
`-serial none`; no external battery peer supplies receive bytes. A valid peer
also needs the random-dependent29-byte challenge response described in
[battery compatibility](../runtime/compatibility.md); framing/XOR alone is
insufficient. No supported peer key source is established in current evidence.

YUV105 model/native/completion/source/plane/next-abort receipts are
`run yuvmodel/`; the next107 submission/full source is in
`run yuvnext/`. Current107 native/plane/next-abort and complete
176/1552-byte command decoding are in`run yuva107/`.
Current SRC_OVER native/frame/completion and joined manager/worker0 event
receipts are in`run srcover/`. The host-only
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
`run seednative/` and `run batteryaccept/`.

## Erased-input baseline: rear-panel table fault during worker exit {#erased-input-baseline-rear-panel-table-fault-during-worker-exit}

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
`run id34producer/`.

Pending slot 3 and the later GUI gate remain zero, and no GUI first frame
is claimed. Receipts: `run batck/` and
`run handoff/` and `run descriptor/`.

### Earlier single-thread battery wait and the readiness producer {#earlier-single-thread-battery-wait-and-the-readiness-producer}

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
in the unentered GUI producer. Receipts: `run startgate/`.

### F091 is a PTP input, not a demonstrated boot sender {#f091-is-a-ptp-input-not-a-demonstrated-boot-sender}

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

### Earlier baseline used for the F091 investigation {#earlier-baseline-used-for-the-f091-investigation}

the [VR investigation](../startup/vrdomain.md) was deferred for
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
`run vrtriage/{arm.argv.txt,arm.exec.log,arm.stderr,summary.txt}`.
The same batch rechecked standalone original VR: its first uncovered access
remains RX VA `0xfff00167` reading `0xfffc8008`.

Read-only MAS IDB inspection confirms that the Thumb wrappers at
`0x107722e4`, `0x10772300` and `0x1077230c` pass `r1` as the request to router
`0x104c6b14`. The router reads the service code at request `+12`, validates the
request and performs a table lookup. F091's row at MAS VA `0x119e483c` contains
handler pointer `0x10502d0b`; the handler calls `0x10502cbc(1)` before provider
admission. Wrapper decoding and the table getter are retained in
`run vrtriage/static.json`.

That upstream dispatch is now identified in the PTP note above. In particular,
`104c0f20` iterates eleven local callbacks; it is not itself an external PTP
transmit function, so its zero count cannot establish that every host-facing
transport is idle. Neither absent immediate literals nor zero hits establish
that the firmware lacks every possible display producer. Earlier absolute
claims of an unreachable first frame or a model complete for this boot are
withdrawn.
