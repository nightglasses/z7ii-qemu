# F091 and the PTP command boundary

Scope: F091 is a subcommand received by the MAS `PTPCMD`
service, not a demonstrated internal boot request. In three original-bundle
hardware-breakpoint windows the service is created and runs, but remains in
state 0 waiting for event bit 1. No command reaches its dispatcher. The
downstream session and service-mode gates are also closed, but neither is the
first observed blocking branch.

All addresses below are MAS domain VA. These are guest-software observations,
not a new hardware contract or a GUI-first-frame receipt.

## The concrete call chain

The outer operation table at `0x119e1e74` has 28-byte rows for operations
`0x94fd`, `0x94fe` and `0x94ff`. Dispatch has separate validation and execution
phases; the F0 router previously described as a direct call to the display
handler is the validation phase:

```text
PTP transport callbacks
  -> PTPCMD event wait at 1046383a
  -> state 1 handler 10463a40 / state 2 handler 10463b60
  -> 10463a02
  -> 10463e8e(request at b2a4e0bc)
  -> 10463d70(request.opcode at +6)
  -> 10478ebc(2), table 119e1e74
  -> 10478f3c, validation callback at row +16
  -> 107722e4 / 10772300 / 1077230c
  -> 104c6b14(request), subcommand at +12
  -> 104c6cc4, F091 row 119e483c
  -> 104c6a80, subcommand validation callback at row +16
  -> validation returns to 10463a02
  -> 10463f88 -> 10478faa, execution callback at outer row +20
  -> 107722ea / 10772306 / 10772312
  -> 104c6b9c -> 104c6cc4 -> 104c6a92, subcommand row +20
  -> 10502d0a
  -> 10502cbc(1), local broadcast 040f0020
  -> display-service request 0403001a
```

The firmware names the event object `PTPCMD` at `0x10463c28`.
`10463854` creates its task through `103dc46a`; the task entry is
`10463818`. The caller is `104634a4`, reached from lifecycle wrapper
`1076efd8` when its command argument is 2. Creation and task entry are both
observed; neither is the missing step.

The request layout consumed here is `u32 length, u16 type, u16 opcode,
u32 transaction, u32 parameters[]`. `10463a40` copies the received header
through `10478920`, requires command type 1 and enters the outer dispatcher.
Its event bit 8 selects the command-receive path; `10463b60` also resumes
`10463a02` on bit 8 after a data phase.

F091 is the low halfword of parameter 0 at request `+12`, not the outer
operation code and not an EX host-command id. Its execution handler reads parameter 1 at
`+16`, sends the local arming broadcast and then attempts provider admission
at `104d7e78`. The request format validator `104c6ab4 -> 104e00be` checks
for five parameter slots before subcommand dispatch.

## Earliest observed blocking condition

`10463818` selects an event mask and callback by its state byte at
`0xb188b768`. State 0 selects mask `1` and callback `104639e0`.
At `1046383a` it calls:

```text
103610da(r0=b188b764, r1=1, r2=<result word on guest stack>)
  -> 10104a14(event handle, mask=1, mode=1, result)
```

The live event handle stored at `0xb188b764` is `0x0100011a`.
The task reaches this wait once and does not reach the return point
`1046383e` in the measured window. State 0's callback, both command/data
dispatchers and `10463a02` remain cold. The 32-byte request header at
`0xb2a4e0bc` stays zero.

There are concrete producers for bit 1. `10463976` registers:

| Callback | Condition that posts bit 1 through `10463cea` |
|---|---|
| `104638de`, callback class 0 | event `0x43120000`, `0x4312000a` or `0x4312000e`, with status byte 0 |
| `10463878`, callback class 1 | event `0x4312001e` |

The class-0 frontend path is
`1048aa42 -> 104a73f0 -> 10486bfc -> 10486ed8 -> 104638de` for the default
`PTPSIC` adapter. Other registered frontends include the firmware's `PTPIP`
service; this is a PTP transport callback chain, not MAS SPI4/VR.

The measured registration is intact: `10486c74` initialises the transport
adapters, `10463976` registers the callbacks, the active interface pointer at
`0xb18118b4` is `0xb1811968`, and the callback arrays contain
`104638df` / `10463879` (Thumb pointers). Neither callback fires. Thus the
current earliest gap is an absent transport start/ready notification before
command reception, not a failure inside the F091 display handler.

