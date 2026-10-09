# Two finite configuration lanes

Complete official SLA `10089B06..10089B32`, in source11 `sla.S`
lines307267..307334 and original `sla-rows.jsonl`, selects the base from
literal `10089C00=2B001000`. Selector1 adds100; every other selector uses
the first lane. The machine therefore maps two separate20-byte regions at
`2B001000` and `2B001100`. The device does not map or accept the gap.

The original reads a software packet at `r2`. If word0 is nonzero it writes
word8=1, word4=0, wordC=packet.word4, word10=packet.word8, then word0=1.
The zero path writes word0=0 then word4=1. All MMIO accesses are aligned
32-bit writes. The whole method contains no MMIO read, poll, result, data
transfer, IRQ definition or callback invocation. Packet loads must not be
mistaken for device reads or byte MMIO.

`TYPE_NIKON_LANES` implements the complete finite configuration writer as
two independent SysBus regions. Each source store is retained in guest
program order. The first three words accept booleans; the two parameters
retain full words. Cold zero and full retention are functional assumptions.
There is no physical lane identity, output transformation, timer, completion
or reset side effect inferred from these fields. This layer does not claim
to implement an unknown downstream engine. Reads, unsupported widths,
misalignment and unsupported boolean values return `MEMTX_ERROR` with the
actual mapped address. No IRQ, ready flag or read-zero fallback exists.

The neighboring five-route signal interface has its own mature model and
is reused separately, as recorded in `docs/devices/signal/sla.md`. The
following source family from `10089C74`, selected by literal
`1008A028=2B200000`, is a different namespace, matching the existing
storage PHY aperture; neither adjacency nor this lane writer proves PCI or
storage behavior. Its complete protocol belongs to the storage owner.

`tests/qtest/nikon/signal.c` provides a fresh private-SLA fixture helper
which exercises both complete lane write paths and confirms no resulting
signal pending bit or IRQ. The sole builder compiled the private signal/lane
group and executed it with exit0 in1.69s; its ignored receipt is
`work/slasignalnativereceipt.log`. This establishes the finite functional
contract, not original downstream engine execution.
