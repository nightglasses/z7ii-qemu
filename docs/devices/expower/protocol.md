# EX monitor and status endpoints

The EX power lane reaches two I2C endpoints: the `0x6b` monitor register file
(`0xd6` on the wire) and the `0x0d` status endpoint. Both are one-byte register
transactions issued by the original firmware through the EX I2C controller, so
this page records the command space the image actually uses, the read-back
rules it enforces, and what the emulator model does and does not claim.

Evidence tags follow the repository convention: observed in the original image,
static from the EX database (module `0_ex1840_013000.bi.bin`, imagebase 0), or
still unqualified.

## Wire framing

The descriptor at `0x2001fdcc` names wire address `0x44`, the register `0x48`,
and a length of one byte. The controller emits:

- write phase: `S 0xd6(W) cmd data P`
- read phase: `S 0xd6(W) cmd Sr 0xd6|1(R) byte P`

The address byte itself comes from the interrupt handler (`sub_8046fbc`), which
masks bit0 for the write phase and sets it for the receive phase; the submit
routine stores the unshifted `0xd6`. Both phases pass a length of one and a
timeout of one tick, so no multi-byte transfer exists on this endpoint.

## Command space

`sub_802b014` writes one data byte under a command byte and reads the same
command back. `sub_802b234` and `sub_802b40c` marshal a 12-byte request from
`0x2001fb90` into the payload array `0x2001fda6+cmd` and then commit registers
`0`, `1`, `2`, `3`, `4`, `5`, `7` (and `6` first in one, last in the other).
`sub_802b170` seeds the payload array with `00 80 a0 00 00 8d 33 00 cd 06`; the
last two bytes are never transmitted.

| Command | Direction | Payload source | Read-back requirement | Firmware reaction |
|---|---|---|---|---|
| `0x00` | write + read | `0x2001fda6` (`0xa0` or `0x00`) | equality, and the value must be `<= 0xa0` | three attempts, then the `0x01 = 0xc0` write |
| `0x01` | write + read | `0x2001fda7`: `(d[1] & 0x3f)`, bit6 from `d[2]`, bit7 from `d[3]` | `(readback & 0x3f) == (written & 0x3f)` and `<= 0x18` | same |
| `0x02` | write only | `0x2001fda8`, constant `0xa0` | none | a transfer error aborts the commit |
| `0x03` | write + read | `0x2001fda9`: `d[5] & 0x1f` | `(readback & 0x1f) == (written & 0x1f)` and `<= 0x19` | same |
| `0x04` | write only | `0x2001fdaa`: `(d[6] & 0xf) \| (d[7] << 4)` | none | as `0x02` |
| `0x05` | write only | `0x2001fdab`, constant `0x8d` | none | as `0x02` |
| `0x06` | write only | `0x2001fdac`, bit3 from `d[9]` and bit2 from `d[8]` | none | as `0x02`; written first by one committer, last by the other |
| `0x07` | write only | `0x2001fdad`: bit7 from `d[10]`, low nibble from `d[11]` | none | as `0x02` |
| `0x0b` | read | reply into `0x2001fdb0` | low three bits are the EX state; only `1` or `2` continues | any other value falls back to the state machine at `sub_8023524` |
| `0x10` | read | reply into `0x2001fdb1` | bit7 must be clear | set bit7 for three consecutive polls raises fault 4 and log 19007 |
| `0x11` | read | reply on the stack | bit3 requests a reprogram together with `0x01` bit7 | with both bits set the lane reprograms itself |

Commands `0x02`, `0x04`, `0x05`, `0x06` and `0x07` are never read back, and the
highest command the image uses is `0x11`.

## Read-back law

Three properties of `sub_802b014` decide whether a monitor commit succeeds:

1. The comparison is masked per command: full byte for `0x00`, `0x3f` for
   `0x01`, `0x1f` for `0x03`.
2. Each comparison carries a range gate (`<= 0xa0`, `<= 0x18`, `<= 0x19`). The
   default `0x00` payload is `0xa0`, exactly on the first gate.
3. A persistent mismatch is not an abort. After three failed attempts the
   firmware writes `0x01 = 0xc0`, clearing the low six bits and forcing the two
   top bits, then returns success. Only a transfer-level failure (`1` engine
   spin timeout, `2` channel busy, `3` event wait timeout) aborts the commit,
   and that path re-initialises the whole lane through `sub_802b170`.

