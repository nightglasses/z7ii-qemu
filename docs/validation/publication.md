# Display publication and kernel waits

These observations qualify a specific input and source revision. They do not describe a new execution or replace the [current startup boundary](frontier.md). Earlier negative observations retain their original scope; later corrections are stated beside the affected interpretation.

## MAS display owner (direct observation, 420 s icount shift 6) {#mas-display-owner-direct-observation-420-s-icount-shift-6}

The MAS side has its own, nearer gate, and it is not `eglSwapBuffers`.  The
display-owner callback `0x104dc804` is registered in the ROM method tables at
`0x1079deac..`, and its state dispatcher `0x104dc522` can reach the natural
panel start `sub_104db8ba` (state 26) and the mask-8 start at `0x104dc66c`
(state 6).  A 2-byte-aligned Thumb `BL` scan finds both call sites, correcting
the earlier "unreferenced" reading that came from 4-byte alignment.

What is idle is the owner itself: at the first app draw the owner context
`0xB1D0D0BC` has all five lane bytes zero, owner counter `+0x254` zero, and
mode `+0x1c9` one, while the app already draws `720x480` into
`0xbf309600` (the machine's `lcd-composition-base`).  The owner callback, the
dispatch site, the natural start and every `0x1046bf0c` call site have zero
hits in that window.  Frame publication therefore needs a display-service
request that arms the owner and admits a provider (`0x104d7e78`, manager-ready
`0xB1807FA8`, manager `0xB1807FA0`, events `0x42060005`/`0x42061005`), not an
EGL swap.  `docs/runtime/guichain.md` carries the full chain and the next
measurement (`probeDMSVC`); the power-classification link above stays a
correlation, not the proximate cause of the idle owner.

The same run narrows the MAS side one step further: the GUI mode block at
`0xB181D970` is never written (u32 `+0` and u16 `+2` watchpoints, zero hits),
the GUI event mailbox never sets ids 50 or 59, the GUI event dispatcher
`0x1053E19A` does not run, and the mode read `0x1062E9B4` is never called.
So the camera-state transition that should request a display mode never reaches
the MAS GUI.  What is still missing is a *producer* (an EX host command or
power-state transition), not display code or model behaviour; both the EX gates
above and this GUI mode request are on the same unexplained path.

The producer chain is now mapped in `docs/runtime/guichain.md`: GUI event
`0x39` -> `0x1054265A` -> view state `25` (`0x1053DF9E`) -> GUI step 1
(`0x105405A2`) -> mode request `3` -> event `0x10009` -> live view.  A 420 s
probe shows the GUI step dispatcher `0x105424E8` and every node of that chain
zero-hit, so the GUI application's step machine is registered in the ROM table
at `0x123B7978..0x123B79D0` but nothing drives it.  The runtime GUI worker
context at `0xB181D9B8` is fully populated and blocks on RTOS event
`0x010001BE`; at the same moment MAS core 1 sits in the scheduler idle loop
(`0x10107078`) while core 0 runs the app.  The missing signal for that worker
is the single upstream gate for the first frame.

Two further receipts bound the same edge:

- The embedded MAS inner image (`O[0x208853C,0x228CF3C)`, 2.1 MiB) **is copied**
  by the guest to `0xB1000000` (the RAM dump matches the payload head), but
  neither the vector base `0xB1000000` nor the reset handler `0xB1000060` is
  ever entered in 420 s.  It contains no GUI strings.
- The only poster of GUI event `0x39` (`0x10695450`) has **no reference** in
  any static form (Thumb/ARM `BL`/`BLX`, 32-bit literals, `TBB`/`TBH` tables),
  and the address appears in neither the RAM window `0xB1800000..0xB3000000`
  nor the inner-image dump.  The same holds for the mode dispatcher
  `0x10695750`.

If no runtime-computed producer exists outside those scans, this image has no
reachable producer for the live-view branch, which would make the first frame a
firmware-input boundary rather than a device-model gap.  Fabricating the event
would be a synthetic stimulus, not an unchanged-firmware receipt.

The overall shape is *registration without a driver*: app stages 1..3 and the
720x480 draw into `0xbf309600` run; the GUI worker (event `0x010001BE`), the
GUI event dispatcher, the only event-`0x39` poster, the mode request at
`0xB181D970`, the live-view gate, the display owner and the provider admission
are all idle; the SLA readout initializes but never completes; the inner image
is copied but never entered.  With the unchanged container and no external
stimulus the first GUI frame is outside the reachable set, and every remaining
input is of the "camera state / sensor readout" kind that the objective
forbids synthesizing.

## Probe time base {#probe-time-base}

Original-firmware probes run with `-accel tcg,thread=single -icount shift=6`:
icount derives `QEMU_CLOCK_VIRTUAL` from executed instructions, so peripheral
byte timers advance with guest progress instead of wall clock, and `shift=6`
(64 ns per instruction) matches one instruction per cycle at the modelled
16 MHz HSI.  icount is incompatible with multi-threaded TCG.  Compare receipts
across time bases by guest progress, never by wall seconds.

## Re-running the 0x6b receipt {#re-running-the-0x6b-receipt}

```sh
run ex-6b-ercd/check-6b.sh            # 900 s run, asserts the markers
run ex-6b-ercd/check-6b.sh --log NAME # assert an existing log only
```

Expected markers in the log:

```text
6BREAD hdr desc=0x2001fdcc r1=0xd6 ...
CD0 desc=0x2001fdcc st=0x22 cnt=0x1 f44=0xffff0000
```

The model dependency is the I2C address-phase stretch: a receive byte is
clocked only after the guest clears ADDR, and no path may deliver data while
`SR1.ADDR` is set.  Those two laws are guarded by
`tests/qtest/nikon/ex.c::i2c_single_byte_read`.

### The app renders, but no candidate panel surface holds pixels {#the-app-renders-but-no-candidate-panel-surface-holds-pixels}

Coverage with `-dfilter`/`-d exec` over an unthrottled shift-6 window shows the
app draw `0x10569c76` executing 41 times in 300 s while every display gate stays
cold.  RAM dumps of `0xbf309600` (composition), `0xcb4a7000` (display buffer)
and `0xbec5f200` (resource-57 record 0) after 30 s and 90 s are **all zero**, so
the app's repeated draws do not land on a panel surface in this model.

The consequence for this file is the same one the GUI chain records: the
remaining link is not a missing pixel copy inside the model but a missing
request that never reaches the display owner, and that request is downstream of
the GUI worker's guest-kernel wait.

## MAS display frontier: the publication request has no producer {#mas-display-frontier-the-publication-request-has-no-producer}

The MAS side of the first-frame chain is now measured end to end.  The runtime
detail lives in [`../runtime/guichain.md`](../runtime/guichain.md); this section
keeps the acceptance-relevant summary and the evidence boundary.

### What the application actually does (measured) {#what-the-application-actually-does-measured}

| Stage | Address | Executions |
|---|---|---|
| choose config | `0x1067dbda` | 12 |
| get config attrib | `0x1067e414` | 8 |
| create DMPX window surface | `0x106815a4` | 12 |
| make current | `0x10680426` | 19 |
| `eglSwapBuffers` | `0x10680b8c` | **0** |
| `eglCopyBuffers` | `0x10680cfe` | **0** |
| `eglDrawFrameModeDMP` | `0x106810be` | **0** |
| `eglDestroySurface` | `0x1067f2a8` | 0 |
| `eglGetProcAddress` | `0x10680f60` | 0 |

The per-frame surface creation path `0x10569c76 -> 0x10568bde` runs (8 times in
120 s) and `eglMakeCurrent` follows, so the app creates and binds a surface per
frame.  The present wrapper `0x10568f60` - the only caller of `eglSwapBuffers`
in the image - has no `BL`, no pointer literal and no computed reference, so
frames are created and abandoned.  This is the direct answer to "why the app
never calls `eglSwapBuffers`/`eglCopyBuffers`/`eglDrawFrameModeDMP`".

### The transfer stage exists and is configured but never given a job {#the-transfer-stage-exists-and-is-configured-but-never-given-a-job}

The display engine window at `0x20180000` is initialized by the guest
(`0x106EDFD6` once, config `0x106EE04A` 3 times) and its setup latch is written
with exactly the model's documented values (`0x20181214 <- 0x0100ffff`, then
`0xffff`, from `0x106EE3C8`).  No stream descriptor (`0x10d0..0x10ec`) and no
`0x01000000` kick is ever written, so **IRQ129 cannot fire** in this boot and
the "swap -> transfer stream -> IRQ129 -> display buffer" receipt has no
producer.

