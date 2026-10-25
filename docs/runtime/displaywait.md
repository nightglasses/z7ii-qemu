# Display workers, kernel waits and request dispatch

This article preserves bounded reverse-engineering observations and their corrections. It is supporting analysis, not the current boot-status page. Use the [GUI chain guide](guichain.md) for the current interpretation and [startup boundary](../validation/frontier.md) for accepted progress. Zero-hit results from historical multi-breakpoint probes are not absence evidence.

## The GUI worker task exists in RAM but is not runnable {#the-gui-worker-task-exists-in-ram-but-is-not-runnable}

### The runtime object (direct observation, RAM dump at the first app draw) {#the-runtime-object-direct-observation-ram-dump-at-the-first-app-draw}

A GDB `dump memory` of `0xB1800000..0xB1830000` at the first app draw
(`0x10569C76`) contains the runtime GUI object:

```text
0xB181D970  mode block (all zero; +0x10 = 0x006B006A from the ROM template)
0xB181D9B8  {0x010000ED, 0x103FAB3B}   event id + worker main
0xB181D9C0  0x1C 0x2000 1             priority / stack / state
0xB181D9D8  {0x010001BE, 0x10204DA9}   second event id + handler
0xB181D9E4  3
0xB181D9E8  method table copied from ROM 0x123B797C:
            0x1053CFA9 0x1053CFB9 0x1053D2FB 0x1053D357 0x1053D367
            0x1053D50F .. 0x1053D72F, 4, 0x105408FB, 0x105424E9,
            0x105425C5 0x105425C5 0x105425F7
```

The ROM table at `0x123B797C` is therefore copied into RAM at `0xB181D9E8`
(entry 17 is the step dispatcher `0x105424E9`).  The same dump contains five
copies of the worker main `0x103FAB3B` at `0xB181D8A8`, `0xB181D9BC`,
`0xB1824EA4`, `0xB1828068` and `0xB182810C`, each followed by
`0x1C`/`0x2000`/`1` and a `0x10204DA9` handler registration.

### The worker main (verified code) {#the-worker-main-verified-code}

`0x103FAB3A` is a noreturn task body:

```text
call  [ctx+48]                       (init method, may be null)
sub_103FAB24(ctx, 1, [ctx+44])       (calls the [ctx+36] hook)
loop:
    sub_103610DA(ctx+32, 1, &v)      -> sub_10104A14(*(u32*)(ctx+32), 1, 1, &v)
    sub_103610D2(ctx+32, 1)          -> sub_101043A4(*(u32*)(ctx+32), ~1)
    call [ctx+52]  (per-iteration step)
    sub_103FAB24(ctx, 2, [ctx+44])
    call [ctx+56]
    call [ctx+64]
```

So `*(u32*)(ctx+32)` is the RTOS wait handle and `[ctx+36]` the hook.  For the
GUI object at `0xB181D9B8`:

```text
ctx+0x00 = 0x010000ED  event id
ctx+0x04 = 0x103FAB3B  worker main
ctx+0x08 = 0x1C        priority
ctx+0x0C = 0x2000      stack size
ctx+0x18 = 0x010001BE  registration id
ctx+0x1C = 0x10204DA9  hook
ctx+0x20 = 0x010001BE  wait handle          <-- the worker blocks here
ctx+0x24 = 3
ctx+0x30 = 0x1053CFA9  (= method table entry 0, then entries 1..20)
```

The worker therefore waits on RTOS event handle `0x010001BE` (via
`0x10104A14`) and calls GUI method-table entries as its init/step bodies.

### CPU state at the same moment (direct observation) {#cpu-state-at-the-same-moment-direct-observation}

Five vCPU threads are exposed by the QEMU gdbstub:

| Thread | CPU | PC at first draw | Meaning |
|---|---|---|---|
| 1.1 | MAS core 0 | `0x10569C76` | app UI draw (running) |
| 1.2 | MAS core 1 | `0x10107078` | RTOS scheduler idle loop `sub_10107060` (current-task slot `0xB2B41604[cpu] == 0`) |
| 1.3/1.4 | SLA cores | `0x10038A4C` / `0x10038A48` | SLA idle |
| 1.5 | EX | `0x0803DD96` | EX code |

### Reading {#reading}

The GUI worker context is fully populated (worker main, callbacks, stack size),
but MAS core 1 has no runnable task: the GUI worker is either blocked on its
`a1+32` event or not yet created.  Nothing observed so far signals that event.
The next measurement is the worker's task context and its wait object, then the
signaler; that signal is the last software link before the mode request.

This also explains why every earlier probe in the chain was zero-hit without
any model diagnostic: the code is idle, not failing.

### The worker blocks on a kernel object, not on a model register {#the-worker-blocks-on-a-kernel-object-not-on-a-model-register}

The wait is a microkernel object, not a device poll.  The worker's context field
`ctx+0x20` is filled by `0x10361082`:

```text
0x10361082: v = {0, 8, 0, 0}; *a1 = sub_10104414(v)   // SVC 6, create
0x103610DA: sub_10104A14(*(u32*)ctx_32, 1, 1, &v)     // wait
0x103610D2: sub_101043A4(*(u32*)ctx_32, ~1)           // clear
0x103610CC: sub_10104844(*(u32*)ctx_32)               // the give a task would use
```

`sub_10104414`, `sub_10104A14`, `sub_101043A4` and `sub_10104844` are one-SVC
thunks (`SVC 6` at `0x10104424` and `0x10104A24`), so the wait object belongs to
the guest kernel; the model sees only the handle.  A watchpoint on `ctx+0x20`
(`0xB181D9D8`) catches the create storing `0x010001BE` from PC `0x1036109e` and
nothing else, so the value is a fresh kernel handle, not a device status word.

`0x103FAB24` is the hook relay (`v = *(u32*)(ctx+36); return v ? v(phase, arg) : 0`)
and `ctx+36` holds `0x10204DA9`; `ctx+44` holds `3`, which the relay passes as the
argument at every phase.  The hook is a notification path, not a wait.

### The frame-slot table shows create/make-current without present (measured) {#the-frame-slot-table-shows-create-make-current-without-present-measured}

`0x10569C76` does not draw pixels: it builds a descriptor for the caller's buffer
and hands it to `0x10568BDE`, which creates an EGL window surface for that buffer
(`sub_106B260C([0xB1800A08], width, height, format, attrs, ...)`).  Surviving
frames live in a 0x50-entry table of 24-byte slots at **`0xB1CE1BCC`**:

```text
slot[i] +0  valid/kind      0x10568BDE sets it after a successful create
        +4  buffer pointer
        +8  width<<16|height and format bytes
        +12 EGL display        0xB1800A0C
        +16 EGL surface        the value eglSwapBuffers takes
        +20 owner flag         0xB1800A00 / 0xB18009FC bookkeeping
0x10569018(i)  slot i valid and i < 0x50
0x10568F60(i)  PRESENT: 0x10680B8C([0xB1800A0C], slot[i]+16)   == eglSwapBuffers
0x10568EA4(i)  RELEASE: 0x1068039C(display, slot[i]+12),
               0x1067F2A8(display, slot[i]+16), 0x106B254A(...), clear slot
```

Coverage over 120 s unthrottled shift 6 (`run-df.sh probeFRAME`) answers "why is
`eglSwapBuffers` never called":

| Function | Role | Executions |
|---|---|---|
| `0x10568BDE` | create window surface for a frame buffer | 8 |
| `0x10568F1C` | `eglMakeCurrent` wrapper | 21 |
| `0x10569018` | slot validity helper | 25 |
| `0x105693E2` | neighbouring EGL helper | 1 |
| **`0x10568F60`** | **present slot -> `eglSwapBuffers`** | **0** |
| `0x10568EA4` | release slot | 0 |

So the app creates surfaces and makes them current, but never presents and never
releases: slots are allocated and left behind.  The present wrapper is the only
caller of `eglSwapBuffers` in the image (`0x10568F7C`), and it has no `BL`, no
pointer literal and no computed reference (2-byte-aligned Thumb scan plus dword
scan of the whole component, including the inner image at `0xB1000000`).
`eglDrawFrameModeDMP` and `eglCopyBuffers` have no caller either.

### The display-service start is entered but its admission loop never runs {#the-display-service-start-is-entered-but-its-admission-loop-never-runs}

