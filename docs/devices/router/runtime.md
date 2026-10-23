# Router requests and normal GUI startup

The current [finite timing/request model](timing.md) implements the three
commands under explicit functional hypotheses. Final-binary original execution
crosses the IRQ87 waits and next rejects capture-control PA `2006800c=1`.
The audit below preserves the earlier paired-control-only checkpoint; its
unsupported-request and absent-producer observations apply to that revision.

## Complete paired-control correction

The C1.70 MAS helpers `102dc808` and `102dc83c` select five pairs of
aligned word ports. Selector0 sets input bits in shared state; selector1
clears them. The original getter `102dc86c..102dc886` reconstructs the
combined control word through the set addresses. A zero mask preserves
the other fields and channels. Treating the aliases as ten independent
replacement words loses both accumulated requests and clear operations.

| Set PA | Clear PA | Source-generated mask |
|---|---|---|
| `20060000` | `20060004` | `3f` |
| `20060008` | `2006000c` | `7fff` |
| `20060010` | `20060014` | `1` |
| `20060018` | `2006001c` | `1` |
| `20060020` | `20060024` | `7ff` |

The upper callers support this interpretation: `102fead0(0)` clears
the low two control bits through `04`, while `102fead0(1)` sets them
through `00`. Both calls also submit a zero mask through the paired
`08` control field, which must preserve its prior value. `102feaa6` sets
the special bit11 control through `10`; `102feab4` clears it through
`14`. Both submit zero for the other groups, preserving their requests.
The six-lane stop body at `102dcb94` likewise writes `1c=1` before
changing its other controls.

The independently implemented Z7 C3.80
[paired-control handler](https://github.com/WonderLantern/z7-qemu/blob/97abf0b81cd3211f8374a38987a0beaf4549b90f/qemu/nikon-z7-lab.c#L1455)
uses the same set/clear interpretation and identifies its corresponding
getter. This is corroboration, not a physical Z7II register specification.
Cold state zero remains a declared functional convention. Only the set
aliases have a recovered read consumer; clear-alias reads are rejected.
All five original field masks, width/alignment checks and reset clearing
are retained. This correction adds no clock, request completion or IRQ.

The updated native router case first fails against the old model:
set `00=3f`, clear `04=21`, then read `00`: actual `3f`, expected `1e`.
After correction, `make check MTESTARGS=nikon_sla` passes all20 cases.
The router case covers all32 singleton input bits of all five groups,
partial/zero masks, bad widths/alignment, reset reuse, all75 independent
external event inputs, masked pending/W1C and absence of clock-driven events.
The qtest read API does not independently expose MEMTX_ERROR status.

## All three request producers

| Request PA | Original producer and condition | Established command |
|---|---|---|
| `20060028` | `102974de` → body `102dc888`; called by `102fea1e` when argument0 is nonzero | aligned WRITE32 `1` |
| `2006002c` | `102dc890` when input bit11 is set; wrapper `102974e8`; also `102957ee` after clearing special bit11 for input1/2 | aligned WRITE32 `1` |
| `20060030` | `102dc890` when input bit12 is set; also the compound `102dcb4e` after A4 bit3/2 clear and bit0/1/3 set | aligned WRITE32 `1` |

The normal GUI trace reaches `102d93a2`, takes `b18a7718 != 0`, then
calls `102fea1e(1,1)` at `102d9448`. The first argument produces `28=1`.
After that request returns, the second argument would select special bit11
and write `70=2`, then `70=3`, then `70=0`. A subsequent call at `102d9468`
has arguments `(1,0)`, requesting `28=1` without the force sequence.
The later `102d948c` call sets the special bit11 control; the caller then
waits for CSENEV01 bit8 at `102d949e`. These later operations are static
successors, not evidence that the unsupported request has returned.

The source establishes the three request formats and their ordering. It does
not establish whether a request resets phase, commits a configuration, or
produces a timing pulse, nor which raw cause follows which request. The
`2→3→0` force sequence alone supplies no mapping to IRQ87 bit4/7. No
request-status poll, request-to-cause table or clock unit is recovered here.
Neither the request stores nor a successful ISR consumer proves that mapping.

## Event consumers and the earlier absent producer

| Hardware status / dispatch | Original consumer | Guest result |
|---|---|---|
| IRQ86, `38` bits0..10 | `102dc5ea`; eleven registered callbacks, each selected bit ACKed separately | callback-dependent; current bit0 callback `102d6c38` sets CSENEV01 bit2 |
| IRQ87, `44` bit7 | `102dc5c0` → `102d6b6a` | public `02010001`; CSENEV01 bit4 after the optional `b18a7604` countdown reaches0 |
| IRQ87, `44` bit4 | same | public `02010002`; CSENEV01 bit8 after the optional `b18a7608` countdown reaches0 |
| IRQ87, `44` bit18 | same | CSENEV01 bit`10` |
| IRQ87, `44` bit10 | same | CSENEV01 bit`20` |
| IRQ88, `50` | `102dc5ac`; same-word ACK and optional object+`3c` callback | callback-dependent; no current callback installed |

`102d6b6a` additionally selects one public notification in priority order:
bits0/1 → `02010007`; bits11/12/16 → `02010006` with an additional
status read; bits13/14/15 → `02010005`; bit17 → `02010004`.
Bit0 also executes the original auxiliary-status read/clear methods. All
these software callbacks remain original guest execution. The model neither
calls them nor writes CSENEV01 directly.

The MAS router and the separate private SLA router each have three IRQ outputs
wired to their own GIC. Their75 named `external-pending` inputs are all
unconnected in original-bundle composition. Those fixture inputs are the
current model's only pending producers; configuration and time advance do
not create events. Thus the missing implementation is a TG hardware source
and its request/control contract, not a missing guest event sender.

The Z7 reference does not close that gap: its
[request entries](https://github.com/WonderLantern/z7-qemu/blob/97abf0b81cd3211f8374a38987a0beaf4549b90f/qemu/nikon-z7-lab.c#L2729)
only retain write-only configuration. Its
[periodic IRQ87 provider](https://github.com/WonderLantern/z7-qemu/blob/97abf0b81cd3211f8374a38987a0beaf4549b90f/qemu/nikon-z7-lab.c#L1338)
is explicitly a diagnostic timing experiment with an assumed cadence, not
an established request-driven device contract. Importing that timer would
add a new hypothesis rather than recover the current hardware condition.

## Earlier original continuation and remaining boundary

With unchanged bundle bytes and the existing functional NOR, RNG0,
acceleration and paced battery UART inputs, the corrected original run has:
`00=3`, `08=236`, `10=0`, `18=0`, `20=503`. The old replacement model
instead reported `08=0`, `20=103`: zero writes had erased the `08` control field,
and an earlier bit10 request had been lost. These corrected values are
derived model state, not physical-camera measurements.

The first rejection remains CPU0 `102dc88c`, WRITE32 PA `20060028=1`.
All three raw pending words remain0, the UART peer rejects no request,
and GUI gate/pending3/display-ready remain0. This is not advancement to a
new startup stop or closure of the three requests. All three request ports
remain explicit errors; no request is silently discarded or acknowledged.

Closing them requires evidence tying a request/control transition to the TG
operation and its cause/phase: a register contract, a trustworthy hardware
trace, or an independently justified finite hardware algorithm. The code
already identifies the exact consumer and the next wait, but those facts
cannot choose a reset/commit/pulse interpretation or a timer period.
Receipts and source projections remain in ignored
`work/runs/router-20261022/`, including original red, native red/green,
the corrected original `alias` snapshot and the failed first verifier
expectation (`20=103` instead of the preserved `503`).
