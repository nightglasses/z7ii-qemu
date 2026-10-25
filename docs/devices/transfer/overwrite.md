# Complete matrix overwrite before path latching

The late normal-GUI rectangle clear no longer rejects the cached matrix that
its producer immediately replaces. The finite model now applies this overwrite
rule to both rectangle records and initialized A8 images. All20 transfer native
subtests pass. Unchanged original execution completes a six-rectangle packet
and reaches a new840/1048-byte mixed-path state boundary, subsequently handled
by the [paint-state/context correction](paint.md). GUI first frame is
still unverified; see the [current frontier](../../validation/frontier.md).

## Source and command facts

Addresses below are unchanged C1.70 MAS VAs; buffers are board PAs. The preceding
mixed-path run captured a late856/1088-byte four-rectangle packet rejecting
`transfer-rectangle-program-not-supported`. With the same external inputs and
extra producer tracing, the new red run groups the late work into1280/1560
bytes: six53-word setup records and six59-word draw records, with the existing
initialization and padding. These are distinct original observations, not a
byte-identical replay claim.

Each setup record begins with two adjacent `0041f046` commands. Each writes
all nine32-bit values of the same setup-bank registers41..49. There is no
command between them. The second matrix is identity; the path latch `6=1`
appears at record words40/41, and the key85 and paired draw trigger1a6 follow.
The rejected first matrix is:

```
00000000 37800000 c69a97d3 38000000 c69588e7
b7800000 c69a97c3 b7800000 00000000
```

The producer explains this sequence:

1. The clear wrapper `1068bf1e` calls initialization `106454b2` at1068c000,
   then `106b581c(context,383,2)` at1068c00c to flush cached dirty state.
2. `106b55a2` tests dirty bit0 at private+136. It converts nine cached values
   from private+984 and emits `0041f046`, count9/stride4, at106b5622.
3. The wrapper calls rectangle producer `1068bd90` at1068c2e4. At1068bdba
   that producer calls `10645a66` with the same header/bank/count/stride,
   taking its payload from ROM1078926c. Reading those36 ROM bytes confirms
   float32 diagonal(1,1,1), all other entries0.
4. `10645a66` routes to `1064640e`, which appends the header and payload to
   the selected command buffer. Matrix writes do not themselves submit a path.
   The later inline-path and key/trigger calls are `10646324` and `10645c3c`.