Provider admission `0x104D7E78` is called from exactly one place inside the
display service: the poller `0x10637B54`, which walks a request-code table

```text
0x10637b54  push {r4, lr}
0x10637b56  ldr  r4, =0xB181D380
0x10637b58  ldr  r0, =unk_1094828C
0x10637b5a  ldr  r1, [r4, #0x58]          cursor in the request-code table
0x10637b5c  ldrb r0, [r0, r1]
0x10637b5e  bl   0x104D7E78               provider admission(code)
0x10637b62  ldr  r0, [r4, #0x58]; add #1; str r0, [r4, #0x58]
0x10637b68  cmp  r0, #0xF; bcc 0x10637B70; movs r0,#0; str r0,[r4,#0x58]
0x10637b70  b    0x10637454
```

The poller is called from `sub_1062EDDC`, the display-service start, right after
the ready setter thunk `0x10637B72` (which tail-branches to `0x104D7F16`):

```text
0x1062f102  cmp  r4, #0; bge 0x1062F1A0      <-- argument gate
0x1062f106  bl   0x1062E9B4
0x1062f10e  bl   0x104BF8F8 (r0=3); cbz r0, 0x1062F12E
0x1062f116  bl   0x106957B4; bl 0x10637B48
0x1062f120  bl   0x10637B72                  <-- ready
0x1062f124  bl   0x10637B54                  <-- admission poller
0x1062f130  bl   0x104BF8F8 (r0=4); cbz r0, 0x1062F140
0x1062f136  bl   0x10637B54                  <-- admission poller again
0x1062f142  bl   0x104BF8F8 (r0=0); cbz r0, 0x1062F16E
```

Measured, 90 s unthrottled shift 6 (`probeDMSVC3`):

| Address | Role | Executions |
|---|---|---|
| `0x1062EDDC` | display-service start | 1 |
| `0x10637B54` | admission poller | **0** |
| `0x104D7E78` | admission | 0 |
| `0x104D7F16` | ready setter | 0 |
| `0x1053D2FA` | GUI method 2 (calls the start) | 0 |
| `0x104DC804` | display owner | 0 |
| `0x1046BF0C` | banks start | 0 |

So the display service is *entered* once and then leaves before ready/admission:
the argument gate `r4 >= 0` or one of the `0x104BF8F8` probes short-circuits it,
and none of the seven `BL` callers of `0x1062EDDC` executes at all
(`0x10437A74`, `0x1043DE7C`, `0x1044E58A`, `0x1053D300`, `0x10656952`,
`0x1070725A`, `0x1073EFEC`, all zero).  The single entry is therefore a
fall-through or a branch from the preceding block, not a call; that discrepancy
is recorded rather than explained, and it is the next measurement.

Whichever branch it is, the consequence is the same: ready, admission, the
provider mailbox, the owner and the bank start are all downstream of a gate that
this boot never opens.  This is the concrete display-side equivalent of the
EX template-zero gates in [`../validation/frontier.md`](../validation/frontier.md).

### RETRACTED: "the display state machine stops at state 1 because the mode is 2" {#retracted-the-display-state-machine-stops-at-state-1-because-the-mode-is-2}

> **Retracted.** This object belongs to the `CARD0` (SD-card) subsystem, not to
> the display; see "Correction: the 0x1042Cxxx/0x1042Dxxx cluster is the
> device-subsystem dispatcher" below.  The measurements are accurate, the
> attribution is not.  The section is kept only as a record of that object's
> behaviour.

Original (mis-attributed) section title: "The display state machine stops at
state 1 because the mode is 2 (measured)".

The display state machine is `0x1042D086(obj, new_state)`.  It stores the new
state at `obj+29`, notifies `[obj+20]` and `[obj+16]` with
`([obj+56], old_state, new_state)`, and then advances by itself:

```text
case 1:  obj+32 = 0
         if (*(u8*)(obj+28) == 1) { new_state = 2; continue; }   <-- arming advance
         else if (obj+40) recurse(obj, 6)
case 2:  mode == 1 -> 0x1042D05C(obj, ...)      mode == 2 -> LABEL_14
case 3:  obj+48 == 0 -> new_state = 4
case 4/5: obj+32 != 0 -> new_state = 6
```

`obj+28` is the **display mode**.  A watchpoint run (`probeMODE3`) catches both
mode writes and both object builds:

```text
MODE1-W#1  pc=0x1042D1DC  val=0x2  lr=0x1042D2BA    object 0xB209862C mode := 2
OBJ2-BUILD#1 pc=0x1042D1F4        lr=0x1042E9BC    object 0xB185DE6C built
MODE2-W#1  pc=0x1042D1DC  val=0x2  lr=0x1020F8DC    object 0xB185DE6C mode := 2
```

and a `-d cpu` capture shows both objects entering the machine with
`new_state = 1`:

```text
0x1042D086  a1=0xB209862C  a2=1  lr=0x1042D2BA
0x1042D086  a1=0xB185DE6C  a2=1  lr=0x1020F8DC
```

Both therefore take `case 1`, find `mode == 2`, and stop.  State 2 - the only
state that posts the attach event - is never reached.

Mode can be written two ways:

| Setter | Behaviour | Executions (45 s) |
|---|---|---|
| `0x1042D72E(obj)` | `0x1042D1DA(72*idx + base, 2)` - hardcoded 2 | 2 |
| `0x1042CCA0(obj, a2, a3)` | `if (a2 == 0) mode = 2 else mode = 1` | **0** |

So the only mode that ever gets written is the teardown/idle mode 2, and the
selector that would pick mode 1 (`a2 != 0`) is never called.

The request handler `0x1042E7FA` agrees: its single invocation is

```text
a1=0xB185DE64  a2=0  a3=1  a4=0x1042E7FB  lr=0x1042D0A8
```

`a3 == 1` is the "disable" request and `a2 == 0` makes its `if (a2)` branch
false, so the handler is a no-op.  Request type `2` - the one whose
`case 2` posts event 0 and arms the display - is never issued.

### Panel observer: the composition buffer is now scannable (model addition) {#panel-observer-the-composition-buffer-is-now-scannable-model-addition}

The model had no way to observe the composition buffer, so every earlier
statement about pixels was a RAM dump.  `hw/nikon/devices/panel.c`
(`TYPE_NIKON_PANEL`, `nikon-panel-functional`) now samples the guest-owned
composition region on a virtual-clock cadence and publishes it to a QEMU
console:

```text
properties   base (0xbf309600), width 1024, height 688, skip-rows 6,
             interval-ns 33333333
source       guest RAM only, mapped read-only through address_space_map
format       the transfer device's documented RGBA8888 program
             (1024x688 with six inactive top rows)
publish      qemu_graphic_console_create + qemu_console_resize +
             composite into qemu_console_surface()->image +
             qemu_console_update_full, on sampled-content change only
```

It adds **no** guest register, IRQ, completion or status, and never writes guest
memory: the guest cannot observe the observer.  The machine instantiates it next
to the `z7ii.lcd-composition` region.

Receipts:

- `tests/unit/nikon/panel.c` — the RGBA8888 -> `0xAARRGGBB` conversion is a pure
  byte reorder (alpha copied, no premultiplication): 2 tests, TAP `ok`.
- A live run with `-qmp` and QMP `screendump` produces a `1024x682` P6 PPM from
  the console (measured 20 s and 60 s after boot), and its content is all black:
  the guest-owned composition buffer is still zero, which matches every earlier
  RAM dump.

So the observer works and immediately confirms the negative: the firmware never
writes a pixel into the composition buffer in this boot.

### Key assistance does not drive the chain (measured) {#key-assistance-does-not-drive-the-chain-measured}

The model already maps seventeen host keys onto the EX GPIO pads
(`hw/nikon/devices/input.c`).  A 60 s assisted run (`run ex-6b-ercd/
run-assist.sh`) pressed `F1..F5` and `1..3` in four rounds at 8/16/24/32 s
through QMP `input-send-event`, with a `-dfilter` over the display frontier and
a panel screendump at the end:

| Address | Role | Executions |
|---|---|---|
| `0x10569C76` | app draw | 27 |
| `0x10568F60` | EGL present wrapper | **0** |
| `0x1053E19A` | GUI event dispatcher | **0** |
| `0x104DC804` | display owner | **0** |
| `0x1046BF0C` | banks start | **0** |
| `0x104BD99E` | mailbox post | **0** |
| `0x105A6906` | frame queue submit | **0** |
| `0x104C6B14` | service request router | **0** |
| `0x10502D0A` | F091 handler | **0** |
| `0x1009ACA0` | SLA readout ISR | **0** |

