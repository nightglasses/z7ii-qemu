# Capture service and cold-neutral input

These observations qualify a specific input and source revision. They do not describe a new execution or replace the [current startup boundary](frontier.md). Earlier negative observations retain their original scope; later corrections are stated beside the affected interpretation.

## Cold-neutral input and capture service frontier {#cold-neutral-input-and-capture-service-frontier}

The explicit [cold-neutral resource80 input](../inputs/nor/virtual.md) now
passes the original loader and version guard on the inspected mode11/subtype3
path. The native `nikon-module` tool defines complete zero-valued readsets,
no-op script lists, cleared source control fields and nonzero timing under
named functional assumptions; its4 native cases pass. It supplies an external
NOR file and does not patch loaded RAM or guest software results.

The unchanged `capturebanks/neutral` run compares all six COPY ranges
with that file; original `10305dac` returns0 at `102d7254` with packed version150.
Property6433 is empty at the checkpoint, so ID68 is absent under this input.
Both timer cases execute: selector4 at record`3230` produces q130/count35319,
then selector3 at record`2c00` overwrites them with q0/count35189. Independent
secondary input remains at`50300000`, with its selected208-byte block zero;
the NULL-clearing branch is not the observed path.

The [two service parameter banks](../devices/capture/banks.md) retain both
source-selected banks at PA`2006a000/2006be00`. The subsequent
[capture configuration group](../devices/capturegroup/protocol.md) covers144 identified
words, complete source masks and independent opaque address fields, without
DMA or result production. Its original configuration-only model passes30
Sensor cases. The unchanged `capturegroup/green` run uses the same
cold-neutral input, compares all six loader COPY ranges and again records
guard0/property6433 empty. It crosses the former PA`2006b800=8` write and
enters `10239dc6(b1887ef8,1) -> 1020c17a -> 10208e0a`.

That run first rejects CPU0 PC`10208e14`, READ32 PA`2006bb70`.
Force1 bypasses a later identity comparison, but does not skip this old-result
snapshot. Shared commit/run for unit20 follows that consumer; it has not yet
executed. The eight result registers' cold values and update producer remain
unrecovered, so the model does not supply zero or copy the requested identity
to force progress in that configuration-only model.

The later cold-empty profile defines a readable empty initial packet, with
all eight words0 under an explicit functional assumption. Either group commit
or effective unit20 run synchronously invalidates the entire packet; stop,
disable and shared unit reset do not restore it. Post-submit reads remain
errors, and there is no ready, result identity copy, frame, DMA or IRQ producer.
Native old-model red is followed by all31 Sensor cases green, with the final
scoped case also checking an unrelated effective unit.

The unchanged `captureresults/green` run uses the same input and again
compares all six COPY ranges with guard0/property6433 empty. The first force1
getter returns, ba30 becomes1, both mask10 commits execute and unit20 reaches
`1020aafc`. Force1 does not consume the result numbers, so this receipt proves
software continuation under the assumption, not the correctness of physical
cold values or any post-submit result. That run's first uncovered access is CPU0
PC`102090bc`, WRITE32 PA`20069514=6`, in the separate `102090a0` parameter
writer through`1020c2d4`.

The [paired parameter family](../devices/captureparams/protocol.md) now covers
141 source-written words: both five/six-word packets, two local controls and
the shared128-word table. All51 holes, including eight unpublished result
words, remain errors. Old-model red is followed by all32 Sensor cases green
and a final scoped reset-readability check. There is no new result, frame,
IRQ or DMA producer.

In unchanged `captureparams/green`, the same cold-neutral input again
compares across six COPY ranges, with guard0/property6433 empty. The six-word
configuration is`6,05640000,03960000,0,0,0`; the128 words compare with actual
ROM source`119b5ce4`. Original code sets952c.bit1, submits both shared command
masks80, and reaches unit200 run through`1020c23c(1) -> 10208f72 -> 1020aafc`.
The next first uncovered access is CPU1 PC`10404398`, WRITE32 PA`c33ab400=100`.
`10404354` is generating interpolation records through a packet destination
pointer. This historical checkpoint precedes the pool correction below.
QMP is debug/stopped before a display console exists.
This is bounded functional-input/model progress, not a new GUI frame, factory
calibration, later-mode acceptance or complete boot.

