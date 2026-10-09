# Notification parameters and external events

`notifier.c` implements the complete finite interface consumed by the official SLA source family at VA `1005fda8..1005ffb8`. The original private PA is `2c0b0000`; registration at `1005fe2c` binds IRQ218 (`da`) to Thumb handler `1005fda9`. These facts do not establish a physical audio, GPIO or signal-controller identity.

The source asset is `sla-whole-source-5/sla-rows.jsonl`. Its exact original instruction bytes, Thumb contexts, references and literals are retained in ignored `work/source/families.json`. The two-family packet contains456 official rows. Literal VA `10060040` supplies PA `2c0b0004`; `10060044` supplies software object `c100549c`; `10060050` supplies PA `2c0b0000`. ROM COPY1 offset `83c4d4` maps to VA `c1000000`. The seven words at `c1005480..549c` are initialized payload zero, not a hardware reset witness. Registry ID119 is a software handle, not an IRQ or MMIO address.

| Offset | Complete source consumer | Functional device interface |
| --- | --- | --- |
| `00` | `5fef2` clears bit0, inserts a two-bit selector at2; `5ff48` changes bit1 | Retain the finite low4 control fields; no inferred start/reset effect |
| `04` | `5fef2` writes a halfword parameter as a word; ISR `5fda8` inverts bits0..2 and writes them back; `5ff48` preserves/restores bit4 | Retain fields3..15, external pending events reflected as active-low bits0..2; writing1 acknowledges the corresponding event |
| `08/0c` | Bytes from setup parameter offsets1/2, stored as words | Retain full byte fields |
| `10/14/18` | Full words from setup parameter offsets4/8/c | Retain opaque32-bit parameters |
| `20` | `5ff48` reads a nonzero state before taking its guest wait path | Read the actual external busy input |

Only aligned little-endian word transactions at these offsets are accepted. Width, field-bound and hole violations return `MEMTX_ERROR` without changing state. There is no source-bound FIFO, DMA address, transfer length, sample packing or clock in this family, so the model does not create one.

The provider boundary is three rising-edge `event` inputs and one level `busy` input. A provider must publish an actual event; opaque parameter/control writes, polling and elapsed virtual time never produce status or IRQ. Each event latches independently; acknowledgement clears only the selected latch. IRQ reflects retained pending events. No gating role is assigned to opaque bit4 or other configuration fields without its producer evidence. Active-low status/acknowledgement, rising-edge publication and IRQ aggregation are explicit functional laws derived from the software's inversion/notification shape, not silicon specifications. Cold disconnected busy is0 and pending is empty; reset clears configuration and latches while retaining actual provider levels, so a held input does not synthesize another edge.

Machine ownership remains separate: map one instance in the SLA private root and connect its real SysBus IRQ to218. Stable QOM child `/machine/sla/notifier` is used by the standalone SLA fixture. External providers are disconnected by default. The native fixture covers all finite control combinations, complete setup fields, no read/time-driven events, all three source acknowledgement states, rejected widths/holes, live busy readback, reset and a genuine new edge after reset. It embeds no factory instructions and does not claim an original-firmware success.

The queried intervals `5fe4e..5feaa` and `5ffbc..60040` have no official CODE, Data or direct incoming references in this saved asset. `5ffba` is the high halfword of the preceding four-byte POP. The sole source owner is investigating indirect/ADR consumers; the model does not promote these gaps to executable functions, invent their semantics or treat them as device readiness gates. This child was not compiled; the sole native builder owns actual results.
