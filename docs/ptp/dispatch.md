# Dispatch Tables

Every request this body accepts over PTP is resolved through static tables compiled into
the image: a 16-bit operation code selects a row, and for the vendor service wrapper a
second 16-bit subcommand selects another row in a second table. Those tables are the most
direct statement of what the firmware implements, because they do not depend on what the
body chooses to advertise in its device-info dataset.

All addresses on this page are C 1.70 object code in the MAS payload, with
`VA = 0x10020000 + O`; they were read from that image and not observed over the wire.
The wire behaviour they predict is recorded in [Operations](operations) and
[Services](services); this page is the reference layer underneath both.

## Routing a command

A command container carries its code in the header. Resolution then runs in this order:

1. the opcode's high bits select a class: a predicate chain at `0x10463d70` tests the
   opcode against the families in a fixed order, `table_for_class` at `0x10478ebc` maps
   the class to one of the operation tables below, and a linear search at `0x10478ef6`
   walks 28-byte rows until the key matches or a zero key ends the table; a code that no
   table contains is answered `0x2005` — `0x10FF` is the control case, absent from every
   table and answered with exactly that code;
2. the vendor service wrappers (`0x94FD`, `0x94FE`, `0x94FF`) take their real selector
   from parameter 0 and search the subcommand table for it; a subcommand with no row is
   answered `0x201d`, not `0x2005`, and the refusal response carries a sixth parameter
   that is `2` for `0x94FF` and `0` for the other two wrappers;
3. a service subcommand other than `F001` requires the session to be unlocked already;
   with the gate closed the answer is `0xa004`, which is why `F001` works on a closed
   session and everything else does not;
4. once admitted, the row's validate callback runs. A zero return refuses with whatever
   code that callback builds — `0x201d` on a bad address or length;
5. the row's execute callback runs and fills the response, optionally with a data phase;
   an internal failure there answers `0x2007`;
6. for a wrapper operation that carries an inbound data phase, a third callback is
   registered in advance and the transport calls it when the data container arrives.

The vendor wrapper path is object code at `0x104c6b14`: it reads the subcommand from the
request block, runs the admission check through `0x104c6ab4` (which forwards to
`0x104e00be`), looks the row up, and then calls the validate callback through `0x104c6a80`.
The execute callback has its own entry, `0x104c6b9c` calling `0x104c6a92`. The `0x94FE`
data callback is registered as `0x107722f0`, which forwards to `0x104c6aec` and then to
`0x104c6a6c`.

## Row layout

Every row in every table below is 28 bytes, and the field roles are the same in both the
operation tables and the subcommand table.

| Offset | Width | Role | How it is used |
| --- | --- | --- | --- |
| `+0x00` | u16 | key | operation code or subcommand; a zero key ends the table |
| `+0x02` | u16 | padding | zero in every row of this image |
| `+0x04` | u32 | flags | only `0` and `1` occur; see below |
| `+0x08` | u32 | context | first argument passed to every callback of the row |
| `+0x0c` | u32 | receive | callback for an inbound data phase |
| `+0x10` | u32 | validate | gate callback; a non-zero return allows execution |
| `+0x14` | u32 | execute | builds the answer |
| `+0x18` | u32 | lifecycle | callback taking a pointer to the context word |

Two properties of the pointer fields hold without exception across all 1035 callback
values in the nine tables below: every callback address has the Thumb bit set, so a value
such as `0x104b0097` is code at `0x104b0096`, and every context value is even, i.e. a data
pointer into RAM rather than code.

The flags word takes only the values `0` and `1`. Where a family shares one validate
callback and splits on flags — `0x90E8`–`0x90FB`, `0x9350`–`0x9361` and `0x9411` all share
validate `0x1052da0f` — flag `1` always pairs with execute `0x1052db63` and flag `0` with
execute `0x1052da3d`. Both handlers exist per row and the flag chooses between them, so
the flag is part of the answer shape rather than a comment. It does not mean "carries a
data phase" in a way that would let a host predict the reply: `0x94FD` answers with data
and carries flag `0`, while `0x94FF` carries flag `1` and no data has been observed from
it. Treat the flag as dispatch state and read the paired handlers instead.

The lifecycle callback is called once per row by the table walk at `0x104c6a28`, which
iterates the subcommand table with a pointer to each row's context word. Whether that walk
runs at start-up or at teardown was not established; for the `F032`/`F033` rows the field
points at `0x104b0097`, which resolves to a bare `bx lr`.

## The table set

The image contains nine 28-byte-row tables: eight keyed by operation code and one keyed by
service subcommand. They sit in two clusters — the operation tables are adjacent to each
other in one region, and the subcommand table is in another.

| Table | VA | Rows | Keys |
| --- | --- | --- | --- |
| T1 | `0x119e1480` | 19 | `0x1001`–`0x101B` |
| T2 | `0x119e16b0` | 70 | `0x90C0`–`0x90CF`, `0x9201`–`0x920F`, `0x9400`–`0x9464`, `0x9455`–`0x945A`, `0x9535`, `0x9801`–`0x9805` |
| T3 | `0x119e1e74` | 3 | `0x94FD`–`0x94FF` |
| T4 | `0x119e1ee4` | 38 | `0x90E8`–`0x90FB`, `0x9350`–`0x9361`, `0x9411` |
| T5 | `0x119e2328` | 11 | `0x9509`–`0x950F`, `0x9407`–`0x940A` |
| T6 | `0x119e2478` | 12 | `0x9520`–`0x9523`, `0x9527`–`0x952A`, `0x9530`–`0x9533` |
| T7 | `0x119e25e4` | 2 | `0x9010`, `0x952B` |
| T8 | `0x119e2638` | 6 | `0x92F0`, `0x92F3`–`0x92F7` |
| S | `0x119e405c` | 175 | `0xF001`–`0xF2BB`, `0xA001`–`0xA102`, `0xB002`–`0xB004`, `0xC001`–`0xC102` |

