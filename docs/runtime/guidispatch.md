# Application dispatch and EX key transport

This article preserves bounded reverse-engineering observations and their corrections. It is supporting analysis, not the current boot-status page. Use the [GUI chain guide](guichain.md) for the current interpretation and [startup boundary](../validation/frontier.md) for accepted progress. Zero-hit results from historical multi-breakpoint probes are not absence evidence.

## The SysC ladder IS drained - and two earlier "never runs" claims were tool artefacts {#the-sysc-ladder-is-drained-and-two-earlier-never-runs-claims-were-tool-artefacts}

### Correction {#correction}

Breakpoint-based probes in this log reported that the `SysC_Msg` message box's
drain handler `sub_102050C8` and its pump body `0x103D3D78` never executed, and
the whole task-start ladder was therefore stuck in the queue.  That is **wrong**.
The probes were run with several hardware breakpoints and watchpoints in the same
GDB session, and this target silently stops honouring them once the debug
register budget is spent; the later breakpoints simply never fire, which reads
exactly like "the code never runs".

The reliable instrument is QEMU's own execution filter, which costs nothing per
hit:

```text
-accel tcg,thread=single -icount shift=6 -dfilter 0x1020F876+4,0x102050C8+4,0x10205120+4 -d exec
```

Unperturbed, 40 s: 6 posts (`0x10205120`), 6 drains (`0x102050C8`), 11 dispatches
(`0x1020F876`), interleaved as post -> drain -> dispatch.  A 60 s run of the same
kind gives 44 pump-body executions.  So the message box works: every queued
record is taken and dispatched.

### What the same instrument says about the rest of the chain {#what-the-same-instrument-says-about-the-rest-of-the-chain}

60 s, no GDB attached:

| function | role | executions |
|---|---|---|
| `0x1020F876` | SysC dispatcher | 10 |
| `0x10569C76` | app graphic-resource create | 47 |
| `0x1044F6E0` | camera-control device state machine | 0 |
| `0x104D7E78` | display provider admission | 0 |
| `0x10568F60` | EGL present | 0 |

So the boot does drain and dispatch its ladder, the application does create its
graphic resources, and the display side is still untouched.  Every earlier
"zero" in this log that came from a multi-breakpoint GDB session has to be
re-measured with `-dfilter` before it is believed.

### Reliable timeline, 60 s, no GDB attached {#reliable-timeline-60-s-no-gdb-attached}

`-dfilter` only, so every number below is a real execution count of the original
firmware:

| address | role | executions |
|---|---|---|
| `0x10201804` | SysC boot-ladder entry | 3 |
| `0x1020F876` | SysC dispatcher | 10 |
| `0x1020F8D0` | controller-handler call site | 4 |
| `0x1044E362` | camera-control task-table entry #1 | 1 |
| `0x1044F6E0` | camera-control device state machine | 0 |
| `0x1054BA98` | GUI message handler | 77 |
| `0x10569C76` | app graphic-resource create | 49 |
| `0x104D7E78` | display provider admission | 0 |
| `0x104DC804` | display owner | 0 |
| `0x10568F60` | EGL present | 0 |
| `0x106EE04A` | GPU/resource-manager config | 5 |
| `0x104C0F20` | host-facing service send | 0 |

Reading: the boot ladder runs and dispatches (10 dispatches, 4 controller calls),
the application is alive (77 GUI messages, 49 graphic resources), the GPU
resource manager is configured (5), and the display slice is completely idle -
no admission, no owner, no present - while the host-facing send API is never
used.

### What still has to be re-measured {#what-still-has-to-be-re-measured}

These earlier claims came from multi-breakpoint GDB sessions and are **not**
trustworthy yet; each needs a `-dfilter` count or an unperturbed dump:

- the display provider query reply being `0` for handle `0x010001C2`
  (the handle value itself *is* present in RAM at 120 s: `0xB181C560`,
  `0xB1D6AD44`, `0xB2C4AAE8`, `0xB2C4AB54`, `0xB2C59030`, `0xB2C61E38`);
- "the worker blocks on kernel object `0x010001BE`" (handle present at
  `0xB181D9D8`, `0xB1D6A620`, `0xB2C3F75C`, ...);
- every "no reference" scan that used a breakpoint to decide reachability.

The rule for this workspace is now: reachability questions are answered with
`-dfilter`; GDB breakpoints are only used to read a value at a known execution,
with at most two or three armed at a time.

### The display start runs late, and its reply is a flags word {#the-display-start-runs-late-and-its-reply-is-a-flags-word}

`-dfilter` over 40 s with three addresses, read in log order:

```text
0x10361082  object-create helper          586 executions
0x1062EDDC  display service start           1 execution, after all 586
0x10637B54  provider-admission caller       0
```

so the display service start is not an early victim of a missing object - it runs
*after* every object the boot creates.  A single-breakpoint read at the
instruction following its query (`0x1062EE0C`) returns `r4 = 0`.

That `r4` is not a handle: the code immediately tests it as a bitfield
(`LSLS R0, R4, #31; BEQ` then `LSLS R0, R4, #2; BPL`) and skips whole blocks when
the bits are clear.  So the display service asks for a **state/capability word**
and receives 0.

180 s unperturbed, same instrument:

| address | role | executions |
|---|---|---|
| `0x1062EDDC` | display service start | 1 |
| `0x10637B54` | provider admission caller | 0 |
| `0x104D7E78` | provider admission | 0 |
| `0x104DC804` | display owner | 0 |
| `0x10568F60` | EGL present | 0 |
| `0x1046BF0C` | display banks start | 0 |
| `0x10569C76` | app graphic-resource create | 45 |
| `0x1020F876` | SysC dispatcher | 13 |

Running three times longer changes nothing downstream of the display start, so
the missing input is the word that start queries, not timing.

### Instrument budget: one hardware breakpoint per run {#instrument-budget-one-hardware-breakpoint-per-run}

Two independent observations show this target honours roughly **one** hardware
breakpoint or watchpoint per GDB session: with a watchpoint armed on the display
context global, a breakpoint on the query reply never fires, while either alone
works.  Every multi-instrument probe earlier in this log therefore has an unknown
subset of its breakpoints silently inactive.

Rules adopted here:

- execution counts and reachability: `-dfilter` only (unlimited, unperturbed);
- a value at a known execution: a **single** hardware breakpoint or watchpoint
  per run;
- ordering between two functions: `-dfilter` on both entries and compare log
  line numbers, never two breakpoints.

### What the display start actually waits on {#what-the-display-start-actually-waits-on}

Static read of the helper chain behind that `r4`:

```text
sub_10634050()      = the constant 0xB181C564
sub_106340D4(a1, a2) maps a1 to 1/2/3 by comparing against 0xB181C55C,
                     0xB181C55C+8 and 0xB181C55C+0x10, logs through
                     nullsub_248(87, index, a2, 0), and calls
sub_10361116(obj, ..)  -> handle = *(u32 *)obj; sub_10104A14(handle, 1, ..)
                       which is the one-SVC microkernel wait
```

