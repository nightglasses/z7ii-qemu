# Processing command import and terminal ownership

C1.70 MAS type4/resource37 submits a 72-byte logical-register packet through
PA `20100810`, after pulsing `20100814`. The initial command-import contract
covers seven processing types, their paired endpoint configuration
and CPU-written lookup tables. Original execution crosses this boundary and
imports eight packets before stopping in the separate paired-event banks at
the command-import checkpoint. The later [paired parameter contract](../pairs/parameters.md)
crosses that historical stop; its startup observation is reported separately.
At this historical checkpoint, actual pixel processing, terminal completion
and type0 slot retirement remained unsupported. The current
[continuous processing contract](processing.md) adds one bounded graph and
separates its functional choices from these earlier measurements. See the
[execution receipt](/evidence/imagerequest/commands.json)
and [startup frontier](../../validation/frontier.md).

The later [type5/type6 stream contract](fanout.md) adds two source-selected
ports and their356-byte prefix layout. The [linked type8/type9 contract](linked.md)
adds eight more ports and their128/136-byte streams. The complete
[type family](family.md) adds the remaining five ports, including a strictly
bounded type14 layout hypothesis. The maintained port set has42 entries;
the original seven-type/27-port observation below retains its historical scope.

Code, ROM and software objects below are original C1.70 MAS virtual addresses
(VA); device addresses are board physical addresses (PA). Addresses, offsets
and masks are hexadecimal; byte lengths, counts and resource/type numbers are
decimal. Ranges exclude their upper endpoint. Device accesses are aligned
little-endian 32-bit words.

## Source producers and packet meaning

Graph dispatcher `103bd04a` selects setters through ROM table `1078a2a0`.
Type4 setter `103bdc72` calls `103bded0(resource,payload)`: write backend+`14`
with `1`, then `0`, then write the payload pointer to backend+`10`. For
resource37 the backend is `20100800`, and the observed payload is at VA
`b2bdb3a8`. This is a command-buffer reset and import sequence, not a request
to declare the graph complete.

Each packet contains blocks. A header's low16 bits select an aligned logical
register offset; bits16..23 give the block's total word count, including the
header. Its high byte is `11` for another block or `01` for the final block.
The following words populate consecutive logical registers. These offsets are
inside the selected processing unit, not additional board physical addresses.
The same-architecture builder `102e2a10` independently constructs the 72-byte
type4 form and its routing mutation. Constructors `103b3226`, `103b3f04`,
`103b4214`, `103b4346`, `103b46ec` and `103b49b0` establish the other accepted
layouts; all current original graph packets agree with their headers.

| Type | Blocks: logical offset / total words | Packet bytes |
| --- | --- | --- |
| 4 | `4/2`, `10/16` | 72 |
| 5, 6 | `0/4`, `200/81`, `400/4` | 356 |
| 7 | `0/22`, `64/5`, `80/10`, `c0/10`, `100/10`, `22c/5` | 248 |
| 8 | `0/28`, `78/4` | 128 |
| 9 | `0/34` | 136 |
| 10, 11 | `0/10`, `40/8`, `80/17` | 140 |
| 12 | `0/7`, `20/2`, `30/6`, `50/6`, `70/6`, `90/6`, `b0/6`, `d0/5` | 176 |
| 13 | `0/4`, `80/26` | 120 |
| 14 | `0/6` (hypothesis; real template unrecovered) | 24 |
| 15 | `0/3`, `10/14` | 68 |
| 16 | `0/8` | 32 |
| 17 | `0/2`, `20/15`, `60/5`, `80/7` | 116 |
| 18 | `0/3`, `10/7`, `40/2`, `60/9` | 84 |

For type4 this means logical register `4` followed by fifteen words at
`10..48`. The graph edge mapper `1038944a` supplies register `4`'s routing
code; register `10` bits4..7 identify the upstream slot. The observed values
are `5a` and `10`, respectively, for upstream slot1. This establishes graph
routing and configuration import. It does not establish the remaining words'
pixel arithmetic, crop/filter equations or output extent. Full32 data words
are retained without inventing such meanings.

The source resource tables and backend pointers give the complete following
port set. Resource numbers are decimal; backend offsets are hexadecimal from
PA `20100000`.

