# Documentation standards

This is the maintained editorial and evidence standard for the handbook.
It applies to articles, navigation, diagrams and published attachments.
Contribution metadata follows the repository's `CONTRIBUTING.md`; work tickets
belong there, rather than in technical explanations.

## Purpose and ownership

Each article answers one technical question. Its opening states the answer,
firmware/component and execution or input profile it covers, and the material
limit on that answer. A reader should not need a chat, ticket, private directory
or another developer's checkout to understand its conclusion.

Keep each maintained contract in one place. Link to it from guides, evidence
articles and the address index instead of copying its register tables or status.
The [startup frontier](validation/frontier.md) owns accepted startup progress;
device pages own device behavior; input pages own their synthetic profiles.
A later result must update or explicitly qualify older contradictory summaries.

| Article type | Minimum useful content |
| --- | --- |
| Guide | Reader goal, supported path, prerequisites, commands, expected result and links to detailed contracts |
| Device or memory contract | Producer and consumer, address domain/range, widths/fields, lifecycle, reset/cancel/error behavior, unsupported boundary, assumptions and scoped validation |
| Input profile | Explicit selection, complete supplied extent and fields, default/omitted-input behavior, validity limits and what remains synthetic |
| Runtime investigation | Profile and source binding, positive observations, conditional call chain, bounded negative observations, unresolved edge and next discriminating evidence |
| Validation article | Exact subject/revision, fixture or original-input identity, host/guest time basis, result and omissions; historical results retain their original scope |

Use only the sections the subject needs. Do not fill every article with empty
template headings, task lists or repeated project policy.

## Claims and evidence

Evidence describes how a claim is supported; it is not a ladder that turns a
fixture into a boot result. Qualify the subject whenever using “verified.”

| Claim | Required qualification |
| --- | --- |
| Source-established | Component, fixed source or original-image identity, relevant offsets/symbols and coverage limits; recovered software behavior is not physical hardware identity |
| Directly observed | Exact execution/input profile and revision, instrument/filter and observation window; retain relevant values and failure details |
| Implemented model contract | Current source location, finite accepted behavior and explicit functional choices; separate implementation from original-firmware execution |
| Inference | Supporting observations, reasoning, alternatives and the next check that can distinguish them |
| Hypothesis | Explicitly chosen behavior or input, its bounds and a falsifiable validation; do not describe it as recovered factory data |

Report native fixtures, unchanged-firmware execution under declared virtual
inputs, assisted experiments, reconstruction comparisons, complete startup and
physical-camera evidence separately. A passing fixture, a live task, a rendered
warning, programmed layer parameters and a normal GUI frame are different
milestones. State which one was observed.

“Not observed” requires a window and instrument scope. A missing trace entry
outside the filter, a chained TCG block or a sampled CPU PC cannot establish
permanent unreachability. Sequential register reads are not an atomic snapshot.
Distinguish host observation time, guest ticks and assumed virtual latency.

## Sources and public attachments

Use primary source materials and immutable repository commits when they support
a technical claim. A moving branch URL is background navigation, not a fixed
source binding. For an older comparison without a published revision binding,
say so; do not substitute the latest revision and call it the original source.
For manuals, name the available edition/revision and relevant section.

Embed the fields, observations and limits needed to assess the conclusion.
Publish reviewable supporting files under `docs/public/evidence/`, follow the
[archive policy](validation/evidence.md), and register page attachments in
`docs/.vitepress/evidence.json`. Each entry must name a real source page and an
existing file. Firmware bytes and raw factory disassembly remain excluded.

Historical run labels are logical identities, not downloadable paths. If the
underlying capture is unavailable, identify that limitation; do not promise a
file or use it as newly reproduced evidence. Preserve archived measurements,
failed trials, revision bindings and omissions when renaming or reorganizing.
An archive edit must never silently turn a failed trial into a passing result.

Generated local build paths are valid in runnable instructions. They are not
public evidence links. Exclude personal absolute paths, private capture paths
and attachments available only from a developer's `work/` tree.

## Language, notation and structure

Write public prose in English. Lead with the result, use descriptive headings
and concrete terms, and prefer short connected paragraphs. Use lists for
parallel items or steps, tables for fields/comparisons and code blocks for
commands or literal data. Separate words, numbers and units clearly.

Put identifiers, addresses, masks, paths and literal values in code formatting.
Declare component/domain and whether addresses are file offsets (`O`), CPU
virtual addresses (`VA`) or board physical addresses (`PA`). Define the numeric
base, range endpoint convention, byte order and units where ambiguity matters.
Do not infer a physical part, capacity, clock rate or electrical meaning from
an address or a compatible software layout alone.

Organize by subject, not ticket numbers or calendar entries. Use semantic page,
section and attachment names; qualify results with source revisions and named
profiles. Calendar dates do not belong in article status or filenames. Original
archive metadata, license notices, source editions and the Dark 2026 theme name
retain their identities. They are not a progress chronology.

Label historical observations near the beginning and link the superseding
contract or frontier. “Current,” “latest,” “now” and “next stop” require a clear
subject/profile; historical checkpoint text must not read as current startup
status. Remove obsolete conclusions from current summaries while preserving
the correction and its evidence in supporting analysis.

## Diagrams and reading experience

Use Mermaid when a call chain, state transition, ownership boundary or data
flow is clearer as a diagram. It builds to inline SVG through Pele and retains
zoom/fullscreen controls. A custom SVG belongs in tracked public assets with
its editable source or generation instructions. Do not depend on a private
rendering service, screenshot or scratch directory.

Explain the arrows and scope near each diagram. Mark functional assumptions,
conditional paths and historical observations explicitly. A drawn arrow is a
technical claim: do not add an unproved dependency to make a complete-looking
pipeline. Diagrams summarize the accompanying contract and do not replace
numeric tables, evidence or failure conditions.

Use the existing dark palette and shared text variables. Normal text must
have at least 4.5:1 contrast against its surface. Check labels, edge text,
clipping and wrapping in the rendered SVG; use the fullscreen view for large
graphs. Keep body scrolling intact outside fullscreen and preserve keyboard
exit from fullscreen diagrams.

## Review and acceptance

Before accepting a documentation change:

1. Check the changed claims against their actual source, profile and evidence.
   Correct stale introductions, diagrams, tables and cross-references together.
2. Add each article to navigation. Keep internal links and anchors valid; update
   all affected references when moving a section instead of leaving aliases.
3. Run `pnpm docs:build` after installing the locked dependencies. The same build
   runs documentation-rule tests, validates navigation/evidence entries, checks
   page targets, rejects forbidden presentation and renders every diagram.
   `npm run docs:build` invokes the identical script with installed dependencies.
4. Review changed anchors and the rendered pages. For diagram/theme changes,
   inspect the visual result and affected controls. A site build alone does not
   prove visual quality or firmware behavior.
5. Review the complete diff, commit a cohesive change and leave the owned tree
   clean. Preserve other worktrees and keep generated output ignored.

Automated checks enforce a useful subset of this standard. They cannot verify
a decompilation, select physical factory values or establish that a technical
claim is true. Editorial and source review remain required.