The related fixed [Z7 software decoder](https://github.com/WonderLantern/z7-qemu/blob/97abf0b81cd3211f8374a38987a0beaf4549b90f/qemu/nikon-z7-gpu.h)
also stores masked register writes in sequence, latches setup paths on6=1,
and draws on1a6. It corroborates register overwrite semantics; it is not a
measurement of Z7II silicon. The precise earlier reason those cached values
were present does not affect this clear: all nine are replaced before use.

Initialized [A8 images](image.md) have the same pattern: the dirty-state matrix
in the initialization prefix is fully replaced by the image body's explicit
matrix before its path latch. The final image matrix need not be identity;
the existing finite affine validator remains authoritative.

## Finite contract and explicit assumption

Only the nine payload words of the first complete matrix write become
unconstrained operands. Both exact full-write headers, their position, the
second complete matrix, and the later latch/trigger order remain mandatory.
Rectangles still require the final identity matrix; initialized images retain
their existing final affine matrix validation. No general register interpreter
or acceptance of partially overwritten matrices is added. An intervening latch,
shortened first write, changed second header, malformed active matrix or changed
command order remains an error. Counts1..256, retained keys/context, immutable
captured command streams, target/alias checks, deferred execution and IRQ/reset
lifetimes retain their existing contracts.

The functional assumption is that unused register payloads do not cause an
eager numeric fault before they are overwritten. Thus even nonfinite, singular
or projective *first* payloads are accepted in these exact grammars. The model
does not numerically evaluate them. Source order proves replacement before
path use; whether physical hardware diagnoses an unused payload eagerly is
unmeasured. Active matrices keep all numeric checks. Pixel coverage/compositing
precision remains the existing functional convention.

## Initialized YUV clear and conversion families

The same finite overwrite rule also applies to the initialized224/1648-byte
YUV105/YUVA107 clears and224/1736-byte RGBA-to-YUV105/YUVA107 conversions.
This is a source-backed extension of existing grammars, not evidence that a
new original GUI submission has required every combination.

The saved C1.70 source projections show106454b2 marking dirty bits through
106b581c(context,65919,1), whose mode1 ORs the private+136 dirty word. A later
mode2 call routes to106b55a2; dirty bit0 reads nine cached fixed values through
private+984, converts them and emits the full setup-bank41..49 matrix. In
these initialized grammars that write has header0041f046 at setup word2 and
nine payload words3..11. The immediately following0041f046 at word12 writes
the active matrix at13..21. There is no intervening command. The path latch
6=1 is at42/43; the corresponding draw trigger occurs later in the draw
stream. These word positions and full replacement are command/source facts.

Only the unused words3..11 are unconstrained. Both matrix headers, the
complete active matrix, setup/draw lengths, geometry, modes, conversion
tables, targets, source/output ranges, overlap and backing-alias checks retain
their existing finite validation. Clear profiles require the active identity
matrix; initialized converters retain their existing YUV105 or scaled
YUVA107 matrix. This is not a general last-write rule for arbitrary packets.

The176/1552-byte retained SRC_OVER converter begins at logical setup word12:
its first matrix is already active. It has no cached matrix prefix, and none
of its nine active payload words become unconstrained. Its established
YUVA107 context requirement is unchanged. The functional assumption about
no eager numeric fault from a completely overwritten payload is the same as
above; physical hardware behavior for unused NaN/Inf values is unmeasured.

The `yuv-matrix-overwrite` native fixture defines232 variants. Its196 cases
across the four initialized profiles cover each cached slot with four patterns
(NaN, infinity, all bits set and a trigger-shaped word), zero cached matrices,
repeat, immutable command capture, both headers, active matrix errors, early
latch, truncated streams, mapping loss, reset and unrealize. It compares the
entire output allocation and guards against independent transparent-black
YUV/YUVA expectations. A further36 variants corrupt each active slot of the
retained converter after successful initialization and must reject it. These
are synthetic fixture requirements, separate from original GUI execution.

The parent-run first cached-matrix case is red against the preceding model.
With this extension and the independently validated clear-mode correction,
the complete transfer batch passes23 subtests in1.95s. Receipts are
`run clearmode-20261023/matrix-{red,green}.log`. This verifies the
preemptive model combinations, not a new original-firmware frontier.

## Native and original validation

The new matrix-overwrite fixture fails against the old model at its first
valid rectangle submission, returning MEMTX_ERROR. After the correction, the
focused case passes; the full applicable `nikon_transfer_unit` batch passes
all20 subtests in0.70s. Its113 variants cover both families: zero, rotated/
translated, projective and nonfinite overwritten values; each of nine slots
with four raw bit patterns; rectangle counts1/4/6/256; continuation/repeat;
immutable commands; whole allocation/guard comparisons; both headers, active
matrix, premature latch and truncated streams; mapping loss, reset and unrealize.
These are synthetic device receipts, separate from original startup.

The original green run keeps descriptor17 TEMP cross-model defaults,
descriptor18, resource12 empty input, RNG0, acceleration(0,0,1024), cold F4,
five-CPU MTTCG and a normally paced healthy9600 UART peer. No guest bytes, PC,
events, readiness or return values are changed. Its late packet is1280/1560,
with six transparent SRC clears into bec5f200:

| x | y | width | height | occurrences |
|---|---|---|---|---|
|136|6|112|40|1|
|594|6|100|38|1|
|90|402|36|36|4|

The traced1068bd90 calls agree with all six coordinate records. After that
START, original handler106ee3ca executes before the next submission. The final
physical target read has zero in every pixel of all six areas. This is output
evidence plus an original completion, not merely parser acceptance.

That run's next first rejection is CPU1 PC106ee3c6, WRITE32 PA201810e0=1,
`transfer-stream-truncated`, on840/1048-byte streams at bd0e0490/bd1605b0.
Setup contains three41+29-word fill/stroke pairs with keys10007..1000c.
Draw has an extra `01848004=0` before the first pair's repeated paint state;
the then-current fixed55/31-word recognizer does not admit that state variation,
and dispatch falls through to LUT length validation. This diagnostic is not
proof of missing guest bytes. The new state's producer and byte-mask
semantics are joined by the later [paint-state analysis](paint.md).
GUI gate/pending3/display-ready remain0, BatCk00010001, application03/02,
primary request11. Both processes remain alive until controlled cleanup;
the UART peer records no protocol rejection.

Receipts: `run matrixoverwrite-20261023/`, including red/green scripts,
argv, traces, read-only snapshots, raw streams/target, source projections,
native-red/focused/green logs and UART/QMP process receipts. Exact late856/1088
grammar is covered by the four-record native variant; the new original run's
late six-record boundary is reported independently.
