# Native GUI integration

Parallel graphics validation extends the[cached matrix contract](../devices/transfer/overwrite.md)
to initialized105/107 clears and converters:232 new variants, with the full
transfer batch passing23 subtests in1.95s after the old-model red. Retained
active matrices remain checked. The[panel observer correction](../devices/panel/protocol.md)
reproduces the missed internal pixel update, then passes3 subtests in0.51s
for whole-frame updates and console/timer lifecycle. Its intermediate fixture
API assertion and corrected final run are recorded separately. The final
unchanged integration, with all three changes, repeats the0→8 clear and147
IRQ129 entries before timing PA2c011538=1; GUI readiness remains0. Native
receipts are in `clearmode-20261023`, final original receipts in
`run graphicsstate-20261023/`. Preemptive raw matrix combinations have
native coverage; the observer fixture does not claim guest progress.

The[clear temporary-mode model](../devices/transfer/clearmode.md) passes all22
transfer subtests in1.67s after a native temporary0 red. Its clear fixture
has40 variants,12 new: both105/1070/8 cases, full planes/guards, state/header/
early-draw rejection, immutable capture and temporary0 cancellation/map-loss
boundaries. Separate normal original execution completes the0→8 clear and
147 IRQ129 entries through later graphics, then encounters timing channel4
extra-feature enable at PA2c011538. Its last176/1552 graphics job completed;
GUI readiness remains0. Receipts: `run clearmode-20261023/`.

The[initialized SRC_OVER target model](../devices/transfer/target.md) passes
all22 transfer subtests in1.12s after a native record0 red. Reused tests cover
49 variants at each of five targets, including0 and an exact4GiB endpoint;
the four new targets contribute196 variants. They compare independent full
pixels/tails/guards, retained/replaced/foreign/failed context, whole-output
range and alias boundaries, immutable capture and deferred cancellation.
Separate normal original execution completes the record0 rectangle and two
subsequent jobs, then rejects224/1648 YUVA107 at temporary mode0 before final8.
A separately scoped debugger-paused original run verifies every active pixel
at IRQ129 and an unchanged slot tail. Application01/03, primary0 and GUI
readiness0 remain final snapshots, not first-frame evidence.
Receipts: `run singletarget-20261023/`.

The [retained image prefix model](../devices/transfer/prefix.md) passes all21
transfer subtests in1.38s after a native216/344 red. Its77 variants cover both
independent prefixes and all prior modes, unused raw matrix values, whole
output/guards, context/active-state/command boundaries, prefix-source overlap,
immutable capture and cancellation. An initial original integration exposes
a shared-header LUT dispatch collision; its new native guard is red, then
passes with bounded image dispatch. The final original run completes ten216/344
images and69 IRQ129 entries from the first one through later work, then rejects
176/368 at the single-clear target restriction. Application01/03, primary0 and
GUI readiness0 are final snapshots. Receipts: `run imageprefix-20261023/`.

The [temporary conversion mode correction](../devices/transfer/mode.md) passes
all20 transfer subtests in1.38s after the new scaled mode2 case fails against
the old model. The three conversion cases have29/30/32 variants,23 new,
covering0/2/4/6 before exact final2, odd/high/solid bits, headers/early trigger,
immutable capture, independent full-plane color/sampler references, mapping
loss and cancellation. Separate original execution completes nine newly
accepted2→2 conversions and84 IRQ129 entries from the first one through later
work, then rejects216/344 A8 with extra cached-state prefixes. GUI readiness
remains0. Receipts: `run convertpaint-20261023/`.

The [paint-state correction](../devices/transfer/paint.md) passes all20 transfer
subtests in0.81s after two actual native reds: optional01848004=0 grammar, then
clear-to-path context retention. The mixed case now has51 variants, including
per-fill disable writes through256 operations, malformed/late state, initial/
retained clears, foreign/reset/failed contexts and deferred cancellation with
whole output/guards. Separate original execution completes three840/1048
packets and26 IRQ129 entries from the first one through later graphics work,
then rejects224/1736 YUVA107 at temporary mode2 instead of the profile's0.
GUI readiness remains0. Receipts: `run paintmask-20261023/`.

The [matrix overwrite correction](../devices/transfer/overwrite.md) passes all20
transfer subtests in0.70s after its new valid case fails against the old model.
Its113 variants cover both rectangle and initialized-image overwritten matrices,
all nine raw payload slots, active/header/latch boundaries, variable counts,
continuation, immutable capture, full output/guards and cancellation. Separate
unchanged original execution completes a late1280/1560 six-clear packet through
IRQ129 with zero physical output in all six areas, then rejects840/1048 mixed
paths with an extra `01848004=0` state write. GUI readiness remains0.
Receipts: `run matrixoverwrite-20261023/`.

The [mixed fill/stroke model](../devices/transfer/stroke.md) passes all19
transfer subtests in1.23s after a valid mixed packet fails against the old
parser. Its36 packet variants cover counts through256 operations, wrapped
keys, ordered full pixels/guards, nonuniform scale, zero width, context,
field/data/alias boundaries, backed4GiB crossing and deferred cancellation.
A separate mask case covers all nine cap/join combinations, open contours,
analytic butt/square bounds, round caps, corner styles and miter truncation.
Independent review's cap/join coverage concern is resolved. Separate original
MTTCG runs complete mixed280/352 and1680/2072 packets and later graphics work,
then reject a new856/1088-byte rectangle matrix state. Exact840/1040 acceptance
is synthetic; post-fix original batch boundaries differ. GUI readiness remains
zero. Receipts: `run guipaths-20261023/`.

The [image context extension](../devices/transfer/image.md) passes all17
transfer unit cases in0.99s after native reds for full initialization and
retained temporary mode14. Its40 variants cover the actual geometry/sampler,
all four temporary modes, validated target selection including0, retained
matching/foreign/failed/reset context, fixed-record7 path rejection, whole
pixels/guards, strict initialization/body fields, ranges/aliases, backed4GiB
crossing, deferred inputs, immutable commands and cancellation. Independent
read-only review finds no concrete defect. Separate original execution completes
224/472 plus seven176/336 pairs through original IRQ129; the first isolated
image changes578 pixels and matches a whole-target independent reference.
At that image-only checkpoint, the next840/1040-byte pair rejects with
`transfer-path-batch-count-not-supported`. That snapshot's GUI readiness remains
zero. Receipts: `run guiimage-20261023/`.

The [RGBA SRC_OVER extension](../devices/transfer/over.md) passes all16 transfer
unit cases in1.21s after the new valid submission fails against the old model.
Its35 variant types include64 source/destination alpha pairs, independent full
pixel/guard comparisons, partial/fractional/clipped geometry, ordered context
continuation, alias boundaries, immutable capture, late indirect changes and
reset/unrealize/map-loss cancellation. Existing path/A8 image cases also cover
nonopaque targets. Independent read-only review found no concrete defect.
Separate unchanged execution crosses five more graphics pairs through original
IRQ129 and, at that RGBA-only checkpoint, next rejects224/472 bytes with
`transfer-rectangle-count-not-supported`. That snapshot's GUI readiness remains
zero. Receipts: `run rgbaover-20261023/`.

