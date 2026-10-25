# Resource11/12 row storage and predictive memory audit

The normal GUI initializer's READ32 at MAS Thumb PC`1023c3fe`, PA`50560000`
is a resource12 input read, not another broken VA conversion. The unchanged
entry to `10238314` receives `r0=50560000` from `1023c452`. The original L1
descriptor is `50514422`, an identity section. Its optional logical view at
VA`30260000` has descriptor`50514426`; there is no physical bus mirror there.

## Producer and ownership

`10248468` installs resource11 pointer`50300000` and resource12 pointer
`50300000+260000=50560000` into the original28-byte resource directory at
`b185eb70`. Getter `103a47a2(12)` → `103bfaa2` → `102486f6` returns the latter.
`102353da` passes it to `1023c37c`, which retains it at `b188817c`; later
`1023c452` passes that cell to the row initializer. These are published source
storage pointers, distinct from the kind40 destination allocation.

The44-byte descriptor table at `b185e5c4` records the original NOR providers:

| Resource | Primary NOR | Alternative NOR | Slot bytes | Directory payload bytes |
| --- | --- | --- | --- | --- |
| 11 | `12a80000` | `12e60000` | `260000` | `24fff8` |
| 12 | `12ce0000` | `130c0000` | `0e0000` | `0dfff8` |

Directory word+16=`00000200` consists of marker/counter selectors0/2; it is
**not** a loaded/ready bit. Validator `102a80f2` checks both banks through
`102a80a8`: marker validation `102a874e` requires `ENDM` at slot end-4;
counter validation `102a807e`/`102a7e30` reads end-8 and rejects values greater
than `fffffff0`. Erased-FF banks fail validation. They do not become a valid
empty row list merely because destination RAM is cold-zero.

The actual producers are the original phased calls to `103c048a`.
Resource11 uses ten16-byte loader records at ROM VA`118d0930`; resource12
uses five at`118d09d0`. Each record carries offset, length, fill mode, phase
and copy mode. The unchanged trace observes both resources in phases3/8/4/5/6.
The phase9 dispatcher is separately visible in `103a4b90`. In particular,
resource12 phase3 copies `[0,b3f94)` and word`d767c`, clears
`[b3f94,d767c)`, fills `[d7680,dfff8)` withFF, and copies word`dfff8`.
The guest performs all copies/fills and callback publication.

The finite board now backs resource11/12's complete contiguous destination
capacities `[50300000,50640000)` with ordinary cold-zero RAM. The exact bounds
come from the two installed bases, NOR slot capacities and complete loader
records, including resource11's later `250000..25fff8` fill. This does not
back the enclosing `[50300000,50700000)` class or create VA bus mirrors.
RAM contents persist across machine reset.

The new `descriptor11-input` and `descriptor12-input` properties install exact
primary NOR files only. They use the existing NOR backing and retain normal
program/erase/reset semantics. Neither property writes destination RAM,
changes a registry cell, selects a bank, validates a footer or supplies an
event. Alternative banks remain independently erased unless guest programming
changes them. See [the external-input contract](../inputs/nor/descriptor.md).

## Three streams and all eight layouts

The original layout table at RAM`b18881a8` has eight12-byte records
`{u8 kind,u8 count,u16 reserved,u32 span_words,u32 offset_words}`.
Each source starts at `50560000 + 4*(offset_words + channel*span_words)`.
Each destination occupies a fixed`39000`-byte slot of kind40.

| Index | Kind | Count | Span words | Offset words | First of three source starts |
| ---: | ---: | ---: | ---: | ---: | --- |
| 0 | 0 | 19 | 9672 | 0 | `50560000` |
| 1 | 0 | 15 | 8520 | 29016 | `5057c560` |
| 2 | 0 | 9 | 5144 | 80136 | `505ae420` |
| 3 | 1 | 15 | 8520 | 95568 | `505bd540` |
| 4 | 1 | 9 | 5144 | 121128 | `505d64a0` |
| 5 | 1 | 17 | 9656 | 136560 | `505e55c0` |
| 6 | 1 | 11 | 6248 | 165528 | `50601a60` |
| 7 | 1 | 15 | 8520 | 29016 | `5057c560` |

