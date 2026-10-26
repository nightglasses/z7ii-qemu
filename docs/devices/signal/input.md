# Five-route shared IRQ and finite configuration input

Native C and fixture recipe are **5ffcb6d3048319c8193fe49beb7249612b17559e**, following the fixed clock/private-class packet **ad4644947a854b53d0467b951a63196aaeb81562**. Source research is `nightglass` **fe97dba5059a95bfc4a97a4f1559d6a54216e447**, `research/z7ii/qemu-five-route-signal-control-contract.md`, with the IRQ extractor introduced at **6c5d7b9c4026ad7e9bde1ce563403b67b39c8631**. Its new 212-byte/75-instruction three-body evidence is anchored original Thumb code with typed literals; saved function/basic-block coverage gaps remain explicit. It does not become a new accepted native disassembly. The earlier ten saved bodies/856 bytes/319 instructions remain the sampling/configuration source.

The new original common ISR reads pending at `2b000008`, writes the same word back to clear it, dispatches rising/falling pair arguments through the five software callback slots, and has original registration for **GIC237**. Pair order is `(0,1),(2,3),(8,9),(6,7),(4,5)`. The initializer writes `28000000=0xf`, then `2b000004=0x3c`, then enables IRQ237 through the guest GIC API. These are software protocol facts; physical IP identity remains unknown.

Source correction **594e882bf3e387082a5a8a13dd5593aefe5123d9** distinguishes that producer-selector order from the ISR callback array: ISR pairs4/5 dispatch element3, and6/7 element4, while producer selectors3/4 configure6/7 and4/5 respectively. The software relation between these two original permutations remains unbound. The model preserves physical pending bits and original sampling selectors; it neither reorders guest callback storage nor calls those callbacks. Shared OR_IRQ aggregation is independent of that software indexing gap.

Pending has aligned 32-bit read access as well as W1C writes. Reads expose the unfiltered ten private pending bits, preserve state and never sample an input or clear an interrupt. This read meaning is an explicit functional convention; original read/writeback establishes access and ACK use, not mask-filter polarity. Masks select each retained pair's output contribution. Official QEMU **OR_IRQ**, configured for five lines, combines the five SysBus outputs before a single connection to the mature GIC's IRQ237 input. Clearing one route cannot lower the shared wire while another masked-in route remains pending. Sampling/configuration does not call guest callbacks or write guest flags; original ISR code performs those actions.

The separate aperture **[28000000,28000004)** is a named opaque configuration input. Only aligned 32-bit writes are accepted. The full guest value is retained, with virtual reset zero. Reads, other widths and neighboring addresses fail. This original consumer is not bound to a physical 280 IP, security/forwarding gate, ACK, clock, completion or IRQ producer. Its retained value has no event side effects. It is a distinct MemoryRegion on the sampler class, with dedicated access handlers; it does not widen the seven-word sampling window. Snapshot/migration remains explicitly unmigratable.

Disconnected external inputs start low. A board-owned storage backend now supplies
the actual route-3 insertion level. Reset retains live providers, cancels
debounce timers, clears pending/configuration and rebases sampled status without
an edge. The neighboring request lanes and host PHY have separate contracts;
the seven-word aperture does not acquire their behavior.

## Level sampling and event capture

The old conjunction of release and gate controls suppressed sampled status.
New C1.70 MAS consumers contradict that assumption: VA `105162a2` reads route-3
filtered insertion before power or PHY setup. Training VA `1051811e` clears its
gate through `1051670c(service333,3,0)` and leaves it clear on success, while
all twelve PCI config/MMIO/barrier methods at `105183d6..10518596` require the
guard `105187f4` to read that same filtered insertion as true. Cancellation
`105181ec/10518240` sets the gate again. Both gate values must therefore permit
truthful card-present status; neither can make the external card disappear.

The finite model separates levels from edge capture across all five routes:

| Operation | Level/status effect | Pending/IRQ effect |
| --- | --- | --- |
| External transition | Update raw immediately; sampled follows immediately or after configured debounce | Latch an actual sampled edge only when capture controls permit it |
| Word `00` release pulse or word `04` gate change | Cancel old deadline and rebase to actual raw input | No invented edge; disabled capture clears that route's pending pair |
| Word `14` debounce change | Cancel old deadline and rebase to actual raw input | No invented edge; active capture retains already latched causes |
| Masks and W1C | Do not change input levels | Select IRQ contribution or clear selected pending causes |
| Reset | Preserve actual provider levels and rebase sampled status | Cancel timers; clear pending/configuration and outputs without an edge |
| Status read | Return raw low bits and sampled high bits | No sampling, time advance or state change |

Word `00` and `04` retain the declared one-enabled convention for **edge
capture only**. Their physical polarity/function is not recovered. Raw/sampled
bank interpretation, W1C, one-enabled masks and the separate 1 MHz clock with
1/2/4/8 debounce ticks remain explicit functional assumptions. No constant
ready value, read-triggered update or GPIO release write is added to the guest.

The direct status audit covers seven MAS call sites: insertion readers
`1049d68e/105162a2`, host guards `105187ae/105187f4`, route-2 reader `1051a6f8`,
caller-selected `1051bbce`, and route-0/1 reader `107555d2`. The latter routes'
physical roles remain unresolved. The complete SLA setter family retains the
same field permutations. New native checks exercise all five pre-control
levels, both gate/release states, disabled-capture debounce, live reset and
original storage training with its gate cleared. The
[storage receipt](/evidence/storage/presence.json) keeps these tests and original
execution separate from the historical results below.

## Fixed validation

Incremental native build against upstream **4fc49f46dc95d4a27de2509e7fceb2931e91faeb** passed; final checkpatch has **0 errors/0 warnings**. Logs are `run startup/signal-irq-5ffcb6d-build.log` and `signal-irq-5ffcb6d-checkpatch.log`. Earlier intermediate comment-format warnings remain in their own logs. All 19 current C/header inputs directly match the upstream build copies; no payload hash substitutes for byte comparison.

`run startup/signal-irq-5ffcb6d/result.json` records **27/27 focused checks**, **1.7459200830198824 s** host wall time, on macOS. Twenty ARM fixtures cover the seven original words, pending cold read, all pulse/configuration permutations, strict field/width/direction/neighbor rejection, original and full-value opaque writes, and rejection of opaque reads/byte/halfword/adjacent accesses. Seven official qtest fixtures drive real qdev inputs and virtual Clock deadlines. The new aggregate lifecycle checks all five simultaneous masked pending bits, non-destructive reads, selective ACK retaining the shared IRQ until the last route clears, original ISR-style read/writeback ACK, both edges, no-event opaque writes, deferred debounce, reset cancellation and aggregate lowering. The previous six external-input/debounce/shared-gate fixtures also pass. These seven qtest cases execute no firmware; their input edges and aggregate output are synthetic validation, not original external events or a guest IAR/EOIR observation. The actual GIC237 connection is board wiring backed by original registration. Fixture names are unique and the recipe matches the frozen commit. The obsolete unsupported-pending-read case was replaced, not retained as a compatibility path. No unrelated full smoke or unchanged original stage was rerun or relabeled.

One changed-stage original run is `run startup/original-signal-irq-5ffcb6d/receipt.json`: native **5ffcb6d3048319c8193fe49beb7249612b17559e**, exit 0, **0.22970937506761402 s** host wall time, and unchanged 38,239,096-byte MAS payload under direct initial/final content comparison. The receipt contains the complete new assumptions and trace filter. It crosses the former `CPU1 PC1051669a / WRITE32 PA28000000=0xf` stop. Its next first uncovered access is `qemu.log:18528`: **CPU1 PC10242cbc, READ32 PA2402000c**, followed by **DFSR1008 / DFAR2402000c** at line18532. This is a new hardware-family dependency, not evidence for a zero reply or an adjacent wide mapping. The run does not prove an external five-route edge, guest IRQ237 handling, phase completion, physical 280 identity or menu. `whole_boot_verified` remains false. Old 77cf/400 receipts preserve their original narrower scope and are not modified.
