# Camera-internal transport jobs and the SPI4 exchange

This file records the transport the MAS firmware uses for its eleven SPI ports,
the client that drives port 4, and the exact bytes that port puts on the wire.
It is a measurement log, not a reconstruction: every number below was either
read out of the original image at the stated address or observed in a run whose
receipt is named.

The bounded VR probe below did not establish a GUI dependency. The observed
ready-bit probe changes SPI4 pacing without activating the measured display
points. This is a bounded negative result, not a complete VR protocol
qualification. The [VR execution note](../startup/vrdomain.md) records the
current source, retired probes and conditions for returning to this branch.

Related: [devices/spi/protocol.md](../devices/spi/protocol.md) (device contract
and DMA binding), [devices/hdmac/protocol.md](../devices/hdmac/protocol.md)
(the controller the SPI ports 2..4 are wired to).

## Port numbering (verified)

`hw/nikon/machines/z7ii.c` creates eleven `nikon-spi` objects named `spi0`..`spi10`
and maps object *N* at `0x2C0A0000 + N*0x100`. The firmware's own descriptor
table agrees exactly, so firmware channel *N*, MMIO page *N* and machine object
`spiN` are the same channel — there is no off-by-one.

The static descriptor table is eleven records of `0x58` bytes at image
`0x1239C040`; channel *N* is entry *N*. Fields read from the image:

| offset | meaning | channel 0 | channel 4 |
|---|---|---|---|
| `+0x00` | MMIO base | `0x2C0A0000` | `0x2C0A0400` |
| `+0x08` | clock divisor | `0x3232` | `0x0F32` |
| `+0x34` | handler pointer | `0x10243657` | `0x1024366B` |
| `+0x38` | handler pointer | `0x10243691` | `0x102436A5` |
| `+0x3C` | interrupt id | `0xEA` | `0xEE` |
| `+0x48` | channel index + 1 | `1` | `5` |
| `+0x4C` | channel bit | `0x01` | `0x20` |
| `+0x50`, `+0x54` | clock ids | `0xC3`, `0xC2` | `0xCB`, `0xCA` |

Channels 2, 3 and 4 are the DMA-bound ones (`dma = i >= 2 && i <= 4` in the
board), which is why port 4 never shows a CPU write to its FIFO — see below.

## The transport entry (verified from the image)

`sub_10370A14(channel, job)` — image `0x10370A14`, Thumb.

```
push {r4,r5,r6,lr}; sub sp,#24
copy 20 bytes of *job to the local at sp        <- bl 0x10361034
ctx = 0xB1808E20 + 104*channel                  <- literal 0x10370DB0 = 0xB1808E10, +16
ctx[0x5C] = job.type ; ctx[0x60] = 0
if (job.type >= 5) return
switch (job.type)   <- tbb at 0x10370A58, table bytes 03 0b 10 0b 16 at 0x10370A5C
```

The job is 20 bytes (`u32 tx; u32 rx; u32 tx_len; u32 rx_len; u8 type; u32 timeout`
at offsets 0, 4, 8, 12, 16, 20). The type dispatch decodes to:

| type | target | effect |
|---|---|---|
| 0 | `0x10370A62` | `sub_10243468(ctx, job)` then `sub_10370D76` |
| 1 | `0x10370A72` | `sub_10370D92(channel, job)` |
| 2 | `0x10370A7C` | `sub_103708C4(channel, job, job.timeout)` |
| 3 | `0x10370A72` | same as type 1 |
| 4 | `0x10370A88` | `ctx[0x64] = job.timeout ?: 1000`, `sub_10370D12` |

Type 1 is the FIFO path already recorded in `devices/spi/protocol.md`
(`10370a14 → 10370d92 → 10243468`). Type 2 is the path the SPI4 client uses.

## Every call site of the transport start (verified)

Scanning the image for Thumb `BL`/`BLX` whose target is `0x10370A14` finds
fourteen sites; there is no indirect call.

| call site | containing client | measured channel |
|---|---|---|
| `0x1020B6D4` | EX receive task (`0x1020B65A` region) | 1 |
| `0x1020B88A` | EX transmit task (`0x1020B7BA` region) | 0 |
| `0x10368038`, `0x103680F6` | `0x82` record writer | not reached |
| `0x1036EB60` | generic transfer wrapper | not reached |
| `0x10382B7C`, `0x10382D72`, `0x10383106`, `0x1038316C`, `0x103831C4`, `0x103833C8` | long-timeout transfer family | not reached |
| `0x103BC2A8` | SPI4 client (below) | **4** |
| `0x10481814`, `0x104BA2F8` | not reached | — |

The channels are direct observation, not inference: a single hardware breakpoint
on `0x10370A14` logging `r0` and `lr` reported `ch=0` for `lr=0x1020B88F` and
`ch=1` for `lr=0x1020B6D9`. A second run of the same probe with the SPI4 peer
attached reported the same two channels only, because the SPI4 exchange starts
later than the 2 s of guest time that a breakpointed run covers.

The channel is not a constant at any site. Each client keeps a state object and
loads the channel from it (`ldrb r0,[r4,#1]`), and the object's value comes from
a resource lookup at run time (`sub_10368B0A(4)` returns 4 for the SPI4 client).

## The SPI4 client (observed)

Module `0x103BC074`; its transfer routine is `sub_103BC266`, state object
`0xB18A75A0`, job descriptor `0xB2B36650` (= `0xB2B36624 + 0x2C`), call site
`0x103BC2A8`.

