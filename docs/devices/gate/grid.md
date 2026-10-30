# Independent RGB grid processing

The C1.70 MAS context8 path is implemented as a finite virtual confidence-grid
producer: statistics RGB DMA → unit8 graph → packed12 kind25 DMA → paired
read dependency. `gategrid.c` supplies arithmetic, `gateplane.c` validates and
executes one transaction, and `gate.c` owns sources, phases, cancellation and
interrupts. The numerical recipe is an explicit virtual-camera hypothesis.
It is not a recovered physical ASIC algorithm. The scoped native validation
passes 219 cases. An unchanged-firmware window executes 1,351 grid jobs with
zero grid errors, but still fails in the General-pool allocation wrapper;
complete mode-change acceptance remains pending.

## Graph and activation

Original C1.70 MAS source and request-packet captures establish context8's
principal type/resource path:

`0/1 → 4/37 → 15/69 → 12/65 → 17/72 → 17/73 → 11/61 → 1/20 → 7/40 → 10/53 → 0/2`.

This shorthand is not the complete topology: type7/resource40 also connects
back to type11/resource61's second input. MAS VA `0x103b3226` and
`0x103b4214` establish those outgoing/incoming references. The implementation
assigns this edge a finite two-filter recipe below; it creates no temporal RAM
history and does not claim to recover the edge's physical meaning.

The eight command ports at board PA `0x20100000` plus offsets `0x800`,
`0xd00`, `0xa40`, `0xa00`, `0xa20`, `0xdc0`, `0x880` and `0x9c0` accept the
source-shaped packets. They are distinct from the ten context9 MAIN ports.
Every required packet field and opaque control tuple must match the finite
profile. An imported packet or ordinary node start alone completes no work.
In particular, logical fields `0xa8/0xb0` are `0xff/0xff` for type11/resource61
and zero for type10/resource53. Both the captured packets and ROM templates
retain that difference. Sharing type11's values with the terminal wrongly
rejects the complete graph; this correction does not generalize other type10
profiles or claim the physical meaning of those fields.

MAS VA `0x10264972` activates unit8 only for frame-mode values 2, 6, 7 and 29.
It rotates the frame's RGB-descriptor queue, prepares four phases through VA
`0x102643e2`, submits through VA `0x102bc1ce`, and installs the phase map
through VA `0x102bddbc`. VA `0x102be02c` later toggles a map/control bank.
Packet byte 51 selects input 0; VA `0x10389b30` updates PA `0x20102004.bit0`,
and VA `0x103bd188` selects configuration map `0x2200` or `0x2300`.
Map bank, phase and source freshness remain separate state.

The captured continuous controls at PA `0x20102000/04/10/14/18` are
`0x131`, `0xc2`, `0x74011`, `0x4100`, `7`. The separate unit1 controls at
PA `0x20102640/44/48/4c/50/58` are `0x202`, `40`, `3`, `2`, `0`, `3` for the
60 × 40 profile. The second word is image height: VA `0x102ba530` prepares
packet offset `+24`, which VA `0x103bd1e0` copies to PA `0x20102644`.
The value 40 does not identify resource40. These observed controls qualify the
implemented profile; other combinations do not become supported by inference.

## Input and output ownership

The original allocator at MAS VA `0x1030af00` reserves six kind19 units of
`0x36000` bytes. VA `0x1030ae70` constructs three little-endian u16 planes at
relative offsets `0`, `0x12000`, `0x24000`, with pitch aligned to 16 bytes.
Statistics DMA endpoints 2/3/4 supply them. Captured bases are PA `0x5d1dc000`,
`0x5d1ee000`, `0x5d200000`, each with unit stride `0x36000` and six slots.
The four selected phases view that ring; they do not denote four newly
acquired frames. Captured input slot1 is 60 × 40 with 128-byte plane pitches.

The implemented `statistics.c` producer separates actual CFA samples into R,
G and B regional means, combining both green phases. Mono input supplies the
same mean to all three planes. A sub-cell without the requested CFA phase
uses its nearest actual phase. The declared virtual scale converts full-range
MAIN16 to RGB14 by rounding the mean divided by four and clipping to
`0x3fff`. This scale and the cell averaging are functional choices, not
physical sensor calibration. Source storage remains u16; the unit8 gain
stage also accepts the full u16 domain and performs its declared clamp.

After all three actual RGB writes, statistics publishes address, pitch,
dimensions and a positive source generation through `gridinput.h`. It
withdraws those addresses before overwriting their RAM. The gate retains at
most six source snapshots, matches the selected phase's mapped addresses,
and copies a fresh source into private job storage. A generation already
consumed at that address cannot replay. Withdrawing a source after this copy
does not replace or cancel the owned job's pixels.

Kind25 is allocated by MAS VA `0x1030bb26`; VA `0x1030ba84` constructs
one-base descriptors with pitch `align16(3 * width / 2)`. Captured output
slot2 is 60 × 40, pitch96. Four `0xd800`-byte units cover board PA
`[0x5f914400,0x5f94a400)`. There are 3600 active bytes in each 3840-byte
pitched footprint. The allocation authorizes neither row-padding writes nor
writes to the unused unit tail.

