# Mode1 composition clear boundary

The preceding original early-F4 mode1 run stopped at CPU1 `102dbba2`, submitting engine0 at `20070010` with value1. [frontier.json](../../work/runs/timingpreset-20261025/frontier.json) records mask11hex, flags1, pattern/dimension/parameter0, width=stride0b40hex, rows01e0hex, destination`bf309600`. This is an observed request, not a successful fill or completed GUI startup.

## Original producer and owner

Read-only original MAS analysis (payload `work/runs/descriptor17-z7cross-mac1/02_eg1840_mas_01700.payload.bin`, base`10020000`) shows `10389e3a` constructing the fill configuration. In its second event branch it clears36 bytes, sets configuration byte0=1 and mask+32=17, copies width/stride/count from request halfwords+2/+4/+6, and computes destination from request word+8 plus u16(+12) plus width*u16(+14). Pattern comes from request word+16; format byte comes from+20; optional callback from+24. `10389eaa` calls `10389e08`, which starts the resource, clears completion eventbit1, submits through `102dba60` at`10389e24` with callback`10389dc2`, waits, then releases the resource.

The destination ownership is independently established by the original resource57 allocation/partition chain documented in [storage.md](storage.md#rear-lcd-composition-slot): ROM`123b7428`, record5 of the nine12-byte records at`123b73ec`, is `{1,004b0000,b181d388}`. Allocation route`1063da70 -> 1030d96e -> 1030d85c` selects index19; partition stores`bf309600` via`b181d388` at`10634b04/10634b06`, then advances by`4b0000`. Thus this request clears only `[bf309600,bf45ae00)`, length`151800` =720*480*4, within the larger owned slot `[bf309600,bf7b9600)`. Its tail and both neighbors must remain unchanged. The720x480 four-byte interpretation is consistent with existing RGBA canvas use; register units and synchronous zero-fill timing remain the explicitly declared functional laws in [fill protocol](../devices/fill/protocol.md), not physical-IP identification.

## Completion and wait

`102dba60` saves the callback at object+12 and copies36-byte configuration to object+16. For engine0 it writes the observed register shape and mask, then `102dbb9a` writes boolean1 to`20070010` at`102dbba2`. The established two-engine IRQ contract routes engine0/1 to GIC103/104; see [runtime contract](../devices/fill/runtime.md), which separately distinguishes original source from virtual result/error/stop timing laws.

The original upper callback `10389dc2` accepts event`41050c01`: saved configuration byte24 bit0 sets eventbit1 through`103854c4`; bit4 copies error-detail bytes28/29 into`b1809695/96` and sets eventbit64. It also calls an optional upper callback with the event and status word. Waiter`1038a3c6` clears eventbit256 and waits through`103854dc` for mask69 decimal (bits0/2/6), timeout3000. It additionally checks cancellation/control through`103854f4`; condition1 invokes stop`102dbb42`. This is the source completion-to-wait consumer, not permission to set guest events directly. That checkpoint had not yet demonstrated this request's ISR/ACK/callback completion; the accepted follow-up is below.

## Minimal finite extension

Add only destination`bf309600` to the existing engine0 exact720x480 zero-clear tuple. Retain mask11hex, flags1, all seven required configuration fields, control0, zero pattern/dimension/parameter, width=stride0b40hex, count01e0hex and ordinary writable-RAM DMA. No general dimension, stride, nonzero-pattern or address facility follows from this request. Preserve existing result, IRQ103, raw ACK, stop/reset and selected-address-space laws; real backing failure must remain error, not done. Native checks should cover first/last active bytes, untouched slot tail/neighbors, rejected near dimensions/destination/engine1, missing/read-only backing and reset/ACK behavior. Native success and subsequent original execution must be reported separately.

No final display/scanout or complete boot is established by this clear; the owned composition buffer is upstream of firmware-generated rendering and presentation.

## Accepted finite extension and current boundary

The extension now adds only that destination to the existing engine0 tuple.
`work/runs/reproducible-20261026/fill-red.json` records the preceding model's
rejection; `final-green.json` records all three fill qtests passing. They cover
the complete active clear, adjacent/tail guards, near-shape/address rejection,
IRQ/ACK/reset and actual failure in a selected private domain without backing.

The independent unchanged-bundle cold-F4 run in `mainline.json` observes status1,
mask11hex and error0 after the original submit. Original IRQ execution reaches
10389dc2 with event**41050c01** and saved configuration+24=1; waiter1038a3c6
returns1 at10389e2e. Active first/last words are0, while the next word at
bf45ae00 remainsff000000. No guest memory/event/callback was injected.

The earlier diagnostic filter mistakenly used41050801 after a decimal-to-hex
conversion error. Its timeouts are not evidence of a missing IRQ or blocked
callback. The accepted constant is directly checked against original ISR
102db8f6's literal1090849793 and the observed callback register.

The following25-host-second bounded window has no new uncovered transaction.
Manager remains current3/target5/mode1/state2, GUI worker class1/mode1,
power/GUI gates and pending3 remain0. Host SIGINT ends the observation; its
final EX PC is not a new firmware fault. A specific next readiness producer
has not yet been established. This mainline receipt has no battery peer;
the separately accepted reproducible authentication/0c/0e route is documented
in [batterypeer.md](batterypeer.md).
