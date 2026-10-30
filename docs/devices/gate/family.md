# Graph type coverage and processing boundaries

The original Z7II C1.70 MAS graph interface declares 19 types, numbered0..18,
and 76 distinct resources, numbered0..75. Fifteen types describe processing
units with42 command ports. The finite importer covers all42 ports; type14's
24-byte single-block layout is explicitly a hypothesis, while the other
remaining layouts are constructor-established. This is configuration coverage,
not15 completed pixel operators. At this historical importer checkpoint, actual
data operations, result production and frame retirement remained unsupported.
The current [continuous processing contract](processing.md) implements one
bounded connected graph, with separate numerical and completion hypotheses;
it does not add arbitrary operators for all19 types. The intermediate
[request contract](../pairs/requests.md) admits pending starts without inventing
completion and corrects type0 capture.
The original observation below precedes the later
[paired-table continuation](../pairs/tables.md), which crosses its configuration
stop and reaches the separate paired-processing start.

The [receipt](/evidence/imagerequest/family.json) separates source, model choices,
native checks and unchanged-firmware execution. Code/ROM/software addresses
below are original MAS virtual addresses (VA); device and explicitly identified
RAM addresses are board physical addresses (PA). Addresses, offsets and masks
are hexadecimal. Counts, lengths, type/resource numbers are decimal. Ranges
exclude their upper endpoint; device words are aligned little-endian32-bit.

## Complete type and resource inventory

Selector `103bd930` chooses resource rows in `[10789dc0,1078a2a0)`. Backend
and setter entries start at `1078a2a0`; constructor records at `108a7c4c`
contain `{type,Thumb callback}` for all19 types. The independent table at
`108a7ce4` is outside that constructor array. Type5/6 resource tables each
contain padding after their single resource; allocation setup `10388c3a`
confirms count1, so the padding does not add duplicate resource0 entries.

| Type | Resource IDs | Interface and finite coverage |
| --- | --- | --- |
| 0 | 0..15 | Sixteen staged/committed13-word parameter slots; bounded active use is defined by the [continuous profile](processing.md) |
| 1 | 16..23 | Eight paired endpoint configurations |
| 2 | 24..27 | Software connection nodes, NULL backend/setter |
| 3 | 28..33 | Software connection nodes, NULL backend/setter |
| 4 | 37..39 | Three72-byte stream ports |
| 5 | 35 | 356-byte stream; separate40-byte CPU trailer |
| 6 | 36 | Shared356-byte stream; same constructor as type5 |
| 7 | 40 | 248-byte stream |
| 8 | 41..44 | Four128-byte streams |
| 9 | 45..48 | Four136-byte streams |
| 10 | 49..56 | Eight140-byte streams plus two auxiliary words |
| 11 | 57..64 | Eight140-byte streams |
| 12 | 65 | 176-byte stream and six CPU-populated LUT banks |
| 13 | 66..67 | Two120-byte streams |
| 14 | 68 | Strict24-byte single-block hypothesis; real template unrecovered |
| 15 | 69..70 | Two68-byte streams |
| 16 | 71 | 32-byte stream |
| 17 | 72..75 | Four116-byte streams |
| 18 | 34 | Workspace provisioning followed by84-byte stream |

The common [command contract](commands.md) owns backend offsets, layouts and
reset/RAM-DMA/atomic-import behavior. Software connection nodes do not require
invented hardware processors. Allocation masks, callback masks and numeric
type order also do not identify the units' physical filter names or arithmetic.

## Remaining stream family and type14 hypothesis

Constructors `103b444c` (type13), `103b48f8` (type16) and `103b47f4` (type18)
directly emit the accepted headers in their force branches. Their setters
`103bdd7c/103bde24/103bdd8c` all finish through common reset/import `103bded0`.
The importer now includes both type13 ports, type14, type16 and type18 together,
with isolated reset and the same complete RAM preflight as earlier ports.

**Type14 functional hypothesis:** constructor `103b45d6` cache-cleans24 bytes,
mutates route bytes at payload offsets `4/6/8/a` and clears bit4 of the word at
offset `10`. It does not emit a header. The finite model accepts only a single
`01060000` header followed by five words at logical0..10. This follows the
common packet protocol and fixed extent, but is not uniquely proved: a
two-block `{0/3,10/3}` template would fit those same mutations. That alternative
and other headers remain explicit errors. A recovered client template or an
original type14 submission would distinguish them and may falsify the chosen
layout. Native success cannot recover the missing template; no original
type14 execution is claimed.

## Type18 provisioning and result ownership

Type18/resource34 uses backend PA `20100920` and software allocation-group1
bitmask `200`. That resource-allocation word is not a raw IRQ callback mask.
Callback `103bdd8c` clears SYS41..44, reads `20100148.bit8` and, if absent,
writes `201002c0=1`, CPU-clears `[24010000,24011000)` through `10361034`, then
pulses backend+`14` and submits its payload at+`10`. The control write precedes
the CPU clear; it is not a request to DMA or generate those zeros itself.