### The display service arms behind a guest-kernel reply {#the-display-service-arms-behind-a-guest-kernel-reply}

`0x10637B54` is the only caller of provider admission `0x104D7E78`; it is called
from the display-service start `0x1062EDDC`, right after the ready-setter thunk
`0x10637B72` (tail branch to `0x104D7F16`).  A full register trace of the
executed path shows the guard is reached with `r4 == 0` and exits before that
block, so ready, admission, the provider mailbox, the owner and the bank start
are all skipped in this boot.

`r4` is the reply of a microkernel query (`sub_106340D4` -> `sub_10361116`,
`SVC 6`) for the display context object whose handle is `0x010001C2`.  The guest
kernel is ordinary guest code inside the MAS image - VBAR `0x10100000`, SVC
vector `0x10100008` executing 417421 times in 45 s, `SVC 6` dispatching through
`0x10103594` to `0x10107624` - so the `0` reply is this firmware's own state,
not a modelling artefact.

### Withdrawn: "the display mode never leaves 2" {#withdrawn-the-display-mode-never-leaves-2}

This section was a mis-attribution and is withdrawn.  The object whose `+0x1C`
byte is written to `2` belongs to the `CARD0` (SD-card) subsystem, resolved from
the named tables: table A at `0x118AED0C` is keyed by `eSyscCommandId`
(`CardInsert 20`, `CardRemove 21`, `CardMount*` 22..25, `CardFormat*` 26..29,
`StartTask1st 4`, ...), and table B at `0x118AEC40` maps subsystems to
descriptors whose names are `CARD0`, `ML_LENS`, `F_LENS`, `EXTSB`, `NwBoard`.
`0x1042CB82` is the card handler, not a display one.

