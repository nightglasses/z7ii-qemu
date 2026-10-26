# Type5 and type6 command import and graph routing

C1.70 MAS type5/resource35 uses backend PA `20100e60`; type6/resource36 uses
`20100e80`. Both pulse backend+`14` before submitting a pointer at+`10`.
The observed 396-byte software structure contains a 356-byte register stream
and a separate 40-byte CPU-consumed trailer. The finite model imports only
the stream, retaining the original routing/table parameters without creating
pixels or completion. At this checkpoint original cold-neutral execution
crosses type5 import and stops at downstream type9/resource45 configuration.
The later [linked-stream contract](linked.md) crosses that historical stop
while keeping actual processing unsupported. See the
[receipt](/evidence/imagerequest/fanout.json) and
[startup frontier](../../validation/frontier.md).

Code, ROM and software addresses below are C1.70 MAS virtual addresses (VA).
Device addresses are board physical addresses (PA). Addresses and register
offsets are hexadecimal; lengths, counts and type/resource numbers are decimal.
Ranges exclude the upper endpoint. Device accesses and command words are
aligned little-endian 32-bit.

## Producers, routing and the two extents

Resource selector `103bd930` reads type5's record at `1078a010` and type6's
record at `1078a030`. Joining their resource IDs with backend table
`1078a2a0` gives resources35/36, backends `20100e60/20100e80` and the common
setter `103bddea`. That setter selects SYS54 for resource35 or SYS55 otherwise,
clears the selected SYS bit through `103c5d86`, then calls `103bded0`.
The latter writes backend+`14=1`, then `0`, then the payload pointer at+`10`.
These SYS operations and command reset do not establish a data-completion event.

Constructor `103b3172` operates on a 396-byte software allocation. Independently
matched MAS builder `102e2b7e` performs the same mutations. The upstream resource
ID, when in8..15, is written into payload offset4. Routing helper `103b2592`
collects up to four downstream branches from graph edges, converts their resource
IDs into route codes and packs four nibbles into payload offset348. The builder
clears bits8..11 at payload offset352, selects trailer channel8 for resource35
or channel9 for resource36, processes that trailer and flushes all396 bytes
through `1034f9ec`. Flushing the allocation does not mean every byte is a
device command.

The captured type5 stream at VA `b2bd9cd8` has these exact block boundaries:

| Payload byte offset | Header | Logical destination | Data words |
| --- | --- | --- | --- |
| `0x0` | `11040000` | `0/4/8` | 3 |
| `0x10` | `11510200` | `200..33c` | 80 |
| `0x154` | `01040400` | `400/404/408` | 3 |

Header counts include the header word. The last header terminates the stream
at offset `0x164`, decimal356 bytes. The first group contains `{8,30,202}`;
all80 captured table words are zero; the final group contains `{0,8,fe}`.
Logical register `0=8` selects the upstream resource. Routing register
`404=8` resolves through the source helper to downstream resource45, type9.
The eighty table words and other control fields are retained without claiming
their physical filter equations or treating zero as a bypass/identity operation.

The separate trailer starts at decimal356. It contains a four-byte prefix and
three twelve-byte records. Helper `10389652` collects three record words when
the prefix byte is not3, then calls `103bd43a`, `103bd3fe` and `103bd496`.
Those helpers use ROM tables `1078a550/1078a500` to write existing device
configuration and finally `20102800=1`. They are CPU stores, not an additional
DMA descriptor stream fetched by the command port.

For trailer channel8, the table selects `201028e0/e4/e8` and `20102920`;
channel9 selects `201028ec/f0/f4` and `20102924`. These are already retained
fields of the shared gate profile. In the captured channel8 trailer the three
collected values are `fe,fe,0`; source decoding predicts packed scalar
`00fe0580`. These are values derived from the observed trailer and source,
not a separate MMIO readback measurement. In particular, `fe` is not promoted
to a physical pixel-buffer address.

## Finite implementation and actual-processing boundary

