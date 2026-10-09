# Startup IRQ and event dependencies

Issue [398](https://github.com/mashiro01/nightglass/issues/398), parent
[326](https://github.com/mashiro01/nightglass/issues/326). This inventory reads
the eight device sources at QEMU `193ee98138c85c2b2848836952228078a64e4f20`
and their saved firmware contracts. It describes every operation family in
those contracts, rather than counting refusal branches as missing devices.
Firmware addresses below are original MAS virtual addresses; device offsets
are relative to the diagnostic machine mappings, whose physical placement is
a model assumption. No new firmware extraction, native query or execution
was performed for this inventory.

## Saved consumers and current behavior

| Device / saved consumer family | Implemented operation families | Source-bound work still required |
|---|---|---|
| `access` / all eight devices | Shared first-stop report includes the actual CPU, PC, direction, width, value and mapped address; unsupported transactions return `MEMTX_ERROR` and stop the virtual machine. Debugger-only accesses do not substitute for a firmware observation. | This is the device error boundary, with no independent firmware register family or event producer. The router's fixed-base diagnostic is corrected to its mapped region address. |
| `router` / `102dc5ac..102dcc9a`, 1,728B/696 continuous candidate instructions, 13 saved function metadata bodies; registrar `102dcc20` separately has 13BB/100B/37 instructions | Two selector/configuration groups, all six lane word/byte fields, packet tail words, whole-word OR/BIC masks, A4 retained low-five-bit RMW, three raw/W1C domains, IRQ86/87/88 and explicit external pending inputs. The guest owns the eleven callback slots and service/clock state. | Requests `28/2c/30` and compound `102dcb4e` activation need the real request/input/result protocol. No producer of raw38/44/50 is bound. IRQ89..93, lane index bounds, data addresses and completion timing are unproved. Mask40/4c filtering remains a declared virtual convention. |
| `dispatcher` / `1047ee48..10480b1a`, 46 saved metadata ranges and 5,148B/2,074 candidate instructions; setup `1047f178` separately has 10BB/118B/58 instructions | Sparse byte configuration, disabled setup at C0A4/F81C, unavailable C0A8/F824 state, nine child masks, four explicit child input banks, raw/W1C and top-level routing to IRQ132/133. The two original setup polls naturally time out; their caller does not check those return values. | Enabling either setup with value8 needs a causal request/status producer. Child408/410/414/41c/420 involve `201620xx`, C014/C01C, F80C/F880..89C and guest buffers; the data publishing and ownership protocol must be bound before enabling those inputs. Other saved configuration families (0800/20xx/4000..4528/C400/C404/C40C/C818/CC00/CC04/F850) remain separate evidence work, not a zero register file. |
| `gate` / `103bce38..103bd75c`, 2,248B/906 candidate instructions; dispatcher `103bde3e` separately has 19BB/150B/58 instructions | All recovered finite configuration/table fields, three full-word mask/raw/W1C domains, external event admission, source-bound stop request with internal confirmation, original IRQ102 delivery and guest callback. Runtime OR/BIC at30/34 and generated packed-field masks are retained. | Asserted pulses20a4/2620/2660, submit2680/2800 and indexed/data/monitor reads at200/220..2ac/1b4..1bc/2040/38..44/140..144 need their actual resources and producers. The later indirect backend/packet family beyond the saved range is incomplete. No frame or table-commit completion is inferred from the stop confirmation. |
| `status` / `10243938..10243c4a`, 786B/306 candidate instructions, two saved metadata bodies/48 instructions | Packet setter08/0c, full-word10/14, bool00, retained unchanged20, unavailable24/30. Word widths are independent of packet byte widths. | Request20.bit0, stop20.bit16 and state24/30 need the real active resource/state lifecycle. The service154 request, callback1024396a and software event at object+18 are a distinct guest protocol. IRQ229's wrapper has no device status read/ACK, so its physical wire is not established. |
| `signal` / ten saved source candidate bodies, 856B/319 instructions; later ISR/config source adds three bodies, 212B/75 instructions | Five explicit level inputs, release/gate configuration, raw and filtered views, selected pair W1C/mask, nominal debounce, opaque configuration input at28000000, and five outputs combined through QEMU OR_IRQ to GIC237. | External signal providers, physical reset/clock and identity remain unbound. Selector3/4 configure pairs6/7 and4/5 while the ISR callback array uses pairs4/5 and6/7; that relation must be proved before any host permutation. Adjacent2b windows are not admitted. |
| `gpio` / six saved original Thumb windows, 388B/163 instructions; twelve-row COPY0 initializer and runtime objects | Five finite 12×16 banks, upper-halfword selected PDR writes, direction/function/pull RMW/readback, explicit standard pad-level inputs and pad outputs feeding the existing EXIU routes. All sixty initializer stores remain guest operations. | No actual external pin provider, alternate-function endpoint or physical pad identity is bound. The later whole 232-record constructor candidate binds logical service60 to port6 pin3, but not its live peer. The model's pull/idle level convention is not a recovered key or sensor input. |
| `exiu` / 672B/278 candidate instructions; saved native coverage 0/166 registration, 0/14 initializer, 310/492 runtime bytes | Sixteen COPY0-routed objects, external-source selection, mask/polarity/edge, real input transition latching, active-level reassertion, selected W1C, virtual self-clearing CLR read, status views and GIC65..80. The guest owns enable/disable, ISR and callbacks. | Nonzero source selection and software input1c lack their persistence/clear contract. Real external pad transitions, physical CLR readback and reset remain unproved. Candidate bytes and software object routing do not promote missing native function coverage. |

The source contracts are the fixed TOOL research referenced by each device's
protocol document: [router](../router/protocol.md),
[dispatcher](../dispatcher/protocol.md), [gate](protocol.md),
[gate runtime](runtime.md), [status](../status/protocol.md),
[signal](../signal/input.md), [GPIO](../gpio/protocol.md) and
[EXIU](../exiu/protocol.md). A continuous candidate decode, saved metadata,
saved native body and original runtime observation remain separate evidence.
None establishes all consumers beyond its recorded scope.

## Producers and startup ordering

The currently closed startup event is the [stop lifecycle](stop.md): original
`103bd4b2` writes global04=0; the explicitly virtual stop controller produces
raw18.bit0; mask0.bit0 admits IRQ102; original ISR `103bce38` calls the guest
service146 callback with R1.bit0; `10388c18` sets F0.bit2; the original ID88
wait can return. Its nominal delay is a model input. This is not a frame,
image-data or physical drain result. Ordinary activation04=1 produces no
completion, and closing external admission cannot suppress the internal
stop acknowledgment.

The DMA/source lane independently binds Audio output to HDMAC DREQ3/7 and
each engine's own completion IRQ (COPY45..48, FILL103/104, HDMAC150..157 and
142..149, Audio167/168). Its saved contracts supply no join from those
producers to this group's gate/router/dispatcher/signal/EXIU inputs. Those
numbers and neighboring addresses cannot be used as a substitute connection.
Original runs therefore leave the unbound inputs disconnected; synthetic
qtest inputs remain explicitly identified fixtures.

The RTOS post-slot8 inventory contains eight aggregate roots and 236 method
rows for slots9..12 and14..17. Each row retains the original getter bytes,
ROM/COPY0 descriptor domain, selected field4/field8 target and coverage gap.
The aggregate roots contain computed getter/field calls; their lack of direct
MMIO does not prove absence of a lower device dependency. Root13's saved
`103da6c8` starts task `103dc50e`. Slot14 first waits through `103610da` with
mask10/mode0, then calls `103e09b0`; its event object and producer are not yet
bound. It must not be replaced by the different ID88/mask2 stop chain.

Next work follows the saved selected method bodies to real MMIO/IRQ/event
consumers, retaining their direct calls, PC words and computed frontiers.
Only a proven request/resource/state publication and consumer/ACK join can
justify connecting a new producer. Merely recovering a callback pointer,
having a mask bit, timing out successfully at the caller, or returning1 from
an aggregate initializer does not establish hardware completion.

The first complete saved-source selection of those table targets finds
220 distinct methods in the 236 rows. The old fddb stream contains two exact
entry bodies, with 218 missing entries. This is an entry-coverage diagnostic,
not a count of missing devices or permission to decode arbitrary raw windows.
The preserved lower dependencies are:

| Table row / selected method | Saved body dependencies |
|---|---|
| Slot9, table1, index7, field4 / `102023a2` | Calls `1024240c`, then tail-jumps to the shared `102070b8` return1. The return value does not certify the callee's device effects. |
| Slot10, table1, index11, field4 / `1076eb78` | Calls `1045fc5e`, `1044278e` and `1045f6c0`. No PC-word MMIO reference occurs in this wrapper; the callees still require their own saved-body evidence. |

These facts come from the existing RTOS work report
`pkg-phase3-post-slot8-all-method-saved-source-first.json`, using the fixed
fddb BB/edge streams, not a new native run. The shared RTOS selector continues
the complete target set against newer saved streams and the four real direct
callee edges above. The current device inventory is not a claim that these
lower initialization methods or the remaining startup slots have completed.

The later read-only index from producer
`fadd878db1d528b514595f96f0ab87005d5cd596` supplies 93 of the 220 selected
method entries; 127 remain absent. Including the four direct dependencies
above, its report `pkg-phase3-post-slot8-fadd-indexed-method-dependencies-first.json`
contains 97 bodies, 921 original instruction rows and 136 calls to 101 unique
callees. Every selected row retains its original O/VA/space/mode/bytes and
official references/P-code. READ-referenced words are original data
observations, not propagated MMIO addresses or established hardware state.

`1024240c` is a conditional guest service dispatch: only r0=1 loads the
object pointer from `b18a7e54`, reads its +4 function and tail-calls it with
the original code pointers `102423b5`/`102423db` in r2/r1. It has no MMIO
access or callback installation, and that indirect callee is unresolved.
`1045fc5e` and `1045f6c0` each consist of `movs r0,#1; bx lr`; those constants
are interface results, not hardware readiness. The remaining `1044278e`
body creates a guest event through `103610d2` and invokes `103c039c` three
times. None justifies a fabricated external pending or host eventflag.
The shared RTOS source lane follows the 101 typed direct callee dependencies
and guest task/event identities; the lower unresolved calls remain explicit.

## Verification handoff

This child adds the [source-bound explicit pad input](../gpio/input.md),
corrects the router's diagnostic address and clarifies saved contracts; it
does not supply an external event producer. The sole builder owns C
qtests, registration and native execution. A relocated-router refusal check
should observe the actual mapped address and retain every configuration,
mask and pending word after an invalid width, hole or generated field.
Existing stop cancellation/coalescing, mask/W1C, input-level reset and guest
IRQ102 checks remain required when their implementation changes. No test,
native build or whole-boot success is claimed by this inventory.
