# MAS readout state ownership

This note records the firmware-owned state that gates the C1.70 readout
descriptor producer.  It is static evidence, not an unchanged-firmware boot
receipt.  The source is the reconstructed MAS closure at
`nightglass-firmware` commit
`6c4ba4cf3226d69f8f62130cfdec6394a3aeb339`, under
`src/mas_outer/mas_closure_10020000/`.  Every ROM address below therefore uses
the `0x10020000` VMA.

The values stored at ROM VAs `0x102395b4` and `0x1023c750` are both the
little-endian literal `0xb26b3888`.  They are literal-pool words, not Thumb
entry points.  The exact predicate is Thumb function
`[0x1023c5ee, 0x1023c604)`.

## State layout

Let `R = 0xb26b3888` and `S = R + 0x2c = 0xb26b38b4`.

| Address | `S` offset | Recovered role |
|---|---:|---|
| `0xb26b38b4` | `+0x00` | lane 0 accumulated mask |
| `0xb26b38b8` | `+0x04` | lane 1 accumulated mask |
| `0xb26b38bc` | `+0x08` | lane 2 accumulated mask |
| `0xb26b38c0` | `+0x0c` | OR of the three lane masks; predicate tests bit 7 |
| `0xb26b38cc` | `+0x18` | lane-participation byte; bits 0, 1 and 2 mean lanes 0, 1 and 2 have participated; predicate tests bit 1 |
| `0xb26b38d4` | `+0x20` | set to one when an added mask contains bit 7 |

The predicate at `0x1023c5ee` loads `R`, advances to `S`, reads
`[S + 0x0c]` at `0x1023c5f4`, rejects when bit 7 is clear, reads
`[S + 0x18]` at `0x1023c5fa`, and rejects when bit 1 is clear.  Thus it is:

```text
(S.aggregate & 0x80) != 0 && (S.participation & 0x02) != 0
```

## Initialization and whole-object reset

There are two source-backed zeroing paths.

First, ARM startup function `0x1002bd9c` loads destination `0xb18aedc0` and
length `0x012af750`, then calls ARM function
`[0x1002c234, 0x1002c2a0)` at `0x1002be70`.  That function programs the fill
engine at `0x20070000` with a zero pattern, `0x4000`-byte units and the caller's
destination.  The range is `[0xb18aedc0, 0xb2b5e510)`.  `R + 0x38` and
`R + 0x44` are respectively offsets `0x00e04b00` and `0x00e04b0c` within the
range, so firmware-issued startup fill initializes both to zero.  This is not
authority for QEMU to preset either field independently.

Second, Thumb function `[0x1023980a, 0x10239820)` computes
`0xb26b3904 - 0x50 = S` and calls the zero-fill routine `0x10361034` with
length `0x50` at `0x10239814`.  `0x10361034` places zero in `r2` and its
fall-through body fills the requested byte count.  This resets all three lane
masks, the aggregate, participation byte and bit-7 latch.  It then tail-calls
`0x10239740` with its original argument for separate flag-dependent lifecycle
cleanup.

All four direct callers of this state reset are visible in the reconstructed
closure:

| Call site | Containing path | Condition before reset | Following writer |
|---|---|---|---|
| `0x1022fccc` | `0x1022fca6` | unconditional after its optional callback | lane 1 mask `1` unless owner state is `2` or `6`; lane 2 mask `1` when state is `2` |
| `0x10234dee` | `0x10234cd0` | after optional synchronization acquire/release; input is non-null and `0x103aeafe` succeeds | lane 0 with mask returned by `0x1023177c(..., 1)` |
| `0x102351d6` | `0x10235188` | first argument nonzero, `0x103aeb22` succeeds and second argument nonzero | lane 2 mask `0x0b` |
| `0x10235296` | local entry `0x10235236` | first argument nonzero, `0x103aeb22` succeeds, second object and both of its words are nonzero | lane 2 mask `1` |

The owner constructor is distinct from field initialization.  During
`0x10232af8 -> 0x10238e40`, code at `0x10238edc` loads literal
`0xb26b3924`, subtracts `0x9c` to recover `R`, and calls `0x103606b6` at
`0x10238ee4` with callback `0x10238d4b` and mask `0x3fffffff`.
`0x103606b6` constructs the event object at `R + 0x20` and stores its callback
and mask at `R + 0x24` and `R + 0x28`; it does not write `R + 0x38` or
`R + 0x44`.

## Set writers and their callers

The three exact Thumb setters are:

