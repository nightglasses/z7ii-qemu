# Processing frontend and produced-buffer provider

`hw/nikon/devices/processing.c` models the finite original C1.70 processing
frontend at physical `24070000`. It is separate from the existing MXIC
configuration window at `24074000`. The class is
`nikon-processing-functional`, with one `1000`-byte SysBus region and one
IRQ output connected by the machine to original IRQ110.

The complete known configuration, indexed input-address table, request control,
capacity/address submission and IRQ acknowledgement are implemented together.
An optional QIOChannelFile supplies **externally produced complete output
buffers**. It does not implement the original processing transform. Its default
is disconnected: elapsed time alone cannot produce output bytes or completion.

## Original source contract

All code addresses below are original MAS execution VA, with linear source
offset `O = VA - 10020000`. Physical MMIO comes from the original literal,
not a guessed VA-to-PA conversion. The single saved native projection contains
76 whole owners, 2,274 instruction providers, 230 calls and 754 expressions.
Original Listing bytes and pcode remain the source; raw decompiled C was used
only as a navigation aid.

`1051C4EE` writes its value argument to `24070000 + offset`; literal
`1051C60C`, O `4FC60C`, is bytes `00 00 07 24`. Read wrapper `1051C4F4`
uses the same base. The original provider table at `B1806FC0`, O `2380F54`,
binds these through `104D6F44/104D6F48`.

| Registers | Actual original producer or consumer |
|---|---|
| `438 / 43C` | `104B9B7A` submits indexed pairs through `1051C4EE`. |
| `464` | `105A85B8` submits `10/11` before input addresses; `105A84A8` submits `0/1` before an output pair. |
| `454` | `104B8C6C` writes 1; `104B8C48` writes 2. |
| `470 / 474 / 478 / 47C / 490 / 498` | `104B9BF4` writes pair[1], then polls that same word until bit0 clears. |
| `494 / 49C` | The same `104B9BF4` writes pair[0] after the address poll, selected by channel 0/1. |
| `C00` | `104B8CB4` writes `800`; `105A81C4` writes `80`, polls `FC0.bit7`, then reads result metadata. |
| `C24 / C28 / C30` | Initialization enables `001FFEB0`; callback `105A8284` can narrow C24 to `80`. IRQ110 handler reads C28 and copies the word to C30. |
| `FC0 / D08 / 4D4 / 4FC` | Original result consumers use index/busy, packed result dimensions, a result offset, and a separate auxiliary result. Their content is not inferred from input addresses. |

Input selector arithmetic is
`(role == 1 ? 0 : 40) + (channel == 0 ? 0 : 20) +
(field == 0 ? 0 : 2) + index*4`, followed by the second address at selector+1.
The model retains 128 independent selector words as a bounded functional table;
this capacity is not a proved hardware maximum. Unknown selectors fail instead
of aliasing a different role or channel.

The submitted software descriptor is 20 bytes. Source `104A1D96` checks
`u16(+08) == u16(+10)`, computes `u16(+02)*u16(+08)*3/2`, and forms three
`{computedSize,addressA,addressB}` triples for `104D7120`. The size remains in
the software backend record at +4C. This arithmetic does not prove an image
encoding, nor does it make addressA/addressB a memcpy source/destination pair.

The output capacity/address roles have an independent original allocation
chain. `1059BB64` calls allocator `1030D96E`, obtains its data base, and stores
base/extent pairs via `105ADC70 -> 10690486`. `1069050A` returns those pairs;
`10691A94` places pool0's base at request+0C. `106B7FA4` copies that request to
object+18, and its original calls at `106B8156/106B8166` pass
`{04000000, allocatedBase}` and `{04000000, allocatedBase+04000000}` to
`105A84A8 ->104B9BF4`. Thus the four `470..47C` slots share their channel's
source-proved capacity at `494/49C`; the capacity is not borrowed merely
because the registers are nearby.

## Finite configuration and status lifecycle

`104B949C` produces the finite parameter words in `A80..A8C`, `B78`,
`C80..C88`, selected `D40..DF0`, and selected `E00..EFC`. `104B9C56`
adds `404/408/480/4A4/4B0/4B4/4C4/4C8`. Only the individual offsets in
`config_offsets` are accepted, including the actual holes. The model retains
their original packed values; it does not claim a codec vocabulary or physical
writable masks. Four-byte little-endian accesses are required. Unknown offsets,
widths, commands and currently unbound result metadata report a bus error.

