# MAS region translation table

The unchanged C1.70 MAS payload uses a software address conversion table before
submitting some COPY requests. This table describes pointer conversion; it does
not allocate RAM, identify a DMA owner, or prove a physical device mapping.

## Runtime COPY boundary

The current multi-thread receipt reaches the generic submit wrapper at
`0x103a3b66` with channel `0`, source `0x122acf40`, destination `0x50200000`,
and length `0x27800`. The producer return link is `0x1024496b`. The observed
descriptor pointer is `0x122d4740`, and its first word is `0x27800`.

The source block is inside the MAS image loaded at `0x10020000`:

```text
0x122acf40 + 0x27800 = 0x122d4740
```

Thus the source is a contiguous MAS ROM range ending immediately before the
observed descriptor address. The descriptor pointer does not establish the
destination's backing.

## Table layout and lookup

The initializer at payload offset `0x3a585c` (`VA 0x103c585c`) sets the table
root to `0xb2b4145c` and initializes equally spaced 12-byte records. A record
has this layout:

```text
word 0: input base
word 1: conversion enabled when equal to 1
word 2: output base
```

The lookup at payload offset `0x3a5ad8` (`VA 0x103c5ad8`) scans record `i`
while `record[i + 1].word0 - 1 < input` and returns the current record. The
conversion helper at `0x103c5a7a` then returns the input unchanged unless
`word1 == 1` and `word2 != 0`; an enabled record returns:

```text
word2 + (input - word0)
```

The relevant initialized records are:

| record | input base | enabled | output base | interval ending at next record |
|---:|---:|---:|---:|---:|
| 10 | `0x50000000` | `1` | `0x50200000` | `0x54900000` |
| 11 | `0x54900000` | `1` | `0x62700000` | `0xaf200000` |
| 12 | `0xaf200000` | `1` | `0xafa00000` | `0xb0000000` |
| 13 | `0xb0000000` | `1` | `0xb1000000` | `0xb1800000` |
| 14 | `0xb1800000` | `0` | `0xbaa00000` | `0xbb300000` |
| 15 | `0xbb300000` | `1` | `0xc3e00000` | `0xcc700000` |

The first row means that the logical class beginning at `0x50000000` is
converted to an output class beginning at `0x50200000`; it does not mean that
`0x50200000` is a display buffer or that QEMU should create a RAM region there.

The initial L1 page table copied from payload VA `0x10031400` contains identity
section descriptors covering `0x50000000..0x51ffffff`. That is guest MMU intent
for the logical range. It does not identify the physical owner of the converted
output range.

## QEMU ownership boundary

The current QEMU machine has no owner for `0x50000000..0x51ffffff` or
`0x50200000`. The bridge alias ends at `0x50000000`; MXIC-backed storage starts
at `0xb1800000`. The region table and the identity PTE therefore provide the
address calculation but not a sufficient owner contract.

Until a board or DDR provider is evidenced with its capacity, permissions, and
DMA visibility, the model must keep this range uncovered. Adding RAM, an alias,
prefilled bytes, or a DMA bypass would conflate a software translation with a
physical memory-owner proof.
