# Image information and display activation

The cold-neutral checkpoint reaches GUI drawing and display-layer programming,
but has no active display target or observed publication through the traced
image-information subscriber.
The image-task branch rejects the surviving sequence phase before image DMA.
An upstream task cancels the eligible phase because two computed-input caches
have not been published in the observed windows. Short focused traces find a
full service callback queue, but longer runs consume the transition and empty
it. The finite model admits auxiliary transport on the existing automatic
input clock and now closes OnVd's FE synchronization through a qualified shared
acquisition notification. The old-cycle processing fence still fails. Actual
statistics work supplies its own stage8 completion; the other requested stages
remain distinct. AF6's state9 request is a separate conditional route;
forcing it is not justified as a normal GUI prerequisite.

Addresses below are original C1.70 MAS CPU VA unless explicitly marked PA.
Source is the unchanged MAS payload at base10020000, inspected in IDA;
unrecognized Thumb wrappers were decoded without changing the database.
The [published admission summary](/evidence/imagerequest/admission.json)
contains the critical observations and omissions. Raw disassembly, argv,
snapshots and traces remain private and are not shipped with the handbook.

## Image-information admission

The branch labels are source conditions. Only the early-exit branch was
observed for the 15 selector4 jobs in the `cadence` window; the remaining
path has further checks and is not demonstrated by a statistics completion.
All code addresses in these diagrams are MAS VAs.

```mermaid
flowchart TD
  Job["Image task 1026a312<br/>Observed type2 job"] --> Pair["103f6b90 reads selector pair<br/>Observed selector4: period 2, phase 0"]
  Pair --> Test{"103f6bb2<br/>period != 0 and sequence % period == phase?"}
  Test -->|"No: observed sequences 5, 7, ..., 33"| Exit["Return before image submission"]
  Test -->|"yes"| Work["Remaining checks, allocation and processing"]
  Work --> Mask{"Nonzero completion mask<br/>equals requested mask?"}
  Mask -->|"yes"| Event["041b0040 to capture adapter 103284d0"]
  Other["Other source-defined producers<br/>1026aa50 / 103cda56"] --> Event
  Event --> Builder["1035a2d8 builds image information"]
  Builder --> Registry["Registered callbacks forward 04380001<br/>106d103c through four subscriber slots"]
  Registry --> GUI["GUI subscriber 104be876<br/>Copy information; GUI record92 / primary80"]
```

The early-exit result explains these jobs only. Other job types and other
publishers retain their separate branches; the period/phase pair is software
data and is not a hardware-ready register.

## Verified producers and conditions

| Edge | Source condition and effect |
|---|---|
| Image task `1026a312` | For the observed type2, read selector byte at `job[3]+14` through `103f6b90`, then require `103f6bb2(job[0]-1,pair)`. A false result returns without executing the remaining image path. Other types have explicit bypass branches. |
| Admission helper `103f6bb2` | `period != 0 && sequence % period == phase`. Getter `103f6b90` copies two words from `b18a9540 + selector*8`; these are software data, not MMIO readiness. |
| Image-information event | After the remaining checks, allocation and processing in `1026a312`, the assembled packet must have a nonzero completion mask equal to the requested mask before callback `b186488c` receives `041b0040`. Other producers include `1026aa50` and `103cda56`; this task is not the only publisher. |
| Capture service adapter `103284d0` | For `041b0040`, call `1035a2d8(packet[266])`. Its separately observed `04180203` case does not publish image information. |
| Information builder `1035a2d8` | Construct the information payload and call `b1866d88(04380001,payload)` if registered. |
| Callback registration | `106d1232(2)` calls `1061d442 -> 1034ff68` with Thumb callback `106d103d`. Wrapper `106d103c` forwards `04380001` to the four callback slots at `b25d874c` through `1061d3ca -> 103e4b08`. |
| GUI subscriber `104be876` | Registered by `104bf1aa -> 10551b62 -> 106d1334`. On `04380001`, `1058027e` copies464hex bytes and calls `1057fdf8`; copy another464hex bytes to `b201c038`, publish GUI record92 and primary event80. |
| Optional switch completion | In the same subscriber, only when `10631c2c()` reads nonzero `b181b470`, clear that word and call `10631c7e`. The latter posts primary400 only if the switch-completion predicate `10631c52` succeeds. An information notification alone does not guarantee activation. |

The packet paths above are original software producers. A processing-statistics
completion, or a raw0.bit6 interrupt, is not interchangeable with `041b0040`.

## Verified display activation path

```mermaid
flowchart TD
  Info["04380001 reaches GUI subscriber"] --> Copy["Copy information and call 1057fdf8"]
  Copy --> Gate{"Valid information geometry<br/>and active, unpaused target?"}
  Gate -->|"yes"| Request["106b84ba builds upper image request"]
  Gate -->|"no"| Blocked["No upper request on this route"]
  Info -->|"Only if switch-pending and completion predicate pass"| Primary["10631c7e posts primary400"]
  Primary --> Phase["Phase18 dispatch<br/>105424e8 to 105409f2"]
  Phase -->|"Activation decision selects operation5"| Enable["105801ce stores requested mask<br/>and posts owner event4"]
  Enable --> Owner["105804fc enables target<br/>OR active mask; clear paused bits"]
  Owner --> Complete["Completion 05040003"]
  Owner -->|"Supplies target state, not image information"| Gate
```

An information event does not guarantee primary400 or activation. The diagram
shows the operation5 branch; policy operation0 and the separate owner-event80
activation handler remain distinct source paths described below. No activation
handler was observed in the focused window.