so the display service does not "query a reply" - it **waits on its context
object** and then tests the returned event word as a bitfield (bit 0 at
`0x1062EE10`, bit 29 at `0x1062EE22`).  The value read there is 0, i.e. no
event.

The context object itself is created: a watchpoint on `0xB181C560` catches the
store of handle `0x010001C2` from PC `0x1036109E` (the generic
`sub_10361082` object-create helper), and `sub_10634050()` returns
`0xB181C564`, the handle slot eight bytes into that block.

So the remaining dependency is one specific event: whatever should set bit 0 /
bit 29 of the display context's event word.  Every documented display step -
provider admission, ready setter, owner, bank start, present - is downstream of
that single event.

### The cold half of the firmware, measured with one instrument {#the-cold-half-of-the-firmware-measured-with-one-instrument}

60 s each, `-dfilter` only, no GDB.  Everything below is a real execution count:

| address | role | executions |
|---|---|---|
| `0x1054BA98` | GUI message handler | 77 |
| `0x10569C76` | app graphic-resource create | 49 |
| `0x1020F876` | SysC dispatcher | 10-13 |
| `0x106340D4` | display context wait | 10 |
| `0x1052E3AE` | display event broadcast | 1 |
| `0x10637B54` | provider admission caller | 0 |
| `0x10502D0A` | F091 display service handler | 0 |
| `0x10502CBC` | arming IPC send inside F091 | 0 |
| `0x104BDC38` | arming guard (`cmp r5,#0x0403001A`) | 0 |
| `0x1053E19A` | GUI event dispatcher | 0 |
| `0x105424E8` | GUI step | 0 |
| `0x104BD99E` | GUI event setter | 0 |
| `0x104C6B14` | service request router | 0 |
| `0x104C0F20` | host-facing service send | 0 |
| `0x104C0F96` | service deliver | 0 |

The application half is warm - messages are handled, graphic resources are
created - and every activation path is cold: no GUI event is ever set, no GUI
dispatcher step runs, no service request is routed or sent, the F091 display
handler never runs, so no arming IPC is sent, the display context never receives
an event, and admission / owner / bank start / present all stay at zero.

That is the natural-boot answer in its current, reliably measured form: this
bundle's application never leaves its own initialisation-and-message phase,
because nothing ever raises the first GUI event.

### The AppC path runs, and still raises no GUI event {#the-appc-path-runs-and-still-raises-no-gui-event}

60 s, `-dfilter` only:

| address | role | executions |
|---|---|---|
| `0x10294318` | AppC message link | 22 |
| `0x1020CA7A` | AppC stage handler | 20 |
| `0x103608AA` | event-0x39 veneer caller | 0 |
| `0x10695450` | event-0x39 poster | 0 |
| `0x10478F3C` | AppC send wrapper | 0 |

so AppC messages are delivered and staged twenty times, and the event-0x39 post
inside the stage handler is never taken.  That matches the gate read out earlier:
the post needs the message's class byte to be 7 and the application context state
at `ctx+0x330` to be 1..4.

A single watchpoint on that state byte (`0xB1818EF4`, from the `LDRB.W R1,[R5,#0x330]`
at `0x1020CD10` with `ctx = 0xB1818BC4`) records **no write at all** in 60 s, so
the byte is never advanced from its initial 0 by any code in this boot.

### The chain, stated once, with every link measured {#the-chain-stated-once-with-every-link-measured}

```text
app messages + graphic resources          warm   (77, 49)
AppC stage handler                        warm   (20)
AppC send wrapper                         0      -> nothing leaves the app
GUI event setter                          0      -> no GUI event is ever raised
GUI event dispatcher                      0
service router / send / deliver           0      -> no service request exists
F091 display handler                      0      -> no arming IPC is sent
display context wait                      10     -> always replies "no event"
provider admission / owner / bank / present 0
GPU resource-manager config               5      -> configured, never submits
application context state byte            0 writes
```

The whole display and service half of this firmware is downstream of a GUI event
that never gets raised, and the state byte the one posting site tests is never
written by anything.

### Correcting the state-byte address, and what actually gates the post {#correcting-the-state-byte-address-and-what-actually-gates-the-post}

An earlier paragraph put the application state byte at `0xB1818EF4`
(`ctx = 0xB1818BC4` plus `0x330`).  That is wrong.  A single breakpoint on the
state setter `sub_1020DEAE` shows the context it is called with:

```text
SETTER ctx=0xb1818bd0 a2=0x2 a3=0x4 a4=0x3 lr=0x102a6ab9
```

so the context is **`0xB1818BD0`**, the same value the docs already quote for the
`AppC_Msg` argument, and the bytes the setter writes are
`ctx+0x32C`, `ctx+0x330` (when `a2 == 0`), `ctx+0x332` (`a2 == 1`) and
`ctx+0x334` (`a2 == 2`).  The gate byte that `LDRB.W R1,[R5,#0x330]` at
`0x1020CD10` reads is therefore **`0xB1818F00`**.

Measured:

- the setter runs once, with `a2 = 2`, so it writes `ctx+0x334 = 3`
  (`0xB1818F04`) - not the gate byte;
- a single watchpoint on `0xB1818F00` records **no write at all** in 60 s;
- a single breakpoint on the gate instruction `0x1020CD10` **never fires**, so
  the stage handler never even reaches that test.

The setter itself is entry 28 of a 64-entry Thumb handler table at
`0x123B2B8C`; `-dfilter` shows entry 0 running 3 times, entry 4 four times,
entry 7 zero and entry 28 once, so the table is live and this boot simply calls
the entries its message classes select.

### Kernel task primitives, counted {#kernel-task-primitives-counted}

60 s, `-dfilter`:

| wrapper SVC | role | executions |
|---|---|---|
| `0x101044FC` | create task (`0x80010100`) | 4 |
| `0x10104964` | start task (`0x80030200`) | 5 |
| `0x10104358` | set task priority | 17 |
| `0x101048DC` | post to a message queue | 106 |
| `0x1010471C` | wait on a message queue | 231 |

so task creation and starting are both happening; the message plumbing is busy.
The application's own event posting, by contrast, is completely absent.

### The stage handler is called twenty times with the same synthetic message {#the-stage-handler-is-called-twenty-times-with-the-same-synthetic-message}

A single breakpoint on the stage-handler entry `0x1020CA7A`, printing the message
fields, records twenty identical invocations:

```text
MSG msg=0xb1818bd0 b0=0x1e b1=0 cmd=0x1 p0=0 p1=0
MSG msg=0xb1818bd0 b0=0x1e b1=0 cmd=0x1 p0=0 p1=0
... (twenty times)
```

The message is the application context itself, its class byte (`+1`) is 0 rather
than the 7 the GUI posting site requires, and its command word (`+0x10`) is 1,
which the handler's `- 0x03000000` switch sends straight to the default case.

