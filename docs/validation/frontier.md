# Original-firmware startup frontier

## Current: graph command-buffer reset at PA `20100814`

The [type0 parameter-slot contract](../devices/gate/slots.md) now covers all
sixteen source-declared slots: complete thirteen-word packets, selector2c,
independent busy ownership and reset cancellation. A submitted slot remains
busy; elapsed time, ordinary stop ACK and W1C cannot turn parameter admission
into processing completion. Physical busy duration/encoding remains a model
hypothesis. No data DMA, result, event, IRQ or guest callback is supplied for
the unmodeled downstream processor.

The [slot receipt](/evidence/imagerequest/slots.json) records native query
red→green and23 passing SLA subtests. In the unchanged60-host-second
region/cold-neutral continuation, the original allocator crosses PA `20102044`
and original code submits slot1. Its complete packet is
`{20121,15500,0,0,80,80,80,0,22003c,0,12000,24000,0}`; slot1 is busy and the
other fifteen slots are idle. The eleven-node graph has a separate downstream
result path; type0's resource descriptors have zero completion masks.

The first uncovered transaction is CPU1 PC `103bdedc`, WRITE32 board PA
`20100814=1`. Graph node1 is type4/resource37, with a decimal72-byte command
payload at VA `b2bdb3a8`. Callback `103bdc72 ->103bded0` pulses backend+14
before supplying its payload pointer at+10. Both input-enable nibbles and both
retained input maps are still zero; parameter submission alone cannot justify
data completion or slot retirement. The next work is this command-buffer
consumer and its real processing/result/terminal-event join, rather than a
timer that clears busy or fabricates a terminal event.

The final observation records128 main acquisitions and448 statistics jobs,
with zero statistics rejection/cancellation and an empty error property6433.
QMP is debug/stopped, neither head has a console, GUI readiness/information
remain zero, and one root is queued with pending `40001`. No debugger pauses
occur inside the window. A separate failed read-only observer included a
reserved input-group word; its E14 is retained as an observer failure, not a
guest regression. The final snapshot uses sparse supported words. Normal GUI
startup and a completed processing-slot reuse cycle remain unverified.

## Previous: processing input availability at PA `20102044`