Primary400 dispatches `1053d0ec -> 106ddbfc(...,18) -> 105424e8 -> 105409f2`.
At its final activation decision, `10631f22()` true sends `104a94fe(5)`;
otherwise policy value3 from `104bde46()` sends operation0. Operation5 calls
`105801ce`, stores the requested target mask at `b181a574` and posts owner
event4. Handler `105804fc` enables the target through `10637aec`, ORs that
mask into active word `b181a56c`, clears its paused bits at `b181a570`, and
calls completion05040003. The owner's event80hex is another source-defined
activation handler; neither handler was observed in the focused window.

`1057fdf8` requires an active, unpaused target and valid information geometry
before it can reach `106b84ba`, which constructs the upper image request.
The active-mask test is therefore a concrete upstream condition on this route.

Mode entry `1053fe10:1053ffb0` also calls `1053fc46(10hex,0)`. This is a
selection refresh: `1063164a` compares old/new logical and raw selections.
In the observed logical0 state, command10hex changes neither, so returns0;
`1053fc46` skips the switch block and returns0. That block would send
operation4 and initiate switch synchronization/timer43. **Operation4 posts
owner event8, a disable operation, not the final enable.** Its absence does
not by itself explain why first activation has not occurred. The phase18
producer and image-information producer must be audited together; there is
no evidence permitting a forced initial enable mask.

## Observed cold-neutral boundary

`entry`, `producer`, `notify` and `cadence` each use the same cold-neutral
descriptor32, RNG0, healthy paced UART battery, held cold F4 and5 CPUs under
MTTCG. No extra key input or in-transaction debugger pause is used. Each
20-host-second window ends with a read-only snapshot. Trace filters differ;
an address outside a run's filter is not an observed absence. TCG chaining
also means entry observations are not exact call counts.

* `entry` observes GUI drawer10569c76 and layer writer1046d48a repeatedly.
  The latter's validator1047ed56 accepts the geometry and returns normally.
* Operations8,10,13 are observed. With property6433 empty, operation10 passes
  its policy checks and sends the original unpause; the historical ID68 skip
  does not explain this checkpoint. Operation5/phase18 and activation handlers
  are absent from their respective focused filters.
* Final active/requested-enable/paused words are all0. Target selection
  `b181b484=1` is present; pending switch words `b181b470/474/478` are0.
  Last pause/unpause masks `b181a57c/580=2` are not persistent pause state.
* At PA20145200, bank unit1 main control is13hex. Both port starts and layer
  enable words remain0. Valid layer parameter programming therefore does not
  prove an enabled scanout transaction; no rejected bank completion is found.
* `notify` and `cadence` observe image task1026a312, but no builder1035a2d8,
  forwarding wrapper106d103c or GUI subscriber104be876. The two observations
  of adapter103284d0 take the separate04180203 branch. Runtime callback
  pointers correctly contain106d103d,104be877 and103284d1 in their registries;
  missing registration is not the sampled problem. Both information buffers
  `b201c038` and `b1fa375c` remain entirely zero.
* In `cadence`, all15 observed admissions use selector4 and the pair `{2,0}`.
  The observed `job[0]-1` values are5,7,...,33. Each observed resume at
  1026a3e4 has return0, taking the early exit. This proves why these particular
  jobs do not publish; it does not prove all image producers have the same
  condition or establish a physical frame rate.

All six loader COPY ranges compare with the supplied descriptor32 input;
guard returns0 with version150, property6433 remains empty, QMP stays running,
and no uncovered access or battery-peer rejection appears. Sequential QMP
reads in `cadence` report1874/1873 accepted/completed readout requests and527
completed statistics jobs, with0 readout/statistics rejection and0 statistics
cancellation. There is still no display console. These are unchanged-firmware
functional-model receipts, not complete startup or physical-camera evidence.

The separate `keys` experiment retains the same data and held F4, then pulses
the remaining existing EX pad inputs through QMP. It observes no activation
or AF6 request and no console. It is not the no-extra-input baseline, and pad
identities are not established physical camera controls.

## Eligible jobs and the missing cache producer

The [phase and producer summary](/evidence/imagerequest/phase.json) extends the
earlier admission receipt with `cpu,nochain` observations. Disabling TCG chaining
does not reveal a hidden successful image-task admission. `103250f2` creates
both sequence parities; `103d0db0` assigns the same type2 task table to both.
The generic dispatcher `102994b4` skips remaining work after `job[1]` becomes
nonzero. Thus the queue does not initially select only the wrong parity.

The earlier step `102669aa` uses `103f6a60(selector4)` and the same `{2,0}`
cadence. For an eligible job it requires both `10272c9e(1)` and
`10272c9e(2)`. The first returns0 in all20 observed failed prerequisite checks;
the second is short-circuited. Store `10266a8c` sets `job[1]=1`. Opposite-phase
jobs bypass this work, survive to1026a312, then correctly fail its cadence test.
Changing the image phase would bypass the missing inputs, not repair their
producer.

| Source edge | Verified contract |
|---|---|
| `10272c9e -> 102de7f0` | Require both allocated storage at context+4 and a published flag at+20. Cache1/2 contexts are `b26c13b0/b26c13c8`. |
| `10272d20 -> 10272c7e -> 102de840` | Copy the supplied field, mark dirty and published under the original mutex. Cache1/2 lengths are10/11 bytes. |
| `10272d62` | Call the firmware calculation `10288560 -> 102d3264`, then publish cache1/2 through the source pointers stored at `b2725e00/b2725e04`. These are firmware-computed fields, not established opaque external records. Their complete input requirements remain a separate question. |
| `10273268` | Serialize a calculation request. A null callback computes inline; an asynchronous request wakes10272dc0 with bit2 and completes with041e000c. |
| `1022ef30 -> 10273268` | Synchronous producer after FECLCTRK completion041a000a, handled by1022f318. |
| `10235afe -> 10235e5c -> 10237d0e/10` | Submit FECLCTRK work when10235f3e accepts the root. Wrapper10237d0e is `MOV R1,R0`, falling through to10237d10. |