Kind0 uses `1023c3d2`: each stream supplies8 rows, then selection rotates,
for `24*count` output rows. Kind1 calls `102382e0` three times, producing
`8*count` rows in each of three planes at destination offsets0/`13000`/`26000`.
Rows contain at most128 words, terminate at`ffffffff` and occupy512 destination
bytes. A row terminator is copied; untouched row tails retain the original
initializer's zero clear. These are packed row words; physical defect-coordinate
semantics and factory calibration are not established by this bounded analysis.

## Declared input and original continuation

The resource12 functional experiment supplies a primary slot filled withFF,
a counter0 at end-8 and `ENDM` at end-4. FF words express empty input rows under
the original parser. This is a virtual empty-list hypothesis, not recovered
factory data or proof that a physical camera has no defective pixels. Generate
the file outside tracked source, then supply it alongside the established
TEMP/acceleration descriptors, raw axes, reproducible RNG and UART peer:

```python
from pathlib import Path
import struct
size = 0xe0000
Path("descriptor12-empty.bin").write_bytes(
    b"\xff" * (size - 8) + struct.pack("<I", 0) + b"ENDM")
```

The original phase3 producer fills resource12's target exactly as the five
loader records prescribe. Read-only snapshots verify its copiedFF prefix,
zero middle, FF tail and copied counter. The complete `1c8000`-byte kind40
output then matches all eight layouts byte-for-byte:2640 row terminators,
zero row tails and the expected padding. Execution reaches `10238408` with
result1 and proceeds beyond this initializer. The next first rejection is
CPU0 PC`103c5d6e`, WRITE32 PA`04280064`=`10`, reason
`sysconfig-access-not-supported`. That is a separate system-configuration
register contract; no GUI first frame is established.

A separate structural-input run supplies the same FF/counter/marker shape to
resource11 too. Its broader payload semantics are unqualified; it is not
claimed to be a valid complete calibration record. Resource11 input is not
required for the measured resource12 row output. Both file properties are
tested as opaque inputs, without firmware-format validation in the model.

A RAM-only control with erased NOR also reaches that register rejection, but
both resource storage and kind40 output remain all-zero. Thus advancing past
the address fault alone does not establish valid row data. The explicit input
run is distinguished by original bank validation, phased publication and the
correct terminators across the complete output; input is not claimed to be
mandatory merely to reach this next fault.

Native old-model row-storage readback fails. The corrected `nikon_memory`
batch passes24 cases: exact RAM endpoints, independent resource cells, outer
holes, no physical logical-view alias, reset retention, both NOR inputs,
size/missing-file errors, unchanged destination RAM and NOR mutation persistence.
Receipts are `run streamstorage-20261022/` (`native-red.log`,
`native-green.log`, `source.json`, `execution.json`, `twelve-execution.json`,
and original red/green/final/twelve
TCG, UART and read-only snapshots). These are separate from native fixture success.

## Predicting the next storage gaps

Audit the complete current producer/consumer cohort before a model change:
read the full phased loader table, all layout variants, allocation sizes,
selected pointers and PTEs, then compare the resulting extents with existing
MemoryRegions. Implement the whole confirmed cohort and test its outer bounds
together. Address conversion records alone do not authorize memory ownership.

| Current cohort | Coverage / remaining condition |
| --- | --- |
| Resource11/12, both source banks, all loader phases | NOR slots already modeled; explicit primary inputs and full destination capacities now supported together |
| Resource12, three streams, eight row layouts | All checked together; no per-layout model recognizers are needed |
| Kind40, `1c8000` bytes | Exact destination already covered; guest MMU owns its VA view |
| Kind38/39, each `201000` bytes in `1023cb62` | Both holders remain0 in the current snapshot; no selected PA or exercised ownership yet, so no guessed RAM range is installed |
| Resource13/14/16 | Directory pointers remain0 in this run; their metadata is not evidence of an allocated destination |
| System configuration `04280064` | Actual next rejected transaction; requires its own source-backed register-family audit |

This predicts fixed destinations, table strides, variants and bounded transfer
extents ahead of execution. Dynamic allocations, bank selection, mode branches
and later device side effects still require execution evidence. Batch those
with a known contract; keep unknowns explicit rather than turning the remaining
address classes into read-zero storage.
