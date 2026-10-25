# Worker exit and the rear-panel resource

Status, 2026-10-21: on the current multi-thread baseline, manager state 3
waits for **worker 0** to finish exiting before starting worker 4 in mode
`0x15`. Its stop event arrives. Worker 0 then waits for a rear-display
configuration request whose handler takes a data abort through a null panel
table. The generic worker exit acknowledgement has not been reached.

All code, data and exception addresses below are C1.70 **MAS domain VA**.
These are unchanged-firmware software observations, not a physical-camera
receipt or a GUI first frame. The execution profile and battery publication
are recorded in [battery.md](battery.md).

## Which worker and which acknowledgement

The one-breakpoint manager probe observes phases `1` five times, `2` once
with index 0, then `4` once with request `{4, 0x15}`. No phase 3 arrives in
the 60-host-second window. Final manager bytes at `b1811e50` are
`00 04 00 00 15 00 00 00 03 00 00 00`:

| Field | Value |
|---|---|
| Current index, `b1811e50` | 0; context `b181d8a4` |
| Target index, `b1811e51` | 4; context `b1828064` |
| Requested mode, `b1811e54` | `0x15` |
| Manager state, `b1811e58` | 3 |

Worker 3 is the GUI worker; it is not this handoff's target. At
`102996ca`, manager `10299614` invokes the current context's stop method
through `10360876 -> 103fabbc -> [ctx+60]`. For worker 0 that method is
`10437aa8`, which posts primary event bit 1 through `10634030/10634088`.
The manager then stores state 3. Only phase 3 with a valid target takes its
`102996dc..102996ee` transition to state 1 and starts the target.

The required sender is the **old worker's generic task body**, not EX or
the battery frontend:

```text
103fab3a(b181d8a4)
  -> [ctx+56] = 10437a6e       old worker's main loop must return
  -> [ctx+64] = 10437ab8       teardown, a BX LR stub
  -> 103fab24(ctx, 3, *(ctx+44)) at 103fab84
  -> [ctx+36] = 10204da9      hook, argument/index 0
  -> 1020e07e                phase 3 becomes app event 01000002
  -> app dispatch -> 102a69c4 -> 1029990a -> 10299614(3, 0)
```

Offsets above are bytes. The manager consumes the phase value; its state-3
branch does not independently compare a sender index.
The sender identity follows from the worker wrapper and context hook.

## The stop event is consumed; display completion is missing

The sequential single-breakpoint probe observes `10438aea` with
`lr=104379cd`, event record `b181d938=4020201f`, and old context class
`[b181d8a4+108]=2`. Thus bit 1 was consumed by `10437924` and dispatched as
local phase 0 through class-2 callback `10438c0a`. This local callback phase
0 is distinct from the manager's phase-3 exit acknowledgement.

The observed cleanup path is:

```text
10437924(flags containing bit 1)
  -> 106ddbfc(ctx0, 0, b181d938)
  -> 10438c0a -> 10438aea
  -> 10636be0(1, 0)
  -> 1057ab2a -> 1057dce2 -> 1057aa94(1, 1, 0)
  -> 1057a8ec                 completes its earlier waits and returns
  -> 1057a078 -> 10578b0a
  -> 10361156(b18107d4, 20, result), return site 10578d28
```

The last wait has `lr=10578d29`, handle `[b18107d4]=01000160`, mask
`0x20`, and worker stack `sp=b5406518`. It is a synchronous configuration
completion wait. Its request at `b5406538` contains:

| Byte offset | Observed word | Meaning from the original consumers |
|---:|---|---|
| `+8` | `b5406538` | request self pointer |
| `+12` | `1057d2c5` | completion callback, Thumb pointer |
| `+16` | `42060004` | rear-display configuration command |
| `+20` | `b1d60dc0` | configuration payload |
| `+24` | `1` | callback argument |

`104454a0` invokes the request's callback after command processing.
`1057d2c4` explicitly loads `r1=0x20` and tail-calls
`103610cc(b18107d4, 0x20)`. That completion would release worker 0 so its
cleanup and main loop could return and the generic wrapper could send phase 3.
The decompiler omits the mask argument here; original Thumb instructions
confirm it.

## The handler faults before it can complete

In the same sequential probe, after that wait is entered, the display handler
reaches `10449a22 -> 104493c8 -> 104819ac(command=5) -> 10481824(5)`.
The retained handler stack contains the original request pointer `b5406538`.
`104493c8` calls command 0 first, whose null-table guard skips its body, then
command 5, whose lookup has no corresponding guard.