The [processing scratch arenas](../runtime/storage.md#processing-scratch-arenas)
back both source-declared allocator capacities: sixteen `80000`-byte blocks
at PA `b0100000` and four `4b000`-byte blocks at `af8d4000`. Allocation,
locking, busy arrays, returned descriptors and record construction remain
original guest execution. No wider address class, guest object or processing
completion is supplied by the host.

The [scratch receipt](/evidence/imagerequest/arenas.json) records native
red→green and72 passing memory/sensor cases. In the unchanged60-host-second
region/cold-neutral continuation, the original allocator owns type1 descriptor
`{b0100000,280000,0,5,1}`: five blocks are busy and the other eleven are free.
Type2's busy array is zero; its scope is source/native validation, not observed
runtime use. The run records128 main acquisitions and543 statistics jobs,
with no statistics rejection/cancellation and an empty error list.

The first uncovered transaction is CPU1 PC `103bd1da`, READ32 board PA
`20102044`. Getter `103bd1d6` reads `20102040 +4*index`, here index1.
Consumer `1038917c` consults this value for processing input type0 before
claiming the resource: zero permits that candidate, nonzero tries other
resources or enters the original wait. At that checkpoint the event-gate had
no producer/qualification contract for this query bank. Its full input-slot
mapping and busy lifecycle must be joined to actual submissions and releases;
returning unconditional idle would bypass that ownership check.

QMP is debug/stopped, neither head has a console, GUI readiness/information
remain zero, and one root request is queued with pending `40001`. The next
work was the complete processing input-slot lifecycle, separately from scratch
backing. Normal GUI startup and physical-camera fidelity remain unverified.

## Previous: processing scratch at PA `b0100000`

The [kind41 workspace](../runtime/storage.md#kind41-processing-workspace)
joins ROM pool metadata, the runtime allocation object and original PTEs.
`1026fbb0` owns a complete `400000`-byte unit at board PA `62221800` and
constructs128 records beginning at offset `300000`. The old write at
`625219bc` is a decimal24-byte clear inside the first record; its sampled
scratch register was not the entry length. The board now backs exactly
`[62221800,62621800)` without changing any declared pool boundary, page table
or firmware byte. Cold zero remains a functional board assumption.

The [workspace receipt](/evidence/imagerequest/workspace.json) records native
red→green,34 memory and37 sensor cases, including every page, actual TCG
CPU/bus sharing, cross-page HDMAC and negative bounds. With the same explicit
region/cold-neutral profile, unchanged firmware crosses the record clear.
The60-host-second run records128 main acquisitions and413 statistics jobs,
with no statistics rejection/cancellation and an empty error list.

Its next first uncovered transaction is CPU1 PC `10361044`, WRITE32 PA
`b0100000=0`, inside another original clear. Saved return `10271ae9` joins
consumer `10271abe`, which clears a decimal200-byte processing record.
The existing arena object at VA `b1808c98` stores this base at field `10`;
the separate scratch allocator and its complete ranges must be admitted on
their own source contract. QMP is debug/stopped, neither head has a console,
and GUI readiness/information remain zero. No normal live-view or physical
camera claim follows from this storage correction.

## Previous: original clear at PA `625219bc`

The [published-result latch](../devices/capturegroup/protocol.md#published-result-lifecycle)
separates actual automatic-TG results from later generator configuration and
run/stop changes. Only a valid automatic boundary can replace the eight-word
packet; commands preserve a prior publication without restoring cold zeros or
echoing a requested cursor. Retention across lifecycle changes is an explicit
functional hypothesis, not a physical reset specification.

The [latch receipt](/evidence/imagerequest/latch.json) records a native red on
the preceding expiration rule, then37 sensor and33 memory cases passing.
The unchanged-firmware continuation with the same explicit region/cold-neutral
inputs crosses the `2006bb70` getter. Its60-host-second run records128 main
acquisitions,428 statistics jobs and no statistics rejection/cancellation.
The error list remains empty. Counts are sequential observations, not
throughput comparisons with the preceding run.

The first uncovered transaction is CPU1 PC `10361058`, WRITE32 board PA
`625219bc=0`, inside the original zero-fill helper. The later read-only
snapshot reports DFAR VA `372219bc`, with `r6=37221800`. This identifies an
address translation and an unbacked write, but not yet the allocation owner,
declared extent or clear length. The helper has already transformed its
length register; the sampled `r1=80000000` must not be treated as its original
requested length. Recover the upstream allocation and complete clear request
before backing any additional storage. Existing RAM boundaries and original
firmware bytes remain unchanged.

QMP is debug/stopped, both heads lack a console, GUI readiness/information
remain zero and one root request is queued with pending `40001`. Passing the
getter does not establish the physical bank/cursor mapping, completed normal
GUI startup or stable live view.

## Previous: capture-group result lifecycle after sequence stop

The [global sequence model](../devices/sensor/protocol.md#global-sequence-start-and-stop)
executes all eleven channel start/stop masks and preserves each pending stop's
own virtual deadline. Only completed active channel8/0 stops publish router
IRQ87 causes18/10; the original ISR and callback set Sensor events10/20.
Mask consumption,1µs latency and the channel/cause association remain explicit
functional hypotheses. No pixel work, guest flag or callback is supplied.

The [sequence receipt](/evidence/imagerequest/sequence.json) records36 sensor
and33 memory cases, including a red→green staggered-stop regression. With
unchanged firmware and the same explicit region/cold-neutral inputs, the
60-host-second final run reaches129 main acquisitions and420 statistics jobs.
The Sensor error list is empty, both command masks have been consumed, and
the sequence model reports running `503`, stopping0. This crosses the earlier
ID63 branch; it does not establish normal GUI startup.

The first uncovered transaction is original MAS PC `10208e14`, READ32 board
PA `2006bb70`. Capture-group's result getter rejects an unqualified package;
its current model invalidates qualification on selected commit and run/stop.
The next investigation must establish the last actual result producer and
the getter's lifecycle across sequence restart, distinguishing retained
published results from unpublished or cold state. Do not return cold zeros
or a guessed bank/identity. QMP is debug/stopped, both display heads lack a
console, GUI readiness/information remain zero, and one root request remains
queued with pending `40001`.

## Previous: Sensor switch completion after first-read consumption

The [explicit resource35 profile](../inputs/nor/descriptor.md#resource35-first-read-region-input)
supplies complete bounded virtual region tables through descriptor17's normal
NOR loader. It preserves the rest of that opaque input, including the footer,
and keeps resource80 cold-neutral. Source field848 supplies64 first-read
samples; the preceding255 was the low byte of an erased FF parameter.
Bounds, temporal limits and virtual coefficients are distinguished from
recovered factory data. No guest flag, callback or frame is injected.

The [region receipt](/evidence/imagerequest/regions.json) records5 native
module-tool cases and33 memory cases, with all original `c00` loader bytes
matching the explicit file. A60-host-second unchanged-firmware run completes
128 main acquisitions and419 statistics jobs, with no statistics rejection
or cancellation and no uncovered access. QMP remains running, and head1
binds an actual1024x682 console containing status widgets and an error overlay.
GUI readiness/information storage remain zero; normal live view is incomplete.

The error list contains only ID63. An independently instrumented read-only
breakpoint observes `1026c1d4(100)` with LR `1026c839`, from the normal
`1026c66e ->102d986e(0)` call. The mapper turns status bit100 into Sensor
state9; `10394d06` maps it through table `118f9338[9]` to ID63. This separates
the new error from the solved resource35 negative-coordinate read.

`102d986e(0)` clears Sensor event10 at object VA `b18a764c`, submits command
101 through `102ff522 ->102976c2` to PA `200632e0`, and commits it with
`200632e4=1`. It then waits for event10 with timeout150 in original API units;
a missing completion contributes
status100. A second conditional breakpoint at `102d9906` observes the wait
returning0 with accumulated status0, isolating this missing completion from
the earlier readiness wait and command submission. These debugger probes
are separate from the uninterrupted60-second run. The SDES channel device
at that revision retained/published these global commands without their
completion work. The bounded sequence model above now supplies that hardware
state transition through the normal router route; the waiter flag is never
set by the host. The alternate107/event20 branch belongs to the same cohort.

## Previous: erased region parameters after first-read processing

The [bounded output-storage profile](../devices/statistics/protocol.md#completion-sequence-and-bounded-output-storage)
separates the configured completion sequence from each source-declared output
storage period. Four/six-slot outputs reuse real DMA storage while endpoint1
retains its large pool and full count. This is an explicit functional hypothesis;
physical counter encoding, autonomous cycling and phase remain unverified.
No RAM extent, firmware byte, queue, callback or completion source is extended.

The [storage receipt](/evidence/imagerequest/storage.json) records native
red→green,99 passing memory/CMOS/sensor cases and an expanded phase-reset
fixture. In the60-host-second unchanged cold-neutral continuation, sequential
queries report290 auxiliary completions,510 main acquisitions and799 statistics
completions, with no statistics rejection/cancellation. The two255-frame
first-read sequences finish, capture run/enable become0, unit16 resets and
the queued root is consumed. This reaches the original post-first-read
consumer, beyond the preceding input and RGB DMA boundaries.

That consumer `10218f84 ->10218d24` reads LE16 samples using region bounds
selected by `10218bca` from resource35 (decimal). The read-only snapshot
finds its pointer `b7f58400`; inspected bounds and count fields containFF.
Bounds at offsets24/124/224/324 hex are signed -1, as are the region counts.
With input pitch32 samples and base `5df45400`, the first coordinate(-1,-1)
computes `base +2*(-32-1) =5df453be`. Original PC `10218d7e` faults there;
QMP is debug/stopped. This is an erased-parameter consumer boundary, not
evidence for expanding the kind32 pool before its source-declared beginning.

The next work is resource35's parent input, original load path and a bounded
self-consistent virtual region configuration. GUI readiness and the information
record remain zero; both heads still lack a display console. Normal GUI
output and physical-camera fidelity remain unverified.

## Previous: RGB address progression during the first read

The [large-count grid pool](../runtime/capturedata.md#large-count-grid-storage-and-the-remaining-address-contract)
now backs kind32's complete source-declared PA range `5df45400..5f8d23ff`.
It uses the existing offset-aware RAM helper, independently of kind18. Original
pool initialization, all five absent overrides and both kind32 consumers establish
the allocation; cold-zero RAM remains an explicit functional assumption.
No new result, event, guest flag or firmware change accompanies this storage.

The [pool receipt](/evidence/imagerequest/pools.json) records native DMA
red→green and98 passing memory/CMOS/sensor cases. A60-host-second unchanged
cold-neutral run removes the previous endpoint1 RAM fault: sequential QMP
queries report380 auxiliary completions,1327 main acquisitions and392 statistics
completions, with1313 statistics rejections. Main acquisition has no rejection
or cancellation. The new rejection is endpoint4's calculated PA `5d4be000`,
span10, outside kind19's six declared slots. A separate30-second read-only
snapshot confirms the source setters' endpoint pointer tables and records.
Earlier cursor values can already enter other mapped pools; this is the first
RAM rejection, not the first ownership violation or evidence of correct RGB DMA.

`1023b86c ->1030af00` explicitly allocates6 RGB slots. Independently,
`1023a6c6 ->1023a63c` programs the first-read count255 into endpoints1/2/3/4.
The existing functional statistics hypothesis advances each DMA address once
per completed frame; it reaches `5d200000 +13*36000` at endpoint4. The
relationship between this completion count and RGB address progression must
be recovered before extending that model. Mapping the adjacent kind24 pool,
wrapping at6 or suppressing RGB output would each assume an unproved behavior.
The model retains the explicit rejection and publishes no completion for it.

CaptureService remains4, the first read is pending, information storage stays
zero, GUI readiness is0 and both display heads lack a console. The next work
is the address-control and completion-count contract, not another arbitrary
RAM aperture. First-read completion and normal GUI output remain unverified.

## Previous: kind32 output backing after main acquisition

The [independent main-input profile](../devices/sensor/protocol.md#independent-main-acquisition)
now acquires fresh scene-dependent gray frames after auxiliary readout stops.
It snapshots main/geometry commits, uses the existing TG and CMOS engine,
retains separate channel ownership and publishes input status only after real
sampling completes. Normal mode1 masks per-frame notifications; masked status
does not block its endpoint ring. Completely zero output extents disable those
outputs while preserving their retained routes; incomplete dimensions and an
all-disabled job still reject. Pixel reduction and those finite output/timing
laws remain explicit functional hypotheses.

Native red→green covers absent main input, masked-frame stalls, cross-channel
cancellation and partial outputs. The applicable97-case batch and final32-case
CMOS extension pass. In a60-host-second unchanged-firmware cold-neutral run,
1325 independent main frames complete with no main rejection/cancellation.
The final45-second profile completes985 main frames after290 auxiliary DMAs;
main acquisition therefore continues across the normal run1d transition.

That revision's next rejected work is statistics endpoint1's PA `5df45400`, span300,
resolving to `z7ii.uncovered`. The original `1030ad70` constructor selects
kind32 rather than ordinary kind18 when requested count exceeds6; its unit
stride is12000 and total size is stride times count. The runtime kind32 base
at MAS VA `b26b4bd0` matches this destination. The complete kind32 allocation,
backing and address conversion must be established together; a single-address
mapping or enclosing DDR aperture is not added. All required active output
DMAs must succeed before the model can publish statistics/ring completion.

The [acquisition receipt](/evidence/imagerequest/acquisition.json) separates
source facts, finite hypotheses, native verdicts and original execution.
CaptureService remains4 with the first read pending, information storage is
zero, GUI readiness is0 and both display heads lack a console. Group40 remains
excluded from this request. No first-read completion, FECLCTRK admission or
normal GUI output is claimed.

## Previous output and first-read boundaries

The [finite grayscale stream](../devices/capturework/protocol.md)
now performs unit1 identity forwarding, unit2 geometry sampling and paired
LUT/packing into the four source-allocated kind16 RAM slots. Arithmetic and
shared input association are explicit functional hypotheses; original bytes,
guest state and memory allocation boundaries are unchanged. Native tests check
actual nonzero output, padding, ring/bank/lifecycle and rejection boundaries.

In the final40-second cold-neutral explicit-scene run,388 stream jobs write737300880
payload bytes. Original stage setters100000/200000/4000000 execute and the
unit1-only fence succeeds. Aggregate05b00000 still fails: of its five requested
stages, group1000000 alone has no observed setter. All36 observed OnVd mask5
resumes succeed, but caches1/2, the information notification and both display
consoles remain absent. The [stream receipt](/evidence/imagerequest/stream.json)
separates source facts, model assumptions and original observations.

The [payload audit](../devices/capturegroup/protocol.md#payload-consumers-and-wire-records)
recovers three wire formats (40/24/32 bytes), their conditional consumers and
packed 12/23/29-bit fields; their generating arithmetic remains unresolved.
The existing group model only advances addresses. Two new 25-host-second
breakpoint windows still bypass FECLCTRK before payload consumption: all
28/27 selected roots retain mode0, metadata byte10132=0 and latchdc8=0.
The retirement comparison is not reached. A group completion is therefore
not established as the producer of the currently missing admission condition.
Another 25-second `firstread` probe observes OnVd's normal state4 submission:
`10233cf6` sets `dd8=1` and queues a root, but neither its `041a0003` branch
nor FE worker8 runs. Its requested/active mask is `0xb`, excluding group and
paired work; final reset is0 and run/enable are `0x1d`. The immediate boundary
is this first-read processing and completion lifecycle, with unit2
requesting 8288 by58 input and2072 by58 output rather than the continuous-stream
profile. A separate 25-second control without in-window GDB attachment reaches
the same pending request, with367 input DMAs and366 statistics jobs and no
reported rejection/cancellation. This is an
instrumented execution boundary, not normal GUI output.

The source now connects mode1's normal first-read completion to raw3.bit7,
owner80 and `1023c57a`: switch endpoint1's address set, then stop and emit
`041a0003` after its next completion. The
[finite ring model](../devices/statistics/protocol.md#endpoint-ring-completion-and-first-read-mode)
publishes that cause only at the committed endpoint1 ring tail after all
output DMA succeeds; address selection and ring-end timing are explicit
functional hypotheses. Native red→green and93 applicable cases pass. Original
55-second tracing confirms owner80 consumption, but no first-read notification
or FECLCTRK. A corrected120-second control without live GDB still stops at380
input DMAs/379 statistics jobs and no console.

That audit identified the missing independent main-image acquisition.
First-read run1d intentionally clears auxiliary readout bit1 while retaining
main-input bit0. The preceding provider only acquired CMOS frames for auxiliary
requests, whose stop also invalidated the shared input generation. It could not
supply the first read's255 new inputs after that normal mode switch. The
independent profile above closes that input lifetime. Group40
is excluded from this request and cannot repair that source lifetime. Group
generating arithmetic and paired numeric getters remain unresolved; no guest
flag, frame or completion is fabricated to replace the missing acquisition.

## Earlier frontier observations

The [image request audit](../runtime/imagerequest.md) separates the normal
GUI/display producer from AF6's state9 request. Original drawing and valid
layer programming run; operation10 now unpauses successfully with ID68 absent.
Display active/requested-enable masks and bank port/layer enables remain0.
Phase18's final enable and the image-information04380001 publication have
not executed in their focused cold-neutral windows.

Both root parities are created. Earlier step102669aa cancels the eligible
phase at10266a8c because computed cache1 is unpublished; its cache2 check is
short-circuited. Opposite-phase jobs survive to1026a312 and fail `{2,0}`
cadence admission. The missing cache producer is the original FECLCTRK /
10273268 ->10272d62 calculation, not an established missing external blob.
FE's bootstrap ends with synchronization flagb1887dc8 still0; normal
mode0/metadata+10132=0 bypass the subsequent calculation submission.

The [phase/producer evidence](/evidence/imagerequest/phase.json) joins the
upstream041a0002 request to queued10326676, which transitions service2 to3.
Its20-second focused window has a full60-record queue and lost enqueues, but
the later [coordination audit](/evidence/imagerequest/coordination.json) observes
`10323198(10,2)` and naturally reaches service4 with queue occupancy0 in both
the baseline and the changed model. Backlog is not a permanent transition block.

In the historical baseline, OnVd `10325ff6 ->103250f2 ->10233570` fails FE event mask5 at
`102339d8` and processing-unit mask05b00000 at `102389c2`. The latter is an
**old-cycle fence before new configuration/commit**, not a new-commit ACK.
The callbacks and seven stage causes are source-established in the
[runtime audit](../runtime/imagerequest.md#onvd-waits-and-processing-stage-ownership).
The kernel confirms an AND wait for FE masks1/4 with requested-bit clearing.
The historical bounded FE scan found no mask4 producer; the event snapshot has pattern3b,
with1 present and4 absent. OnVd immediately overwrites the wait return, so
failure alone does not establish a hardware-ready gate. The notification audit
below now resolves the mask4 software route and models qualified input sync;
the independently guarded calculation entry remains open. Literal100/200 timeout
arguments retain unverified extended-ABI units.

The [per-stage audit](../runtime/imagerequest.md#work-and-result-ownership-across-the-fence-family)
separates front-end packets, group cursor metadata, paired parameter results
and actual statistics work. The accepted
[statistics completion](../devices/statistics/protocol.md#processing-stage-completion)
now publishes its own raw3.bit5/IRQ96 only after the owned ten-output processing
and numerical results succeed. The added fixture fails on baseline raw3=0
after actual DMA;18 CMOS plus33 Sensor cases pass. No other stage or FE bit is
inferred from it.

In the30-host-second cold-neutral `end` run,735 auxiliary DMAs and734 statistics
jobs complete, with734 original event800000 setters from102394c9. FE mask5
and the aggregate05b00000 still fail in50 matched waits each. A30-second
guest-error-only run completes725/724. Both naturally end in service4/queue0,
with caches1/2 unpublished, display masks0 and no console; reported rejection,
statistics cancellation and peer rejection remain0. Front-end work, group
buffer processing and PA2006953c-family result production remain open.
The [fence receipt](/evidence/imagerequest/fence.json) separates source, native
and unchanged-firmware observations; accepted configuration does not justify
all requested completions.

The subsequent [front-end audit](../runtime/imagerequest.md#front-end-input-and-buffer-boundaries)
locates unit1's optional parameter pointers and unit2's ROM-policy/coefficient
sources, without identifying their actual pixel input/output contract.
Kind23 construction does not populate its payload; the pool's bytes are zero
in both final physical reads, with its handle already cleared in the second.
The paired result getters do not execute
in these focused windows. Owner mask1's041a000f input notification is also
absent within the owner-entry filter. The new30-second runs complete813/812
and742/741 auxiliary/statistics operations without reported rejection, but
end in different lane/run states and still have no console. No additional
completion model is accepted from configuration or these negative observations.

The [explicit image-input continuation](../runtime/imagerequest.md#explicit-scene-input-through-actual-dma-and-statistics)
repairs source binding, raster parsing, scene sampling and C170 grayscale
encoding. Under the same cold-neutral descriptor32, a60-second original run
with fixture-provider false completes814 auxiliary DMAs and813 statistics jobs.
Four buffer samples and all four scalars match the supplied2x2 scene exactly.
The54-case native batch separately verifies the finite data path. Cache1/2
remain unpublished and there is still no console: usable auxiliary image data
alone does not close the front-end input or calculation-production boundary.

The [notification/submission audit](../runtime/imagerequest.md#notification-routing-and-actual-submission)
closes `041a000f -> FE wait mask4` through its shared instruction tail. This
supersedes the earlier bounded search that found no mask4 producer. The separate
`041a0001 -> FE worker10000` does not dispatch the FE8 completed-queue consumer.
Unit1 initialization, unit2 policy
submission and incremental commit `9e` execute. Eight selected roots still take
the mode0/metadata0/synchronization0 branch before calculation submission.
The implemented shared virtual acquisition hypothesis now publishes input sync
only after actual owned CMOS DMA, snapshot and completion, with continuously
qualified capture run bit0. A55-case native batch covers token validation,
revocation, cancellation/reset and mask/W1C behavior. In the changed-model
40-second explicit-scene run,375 auxiliary DMAs and374 statistics jobs complete.
Owner1 and the original FE mask4 setter execute, and all41 observed OnVd mask5
resume points return1. The unit1-only fence and39 aggregate processing fences
still return0. Cache1/2 and GUI output remain absent; input synchronization
does not substitute for unit1/2, group or paired-result work. The physical
association remains an explicit hypothesis.

The normal information producer remains registered without an observed
publication. An additional first-enable candidate is now source-joined:
`1035a2d8 ->04380001 ->104be876 ->record92 ->1053f42a`.
A change to packet offset1016decimal (`3f8` hex) and the `105b0a7e` policy
guard lead to `104a94fe(5)` at `1053f7c0`; nonzero additionally posts record133
but is not required for operation5. The record has not
been successfully consumed in the focused windows. This conditional route
does not require a target switch or property6433 change; it is not established
as the unique first-enable producer. AF6/state9 is a separate guarded request.

The accepted finite model now reserves an auxiliary request immediately and
admits actual CMOS DMA only at the next fresh automatic TG input edge. This
functional association removes independent completion/resubmission feedback;
earlier/busy edges are discarded, forced levels do not acquire input, and the
original33333334ns virtual period is unchanged. Same-edge statistics uses the
previous completed input. No software event/cache/state or display enable is
injected. Native red→green evidence and the applicable51-case batch cover this
ownership/input boundary separately from original-firmware progress.

With unchanged firmware, cold-neutral descriptor32, RNG0, healthy paced battery,
held F4 and5 MTTCG CPUs, a20-host-second focused window completes519 readouts
and518 statistics jobs; the comparable baseline completes1999/523. The changed
90-second CPU/nochain run completes739/738 and a30-second guest-error-only run
completes818/817. Both end in service4 with empty queues, unpublished caches1/2,
zero display masks and no console; no readout/statistics rejection, statistics
cancellation or peer rejection is reported. Different trace filters affect
throughput, so these are coordination observations, not rates or a complete
stability claim. The next boundary is genuine FE/remaining-processing-stage completion
and computed-input publication before the GUI information notification.
The earlier [admission receipt](/evidence/imagerequest/admission.json) and
six-range COPY comparisons retain their original scope. CPU0 at10100008 alone
is not a fault receipt: the original extended API uses the normal SVC vector.

## Finite processing-data completion

Published records for this checkpoint:
[original execution summary](/evidence/capturecompletion/finalreview-proof.json),
[native CMOS/Sensor results](/evidence/capturecompletion/native-final.txt),
and [commit-revision regression results](/evidence/capturecompletion/native-revision-green.txt).
These attachments are shipped with the site; the [archive](evidence.md)
describes their scope and omissions.

The complete-bundle [statistics profile](../devices/statistics/protocol.md)
joins unit16's selected commit8 snapshot and automatic TG with immutable
successful readout input, ten endpoint DMAs, running-bank twelve-word results,
four scalars, then raw0.bit6 through existing IRQ94/W1C. Geometry, ring
configuration, consumer packing and software retirement are established.
Grayscale arithmetic, auxiliary-to-processing association and1ms latency
are explicit functional hypotheses, not recovered physical ISP fidelity.
No guest software state, callback, PC or UI pixels are injected.

The scoped old implementation fails the new native fixture because the
coordinator is absent. The final applicable native evidence is32 memory,
16 CMOS and33 Sensor cases (81 total). It covers all ten outputs, exact
arithmetic including signed24/u29/u17 saturation, immutable input/config,
bank selection, ring advance, unread-frame replacement after ACK, cancellation,
reset and invalid ROM/overflow/layout/extent/overlap boundaries.

Unchanged `capturecompletion/finalreview` uses the same cold-neutral
descriptor32, RNG0, healthy paced UART battery, held cold F4 and5 CPUs under
MTTCG. All six original loader COPY ranges compare, guard returns0 with
version150, and property6433 is empty. After20 host seconds QMP is running;
sequential reads report1570/1569 accepted/completed auxiliary requests and
519 completed processing jobs, with no processing rejection/cancellation,
uncovered access or peer rejection. The focused trace observes owner bit2,
the twelve-word consumer and pending40000 retirement. All ten configured
output slot0 prefixes contain nonzero DMA-produced bytes. Queue lengths are
0/0/0/0. The preceding `final` checkpoint completed527 processing jobs and
had0/1/1/1 queues with a new root pending40001. These observations demonstrate
sustained processing and retirement, not a one-job/one-root correspondence
or physical frame cadence. Packages may be discarded when queues are empty.

There is still no display console. No new unsupported hardware transaction
is observed, so a sampled CPU PC is not identified as a startup stop.
The next boundary is the original software image/display request: sampled
root[8]/root[16] were0 in `final`, and both runs have service
`b1868af1=2`, `b1868af6=0` and `b1868b0c=0`.
`103f27ba -> 10328ca6` requires state9/property1053=1 before enqueuing
`10323a7e`, which sets AF6=1. Root[8] separately requires B0C=2. Neither
AF6 request function appears in the focused trace. These conditions explain
why those conditional routes are unselected. The later image request audit
above identifies separate normal information and display activation producers;
the full startup prerequisite relationship remains unresolved. No mode is forced.


## Related analysis

| Question | Article |
| --- | --- |
| How do cold-neutral input and processing completion interact? | [Capture service](capture.md) |
| What does a rendered error frame establish? | [Graphics and timing qualification](graphics.md) |
| How do battery and held-input profiles differ? | [External-input profiles](inputprofiles.md) |
| Who publishes battery reports and host commands? | [EX reports](exreports.md) |
| Which display requests and kernel waits were observed? | [Display publication](publication.md) |

The current boundary above takes precedence over earlier observations. Native
fixtures, unchanged-firmware observations and assisted experiments remain separate.
