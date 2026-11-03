# Services

The service family is dispatched by op `0x94FD` (no data phase), `0x94FE` (with a host
data phase) or `0x94FF` (a third variant). Parameter 0 carries the subcommand; parameters
1–4 are the subcommand's own. The subcommand table lives at `VA 0x119e405c` with 28-byte
rows.

## Row layout

A row is 28 bytes: the subcommand as a `u16`, a flags word, a context word, and four
callbacks — `+0x0c` for an inbound data phase, `+0x10` as the gate, `+0x14` as the
execute callback and `+0x18` as a lifecycle hook. The field table and the complete
175-row listing are in [Dispatch tables](dispatch); the table numbers four families —
159 `F` rows, 8 `A`, 3 `B` and 5 `C` — of which only the `F` rows below have been
driven.

The gate is the only part of dispatch that decides whether parameters are acceptable: a
zero return refuses and the gate callback itself builds the error answer, a non-zero
return runs the execute callback. A subcommand other than `F001` also has to arrive on an
unlocked session, otherwise the gate is never reached and the answer is `0xa004`.

Because the validate callback owns the parameter checks, the parameter slots are not
interchangeable. `F02C` reads its block selector from parameter 1 and rejects `0` and
anything above `0x3f`; `F029` reads a length from parameter 2 and a selector from
parameter 3. Passing the right number of parameters in the wrong slots looks like a
refusal, not a protocol error — check the observed request shapes below before concluding
that a subcommand is unusable.

## Payload catalogue

The data-returning subcommands observed on an idle body, with the first bytes of the
archived capture:

| Subcommand | Length | Archived first bytes | Note |
| --- | --- | --- | --- |
| `F003` | 128 | ASCII | identity record; see the selector table |
| `F006` | 4 | `09190000` | status word `0x1909` |
| `F04C` | 60 | zeros | task and clock record, content not decoded |
| `F050` | 28 | `020000003031303030313630` | keyed record, see below |
| `F051` | 28 | `020000003031303030313630` | keyed record, see below |
| `F052` | 80 | `020000003031303030313630` | keyed record, see below |
| `F06E` | 296 | `387f010000000000` | block addressed, see below |
| `F113` | 8 | `cec1d809a4110000` | words `0x09d8c1ce`, `0x11a4` |
| `F114` | 12 | `9d995e01b77b0700090b8900` | words `0x015e999d`, `0x00077bb7`, `0x00890b09` |
| `F162` | 12 | zeros | bootstrap scratch, unpopulated |
| `F163` | 1402 | zeros | bootstrap scratch block, unpopulated |
| `F1F9` | 8 | `0100000004000000` | the pair `1`, `4` |

The two "unpopulated" rows read as zeros on an idle body; that is a state observation,
not a claim that the fields are unused. `F113` and `F114` were catalogued from the
archive-wide scan and were not re-read here.

`F04C` is the one row where the archive and the live observations disagree: the archived
capture is 60 zero bytes, while a live read of the same subcommand once returned
`07000000b72aa52520ca0000`. The record is therefore read-through of state that can be
either empty or populated, and its layout is not settled by either capture.

### Keyed records `F050`–`F052`

All three begin with the same sixteen bytes: a `u32` value of `2`, then the eight ASCII
characters `01000160`. The bodies then differ in both length and shape.

| Subcommand | After the sixteen-byte prefix |
| --- | --- |
| `F050` | `0xa6`, `0x00021800`, `0x007f000d`, `0` |
| `F051` | `6`, `0`, `0x00010500`, `0` |
| `F052` | `0x1e`, then 32 `u16` values |

`F052`'s values are 32 entries long although the leading word says 30, so that word is a
field rather than the array length. The values are nearly all `2`, `3` and `5`, with two
entries that only look different because they are byte pairs (`0x0705`, `0x0602`) read as
`u16`; the array is better read as a byte table over a small alphabet than as a count
paired with lengths. `F050` and `F051` are similarly sparse — two large words and one
byte pair each.

The eight-character prefix is not a stored string: it is assembled by the executor. The
first four characters are a literal `0100` at `VA 0x10774650`, copied by an `adr`-relative
copy inside `F050`'s executor `0x10774412`. The next four are produced by the helper at
`0x107743d2`, which reads string-table id `0x3900`, skips `.` characters among the first
four source characters and zero-pads the result to four digits. The prefix is therefore a
version tuple — a fixed `0100` followed by the same four-digit field that `F003` selector 4
reports as `0160b1`, i.e. the `1.60` body version rendering as `0160`. That reading also
explains why the three subcommands share it and why the string exists nowhere in the image
as ASCII.