`hw/nikon/devices/gatepacket.c` adds the shared fixed layout for both types.
Its internal logical-register snapshot extends through `408`; this changes
host-side state capacity, not board RAM or a physical MMIO aperture. The
existing gate import lifecycle supplies isolated reset/deassert/one-import
admission, complete RAM preflight, QEMU DMA and atomic decode. It reads exactly
356 bytes and requires all three exact headers, lengths and final marker.
Trailing CPU records are neither decoded as register writes nor fetched to
satisfy the import. See the shared [command lifecycle](commands.md#finite-import-lifecycle).

The shared type6 prefix shape is a bounded inference from its common constructor
and identical field-offset/extent mutations. Only type5's headers are observed
in original execution here. A type6 caller template with different headers
would require another explicit layout; the current decoder rejects it.

Cold empty state, readback, local reset cancellation and one-import admission
remain explicit functional conventions. Invalid source spans, MMIO pointers,
alignment, headers, held reset, unarmed or repeated submissions reject without
installing a partial packet. Resetting either type5/6 port cannot clear any
submitted type0 slot. Elapsed time, CPU trailer publication, W1C and the
separate stop acknowledgment cannot retire processing ownership.

The observed upstream slot8 retains a thirteen-word type0 packet, including
`560` strides and an offset `102ac0`. The input-port address maps still contain
the earlier slot1/2 addresses; they do not establish a complete slot8 physical
input extent. No DMA is inferred from those small offsets. Implementing an
actual pixel operation requires the source-selected base/plane association,
the table/control transfer equations and the output contract of downstream
resource45. Those semantics are unresolved; the current model does not copy
arbitrary buffers, fill black output or assume an identity transform.

## Completion, callbacks and slot release

Type5's resource record assigns raw-domain2 mask `80000`; type6 assigns
`100000`. Domain indices are zero-based, so domain2 is pending PA `20100020`.
Those source masks identify callback routing, not a license to assert an IRQ
when configuration is imported.

Worker `103b4adc` first configures the whole graph through `103bd04a`.
`103b4cea` then walks its nodes in reverse, starting processing resources
through `103bd07c`, which writes backend+`0=1`. Its wait mask is formed from
terminal resources49..56, rather than assuming a type5 import completes the
graph. Raw pending is consumed by original IRQ102 handler `103bce38` and
dispatcher `103bde3e`; normal software completion ultimately releases allocated
resources through `10388b3c ->1038897e`. There is no hardware query-bank write
in that software release.

The separate streaming worker `103b4e1e` also distinguishes graph configuration,
input enable, stop/drain and status queries. Source status helper `103bdbbe`
tests `(word & 3) != 3`; it does not itself produce a result or prove physical
busy timing. The finite command model supplies no successful processor start,
result DMA, raw event, host callback or automatic slot retirement. Import
success therefore cannot complete either worker path.

## Validation and unchanged-firmware continuation

The old device fails the focused HMP read at `20100e74`. The accepted native
batch passes three decoder subtests and23 SLA subtests. It covers both new
ports and the earlier ports, all fixed layouts, every type5 prefix truncation,
each header-bit corruption, logical-register bounds, reset isolation, invalid
DMA, unsupported activation and absence of time/pending/IRQ completion.
A deliberately invalid40-byte trailer cannot affect prefix import. Resetting
either command port preserves all sixteen busy type0 slots in the fixture.
These are synthetic device-contract checks, not original SLA image processing.

The final original run keeps the bounded NOR resource35/cold-neutral resource80,
quadrant scene, healthy paced UART, held F4 and five-CPU MTTCG profile. After60
host seconds with no GDB pause in the window, type5 retains pointer `b2bd9cd8`
with reset deasserted. Total imports increase to9 packets/358 words. The
eighteen-node graph begins `0/8 ->5/35 ->9/45`; its other configured and pending
nodes are recorded in the receipt. Slots1/2/8 remain busy and type6 remains
unconfigured. No actual processing activation is reached.

The first uncovered access becomes CPU1 PC `103bdedc`, WRITE32 PA
`20100914=1`, downstream type9/resource45's reset at backend `20100900`.
The run completes128 main acquisitions and542 statistics jobs, with no
rejections/statistics cancellations and one main cancellation. Error
property6433 and both GUI information records remain zero; neither display
head has a console. Sequential QMP and read-only GDB snapshots are not atomic
or physical rates. Normal GUI publication and processing-slot reuse remain
unverified.