The [rectangle context continuation](../devices/transfer/continuation.md)
passes the complete 15-case `nikon_transfer_unit` batch after the new valid
continuation fails against the old model. Its 31 variants cover cross-batch
key/wrap, chained odd batches, exact ordered pixels/guards, counts and padding,
explicit target changes, invalid/missing/foreign contexts, alias boundaries,
immutable capture and reset/unrealize/map-loss cancellation. Independent review
found no concrete defect. Separate unchanged execution receives original IRQ129
after 2120/2368 START and, at that continuation-only checkpoint, next rejects a
176/368-byte partial-alpha SRC_OVER program. That snapshot's GUI readiness is
zero; no GUI first frame is established.
Receipts are in `run rectanglecontinuation-20261023/`.

The [MAS RAM backing fix](../startup/ram.md) passes all 43 applicable cases:
memory qtest 26, memory unit 3 and transfer unit 14. Its new real-TCG regression
fails with the old host IOTLB assertion, then verifies 39 addresses with
CPU/bus bidirectional reads and writes over two reset phases. Existing boundary
and MXIC cases remain green. Separate unchanged execution crosses the host
assertion and, at that RAM-only checkpoint, stops at the 2120/2368-byte submission; the final
snapshot has GUI gate/pending3/display-ready zero. Receipts are in
`run ramalignment-20261023/`. No GUI first frame is claimed.

The [inline rectangle family](../devices/transfer/rectangles.md) passes the
complete14-case `nikon_transfer_unit` batch after its new submission fails
against the old parser. Its40 variants plus shared-header LUT regression
cover variable batch sizes, SRC RGBA/alpha, ordering, key wrap, full pixels/
guards, field/count/padding rejection, physical backing aliases, true backed
4GiB crossing, snapshot ownership and reset/unrealize/map-loss cancellation.
Independent review identified the32-bit end gap; it was fixed before the
final full applicable batch and final review passed. At that rectangle-only
checkpoint, unchanged execution crosses856/1088 START and enters original IRQ129, then aborts in
host QEMU's RAM/IOTLB page-alignment assertion. No final guest snapshot,
physical GPU fidelity or GUI first frame is claimed. Receipts are in
`run graphicscommands-20261023/`, including `native-final.log` and
`verified-progress.json`; earlier failed integration remains separate.

The [Audio observation profile](../devices/audio/monitor.md) passes22
applicable native cases: Audio7/DMA10/HDMAC5. A real HDMAC monitor read
reproduces the old model's SS3 before the new finite profile returns SS5 and
the actual word. New cases cover all8 observation registers, real sink-derived
output, held/empty/underflow behavior, separate lanes, quieter replacement,
negative writes and reset/unrealize. Last-observed policy,16-bit extraction
and zero high bits are documented functional assumptions. Independent review
found no defect. Separate unchanged-bundle runs cross the input getter and
amplitude consumer, then first reject CPU1 PA201810e0=1 at106ee3c6 with
transfer-stream-truncated. Receipts are in `run audiolevels-20261023/`;
no full-suite, microphone sample transfer or GUI first-frame claim is made.

The [HDMAC reload family](../devices/hdmac/reload.md) passes an applicable
21-case batch (DMA10/HDMAC4/Audio4/memory3) after the old-model RD matrix red.
A later private-root/DREQ/error case passes in the focused5-case HDMAC run;
additional rejection guards pass in the focused3-case reload run. All8 reload
combinations,3 widths and4 fixed-address combinations execute real bus copies;
hold/rearm, reentrant cancel, reset, no automatic repeat and partial-error
boundaries are covered. Receipts are in `run hdmacflags-20261023/`.
Independent read-only review found no defect. Separate unchanged-bundle runs
cross the original descriptor stores/starts and input enable, reach the
microphone initializer's return sequence and, at that earlier checkpoint, next reject Audio READ32
PA2c080154 at1036e3d8. No input DMA beat or GUI first frame is established.
This is not a new full-suite result.

The [audio format/block profile](../devices/audio/format.md) passes4 new
`nikon_audio`,7 `nikon_dma` and4 `nikon_hdmac` cases in one applicable batch,
after the old-model format case fails on0d820000. Coverage includes all format,
port and1024 divider combinations, real FIFO drain, programming-only block
state, preserved-state rejection and reset/unrealize. Two review-requested
guard assertions pass in a focused4-case audio rerun. Receipts are in
`run audioformat-20261023/`, including `native-final-receipt.log` and
`native-guards-receipt.log`. The separate unchanged original continuation
crosses format/block setup and, at that earlier checkpoint, next rejects HDMAC PA2c030034=2ae00000;
it does not complete microphone initialization, capture input samples or
establish a GUI first frame. This is not a new full-suite result.

The [capture run/stop model](../devices/sensor/runstop.md) passes all25
`nikon_sensor` cases in one final applicable batch after the old-model and
shared4/8 deadline/lost-identity reds. The whole11-unit family, both stop
notification layers, shared drain, staggered/duplicate commands, per-unit
cancellation, pending/W1C, reset/unrealize and negative access boundaries are
covered. Receipts are in `run captureflow-20261022/`, including
`native-final-receipt.log`. Original-bundle execution is separate: the old0c
store returns, a later GUI4000 wait receives its original callback, and the
at that capture-only checkpoint the next rejection is audio PA2c08010c=0d820000. No capture ISR, completed
pixel frame or new full-suite pass is claimed by this continuation.

The [finite router timing model](../devices/router/timing.md) passes23
`nikon_sla` plus25 `nikon_memory` cases in one focused batch after actual
unsupported-request and staggered-fence reds. New coverage includes all three
requests, independent deadlines/coalescing, phase/gates/force, absent input,
invalid accesses/period and reset/unrealize cancellation. Receipts are in
`run routerfunctional-20261022/native-final-receipt.log`. Final-binary
original execution under the declared functional hypotheses crosses both
IRQ87 waits and next rejects capture-control`2006800c=1`. This is not a new
full-suite, physical-camera or GUI-first-frame result. The earlier20-case
paired-control checkpoint remains in `run router-20261022/`.

The complete SYS indexed configuration family at`04280064/68/6c` passes
all25 `nikon_memory` qtests after the new case fails against the old model.
The case covers every bit of all three ports, word replacement, independent
state, bad-width/unaligned-write preservation, holes and reset reuse.
At that earlier SYS-only checkpoint, unchanged-bundle execution crosses the store and next rejects
`20060028=1` in the peripheral-event router; UART requests remain accepted.
See [SYS runtime](../devices/sysconfig/runtime.md) and ignored receipts in
`run sysconfig-20261022/`. This is a focused batch, not a new full-suite
or GUI-first-frame result.

The resource11/12 destination and explicit-NOR-input change passes all24
`nikon_memory` qtests after a native old-model row-storage failure. New cases
cover RAM capacity/bounds/alias/reset and both input files' bytes, exact sizes,
missing files, erased neighbors/alternative banks, unchanged destination RAM
and NOR mutation persistence. Original execution is separately qualified in
[rows.md](../runtime/rows.md): the whole2640-row output matches and the next
rejection is system configuration. Receipts: `run streamstorage-20261022/`.
This is a focused batch; the full-suite result below predates this change.