The [interpolation storage investigation](../runtime/interpolation.md) joins
the current pointer to kind74's full source-declared15MiB pool at
PA`[c24cb400,c33cb400)`. The request is19c80 bytes; the allocator reserves
two10000-byte units at PA`c33ab400`. Original translator and guest section
descriptors agree on VA`3d5ab400`. Native old-model red is followed by all29
memory cases green, including CPU/DMA sharing, exact holes and reset retention.
Cold zero remains a functional assumption; records are generated by firmware.

Unchanged `interpolationmemory/verified` again compares all six COPY
ranges and records guard0/property6433 empty. It writes568 nonzero-tag records
before another task's first uncovered READ16 at CPU1 PC`1024ffda`,
PA`5d170000`. That consumer reads a descriptor-selected16-bit grid and computes
threshold statistics. Source ownership/data remain unknown; no samples are
fabricated. Record-generation completion is not established. QMP remains
debug/stopped with no display console, so normal GUI output is still unverified.

The [grid storage investigation](../runtime/grid.md) establishes kind18's
six12000-hex units over PA`[5d170000,5d1dc000)`, identity guest mapping and
the endpoint1 output route. Geometry comes from firmware ROM; samples are
intended capture output, with statistics DMA then unimplemented. The model
supplies the complete pool with explicit cold-zero RAM semantics. Old-model
red is followed by all30 memory cases green, with all six units independently
DMA-tested through reset and exact byte edges kept uncovered.

Unchanged `gridstorage/verified` again compares all six COPY ranges,
records guard0/property6433 empty and returns from`1024feb6` to`10253caa`.
Original software computes mean0 and central low-threshold count140 from the
assumed initial zeros. The next first error is CPU0 PC`10208e14`, READ32
PA`2006bb70`, on `1023aed8 -> 10239dc6(force0,LR1023afb7)`. The earlier
commit/run has invalidated the cold-empty result packet; no later result
producer is invented. QMP is debug/stopped with no display console. This
qualifies storage continuation, not captured statistics or normal GUI output.