So the application is not processing real AppC messages at all - it is re-entering
its stage handler with one constant record - and the AppC send wrapper
`0x10478F3C` never executes (measured 0).  Nothing in this boot produces an AppC
message, therefore nothing selects the class-7 path, therefore no GUI event is
raised and the entire display half stays cold.

That is the current, reliably measured answer to "why does no frame appear
naturally": the application's inbound message channel is empty, so its state
machine never advances past its initial stage.

### The AppC publisher chain, and why nothing ever publishes {#the-appc-publisher-chain-and-why-nothing-ever-publishes}

The delivery wrapper `sub_10478F3C` has exactly one caller, and the chain up to
it is short:

```text
sub_10463A02(a1)         build the message at 0xB2A5070C and publish it
  -> sub_10463E8E(msg)   read class = *(u16 *)(msg+6), look it up with
                         sub_10463D70(class); if no subscriber: drop
                         otherwise deliver with sub_10478F3C(subscriber, msg)
```

60 s, `-dfilter`, every address in that chain:

| address | role | executions |
|---|---|---|
| `0x10463A02` | AppC post API | 0 |
| `0x10463A40` | publisher A | 0 |
| `0x10463B60` | publisher B | 0 |
| `0x10463E8E` | publish / class lookup | 0 |
| `0x10463D70` | subscriber lookup | 0 |
| `0x10478F3C` | deliver to subscriber | 0 |

So the AppC subsystem is not failing - it is never entered.  Both publishers are
registered as callbacks in a table at `0x12425700`:

```text
0x12425700  0x00000001
0x12425704  0x104639E1   0x000183DA
0x1242570C  0x10463A41   0x000101DA
0x12425714  0x10463B61   0x00010FDA
0x1242571C  0x10463BD5   0x00000000
```

i.e. publisher A is the handler for message class `0x83DA`, publisher B for
`0x101DA`, and a third handler for `0x10FDA`.  Nothing in this boot sends a
message with any of those classes, so none of the publishers ever runs.

### The answer, in one sentence {#the-answer-in-one-sentence}

The application runs, handles messages and creates graphic resources, but its
inbound AppC channel never carries a real message - the classes that would drive
its publishers (`0x83DA`, `0x101DA`, `0x10FDA`) are never sent - so its stage
handler only re-enters with one constant record, the class-7 GUI message that
would raise the first GUI event is never selected, and everything downstream
(service router, F091 display handler, arming IPC, display context event,
provider admission, owner, bank start, present, GPU submission) stays cold.

### The operation (AutoOperation) module is cold too {#the-operation-autooperation-module-is-cold-too}

The published Z7 model raises its first UI by injecting one MENU **AutoOperation**
call into the application object from the app's idle checkpoint - not by waiting
for an internal event - so the same module is the natural next suspect here.

60 s, `-dfilter`:

| address | role | executions |
|---|---|---|
| `0x10522EC8` | operation dispatcher (`op` switch) | 0 |
| `0x1052329C` | its first in-module caller | 0 |
| `0x10526B7E`, `0x10527378`, `0x1052830C` | other in-module callers | 0 |

So no operation is ever dispatched in this boot: keys reaching the pad cannot
become operations because the operation module never runs, which is consistent
with every key experiment in this log producing nothing.

See [`../reference/assistedboot.md`](../reference/assistedboot.md) for the
reference recipe and which of its properties transfer.

### Poking one AutoOperation tick {#poking-one-autooperation-tick}

Following the reference method (see `../reference/assistedboot.md`), one
operation tick was invoked from outside with the call harness, with the execution
filter counting the aftermath:

```text
ASSIST_CALL entry=0x1052329d reply=0 status=returned     (the AO poller)
```

Counts afterwards, same run:

| address | role | executions |
|---|---|---|
| `0x1052329C` | AO poller (our call) | 1 |
| `0x10522EC8` | AO dispatcher, one per operation id | 130 |
| `0x1020CA7A` | AppC stage handler | 30 (20 without the poke) |
| `0x1054BA98` | GUI message handler | 67 |
| `0x104BD99E` | GUI event setter | 0 |
| `0x1053E19A` | GUI event dispatcher | 0 |
| `0x10695450` | event-0x39 poster | 0 |
| `0x10637B54` | provider admission caller | 0 |
| `0x10568F60` | EGL present | 0 |
| `0x1044F6E0` | camera-control state machine | 0 |

So the operation module can be driven from outside - the poller evaluates every
operation id, 130 dispatcher calls - and it still raises no GUI event, because no
switch state is present for any operation to act on.  The next input the firmware
wants is therefore the switch state itself, not another tick.

Note what this also shows: an assisted call can move numbers in the warm half
(67 GUI messages, 30 stage entries) while the measured cold half stays exactly at
zero, so "nothing changed" is a real result and not an artefact of the poke.

### AutoOperation internals read so far {#autooperation-internals-read-so-far}

- `sub_1052329C` is the poller: `for (id = 0; id < 0x40; id++) sub_10522EC8(id);`
- `sub_10522EC8(id)` is the per-operation decision: it takes the operation's
  state block `off_1094AA24[id]`, picks one of three field groups depending on
  the mode globals (`0xB202C1F0`, `0xB202C170`, `0xB202C17C`), reads
  `*(u32 *)(block + {4,16,28} + 4 * index)`, and switches on the operation id.
  It also consults `0xB202BE70` and `0xB181DA91`.
- `off_1094AA24` is a table of RAM pointers (op 0 -> `0xB181DDBC`,
  op 1 -> `0xB1821374`, ...).  In a 25 s dump each block looks like
  `{0, 0x00FFFFFF x9, 0x0000000N, pointer}` - i.e. mostly "unset" markers with a
  small per-operation value at `+0x28` (1 for op 0, 8 for op 1).

So an operation becomes active when its state block says so; the switch state
that would set those words arrives through the key path, which is the thing the
EX-side investigation is now looking for.

### The EX key path is alive - the assumption that keys never arrive was wrong {#the-ex-key-path-is-alive-the-assumption-that-keys-never-arrive-was-wrong}

Static map of the EX side (base `0x08000000`, from the EX image): pad table
`0x0804EF8F` (17 pad codes), GPIO read `0x08043ACC`, three scanners
`0x0803DD00` / `0x0803E000` / `0x0803E51C`, debounce `0x0803E428`, key
publication `0x0803E12C` (event ids `0x02030083`, `0x0203008C`, `0x02030081`),
consumer = switch manager `0x0803D290`.

60 s, `-dfilter`, EX addresses:

| address | role | executions |
|---|---|---|
| `0x0803DDBC` | "SwichCy8" timer callback | 9645 |
| `0x0803E098` | EX event dispatcher | 9786 |
| `0x0803DD00` | scanner A (6 pads) | 146650 |
| `0x0803E000` | scanner B (6 pads) | 861 |
| `0x0803E12C` | key publication | 9551 |
| `0x0803D290` | switch manager (consumer) | 10530 |
| `0x080325B8` | application-cycle caller in the static chain | 0 |