The integrated 2026-10-22 macOS `make check` run passes all40 Nikon test
executables and348 TAP cases (ARM/RX qtests and native units). It includes
the previously uncommitted finite graphics implementation and the kind40
storage correction. The first full run exposed an obsolete eye-sensor fixture
expectation that HOLD TX completion stayed clear; the corrected fixture checks
completion while the bus remains held, including its post-reset reuse. All24
sensor cases pass, followed by the complete40-executable green batch.
Raw logs are `run kindforty-20261022/native-suite-red.log` and
`native-suite-green.txt`/`.json`. This validates the integrated functional
models, not physical timing or an original-firmware GUI first frame.

The kind40 exact-storage correction passes all19 cases in `nikon_memory`
after an actual old-model red. The new case covers allocation endpoints,
independent cells, neighboring holes, absence of a physical VA mirror and
reset retention. The separate unchanged-firmware run completes the original
clear and stops at a source-stream read at PA50560000. See
[kind40.md](../runtime/kind40.md) and `run kindforty-20261022/`.

The capture-control mask/enable correction passes all four focused capture
cases after exact original/native reds. It covers complement normalization,
all12 reset/enable bits and their aliases, independent RMW/IRQ controls,
adjacent fields, invalid requests, publication/W1C and reset. Separate unchanged
original runs now finish the initializer and reject a buffer clear at PA62059800.
No frame completion is generated. See [control.md](../devices/sensor/control.md)
and parent-owned `run capturemask-20261022/` receipts; this is not a
whole-suite or GUI-first-frame result.

The timing divider/load correction passes all12 cases in `nikon_timer`
after recorded original/native reds. It tests all256 byte encodings across
12 channels, scaled initial/recurring expiry, divider phase, terminal/MAX
counts, invalid submission retention, capture, compare and reset cancellation.
The source-derived divisor field replaces the obsolete one-shot/source1 opcode
split. Separate unchanged-firmware runs cross channel7's seven waits and next
reject a capture-control mask write. These focused results do not establish
a GUI first frame; see [load.md](../devices/timing/load.md) and parent-owned
`run timingmatrix-20261022/` receipts.

The earlier MXIC active read-view correction passes all eight configuration qtests
in one focused native batch after a recorded old-model selector failure.
Coverage and unchanged-firmware continuation are qualified separately in
[view.md](../devices/config/view.md). That original run completed the
configuration initializer and stopped at channel7's raw31 shared timing load;
no GUI first frame is established. Receipts: `run mxic-20261022/`.

The reproducible-input extension passes five RNG qtests, two raw-UART routing
qtests, eight battery arithmetic/CLI units, nine finite-peer units and three
fill qtests (27 distinct cases in their applicable batches). Actual old-source
RNG/property, routing, attention-parser and record5-profile red receipts are
retained separately from greens in `run reproducible-20261026/`.
The original EX auth/0c/0e acceptance and original MAS record5 callback/waiter
are separate execution receipts. This is a focused macOS result, not a whole
suite, Linux CI, physical timing or GUI-first-frame result. See
[batterypeer.md](../runtime/batterypeer.md) and [fill.md](../runtime/fill.md).

> Status: this file mixes historical receipts with the current green native
> checkpoint. No original-firmware GUI startup is claimed. The EX continuous-run
> checkpoint below records 33 passing Nikon executables; the earlier 0x6b read
> wait is resolved by the receive shift-byte change. The 2026-10-21 VR retirement
> check is a focused 2-executable/22-subtest run, not a new full-suite receipt;
> see [the VR note](../startup/vrdomain.md). The later affine-path check is
> one transfer executable/nine cases. Earlier receipts keep their own scope.

The integration uses QEMU 11.1.2 at upstream commit
`4fc49f46dc95d4a27de2509e7fceb2931e91faeb`. The configured development
build is `work/build/qemu`; its source is the ignored `work/config/source`
view composed from the root `qemu/` submodule. Maintained Nikon source lives in
the parent repository's `hw/nikon`, `include/hw/nikon` and `tests/*/nikon`; the
current official dependency is the read-only root `qemu/` submodule.

The historical `106a0ce` build compiled and linked the complete six-class
storage profile, five-component bundle input, GPIO, completion, HDMAC, bridge
and packet transfer. The original complete bundle of that build entered GUI
initialization phases 1 and 2 and the GUI message task, then stopped on an
unsupported USIO byte write. The corresponding original-run record is linked
from [storage](../runtime/storage.md); it does not establish visible GUI
output and is not current-release evidence.

The historical cases below passed across the initial batch and its necessary
corrections:

| Cases | Result | Local evidence under `run validation/gui` |
|---|---|---|
| Memory class boundaries, independence, decode retention and holes: 4 | PASS | `memory.log` |
| GPIO explicit 192-pad input, completion domains and bridge events: 3 | PASS | `devices.log`, before its transfer failure |
| Packet byte copy, cancellation and equal-plane geometry: 3 | PASS | `transfercorrected.log`, 1.49 seconds |
| DMA pause, halt, cancel, reset, reentrant bus writes and Audio: 7 | PASS | `dmacorrected.log`, 2.06 seconds |
| Bundle parsing and invalid input rejection: 2 | PASS | `bundle.log`, before its VR record failure |
| VR record extraction and bounds: 1 | PASS | `recordscorrected.log`, 0.33 seconds |

The original 16 passing cases were not rerun. These 20 historical cases validate
native functional contracts; they are not measurements of camera hardware or a
claim that the complete camera boots.

The first Make invocation compiled the system and main qtest executables, but
its extra explicit parallel qtest targets selected an upstream implicit Make
rule and failed. The build continued with the upstream Ninja targets. The
initial device test failures had two independent causes: `-S` disabled virtual
timer execution in qtest fixtures, and a signed `ldl_le_p()` value was extended
while comparing VR addresses with `size_t`. Tests now observe actual board GIC
inputs instead of accessing the secure guest CPU interface. Each failed run
remains in the ignored logs; only the affected cases were repeated.

The next integration at `f44817e` also compiled both native ARM and RX system
targets and passed only its six new or previously failing cases:

| Cases | Result | Local evidence under `run validation/gui` |
|---|---|---|
| USIO transmit, receive, idle, flushing and disconnected nine-bit lanes: 4 | PASS | `usio.log`, 1.44 seconds |
| Source-derived paired transfer planes: 1 | PASS | `planes.log`, 1.00 seconds |
| RX TCG reset, caller stack, EXTB read/write and relocated undefined exception: 1 | PASS | `vrcorrected.log`, 0.91 seconds |

The RX target is the existing upstream TCG implementation with explicit EXTB
support. Its first native fixture had its register-store operands reversed;
the corrected synthetic instructions and paused TCG launch establish the
intended CPU behavior. Original RX execution separately revealed the TLB's
page-offset requirement for unaligned RAM extents. Private backing with an
offset alias preserves each exact mapped extent without adding padding or
changing guest bytes. These results do not establish complete original RX
firmware execution, device peers or visible camera output.

## Native source graph before bridge ISR correction

