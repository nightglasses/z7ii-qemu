# Graphics packet transfer

The original C1.70 producer `VA106eeb5c` fills a packet consumed by
`106ee148`. The latter writes source and destination addresses at offsets
`c00/c04`, secondary addresses at `c34/c38`, geometry at `c08/c0c`, control at
`c10`, and starts with `c18=1`. Control bit3 selects the alternate producer
which supplies byte extent at `c20` and zero extension/padding at `c24/c28`.
Its extent is computed from the two dimensions and the numeric byte factor;
neither a color name nor a framebuffer address is assumed. For ordinary
single-plane packets with equal geometry and equal numeric format selectors,
the corresponding byte factors are `4/3/2/2/2/1`. The unconverted transfer
copies the resulting contiguous byte extent. The original selector function
`106eeaf0` maps only format `1c` to selector5. Its ordinary command also
supplies a secondary address pair at `c34/c38`; the same producer's alternate
byte-stream equivalent gives that second plane the same width*height byte
extent (`width*height*8 >> 3`). This source-backed second plane is copied
after the first and completes as one packet, without interpreting either
plane's colors. Both extents are validated before any bytes are written.
Contiguous rows follow the
producer's dimension-only packet; unlike an imported surface's software
stride, that command has no independent row-pitch register. Rotation, format
conversion, unequal geometry and unbound secondary-plane formats remain
outside this implemented protocol rather than being silently treated as copy.

`106ee4e4` registers IRQ129/130/131 using the original literal words at
MAS O`6ce584/588/58c` (`106ee3cb/106ee3d7/106ee3e3`). The respective Thumb
handlers `106ee3ca/106ee3d6/106ee3e2` write zero to `20181020/20180c18/2018001c`
and dispatch software notification masks `1/4/2` to the same continuation.
The registration and kernel user-handler table consumer form the static
binding; the guest has not yet run these handlers in the original boot.

The shared continuation `106ee4c6` keeps notification values 2 or 4 and maps
other values to 1, then sends the first packet word through
`106d4ffe -> 105a65a4`. The corresponding query
`106ee436 -> 106d5034 -> 105a6626` uses the original 20-byte record at
`VA10788f00` (MAS O`768f00`): words `0,7,21,0,ffffffff` in hex. The query
replaces record+c with its output pointer. These are real guest event-handle
and wait/clear consumers; the device never fills their RAM or returns a
host-generated event flag. IRQ130's original handler supplies mask4, which
the subsequent command3 consumer explicitly checks.

The byte-stream model snapshots a complete packet, validates the complete
source and destination ranges against actual directly backed memory, and
uses QEMU DMA to copy bytes. Each DMA transaction is bounded by the current
effective source and destination MemoryRegion sections. In particular, a short
ROMD alias must switch to its adjacent RAM or ROMD backing, not read hidden
bytes from the aliased ROM device. Each timer batch rechecks direct access and
destination writability; losing either stops the packet without a successful
completion or an MMIO read. Its virtual timer raises IRQ130 only after all
bytes have been written. `c18=0` acknowledges the IRQ, or cancels a transfer
before its next byte batch. Address/configuration registers remain the
original packet values; internal progress is separate. A failed DMA never
raises a successful completion. Unknown commands, addresses, layouts and
access widths fail through the common access diagnostic.

Zero reset state, a nominal 48MHz clock, at-most-4096-byte scheduling batches and
cancel-before-next-batch semantics are functional-board timing/reset
assumptions. They are not measurements of the Nikon transfer engine.
The interrupt ACK address and mask binding have source evidence; the level
completion timing is a functional model of that software-visible protocol.
The complete initializer `106edfd6` writes `1020=0,1248=1,c18=0,1c=0`,
registers the three handlers, then writes `34=0,c1c=8000,151c=1,155c=1`.
Those finite configuration fields are retained. The later `106ee04a`
configuration also retains `1214=ffff` or `0100ffff` (the original boolean
branch) and `1298=0`. This admits its empty-table setup, not an upload job or
an arbitrary configuration aperture. Nonzero optional table lengths instead
supply source/count/start fields at `10d4/10dc/10d0` and
`10e4/10ec/10e0`. Their owned request frontend is described below;
processing/completion remains unsupported. No extra IRQ is raised by accepting
these two setup fields. The `c1c` bit15 enable
interpretation is a functional convention grounded in the installed value;
clearing that field suppresses the modeled completion IRQ. It is not an
independently measured silicon register definition.

The source file registers `nikon-transfer-functional`, a 0x2000 SysBus MMIO
region with IRQ outputs 129,130,131 in that order. Board mapping is
`PA20180000`, subject to the original page-table identity mapping evidence;
machine/header/Meson integration belongs to the shared integration owner.

## Owned graphics stream requests