So the EX firmware scans pads, debounces, publishes key events and merges them
in its switch manager on every cycle.  Keys are not being lost inside the EX.
Whatever is missing sits between that merged state and the MAS - the next
question for the EX-side analysis.

### Method caveat: the execution filter only counts TB starts {#method-caveat-the-execution-filter-only-counts-tb-starts}

`-dfilter` reports a TB only when that exact address starts a TB, so an address
*inside* a function reads 0 no matter how often the code runs.  Re-checking the
one address in the cold list that was not a function entry:

```text
0x104BDC38 (arming guard, inside sub_104BDB42)   filter: 0
0x104BDB42 (its function entry)                  filter: 1
```

so the guard's function does run once.  All the other "0" entries in the tables
above were verified to be real function entries, and therefore stand.

### The EX key path runs, but its report to MAS never fires {#the-ex-key-path-runs-but-its-report-to-mas-never-fires}

Static work on the EX image (base `0x08000000`) found one and only one
off-chip forwarder for the merged switch state:

```text
0x0803D1CC  LSLS R0,R4,#0x1F   ; event bit 1 of the 40-unit "SwMngCyc" timer
0x0803D1D4  LDR R1,[R0,#4]  / CBZ skip     ; 0x20011670 must be non-zero
0x0803D1D8  LDR R1,[R0,#0xC]/ CBZ skip     ; 0x20011678 must be non-zero
0x0803D1DC  LDR R1,[R0]     / CBZ skip     ; 0x2001166C must be non-zero
0x0803D1F2  BL 0x0803D5FC                  ; the forwarder
```

and `0x0803D5FC` copies the 120-byte merged state to `0x200115F4`, fills a
24-byte record descriptor at `0x200115D8` and calls the transport hook
`*(0x20011490)`.

Measured with `-dfilter`:

| address | role | executions |
|---|---|---|
| `0x0803D290` | switch manager (merges key records) | 9989 |
| `0x0803D5FC` | switch-state forwarder to MAS | **0** |

So the EX scans, debounces and merges key state, and never reports it to the
MAS.  The static reading is that `0x20011670` and `0x20011678` have no writer
anywhere in the image, i.e. the forwarder's gate is never satisfied; the
transport hook `0x20011490` is installed only by `0x0803CEF0`, which has no
reference in the ROM either.  Both would have to come from the decompressed init
record.

### Caveat: EX RAM is not readable through the emulator's debug paths {#caveat-ex-ram-is-not-readable-through-the-emulator-s-debug-paths}

EX RAM lives in the EX device's own address space (`ex.address-space`, SRAM at
`0x20000000`, 256 KiB).  It is **not** part of the machine address space, so:

- `pmemsave` at `0x20011xxx` silently returns zeros (a dump of that region is
  meaningless, not evidence);
- gdb cannot read it from any thread: `Cannot access memory at address 0x20011670`
  for all five vCPUs, because the gdbstub resolves memory in the system space.

Consequences for this workspace: EX memory values must either come from a
purpose-built debug hook inside the device, or not be claimed at all.  Execution
counts for EX code addresses are unaffected and remain valid.

### Reading EX RAM with guest code, and what it shows {#reading-ex-ram-with-guest-code-and-what-it-shows}

EX SRAM is unreachable from the debugger, but the original firmware can read it
for us: call the EX image's own `LDR R0,[R0]; BX LR` gadget (`0x08000646`) with
the address in `r0` and return to an existing `B .` loop (`0x0800025E`) through a
hardware breakpoint.  The read is performed by EX instructions, not by the
debugger.

```text
0x2001148C channel                        2
0x20011490 transport hook                 0x08032001   installed
0x20015DD4 peer lane hook                 0x08032001   same function
0x2001166C gate flag                      1
0x20011670 gate flag                      1
0x20011678 gate flag                      1
0x20011688 already-sent                   0
0x200114E6 merged switch state            0
0x2001155E last-sent mirror               0
0x2000E738 transport token                0xFFFF  idle
```

**This corrects the static prediction.**  The analysis expected `0x20011670` /
`0x20011678` to have no writer and the forwarder to be dead by those flags.  They
are both 1, the transport hook is installed and the transport is idle, so the
forwarder's own gate is satisfied.

What is actually missing is one level up:

| address | role | executions |
|---|---|---|
| `0x0803D044` | switch-manager registration | 31 |
| `0x0803D094` | "SwMngCyc" timer callback | **0** |
| `0x0803D180` | switch-manager event handler (starts the timer at `0x0803D22C`) | **0** |
| `0x0803D5FC` | switch-state forwarder | **0** |
| `0x080325B8` | EX application cycle (event bit `0x02` in the table at `0x0804DF68`) | **0** |

The handler starts its own timer, and the handler is driven by event bit 2 of the
EX application cycle.  That cycle never runs, so the switch manager is never
enabled, its timer never ticks, and no key state is ever reported to the MAS.

### The EX waits for one lane frame that the MAS never sends {#the-ex-waits-for-one-lane-frame-that-the-mas-never-sends}

Inbound EX traffic, 60 s, `-dfilter`:

| address | role | executions |
|---|---|---|
| `0x08027E58` | inbound host-command dispatcher (0x65..0x8A) | 65 |
| `0x0802A57C` | 0x65 handler | 30 |
| `0x0802A888` | 0x66 handler | 29 |
| `0x08032000` | `McuCm` lane enqueue (EX -> peer) | 52 |
| `0x08031F28` | `McuCm` send task | 37 |
| `0x080332A8` | `McuMg` mask dispatcher | 1 |

and on the McuMg side:

| address | role | executions |
|---|---|---|
| `0x080322F8` | McuMg registration (timers "McuMgCyc"/"McuMgCy2") | 7 |
| `0x080332F4` | McuMg message router | 1 |
| `0x080341B4` | "McuMgCyc" 10-tick timer callback | **0** |
| `0x0803350C` | the only poster of event bit 0x02 | **0** |
| `0x080325B8` | EX application cycle (bit 0x02 handler) | **0** |

The router `0x080332F4` dispatches inbound messages by their payload's first byte
through the table at `0x0804E15C`; id `0x04` is the only path to
`0x0803350C`, which is the only poster of the McuMg bit `0x02`.  So the EX
application cycle starts only when the peer sends a frame whose payload begins
with `0x04`.

Measured: the lane carries the 0x65/0x66 host commands (30 + 29) and the EX
enqueues 52 frames the other way, but **no frame with payload id 0x04 ever
arrives** - the router runs exactly once, and the McuMg timers never tick.

So the two MCUs are in a mutual wait in this boot: the MAS never sends the EX the
frame that enables its key/switch cycle, and the EX therefore never reports key
state back.  Both sides are otherwise alive - the lane transports frames in both
directions and the EX send task runs.

### Where the bootstrap actually closes {#where-the-bootstrap-actually-closes}

