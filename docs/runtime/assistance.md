# Assisted guest execution

> Scope: an explicit, logged method for driving the original firmware with
> guest-level assistance. Assisted execution is never presented as natural
> boot, and no firmware byte is modified.

## Why this page exists

The frontier documented in [`guichain.md`](guichain.md) and
[`../validation/frontier.md`](../validation/frontier.md) shows the unchanged
C1.70 bundle reaching its application and creating graphic surfaces, while the
display chain never arms: the display context object is never created, so the
provider query replies 0, the provider is never admitted and no GPU command
stream is ever submitted. Every surface the panel observer can be pointed at is
therefore still black, including after 120 s of unthrottled execution.

A published Z7 model (`WonderLantern/z7-qemu`, firmware 3.80) reaches its
menu/shooting UI only through the same class of assistance, and states it
plainly: *"Original firmware menu/shooting UI with explicit startup
assistance"* and *"the result is assisted system emulation, not passive
observation or proof of natural camera power-on"*. Its assistance is not CMOS
content and not electrical key injection; it is a GDB-RSP **call into original
firmware functions** with prepared registers and RAM (`tools/probe.py`
`call_fixture`): save registers, place a payload on a scratch stack, set `lr`
to a return checkpoint, set `pc` to `function & ~1`, set the Thumb bit, resume.

## The method as implemented here

`work/runs/ex-6b-ercd/assist.py` implements the same primitive for this
workspace, with one change that keeps the guest's own memory authoritative: the
return checkpoint is not a firmware address but a two-byte `b .` sentinel
written into unused stack scratch, and the harness owns a breakpoint on it.

```text
scratch   = (sp - 0x200) & ~7
*scratch  = 0xE7FE          // Thumb `b .`
sp        = scratch - 0x40
lr        = scratch         // sentinel
pc        = entry | 1       // Thumb
cpsr     |= 0x20            // Thumb state
hbreak *scratch             // hardware breakpoint: no code patch, no
continue                    // dependency on an executable being loaded
```

Only RAM outside every live frame is written, and only registers are set. The
firmware image, its load addresses and its data are untouched.

Each call emits one line, so a log always distinguishes assisted from natural
execution:

```text
ASSIST_CALL entry=0x106ee430 args=0x58 state=entering sentinel=0xb51c8920
ASSIST_CALL entry=0x106ee430 args=0x58 reply=0 status=returned sentinel=0xb51c8920
```

`status=returned` means the sentinel was reached with the stack and link
register intact; any other stop is reported with the actual PC instead of being
retried or hidden.

### Receipts

| Call | Result | Meaning |
|---|---|---|
| `0x10202B06` (`BX LR`) | `reply=15 status=returned` | the primitive itself: entry, Thumb switch, sentinel return |
| `0x106EE430(0x58)` | `reply=0 status=returned` | the RM GPU-IF register accessor runs and agrees with a direct read of `0x20180058` |

The second receipt also fixes the graphics aperture: `sub_106EE430` is
`return *(u32 *)(a1 + 0x20180000)`, and `sub_106EE3C4` is the matching store, so
the SMAPH-F resource-manager register block on this part is the window this
model already owns at `0x20180000`.

## Boundaries

- A call is guest code running on the guest's own CPU with the guest's own
  stack discipline. It is not a model shortcut and not a fixture.
- Assistance may supply **inputs and context** (registers, argument payloads,
  RAM scratch, image data, keys). Forging the outputs the firmware is waiting
  for - ready flags, service replies, kernel object gives, interrupts or frames
  - stays forbidden: those must be produced by guest code or by a device that
  implements its documented behaviour.
- Assisted runs are recorded as assisted. In particular, a frame produced after
  an assisted call is never evidence about unassisted startup.
- Every call is bounded: the harness stops with a reported PC instead of
  continuing into unrelated execution, and the runner kills the machine after
  the call.

## Next use

The reference reaches its UI by calling, in order, an image-memory init, a
power report, a system report, a virtual power-on, an app-service wait, then
injecting a menu key through the firmware's own auto-operation dispatcher, and
finally a live-view start. Reproducing that ladder on C1.70 needs the C1.70
addresses of those entry points; they are found the same way the reference
documents them - from the application's own call graph, not by guesswork. The
primitive above is what makes the result observable once they are identified.

## The first assistance target: the camera-control live-view command

The MAS image exposes the host-facing camera-control family by name
(`CCaptureCameraControl_StartCameraLiveView : fail Result %xh`,
`CCaptureCameraControl_EndCameraLiveView : fail Result %xh`).  The two handlers
are registered as Thumb pointers in a descriptor table at `0x123B3FF4` and
`0x123B3FF8`; rows in that table are `{state pointer, 1, handler}`, e.g.
`{0xB1819F84, 1, sub_107655A8}`, `{0xB181A034, 1, 0x10765995}`,
`{0xB181A038, 1, 0x10765A07}`.

`sub_10765994` (`a1, a2, status_out, data`) is the StartLiveView command:

```text
sub_10521298(1)                     wait on object 0xB1819B0C
sub_103BA53E(sub_10765980)          run the command state machine
sub_10765980(code):  code 0x04100043/0x04100044 -> sub_10521290 -> signal 0xB1819B0C
sub_105212AA(1, &status, 10000)     wait up to 10 s for the confirmation
" Start live view." / "Start live view was timeout."
```