Direct observations immediately before the fault:

- `[b18084fc]=b1808508`, but `[b1808508]=0`: the panel-table cell is null.
- `10481824` loads this table base into `r7`; the selected index is 5.
- `1048185e` forms `r0=r7+2*5=0x0a`.
- `10481864: LDRH.W r0,[r0,#0x114]` accesses VA **`0x11e`**.
- The first MAS data-abort entry at `10100010` has `lr_abt=1048186c`,
  `DFAR_S=0x11e`, `DFSR_S=5`, and `r7=0`. Thus `lr_abt-8` identifies
  precisely that load.

This abort occurs before the handler's completion call at `10449a62`, so the
request cannot reach `104454a0 -> 1057d2c4` on this path. The earlier
`b2bb0110` final CPU0 PC is in the copied diagnostic-output code: its first
512 bytes match original MAS source VA `12448d80` directly. It is not a
worker event-wait instruction. The later CPU1 aborts with VA `0x4f` occur
after the first CPU0 panel fault and are not the original blocking condition.

## Where the table should come from

Static original-code tracing identifies the producer:

```text
102051b4, event 42068001
  -> wait at b1875420, mask 5, timeout 5000
  -> 103a485c(resource ID 24, lookup result)
  -> 104d7376(result, output 0)
  -> 10448c3a: install result[0] in the rear-display context
  -> 1048178c: store that pointer through b18084fc
```

ID 24 is the `HAIMENMONI` record in external descriptor17, as documented in
[descriptor.md](../inputs/nor/descriptor.md). The current baseline supplies
no descriptor17 input, and the runtime footer at `132dfffc` reads
`ffffffff`, rather than `ENDM`. The descriptor contract requires successful
validation, record copy and publication before the resource pointer is usable.
A separate single-breakpoint probe sees no ID-24 `103a485c` lookup during its
60-second window. A focused return probe at `10205226` observes the timed
wait return **0**, not the required 1, on event handle `010000f2` at
`b1875420`. The original branch then exits at `10205218`, skipping both
ID-24 lookup and installation. Its result buffer remains zero; because the
wait failed, that buffer is not treated as valid returned event flags.

The missing successful wait is for mask `5`: resource-completion events
`04400043..04400045` handled by `102051b4` post bit 1 for ID24 and bit 4
for ID27; ID25 uses bit 2 separately. The helper uses wait mode 0 and timeout
5000. The subsequent [resource investigation](resource.md) captures the raw
RTOS return `ffce0000`, the failed `ENDM` comparison and validator result 0.
It also observes all three loading phases run, and the registered resource
callbacks remain unpublished on the erased-input baseline.

The same investigation supplies the existing Z7 cross-model seed through the
explicit NOR input property. It observes phase-2 resource publication, wait
success and installation of the `HAIMENMONI` table, then the separate erased
ID34 overrun before worker startup. This experiment establishes the publication
edge, not completion of the worker handoff. Usable descriptor17 record data
remains required. No null guard, fabricated table, event or forced exit was
added here.

## Receipts and limits

Ignored receipts are in `run handoff-20261021/`: actual argv, manager,
cleanup, subsystem, completion, abort, confirm, resource and resourcewait probes, filtered
instruction traces, and static decompilations. Each accepted probe uses at
most one hardware breakpoint at a time. `confirm` joins the worker wait,
request pointer, null table and first abort in one run and exits GDB cleanly.
The initial `cleanup` final-memory read selected the wrong CPU after register
inspection; its failed read is excluded. The early-stop `fault` probe hit a
GDB internal abort after printing the guest fault; its final snapshot is
excluded and its positive observations were repeated by `confirm`.

A separate native-start run has no debugger connection or breakpoint during
its 60-second measurement. QMP stops it before the read-only snapshot. It
reproduces manager state 3/current 0/target 4/mode `15`, the same request and
null table, `DFAR_S=11e`, `DFSR_S=5`, and battery status 2. Its filtered trace
enters `10438aea`, `104493c8` and `10481824` once each, with no worker-loop
return/teardown at `103fab7c..103fab84` or table installation at `10448c3a`.
It also follows resource initializer `1020521c -> 10205226 -> 10205218`,
the timed-wait failure branch that skips ID-24 lookup and installation.
These are window observations, not claims of permanent unreachability.

Original firmware and device behavior remain unchanged. This is a diagnosis
and documentation change; device unit/qtests are not repeated.
