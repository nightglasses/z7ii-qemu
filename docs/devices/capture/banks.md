# Capture service parameter banks

The finite model retains11 source-identified configuration words in each of
two independent banks. SysBus region0 is mapped by the board at PA`2006a000`,
region1 at PA`2006be00`; each region is exactly130 hex bytes. Aligned
little-endian32-bit access is accepted only at the offsets below. The model
also accepts externally published twelve-word numerical results at100..12c.
It has no DMA, timer or IRQ producer of its own; the complete-bundle
[statistics coordinator](../statistics/protocol.md) owns that processing transaction.

## Objects and original producers

Read-only runtime observations identify two16-byte service objects at MAS
VAs`b18067b4` and`b18067c4`. Their fields are respectively
`{122,0,40,0}` and`{123,1,80,1e00}` in hexadecimal. `10209e8c` registers
the objects; field+4 is the bank ordinal, field+8 is the separate capture-unit
mask, and field+12 selects the parameter-bank displacement from`2006a000`.
These service identifiers are not asserted to be physical IRQ numbers.

`10209ef6` uses the selected object's displacement and writes eight unsigned
halfwords into bank offsets00/04/08/0c/10/14/18/1c, followed by two unsigned
bytes at20/24. `10209fea` writes offset40 as an unmasked word OR another
unmasked word shifted by4. Consequently the complete32-bit payload is valid;
this is not a two-boolean grammar or a whitelist for an observed value.

The wrappers `1020c592` and`1020c5fe` select the respective handles. Upstream
`10239efe(bank,request)` constructs an18-byte packet at MAS VA`b26b4730`:

| Packet field | Original source |
|---|---|
| Halfword0 | `request.u16(+0xc) >> 2` |
| Halfwords1..6 | Selected24-byte ROM row offsets`0/4/8/10/14/c` hex, from `10272aee(request.byte0,byte1,byte2)` |
| Halfword7 | Selected4-byte ROM row halfword+2, from `102729e6(request+4)` |
| Bytes16/17 | The latter ROM row bytes0/1 |

`10231c70` fills the request through pointer-array entry3. Its context is
`*(a1+0x144)` and the source fields are bytes at context+0x168a/+0x4d4/+0x1689,
the eight-byte field at+0x2e58, and the halfword at+0x4da. The shared pointer
array is`b25f2368`, with entry3 observed as`b25eefc0`. These offsets identify
data flow, not recovered physical geometry, clock units or sensor meaning.
The first observed bank1 word being0 follows the final request halfword's
shift by2; zero alone does not prove a ready or completion state.

`1023c916` configures the shared capture mask, calls the selected bank writer
and uses the existing commit path. `1023abaa` selects the bank from request
flags10/20; `1023aed8` is an incremental producer. These upstream operations
remain ordinary guest execution. The model does not reimplement their packet
construction or embed original ROM/firmware arrays.

## Finite configuration contract

| Bank-relative offset | Accepted bits |
|---|---|
| `00..1c`, step4 | `0000ffff` |
| `20`, `24` | `000000ff` |
| `40` | Full32-bit |

Every accepted write replaces only that bank's selected word. The other bank
and other words remain unchanged. An unsupported upper bit, width or
alignment returns a transaction error before mutation. Configuration reads
return the retained value. Cold-zero configuration readback is an explicit
functional convention; physical reset values and general physical register
readability have not been established. Reset and unrealize clear both banks.

Offsets28..3c and44..fc remain holes, including the unimplemented status
consumed by `10209ffa` at44. `10209f4a` consumes results at100..12c;
aligned32-bit reads succeed only after the external provider publishes that
bank. Result writes always fail. Reset invalidates both banks; commits,
staging and stop retain published values until replacement. The provider
updates only effectively running banks selected by capture units40/80.
Read masks are observation-only QOM properties and do not gate new jobs.
Storing a value at40 produces no status bit or completion. The130-byte
regions do not join the unrelated intervening devices.

The service objects' capture-unit masks40/80 belong to the existing separate
capture-control reset/enable/disable interface. They do not equate bank
offset40 with a run command, and configuring these banks introduces no new
completion into that interface.

## Evidence and validation boundary

The original diagnostic probe is qualified as a version-compatibility
experiment with an explicit timing hypothesis and incomplete resource80.
It is not a normal GUI-startup run or recovered factory-data receipt. Source
projections and runtime objects are retained separately in
`run capturebanks-20261024/`. The first discovered original bank1 write
and the native fixture's arbitrary bank0 value have distinct purposes.

The historical configuration-only receipt`native-banks-red.log` records the new
fixture writing `2006a000=1234` and reading0 from the uncovered model. The
fixture `/arm/nikon/sensor/capture/banks` then checks both banks' complete
fields across five data patterns, every bounded field's reserved upper bits,
the complete raw32 offset40, independent retention, byte/halfword/unaligned
rejection, both mapping boundaries, every hole and every unknown status/result
word, reset, and absence of capture run intent, pending causes, IRQs, readout
requests or RAM changes after time advances. HMP's physical read uses the
actual MemTxResult to verify errors; a zero-filled qtest read buffer is not
accepted as evidence of successful status readback. The parent's complete
`nikon_sensor` batch passes all29 subtests in7.75 seconds
(`native-banks-green.log`).

The unchanged-firmware `banked` continuation retains the preceding diagnostic
timing input. It executes `10209ef6` with object`b18067c4` and packet`b26b4730`,
crosses the former `2006be00` rejection and next rejects CPU0 PC`10208ea4`,
WRITE32 PA`2006b800=8`. QMP is debug/stopped before a display console exists.
The producer is the separate configuration family reached through request
flag40; it is not another bank selected by these two objects. That family's
result producers remain a separate evidence boundary. This continuation is
distinct from acceptance of the new cold-neutral virtual input.