The display side keeps the frontier documented above (owner `0x104DC804`,
admission `0x104D7E78`, banks start `0x1046BF0C`, kernel gives
`0x010001C2`/`0x010001BE`); the display-mode statements below are retracted.

### Historical (withdrawn) display-mode section {#historical-withdrawn-display-mode-section}

The display state machine `0x1042D086` advances from state 1 to state 2 - the
only state that posts the attach event - only when `mode(obj+28) == 1`.  Both
display objects (`0xB209862C`, `0xB185DE6C`) enter with `new_state = 1` and mode
`2`, and the two mode writers measured in the window are:

| Setter | Behaviour | Runs |
|---|---|---|
| `0x1042D72E` | `0x1042D1DA(obj, 2)` hardcoded | 2 |
| `0x1042CCA0(obj, a2, a3)` | `a2 == 0 ? 2 : 1` | **0** |

So the firmware writes only the idle mode, and the selector that could write
mode 1 is never called.  The display request handler `0x1042E7FA` is likewise
called once with `a3 = 1` (disable) and `a2 = 0`, which its `if (a2)` makes a
no-op; request type 2 (attach) is never issued.

### The missing dependency is two specific kernel gives (measured) {#the-missing-dependency-is-two-specific-kernel-gives-measured}

The RTOS kernel call table resolves and is active: `create 0x10110D34`,
`give 0x101110EC`, `clear 0x1011131C`, `wait 0x10111410`, with 173965 gives and
33276 waits in 45 s.  Capturing the give calls themselves (87293 calls, 83
distinct objects, 25 s) shows which objects this boot signals, and the two the
chain needs are absent:

| Object | Role | Gives |
|---|---|---|
| `0x010001BE` | GUI worker wait object | **0** |
| `0x010001C2` | display-service gate object | **0** |
| `0x010001C1` / `0x010001C3` / `0x010001C4` | neighbouring display objects | 3 / 1 / 68 |

