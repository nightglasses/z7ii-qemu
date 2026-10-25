# Image information and display activation

The cold-neutral checkpoint reaches GUI drawing and display-layer programming,
but has no active display target or observed publication through the traced
image-information subscriber.
The newly isolated image-task branch rejects the observed sequence phase before
any image DMA submission. This is a software admission boundary, not a new
unsupported device access. AF6's state9 request is a separate conditional route;
forcing it is not justified as a normal GUI prerequisite.

Addresses below are original C1.70 MAS CPU VA unless explicitly marked PA.
Source is the unchanged MAS payload at base10020000, inspected in IDA;
unrecognized Thumb wrappers were decoded without changing the database.
The [published admission summary](/evidence/imagerequest/admission.json)
contains the critical observations and omissions. Raw disassembly, argv,
snapshots and traces remain private and are not shipped with the handbook.

## Image-information admission

The branch labels are source conditions. Only the early-exit branch was
observed for the 15 selector4 jobs in the `cadence` window; the remaining
path has further checks and is not demonstrated by a statistics completion.
All code addresses in these diagrams are MAS VAs.

```mermaid
flowchart TD
  Job["Image task 1026a312<br/>Observed type2 job"] --> Pair["103f6b90 reads selector pair<br/>Observed selector4: period 2, phase 0"]
  Pair --> Test{"103f6bb2<br/>period != 0 and sequence % period == phase?"}
  Test -->|"No: observed sequences 5, 7, ..., 33"| Exit["Return before image submission"]
  Test -->|"yes"| Work["Remaining checks, allocation and processing"]
  Work --> Mask{"Nonzero completion mask<br/>equals requested mask?"}
  Mask -->|"yes"| Event["041b0040 to capture adapter 103284d0"]
  Other["Other source-defined producers<br/>1026aa50 / 103cda56"] --> Event
  Event --> Builder["1035a2d8 builds image information"]
  Builder --> Registry["Registered callbacks forward 04380001<br/>106d103c through four subscriber slots"]
  Registry --> GUI["GUI subscriber 104be876<br/>Copy information; GUI record92 / primary80"]
```

The early-exit result explains these jobs only. Other job types and other
publishers retain their separate branches; the period/phase pair is software
data and is not a hardware-ready register.

## Verified producers and conditions

| Edge | Source condition and effect |
|---|---|
| Image task `1026a312` | For the observed type2, read selector byte at `job[3]+14` through `103f6b90`, then require `103f6bb2(job[0]-1,pair)`. A false result returns without executing the remaining image path. Other types have explicit bypass branches. |
| Admission helper `103f6bb2` | `period != 0 && sequence % period == phase`. Getter `103f6b90` copies two words from `b18a9540 + selector*8`; these are software data, not MMIO readiness. |
| Image-information event | After the remaining checks, allocation and processing in `1026a312`, the assembled packet must have a nonzero completion mask equal to the requested mask before callback `b186488c` receives `041b0040`. Other producers include `1026aa50` and `103cda56`; this task is not the only publisher. |
| Capture service adapter `103284d0` | For `041b0040`, call `1035a2d8(packet[266])`. Its separately observed `04180203` case does not publish image information. |
| Information builder `1035a2d8` | Construct the information payload and call `b1866d88(04380001,payload)` if registered. |
| Callback registration | `106d1232(2)` calls `1061d442 -> 1034ff68` with Thumb callback `106d103d`. Wrapper `106d103c` forwards `04380001` to the four callback slots at `b25d874c` through `1061d3ca -> 103e4b08`. |
| GUI subscriber `104be876` | Registered by `104bf1aa -> 10551b62 -> 106d1334`. On `04380001`, `1058027e` copies464hex bytes and calls `1057fdf8`; copy another464hex bytes to `b201c038`, publish GUI record92 and primary event80. |
| Optional switch completion | In the same subscriber, only when `10631c2c()` reads nonzero `b181b470`, clear that word and call `10631c7e`. The latter posts primary400 only if the switch-completion predicate `10631c52` succeeds. An information notification alone does not guarantee activation. |

The packet paths above are original software producers. A processing-statistics
completion, or a raw0.bit6 interrupt, is not interchangeable with `041b0040`.

## Verified display activation path

```mermaid
flowchart TD
  Info["04380001 reaches GUI subscriber"] --> Copy["Copy information and call 1057fdf8"]
  Copy --> Gate{"Valid information geometry<br/>and active, unpaused target?"}
  Gate -->|"yes"| Request["106b84ba builds upper image request"]
  Gate -->|"no"| Blocked["No upper request on this route"]
  Info -->|"Only if switch-pending and completion predicate pass"| Primary["10631c7e posts primary400"]
  Primary --> Phase["Phase18 dispatch<br/>105424e8 to 105409f2"]
  Phase -->|"Activation decision selects operation5"| Enable["105801ce stores requested mask<br/>and posts owner event4"]
  Enable --> Owner["105804fc enables target<br/>OR active mask; clear paused bits"]
  Owner --> Complete["Completion 05040003"]
  Owner -->|"Supplies target state, not image information"| Gate
```

An information event does not guarantee primary400 or activation. The diagram
shows the operation5 branch; policy operation0 and the separate owner-event80
activation handler remain distinct source paths described below. No activation
handler was observed in the focused window.