The 161 operation codes across T1–T8 are all distinct, so the eight tables partition the
code space rather than repeat it, and each table is terminated by its own zero key.

Do not confuse the implemented set with the advertised one. The device-info dataset
advertises 94 operations while the tables hold 161 codes, and the vendor families are far
larger than a stock PTP body advertises. Whether a particular code appears in the
advertised array has to be read from that array rather than inferred from the table; the
advertised list is a promise about intent, the tables are what actually runs.

### Class selection

The tables are not scanned as one list. `table_for_class` at `0x10478ebc` is a jump table
over classes `0` to `8`; its byte table sits at `0x10478ec8` and reads
`05 07 09 0b 0e 10 12 06 14`, which resolves to:

| Class | Table | Class | Table |
| --- | --- | --- | --- |
| 0 | T1, loaded directly | 5 | T6, loaded directly |
| 1 | T2, loaded directly | 6 | T7, loaded directly |
| 2 | T3, loaded directly | 7 | no table: returns null |
| 3 | T4, as T3 + `0x70` | 8 | T8, as T7 + `0x54` |
| 4 | T5, loaded directly | 9 and above | no table: returns null |

That is why T4 and T8 carry no 32-bit literal anywhere in the image: they are reached only
as fixed offsets from T3 and T7. The `0x70` offset is exactly four rows of 28 bytes — T3's
three rows plus its terminator — and `0x54` is three rows after T7's base.

The classes come from the predicate chain at `0x10463d70`, which tests the opcode in this
order and takes the first match:

| Order | Predicate | Matches | Class |
| --- | --- | --- | --- |
| 1 | `0x1047895a` | `opcode >> 12 == 1`, i.e. `0x1000`–`0x1FFF` | 0 |
| 2 | `0x10478a08` | `opcode >> 12 == 9` and not matched by any predicate below | 1 |
| 3 | `0x1047897c` | `0x94FD`–`0x94FF` | 2 |
| 4 | `0x104789ea` | `0x90E8`–`0x90FF`, `0x9350`–`0x93FF`, `0x9411` | 3 |
| 5 | `0x104789b6` | `0x9520`–`0x952A`, `0x9530`–`0x953F` | 5 |
| 6 | `0x1047899e` | `0x9010`, `0x952B`–`0x952F` | 6 |
| 7 | `0x1047896a` | `0x92E0`–`0x92FF` | 8 |

Class 1 is a catch-all, so its predicate is a union of negations: `0x10478a08` returns
true only when the high nibble is `9` and none of `0x104789ea`, `0x104789d0`,
`0x104789b6`, `0x1047899e`, `0x1047898e`, `0x1047897c` or `0x1047896a` matched. The
resolved row pointer is cached per slot in RAM at `0xB2A4E21C`, one word per class, and a
cached row is reused while its key still equals the requested code.

Two consequences follow from the chain, and both are visible in the tables:

- **T5 is unreachable.** `table_for_class` has exactly seven call sites, all inside
  `0x10463d70`–`0x10463e66`, and every one of them passes a literal class: `0`, `1`, `2`,
  `3`, `5`, `6` and `8`. Class 4 is never passed, `0x10478ebc` never appears in the image
  as a pointer (so there is no indirect caller), and the only literal reference to T5 is
  the class-4 branch's own pool slot at `0x10478fd4`. Nothing in this build can resolve a
  row from T5: its key ranges `0x9407`–`0x940A` and `0x9509`–`0x950F` are excluded from the
  class-1 catch-all by `0x104789d0` and then have nowhere to go, so they resolve to
  nothing. T5 is dead table data in C 1.70. The prediction on the wire is `0x2005` for
  those nine codes, the same answer `0x10FF` gets for having no row; that has not been
  measured.
- **`0x9100`–`0x91FF` is excluded from class 1 and has no table at all.** `0x1047898e`
  matches that range by high byte, and no table in the image contains a `0x91xx` key, so
  those codes also resolve to nothing.

Class 7 is unreachable twice over: no call site passes it, and the byte table sends it to a
`bx lr` with a zero return. Class 8 is what `0x92E0`–`0x92FF` maps to, which is how the
diagnostic console `0x92F0` reaches T8 — and why the dispatch of that family is worth
reading on [the operations page](operations) rather than here.

## What the tables do not tell you

A row proves that a code is implemented and names the code that runs. It says nothing
about the parameter layout, the reply shape or the side effects, because those live inside
the callbacks. The two families where the callbacks were read in full are the address
access pair, documented in [Services](services), and the service gate itself.

