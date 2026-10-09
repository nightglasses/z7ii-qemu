# Status flags

`flags.c` models the six-word status and acknowledgement protocol consumed by
SLA's interrupt handler. It does not identify the physical block or supply its
unknown event producer. The unrelated `events` device at `2C012000` and the
`2001` register family remain separate.

## Source evidence

Official source5 and source10 agree on the complete handler
`10059904..10059970`: it reads six 32-bit words from the literal `10059CEC =
201A0300`, stores the snapshot on its stack, and writes the six original values
back to the same addresses. It sets two ordinary guest-RAM flags from bit0 of
the second and third words. If the guest callback is installed, it receives
identifier `410B0801` and the whole six-word snapshot. The model does not write
these guest flags or call the guest callback itself.

Registry `10059972..10059988` publishes software handle `14F` with object
`C100648C`, then registers handler `10059905` for IRQ `118` (`76` hexadecimal).
Initializer `1005998C..1005999A` enables that IRQ. Neither function configures
this MMIO family or produces an event. A whole official source10 reference
scan finds two references to the base literal and the six reads plus six
writes; no additional producer or configuration accesses were qualified.

The unqualified span between `1005999A` and the next official section
`10059DD0` is not classified as code, data, or a missing configuration API.
IRQ registration proves the firmware's chosen interrupt number; it does not
prove how a physical external producer asserts that line. There is no original
runtime witness for this family's MMIO yet.

## Finite interface

The board maps one little-endian region at `201A0300`, size `20` hexadecimal,
and connects its sole SysBus IRQ to the private interrupt controller's IRQ118.
Only aligned 32-bit accesses to these offsets are supported:

| Bank | Offset | Read | Write |
| --- | --- | --- | --- |
| 0 | `00` | Pending word | Acknowledge set bits |
| 1 | `04` | Pending word | Acknowledge set bits |
| 2 | `08` | Pending word | Acknowledge set bits |
| 3 | `0C` | Pending word | Acknowledge set bits |
| 4 | `10` | Pending word | Acknowledge set bits |
| 5 | `1C` | Pending word | Acknowledge set bits |

Offsets `14` and `18`, unaligned accesses, and other widths return
`MEMTX_ERROR` without changing state. Full 32-bit words are opaque callback
payloads; the model does not invent meanings or narrower hardware masks for
bits the ISR passes through unchanged.

Named GPIO input `event` has 192 lines: line `bank * 32 + bit` supplies that
bit's real external level. A low-to-high transition latches the corresponding
pending bit. Any pending bit asserts the single IRQ. Writes clear only their
set bits in the addressed bank; reads do not acknowledge or produce events.
No mask register, FIFO, DMA engine, time-based completion, or data producer is
inferred. Cold inputs are disconnected and low.

W1C acknowledgement, edge latching, any-pending IRQ aggregation, and zero
pending on reset are explicit functional conventions consistent with the
observed readback acknowledgement sequence. They are not measured silicon
semantics. Reset retains actual provider levels while clearing pending flags:
a held input cannot become a new event solely because the device reset.

## Native verification

`nikon_test_flags(QTestState *q)` uses one fresh standalone SLA qtest VM and
the stable child `/machine/sla/flags`. It exercises all 192 input bits, the
six-word snapshot/writeback sequence, bank independence, new-bit retention
across an older acknowledgement, disconnected time progression, rejected
access state retention, and reset with held input followed by a genuine new
edge. The qtest invalid-access checks assert retained state, not the transport's
`MemTxResult`. Sole build ownership remains with the project builder; this
device task does not claim compilation or runtime success before that result.