Initialisation (`0x103BC13C`..`0x103BC1FC`) creates four resources (ids `0x70`,
`0x71`, `0x72`, `0xAE`), asks for channel id 4 and stores the answer in
`state[1]`, registers with `sub_1037064C` / `sub_103706C4`, and stages an idle
job with `type = 2`, `timeout = 1`.

Per transfer (`0x103BC266`) it:

1. takes a lock by spinning `sub_103BC242(0, 2)`,
2. reads the pending record `r = state[4]`,
3. fills the job: `job.tx = r[8]`, `job.rx = r[12]`,
   `job.tx_len = job.rx_len = (u16)r[2] / 2`,
4. calls `sub_10370A14(state[1], job)`,
5. releases the lock and reports success when the transport returned 0.

A breakpoint on the type-2 dispatch (`0x10370A82`) that also prints the copied
job fired fourteen times in one run, and every hit was identical in shape:

```
ch=4 caller=0x103bc2ad job={tx=0xb23be680 rx=0xb23c14c0 len=0x4c/0x4c type=2 timeout=0}
ch=4 caller=0x103bc2ad job={tx=0xb23bfbc0 rx=0xb23c2a00 len=0x4c/0x4c type=2 timeout=0}
...
```

Fourteen transport calls, fourteen wire transactions: the job length is in
**halfwords**, and `0x4C` halfwords is exactly the 76 clocks the peer observes.

The client also inspects the record it has just sent: `record[0] == 0xD3` and
`record[1] == 0` (`0x103BC088`). `record[0]` is the record's kind, not received
data, so this is a kind test on one record class; see the correction at the end
of this file.

## What the wire carries (observed)

Capture with a peer attached to `spi4` and chip select on GPIO pin 177; frame
width is 16 bits, 76 halfwords per chip-select. Fourteen transactions in one run
reproduced one full cycle and the first transaction of the next:

| # | halfwords (only non-zero shown) |
|---|---|
| 0 | `00D0 0010` |
| 1 | `00F2 0018 AA55 1FF0` |
| 2 | `01F2 0010` |
| 3 | `04F1 0052` |
| 4 | `00F2 0090 AA55 2000` |
| 5 | `01F2 0010` |
| 6 | `00F2 0090 AA55 2080` |
| 7 | `01F2 0010` |
| 8 | `00F2 0090 AA55 0000` |
| 9 | `01F2 0010` |
| 10 | `00F2 0090 AA55 0080` |
| 11 | `01F2 0010` |
| 12 | `00D4 001C 00E3` |
| 13 | `00D0 0010` (cycle restarts) |

So the shape is a 16-bit command word whose low byte is the opcode and whose
high byte is a flag byte, followed by a 16-bit argument and, for `F2`, a key and
a 16-bit value. Opcodes seen: `D0`, `F2`, `F1`, `D4`. (An earlier draft of this
file read the flag byte as part of the opcode and the `AA55` as a payload; the
template table and the builder below settle both.)

**Correction to an earlier record.** A previous capture reported "76 clocks per
select, one non-zero 4-byte group" and listed eight groups. That was an artefact
of the capture script: it appended `word & 0xFF`, so each 16-bit halfword was
truncated to its low byte and the transactions collapsed into one. The eight
"groups" are eight separate transactions; the true width is 16 bits.

## Why no CPU access to port 4 is visible (verified)

A hardware watchpoint on the SPI4 FIFO (`0x2C0A0400`) saw **zero** writes in a
run where the same port clocked its full sequence, and a watchpoint on the
chip-select register (`0x2C0A0460`) saw one write. Only the enable register
(`0x2C0A043C`) showed the expected fourteen set and fourteen clear writes, from
`0x1024352C` and `0x102436D2`.

That is the DMA path: `sub_10243468` takes its type-2 branch, programs the FIFO
watermark, sets the SPI control DMA bit (`|= 0x400` at `0x10243522`), enables the
port (`0x1024352C`) and hands the byte stream to HDMAC bank 0, whose channel pair
4/5 is the one wired to `spi4`. A CPU write to the FIFO therefore never happens,
and a watchpoint on it cannot see the traffic.

## Instrument notes

The capture scripts print completed transactions only when the capture loop
exits, so a killed capture prints `frames=0` even when the port clocked. The
select stream (`CAP select ...`) is the reliable liveness signal, and
`cap-word.py` in the run directory now prints each transaction as it completes.

## A functional peer model, and what it does not unlock (observed)

`vrpeer.py` in the run directory implements the master side of the seam as a
candidate device: it completes the 76 halfwords of every chip-select and drives
a reply buffer it builds from the command word it just received. Two policies
were run against the unchanged firmware, with `-dfilter` counting the four
progress indicators on the same invocation:

| policy | transactions | `0x104D7E78` | `0x1053E19A` | `0x10695450` | `0x0803350C` | `0x10370A14` | `0x10243468` | `0x103BC266` |
|---|---|---|---|---|---|---|---|---|
| reply `D3 00` to `D0` | 13 + restart | 0 | 0 | 0 | 0 | 1302 | 1468 | 13 |
| reply zero | 13 + restart | 0 | 0 | 0 | 0 | 1349 | 1521 | 11 |

Both policies produced the identical thirteen-transaction cycle word for word,
so the emitted sequence is reply-independent over the whole window, not only
over its first transactions. Every transport job returned and none of the four
indicators moved. The execution counts differ by a few percent between the two
runs (and `0x103BC266` ran 13 times against 11) because the runs cover slightly
different amounts of guest progress; that variation is measurement noise, not a
policy effect, since the transaction list itself is byte-identical.

These two reply policies produced the same script and left display admission,
the GUI dispatcher, the `0x39` poster and the EX ladder poster cold. The ready-bit
probe below refines this result: reply content does affect pacing. Neither
experiment establishes that the link is only a heartbeat or that all later
sequences are independent of VR.