The read-only snapshot finds both caches allocated, with dirty/published flags0.
Caches3..6 are published, so this is not failure to initialize the entire cache
family. No10273268 or10272d62 entry is observed in their focused filters.

`102313ea` initially takes its bootstrap branch. Afterwards `b1887dc8=0`,
mode byte `b1887d72=0`, and metadata byte+10132=0 make it skip10235afe.
Another branch can setdc8 after exposure synchronization; `10230368` can also
set it after consuming a completed read queue entry. Neither justifies setting
the flag directly. Initial configuration10322886(0) constructs mode0 for
10232de6; zero is a software request, not a missing register readback.

## Callback backlog and observation limits

In the focused `join` window the skipped FE roots notify registered
`10327976(041a0002)`. It requests queued callback10326676 through10319d56.
That callback would call10323198(10,2); the original transition table maps
service state2/event10 to3. It is **not** observed executing. Registration and
the request are therefore present; consumption is the new boundary.

The queue at `b23ede14` holds60 records of92 bytes. Its final occupancy is60.
Of completed, matched enqueue observations, OnVd callback10325ff6 is accepted94
times and rejected442 times; callback10326676 is accepted twice and rejected31
times. `1022587c` returns0 on full, while10319d56 ignores that result and still
wakes the worker. The worker103191b2 has entered38 OnVd calls in this window.
The accepted transition requests can remain behind older work; the rejected
requests are not pending messages. Initial10232de6 returns in this trace, so
the backlog is not evidence of an indefinitely blocked initial configuration.

**Hypothesis:** the virtual input cadence and guest processing throughput can
prevent the service from catching up. The board's33333334ns input period is a
functional assumption. A separate100ms-period experiment empties the queue but
ends in service state10 with no image task or published cache1/2; a slower1s
attempt has no complete final snapshot. Neither validates a pacing repair.
Both temporary board changes were removed.

The later `steady` window uses the same baseline implementation `98fe5e02`,
default period and cold-neutral inputs for90 host seconds, with scoped
`cpu,nochain` tracing. It observes `10323198(10,2)` three times and finishes
in service state4 with queue occupancy0. The queue's `+38` offset from snapshot
base `b23edde8` is occupancy; `+3c=5` is a ring index, not five pending records.
Thus the focused backlog is real but not a permanent failure to consume the
transition. Caches1/2 remain unpublished and there is still no console.

A prior sample at CPU0 vector `10100008` was insufficient evidence of a fault:
the decoded original wrapper `10104a14..10104a24` invokes SVC6 through that
normal SVC vector. Older runs with CPU logging disabled failed the diagnostic
script's non-null descriptor precondition; they are not guest-fault receipts.
The corrected low-observation run below permits a null descriptor.

## OnVd waits and processing-stage ownership

The [coordination summary](/evidence/imagerequest/coordination.json) binds the
following observations to the original MAS and baseline `98fe5e02`. The OnVd
chain is `10325ff6 -> 103250f2:10325714 -> 10233570`. Many calls return;
sampling inside this function does not establish a permanently blocked call.

| Call inside OnVd | Original wait and observed boundary |
|---|---|
| `10233782` | `103611b2(b1887d9c,8,0,100)` generally succeeds. |
| `102339d8` | `103611b2(b1887d9c,5,0,100)` repeatedly fails. The initial mode1 mask1 succeeds. |
| `10233cc6 -> 10232154 -> 1023b00e:1023b016 -> 10238974` | `10361192(b188814c,05b00000,&out,200)` waits for selected old processing units. It fails, clears the requested bits and returns; the caller then proceeds to `1023aed8` configuration/commit. |

The literal100/200 values are timeout arguments; their time unit in this
extended RTOS ABI is not established. Matched wrapper entry/return observations
in `steady` record54 FE mask5 failures and54 unit mask05b00000 failures, each
with raw return `ffce0000`. The higher wrappers report0. Low-level event setters
for the FE handle publish8/1 repeatedly, but no bit4 is observed in this filter
and window. The unit handle receives10000000 and08000000, neither satisfying
05b00000. The remaining FE bit4 producer needs a source/runtime join.

The original ISR family identifies the required processing-stage causes.
All table values are hexadecimal; raw word indices are zero-based in the
eight-word capture packet. The event object is MAS VA `b188814c`.

| Software unit bit | Event flag | Capture cause | Callback VA |
|---|---|---|---|
| `1` | `100000` | raw word0 bit3 | `102391b2` |
| `2` | `200000` | raw word0 bit4 | `102391b2` |
| `4` | `400000` | raw word4 bit16 | `102392c6` |
| `8` | `800000` | raw word3 bit5 | `10239480` |
| `40` | `1000000` | raw word3 bit0 | `10239436` |
| `100` | `2000000` | raw word3 bit30 | `10239394` |
| `200` | `4000000` | raw word4 bit7 | `1023931c` |