Subcommand rows outside `F001`–`F007`, `F021`–`F033`, `F091`–`F097` and `F165` have not
been driven over the wire. The `A`, `B` and `C` families are entirely unprobed: their rows
prove they exist and hand out their callback addresses, nothing more. The `A` family markup
is also different — its rows carry a receive callback of `0x104910b3` under a zero context,
so those subcommands expect a data phase the same way `F033` does.

## Complete listings

The listings are the image contents. `—` means the field is zero, i.e. the row has no such
callback. A flag of `0` is written out rather than dashed because it is a value, not an
absence.

### Operation table 1 (T1)

`VA 0x119e1480`, 19 rows.

| Key | Flags | Context | Receive | Validate | Execute | Lifecycle |
| --- | --- | --- | --- | --- | --- | --- |
| `1001` | `1` | `00000001` | — | — | `104643cf` | `104643f7` |
| `1002` | `0` | — | — | — | `104797b9` | `10479839` |
| `1003` | `0` | — | — | — | `104797b9` | `10479891` |
| `1004` | `1` | `00000001` | — | — | `10771e03` | `10771e85` |
| `1005` | `1` | `00000001` | — | — | `10771e2b` | `10771f41` |
| `1006` | `1` | `00000001` | `b2a4e0dc` | — | `107710e1` | `107711c3` |
| `1007` | `1` | `00000001` | `b2a4e0f8` | — | `107710e1` | `107711c3` |
| `1008` | `1` | `00000001` | — | — | `10501a37` | `10501abd` |
| `1009` | `1` | `00000001` | `b2a4e118` | — | `104ae8e5` | `104aea9f` |
| `100A` | `1` | `00000001` | `b2a4e158` | — | `104aedb3` | `104aeec1` |
| `100B` | `0` | — | `b2a4e1f8` | — | `104c1bed` | `104c1d05` |
| `100C` | `0` | — | — | `10478fb3` | `10771c3b` | `10771c63` |
| `100D` | `0` | — | — | `10478fb3` | `10771c3b` | `10771c63` |
| `100E` | `0` | — | — | — | `10490695` | `10490a03` |
| `100F` | `0` | — | — | — | `1076f1d9` | `1076f2d1` |
| `1014` | `1` | `00000001` | `b188b7ac` | — | `1050181f` | `1050186f` |
| `1015` | `1` | `00000001` | `b188b7b0` | — | `10501847` | `1050186f` |
| `1016` | `0` | — | — | `10478fb3` | `105018e3` | `10501945` |
| `101B` | `1` | `00000001` | `b2a4e138` | — | `104ae91b` | `104aea9f` |

### Operation table 2 (T2)

`VA 0x119e16b0`, 70 rows.

