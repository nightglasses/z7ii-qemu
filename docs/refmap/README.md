# Address reference map

Durable, traceable **address -> function/chain** index for the Z7II firmware
(MAS/EX/SLA/VR/TPJ) and the QEMU model. Raw research stays in the receipts; this
directory is the stable index and the route dashboard.

It lives in **this repository** (not the upstream `qemu/` submodule and not the
firmware-ledger repository) because it is the cross-domain integration artifact:
firmware addresses, QEMU device/MMIO/IRQ addresses and runtime-observed chains
share one namespace here, and this repository owns the firmware-facing docs and
the device model.

## Files

[Download the complete address index](/refmap/REFS.tsv).
Every row retains its component, address, evidence class, references and notes.
Historical run labels are provenance identifiers; the
[public archive](../validation/evidence.md) lists the records actually available.

| file | purpose |
|---|---|
| `REFS.tsv` | master table; one row per address (component + evidence + source) |
| `master.dot` / `master.mmd` | full graph (Graphviz) and chain-route graph (Mermaid) |
| `chains/*.mmd` | end-to-end feature chains; Mermaid renders on GitHub |
| `FRONTIER.md` | research gaps: chain gaps + meaningful hypothesis/unknown nodes |

## Schema (`REFS.tsv`, TAB separated)

`component  addr  addr_kind  name  module  evidence  source  refs  notes`

- `component`: MAS | EX | SLA | VR | TPJ | QEMU | SHARED.
- `addr`: firmware = VMA (MAS `0x10020000`, EX `0x08000000`), MMIO = PA,
  state = base address; ranges allowed.
- `addr_kind`: code | data | mmio | ram | table | field | record | irq | region.
- `evidence`: observed | verified | inferred | hypothesis (see below).
- `source`: mandatory provenance (receipt path or issue URL).
- `refs`: related addresses (relation described in `notes`).

Evidence discipline: `observed` = runtime observation; `verified` = confirmed
against code/device contract; `inferred` = consistent interpretation; `hypothesis`
= unproven. Never present a hypothesis as fact. Unknowns are explicit, not dropped.

## Graph encoding

Node fill = component (MAS blue, EX green, SLA purple, VR cyan, TPJ tan, QEMU
grey, SHARED white). Node shape = `addr_kind`. Edge style/color = evidence:
green solid = observed/verified, amber dashed = inferred, orange dotted =
hypothesis, red dotted = missing. Red `MISSING:` nodes are runtime gaps
(code recovered, not naturally executed). A complete feature is one connected
chain; a break is the next research/modelling point.

## Chains

`boot` -> `provider` -> `display` is the main LCD route (they share boundary
nodes); `readout`, `input`, `graphics` are the parallel branches. `master.mmd`
combines them into one graph.

## Updating

Maintain the tracked `docs/refmap/REFS.tsv` directly. Keep the fixed columns,
component/address domain and evidence class intact. A source should identify
a tracked contract section, a published attachment or a fixed primary-source
reference. Historical run labels identify older observations whose raw captures
may be unavailable; they must not be treated as downloadable files or upgraded
to new verification. Update related graphs when a relationship changes, then
build the handbook to check links and diagram syntax.

## Provenance

Derived from `run **` receipts, `docs/**` and the issue tracker. The
receipts remain the evidence; this map only indexes them. It does not claim
physical-camera fidelity, and it does not modify firmware bytes.