```text
MAS app starts, but its own AppC publish chain never runs (measured 0)
  -> nothing on the MAS ever raises an app event
  -> the MAS-side sender that would emit the McuMg "id 0x04" frame never runs
  -> EX application cycle never starts, key/switch cycle never enables
  -> EX reports no key state, so the MAS gets no input even if a key is pressed
  -> no GUI event, no service request, no display arming, no GPU submission
```

Both ends are waiting on the other, and the first move belongs to the MAS
application, which never leaves its initial stage.

### The McuMg object is alive and simply never receives its message {#the-mcumg-object-is-alive-and-simply-never-receives-its-message}

Read through the EX gadget (firmware reads its own SRAM):

```text
0x200138D4  0x00010001      module id/mode word
0x200138F8  25              object queue handle
0x200138FC  0x080214D9      its event task body (0x080214D8)
0x20013924  8               "McuMgCyc" timer object
0x20013938  9               "McuMgCy2" timer object
0x20013A68  0               the 106-byte id-4 message buffer: never written
```

and the MAS side of the same lane is busy:

| address | role | executions (60 s) |
|---|---|---|
| `0x10243468` | MAS TX descriptor/FIFO to the EX | 2320 |
| `0x10243400` | MAS TX neighbour | 1764 |
| `0x1034FBAE` | MAS lane enqueue | 7577 |
| `0x104C0F20` | host-facing service send | 0 |

So: the EX McuMg object, its queue and its event task all exist, the lane
transports thousands of MAS frames, and the one message this module needs
(payload id `0x04`) is never among them.

### There is no relay in the MAS image, and the inner image is not it either {#there-is-no-relay-in-the-mas-image-and-the-inner-image-is-not-it-either}

A whole-image search of the MAS side (base `0x10020000`) for a sender of the EX
`McuMg` ladder frames found none:

- the MAS<->EX link is the "ExMcu" module (`0x10206180` dispatcher,
  `0x1020624E` init, comm state machine `0x1020B7E8`, frame assembly `0x1020BDB6`);
- the frame assembly produces an 8-byte header + payload (<=240 B) + 8-byte tail
  and carries **no command-id byte** and no `0x6A` size;
- the ExMcu payload descriptor (`0xB1D1A878`, length at `+0x3A`, pointer at
  `+0x40`) is written only by the comm init and the debug CLI;
- the sysc command area (`0x1020E000..0x10214000`) makes **zero** calls into the
  ExMcu module, and the sysc controller table has no ExMcu controller;
- the ids the EX router expects (`{1,4,6,8,0xA,0xC,0xD,0x10}`) are a subset of
  the MAS *sysc* command ids, i.e. the EX side is a mirror of the MAS ladder, and
  the mirror relay is absent from the image.

The other place such a relay could have lived - the 2 MiB MAS inner image that is
copied to `0xB1000000` and never started - is a **Bluetooth stack** (OBEX, GATT,
`IpcuDriver.c`, `Arm926IpcuService.c`), not an EX relay.  So that hypothesis is
closed too.

### Poking the EX cycle does nothing either {#poking-the-ex-cycle-does-nothing-either}

Calling the EX `McuMg` 10-tick timer callback `0x080341B4` eight times from
outside (EX vCPU, registers only, returning to the image's own `B .` loop) returns
0 every time and changes nothing downstream:

```text
0x0803D094 (SwMngCyc timer cb)  0
0x0803D180 (switch-manager handler) 0
0x0803D5FC (switch-state forwarder) 0
0x0803350C (event bit 0x02 poster)  0
0x080325B8 (EX application cycle)   0
```

so the cycle is not waiting on a tick; it is waiting on the peer frame.

### Where that leaves the natural-boot question {#where-that-leaves-the-natural-boot-question}

Both MCUs boot into a mutual wait that neither image can break from the inside:
the EX application cycle needs one lane frame that no code in the MAS image
sends, and the MAS application's own message chain is never fed, so it never
emits that frame.  This matches the published Z7 model's own conclusion that
these firmwares do not reach a UI unassisted.

Two open possibilities remain, both narrowing the problem rather than blocking it:

1. the EX `McuCm` peer is not the MAS - "McuCm"/"McuMg" only say "MCU"; the SLA
   component has not been searched for this relay;
2. the relay belongs to a stage the MAS never reaches, which is the assisted-boot
   territory the reference project occupies (see `../reference/assistedboot.md`).

### Z7-style forcing on this firmware: what each poke actually moved {#z7-style-forcing-on-this-firmware-what-each-poke-actually-moved}

Following the reference model's method (call original routines from a checkpoint),
each candidate was poked from outside and the downstream counters measured with
`-dfilter` in the same run.

| poke | immediate effect | downstream |
|---|---|---|
| MAS AutoOperation poller `0x1052329C` | 130 operation evaluations | GUI event setter 0, dispatcher 0, poster 0 |
| MAS display provider admission `0x10637B54` x24 | admission `0x104D7E78` runs 24 times | ready setter `0x104D7F16` 0, owner 0, banks 0, present 0 |
| EX `McuMg` 10-tick timer callback `0x080341B4` | returns 0 each time | switch-manager timer 0, forwarder 0, cycle 0 |

So the routines themselves are callable and they do run, and none of them
produces a frame: each one stops at the next input that the boot never supplies.
The admission path is a good example - it admits on demand, but the ready setter
and owner that follow belong to the display service's own state machine, which is
still waiting on its context event.

The practical consequence is that "imitate the reference" on this firmware needs
a real input at the *bottom* of the chain, not a call in the middle: the
reference's ladder works because its firmware exposes a single app object that
every call can advance; this firmware exposes no such object reachable without
the missing peer frame.

### SLA: what the image does and does not contain {#sla-what-the-image-does-and-does-not-contain}

The SLA component (`4_eg1840_sla_01700.bin`, 8,834,368 B = 0x86CD40) loads at
**0x10020000**, file offset 0, one segment, 7488 functions.  Verified against
`docs/startup/sla.md`: the ARM vector table sits at `0x10034100` (`18f09fe5
da0300ea ...`) and the ROM section descriptors at `0x10030400`.

It carries a **service directory**: 159 records of `{u16 service_id, name}` from
`0x10871124`, including

| id | name |
|---|---|
| `0x0304` | `ExMcuService` |
| `0x0429` | `ExMcuCommandServ` |
| `0x0800` | `BExMcuProtocolSer` |

and the directory base `0x1087103C` is referenced exactly once in the whole
image, from `0x10298628`, inside a table of 16-bit pairs at `0x10298600`.

That is a **directory entry, not an implementation**: the MAS image carries the
same three names, so their presence proves the SLA knows the services, not that
it hosts them.

Two searches for the frame builder came back negative:

- no function in `0x10020000..0x10700000` contains both a `#0x6A` size-like
  immediate and a `#0xF0` mask (the shape the MAS ExMcu assembly uses); the six
  functions that contain both use `0x6A` as a struct offset or an unrelated
  constant;
- the SLA's long pointer tables (`0x10038DFC`, `0x10169E48`, `0x103AF550`,
  `0x10865D74`, `0x1086B3B8`) do not match the 159-entry service directory, so
  there is no obvious parallel service-handler table to index into it.

