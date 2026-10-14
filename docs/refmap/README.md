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

The generator and the per-subsystem sources live in `work/runs/refmap/`
(git-ignored): `parts/*.tsv` (one writer per subsystem), `merge.py` (writes
`REFS.tsv` + graphs), `coverage.py` (reports source addresses missing from the
map), `extract_qemu.py` (QEMU MMIO/IRQ). Regenerate there, then copy the curated
set here. `REFS.tsv` may also be edited directly for small fixes; keep `source`
mandatory and evidence classes honest.

## Provenance

Derived from `work/runs/**` receipts, `docs/**` and the issue tracker. The
receipts remain the evidence; this map only indexes them. It does not claim
physical-camera fidelity, and it does not modify firmware bytes.
