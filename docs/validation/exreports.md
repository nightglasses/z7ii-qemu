# EX report publication and host commands

These observations qualify a specific input and source revision. They do not describe a new execution or replace the [current startup boundary](frontier.md). Earlier negative observations retain their original scope; later corrections are stated beside the affected interpretation.

## Earlier EX and display observations {#earlier-ex-and-display-observations}

The sections below retain their recorded windows and source analysis for the
supplied EX component (C1.70, container offset `0xd0`, length `0x80002`). Their
negative observations do not extend the current checkpoint into a proof that
other modes or longer runs cannot make progress.

A fixture PASS does not establish anything here; see
[`native.md`](native.md) for the synthetic contract level.  Physical-camera
behaviour stays outside this model.

## Reached by the unchanged firmware {#reached-by-the-unchanged-firmware}

All rows are observed on the bundle with the probe time base below.

| Stage | Evidence |
|---|---|
| EX cold boot, RTOS init, 27 cyclic objects created | breakpoints on `0x0804bf38`/`0x0804d388`; object pool `0x200224f0` |
| power monitors publishing ids 113/114/115 | **not reproduced** in the current 600 s window: dispatcher `0x08027e58` takes only `0x65`/`0x66`; the older `1113/1114/1115` receipt is not current |
| host commands 101/102 dispatched | same dispatcher, ids `0x65/0x66` in the current window; 103/104/131 come from earlier windows |
| EX record dispatcher `0x080332f4` handles the four boot records | `{type,handler}` table `0x0804e15c` |
| MAS record TX writes `0x2c0a0000`/`0x2c0a0100` | `0x1024325c` with `datareg=0x2c0a0000/0x2c0a0100`, 0x80-byte frames |
| **0x6b monitor read completes** | `sub_802ae48` 10x on `0x2001fdcc`, wire address `0xd6`; `sub_8046cd0` entered with `+61=0x22`, `+42=1` |
| MAS kernel idle and GUI app states 1..3 | dispatcher `0x1020d41a`, state-3 branch `0x1020d51e` |

## Not reachable in this image {#not-reachable-in-this-image}

Each gate is a state byte that the compressed SRAM template initialises to zero
and that no later code writes.  The three are independent, so none of them is
an artefact of the other two.

| Gate | Consumer | Verification |
|---|---|---|
| `0x2001fba6` | id-130 publisher start, `sub_8027d48`; also the detailed path of the report-input builder `sub_80278fc` | only write is the boot template's zero; selector always takes the `v = 0` branch |
| `0x20016058` (and the `0x20016040..` block) | EX report `+61`, MAS power classifier | four dwords watched for 900 s: one boot-template write, three never written; producer interface identified in "EX report-input producer" below |
| `0x200138da` | type-`0x12` record handler | written by the handler, never read |

## EX report-input producer {#ex-report-input-producer}

The `0x20016040..` block has a concrete producer, so it is not "written by
nothing":

```text
host dispatcher 0x8027e58, case 'q' (id 113)
  -> sub_80278fc(1)                     build a 60-byte payload
     (payload from 0x2001faa4/faa8/faab/fabd/fabe/fac8/fb08/fbb8/fbb9;
      detailed path gated by 0x2001fba6)
  -> dispatch 0x02020045 to slot 0x2001f5fc
  -> 0x8025268: memcpy(0x20016040, payload, 60)   (0x8020552)
  -> 0x802551c(15)
```

Sibling handlers fill other report fields for the `0x02020041`/`0x02020042`
families (`0x8025120` writes `0x2001607c`/`0x200160b8..c4`, `0x8025184` writes
`0x200160c8..d2`).  The report builder `sub_8025544` then consumes the block
(`+0/+1/+2/+4/+8...`) and `+61` is guarded by block byte 24
(`0x20016058`).

Measured in a 600 s icount window: the host dispatcher only takes ids `0x65`
(101) and `0x66` (102) - 60+ alternating calls; ids `113`/`114`/`115` never
arrive, so `sub_80278fc` is never called, the dispatch never happens, and the
block keeps only the boot-template zero (`pc=0x80038be`).  The missing link is
the EX-internal power-monitor command 113, not a register or DMA writer.

### The BatCmCy monitor table and its publishers {#the-batcmcy-monitor-table-and-its-publishers}

The table at `0x8051444` holds 20-byte cyclic-monitor entries
`{callback, period, name[8], slot}`:

| Entry | Callback | Publishes | Period | Slot |
|---|---|---:|---:|---:|
| BatCmCy1 | `0x8027d40` | `0x459` (1113) | 500 | `0x71` |
| BatCmCy2 | `0x8027d38` | `0x46f` (1135) | 250 | `0x87` |
| BatCmCy4 | `0x8027d30` | `0x45b` (1115) | 20 | `0x73` |
| BatCmCy5 | `0x8027d28` | `0x45a` (1114) | 5 | `0x72` |
| BatCmCy6 | `0x80268e4` | `0x468` (1128) | 500 | `0x80` |
| BatCmCy7 | `0x8029c78` | `0x45c` (1116) | 500 | `0x74` |
| ... | `0x8029c70`/`0x8029a7c`/... | `0x45e` (1118), ... | | |

Publisher API: `0x8028b70(id)` -> allocate `0x80268ec` -> queue post
`0x8041cac` (the queue handle is loaded at `0x8028b94`, separate from the
transport dispatcher `0x8027e58`).

Measured at shift 6: the `1114` and `1115` publishers fire repeatedly (23
publishes in 600 s), while `1113` and `1128` are zero-hit in 600 s **and** in a
900 s window.

The reason is not the period.  A dump of the RTOS cyclic object pool
(`0x200224f0`, 48-byte objects) taken at the 10th `1114` tick shows:

| Object | Descriptor | Callback | State | Period | Links | Expiry |
|---|---|---|---|---|---|---|
| idx0 / object 11 | `0x2001FC2C` | `0x8027D41` (1113) | **0** | 500 | none | `0x271` |
| idx4 / object 12 | `0x2001FC7C` | `0x8027D29` (1114) | **1** | 5 | linked | `0x510` |
| idx3 / object 13 | `0x2001FC68` | `0x8027D31` (1115) | **1** | 20 | linked | `0x515` |
| idx5 / object 14 | `0x2001FC90` | `0x80268E5` (1128) | **0** | 500 | none | `0x271` |

The descriptors map index -> object correctly (`0x2001FC2C` -> 11,
`0x2001FC68` -> 13, `0x2001FC7C` -> 12).  Started objects carry state 1, list
links and the dispatch code pointer `0x804AB5B`; the 1113 and 1128 objects have
state 0 and no links.  So they are **created but not started**, which is the
same shape as the id-130 object in `docs/runtime/ex.md`.

The power module has two wrappers around the start API:
`0x8029264(idx)` (ends in `0x804D388`, start) and `0x80292BC(idx)` (stop).
Both are called with index trios `(0,4,3)` from `0x8027D50..0x8027D94`, gated by
the state byte `[0x2001F598+37]`; the last transition for index 0 therefore
leaves it stopped while indices 3/4 remain started.  The `1113` -> host-113
translation is consequently still unobserved, and with it the report-input
builder.

### Which start site runs {#which-start-site-runs}

A 300 s probe with `lr` at the start wrapper shows that every observed start is
`idx=3` followed by `idx=4`, both with `lr=0x802A921`/`0x802A929`: the periodic
re-arm tail at `0x802A918` starts only slots 3 and 4.  The selector that also
starts slot 0 (`0x8027D48`, the host-131 route) has **zero hits** in the same
window, and `START-WRAP idx=0x0` has zero hits.  The host dispatcher carries
only `0x65`/`0x66` in the current windows, so the host-131 route that would
start slot 0 never runs.

So the 1113 absence is a *missing host-131/power transition*, not a period
effect and not a start-API failure: slots 3/4 are re-armed every cycle by
`0x802A918`, while slot 0 is started only by `0x8027D48`, which the current
windows never reach.

### The MAS host-command table does not contain 113 or 131 {#the-mas-host-command-table-does-not-contain-113-or-131}

The MAS periodic host-command descriptor table at `0x10949704` (20-byte
entries `{timeout, flag, handler, cmd, period}`) lists
`0x63 0x65 0x66 0x67 0x68 0x69 0x6a 0x7b 0x7c 0x7d 0x8f 0x90 0x91 0x94`
with handlers `0x105b53xx..0x105b54xx`.  The two ids that matter here, `0x71`
(113) and `0x83` (131), are **not in the table** (no dword occurrence in the
surrounding `0x10949600..0x10949a00` window either).  So they are not periodic
MAS host commands; if they are ever sent, it is from an event path that has not
been identified.

That closes the "MAS sends 113/131 periodically" hypothesis: the EX waits for
event-driven commands that the current MAS image never emits on the observed
lane.

Consequences: the provider lifecycle and LCD start are downstream of the power
classification, which is downstream of a block this image never fills.  The
transport, the factory configuration and the monitor lane are all ruled out as
causes by direct measurement.
