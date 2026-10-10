# Lookup interfaces

One core implements the two complete source-defined configuration/table/result
families. `nikon-lookup-bytes` and `nikon-lookup-words` are explicit interface
profiles. These names describe staged tables, not an identified image algorithm,
codec, or silicon IP. No processing, DMA, clock rate, or image output is invented.

## Whole source and board bindings

The official SLA source11 contributes 1,542 rows over `10095408..100963CE`.
All 169 qualified references to `24072xxx`/`24073xxx` occur inside these two
families; the remaining accesses are pointer arithmetic inside their complete
table loops. The source artifact is `sla-whole-source-11/sla-rows.jsonl`, paired
with its generated `sla.S`. This task uses those artifacts, without another
decoder or qualification index.

| Profile | Main MMIO 0 | Auxiliary MMIO 1 | Handle / guest object | IRQ / handler |
| --- | --- | --- | --- | --- |
| bytes | `24073400`, size `F4` | `24073000`, size `200` | `114` / `C1001408` | 106 / `10095433` |
| words | `24072400`, size `F4` | `24072000`, size `200` | `113` / `C1001414` | 105 / `10095ADF` |

Sizes and register offsets in this document are hexadecimal. Addresses above
are the board's finite mapping of the original literal access addresses.
Software handles and guest objects are not hardware IRQ numbers. Main regions
are little-endian aligned 32-bit interfaces. Auxiliary width is strictly one
byte or one aligned 32-bit word, according to the profile; holes fault.

For bytes, the stop routine is `10095408`, ISR `10095432`, registry `10095636`,
initializer `10095652`, table/configuration constructor `10095778`, submit
`100959AE`, and stop alias `100959EC`. For words, stop is `100959FC`, ISR
`10095ADE` with helper `10095A26`, registry `10095CE8`, initializer `10095D02`,
constructors `10095E58` and `100960E4`, submits `100960B8`/`1009624A`, and
helpers `10096278..100963CE`. The source literal sets are `10095804..10095818`,
`100959F0..100959F8`, `10095DF8..10095E10`, `10096218..10096224`, and
`100963D0..100963D8`. Code deliberately branches around these data pools.

Initializers select object E8's clock controls `17`/`16` and enable the actual
IRQ. Clock/reset helpers use encoded `30012`/`20018` and object152's outputs;
these remain calls into existing guest-controlled devices, not host clock
effects inferred here. Guest callbacks and guest-RAM status accumulation stay
in the guest. There is no original runtime MMIO receipt for these models yet.

Neither initializer reads a lookup busy/status register. The first explicit
stop callers are configuration methods `1009573A` and `10095E1C`. Stop clears
`C000` in main `00` and `04`, sets auxiliary byte `01` bit7 or word `04`
bit31, then polls main `88` until its sign bit clears. Both ISRs also call
these stop helpers on specific error/completion combinations. No source body
produces that hardware busy value or establishes its physical cold state.
The explicitly authorized functional reset profile starts idle when there is
no owned or observed active operation. A stop write cannot itself retire an
active operation or serve as its successful acknowledgement.

## Main finite fields

Both profiles stage zero-initialized control/configuration words. This cold
configuration is a named functional convention. The positive fields are:

| Offset | Writable mask / role |
| --- | --- |
| `00`, `04` | `C03E`, `C00F`; command field accepts only source clear `0000` or submit `4000`; other `C000` encodings reject |
| `08` | bytes `10C01`; words `10110C01` |
| `10`, `14`, `30`, `34`, `40`, `54`, `58` | Opaque full words, stored without DMA or address interpretation |
| `18`, `1C`, `50` | `FFFF` |
| `84` | Full source mask word |
| `90`, `94` | Configuration mask `3000F7`, with externally supplied high result field |
| `E0` | `FF3F` |
| bytes `F0` | Full opaque configuration word |
| words `20`, `24`, `28` | `FFFF` |
| words `B0` | `FFFFFF` |
| words `C0`, `C8`, `D0` | `03FF03FF` |
| words `C4`, `CC`, `D4` | A source sign-extended 16-bit value |

