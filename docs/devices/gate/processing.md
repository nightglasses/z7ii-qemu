# Continuous image processing

`gatework.c` implements the finite context9 MAIN continuous graph. It receives an immutable
configuration, committed parameter slots, imported commands, one owned tight
NV12 image and a phase in `[0,4)`. It owns neither scheduling nor the input
queue, phase advancement, reset, cancellation or IRQ publication. Its prepare,
compute and publish boundaries separate main-thread address-space access from
pure private numerical work. The synchronous composition remains available to
native arithmetic fixtures.
The separate [context8 grid graph](grid.md) executes through `gategrid.c`,
`gateplane.c` and the gate runtime. It consumes statistics-produced RGB planes,
writes a packed12 confidence map and publishes its own completion. Paired
processing reads that completed map when its grid dependency is enabled.

## Admission and source facts

The admitted path is type0 slot8 → type5 resource35 → type9 resource45 →
type11 resource62 → type1 resource21, branching through type8 resources41–43
and type10 resources54/49/55 to three committed type0 destinations. Types2/3
are connecting edges. The current type18 resource34 parameter tuple is
qualified, with no numerical effect assigned to it. Its metadata-only role is
a finite interpretation, not a recovered hardware specification.

Every imported register's presence and value is checked. The type5 80-word
table must be zero; its carrier and tail controls remain exact. Type9's
non-numerical controls, the neutral type11 window and all three branch control
tuples remain exact. Unsupported correction tables, cross terms, formats and
control combinations fail. Dimensions, pitches, DMA addresses, grid records,
positive axis scales and signed offsets are variable numerical inputs within
the bounded profile; they are not address or image-size allowlists.

The continuous selection words at PA `20102110/14` must be `200087e0/88000`;
each terminal's dependency words must agree. These source resource-selection
bitmaps close the admitted processing graph. Other imported ports can belong
to another worker and do not become selected merely by being loaded. All four
phase-map entries for input slot8 must bind its kind43 port at PA `40000000`.
The special port is never expanded into frame RAM or replaced by an unrelated
input address.

The source-backed destination is the type10 route's low four bits. For a
caller-selected phase, its base is configuration offset
`0x2200 + phase * 0x40 + slot * 4`. The committed slot supplies active width and
height, independent Y and UV pitches, and the relative UV offset. The type10
window may have additional bottom rows; those do not enlarge the active DMA.
The type1 height is a nonzero band extent, not the full image height.

The original affine constructor `0x102B75A8` and consumer `0x102B8698` establish
signed 31-bit Q16 offsets, independent bit31 flags, signed 24-bit Q16 scales
and dimensions. The admitted diagonal profile requires zero cross terms,
positive scales and the branch's known flag combination. Sampling uses the
active destination's top-left domain. The larger window is retained for
qualification rather than silently cropping or centering the active image.

All three source views use format selector2. The independent storage consumers
at `0x10268EBE..0x10268EE8` and `0x10268F36..0x10268F5E` compute the required end
as `UV - Y + pitchY * storageHeight / 2` for that selector. This supports a
half-height chroma plane for all three branches; it does not establish the
physical reconstruction filter. The original CPU producer `0x102B3B82` writes
interleaved U then V for the related selector1 view. Reusing that ordering for
selector2 is a finite family inference, consistently with the typed NV12 input;
the actual display sampler's selector2 ordering has not been independently
recovered.

The affine constructor's integer division reproduces all six scale and offset
values in the original photo graph from the effective 1374×914 source extent.
That extent differs from the owned 1376×914 frame. The wrapper supplies zero
origin; the descriptor's separate one-pixel ROI origin has not been joined to
its eventual consumer. The model adds no unproved extra crop.

## Numerical and completion hypotheses

The zero type5 table is treated as identity. The type9 operation uses
`gatepixels.c`: signed23 Q8 four-corner records, validated grid tags and
bilinear interpolation, followed by edge replication. The three type8 branches
perform diagonal affine sampling using nearest rounding with ties upward and
edge replication. Chroma is co-sited with even destination luma coordinates.
These filters, phase association and chroma ordering are explicit functional
choices. They do not claim physical DSP or Bayer fidelity.

The warp evaluates the same four-corner integer numerator by tile and row.
It decodes each40-byte record once, advances the unrounded numerator with an
exact integer recurrence, and applies the existing signed half-away rounding
at each sample. The validated width is a power of two, so an unsigned-magnitude
shift replaces division without changing negative half ties. All record and
alias checks still finish before any output write. The affine branches reuse
their separable X indices, calculate Y once per row, and share the actual U/V
address; every output sample remains present. Source and output dimensions are
bounded by4096, so the temporary uint16 X map uses at most8KiB on a host worker
stack. No guest stack or persistent cache is added.

Only a complete successful graph is eligible for the caller's existing three
terminal causes and separate whole-frame cause. The source maps resources49,
54 and55 to raw1 bits0,5 and6, respectively. The selected whole-frame raw2 bit3
association is a finite completion hypothesis. `gatework.c` itself emits none
of these causes. It also never reads guest workers, modifies software events,
releases parameter slots or treats a configuration write as pixel work.

## Device ownership and scheduling