These callbacks also mark processed units at `b26b38c4`; `102389dc` consults
that state before a stop fence. The wait precedes the new commit, so publishing
an ACK merely because configuration was accepted would reverse the lifecycle.
Software unit1 is required repeatedly although its initialization commit1 is
separate from later configuration; one commit cannot justify one universal
completion. Each unresolved cause needs its own actual work, result extent,
generation and cancellation contract. Existing ten-output statistics DMA and
raw0.bit6 do not complete every stage. Its source-associated stage8 completion
is now modelled separately below; the other causes are not filled in.

### FE mask semantics and the missing producer

The [fence audit](/evidence/imagerequest/fence.json), based on `f7383be5` and
the accompanying statistics change, resolves the original kernel contract.
Wrapper `103611b2` supplies wait mode20hex; handler `10111410` at
`10111608..1011165c` requires all requested bits when mode bit0 is clear.
Mode20 clears only those requested bits after success. Thus mask5 really
requires masks1 and4 together; “bit4” here means mask04, not bit index4.
Constructor `10361082` creates the event with pattern0 and attribute8.

The historical bounded source audit covers96 functions referenced by the nearby FE
global literals and direct setter calls in `1022de00..10236000`. It identifies
mask1/2/8/10/20 producers, but no mask4 producer. This is not a claim that no
indirect producer exists anywhere in the firmware. The30-second cold-neutral
baseline `stages` snapshot has FE handle01000038, pattern3b: mask1 is still
present while mask4 is absent. The changed-model `end` snapshot agrees.
An additional8-second snapshot rules out three concrete indirect aliases at
that sample: EX callback10206576 uses event-object pointer `b18a79e8` from
`b18a7a60`;104aee2e uses `b188bb98` from `b188bb9c`;1037156a uses handle
01000095 from `b180943c`. None is the FE object/handle. This does not exclude
other callbacks or later pointer changes.