The rest of the record is generated the same way rather than read out of a static blob,
which is why the archived bytes look sparse. After the prefix, `F050`'s executor appends
nine values taken from the RAM state block at `0xB188BDE8` through the accessor at
`0x101004a0` (one halfword per four-byte slot), clamps each to `0`–`0xff` and appends it as
a single byte. The archived payload's bytes 12–20 are exactly that run:
`a6 00 00 00 00 18 02 00 0d`. The buffer is a fixed 28 bytes that `memset` zeroes before
anything is written, so the seven bytes after the run come from further append helpers
(`0x105014fe`, `0x10501522`, `0x10501532`) whose exact contribution was not resolved. That
is the remaining gap in this record, not a missing capture.

`F051` and `F052` carry their own executors at `0x107744d8` and `0x1077459e`, in the same
three-callback shape and with the same version prefix, so the family is three sibling
records rather than one record with a mode argument.

### Block-addressed record `F06E`

The captures for this subcommand differ in length by request, and every one of them begins
with the same four bytes `387f0100` (`0x00017f38`) followed by zeros. The archived
captures are:

| Request | Length |
| --- | --- |
| block 0 | 296 |
| block 1 | 20038 |
| block 2 | 577 |
| block 3 | 12 |
| block 5 | 44 |

Block 4 is absent from the archive, so it either refused or was not requested. Because the
lengths differ by a factor of sixty across blocks, the parameter selects a block rather
than a byte range, and the first word is a per-block field rather than a total size — it
is identical in all five captures. Reading it as a table of contents would require one
more capture per block with a non-zero tail; on this body the archived blocks are almost
entirely zero after the first word, which is why the record stays undecoded.

Two conventions matter when reading the observations below:

- a subcommand can accept a request, answer `0x2001`, and return **no data** — either
  because the requested value is genuinely absent, or because the handler refuses quietly;
- the archive-wide scan of `F000`–`F200` found 490 responding subcommands and only eleven
  that returned data with zero arguments, so "answers" is not "does something".

Handler addresses are C 1.70 object code.

## Session control

### `F001` Unlock service mode

Parameters `0,0,0,0`. Returns `0x2001` and enables the rest of the service family.
Handlers `0x1047994d`, `0x104798df`.

### `F002` Lock service mode

Parameters `0,0,0,0`. Returns `0x2001`. Send it before closing the session; leaving the
body unlocked works but changes the state a later run will observe.

### `F003` Identity records by selector

Parameters `selector,0,0,0`, returns 128 bytes. Observed selectors:

| Selector | Payload (ASCII) | Reading |
| --- | --- | --- |
| 1–3 | `Z 7_2` | model identity |
| 4 | `0160b1` | firmware identity string |
| 5 | `012000` | resource or build identity |
| 6 | `003` | short counter or build field |
| 8 | `------` | placeholder |
| 9 | `010100` | version pair |

Selectors 0, 7, 10 and 11 returned `0x2001` with no data. The strings are the vendor's
own labelling; the field meanings beyond the firmware string are not established.

## Status and telemetry

### `F006` Engine status word

Parameters `0,0,0,0`, returns 4 bytes. Observed `09190000`.

### `F04C` Task and clock telemetry

Parameters `0,0,0,0`, returns 60 bytes. Reads as zeros on an idle body and as a populated
record on a body that has been running; the layout was not decoded.

### `F06E` Body parameter record

Parameters `block,0,0,0`, returns a block whose length depends on the block: 296 bytes for
block 0, 20038 for block 1, 577 for block 2, 12 for block 3 and 44 for block 5. Every
block begins `387f0100`. See the block table above.

### `F113` / `F114` Counter pairs

`F113` returns 8 bytes and `F114` returns 12 bytes. The archived captures are
`0x09d8c1ce`/`0x11a4` and `0x015e999d`/`0x00077bb7`/`0x00890b09`. In each capture the
first word is by far the largest, which is the shape of a monotonic tick counter with one
or two derived fields beside it; the readers were not re-run, so neither the units nor
whether the values increase across calls is established.

### `F162` / `F163` Bootstrap scratch

12 bytes and 1402 bytes. Both read as zeros on an idle body; the archive labels `F163`
the bootstrap scratch block. No consumer was established.

### `F1F9` Subsystem status

8 bytes, archived as the pair `1`, `4` rather than zeros. The two words are small enough
to be an index and a count; no second capture exists to say whether either changes.

## Resource reads

### `F029` / `F02A` Resource selector read

Parameters `0,length,selector,0`.

`F029` resolves object 71 and streams `length` bytes from its descriptor. Observed with
`length = 0x1000`: selectors `0`–`6` return data, everything beyond `6` returns nothing.
Payloads: selector 2 begins `83020000336f7f01`, selector 4 `7d2a000042e20000`, selector 5
is all `0xff`, selector 6 begins `414661646a757374` — the ASCII `AFadjust…` signature.

This is the read path the earlier physical capture used for NVRAM-style resources. It is
bounded by the object descriptor, not by the address guard, so it is unaffected by the
RAM map restrictions.

Handler `0x104af8f2` (`F029`), `0x104afa30` (`F02A`).

## Address access

### `F032` Address read

