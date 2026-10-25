# Paired capture parameter records

The finite `nikon-capture-parameters` device maps board PA
`[20069500,20069800)`, size300 hex. It retains141 source-written configuration
words: a five-word record, a six-word record, two local controls and one shared
128-word table. All51 remaining aligned words remain transaction errors,
including eight identified result words. It produces no results, DMA, IRQ,
frame, readiness or guest callbacks.

## Six-word producer: source facts

Flag200 in `1023abaa` requires a non-NULL pointer-array slot7 and calls
`1023c846 -> 1023994c -> 1020c2d4 -> 102090a0`. `1023994c` builds a28-byte
packet on its own stack. All offsets and addresses below are hexadecimal;
u8/u16/u32 describe the software packet's little-endian fields.

| Board PA | Original packet expression | Retained bits |
|---|---|---|
| `20069514` | `u8[5] OR (u8[4] << 3) OR (u32[0] << 2) OR (u32[10] << 5)` | Full32 |
| `20069518` | `ROR32(LE32[6],16)`: u16[6] high, u16[8] low | Full32 |
| `2006951c` | `ROR32(LE32[a],16)`: u16[a] high, u16[c] low | Full32 |
| `20069520` | u8[e] | `ff` |
| `20069524` | `ROR32(u32[18],16)`: u16[18] high, u16[1a] low | Full32 |
| `20069528` | `u16[16] OR (u8[14] << 16)` | `ffffff` |

These are complete accessor expressions, not physical bit permissions. In
particular the first word accepts raw32: its unmasked u32 argument plus byte
fields can construct all32 bits. The observed value6 is not a command whitelist.
`103c5bca` explicitly assembles a little-endian word from four packet bytes;
the rotation swaps its two halfwords without truncation.

The input to `1023994c` is a separate32-byte record. Its bytes0/1 and word[c]
select an8-byte ROM policy row through `102ce57e`/`102ce450`. Row word0 supplies
packet u32[0]; when nonzero, the caller also programs the double10-bit parameter
and the table below. This is a conditional software producer, not a hardware
completion flag. Descriptor pointer input[18] supplies byte24 and halfwords
28/2a. `1023c830` maps descriptor byte24 equal2 to selector2, equal3 to0,
and all other values to1; the selector supplies packet byte5. Byte4 and word10
are explicitly0 in both recovered six-word producers.

The first producer normally sends descriptor halfwords28/2a to packet6/a,
with the paired low halves0. Input kind4/subtype5 changes packet8 to2. A
separate input predicate can replace packet c with input halfword4.
`10239820` may submit a second packet through the same writer when
`input.byte2 != 0`, `input.word8 == 0` and either byte15 or16 is0; that packet
uses descriptor halfwords2c/30 and2e, and input halfword4. On the other branch
(byte2=0 or word8 nonzero), only input kind a submits an alternative. It uses
descriptor28/2a, packet bytee=1, and can place descriptor28 in packet18
instead of6 according to input word1c. Other paths return without writing.
Each skipped store preserves the previous register; there is no implicit clear.

The same descriptors drive the later `2*x`, `(7*x)>>2` or `(3*x)>>1` values
rounded to16-byte spans. This supports an inference of sample packing and
extent parameters, but does not establish physical pixel encoding, ROI units
or sensor geometry. The model preserves the packed values without interpreting
or validating those inferred physical meanings.

## Complete adjacent configuration family

| Board PA | Contract and producer |
|---|---|
| `20069500` | Raw32 first-word packing, `1020905a`/`1020c2c8` |
| `20069504/20069508` | Full32 rotated halfword pairs from packet6/a |
| `2006950c` | Packet bytee, maskff |
| `20069510` | Packet halfword10, maskffff |
| `2006952c` | Two independent Boolean RMW fields, mask3 |
| `20069530` | Signed input pair truncated to10 bits each, mask03ff03ff |
| `20069600..200697fc` | 128 contiguous raw32 table words |

The first five words form the separate20-byte record from
`10239c34/10239bcc`. Their first control word has the same packing as9514
without the packet word10 shift. `1020c2ea` changes only952c bit0;
`1020c300` changes only bit1. Their encoded MMIO values are0..3, and changing
one bit preserves the other. No local control generates a result or run request.

`1020c30a` packs signed policy-row halfwords+4/+6 as
`(a & 3ff) OR ((b & 3ff) << 16)` at9530. `1020c316 -> 1020916c` iterates
128 times and loads all512 input bytes through `103c5bca`, writing one32-bit
word per iteration. The table is shared, not independently duplicated for
each parameter record. No firmware table bytes are embedded in this device.

