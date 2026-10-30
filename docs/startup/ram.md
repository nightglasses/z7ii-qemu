# MAS RAM backing alignment

The normal GUI continuation's host TLB assertion was caused by the MAS board's
RAM mapping helper. It mapped an independently allocated RAM block directly at
a non-page-aligned physical base. The fix preserves every physical base and
length, with a private backing prefix and an exact alias at the original base.
The RAM-only original run reaches a 2120/2368-byte graphics submission.
The later [context continuation model](../devices/transfer/continuation.md)
completes it. Later graphics progress is recorded in the
[current frontier](../validation/frontier.md). GUI first frame remains unverified.

## Captured failure

The unchanged bundle reproduced `tlb_set_page_full`'s
`!(iotlb & ~TARGET_PAGE_MASK)` assertion. A temporary conditional diagnostic in
the ignored native source graph captured these values immediately before the
same assertion; the official QEMU checkout was untouched:

| Field | Value and domain |
|---|---|
| CPU / PC | MAS CPU0 / original Thumb VA `10634fbe` |
| Access | MAS VA `38f661c8` → board PA `bed661c8` |
| Physical page | PA `bed66000` |
| MemoryRegion | `z7ii.resource57-record0` |
| Region base / length | PA `bec5f200` / `4b0000` bytes |
| Translation offset | RAM-relative `106e00` |
| RAM block address | QEMU internal RAM address `0f420000` |
| Computed IOTLB | QEMU internal RAM address `0f526e00` |

The arithmetic identifies the cause directly:
`bed66000 - bec5f200 = 106e00`, then
`0f420000 + 106e00 = 0f526e00`. The physical page is aligned; the RAM address
retains a low `200` offset. This is a board mapping error, not evidence that
the original guest supplied a malformed physical address. A separate synthetic
TCG access at PA `bec5f400` reproduces the same invariant in the same region.
Host LLDB launch failed to acquire attach permission; the conditional log was
the successful diagnostic method.

## Exact mapping contract

`z7ii_ram` now creates one `Z7IIRAM` pair per existing slot:

- `offset = base & (qemu_target_page_size() - 1)`.
- The migratable backing has length `size + offset` and is not added to the
  physical address space.
- The visible alias has the original `base` and exact `size`, selecting backing
  bytes `[offset, offset + size)`.

For record0 the private prefix is `200` bytes. An aligned physical page now
selects an aligned RAM address because its backing translation includes that
prefix. Neither prefix nor allocator-rounded tail becomes accessible RAM.
Adjacent published slots remain independent; holes remain transaction errors.
All callers use the same helper, including aligned storage classes, resource57,
LCD/display buffers, kind40, row storage and the small early stack/release slots.
MXIC attaches to each visible storage-class alias, so decoder enable/disable
still applies to the original physical interval rather than backing padding.

The fixed upstream initializes and finalizes target page size in
`machine_memory_init` before board initialization. The helper therefore uses
the selected target page size, not a host-page-size assumption. Existing SLA
and VR helpers independently use the same backing/alias principle. This change
does not alter firmware, PTEs, CPU state, original allocation sizes, RAM contents,
reset persistence or the declared cold-zero storage assumptions. It establishes
no additional physical-camera DDR geometry.

## Validation and next stop

The new `/arm/nikon/memory/cpu-ram` qtest fails on the old helper with the
same host assertion. It runs synthetic ARM code through actual TCG, probing
39 addresses covering partial pages, interior pages, last words and original
PA `bed661c8`. It checks bus writes read by the CPU, CPU writes read by both
the CPU and bus, then repeats after reset. Existing memory cases cover exact
holes, adjacent slot independence, reset retention and MXIC decode toggling.
The complete applicable native batch passes: memory qtest 26, memory unit 3,
transfer unit 14, totaling 43 cases. Independent read-only review found no
remaining concrete defect in page-size timing, ownership, mapping or fixtures.

The diagnostic source link was restored and its object removed before the
green native build, which recompiled the unmodified upstream TLB source.
The final unchanged-bundle MTTCG run uses the same descriptor/TEMP, RNG 0,
acceleration, cold F4 and normally paced healthy UART inputs as before.
It crosses the old host failure, completes the 856/1088 submission through
original IRQ 129, and stops on CPU0 WRITE32 PA `201810e0=1`, original
PC `106ee3c6`, reason `transfer-stream-truncated`. QEMU remains alive in debug
state and a read-only GDB snapshot completes. The UART peer rejects no request;
BatCk is `00010001`, and GUI gate/pending3/display-ready are still zero.

The complete next buffers are 2120 bytes at PA `bd0e02f0` and 2368 bytes at
PA `bd1603d0`. They contain ten signed integer rectangles with target
`bec5f200`, paint zero, alternating scratch operands and keys `10005..1000e`.
Setup has ten 53-word records without the earlier two-word initialization;
draw has ten 59-word transition/body groups and a two-word ending. The earlier
self-contained initialization prefix is absent. At this RAM-only checkpoint
context continuation was unsupported; a length fallback's truncated diagnostic
does not establish genuinely incomplete guest commands. The subsequent
[continuation model](../devices/transfer/continuation.md) implements that
contract separately from the RAM fix and completes this pair through IRQ129.

Ignored receipts: `run ramalignment/` contains the diagnostic
probe, native red/green logs, normal green argv/QMP/GDB/trace, both complete
command streams, and `verified-progress.json`. The diagnostic run and synthetic
tests are separate from the final unchanged-firmware continuation.
