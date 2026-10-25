# Original-firmware startup frontier

## Current: finite processing-data completion

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
why those conditional routes are unselected; their necessity for normal GUI
and the upstream normal display request remain unresolved. No mode is forced.


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