Raw instructions correct an omitted decompiler argument: `1023994c` calls
`103aec7c(1,1)`, selecting ROM VA`119b5ce4`, not the alternate`119b5ee4`.
The selected512 bytes are monotone when viewed as256 u16 values, ending at1023;
the alternate ends at4092. That observation alone does not recover the physical
transfer function. The model retains all packed words and does not apply a
guessed curve to pixels.

`1020c2e0 -> 102090fe` reads9518 and9524, then exchanges their high16-bit
halves while retaining each low half. This establishes a real readback
dependency. The matched SLA implementations include the parameter writers,
two-bit RMW, signed10-bit pair,128-word loop and high-half swap; their source
instruction receipts remain separate from MAS original execution.

## Shared control relationship

`1023c846` configures shared IRQ masks with unit200, writes the parameter
records, sets952c bit1, then calls `1020c23c(1)`. The selector mapping is:

| Selector | Parameter path | Both shared command100/104 masks | Shared run08 units |
|---|---|---|---|
| 0 | flag100, `1023c8ae`, five-word record,952c bit0 | `100` | `400` |
| 1 | flag200, `1023c846`, six-word record,952c bit1 | `80` | `200` |
| 2 | Combined low-level control API | `180` | `600` |

`1020c1ee` separately asserts/deasserts the selected shared reset bits and
`10208f9e` enables the units. `1020c284` stops them. These reuse the existing
capture-control profile; parameters, local bits and table writes themselves
neither submit that request nor complete it. The new device has no connection
that turns commit/run into a result or data event.

The result getters `10209186/102091a8` read9534/538/544/54c and
953c/540/548/550. Their data producers and cold values remain unrecovered;
all eight remain errors. The earlier capture-group cold-empty assumption is
not extended to these distinct consumers. Other gaps through95fc also reject.

## Model assumptions and boundaries

Accepted accesses are aligned little-endian32-bit. A successful write replaces
only its own retained word. Unknown offsets, widths or upper bits reject
before mutation. Configuration readback and device/system cold-zero reset
are explicit functional conventions, not measured physical reset values or
universal register readability. Reset/unrealize clears configuration and the
diagnostic latch. There are no timers, callbacks, events or DMA to cancel.

The aperture ends exactly at20069800, where the existing independent readout
buffer device begins. No backing RAM, actual memory extent or firmware bytes
are changed for this parameter model.

## Validation and original execution

The parent's `/arm/nikon/sensor/capture/parameters` native fixture first fails
on the old uncovered model: synthetic9514=80000006 reads back0. This is
separate from the original value6. After implementation, all32 Sensor native
subtests pass in`native-params-green.log`. The fixture covers all141 fields
with four patterns, high raw32 controls/table data, every bounded upper bit,
both local control bits, the accessor's read/write swap sequence, preservation after control0,
all51 unknown aligned words before/after reset, byte/halfword/unaligned access,
and the following device's independent field. Shared commit/run retains the
parameters without generating pending status, IRQ, readout requests or RAM
writes after virtual time advances. Reset clears the finite configuration;
the final scoped fixture also checks actual HMP readability of every known
word after reset, independently of qtest's zero-filled error buffer.

The unchanged20-host-second `captureparams-20261024/green` run uses the same
cold-neutral input and final read-only debugger snapshot. Six original loader
COPY ranges compare with the file, original version guard returns0 with packed150,
and property6433 is empty. The prior capture-group cold-empty assumption still
applies; no post-submit result producer is added by this change.

The actual flag200 input is MAS VA`b25eeff8`, kind3/subtype0. Its descriptor
byte24=2 and halfwords28/2a are564/396; policy control is1. The six retained
words are `6,05640000,03960000,0,0,0`. The second producer returns without
overwriting them for this input. The table compares byte-for-byte with ROM
VA`119b5ce4`;9530 is00200040 and952c is2. Original code then submits both
shared command masks80 and reaches `10208f72 -> 1020aafc(unit200)`.

The next first uncovered access is CPU1 PC`10404398`, WRITE32 PA`c33ab400=100`.
The software interpolation-record generator `10404354` dereferences its
packet's destination pointer at+28 for that first store; destination ownership,
extent and translation are the next boundary. No memory aperture is added to
pass it. QMP is debug/stopped and neither display head has a console. This
qualifies software progress under the declared finite profiles, not physical
parameter meaning, completed capture, normal GUI output or full boot.
The paired flag100 path and swap helper have source/native-fixture evidence;
they are not executed in this original flag200 receipt.

Raw source projections, instruction comparisons, native red/green receipts,
actual argv, QMP status, read-only snapshots and loader/table comparisons stay
in`run captureparams-20261024/`.