Two corrections follow from this run and are recorded here rather than in a
separate entry:

- The record the client inspects is a **queue entry**, not the received data:
  `0x103BC088` reads `record[0]` and compares it with `0xD3`, where the record
  is the entry `state[4]` points at. `0xD3` is a record kind, so it is not
  evidence about the reply encoding after all.
- The client is a *record pump*: it takes a queued entry, sends `entry[8]` for
  `(u16)entry[2]/2` halfwords, and stores the reply into `entry[12]`. The
  meaning of `D0`/`F2`/`F1`/`D4` is therefore decided by whichever module
  enqueues those records, not by the transport and not by this client.

## The request templates and their builders (observed)

The client is a record pump, so the protocol is decided by whoever builds the
records. Two watchpoints on the transmit buffers of two known records name the
builders directly:

| buffer | written at | builder |
|---|---|---|
| `0xB23BE680` (the `D0` record) | `0x103B82A4`, `0x103B82AC` | `sub_103B8284` |
| `0xB23BFBC0` (an `F2` record) | `0x103B84A8`, `0x103B84B8` | `sub_103B847E` |

Both builders take their constants from a shared template table. `sub_103B7C22(id)`
returns `base + 12*id`, and reading the global the lookup uses (image literal
`0x103B7C50`) gives `base = 0xB18665B0`. Read out of a capture-time RAM dump the
table holds 35 slots of 12 bytes; slot 30 is empty and the other 34 are
populated:

| id | kind | flags | len | arg | id | kind | flags | len | arg |
|---|---|---|---|---|---|---|---|---|---|
| 0 | `0x16` | 0 | 152 | 1 | 15 | `0xF1` | 1 | 152 | 0 |
| 1 | `0x50` | 0 | 152 | 1 | 16 | `0xF1` | 2 | 152 | 0 |
| 2 | `0xD0` | 0 | 152 | `0x10` | 17 | `0xF1` | 3 | 152 | 0 |
| 3 | `0xD0` | 0 | 152 | `0x60` | 18 | `0xF1` | 4 | 152 | `0x52` |
| 4 | `0xD2` | 0 | 152 | `0x60` | 19 | `0xF2` | 0 | 152 | `0x10` |
| 5 | `0xD3` | 0 | 152 | `0x60` | 20 | `0xF2` | 1 | 152 | `0x10` |
| 6 | `0xD4` | 0 | 152 | `0x1C` | 21 | `0xF3` | 0 | 152 | 2 |
| 7 | `0xD0` | 0 | 152 | `0x10` | 22 | `0xF4` | 0 | 152 | 2 |
| 8 | `0xD5` | 0 | 152 | 1 | 23 | `0xF5` | 0 | 152 | `0x10` |
| 9 | `0xD6` | 0 | 152 | `0x60` | 24 | `0xF7` | 0 | 268 | `0x108` |
| 10 | `0xD7` | 0 | 152 | `0x0A` | 25 | `0xF8` | 0 | 152 | `0x10` |
| 11 | `0xD8` | 0 | 152 | 2 | 26 | `0xFA` | 0 | 152 | `0x10` |
| 12 | `0xD9` | 0 | 152 | 1 | 27 | `0xD0` | 0 | 152 | `0x10` |
| 13 | `0xE0` | 6 | 152 | 1 | 28 | `0xFF` | `0xFF` | 152 | `0x10` |
| 14 | `0xF1` | 0 | 276 | `0x110` | 29 | `0xD0` | 1 | 152 | 1 |

Entry layout: the record header pair appears twice in swapped order at bytes
0/1 and 2/3 (byte 2 = kind, byte 3 = flags, byte 1 = kind, byte 0 = flags),
then a `u16` length at 4, a `u16` argument at 6, and a `u32` tag at 8 whose low
byte is a step number.

Every length in the table is a whole number of halfwords and the `152` entries
are exactly the 76 halfwords the port clocks, so a template length *is* the
transfer length. The two long entries are `0xF1` at 276 bytes (138 halfwords) and
`0xF7` at 268 bytes (134 halfwords); every other entry is one 76-halfword frame.

The observed cycle uses templates 2 or 7 (`D0 0010`), 19 and 20 (the two `F2`
flag variants), 18 (`F1 04 0052`) and 6 (`D4 001C`), which is consistent with the
wire and with the flags byte the templates carry.

`sub_103B8284(ctx, buf, rx)` builds the plain form, byte for byte as the wire
shows it: `buf[0]=kind`, `buf[1]=flags`, `buf[2..3]=argument`, then the record
header (`kind`, `flags`, `argument (twice as length)`, `tx=buf`, `rx`, tag).

`sub_103B847E(ctx, buf, rx, request)` builds the `F2` form and adds two fields:

```
buf[2..3] = request->u16[6] + 16          // argument is payload length + 16
buf[4..5] = request->u8[0] ? 55 AA : AA 55   // write key, order chosen by a flag
buf[6..7] = request->u16[2]               // the 16-bit value
```

That explains the two shapes in the capture exactly: `00F2 0018 AA55 1FF0` is
`F2` with payload length 8 and value `0x1FF0`, and `00F2 0090 AA55 2000` is `F2`
with payload length `0x80` and value `0x2000`. The 128 payload bytes are zero in
the transmitted buffer — the builder zeroes the whole template length and writes
nothing past byte 7, so the firmware really does clock zeros there.

### What the opcode families do (inference, from structure)

- `F2` carries a write key and a 16-bit value plus a declared payload length, so
  it is the write direction. This is read from the builder, not guessed from the
  opcode's value.
