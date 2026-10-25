# Capture parameter packet and independent enables

The diagnostic module-input experiment first rejects an original WRITE32 at
MAS PC `102094c2`, PA `20069d00=03960000`, with `uncovered-address`. Its
declared input provides resource80 version15 and structural bodies for the
other module records. This is diagnostic execution under explicit constructed
inputs, not acceptance of normal startup or recovered factory calibration.
The original bytes and loader callbacks are unchanged.

## Producers and exact aperture

Original MAS `1023a9d2` copies24 bytes from the selected module record, obtains
its configuration and two168-byte auxiliary records, and calls the normal
wrappers in order:

| Wrapper | Writer | Exact board PAs |
|---|---|---|
| `1020c3ec` | `102094aa` | `20069d00..20069d24` |
| Called conditionally by `102094aa` | `102093ec` | `20069d28..20069dcc`, `20069dd0..20069e74` |
| `1020c3fa` | `10209578` | `20069e78..20069efc` |
| `1020c406` | Original body at `10209768` | RMW bit0 at `20069f00` |
| `1020c410` | Original body at `1020977c` | RMW bit4 at `20069f00` |

The first writer halfword-rotates two unaligned little-endian words, packs a
byte and a halfword, then writes six separate halfwords and a control word.
`103c5bca` assembles four bytes into a complete little-endian word. The two
auxiliary records each contain four seven-word arrays, two six-word packed
arrays and two terminal words: all42 aligned words in each168-byte record
have a direct store. The following writer stores every remaining word through
`efc`. Thus every one of the129 aligned words in `[20069d00,20069f04)` has an
identified configuration writer; this is not an enclosing peripheral aperture.

The existing capture-control device exposes this range as SysBus region1,
size`204`, separately from its unchanged region0 at `[20068000,20069180)`.
Machine glue maps region1 at `20069d00`. It does not expand region0 across
the existing readout-controller aperture at `20069800`, or map the gaps on
either side of the packet. Device diagnostics retain the actual PA.

## Finite retention contract

All accesses are aligned little-endian32-bit transactions. The following
masks are bounds of complete software-produced values, not physical IP masks:

| Packet-relative byte offsets | Accepted bits |
|---|---|
| `008`, `1d8` | `000fffff` |
| `00c..020`, `1d4`, `1dc..1e4` | `0000ffff` |
| `0cc`, `174`, `1e8` | `00ffffff` |
| `1f8` | `ffff00ff` |
| `1fc` | `000000ff` |
| `200` | `00000011` |
| Every other aligned word within the exact packet | Full32-bit payload |

For example `d08` accepts20 bits because the source ORs a full halfword with
an unmasked byte shifted by12. `d24` remains a raw word: its producer ORs
two unmasked32-bit fields and shifted bytes. Their caller may use small
control values, but that does not justify normalizing the retained word to
booleans. Packed auxiliary words also preserve all32 bits produced by the
byte plus shifted-word constructors. Unknown upper bits on bounded fields
reject the entire write and preserve the old value.

Readback retains the accepted word; the existing explicitly functional
cold-zero configuration profile extends only to these identified words.
Physical reset values are not established. The two original `f00` helpers
directly read and modify that word. No inverse getter for the rest of this
packet was identified in the scoped producer/caller analysis; general
retained configuration readback is a finite model convention, not a claim
that every original physical register is readable.

Writes overwrite the selected configuration word independently. They do not
start a timer, perform DMA, emit a request, set readiness, publish status or
raise an IRQ. Reset and unrealize discard the retained packet through the
same device lifecycle as existing capture configuration. The source does
not establish a normal completion producer for these enable writes.

## Commands and event boundaries

The nearby `10209796` translates caller selector bits0/1 into existing
capture unit bits8/4. `10209394` sets the separate unit configuration at
`20068018` and enables through `20068010`; `1020937c` requests run through
`20068008`; `102093d6` requests stop through `2006800c`. Those command and
status paths are distinct from packet `f00` bits0/4. Their existing bounded
run/stop and external-publication contracts are unchanged. In particular,
the shared unit4/8 stop cause`8000` in status word0 reaches existing IRQ94;
packet configuration does not synthesize that cause or a normal data event.
No sensor frame provider or reply is inferred from a configured packet.

## Native fixture scope

The new `/arm/nikon/sensor/capture/packet` fixture first writes and reads
`20069d00=03960000`; the old uncovered model fails that scalar assertion in
the parent's `native-packet-red.log`. This is an old-model native red, separate
from the original diagnostic execution that discovered the address.

The fixture then exercises all129 words across five independent data
patterns, both auxiliary banks, full-word payloads including`d24`, every
bounded field's unknown upper bits, byte/halfword/unaligned rejection, both
aperture edges, independent `f00` RMW controls, isolation from region0 and the
readout-buffer domain, reset, and absence of run intent, status, IRQs or a
readout request generation after virtual time advances. It embeds scalar
protocol values rather than original firmware or module-record arrays.
The parent's complete `nikon_sensor` native batch passes all26 subtests
(`native-packet-green.log`). Neither fixture success nor further diagnostic
continuation proves normal GUI boot.

The unchanged-firmware `packetdiagnostic` continuation crosses the original
`20069d00` store and stops at CPU0 PC`1020b158`, WRITE32
PA`2006a414=3`, reason`uncovered-address`. QMP reports debug/stopped, with
no display console yet. Its compatibility declaration removes ID68 at this
checkpoint, but the constructed resource80 body is incomplete and remains
an explicitly diagnostic input. Receipts remain in
`run sensorprofile/`.