At `102339dc`, immediately after the wait, the original code overwrites R0
from its stack rather than testing the wait result. The following branches
use independently prepared state, including the root and synchronization
conditions described above. This is a timeout-tolerant synchronization wait;
its failure does not alone establish a missing hardware-ready prerequisite.
The evidence also does not establish that it is deliberately unused or obsolete.
The later [notification audit](#notification-routing-and-actual-submission)
follows the indirect registration and shared tail, resolving the missing mask4
software producer. Its finite acquisition association is separately labelled;
processing-stage completion alone is not translated into this FE mask.

### Work and result ownership across the fence family

The observed reconfiguration request mask2eb selects software units
1/2/8/20/40/80/200. `10238974` maps only1/2/8/40/200 into05b00000;
the statistics-bank and auxiliary-readout units20/80 have separate lifecycles.
The complete seven-entry fence family is classified below. Values are
hexadecimal; board addresses are PAs and function addresses MAS VAs.

| Software unit | Configuration and actual result boundary |
|---|---|
| `1` | `1023aafe ->10238fbc ->1020c444`: optional front-end parameters at PA20068500/508..81c, initialization commit1/run1. These are separate from unit2's PA20069d00 packet. Existing mode0 auxiliary DMA can produce raw0.bit3 under its finite profile, but mode1 produces bit5; neither commits nor another stage's DMA justify replaying unit1's fence. |
| `2` | `1023a9d2 ->1020c3ec/c3fa`, commit4, run units4/8: front-end packet and controls. Configuration is retained; complete image transfer/result ownership is not established. |
| `4` | `1023a984 ->1020c6ac`, commit200: five fields at PA20069200..20069210. No result producer is implemented; this unit is not selected by the observed2eb request. |
| `8` | `1023a07e ->1020c862/c86c/c880/c894/c89e`, commit8/run10: existing ten output DMAs, twelve bank words and four scalars. This closes a specific actual-work contract; see below. |
| `40` | `10239dc6 ->1020c17a`, commit10/run20: eight group result words at PA2006bb70..2006bd7c. The finite automatic-TG model produces address/cursor metadata, not writes to the referenced image buffers; that does not prove a completed pixel stage. |
| `100` | `10239bcc ->1020c2c8`, commit100/run400: five-word PA20069500 record; result getter10209186 reads9534/9538/9544/954c. Those result meanings/production remain unimplemented; this unit is not selected by2eb. |
| `200` | `10239820` and initial1023994c through1020c2d4, commit80/run200: six-word PA20069514 record; getter102091a8 reads953c/9540/9548/9550. The normal kind3 incremental producer can return without rewriting its retained packet. Neither that no-op nor acceptance of the initial parameters establishes the four result fields. |

**Implemented functional hypothesis:** the unit8 configuration family is the
same family consumed by the existing statistics coordinator. Only after that
owned job's required DMAs and numerical results succeed does it publish
raw word3 bit5. Original `10239480` then owns processed8 and event800000.
This is an additional completion cause of real work, not an event on commit
or TG. The [statistics contract](../devices/statistics/protocol.md#processing-stage-completion)
defines its inherited ownership, cancellation and failure boundaries.

In the30-host-second `end` trace,735 auxiliary transfers and734 statistics
jobs complete. There are734 observed original event setters with object
`b188814c`, mask800000 and LR `102394c9`; the source-associated ISR therefore
consumes the newly modelled cause naturally. FE mask5 and the full05b00000
still return `ffce0000` in50 matched waits each. The model does not supply
the other requested causes100000/200000/1000000/4000000 merely to satisfy
the aggregate mask. Service ends in4 with queue occupancy0, caches1/2
unpublished, display masks0 and no console. No readout/statistics rejection,
statistics cancellation or battery-peer rejection is reported. This remains
the processing/input-publication boundary, not a new unsupported access or
normal GUI output. A final processed-shadow sample of0 does not negate the
observed earlier ISR/event setters.
The30-second guest-error-only `quiet` run completes725 auxiliary transfers and
724 statistics jobs with the same final boundary and zero reported rejections
or statistics cancellations. Its CPU setters are not measured. Different trace
filters preclude treating these counts as physical rates or speed comparisons.

## Front-end input and buffer boundaries

The [front-end receipt](/evidence/imagerequest/frontend.json) binds this
investigation to unchanged C1.70 and implementation `029ee3bd`. It does not
add a device model or establish the front-end pixel algorithm. The missing
edges are an owned front-end input, its actual processing outputs and the
separate group/paired-result update rules, rather than another rejected
configuration value. The existing auxiliary input has a finite LE16 profile
of8320 samples by34 rows; that alone does not identify the front-end stream.

### Unit1 and unit2 producers

`1023aafe` obtains a pointer to the23-word parameter-pointer array at MAS VA
`b23ed878` through `103981fa ->1026dbb0` when lane participation has mask6.
It copies that array, then `10238fbc` invokes only setters whose required
pointers are non-NULL. For example the14 software fields supplied through
`1020c46a ->102097fa` pack into PA `20068500`, shifted13 down to0.
This is optional configuration, not a pixel buffer. The dynamic path
`1023912c` copies another23-pointer packet, invokes the same conditional
setters, submits commit1 and optionally the companion command when packet+5c
is nonzero. Thus the initialization provider is not the only parameter source.
The physical operation of each enabled correction remains unresolved.

Software unit2's [packet producer](../devices/sensor/packet.md#software-unit2-input-boundary)
selects firmware ROM policy using software kind/subtype. Its two24/128-byte
copies and calculated coefficients are configuration inputs, not recovered
pixel samples or DMA addresses. Incremental `1023aed8` rewrites that packet
only when the kind byte differs from cached `b1888142`; otherwise it still
includes shared commit4 in the later `1020bfaa` call. An accepted commit is
therefore neither evidence of a new packet nor evidence of completed data.

In both30-host-second windows, unit1 control PA `20068500` is0. In `flow`,
the only nonzero parameter in PA `[20068508,20068820)` is `20068708=1`;
in `input`, that entire extent is0. These snapshots do not prove that all
front-end work is disabled: the source has not identified a complete bypass
or input-consumption rule for that control word. The second snapshot's
initialization pointer array is also all0; it cannot exclude earlier pointers
or the separate dynamic packets observed by the CPU trace.

The packet snapshots differ: d00/d04/d08 are
`03960000/0ac80000/13f0` in `flow` and `003a0000/20600000/3f0` in `input`.
The former's high halves numerically agree with the paired parameter sizes,
including `ac8 = 2*564`; this is not enough to assign physical format, crop,
stride or a resize algorithm. In particular these two retained snapshots
must not be treated as one constant geometry.

The raw0.bit0 branch of `102391b2` separately posts owner mask1. Owner
`10238d4a` then calls its registered input notification with041a000f. That
software edge is distinct from raw0.bit3/4 and their unit1/2 completion flags.
Neither window observes owner mask1 in its full owner-entry filter; both do
observe auxiliary owner20000000 and statistics owner4. Current auxiliary
transport therefore does not demonstrate the main input-notification join.

### Group buffers and paired results

Kind23's source construction establishes a five-unit ring with unit sizec00
and three interior addresses at offsets0/80/d0. Crucially, `1030b630` clears
the **software descriptor's**98hex bytes at+14, not the unit payload.
`1030b3bc` supplies geometry metadata to descriptor+96 and a separate stack
record; the following hook `102cec72` is an original `BX LR`. The enqueue
path `102cd9a0` and group setup `10239e36` also do not access payload bytes.
Neither input-table direction nor image-output direction follows from those
constructors. The current cursor model generates addresses only.

Both final physical reads find all bytes of PA `[5d4b5000,5d4b8c00)` zero.
The `flow` snapshot still has kind23 handle170000; `input` has handle0, so its
read is not evidence of an active allocation. Neither read establishes a hardware
reset value, a successful clear or zero-valued processing results. The next
discriminating source edge is a real load/store through descriptor+14/+18/+1c,
or an independently identified hardware transfer into/out of those addresses.

The paired four-field result APIs are separate again. Wrapper `1020c32a`
tail-calls `102091a8`, which reads PA953c/9540/9548/9550 (common prefix2006)
with32-bit accesses and copies u32/u32/u16/u32 to output offsets0/4/8/c.
Wrapper `1020c320` selects the
other record's getter. Neither wrapper nor getter executes in these two
focused windows. Their source-established control/commit/run gates do not
establish the numerical result producer or prove these reads currently block
GUI startup. They remain errors; no cold-zero or identity result is supplied.

`flow` completes813 auxiliary transfers and812 statistics jobs; `input`
completes742/741. Both report zero readout/statistics rejection, statistics
cancellation and battery-peer rejection, with no display console. Their final
lane participation/run masks differ (`2/2bf` versus `4/1d`); observation filters
and MTTCG scheduling affect progress. These are two bounded observations,
not proof of a single steady state or complete runtime stability. They do not
repeat the earlier six-range loader COPY comparison. No new completion,
software event, frame, firmware patch or memory aperture is introduced.

### Explicit scene input through actual DMA and statistics

The subsequent [scene-input receipt](/evidence/imagerequest/scene.json) closes
the existing auxiliary input's file/geometry/encoding contract. The
[image profile](../inputs/camera/image.md) now binds an explicit source without
fixture bytes, preserves raster whitespace, retains a scene until explicit
replacement, and maps it through guest sensor/crop coordinates. C170 output
is functional LE16 grayscale; a PPM is converted to luma rather than placing
R/G bytes into one unrelated16-bit sample. These choices are implemented and
tested; they do not identify a physical sensor or unit1/2 algorithm.

In the60-host-second `scene` original-firmware run, the same cold-neutral
descriptor32 is combined with an explicit2x2 PGM containing32/192/64/224.
The actual readout is8320x34. QOM reports provider-bound true and
fixture-provider false. Four CPU-VA reads at the addresses of the submitted
primary buffer contain `2020/c0c0/4040/e0e0`; the four actual statistics scalars
are the same numerical values. There are814 completed auxiliary transfers and
813 statistics jobs, with no reported rejection, statistics cancellation or
battery-peer rejection. This is data reaching the existing model through
normal requested DMA and computation, not host-prefilled output memory.

The separate30-second `control` uses the default deterministic source and
completes812/811. Both windows finish with empty service queues and unpublished
cache1/2; neither has a console. The scene filter observes no `10230368`
completed-read consumer, `10273268` calculation request, `10272d62` cache
calculation, `1035a2d8` information builder or `105801ce` enable entry.
The stable scene therefore does not repair the missing front-end/calculation
join. Counts from different durations are not a rate or stability comparison.
No additional stage, FE flag or display enable is synthesized by this change.

### Notification routing and actual submission

The [notification audit](/evidence/imagerequest/notification.json) separates
three status/worker namespaces on baseline `9594e980`, and validates the
accompanying finite input-synchronization model with unchanged C1.70 and the
same cold-neutral scene input. Addresses below are
MAS VAs except the explicitly labelled board PAs; identifiers and masks are
hexadecimal. These are source-established software routes, not recovered
physical pixel-event definitions.

| Capture status and owner | Original callback | FE worker effect |
|---|---|---|
| Raw word 0 bit 0 -> owner `1` | `10238d4a -> b188816c(041a000f,0) ->1022f318` | Shared tail `1022f526 ->1022f37c` sets mask `4` on FE wait object `b1887d9c`. This is the previously unresolved half of the OnVd mask `5` AND wait, not a direct FECLCTRK submission or cache1/2 publication. |
| Raw word 0 bit 7 -> owner `10` | `10238d4a ->1023c6ec -> b1888168(041a0001,root)` when lane flag bit0 selects the queue path | `1022e2e0` posts `10000`, dispatched to `1022ff20`. This is **not** worker bit `8` and does not directly call `10230368`. Other `1023c6ec` branches can send `041a0004` instead. |
| FE worker bit `8` | Producer not closed by this audit | `1023162c ->10230368` consumes queue `b25eebc4`; only a nonempty root reaches its stop/callback path and sets synchronization latch `b1887dc8=1`. No row above establishes the producer of this bit. |

The worker-`20` path through `10235944` belongs instead to notification
`04400044` with argument low byte `21`. It clears auxiliary `80` participation
before testing transition state. Its shared instruction tail must not be
confused with `041a000f`. The raw bit0 route establishes the missing FE mask4
**software producer**, but not the hardware work that should originate it.

`102389dc` explicitly adds raw word 0 mask `80` to the normal `79` packet
on its guarded retirement path. The input notification and this retirement
notification therefore must not be conflated, or supplied after every auxiliary
DMA. In the instrumented active configuration the raw word 0 mask is
`10000079`; at the final changed-lane snapshot it is `10000020`. A missing
owner entry alone does not establish that its cause should be enabled and
produced in every phase.

The submission side is positively identified. `1022fca6 ->1023ad8e(1)` first
initializes unit1 with an empty eight-entry stage-input packet, calls
`1023aafe ->1020c444`, submits both commit commands with mask `1`, and requests
physical run mask `1` (bit0). Dynamic `1023912c` updates follow. The probes see unit1
control PA `20068500=0` and a temporary parameter PA `20068708=1`, later
overwritten to zero. This does not establish a complete bypass or a no-work
mode.

`10232040` subsequently adds software mask `2ea`. Unit2's actual kind/subtype
is `3/0`; its ROM-selected policy reaches the packet writers and both mask `4`
commit commands before physical run masks `8` and `4`. Thus the normal producer
is executing, rather than waiting for a host-supplied packet. Later
`1023b00e(2eb)` calls the old-cycle fence with clear requested, then falls
through to `1023aed8` **without branching on its return**. That updater can
retain unit2's packet when the kind is unchanged, updates the other selected
units and issues shared commit `9e` through `1020bfaa`. The pause-free trace
observes that commit and the normal auxiliary/statistics owners. A fence
timeout by itself is not a sufficient explanation for absent new submissions.

A separate read-only breakpoint probe samples eight selected roots, sequences
4 through 11, at `102314e6`. Each has `b1887db0=0`, `b1887dc0=0`,
`b1887dc8=0`, mode byte `b1887d72=0` and metadata byte `+10132=0`.
This is the exact source branch that bypasses `10235afe`, rather than a
calculation blocked inside unit1/2. In sequences 5 through 11 the two expected
and observed exposure pairs at metadata `+11580/+11856` and `+11588/+11864`
already match. The preceding mode/metadata guard prevents those comparisons
from being attempted in this branch; matching them does not set the latch.
This probe does not establish which upstream request should change that guard.

The pause-free 40-host-second `join` window completes 733 auxiliary DMAs and
732 statistics jobs. There are no reported transport/statistics rejections,
statistics cancellations or battery-peer rejections. The final service queue
is empty, caches1/2 remain unpublished, and neither display head has a console.
The focused filter sees no `10230368`, `10235afe`, `10273268`, `10272d62` or
`1035a2d8` entry. Three additional 40-second breakpoint windows establish the
configuration and root fields above; their scheduling is instrumented and is
not pooled with the pause-free observation.

**Implemented functional hypothesis:** a shared virtual acquisition delivers
the auxiliary samples and an input-synchronization observation. When physical
capture run bit0 is high at successful CMOS submission on the fresh automatic
TG boundary, that generation can publish raw0.bit0 after its actual DMA,
snapshot and readout completion succeed. A falling run bit permanently revokes
that generation's qualification without cancelling its still-valid auxiliary
DMA. A later start does not qualify input already being acquired. Reservation,
commit, TG alone, unbound input, cancellation/reset and premature or stale DONE
do not publish it. The provider now returns an actual CMOS generation token;
DONE must match that token and the completed DMA phase. This also fixes the
previous premature-DONE acceptance boundary.

This explicitly associates one virtual camera acquisition with two consumers;
it does not recover the physical origin of raw0.bit0, identify a main-stream
format or produce unit1/2 processing results. Mask/GIC/W1C and original ISR/
owner/callback code deliver the notification. No host code sets the FE event.
The [device contract](../devices/sensor/protocol.md#shared-acquisition-input-synchronization)
defines the model and native boundaries.

The changed-model 40-second `sync` window completes 375 auxiliary DMAs and
374 statistics jobs, with 375 qualified input status publications. The trace
now observes owner1 and the original FE mask4 setter with LR `10238d65`.
All 41 observed OnVd resume points at `102339dc` have wrapper return1, instead
of the historical failed mask5 waits. That closes this synchronization edge
through normal guest code. It does not prove the physical association: this
is acceptance of the declared functional model, not camera measurement.

The next processing boundary remains explicit. One unit1-only fence
(`100000`) and 39 aggregate fences (`05b00000`) return0 in the same trace.
The observed data-stage setter is still only `800000` from `102394c9`.
Cache1/2 remain unpublished, with no `10235afe`, `10273268`, `10272d62`,
`1035a2d8` or first-enable entry observed. Both heads still lack a console.
The trace is much wider than `join`; its counters are not a throughput
comparison. Rejection, statistics cancellation and peer rejection remain0.
A separate 40-second guest-error-only `steady` window completes 345 auxiliary
DMAs and 345 statistics jobs, with 345 qualified input status publications.
It also has no reported rejection/cancellation, no console and unpublished
caches1/2. This verifies the low-observation boundary without measuring FE wait
returns or establishing complete runtime stability.

**Unresolved processing boundary:** unit1/2's actual stream operation and owned
outputs are not identified by this input-synchronization model. In particular,
control zero is not yet a proven whole-stage bypass, and the source does not
associate raw bit7 with the unwritten secondary buffer. Group and paired-result
work remains separate. The next discriminating evidence is a bounded streaming
input/output operation for the submitted stages and the normal guard transition
that admits FECLCTRK. No new external-record or real-camera requirement has been
established. Those unknown completions are not supplied by the new input sync.

### Actual stream work after qualified input

The subsequent [grayscale stream contract](../devices/capturework/protocol.md)
joins unit1/2 and paired flag200 through real output DMA. Source producers
identify disabled correction flags, ROM geometry,16/14/12-bit byte strides,
four allocated kind16 outputs, commit masks1/4/80 and effective run masks
1/4/8/200. Identity forwarding, nearest grayscale sampling, LUT indexing and
little-endian sample packing remain explicit finite functional assumptions.
The actual payload is written before the three stage causes are published;
neither input synchronization nor statistics supplies their ACKs.

The [stream receipt](/evidence/imagerequest/stream.json) records a40-second
final unchanged cold-neutral execution:388 complete stream jobs,737300880 payload
bytes, and original fence setters100000/200000/4000000. The unit1-only wait
now succeeds. Aggregate05b00000 still fails; of its requested stages,
**group1000000 alone lacks an observed setter**. All36 observed OnVd mask5
resumes succeed. Cache1/2 and the normal information/first-enable chain remain
unpublished, with no console. These are functional model observations, not
physical-camera validation.

Group remains an independently open payload contract. Kind23's three interior
addresses and cursor metadata do not establish processing: descriptor
construction clears152 bytes of metadata, not the C00-hex payload. The current
`1030b630 ->102cec72` hook is a no-op; the inspected force0 getter consumes
only the index comparison, not numeric payload contents. No independently
recovered packet layout/arithmetic permits treating the three addresses as
completed data. The remaining evidence needed is the actual payload consumer
or a same-architecture producer defining those fields and the source-to-output
operation. Neither an all-zero payload, a matching index, nor a completed
paired stream qualifies group's raw3.bit0. It is not emitted by this change.

## Acquisition on the normal input clock

Previously the auxiliary producer submitted CMOS immediately after every
readout request. Its real DMA completion caused the original owner to request
again, independently of the input cadence. The configured1ns readout quantum
applies per at-most4096-byte chunk without crossing a row, not per frame. The
observed stride4100/span8a200 profile has34 rows and170 chunks: its ideal engine
latency was171ns including exposure, without claiming physical sensor timing.

**Functional hypothesis, implemented:** a valid request reserves its immutable
generation immediately, then consumes the next fresh automatic TG rising edge.
Earlier/busy edges are discarded, and forced router levels cannot acquire an
input. Actual timed CMOS DMA still produces completion. The existing33333334ns
TG period is unchanged. Processing at the same edge sees the previous completed
input, while the newly admitted transfer finishes later. The
[device contract](../devices/sensor/protocol.md#buffer-selectors-and-readout-parameters)
defines ownership, errors and cancellation.

| Original-firmware window | Completed auxiliary DMA | Completed statistics | Final service / queue occupancy |
|---|---|---|---|
| Baseline `onvd`,20s, focused CPU trace | 1999 | 523 | 2 /60 |
| Input-clock `clocked`,20s, same focused filter | 519 | 518 | 2 /60 |
| Baseline `steady`,90s, expanded CPU/nochain filter | 2808 | 829 | 4 /0 |
| Input-clock `coordinated`,90s, narrower CPU/nochain filter | 739 | 738 | 4 /0 |
| Input-clock `quiet`,30s, guest-error logging only | 818 | 817 | 4 /0 |

Windows are host observation time; filters and resulting guest throughput
differ, so these counts are not physical rates or a speed benchmark. All have
zero reported readout/statistics rejection and statistics cancellation, no
battery-peer rejection, and no display console. In `coordinated`, the FE mask5
wrapper returns0 at56 observed resume points, and the unit fence returns0 at55;
TCG positions are not distinct-call counts. Its three observed transition
requests use `(10,2)`. The low-observation run also empties the queue naturally.
Both leave caches1/2 unpublished and display masks0. This validates bounded
input/transport coordination, not complete runtime stability or normal GUI
startup. Native validation separately passes17 CMOS,33 Sensor and1 readout-unit
case, including a new input-boundary fixture that fails on the old model.

## Additional first-enable producers

Phase18 is not the only source-defined enable route. Initial mode entry
1053fe10 can post primary8000 at10540502. `1053d0ec` maps that bit to phase17,
whose10540be2 handler can reach operation5 after its target/policy guards.
In the focused window the calculated mask is1, selected target is already1,
and1063192c returns0; the code bypasses the8000 post. No phase17 entry occurs.
This is an intentional no-change decision, not a failed hardware ACK.

A distinct direct route is primary4 -> phase5 ->105413d6. Its
`104bf2ee(ed)` result must be nonzero before10541578 reads property6433.
With an empty list and flag42 clear it then sends operation5 at105415a8.
The property-to-index record at MAS VA12449ff0 maps6433 to index `ed`; the lookup
uses its RAM table atb2bb1080. Property callbacks104be4ea/104be5fa map a
notification to this index, call104bd90e, then post primary4. `104bd90e`
requires the index enabled and not already pending;104bf2ee consumes the flag.
An empty property value alone therefore does not request an enable.

In `join`, phase5 executes repeatedly, but all99 observations at10541578
haveR4=0, bypassing this direct enable. Policy is not permanently0:
104be90a receives0410000b and writes1 to b181c99c. This rules out explaining
the entire window by the earlier policy0 observation. A further focused
`enable` window confirms the runtime6433 ->ed mapping, index-enabled byte1,
pending byte0, no104bd90e(ed), and no1035ed5c entry. That helper's clear-list
arm can write an empty6433 through1034ffe0; one source route is capseq worker
bit20000000 from103977f6's successful error-clear branch. It has its own
guards and is not established as a mandatory initial-startup request.
There is also an image-information route that does not require a selection or
property6433 change. Builder `1035a2d8` publishes through registered04380001 to
`104be876`. The subscriber copies464hex bytes to `b201c038`, queues record92
through `104bd99e`, and posts primary80. Phase5 `10542486 -> 1053f42a` consumes
record92 through `104bdde0(92,&pointer)`. At `1053f7a0`, it compares packet
offset1016decimal (`3f8` hex) against the saved word at `b202c970`. A change
posts record132; a new nonzero value additionally posts133. After either zero
or nonzero change, `105b0a7e()==0` permits `104a94fe(5)` at `1053f7c0`.
That guard reads `b1821e81` and is1 only in
states1/4. The builder writes this packet field from root[6] at `b23cd5c0`;
its full semantic identity remains opaque, so it is not labelled image-valid.

In `steady`, phase5 is active but no successful record92-consumption resume
at `1053f446` or downstream comparison/enable is observed. `coordinated` also
observes no record92-success resume or operation5; its three `104a94fe` entries
use operations8/10/13decimal. This is a concrete normal candidate for first
enable, conditional on genuine image-information publication and its policy
guard. It is not proof of a unique mandatory first-enable path. The next join
must recover the processing inputs/completions before that publication, rather
than posting a notification or activating a target from the host.

## AF6 relationship and next evidence

The [AF6 producer audit](../devices/statistics/protocol.md#separate-software-image-request-condition)
establishes `103f27ba -> 10328ca6 -> 10323a7e`: service state9 and
property1053=1 guard the queued request that sets AF6=1. `10324f04` then sets
root[16] only while requested/produced counters differ. Root[8] separately
requires B0C=2. This window still has service state2, AF6=0 and B0C=0, with
neither AF6 API nor worker observed. Those gates explain that route's absence.
The normal information/display functions above have their own guards; they
do not test AF6 at the identified edges. A shared earlier dependency remains
possible, but no evidence establishes AF6/state9 as their required producer.

The eligible-job cancellation is now established; the earlier conjecture of
an unexplained parity selector is superseded by the cache/queue evidence above.
The statistics arithmetic remains unchanged; the accepted acquisition boundary
is documented above. The historical81-case native receipt is separate from
the51-case coordination batch, original-firmware observations and unsuccessful
period experiments.