This does not identify a defective physical transport register or prove that
all possible PTP inputs were tried. The run has no attached PTP host. Inferring
that a normal cold boot must generate F091 internally from the existence of
this command handler was unsupported.

## Two later gates, not yet reached

Even after transport readiness, a bare F091 packet would not establish the
original startup contract:

| Gate | Source | Observed value |
|---|---|---|
| PTP session | `10463e8e` requires `104797aa(0)` for outer operations `94fd..94ff` | `0xb188bc6c = 0` |
| F0 service mode | `104c6b14` calls the predicate at `0xb188bc7c`; only F001 bypasses a false predicate | predicate `104798d8`, reading `0xb188bc80 = 0` |

The standard operation row for `0x1002` at `0x119e149c` selects
`10479838`, which stores the session parameter through `10479824` into
`0xb188bc6c`. The F001 row at `0x119e405c` selects `1047994c`; only after
its preparation and `1047990c(0)` handshake succeed does the store at
`104799a8` set `0xb188bc80 = 1`. F091 with the mode predicate false takes
the router's `0xa004` rejection path. These are source-derived later
conditions, not observed rejection events in the current idle run.

Physical camera verification on an attached Nikon Z 7II confirms both sides
of this gate: with service mode locked, `0x94fd` subcommand `0xF091` returns
`0xa004` exactly as predicted; after subcommand `0xF001` unlocks the service
state, `0xF091` returns `0x2001` (OK) and executes provider admission. See
[Physical camera PTP interface](physical.md) for complete receipts.

## The separate firmware path to display readiness

F091 is not the only caller chain that can set display readiness.
`1062eddc` polls its event group and, at `1062f104`, tests bit
`0x80000000` of the returned flags. If set, it tests/consumes pending slot 3
through `104bf8f8(3)`, whose cell is `0xb201bf6c`. A nonzero slot takes:

```text
1062f116 -> 106957b4(1)
1062f11a -> 10637b48 -> 104d79ea(5)
1062f120 -> 10637b72(1) -> 104d7f16(1)
1062f124 -> 10637b54 -> 104d7e78(...)
```

A focused 45 s hardware-breakpoint run observes one poll return at
`1062ee0c` with flags `0`; the high-bit branch and readiness setter are not
entered. Pending slot 3 reads `0` both at the positive-control entries and
the final snapshot. These are concrete unsatisfied conditions on an independent
firmware path, not proof that no later caller can satisfy them.

The [display producer investigation](../runtime/display.md) identifies the paired writer
at `10540470` / `1054047c` in GUI mode entry `1053fe10`. The single-thread
probe stops earlier at worker 0's battery wait `104386c4`. The subsequent
[execution-profile comparison](../runtime/battery.md) reproduces battery delivery on
the current multi-thread baseline and advances past that wait; its later mode
handoff is blocked by the [rear-panel resource fault](../runtime/handoff.md). The
producer's own gate `b181a6ec` is also zero,
but its test has not been reached.
Implementing an automatic F001/F091 sender would introduce a separate
external-input experiment rather than explain this original path.

## Receipts and scope

Raw material is under `run f091/`:

- `probe.gdb`, `gdb.log`: 60 host seconds from reset; PTPCMD creation/task
  entry and the idle final state.
- `probe2.gdb`, `gdb2.log`: 45 host seconds; registration and the exact
  mask-1 wait, with live callback tables, session/mode and request snapshots.
- `natural.gdb`, `natural.log`: 45 host seconds; independent poll flags,
  high-bit branch and pending-slot checks.
- `gdb.argv.txt`: QEMU argv; `static.json` and `callers.txt`: read-only
  IDB analysis and bounded GNU objdump call-site search.

All runs use the complete unchanged decoded bundle, single-thread TCG and
`-icount shift=6,sleep=off`. Probes use hardware breakpoints and memory reads.
No command packet, guest state, call, PC or firmware byte is injected. Host-time
windows under GDB are not camera-time or performance measurements. No new
device behaviour was changed, so the native suite was not repeated.