Read-only observations `70` (count), `74` (result), and the high
result fields of `90`/`94` have no default data. Pending `80` is read followed
by same-value writeback in both ISRs. These reads and the pending ACK return
`MEMTX_ERROR` until a real provider publishes a complete sample. A configuration
write to `90`/`94` supplies only the known low fields; it cannot establish an
unknown external result. Main `88` has the separate functional lifecycle
below. Control writes do not manufacture readiness, completion, or an IRQ.

## Auxiliary finite fields and actual tables

Bytes has configuration offsets `00`, `01`, `03..0A`; masks are `1F`, `83`,
and `FF` respectively. Result bytes `0B`, `0C`, `0D`, `0F` reconstruct the
source's 32-bit result as `0F:0B:0C:0D`. Status `0E` is acknowledged by writing
its inverse. Words has configuration `00` (`73F`), `04` (`80000003`), `0C`
and `10` (`3F`), `14` and `28` (full words), `18` and `1C` (`FFFF`), and `24`
(`7`). External result/status registers are `20`, `2C`, `30`, `34`, `38`,
`3C`; status `2C` likewise receives an inverse acknowledgement.

Both table windows are exactly `100..1FF`: four tables of 64 actual source
bytes. The byte constructor copies each byte directly. The word constructor
explicitly packs four source bytes in big-endian order before writing one
32-bit little-endian MMIO word. The model preserves that distinction and
exports the original logical byte ordering through `nikon_lookup_table`.
Unwritten table reads and incomplete table exports reject; zero-filled RAM
does not stand in for missing table bytes. Table writes and reads do not run
an algorithm or infer a transfer to another device.

## Provider, acknowledgement, and reset

`nikon_lookup_publish` accepts one complete `NikonLookupSample` atomically.
The caller supplies actual observed busy/count/results, both lane result
fields, pending and auxiliary pending. The word profile also receives its
four additional auxiliary observations. Invalid profile fields reject the
entire publication without changes. Pending causes append until acknowledged;
later complete samples replace data observations without losing old causes.
There is no attached provider by default.

Busy `88` is the explicit functional exception to unknown external data:
with no owned or externally observed active operation it reads idle zero.
Writing source submit field `4000` owns that lane's opaque operation and sets
busy bit31. Clearing `C000` and the source's auxiliary stop writes do not
clear owned busy. A complete real provider sample with busy bit31 set records
an external active operation; a later complete real idle sample retires the
owned operations. Provider disconnection does not retire previously active
work. Reset retires the functional operations and returns to idle. This is a
declared virtual lifecycle, not evidence that physical silicon resets idle.
It supplies neither pending causes nor result data, and elapsed time cannot
complete work. The cold stop/configuration path can therefore observe actual
model idle, while stop during active work still requires an external idle
observation or reset.

Main `80` uses W1C, auxiliary `0E`/`2C` uses W0C, and IRQ is
`connected && (pending & mask84) != 0`. These are explicit functional
conventions consistent with the source's readback/inverse ACK and per-bit
mask tests, not measurements of silicon polarity. The provider must publish
real main summary bits; auxiliary data publication does not synthesize a main
cause. Main and auxiliary ACKs are independent. There is no timer, automatic
success, synthetic frame, or arbitrary memory copy.

`nikon_lookup_disconnect` withdraws data and deasserts IRQ while leaving staged
configuration, tables, and active-operation ownership intact. Reset clears
configuration, table validity, causes, ownership and connection. Unknown data
observations fault again until a fresh complete publication; busy uses the
explicit idle/active lifecycle above. Establishing physical initial status, processing
semantics and event timing requires actual hardware/provider evidence.

## Native validation

The native `tests/unit/nikon/lookup.c` reuses the established full-system QOM
fixture recipe and production `memory_region_dispatch_*` API. Four cases cover
both profiles' full finite configuration, all four 64-byte tables and packing,
real `MemTxResult` rejection, disconnected time progression, complete provider
data, masked IRQ, retained overlapping causes, W1C versus inverse W0C ACK,
owned busy before provider connection, cold stop at idle, unchanged active busy
across stop/disconnect, real idle publication, invalid publication and reset.
The sole builder compiled the fixed integrated source and executed all four
native cases with exit0 in0.65s; the receipt is
`work/lookupprotocolnativereceipt.log`. No physical result values or completed
original processing are inferred from the synthetic provider fixtures.
