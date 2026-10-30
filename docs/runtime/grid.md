# Grid sample storage and producer boundary

The C1.70 MAS consumer at PC`1024ffda` reads a16-bit sample at VA/PA
`5d170000`. It belongs to allocation kind18's complete pool, not to a NOR
calibration payload. The board now supplies that pool's ordinary RAM. Initial
zero is an explicit functional assumption; the storage-only continuation below
does not establish statistics DMA. The later
[finite statistics profile](../devices/statistics/protocol.md) now fills this
grid from completed input under explicit numerical/input assumptions.
Addresses and offsets below are hexadecimal; dimensions/counts are decimal
unless marked otherwise.

## Source-backed ownership and range

Initializer `10306fb0` selects kind18 through `11982938[18]` at`11982980`.
Its row`119832f0` contains `{d98,6c000,12000,0,5d170000,700}`. All five mode
override tables have NULL entry18. `10298a7a -> 10297d0e -> 102e5cf8`
constructs six unit descriptors from the declared base/size/unit. It allocates
metadata separately and retains the20-byte pool parameters. The observed
pool at`b1da0c8c` agrees on PA base`5d170000`, total`6c000`, unit`12000` and
six descriptors. Thus the entire owned pool is PA`[5d170000,5d1dc000)`.
The following pool starts at the exact end and is not included in this model.

`1023b89a` calls `1030ad70` with a count selected from its context. At initial
registration the latter selects kind18 for counts up to six, or kind32 for
larger counts. The observed handle at`b26b4cb0` is`120000` (kind18).
Kind32 is outside this pool's contract. `102cd692` builds
software descriptors through `1030ae24 -> 10298f1a -> 10299176`.
The index helper selects `base + (index % count) * unit`, without writing
sample memory. The descriptor builder `1030ac6e` stores the selected pointer
at descriptor+2c and obtains the geometry from `102ce622`.

`1023bc02 -> 102ce3f0(context,b26b3f60)` acquires descriptor`b2c9e588` and
publishes it at root`b2c9da00+70`. The descriptor's +4 points to`b2c9f328`,
whose first two words are `{5d170000,12000}`. Both descriptor+c and+2c hold
`5d170000`. Its metadata describes30x20 samples with64-byte stride, hence a
`500`-byte padded prefix within the first`12000`-byte unit. The last sample
ends at offset`4fc`; each row's remaining four bytes are padding.

The consumer chain is `10210334 -> 10253c66 -> 1024feb6`. It follows
`consumer[b20f4094].word0 -> root+70 -> descriptor+2c`, loads each u16 sample,
expands the600 samples into32-bit software output words, and computes central
threshold counts and mean. This proves layout/usage, not the physical
statistics algorithm or exact sensor scaling.

## Address conversion

The range lookup table at`b2b4145c` has row14 `{54900000,0,0}`, followed by
PA class base`5f900000`. Type0 makes the conversion helpers retain the original
address. The descriptor path above also retains the selected PA directly.
The guest L1 descriptor at`b2b74000 + 4*5d1` is`5d114426`, independently
mapping VA`5d170000` to PA`5d170000`. The whole pool lies in this one section.
Neither that larger class nor the rest of the section is granted RAM here.

The machine uses existing `z7ii_ram` for the exact`6c000`-byte view, shared by
CPU and DMA. Firmware owns the allocator, descriptors and PTEs. Bytes persist
across reset, matching existing board RAM. No guest pointers, software return,
events or status are supplied, and no neighboring pool is implicitly mapped.

## Content source: facts and remaining assumptions

`102ce4f6` selects geometry from fixed firmware ROM table`119ba264`, using
the context selectors from `102cebc4` and `102ce450`. The cold-neutral module
input participates in context construction; it does not contain these samples.
The inspected kind3/subtype0 row is`119bcb90`; its30x20 dimensions agree with
the runtime descriptor. Geometry bytes and empty sample storage have distinct
producers.
`1030ac6e` and `102ce622` populate metadata, not the pointed-to sample array.
The scoped acquisition path has no NOR-to-grid copy or software sample fill.

The intended device path is visible in original configuration:
`10231c70` puts root+70 in pointer-array slot2+30, then
`1023abaa(flag8) -> 1023c998 -> 1023a6c6` selects endpoint1.
`1030adf0 -> 1023a63c` builds a six-unit output route with12000-hex stride.
`1023a5f6 -> 1020c8e2 -> 1020b1da` writes PA`5d170000` to both address sets
at`2006a550/560`; endpoint stride at`2006a580` is`12000`.
The shared commit mask is8 and the subsequent run unit is10hex/16decimal
through `1020c802 -> 1020ae3e -> 1020aafc`. Endpoint3c bit0 remains a parameter
bit of unresolved physical meaning, not an established DMA-enable flag.

These facts identify the output storage and its configured capture path.
They do not recover the sample-production arithmetic, frame association,
bank selection, DMA timing or completion. The current endpoint device only
retains configuration; this change does not add a statistics producer.
Consequently the first grid read receives the RAM model's assumed cold zero,
not recovered factory values or a qualified captured black frame. Post-submit
capture-group results remain errors under their separate invalidation rule.

## Validation

The new native fixture first fails on the old model's COPY destination with
status1 instead of8. After implementation all30 `nikon_memory` cases pass
in8.97s. CPU coverage includes first/interior/last words; the pool fixture
DMA-copies distinct nonzero patterns into all six complete units, compares
every byte before/after reset, and checks exact adjacent byte errors through
HMP's actual MemTxResult. This is storage validation, not statistics DMA.

Unchanged cold-neutral execution crosses the old read, and `1024feb6` returns
to`10253caa`. The zero samples yield mean0 and central low-threshold count140
through original software; no host statistics or results are injected.
The next first rejection is CPU0 PC`10208e14`, READ32 PA`2006bb70`, after
the earlier capture-group commit/run invalidated its cold-empty result packet.
The actual incremental caller is`1023aed8`, with LR`1023afb7` and force0
at `10239dc6`, unlike the earlier force1 initial snapshot.
That later result needs a producer; the model does not restore cold status
or copy a requested identity to satisfy the read. Normal GUI output and a
valid statistics stream remain unverified. All six original loader COPY
ranges still compare with the same cold-neutral file, guard`10305dac`
returns0 at`102d7254`, and property6433 is empty. QMP remains debug/stopped,
with neither display head bound to a console.

Raw source, ROM row bytes, descriptor/route/PTE snapshots, original traces and
native red/green receipts remain in ignored `run gridstorage/`.
