# Capture-group configuration and functional statistics

The finite device retains144 source-identified configuration words at board
PAs`[2006b800,2006bd84)`. Its one SysBus region is exactly584 hex bytes;
all holes within that interval remain transaction errors. Eight read-only
result words have the cold-empty and automatic-TG cursor profiles below. Once
actually published, the packet remains latched across generator lifecycle
changes until another publication or whole-model reset. This retention is an
explicit functional hypothesis. There
is no readiness or completion implied by this metadata. In the bundle
composition, a separate owned job computes the finite RAW statistics profile
below and publishes completion only after its three output families reach RAM.
Its arithmetic is a declared reference operator, not recovered ASIC arithmetic.

## Original producers and masks

Original MAS source projections, diagnostic execution and native receipts
are scoped separately under`run capturegroup/`. Addresses in
the table are board PAs, with the common prefix2006 omitted. Ranges step by4
unless stated otherwise. Masks bound complete values constructed by the
identified software; they are not recovered physical IP permissions.

| Words | Accepted bits | Original MAS writer |
|---|---|---|
| `b800/b804/b81c` | `ff` | `10208e96`, separate byte fields |
| `b808..b818` | `ffff` | `10208e96`, five halfwords |
| Three8-word rows at`b820/b840/b860`: first word | `ff` | `10208cb0` |
| Same rows: remaining seven words | `7f` | `10208cb0`, masked byte stores |
| `b880..b980`,65 words | `ffff` | `10208db6`/wrapper`1020c14a` |
| `b984` | Full32-bit | Original unmasked three-argument packing API |
| `ba00/ba04/ba08/ba0c` | `ffff` | `10208cf4` |
| `ba10` | Full32-bit | `10208f1e`/wrapper`1020c1e2` |
| `ba14..ba28`, six words | `ff` | `10208cf4` |
| `ba2c` | Full32-bit | `10208f12`/wrapper`1020c1d4` |
| `ba30` | Only0/1 | `10208ed0`, known count0/1 packet footprint |
| `ba34..ba50`, eight words | `ffff` | `10208ed0 -> 10208f26` |
| `ba94` | `ffff` | `10208cf4` |
| `ba98/ba9c` | `7fffff` | `10208cf4`, halfword OR byte shifted by15 |
| `baa0` | Full32-bit | `10208cf4`, complete word payload |
| `baa4/baa8/baac` | `ffff` | `10208cf4` |
| `bab0/bab4/bab8` | `ff` | `10208cf4` |
| `bb18/bb24/bb44/bb48/bb4c` | `ff` | Byte setters in`10208dd8..10208e96` |
| `bb3c` | Full32-bit | `10208e76`/wrapper`1020c194` |
| `bb50/bc50/bd50`, `bb60/bc60/bd60` | Full32-bit | `10208e4a`/wrapper`1020c186` |
| `bb80/bc80/bd80` | Full32-bit | `10208dd8` |

The isolated b984 setter has no identified static caller in the scoped
analysis. Its unmasked API establishes only raw configuration retention;
it supplies no evidence for side effects or a result at the neighboring b988.
The latter remains unsupported.

The ba10 API stores its original32-bit argument directly, without truncation
or masking. A current upstream halfword value does not narrow that API's
accepted payload. Likewise ba2c stores `a2 | (a1 << 1)` without masks; the
current caller's boolean values0..3 do not justify a whitelist for3. Bb3c and
all opaque address words also preserve their complete input. No bit in these
words is promoted to a run, enable, ready or completion action.

## Count and retained packet semantics

`10208ed0` computes `(1 << count) - 1` at ba30 and emits parameter records through
the four-halfword helper`10208f26`. The identified successful provider path
`102aa02a` supplies count1 at output+340, whose two four-word writes cover
exactly ba34..ba50. A NULL provider can return without filling the output;
the wrapper may
reuse an older buffer, so that observation does not establish arbitrary
counts, more record locations, or clear-on-disable behavior. The finite
contract therefore accepts only masks0/1, representing count0/1. Other masks remain rejected;
ba54..ba90 remains unknown rather than receiving a speculative wider array.

Count0 produces no subsequent payload stores in the original loop. Writing
ba30=0 consequently preserves all eight retained halfword parameters. More
generally each accepted MMIO store replaces only that word: there is no
implicit batch reset, adjacent-field clearing or auto-population of omitted
fields.

`10239e36` prepares the group through the ordinary configuration wrappers
and obtains three provider slots through`1030b730`, `1030b764` and`1030b7b4`.
The two address sets and extra words at bb/bc/bd are independent retained
payloads. The later provider investigation below identifies their software
allocation, but not physical result or DMA semantics. The metadata contract
does not itself access RAM; the functional processor separately validates
these destinations before computing and writing records.

`10239dc6` reads the existing result packet through`1020c17a`, conditionally
updates ba2c and then writes ba10 through`1020c1e2`. Its comparison involving
the returned bb74 byte is not a producer of that byte. The model must not
return a matching byte to force this caller forward.

The full configuration branch is `1023abaa` with flag40 and a non-NULL
pointer-array slot4. It configures shared interrupt masks for unit20 through
`1020c01a -> 1020ab7c`, calls
`10239e36`, then calls `10239dc6` with force1. Force bypasses the descriptor-byte
comparison, but still reads the complete old result packet first. Only after
that call returns does `1020c07e` submit mask10 to the shared command ports
at20068100/104 and call `10208c46 -> 1020aafc` to request run for unit20
at20068008. This last call is run, not reset. The incremental branch through
`1023aed8` calls the same result consumer with force0 before its later shared
commit. Neither path establishes a result producer at bb70..bd7c.