The kind25 direction is source-established. Unit8 VA `0x102643e2` calls
`0x102cd202` for a producer descriptor; VA `0x102cd176` adds it to the active
registry and the subscriber queue of registry VA `0xb2362b70`. Paired VA
`0x1026405a` obtains the same descriptor through `0x102cd2ba`, which removes
it from that subscriber queue. Both use serializer VA `0x102cc1b0`.
Unit8's graph slot2 is the sink, and paired bank offsets `0x398..0x3b4` select
these same kind25 addresses as inputs. Paired enable at `+0x12c` follows
packet byte 62 bit3. The original compact-coordinate path through VA
`0x102be6b0` and `0x1036bcde` establishes the grid coordinate fields; its
captured record has steps 11915/11915, maxima 59/39 and origin 41/31.

`pairs.c` therefore waits for a fresh completed grid at the configured address
when that dependency is enabled. It validates dimensions and pitch, snapshots
and unpacks the actual RAM into an owned u12 map, and records consumption per
address and source epoch. It writes no replacement kind25 map. Withdrawal
before a later overwrite prevents new borrowing while an already owned
snapshot may finish. The virtual paired operator samples this confidence map
to modulate sharpening; that arithmetic remains a functional hypothesis.

## Numerical evidence and virtual recipe

The source packets constrain a useful finite recipe without recovering ASIC
math. Type17 fields `0x20..0x3c` contain sixteen signed 15-bit coefficients in
a 4 × 4 arrangement. Resource72's first three row sums are 4096, 0 and 0;
its fourth row is zero in the captured profile. Resource73 has different
coefficients, bounds and a nonzero final tuple. Its preinitialized graph
packet is at ROM VA `0x1247ca60`; VA `0x103b49b0` patches routing bytes in
logical register0. Those changing bytes do not establish numerical bypass.

The captured type15 gains are 512 with candidate shifts 9 and upper clamps
`0x3fff`. The type7 kernel's 17 coefficients expand to a symmetric 5 × 5
kernel with sum 2048, consistent with three shift11 fields. Type4's four
additional configured rows qualify the packet; they authorize no extra RAM
reads. These observations motivate the virtual choices without proving them.

The implemented recipe is:

1. Apply each type15 gain and right shift with symmetric nearest rounding,
   then clip to its declared upper bound, at most `0x3fff`.
2. Interpolate each RGB channel through its two staggered 1024-entry u14 LUT
   banks. A 14-bit input selects a knot pair and a three-bit fraction;
   interpolation rounds to nearest and retains the final knot at the endpoint.
3. Apply resource72's signed Q12 matrix and clamps. Its first three signed
   output planes form the virtual three-channel bus into resource73. Decode
   and compute all four rows at each matrix stage, with a fixed zero fourth
   input. Resource73's four signed outputs all contribute to confidence.
4. For each resource73 output, normalize its absolute magnitude by the larger
   absolute clamp bound into `[0,4095]`, then average the four normalized
   features. A zero normalization bound contributes zero.
5. Apply the type7 17-coefficient expansion as a 5 × 5 Gaussian-shaped filter
   with edge replication. Average that result with the unfiltered confidence,
   then apply the same filter again. This finite blend and two-filter recipe
   represent the graph backedge without inventing temporal history.
6. Pack each final u12 pair `a,b` as bytes `a & 0xff`,
   `(a >> 8) | ((b & 0xf) << 4)`, `b >> 4`. Write only the active row bytes
   at the committed output pitch.

Both matrices retain signed intermediate values. The fourth resource73 row
is not discarded or treated as identity. Its opaque tuple at logical
`0x80..0x94` must be `1`, `0x11111111`, `0x32103112`, `0x16b`, `0x076e07ba`,
`0x011f0000`; it qualifies this virtual profile rather than pretending to
decode undocumented selectors. Unsupported tuple or packet changes fail.
Q12 scaling, zero fourth input, LUT interpolation, confidence projection,
Gaussian-shaped reconstruction and packed12 interpretation are declared
virtual laws. None asserts Nikon's physical equations or color science.

## Scheduling, cancellation and causes

The gate schedules a fresh owned source at a nominal virtual latency of
1 ms. Its retained RGB sources are bounded to dimensions in `[1,256]` on
each axis. Elapsed time with configuration alone cannot generate a result.
`gateplane.c` validates the complete profile and every active RAM row before
publication. Destination rows must be writable direct RAM, must not overlap
each other or the input rows, including physical aliases, and must not alias
the private RGB snapshot. Failure writes no output or completion.

Immediately before the first output DMA, the gate withdraws that destination
from the paired consumer. A cancellation or reset during this synchronous
notification prevents DMA. After all active packed12 rows are written, the
runtime marks the source consumed, advances the phase, publishes the fresh
output generation and latches the result causes. Pending causes are already
latched when the positive `grid-ready` callback runs. The IRQ is recomputed
from the resulting state afterward, so a synchronous ACK, stop or reset cannot
resurrect a cause or have its phase/counter state overwritten by the caller.
No software event, worker PC or guest allocation record is modified.