| Key | Flags | Context | Receive | Validate | Execute | Lifecycle |
| --- | --- | --- | --- | --- | --- | --- |
| `90C0` | `0` | — | — | — | `10490a7b` | `10490ac9` |
| `90C1` | `0` | — | — | — | `104ae3e1` | `104ae445` |
| `90C2` | `0` | — | — | — | `104c1929` | `104c19b1` |
| `90C3` | `0` | — | `b2a4e204` | — | `104c1eb7` | `104c1f23` |
| `90C4` | `1` | `00000001` | `b2a4e178` | — | `104aedb3` | `104aef79` |
| `90C7` | `1` | `00000001` | — | — | `1076f5cf` | `1076f5f7` |
| `90C8` | `0` | — | — | — | `1076f133` | `1076f15b` |
| `90C9` | `0` | — | — | `10771d37` | `10771c6b` | `10771d5d` |
| `90CA` | `1` | `00000001` | — | — | `105019ad` | `105019d5` |
| `90CB` | `0` | — | — | — | `10490b35` | `10490b77` |
| `90CC` | `1` | `00000001` | — | — | `104c2e4d` | `104c3237` |
| `90CD` | `0` | — | — | `10478fb3` | `104c32cb` | `104c3c11` |
| `90CE` | `0` | — | — | — | `104c3da3` | `104c3e75` |
| `90CF` | `1` | `00000001` | — | — | `104c3ec9` | `104c3f19` |
| `9201` | `0` | — | — | — | `104c2085` | `104c2101` |
| `9202` | `0` | — | — | — | `104c21bb` | `104c21f7` |
| `9203` | `1` | `00000001` | — | — | `104c2231` | `104c272d` |
| `9204` | `0` | — | — | — | `104ae4ef` | `104ae55d` |
| `9205` | `0` | — | — | — | `104c2c35` | `104c2c8b` |
| `9206` | `0` | — | — | — | `104ae49b` | `104ae4c1` |
| `9207` | `0` | — | — | — | `10490acf` | `10490b2f` |
| `9209` | `1` | `00000001` | — | — | `10771e03` | `10771e85` |
| `920A` | `0` | — | — | — | `104c2d57` | `104c2dab` |
| `920B` | `0` | — | — | — | `104c2df1` | `104c2e19` |
| `920C` | `0` | — | — | — | `10490b9f` | `10490bdb` |
| `920F` | `1` | `00000001` | `b2a4e1b8` | — | `104af0e9` | `104af16d` |
| `9400` | `1` | `00000001` | `b2a4e198` | — | `104aead3` | `104aeb99` |
| `9402` | `0` | — | — | — | `10502955` | `10502a4d` |
| `9403` | `0` | — | — | — | `10502adb` | `10502b33` |
| `9404` | `0` | — | — | — | `10502b6d` | `10502baf` |
| `9405` | `0` | — | — | — | `10502bd9` | `10502c1b` |
| `9406` | `0` | — | — | — | `10502c41` | `10502c81` |
| `940C` | `0` | — | `b2a4e210` | — | `104c1fb9` | `104c2023` |
| `9414` | `1` | `00000001` | — | — | `10771583` | `107715c3` |
| `9415` | `1` | `00000001` | `b188b7b4` | — | `1077166b` | `107717a7` |
| `9416` | `1` | `00000001` | `b188b7b8` | — | `107717af` | `107717b1` |
| `9417` | `0` | — | — | `10478fb3` | `107717b5` | `10771815` |
| `9418` | `1` | `00000001` | `b188b7bc` | — | `1077192b` | `10771a7b` |
| `9419` | `1` | `00000001` | `b188b7c0` | — | `10771a83` | `10771a85` |
| `941A` | `0` | — | — | `10478fb3` | `10771a89` | `10771adf` |
| `941B` | `0` | — | — | — | `10771ba1` | `10771c0d` |
| `941C` | `1` | `00000001` | — | — | `1076f5cf` | `1076f6d3` |
| `9420` | `0` | — | — | — | `104c1837` | `104c189b` |
| `9421` | `1` | `00000001` | — | — | `104af201` | `104af205` |
| `9422` | `0` | — | — | — | `10490c17` | `10490cd5` |
| `9423` | `1` | `00000001` | — | — | `104c2cbd` | `104c2ce5` |
| `9424` | `0` | — | — | — | `107720a5` | `10772107` |
| `9425` | `0` | — | — | — | `1077217d` | `107721cb` |
| `9426` | `0` | — | — | — | `1049028f` | `1049035f` |
| `9428` | `1` | `00000001` | — | — | `104c27cd` | `104c2bb5` |
| `942B` | `1` | `00000001` | — | — | `107721fb` | `10772225` |
| `9431` | `1` | `00000001` | `b2a4e1d8` | — | `104ae97d` | `104aea9f` |
| `9432` | `1` | `00000001` | — | — | `10770341` | `10770383` |
| `9433` | `0` | — | — | — | `104c444d` | `104c44b5` |
| `9434` | `1` | `00000001` | — | — | `10502089` | `1050234b` |
| `9435` | `0` | — | — | — | `10490503` | `10490553` |
| `9436` | `0` | — | — | — | `1077138f` | `107714f1` |
| `944B` | `0` | — | — | — | `107722a7` | `107722cb` |
| `9464` | `0` | — | — | — | `104ae5ad` | `104ae61f` |
| `9535` | `0` | — | — | — | `1076f0e9` | `1076f111` |
| `9455` | `0` | — | — | `10478fb3` | `104793e3` | `1047940b` |
| `9456` | `0` | — | — | — | `10478fe9` | `1047905d` |
| `9457` | `0` | — | — | `10478fb3` | `10479117` | `10479315` |
| `9458` | `0` | — | — | — | `104794c7` | `1047950d` |
| `9459` | `1` | `00000001` | — | — | `10479563` | `1047958b` |
| `945A` | `0` | — | — | — | `10479619` | `10479657` |
| `9801` | `1` | `00000001` | — | — | `10770419` | `10770457` |
| `9802` | `1` | `00000001` | — | — | `107704b5` | `1077050b` |
| `9803` | `1` | `00000001` | — | — | `10770585` | `107705f5` |
| `9805` | `1` | `00000001` | — | — | `1077065b` | `10770f59` |

### Operation table 3 (T3)

`VA 0x119e1e74`, 3 rows.

| Key | Flags | Context | Receive | Validate | Execute | Lifecycle |
| --- | --- | --- | --- | --- | --- | --- |
| `94FD` | `0` | — | — | — | `107722e5` | `107722eb` |
| `94FE` | `0` | — | — | `107722f1` | `10772301` | `10772307` |
| `94FF` | `1` | `00000001` | — | — | `1077230d` | `10772313` |

### Operation table 4 (T4)

`VA 0x119e1ee4`, 38 rows.