The original `106ee04a` writes two independent source/count/start
triples at `10d4/10dc/10d0` and `10e4/10ec/10e0`. The start value is1.
The read-only `graphicsextent1` probe establishes that descriptor extent
fields56/422 count 32-bit words, while port counts28/211 count 64-bit units:
the full sources are224/1688 bytes (`8*count`), not56/422 bytes.
The corrected frontend at `8e113e58` requires explicit `1214/1298`
configuration and both operands, validates the entire `8*units64` direct-memory
source, and captures it using effective section boundaries. The former
`2*count` halfword assumption captured prefixes and is superseded, not
full-bound proof. The frontend does not parse command encoding, infer
LUT/program semantics, alter pixels or complete a graphics job. Each nonempty
submission remains pending without a processing provider.

The functional allocation bound is128 KiB per stream, not recovered silicon
capacity. Unaligned sources, oversized counts, zero/missing-count starts,
MMIO/unbacked sources, duplicate starts and pending operand changes fail.
The six stream ports are word-write-only; neighboring fields remain errors.
Named `stream-request[0..1]` outputs are host-facing, not GIC connections.
`nikon_transfer_stream_request()` returns an immutable, reference-counted
actual-byte snapshot with source, `units64` count and a nonzero ownership token.
Callers release the returned GBytes reference. Reset/unrealize cancel both
submissions and invalidate tokens without reusing them; held byte snapshots
remain readable. Cancellation is not processing or successful completion.

Read-only QOM pending/token/SHA256 properties describe those actual owned
bytes. No provider or completion API is connected. Time passing cannot clear
a pending stream or raise IRQ129/131. The existing1020=0 write does not turn
an unprocessed request into a successful result. Status58, fence operations,
processing replies and IRQ129 producer semantics remain unknown/unsupported.
The ordinary byte-copy engine and its real IRQ130 completion remain separate.

Historical frontend snapshots are56/422-byte prefixes of the full224/1688-byte
original inputs, not independent full-bound proof. Read-only hardware watchpoints
locate an actual first-header writer at `10645418` and a template-copy writer
at `10360f5c`. Following `106453cc/10645454` corroborates word-pair append and
bulk-copy production. The streams must not be casually called LUTs; these
producer observations do not establish a processor ISA or justify an
instruction interpreter. Z9's different command encoding is not transplanted.

The native tests in `tests/qtest/nikon/transfer.c` check data movement,
unchanged packet addresses, delayed completion, IRQ130, and cancellation.
`tests/unit/nikon/transfer.c` additionally checks byte-exact crossing of a
short ROMD alias into adjacent RAM and ROMD, untouched destination tails,
whole-packet MMIO rejection without side effects, and loss of ROMD direct
access or destination writability before a timer batch. The boundary test
failed its byte comparison on the old implementation before passing with
section-bounded DMA. An additional empty-configuration test checks both
1214 values, retained readback, reset, unknown-bit/neighbor/table-start
rejections and absence of all three IRQs after time advances. It failed on
the old first word write before passing with the finite setup extension.
The historical stream-submission case additionally checks real56/422-byte
snapshots under the old length assumption (synthetic transport coverage, not
validation of the full original extents),
independent ownership, caller-memory mutation isolation, MMIO preflight without
reads, unknown fields/counts, absent processing status, retained pending state
without any GIC IRQ, reset/unrealize cancellation and token invalidation. The
ROMD test also checks section-exact stream capture across both adjacent backing
types. The old frontend failed the initial10d4 word write before these tests
passed. These are synthetic device tests, not GUI boot evidence.

The corrected stream-submission test uses counts 28/211 and compares full
224/1688-byte snapshots. `work/research/z9reference/extent-red.log` records
the actual old-length failure (`56 != 224`), then `extent-green.log` passes
all four transfer unit cases. The added `stream-extent-boundaries` case checks
both banks, incomplete setup, zero/invalid starts, unsupported widths/read
directions/neighbors, the exact 128-KiB limit and over-limit counts, and a full
extent reaching adjacent MMIO without any MMIO read or IRQ. Reset/unrealize
cancellation and ownership isolation remain tested. The parent native receipt
`extent-nativecheck.log` passes 29 fixtures / 247 subtests; it is not GUI boot.
Two parent original-bundle `extentfixed1/2` observations match the full
producer-bounded stream inputs but retain pending requests and no wait return,
IRQ129 or frame; see [runtime evidence](../../runtime/coprocessor.md).

Remaining work is active, not accepted as a completed GUI: conversion and
other geometry commands, the other two IRQ producers,
status58 bit26, processing of the nonempty streams following configuration1214,
and final
scanout must be joined to their
complete saved source. The DMA destination is not presented as a screen.
Console output must consume the guest's actual scanout commit and layout.
