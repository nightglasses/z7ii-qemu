# Application stages and GUI event producers

This article preserves bounded reverse-engineering observations and their corrections. It is supporting analysis, not the current boot-status page. Use the [GUI chain guide](guichain.md) for the current interpretation and [startup boundary](../validation/frontier.md) for accepted progress. Zero-hit results from historical multi-breakpoint probes are not absence evidence.

## Measured app-state progression (latest pass) {#measured-app-state-progression-latest-pass}

The MAS GUI app dispatcher at `0x1020D41A` takes `r1` as the state and `r0` as
the context pointer (`0xB1818BD0`). A 60 s window reaches states `1`, `2` and
`3`, and the state-3 branch at `0x1020D51E` executes (`lr=0x1020D42B` from the
dispatcher). The deeper one-run marker `0x1020D5FA` stays at zero, so it
belongs to a later sub-step rather than to the state-3 entry.

In the same pass the EX record lane delivers only the four boot records
(`0x01`, `0x0D`, `0x08`, `0x0A`). The type-`0x12` record never arrives, but it
is **not** a boot dependency: its EX handler `0x08033328` only stores one
payload byte into `0x200138da`, and that byte has a single writer and no
readers. The earlier reading of this pass that blamed the missing `0x12` for
the power classification and LCD start is withdrawn (see
`docs/runtime/ex.md`). The classifier `0x1038A6AA` staying at zero hits is
therefore its own question, and the code-supported frontier remains the
client-4 report field at `+61` described in "New frontier: MAS power
classification of the 112-byte record" above.

## What the normal EX-to-MAS flow still lacks {#what-the-normal-ex-to-mas-flow-still-lacks}

Reading the verified pieces in boot order, with the evidence class of each
link:

| Stage | State | Evidence |
|---|---|---|
| EX cold boot, RTOS init, cyclic monitors created | runs | 27 objects created, `probeRTOS2` |
| EX power monitors publishing (ids 113/114/115) | runs | `probeHOST1` lane counts |
| EX host-command dispatcher (101/102/103/104/131) | runs | `probeHOST1` |
| EX record dispatcher `0x080332f4` | runs for 4 boot records | `{type,handler}` table `0x0804e15c` |
| MAS kernel and GUI app states 1..3 | runs | dispatcher `0x1020d41a` |
| MAS record TX to `0x2c0a0000`/`0x2c0a0100` | runs | `probeMAS3` (0x80-byte frames) |
| EX power report emitted and classified by MAS | **classification wrong** | EX builder leaves `+61` zero; classifier `0x103f8ec6` reads it |
| Provider lifecycle and LCD start | not reached | downstream of the classification above |

So the normal flow is not missing a record *type*; it is missing the EX report
field `+61` (and the `0x103f8ae4` state beside it) that the MAS classifier
needs, or the MAS-side decode of that field. That is the next thing to settle,
and it is a data/condition question, not a missing command.

## Display owner: the natural start is reachable, the EGL present is dead {#display-owner-the-natural-start-is-reachable-the-egl-present-is-dead}

### Corrected call scan (direct observation) {#corrected-call-scan-direct-observation}

The earlier statement in this file that `sub_104db8ba` has no caller was a
scanning artefact.  A Thumb-2 `BL` is a 32-bit instruction that can start at
any 2-byte boundary; a 4-byte-aligned scan finds only half of them.  A
2-byte-aligned `BL`/`BLX` decode over the MAS image was validated against two
known call sites (`0x10569f4e -> sub_10569c76`, `0x10569eda -> sub_10568f1c`)
and reports:

| Target | Role | Call sites |
|---|---|---|
| `sub_104db8ba` | pre-restart display predicate | `0x104dc07c`, `0x104dc6de` |
| `sub_1046bf0c` | main/sub-unit command writer | `0x1044936c`, `0x1044a0c6`, `0x104db90e`, `0x104dc26a`, `0x104dc294`, `0x104dc2ce`, `0x104dc66c` |
| `sub_10568f60` | EGL present wrapper (`eglSwapBuffers` + window table) | none |
| `eglSwapBuffers` (`0x10680b8c`) | driver swap handler | only `0x10568f7c`, inside `sub_10568f60` |