| Type | Resource → backend offset |
| --- | --- |
| 4 | `37→800`, `38→820`, `39→840` |
| 5 | `35→e60` |
| 6 | `36→e80` |
| 7 | `40→880` |
| 8 | `41→8a0`, `42→8c0`, `43→8e0`, `44→c80` |
| 9 | `45→900`, `46→ac0`, `47→cc0`, `48→ce0` |
| 10 | `49→940`, `50→960`, `51→980`, `52→9a0`, `53→9c0`, `54→9e0`, `55→ba0`, `56→bc0` |
| 11 | `57→d40`, `58→d60`, `59→d80`, `60→da0`, `61→dc0`, `62→de0`, `63→e00`, `64→e20` |
| 12 | `65→a40` |
| 13 | `66→a80`, `67→aa0` |
| 14 | `68→a60` |
| 15 | `69→d00`, `70→d20` |
| 16 | `71→e40` |
| 17 | `72→a00`, `73→a20`, `74→b60`, `75→b80` |
| 18 | `34→920` |

## Finite import lifecycle

Implementation lives in `hw/nikon/devices/gate.c`, with packet decoding in
`hw/nikon/devices/gatepacket.c`. The local upload pulse opens a replacement
transaction and deassertion arms one import. It retains the committed packet,
pointer, configured flag and already owned numerical work until a successful
replacement, as required by the [online upload contract](processing.md#device-ownership-and-scheduling).
Type10 setter `103bdc92` stages auxiliary words at backend+`8/+c` before the
pulse; a successful import commits those words together. Whole-device reset
clears every packet, auxiliary word and import counter.

Pointer submission requires an armed, deasserted unit, an aligned nonzero
address and a complete readable RAM source. QEMU MemoryRegion checks reject
MMIO, holes, overflow and incomplete spans before DMA. The decoder validates
the entire fixed layout, lengths and final marker before atomically retaining
a snapshot. A failed import does not consume the arm or install a partial
packet. Repeated submission requires another reset pair. Pointer/configuration
readback, cold empty state, atomic replacement and this one-import arm are
explicit functional conventions; source establishes the successful ordering,
not all physical error/reset behavior.

The model exposes read-only QOM import counters and a64-bit configured mask
for all42 ports. The later [request contract](../pairs/requests.md) admits
backend+`0=1` only after complete import and retains a pending node request.
Readback0/1 is a finite incomplete-state convention, not physical status bits.
Source predicate `103bdbbe` checks `(status & 3) != 3`; it does not establish
bit1 alone as busy. Request admission alone returns no successful completion
encoding and executes no data operation. The later [continuous profile](processing.md)
requires owned input and actual output DMA before its caller publishes completion.
Unknown packet variants, ports, local words, widths and commands remain errors.

## Paired endpoints and CPU-populated storage

Setter `103bdc12` configures eight type1 resources16..23 through
`[20104000,20104080)`, stride `10`, words `0/4`. Their generated-bit masks are
`773` and `3fff7fff`; the two trailing words remain holes. These are retained
configuration, not paired processing results.

Type12 helper `103bdcee` imports its 176-byte packet and then reads six
`{byte length,pointer}` records after it. In the observed profile each input
contains 2048 bytes: the CPU loads 1024 halfwords and writes them as 1024
32-bit device entries. ROM table `1078a670` orders the destination banks as
`24000000,24002000,24004000,24001000,24003000,24005000`. The model backs the
complete `[24000000,24006000)` store, retaining low16 values in aligned
32-bit entries and rejecting other widths/high bits. It neither generates
factory LUT values nor applies an unmodeled transform.

Helper `103bdd0e` reads `20100148` before selecting each LUT bank, but the
switch overwrites that read for every valid index0..5. Another helper,
`103bdd8c`, clears SYS41..44, tests bit8 and, when absent, CPU-clears the
4096-byte store `[24010000,24011000)`. Four 1024-byte sub-banks are also
selected by `103bd552`. SYS operation `103c5d86` is a banked bit RMW, not a
completion producer.

**Functional hypothesis:** `20100148.bit8` records provisioning of all 1024
workspace words. It remains zero until the CPU actually writes every word;
duplicate writes do not increase coverage. Only then does it read `100`.
Command reset preserves this separate store; whole-device reset clears data
and coverage. Source proves the conditional CPU initialization, not this
physical status meaning. A later consumer requiring a different interpretation
would falsify the hypothesis. The original graph at this checkpoint reads the
word during LUT loading; it does not demonstrate the hypothesized bit8 transition. No
clock, nominal delay, configuration write or processing result sets it.
The later [type18 provisioning contract](family.md#type18-provisioning-and-result-ownership)
handles the explicit `201002c0=1` request by invalidating write coverage before
the original CPU clear; it does not generate the bytes or a processing result.

## Terminal completion and slot release

Worker `103b4adc` configures the graph, then `103b4cea` starts later nodes and
forms its wait mask from resources49..56. The ROM resource descriptors give
type4 resources37..39 software allocation-group1 bitmasks `1/2/4`,
type10/resource53 allocation-group1 bitmask `4000` and type11/resource61
allocation-group2 bitmask `4000`. These are allocator ownership words, not raw
callback masks; type0's descriptor has no additional allocation bits. Identical
allocation values in different groups are independent resource ownership bits.
They do not determine a hardware cause or make resource61 a terminal source.

Actual raw-domain1 pending is at PA `2010001c`, and raw-domain2 at PA
`20100020` (zero-based domains). For the later continuous worker9 graph,
resources49/54/55 contribute software terminal bits `10/200/400`, combined
`610`, through raw-domain1 causes0/5/6, combined `61`. The separate whole-frame
raw-domain2 bit3 (`8`) association is a [functional hypothesis](processing.md),
not a deduction from those allocation descriptors.

Original IRQ102 handler `103bce38` acknowledges raw pending and calls
`103bde3e` to dispatch registered guest callbacks. Callback `103b4b9e`
normalizes callback mask classes into worker event bits; the `40/4000` class maps to
event `80`. On normal worker completion, `10388b3c ->1038897e` clears software
resource/worker ownership and wakes the waiter on the `421930f1` route. This
does not write the hardware slot query bank.

These source paths located consumers at the historical import checkpoint, but
did not supply its missing pixel processor or completion condition. The current
[continuous model](processing.md) performs actual bounded output DMA before
its caller may publish the selected causes; it still does not invoke guest
callbacks or edit guest software events. Local command reset,
LUT writes, stop acknowledgment and W1C are not terminal processing results.
The operation's input association, output DMA extent and terminal publication
condition remain prerequisites; successful packet import alone cannot establish them.

## Historical validation and original execution

This section preserves the command-import checkpoint. The later
[request contract](../pairs/requests.md) admits configured starts and replaces
the old busy-on-parameter-publication hypothesis; the counts below remain
observations of that earlier model.

At the original command-import checkpoint, the old device fails the focused
reset read at `20100814`. The completed
native batch passes two decoder subtests and all23 SLA subtests. It covers all
27 ports, seven layouts, truncation and header corruption, complete versus
rejected RAM DMA, reset isolation/cancellation, auxiliary preservation, all
paired configuration words, six full LUT banks and workspace write coverage.
Unimplemented activation preserves imported state, remains incomplete after
elapsed time and generates neither pending nor IRQ. These are synthetic
contract fixtures, not an original SLA boot or pixel-processing receipt.

With unchanged firmware and the same bounded resource35/cold-neutral resource80,
quadrant scene, paced healthy UART and five-CPU MTTCG profile, the original
60-host-second continuation imports eight packets totaling269 words. It also
programs six LUT banks and both type0 slots1/2 remain busy. Both input-enable
nibbles are zero, but original CPU writes now populate map indices1/2,
17/18,33/34 and49/50 with buffer addresses. This records an upstream buffer-map
join, not proof that the processor has consumed those buffers.

The first uncovered access moves to CPU1 PC `1036a2ee`, WRITE32 PA
`203800a8=4`, in the separate paired-event banks before processor activation.
The [receipt](/evidence/imagerequest/commands.json) records exact counts and
limits. GUI readiness/information remain zero and neither display head has a
console. Failed observer attempts and a separate CPU-trace diagnostic are
retained separately; stale post-call heap contents are not treated as executable
operator evidence. No complete normal GUI or reusable processing-slot cycle
is claimed.
