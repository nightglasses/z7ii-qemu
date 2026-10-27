# Independent MAIN and auxiliary input

The readout pipeline retains separate completed MAIN and auxiliary acquisitions.
The capture-work RAW producer consumes only the full-MAIN snapshot. Auxiliary
DMA still completes through the original readout protocol, and statistics still
sees the latest successfully completed acquisition. Both paths use the same
CMOS provider, with one actual acquisition in flight and fresh automatic TG
admission. Their serialization and input association are explicit functional
choices for C1.70; no separate physical sensor port is claimed.

## Two owned domains

C1.70 independently initializes the capture MAIN path through MAS VA
`0x1023AAFE` and `0x1020C444`, and supplies unit2 geometry through VA
`0x1023A9D2` and its geometry writers. The auxiliary readout path owns a distinct
descriptor and requeues through its completion cause. Its width, height and
stride describe that transport, rather than the unit2 image dimensions.

The previous shared-latest-input choice allowed an auxiliary acquisition of
8320×34 samples to become a 1380×918 MAIN RAW image. Phase-preserving enlargement
repeated each input CFA tile for 27 output tiles vertically: `918 / 34 = 27`.
For a source bar beginning at image row 96 of 800, adjacent green phases could
enter the bar at different input rows, producing output half-row boundaries 54
and 81. This is a geometry error before matrices, demosaic or display conversion.
Changing white balance cannot restore the missing spatial samples.

The implemented contract retains two independent owned byte buffers:

- A MAIN snapshot is created only after the internal full-MAIN CMOS generation
  reaches DONE and its complete LE16 stream can be copied. It carries its own
  monotonic generation, dimensions, stride and mono/CFA encoding.
- An auxiliary snapshot is copied only after its actual destination DMA and
  owned readout completion succeed. Auxiliary completion and cancellation never
  free, invalidate or replace the dedicated MAIN snapshot.
- The existing latest-acquisition interface for statistics keeps its original
  globally monotonic generation. It points to the appropriate owned domain;
  invalidation never falls back to an older generation. The dedicated MAIN
  accessor has no RGB side channel, inferred pixels or separate image cache.

Capture-work copies the dedicated MAIN bytes into its immutable job. It requires
that the actual input cover the RAW descriptor dimensions; supported integer
reductions and CFA phase-preserving sampling remain unchanged. A consumed MAIN
generation cannot be replayed because an auxiliary frame arrived or was stopped.

## Fresh-edge arbitration and lifetime

Every successful request still uses the existing CMOS token, timed chunks,
actual DMA or complete internal stream, and DONE qualification. No clock edge,
commit, pending marker or elapsed time alone publishes a completed image.

On a fresh automatic TG edge, an idle provider initially prefers an owned
auxiliary request. After admitting auxiliary work, the next eligible edge
prefers MAIN if its committed geometry, run gates and existing data backpressure
permit it. After admitting MAIN, auxiliary is preferred again. With only one
eligible domain, that domain proceeds normally. This bounded one-to-one fairness
rule is a virtual arbitration hypothesis, rather than a recovered hardware rate.
Busy edges are discarded; the model does not queue edge credits or add another
timer. A queued auxiliary descriptor remains owned while MAIN is in flight.

MAIN stop or changed committed geometry cancels only an owned MAIN acquisition
and invalidates its completed snapshot. If the latest-acquisition interface
currently points to MAIN, that reference is cleared before its bytes are freed.
Auxiliary stop retains the independently delivered MAIN image. System reset
clears both snapshots and generation namespaces, cancels actual work and leaves
the existing external TG level untouched; a new low-to-high edge is required.
The original mask/GIC/W1C routing and frontend/statistics completion contracts
are unchanged. No new result, IRQ, consumer callback or GUI state is injected.

Read-only `main-input-generation`, `main-input-width`, `main-input-height` and
`main-input-stride` properties describe the currently valid MAIN snapshot.
Generation zero denotes no valid snapshot; these fields do not create readiness.

## Validation boundary

The native MAIN-domain fixture keeps repeated real 8320×34 auxiliary requests
pending while it admits an actual 8×8 P6 MAIN acquisition. It checks auxiliary
DMA bytes, fair provider ownership, retained MAIN metadata after another auxiliary
completion, and actual phase-preserving 4×4 RAW12 output against fixed byte
vectors. Both tile coordinates and CFA phases vary in the source, exposing the
spatial error hidden by constant or broad-quadrant images. Complete row payloads,
padding, no replay, cancellation and reset are checked independently of display.
Existing paired fixtures now obtain a real full-MAIN acquisition before their
capture edge instead of borrowing an auxiliary thumbnail. These are synthetic
protocol and numerical checks; they do not prove physical sensor fidelity or
original-firmware startup progress.