`gate.c` schedules a one-millisecond virtual work boundary only with an owned
input and the enabled continuous input/type1 controls. At that boundary it
validates the whole current configuration and snapshots the numerical records.
The latency is a functional timing choice. Time alone cannot establish success.
The main thread validates RAM/aliases and snapshots all ten selected packets,
committed slots, configuration, tight NV12 input, warp records and three affine
records. QEMU's existing worker pool computes only those private images; workers
never read guest memory, touch device state or publish DMA/IRQ. Its Aio callback
marks the result ready and schedules the existing virtual publication timer.
Pausing the virtual clock cannot publish a ready host result.

Layout, binding and committed route changes cancel the owned attempt. The
normal source upload at MAS VA `103bded0` instead uses local `14=1`, `14=0`,
`10=pointer` without graph stop/drain. That upload retains the old committed
packet and owned numerical snapshot. `14=0` only arms the import; successful
decode at `10` closes its atomic publication. Future admissions wait for every
selected open upload. Same or unrelated setup, auxiliary staging and future
numerical records preserve existing work; unknown controls still reject.
The committed numerical pointer at logical `54` and valid-row high half at
`4054` can change for future work. Current source spans remain part of live
RAM/alias preflight, so a future coefficient address cannot be overwritten by
an old result. Shape, stride, route, mode and packet-presence changes remain
strict. A node generation prevents old completion from clearing a newer start.
This immutable numerical latch is a finite ownership hypothesis; the source
establishes upload order, not physical hardware latch timing.

Only actual successful three-output DMA retires the input, clears ordinary
start flags of the ten owned nodes, advances the phase and latches the terminal
and whole-frame causes. Unbound nodes retain their request flags. The initial
phase comes from input control bits4..5; a changed explicit phase selection
reselects it, and successful work advances modulo four. This automatic phase
association is a bounded model assumption. Output-map bank0 is part of this
qualified profile, separately from the continuous input selector.

The next graph waits while either prior terminal or whole-frame cause remains
latched. A result already computed when either cause becomes pending remains
owned and waits for both actual W1C acknowledgements. It is not recomputed.
W1C acknowledges results and can admit a previously queued real input;
it does not manufacture a frame. This one-result backpressure is a functional
storage law. Stop, input reconfiguration, reset and unrealize cancel scheduled
work without delayed output or completion. Continuous disable preserves queued
input; a gate stop explicitly cancels it. Status3 represents a scheduled owned
job and0 rest; the physical status-bit encoding remains unknown.

Read-only QOM `processed`, `processing-errors`, `processing-cancelled`,
`processing-result` and `processing-phase` expose the work boundary.
`processing-compute` is zero without numerical work, one while queued/running,
and two with a private ready result; `processing-inflight` counts the single
aggregate, including cancelled work until its callback returns. `input-epoch`
distinguishes normal input consumption from revoked bindings. A failure
reason is logged when it changes, without repeating a per-frame diagnostic.

## Complete output storage

The original kind27/28 override tables at MAS VA `11982ab4/11982c30` agree on
their output pool rows; kind29 uses its default row. The machine backs these
complete source extents with ordinary shared QEMU RAM. Addresses below are PA;
sizes and units are hexadecimal, counts decimal. Initial zero and reset
retention are functional board assumptions.

| Kind | Source row (MAS VA) | Base | Size | Unit | Count |
| --- | --- | --- | --- | --- | --- |
|27|`11983620` / `119836c8`|`dcb3c800`|`710000`|`1c4000`|4|
|28|`11982a9c` / `11982c18`|`dbb1d000`|`101f800`|`339800`|5|
|29|`119833b0`|`be5d2200`|`486600`|`182200`|3|

These declarations provide storage, not pixels or ready state. Each output
uses its committed independent plane offsets and only its active rows. Gaps,
unit tails and adjacent unallocated memory remain untouched. Phase maps can
reuse a unit in another epoch; that is distinct from overlapping outputs in
one transaction.

## DMA and failure boundary

Input dimensions and all destination dimensions are even and bounded by 4096.
The source slot8 must match the owned frame. Grid and affine records must be
readable direct RAM. Every actual output row must be writable direct RAM;
unknown, MMIO and read-only destinations fail. All three outputs are checked
before computation or publication. Translated RAM backing intervals detect
aliases across bus addresses, output-to-output overlap and output overlap with
the input, grid or affine records. Unwritten row padding is excluded from
the overlap calculation.

The operation snapshots the numerical records, computes host-owned images,
then rechecks the token, input epoch/generation, phase, current output layout
and all RAM/alias constraints before actual DMA. A rejected preflight leaves
RAM unchanged and reports configuration, topology, format, grid, affine,
memory, alias or pixel validation failure. Stop/reset/unrealize invalidate the
owner before later callbacks; unrealize drains every callback before resources
are freed, and a later realize explicitly reopens admission. There is no
fallback output or partial-graph success.

## Native fixture scope

The fixture constructs synthetic RAM, packets, a generated identity grid and
three affine branches. Its independent integer oracle checks every active Y
and UV byte, row padding, all four phase maps and a translated grid. Negative
variants include a late malformed record, wrong controls, a missing terminal,
read-only or unmapped RAM, address overflow and backing aliases. They require
the whole RAM image to remain unchanged on failure. These are designed native
checks; their execution receipt is maintained by the integrating parent.
