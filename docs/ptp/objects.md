# Objects

Registry objects are what the service family streams. Each object has a descriptor record
in RAM holding a base address and a length, and the object services copy
`base + offset` for the requested length. Because those services never consult the address
guard, a descriptor is a redirectable window into memory.

## Record layout

The object records found on this body are 24 bytes:

| Offset | Field |
| --- | --- |
| `+0` | object id |
| `+4` | zero |
| `+8` | base address |
| `+12` | length |
| `+16` | zero |
| `+20` | region index |

Two records were located by scanning the region/object area and were then used:

| Object | Record | Observed fields |
| --- | --- | --- |
| 8 | `VA 0xB185E7DC` | id 8, base `0xB20CFD14`, length `0x1FFFC`, region 8 |
| 17 | `VA 0xB185E8D8` | id 17, base `0xB7F4A000`, length `0x1FFFC`, region 17 |

Object 8 is special twice over: it is the record the address guard reads for its runtime
interval, and it is the object `F02C`–`F02E` and the `0x90C0` capture use.

A separate, 28-byte registry is consulted by the lookup at `VA 0x102486f6` (stride `0x1c`,
base from a literal). The two structures are not interchangeable; the 24-byte records were
found by scanning for records whose id byte matched the service being tested, and each was
confirmed by patching and observing the service change.

## Services by object

| Service | Object | Behaviour |
| --- | --- | --- |
| `F021` / `F022` | 17 | stream `base + offset`, `length` bytes |
| `F024`–`F026` | 53 | same shape |
| `F029` / `F02A` | 71 | resource selectors; same shape |
| `F02C`–`F02E` | 8 | block selector `1..0x3F` |
| `F02F`/`F030`/`F031` | 1 | boot material; commit and verify semantics |
| `F037`–`F03B` | 10 | same shape as object 17 |
| `F03C`–`F03E` | 9 | same shape |
| `0x90C0` | 9 | capture through the file service |

## Redirecting a descriptor

The read pattern, using object 17:

1. read the record at `VA 0xB185E8D8` and save the base field at `+8`;
2. write `0x10000000` into that field with `F033`;
3. read the record back to confirm;
4. call `0x94FD [0xF021, offset, length, 0, 0]` — the service now streams from the
   boot region;
5. write the saved base back and read back to confirm.

This works because the service computes `descriptor_base + offset` and copies, with no
guard in the path. The same trick can redirect any object service to any address,
including addresses the address guard would refuse.

## The configuration table

Diagnostic configuration is a byte array addressed by index. Index `0x4D6` lives at
`VA 0xB20D2D6A`, inside object 8's runtime interval, so direct address access to it is
refused while the interval is stock.

To reach it:

1. read object 8's length field at `VA 0xB185E7E8` and save it;
2. write a small length (e.g. `4`) so the interval no longer contains the byte;
3. read and, if needed, write `VA 0xB20D2D6A` with the address primitives;
4. restore the length and verify.

This was exercised: the byte was written to `1` and the length restored. Note that the
interval also gates `F032`/`F033` for that whole range, so the window in which the
configuration is reachable is exactly the window in which object 8's protection is
disabled. Keep it short, and restore before anything else.

## Cautions

- Rewriting a descriptor while a service is copying from it produces reads from the new
  address with the old length, i.e. arbitrary data. Restore immediately.
- Object 8's record has a second consumer (the guard), so patching it has a wider effect
  than patching object 17's.
- Object 1 holds the boot material. The services on it are commit and verify paths; they
  were deliberately not driven with invented values.
- None of this survives a reboot. If a restore fails, power-cycle and re-read the records
  before concluding anything.
