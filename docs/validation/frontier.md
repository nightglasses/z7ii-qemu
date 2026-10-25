# Original-firmware startup frontier

## Current: computed inputs, service backlog and first display enable

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
upstream041a0002 request to queued10326676, which would transition service2
to3. The focused service queue is full at60 records:94 OnVd enqueue successes
and442 failures, versus2 transition successes and31 failures. No10326676
execution is observed. This establishes backlog/loss in that window; the
outstanding OnVd wait and first successful transition consumption remain open.
The normal041b0040 ->103284d0 ->1035a2d8 ->106d103c ->104be876 producer
remains registered without an observed information publication.
AF6=0/service2 explains a separate conditional route; state9 is not established
as a prerequisite for these normal display functions.

The first-enable audit adds primary8000/phase17 and property6433 change-indexed
primary4/phase5 routes to the earlier primary400/phase18 path. Mode entry sees
target1 already selected and skips8000; phase5 sees no6433 index-ed change and
skips its direct operation5. A further window confirms index-ed enabled1,
pending0 and no mark request. An empty list is not itself a change notification.
The policy becomes1, so permanent policy0 is not the explanation.

The focused20-second cold-neutral `join` window compares all six loader COPY
ranges and reports1913/1912 accepted/completed readouts and525 statistics jobs,
with0 rejection/cancellation and no console. These are sequential QMP reads.
Temporary100ms input pacing empties the queue but enters a different stop path;
default-period sparse tracing also diverges, to service4 and CPU0 vector10100008.
Neither is a GUI success or proof that backlog is the sole blocker. The original
33333334ns period is restored, with no accepted device change, firmware edit,
injected event/cache/state or forced enable. The earlier
[admission receipt](/evidence/imagerequest/admission.json) retains its own scope.

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
