# Descriptor17 publication and the panel-table boundary

Status, 2026-10-21: the current multi-thread baseline rejects the erased
descriptor17 footer before copying or publishing its records. This explains
the failed resource wait upstream of the [worker exit](handoff.md). A separate
run with the existing, explicitly supplied Z7 cross-model seed publishes the
display resources and installs the rear-panel table, then encounters the
already-known invalid ID34 input boundary. It does not complete startup.

All addresses below are C1.70 **MAS domain VA**, including external NOR views.
The firmware bundle and device implementation are unchanged. The seed run is
an external-input experiment, not recovered Z7II factory data.

## The producer and its order

The original loader wrapper `103a4ac0(phase)` obtains the 25-row manifest at
`118d0a20` through `102a8834/102a8838`, then calls
`103c05f4(ID=17, manifest, count=25, phase)`. A single-breakpoint probe
observes phases **1, 2 and 3** on the erased-input baseline. Thus the loading
tasks do run; this is not an unstarted phase-2 worker.

Original call sites are `10200644` for phase 1, the thunk `10200c86` for
phase 2, and `10200ae2` in task body `10200ac2` for phase 3.

The manifest contains these relevant rows:

| Resource | Descriptor offset | Copy length | Phase | Published event |
|---|---:|---:|---:|---|
| 24, `HAIMENMONI` | `aa00` | `400` | 2 | `04400044` |
| 25, `HDMI` | `af00` | `100` | 2 | `04400044` |
| 27, `EVF` | `b200` | `100` | 2 | `04400044` |
| 34 | `d700` | `c00` | 3 | `04400045` |

In `103c05f4`, successful descriptor validation precedes the row loop:

```text
103c05f4
  -> descriptor registry and backing lookup
  -> 102a80a8 -> 102a874e: footer validation
  -> if validation fails: return before the row loop
  -> select rows whose byte +9 equals the requested phase
  -> copy/translate the record
  -> 103bf374(phase): phase 2 becomes 04400044
  -> 103bf32c(event): phase-2 ready mask is 2
  -> store the resource's ready mask and call registered callbacks
```

The resource ready mask **2** above is a different object from the display
initializer's event bits **1/2/4**. `1020526c` registers `102051b4` for IDs
24, 25 and 27 through `103c039c`. Registration also replays the completion
immediately if the requested phase's ready mask is already set. Consequently
the loader can finish before this callback is registered without losing the
publication. The cross-model run observes precisely this replay route.

`102051b4(04400044, ID)` posts bit **1** for ID24, bit **2** for ID25 and
bit **4** for ID27 through `103610cc(b1875420, bit)`. The rear/EVF initializer
then waits for mask `5`; a successful wait permits ID24/27 lookup and
`104d7376 -> 10448c3a -> 1048178c` table installation.

## What fails on the current baseline

A sequential probe uses one hardware breakpoint at a time and observes:

```text
103e0ac2 comparator entry:
  r0=b185e59c, r1=132dfffc, r2=4, lr=102a8773
  expected bytes 45 4e 44 4d (ENDM)
  actual bytes   ff ff ff ff
102a8772: comparator result ffffffff (-1)
102a80c4: footer validator 102a874e returned 0
10205226: display resource-wait wrapper returned 0
```

The original Thumb path at `102a8772..102a8776` changes the validator's
result from 1 to 0 on a nonzero comparison. `102a80a8` combines that result
with its other checks; `103c05f4` exits at the failed-validation branch
before a record copy, ready-mask store or resource callback. The callback
rows for IDs 24 and 27 contain `102051b5`, count 1, but ready mask 0 in the
final baseline snapshot. Their registration is present; publication is absent.

A separate focused probe records the wait arguments at `10361192`:

- object `b1875420`, handle `010000f2`;
- mask `5`, timeout argument `0x1388` (5000);
- the raw RTOS return at `103611a8` is **`ffce0000`**;
- the wrapper converts any nonzero raw result to Boolean **0**.

The saved result buffer is not interpreted as successful returned event flags.
The numeric RTOS error is recorded without assigning an unverified symbolic
name. No ready bit, callback, IRQ or table was supplied to bypass this path.

## Controlled external-input experiment

The existing file
`descriptor17-z7cross.bin` is 128 KiB,
contains the Z7-derived display records and `ENDM`, and leaves ID34 erased.
It was supplied only through the documented `descriptor17-input` machine
property. Its provenance and limitations remain those in
[descriptor.md](../inputs/nor/descriptor.md).

With the same current bundle/build and multi-thread TCG, the probe observes:

```text
102051b4(04400044, 24)
102051b4(04400044, 25)
102051b4(04400044, 27)
10205226: resource-wait result 1
10448c3a: install pointer b7f54a00, marker HAIMENMONI
final [b1808508] = b7f54a00
```

This demonstrates the normal publication and table-installation path under
an explicitly qualified external input. It does not demonstrate worker 0's
later exit: this run ends earlier in startup, with manager state 0/current 5/
target 5, after the partial seed exposes another invalid record.

The follow-up seed probe observes `10217942 -> 1021796a`, the ID34 translator.
Its inner loop at `10217b1c` executes 160,531 traced blocks. ID34 source
`b7f57700` corresponds to descriptor offset `d700`; its `+24/+28` dimension
words are `ffffffff`, whereas the original fixed-stride destination supports
at most 8 by 8. The loop's original store at `10217b2a` therefore runs beyond
its intended array. The first observed data abort in this experiment is at
the ARM exception-context save (`lr_abt=1010368c`, `sp_abt=4f`,
`DFAR_S=4f`, `DFSR_S=801`) while the interrupted register set still identifies
the ID34 translator. The exact path from the overrun to that corrupted context
has not been separately traced; the invalid dimensions and out-of-bounds loop
are established, not a repaired interrupt contract.

The required external input must satisfy its original consumers, not just the
footer. The subsequent explicit [functional profile](../inputs/nor/functional.md)
constructs a nonempty ID34 grid and declares its remaining scalar assumptions;
it is not recovered factory data. It makes the mapper return and reaches
manager current worker 4/state 2 before a graphics-stream rejection. No firmware
guard or emulator fallback is introduced. The erased-input and display-only
receipts above retain their original results.

The [ID34 consumer analysis](id34.md) now separates immediate initialization
inputs from later grid processing. Four rectangle-coordinate tables and two
coefficient tables accompany the dimensions; the mapper also has 83 scalar
assignments and the preceding initializer consumes `+774`. The fresh official
[Z6II/Z7 comparison](../inputs/nor/id34.md) recovers matching Z6II schema and
Z7 display defaults, but no usable compatible ID34 record. Static structure
does not establish numeric factory values or a measured minimum that boots.

## Receipts

`run descriptor-20261021/` retains actual argv, original-byte
decompilations, `footer`, `phases`, `waitcode`, `cross` and `crossfault` probes
and filtered traces. Baseline and seed runs are kept separate; all use at
most one hardware breakpoint at a time and perform read-only guest inspection.
The previous native-start no-debugger reproduction of the panel fault remains
in [handoff.md](handoff.md). No original GUI first frame is claimed and no
device code changed, so device unit/qtests are not repeated for this diagnosis.
