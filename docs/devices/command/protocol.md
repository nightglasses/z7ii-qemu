# Word command and message windows

## Original source evidence

The complete C1.70 SLA Listing family `100607F0..10060C8C` and its callers `10059262..1005950C`/`10059576..10059628` describe word message windows, not byte DMA. They are preserved in the source5 qualification output `sla.S`; the original pointer data at SLA VA1016B2D8, payload O14B2D8 is75 words, three25-word records. The first16 pointers in each record address logical-slot status, the next8 address command engines and the last points to an engine ownership bitmap. This data classification does not classify those bytes as instructions or identify a physical IP.

| Bank | PA root in the SLA domain | Slot status | Eight engine windows | Ownership | GIC IRQs |
|---|---|---|---|---|---|
| 0 | 04250000 | +80..BC,16 slots | +100..480, stride80hex | +900 | 49..64 |
| 1 | 24061000 | +80/+84,2 slots | +100..480, stride80hex | +900 | 227/228 |
| 2 | 2C041000 | +80/+84,2 slots | +100..480, stride80hex | +900 | 247/248 |

The other14 status pointers in each of banks1/2 are zero, not additional MMIO ports. The whole `100608C6` initializer installs20 callbacks: literals60BF0..60C3C select the wrappers for bank0 slots0..15, bank1 slots0/1 and bank2 slots0/1. The registry at C1006438 contains three20-byte records for objects153/154/155; its packed configuration is0FFF0000/01FF0001/01FF0002. `10060996` enables the selected20 guest IRQ numbers from a software mask; this is not a device interrupt-enable register.

`100609CE` tests8 busy bits, claims a free engine by writing one logical-slot bit to its +0 port and checks exact readback. `10060A66` releases it by writing0. `10060C70` resolves an engine and a word-indexed offset. The remaining helpers consume these finite ports:

| Engine offset | Original consumer/producer |
|---|---|
| +0 | Onehot slot ownership, allocation readback and release |
| +4 | Three-bit control from10060A96 |
| +8 | Start1 from10060B50 |
| +10 | Low16 parameter written by10060AAA |
| +14 | Onehot logical-slot receive acknowledgement from10060B90 |
| +20 | Low16 extension written by10060AC2 |
| +34/+38 | Low16 acknowledgement and cause read from10060C50/10060C40 |
| +40..7C | Raw32-bit words from10060AF8/10060B24 |

Bank1 request caller100594B2 obtains object154, allocates slot0, writes control3 and parameter4, writes the caller's counted words, clears a software wait object, starts, waits and releases. Callback10059262 handles slot0 by reading+38 and echoing it to+34 before waking the software waiter. Slot1 reads9 actual words using the ISR-supplied engine index, acknowledges bit1 at+14 and invokes the installed message callback. Bank2 has the analogous receive consumer10059576. The packet contents remain opaque; these calls do not bind RAM source/destination, byte count, code-clone data or a processing algorithm.

ISR100607F0 reads the slot's low12 status. Nonzero low7 bits yield callback engine0; otherwise values2..9 in bits7+ select the TBB. The exact8-byte original table `11 04 06 08 0A 0C 0E 10` routes value2 to engine0 and values3..9 to engines1..7. The ISR calls the bank callback at C144DC80 with `(logical_slot, engine_index)`. It does not write an acknowledgement itself. Hardware queue priority and a physical reset encoding remain unknown.

COPY, HDMAC and FILL each bind actual memory transfer operands; TRANSFER binds separate graphics packet operands. None shares this word-window protocol. No DMA address space is added, and no private request consults system memory. Each machine maps the three register regions into its own domain. Shared numerical addresses in other domains do not establish a peer route.

## Implemented functional boundary

`nikon-command` exposes three finite SysBus MMIO regions and20 SysBus IRQ outputs in table order. Only the listed aligned32-bit ports exist; holes, other widths and unbound fields return MEMTX_ERROR. Cold reset clears owned windows, words, requests, pending events and IRQs. This zero cold state is a virtual assumption. Each engine keeps actual caller words, their written mask and opaque control/parameter/extension values. Claiming a busy engine retains its existing owner so the original readback detects lost arbitration. Changing an active or received frame rejects.

Start publishes a named `request` output at `bank*8+engine`, retaining the real frame. The original sender passes its leading count byte to `10060AF8` and writes that many contiguous words from word0; for example `GM3DSPEV1` builder `10054A90` writes two words. Start therefore requires a nonempty contiguous word prefix, not all nine words consumed by the receive callback. A new ownership claim clears unwritten words, and a hole in the prefix rejects. The device has no timer, transforms no data and produces no response or completion IRQ. An unconnected output stays pending. `nikon_command_request` returns a side-effect-free typed snapshot with a unique token and an exact written-word bitmap; the token is a host lifecycle guard, not guest MMIO. Cancellation, reset and unrealize invalidate the pending request. A later reuse obtains a different token, so delayed completion of the old request rejects.

An actual connected provider may call `nikon_command_complete` with the live token and nonzero16-bit cause word. That operation publishes a completion for the owned logical slot, lowers the request output and raises its actual IRQ. It supplies no payload. The guest reads that cause at+38 and acknowledges the same word at+34, clearing the event; release remains a separate+0 write. Cause encoding is supplied by the provider, never inferred from control or elapsed time.

An actual provider may alternatively call `nikon_command_receive` with a free engine, real1..16-word frame and valid logical slot. The frame is copied verbatim into that engine; no parsing, reply or extra message is synthesized. Slot status uses `(engine+2)<<7`, the original ISR's decoded index. Receive+14 acknowledgement clears the matching slot event and releases its ownership, permitting the next incoming frame. The model supports one pending event per slot and rejects overwrite. These queue, ownership-release and whole-cause acknowledgement rules are explicit finite virtual conventions; the original callers establish the operations but not arbitrary physical queue depth or partial-ack semantics.

No automatic peer route is identified. Machine wiring must leave request outputs disconnected until a complete provider/consumer join proves the route and real message source. The API is a data/event boundary, not permission to manufacture firmware replies, successful code copies, guest flags or an IRQ from a start write. Reset and unrealize cancel all lines without publishing an event. State is unmigratable.

## Validation scope

The native units `/nikon/command/causality` and `/nikon/command/receive-reuse` drive real MemoryRegion transactions, capture20 IRQ and24 request outputs, and supply actual patterned words through the provider API. They check nine-word request integrity, a counted two-word request with zeroed tail, empty and non-prefix rejection, no completion after a1-second clock jump, live-token completion and guest cause ACK, real9-word receive and slot ACK, lost ownership arbitration, all20 independent logical-slot outputs, reset/cancel and stale-token rejection. They use the established system-library qtest unit entry pattern. Compilation and execution belong to the sole builder; no original SLA command transaction or matched peer response is claimed here.
