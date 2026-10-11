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
| Query loop threshold read | `0x103c5aea` |
| Query return | `0x103c5af8` |
| Record size | `0x0c` bytes |

The query computes `root + index * 0x0c`, reads the following record's fourth
word as a threshold, and advances until that threshold is at least the target.
This is an address-class lookup. It does not by itself identify a RAM bank,
device, DMA AddressSpace, or completion provider.

The observed threshold sequence includes:

```text
0x14000000  0xb5f00000  0xb7d00000  0xbc100000
0x19c00000  0x50300000  0x50700000  0x5f900000
0xbd200000  0x50000000  0x50200000  0x50300000
0x50700000
```

During the runtime query that was captured, the firmware searched for target
`0xbaa00000` at index `0x0a`; the next threshold read at
`0x103c5aea` was `0x50200000`, and the search continued. This shows that the
table contains a boundary at `0x50200000`; it does not show that the boundary
is backed by a guest RAM region.

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