A wait on a never-given object returns no flags, and no flags is exactly the
`r4 == 0` that keeps the display-service arming block skipped.  So the frontier
is a pair of missing kernel gives inside the unchanged firmware, not a stalled
kernel, a dead device or missing bundle data.

### The chain in order, with each link's measured state {#the-chain-in-order-with-each-link-s-measured-state}

| # | Link | State | Evidence |
|---|---|---|---|
| 1 | AppC message class delivered to `0x1020CA7A` | only `0x03000007` observed | `-d cpu` at the TBB, `r0 = 7` |
| 2 | class that reaches the state test (`0x1020CD0A`) | never selected | coverage 0 for `0x1020CCA0`, `0x1020CCC2`, `0x1020CCDC`, `0x1020CD0A` |
| 3 | code translator `0x1020E98A` (`0x04100001 -> 0x03000004`) | never runs | coverage 0; only ref is a `B.W` thunk at `0x10204D4C` |
| 4 | event-0x39 poster `0x10695450` | reachable, never posted | veneer `0x103608AA -> 0x1048DB00 -> 0x10695450`, 10 callers, none reaching it |
| 5 | GUI event dispatcher / live-view gate | never runs | coverage 0 |
| 6 | display arming request | only start event `0x04090017` -> `0x04030034`; needs `0x0403001A` | two-level broadcast capture + `cmp r5,#0x0403001A` guard |
| 7 | device-subsystem dispatcher `0x1042CB82` (`CARD0`) | runs, gets `op = 4` | 3 entries; `20..29` switch takes default; not a display handler |
| 8 | display event `(0|1, 0)` (arming) | never posted | only `(2,5) (5,5) (6,5)` observed |
| 9 | kernel give of `0x010001C2` / `0x010001BE` | never happens | 87293 give calls, 83 objects, both absent |
| 10 | ready `0x104D7F16` / admission `0x104D7E78` | never runs | coverage 0 (gated on the never-given object) |
| 11 | EGL present `0x10568F60` | unreachable | no `BL`/`B`/literal reference under both scanners |
| 12 | transfer job / IRQ129 | never issued | no `0xC00..` register writes, no stream kick |

Every link is measured cold, and the gates are all *upstream* of anything a
device model could supply: message classes the boot does not deliver, a service
request (`0x0403001A`) that nothing issues, and two kernel objects nothing
signals.

### Where this meets the EX host lane {#where-this-meets-the-ex-host-lane}

The display arming chain now ends at a *service request* rather than at a device:
F091-class request -> handler `0x10502D0A` -> IPC `0x040F0020` -> display service
-> request `0x0403001A` -> ready `0x104D7F16` + admission `0x104D7E78`.  The
handler never runs, and the display service's only broadcast in a 60 s window is
its start event `0x04090017` (delivered as `0x04030034`), never `0x040F0020`.

That is the same shape as the EX side of this file: the EX host-command table at
`0x10949704` carries neither 113 nor 131, and the EX lanes observed here carry
only `0x65`/`0x66`.  Both halves therefore wait on an inbound service request
that this boot does not deliver, which is the next measurement named in
[`../runtime/guichain.md`](../runtime/guichain.md) - find the producer of the
F091-class request rather than any further device behaviour.

### The host-facing service bus is idle in both directions {#the-host-facing-service-bus-is-idle-in-both-directions}

The class-2 envelope path (`class table 0x119E1E84.. -> router 0x104C6B14 ->
handler table 0x119E4820`) is dormant in this boot:

| Address | Role | Executions |
|---|---|---|
| `0x104C0F20` | request send API (51 static call sites) | **0** / 45 s |
| `0x104C6B14` | request router | **0** / 120 s |
| `0x10502D0A` | F091 handler (display arming sender) | 0 |
| `0x10502DB8` / `0x10502E12` | F092 / F093 handlers | 0 / 0 |

