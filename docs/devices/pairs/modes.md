# Paired capture scheduling and output ownership

C1.70 MAS produces different paired configurations for photo and movie live
view. The finite model decodes their coupled scheduling fields and independent
WORK enable, while requiring real RAW input and a supported MAIN destination.
This closes the chunked RAM-output contract; it does not turn the special
shared MAIN port into RAM or establish a displayed photo scene.

Code addresses here are MAS CPU virtual addresses (VA). Register offsets and
values are hexadecimal, relative to board PA `20380000` or `203c0000`.
Structure offsets, lengths and dimensions are decimal. Registers are aligned
little-endian 32-bit words.

## Core packet production

The original photo chain is
`10264972 → 10263f98 → 102c0430 → 102c0ae2 → 102c0304 → 1036a2ac`.
The core block is 68 bytes. Context byte872 selects its first byte: zero
produces `88=2` and `10c=1`; nonzero produces `88=0` and `10c=0`.
These facts establish two software scheduling shapes, not a universal
photo/movie register enum.

For the inspected chunked shape, `a8=2`, `138=1`, and `102c0966` selects a
ten-byte ROM record through VA `118e52f4`. At index5 its halfword at +2 is
11915 and byte at +5 is39. `102c0304` derives 22 lines per chunk and 40 chunks,
publishing `13c=00160000`, `140=00280000`, `94=5c` and `9c=36f`.
Thus `9c=879` is a scheduling product minus one, while the actual completed
RAW ROI is 1380×918. Truncating RAW to 880 rows would discard real input.

The implemented continuous shape requires `88=0`, `8c=3`, `a8=4`,
`10c=138=13c=140=94=0`, and `9c=RAW_height−1`. The chunked shape requires
`88=2`, `8c=0`, `a8=2`, `10c=138=1`, positive high-half line/count values,
zero low halves, `9c=lines*count−1`, and `94=4*lines` or `4*(lines+1)`.
The latter alternatives cover the original floor/ceiling scheduling rounding.
The finite model processes the complete admitted RAW ROI once; it does not
claim cycle-accurate chunk overlap or generate per-chunk events.

Core field `e0` is produced from context halfword466 plus340: the inspected
chunked/continuous configurations use `156/158`. Other core modes, uncoupled
fields and opaque tuples remain unsupported. Input bit depth, crop bounds,
stride and backing extent still come from the actual completed RAW descriptor.

The original photo and movie observations use separately selected complete
opaque tuples. Supporting these live scheduling fields does not make all other
mode-dependent calibration words interchangeable; a runtime selector transition
can still encounter an unmodeled opaque calculation. No fallback tuple or
factory configuration is injected into guest registers.

## Independent WORK and MAIN outputs

`102be8a0` constructs the output packet for `1036aac2` and related setters.
Context byte880 independently drives both WORK enable words `408/410` and
core `124`. MAIN enable words `420/428` remain one in the inspected photo
configuration. The model requires both MAIN enables and decodes WORK as a
matched boolean pair. Optional auxiliary input format `3d8` is2 with WORK
enabled and cold zero without it; it does not select MAIN pixel format.

With WORK enabled, its two published rings must mirror one another, both
half-resolution planes must have valid positive strides and complete RAM
backing, and only successfully published same-epoch outputs qualify as history.
With WORK disabled, there is no WORK/history DMA, alias check or byte count.
Unused WORK pointer/stride changes do not cancel a MAIN transaction; changing
the WORK enable itself does. MAIN and grid DMA must still complete before
result increment and the original paired notification. Reset and changes to
consumed configuration cancel the outstanding numerical job.

Output selector `424` comes from context bytes876/888: disabled selection
produces0; enabled ordinary output produces2; enabled shared output produces3.
Its physical pixel-format meaning is not established. The supported ordinary
RAM shapes use0 for continuous and2 for chunked scheduling. Type2 descriptor
packing yields `404=1` and `42c=3` in both inspected paths. Y/UV are a bounded
NV12 functional encoding, with contiguous row-pitched RAM planes; enabled
planes with zero stride reject rather than being mistaken for disabled output.

## Shared MAIN endpoint

The observed photo output instead has `424=3`, WORK disabled, MAIN enabled,
1376×914 cropped output, pitch 1408, and all four Y/UV slots repeating
`40000000/40008000`. Source VA `1026563e` calls `1030d020`, which allocates
kind43 through `10298bea → 1030c214`; global VA `b18648b4` owns that descriptor.
ROM descriptor VA `119834b8` gives a 64 KiB special region at `40000000`.
Its constructor explicitly places the second port at base+`8000`, rather than
at pitch*height. These are two 32 KiB port partitions, not full image surfaces.

The shared descriptor is copied to four phase records by `1026405a` when
needed; identical addresses do not establish four independent frame buffers.
`102654c6` clears processing queues, releases kind43 through `1030d05c`, and
clears its global pointer. A later register read may therefore describe an old
configuration after software has already released its owner.

CPU bridge mapping alone does not establish the pixel consumer. A read-only
snapshot of the current graph retains ownership bits8/9. Worker9 begins at
`type 0/resource 8 → type 5/resource 35 → type 9/resource45`; its slot 8 geometry,
pitch and relative UV offset match MAIN. The imported type 5 packet explicitly
selects upstream slot 8. The [typed input contract](../gate/input.md) therefore
connects the special MAIN view to that hardware-qualified local input, storing
actual computed Y/UV bytes rather than writing a peer address space.

Separately, route `2690=141` selects type 10/resource 53 in worker 8's different
graph, which ends at slot2. `102e3560` adds `1<<(resource−49)` when the
160-byte software packet's trailer at +156 is nonzero. This is not the MAIN
receiver qualification. Runtime devices use only the committed slot and
imported packet; they do not inspect guest worker structures.

For shared MAIN, admission and deferred completion both require the matched
input shape and the same gate configuration generation. The four selected
phase records must each use the special Y/UV endpoints and a valid 64-aligned
pitch. Only MAIN is excluded from RAM-address/alias checking; the real RAW
input and enabled RAM outputs retain full backing and overlap checks. A
shared job also excludes simultaneous shared work by the other paired bank.
Successful publication supplies the actual tight NV12 result to the gate,
then records paired bytes/results and produces the existing notification.
An unavailable or changed input never creates completion. The downstream
input lifecycle and its remaining processing boundary are defined in the
separate contract.

## Validation

The native photo fixture uses a real 36-row RAW producer with chunk metadata
for 880 scheduled lines. It checks the complete MAIN/grid DMA and result flags,
untouched WORK RAM, invalid retained WORK pointers before and during a job,
and unchanged synthetic pixel values. Negative fixtures cover treating `9c`
as RAW height, malformed chunk metadata and a shared destination without a qualified receiver.
Separate fixtures reject zero MAIN, WORK and grid strides while satisfying the
preceding address-layout checks. These are synthetic RAM-output fixtures;
unchanged-firmware continuation and the shared-stream boundary are recorded in
the [startup frontier](../../validation/frontier.md).

The stream fixtures additionally compare every queued NV12 byte, preserve
payload across diagnostic reads and repeated parameter commits, test producer
backpressure, cross-bank exclusion, in-flight generation invalidation, stop,
reload, staging and whole reset. No test substitutes a gate completion for
unimplemented processing. Native verdicts and the distinct original photo and
movie observations are recorded in the [capture receipt](/evidence/paired/capture.json).