The independent result causes are raw1 bit4 (`0x10`, terminal resource53)
and raw2 bit2 (`0x4`, context8 whole-frame completion). Both pending latches
must be acknowledged through their normal W1C registers before another grid
phase starts. Context9 MAIN retains its own raw1 bits0/5/6 and raw2 bit3;
MAIN completion does not stand in for unit8 work.

After actual DMA has completed every phase in `[0,4)`, the model also latches
raw2 bit6 (`0x40`) and restarts that traversal watermark. The original table
maps this bit to callback flags bit2, which requests a map refresh at MAS VA
`0xb18648c8`. Associating it with four successful phases is a virtual cadence
hypothesis. Four elapsed timers, rejected jobs or cancelled phases do not
satisfy it. A retained bit6 also blocks the next grid job until its W1C ACK.

A unit8 packet, LUT, selected input/output slot or map update cancels only
unit8's queued work; independent MAIN configuration cancels only MAIN.
Global gate stop or reset cancels the job, clears retained RGB sources and
withdraws the output epoch. Source snapshots and completion generations are
not guest readiness flags. The runtime retains existing pending causes until
W1C or reset; cancellation does not manufacture success.

## Historical missing completion and retirement

The following observations predate the unit8 executor and retain their
original scope. They explain the missing producer that motivated this
contract; they are not acceptance of the implemented path.

In the five-CPU MTTCG cold-neutral profile at `aafb8aba`, unchanged C1.70
aborts in the General-pool aligned allocation wrapper at MAS VA `0x1034fe78`,
DFAR `0x3c`, after 39.69 host seconds. The underlying allocator returns zero;
the wrapper aligns it and attempts its metadata write. After the OnVd core,
VA `0x103d0db0` allocates a `0x2cc` descriptor and `0x3e44` work record per
request. Retirement requires all eleven software lanes. Sequential translated
snapshots show active-record counts 58, 326 and 575 with empty submission lists.

The oldest sampled lane2 record waits in microstep1 at VA `0x102682ae`.
Its call at VA `0x1026830e` waits for event `0xb1864850`, pattern `0x81000000`,
with timeout 1000 in native API units. Normal completion is `0x01000000`;
`0x80000000` is cancellation. The packet callback at VA `0x10262dfc`
decrements its initial discard counter or sets the normal event bit through
VA `0x103610cc`. The source whole-frame route is:

| Producer or consumer | C1.70 MAS source route |
| --- | --- |
| Gate ISR VA `0x103bce38` | Reads board PA `0x20100020`, raw2 |
| Callback VA `0x103b5148` | raw2 bit2 → flags bit0; raw2 bit3 → flags bit7 |
| Dispatcher VA `0x10388916` / `0x10388880` | Selects context8 / context9 respectively |
| Bridge VA `0x102e1c9a` / `0x102e3f04` | Resolves the owner's request packet and callback |
| Context8 callback VA `0x10262dfc` | After initial discard, sets `0x01000000` |

The [curated historical receipt](/evidence/runtime/grid.json) records sixteen
completed waits that timed out, no recorded context8 callback, and 192 sampled
ISR entries with raw2 bit3 but none with bit2. Its eight-PC GDB probe records
only global threads 1/2, and host durations include debugger stops. It does not
establish permanent unreachability. A common cause with the separate AF
history overwrite and complete mode-change recovery remain unproved.

The pinned [Z7 reference](https://github.com/WonderLantern/z7-qemu/blob/110c8ab93278e32870ae136533b5ee8b17db19b3/qemu/nikon-z7-isp.h#L48-L113)
directly generates MAIN NV12 and adds bit2 when its first command bank is
enabled; its [port handler](https://github.com/WonderLantern/z7-qemu/blob/110c8ab93278e32870ae136533b5ee8b17db19b3/qemu/nikon-z7-lab.c#L1611-L1705)
retains this graph's configuration without executing it. That combined
completion is not the implementation or acceptance evidence for this path.

## Validation boundary

The [curated receipt](/evidence/runtime/grid.json) separates the historical
trace from the implemented model's 219 native passes: 127 CMOS/paired
integration cases and 92 unit cases. It records red→green comparisons for the
RGB channel projection and distinct type10/type11 controls. Native runtime
cases check actual packed output/padding, input freshness, private snapshots,
ACK backpressure, four successful phases, cancellation and synchronous
ACK/reset during publication.

A separate unchanged-firmware, debugger-free five-CPU cold-neutral window
executes 1,351 grid DMAs, 51 paired jobs and 1,121 MAIN acquisitions, with zero
grid or MAIN-graph errors. At 40.57 host seconds it still aborts at MAS VA
`0x1034fe78`, DFAR `0x3c`, after General-pool allocation failure. Thus the new
producer runs, but full graph retirement is not accepted. Later translated
read-only snapshots show lane2 completed and the oldest records waiting in
lane0 cell2/microstep0 and lane3 cell1/microstep0. A separate four-PC trace
records three successful lane0 waits for normal bit `0x00400000`; its GDB host
spans cannot establish normal throughput or the remaining backlog cause.
Mode changes, the independent AF history failure and physical fidelity remain
pending acceptance levels.