The `f02f0c43cd4da62b3a469717accd86e03d540f0c` worktree with the local
section-bounded transfer and test changes builds from the maintained root
`qemu/` dependency through `work/config/source` into `work/build/qemu`.
`make check MTESTARGS='--print-errorlogs'` passes all 27 Nikon fixtures,
239 subtests, with no failures. Raw output and the structured Meson record
are retained as `run research/guiresume/nativecheck3.log` and
`nativecheck3.json`. The official upstream submodule remains unchanged.

The new transfer unit boundary test fails its byte assertion on the old
implementation (`transfer-red.log`) and passes after the fix
(`transfer-green2.log`). It verifies switching from a short ROMD alias to
adjacent RAM/ROMD, not copying hidden backing bytes, and rejecting inaccessible
sources, read-only destinations and MMIO without successful completion or
read side effects. The existing transfer qtests also pass in the full run.

The first full build exposed the pre-existing save unit fixture's missing
`qemu_main` symbol on Darwin; its first runnable attempt then failed with
unknown QOM type `nikon-save`. The fixture now uses the same machine-none,
qtest QEMU initialization and cleanup as the other device unit fixtures.
All three original assertions pass without changing the save device contract.
Those failed attempts remain in `nativecheck.log` and `nativecheck2.log`.

The rebuilt ARM binary SHA256 is
`13158f95a0c3313ee9d177f50ed1d55ce9a5a317a67e3bab106736fab71fc764`.
Two separate bounded complete-bundle runs pass the observed COPY and loader
byte checks but fail the transfer-producer/GUI-first-frame gate. The current
[EV3 wait boundary](../runtime/wait/ev3.md) records the actual unsatisfied
mask and scope. Native synthetic tests do not establish camera boot or GUI
acceptance.

## Bridge ISR correction revalidation

The same base `f02f0c43cd4da62b3a469717accd86e03d540f0c` with uncommitted
transfer, fixture and bridge changes again passes all 27 Nikon fixtures,
239 subtests with `make check MTESTARGS='--print-errorlogs'`. The final raw
and structured receipts are `run research/ev3producer/nativecheck_final2.log`
and `nativecheck_final2.json`; the upstream submodule remains clean.

The bridge tests first fail on the old configuration IRQ cause
(`0x40 != 0x20`, `bridge-red.log`) and third peer-event mapping
(`0x20 != 0x10`, `event-red.log`). They pass after correcting both original
ISR sign-test bit numbers, without changing timing, reset, masking or W1C
expectations. The maintained bridge change contains no additional RX-word
backing or peer-bank assumption.

The final ARM binary SHA256 is
`f2512544ef07e70b703574fbb52e2a491bc5d43386ec17de55cf84ca030f2dbc`.
Two independent eight-second original complete-bundle runs confirm natural
bridge flag4 production and configuration-wait return, then reject the word
read at PA `0x2013e000` in MAS and SLA. Both final COPY/loader-byte and
transfer/first-frame gates fail; there are no observed COPY submissions.
The previous 14-COPY PASS belongs to its older binary and is not carried
forward. A withdrawn paired-word experiment remains ignored research only.
The [current EV3 frontier](../runtime/wait/ev3.md) separates these identities
and the still-unclosed getter/publisher contract. GUI acceptance remains
undelivered.

## Notification experiment and I2C divisor revalidation

The uncommitted native source graph now passes 28 Nikon fixtures and 242
subtests with `make check MTESTARGS='--print-errorlogs'`.
`run research/bridgeports/nativecheck_final2.log/json` hold the final raw and
structured records; the official QEMU submodule remains unchanged.

The added bridge unit fixture first rejects the old unsupported flag read,
then verifies local set/clear masks and the default-off experimental connected
profile. It covers masking/W1C, duplicate and zero sets, both directions,
invalid accesses, reset and disconnect. These synthetic tests validate a
declared model, not the hypothesized notification wiring/reset/firstword.

The I2C divisor qtest fails on the old full-control-word guard (`0 != 4`),
then passes for the same finite TX/RX modes with clock bits across all four
empty buses. It expects address NACK, not fabricated application success;
a different unsupported monitor mode remains rejected. The native build and
active LSP probes confirm that initial missing-QEMU-header/GLib diagnostics
were parser-environment false positives, not code fixes to bypass failures.

The current ARM binary SHA256 is
`3848f3146a462f0649e586d548d1a264f1424949c17a67be24979eb8d06fdae9`.
Two separate original complete-bundle, functional, MTTCG eight-second
observations explicitly enable `notification`. Both enter GUI phase3 and
pass all three loader-byte comparisons; they capture 11/13 COPY requests and
no COPY first fault. They then reject WORD write `0` at PA `0x2c04000c`, MAS PC
`0x1020b4da`. Neither captures a child constructor or graphics transfer, and
both fail the first-frame gate. This is conditional software-path progress,
not default-profile boot or qualification of the experimental hardware laws.

The same binary's default run confirms both notification properties false,
rejects `0x2013e000`, captures zero COPY requests and fails the loader checks.
Ignored receipts `run research/guiresume/{flagsi2c1,flagsi2c2,flagsdefault2}`
record each connection state, hashes, dirty source identity and bounded exit.
No GUI first-frame acceptance is claimed; the connected results do not
replace unconnected failures or the historical identities above.

## Read-only coprocessor startup qualification

Six later eight-second observations `coprocessor1..6` use this same binary,
complete bundle and the composed notification mapping. Both actual
notification properties are true in each receipt. Each captures 13 COPY submissions,
three byte-exact loader extents and the same rejected `2c04000c` write.
None observes a child constructor, graphics transfer or frame. No emulation
C, hardware reset/status behavior, backend, ISA decoder or ready producer was
added; the native suite above was not rerun merely for these research hooks.

The last two observe GUI component phase3 return1 before the controller
fault. The last also observes indexed callback bind returns0 and both actual
guest pointers in the callback array. Following the facade veneers rules out
the provisional interpretation of those calls as task destruction. This
qualifies a particular callback-configuration routine, not all global phase3
participants or framebuffer production.

Original byte checks corroborate the bootstrap entry arithmetic and matching
program/data footers; the ARM loader's mask200 signal/wait/return is observed.
This is program transport/validation, not coprocessor execution or a boot
marker produced by hardware. The [startup boundary](../runtime/coprocessor.md)
retains that pre-model unseen ready poll, original timeout path and
still-unqualified physical controller/provider law. A fresh run without the
connection, `coprocessordefault1`, records both properties false, zero COPY,
three failed loader comparisons and the unchanged `2013e000` rejection. All raw receipts and scripts remain under `work/`.

## Bootstrap request boundary and timeout continuation

The subsequent request-only processor device and finite TRANSFER setup
extension are separate from the earlier study and binary. Native bootstrap
and empty-configuration tests fail on the old rejection behavior, then pass
without ready words, automatic acknowledgements, processing replies or guest
completion IRQs. Negative halfword access is correctly checked as dispatch
`MEMTX_DECODE_ERROR`, not a device callback error.

