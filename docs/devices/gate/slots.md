# Type0 parameter capture and processing ownership

C1.70 MAS declares sixteen type0 parameter slots. A complete thirteen-word
submission atomically installs configuration; it does not start pixel work or
make that configuration permanently unwritable. Normal configuration-only
execution reloads an already allocated slot before any processor starts,
falsifying the earlier busy-on-configuration hypothesis. The model separates
staging, committed parameters and active data ownership. The current
[continuous processing contract](processing.md) adds bounded data use for one
connected graph; the historical slot-stage observations below precede it.
See the [request and storage contract](../pairs/requests.md) and
[current startup frontier](../../validation/frontier.md).

## Original producers and consumers

Code and software-object addresses below are original MAS virtual addresses;
device addresses are board physical addresses in the functional machine. Sizes,
offsets and addresses are hexadecimal unless explicitly marked decimal.

Initializer `10388c3a` sets type0's resource count at VA `b1d52252` to decimal16.
Selector `103bd930(type,index,out)` copies a sixteen-byte ROM descriptor;
type0 table `10789dc0..10789ebf` supplies slot numbers0..15. Its other three
words are zero for every type0 slot, so these entries add no secondary software
allocation bits. These allocator words are not raw IRQ callback masks.
This establishes a complete finite slot domain, rather than an index bound
inferred from the first observed read.

Allocator `1038917c` first checks the guest software resource bitmap. For type0
it additionally calls `103bd1d6`, which reads PA `20102040 +4*slot`. A zero
result admits that candidate; a nonzero result searches other candidates or
enters the original timed wait. Only after admission does original software
set its resource bit and write the selected slot into the graph descriptor.
The software allocation and hardware query are separate ownership checks.

Dispatcher `103bd04a` traverses the selected graph. ROM callback table
`1078a2a0` selects `103bda1e` for slots0..15; address table `10789cc0` selects
PA `20100400 +40*slot`. The callback packs and writes decimal13 consecutive
words, writes the slot number to PA `2010002c`, then delays eight counter
ticks. There is no completion poll or software resource release in that
setter. The delay alone does not establish whether physical busy covers
parameter import, data processing, or both.

## Finite interface

The maintained implementation is `hw/nikon/devices/gate.c`. Each packet occupies
`[20100400 +40*slot, 20100434 +40*slot)`, with aligned little-endian32 accesses.
The remaining three words in each `40`-byte stride remain holes. Query words
occupy exactly `[20102040,20102080)` and are read-only. Selector `2010002c` is
write-only and accepts only slot numbers0..15.

| Packet word | Source packing | Accepted generated-bit union |
| --- | --- | --- |
| 0 | Constant bit0 and packed fields from software payload bytes32..35,54,55 decimal | `ff771f71` |
| 1 | Packed fields from payload bytes36..40,56..60 decimal | `0001ff3f` |
| 2,3 | Four complete halfwords from payload offsets42..48 decimal | Full32 |
| 4..7 | Low17 bits of four payload words at offsets0..12 decimal | `0001ffff` each |
| 8 | Low15-bit halfword and low14-bit word, packed into two halves | `3fff7fff` |
| 9..12 | Four complete payload words at offsets16..28 decimal | Full32 each |

These are the setter's generated value shapes, not recovered physical reserved
bits, pixel formats, pitches or a license to treat every word as a DMA address.
The model retains fields and supports readback as an explicit functional
convention. In particular, the original observed packet contains small offsets
`0`, `12000` and `24000`; no standalone physical-buffer interpretation or DMA
is inferred from those values.

All thirteen words must have been staged since the previous successful capture
before selector2c can publish, and word0.bit0 must be set. A successful capture
copies the actual packet into the selected committed snapshot and invalidates
only its staging coverage. Reads return that committed snapshot, initially zero;
partial or malformed reloads leave it unchanged. A subsequent complete packet
can replace it before processing starts. Read-only QOM `parameter-valid` and
`parameter-commits` expose installed slots and successful publication count.

This synchronous capture/readback is an explicit functional convention. Source
proves the CPU writes, selector and eight-counter-tick settling wait, not a
hardware latch latency. No timer clears a busy bit. Configuration publication
does not release software resources, complete pixels or produce pending/IRQ.