So the relay has not been found in the MAS image, in the never-started inner
image (a Bluetooth stack), or in the SLA by shape.  Before spending more on
protocol archaeology, the cheaper discriminator is on the EX side: the EX
`McuMg` router ran exactly once early in the boot, so *some* peer frame did
arrive.  Identifying which frame that was says who the real peer is.

### The one frame the EX does receive is a version announcement {#the-one-frame-the-ex-does-receive-is-a-version-announcement}

The EX `McuMg` router `0x080332F4` runs exactly once per boot, so its single
invocation can be captured.  With one hardware breakpoint on the router (armed
before the guest starts) and the EX read gadget for the memory:

```text
ROUTER hit=1 msg=0x20002108 lr=0x08031F17
  msg+0  28            message length
  msg+4  0x20011964    payload pointer
  msg+8  0
  msg+12 0x20017B28

payload  {0x00000513, 0x372E3130, 0x32302E30, 0}
bytes    13 05 00 00  30 31 2E 37  30 2E 30 32
         id=0x13 len=0x05  "01.7"      "0.02"
```

So the peer does talk to the EX, and what it sends once is a **version
announcement**: id `0x13`, followed by the string `01.70.02` - the firmware
version.  The router's table (`0x0804E15C`) handles ids
`{1,4,6,8,0xA,0xC,0xD,0x10}` only, so this frame is not a ladder command and is
dropped without effect.

That refines the earlier statement: the lane is not silent, and the peer is not
absent - the peer sends exactly one thing, a version string, and never any of
the ladder ids the EX application cycle waits for.

### The EX also transmits, and the first message is a short id-1 record {#the-ex-also-transmits-and-the-first-message-is-a-short-id-1-record}

The `McuCm` channel enqueue `0x08032000` was captured at its first two calls
(one hardware breakpoint armed before the guest starts):

```text
hit 1  r0=0x20013C20  r2=0x1C  lr=0x08033EE6   (caller in the McuMg module)
hit 2  r0=0x20015F18  r3=0x20015E36  lr=0x08025486   (caller in the 0x08025xxx record module)
```

reading `r0+0` gives `0x001C0001`, i.e. `{u16 id = 1, u16 length = 0x1C}` — the
same 28-byte size as the incoming version frame.

So the lane carries a short exchange at boot: the peer announces `id 0x13`
(`"01.70.02"`), the EX enqueues an `id 1` record of the same length, and after
that the ladder ids never appear in either direction.

That is the shape of a version handshake that completes and then goes quiet,
which is why every later stage waits forever.

### Answer to "why is the ladder never sent" {#answer-to-why-is-the-ladder-never-sent}

Every running image was checked for a sender of the EX ladder frames.

| image | loaded at | result |
|---|---|---|
| MAS (`0x10020000`, Cortex-A7 x2) | runs | no sender: the ExMcu module's frame assembly has no command-id byte, its payload descriptor is written only by comm init and the debug CLI, the sysc layer never calls into it, and there is no ExMcu controller |
| EX (`0x08000000`, Cortex-M4) | runs | receives one version announcement (`id 0x13`, `"01.70.02"`), replies `id 1`, then waits |
| MAS inner image (`0xB1000000`) | copied, never started | a Bluetooth stack (OBEX/GATT/`IpcuDriver.c`), not a relay |
| SLA (`0x10020000` diagnostic, Cortex-A7 x2) | runs | carries the service directory naming `ExMcuService` / `ExMcuCommandServ` / `BExMcuProtocolSer`, but no 106-byte frame builder of the expected shape and no parallel service-handler table |
| VR (`3_vr1840`, Renesas **RX**) | **not instantiated** | modelled as its own machine `nikonvr`, not part of `z7ii-mas-diagnostic` |

So the ladder ids the EX waits for have exactly one candidate source left: a
component this machine does not run.  The five-vCPU topology is
two MAS Cortex-A7, two SLA Cortex-A7 and one EX Cortex-M4, and the bundle's
fifth component is a Renesas RX MCU image that exists only in the separate
`nikonvr` board.

That closes the natural-boot question in a measurable way: the EX key/switch
cycle is enabled by a peer frame that none of the components this machine runs
can produce, and the MAS image provably lacks the relay.  The firmware is not
failing - it is waiting for the peer that this machine does not instantiate.

### The VR link is wired, and the VR cannot boot {#the-vr-link-is-wired-and-the-vr-cannot-boot}

Following the repo's documented composition rule (ARM and RX are separate QEMU
targets; the full chain must use the peer transport, not a mixed-ISA process),
the camera and the VR were run as two processes with the existing peer devices:

```sh
qemu-system-rx  -machine nikonvr -bios <bundle> -display none -nodefaults \
  -chardev socket,id=s0,path=/tmp/nikon-vr.sock,server=on,wait=off \
  -device nikon-peer,id=wire,chardev=s0 \
  -device nikon-vr-peer,id=vrpeer,peer=wire,peripherals=/machine/vr-peripherals

qemu-system-arm -machine z7ii-mas-diagnostic,bundle=<bundle> ... \
  -chardev socket,id=p0,path=/tmp/nikon-vr.sock \
  -device nikon-peer,id=wire0,chardev=p0 \
  -device nikon-spi-peer,peer=/machine/peripheral/wire0,spi=/machine/spi4,\
gpio=/machine/gpio,pin=177
```

The transport works and the camera process runs, but nothing downstream moves
(60 s: display admission 0, GUI dispatch 0, event-0x39 poster 0, AO dispatcher 0,
EX ladder poster 0, EX forwarder 0, present 0).  The reason is on the VR side:

```text
nikonvr: loaded-component functional assumptions: ... no cold BootROM,
         fixed exception table or peer ACK supplied
nikon: uncovered access pc=0xfff00167 address=0xfffc8008 (read)  -> MEMTX_ERROR
nikon: uncovered access pc=0x00000000 address=0x00000000
nikon: uncovered access pc=0xffffff7c address=0xffffff7c
```

`0xfffc8008` is a 4-byte slot inside the VR's **package-external resident
interval** `[0xfffc8000, 0x100000000)` (`NIKON_VR_EXTERNAL_BASE/SIZE`).  The
bootstrap at `fff00167` reads that slot and JSRs through it before entering the
application; the component supplies neither the value nor the target, and the
model deliberately maps nothing there (explicit `MEMTX_ERROR`, never an erased
default).  The whole interval is `0x38000` bytes at the very top of the VR
address space - i.e. that MCU's resident ROM, which is not part of the firmware
update package.

So the VR guest never reaches its peripheral init (`fff001f8`/`fff005bd`), SCI5
is never configured, the MAS SPI4 clocks get no answer, and the camera stays
exactly where it was.  The documented way to supply such bytes is
`-device nikon-vr-input` plus an exact `nikon-vr-span` naming a real,
source-qualified file; fabricating the slot is explicitly rejected.