| Key | Flags | Context | Receive | Validate | Execute | Lifecycle |
| --- | --- | --- | --- | --- | --- | --- |
| `90E8` | `0` | — | — | `10478fb3` | `1052da0f` | `1052dacb` |
| `90E9` | `0` | — | — | `10478fb3` | `1052da0f` | `1052dacb` |
| `90EA` | `0` | — | — | — | `1052da0f` | `1052da3d` |
| `90EB` | `1` | `00000001` | — | — | `1052da0f` | `1052db63` |
| `90EC` | `0` | — | — | `10478fb3` | `1052da0f` | `1052dacb` |
| `90ED` | `1` | `00000001` | — | — | `1052da0f` | `1052db63` |
| `90EE` | `0` | — | — | `10478fb3` | `1052da0f` | `1052dacb` |
| `90EF` | `0` | — | — | — | `1052da0f` | `1052da3d` |
| `90F0` | `0` | — | — | `10478fb3` | `1052da0f` | `1052dacb` |
| `90F1` | `0` | — | — | — | `1052da0f` | `1052da3d` |
| `90F3` | `0` | — | — | `10478fb3` | `1052da0f` | `1052dacb` |
| `90F4` | `0` | — | — | `10478fb3` | `1052da0f` | `1052dacb` |
| `90F5` | `1` | `00000001` | — | — | `1052da0f` | `1052db63` |
| `90F6` | `0` | — | — | `10478fb3` | `1052da0f` | `1052dacb` |
| `90F7` | `0` | — | — | — | `1052da0f` | `1052da3d` |
| `90F8` | `0` | — | — | — | `1052da0f` | `1052da3d` |
| `90F9` | `0` | — | — | `10478fb3` | `1052da0f` | `1052dacb` |
| `90FA` | `0` | — | — | — | `1052da0f` | `1052da3d` |
| `90FB` | `0` | — | — | — | `1052da0f` | `1052da3d` |
| `9350` | `1` | `00000001` | — | — | `1052da0f` | `1052db63` |
| `9351` | `1` | `00000001` | — | — | `1052da0f` | `1052db63` |
| `9352` | `0` | — | — | — | `1052da0f` | `1052da3d` |
| `9353` | `1` | `00000001` | — | — | `1052da0f` | `1052db63` |
| `9354` | `0` | — | — | — | `1052da0f` | `1052da3d` |
| `9355` | `0` | — | — | — | `1052da0f` | `1052da3d` |
| `9356` | `0` | — | — | — | `1052da0f` | `1052da3d` |
| `9357` | `0` | — | — | — | `1052da0f` | `1052da3d` |
| `9358` | `0` | — | — | — | `1052da0f` | `1052da3d` |
| `9359` | `0` | — | — | — | `1052da0f` | `1052da3d` |
| `935A` | `0` | — | — | — | `1052da0f` | `1052da3d` |
| `935B` | `0` | — | — | `10478fb3` | `1052da0f` | `1052dacb` |
| `935C` | `0` | — | — | — | `1052da0f` | `1052dc13` |
| `935D` | `0` | — | — | — | `1052da0f` | `1052da3d` |
| `935E` | `0` | — | — | — | `1052da0f` | `1052da3d` |
| `935F` | `0` | — | — | — | `1052da0f` | `1052da3d` |
| `9360` | `0` | — | — | — | `1052da0f` | `1052da3d` |
| `9361` | `1` | `00000001` | — | — | `10501a37` | `10501e55` |
| `9411` | `0` | — | — | — | `1052da0f` | `1052da3d` |

### Operation table 5 (T5)

`VA 0x119e2328`, 11 rows.

| Key | Flags | Context | Receive | Validate | Execute | Lifecycle |
| --- | --- | --- | --- | --- | --- | --- |
| `9509` | `0` | — | — | — | `1052da0f` | `1052da3d` |
| `950A` | `0` | — | — | — | `1052da0f` | `1052da3d` |
| `950B` | `1` | `00000001` | — | — | `1052da0f` | `1052db63` |
| `950C` | `0` | — | — | — | `1052da0f` | `1052da3d` |
| `950D` | `0` | — | — | — | `1052da0f` | `1052da3d` |
| `950E` | `0` | — | — | — | `1052da0f` | `1052da3d` |
| `950F` | `0` | — | — | `10478fb3` | `1052da0f` | `1052dacb` |
| `9407` | `0` | — | — | — | `1052da0f` | `1052da3d` |
| `9408` | `1` | `00000001` | — | — | `1052da0f` | `1052db63` |
| `9409` | `0` | — | — | — | `1052da0f` | `1052da3d` |
| `940A` | `0` | — | — | — | `1052da0f` | `1052da3d` |

### Operation table 6 (T6)

`VA 0x119e2478`, 12 rows.

| Key | Flags | Context | Receive | Validate | Execute | Lifecycle |
| --- | --- | --- | --- | --- | --- | --- |
| `9520` | `0` | — | — | — | `1052da0f` | `1052da3d` |
| `9521` | `1` | `00000001` | — | — | `1052da0f` | `1052db63` |
| `9522` | `1` | `00000001` | — | — | `1076f867` | `1076f98d` |
| `9523` | `0` | — | — | `10478fb3` | `1052da0f` | `1052dacb` |
| `9527` | `0` | — | — | `10478fb3` | `1076f463` | `1076f48b` |
| `9528` | `0` | — | — | `10478fb3` | `1076f53d` | `1076f565` |
| `9529` | `0` | — | — | — | `1076f33b` | `1076f429` |
| `952A` | `0` | — | — | `10478fb3` | `1052da0f` | `1052dacb` |
| `9530` | `1` | `00000001` | — | — | `1076fa1b` | `1076fa4b` |
| `9531` | `0` | — | — | — | `1052da0f` | `1052da3d` |
| `9532` | `1` | `00000001` | — | — | `1052da0f` | `1052db63` |
| `9533` | `0` | — | — | — | `1052da0f` | `1052da3d` |

### Operation table 7 (T7)

`VA 0x119e25e4`, 2 rows.

| Key | Flags | Context | Receive | Validate | Execute | Lifecycle |
| --- | --- | --- | --- | --- | --- | --- |
| `9010` | `1` | `00000001` | — | — | `1052da0f` | `1052db63` |
| `952B` | `1` | `00000001` | — | — | `1052da0f` | `1052db63` |

### Operation table 8 (T8)

`VA 0x119e2638`, 6 rows.

