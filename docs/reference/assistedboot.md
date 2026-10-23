# How the published Z7 model reaches a UI, and what transfers to C1.70

> Source: `WonderLantern/z7-qemu` (Nikon Z7, firmware C 3.80), files
> `docs/architecture.md`, `docs/firmware.md`, `docs/research-notes.md`,
> `docs/usage.md`, `docs/debugging.md`, `tools/interactive.py`, `tools/probe.py`,
> `tools/capture_diagnostics.py`.  Their addresses are **not** transferable; the
> shape of the method is.

## What they do, in their own words

> "The host pauses the guest at version-specific checkpoints and calls original
> **memory, power, system-state and application-ready** routines. A synthetic
> system report and a logged power transition stand in for missing controller
> behavior."

> "Original program bytes in the input file are unchanged. Guest RAM is changed,
> and original functions are deliberately invoked. Thus the result is **assisted
> system emulation**, not passive observation or proof of natural camera
> power-on."

So the reference does not reach a UI naturally either.  Its `docs/debugging.md`
says a plain direct boot has "no browser startup assistance; reaching a UI is not
expected".  Assistance is the documented path, and it is scrupulous about what it
does and does not claim.

## The ladder they run

```text
run_to 0x103f9ce2                 checkpoint after early image-memory init
call   0x10123432                 init image memory
run_to 0x100531b8
call   0x101f9cca(0x43090010, ..) load power report
call   0x10058b90(0x03040002, ..) init system state  (synthetic SYSTEM_REPORT)
run_to 0x102699ec
RAM    power block 0xa1f90bf0 |= 0x200
call   0x101b493e(..)             switch on virtual power
run_to 0x100531bc                 <-- the app's idle checkpoint; everything below
call   0x1005cbf8(0xa1819748, 0)  wait for the app service (object 0xa1819748)
AO     0x105230a6                 inject one MENU key  -> raises the menu UI
```

and for the shooting profile, after the same prefix:

```text
zero 128 B at 0xa18fb73c          MVD frame schedule (16 x period/phase pairs)
call 0x101c9a5e(0x3dc/0x3dd, 1)   live-view diagnostic toggles
call 0x101b4224(..)               fixed orientation through the firmware's own API
call 0x101c9a2c / 0x101c9a5e      choose the empty correction table
call 0x1005d4c0(0xa1819748, 0x05040016, 2)   shooting-mode event to the app object
call 0x1015ba4a(0x180c, 1)        a generic (id, value) setter
poll 16x: run_to 0x100531c2 then 0x100531bc, read app readiness flags
call 0x10122912(0)                take the default (absent) calibration branch
call 0x10521d84(buf, 0, buf+16)   StartLV; success = r0==1 AND out[16:20]==0
call 0x1017081e(5000)             let the guest finish its first draw
```

Three properties of that ladder matter more than its addresses:

1. **one app object pointer is threaded through every call** (`0xa1819748`);
2. **the calls are issued from one app idle checkpoint** (`0x100531bc`), with an
   adjacent poll pair (`0x100531c2`/`0x100531bc`) used to advance the app task one
   iteration at a time;
3. **the last call takes an output structure, not just a return code** - StartLV
   is only accepted when `out[16:20] == 0`.

## Keys

The key call is the firmware's own AutoOperation entry with a **bitmask**, not a
keyboard matrix: eight `u32` words, exactly one bit set at the switch ID
(`right 34, left 35, up 36, down 37, menu 67, play 68, delete 69, i 72, ok 74`),
word 4 is the press duration in units of 10 virtual ms, and the third argument is
an ack buffer.  They state plainly that this "is not a physical electrical switch
protocol" and that the ack "can still represent a failed capture start".

## The RAM fixtures

- 128 bytes at `0xa18fb73c`: the MVD engine's frame schedule (16 period/phase
  pairs). The MVD block at `0x20201000` is *not modelled*, so the schedule is
  suppressed **before any capture is allocated** rather than fabricating
  completions. It is SHA-256-guarded against the verified table.
- 2 bytes `"0470"` for the sensor resource version, control-test only.
- Word `0x200` in a 0x30-byte power block, fed to the virtual power-on call.

## What transfers to Z7II C1.70

| their property | what it means here |
|---|---|
| they never reach a UI naturally | matches what this workspace measured: the C1.70 app runs but its GUI event layer is cold |
| assistance = calling original functions with prepared registers/RAM | already implemented here as `assist_call` (see `docs/runtime/assistance.md`), with a `b .` return sentinel instead of a firmware trampoline |
| one app object threaded through every call | find the C1.70 equivalent of `0xa1819748`; the candidate measured here is the AppC context at `0xB1818BD0` |
| every call issued from one app idle checkpoint | find the C1.70 app's poll loop (`0x100531bc` analogue) rather than calling from an arbitrary context - this is also why a caller-identity-sensitive kernel syscall failed when called from a hijacked context |
| the UI is raised by an injected MENU AutoOperation | this is the single most transferable step: find the C1.70 AutoOperation entry and inject one MENU operation, instead of waiting for an internal message that never arrives |
| last call needs an output structure | C1.70 equivalents must be judged on their output block, not only the return value |

## What is deliberately not copied

Their addresses, IRQs, memory map, protocol bodies and startup order.  Their
own firmware.md warns: "Do not inherit Z7's addresses, IRQs, memory map,
protocol bodies or startup calls by product-family name."

## Next experiment this enables

The C1.70 AutoOperation module is the `0x1052xxxx` cluster (the dispatcher
`sub_10522EC8(op)` plus its callers); the reference's equivalent entry is a
different address in a different build, so it has to be identified here by shape:
a function that takes a switch bitmask block, a selector and an ack buffer.
Injecting one MENU operation through it, from the app's idle checkpoint, is the
direct analogue of the reference's step 12 and the most promising way to reach a
first GUI event in this firmware.
