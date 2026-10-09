# Dual-bank image and event protocol

The existing device maps two finite4KiB-unit banks, each with two ten-layer
image ports, and three narrow global apertures. Source-qualified packet
consumers, data processing, virtual encoding assumptions and native validation
are detailed in [image.md](image.md). The current source supports actual image
transactions and finite packed tables in independent2404/2405 apertures.

Board mappings remain PA20140000/20144000 with independent B000/D000/D100/
E000 retained control words. Access is aligned32-bit and only explicitly
enumerated fields are supported. Mapped bank extents do not authorize arbitrary
register reads or writes. Scalar configuration has named zero reset/retained
readback conventions; no opaque write generates data or an event. Globals
D004/D200 still lack a source-bound operation and are rejected.

Main1100 is the original full-byte-derived IRQ route: values1/2/3 select
IRQ111/112/113. Main1210 is a named one-inhibits mask,1214 retains raw pending
and uses W1C. Two banks aggregate on each route. ACK or rerouting one bank
cannot erase the other bank's contribution. Optional64 pending-bit inputs
remain explicit external events; cold start leaves them disconnected. Reset
retains actual provider levels so it does not create an input edge.

Successful input DMA/composition supplies its causal frame event through this
same pending/mask/IRQ protocol. Original1046bc6c reads/ACKs the raw word and
invokes the registered callback with its own stack packet;1046efa8 and the
kernel event APIs produce the guest flag. Host code does not modify object+C,
object+10, software active bitmaps, IRQ registration tables or eventflag RAM.

The class remains unmigratable. Original GUI frame submission and physical LCD
identity remain unverified; no initialization or synthetic qtest is presented
as an original menu receipt. New image fixtures and sampler/table request
fixtures are handed to the sole native builder as one batch.