The subsequent [post-submit group investigation](../devices/capturegroup/protocol.md#post-submit-investigation-incremental-caller-and-provider)
identifies the actual upstream service call`103250f2:10325714 -> 10233570`.
Mode2 at VA`b1887d75` and root VA`b2cb5100` word+270=1 select
`10232154 -> 1023b00e(flags2eb) -> 1023aed8`. Flag40 and pointer-array slot4
then call`10239dc6(force0)` before this operation's commit. Unequal result
byte1 only preserves the previous ba2c selection; the function still
returns1. The actual blocker is the unsupported result read before that
comparison, not a slot mismatch or a polling wait.

Kind23 supplies the fivec00-byte units at PA`[5d4b5000,5d4b8c00)` and the
three configured bases at offsets0/80/d0; the requested bb44 byte comes
from software descriptor+8. Its slot/index meaning remains unestablished.
These facts do not establish result values
or their update trigger. Unit20 is enabled/running, but its registered
software event callback does not produce the packet and the device has no
bound result producer. No model extension is justified by those observations.
Unchanged `groupresults/origin` again compares all six COPY ranges,
records guard0/property6433 empty, and stops at the same READ32
PA`2006bb70`, CPU0 PC`10208e14`, with no console. No cold-zero restoration,
requested-slot echo or later startup progress is claimed.

The later [active-cursor profile](../devices/capturegroup/protocol.md#finite-active-cursor-assumptions)
uses new source evidence: kind23 descriptor+8 is a software unit index,
resolved by`10299176` as base+(index%count)*unit. The actual caller is the
VD-dispatched`10325ff6 -> 103250f2`. The finite functional model latches
complete metadata at commit, then publishes/advances active target cursors
only on existing automatic TG rising boundaries after effective run. It
does not echo staging writes, recover cold zeros, perform DMA or produce a
completion IRQ. Result ordering, common cursor, bank interpretation and
first-edge phase are explicit assumptions. Native old-model red is followed
by33 Sensor and23 SLA cases green, including asymmetric addresses/strides,
nonzero start, wrap, staging isolation, forced-edge rejection and cancellation.

Unchanged `groupcursor/cursor` crosses the old bb70 error and completes
39 getters, including38 generated packets with indices0..4. All six COPY
ranges compare, guard0/property6433 empty remains confirmed, and the same
healthy UART peer reports no rejection. QMP remains running after20 host
seconds without an uncovered access; neither head has a console. The separate
`wait` observation confirms the same checks with33 completed getters.
CMOS frame/readout request counts remain0, so this is progress under the
active-address hypothesis, not captured data or normal GUI output. No new
MMIO stop is identified; the next task is to localize the absent image-request
condition. Gate/pending3/ready0 alone do not establish that condition.

The subsequent [readout producer audit](../runtime/readoutstate.md#current-cold-neutral-producer-and-completion-join)
finds the missed initial path: `10238876 -> 1023869e -> 1020c728` commits
mask2 then runs unit2 without a `b400` marker. The finite first-run/completion
profile closes that gap, snapshots descriptors, preserves busy ownership and
cancels on effective stop/reset/disable. Its first unchanged `model` run
requests once and rejects DMA to unmapped PA61654800. Source allocation
kind38/39 then establishes the complete adjacent three-/two-unit pools;
both are backed with exact shared RAM and explicit cold-zero assumptions.

Old-model native tests fail at first-run acceptance and readout-pool access;
the final batch passes12 CMOS,33 Sensor,31 memory and23 SLA cases. Unchanged
`readoutrequest/pools` records1656 accepted/1655 completed requests
and no rejection in20 host seconds. A second `consumer` run records1512
accepted/1511 completed; these QMP properties are sequential, not atomic.
Both remain running with no uncovered access or console; all six COPY ranges,
guard0 and empty property6433 remain confirmed. `consumer` reads nonzero
primary DMA bytes and an untouched cold-zero secondary prefix.

Original IRQ callback `102391b2`, owner `10238d4a` and marker consumer
`1023c610` now execute. In the focused `consumer` trace the owner receives
bit29 repeatedly but no bit2 (`4`), and `10238c40` is never entered. That
data branch needs capture raw0.bit6 (`40`) to increment `b1888164` and post
owner bit2. The model produces only actual mode1 primary-completion bit5,
not that separate data cause or secondary samples. This is the next concrete
readout-consumer boundary; its necessity for GUI and its actual data producer
still need tracing. Other producers do send global event041a0002, so that
event number alone is not proof of this owner branch. No normal-menu output
or physical frame rate is claimed.

The later [capture data audit](../runtime/capturedata.md) traces normal
submission `10235c84 -> 1023bc02`, the four queues, bank1 statistics and
global-scalar consumers, then041a0002/pending40000/041a0008 retirement.
The final cold-neutral `verified`/`routes` runs each have queue lengths
0/33/33/33:32 roots pending40000 and one40001. All33 roots checked by
`routes` have root[8]=root[16]=0; the upper image-delivery branch requires
root[16]!=0. Bit6 is enabled but unproduced, and the owner data consumer
is still absent. This identifies request retirement, not a proven GUI
main-image completion. Direct setter `103250f2 -> 10324f04(root+8)`
requires `b1868af6=1` and unequal `b1868b94/b1868b98` counters to set
root[16]; the `routes` snapshot has `b1868af6=0`, which would select the
clear branch. Its state at each request's setter call was not traced.

Complete source-declared pools17/19/20/21/22/23 now have exact RAM beside
the existing kind18 grid. Old-model DMA fails at the first new pool; all32
memory subtests pass after the change. The unchanged runs remain running
without uncovered accesses or UART rejection, with1662/1662 and1640/1640
sequential accepted/completed auxiliary counters. Both retain all six loader
COPY comparisons, guard0 and empty property6433, with no console. Storage
does not produce numerical data or completion, so the current boundary
was a coherent processing-data/result/bit6 producer; selecting the upper
image-delivery request is a separate unresolved upstream condition.