## Result consumers and control order: source facts

All accesses must be aligned little-endian32-bit. Unknown widths, offsets
and upper bits reject before mutation. Known configuration reads return the
last accepted value. Reset and unrealize clear that retained configuration
under an explicit functional cold-zero/readback convention; physical reset
values and universal physical register readability are unestablished.

`10208e0a`/wrapper`1020c17a` consumes the complete28-byte packet:

| Board PA | Read width | Destination |
|---|---|---|
| `2006bb70` | 32-bit | Low byte to output+0 |
| `2006bb74` | 32-bit | Low byte to output+1 |
| `2006bb78/bc78/bd78` | 32-bit each | Full words to output+4/+8/+c |
| `2006bb7c/bc7c/bd7c` | 32-bit each | Full words to output+10/+14/+18 |

Raw instructions establish one read of each register: MAS`10208e44` loads
bd7c once, and`10208e46` stores that loaded value. The preceding decompiler
projection incorrectly printed two memory reads. The independently matched
SLA getter`1009b3c2..1009b400` has the same packet and single-read sequence.
Neither sequence proves read-to-consume semantics. In the recovered direct
call graph, `10239dc6` uses only output byte1 for its descriptor-byte
comparison against MAS VA`b26b47cb`. The requested byte also reaches bb44,
but a desired/applied slot relationship between bb44 and bb74 remains a
hypothesis, not a recovered producer. Neither a zero-valued requested byte
nor force1 establishes zero hardware results. Other packet words are not
numerically consumed by this caller. Force1 still performs all reads before
bypassing the comparison, so its success cannot validate their numeric values.

The narrow original `captureresults/red` trace confirms
`1020c050 -> 1020a50c(unit20,0/1)` before configuration, followed by
`1020a518(unit20,1)`. The former writes shared reset set/clear ports00/04;
the latter writes enable port10. No group commit or unit20 run precedes the
first force1 getter. `1020c01a` is mask configuration, not a result producer.
The getter precedes both command writes100/104 with mask10 and run08 with
unit20. Software alone does not distinguish result updates at configuration,
commit or a subsequent processing boundary.

No writer of the eight results or complete hardware result producer has been
recovered. The direct getter graph remains
`10239dc6 -> 1020c17a -> 10208e0a`; its known callers are full configuration
`1023abaa` and incremental `1023aed8`. Lack of another static caller does
not rule out dynamic callers. These facts bound a cold contract without
recovering physical reset values or a post-run snapshot.

## Cold-empty profile: functional assumptions

The model defines an initially readable empty snapshot, with all eight words
zero. This is an explicit functional cold-reset convention, supported by the
read-before-first-submit caller contract, **not measured hardware values,
applied identity, readiness or completed processing**. It is independent of
the requested identity, the cold-neutral file and whether software uses force.
There is no post-submit zero provider.

| Event | Entire eight-word packet |
|---|---|
| Model device/system cold reset | Readable empty snapshot |
| Configuration write, unrelated commit, rejected access or inhibited run | Preserve current qualification and published packet |
| Either shared command100/104 containing mask10 | Expire cold-empty; retain any actually published packet |
| Effective unit20 run after existing reset/enable gates | Expire cold-empty; retain any actually published packet |
| Stop, disable or shared unit reset | Cancel active cursor; unknown stays unknown, published packet stays latched |
| Qualified automatic TG rise after commit/run | Publish the functional active-cursor packet described below |
| Aligned32-bit read | Return the qualified cold/cursor packet; otherwise transaction error |
| Write, wrong width or unaligned access | Transaction error without state mutation |

The model distinguishes cold-empty, unknown and actually published states.
Command/run invalidates only cold-empty knowledge; neither transition can
create a result or clear the last actual publication. Only an explicit model
cold reset opens a new empty epoch; shared reset00/04 does not invent a result
reset law. The later cursor profile qualifies a non-cold packet without
reopening that epoch. The earlier expire-on-every-command hypothesis is
superseded by the published-result lifecycle below.

The machine wires shared command-data bit4 and command-publish for both ports
to the group. Capture-control's synchronous `run-state[0..11]` outputs mirror
effective run intent, and bit5 feeds the group. Those are functional lifecycle
connections, not physical pins or GIC lines. Publication precedes return from
the initiating MMIO transaction. The automatic TG input is wired separately
from these lifecycle connections. The functional statistics job owns a virtual
computation timer.

Configuring this group does not trigger the shared stop fence as a normal data
result. The bundle processor consumes completed MAIN input and publishes
through the existing capture-control IRQ routing.
Readout admission and CMOS observation completion likewise have no recovered
mapping to this result packet. A post-submit producer still needs the eight
field meanings and actual processing/update trigger to claim physical
results. The finite cursor assumptions below are a separate contract.
All201 other aligned holes, including b988 and ba54..ba90, remain errors.

## Validation and execution scope

The original diagnostic red uses unchanged firmware and an explicit
functional resource80 input supplied through the original NOR loader. Only
its cold11/subtype3 branch is qualified; it is not factory data, complete
normal startup or a normal GUI receipt. The saved final diagnostic snapshot
is read-only after20 host seconds. Such progress is reported separately from
native synthetic protocol acceptance.

The parent's scoped `native-group-red.log` writes b800=ff in the new fixture
and reads0 from the old uncovered model. The fixture
`/arm/nikon/sensor/capture/group` covers all144 fields across four independent
data patterns, every bounded field's unknown upper bits, high raw32 payloads,
three independent opaque address sets, count0 retention and unsupported masks
(including encoded count2/3) rejection, byte/halfword/unaligned accesses, both aperture boundaries, reset,
and absence of capture run intent, pending status, IRQs, readout requests or
RAM changes after virtual time advances. HMP physical reads assert an actual
MemTxResult error at all209 unknown aligned words before and after reset;
a zero-filled qtest error buffer is not treated as a successful status read.
The parent's `native-group-green.log` records all30 Sensor subtests passing.
This native synthetic receipt is separate from the following execution.

