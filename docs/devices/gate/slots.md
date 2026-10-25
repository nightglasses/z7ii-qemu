# Type0 parameter slots and processing ownership

C1.70 MAS declares sixteen type0 resource slots. The finite event-gate model
now retains complete thirteen-word packets and makes each committed slot busy.
It does not retire a slot on elapsed time, configuration, W1C or the separate
stop acknowledgment. The historical command-buffer stop below is superseded
by the [processing command contract](commands.md); a normal completion/release
cycle has not been demonstrated. See the [receipt](/evidence/imagerequest/slots.json)
and [current startup frontier](../../validation/frontier.md).

## Original producers and consumers

Code and software-object addresses below are original MAS virtual addresses;
device addresses are board physical addresses in the functional machine. Sizes,
offsets and addresses are hexadecimal unless explicitly marked decimal.

Initializer `10388c3a` sets type0's resource count at VA `b1d52252` to decimal16.
Selector `103bd930(type,index,out)` copies a sixteen-byte ROM descriptor;
type0 table `10789dc0..10789ebf` supplies slot numbers0..15. Its other three
words are zero for every type0 slot, so these entries add no completion mask.
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

All thirteen words must have been written since reset before selector2c can
submit an idle slot, and word0.bit0 must be set. Successful submission changes
only that slot's ownership to busy. Repeated/invalid submission, partial
packets, writes to a busy packet, reserved generated bits, query writes,
unknown slots, holes, widths and unaligned accesses reject before changing
packet or ownership state. Whole-model reset cancels every retained request,
clears packets/staging and restores idle. The ordinary stop acknowledgment
and pending W1C preserve these requests; neither is a data completion.

Cold idle, retained readback, full-packet admission, immutable busy ownership,
the boolean0/1 query encoding and whole-model reset are bounded ownership
hypotheses. Source establishes zero/nonzero admission, packet shape and submit
ordering, but not physical busy encoding or reset timing. No processor or
automatic completion timer is attached: once submitted, ownership remains busy
because the downstream processing contract is still unsupported.

## Completion and release boundary

The type0 descriptor's zero completion masks prevent assigning a task event or
IRQ to selector2c. Worker `103b4adc` configures the graph through `103bd04a`,
then `103b4cea` starts later graph nodes and constructs its wait mask from the
terminal nodes. Original IRQ102 handler `103bce38` reads/acknowledges three
raw pending domains and calls `103bde3e`; that dispatcher invokes the original
registered software callbacks. None of these guest calls is invoked by the
host model.

Callback `10388b3c` handles worker completion/termination notifications and
calls `1038897e`, which clears the guest's allocated type masks and worker
ownership. On the normal `421930f1` route it subsequently wakes the original
resource waiter. This releases software resources; it does not write the
hardware slot query bank. The hardware data-consumption condition that makes
a type0 slot reusable still needs to be connected to the real graph processor.
There is no guessed callback, raw bit or guest-memory update for that missing
edge.

The baseline unchanged-firmware continuation submitted slot1, then stopped in graph
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

## Validation

The original unavailable-query symptom first fails a native HMP read check.
After the change, the applicable `nikon_sla` batch passes all23 subtests. The
expanded gate fixture covers all sixteen packets/queries, every generated
field family and full-word payload, complete versus partial staging, isolated
busy transitions, immutable ownership, elapsed time, stop/W1C independence,
reset cancellation and negative slot/width/bit/hole boundaries. This is a
synthetic gate-contract fixture, not an original SLA boot or a processed frame.

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