So the natural panel start is reachable, and only the EGL present wrapper is
dead code.  This does not by itself make a frame: it moves the question from
"the start is unreferenced" to "what arms the display owner".

### Owner state machine (verified from code; refs in `docs/refmap/REFS.tsv`) {#owner-state-machine-verified-from-code-refs-in-docs-refmap-refs-tsv}

- `0x104dc804` is the display-owner lifecycle callback.  ROM method tables at
  `0x1079deb4`, `0x1079deec`, `0x1079df24` and `0x1079df5c` (0x38-byte rows
  from `0x1079deac`) hold `0x104dc805`, so the callback is registered rather
  than called by a `BL`.
- The callback reads the five lane state bytes at `0xB1D0D0BC + lane`.  Lane
  state 6 calls `0x104dc522`; lane state 7 calls `0x104dc46a`.
- `0x104dc522` switches on the owner counter `[ctx + 0x254]` through a
  28-entry TBB at `0x104dc54c`:

  | State | Target | Action |
  |---:|---|---|
  | 0 | `0x104dc568` | provider/commit probe |
  | 1 | `0x104dc5b4` | state-1 helper |
  | 5 | `0x104dc5c2` | state-5 helper |
  | 6 | `0x104dc5cc` | per-port setup, `0x104dae5a(1)`, `0x1046bf0c(sel 8, value 0)` at `0x104dc66c`, counter++ |
  | 2-4, 7-24 | `0x104dc5ba` | counter++ only |
  | 25 | `0x104dc690` | parks while `[0xB1D020A0]` (pending provider) is null |
  | 26 | `0x104dc6c0` | consumes the provider (`0x104dbb40`), clears the mailbox, calls `sub_104db8ba` |

  After state 26 the counter is reset to zero at `0x104dc7fc`, so the machine
  cycles.  `sub_104db8ba` itself writes `0x1046bf0c([0xB1808064], 8, 0)` at
  `0x104db90e` after its predicates `sub_1046f546(1)`, `sub_10470276`,
  `sub_10470214` and the display state read `sub_104810e6()` (global
  `0xB1D01800`).

### Provider admission (verified) {#provider-admission-verified}

- `0x104d7e78` is the admission wrapper.  It returns 3 after ten bounded
  retries while the manager-ready word `[0xB1807FA8]` is zero.  With ready set
  it checks manager `[0xB1807FA0]` and the request code through `0x1051d6f6`
  (accepts 0..17, 32..33, 37, 48..49, 64..67, 80..89, 96..111, 128..159, 161,
  176..190, 192 and 208..216).
- On success it calls `0x104d7d7e(0, code)`, the provider domain dispatcher,
  which builds objects with `0x1051d506` and publishes through `0x104d7b6e`
  (event `0x42060005`) or `0x104d7c60` (event `0x42061005`).
- The ready word is written by `0x104d7f16`; its only reference is a tail
  branch from the internal display self-test at `0x10637b72`.  The manager word
  is written by `0x104d79ea`, reachable from the F092 handler `0x10502db8`
  (request byte at `+0x10`).  `0x104dcd16` is the owner "accepted mode change"
  method, and `0x104dce06` moves eligible lanes from state 4 to state 6.

### Runtime receipt (direct observation, icount shift 6, 420 s) {#runtime-receipt-direct-observation-icount-shift-6-420-s}

At the first app draw (`sub_10569c76`, `a1=0xb81849c0`, `a3=0x200020`):

```text
disp = *(u32*)(0xB180807C) = 0xB1D0D0BC
ctx+0x254            = 0        owner counter
ctx+0x1c8 / ctx+0x1c9 = 0 / 1   display mode 1
ctx+0x2c0..0x2c3      = 3 3 5 5
lanes ctx+0..4        = 0 0 0 0 0
```