The [paired MAIN transport](input.md) adds one explicitly modeled data owner:
a real queued slot8 input makes its PA `20102060` query nonzero until cancellation.
Parameter capture alone still does not acquire ownership. This finite input
occupancy does not establish downstream processor completion or software
resource release.

At the historical request-admission checkpoint, the query bank's quiescent zero
was a functional availability hypothesis after actual parameter capture,
not a recovered physical busy law: no processing
node had an outstanding start. Once a node was requested, both
hardware-availability queries and further parameter publication remained explicit
errors until active frame ownership was modeled. The current
[input and processing ownership contract](input.md) extends that checkpoint
with actual owned input and completed output DMA, under the bounded
[continuous profile](processing.md). Staging remains independent;
resetting a node cannot complete that work or alter committed parameters.

Incomplete/repeated capture without restaging, reserved generated bits, query
writes, unknown slots, holes, widths and unaligned accesses reject without
installing partial parameters. Whole-model reset clears staging, snapshots,
validity and counters. Stop acknowledgment and W1C preserve configuration.

## Completion and release boundary

The type0 setter supplies configuration without a completion callback; its ROM
allocation descriptor's zero words do not define raw IRQ routing. Selector2c
therefore has no modeled task event or IRQ. Worker `103b4adc` configures the
graph through `103bd04a`,
then `103b4cea` starts later graph nodes and constructs its wait mask from the
terminal nodes. Original IRQ102 handler `103bce38` reads/acknowledges three
raw pending domains and calls `103bde3e`; that dispatcher invokes the original
registered software callbacks. None of these guest calls is invoked by the
host model.

Callback `10388b3c` handles worker completion/termination notifications and
calls `1038897e`, which clears the guest's allocated type masks and worker
ownership. On the normal `421930f1` route it subsequently wakes the original
resource waiter. This releases software resources; it does not write the
hardware slot query bank. At the historical slot-stage checkpoint, the graph
processor and active frame-ownership transitions were still missing. The
current [continuous contract](processing.md) requires all three actual output
DMAs before its caller may publish the selected terminal causes; parameter
reload alone still cannot complete work. Guest callback execution and software
event updates remain guest-owned.

The historical baseline continuation submitted slot1, then stopped in graph
node1, type4/resource37: callback `103bdc72 ->103bded0` writes PA `20100814=1`
before deasserting that command-buffer reset and supplying the payload pointer
at `20100810`. The eleven-node graph is
`0/1,4/37,15/69,12/65,17/72,17/73,11/61,1/20,7/40,10/53,0/2`
(`type/resource`, all decimal). The first payload is a decimal72-byte command
packet at VA `b2bdb3a8`, not an empty processing request. Both input-port enable
nibbles and both retained64-word maps are still zero at this stop. Completion
cannot be justified by the already queued type0 packet or by a nominal delay.
The next contract must recover the downstream command-buffer parser, its
actual data operation and the terminal result/event path together.

## Historical validation and corrected hypothesis

The original unavailable-query symptom first fails a native HMP read check.
After the change, the applicable `nikon_sla` batch passes all23 subtests. The
expanded gate fixture covers all sixteen packets/queries, every generated
field family and full-word payload, complete versus partial staging, isolated
busy transitions, immutable ownership, elapsed time, stop/W1C independence,
reset cancellation and negative slot/width/bit/hole boundaries. This is a
synthetic gate-contract fixture, not an original SLA boot or a processed frame.
The [historical receipt](/evidence/imagerequest/slots.json) retains those exact
measurements. Its busy-on-commit interpretation is superseded by the observed
pre-start reconfiguration and corrected capture contract above; those old
fixtures do not establish actual processing ownership.

The historical slot-stage unchanged-firmware continuation uses the same bounded resource35,
cold-neutral resource80, quadrant input, healthy paced battery peer and five-CPU
MTTCG profile as its baseline. The60-host-second window has no debugger pause;
QMP observations are followed by a read-only debugger snapshot. It completes128
main acquisitions and448 statistics jobs, with zero statistics rejections or
cancellations. Slot1 is busy and all other slots are idle; original error
property6433 and both GUI information records remain zero. The first uncovered
transaction is CPU1 PC `103bdedc`, WRITE32 PA `20100814=1`. Neither display head
has a console. These are sequential observations, not an atomic snapshot or a
physical performance measurement; normal GUI startup remains incomplete.