So the whole command is a request/confirm pair over one kernel object: the
camera-control state machine posts `0x04100043`, and the command completes when
that object is signalled.  This is a host command, not an internal event, which
is precisely the class of input assistance is allowed to supply.

### What happened when it was called directly (measured)

`assist.sh 25 0x10765994 0 0 @scratch-0x10 0` enters the handler and never
returns: the call was still inside it when the 90 s gdb budget expired, i.e.
after thousands of guest seconds at shift 6.  The handler is a *blocking*
camera-control command that expects to run in its own task context, and the
object it waits on is never signalled because nothing posts
`0x04100043`/`0x04100044` in this boot.

Conclusion: a blocking command handler is the wrong call target.  The correct
assisted step is to post the camera-control request into the firmware's own
request pipeline - `sub_103BA53E`'s entry path - so the firmware runs it in its
own task, and then let the unchanged firmware decide whether live view starts.
That is the next experiment; it needs the request record layout for
`0x04100043`, which is the same `AppC` message shape the app already sends
(`sub_10478F3C` builds a message with a class word at `+6` and a command word at
`+16`).

### The delivery API a host command would use

`sub_10478F3C(subscriber, message)` is the AppC delivery wrapper:

```c
sub_104AED26(*(u16 *)subscriber);                 /* guard the subscriber id   */
if (subscriber->callback) return callback(subscriber->id, message);
sub_10361034(&record, 36);                        /* 36-byte transport record  */
record.class = *(u16 *)(message + 6);
record.words = (*(u32 *)message - 12) >> 2;       /* payload words             */
memcpy(record.payload, message + 12, 20);
if (sub_104C0F96(&record) == 8193) return 1;      /* 0x2001: delivered         */
sub_1063C4FC(message);                            /* otherwise: release        */
```

so an AppC message is `{+0 byte length, +6 class word, +12..+32 payload}` and
`sub_104C0F96` is the deliver step that answers `0x2001`.  Supplying the
live-view command means building that record for code `0x04100043` with the
payload the camera-control receiver expects, and delivering it once the app's
control task is running - the payload layout is the remaining unknown, and a
wrong guess is a guest crash rather than a silent success, so it must be read
out of the receiver before it is sent.

## Receipt: a camera-control command delivered from outside

`sub_104BE90A(code, payload, a3, a4)` is the camera-control receive path.  It
translates the code through the 44-entry table at `0x1094559C`
(`{u32 code, u8 id}` pairs, e.g. `0x04100043 -> 0x3B`), logs through the
`nullsub_248` hook, and switches on `code - 0x04100001` with a byte jump table
at `0x104BE970`.  Case 66 is the live-view command (`0x04100043 ->
0x104BECB0`):

```text
memcpy(0xB201C49C, payload, 0x1C)          /* 28-byte live-view parameter block */
if (block[0] == 2) { if (block[1] == 1 && u32@4 <= 0x7D0) { sub_10653378(1); sub_1065335E(1); } }
sub_10653378(0);
sub_104BD99E(0x63, &block);                /* post GUI event 99 with the block    */
sub_10634030(); sub_10634088(0x80);
if (block[0] == 0) { sub_10714CC8(8, sub_104BE8E4 + 1); sub_10630C6A(1); sub_10630CF0(); }
```

Measured, with the payload `{0, 1, 0, ...}` written to scratch RAM:

```text
ASSIST_CALL entry=0x104be90a args=0x4100043,0xb51c8870,0x0,0x0 reply=0 status=returned
ASSIST_LV reply=0
LV_BLOCK=[0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
LV_FLAG63=0x1 LV_DATA63=0xb201c49c
```

The 28 bytes the firmware copied are exactly the bytes supplied, and the
firmware itself raised event 99 through its own setter (`sub_104BD99E` writes
the flag at `0xB201BDD0 + 4*id` and the data at `0xB201C984 + 4*id`).  Nothing
was forged at the model level: the command went in at the receive entry, and
every byte after it is guest-produced.

### Where the chain stops

Event 99 has two consumers, found by locating every `id == 99` argument to the
event getter `sub_104BDDE0`:

| Consumer | Role |
|---|---|
| `sub_1044F6E0` case 9 | device-control stage machine: consume event 99, then `sub_10632494`/`sub_10632DF4`, then `sub_10636CF8(2)` or `sub_1063C2B2`/`sub_1044F366` |
| `sub_10740296` | the other event-99 reader |

`sub_1044F6E0` **never executes** in a 30 s boot window (hardware breakpoint,
zero hits), so the flag set at `0xB201BDD0 + 4*0x63` is never consumed.  Its
entry survives in the image as a single Thumb pointer at `0x123BEEA8`, in a run
of task entries `0x123BEE64..0x123BEEB4` (`0x1044E363`, `0x1044E3AF`,
`0x1044F323`, `0x1044F6E1`, `0x1044F83D`, ...).  No literal in the image points
at that table, so the camera-control task set is entered by module dispatch, not
by an in-image function pointer.

The conclusion is the same one the display chain gives, from the other side: the
transport, the command decode and the parameter handling are all reachable and
verified, and what never starts is the camera-control task set itself.

### Harness pitfall worth remembering

The first delivery read back a stack frame instead of the payload because the
payload was placed at `scratch - 0x80` while the call's stack starts at
`scratch - 0x40` and grows down through it.  Payloads now live at
`scratch - 0x400`, and the receipt above checks the copied block byte for byte
so a regression cannot pass silently.