Later in the same window the UI dispatcher takes `0x42010001` and draws
`720x480` at `0xbf309600`, which the machine log names `lcd-composition-base`.
**That surface stays empty in the later runs:** a RAM dump after 30 s and after
90 s of unthrottled shift-6 execution (`run ex-6b-ercd/run-dump2.sh`,
`probeFB4`/`probeFB5`) finds `0xbf309600`, `0xcb4a7000` (`display-buffer-base`)
and `0xbec5f200` (resource-57 record 0) entirely zero, while the app draw
`0x10569c76` executes 41 times in 300 s.  The single earlier sighting of the
`720x480` composition draw is therefore not reproduced; the app draw that does
run writes other surfaces, and no candidate panel surface holds pixels yet.
The owner callback `0x104dc804` (entry), the owner dispatch site `0x104dc9ea`,
the natural start `0x104db8ba` and every `0x1046bf0c` call site record zero
hits.  So the app already renders a panel-sized frame into the composition
buffer, the display owner never runs, and no provider is admitted.

### Reading (inference, with the next measurement named) {#reading-inference-with-the-next-measurement-named}

The owner is registered but idle (lanes 0).  The frame therefore does not need
`eglSwapBuffers`: the DMPX window surface is created from a caller pointer
(`eglCreateWindowSurfacePointerDMPX`, `0x106815a4`) and the app renders directly
into the composition buffer.  What is missing is the display-service request
that arms the owner and fills the provider mailbox.  Two candidates remain:

1. the F091/F092 request family that arms the owner (`0x104dcd16`) and sets
   ready/manager, and
2. an image provider object that would fill `0xB1D020A0`.

`run ex-6b-ercd/probeDMSVC.gdb` watched the F0 router `0x104c6b14`,
admission `0x104d7e78`, the ready setter `0x104d7f16`, the manager setter
`0x104d79ea`, the owner event method `0x104dcd16`, the owner callback
`0x104dc804`, the two service handlers `0x10502d0a`/`0x10502db8` and the
provider builder `0x1051d506` for 480 s.  **All nine are zero-hit.**  A 900 s
confirmation with the draw marker (`probeDMSVC2`) also records only the first
`512x512` app draw (`a3=0x200020`) and still no service, ready, manager, owner
or start hit.  The whole display-service envelope path is idle, so the request
is not produced at all; the question is which component produces it, not which
gate rejects it.

Run-to-run variation is real but bounded: the first app draw is always seen;
the later screen switch and the `720x480` composition-buffer draw appeared in
one 420 s window and not in the 900 s window.  Compare guest progress, not
wall seconds, and treat a zero-hit late-stage probe as "not observed", not
"proven absent forever".  The four-way gate below (router, admission, owner,
start) is what the next windows must break.

## The application stage ladder and the AppC_Msg table {#the-application-stage-ladder-and-the-appc-msg-table}

### Static record (verified) {#static-record-verified}

The ROM contains a service record at `0x123b2b50`:

```text
+0  0xB1818BC4   RAM context pointer
+4  0x1209CB48   "AppC_Msg" name pointer
+8  0
+12 1
+0x3C = 0x123B2B8C  first entry of a 25-entry table of Thumb pointers
```

The table itself is not referenced by any `BL` or 32-bit literal; it sits
directly after the record, so the record base plus `0x3C` is the only
code-supported way to find it (inference).  Entries:

```text
0 0x10204B8D  1 0x10204B97  2 0x10204BB5  3 0x10204BB9  4 0x1020D5DF
5 0x1020D5FB  6 0x1020D621  7 0x1020D629  8 0x1020D631  9 0x1020D63F
10 0x1020D67D 11 0x1020D6AD 12 0x1020D6BD 13 0x1020D6C3 14 0x1020D6C9
15 0x1020D76F 16 0x1020D82B 17 0x10204BD9 18 0x1020D841 19 0x1020D8BD
20 0x1020D8F7 21 0x1020D93D 22 0x1020D945 23 0x10204BBD 24 0x10204BDB
```