| Function body | Lane | Target writes |
|---|---:|---|
| `[0x1023ad62, 0x1023ad8e)` | 0 | OR mask into `S+0` at `0x1023ad6c`; OR it into aggregate at `0x1023ad74`; OR participation bit 0 at `0x1023ad7c` |
| entry `0x1023ad8e`, writer body `[0x1023ad8e, 0x1023adba)` | 1 | OR mask into `S+4` at `0x1023ad98`; OR it into aggregate at `0x1023ada0`; OR participation bit 1 at `0x1023ada8` |
| `[0x1023adba, 0x1023ade6)` | 2 | OR mask into `S+8` at `0x1023adc6`; OR it into aggregate at `0x1023adce`; OR participation bit 2 at `0x1023add6` |

Each setter also writes one to `S + 0x20` when the supplied mask contains
bit 7, then enters the common state/service dispatcher rooted at
`0x1023abaa`.  Participation is set even when the supplied mask is zero.

Exhaustive direct-call enumeration gives these setter sites:

| Call site | Lane and mask | Static trigger |
|---|---|---|
| `0x1022fcec` | lane 1, `1` | after `0x1022fca6` reset, owner state byte is neither `2` nor `6` |
| `0x1022fd18` | lane 2, `1` | after the same reset, owner state byte is `2`; selector comes from `0x10210ae2` |
| `0x102320f8` | lane 1, dynamic delta | `0x10232040` computes new bits with BIC, applies its event-code filter, delta is nonzero and nested state is not `3` |
| `0x1023213c` | lane 2, dynamic delta | same updater when nested state is `3`; lane-2 participation is recorded even if the delta is zero |
| `0x10234948` | lane 1, current `B+0x5c` OR `0x80` | the natural readout producer conditions described below; bit 7 is fixed but other retained mask bits may also be present |
| `0x10234e06` | lane 0, dynamic mask | after the `0x10234cd0` reset and its input/service validation; mask is from `0x1023177c(..., 1)` |
| `0x10235202` | lane 2, `0x0b` | after the `0x10235188` reset and successful input/service validation |
| `0x102352c2` | lane 2, `1` | after the local `0x10235236` reset and successful two-word input validation |
| `0x102352f2` | lane 2, `0x0a` | tail call from `[0x102352e2, 0x102352f6)` after publishing its local state word |

The lane-1 call at `0x10234948` is the only enumerated site that unconditionally
adds bit 7 while satisfying both predicate terms in one call.  Its complete
mask is the current `B+0x5c` OR `0x80`, not necessarily exactly `0x80`.
A dynamic lane-1 call can do the same when its delta contains bit 7.  Because aggregate and
participation are separate, lane-1 participation can also be established by
an earlier lane-1 call while a later lane-0 or lane-2 bit-7 mask supplies the
aggregate term, provided no whole-state reset intervenes.

## Clear writers

Two mask-removal functions clear mask bits but deliberately preserve the
participation byte:

| Function | Writes | Direct callers and condition |
|---|---|---|
| `0x10238680` | BIC input mask from `S+0`, `S+4`, `S+8` and aggregate at `0x10238686`, `0x1023868c`, `0x10238692`, `0x10238698` | `0x102301fa` in teardown path `0x102301b6`; and `0x1023c59a` when participation bit 2 is set, the companion service byte is `1`, and the selected slot callback is non-null |
| `0x102389dc` | after its flag/service preparation, BIC input mask from all three lanes and aggregate in `[0x10238a42, 0x10238a5a)` | sole call `0x102301d8`, the zero-mode branch of `0x102301b6` |

In `0x102301b6`, the nonzero-mode branch selects `0x10238680`; its mask is
the active owner `+0x5c` mask when present, otherwise the alternate stored
mask.  On success it calls `0x1023aed6(mask)` and zeros that source mask.  The
zero-mode branch clears event bit 2, calls `0x102389dc` with owner `+0x5c`,
and performs the associated wait/notification handling.  `0x102389dc` may
set `S + 0x1c` after observing participation bit 1, but does not alter
`S + 0x18`.

No per-mask clear writes `R + 0x44`.  The participation byte is sticky only
within a state session: `0x1023980a` clears it together with the masks, and
the startup fill initializes it on cold start.

## Natural producer and event join

The readout producer inside Thumb function body
`[0x102347b6, 0x102349a8)` has an initial start path and a later state-2
update path; they must not be collapsed into one linear call chain:

```text
entry with B[3] == 1
    -> 0x10234948 lane-1 setter(mask=*(B+0x5c)|0x80,
                                vector=0xb25f2510)
    -> 0x1023ad8e -> 0x1023abaa
    -> bit-7/vector gate at 0x1023acfa
    -> 0x1023ad04 -> 0x10238876 -> 0x10238910
    -> 0x1023869e descriptor builder
    -> on nonzero return set B[3] = 2 and return

later entry with B[3] == 2
    -> 0x10234990
    -> 0x1023b00e
    -> 0x1023aed8
    -> 0x1023afc6
    -> 0x1023869e descriptor builder
```

