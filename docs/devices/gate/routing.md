# Event admission and route configuration

## Shared route application request

The C1.70 MAS route group uses board PA `20102684` and `20102690` as two
retained packed configurations, then writes32-bit value1 to PA `20102680`.
The finite model snapshots both staged words into applied configuration at
that request. Later staging writes wait for another request; clearing one
family and requesting application removes that family while preserving the
other staged word. This is a configuration contract, not a pixel operator or
a completion producer. Its two-family snapshot is an explicit functional
inference from the shared application and teardown paths, not recovered
physical atomic-latch behavior.

Original MAS VA `103BD2D8` packs the ten-byte kind1 record into `2684`;
`103BD31E` packs the three-byte kind2 record into `2690`. The corresponding
setups at `103B1FF2` and `102E1A7C` assign routing indices from software-owned
allocation bitmaps. These indices are not image-buffer addresses.
VA `103BD2CC` writes the request and immediately returns0, without polling
status or awaiting a hardware completion. The normal photo request
`42193083` in VA `102E1B86` reaches `10389BAC` when its route record is present.
It then calls `103894A0 → 103B527C` to kick the original worker itself.
The separately observed VA `1038897E` release path zeros the selected kind1
or kind2 configuration, makes the same application request, then clears its
48-byte software record. These are MAS virtual code addresses; register
suffixes above are hexadecimal offsets within board PA `20100000`.

The existing packed write boundaries remain: `2684` accepts its full32-bit
setter shape and `2690` retains the finite16-bit shape. Application accepts
only aligned32-bit value1. Unknown request values, widths, alignment and
reads of `2680` remain errors. Reset clears staging, applied state and the
application count. Repeated valid requests capture the current state again.
Read-only QOM `applied-route1`, `applied-route2` and `route-commits` expose this
device state without assigning new guest readback semantics.

The request does not modify command activity, type0 slots, loaded packets,
pending events, masks or IRQs. There is no new timer, automatic busy clear,
fabricated DMA or completion. The current event-gate implementation has no
data-routing operator consuming these records; accepting their configuration
does not claim to implement that operator. Processing remains subject to the
existing command and pixel models.

The focused native SLA gate fixture passes, covering both-family staging,
commit, one-family teardown, simultaneous updates, repeats, unknown reads,
invalid writes, reset and preserved nonzero pending/mask/IRQ state. The old
model lacks the applied-state interface and rejects the original application
write. Native success remains separate from original startup progress.

In the unchanged cold-neutral photo profile with the fixed lens, original
execution submits one request and applies route1=0, route2=`141`. It then
reaches its paired start at CPU1 MAS VA `10369FC0`, WRITE32 PA `20380008=1`.
The paired model rejects mode field `88=2`; this is the next strict boundary,
not a route completion. At15 host seconds QMP is running, and by30 it is
debug-stopped. The60-second observation ends with583 successful lens packets,
zero lens rejection, no panic, no paired completion and no bound display.
The [curated receipt](/evidence/gate/route.json) records the exact input and
native scopes; normal photo live view and data-routing fidelity remain unproved.

## Historical event-gate and stop lifecycle

Native C and fixture recipe are `0698596d25cd9670075e21cda48911e92c063500`, after the independent 2016 report `695afa70ed2f5d1ae2549197e44ce3c2b1d46e5b`. Source is `e8738c4ad6eb985a781b9ff4bf48e939ff80a964`, the runtime section of `research/z7ii/qemu-2010-control-contract.md` in the source worktree. This source reuses the previously recovered collection; no new payload/native analysis was needed. The complete D03A six-instruction method reads global04, clears low2, ORs1 and writes it back. It has no packet, DMA source/size, delay, status poll, IRQ wait or completion producer. D4B2 independently writes complete0. D4BA asserts three still-unexplained pulses20a4/2620/2660 before writing08=0; that entire cancel/control candidate remains unsupported.

The existing finite configuration/table, three mask/raw domains and GIC102 device now uses04 low2 as a named virtual event gate. Value1 enables admission of new rising fixture input edges and pending participation in IRQ; value0 drops new edges, preserves pending/masks/configuration and suppresses IRQ. Reopening recomputes already-latched pending but never creates an event, frame, timer completion or data. All96 provider levels are tracked even while disabled, so held-high or disabled-era edges are not replayed on enable. Mask1-enable and W1C ACK continue to operate independently while stopped. These laws describe this functional profile, not proven physical pipeline semantics.

Read/RMW and full0 stop share one retained04 word. Nonzero writes must preserve all existing high30bits; low2=2/3 or high-field mutations reject before modifying gate, fields, pending, mask or IRQ. Cold reset0 remains an explicit assumption, rather than permission to configure unidentified high fields. Unknown pulse assertions and input/parameter/table activations remain rejected. This change does not infer any linkage from global04 to physical frame handling, source data or prospective2000/2100 input selectors. Reset clears gate/config/masks/pending and output while retaining actual provider levels; enabling after reset does not synthesize an edge. Original-run event inputs stay disconnected and no guest callback, flag, ready state or memory is supplied.

Only the existing2010 C, device fixture script and future original-run assumptions/filter changed. Obsolete fixture expectation that04=1 must fail was replaced by04=2; affected pending/preflight fixtures now explicitly enable the gate. Historical native/receipt files remain unchanged. The new caller filter10201a12+36 was used only in this changed-stage original observation, not an extra old-model replay.

The one-job incremental native build exited zero at upstream `4fc49f46dc95d4a27de2509e7fceb2931e91faeb`; all29 board C/header inputs directly equal upstream build inputs. Final checkpatch reported zero errors and zero warnings. `run startup/control2010-gate-0698596/result.json` records19 unique Mac-local affected checks, all passed in `1.7319477909477428` seconds. Fourteen ARM fixtures cover the original RMW enable, stop preserving configuration, reopen without ready, strict widths, modes2/3, unidentified high fields and unsupported cancel pulses. Three new qtests cover stopped-edge dropping and held-level tracking, preserved pending/masks and independent ACK across close/reopen, preflight retaining complete state and reset/recovery. Two affected existing qtests verify three-domain mask/ACK and full-state preflight under an explicitly enabled gate. These are necessary changed-behavior regressions, not a repeat of the historical20-check whole2010 packet or any whole suite. All tests passed on the first focused run; virtual IRQ output checks do not establish original ISR delivery or physical external sources.

The one changed-stage original observation is `run startup/original-control2010-gate-0698596/receipt.json`. It ended with `bounded_observation_timeout`, exit0 after `5.129526625038125` seconds at the existing observation bound. The38,239,096-byte MAS input passed direct initial/final equality; whole-boot remains false. The retained qemu.log contains no uncovered/unsupported transaction, Data/Prefetch Abort or DFSR/DFAR marker. This only describes the retained bounded observation. Final CPU0 PC is10107094, CPU1 PC10107074, both with ordinary LR10104d48; their actual wait/caller/state cause was handed to the RTOS source owner. No kernel-idle success, completed phase, peer readiness, menu or usable firmware is inferred. The deadline was not lengthened and the unchanged model was not replayed to wait longer. This packet is Mac-local verification, not Linux CI.