- `F1` has five flag variants (`0..4`) and the only table entry with a long
  buffer is an `F1` (`0x110` argument, 276-byte transfer), which fits a read of
  a block longer than one transfer.
- `D0`, `D2`..`D9`, `E0`, `16`, `50`, `FF` have small arguments (1, 2, `0x0A`,
  `0x1C`, `0x10`, `0x52`, `0x60`) and identical 152-byte transfers, so they look
  like control/status commands distinguished by argument only.
- The meanings of those arguments are **not** established. Nothing measured so
  far connects `0x60` or `0x52` to a register, and neither the reply contents
  nor the selector logic of the builders have been decoded.

The record the client pumps is 0x2C bytes or more and reads, from a dump taken
mid-transfer with the `D0` record in flight:

```
rec+0x00  u16 kind = 0x00D0   u16 length = 0x0098
rec+0x04  u32 length = 0x98
rec+0x08  void *tx = 0xB23BECC0
rec+0x0C  void *rx = 0xB23C1B00
rec+0x28  u32 tag  = 0x02050020      (step 0x20, i.e. template id 7)
```

and its transmit buffer began `D0 00 10 00` — template 7, the `D0` with
argument `0x10` — so the identification of the template against the wire is
closed at both ends.

### Where the requests come from (verified)

A `BL`/`BLX` scan for the two builders leaves six call sites, all inside one
module:

| builder | call sites |
|---|---|
| `sub_103B847E` (F2 form) | `0x103AA8E0`, `0x103ABA94` |
| `sub_103B8284` (plain form) | `0x103AA6E6`, `0x103AB2D2`, `0x103ABA42`, `0x103ACCFE` |

All six lie in `0x103AA6E6`..`0x103ACCFE`, and the SPI4 completion handler the
client invokes after a successful transfer (`state[8]`, which a capture-time
dump shows as `0x103AB4EB`) sits inside the same range. So the module that
builds requests, the module that consumes completions and the module that
chooses the next request are one sequencer at `0x103AAxxx`..`0x103ABxxx`, and
the transport, the record queue and the record pump below it are all generic.

That is the boundary to cross for the remaining semantics: the arguments `0x60`,
`0x52`, `0x90` and the values `0x1FF0`/`0x2000`/`0x2080`/`0x0080` are chosen
inside that sequencer, not inside anything named above.

### Every template has exactly one builder (verified)

Scanning the image for `BL` whose target is `sub_103B7C22` finds 33 sites. Each
site's function loads a template id immediately before the lookup, so the
mapping is a direct read, not a guess. Taken together with a scan for the branch
targets inside `0x103B7000`..`0x103B9000`, which yields the builder entry points,
the catalogue closes:

| template | kind | builder | called from |
|---|---|---|---|
| 0 | `0x16` | `0x103B818E` | `0x103ABA12`, `0x103ACCA2` |
| 1 | `0x50` | `0x103B868C` | `0x103AC662`, `0x103ACCD0` |
| 2 | `0xD0` | `0x103B8284` | `0x103AA6E6`, `0x103AB2D2`, `0x103ABA42`, `0x103ACCFE` |
| 3 | `0xD0` | `0x103B7F80` | `0x103ACD2C` |
| 4 | `0xD2` | `0x103B8964` | `0x103ACD7C` |
| 5 | `0xD3` | `0x103B8A58` | `0x1069344C` |
| 6 | `0xD4` | `0x103B85CC` | `0x103AA6D6`, `0x103ABB20`, `0x103AC6A8`, `0x103ACE08` |
| 7 | `0xD0` | `0x103B8582` | `0x103AB9A6`, `0x103AC5EC`, `0x103ACE86` |
| 9 | `0xD6` | `0x103B7EF0` | `0x103AA69C`, `0x103ACF56` |
| 10 | `0xD7` | `0x103B88F0` | `0x103AB1EC`, `0x103ACEB8` |
| 11 | `0xD8` | `0x103B8DDA` | `0x103ACEF4` |
| 12 | `0xD9` | `0x103B8642` | `0x103AC738`, `0x103ACF3A` |
| 14 | `0xF1` | `0x103B82CE`, `0x103B83BE` | `0x103AABBA`; `0x103AA8A4`, `0x103ABAE0` |
| 15 | `0xF1` | `0x103B877C` | `0x103AA92E`, `0x103AAB76`, `0x103ABABA` |
| 16 | `0xF1` | `0x103B8874` | `0x103AA942`, `0x103AABDE` |
| 17 | `0xF1` | `0x103B87F8` | `0x103AA91A` |
| 18 | `0xF1` | `0x103B86EC` | `0x103ABA68`, `0x103AC76A`, `0x103ACF84` |
| 19 | `0xF2` | `0x103B847E` | `0x103AA8E0`, `0x103ABA94` |
| 20 | `0xF2` | `0x103B84EE` | `0x103AA906`, `0x103AABCC`, `0x103AB9EA` |
| 21 | `0xF3` | `0x103B81E0` | `0x103AC79A`, `0x103AD036` |
| 22 | `0xF4` | `0x103B8232` | `0x103AC62A`, `0x103AD064` |
| 27 | `0xD0` | `0x103B8538` | `0x103AA964`, `0x103AAB92` |
| 28 | `0xFF` | `0x103B8C3E` | `0x103AB17E`, `0x103AD00A` |
| 29 | `0xD0` | `0x103B8BA4` | `0x103ACFB2` |
| 31 | `0xE0` | `0x103B8CEA` | `0x103AB21A` |
| 32 | `0xE0` | `0x103B8BF4` | `0x103AB24A` |
| 33 | `0xE0` | `0x103B8D80` | `0x103AB294` |
| 34 | `0xE0` | `0x103B8D36` | `0x103AB1B4` |