and the panel frame stays entirely black.

The input path itself is verified: `hw/nikon/devices/input.c` now logs every key
that reaches a pad, and the same run records

```text
nikon-input: pad=0  ex-pad=0x0a down active-low level=0
nikon-input: pad=0  ex-pad=0x0a up   active-low level=1
nikon-input: pad=1  ex-pad=0x11 down active-low level=0
nikon-input: pad=12 ex-pad=0x6e down active-low level=0
```

so the host keys do drive the seventeen EX pads.  The negative is on the EX
side: it receives the pad levels but does not turn them into a host command that
MAS would act on.  The EX key profile is therefore a working assistance channel
whose consumer is inactive, not a broken channel.  The remaining
assistance candidate is a host command injected on the MAS host-command lane
itself, which needs the frame protocol that this investigation has not yet
recovered.

### The only display messages posted are channel teardowns (measured) {#the-only-display-messages-posted-are-channel-teardowns-measured}

The event handler wrapper `0x10299A80` has no `BL` caller and no pointer
literal; a `-d cpu` capture shows all three of its calls come from the single
site `0x1042C5D6` inside `sub_1042C550`:

```text
0x1042c5d0  ldrb r0, [r5, r4]     ; channel from a 6-entry table
0x1042c5d2  mov  r2, r7           ; payload pointer
0x1042c5d4  mov  r1, r6           ; event code
0x1042c5d6  bl   0x10247FB4       ; post (wrapper -> dispatcher -> 0x104BF4AE)
0x1042c5da  adds r4, #1
0x1042c5dc  cmp  r4, #6           ; loop over six channels
```

and `r6` (the event) is compared with `5` just before: when it *is* 5, the code
first runs a per-channel disable helper chosen by channel class
(`0/1 -> 0x1042C922`, `2/3 -> 0x1042E630`, `4-6 -> 0x1042D70C`), and may then
skip the post when that helper returns 1.  So **event 5 is the teardown path**.

All three captured messages are teardowns:

```text
(2, 5, 0xb185de5c)   (5, 5, 0)   (6, 5, 0)
```

The boot therefore posts only "disable channel" notifications; the attach event
`(0 or 1, 0)` that arms the display service is never posted anywhere in the
window.

The post API `0x10247FB4(channel, event, ptr)` has exactly four call sites in
the image, and coverage over 40 s shows only the teardown one runs:

| Poster | Function | Runs |
|---|---|---|
| teardown loop (event 5) | `0x1042C550` | 1 |
| `0x1042D028` | forwards `(arg3, arg2, arg1+17)` | 0 |
| `0x1042D4B6` | forwards `(arg4, arg1, 0)` after `0x1042D794` | 0 |
| `0x1042E744` | forwards `(kind, arg1, ptr)` after `0x1042EA50` | 0 |

So the display message producers that could attach or arm a channel exist and
are simply never invoked; the only producer that runs is the teardown loop.

### The display event channel never delivers the arming event (measured) {#the-display-event-channel-never-delivers-the-arming-event-measured}

The function that gives the missing context-2 object is the display event
handler `0x104BF4AE(channel, event, ptr)`.  It stores `event` in a per-channel
table and switches:

```text
case 0/1:                               channel 0 and 1 are the arming channels
    switch (event)
      case 0:                           <-- the arming event
          if (0x10631F9E(...)) {
              0x10634088(0x10634050(), 0x20000);   give context 2
              0x10634088(0x10634030(), 0x200000);  give context 1
          }
          ...
      case 2/5:  0x10634088(0x1063406C(), 256);     give context 3
      case 3:    ... give context 1 with 8
case 2:   switch (event) ... give context 1 with 4 / give context 3
```

A `-d cpu` capture of the handler over 45 s unthrottled gives **three** entries
in total:

```text
channel(r0)=2  event(r1)=5  ptr=b185de5c   lr=0x10299AA4   (dispatching)
channel(r0)=5  event(r1)=5  ptr=0
channel(r0)=6  event(r1)=5  ptr=0
```

All three carry event `5`; the only event ever delivered on channel 2 takes the
`give context 3` branch, and channels 5/6 are outside the switch.  **Channel 0
or 1 with event 0 is never delivered**, so the arming path - and therefore the
context-2 give - cannot run.  That matches the give histogram exactly: context 3
is given once, contexts 1 and 2 not at all through this handler.

The caller is a message dispatcher (`0x10299A80 -> 0x10360882`), so the display
arming waits on an *event message* that this boot never produces.

### The RTOS kernel calls resolve and the two blocking objects are never given {#the-rtos-kernel-calls-resolve-and-the-two-blocking-objects-are-never-given}

The `r12` descriptor each stub loads is also the value the kernel indexes with.
In `0x10107624` the dispatch computes `sl = r1 >> 16`, bounds it against
`0xFFFF807E`, and jumps through the table base `0x10127428`; because `sl` is a
sign-extended value in `0xFFFF80xx`, the effective entries land *below* that
base, in the ROM table at `0x101074D8..0x101074E8`:

```text
create  0x802C0100  ->  0x10110D34
give    0x802E0200  ->  0x101110EC
clear   0x802F0200  ->  0x1011131C
wait    0x80300500  ->  0x10111410
```

Coverage over 45 s unthrottled shift 6 (`run-df.sh probeKCALL`) shows these are
the live RTOS paths, not a dead table:

| Kernel call | Handler | Executions |
|---|---|---|
| create | `0x10110D34` | 568 |
| **give** | `0x101110EC` | **173965** |
| clear | `0x1011131C` | 7915 |
| **wait** | `0x10111410` | **33276** |

The kernel therefore signals constantly.  What it never signals is the two
objects this chain needs.  A `-d cpu` capture of 87293 give calls in 25 s (83
distinct objects, `probeGIVE`) gives the object histogram:

```text
0x01000008  83433      0x01000020   1696      0x01000007    726
0x0100003c    343      0x010000a9    330      0x010000ee    257
0x010001c4     68      0x0100014a     56      ...
```

and the two objects that block the chain are absent from it:

| Object | Role | Gives in 25 s |
|---|---|---|
| `0x010001BE` | GUI worker wait object | **0** |
| `0x010001C2` | display-service gate object | **0** |
| `0x010001C1` / `0x010001C3` / `0x010001C4` | neighbouring display objects | 3 / 1 / 68 |

So the blocker is now a pair of *specific missing kernel gives*, not a stalled
kernel, not a dead device and not firmware data: sibling display objects of the
same family are given, and these two never are.  The wait on a never-given
object returns with no flags, and `0` flags is exactly the `r4 == 0` the display
gate needs to be negative.

### The guest kernel lives in the MAS image and its SVC path is healthy {#the-guest-kernel-lives-in-the-mas-image-and-its-svc-path-is-healthy}

The wait/create/give stubs are ARM, not Thumb, and they pass a **descriptor
pointer in `r12`** before `SVC 6`:

```text
0x10104414  push {r4}; add r4, sp, #arg_0; push {lr}
0x10104420  ldr r12, =0x802C0100     create
0x10104424  svc 6
0x10104848  ...                      ldr r12, =0x802E0200   give
0x10104A14  ...                      ldr r12, =0x80300500   wait
0x101043A8  ...                      ldr r12, =0x802F0200   clear/mask
```

The reset path installs the vector base before any task runs:

```text
0x1002bd20  ldr r0, =0x10100000
0x1002bd24  mcr p15, 0, r0, c12, c0, 0     ; VBAR = 0x10100000
```

so the exception vector table is at VA `0x10100000` in the MAS image.  Coverage
over 45 s unthrottled shift 6 (`run-df.sh probeVEC3`):

| Address | Role | Executions |
|---|---|---|
| `0x10100000` | vector-table block start | 0 (entered at `0x10100008`) |
| `0x10100008` | SVC vector (`ldr pc,[pc,#0x14]` -> `0x10103560`) | **417421** |
| `0x1010000c` / `0x10100100` | other vector slots | 0 |