Let `P` be the entry argument's first word and let `B = 0xb1887d70`.
`0x10360f2e(..., 0x11)` operates on the mutex word at `0xb1887da0`; its
Boolean return is used only by the matching conditional release, so `0x11`
is not established as a service ID.  `0x101004ce(0x2015, sp + 0x10, 8)` is a
bounded keyed-data copy wrapper and unconditionally returns zero.  The
`CBNZ` at `0x10234814` therefore does not branch in this reconstruction; this
call is not a request or response dispatcher.

The necessary success chain from the entry to the fixed setter is `P != 0`,
a non-null handle at `0xb25f250c`, its field conversion through
`0x10443e3e` (which returns one on all reconstructed paths), nonzero
`0x10398206(sp)`,
`0x10235c84(P, 0) != 0`, `B[3] == 1`, and `*(u32 *)(B + 0x60) != 0`.
On success `0x1023493c..0x10234944` also ORs `0x80` into `B + 0x5c`, then
`0x10234948` calls the lane-1 setter with vector `0xb25f2510`.  The setter
writes the lane/aggregate/participation state, then tail-calls the common
dispatcher at `0x1023abaa`.  Its bit-7 path requires vector slot `+0x14` to
be non-null before reaching the descriptor builder through `0x10238876`.
A nonzero setter return advances `B[3]` to `2` and returns from this
invocation.  State byte `2` takes `0x10234978` on a later invocation and
reaches `0x10234990` without calling the fixed setter again; at `0x1023aed8`
that update path preserves the input mask and requires its bit `0x80` plus a
non-null vector slot `+0x14`; it does not compute a delta.  The vector slot is
written with parameter pointer `0xb25eefd0` at `0x1023490a`.  This fixed
lifecycle object is distinct from
aggregate-state root `R`.  Static control flow establishes both paths but
does not prove that unchanged firmware has naturally executed either entry.

Separately, the firmware-local code-`0x281` path publishes owner event bit 29:

```text
0x10452976 -> 0x10452ab0 -> 0x1047b944 -> 0x10498a04
    -> 0x103610cc(..., 0x20000000)
    -> registered callback 0x10238d4a
    -> 0x1023c610
```

`0x1023c610` first consumes another owner phase word.  When that word is zero,
it calls the predicate at `0x1023c62e`; a true result reaches `0x1023c648`,
which calls `0x102cd1ce`, loads the returned object at `+0x0c`, then calls
`0x1020c7ba` and `0x1020c7b2` for the recovered parameter/marker join.

This establishes a firmware-owned state machine: session reset, lane/owner
participation, a first entry that sets the lane-1 readout mask, can build the
initial descriptor and advances the owner state, a later state-2 descriptor
update path, then a separate firmware-local bit-29 callback that admits the
final marker path.
It does not establish a natural runtime traversal of those phases.  CMOS
completion must not be translated directly to code `0x281` or bit 29.

## Enumeration boundary and next unknown

The writer audit covered every reconstructed literal in
`[0xb26b3788, 0xb26b3988]`, not only direct references to `R`.  This is what
exposed the indirect `0xb26b3904 - 0x50` reset.  The remaining nearby bases
do not compute or pass an alias that writes either target field.  Direct calls
to all three setters, both mask clearers and the whole-state reset were then
enumerated from the reconstructed assembly.  Static control flow was
sufficient, so no runtime state write or active probe was used.

The one precise unresolved upstream edge is the natural caller of entry
`0x102347b6`.  Reconstructed code contains no direct BL/BLX caller and the
recovered literal inventory contains no `0x102347b6/7` function pointer.
This does not prove the complete payload lacks either form: local metadata
marks only `4,227,868` of `38,239,096` payload bytes declared, leaving
`34,011,228` bytes undeclared and `full_payload_ready=false`.  An indirect
registration/table dispatch is one candidate, but an unrecovered direct
caller is not excluded.  Until that edge is recovered, zero natural readout
requests locate the blockage before the firmware producer, not after
`NikonReadoutPipeline` submission.

The minimal passive probe is a breakpoint at `0x102347b6` that records
`r0-r3`, `lr`, `sp` and `cpsr`, plus read-only snapshots of the input wrapper,
its first-word `P`, `P+0x144`, and `B+3/+0x5c/+0x60`.  Optional breakpoints at
`0x10234948`, `0x10234990` and `0x1023869e` distinguish the start setter,
state-2 update and builder caller.  These observations must not write guest
registers, memory, callbacks or IRQ state; absence of a hit proves only that
the bounded observation window did not enter the function.

QEMU must continue to leave `R + 0x38`, `R + 0x44`, code `0x281`, event bit 29
and guest callbacks under firmware ownership.  Completion still needs a
source-backed device pending -> GIC -> original ISR -> ACK -> original task
consumer before unchanged-firmware progress can be claimed.