**Provisioning hypothesis:** the explicit `2c0=1` request invalidates workspace
write coverage while retaining all existing bytes. Bit8 remains absent until
actual CPU writes cover every one of the1024 words. Duplicate writes and time
cannot advance coverage. Invalid values, widths and adjacent control words
remain unsupported. Local command reset preserves workspace provisioning;
whole-device reset clears its data and coverage. The pulse does not touch
other command snapshots, type0 slots, pending bits or IRQ. This extends the
existing [workspace convention](commands.md#paired-endpoints-and-cpu-populated-storage),
not a recovered physical status law.

Constructor `103b47f4` follows the graph's upstream key and places an input
selector in logical4 bits0..2. Resources49/50/51/52/57/58/59/60 select0..7.
In the observed graph, type18 consumes type10/resource49's output, which also
routes to type0/slot4. Starting type18 before that producer cannot supply its
statistics.

Result reader `103bd5ca` consumes the following sparse PA groups. All accesses
are aligned little-endian 32-bit words; gaps and writes remain unsupported.

| PA | Source-decoded result |
| --- | --- |
| `20100200` | Four opaque bytes at result offsets `80..83` |
| `20100220/224 + 8*i` | Four `{u14 field0, u14 field1, s15 field2}` records |
| `20100240/244 + 8*i` | Another four records of the same shape |
| `20100280/284/288 + 12*i` | Four `{u14 field0, u14 field1, signed49 scalar}` records; high word supplies 17 bits |
| `24010000 + 400*i` | Four optional 1024-byte CPU copies after SYS clears |

Here `i` is decimal 0..3. Code and software-result offsets are MAS VA; table
addresses are PA. The positive signed49 limit is `2^48-1`. Release helper
`1038897e` clears the optional result pointers before the getter, so absent
copies do not eliminate any result-register read.

The CPU reference `1038f20a`, used alongside accelerated `1038f01a`, accumulates
`max(u16_pixel-black,0)` into four spatial parity groups `2*(y%2)+(x%2)`.
Large regions combine CPU edges with the monitor's four wide sums; subsequent
`1038f6d4..1038f790` reorders sums and counts for the selected CFA descriptor.
This establishes additive RAW parity statistics for that recipe. Histogram
consumers `10410f54`, `10412130` and `10416a16` use 256-bin populations and
quarter-image counts for tone statistics. Neither source establishes NV12
projection, extrema meanings for the first two groups, coordinate ties, bin-bank
association or the meaning of the four opaque bytes. Logical `60..7c` pairs
and logical0 control-bit polarity also remain unknown.

**Finite numerical hypothesis:** only the already qualified complete 84-byte
tuple is admitted: logical0/4 zero, `10..1c={20200,20201,20202,203}`, and
`20/24/40/60..7c` zero. It measures the actual resource49 branch1/slot4 output
after affine processing. Active NV12 Y becomes lossless virtual14-bit `Y<<6`;
no new RAW precision is inferred. Four output-coordinate parity groups produce
minimum and maximum values, the first row-major coordinate on ties, additive
sums, the last accumulated coordinate, and 256 bins indexed by Y. UV, pitch
padding and window-only rows are excluded. The four header bytes report
signed49 overflow under this virtual arithmetic; all admitted geometries fit,
so their computed overflow is zero. This is not a ready bit or a cold default.
Different profiles remain explicit errors. A recovered NV12 reference or
field setter showing another domain, bin mapping or header meaning would
falsify these choices; physical extrema and histogram fidelity are unproved.

`gatework.c` computes the entire monitor privately with the three real output
images. `gate.c` publishes all fields and four histogram banks only after
successful output DMA, before input retirement and the existing terminal IRQ.
No monitor-specific timer, completion cause or software-slot release is added.
Publication is one BQL-protected transaction; a series of reads may span two
complete frames, since no source read-latch protocol is established. The
cleanup reader follows the normal source stop/drain sequence. Cold reads
reject until an actual result exists. Local uploads, stops and W1C preserve the
last complete result; cancelled, rejected or paused jobs cannot replace it.
Whole-device reset invalidates it and clears workspace. Numerical writes do
not count as CPU provisioning coverage for `148.bit8`; `2c0` retains the last
result and bytes while restarting only that coverage.

The [monitor receipt](/evidence/runtime/grid.json) records 31 gatework and
137 CMOS integration cases passing with normal exits; the exact histogram-sum
reduction separately retains all 31 gatework passes. A normal main-switch OFF
run crosses the former monitor boundary and stops on paired-bank PA
`203800ec=ffffff71` at MAS VA `10369f02`. A separate read-only instruction/RAM
observer records one complete original getter: all 30 reads, four nonzero wide
values `733673216/733707264/733473024/733524416`, then return0 to cleanup.
Optional copy pointers are zero in that cleanup; original workspace copying is
not claimed. The observer is separate from uninstrumented execution.

The first long-M attempt fails earlier at AF MAS VA `10372abe`, DFAR `818`,
after 83.27 host seconds. The exact-reduction normal-switch window completes
one A/S/P/M cycle, then fails at AF `1021d7ce`, DFAR0, after 100.20 seconds;
FIFO occupancy reaches60. These failures remain in the receipt. Finite monitor
acceptance does not establish long queue bounds, all-mode acceptance, complete
power-off or physical statistics fidelity.

## Whole-graph execution and model boundaries

The current18-node graph connects slot8 through type5/resource35,
type9/resource45, software join type2/resource24, type11/resource62 and paired
endpoint type1/resource21. Its three branches pass through type8 resources
41/42/43 and terminate at type10 resources54/49/55, respectively, routing to
type0 slots3/4/5. Type18 is a side consumer of resource49, not an independent
whole-graph completion source. Software joins type3/resources28/29 appear on
two branches; this topology does not assign them pixel operations.

Worker `103b4cea` reverse-starts processing nodes through `103bd07c` in order:
`18/34,10/55,10/49,10/54,8/43,8/42,8/41,11/62,9/45,5/35` (type/resource).
It forms its terminal wait only from resources49..56. For this graph the
software terminal mask is `610`: resource49 contributes `10`, resource54 `200`
and resource55 `400`. Their source-selected hardware causes are raw-domain1
bits0/5/6, combined mask `61`, consumed through the original ISR and callbacks.
The earlier `c2` calculation incorrectly reused the software allocation bits
`2/40/80`; allocation ownership is independent of terminal callback routing.
The [continuous profile](processing.md) separately chooses raw-domain2 bit3
(`8`) as a whole-frame completion cause under an explicit functional hypothesis.
Neither allocation bits nor type9/type11 callbacks substitute for the terminal
wait. At the historical checkpoint, this reverse-start order predicted PA
`20100920=1` as the first activation, before it was observed.

At that checkpoint, the outstanding processing capability was a connected
input/operation/output contract: complete slot8 base/plane/extent association,
type5 table arithmetic,
type9 mesh sampling, type8 transforms, type10/11 stream-to-slot output DMA,
type18 numerical results, then actual terminal publication and slot retirement.
The current [continuous contract](processing.md) supplies one bounded input,
neutral type5 carrier, type9 mesh, type8 transform and three-output DMA path.
The type18 side consumer now measures actual branch1 output under the bounded
numerical hypothesis above. Its physical statistics equations remain
unrecovered. Each operation needs observable data and a result condition;
configuration import, elapsed time and a software release consumer cannot
provide them.

The older borrowed-record snapshot also supplied narrower numerical hypotheses,
rather than recovered hardware laws. Its type8 resources42/43 fit Q16.16
pixel-center scaling:
scale `1e000/10000=1.875`, translation `7000/10000=0.4375`, and output1024x582.
Resource41's `80000000` fields were unexplained in that snapshot; no identity
result was inferred. Subsequent constructor/consumer analysis establishes
signed low31-bit Q16 offsets with separate bit31 flags, rather than negative
signed-magnitude offsets. The [current profile](processing.md) records that
interpretation, the newer active geometry and the remaining sampling choices.
The old snapshot and its unexplained fields are not current format definitions.

Error labels elsewhere are not a count of missing features. The preceding
cold-neutral run hits only the type18 initialization request; graphics,
capture, timing and other historical stop labels also guard malformed inputs
in already-supported models. Unknown accesses, alignment/width failures,
invalid DMA and unrecognized command variants remain necessary explicit errors.

## Historical validation and unchanged-firmware continuation

These measurements precede the [request and capture correction](../pairs/requests.md).
Their busy flags describe the old model, not established physical ownership.

The old model fails a focused provisioning regression: after a complete
workspace population, `2c0=1` leaves `148=100` instead of invalidating coverage.
The accepted native batch passes five decoder and23 SLA subtests. It covers
all42 ports, exact remaining layouts, every shorter prefix, oversized inputs,
all header-bit mutations, sparse logical destinations and atomic failure.
The type14 alternative is deliberately rejected. Provisioning tests require
all1024 actual writes, preserve old bytes until written, reject bad values and
widths, retain state across local reset and create no IRQ or busy-slot release.
These fixtures establish the finite contracts separately from original startup.

In the unchanged60-host-second bounded NOR resource35/cold-neutral resource80
run, with quadrant scene, healthy paced UART/RNG0/held F4/five-CPU MTTCG, type18
imports at pointer `b2bda5d8` and `148` reads `100` after CPU initialization.
Total imports become18 packets/649 words. Original code additionally submits
type0 output slots3/4/5; slots1/2/3/4/5/8 remain busy. Types13/14/16 are not
configured in this window, so their support is source/native or hypothesis
coverage only.

Before the predicted reverse-start sequence, another configurator reaches
CPU1 PC `1036ca8a`, WRITE32 PA `20381400=00f600fa`. This paired-bank table
publication is the first uncovered transaction. Type18 activation and all
actual processing remain unreached. Main acquisitions total128, with zero
rejections and one cancellation;453 statistics jobs complete with no rejection
or cancellation. Error property6433, GUI readiness/information remain zero,
and neither display head has a console. There is no in-window GDB pause;
later sequential QMP/read-only GDB reads are not atomic or physical rates.
