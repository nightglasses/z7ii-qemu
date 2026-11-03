# Recipes

Ready-to-run sequences for the tasks this surface supports. Parameter lists are the
`u32` argument array of the command container; the opcode is shown separately. Every
recipe assumes an open session and unlocked service mode.

```text
open session       0x1002  [1]
unlock service     0x94fd  [0xf001,0,0,0,0]
lock service       0x94fd  [0xf002,0,0,0,0]
close session      0x1003  []
```

## Identify the body

```text
0x1001  []                      -> device info; expect model "Z 7_2"
0x1004  []                      -> storage ids
0x94fd  [0xf003,4,0,0,0]        -> 128-byte identity record, firmware string
```

The firmware string is the value the rest of this section was measured against. A
different string means the addresses and constants must be re-derived.

## Read RAM

```text
0x94fd  [0xf032, address, length, 0, 0]
```

Returns a data container on success. An empty data container with parameter `0x201d`
means the address is inside the guard's deny list — it is not an error and not a short
read.

Example: `0x94fd [0xf032,0xb1800474,1,0,0]` returned `00` on an idle body.

## Write RAM

```text
0x94fe  [0xf033, address, length, 0, 0]   + data container with the bytes
```

`length` is at most `0x400`. Read the bytes back to verify; then restore the saved value.

Example: writing `01` to `0xb1800474` answered `0x2001` and a read-back returned `01`.

## Read the boot region

The address guard blocks the boot window, so read it through an object service whose base
you redirect.

1. `0x94fd [0xf032,0xb185e8d8,0x18,0,0]` — read object 17's record; keep bytes `[8,12)`
   (the base field).
2. `0x94fe [0xf033,0xb185e8e0,4,0,0]` + `00 00 00 10` — set the base to `0x10000000`.
3. `0x94fd [0xf032,0xb185e8d8,0x18,0,0]` — confirm the base changed.
4. `0x94fd [0xf021, offset, length, 0, 0]` — read `0x10000000 + offset`.
5. `0x94fe [0xf033,0xb185e8e0,4,0,0]` + the saved base — restore.
6. `0x94fd [0xf032,0xb185e8d8,0x18,0,0]` — confirm the restore.

With `offset = 0x80`, `length = 0x10` the service returned
`302e303300000000` followed by eight bytes; the first eight are the bootloader version
string `0.03` and the last eight are the update material. See [Key recovery](recovery).

## Read the diagnostic configuration byte

The byte at `0xb20d2d6a` (configuration index `0x4d6`) sits inside object 8's runtime
deny interval, so it is unreachable while that interval is stock.

1. `0x94fd [0xf032,0xb185e7e8,4,0,0]` — read object 8's length field; keep it.
2. `0x94fe [0xf033,0xb185e7e8,4,0,0]` + `04 00 00 00` — shrink the interval.
3. `0x94fd [0xf032,0xb20d2d6a,1,0,0]` — read the byte (write it with `0xf033` if needed).
4. `0x94fe [0xf033,0xb185e7e8,4,0,0]` + the saved length — restore.
5. `0x94fd [0xf032,0xb185e7e8,4,0,0]` — confirm.

The interval is the only thing protecting that byte, so keep the window as short as
possible and restore before any other request.

## Read the diagnostic log

The ring is ordinary memory:

```text
0x94fd  [0xf032,0xb1800490,4,0,0]    -> write sequence
0x94fd  [0xf032,0xb18b9da8,128,0,0]  -> slot (sequence mod 4096)
```

Slots are 128 bytes; the sequence is a free-running `u32`. A record is only valid once
the sequence has advanced past its slot. The producer that fills the ring was not
established — see [Services](services) for the diagnostic gates that were found.

## Capture an object

```text
0x1007  [storage, 0, parent]     -> object handles
0x1008  [handle]                 -> object info (no ObjectHandle prefix)
0x1009  [handle]                 -> object bytes
```

Object upload is not available: `0x100c` and `0x100d` refuse every storage, parent and
advertised format with `0x2020`.

## Read a resource

```text
0x94fd  [0xf029, 0, 0x1000, selector, 0]
```

Selectors `0`–`6` return data; everything beyond `6` returns nothing. Payloads are
opaque; selector 6 begins with the ASCII `AFadjust` signature.

## Recover the update material

The full sequence, including verification against the stock header, is in
[Key recovery](recovery). It is the boot-region read above followed by
`K8 = seed XOR mask` and a digest check.

## Cleanup checklist

1. Every descriptor byte written back, read-verified.
2. Configuration byte restored if it was changed.
3. `0x94fd [0xf002,0,0,0,0]`.
4. `0x1003` to close, then release the interface.
5. If any restore failed, power-cycle and re-read the records before trusting them.