The dispatcher that indexes this table has not been identified; the table
entries, not the index rule, are the verified part.

### Stage functions (verified from code) {#stage-functions-verified-from-code}

- `0x1020D41A(ctx, stage)` is the stage dispatcher.  Cases 1/2/3 run their
  setup blocks and then post `0x2000000` through `0x1020D364`.
- `0x1020D316(ctx, stage)` is the stage initializer:
  - stage 1 registers `sub_1020CA7A` through `0x103606F8`, creates the
    deferred-drain object (`0x102A7C18`) through `0x102A7C5C`, and writes five
    message templates into the deferred array: `(slot,cmd)` =
    `(0,0x03000003)`, `(1,0x03000005)`, `(6,0x03000007)`,
    `(7,0x03000008)`, `(2,0x06000003)`;
  - stage 3 registers the `"AppC_Lck"` lock at `ctx+0x388` and fills the
    deferred entry at `ctx+0x304` with `{ctx, 0x03000002, slot 4}` before
    calling `0x102A7C92`.
- Deferred queue: array base `0xB1819A80` (8 slots of 8 bytes), count at
  `0xB1819AE0`.  Push `0x102A7C92` copies the two words and signals through
  `0x1036074E`.  Drain `0x102A7C18(mask)` builds a 68-byte message from each
  set slot and links it at `ctx+32` through `0x10294342`; the slot's second
  word becomes message command `+16`.
- `0x10201854(stage)` is the AppC client fan-out: a ROM/RAM factory table at
  `0xB185DEF0` supplies six client factories, and each returned object's
  method `+4` is called with the stage value.  The factual meaning of the six
  clients is not yet identified.

### Measured stops (icount shift 6) {#measured-stops-icount-shift-6}

| Window breakpoints | Stages seen | Notes |
|---|---|---|
| stage dispatcher only | 1, 2, 3 | 420 s; no later event |
| dispatcher + message link + owner | 1, 2 | 420 s, `0x2000000`/`0x2000001` only |
| dispatcher + link + deferred push/drain + lifecycle (9) | 1, 2 | 420 s; defer pushes `(0,0x03000003)`, `(1,0x03000005)`, `(6,0x03000007)`, `(7,0x03000008)`, `(2,0x06000003)` |
| dispatcher + link + deferred push/drain + lifecycle (12) | 1, 2 | 600 s icount and 240 s wall clock, byte-identical |

The stage-3 deferred `0x03000002` message was **not observed** in any window
so far, because the windows that include the deferred probes stop at stage 2.
Probe-set size and SMP interleaving change how far a run gets; stage receipts
must be compared by stage, not by wall time or probe count.

### What this rules in and out {#what-this-rules-in-and-out}

- The app's own message framework works: the deferred push, drain, queue link
  and the `0x2000000`/`0x2000001` round trip all execute at stages 1-2.
- The display-service client (`0x10463A02` init, `0x10463BD4` control,
  `0x10463E8E`/`0x10478F3C` send -> `0x104C0F96`) never runs in any window,
  and neither does the F0 router or the owner.
- So the missing display request is not the app's generic message framework;
  it is the client that would call `0x104C0F96` with a display class, and that
  client is not being initialized or driven.

## The stage-3 GUI client and the live-view event {#the-stage-3-gui-client-and-the-live-view-event}

### Client fan-out (measured + code) {#client-fan-out-measured-code}

`0x10201854(stage)` reads a six-entry factory table at `0xB185DEF0` and calls
each returned object's method `+4` with the stage.  One measured filling of the
table (probeCLIENT, 240 s, first stage 2):

```text
0xB185DEF0  0x1042F51D
0xB185DEF4  0x104375C1
0xB185DEF8  0x10763BED
0xB185DEFC  0x102017D5
0xB185DF00  0x10201801
0xB185DF04  0x1042F51D   (same factory as entry 0)
```

The second client (`0x104375C1`) returns the static object `0x104376FC`; its
method `0x104375C4` is the GUI client's stage handler.  It handles stage 2 and
stage 3; the stage-3 block initialises the GUI and DMP pieces in this order:

```text
0x104377B8  0x1044E304 "GuiMeEv1" (id 0x10007)
0x1073ED94  0x1053CF38 "GuiLvEv1" (id 0x1000A)
0x1043DCD0 "GuiMsEv1" (id 0x1000B)
0x1072E1AC  0x10634034  0x10634056  0x10634072   (DMP/graphics setup)
0x1053A9E4  0x1073D3B8  0x106558C8  0x1065385C  0x1058014C  0x1063D98E
0x103BC584(...) with short string arguments
```

`0x103FAB8A(obj, "GuiLvEv1")` registers the event object and `0x106DDC68`
binds its handler.  The event ids are `0x10007` (menu), `0x1000A` (live view)
and `0x1000B`; the strings are the ones the earlier `gui-event-producer` probes
looked for.

### Live-view start gate (verified code) {#live-view-start-gate-verified-code}

`0x1053DBA2(arg)` posts event `0x40001` to the object at `0xB181D9B8` through
`0x1062E474` only when all five conditions hold:

```text
sub_1061E364()            != 0   (panel/engine state machine, no events 113-115)
*(u32*)(0xB181D970+0x2C)  == 0   (0xB181D99C)
sub_10633910()            -> v3 = 15 if nonzero else 5
sub_10652C36(v3)          != 0
sub_104A9824()            == 0
```

Only three sites call it: `0x1053DC48`, `0x1053DCBE`, `0x1053DEAE`, all in the
GUI event-module.  It is event-driven, not periodic.

### Measured stop (icount shift 6, 420 s) {#measured-stop-icount-shift-6-420-s}

`probeGUIC` records the client-2 method for stages 1, 2 and **3**, and the
`GuiLvEv1` registration with `r0=2, r1=0xEC=236` at the stage-3 block (the same
arguments as the earlier wall-clock `GUILVEV_REGISTER` receipt).  The same
window records **no** F0 router, no owner callback and no `0x1046bf0c` start.

`probeLV` (420 s) watches the live-view gate `0x1053DBA2`, its five condition
return sites, its only three callers (`0x1053DC48`, `0x1053DCBE`, `0x1053DEAE`),
the event post `0x1062E474`, the owner and every `0x1046bf0c` call site.  **All
are zero-hit.**  The gate is not even entered, so the question is not which
condition fails; the GUI event that should call the handler has not arrived.

The handler containing the three call sites is `0x1053DC04`.  It takes a small
event descriptor `r4` (`[r4]` = mode 0/1, `[r4+1]` must be 1, `[r4+4]` a value
compared with 2000), checks the GUI mode byte at `0xB181D970+2`, and calls the
live-view gate when the pending flag at `0xB181D970+0x28` is set.  It is an
event handler, not periodic code.

### The event ids that drive it (verified code) {#the-event-ids-that-drive-it-verified-code}

The caller is the GUI event dispatcher at `0x1053E19A`.  It fetches one event
per id through `0x104BDDE0(id, &data)` and calls the matching handler:

```text
0x3B (59) -> 0x1053DC04   (live-view handler; called at 0x1053EBC6)
0x32 (50) -> 0x1053EC74 -> 0x10632916(12); set event 59; 0x104E63BE;
                          clear event 59   (the only set-site for 59)
0x2F,0x30,0x31,0x33..0x3A,0x41.. -> other GUI handlers
```

So the live-view gate needs **GUI event 50 (0x32)**: the dispatcher translates
it into a one-shot event 59 in the next iteration.  A scan of the whole image
for `movs r0,#50` plus the kernel event-set call finds only this dispatcher
site, so event 50 is posted with a computed id (or by the event framework),
not a literal immediate.

### The event-50 producer (verified) {#the-event-50-producer-verified}

The GUI event post API is `0x104BD99E(id, value)`.  It stores the value word at
`0xB201C984 + 4*id` and sets the flag byte at `0xB201BDD0 + 4*id`; the get
`0x104BDDE0` returns the flag and clears it.  Scanning every `BL 0x104BD99E`
call site for its `r0` finds exactly one site that posts id 50:

```text
0x1062ED9E  movs r0,#50 ; bl 0x104BD99E
```

It sits inside `0x1062ECDE` and only runs after `0x1062ED6E` fetched event 50
and `sub_1062E9B4()` reported state 3 and then state 5 (`0x1062ED88` stores 1
at `[r6+4]` for state 3, `0x1062ED94` sets r5=1 for state 5).  So the live-view
start depends on the display-mode machine behind `0x1062E9B4` /
`0x1062E852` reaching state 5, which then reposts event 50.

`probeEV50` (420 s) records that entry twice but neither mode check
(`0x1062ED84`, `0x1062ED90`) nor the id-50 post (`0x1062ED9E`) runs; the
live-view gate and the owner stay zero-hit.  Because the `0x1062ED9E` post is
only reachable when event 50 was already present and the mode machine reported
5, the **initial** event-50 producer is a different `0x104BD99E` call site with
a computed id.  The next measurement either reads the mode machine
`0x1062E852`/`0x1062E9B4` at its nine call sites or narrows the 36 non-literal
post sites to the display one.

### Current frontier {#current-frontier}

The GUI client is initialised at stage 3 and registers the live-view event, but
GUI event 50 never arrives, so the live-view start gate never runs and the
display provider is never requested.  The frontier is now the producer of GUI
event 50 (and the `0x10007`/`0x1000A`/`0x1000B` family that feeds it), not the
AppC message framework and not EGL.

## The GUI mode block at 0xB181D970 is never written {#the-gui-mode-block-at-0xb181d970-is-never-written}

### The block and its consumers (verified code) {#the-block-and-its-consumers-verified-code}

| Offset | Meaning | Evidence |
|---|---|---|
| `+0` | mode request (`1`, `2`, `3`) | `0x1053FA1E` switches on it and maps `1 -> event 0x10002`, `2 -> 0x10003`, `3 -> 0x10009`, then clears it |
| `+2` | current mode byte | live-view handler `0x1053DC04` stores the event descriptor's first byte here at `0x1053DDA2`; the start branch requires `+2 == 3` |
| `+0x10` (dword) | `0x006B006A` static template value | read at the first draw |
| `+0x28` | pending flag | the handler calls the live-view gate only when it is nonzero |
| `+0x2C` | clear/gate input | the gate requires it to be `0` |

The GUI step dispatcher `0x105424E8` runs step 9 -> `0x10542486`, which first
calls `0x1053FA1E`.  While a mode request is pending that call returns 1 and
the GUI event dispatcher `0x1053E19A` is **skipped for that step**; it then
clears `+0`.  So `+0` is the hand-off from the mode-request producer to the GUI
event pass.

`sub_1062E9B4()` (the state read used by the event-50 handler) walks a five
entry RAM table at `0xB181AB54` (entry = `{u8 mode, pad, pad, pad, fn}`), calls
each `fn`, classifies through `0x106DDBE0`, and returns the first entry's mode
byte whose class is not 4.  The mode bytes are data, not computed.

### Measured (icount shift 6, 420 s) {#measured-icount-shift-6-420-s}

- At the first app draw the block is `+0..+0xC = 0`, `+0x10 = 0x006B006A`,
  `+0x14.. = 0`, `+2 = 0`, `+0x28 = 0`.
- Watchpoints on `+0` (u32) and `+2` (u16) record **no write** in 420 s.
- `0x1053E19A` (GUI event dispatcher) is zero-hit; the only mailbox fetch seen
  is one `id=0x31 flag=0` from another caller.
- Watchpoints on the GUI event mailbox for ids 50 and 59 (`0xB201BE98`,
  `0xB201BECC`, data `0xB201CA4C`) are **zero-hit**: neither event flag is ever
  set to 1.
- `0x1062E9B4` (mode read) is zero-hit.

### Reading (inference) {#reading-inference}