Templates 8 (`0xD5`), 13 (`0xE0` flag 6) and 26 (`0xFA`) have no builder found
this way, and one lookup site at `0x103AA304` takes its id from a value that is
not a literal, so it is presumably the generic path for those.

Two facts fall out of the caller column:

- There are exactly **two** caller regions, `0x103AA69C`..`0x103AB2D2` and
  `0x103AC5EC`..`0x103AD064`, plus the single `0x1069344C` for template 5. So two
  sequences use the same transport, and one request outside both exists.
- Template 6 (`D4 001C`) is the only `0xD4` and it is called from four places, so
  `D4` is the common terminator of several exchanges rather than one script step.

### What the firmware reads out of a reply (observed)

The completion handler (`state[8]`, `0x103AB4EB` in a capture-time dump) takes the
reply buffer as its second argument and immediately reads three bytes from it:

```
ldrb r0, [reply, #8]
ldrb r2, [reply, #9]
ldrb r1, [reply, #14]
add  r4, reply, #20
```

so the response struct that the firmware expects has fields at offsets 8, 9, 14
and a sub-structure at +20. The handler also calls `sub_103B7BC2(reply, 18)`,
but that address is `bx lr` — a no-op stub in this build — so the `18` is not
evidence of a response length and is recorded here only to prevent a later reader
from mistaking it for one.

### The first step dispatcher (verified)

`sub_103AA658(step, ...)` compares its first argument and selects one builder:

```
cmp r0, #2   ; beq -> bl 0x103B8284   template 2   D0 0010
cmp r0, #6   ; beq -> bl 0x103B85CC   template 6   D4 001C
cmp r0, #9   ; bne -> return
             ;        bl 0x103B7EF0   template 9   D6 0060
```

and each taken branch ends by posting a result code through `sub_103AA0A2`:

| step | template | command | result code |
|---|---|---|---|
| 2 | 2 | `D0 0010` | `0x2CF` |
| 6 | 6 | `D4 001C` | `0x2B0` |
| 9 | 9 | `D6 0060` | `0x2C1` |

