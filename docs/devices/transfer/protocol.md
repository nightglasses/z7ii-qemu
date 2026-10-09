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
copies the resulting contiguous byte extent. Contiguous rows follow the
producer's dimension-only packet; unlike an imported surface's software
stride, that command has no independent row-pitch register. Rotation, format
conversion, unequal geometry and combined secondary-plane commands remain
outside this implemented protocol rather than being silently treated as copy.

`106ee4e4` registers IRQ129/130/131 using the original literal words at
MAS O`6ce584/588/58c` (`106ee3cb/106ee3d7/106ee3e3`). The respective Thumb
handlers `106ee3ca/106ee3d6/106ee3e2` write zero to `20181020/20180c18/2018001c`
and dispatch software notification masks `1/4/2` to the same continuation.
The registration and kernel user-handler table consumer form the static
binding; the guest has not yet run these handlers in the original boot.

The byte-stream model snapshots a complete packet, validates the complete
source and destination ranges against actual directly backed memory, and
uses QEMU DMA to copy bytes. Its virtual timer raises IRQ130 only after all
bytes have been written. `c18=0` acknowledges the IRQ, or cancels a transfer
before its next byte batch. Address/configuration registers remain the
original packet values; internal progress is separate. A failed DMA never
raises a successful completion. Unknown commands, addresses, layouts and
access widths fail through the common access diagnostic.

Zero reset state, a nominal 48MHz clock, 4096-byte scheduling batches and
cancel-before-next-batch semantics are functional-board timing/reset
assumptions. They are not measurements of the Nikon transfer engine.
The interrupt ACK address and mask binding have source evidence; the level
completion timing is a functional model of that software-visible protocol.
The complete initializer `106edfd6` writes `1020=0,1248=1,c18=0,1c=0`,
registers the three handlers, then writes `34=0,c1c=8000,151c=1,155c=1`.
Those finite configuration fields are retained. The `c1c` bit15 enable
interpretation is a functional convention grounded in the installed value;
clearing that field suppresses the modeled completion IRQ. It is not an
independently measured silicon register definition.

The source file registers `nikon-transfer-functional`, a 0x2000 SysBus MMIO
region with IRQ outputs 129,130,131 in that order. Board mapping is
`PA20180000`, subject to the original page-table identity mapping evidence;
machine/header/Meson integration belongs to the shared integration owner.

The native test content in `tests/qtest/nikon/transfer.c` checks data movement,
unchanged packet addresses, delayed completion, IRQ130, and cancellation.
The sole build owner registers and runs it; no independent build was run by
this implementation lane.

Remaining work is active, not accepted as a completed GUI: conversion and
multi-plane geometry commands, the other two IRQ producers,
status58 bit26 and command1214, and final scanout must be joined to their
complete saved source. The DMA destination is not presented as a screen.
Console output must consume the guest's actual scanout commit and layout.