The GUI display-mode machine is completely dormant: no mode request reaches
`0xB181D970`, so `+2` stays 0 (the start branch needs 3), no event 59/50 is
posted, and the live-view gate and the display provider are never requested.
The producer of the `+0` mode request is therefore the next target; it is the
bridge between the camera state (EX host commands / power classification) and
the MAS GUI.  The event-50 self-loop at `0x1062ED9E` cannot bootstrap it,
because it only runs after event 50 was already present.

This is consistent with the EX-side frontier in
[`../validation/frontier.md`](../validation/frontier.md): the display start is
not blocked by EGL, by the app message framework, or by the display owner's
own code, but by a camera-state transition that never reaches the MAS GUI.

## The GUI view-state chain (event 0x39 -> mode request -> live view) {#the-gui-view-state-chain-event-0x39-mode-request-live-view}

### The writers of the mode request (verified code) {#the-writers-of-the-mode-request-verified-code}

`0x105405A2` is GUI step 1 (`0x105424E8` case 1).  It reads the view state
`0xB1821DBC` through `0x1054315E` and writes the mode request byte:

```text
0x1054073A  if (sub_1063D32C(r7) != 0 && r6 == 0):
              set kernel event 135 = 1
              state = 0xB1821DBC; clear 0xB1821DBC to 0
              25 -> 3, 26 -> 2, 28 -> 1, else -> 0
              strb mode -> [0xB181D970]   (0x10540762/66/6C, clear at 0x1054075A)
```

The view state is set to **25** by `0x1053DF9E(desc)` at `0x1053E05C`, which
requires `sub_1063385A() != 0`, `desc[0] <= 1` and `desc[1] == 1`; it also
programs `0x10634088(.., 0x2000000)` on that path.  A second branch sets the
live-view mode byte directly (`0x1053DFC4: [0xB181D972] = 1`).

`0x1053DF9E` is called only from `0x1054265A`, the descriptor handler for GUI
**event 0x39 (57)**, which requires `desc[0] == 3` and `desc[1] == 1` and then
calls `0x1053E072`, `0x1053DF9E`, `0x10542DA6` and `0x10630B10` in order.
`0x1054265A` is called from the GUI event dispatcher `0x1053E19A` at
`0x1053EB8C` after fetching event 0x39 through `0x104BDDE0`.

### The GUI step driver does not run (measured, icount shift 6, 420 s) {#the-gui-step-driver-does-not-run-measured-icount-shift-6-420-s}

`probeGUISTEP` watches the GUI step dispatcher `0x105424E8`, mode step
`0x105405A2`, the event-0x39 descriptor handler `0x1054265A`, the state-25
setter `0x1053DF9E`, the GUI event mailbox and the owner.  **All are zero-hit**
except one unrelated `GET id=0x31 flag=0`.  Event 0x39 is never fetched, so the
whole chain above never starts.

`0x105424E8` is referenced only by the ROM method table entry at `0x123B79C0`
next to `0x105425C5`/`0x105425F7` and `0x105408FB`/`4`; no `BL` or literal
reaches it.  The GUI descriptor around it is a ROM template:

```text
0x123B7680  0x01E002D0 0x01E002D0 0x64 0x10 0x11..0x1B   (720x480, mode ids)
0x123B7700  0x04120000..0x0412001E, 0x04120100..0x04120105
0x123B7904  0x006B006A ..                                 (runtime block template)
```

The `0x006B006A` at `0x123B7914` is exactly the runtime value seen at
`0xB181D980`, so the runtime mode block is a copy of this template.  The GUI
step machine is registered (table at `0x123B7978..0x123B79D0`) but no driver
invokes it.

### Conclusion {#conclusion}

The chain from GUI event 0x39 to the live-view gate is fully mapped, and the
missing link is now unambiguous: **the GUI application's step machine is never
driven**.  That driver is the next target; it is upstream of every remaining
display gate, including the mode request, the live-view event, the display
provider and the owner start.  The display code itself is complete and idle.
