# 0x50200000 runtime consumer boundary

This note records a read-only execution trace of the unchanged Z7II C1.70 MAS
payload. It describes the firmware's address interval lookup and the later
COPY submission. It does not assign a QEMU owner to the destination.

## Observed COPY request

The first uncovered request reaches the firmware COPY submit wrapper with:

| Field | Value |
| --- | --- |
| Submit callsite | `0x10244966` |
| Submit wrapper | `0x103a3b66` |
| Channel | `0` |
| Source | `0x122acf40` |
| Destination | `0x50200000` |
| Length | `0x00027800` |
| Descriptor | `0x122d4740` |
| Descriptor word 0 | `0x00027800` |
| Descriptor words 1–3 | `0x00000000` |

The payload is unchanged and the observer did not write guest memory or inject
an event, IRQ, completion, or framebuffer. QEMU reaches the COPY bus-error
boundary on the destination write. The firmware continues after status `1`, so
the QEMU first-fault record and the firmware's later submissions are separate
observations.

## Firmware interval table

The firmware builds an address interval table rooted at `0xb2b4145c`.

| Operation | Address |
| --- | --- |
| Builder entry | `0x103c585c` |
| Builder region writer | `0x103c58fc` |
| Query entry | `0x103c5ad8` |
| Query loop | `0x103c5ae0` |
| Next-record threshold load | `0x103c5ae8` |
| Query return address calculation | `0x103c5af0` |
| Record size | `0x0c` bytes |

The query starts with `index = 0`, computes `root + index * 0x0c`, and loads
the next record's first word at `record + 0x0c`. It subtracts one from that
threshold and advances while the result is below the input. The return value is
`root + index * 0x0c`. This is an address-class lookup. It does not by itself
identify a RAM bank, device, DMA AddressSpace, or completion provider.

The observed threshold sequence includes:

```text
0x14000000  0xb5f00000  0xb7d00000  0xbc100000
0x19c00000  0x50300000  0x50700000  0x5f900000
0xbd200000  0x50000000  0x50200000  0x50300000
0x50700000
```

The twelve-byte records use three words, `[input_base, enabled, output_base]`.
The conversion wrapper is `0x103c5a7a` (the frequently copied `0x103a5a7a`
spelling is not a function entry in the MAS image). It saves the input, calls
the query at `0x103c5ad8`, checks record `+4`, then checks record `+8`. If the
record is disabled or has a zero output base it returns the original input;
otherwise it returns `output_base + (input - input_base)`. The sibling helpers
`0x103c5a96`, `0x103c5ab0`, and `0x103c5ac6` call the same query and consume
the record `+4`/`+8` fields for predicates or status. The relevant initialized
records are:

| input base | enabled | output base | next input base |
| --- | ---: | --- | --- |
| `0x50000000` | `1` | `0x50200000` | `0x54900000` |
| `0x54900000` | `1` | `0x62700000` | `0xaf200000` |
| `0xaf200000` | `1` | `0xafa00000` | `0xb0000000` |
| `0xb0000000` | `1` | `0xb1000000` | `0xb1800000` |
| `0xb1800000` | `0` | `0xbaa00000` | `0xbb300000` |
| `0xbb300000` | `1` | `0xc3e00000` | `0xcc700000` |

This makes `0x50200000` a software conversion output base for the logical
class beginning at `0x50000000`. It does not identify a RAM bank, a display
buffer, a DMA AddressSpace, or a completion provider.

During the runtime query that was captured, the firmware searched for target
`0xbaa00000` at index `0x0a`; the next-record threshold load at `0x103c5ae8`
returned `0x50200000`, and the search continued. This shows that the table
contains a boundary at `0x50200000`; it does not show that the boundary is
backed by a guest RAM region.

## QEMU ownership conclusion

The current machine map has no source-backed owner for
`0x50000000..0x51ffffff`. The active guest translation observed for the COPY
destination is identity (`0x50200000 -> 0x50200000`), which only proves the
guest page-table mapping. It does not supply physical storage or an external
device provider.

Therefore the first unmodeled operation remains the COPY DMA write to
`0x50200000`. No RAM alias, framebuffer, device window, completion IRQ, or
synthetic provider is justified by this evidence. The next required evidence
is a runtime producer/consumer that assigns an owner and establishes its
capacity and access semantics.