So a step number selects a command, and the command's completion is reported
under a distinct result code. The step numbering here is not the template tag
numbering (template 2's tag step byte is `3`), so they are two separate
enumerations and must not be conflated.

The same function region also holds a second dispatcher, `0x103AA70C`
(`cmp r0, #14; bcs return` followed by `tbb [pc, r0]` at `0x103AA73A`), which
routes fourteen further state values. Its jump table targets are not resolved in
this note: the table sits at a four-byte-aligned address that falls inside the
preceding instruction as objdump decodes it, so the offsets were not published
rather than guessed.

### Two protocol families share port 4 (verified)

The template table holds both, and the two families are distinct:

| family | templates | opcodes | lengths |
|---|---|---|---|
| update upload | 23, 24, 25, 26 | `F5`, `F7`, `F8`, `FA` | 152, 268, 152, 152 |
| observed cycle | 2/7, 18, 19, 20, 6 | `D0`, `F1`, `F2`, `D4` | 152, 152, 152, 152 |

The update family is the one already described in
[devices/update/protocol.md](../devices/update/protocol.md), which reads the same
code region (`103AA134..103AA464`) and states that the source emits `F5`/`F8`/`FA`
control packets of 152 bytes and `F7` packets of 268 bytes, with the MAS callback
consuming response byte 0 only. The template lengths in this file were measured
independently, from a RAM dump and from the wire, and agree with that document
exactly - 152, 268, 152, 152 - which is a useful check that both descriptions are
of the same code.

The cycle this note captures belongs to the **other** family. It never emits
`F5`, and the builders for templates 23, 24 and 25 exist but were never called in
any run recorded here. So port 4 carries a configuration exchange before (or
independently of) the upload exchange, and the two must not be conflated: a peer
that implements the documented upload opcodes alone will never satisfy the cycle
measured above.

### Reply byte 14 bit 2 is the ready flag (verified)

The completion handler saves three reply bytes and tests exactly one of them:

```
0x103AB4FA  ldrb r0,[reply,#8]
0x103AB504  ldrb r2,[reply,#9]      ; (rx[8], rx[9]) are passed on as a pair
0x103AB50A  ldrb r1,[reply,#14]
0x103AB50C  str  r1,[sp,#28]
...
0x103AB5C6  ldr  r0,[sp,#28]        ; the saved rx[14]
0x103AB5C8  lsls r0,r0,#29          ; bit 2 -> bit 31
0x103AB5CA  bmi  0x103AB960         ; set:   b.w 0x103A9DCE with r0=5, no wait
                                    ; clear: 0x103AB8EC -> bl 0x103A9DE8(500)
```

The clear path calls a delay of 500 units before returning 0; the set path returns
at once. Measured with an explicit byte placement (the peer takes `b14=04`, so an
offset cannot be miscounted):

| reply | transactions in a 40 s window |
|---|---|
| all zero | 14 |
| byte 14 = `0x01` | 14 |
| byte 14 = `0x08` | 14 |
| byte 14 = `0x04` | 67 |
| byte 14 = `0x04` (repeat) | 62 |
| byte 14 = `0xFE` | 54 |
| byte 14 = `0xFF` | 67 |
| byte 0 = `0x02` | 14 |
| byte 0 = `0x03` | 14 |
| every byte `0xFF` | 67 |

Bits 0 and 3 of byte 14 do nothing and bit 2 does everything, which is the bit
the code tests. The request sequence is the same thirteen transactions under
every one of these policies, so the flag does not decide *what* the firmware
sends - it decides whether each record waits 500 units before the next one. A
peer that sets the bit runs the fixed script about four and a half times faster.

This closes the reply question for this exchange with a real semantic rather than
a guessed status code: **response byte 14, bit 2 = ready**. It also explains why
the earlier "answer `D3 00` to `D0`" experiment showed nothing - word 0 is not a
field the firmware consults at all.

Method note, recorded because it cost a wrong answer: the first batch of tests in
this round placed bytes by counting hex characters in a literal string, and
several of those strings were one byte short, so those runs were unknowingly
placing their value at offset 13 instead of 14. That produced a self-consistent
but false "rule" (0xFF fast, 0x04 slow) which disagreed with the code. The peer
now accepts explicit placements and every row in the table above was measured
after that change; the miscounted runs are discarded rather than reconciled.

### The ready-bit peer leaves the progress indicators cold (observed)

With the ready flag set on every reply (`b14=04`) the peer completes every
transaction and the firmware runs its script 4.7 times faster than against a
zero-replying peer. Counting only the four progress indicators on that same
invocation:

| indicator | executions |
|---|---|
| display provider admission `0x104D7E78` | 0 |
| GUI dispatcher `0x1053E19A` | 0 |
| event-`0x39` poster `0x10695450` | 0 |
| EX ladder poster `0x0803350C` | 0 |
| SPI4 pump `0x103BC266` | 45 |
| transport start `0x10370A14` | 133 |

66 transactions and 45 pump iterations leave those points cold. Satisfying the
observed ready-bit wait changes how often the firmware re-runs the fixed
thirteen-transaction script; it does not, by itself, start the GUI or the EX
ladder in this window. This peer is synthetic and does not qualify the complete
device protocol.

The measured effect of byte 14 bit 2 is now known, and satisfying this wait does
not by itself advance the camera. The next investigation of `0x104D7E78` or
`0x0803350C` should trace its own startup producer rather than add more SPI4
reply assumptions without a demonstrated consumer.

### What the two `F2` arguments mean (verified + one inference)

Two code facts fit the measured arguments exactly. The `F2` builder sets

```
buf[2..3] = request->u16[6] + 16
```

and the caller that builds the request at `0x103AA8C2` computes it like this:

```
ldrh r1,[ctx,#28]        ; total length
cmp  r1,#0x80
bcc  1f                   ; shorter than 128: use it as is
ldrh r2,[ctx,#32]         ; already sent
subs r1,r1,r2             ; remaining
uxth r1,r1
cmp  r1,#0x80
bcc  1f
movs r1,#0x80             ; clamp to 128
1:  strh r1,[ctx,#30]     ; this transfer's chunk length
```

So the chunk length is `min(remaining, 128)` and the wire argument is that chunk
length plus 16. Reading the two measured values back through that rule:

| wire argument | chunk length | reading |
|---|---|---|
| `0x18` | 8 | an 8-byte block write |
| `0x90` | 128 | a full 128-byte block write |

The `16` is the size of the header in front of the payload — command word, flag
byte, argument, key (`AA55`) and value — which is exactly the eight bytes the
builder fills before the payload region. That `F2` is a **block write of up to
128 bytes** is an inference from those two facts, not a separate measurement; the
payload was zero in every captured frame because the source buffer was empty at
that point in the boot, while the length bookkeeping had already been set.

Each step also posts a numbered result code through `sub_103AA0A2`, and the codes
are distinct per step, which is how a failure would be attributed:

| call site | builder | result code |
|---|---|---|
| `0x103AA8A4` | `sub_103B83BE` (template 14, `F1`, 276 B) | `0x12E` |
| `0x103AA8E0` | `sub_103B847E` (template 19, `F2`) | `0x152` |
| `0x103ABA42` | `sub_103B8284` (template 2, `D0`) | `0x286` |
| `0x103ABA68` | `sub_103B86EC` (template 18, `F1` flag 4) | `0x296` |
| `0x103ABA94` | `sub_103B847E` (template 19, `F2`) | `0x2A6` |

### Are the rarely-observed clients ever reached? (partly answered)

A 150 s run with a satisfying peer, filtered to a 0x100-byte window around each
of the eleven call sites that no breakpoint had ever caught:

| window | executions |
|---|---|
| `0x10367FB8` (site `0x10368038`, record writer) | 0 |
| `0x10368076` (site `0x103680F6`, record writer) | 8 |
| `0x1036EAE0` (site `0x1036EB60`, generic transfer) | 53228 |
| `0x10382AFC` (site `0x10382B7C`) | 0 |
| `0x10382CF2` (site `0x10382D72`) | 0 |
| `0x10383086` (site `0x10383106`) | 0 |
| `0x103830EC` (site `0x1038316C`) | 0 |
| `0x10383144` (site `0x103831C4`) | 0 |
| `0x10383348` (site `0x103833C8`) | 16 |
| `0x10481794` (site `0x10481814`) | 9 |
| `0x104BA278` (site `0x104BA2F8`) | 9 |

**These are window counts, not call counts**, and the distinction matters: a
window counts every translation block that *starts* inside it, so the
`0x1036EBE0` entry's 53228 is dominated by unrelated code that happens to be
adjacent, and a non-zero window does not prove that the call site itself ran. A
precise per-site count needs the site's own basic-block start, and none of these
eleven sites sits on one. What the run does support is the weaker statement that
five of the eleven call sites have no executed code within 128 bytes of them in
150 s, so those clients are not active during this part of the boot at all.

### The other two reply bytes are diagnostic, not protocol (verified)

Only the ready flag changes behaviour, and the reason the other two bytes do not
is now visible. The handler calls `sub_103B7C0A(rx[8], rx[9])`, which is

```
0x103B7C0A  push {lr}; sub sp,#28
            strd r0,r1,[sp]        ; {rx[8], rx[9]} into a 12-byte local
            strb r2,[sp,#8]        ; and a zero byte
            bl   0x1035E1AA
0x1035E1AA  movs r2,#28
            ldr  r0,<global>       ; 0xB23D05C4  (literal 0x1035E458)
            b.w  0x10360F40        ; copy 28 bytes into that global
```

and the global in a capture-time dump reads

```
0xB23D05C4  00 00 00 00 00 00 00 00  0F 0A 37 10  00 00 00 00
            50 66 B3 B2  04 00 00 00
```

i.e. the two reply bytes in the first eight, then `0x10370A0F` (a return address
inside the transport), then `0xB2B36650` (the SPI4 job descriptor) and the
channel number `4`. So `rx[8]`, `rx[9]` are published into a **last-transfer
trace record**; they are diagnostic fields, and the firmware's own behaviour does
not branch on them. That is why setting them to `0xFF` changed nothing, and it is
consistent with the repo's earlier note that the upload family consumes response
byte 0 only - each family reads only the fields it defines.

### Where this leaves the request/response semantics

From the MAS side the protocol is now characterised end to end:

- a fixed thirteen-transaction script, each transaction one 76-halfword frame;
- frame layout `{kind, flags, argument16}` plus, for `F2`, `{key, value16}` and a
  payload region of `argument - 16` bytes, chunked at `min(remaining, 128)`;
- the only reply field with a behavioural effect is **byte 14 bit 2 (ready)**;
  bytes 8 and 9 are copied into a trace record and byte 0 is not consulted here;
- the script was identical under the reply policies tested here; this does not
  establish the behaviour of untested replies or later sequencer states.

What the *arguments mean to the device* is not derivable from this image: the
registers `0x18`, `0x52` and `0x90` and the bit patterns `0x1FF0`, `0x2000`,
`0x2080` and `0x0080` are decided by the sequencer and interpreted by the VR. A
useful lead for that half is the file `3_vr1840_010100.bi.bin`, which is
exactly `3201 * 262 = 838662` bytes - the size the update note gives for the
complete VR upload stream - so that component is the VR payload in update-record
form. Its body can be extracted for the standalone RX profile, but the stream
omits the resident interval needed to run it past startup. The
[VR execution note](../startup/vrdomain.md) records that boundary; the device-side
meanings of these commands remain unresolved.

### Channel attribution for all fourteen call sites (verified)

Every call site loads its channel from a small state object with the same idiom
as the two sites that a breakpoint measured directly, so the channel can be read
statically: find the literal the site loads its object register from, then read
the byte at the offset the site indexes. Doing that against a capture-time RAM
dump gives a channel for all fourteen sites:

| call site | object | channel |
|---|---|---|
| `0x1020B6D4` | (breakpoint) | **1** |
| `0x1020B88A` | (breakpoint) | **0** |
| `0x10368038` | `0xB18A7808 + 0` | 8 |
| `0x103680F6` | `0xB18A7808 + 0` (`r5` then `mov r10,r5`) | 8 |
| `0x1036EB60` | `0xB1807B10 + 0` | 6 |
| `0x10382B7C` | `0xB18A7840 + 3` | 3 |
| `0x10382D72` | `0xB18A7840 + 3` | 3 |
| `0x10383106` | `0xB18A7850 - 16 + 3` = `0xB18A7843` | 3 |
| `0x1038316C` | `0xB18A7850 - 16 + 3` = `0xB18A7843` | 3 |
| `0x103831C4` | `0xB18A7850 - 16 + 3` = `0xB18A7843` | 3 |
| `0x103833C8` | `0xB18A78E8 + 1` | 2 |
| `0x103BC2A8` | `0xB18A75A0 + 1` | **4** |
| `0x10481814` | `0xB18084F8 + 0` | 9 |
| `0x104BA2F8` | `0xB1808144 + 2` (`literal - 12`) | 9 |

Channels 0, 1, 2, 3, 4, 6, 8 and 9 are therefore attributed and channels 5, 7 and
10 have no client. **Five** sites share channel 3: `0x10382B7C`, `0x10382D72`,
`0x10383106`, `0x1038316C` and `0x103831C4`, all indexing the object at
`0xB18A7840`, which reads `00 05 02 03 01` and is therefore initialised.

Two of the columns are independent checks rather than new claims: the breakpoint
measured 0, 1 and 4, and the static read of those same three sites agrees, which
is what licenses reading the remaining nine the same way.

The attribution of channel 9 does **not** come with an observation. Watching the
transport context of a channel is a working instrument -
`ctx + 0x5C` is written once per transport call, and a watchpoint on channel 4's
context (`0xB1808E20 + 104*4 + 0x5C = 0xB180901C`) caught exactly the expected
event, `val = 2`, the job type the SPI4 client uses - but the same watchpoint on
channel 9 (`0xB1809224`) saw nothing in 120 s. That null result is weak evidence:
because a breakpointed run advances the guest roughly thirty times slower, the
channel-4 watchpoint caught only a single event in 90 s, so the absence of a
channel-9 event does not establish that the channel is unused. What the run does
establish is that the context-write instrument works and reports the job type.

### What the cycle is preparing for (verified)

The `F2` block writes of the cycle only make sense as preparation, and the payload
they prepare for turns out to be present in the bundle. `3_vr1840_010100.bi.bin`
is 838,662 bytes, which is `3201 * 262`, and decoding it as the update stream the
[update note](../devices/update/protocol.md) describes gives:

- record 0 is metadata and carries the ASCII string `NikonBVR`;
- records 1..3200 are a 6-byte header (`04 01`, then a little-endian `u32`
  destination) followed by exactly 256 body bytes;
- the destinations are contiguous: `0xFFF00000 + 0x100 * n` for `n = 0..3199`.

So the stream covers `0xFFF00000 .. 0xFFFC8000`, a single 819,200-byte image, which
is exactly the `upload-storage-size=819200` the update profile uses. Every one of
the 3200 records checked, with no gaps and no exceptions.

That has one consequence worth recording. The VR's failing access in the earlier
investigation was to `0xFFFC8008`, and the top of this uploaded image is
`0xFFFC8000` - the absent slot is **eight bytes above the end of the image the
bundle carries**. So the resident slot the VR needs is not missing from this
payload by truncation or by a bad record; the payload ends exactly where it ends,
and the slot lies beyond it. That corroborates the earlier conclusion that
`0xFFFC8008` is package-external rather than something the update stream should
have supplied.

The `D0`/`F2`/`F1`/`D4` cycle is therefore the configuration exchange that a peer
must satisfy *before* the `F5`/`F7`/`F8`/`FA` upload family can run - which is
consistent with the upload family never appearing in any capture of this cycle,
and with the cycle repeating instead of progressing.

### Correction: the three "uninitialised" sites are channel 3

An earlier revision of this file reported that `0x10383106`, `0x1038316C` and
`0x103831C4` could not be attributed because the object they load, `0xB18A7850`,
is all zero in every dump. That was wrong, and the mistake is worth naming: the
literal at `0x103832A8` holds `0xB18A7850`, but the function that owns those three
sites subtracts sixteen from it immediately after loading:

```
0x1038305A  stmdb sp!, {...}
0x1038305E  ldr  r4,[pc,#584]   ; r4 = 0xB18A7850
0x10383062  subs r4,#16         ; r4 = 0xB18A7840
```

so the object is `0xB18A7840` - the same one the two `0x10382Bxx` sites use - and
its byte `+3` is `3`. The object is initialised in the dump (`00 05 02 03 01`), and
the "all zero" reading came from looking sixteen bytes too high. Reading the
table above with that fixed, every one of the fourteen call sites has a channel.

The client behind those five sites is a retry-driven command handler: it
increments a counter at `object+0` and only proceeds while it is `1`, dispatches
on a command byte at `*(0x103832B4) + 0x50 + 4` (values `0x16` and `0x21`
observed), timestamps at `object+0x20`, and calls the transport with
`object+3` as the channel.

### The frame grammar of the observed cycle (verified)

Assembling the four builders against the capture gives the complete grammar of
the thirteen-transaction script. Byte offsets are within the 152-byte transmit
buffer; every unnamed byte is zero because each builder clears the whole template
length first.

`D0` (templates 2, 7, 27; builder `sub_103B8284`):

```
0  0xD0        kind, from the template
1  0x00        flags, from the template
2  0x10        argument, from the template (little-endian u16)
```

`F1` flag 4 (template 18; builder `0x103B86EC`):

```
0  0xF1        kind
1  0x04        flags
2  0x52        argument, from the template
```

`F2` (templates 19 and 20; builder `sub_103B847E`):

```
0  0xF2        kind
1  0x00 | 0x01 flags, from the template
2  chunk + 16  argument, computed: payload length plus the 16-byte header
4  0x55 0xAA   key, or 0xAA 0x55 when the request's first flag is zero
6  value       little-endian u16 from the request
8  ...         payload region, `chunk` bytes, zero in every capture
```

`D4` (template 6; builder `sub_103B85CC`):

```
0  0xD4        kind
1  0x00        flags
2  0x1C        argument, from the template
4  0x83 | flags status, computed
```

and the status byte is `0x83`, plus `0x40` when the request's first flag is `1`,
plus `0x20` when its second flag is `1`:

```
0x103B85E0  ldrb r0,[r7,#0] ; cmp #1 ; r9 = 0x40 or 0
0x103B85EE  ldrb r0,[r7,#1] ; cmp #1 ; r8 = 0x20 or 0
0x103B860C  orr.w r2,r0,#0x83
```

The capture's `00D4 001C 00E3` is exactly that with both flags set
(`0x83 | 0x40 | 0x20 = 0xE3`), so the last transaction of the cycle reports
`0x83` as its base with both optional bits raised.

Read that way the cycle is a self-consistent exchange rather than an opaque byte
run: open with argument `0x10`, read with argument `0x52`, write blocks of up to
128 bytes under a `0xAA55`/`0x55AA` key, and close with argument `0x1C` and a
status byte.

## What remains

- The **device-side** meaning of the arguments is not derivable from this image.
  `0x10`, `0x52`, `0x1C`, `0x18` and `0x90`, and the values `0x1FF0`, `0x2000`,
  `0x2080`, `0x0000` and `0x0080` written under the `AA55` key, are chosen by the
  MAS sequencer and interpreted by the VR. The bundle's update stream yields the
  application body, but not the external resident implementation; the standalone
  RX startup probe does not yet produce a reply. Further device-side semantics
  need code or evidence for the actual command consumers.
- Whether the fixed cycle ever **progresses** to the `F5`/`F7`/`F8`/`FA` upload
  family. It did not in any capture recorded here, including with a peer that sets
  the ready flag; the builders for templates 23, 24 and 25 exist but were never
  called.
- The gate for the four progress indicators remains on the mainline: satisfying
  the observed SPI4 ready-bit wait leaves all four at zero. Trace the display
  startup producer before expanding the VR model.