Local display events do flow, but through in-process callback tables
(`0x1052E3AE` with 4 subscribers, `0x107392D6` with 16), not through this bus.
So MAS sends and receives no host-facing service request at all, and the display
arming request `0x040F0020` (routed to display code `0x0403001A`) is never
issued by anything.

### Observability added: a panel scanout model (step 1 of the assisted path) {#observability-added-a-panel-scanout-model-step-1-of-the-assisted-path}

The model now includes `nikon-panel-functional` (`hw/nikon/devices/panel.c`),
which samples the guest-owned composition region `0xbf309600` (1024x688
RGBA8888, six inactive top rows) on a virtual-clock cadence and publishes a QEMU
console surface.  It reads guest RAM only - no register, IRQ, completion or
status is added - so it changes nothing the guest can observe.

Receipts: `tests/unit/nikon/panel.c` (2 TAP tests, the conversion is a pure byte
reorder) and a live `-qmp` + `screendump` run that yields a 1024x682 P6 PPM.

The first scanned frame is entirely black, which is the same negative every RAM
dump showed: the unchanged firmware writes no pixel into the composition buffer
in this boot.  The observer is therefore a prerequisite for the assisted path in
[`../runtime/guichain.md`](../runtime/guichain.md), not evidence of a frame yet.

### Assisted-path status (measured) {#assisted-path-status-measured}

- Panel scanout observer: added, unit-tested, and a QMP `screendump` yields a
  1024x682 PPM.
- Camera image input profile: documented in
  [`../inputs/camera/image.md`](../inputs/camera/image.md); with an image
  configured the readout-pipeline counters stay 0 because the guest never issues
  the readout request.
- EX key assistance (F1..F5, 1..3 in four rounds through QMP): the app keeps
  drawing (27 times), but present, GUI dispatcher, owner, banks start, mailbox
  post, frame queue, service router, F091 handler and the SLA readout ISR all
  stay at 0, and the scanned frame stays black.

So the observer and the image input are in place, and the remaining assistance
must be a host command injected on the MAS host-command lane, which requires the
frame protocol that has not been recovered here.

### Why the natural (unassisted) path produces no frame: the two-sided answer {#why-the-natural-unassisted-path-produces-no-frame-the-two-sided-answer}

Both sides are now measured with their own receipts, and they meet in the middle:

**MAS side.** The application performs EGL setup and abandons each frame's
surface (create 8, present 0, release 0).  The only present wrapper
(`0x10568F60`) has no `BL`, `B`/`B.W` or pointer reference.  The frame-queue
submit `0x105A6906` never runs, the transfer engine is configured but never
given a job, and the display owner, provider admission and banks start are all
zero-hit.  Arming needs display request `0x0403001A`, which needs IPC
`0x040F0020` from the F091 handler `0x10502D0A`, and the host-facing service bus
is idle in both directions (send `0x104C0F20` 0 in 45 s, router `0x104C6B14` 0
in 120 s).

**EX side.** The EX receives external input: the seventeen host keys drive its
GPIO pads and `hw/nikon/devices/input.c` logs every transition
(`nikon-input: pad=0 ex-pad=0x0a down active-low level=0`, ...).  It still emits
only host commands `0x65`/`0x66`, and its host-command table `0x10949704`
carries neither 113 nor 131.

**The meeting point.** MAS's display arming is downstream of an inbound service
request that only a host command from the EX (or an internal MAS module) would
produce, and neither producer exists in this boot.  The EX is not stuck on a
missing device input either - it is driven - it simply does not convert that
input into the commands MAS waits for.

So the natural-path answer is not "the model lacks a device": the model now
observes the panel, feeds an image, and delivers host keys.  It is that the two
firmware domains in this boot never exchange the request that would start the
display.

### The SLA readout cohort stops after init as well (measured, 60 s) {#the-sla-readout-cohort-stops-after-init-as-well-measured-60-s}

The same window over the SLA result-readout cohort shows only the initialisation
running:

| Address | Role | Executions |
|---|---|---|
| `0x1009AD52` | readout initialization | 1 |
| `0x10093CEC` | mode2 dispatcher | 1 |
| `0x1009AD7E` | paired request submission | **0** |
| `0x1009AE04` | paired request stop | 0 |
| `0x1009AE6C` | full configuration writes | 0 |
| `0x1009AF8C` | three fullword setters | 0 |
| `0x1009ACA0` | readout ISR | 0 |

So the SLA initialises the interface and never submits a request either.  That
closes the last candidate for a model-side fix in the image path: the SLA
readout-control `readout-request` line is unconnected in this machine, but
connecting it would change nothing because the guest never raises it.

### The graphics window is the SMAPH-F resource-manager block (identified) {#the-graphics-window-is-the-smaph-f-resource-manager-block-identified}

The MAS image carries the DMP/OpenVG driver (`VG_DMP_*`, `VG_DMPX_*`, `SMAPH-F`,
`rm_gpuif_wait_interrupt`, `int_cause & RM_HWIF_INT_GPU`, `BGraphicService`).
Its register accessors fix the aperture:

```text
sub_106EE430(a1)  return *(u32 *)(a1 + 0x20180000)      // read
sub_106EE3C4(a1, v)  *(u32 *)(a1 + 0x20180000) = v      // write
sub_106EE1F8(cause, ctx)  rm_gpuif_wait_interrupt       // 0x58 cause word
```

so the "transfer/display engine window" this model already owns at `0x20180000`
is the GPU's register file, not a separate DMA block. The command word format
the model already fingerprints for the LUT draw

```text
0x01408004  op = 4, reg = 0x0140, mask = bit15, count = 1
0x01a3f004  op = 4, reg = 0x01a3, mask = 0xf,   count = 1, r[0x1a3] = w|h<<16
0x00a6f004  op = 4, reg = 0x00a6, mask = 0xf,   count = 1
```

is exactly the packed command format a published Z7 model documents for the
same IP (`op = header & 7`, `count = ((header >> 3) & 255) + 1`, per-byte write
mask in bits 12..15, base register index in bits 16..23, two 512-word register
banks). Two independent derivations agree, so the model's existing LUT profile
is a narrow slice of one command family, and the *fill/stroke/textured* families
are unimplemented rather than absent from the hardware.

### Still no pixels after 120 s (measured) {#still-no-pixels-after-120-s-measured}

`pmemsave` of the kernel, graphics and pixels RAM regions at 120 s unthrottled
shift 6 (74,448,896 + 14,680,064 + 17,825,792 bytes), scanned for image-like
pages (a page counts when more than 200 of its 1024 words are non-zero):

| Region | Pages | Largest structured cluster |
|---|---|---|
| `0xb1800000` kernel | 536 | `0xb18a2000`, `0xb2b75000`, `0xb21dd000` - 1024 distinct words per page, i.e. random-state buffers, not pixels |
| `0xbb300000` graphics | 4 | `0xbc010000..0xbc013000` - object records (float `1.0`, small ints, pointers) |
| `0xbc100000` pixels | 1 | `0xbd160000` - 315 distinct words, too sparse for a surface |

Nothing in the dump has the row-structured colour field a rendered UI would
leave. The measurement matches the mechanism: no GPU command stream is
submitted, so no renderer - modelled or missing - has anything to execute.

### Renderer hypothesis: no observed command producer {#renderer-hypothesis-no-observed-command-producer}

A runtime sweep points the panel observer at every surface the firmware could be
drawing into, through QMP `qom-set` on `/machine/panel` (base, width, height,
skip-rows), then takes a `screendump` of each.  At 20 s all of them are black:

| Surface | Geometry | Pixels |
|---|---|---|
| `0xbf309600` composition | 720x480 and 1024x688 | all black |
| `0xcb4a7000` display buffer 0/1 | 1280x720 | all black |
| `0xbec5f200` resource-57 record 0 | 1024x688 | all black |
| `0xbf10f200` record 1 | 960x540 | all black |
| `0xb81849c0` first app draw target | 512x512, 480x720, 720x480 | all black |

and the app draw target's neighbours are not empty either: the frame-slot table
`0xB1CE1BCC` has eight live entries with ~2 KiB descriptors, so what the app
allocates is bookkeeping, not a rendered image.