The SVC dispatcher at `0x10103560` extracts the SVC immediate, subtracts 4, and
jumps through the table at `0x10103594`; `SVC 6` therefore reaches
**`0x10107624`**, which reads `MPIDR` (per-CPU), indexes a per-CPU current-task
slot at `[0x1010711C]`, and finally dispatches through a syscall table pointer
loaded at `0x101077E0` (`ldr pc,[ip, sl, lsl #2]`).

Reading: the guest kernel is ordinary guest code inside the MAS image with an
identity-mapped ROM vector base; it executes hundreds of thousands of syscalls
per window and schedules the tasks that do run.  A `0` reply from the
display-gate query is therefore kernel/object state produced by this firmware,
not a missing device model and not a decode defect.

### The transfer engine is only configured, never used (measured) {#the-transfer-engine-is-only-configured-never-used-measured}

Watchpoints over the classic transfer register block at `0x20180000` for 60 s
unthrottled shift 6 (`probeXFER2`) record **no write at all** to source
`+0xC00`, destination `+0xC04`, control `+0xC10` or start `+0xC18`, and the
earlier stream watch records no `0x10D0..0x10EC` descriptor and no `0x01000000`
kick.  The only guest writes in that window are the setup latch
`+0x1214 <- 0x0100FFFF` / `0xFFFF` from `0x106EE3C8`, i.e. exactly the
initialization the model documents (`setup_values[7]`).

So the transfer stage is configured and then never asked to move a byte: no
classic job, no stream job, therefore no completion and no IRQ129 in this boot.

### The display service arms behind a guest-kernel reply {#the-display-service-arms-behind-a-guest-kernel-reply}

`-d cpu` with `-dfilter 0x1062EDDC+0x3C4` logs the full register state of every
executed block inside the function, which turns the branch question into a
direct reading (`run ex-6b-ercd/probeCPUF`, 60 s unthrottled):

```text
pc=1062eddc r4=b181d8a4 r0=10437a6f lr=10437a79   entry, called from 0x10437a74
pc=1062edf4 r4=ffffffff r0=b181c564               r4 <- r8 = -1
pc=1062ee02 r4=00000000 r0=00000000               r4 <- result of sub_106340D4
... r4 stays 00000000 through every block ...
pc=1062f102 r4=00000000 r0=00000000 lr=1063280f   cmp r4,#0 ; bge taken
```

Static confirmation: inside `0x1062EDDC..0x1062F1A0` only two instructions write
`r4` - `0x1062EDE8 mov r4,r8` (`r8 = -1`) and `0x1062EDFC mov r4,r0` - and the
path between them is straight-line, so `r4` at the gate is always the return of
`sub_106340D4(sub_10634050(), -1, 0)`.

`sub_106340D4` returns an **out-parameter**, not a constant (IDA's first
decompile is misleading):

```text
0x106340d4  push {r1-r7, lr}; mov r4, r0 (context pointer); mov r6, r2; mov r7, r1
0x106340d8  r0 = 0xB181C55C; ... classify the context (id 1/2/3)
0x10634100  movs r0, #0x57; bl nullsub_248      (trace hook 87)
0x10634114  ldr r0, [r4]                        (*context = the kernel object word)
0x10634116  mov r3, r6; mov r1, r7; add r2, sp, #0xC
0x1063411a  bl sub_10361116                     (SVC 6 wrapper, writes the out word)
0x10634122  ldr r0, [sp, #0xC]; pop {r1-r7, pc}  returns the kernel's out value
```

The three display contexts are ordinary guest RAM objects holding kernel
handles (dump of the same run):

```text
0xB181C558  0x010001C1   0xB181C55C  0xB181C558     context 1
0xB181C560  0x010001C2   0xB181C564  0xB181C560     context 2
0xB181C568  0x010001C3   0xB181C56C  0xB181C568     context 3
```

so the arming block is gated on a **microkernel reply** for handle
`0x010001C2`, not on a device register and not on firmware data.  In this boot
the reply word is `0`, the guard takes the `bge` exit, and ready/admission never
run.  Handle `0x010001BE` (the GUI worker's wait object) belongs to the same
allocator range, which is why the two frontiers are structurally identical.

Nothing here may be faked: the reply is produced by the guest kernel for its own
object, so the next measurement is what operation the wrapper issues and which
state of the object makes the reply negative.

### Measured EGL usage map: setup only, never present (120 s, unthrottled) {#measured-egl-usage-map-setup-only-never-present-120-s-unthrottled}

The EGL dispatch table at `0x1239A9F8` holds 49 `{name, handler}` pairs (first
pair at `0x1239AA08`, table header `{0, 0x1209BEB0, 0x1209BCD4, 1}`).  Coverage
of every present-capable entry and the setup entries, 120 s unthrottled shift 6:

| EGL entry | Handler | Executions |
|---|---|---|
| `eglChooseConfig` | `0x1067DBDA` | 12 |
| `eglGetConfigAttrib` | `0x1067E414` | 8 |
| `eglCreateWindowSurfacePointerDMPX` | `0x106815A4` | 12 |
| `eglMakeCurrent` | `0x10680426` | 19 |
| `eglCreateWindowSurface` (plain EGL) | `0x1067EA28` | 0 |
| `eglCreatePbufferSurface` | `0x1067EE3A` | 0 |
| `eglDestroySurface` | `0x1067F2A8` | 0 |
| `eglQuerySurface` | `0x1067F332` | 0 |
| `eglWaitGL` / `eglWaitNative` | `0x10680AFE` / `0x10680B12` | 0 / 0 |
| **`eglSwapBuffers`** | `0x10680B8C` | **0** |
| `eglCopyBuffers` | `0x10680CFE` | 0 |
| `eglDrawFrameModeDMP` | `0x106810BE` | 0 |
| `eglQueryFrameDMP` | `0x10681164` | 0 |
| `eglGetProcAddress` | `0x10680F60` | 0 |

So the application selects a config, creates a DMPX window surface per frame and
makes it current, and then stops: no present, no copy, no frame-mode draw, no
surface destroy, and no run-time `eglGetProcAddress` at all.  Twelve surface
creations against nineteen `eglMakeCurrent` calls show surfaces being reused and
re-bound rather than queued for presentation.

**Pointer parity matters here:** the table stores Thumb pointers, so every
handler is odd.  A `-dfilter` range at the odd address matches no translated
block and silently reports zero; these numbers use the even instruction
addresses.  The earlier zero-hit EGL probes used even addresses, which is why
they agree.

`eglSwapBuffers` itself (`0x10680B8C`) is a validator: it checks display `1|2`,
`sub_106B161A(display+24, surface)`, `nullsub_273(surface)`, display states
`12448/12449/12450` and `*(u32*)(surface+96) == 12437`, then either calls
`0x105A6906(display, cfg, surface, ..., mode, 0x8100, 1, 0x3038)` (the queue
submit) or `0x1067C99E(surface, 1)` (the commit).  `0x105A6906` records zero
executions while `0x1067C99E` runs 46 times, from other call sites in the
driver, so the driver's commit helper is exercised but no frame is ever queued
for the display through the swap path.

### The event bus is silent in the same windows (measured, two time bases) {#the-event-bus-is-silent-in-the-same-windows-measured-two-time-bases}

The same runs watch the mailbox primitives: `0x104BD99E` (post) executes **0**
times while `0x104BDDE0` (get) executes once, in 180 s at shift 6 both with and
without `sleep=off`.  The GUI's external entry `0x1053CF66` (registered as the
first entry of `0xB202F728`) also executes **0** times, and so do the dispatch
helpers it would call (`0x104E61A8`, `0x104E6400`, `0x104E619A`).

The two-mode A/B is the control for the harness change: at 180 s the throttled
and unthrottled runs give the same counts (`0x1053CFA8` 1, `0x104BD99E` 0,
`0x104BDDE0` 1, `0x105424E8` 0, draw 43 vs 41), so `sleep=off` changes wall cost,
not guest behaviour at this depth.

A QMP key injection (`run-key.sh`, `F1` -> EX pad `0x0a`) leaves every count
unchanged: no post, no step dispatch, no owner, no bank start in the following
35 s.

### Consequence for the present chain {#consequence-for-the-present-chain}

The publication path this build has is `0x10568BDE` (surface per frame) ->
`0x10568F60` (present) -> `eglSwapBuffers` -> transfer program -> IRQ129 ->
display buffer, and it stops at the second step: nothing in the image reaches
the present wrapper, and no event is posted that could reach it.  Frames are
created and abandoned, which is what the zero present/release counters and the
zeroed panel surfaces show.

