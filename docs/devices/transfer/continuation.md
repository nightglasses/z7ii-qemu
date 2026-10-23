# Inline rectangle context continuation

The normal GUI path's 2120/2368-byte pair contains ten more integer SRC
rectangles in the previously initialized 720×480 graphics context. The finite
model now retains that context and validates the next batch's key sequence.
Unchanged original execution completes the pair through original IRQ 129;
the next rejection is a 176/368-byte SRC_OVER program with partial alpha.
GUI first frame remains unverified.

## Producer and omitted initialization

Addresses here are unchanged C1.70 MAS VAs unless labeled board PA. The same
clear chain `10643a9a → 1064aace → 1068bf1e → 1068bd90` produces both
rectangle batches. `1068bf1e` calls `106454b2` at `1068c000` before packing
the clear color and emitting inline geometry through `1068bd90`.

`106454b2` tests the global word at `b1800e90` (`106454be`). When zero it
returns without emitting initialization. Otherwise it clears the flag,
resets the software key at `private+184`, and emits format, geometry, clip,
setup `691004=80` and the other global state. `10645c3c` increments the
per-context key at `private+184`, wraps `fffe → 1`, emits matching setup `85`
and draw `a6` keys, then triggers `1a6`. Submission through
`1068219e → 1068216c` does not reinitialize this state; `1068215c` advances
the two command-buffer cursor baselines.

A new unchanged-firmware red run observes the same context `b1800c1c` and
private object `bc01e4b0`. At the rejected ten-rectangle submission the global
flag is zero and `private+184` is `e`. The last ten traced `1068bd90` calls
match the submitted x/y/width operands below. Both complete command buffers
are captured at board PAs `bd0e02f0` and `bd1603d0`. The normal submitter
uses `1214=ffff`; transport setup is separate from command-stream initialization.

| Key | x | y | width | height |
|---|---:|---:|---:|---:|
| 10005 | 286 | 58 | 92 | 52 |
| 10006 | 284 | 56 | 104 | 56 |
| 10007 | 398 | 58 | 117 | 52 |
| 10008 | 396 | 56 | 129 | 56 |
| 10009 | 398 | 58 | 125 | 52 |
| 1000a | 396 | 56 | 129 | 56 |
| 1000b | 201 | 64 | 316 | 40 |
| 1000c | 201 | 64 | 316 | 40 |
| 1000d | 2 | 304 | 80 | 48 |
| 1000e | 660 | 304 | 58 | 48 |

Each record still explicitly specifies identity transforms, signed 32-bit
rectangle coordinates, packed paint zero, board PA target `bec5f200`, scratch
operands and matching keys. Scratch alternates `bcca7f50/bcec3f70` in this
capture. SRC replaces all four covered destination channels, including alpha.
The omitted state is the earlier global initialization, not rectangle data.

## Finite retained-state contract

The [initial family](rectangles.md) retains its existing exact grammar.
For a continuation of n rectangles, setup has n 53-word records and draw has
n 59-word transition/body groups followed by two ending words. Thus aligned
byte lengths are `4*round_up(53*n,2)` and `4*round_up(2+59*n,2)`. The common
record validator and deferred pixel executor handle both layouts; all
transitions, matrices, segments, fields, padding and endings remain checked.
Counts 1..256 use the existing functional work bound.

The device retains one expected key only after successful execution and output
unmapping. Nonzero key also marks the exact initialized 720×480 format/clip
profile. Continuation requires this profile and the expected first key, then
checks every paired key and its wrap. Paint and destination remain explicit
operands; a changed target must independently satisfy the same complete RAM,
alignment, 32-bit extent and command/backing-alias checks. Rectangles remain
positive, fully in bounds and applied in order after whole-batch preflight.

IRQ ACK retains the profile. A rejected paired program, execution failure,
reset, unrealize or successful different graphics program invalidates it.
Busy or pre-ACK metadata writes remain errors without cancelling an existing
job. Queued work publishes neither context nor completion before actual
execution. Immutable command capture and execution-time map rechecks are
unchanged. The old record7 RGBA720/YUVA107 flags remain separate.

The retained global-state interpretation is supported by the producer's cache
and cursor behavior. The exact key continuity rule is a conservative software
family contract, not a claim about a physical GPU's acceptance of arbitrary
keys. Integer half-open coverage, packed RGBA interpretation and opaque scratch
handling retain the functional conventions documented for the initial family.
No generic GPU VM, firmware callback, guest event or prefilled UI is added.

## Validation and next stop

The new native case fails against the old model at the first valid continuation
with MEMTX_ERROR. After the fix, the complete `nikon_transfer_unit` batch passes
all 15 cases. The new case has 31 variants covering the captured ten rectangles,
colors/alpha, overlapping order, counts 1/2/3/5/256 and rejected 257, cross-batch
wrap, chained odd batches, target changes, missing/reset/failed/foreign context,
wrong/replayed keys, padding, late malformed fields, command backing alias,
immutable capture, map loss, reset, unrealize and busy/pre-ACK guards. Entire
output allocations and guards are checked. Independent read-only review found
no remaining concrete defect.

The final unchanged-bundle MTTCG run retains the prior descriptor/TEMP, RNG 0,
acceleration, cold F4 and normally paced healthy UART inputs. Trace shows
856/1088 START → original IRQ 129 → 2120/2368 START → original IRQ 129.
The next CPU1 WRITE32 board PA `201810e0=1`, original PC `106ee3c6`, rejects
with `transfer-stream-program-not-supported`. Complete next buffers are
176 bytes at PA `bd0dff90` and 368 bytes at PA `bd15ff90`.

The next pair matches the existing single-path 720×480, record7 target
`c175d600`, SRC_OVER register profile except paint `cc000000` where the finite
model requires `ff000000`. Draw word25 is 8, an already accepted shadow bit3
variation. Indirect coordinate/segment contents have not yet been validated
for this new pair; neither pointer equality nor matching fixed fields proves
their contents. Partial-alpha compositing is the next contract to investigate.
The final read-only snapshot has BatCk `00010001` and GUI gate/pending3/
display-ready zero; the UART peer rejects no protocol request.

Ignored receipts are in `work/runs/rectanglecontinuation-20261023/`: original
red/green traces, source projections, complete buffers, snapshots, native
red/green logs, profile comparison and `verified-progress.json`. Native fixtures
and original continuation remain separate from physical-camera evidence or
complete GUI boot.