A model that echoes the written byte therefore satisfies the original image as
long as it latches the data byte per command and accepts the `0xc0` write.

## Status endpoint

`sub_802b6f0`, running in the `BatCmAl1` task from software timers in
`0x08051444`, drives wire address `0x0d` (address byte `0x1a`, receive address
`0x1b`):

1. two-byte write `08 01`
2. four-byte receive into a stack buffer
3. two-byte write `08 00`
4. decode the fourth byte: `raw & 3` is the raw state, `((raw >> 2) & 7) == 1`
   is a flag, and the class table accepts `0xa9`/`0xaa` as class 1, `0xc9`/`0xca`
   as class 2, `0xe9`/`0xea` as class 3 and anything else as class 0
5. store class in `0x2001f64e`, raw state in `0x2001f64c`, and set the
   transaction-valid dword `0x2001f650 = 1`

The four-byte receive takes the multi-byte path: the interrupt handler reads
`DR` on BTF for lengths 4 and 3, clears the buffer interrupt enable, clears ACK
before the third byte, and reads the last two bytes back to back after STOP.
Because the emulator now times the receive shift byte by its SCL period, BTF
appears only when the guest still acknowledges, which is the sequence this
endpoint uses.

Consumers are narrow and one-directional:

- eight handlers copy the class into the snapshot byte `0x2001f5dc`;
- `sub_8029a84` compares the class with that snapshot and, on a change, builds a
  request and reprograms the `0x6b` registers (`sub_802b40c`), then optionally
  posts event `0x0202004d`;
- no per-class branch, GPIO write, rail enable or ADC reconfiguration exists.

The dependency therefore runs `0x0d` read -> class compare -> `0x6b` program,
never the reverse. The `0x6b` commit itself is gated by the transaction-valid
dword `0x2001f650`, not by the class.

## Probes

The following hardware breakpoints distinguish the paths on an original bundle
run; the addresses and meanings come from the database, and a run reaching them
proves the path rather than a model value:

| Breakpoint | Meaning |
|---|---|
| `0x0802b044`, `0x0802b070` | `0x6b` write and read-back of one register |
| `0x0802b0e4`, `0x0802b0da` | verify equality and verify mismatch edges |
| `0x0802b112` | the `0x01 = 0xc0` sanitiser write after three mismatches |
| `0x0802b2f2`..`0x0802b3de` | the five write-only registers of one committer |
| `0x0802b6f0`, `0x0802b7e4` | status service entry and raw fourth-byte store |
| `0x0802b862`..`0x0802b870` | class 0..3 stores at `0x2001f64e` |
| `0x0802b864` | transaction-valid write `0x2001f650 = 1` |

## Model contract

`hw/nikon/devices/expower.c` models the endpoint as an explicit finite register
file:

- `exmonitor` (wire `0x6b`) stores one data byte per command below `0x12`,
  returns it on the next read, returns ready `1` for command `0x0b`, and NACKs
  any command at or above `0x12`.
- `exstatus` (wire `0x0d`) answers the two-byte request `08 01` with four bytes
  whose fourth byte is the class register: `0x00`, `0xa9`, `0xc9` or `0xe9` for
  class 0..3, with class 1 as the bundle default and a QOM `class` property that
  reset preserves. The request is acknowledged as data bytes, and the four-byte
  receive follows the controller's multi-byte path.
- `expower` keeps the source-defined `0x4f` register pair and the auxiliary
  `0x10` endpoint.

`/nikon/ex/power-devices` covers the register-file echo, the ready value, the
command bound, the class table and reset behaviour. The original bundle run
reaches the `0x6b` setup comparison and its success exit for commands 0, 1 and
3, and the periodic `0x0d` service, so the model is exercised by the image
rather than only by fixtures.

## Open items

- Command `0x10` is modelled as zero, which satisfies the `bit7 == 0` health
  requirement but is not a recovered device identity; `0x11` is likewise zero,
  so the reprogram branch never fires.
- The `cmd >= 0x12` NACK has no firmware path behind it (the image stops at
  `0x11`), so that boundary is fixture-only coverage.
- The write-only commands carry constants that the firmware re-sends on every
  commit; the model accepts them but does not claim they are inert on real
  hardware.
- `0x2001fdae/fdaf` (`cd 06`) are seeded but never transmitted; their consumer
  is unresolved.
- The class values themselves are board inputs in this model. The image only
  compares them with its snapshot, so no physical meaning, sensor identity or
  rail effect is claimed for any class.