### The init method registers the application callback table {#the-init-method-registers-the-application-callback-table}

`[ctx+48]` is the worker's init method, i.e. method-table entry 0
`0x1053CFA8` = `sub_104E5F48(0x1053CF66)` then `sub_10583730(...)`.  Those two
calls are the only place the GUI publishes itself to the rest of the image:

```text
0x104E5F48  0xB202F728..0xB202F78C <- 24 GUI service entry points
            (0x104E65FC(0x1053CF66), 0x1058C544, 0x105AF230, 0x105C0118 x2,
             0x104E7038, 0x104E703C, 0x104E72F4, 0x10542E28, 0x104E1BDC,
             0x105B5650, 0x104E9072 x3, 0x106589EC, 0x104EA300,
             0x10585BCC x2, 0x104E9D7C, 0x10656174, 0x104EAA40, ...)
0x10583730  0x10361034(~0xB2030060, 516)   clear the 516-byte request queue
0x1053CF66  (cmd == 4 && HIWORD(arg) == 16) -> 0x104E61A8(0),
            0x104E6400(arg), 0x104E619A(...)   the external event entry
```

So the GUI is reachable from outside only through the table at `0xB202F728` and
the `0x1053CF66` callback registered as its first entry.

### Coverage says exactly which of these run (unthrottled shift 6, 300 s) {#coverage-says-exactly-which-of-these-run-unthrottled-shift-6-300-s}

A `-dfilter`/`-d exec` run (`run ex-6b-ercd/run-df.sh probeLONG`) over the
frontier addresses plus the ROM range of the GUI framework gives:

| Address | Meaning | Executions in 300 s |
|---|---|---|
| `0x10569C76` | app UI draw | 41 |
| `0x1053CFA8` | worker init method (entry 0) | 1 |
| `0x10542E28` | GUI service entry called by the init table | 1 |
| `0x105424E8` | step dispatcher (entry 17) | 0 |
| `0x10542486` / `0x105429D4` | step states 9 / 11 | 0 |
| `0x1053FA1E` | mode-request consumer | 0 |
| `0x1053E19A` | GUI event dispatcher | 0 |
| `0x1053DBA2` | live-view gate | 0 |
| `0x10695450` | event-0x39 poster | 0 |
| `0x104DC804` | display owner callback | 0 |
| `0x104D7E78` | provider admission | 0 |
| `0x1046BF0C` | banks start | 0 |

`0x105424E8` switches on its first argument and reaches the mode-request path
through only two arms:

```text
case 9  -> 0x10542486 : sub_1053FA1E(a1) -> on success 0x1053F42A(5,a1),
                         0x1053E19A(a1), 0x1053EE72(5), 0x1053F8C6(5),
                         0x104BDDE0(23/24) -> 0x1061E778(15, 0/1), 0x1053F158(5)
case 11 -> 0x105429D4 : sub_104E6402(a1), then sub_1053FA1E() and 0x10543442/
                         0x10543414/0x1054349E
```

Both call `sub_1053FA1E`, which is the only consumer of the mode-request word
at `0xB181D970`; it maps request 1/2/3 to `0x10002/0x10003/0x10009` and clears
the word.  The mode request therefore cannot be produced or consumed while the
worker's kernel object is not given.

### Reading {#reading-1}

The remaining link is a single kernel-object give.  Every display gate sits
downstream of the step dispatcher, the step dispatcher is only reachable through
method-table entry 17, and entry 17 is only called after the worker's wait
returns.  Nothing executed in a 300 s unthrottled window gives that object, and
no code in the image references `ctx+0x20` outside the GUI methods themselves, so
the give is an external event delivered through the GUI callback table or the
kernel, not a device register the model can report.

## The GUI event-0x39 setter and the SLA readout {#the-gui-event-0x39-setter-and-the-sla-readout}

### Event 0x39 has exactly one poster (verified) {#event-0x39-has-exactly-one-poster-verified}

The GUI mailbox poster for event `0x39 (57)` is `0x10695450(a1, a2)`:

```text
if (a1 > 3 || a2 > 1) return;
ctx = *(u32*)0x1069570C          // 0xB181B3D4
ctx[0] = a1; ctx[1] = a2          // descriptor bytes
if (*(u8*)0xB181B5BC & 2) 0x106DDCA4()
r1 = ctx; r0 = 57; 0x104BD99E(r0, r1)   // post GUI event 0x39
0x10634030(); 0x10634088(.., 0x80)
```

The event-`0x39` handler `0x1054265A` requires `desc[0] == 3 && desc[1] == 1`,
so the producer must call `0x10695450(3, 1)`.  That function has **no `BL`
caller and no pointer literal**: it is invoked indirectly, from a callback the
caller has not been identified for yet.  Its context at `0xB181B3D4` is a mode
descriptor (runtime dump: `[0]=0, [1]=0, +0x28 = 0x13, +0x2C = 6`,
`+0x44 = 0x10204DB5`).

### The SLA readout initializes but never completes (direct observation, 420 s) {#the-sla-readout-initializes-but-never-completes-direct-observation-420-s}

`probeSLAREAD` watches the SLA result-readout cohort:

| Address | Role | Result |
|---|---|---|
| `0x10093CEC` | SLA mode2 dispatcher | **hit** |
| `0x1009ACE2` | readout registration | **hit** |
| `0x1009AD52` | readout initialization | **hit** |
| `0x1009ACA0` | readout ISR / guest callback | zero-hit |
| `0x104DC804` | MAS display owner | zero-hit |

So the SLA readout path is initialized (the same evidence as
`docs/devices/readout/protocol.md`) but no request completion ever reaches its
ISR.  The model deliberately does not fabricate that callback.

### Combined frontier {#combined-frontier}

Three independent "last links" are now measured and none is a device-model bug:

1. MAS GUI worker blocked on RTOS event `0x010001BE` (MAS core 1 idle).
2. The only GUI event-`0x39` poster (`0x10695450`) is never invoked.
3. The SLA readout initializes but its completion ISR never fires.

All three are "the firmware waits for an event that no reachable producer
sends".  The next step is to identify the caller of `0x10695450` (likely the
app-service interface at `0xB1819848` or an EX command lane) and to decide
whether the SLA readout completion requires a sensor/readout input that the
container does not contain.

## The inner image is copied, never started; the 0x39 setter has no reference {#the-inner-image-is-copied-never-started-the-0x39-setter-has-no-reference}

### The MAS inner image (direct observation) {#the-mas-inner-image-direct-observation}

The container's MAS component carries an embedded inner image at payload offset
`0x208853C` (2,117,120 bytes, `O[0x208853C,0x228CF3C)`).  The loader is
`0x104A132C`:

```text
0x10368B76(5)                                   prepare
copy(dst=0xB1000000, src=0x120A853C, len=*(u32*)0x122ACF3C = 0x204A00)
copy(dst=0x04008000, src=0x120A853C, len=64)     early handoff (vector table)
0x103610CC(0xB1807A90, 4)                       signal
```

(`0x103A3B66` is the copy helper; source `0x120A853C`, destination
`0xB1000000`, size `0x204A00`, handoff destination `0x04008000`.)

A RAM dump at the first app draw (`probeINNER`) shows the guest **did** copy it:
`0xB1000000` holds the same ARM vector table (`18 F0 9F E5` x5,
`FE FF FF EA`, then the handler table `0xB1000060`, `0xB10D0A08`, ...) and
1,879,425 of the 2,113,536 bytes are non-zero.

The image is not the GUI app: it contains none of the GUI strings
(`GuiLvEv1`, `GuiMeEv1`, `AppC_Msg`).  Its reset handler `0xB1000060` is only a
register-zeroing prologue.

A 420 s probe (`probeINNERSTART`) watches `0xB1000000` (vector base) and
`0xB1000060` (reset handler): **both are zero-hit**, while the app draws once.
So the inner image is resident but never entered at its own entry.

### The event-0x39 producer IS reachable through a veneer (corrected) {#the-event-0x39-producer-is-reachable-through-a-veneer-corrected}

**Correction.** The earlier "no reference" receipt below was produced by a scan
that only looked for `BL`/`BLX`.  The image also uses unconditional `B`/`B.W`
tail branches, and those carry the reference:

```text
0x103608AA  B.W 0x1048DB00      (veneer)
0x1048DB00  B.W 0x10695450      (4-byte thunk -> the event-0x39 poster)
```

`0x103608AA` has ten `BL` callers: `0x1020CD34`, `0x1020E17A`, `0x1020E19C`,
`0x1020F772`, `0x10246928`, `0x1024697C`, `0x102477FE`, `0x1024781E`,
`0x10247E26`, `0x10247EA0`.  So the poster is not dead code; it is posted
through that veneer.  A rescan of the EGL present wrapper `0x10568F60` with the
same `B`-aware scanner still finds **zero** references, so that conclusion
stands.

Coverage of the ten callers' functions (45 s unthrottled):

| Function | Runs | Contains a post site |
|---|---|---|
| `0x1020CA7A` | 26 | yes (`0x1020CD34`) |
| `0x1020E0E8` | 2 | yes (`0x1020E17A`, `0x1020E19C`) |
| `0x1020F6F2` | 1 | yes (`0x1020F772`) |
| `0x102468C4`, `0x1024692E`, `0x10247DEA`, `0x10247E3C` | 0 | yes |

so three of the posting functions *do* run, yet the veneer (`0x1048DB00`) and
the poster (`0x10695450`) never execute: the call is skipped on a conditional
path.

The condition at the busiest site is explicit:

```text
0x1020cd0a  ldrb r0,[r4,#1] ; cmp r0,#7 ; bne 0x1020CC34        (needs 7)
0x1020cd10  ldrb.w r1,[r5,#0x330]                                state byte
            state 1 -> post (0,1)   state 2 -> (2,1)
            state 3 -> post (3,1)   state 4 -> (1,1)   other -> (0,1)
0x1020cd34  bl 0x103608AA                                        post event 0x39
```

The event-0x39 handler needs the pair `(3, 1)`, i.e. `[ctx+0x330] == 3` **and**
`[ctx+1] == 7`.  The GUI live-view path is therefore gated on an application
context state, not on missing code.

### Which AppC message classes actually reach that post (measured) {#which-appc-message-classes-actually-reach-that-post-measured}

The posting function `0x1020CA7A` is the AppC stage handler.  A `-d cpu` capture
shows 21 entries in 45 s, always with `a1 = 0xB1818BD0` (the AppC_Msg context at
`0xB1818BC4 + 0xC`) and a varying payload pointer, all from caller
`0x10294318`.

Its message switch is a TBB at `0x1020CC6E` on
`*(u32*)(payload + 0x10) - 0x03000000` with ten cases.  Coverage of the switch
block and its case targets over 45 s:

| Block | Role | Executions |
|---|---|---|
| `0x1020CC6A` | the TBB itself | 4 |
| `0x1020CCA6` | one case block | 1 |
| `0x1020CCB8` | another case block | 1 |
| `0x1020CC78` (`0x03000000`) | case block | 0 |
| `0x1020CC94` (`0x03000001`) | case block | 0 |
| `0x1020CD0A` | the state test that precedes the event-0x39 post | **0** |

A `-d cpu` capture at the TBB records the delivered class directly: the single
observed execution carries `r0 = 7`, i.e. message code **`0x03000007`** (case 7,
whose block is `0x1020CCA6`).

The path to the state test is a different one: `0x1020CCB8` (a first-switch case
block) feeds the nested switch `0x1020CAAA`, whose case 4 is `0x1020CCA0`, which
branches to the second TBB at `0x1020CCC2` (switch on
`*(u32*)(payload+0x10) - 0x04000000`, 11 cases, TBB at `0x1020CCD0`).  Its
`0x04000000` case is `0x1020CCDC`; from there `cbz [r5+0x148]` falls through to
`0x1020CD0A`.  Coverage shows the whole tail cold:

| Block | Role | Executions |
|---|---|---|
| `0x1020CCA0` | nested case 4 | 0 |
| `0x1020CCC2` | second TBB | 0 |
| `0x1020CCDC` | `0x04000000` case | 0 |
| `0x1020CD0A` | state test before the post | 0 |

So the handler does receive messages - but the delivered class is
`0x03000007`, and the branch that reaches the state test is never selected.
The event-0x39 chain is gated on the application message class delivered to
`0x1020CA7A`, one level above the `[ctx+0x330] == 3` / `[ctx+1] == 7` pair
documented above, and on the nested class it would pass down.

### The `0x03000004` class is produced by a translator that never runs {#the-0x03000004-class-is-produced-by-a-translator-that-never-runs}

The class the event-0x39 branch needs is produced by a translator,
`0x1020E98A(in_ctx, code, payload)`: it maps incoming codes to outgoing ones via
`0x1020D364(ctx, out_code, 0, 0, payload)`, and the literal pool at
`0x1020EA5C` holds the outgoing codes it emits:

```text
0x1020EA5C  0x05000007  0x04100019  0x0410000E  0x04000001  0x03000004
```

Read from the switch: incoming `0x04100001` (68157441) produces outgoing
**`0x03000004`** (`50331652`); `0x0410000E`/`0x0410000F` produce `0x04000002`/
`0x04000003`; `0x04100027`/`0x04100028`/`0x04100036`/`0x0410003E` produce the
`0x05000001`/`0x05000002` family.

Coverage over 40 s:

| Function | Role | Executions |
|---|---|---|
| `0x1020E98A` | code translator | **0** |
| `0x1020D364` | outgoing post helper | 19 |

and the translator's only reference is a `B.W` thunk at `0x10204D4C`
(`mov r1,r3; b.w 0x1020E98A`) sitting inside the AppC handler-thunk block at
`0x10204D3A..0x10204D60` (a run of four-byte `movs rX,#n; bx lr` stubs).  So the
producer of `0x03000004` exists, is wired into the AppC handler table, and is
still never selected: the message class that would select it never arrives.

This is one level above the `0x03000007` measurement: that class reaches the
switches of `0x1020CA7A`, while the translator needs `0x04100001` traffic first.

### The display "enable" dispatcher runs with unsupported request codes {#the-display-enable-dispatcher-runs-with-unsupported-request-codes}

The display module is described by the ROM table at `0x118AF660`, which holds the
teardown op `0x1042C5E4` at `+0x0C` and the enable op `0x1042CB82` at `+0x24`
followed by a run of further ops (`0x1042CE67`, `0x1042CE97`, `0x1042CEC3`, ...)
and the panel strings `CARD0`/`CARD1`.  Coverage over 40 s:

| Op | Role | Executions |
|---|---|---|
| `0x1042C5E4` | teardown loop | 5 |
| `0x1042CB82` | enable dispatcher | **2** |
| `0x1042BFCA` | table user A | 0 |
| `0x1042C1F0` | table user B | 0 |

So the enable dispatcher is *not* dead - it runs - and it switches on
`*(u32*)(request + 16)`:

```text
23       -> 0x1042C8C4(idx, 1)
24/25    -> 0x1042C8C4(idx, 2 / 3)
26       -> 0x1042CD62(idx)
29       -> 0x1042CD76(idx)
20       -> 0x1042C87C(idx, 1)   -> 0x1042CCD6   (the selector that can pick mode 1)
other    -> default, returns
```

A `-d cpu` capture records three entries from the same indirect call site
(`0x1020F8D8`), with request pointers `0xB51BB538` (twice) and `0xB209FA24`.  A
gdb peek at those addresses in a fresh run shows `[req+16]` holding `4` (and a
pointer value in the other), i.e. **request codes outside the supported 20..29
set**, so all three take the default branch and neither `0x1042C87C` nor the
mode selector ever runs.

Together with the mode watch this closes the display-mode question: the only
mode write that happens is the hardcoded `mode = 2` from `0x1042D72E`, and the
enable requests that this boot issues carry codes the handler does not accept.

### The display-service walk runs and skips the ready branch (measured) {#the-display-service-walk-runs-and-skips-the-ready-branch-measured}

The display module function `0x104BDB42(request_code)` does run (once per
window).  A full register trace of its single execution (`probeDM2`, filter
`0x104BDB42+0x200`) shows it walking a list of service sub-codes:

```text
entry       r0 = 0x04030034
items       0x04030002  0x04030003  0x0403000a  0x04030008  0x04030009
            0x04030019  0x0403001a  0x0403000d  0x0403000e  0x0403000f  0x04030015
```

and it **never executes the ready-setter call at `0x104BDC64`**.  The guard is
explicit:

```text
0x104bdc34  ldr  r0, =0x04030002
0x104bdc36  adds r0, #0x18            r0 = 0x0403001a
0x104bdc38  cmp  r5, r0               r5 = the current service item
0x104bdc3a  bne  0x104BDC68            skip the whole ready path
```

so the ready setter (thunk `0x10637B72` -> `0x104D7F16`) is reached only when the
walk's current item is exactly **`0x0403001A`**, and this boot's walk does not
satisfy that at the comparison point.

Related callers found with the `B`-aware scanner:

| Target | Callers |
|---|---|
| `0x10637B72` (ready thunk) | `0x104BDC64` (this walk), `0x1062F120` (service start) |
| `0x104D7E78` (admission) | `0x10502D38` (**F091 handler `0x10502D0A`**), `0x10637B5E` (poller) |
| `0x104DB8BA` (natural bank start) | `0x104DC07C`, `0x104DC6DE` (owner states) |
| `0x1046BF0C` (bank command) | the seven previously documented sites |

So there are two independent routes into the display envelope - the service walk
and the F091 handler - and both are measured cold in this boot for different
reasons: the walk skips its `0x0403001A` iteration, and no F091 request arrives.

### The display service request table maps codes to handlers (structure verified) {#the-display-service-request-table-maps-codes-to-handlers-structure-verified}

The service codes live in a three-word-entry table in ROM around
`0x119DB400..0x119DBA00`:

```text
{request-class word, request code, handler}

0x119DB438  0x040F0020  0x0403001A  0x107387B9
0x119DB444  0x040F0014  0x04030018  0x1073873F
0x119DB9E4  0x04090017  0x04030034  0x1073885D
0x119DB9F0  0x04090032  0x04030035  0x1073873F
```

so `0x0403001A` is routed to `0x107387B8` and `0x04030034` to `0x1073885C`.
A `-d cpu` capture of the walk shows its caller is `0x107392EE` (LR
`0x107392F3`) and that `r5` at the ready guard is the **request code**
`0x04030034`, not a loop item:

```text
pc=104bdbfe r5=04030034   pc=104bdc34 r5=04030034   pc=104bdc68 r5=04030034
```

so the guard `cmp r5, 0x0403001A` is asking "is this request 0x0403001A?" and the
boot's request is `0x04030034`.  The ready setter is therefore reached only for
display-service request **`0x0403001A`**, and this boot never issues it.

### The display event is broadcast twice and arrives as 0x04030034 (measured) {#the-display-event-is-broadcast-twice-and-arrives-as-0x04030034-measured}

The display service propagates its state changes through two levels of
registered callbacks:

```text
0x1052E3AE(code, arg)   iterates a 4-entry table at 0xB2B2CB7C
                        and calls each handler(code, arg)
0x107392D6(code, arg)   iterates a 16-entry table at 0xB2989C40 and does the same
```

A `-d cpu` capture over 45 s records exactly one entry at each level:

| Level | Caller | Code |
|---|---|---|
| `0x1052E3AE` | `0x10509FDC` (inside the display service start `0x10509F9E`) | `0x04090017` |
| `0x107392D6` | `0x1052E3C4` (the outer broadcast's `blx`) | `0x04030034` |

So the service-start event `0x04090017` reaches the display subscriber as
request **`0x04030034`** - the code that the walk at `0x104BDB42` receives and
that fails the `0x0403001A` guard.  The translation happens in the per-subscriber
adapter between the two broadcasts (same caller site, different `r0`); the
router table entry for `0x0403001A -> 0x107387B8` exists in ROM, so the arming
event is a different outer event that this boot never emits.

### The arming request is the F091 handler's IPC and never leaves (measured) {#the-arming-request-is-the-f091-handler-s-ipc-and-never-leaves-measured}

The display service event codes are IPC ids, and the arming one is
`0x040F0020`:

```text
sub_10502CBC(v):  sub_104C0F20(0x040F0020, {v, 0, 0x10502CAE})   then waits 1000 ms
sub_10502E7C(..): sub_104C0F20(0x040F0000, {0x105031AC})         then waits 3000 ms
```

`0x040F0020` is the router-table class word whose display request is
`0x0403001A` - the code the ready guard requires.  Both senders have callers in
the F0-handler block:

| Sender | Callers |
|---|---|
| `0x10502CBC` | `0x10502D20` (inside **F091 handler `0x10502D0A`**), `0x10502F4E` |
| `0x10502E7C` | `0x10502F30` |

and coverage over 45 s shows the whole F0 block cold:

| Function | Role | Executions |
|---|---|---|
| `0x10502CBC` | send `0x040F0020` | 0 |
| `0x10502E7C` | send `0x040F0000` | 0 |
| `0x10502D0A` | F091 handler | 0 |
| `0x10502DB8` | F092 handler | 0 |

So the display arming path is: **F091-class request -> handler `0x10502D0A` ->
IPC `0x040F0020` -> display service -> request `0x0403001A` -> ready
(`0x104D7F16`) and admission (`0x104D7E78`)** - and the first link never fires.
The same handler also calls admission directly (`0x10502D38`), so one inbound
F091 would arm both halves at once.

Consolidated event receipt for a 60 s window: the display service broadcasts
exactly one outer event (`0x04090017`, twice) and the subscribers receive exactly
one code (`0x04030034`); `0x040F0020` is never broadcast.

### The service request plumbing, from the class table down to the handler {#the-service-request-plumbing-from-the-class-table-down-to-the-handler}

The request path has three layers, all verified structurally:

```text
class table 0x119E1E84..  rows whose handlers are 0x107722E5/EB/E3
   -> router 0x104C6B14(request)
        v2 = *(u16*)(request+12)            the service code
        sub_104C6AB4(request) == 0x2001     validate
        v4 = sub_104C6CC4(v2)               look up the handler row
        if [0xB188BC7C]() || v2 == 0xF001   dispatch via sub_104C6A80(v4, request)
        else                                reject via sub_1063C4FC(request)
   -> handler row from the table at 0x119E4820
```

The handler table is 28-byte entries `{code, 0, 0, 0, handlerA, handlerB, handlerC}`:

```text
0x119E4820  code 0xF079   handlers 0x104B2F72 0x104B2F84 0x104B2F70
0x119E483C  code 0xF091   handlers 0x10502CAA 0x10502D0A 0x10502CA8
0x119E4858  code 0xF092   handlers 0x10502D82 0x10502DB8 0x10502D80
0x119E4874  code 0xF093   handlers 0x10502E12 0x10502F10 0x10502E10
0x119E4890  code 0xF094
```

so `0xF091`'s middle handler is exactly the `0x10502D0A` measured cold above, and
`0xF092`'s is `0x10502DB8`.  The router is itself installed in the class-2
envelope table (its three call sites are `0x107722E6`, `0x10772302`,
`0x1077230E`), and the send side is `0x104C0F20` with 51 call sites.

`0xF091`/`0xF092` appear **only** in this handler table: there is no `movw`
immediate for either code anywhere in the image and neither appears in the EX
component, so the sender addresses the row through the table rather than by a
literal.

A 120 s unthrottled window shows the whole request layer idle, not just the two
display rows:

| Address | Role | Executions (120 s) |
|---|---|---|
| `0x104C6B14` | service request router | **0** |
| `0x10502D0A` | F091 handler | 0 |
| `0x10502DB8` | F092 handler | 0 |
| `0x10502E12` | F093 handler | 0 |
| `0x10502CAA` | F091 sibling | 0 |

So no class-2 service request reaches MAS at all in this boot.  The **send**
side is idle too: a `-d cpu` capture of the send API `0x104C0F20` over 45 s
records **zero** entries, even though the image has 51 static call sites for it.
So MAS's host-facing service bus is dormant in both directions: nothing sends a
request and nothing routes one.

That is the remaining link between the display service and its host, and it is
the same conclusion the EX side reaches from the other direction (its
host-command table carries neither 113 nor 131).  The local display events still
flow - they use in-process callback tables (`0x1052E3AE`, `0x107392D6`), not this
bus - but the host-facing service envelope is completely quiet.

### Correction: the 0x1042Cxxx/0x1042Dxxx cluster is the device-subsystem dispatcher {#correction-the-0x1042cxxx-0x1042dxxx-cluster-is-the-device-subsystem-dispatcher}

The op dispatcher `0x1020F876` does not drive the display.  Its tables are the
**system-command and subsystem tables**, and they are named in the ROM:

```text
table A 0x118AED0C  28-byte entries keyed by eSyscCommandId, e.g.
  0  eSyscCommandId_Create2nd              00 0e
  1  eSyscCommandId_InitializeFastBoot     0f 0c 0b 01 05 08 04
  2  eSyscCommandId_StartTaskFastBoot      01
  3  eSyscCommandId_Initialize1st          03
  4  eSyscCommandId_StartTask1st           08 03 01
  5  eSyscCommandId_Initialize2nd          0d 0e 05 02 10 04 09 0a
  6  eSyscCommandId_StartTask2nd           00
  7..11 BSShutdown / BSShutdownCancel / ...
  18 eSyscCommandId_AppFirstStartComplete  02 0a 04
  19 eSyscCommandId_PowerOnBoot            03 01
  20 eSyscCommandId_CardInsert             03
  21 eSyscCommandId_CardRemove             03
  22..29 CardMountComplete/CardMountError/CardUnmount*/CardFormat*
```

and table B (`0x118AEC40`, 12-byte entries) maps each sub-op index to a getter
that returns a subsystem descriptor; the descriptor's first word is the
subsystem handler:

| idx | descriptor | handler | name string |
|---|---|---|---|
| 0 | `0x118AF5CC` | `0x1042BACC` | - |
| 1 | `0x118AF66C` | `0x1042C5E4` | - |
| 2 | `0x118AF7F4` | `0x1042E948` | - |
| **3** | `0x118AF684` | **`0x1042CB82`** | **`CARD0`** |
| 4 | `0x118AF6B0` | `0x1042D32A` | - |
| 5 | `0x118AF77C` | `0x1042DCFA` | `ML_L...` |
| 6 | `0x118AF780` | `0x1042DE40` | `ML_LENS` |
| 7 | `0x118AF758` | `0x1042D9D8` | `F_LENS` |
| 8 | `0x118AF734` | `0x1042D866` | `EXTSB` |
| 9 | `0x118AF7C8` | `0x1042E432` | - |
| 10 | `0x118AF7A4` | `0x1042E246` | `NwBoard` |
| 11..16 | `0x118AF834..` | `0x1042ECD2..` | - |

**So the earlier "display mode / display enable" reading was a mis-attribution.**
`0x1042CB82` is the `CARD0` (SD-card) subsystem: its `20..29` switch is exactly
`eSyscCommandId_CardInsert/CardRemove/CardMount*/CardUnmount*/CardFormat*`, its
`op = 20` branch (`0x1042C87C -> 0x1042CCD6 -> 0x1042CCA0`) is card-insert
handling, and the `mode` byte at `+0x1C` written with `2` by `0x1042D72E`
belongs to that card object, not to the display.  `0x1042D866` is `EXTSB`, also
not the display.

The display side is the owner/admission/provider path documented earlier
(`0x104DC804`, `0x104D7E78`, `0x1046BF0C`); that part of the frontier is
unaffected by this correction, and the earlier statements about the display mode
are withdrawn.

### The display op dispatcher and its tables (structure verified) {#the-display-op-dispatcher-and-its-tables-structure-verified}

`0x1020F876(request, a2)` is the op dispatcher.  It reads `op = *(u32*)(request+16)`
and uses two ROM tables:

```text
table A  0x118AED0C  28-byte entries: {op id, name pointer, up to 8 sub-op bytes}
                     terminator 0x11; only entries with entry[0] == op are valid
table B  0x118AEC40  12-byte entries: {index, name pointer, getter stub}
```

The op-4 entry is `{4, 0x120A62A8, bytes 08 03 01 11}`, i.e. sub-ops
**8, 3, 1**; table B resolves them to the getter stubs at `0x1042D7F8`,
`0x1042C770`, `0x1042C54C`.  For each sub-op the dispatcher calls
`handler = *getter(); handler(request)`.

Measured: the only handler reached from this site in the window is
`0x1042CB82` (three calls, all from `0x1020F8D8`), and it receives the same
`op = 4`, which its `20..29` switch does not cover, so it returns without acting.
**Correction:** `0x1042CB82` is the `CARD0` (SD-card) handler, resolved from the
subsystem descriptor names - it is not a display handler (see the correction
section below).

The getter stubs themselves are two-instruction returns whose literal pools the
current IDB has not typed as code, so the exact getter -> descriptor mapping is
still open; the measured callee identity (`0x1042CB82`) is what matters here.

### Historical scan receipts (superseded by the correction above) {#historical-scan-receipts-superseded-by-the-correction-above}

`0x10695450` had been searched for as:

| Search | Result |
|---|---|
| 2-byte-aligned Thumb `BL`/`BLX` targets | none |
| ARM `BL`/`BLX` immediate targets | none |
| 32-bit literal, both alignments, whole image | none |
| `TBB`/`TBH` table entries landing on it | none |
| 32-bit value in RAM `0xB1800000..0xB3000000` (GDB `find`) | none |
| 32-bit value in the inner image dump `0xB1000000..0xB1204000` | none |

None of those searches covered `B`/`B.W`, which is where the reference is.

`0x10695750`, the display-mode dispatcher that would call the callback table at
`0xB181B3D4+0x44` (`0x10204DB5` twice), has the same historical footprint and
should be re-scanned with the `B`-aware scanner before any conclusion.

### Reading {#reading-2}

The two remaining candidates for the live-view start are (a) a producer whose
address is computed at run time in a way none of the scans see, and (b) dead
code left in this container image.  The scans above are exhaustive for the
static forms the MAS code actually uses elsewhere (literal pointers, direct
`BL`, jump tables), and neither the ROM, the main RAM window nor the inner
image holds the address, so the practical conclusion is: **the supplied
image has no reachable producer for GUI event 0x39**.  If that is the case, the
view-state-25 -> mode-request -> live-view branch cannot start from this image
alone, exactly like the EX template-zero gates in
[`../validation/frontier.md`](../validation/frontier.md).

This is a firmware-data boundary, not a device-model gap; fabricating the
event would be a synthetic stimulus, not an unchanged-firmware receipt.

## Reachability summary (unchanged container, 420 s windows) {#reachability-summary-unchanged-container-420-s-windows}

| Link | State | Evidence |
|---|---|---|
| MAS app boot, stages 1..3 | runs | `0x1020D41A` stage receipts |
| app UI draw `0x10569C76` | runs (41x in 300 s) | `-dfilter` coverage |
| composition surface `0xbf309600` | stays zero | RAM dump at 30 s and 90 s |
| AppC message framework (defer/queue) | runs | `probeDEFER` |
| GUI worker task | registered, blocked on RTOS event `0x010001BE` | RAM dump + CPU 1 idle |
| GUI worker init (method 0) `0x1053CFA8` | runs once | `-dfilter` coverage |
| GUI step dispatcher `0x105424E8` (method 17) | never runs | `-dfilter` coverage, 300 s |
| step states 9/11 `0x10542486`/`0x105429D4` | never run | `-dfilter` coverage |
| mode-request consumer `0x1053FA1E` | never runs | `-dfilter` coverage |
| GUI event dispatcher `0x1053E19A` | never runs | `probeGUIEV` |
| GUI event `0x39` | never posted | mailbox watchpoints zero |
| event-`0x39` poster `0x10695450` | reachable via `0x103608AA -> 0x1048DB00`; never posted (callers skip it) | `B.W`-aware scan + coverage |
| mode request `0xB181D970+0` | never written | u32/u16 watchpoints zero |
| live-view gate `0x1053DBA2` | never entered | `probeLV` |
| display owner `0x104DC804` | never runs | all windows |
| provider admission `0x104D7E78` | never runs | `probeDMSVC` |
| SLA readout init | runs | `0x10093CEC`/`0x1009ACE2`/`0x1009AD52` |
| SLA readout ISR/completion | never fires | `0x1009ACA0` zero-hit |
| MAS inner image copy | runs (RAM matches payload) | `probeINNER` |
| inner reset `0xB1000060` | never entered | `probeINNERSTART` |

The shape is consistent: every display-side module is initialised or
registered, then waits for an event no reachable producer sends.  With the
unchanged container and no external stimulus, the first GUI frame is therefore
not reachable in this model; the missing inputs are the camera-state/sensor
events documented above, and producing them would be a synthetic stimulus,
which the objective excludes.