Parameters `address,length,0,0`. Streams `length` bytes from `address` and refuses inside
the guard's intervals (see [Memory](memory)).

The gate `0x104b010a` rejects a zero length and any range the guard denies, answering
`0x201d` with an inner parameter of `2`. The executor `0x104b020c` classifies the address
through `0x103c5ab0` and takes one of two backends before answering `0x2001`, or `0x2007`
if the backend reports failure. Neither backend is DMA: the bytes are copied into the
served buffer by the CPU through `0x10360fac`.

### `F033` Address write

Parameters `address,length,0,0` plus a host data phase of `length` bytes, `length` up to
`0x400`. Copies the data phase into memory. **Must be sent with `0x94FE`**; a data phase
sent with `0x94FD` is never received and hangs the session.

The three callbacks are the clearest example of the row layout:

| Field | Address | Role |
| --- | --- | --- |
| `+0x0c` | `0x104b026a` | publishes the received data-phase pointer and length into two out-parameters |
| `+0x10` | `0x104b0280` | gate: length non-zero and at most `0x400`, then guard over `[address, address+length-1]`; refuses with `0x201d` and inner parameter `0` |
| `+0x14` | `0x104b02c8` | execute: `memcpy(address, buffer, length)` through `0x10360fac`, then answers `0x2001` with the copied length as a parameter |

The gate and the executor both read the destination from parameter 1 and the length from
parameter 2, which is why a write sent as `F033 [0, length, 0, 0]` is refused rather than
misinterpreted. The address guard is consulted by the gate only; the copy itself is
unconditional once the gate returns.

## Object streams

Each of these resolves a fixed registry object and streams from its descriptor. The
descriptor can be rewritten with [the address write](memory), which is how a service can
be pointed at a region the address guard denies.

### `F021` / `F022` Object 17

Parameters `offset,length,0,0`; streams `object17.base + offset` for `length` bytes.
Object 17's record was at `VA 0xB185E8D8` with base `0xB7F4A000` and length `0x1FFFC`.
Reading `0x10000080` through it after rewriting the base to `0x10000000` is the procedure
in [Key recovery](recovery).

Handlers `0x104af3c2` (`F021`), `0x104af484` (`F022`).

### `F024` / `F025` / `F026` Object 53

Same shape, object 53. Requests returned no data in the tested configurations.

Handlers `0x104af600`, `0x104af6f6`, `0x104af7b2`.

### `F02C` / `F02D` / `F02E` Object 8

Parameter 1 is a **block selector, 1 to `0x3F`** (zero and values above `0x3F` are
rejected with `0x201d`). Block 1 and 2 returned no data with the stock descriptor.

Object 8 is the same record the address guard reads for its runtime interval, so patching
it affects both the object service and the guard.

Handlers `0x104afb6e`, `0x104afc4c`, `0x104afd4a`.

### `F02F` / `F030` / `F031` Object 1 — boot material

These operate on object 1, the boot-material object that maps the NOR window. They are
**not read services**: `F02F`'s validate callback accepts only parameter 1 equal to `0`
or `1`, and its executor runs a commit-style sequence (`movw #0x4d5`, then object-8
helpers). `F031`'s executor walks a bitmask of object ids and runs a 20-byte comparison
through the same compare routine the update digest uses, i.e. it is an integrity check.

No configuration of these three returned data. They are the closest thing to a boot-region
writer in the service family, and they were not driven further because a wrong value can
damage the material.

Handlers `0x104afe46` / `0x104afe7c` (`F02F`), `0x104afefa` (`F030`),
`0x104aff3a` / `0x104aff94` (`F031`).

### `F037`–`F03B` Object 10

Same stream shape over object 10. Tested requests returned no data.

Handlers `0x104b06ba`, `0x104b0762`, `0x104b07dc`, `0x104b0860`, `0x104b0922`.

### `F03C`–`F03E` Object 9

Object 9 is also the region `0x90C0` captures through. Tested requests returned no data.

Handlers `0x104b09e0`, `0x104b0a2e`, `0x104b0aea`.

## Admission and reset

### `F091`–`F094` Provider admission

Parameters `0,0,0,0`. All return `0x2001` after `F001`, with no data and no observed effect
on memory, authentication state or the log ring. `F091` is the command the earlier
F091 boundary work analysed: it is rejected (`0xa004`) while service mode is locked and
accepted once `F001` has run.

### `F165` Bootstrap

Parameters `0,0,0,0`, returns `0x2001`, no data. Reading the digest context and the object
records immediately afterwards showed no change, so it does not start an authentication.

## How to probe an undocumented subcommand

1. Call it with `0,0,0,0` and record the response and data length.
2. If it answers `0x2001` with no data, try `0,0x1000,0,0` and `0,0,0x1000,0` — several
   handlers take a length in a different slot.
3. Treat a missing response as a hang: power-cycle instead of retrying.
4. Compare any refusal code against the `0x10ff` control, which answers `0x2005`.