| Key | Flags | Context | Receive | Validate | Execute | Lifecycle |
| --- | --- | --- | --- | --- | --- | --- |
| `92F0` | `0` | — | — | `10478fb3` | `1044415f` | `1044418b` |
| `92F3` | `1` | `00000001` | — | — | `104443bd` | `1044461b` |
| `92F4` | `0` | — | — | — | `104443bd` | `10444681` |
| `92F5` | `1` | `00000001` | — | — | `104443bd` | `104443e1` |
| `92F6` | `1` | `00000001` | — | — | `104443bd` | `1044444f` |
| `92F7` | `0` | — | — | `10478fb3` | `104443bd` | `1044453f` |

### Service subcommand table (S)

`VA 0x119e405c`, 175 rows: 159 in the `F` family, 8 in `A`, 3 in `B` and 5 in `C`.
The `F` numbering has gaps because the keys are not contiguous; the rows are.

| Key | Flags | Context | Receive | Validate | Execute | Lifecycle |
| --- | --- | --- | --- | --- | --- | --- |
| `F001` | `0` | — | — | — | — | `1047994d` |
| `F002` | `0` | — | — | — | — | `10479a67` |
| `F003` | `1` | `00000001` | — | — | `10479ab1` | `10479ae7` |
| `F004` | `0` | — | — | `104c6a9b` | `10479ce5` | `10479d25` |
| `F005` | `0` | — | — | `104c6a9b` | `10479e2d` | `10479e31` |
| `F006` | `1` | `00000001` | — | — | `10479e81` | `10479e85` |
| `F007` | `0` | — | — | — | `10479edb` | `10479f19` |
| `F021` | `1` | `00000001` | — | — | `104af3c3` | `104af417` |
| `F022` | `0` | — | — | `104c6a9b` | `104af485` | `104af579` |
| `F024` | `0` | — | — | — | `104af601` | `104af69d` |
| `F025` | `1` | `00000001` | — | — | `104af6f7` | `104af749` |
| `F026` | `0` | — | — | `104c6a9b` | `104af7b3` | `104af805` |
| `F028` | `0` | — | — | — | `104af885` | `104af889` |
| `F029` | `1` | `00000001` | — | — | `104af8f3` | `104af989` |
| `F02A` | `0` | — | — | `104c6a9b` | `104afa31` | `104afac7` |
| `F02C` | `0` | — | — | — | `104afb6f` | `104afba7` |
| `F02D` | `1` | `00000001` | — | — | `104afc4d` | `104afc9f` |
| `F02E` | `0` | — | — | `104c6a9b` | `104afd4b` | `104afd9d` |
| `F02F` | `0` | — | — | — | `104afe47` | `104afe7d` |
| `F030` | `0` | — | — | — | `104afefb` | `104afeff` |
| `F031` | `0` | — | — | — | `104aff3b` | `104aff95` |
| `F032` | `1` | `00000001` | — | — | `104b010b` | `104b020d` |
| `F033` | `0` | — | — | `104b026b` | `104b0281` | `104b02c9` |
| `F036` | `1` | `00000001` | — | — | `104b0525` | `104b065d` |
| `F037` | `0` | — | — | `104c6a9b` | `104b06bb` | `104b06fb` |
| `F038` | `0` | — | — | — | `104b0763` | `104b0799` |
| `F039` | `0` | — | — | — | `104b07dd` | `104b0813` |
| `F03A` | `1` | `00000001` | — | — | `104b0861` | `104b08b5` |
| `F03B` | `0` | — | — | `104c6a9b` | `104b0923` | `104b0975` |
| `F03C` | `0` | — | — | — | `104b09e1` | `104b09e5` |
| `F03D` | `1` | `00000001` | — | — | `104b0a2f` | `104b0a81` |
| `F03E` | `0` | — | — | `104c6a9b` | `104b0aeb` | `104b0b3d` |
| `F03F` | `0` | — | — | — | `104b0ba7` | `104b0bab` |
| `F040` | `1` | `00000001` | — | — | `104b0bf5` | `104b0c27` |
| `F041` | `0` | — | — | `104c6a9b` | `104b0cbf` | `104b0cf1` |
| `F044` | `1` | `00000001` | — | — | `104b0d4d` | `104b0d7f` |
| `F045` | `0` | — | — | `104c6a9b` | `104b0e3f` | `104b0e71` |
| `F046` | `1` | `00000001` | — | — | `104b0f27` | `104b0f59` |
| `F047` | `0` | — | — | `104c6a9b` | `104b101d` | `104b104f` |
| `F048` | `0` | — | — | `104c6a9b` | `104b110b` | `104b113d` |
| `F049` | `0` | — | — | `104c6a9b` | `104b11eb` | `104b1235` |
| `F04A` | `1` | `00000001` | — | — | `104b12c3` | `104b12c7` |
| `F04B` | `0` | — | — | — | `104b1355` | `104b1359` |
| `F04C` | `1` | `00000001` | — | — | `104b13ff` | `104b1403` |
| `F04D` | `0` | — | — | — | `104b1483` | `104b1487` |
| `F04E` | `1` | `00000001` | — | — | `104b0303` | `104b0349` |
| `F04F` | `0` | — | — | `104c6a9b` | `104b0419` | `104b045b` |
| `F050` | `1` | `00000001` | — | — | `107743cf` | `10774413` |
| `F051` | `1` | `00000001` | — | — | `107744d5` | `107744d9` |
| `F052` | `1` | `00000001` | — | — | `1077459b` | `1077459f` |
| `F059` | `0` | — | — | `104c6a9b` | `104b1909` | `104b193b` |
| `F061` | `0` | — | — | — | `104b23e3` | `104b2419` |
| `F063` | `0` | — | — | — | `104b2461` | `104b2497` |
| `F064` | `0` | — | — | — | `104b24c9` | `104b250f` |
| `F065` | `0` | — | — | — | `104b2553` | `104b2587` |
| `F066` | `0` | — | — | — | `104b25b9` | `104b25ed` |
| `F067` | `0` | — | — | — | `104b261f` | `104b2653` |
| `F068` | `1` | `00000001` | — | — | `104b2705` | `104b2759` |
| `F069` | `1` | `00000001` | — | — | `104b2821` | `104b287d` |
| `F06B` | `1` | `00000001` | — | — | `104b28f7` | `104b28fb` |
| `F06C` | `0` | — | — | — | `104b29ff` | `104b2cbd` |
| `F06D` | `0` | — | — | `104c6a9b` | `104915bb` | `10491be9` |
| `F06E` | `1` | `00000001` | — | — | `10491cb9` | `10492393` |
| `F070` | `1` | `00000001` | — | — | `104924e5` | `10492523` |
| `F071` | `0` | — | — | — | `104925c5` | `104925fb` |
| `F072` | `0` | — | — | — | `104b2681` | `104b26b7` |
| `F073` | `1` | `00000001` | — | — | `104b2d13` | `104b2d73` |
| `F074` | `0` | — | — | `104b2e09` | `104b2e41` | `104b2e73` |
| `F075` | `0` | — | — | — | `104b2edf` | `104b2f17` |
| `F076` | `0` | — | — | — | `104b3055` | `104b3089` |
| `F077` | `1` | `00000001` | — | — | `104b30bb` | `104b30fb` |
| `F079` | `0` | — | — | — | `104b2f73` | `104b2f85` |
| `F091` | `0` | — | — | — | `10502cab` | `10502d0b` |
| `F092` | `0` | — | — | — | `10502d83` | `10502db9` |
| `F093` | `0` | — | — | — | `10502e13` | `10502f11` |
| `F094` | `0` | — | — | — | `10502f6f` | `10503023` |
| `F095` | `0` | — | — | — | `105030af` | `105030ef` |
| `F096` | `0` | — | — | — | `104c6cfb` | `104c6d67` |
| `F097` | `0` | — | — | — | `104c6ddb` | `104c6e2d` |
| `F111` | `0` | — | — | — | `10773aaf` | `10773ae5` |
| `F112` | `0` | — | — | `104c6a9b` | `10773b49` | `10773b4d` |
| `F113` | `1` | `00000001` | — | — | `10773bd7` | `10773bdb` |
| `F114` | `1` | `00000001` | — | — | `10773c67` | `10773c6b` |
| `F131` | `0` | — | — | — | `10773657` | `1077365b` |
| `F132` | `0` | — | — | — | `1077369d` | `107736a1` |
| `F133` | `0` | — | — | — | `107736df` | `107736e3` |
| `F134` | `0` | — | — | — | `10773721` | `10773725` |
| `F135` | `0` | — | — | — | `10773763` | `10773767` |
| `F136` | `0` | — | — | — | `107737a3` | `107737a7` |
| `F137` | `0` | — | — | — | `107737e9` | `107737ed` |
| `F138` | `0` | — | — | — | `1077382b` | `1077382f` |
| `F139` | `0` | — | — | — | `1077386d` | `10773871` |
| `F13A` | `0` | — | — | — | `107738b1` | `107738b5` |
| `F13B` | `0` | — | — | — | `107738f7` | `107738fb` |
| `F13C` | `0` | — | — | — | `1077393b` | `1077393f` |
| `F13D` | `0` | — | — | — | `107739d9` | `107739dd` |
| `F13E` | `0` | — | — | — | `10773a1d` | `10773a21` |
| `F13F` | `0` | — | — | — | `10773a67` | `10773a6b` |
| `F162` | `1` | `00000001` | — | — | `10773cf9` | `10773cfd` |
| `F163` | `1` | `00000001` | — | — | `10773db1` | `10773db5` |
| `F164` | `0` | — | — | — | `10773e51` | `10773e55` |
| `F165` | `0` | — | — | — | `10773e9f` | `10773ed5` |
| `F166` | `0` | — | — | `104c6a9b` | `10773f23` | `10773f59` |
| `F181` | `0` | — | — | — | `10774037` | `1077406f` |
| `F182` | `0` | — | — | — | `107740b5` | `107740b9` |
| `F183` | `0` | — | — | — | `107740fd` | `10774151` |
| `F184` | `0` | — | — | — | `107741eb` | `107741ef` |
| `F1A1` | `0` | — | — | — | `10772ce7` | `10772ceb` |
| `F1A2` | `1` | `00000001` | — | — | `10772d2f` | `10772d79` |
| `F1A3` | `0` | — | — | — | `10772df7` | `10772dfb` |
| `F1A4` | `1` | `00000001` | — | — | `10772e3d` | `10772e87` |
| `F1A5` | `0` | — | — | — | `10772f19` | `10772f1d` |
| `F1A6` | `1` | `00000001` | — | — | `10772f5f` | `10772fa9` |
| `F1A7` | `0` | — | — | — | `10773037` | `1077306f` |
| `F1A8` | `0` | — | — | — | `107730ab` | `107730df` |
| `F1A9` | `0` | — | — | — | `10773129` | `1077315d` |
| `F1AA` | `0` | — | — | — | `107731a7` | `107731ab` |
| `F1AB` | `0` | — | — | — | `107731fb` | `107731ff` |
| `F1AC` | `0` | — | — | — | `1077324d` | `10773251` |
| `F1AD` | `1` | `00000001` | — | — | `1077329f` | `107732e9` |
| `F1AE` | `0` | — | — | — | `107733cd` | `107733d1` |
| `F1AF` | `1` | `00000001` | — | — | `10773413` | `1077345d` |
| `F1B0` | `0` | — | — | — | `107734d9` | `107734dd` |
| `F1B1` | `1` | `00000001` | — | — | `1077351f` | `10773569` |
| `F1B2` | `0` | — | — | — | `10773377` | `1077337b` |
| `F1B3` | `0` | — | — | — | `107735e5` | `1077361b` |
| `F1F1` | `0` | — | — | `104c6a9b` | `104c6f15` | `104c7055` |
| `F1F2` | `1` | `00000001` | — | — | `104c70c3` | `104c70c7` |
| `F1F3` | `1` | `00000001` | — | — | `104c7161` | `104c7165` |
| `F1F4` | `1` | `00000001` | — | — | `104c71fd` | `104c7233` |
| `F1F5` | `0` | — | — | `104c6a9b` | `104c72cb` | `104c72fd` |
| `F1F8` | `0` | — | — | `104c6a9b` | `104c7373` | `104c73a9` |
| `F1F9` | `1` | `00000001` | — | — | `104c7433` | `104c7467` |
| `F221` | `0` | — | — | — | `104b2363` | `104b23a7` |
| `F251` | `1` | `00000001` | — | — | `10490db3` | `10490db7` |
| `F253` | `0` | — | — | `10490e37` | `10490e53` | `10490e7b` |
| `F254` | `1` | `00000001` | — | — | `10490f3b` | `10490f51` |
| `F255` | `0` | — | — | — | `10490fc3` | `1049100b` |
| `F271` | `1` | `00000001` | — | — | `1077231b` | `10772351` |
| `F272` | `0` | — | — | — | `10772427` | `1077242b` |
| `F273` | `0` | — | — | — | `10772459` | `1077245d` |
| `F274` | `0` | — | — | — | `1077248b` | `107724bf` |
| `F275` | `0` | — | — | — | `107724ed` | `10772523` |
| `F276` | `0` | — | — | — | `10772561` | `107725b7` |
| `F278` | `0` | — | — | — | `10772671` | `107726b9` |
| `F279` | `1` | `00000001` | — | — | `10772773` | `10772a07` |
| `F27A` | `0` | — | — | — | `10772add` | `10772ae1` |
| `F27B` | `0` | — | — | — | `10772b41` | `10772b45` |
| `F27C` | `0` | — | — | — | `10772ba5` | `10772ba9` |
| `F27D` | `0` | — | — | — | `10772c29` | `10772c61` |
| `F2B1` | `0` | — | — | `104b1a63` | `104b1a87` | `104b1b17` |
| `F2B3` | `0` | — | — | — | `104b1c1d` | `104b1c55` |
| `F2B4` | `0` | — | — | `104b1d4f` | `104b1d73` | `104b1dd3` |
| `F2B5` | `0` | — | — | — | `104b1ee1` | `104b1ee5` |
| `F2B6` | `0` | — | — | — | `104b1f63` | `104b1f67` |
| `F2B8` | `0` | — | — | — | `104b1fe5` | `104b1fed` |
| `F2B9` | `1` | `00000001` | — | — | `104b2027` | `104b202b` |
| `F2BA` | `0` | — | — | — | `104b20d7` | `104b2119` |
| `F2BB` | `0` | — | — | `104c6a9b` | `104b21fd` | `104b2201` |
| `A001` | `1` | `00000001` | — | `104910b3` | `104910af` | `1049114b` |
| `A002` | `0` | — | — | `104910b3` | `104910af` | `10491229` |
| `A003` | `0` | — | — | `104910b3` | `104910af` | `1049126d` |
| `A004` | `0` | — | — | `104910b3` | `104910af` | `104912ab` |
| `A005` | `0` | — | — | `104910b3` | `104910af` | `1049133f` |
| `A006` | `0` | — | — | `104910b3` | `104910af` | `10491409` |
| `A101` | `0` | — | — | `104910b3` | `104910af` | `10491509` |
| `A102` | `1` | `00000001` | — | `104910b3` | `104910af` | `1049154b` |
| `B002` | `0` | — | — | — | `10774229` | `1077423b` |
| `B003` | `0` | — | — | — | `107742b1` | `107742b5` |
| `B004` | `0` | — | — | — | `107742ef` | `10774329` |
| `C001` | `0` | — | — | — | `10774679` | `1077467d` |
| `C051` | `0` | — | — | — | `107746bf` | `1077477f` |
| `C052` | `0` | — | — | — | `107747b1` | `10774871` |
| `C101` | `0` | — | — | — | `107748bb` | `1077497b` |
| `C102` | `0` | — | — | — | `107749ad` | `10774a71` |

