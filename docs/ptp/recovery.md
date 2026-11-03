# Recovering the update authentication key over the physical PTP session

Scope: a physical Nikon Z 7II reporting firmware `V1.60` (`lsusb` `04b0:044b`, model
`NIKON DSC Z 7_2`), against the registered `Z7_2_0170.bin` (C 1.70) sample. The key
bytes are deliberately not printed here; they are kept in an ignored local file. No
storage card, disassembly or external material was used.

Result: the eight bytes at `VA[0x10000088,0x10000090)` were read from the running
camera, and the stock C 1.70 header digest was reproduced from them.

The general access surface behind this procedure — the service family, the memory
primitives, the guard and the object model — is documented in
[the PTP section index](index). This note only covers the recovery itself.

## Result

| Item | Value |
| --- | --- |
| seed at `VA[0x10000088,0x10000090)` | local key file (8 B) |
| mask | `ce13bdcc401957fa` (keystream A=101, B=238, M=0x260) |
| K8 | seed XOR mask |
| verification | `SHA1(decoded_body ‖ K8)` equals the stock header `b3db09b2575be899f6f6a06055b7f036827fbac7` byte for byte |

The same read returned `VA[0x10000080,0x10000088)` = `"0.03\0\0\0\0"`, a bootloader
version string; the seed starts immediately after it, so the window is a coherent
boot-region field.

## Why address reads cannot reach NOR

`F032` (address read) and `F033` (address write) share the guard at `VA 0x104b0098`.
Measured and statically re-checked behaviour:

- The guard is a **deny list**. A request whose range matches a listed interval is
  rejected: the outer response is `0x2001` with an empty data container and the inner
  parameter `0x201d` (observed as `[0x201d, 0, 0, 0, 0]`). Only non-matching addresses
  reach the executor.
- The static deny interval `[0x10000000,0x14000000)` covers the NOR/ROM projection, so
  an address read of the boot region can never succeed.
- The runtime deny interval is object 8's cache: its record is at `VA 0xB185E7DC`, base
  at `+8` = `0xB20CFD14`, length at `+12` = `0x1FFFC`, i.e. `[0xB20CFD14,0xB20EFD10)`.
- The read executor itself is a **CPU copy** (`O0x4b0196 blx 0x10360fac`), not a DMA
  transfer. Any read path that does not consult this guard can therefore reach NOR.

## Object services bypass the guard

The `0x94FD` service family contains handlers that operate on registry objects, each
with a hard-coded object number:

| Service | Object |
| --- | --- |
| `F021` / `F022` | 17 |
| `F024`–`F026` | 53 |
| `F029` / `F02A` | 71 |
| `F02C`–`F02E` | 8 |
| `F02F` / `F030` / `F031` | 1 (boot material; write/verify only) |
| `F037`–`F03B` | 10 |
| `F03C`–`F03E` | 9 |

The `F021` executor (`VA 0x104af416`) resolves object 17 through `VA 0x103a485c`, then
sends `descriptor_base + params[1]` for `params[2]` bytes through `VA 0x1063c46e`. This
path never touches the `0x104b0098` guard, so pointing the descriptor base into NOR
turns `F021` into a boot-region read.

Object 17's record is at `VA 0xB185E8D8` (24-byte record; base at `+8`, length at `+12`).
The observed values were base `0xB7F4A000` and length `0x1FFFC`.

## Write primitive

The `F033` executor (`VA 0x104b02c8`) is `memcpy(params[1], data_phase, params[2])` with
a length limit of `0x400`; its validator requires `0 < length <= 0x400` and the same
address guard.

The data phase must be sent with op **`0x94FE`**: only that opcode registers the receive
callback (`0x107722f0 → 0x104c6aec`). Sending a data phase to `0x94FD` leaves the camera
waiting for data and hangs the session; recovery required a power cycle. The primitive
was verified by writing `0xB1800474` and `0xB180048C` and reading `01` back.

## Procedure

1. Stop `ptpcamerad` (it holds the interface; `libusb_claim_interface` otherwise returns
   `-3`), open `04b0:044b` with libusb and claim interface 0.
2. `OpenSession(1)`; `0x94FD [0xF001,0,0,0,0]` unlocks service mode (`0x2001`).
3. `0x94FE [0xF033, 0xB185E8E0, 4, 0, 0]` with data `00 00 00 10` rewrites object 17's
   base field to `0x10000000`; read the record back to confirm.
4. `0x94FD [0xF021, 0x80, 0x10, 0, 0]` returns `VA[0x10000080,0x10000090)`.
5. The seed is bytes `[8,16)` of that payload; `K8 = seed XOR ce13bdcc401957fa`.
6. Verify `SHA1(decoded_body ‖ K8)` against the stock header before using the key.
7. Restore the descriptor base with the saved value, then `0x94FD [0xF002,0,0,0,0]`.

## Observed values

| Observation | Value |
| --- | --- |
| `F032 0x10000000`, `0x10000080`, `0x10000088` | outer `0x2001`, empty data container, parameter `0x201d` |
| `F032 0x04000000` | 8 bytes `b4f09fe5feffffea` (reset vector, normal) |
| `F032 0xB1800474`, `0xB180048C` | readable; both `00` before the run |
| `F021(0x80,0x10)` after the base patch | `302e303300000000` then the 8 seed bytes |
| descriptor after restore | base back to `0xB7F4A000`; camera state unchanged |

## Related findings

- Object 8's descriptor is also the source of the runtime deny interval. Temporarily
  shrinking its length field (`VA 0xB185E7E8`, e.g. to `4`) makes the protected
  configuration byte `VA 0xB20D2D6A` (diagnostic config `0x4D6`) readable and writable;
  this was exercised (value written as `1`, length restored) before the read path above
  was found.
- The diagnostic console `0x92F0` (`debug memdump <addr> <len>`, zero parameters plus a
  host data phase) does not answer on this body, and `0x92F3` stalls. That path is not
  established.
- The diagnostic gates `0xB1800474` (debug admission) and `0xB180048C` (log retention)
  are writable and read back consistently.

## Boundaries

- Service mode and RAM writes change runtime state. Every run must restore the
  descriptor bytes it changed and exit service mode, or power-cycle the camera; RAM
  changes do not persist across a reboot.
- This procedure only reads the boot region. It does not write NOR, does not start an
  update and does not touch a card. The correctness of the recovered bytes is proven by
  the stock header digest match, not by the read alone.
- Addresses and offsets were measured on a `V1.60` body against the `C1.70` sample. The
  C1.70 guard, service table and object record constants come from static analysis and
  must be re-checked for other versions.
- Key bytes stay in the ignored local working tree and are not part of this document or
  any commit.

## Next

Re-sign a modified package with the recovered K8: recompute the whole-package SHA-1,
the component CRCs, the package CRC and the XOR encoding, verify in QEMU first, and only
then consider a physical installation. The signing model is already closed against the
stock package; what remains is feeding the modified content through the existing
producer.
