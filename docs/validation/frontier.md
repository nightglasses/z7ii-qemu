# Original-firmware startup frontier

## Current: computed inputs, processing fences and first display enable

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

OnVd `10325ff6 ->103250f2 ->10233570` repeatedly fails FE event mask5 at
`102339d8` and processing-unit mask05b00000 at `102389c2`. The latter is an
**old-cycle fence before new configuration/commit**, not a new-commit ACK.
The callbacks and seven stage causes are source-established in the
[runtime audit](../runtime/imagerequest.md#onvd-waits-and-processing-stage-ownership).
The kernel confirms an AND wait for FE masks1/4 with requested-bit clearing.
The bounded FE scan finds no mask4 producer; the event snapshot has pattern3b,
with1 present and4 absent. OnVd immediately overwrites the wait return, so
failure alone does not establish a hardware-ready gate. Indirect provenance and
the independently guarded calculation entry remain open. Literal100/200 timeout
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