The unchanged20-host-second `green` run uses the same generated cold-neutral
file. All six original loader COPY ranges compare with it; version guard
`10305dac` returns0 at `102d7254`, packed150, and property6433 remains empty.
Request flags2ea enter `1023abaa`, then `10239e36(b1887ef8,0)` configures this
group. Its b800/b804 values8 and three bb/bc/bd80 valuesc00 are retained;
the packet count is1 and requested identity is0. Execution then calls
`10239dc6(b1887ef8,1) -> 1020c17a -> 10208e0a` and first rejects CPU0
PC`10208e14`, READ32 PA`2006bb70`. The original ba30/count writer has not yet
executed, so the model's retained ba30 is still cold0 despite the source
packet's count1. No unit20 entry into `1020aafc` occurs in the scoped trace.
The source order places its configuration commit/run after the blocked
result read, not before it. QMP reports debug/stopped and neither display
head has a console. This crosses the configuration frontier under declared
functional assumptions; it does not establish normal GUI output or complete
boot. Raw source projections, trace, read-only snapshot and proof remain in
`run capturegroup/`.

## Cold-result validation and later frontier

The new `/arm/nikon/sensor/capture/group-results` fixture first fails on the
old model's actual HMP memory error at bb70 (`native-results-red.log`). After
the profile and shared lifecycle wiring, all31 Sensor subtests pass in
`native-results-green.log`. A final scoped case also checks an unrelated
effective unit leaves the packet qualified. The fixture reads all eight
words through HMP to distinguish readable zero from an error buffer; tests
read-only/non-consuming cold reads, no identity mirroring, unrelated commands,
invalid widths/masks, inhibited and effective run, both commit ports, sticky
invalidation through stop/disable/shared reset/time advance, and model reset.
The configuration fixture still covers144 writable fields and now201 holes.

The unchanged20-host-second `captureresults/green` run supplies the
same cold-neutral file. All six loader COPY ranges compare, the original
guard returns0 with packed150, and property6433 is empty. The original
force1 getter returns, then ba30 becomes1. Source execution reaches
`1020c07e`, both shared commit accessors with mask10, and
`10208c46 -> 1020aafc(unit20)`. The cold qualification has expired before
run; no result is published and no post-submit values are asserted.

The first uncovered access is CPU0 PC`102090bc`, WRITE32 PA`20069514=6`.
Its separate six-word writer is `102090a0` through `1020c2d4`; source callers
`1023994c/10239820` belong to the later flag200 branch through`1023c846`.
QMP reports debug/stopped and neither display head has a console. This is
original software progress **under the cold-empty assumption**. Force1 does
not consume the result numbers, so the run does not validate their physical
values or establish an actual result producer, normal GUI output or boot.
Raw instruction/source projections, red/green trace and read-only snapshots
remain in`run captureresults/`.

## Post-submit investigation: incremental caller and provider

The later unchanged `groupresults/origin` run reaches the same
post-submit frontier as [grid storage](../../runtime/grid.md). Read-only
snapshots and raw instruction traces establish this actual call chain;
instruction addresses below are original MAS code addresses:

`CaptureService.c:103250f2 -> 10325714 -> 10233570 -> 10232154 ->
1023b00e -> 1023aed8 -> 10239dc6(force0) -> 10208e0a`.

`103250f2` constructs a five-word service parameter on its stack and calls
`10233570` with it. This is not an identified hardware completion callback.
The mode getter`10235734` reads MAS VA`b1887d75`; its current value2 selects
the incremental branch. The current root is VA`b2cb5100`, whose word at+270
is1. That enables`10232154` to construct pointer array VA`b25f2388` and call
`1023b00e` with flags2eb. Flag40 and non-NULL array slot4 reach
`10239dc6(b1887ef8,0)` at actual call instruction`1023afb2`, LR`1023afb7`.
This getter precedes this incremental operation's later`1020bfaa` commit;
that not-yet-executed commit cannot have produced the values being read.
Their association with a preceding operation is not established.

Force0 is **not a polling wait or a failure return**. After all eight reads,
`10239dc6` compares packet byte1 with VA`b26b47cb`. Equal or force1 updates
ba2c and saved selection VA`b26b47d4`; unequal preserves both. Both branches
write ba10, copy the saved selection into the object's descriptor+80, and
return1. The present transaction error occurs before that comparison. There
is no justification for returning a matching byte to make this path succeed.

The requested byte has an identified software source:
`10239e36` copies it from byte+8 of the descriptor at`*(object+4)` into
VA`b26b47cb` and bb44. `1030b630` initializes descriptor+8 to0, but this
scoped chain does not independently establish the byte's slot/index meaning.
Separately,
`1030b6a6` registers kind23 with unit sizec00 and requested unit count;
`1030b730 -> 10298ef0 -> 1029915c` obtains its four-word provider
`{base,total,unit,count}`. `1030b764` and`1030b7b4` copy that same provider,
adding80 andd0 respectively to its base. `1030b630` also prepares descriptor
addresses at those offsets; it does not populate MMIO results.

The current kind23 handle is170000. Its five-unit provider covers
PA`[5d4b5000,5d4b8c00)`. Both configured address sets contain
`5d4b5000/5d4b5080/5d4b50d0`, all three strides arec00, bb44 is0 and bb48
is4. These are observed configuration values and a source-defined software
allocation. They do not establish that bb70 is an active bank, bb74 is an
applied slot, or the six result words equal base+slot*stride. Those proposed
relationships, including their latching time, remain hypotheses.