The render blocks are not being driven: GDB watchpoints on the IPUD window
`0x24020000` (`+0x04`, `+0x20`) and the processing window `0x24070000` (`+0x04`)
record **no writes** in 45 s.

The reference project's own scope list is the confirmation: it implements
"selected GPU paths, strokes, bitmap sampling, alpha and LCD composition" and
leaves "complete GPU/OpenVG implementation" outside its verified scope.  This
model has the transport, transfer, fill, copy, lanes and banks blocks, but no
renderer that turns the firmware's UI draw calls into pixels.

A broader functional render engine would still need actual guest commands.
These black surfaces and idle windows do not establish that implementing one
alone would unblock the boot. The upstream startup/command producer remains
the investigation target; the panel observer can expose any resulting pixels.

### Evidence boundary {#evidence-boundary}

- Not observed in these windows: EGL present, frame queueing, a transfer job,
  IRQ129, the display owner, provider admission and the bank start.
- The recorded missing guest-kernel reply (handle `0x010001C2`) and cold present
  path have not been traced to a missing device contract that would unlock them.
- Fabricating either (injecting a swap, a frame slot, ready/admission, a stream
  kick or a kernel reply) would be a synthetic stimulus, not an
  unchanged-firmware receipt, so it is excluded here.

## Conclusion: no frame producer observed in the recorded windows {#conclusion-no-frame-producer-observed-in-the-recorded-windows}

The recorded first-frame chain is cold upstream of display admission. The
current task is to trace the independent firmware startup conditions described
in the PTP note, rather than infer from an idle PTP handler that no producer
can exist.

### The answer to the original question {#the-answer-to-the-original-question}

In these windows the application does not call `eglSwapBuffers`, `eglCopyBuffers`
or `eglDrawFrameModeDMP`. Static scanning found that the only present wrapper
(`0x10568F60`, whose body is the sole call site of `eglSwapBuffers`
`0x10680B8C`) has no reference of any kind - no `BL`, no `B`/`B.W`, no pointer literal, in the ROM,
the RAM window or the inner image.  The app performs EGL setup
(`eglChooseConfig` 12, `eglGetConfigAttrib` 8, `eglCreateWindowSurfacePointerDMPX`
12, `eglMakeCurrent` 19) and then abandons each frame's surface: the frame-slot
table at `0xB1CE1BCC` records create (8) but **present 0** and **release 0**.

### Why no alternative path publishes {#why-no-alternative-path-publishes}

| Alternative | State |
|---|---|
| frame queue submit `0x105A6906` | 0 executions |
| transfer descriptor / stream / kick | never written |
| IRQ129 | unreachable without a job |
| display owner `0x104DC804` | 0 executions |
| provider admission `0x104D7E78` | 0 executions |
| banks start `0x1046BF0C` | 0 executions |
| kernel give `0x010001C2`, `0x010001BE` | absent from 87293 give calls |

### The PTP arming route is idle in these windows {#the-ptp-arming-route-is-idle-in-these-windows}

The PTP route sends display-service request `0x0403001A` through outer event
`0x040F0020`, broadcast by `0x10502CBC` from F091 handler `0x10502D0A`. No F091
request was observed in those windows: `0x104C0F20` local broadcast and
`0x104C6B14` PTP router both have zero entries. The previous
claim that no bundle component contains `0xF091` was incorrect: the MAS handler
table contains it at `0x119e483c`. Finding no immediate-code producer does not
exclude a table-driven or computed request. The upstream dispatch is now traced
to PTPCMD, whose earlier event wait remains unsatisfied. The separate
`1062f120` readiness call means this PTP route is not the only software path.

### What this means for the objective {#what-this-means-for-the-objective}

No original-firmware GUI frame is established. Continue tracing the actual
startup producer and the conditions that gate it. A missing request is an
observation to explain, not evidence that the model is complete or that only a
different image could progress. Any future device change needs a finite guest
contract and native validation; directly injecting a guest request, event or
present call would remain a separate hypothesis experiment.