Primary400 dispatches `1053d0ec -> 106ddbfc(...,18) -> 105424e8 -> 105409f2`.
At its final activation decision, `10631f22()` true sends `104a94fe(5)`;
otherwise policy value3 from `104bde46()` sends operation0. Operation5 calls
`105801ce`, stores the requested target mask at `b181a574` and posts owner
event4. Handler `105804fc` enables the target through `10637aec`, ORs that
mask into active word `b181a56c`, clears its paused bits at `b181a570`, and
calls completion05040003. The owner's event80hex is another source-defined
activation handler; neither handler was observed in the focused window.

`1057fdf8` requires an active, unpaused target and valid information geometry
before it can reach `106b84ba`, which constructs the upper image request.
The active-mask test is therefore a concrete upstream condition on this route.

Mode entry `1053fe10:1053ffb0` also calls `1053fc46(10hex,0)`. This is a
selection refresh: `1063164a` compares old/new logical and raw selections.
In the observed logical0 state, command10hex changes neither, so returns0;
`1053fc46` skips the switch block and returns0. That block would send
operation4 and initiate switch synchronization/timer43. **Operation4 posts
owner event8, a disable operation, not the final enable.** Its absence does
not by itself explain why first activation has not occurred. The phase18
producer and image-information producer must be audited together; there is
no evidence permitting a forced initial enable mask.

## Observed cold-neutral boundary

`entry`, `producer`, `notify` and `cadence` each use the same cold-neutral
descriptor32, RNG0, healthy paced UART battery, held cold F4 and5 CPUs under
MTTCG. No extra key input or in-transaction debugger pause is used. Each
20-host-second window ends with a read-only snapshot. Trace filters differ;
an address outside a run's filter is not an observed absence. TCG chaining
also means entry observations are not exact call counts.

* `entry` observes GUI drawer10569c76 and layer writer1046d48a repeatedly.
  The latter's validator1047ed56 accepts the geometry and returns normally.
* Operations8,10,13 are observed. With property6433 empty, operation10 passes
  its policy checks and sends the original unpause; the historical ID68 skip
  does not explain this checkpoint. Operation5/phase18 and activation handlers
  are absent from their respective focused filters.
* Final active/requested-enable/paused words are all0. Target selection
  `b181b484=1` is present; pending switch words `b181b470/474/478` are0.
  Last pause/unpause masks `b181a57c/580=2` are not persistent pause state.
* At PA20145200, bank unit1 main control is13hex. Both port starts and layer
  enable words remain0. Valid layer parameter programming therefore does not
  prove an enabled scanout transaction; no rejected bank completion is found.
* `notify` and `cadence` observe image task1026a312, but no builder1035a2d8,
  forwarding wrapper106d103c or GUI subscriber104be876. The two observations
  of adapter103284d0 take the separate04180203 branch. Runtime callback
  pointers correctly contain106d103d,104be877 and103284d1 in their registries;
  missing registration is not the sampled problem. Both information buffers
  `b201c038` and `b1fa375c` remain entirely zero.
* In `cadence`, all15 observed admissions use selector4 and the pair `{2,0}`.
  The observed `job[0]-1` values are5,7,...,33. Each observed resume at
  1026a3e4 has return0, taking the early exit. This proves why these particular
  jobs do not publish; it does not prove all image producers have the same
  condition or establish a physical frame rate.

All six loader COPY ranges compare with the supplied descriptor32 input;
guard returns0 with version150, property6433 remains empty, QMP stays running,
and no uncovered access or battery-peer rejection appears. Sequential QMP
reads in `cadence` report1874/1873 accepted/completed readout requests and527
completed statistics jobs, with0 readout/statistics rejection and0 statistics
cancellation. There is still no display console. These are unchanged-firmware
functional-model receipts, not complete startup or physical-camera evidence.

The separate `keys` experiment retains the same data and held F4, then pulses
the remaining existing EX pad inputs through QMP. It observes no activation
or AF6 request and no console. It is not the no-extra-input baseline, and pad
identities are not established physical camera controls.

## AF6 relationship and next evidence

The [AF6 producer audit](../devices/statistics/protocol.md#separate-software-image-request-condition)
establishes `103f27ba -> 10328ca6 -> 10323a7e`: service state9 and
property1053=1 guard the queued request that sets AF6=1. `10324f04` then sets
root[16] only while requested/produced counters differ. Root[8] separately
requires B0C=2. This window still has service state2, AF6=0 and B0C=0, with
neither AF6 API nor worker observed. Those gates explain that route's absence.
The normal information/display functions above have their own guards; they
do not test AF6 at the identified edges. A shared earlier dependency remains
possible, but no evidence establishes AF6/state9 as their required producer.

**Inference:** the observed opposite sequence phase is a useful next upstream
boundary. `103250f2` numbers roots and records the selector; `103cc1a6` performs
further packet admission, and `102994b4` dispatches the source task table that
contains1026a312. Capture both phases across those edges and match job identity
to the completion/publication callbacks before attributing the selection to a
device timing or missing data contract. Separately trace callers of10631c7e
and the switch-pending setters to identify the initial phase18 trigger.

**Unproved hypothesis:** a device phase/completion association might suppress
the eligible jobs. The current receipts do not distinguish that from intended
software task selection. Accordingly this change adds no hardware behavior:
do not alter the software period/phase, change a job number, force state9,
inject400/04380001, or enable display ports to bypass these checks. The existing
statistics model remains unchanged; its81-case native receipt is separate.