The VR path is therefore closed until a real dump of the VR resident interval
exists.  See `docs/startup/vrdomain.md` for the full record.

### MAS-side SPI4 protocol: first capture {#mas-side-spi4-protocol-first-capture}

The peer transport makes the guest's SPI4 traffic observable without touching the
model: run the camera with `nikon-spi-peer` on `/machine/spi4` and read the
40-byte peer envelope from the chardev (`run ex-6b-ercd/cap-vr.py`).

Without answering any clock request, the guest asserts select, clocks exactly
one byte `0xD0`, then cancels and retries - 14 times in 40 s.  So `0xD0` is the
first byte of the VR request and the driver will not continue until that byte is
answered.

Answering each clock with a diagnostic `0xFF` (capture side stays genuine guest
traffic; the reply is the harness's) makes the guest clock a full **76-byte
transfer** per select.  The first four bytes of the transfers observed:

```text
d0 10 00 00     f2 18 55 f0     f2 10 00 00     f1 52 00 00
f2 90 55 00     f2 10 00 00     f2 90 55 58     d4 1c e3 00
```

i.e. a fixed 4-byte command word followed by 72 further clocks, which is the
request/response window the driver expects the peer to fill.

The MAS-side stack behind that traffic was located by climbing from the SPI
driver (`0x10243xxx`) through the byte-stream transport (`0x10370A14`,
`0x10367FE0`, `0x10368096`) to the protocol client module `0x102FE6xx`:

| address | role |
|---|---|
| `0x102FE630(a1, a2, ..)` | sends a 4-byte command: `*a2` with its top byte replaced by `(a1 != 0)` |
| `0x102FE6AC(addr16, len, buf)` | bounded memory read/write: 2-byte address, chunked to 0x100, total capped at 0x900 |
| `0x10367FE0` | transport write, chunk 0xC0 |
| `0x10368096` | transport write+read |

So the VR link is an addressed command/data protocol rather than a plain frame
stream, and the 76-byte transfer is its request+response window.

### The VR command word matches a MAS RAM table {#the-vr-command-word-matches-a-mas-ram-table}

The captured first command word `d0 10 00 00` (little-endian `0x000010D0`) does
not exist as a constant in the MAS image, but it does exist in MAS RAM at
`0xB182A44C`, inside a table of 16-byte records:

```text
0xB182A410  d9 10 00 00  00*8  01 00 00 00
0xB182A420  d6 10 00 00  00*8  02 00 00 00
0xB182A430  cd 10 00 00  00*8  03 00 00 00
0xB182A440  d0 10 00 00  00*8  04 00 00 00
0xB182A450  d3 10 00 00  00*8  03 00 00 00
0xB182A460  2e 11 00 00  00*8  02 00 00 00
0xB182A470  2d 11 00 00  00*8  01 00 00 00
0xB182A480  2c 11 00 00  00*8  00 00 00 00
0xB182A490  2f 11 00 00  ...
```

i.e. `{u32 word, 8 zero bytes, u32 index}` with words such as `0x10D9`, `0x10D6`,
`0x10CD`, `0x10D0`, `0x10D3`, `0x112E`, `0x112D`, `0x112C`, `0x1254`, `0x1258`.

The frame builders `0x102FE574` / `0x102FE64E` compute their buffer as
`*(ctx+0x14) + 0xCD9A0 + 0x20 + index*16` (and `0xCADD0`, `0xAF` stride for the
other table), so the 76-byte transfer is assembled from these RAM tables rather
than from image constants - which is why the command words cannot be found by
searching the firmware image.

That gives the decoding path: the command word is a table entry, the transfer is
one command plus a fixed 72-byte window, and `-device`-free RAM dumps already
contain both the tables and the assembled frames.

### Frame-table follow-up: negative, and what it means {#frame-table-follow-up-negative-and-what-it-means}

Following the two builder formulas
(`*(ctx+0x14) + 0xCD9A0 + 0x20 + index*16` and the `0xAF`-strided table at
`0xCADD0`) into RAM does **not** locate assembled frames:

- `*(0xB18A77A0)` is `0xB7D00000`, and `0xB7D00000 + 0xCADD0` /
  `+0xCD9A0` are entirely zero, both at 20 s and during an active SPI4 capture
  (dumped with QMP `pmemsave` while `cap-vr.py` was answering clocks);
- so the base in those formulas is not that global, and the command words are not
  read out of a persistent RAM table.

What the capture does establish, deterministically (identical with `0x00` and
`0xFF` replies):

- every transfer is one chip-select window of exactly **76 clocked bytes**;
- exactly **one 4-byte group** per window is non-zero, the remaining 72 bytes are
  `0x00`, i.e. the guest clocks a 4-byte command and then dummy bytes to read a
  72-byte window back;
- the command groups seen are `d0 10 00 00`, `f2 18 55 f0`, `f2 10 00 00`,
  `f1 52 00 00`, `f2 90 55 00`, `f2 90 55 58`, `d4 1c e3 00`.

That matches `sub_102FE630`'s shape exactly - a 4-byte command written as three
payload bytes plus a flag byte in the top position - but the command values do
not decode into any table found so far, so the next step is to read the *caller*
of `sub_102FE630` / the argument `*a2` at runtime rather than to guess it from
RAM.

### Runtime argument capture of the transport: blocked again by instrumentation {#runtime-argument-capture-of-the-transport-blocked-again-by-instrumentation}

Attempting to read the transport arguments (`sub_10367FE0` entry: buffer pointer,
length, caller) while the SPI4 peer client answers clocks gave no transport call
at all inside 50 s; the peer only ever reported chip-select *deassert* signals:

```text
CAP select level=1 events=1
CAP select level=1 events=2
...
```

while the same client without GDB attached receives a full sequence of clock
requests and 76-byte windows.  So attaching the debugger changes the SPI4
activity in this configuration, exactly like the earlier breakpoint-coverage
problems - the capture path and the GDB path do not compose for this question.

Practical conclusion: the remaining protocol work should stay **static**, in the
`0x102FExxx` client module (frame builder `0x102FE1EA`, senders `0x102FE574`,
`0x102FE64E`, the multi-chunk writer `0x1030621E`, the addressed reader/writer
`0x102FE6AC`) plus the captured 76-byte windows as ground truth, instead of
trying to observe the transport live.

### The captured SPI4 traffic is not the BodyVr record builder {#the-captured-spi4-traffic-is-not-the-bodyvr-record-builder}

Static reading of the BodyVr request builder `0x102FE1EA` (4942 bytes of
decompiled code, 21 operation cases) shows a clear record format:

```text
81 00 <len> <data...>      written into the packet buffer at 0xB2B37590
...
0xB2B375B8 (buffer + 0x28)  per-record byte counts, e.g. 5, 7, 4
```

i.e. `0x81` is a write-style command, followed by a sub-address, a length byte
and the data, with a parallel length table at `+0x28`.

But that buffer is **all zero** in a normal run *and* during an active SPI4
capture (`pmemsave` of `0xB2B37000..0xB2B38000` while the peer was answering
clocks: 22 non-zero bytes in the page, none at the buffer).  So the 76-byte
windows captured on `/machine/spi4` are produced by a different client, and the
BodyVr record builder never runs.

That reopens one question rather than closing it: **which MAS client owns SPI4**.
The answer is static and cheap: the channel table at `0x1239C040` gives each
channel its MMIO base, threshold, ISR entry and clock ids, and the driver's
open/transfer API (`0x10243044`, `0x10243130`, `0x10243468`, `0x10243542`) is
reached only from `0x1037xxxx`; climbing from those callers names the clients.

### Three different senders on the same transport, and which one is unknown {#three-different-senders-on-the-same-transport-and-which-one-is-unknown}

Climbing the transport from its singleton handle (`0xB18A7808`, referenced once,
from the literal pool at `0x10368138`) gives the frame shapes of the clients that
use it:

| builder | frame prefix | buffer | status |
|---|---|---|---|
| `0x102FE1EA` (BodyVr records) | `0x81 00 <len> <data>` | `0xB2B37590`, length table at `+0x28` | never runs (buffer stays zero, also during capture) |
| `0x10368096` (transport write+read) | `0x82 <b0> <b1> <data>` | `0xB2B375CA`, receive window `0xB2B3774A` | shape known, not yet tied to a channel |
| captured on `/machine/spi4` | `0xD0 10 00 00`, `0xF2 ..`, ... | unknown | deterministic, reply-independent, 76-byte window |

The transport descriptor is `{tx_buf, rx_buf, 4, length}` with a 0xC0 chunk
limit, and `sub_10385D50(handle, level)` guards it as a mutex.

So the repository's documented statement - MAS SPI4 is consumed by the VR SCI5 -
is still the best hypothesis for the 0xD0 traffic, but none of the builders found
so far produces it.  The next static step is to enumerate every client that
reaches `sub_10370A14` (the transfer start) and map each to its channel by the
buffer it uses; the client whose buffer carries the `0xD0` command is the VR
protocol implementation.

### Capture-time RAM search: one command word matches a table, the rest do not {#capture-time-ram-search-one-command-word-matches-a-table-the-rest-do-not}

A 106 MB `pmemsave` of the MAS kernel and private regions taken *during* an
active SPI4 capture (peer client answering clocks) was searched for the captured
command patterns:

| pattern | hits in 106 MB of MAS RAM |
|---|---|
| `d0 10 00 00` | one, at `0xB182A44C` |
| `f2 18 55` | none |
| `f2 90 55` | none |

The single hit is inside a table of 16-byte records at `0xB182A40C`, which holds
values such as `0x125F`, `0x1260`, `0x1261`, `0x1262`, `0x10D9`, `0x10D6`,
`0x10CD`, `0x10D0`, `0x10D3`, `0x112E`, `0x112D`, `0x112C`, `0x112F`, `0x1254`,
`0x1258`, `0x124D` with small indices beside them - an event/state table, not a
command list.

So the first captured command word coincides with an entry in that table, while
the later command groups (`f2 18 55 f0`, `f2 90 55 00`, `f2 90 55 58`) exist
nowhere in RAM as bytes, i.e. they are produced on the fly (computed or encoded)
rather than copied from storage.

That is the state of the protocol work: command *shape* and *window size* are
known, the *values* are computed, and the producing client is still unidentified.
The next static step is the one already planned - map every client that reaches
`sub_10370A14` to the channel it uses, so the owner of the `0xD0`/`0xF2` traffic
is named.

### Isolating the SPI4 client by differential execution {#isolating-the-spi4-client-by-differential-execution}

Instead of guessing, run the camera twice with `-dfilter` over a wide code range -
once without any peer and once with the SPI4 peer client answering clocks - and
diff the sets of executed addresses.  Everything unique to the second run is on
the SPI4 client path.  The method works with no debugger attached, so it does not
suffer from the instrumentation perturbation seen earlier.

Result, 30 s per run: **57 addresses execute only when the peer is attached**,
clustered as:

| cluster | functions | size |
|---|---|---|
| `0x10306xxx` | `sub_103069DC`, `sub_10306DE8`, `sub_10306FB0` | 820, 214, 254 |
| `0x10307xxx` | `sub_10307FB4` | 48 |
| `0x10368xxx` | `sub_10368150`, `sub_1036820A`, `sub_10368AEC` | 86, 132, 14 |
| `0x1037xxxx` | `sub_10370242`, `sub_103708C4`, `sub_10370A14`, `sub_10370E84`, `sub_10370EE8` | 178, 336, 138, 50, 86 |

So the SPI4 client is the `0x10306xxx`/`0x10307xxx` module, and the `0x1037xxxx`
functions are the transport it drives.  `sub_103069DC` (the largest) is a service
handler that pumps `MEMORY[0xB26B4C9C]` states 4/5/6 and obtains buffers through
`sub_10298BEA(0, id, size)` - the frame assembly lives in this module.

This also explains why the earlier builders were dead ends: they belong to other
transports, not to the SPI4 client.

### SPI channel numbering, and which handles belong to which client {#spi-channel-numbering-and-which-handles-belong-to-which-client}

The transport keeps one 104-byte context per channel at `0xB1808E20 + 104*N`, and
each context's `+0xC` points at an 0x58-byte channel descriptor.  Read from a
capture-time RAM dump, the descriptors say:

| descriptor field | meaning |
|---|---|
| `+0x00` | IRQ / peripheral id (`0xE9`, `0xEA`, ... step 1) |
| `+0x0C` | channel number `N` |
| `+0x10` | `1 << N` |
| `+0x18`/`+0x1C` | clock ids (`0xC1/0xC0`, `0xC3/0xC2`, ... step 2) |
| `+0x1C` | MMIO base `0x2C0A0000 + N*0x100` |

So **firmware channel N is the model's `spi<N>` port** - the mapping is exact, no
off-by-one.

The transport handle read from the same dump is the channel number, and the known
clients hold:

| client | handle global | value |
|---|---|---|
| `0x10367FE0` / `0x10368096` (record writer) | `0xB18A7808` | 8 |
| `0x1036EB3C` (generic transfer wrapper) | `0xB1807B10` | 6 |
| `0x10383388` (long-timeout transfer) | `0xB18A78E9` | 2 |

so none of the already-identified clients drives channel **4**; each loads its
channel byte from its own context (`LDRB R0,[R4,#k]`) rather than from a global,
which is why the owner still has to be found by its context struct rather than by
a constant.

### Multi-peer observation does not work {#multi-peer-observation-does-not-work}

Attaching a peer to several channels at once, or to a channel without the
chip-select GPIO link, yields **no clock requests at all** (0 on `spi0`, `spi1`,
`spi4`), while the same single peer on `spi4` with `gpio=/machine/gpio,pin=177`
receives the full sequence.  So the peer only enters its asynchronous path when
the select link is present, and one CS pin per run is what this model supports -
multi-channel capture needs each channel's own CS pin, which is known only for
channel 4.