A complete native check initially exposed `nikon_memory_unit` owner/region
teardown: QOM assertion in deferred flat-view destruction. The fixture now
explicitly removes mapped siblings while their embedded storage and owner
are alive, drains retired views, then releases ownership. Production DMA
behavior and all boundary/permission assertions are unchanged. Five isolated
repetitions and the complete rerun pass. This is fixture lifecycle repair,
not original-firmware red→green. Native host GDB bus-error and LLDB launch
incompleteness are inspection failures, not guest failures.

`make check MTESTARGS='--print-errorlogs'` passes29 fixtures /245 subtests,
zero failures, receipt `run research/z9reference/nativecheck-green.{log,json}`.
The ARM binary is
`ecb020af61b554c65d075640b6403095833e17b1b255eea812783a6d1d57a85f`.
Two independent complete-bundle twelve-second experiments,
`processorfinal1/2`, verify14 COPY calls, three loader extents and the actual
16 KiB bootstrap bytes; both observe ten original marker polls, natural stop
and shared cleanup, then graphics configuration. Pending processor ownership
isfalse at the bounded snapshot. Actual notification properties aretrue in
both domains; this is not default/physical notification qualification.

Both experiments first reject a nonempty graphics source write at
`201810d4`, value`bd0dff90`, PC`106ee3c6`. The live descriptor extents56/422
were then interpreted as bytes; the later read-only qualification below
establishes words and full224/1688-byte inputs. Upload/processing/completion
is not implemented. COPY-only `first-fault=null` does not mean global fault-free boot. The observer now
records uncovered transactions separately. No child, ordinary graphics
packet, scanout or natural first frame is accepted.

The same binary's unconnected eight-second `processordefault1` retains
notification properties false, zero COPY, failed loader comparisons and first rejection
`2013e000`. Its zero-filled bootstrap/source equality does not prove a copy.
The [processor contract](../devices/processor/protocol.md) and
[runtime evidence](../runtime/coprocessor.md) distinguish ownership/storage,
real timeout continuation, missing execution and the new graphics boundary.
The fixed Z9 reference supplied design context only; its AArch64 addresses
and synthetic service/peer fixtures were not ported. Final active LSP checks
are clean for six changed C/header paths; machine and transfer-fixture probes
lack `qemu/osdep.h` in their standalone include context and produce cascading
unknown-type diagnostics. Those are environment limitations, not an all-path
LSP-clean receipt; no fake includes were added. Native compilation, the full
suite and `git diff --check` pass; the official submodule remains clean.

## Owned graphics streams without processing completion

A later finite frontend admits the original two source/count/start triples
and captures real bytes, but leaves them unprocessed. These are historical
receipts under the superseded halfword-count assumption, not validation of
full original extents. Native red `stream-red.log` rejects10d4; green tests
verify real56/422-byte immutable snapshots, independent pending
ownership/tokens, negative transactions,
side-effect-free MMIO rejection, reset/unrealize cancellation and absence of
all three GIC IRQs. ROMD-boundary tests also verify section-exact stream
capture. The first build attempt lacked the newly added header in the ignored
source graph; `make prepare` resolved this infrastructure failure before the
behavioral red run. It is not firmware red→green.

`stream-nativecheck.{log,json}` records29 fixtures /246 subtests, allOK.
Active LSP probes confirm the three changed implementation/header/fixture
paths clean. The unchanged machine path retains its earlier standalone
include-context limitations; no machine code or official QEMU source was
modified for this stream extension.

Two independent twelve-second `graphicsstream1/2` observations use ARM
`df46dc78f70c5de5b503a71d39e4614e37cffb9b53e36841285ed147af0dfd96`
and actual notification propertiestrue. Both retain14 COPY calls, exact
loader/bootstrap bytes and the natural processor timeout. Both submitted
stream hashes match guest-memory prefixes using the same assumed56/422-byte
lengths; this is not independent full-bound proof. Both are pending at the
snapshot. The original graphics wait is entered with ordinal10, with no
observed return or IRQ129 handler. The distinct first rejection is another
MAS task's WORD write2014d200=0 at1046e19a. Thus there is both missing graphics
processing/completion and an independent display-parameter aperture gap;
these receipts do not isolate a single sole wait dependency or prove a frame.

The same binary's unconnected `graphicsstreamdefault1` retains notifications
false,
zero COPY, failed loader comparisons, no pending streams and2013e000
rejections. The [runtime evidence](../runtime/coprocessor.md) records the
actual stream identities, corrected command ordinal and separate display
boundary. All raw byte dumps, watchpoint receipts and original listings
remain ignored under `work/`; no command arrays or CPU/ISA decoder were added.

## Historical read-only stream extent qualification