Functional reset starts these registers, selector words and pending IRQs at
zero. Start 1 snapshots the output configuration and arms one complete-buffer
request. `FC0.bit7` means that this functional request remains outstanding;
no result index is fabricated. A disconnected provider leaves it outstanding.
Stop 2, reset `C00=800`, and functional flush `C00=80` cancel the pending
delivery. Already written bytes remain in RAM, and a canceled delivery emits
no completion. The functional flush does not manufacture the original final
result metadata that `105A81C4` subsequently reads.

An externally produced successful full buffer latches raw status bit9 for
channel0 or bit10 for channel1. Original normalizer `105A7932` maps these to
bits22/21; `105A7F0E` then emits event0/event1 and reads the retained
capacity/address pair through `1068CE9A`. This is why a short payload cannot
announce this full-buffer event. Short final results require the separate
`D08/4D4/FC0` metadata protocol and remain disconnected.

IRQ is asserted for `pending & C24`. Completion remains pending while masked;
changing C24 can reveal it. C30 is write-one-to-clear. Acknowledging another bit
does not clear this event. Stop leaves an already completed pending event intact;
reset clears it. A new request needs a new original start write.

## Explicit produced-buffer profile

`result` is an optional regular byte-file path opened read-only through QEMU's
QIOChannelFile. `result-slot` explicitly
selects 0..3, corresponding to `470/474/478/47C`; the channel is slot&1.
The exact `fstat` byte length must equal the snapshotted capacity at `494/49C`;
there is no sector rounding. A 51-byte file can satisfy a 51-byte capacity, and
a 511-byte file cannot satisfy a 512-byte capacity. The device rewinds the open
file for each accepted result request. Existing BlockBackend drive identifiers
are removed; `result` now names the byte file directly.
Output addresses must be nonzero, even, nonwrapping normal writable RAM. MMIO,
ROM, inaccessible memory, a short file and an unavailable provider cannot
announce completion. Slots `490/498` and other result metadata retain their
separate numerical source contracts; this profile does not deliver through them.

The provider reads at most 4096 bytes per virtual timer event and writes the
actual supplied bytes through QEMU DMA. Its nominal 48MHz byte service rate is
a named virtual timing assumption, not an EXPEED throughput claim. All required
DMA writes must succeed before C28/IRQ110 are published. Stop/reset/flush remove
the timer and invalidate its generation. Exact extent is checked at start and
before and after each `qio_channel_read_all`; an actual short read or EOF fails
instead of supplying padded zeros. Every DMA chunk rechecks writable RAM. Failures are
diagnosed and leave the request without success status.

`include/hw/nikon/processing.h` also exposes
`nikon_processing_publish(dev, slot, bytes, length, errp)` for a real external
processor. The caller holds the BQL and supplies one complete buffer; it uses
the same start snapshot, capacity/address checks and post-DMA completion.
Neither API nor file provider generates processing bytes from the indexed inputs.

## Callback closure and validation scope

Original IRQ110 registration is `1051C526 ->103A051A`, encoded handler
`1051C4FD`. Its worker `1051C490` consumes the 32-word ring `B1CF17E8` and
loads its callback from `B1806FAC`. Producer `105A83B6 ->104D6F3E ->1051C5C4`
stores original encoded pointer `105A8285` from literal `105A87B4`, O `5887B4`.
The registered callback dispatches through `105A8A3A`: tag1 reaches `105A7F0E`,
tag2 reaches `105A7A22`. The coordinator's single native incremental recovery
batch has now recovered both previously missing callback/helper bodies: 117
and 10 original Listing instructions, successful C decompilation, exact source
bytes/pcode/context, and official basic blocks/edges. Those native instructions
agree with the bounded original decodes used to close the registration. The
model retains the actual dispatch instead of substituting a direct callback.

The original eight native qtest cases cover actual supplied-buffer DMA and IRQ, four explicit
slots, chunk boundaries and mask/W1C behavior, cancellation after a partial
write, default disconnection, short-file/MMIO/unmapped/ROM/zero-address rejection,
configuration snapshot isolation, and independent indexed input roles. The sole
builder compiled/linked that batch and observed all eight passing in
`work/processingfinal.log`. That prior receipt used sector-aligned file fixtures
and did not prove exact byte-file length.

The exact-file fix adds two focused cases: 51-byte success plus 511-byte rejection
against a 512-byte capacity, and truncation after a real 4096-byte DMA chunk.
Cancellation also covers machine reset, in addition to stop, flush and register
reset. The focused `bytes`, `eof` and `cancel` cases require the sole builder's
new actual receipt; no new passing result is claimed before that run. These are
synthetic native fixtures. There is no original runtime processing or
capture-to-save witness. The capture-to-processing producer join, original
transformation and final-result metadata remain explicit external dependencies.