## Preceding result update boundary

The current model has accepted both earlier group commits and effective
unit20 run. At the later fault, capture-control run and enable readbacks are
both2bf, so unit20 is running and enabled; reset readback is0. Its eight
pending words at PA`20068210 + 20*i` are0 in the final snapshot. That does
not prove an earlier event never occurred or was never acknowledged.

The scoped callback registration`1023954a -> 1020c034` binds mask20 to
`10239436` (Thumb pointer`10239437`). Its raw body consumes shared event
status and updates software event flags; it neither reads nor writes the
group result packet. The device/machine source contains configuration
retention and commit/run invalidation, with no bound result update, frame
input or statistics DMA. Neither the callback nor effective run supplies
an independently recovered eight-field producer or update boundary.

The same-family [Z7 implementation at its inspected fixed revision](https://github.com/WonderLantern/z7-qemu/blob/97abf0b81cd3211f8374a38987a0beaf4549b90f/qemu/nikon-z7-lab.c#L1758)
returns zero at the matching eight addresses as an explicit idle/reset
hypothesis. It contains no post-submit producer to reuse. It therefore does
not justify extending this model's expired cold-empty snapshot.

No post-submit model extension was accepted from that investigation. A finite
producer still needs independent evidence for the two byte fields and six
word relationships, plus the boundary that publishes a coherent packet:
configuration latch, commit, run admission or processing completion. A
matching source implementation, register description or original register
record could distinguish these; making force0 continue would not. Stop,
disable, shared reset and time advance continue to preserve unknown results.

Both new original runs retain the same cold-neutral input and immutable
firmware. The `origin` receipt compares all six loader COPY ranges, records
guard0 with packed150 at`102d7254`, and keeps property6433 empty. Its first
error is still CPU0 PC`10208e14`, READ32 PA`2006bb70`; QMP is debug/stopped
and neither head has a console. There is no new startup frontier or new
native behavior in this investigation; the existing group-results fixture
already covers sticky invalidation and rejection of identity mirroring.
Raw projections, filtered traces, snapshots and comparison receipt remain
under`run groupresults/`.

## Software unit index and actual VD dispatch

The subsequent `groupcursor` source audit establishes the byte's
software meaning independently of the result comparison. `1023b78e` registers
five kind23 units and calls`102cd9a0`. Its loop passes index i to`1030b804`
at`102cd9e8`, then stores the same i at descriptor+8 at`102cd9ee`.
`1030b804 -> 10298f1a -> 10299176` resolves the unit to
`base + (i % count) * unit`, returning its unit size. The rebuild callback
`102cd97c` reads descriptor+8 and resolves that index again. Multiple software
descriptors can represent the same unit; this is not a unique descriptor ID.
This upgrades the earlier unknown byte to a **software unit index**, not to
an already-applied hardware slot or an arbitrary identity to echo.

The actual outer caller`10325ff6` ends with the original diagnostic string
`OnVd`. Dispatcher`10327976` receives event041a0001, then queues that callback
with16 payload bytes through`10319d56`. The `vd` run observes the original
chain`10397a18(04200141) -> backend flag -> 1022f318(04200001) ->
10327976(041a0001) -> 10325ff6 -> 103250f2`. It dispatches22 VD notifications
and executes two queued OnVd callbacks before the same bb70 error. The
current descriptor's software index is1. CMOS frame count and all readout
pipeline request/accepted/completed counters are0. A VD notification is
therefore not evidence of pixel/statistics completion in this model.

## Finite active-cursor assumptions

This is an address-generator model, **not recovered physical register
semantics, produced statistics, DMA completion or captured frames**. Its
mechanism is anchored by the software unit-index formula, three providers
with independent strides, two configured address sets and VD-driven service
consumption. The following choices remain individually falsifiable assumptions:

| Result PA | Functional interpretation | Assumed source |
|---|---|---|
| `2006bb70` low byte | Committed bank selector | Complete bb3c value0/1 |
| `2006bb74` low byte | Current internal ring cursor | Initial bb44; subsequent automatic rises advance modulo bb48+1 |
| `2006bb78/bc78/bd78` | Three set0 current addresses | Committed bb50/bc50/bd50 + cursor times corresponding bb80/bc80/bd80 |
| `2006bb7c/bc7c/bd7c` | Three set1 current addresses | Committed bb60/bc60/bd60 + cursor times corresponding stride |

The bank meaning is the weakest inference. Neither the original raw32 API
nor the present identical address sets establish bank selection, set ordering
or a shared cursor. The functional profile supports bank0/1 explicitly;
other full bb3c values remain retained configuration but cannot qualify a
result. It never silently masks an unsupported value to one bit. Initial
phase (first rise selects start), shared cursor, six-word ordering and
advancement on automatic TG rather than processing completion are assumptions.
Future independent evidence can falsify them without changing firmware bytes.

Every one of the12 metadata fields must have been explicitly written since
model reset: bank/start/count-minus-one, three strides and six bases. Both
selected shared commit ports atomically capture those staged values. Initial
index must be less than count, all strides nonzero, and every base plus the
maximum index times its stride must fit32 bits. Incomplete/unsupported/overflowing
snapshots cannot publish; they leave any last actual packet intact, or remain
unknown if none has been published. No neighboring
fields, address apertures, bytes or upper bits gain acceptance from this profile.

Run itself does not publish. Effective run enables the committed generator;
only the existing router's **automatic** TG rising boundary publishes the
first packet, at the configured start index. Later automatic rises advance
modulo count and replace all eight words synchronously. Falling edges, forced
high, repeated force writes, resync commands and pending/ACK operations do not
advance it. The router pulses this lifecycle output before delivering its
existing IRQ; it creates no additional guest cause. Resync can schedule a
future automatic boundary but is not itself a cursor tick.

Staging writes leave both the committed generator and readable packet intact.
Thus changing bb44 does not make bb74 follow it; only a new commit can change
the start configuration, and only an automatic rise replaces the latched
result. Commit during run starts a new epoch at the next rise. Duplicate run
high leaves an existing epoch intact. Stop/shared-reset/disable cancel progression
while retaining the last publication; restart reuses committed configuration
only at a new automatic rise. Only whole-model reset clears the packet and
restores cold-empty. There is
no group timer, pixel input, DMA write, completion IRQ, guest flag write or
guest callback invocation. These are active target addresses, not evidence
that memory at those addresses has received data.

### Published-result lifecycle

**Source fact:** `1023aed8` handles flag `0x40` by calling
`10239dc6(request,0)` before its later `1020bfaa` shared commit. The getter
first reads all eight words through `1020c17a`, compares only the returned
cursor byte with VA `b26b47cb`, conditionally updates the next address and then
writes the control. Thus staging a new request does not provide its own
applied cursor. This is a consumer-order fact, not a complete physical latch
or reset specification.

**Functional correction:** the previous expire-on-commit/run hypothesis also
discarded packets already generated by actual automatic input. Separate the
result latch from the next generator snapshot. A valid automatic rise replaces
all eight latched words; configuration, commit, stop, disable and shared unit
reset do not replace them. An invalid or incomplete committed generator cannot
manufacture a replacement. Only whole-model reset clears the packet. This is
a bounded retention hypothesis; the bank/cursor/address arithmetic and physical
reset uncertainty above remain unchanged. It never promotes unknown state to
zero or echoes the requested software index.

The [latch receipt](/evidence/imagerequest/latch.json) records a native red on
the old commit-expiration rule and70 passing sensor/memory cases. Distinct
nonzero/asymmetric packets verify retention across both commit ports,
stop/disable/shared reset, invalid generation and restart, replacement only on
an actual automatic boundary, and whole-reset removal of old data. The original
continuation and its remaining limit are reported separately from these
synthetic fixtures in the [startup frontier](../../validation/frontier.md).

## Cursor validation and original continuation

The new `group-cursor` qtest fails against the old device on a real HMP
transaction error at the expected first packet. Its asymmetric fixture uses
two distinct address sets, three different strides, start2/count4 and bank1.
Literal expected packets check first rise, falling-edge retention, advancement,
wrap and a new commit through the other shared port. A staged start0 leaves
the published cursor2 intact and the next cursor is3, independently disproving
direct configuration echo. Separate cases cover TG-disabled/force-high,
missing fields, invalid bank/start/count, zero stride, overflow in both sets,
stop/disable/unit-reset cancellation, model reset and no capture pending/IRQ.
All33 Sensor and23 SLA subtests pass in the scoped native batch. This checks
the defined functional contract, not which hypothetical hardware mapping is true.

Raw source, the old-model native red, final native log, immutable input
comparisons and original execution records remain under
`run groupcursor/`. The `vd` receipt describes the old model;
the later `cursor` receipt describes this explicit hypothesis model. Passing
force0 or reaching a later startup frontier cannot validate the unconsumed
byte0/six address fields or the assumed cursor phase.

In the unchanged20-host-second `cursor` run, all six loader COPY ranges
compare with the same cold-neutral file, guard`10305dac` returns0 with
packed150, and property6433 remains empty. The trace contains39 completed
getters: one initial cold snapshot and38 non-cold packets. The latter report
all five cursor indices0..4 and the expected six nonzero generated addresses;
bb44 and cached software index remain0. Thus the original consumer can
continue with a nonmatching running index; the profile does not force its
comparison true. There is no new uncovered access in this window. QMP is
running, the UART peer has no rejection, and neither display head has a console.

The separate `wait` observation confirms six COPY comparisons and guard0/
property6433 empty again, with33 completed getters and no uncovered access.
One logged entry is replayed before any result read and is excluded from
completed-call counts. The current software descriptor index is3 while
bb44/cached index remain0. CMOS/readout counters are still0. Gate, pending3
and display-ready are0; these are not universal display prerequisites and
do not by themselves identify a blocked task. The new boundary is a running
service without produced image data or a console, rather than another
identified MMIO failure. The specific upstream condition preventing the
next image request remains to be localized; these receipts do not establish
normal GUI output, actual statistics or full boot.

## Payload consumers and wire records

The subsequent C1.70 source audit recovers three packed record formats. It
does **not** recover their pixel-to-statistic arithmetic. That audit alone
qualified only the address generator. The functional reference operator below
subsequently adds explicitly hypothetical arithmetic; it does not upgrade the
audit into physical arithmetic evidence.
The [payload receipt](/evidence/capturegroup/payload.json) separates these source
facts from bounded cold-neutral executions at baseline `68b2696e`.
All offsets in this section are hexadecimal unless explicitly stated otherwise;
code addresses are MAS VA, payload allocation addresses are board PA, and words
are little endian.

`1023b050` copies the upstream `102aa02a` parameter packet into the 172-byte
(decimal) kind23 descriptor. Descriptor `+20/+24/+28` (decimal) hold the three
payload pointers, at provider offsets `0/80/d0`. The descriptor also carries
region geometry, count, enables and a saved selection. Its metadata is not the
payload. In particular, `1030b630` clears descriptor storage, not these outputs.

The consumer chain is conditional:

`10235afe -> 1021f424 -> 103a5bd8` copies the descriptor into an eight-entry
software input ring at VA `b217b7f8`, stride `d18`. Within each input record,
the descriptor starts at `+14`; the payload pointers are consequently at
`+28/+2c/+30`. A null group descriptor or metadata word `+16` (decimal) equal
to 1 suppresses this descriptor copy. `1021e130` subsequently calls
`103dd76c` only if the first payload pointer is nonzero. The deferred worker
`103dcbfa -> 102ab7fa -> 102a9c54` reaches the actual record readers.
This identifies an AF-associated software consumer; it is not evidence that
the current execution has admitted this work.

| Provider region | Reader and admission | Wire footprint |
| --- | --- | --- |
| `base+0` | `102a9bd6`, descriptor word `+68` nonzero and both first/second pointers nonzero | `0x28` bytes per record |
| `base+80` | Same call; count is descriptor byte `+6c` | `0x18` bytes per record, final word not read by this helper |
| `base+d0` | `102a9b20`, descriptor word `+80` nonzero | `0x20` bytes per grid cell; packed input cells are contiguous |

The first and third readers unpack a common prefix from input words `W0/W1`:
`P = W0 & fff`, `S = (W0 >> 12) | ((W1 & 7) << 20)`, and `T = W1 >> 3`.
These are a 12-bit field, a 23-bit field and a 29-bit field. Their arithmetic
meaning is unresolved; naming them mean, luminance, contrast or pixel count
would exceed this evidence.

| Wire field | `0x28`-byte record: compact output offset | `0x20`-byte grid record: compact output offset |
| --- | --- | --- |
| `P / S / T` | `14` (u16) / `1c` (u32) / `20` (u32) | `10` (u16) / `18` (u32) / `1c` (u32) |
| u32 at `+08/+0c/+10/+18` | `00/04/10/0c` | `00/04/0c/08` |
| u16 at `+14/+1c` | `1a/18` | `14/12` |
| u32 at `+20`; u16 at `+24` | `08`; `16` | Outside this wire record |

Compact offsets above are relative to the first helper's destination `+4`
or the grid cell's destination `+ac`, respectively. Unlisted input bytes are
not read by these two helpers. They are not thereby
defined as padding or zero. The `0x18`-byte companion record reads five u32
fields at `+0/+4/+8/+c/+10`, placing them at companion output offsets
`0/4/10/c/8`; its last four bytes remain unclassified. That companion output
starts at destination `+28`. The helper advances its two output pointers by
`0x24` and `0x14` independently; a general interleaved pair layout does not follow
from this, and only count1 is observed. No endianness conversion
or image-format conversion appears in these copy/unpack helpers.

The grid reader walks rows and columns from the selected table returned by
`103cbab2`, using table counts plus one. Its compact output has a fixed
nine-cell row stride (`0x120` bytes), independent of the packed input stride.
It also selects the largest `P`, retaining the first strictly greater value,
and stores the maximum and its index. This operation does not establish the
meaning or generating filter of `P`.

The live table's first two variants both contain decimal
`{8,8,270,88,246,82,0,0}`. This bounds their wire grid to `9*9*32=2592`
bytes, ending at provider offset `af0`, within the `c00` unit. The selected
root snapshots have descriptor enable/count `1/1`, hence the first two
readers need `40/24` bytes, rather than the full `0x80/0x50` gaps between bases.
Their saved-selection word is 0 in all 27 samples, so those samples do not
qualify the third reader. A table supporting different geometry is not proof
that its control profile or statistics computation is implemented.

### Completion and calculation admission remain distinct

Source callback `10239436` handles raw3 bit0 by setting software processed
bit `40` at VA `b26b38c4` and stage event `01000000` at VA `b188814c`.
Its separate raw0 bit27 branch drains a stop mask. The callback does not
decode payloads, publish GUI caches or set the FE synchronization latch.
A future functional processor must own qualified input, perform the complete
declared operation and DMA all required records before raising raw3 bit0.
Commit, TG address advancement and another stage's completion cannot qualify it.

The `payload` probe records one shared callback invocation with raw0
`10000000` and raw3 equal to 0. Raw0 bit28 is neither group's bit27 stop
condition nor its raw3 bit0 completion. Counting callback entries alone would
incorrectly imply group progress; neither group branch qualifies in this sample.

The old-cycle fence `1023b00e -> 10238974` still ignores its return before
configuration submission. A separate retirement path `102389dc` compares
processed bits against `requested & ffffff4f`, and can wait/drain if they differ;
this comparison could be sensitive to missing group work. Neither retirement
entry nor its comparison executes in the 25-host-second `retire` observation.
It does not explain the selected-root admission failures in that window.

In that same observation, all 28 selected roots at `102314e6` have mode byte
VA `b1887d72=0`, metadata byte `+10132` (decimal) equal to 0 and synchronization
latch VA `b1887dc8=0`. `102313ea` therefore bypasses `10235afe` before either
descriptor copy or payload processing. The 25-second `payload` probe records
27 such roots and no entry to `103a5bd8`, `102a9c54` or `10235afe`.
This is not a consumer rejecting a malformed group record.

Two upstream routes are now explicit. `103197fc`, called by configuration
builder `10322886`, writes metadata byte `+10132=1` when VA `b1868b1e` is
nonzero. `10324960` can set that service flag when its prior value is 0 and
CaptureService state VA `b1868af1` equals 12. Separately, notification
`041a0003` in `1022e2e0` posts FE worker bit8 only when VA `b1887dd8` is
nonzero; otherwise it posts `20000`. Its `10230368` consumer still requires
a queued root at VA `b25eebc4` before setting `b1887dc8`. The direct
`041a0002` notification used by the mode0 skip branch does not match a handled
case in its registered callback `10321a4c`. These are software conditions,
not permission to change the mode or flags from the host.

A further 25-host-second `firstread` breakpoint window observes the normal
queue producer. OnVd `10325ff6`, in CaptureService state4 with VA `b1868b50=0`,
allocates its first root and calls `10233cf6` at `103260fc`. That function
sets `b1887dd8=1`, enqueues the root at `b25eebc4`, then calls
`10235c84` and conditionally `10232040`. The window records one first-submit
entry, one FE initialization and two `10232040` entries (one earlier in state2,
one after this state4 submission). No `041a0003` handler branch or worker8
entry is observed. Final `b1887dd8` is 1, with one root still on that queue.
This does not reproduce a later FE reinitialization clearing the request.

The submitted first-read software masks at VA `b1887dcc/b1887dd0` become
`b/b`. The final selected/active capture masks at VA `b26b38bc/b26b38c0` are
also `b/b`: they exclude group `40` and paired `200`. Capture-control readbacks
are reset0 at PA `20068000`, run `1d` at PA `20068008` and enable `1d` at
PA `20068010`; all eight raw words are zero. Thus group
completion is not an outstanding stage of this observed transaction. Its
remaining boundary is the first-read processing and completion lifecycle
and the normal notification producer, not a group payload being rejected.
The retained unit2 packet also requests 8288 columns by 58 rows, unlike the
earlier continuous stream profile; its output geometry is 2072 by 58. The
existing combined stream operation
requires paired run and cannot establish work completion for this different
request. The units are running, enabled and out of reset; run intent alone
does not explain or qualify the missing completion. The continuous-stream
contract alone cannot supply this first-read profile.

A separate 25-host-second `control` observation uses the same inputs with no
GDB attachment or pause during the UART transaction window. Its final read-only
snapshot independently finds CaptureService state4, counter `b1868b50=54`,
`dd8=1`, one root on the FE read queue, software masks `b/b`, reset0 and
run/enable `1d`. QMP records 367 completed input DMAs and 366 statistics jobs,
with zero reported rejections/cancellations; both heads still lack a console.
This reproduces the pending first-read state without in-window breakpoints.
It does not prove a complete first-read result or long-term runtime stability.

The [statistics mode1 consumer](../statistics/protocol.md#endpoint-ring-completion-and-first-read-mode)
now closes the normal notification chain: raw3.bit7 posts owner80, whose
`1023c57a` consumer switches endpoint1's address set on its first invocation
and stops capture/emits `041a0003` on the second. It requires lane flag4 and
VA `b1888147=1`, both present in these final snapshots. A finite ring-end
notification is now tied to actual successful DMA, with bit0 address-set
selection and the zero-stride second address set supported as explicit
functional hypotheses. Group wire arithmetic is unchanged.

In a 55-host-second unchanged-firmware run of that model, the original owner
dispatcher consumes event80 in62 traced entries (including combined masks),
but no `041a0003` entry, FE worker8 or FECLCTRK entry appears. The count is a
bounded trace observation, not a one-to-one hardware IRQ count. There are378
input DMAs and377 statistics jobs. The blanket debug-script renaming made three
readout-control QOM queries invalid in this window; their error replies are
excluded from evidence. The corrected 120-second guest-error-only control
records381 requests,380 completed input DMAs and379 statistics jobs, no reported
rejection/cancellation in statistics, no console, and the same pending first
read. Its snapshot is read-only after the transaction window; no live GDB
attachment supplies progress.

The remaining source/model disconnect is specific: capture-control effective
run bit1 owns auxiliary readout, but bit0 owns main input. The first-read run
mask1d retains bit0 and clears bit1. Board wiring sends bit1 to readout's
`run` input; `nikon_readout_run` cancels its owned request on that transition,
and `nikon_readout_pipeline_cancel` invalidates the shared input generation.
The current pipeline only submits CMOS work for an auxiliary readout request.
The final main-input and statistics units are running, but no independent
main-image acquisition producer exists. Additional elapsed host time cannot
turn the retired auxiliary generation into the255 fresh inputs this first
read requests. The normal main-input acquisition/processing connection is the
next finite contract, rather than another group completion or a changed guest
counter. No complete group algorithm, first-read completion, normal GUI console
or physical-camera behavior is claimed here.

The subsequent [independent acquisition model](../sensor/protocol.md#independent-main-acquisition)
closes that missing input lifetime and supports mode1's masked per-frame flow.
Its final cold-neutral window acquires985 main frames while auxiliary readout
is stopped. The next rejection is the active kind32 output's uncovered RAM,
not group payload decoding. Group arithmetic remained outside that change.

## Declared functional RAW statistics operator

`hw/nikon/devices/capturegroup.c` now implements a bounded numerical reference
profile. The wire layouts and original consumer ratios support an aggregate
interpretation: primary wire `+08` is the denominator of three filter ratios,
`+0c` is divided by ROI area and multiplied by 100, and `+10/+18/+20` supply
the three numerators. Interpreting these as brightness sum, covered sample count
and three filter responses is a strong inference. The exact filters, scaling,
packed prefix and halfword meanings remain unrecovered. The following choices
are **functional hypotheses**, not measured Nikon or silicon behavior.

The grid is bounded to at most nine rows and nine columns. Register pairs
`b800/b804`, `b808/b80c` and `b810/b814` define count-minus-one, cell centers
and cell extents respectively. The domain rectangle is `ba00/04/08/0c`, with
origin zero and positive width/height at most 16384. The two `ba34..ba50`
rectangles have positive width/height and must fit that domain. Count mask
`ba30=1` selects one primary/companion pair. Each grid rectangle starts half
an extent before its center. Nonzero correction/mode fields are rejected:
`b818/b81c/b984`, `ba10`, `ba14..ba28`, and `ba94..bab8`; `ba2c=3` selects
the complete primary/companion and grid profile. Other selections remain
retained MMIO configuration but do not qualify this numerical operation.
Other metadata continues to have its existing opaque readback contract.

The normalized MAIN input is an immutable, synchronized, little-endian RAW16
snapshot. Each sample interpolates the 65 monotonic `b880..b980` points at
1024-code spacing, using floor division, then shifts right four to obtain a
12-bit value. Constant curves, including erased all-`ffff`, and zero-magnitude
coefficient rows reject. The three eight-tap coefficient rows use signed
two's-complement eight-bit first coefficients and seven-bit remaining
coefficients. Horizontal taps run from `x-3` through `x+4`, clamping at image
edges. Each response is the absolute signed convolution divided by the sum
of coefficient magnitudes. This is an explicit FIR convention; no Bayer
demultiplexing or physical focus response is claimed.

Five integral planes store corrected brightness, squared brightness and the
three normalized responses. Domain rectangle edges map to input coordinates
by rational floor/ceiling; the sample averages are scaled to the declared
domain area. Thus the reported count equals ROI area even when acquisition
and analysis dimensions differ. The known source ratios receive actual
image-derived sums and coverage, rather than identity or zero placeholders.

| Wire family and field | Declared numerical value |
| --- | --- |
| Primary/grid `+08/+0c` | Brightness sum saturated to u32 / domain area |
| Primary `+10/+18/+20` | Three response sums saturated to u32 |
| Grid `+10/+18` | First two response sums saturated to u32 |
| Companion `+00/+04/+08/+0c/+10` | Brightness sum / area / three response sums |
| Prefix P12 | Largest of the three average responses, saturated to 12 bits |
| Prefix S23 / T29 | Brightness sum / squared-brightness sum, saturated to their widths |
| Primary/grid `+14/+1c` | Mean brightness / domain rectangle center X |
| Primary `+24` | Domain rectangle center Y |

All field offsets above are hexadecimal. The three halfwords are explicit
numerical conventions whose physical meanings remain unresolved. Unclassified
bytes are preserved from destination RAM: primary `+16/+1e/+26` halfwords,
grid `+16/+1e` halfwords and companion final word `+14`. Gaps between record
families and after the last cell are also preserved. Source-consumer
classification does not turn unread bytes into padding.

The existing TG address packet remains independent metadata. A fresh MAIN
generation on an automatic rise, after selected commit and effective unit20
run, admits one owned computation with a declared 1 ms virtual latency. The
job snapshots the committed configuration, cursor packet and input bytes.
Both address sets and every ring slot must cover nonoverlapping writable RAM
spans; each stride must cover its record family. All computation and private
record construction precede output writes. Successful DMA of all three
families latches the job's coherent packet, increments `completed` and publishes
capture `THIRD_EVENT {1,0}` (raw3 bit0). A TG edge without input still updates
metadata but cannot publish data completion.

Selected commit, effective run-low/disable/reset and whole-model reset cancel
pending work without DMA or completion. A selected commit or run transition
also prevents replay of an already delivered input generation. Pending raw3
bit0 blocks admission even when masked; unrelated pending causes do not.
Only the matching guest ACK releases that ownership. Invalid geometry,
unsupported modes, erased/invalid curves, insufficient strides, overlapping
routes or uncovered RAM increment `rejected` and leave payloads/completion
untouched. The read-only `completed/rejected/cancelled/blocked` properties
expose this bounded lifecycle.

### Explicit resource39 input

The original `102aa02a` producer reads resource39 at descriptor17 offset
`11500`, length `2c00`; it is unrelated to descriptor32/resource80.
Its coefficient variants are 24 u32 values at resource offsets `186c`,
`18cc`, `192c`, `198c` and alternate `26b0`. The packed curve is 65 u16
values at `1a1c`. Mode/threshold fields come from the 27 u32 values at
`1800`, and mode counts from `19ec..19f8` and alternate `2710`.

The existing module tool now offers a separate, explicit transform:

```sh
nikon-module functional-group-descriptor17 INPUT_DESCRIPTOR17 OUTPUT
```

It requires exactly `20000` hex bytes, preserves every other byte and writes
only those source-consumed fields. All modes/counts become zero, the curve
declares `min(i*1024,65535)`, and each coefficient row declares a difference
between taps 3 and 4, 5 or 6 with coefficients +1 and -1. This supplies a
usable, image-dependent virtual filter profile; it is neither factory tuning
nor an implicit fallback for erased inputs. Apply it after the separate
`neutral-regions-descriptor17` transform when both profiles are desired.
Resource35 and the `cold-neutral-resource80` profile remain separate inputs.
The original firmware loader remains responsible for copying the declared
resource into guest RAM; this tool neither edits firmware nor prefills RAM.

Native CMOS tests under `/nikon/cmos/group/` cover packed-prefix decoding,
all three families, actual brightness/filter response arithmetic, guard-byte
preservation, bank/ring selection, stale input, masked pending/ACK ownership,
invalid profiles/geometry/RAM and cancellation. Module codec/CLI tests cover
the declared fields, exact-size rejection and byte-for-byte preservation
outside them. These are synthetic fixtures; parent native validation and
original-firmware execution are reported separately, and no complete boot or
physical-camera fidelity follows from this implementation contract.