The later parent probe `run research/guiresume/graphicsextent1/gdb.log`
establishes descriptor extents56/422 as32-bit words and register counts28/211
as64-bit units, hence full224/1688-byte sources. The old56/422-byte captures
are prefixes, not independently validated full inputs. The
[runtime qualification](../runtime/coprocessor.md#superseding-read-only-stream-extent-qualification)
records the actual descriptor/port observations and full-word input hashes;
at this stage those hashes were observed only by the read-only probe, not a
fixed frontend. The historical29-fixture/246-subtest PASS above remains a
receipt for that binary and its synthetic old-length contract, not full-extent
acceptance. The separate corrected implementation and receipts follow.

## Corrected stream count-unit revalidation

The parent accepted `8e113e58` changes capture to `8*units64`, preserving the
functional 128-KiB limit and unprocessed pending ownership. The actual native
red `run research/z9reference/extent-red.log` fails the updated submission
comparison on the old implementation (`56 != 224`). `extent-green.log` then
passes all four transfer unit cases, including full 224/1688-byte snapshots
and `stream-extent-boundaries`. The latter checks both stream banks, finite
count/access/setup guards, the exact allocation limit and over-limit counts,
and whole-extent MMIO preflight without read side effects or guest IRQs.
Existing reset/unrealize and ownership-isolation checks remain intact.

The focused memory fixture now also checks the finite source-bounded display
scalars. D200 accepts only the sole direct caller's observed zero while D100
remains independently retained; C0A0 accepts only the paired zero/one bit0
writes. Reset returns the virtual retained fields to zero, nonzero D200 and
other C0A0 bits fail, and table offset zero remains independent of the C0A0
slot. These checks establish neither a physical reset value nor any event,
completion or IRQ effect.

Persistent Ghidra Thumb marking and raw-byte verification further show that
the previously ranked `1046bfxx` image-start route is downstream of the
registered `tftdbg` handler. It is excluded from natural-startup acceptance.
No modeled graphics completion or external C010/control initial-value profile
is included in this validation scope.

The parent incremental build and
`make check MTESTARGS='--print-errorlogs'` receipt
`run research/z9reference/extent-nativecheck.log` passes 29 fixtures / 247
subtests, zero failures. Parent active root LSP probes are clean 3/3 for the
transfer implementation, header and unit fixture. These are scoped native
contract/build checks, not firmware or physical-camera red→green. No
additional native run accompanies this documentation-only correction.

Two separate twelve-second original complete-bundle experiments,
`extentfixed1/2`, use ARM SHA256
`0398c59a4da249213e15e475ef8ccc9e8b763ccbc758d315162be823b1f688ad`
with functional, MTTCG and both notification propertiestrue. Each retains 14
COPY calls, three exact loader extents, actual 16-KiB bootstrap equality and
ten marker polls followed by natural timeout/stop. Device stream hashes match
producer-bounded full 224/1688-byte inputs; the
[corrected runtime receipt](../runtime/coprocessor.md#corrected-full-stream-capture-without-processing-completion)
records source/count units and actual hashes. Tokens1/2 remain pending at the
ordinal10 graphics wait with no observed wait return, IRQ129 or frame.

The first uncovered access is still WORD write0 to PA`2014d200` at
MAS PC`1046e19a`. The new hooks observe caller VA`1044a4c4`, r1=0, and the
already-loaded D100 value at VA`1046e190`, r0=0, without debugger MMIO reads or
bypass. This is parameter/caller evidence, not a D200 implementation or
permission to admit zero as a discard. The same binary's unconnected eight-second
`extentdefault1` keeps both notification properties false, zero COPY, failed
loader comparisons, no pending streams and PA`2013e000` rejection; its
all-zero bootstrap equality is vacuous. These corrected capture receipts do
not provide graphics processing/fence/status58/IRQ129, processor execution or
independentD200, and do not promote notification hypotheses or accept GUI
startup. No self-designed hash gates or fabricated success were added.

## Profile-selector removal revalidation

The `04ec65c1` selector removal (old `7d055a2a`) deletes the machine-level
`functional-profile`, `nor-profile` and `cmos-provider` selectors. The
complete bundle now selects the composed board path; raw `-bios` remains a
separate MAS-only diagnostic input. The following EX I2C cleanup removes the
dead `last_write` field and the duplicate bundle-input selection call, and
the controller uses ACK/STOP-bounded FIFO progress instead of a per-address
receive-length table.

`make check` passes all 32 Nikon executables with zero failures; the raw
record is `work/build/qemu/meson-logs/testlog.txt`. The rebuilt ARM binary
SHA256 is
`b3d97591b347fad34e78140e86947fb275dc6daecf5c04c295b6f9a397303fe4`
(historical selector-removal checkpoint `04ec65c1`/`01daed54`).
The previously failing `/arm/nikon/ex/io` three-byte expander read passes.
These are native contract/build receipts; they do not establish
original-firmware startup or GUI output. The older 29-fixture/247-subtest
receipts above remain historical.

## EX 0x6b/0x0d device-model correction

The EX-side IDB shows that `0x0802b014` writes one descriptor byte under
command 0/1/3, reads it back, and requires equality after the mode mask. The
descriptor builders `0x08028ed8` and `0x08028cf0` supply zero or `0xa0`, not
fixed `1/3/1`. The `0x6b` model was therefore changed from a constant
responder to a register file: setup writes are echoed on the next read, and
command `0x0b` remains a separate ready status of `1`.

The `0x0d` model now exposes an explicit class register: class 0 `0x00`,
class 1 `0xa9`, class 2 `0xc9`, class 3 `0xe9`. Class 1 is the bundle
default, the `class` QOM property is a board input, and reset preserves it.

The focused `/nikon/ex/power-devices` case covers write/read equality for
commands 0/1/2/3, the `0x0b` finite ready/zero response, the `0x0d` class
table (0/1/2/3) and reset-preserved class selection, the out-of-range command
NACK, and reset clearing. `make check` passes all 32 Nikon executables with
zero failures; the rebuilt ARM binary SHA256 is
`05b82111753f96cd8b9c8464bf40ff2b95ba969dc52846a0d5c6f2a78df41425`
(historical 0x0d checkpoint `6a363d24`). This is a native model receipt and a
static IDB comparison, not a fresh original-firmware power-snapshot receipt.

## EX I2C STOP/RXNE revalidation

The EX I2C controller previously cleared RXNE/BTF and dropped the received
byte on a CR1 STOP. The 0x4f readback waits for RXNE or STOPF after that STOP
and therefore never returned. The controller now sets STOPF, preserves
RXNE/BTF and the received byte, and retires RXNE/BTF on the last DR read.
SR1 followed by SR2 now clears ADDR and STOPF, matching the HAL event
acknowledge. The `/arm/nikon/ex/stop-preserves-rx` qtest covers the STOP
written before the DR read and that SR1/SR2 clear.

`make check` passes all 32 Nikon executables with zero failures; the raw
record is `work/build/qemu/meson-logs/testlog.txt` and the rebuilt ARM binary
SHA256 is
`838805c46707e5b2669914d77b76a6dc4d42f8deed2a9b101986211e1d5cc231`
(release checkpoint `948f8a11`).
An original-firmware hardware-breakpoint probe on that bundle reaches the
0x6b read start with descriptor `0x2001fdcc` and its free-bus exit, and the
I2C receive handler then takes the `SR1` RXNE-with-BTF branch
(`0x08046bfe`) instead of the single-byte completion branch
(`0x08046cd0`), which leaves the driver state at `0x22` and prevents the
read-back comparison from ever running. That receive-prefetch boundary is
resolved by the [shift-byte change below](#ex-i2c-receive-shift-byte); this
checkpoint keeps only the diagnosis.

## EX I2C receive shift byte

The controller previously queued two receive bytes as soon as the address
matched, so a one-byte reader saw `SR1` RXNE together with BTF. The
acknowledge bit for a received byte is sampled at the end of that byte, so a
reader that clears ACK during the address phase never receives a second byte
and BTF stays clear, while a reader that keeps ACK does. The model now
delivers one byte at address match and the acknowledged shift byte after nine
`CCR`-scaled periods of the APB1 clock (100 kHz until `CCR` is programmed),
and cancels it when the guest clears ACK or ends the transfer.

`/nikon/ex/i2c-single-byte-read` covers the original 0x6b sequence (`SR1`
`0x42`, then `0x40`, then RXNE without BTF across a stepped clock, then STOPF
preserving the received byte). The existing `/nikon/ex/io` three-byte HAL
finish now steps one byte time before it expects BTF.

`make check` passes all 32 Nikon executables with zero failures; the rebuilt
ARM binary SHA256 is
`5157574bb9753ab2a785ba1e30b81b8d4d3d847422071fa1f2344bbdcceb9920`
(rebuild of the `0debf1e2` code together with these documentation edits).
An original-firmware hardware-breakpoint probe on the complete bundle now
reaches the 0x6b read helper's event wait, the setup comparison at
`0x0802b0d6` and its success exit at `0x0802b0e4` for commands 0/1/3 and later
repeats, the periodic `0x0d` status service at `0x0802b6f0`, its raw
fourth-byte store and the class stores that follow. A separate 300 s window
records only `0x0802b0e4` equality hits, with no `0x0802b0da` mismatch and no
`0x0802b112` sanitiser write, so the original read-back equality path really
passes against the model rather than being sanitised. That is still not a
complete original-firmware qualification of the monitor service.

## EX continuous-run checkpoint

The EX-specific fixes on this branch (RNG window at `0x50060800`, boot scatter
loader, power-good wire polarity, board record published at attach) make the
original EX image boot and run instead of restarting. Measured on the complete
bundle with hardware breakpoints:

| Observation | Window | Result |
|---|---|---|
| RTOS module initialisation | 180 s | `0x804b808` entered once, no re-init loop |
| Time base | 180 s | tick `0x7cf, 0xf9f, 0x176f, ... 0x98ab` over 60,000 increments |
| Host command dispatch | 240 s | >16,000 dispatches, ids `101`, `102`, `103`, `131`, `1114`, `1115` |
| Software timers | 240 s | >10,000 posts, timer ids 114/115 reach their handlers |
| Monitor lane | 240 s | 30,000 `0x0d` service entries and repeated `0x6b` equality edges |
| Resets | 600 s | one cold-boot escalation, then continuous |

The single reset is the firmware's own cold-boot ladder: it arms at tick
`0x2aa4`, records reason 2, and escalates at tick `0x51b4` through
`sub_8042d70` (status `15`) and the AIRCR writer. Further watchpoint windows
identified the mechanism and show that the ladder is a one-shot design, not a
model defect:

| Observation | Window | Result |
|---|---|---|
| Ladder parameters | one boot | copied at tick `0x3da` from the static word `0x010a0a01` at `0x20013b54` into `0x20013bc2` |
| Done-byte writers | 60 s | one store only, the tick expiry at `0x8032cc6`; no completion path observed |
| Re-arm guard | one boot | `0x200138ea + 4` is written `1` by the arm's own record builder (`0x8033b92`) |
| Verdict invocations | 100 s | two: arm at `0x2aa5`, escalation at `0x51b5`; none after the reset |
| Reset vector | 90 s | two entries: the attach entry and the one escalation |

The block initialisation that gates the ladder runs once per cold boot, and the
post-reset pass neither re-initialises it nor posts the verdict again, so the
ladder cannot re-arm and the domain keeps running. Whether the real camera's
host answers the management cycle inside its ten-invocation window is not
established; the bundle does not synthesize such a reply. The full block
description is in the runtime notes.

This is a bundle guest observation, not a native test receipt: `make check`
still covers only the device and machine contracts (33 Nikon executables, all
passing on this branch).

## RGBA initial mode-byte variant

The extended `/nikon/transfer/rgba-clear` case fails on the old parser's
word25=8 submission (`MEMTX_ERROR` instead of `MEMTX_OK`). After admitting
only initial0/8 with the later word60=8 still required,
`make check MTESTARGS='nikon_transfer_unit'` builds the native source graph
and passes one executable, all six registered transfer cases. The fixture
verifies output bytes, untouched rows/tail/guards, no early IRQ, ACK,
initial18 rejection, final0 rejection and initial8 map-loss/reset/unrealize
cancellation. Receipts: `run streammodel-20261022/{red,green}.log`.

This is a focused native receipt. The separately qualified unchanged-firmware
run uses an explicit constructed descriptor17 input, reaches original IRQ129,
and then rejects a later176/368-byte program. It does not establish GUI startup
or physical-camera graphics fidelity; see [the frontier](frontier.md).

## RGBA720x480 single-path clear

The new `/nikon/transfer/rgba-single-clear` case fails against the preceding
model: the first valid176/368-byte submission returns `MEMTX_ERROR` instead of
`MEMTX_OK`. The finite single-path fields and shared RGBA executor then pass
`make check MTESTARGS='nikon_transfer_unit'`: one executable, seven registered
cases. The shared fixture runs both profiles, checking all pixels, neighbors,
slot tails, no early IRQ and ACK, changed format/geometry/clip/target/endings,
setup/draw truncation, indirect path/coordinate and opaque-key rejection,
overlap, read-only output, and initial8 map-loss/reset/unrealize cancellation.
The second-key equality test applies only to the two-path profile; single-path
draw91 is instead checked as the fixed ending operand.

Receipts are `run singleclear-20261022/{red,green}.log`. This is a focused
batch, not a rerun of the whole Nikon suite. The unchanged-bundle functional
run separately observes this clear and original IRQ129, then rejects the
168/232-byte gray continuation. No GUI frame or physical color fidelity is
established.

## RGBA gray continuation context

The new `/nikon/transfer/rgba-gray` fixture establishes720x480 through a real
modeled initialization/completion/ACK, then submits the168/232-byte gray pair.
It fails against the preceding parser (`MEMTX_ERROR` instead of `MEMTX_OK`).
After adding finite retained context and strided rectangle output,
`make check MTESTARGS='nikon_transfer_unit'` passes one executable/eight cases.
The gray case compares every target pixel and guards/tail; it exercises ACK
retention and repeat, cold/other/reset contexts, pending initialization
cancellation and execution failure, invalid fields/keys/color/path/ranges,
pending reset/unrealize and execution-time map loss. Rejected complete programs
and failed execution are checked not to leave reusable context.

Receipts: `run graycontext-20261022/{red,green}.log`. The intermediate
`fixturecomparison.log` contains a signed-extension error in the fixture's
hex comparison; storing the loaded word as uint32 corrects it. This is a
focused transfer batch, not a full-suite receipt. The separate unchanged-bundle
functional run observes actual gray/black boundary samples and original IRQ129,
then rejects the next21488/28832-byte batch. It establishes neither scanout
nor a GUI first frame.

## Affine outline fill

The new `/nikon/transfer/path-fill` fixture first establishes/ACKs the real
modeled720x480 context, then submits two synthetic41/55-word path pairs. The
preceding parser rejects the valid batch (`red.log`). An independent review
adds a RAM-backing alias case: valid coordinates reached through a different
guest address aliasing the output are incorrectly accepted before the fix
(`aliasred.log`) and rejected afterwards. The final
`make check MTESTARGS='nikon_transfer_unit'` passes one executable/nine cases
(`finalgreen.log`). `buildinclude.log` is an earlier missing `<math.h>` compile
failure; it is not a runnable red-test result.

Coverage includes affine scale/translation, opposite and same-direction inner
contours distinguishing nonzero fill, quadratic interior/exterior samples,
coverage fringes over gray, clipping, all unaffected pixels and guards/tail,
retained context across ACK/repeat, cold context rejection, over-limit batches,
unknown/relative segments, bad counts/keys/fields/color/fill-rule flags,
nonfinite/perspective inputs, unbacked/overlapping/aliased inputs, unsupported
alpha, late bad DMA data, map loss, reset and unrealize. The latter cases
preserve the full destination and produce no completion IRQ.

Adding the path source and explicit Cairo dependency uses `make configure`
before the native batch. The fixed official submodule stays clean. This is a
focused transfer receipt, not a full-suite or pixel-exact-camera claim.
The final unchanged-bundle functional execution separately renders all131
original glyphs, enters original IRQ129 and then rejects the824/1112-byte
five-rectangle pair. Raw memory/PNG preview is not final scanout evidence.
Receipts: `run pathmodel-20261022/`.

## Ordered opaque path paints and relative H/V

`run coloredpaths-20261022/red.log` reproduces valid even-odd rejection
on the preceding model. The extension adds relative07/09 in object space,
even-odd fill, ordered opaque paints and distinct packed RGB channels. The
former gray continuation uses the shared path processor; its invalid geometry
and paint tests now use NaN/unsupported alpha instead of valid new inputs.
`green.log` records the intervening stale coordinate-negative fixture failure;
`finalgreen.log` passes one executable/all nine transfer cases, including
late rejection, unchanged output, DMA alias/map loss, reset and cancellation.
The separate unchanged-bundle run completes the824/1112-byte batch through
original IRQ129 and compares every pixel against the previous glyph frame.
Its next unsupported packet is176/336 bytes with an inline path and image
sampling state. These results establish neither physical raster precision nor
final display scanout.

## Inline path and A8 image modulation

`run texturemodel-20261023/red.log` records valid176/336-byte synthetic
image rejection on the preceding parser. `verifiedgreen.log` passes the
expanded native transfer executable, all10 cases. Image coverage includes
alpha0/64/128/255 and RGB channels, padded row width, affine path transforms,
f24 sampler, vertical inversion/clamping, entire target and guards, context
and repeat, delayed source reads, malformed fields/types/matrices/keys,
range/overlap/backing aliases, target alpha/map loss, reset and unrealize.
Unrealize is the final image subcase so later cases never reuse an unrealized
device. Intermediate logs retain a direct-load comparison correction and a
wrong f24 translation constant in the flip oracle before their correction;
only`verifiedgreen.log` is the final native receipt.

Independent read-only review finds the captured44/84-word state/geometry,
80x72 A8 stride, f24 sampling and mode14 modulation consistent with the fixed
reference and current bounds. The parent separately runs unchanged firmware
with the same functional descriptor:3751 icon pixels change before original
IRQ129, all pixels outside the72x72 patch remain equal to the prior frame,
and target alpha stays opaque. The subsequent224/1736-byte
RGBA-input/YUV-output pair is rejected. No physical GPU precision or final
display publication is established.

## RGBA input to YUV105 conversion

`run yuvmodel-20261023/red.log` records valid224/1736-byte rejection by
the previous model. `green.log` passes one native transfer executable, all11
cases. New coverage uses seven independently tabulated RGB/YUV colors,
different neighbors/source rows, a floating f24 oracle against the device's
integer expression, full-plane comparison, alpha0, alpha255 and unsupported
partial alpha, deferred source mutation, malformed sampler/LUT/format/key,
layout/wrap/range/command overlap, source/plane backing aliases, map loss,
reset and unrealize. Earlier `fixturebuild.log` and `fixturemath.log` record
test declaration/math-header issues, not behavioral red receipts.

Independent review matches every fixed field and all682 source rows to the
original capture/reference, and checks SRC/chroma plus output protections.
The separate unchanged-firmware run completes before IRQ129: full black RGB
source unchanged, Y all16, UV all128,698368 bytes per plane, adjacent guards
unchanged. The next captured pair retains224/1736 lengths but switches to
720x480 scaling/YUVA107, subsequently covered below. No final scanout or normal
startup is established by these results.

## Scaled RGBA input to packed YUVA107

`run yuva107-20261023/red.log` fails on the preceding model's rejection
of valid224/1736-byte input. `green-final.log` passes one native transfer
executable, all12 cases. The new case checks720x480→1024x682 sampling against
an independent floating f24 oracle, seven tabulated colors, all packed and
alpha bytes,0/64/128/255 source alpha, old-alpha retention at partial coverage,
white old-target/blue-source edge color mixing, deferred mutation, malformed
fields/LUT/layout/ranges, source/plane backing aliases, map loss, reset and
unrealize. `green-color.log` retains a test-oracle correction: a one-unit RGB
change does not imply one Y unit. Device arithmetic was unchanged by that fix.

Independent read-only review matches all fixed fields and all1024/682 sampling
indices, packed layout, SRC coverage and output protections. Separate original
execution with unchanged bundle/input completes before IRQ129, with1382400
zero source bytes unchanged,1396736 primary bytes containing Y16/UV128,
698368 alpha bytes0 and neighboring guards unchanged. The next176/1552-byte
warning-source SRC_OVER continuation is subsequently covered below. Its
complete command decode and former parser-fallback diagnosis remain alongside
that historical run.

## YUVA107 SRC_OVER continuation and retained context

`run srcover-20261024/red.log` records valid initialized176/1552-byte
rejection on the preceding model. `green-final.log` passes one native transfer
executable, all13 cases. New coverage tabulates white-over-blue colors with
source alpha0/64/128/255 and old alpha0/128/255; it checks every packed/alpha
byte, bounded fractional-edge error, repeat across ACK, absent/failed/canceled/
reset/incompatible initialization, rejected-stream invalidation, malformed
blend/sampler/format/LUT/196 mask/key/operand/layout/ranges, backing aliases,
deferred source mutation, map loss and reset/unrealize. Late failure/reset
also reject a following continuation. Initial`fixturebuild.log` is a helper
name compile issue, not behavioral red. Independent review verifies the
captured fields, byte masks, lifecycle and non-overflowing integer weights.

Separate unchanged-firmware/input execution reaches original IRQ129 and
composites the actual warning into1396736 packed YUV bytes and698368 alpha
bytes. An independent floating sampler checks every packed byte; alpha is
within1 of analytic coverage. Source and adjacent guards are unchanged.
`composition.png` is a decode of captured memory, not host-rendered UI or
proof of scanout. The subsequent60-second window has no new graphics error
or MAS data abort. Joined manager probes observe worker4→worker0 completion;
worker0 later stays class1/mode0 while GUI remains class4/mode0, readiness0.
These are functional-input execution receipts, not normal camera boot.
## Acceleration and paired NOR input checkpoint

The parent built through the existing native Make/Meson graph. The descriptor18
fixture first rejected the absent property; after implementation,`nikon_memory`
passes18 subtests, including paired descriptor17/18 input, boundaries, NOR write
persistence across reset, wrong sizes and unreadable files. Fixtures use synthetic
bytes; no original record is included.

The held-selector regression first fails with COMP0 on the old I2C model. The
source-backed held-batch correction passes the complete`nikon` qtest executable:
105 subtests, including the new acceleration controller/repeated-START cases and
16+2-byte held TX batches. A combined two-prefix GLib selection ran18 passing
subtests but produced invalid multiple TAP plans; the clean full-executable pass
is the accepted batch. Existing unrelated compile warnings remain unchanged.

`nikon_acceleration_unit` passes5 subtests covering initial/configuration sequence,
signed sample extrema, missing data, invalid transactions, partial/reset/restart
boundaries and out-of-range values on every axis. The normal board attaches the
peer without samples unless explicitly provided. The official QEMU submodule
tracked tree remains clean.

Review also found that the clocked slave callback can run without`current_cpu`,
so a generic CPU-access reporter could silently skip its uncovered-read error.
The peer now reports its bus address/register/offset and requests a debug stop
independently of CPU attribution. The final acceleration-only qtest batch passes
3 cases, including a seven-byte request against the six-byte sample boundary;
QMP confirms`debug`. This adds one distinct case to the105-case earlier batch.

Commands: `make check MTESTARGS=nikon_memory`,
`make check MTESTARGS=nikon`, and
`make check MTESTARGS=nikon_acceleration_unit`.
Original-firmware progress is independently recorded in
[guiupdate](../runtime/guiupdate.md), with no first-frame claim.
