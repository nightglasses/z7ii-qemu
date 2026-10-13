# Dual-bank image and event protocol

The existing device maps two finite4KiB-unit banks, each with two ten-layer
image ports, and four narrow global apertures. Source-qualified packet
consumers, data processing, virtual encoding assumptions and native validation
are detailed in [image.md](image.md). The current source supports actual image
transactions and finite packed tables in independent2404/2405 apertures.

Board mappings remain PA20140000/20144000 with independent B000/C000/D000/
D100/D200/E000 retained control words. Access is aligned32-bit and only
explicitly enumerated fields are supported. Mapped bank extents do not
authorize arbitrary register reads or writes. Scalar configuration has named
zero reset/retained readback conventions; no opaque write generates data or an
event.

The C1.70 producer has one direct call to the D200 wrapper:1044a4c4 supplies
zero while D100 is asserted, then invokes the existing delay wrapper before
clearing D100. Accordingly D200 admits and reads back only zero; nonzero values
remain errors. The unique C0A0 wrappers are called at1044a4ca and1044a520 and
write zero/one around the event-object transaction, so that word retains only
bit0. Original1046e3c2/1046e3fe/1046e572 additionally bind the sparse C000
words at offsets004/008/00C/010/014/018/02C/034/090/0A0/0A4/0C8 to their
packed source fields and bit masks. Original1046d3c6/1046d3ee bind the bank1
words at PA20145420/20145424 to one packed three-halfword tuple. These finite
retained-readback contracts create no event or completion. D004 still lacks a
source-bound operation and remains rejected.

Main1100 is the original full-byte-derived IRQ route: values1/2/3 select
IRQ111/112/113. Main1210 is a named one-inhibits mask,1214 retains raw pending
and uses W1C. Two banks aggregate on each route. ACK or rerouting one bank
cannot erase the other bank's contribution. Optional64 pending-bit inputs
remain explicit external events; cold start leaves them disconnected. Reset
retains actual provider levels so it does not create an input edge.

Main completion publishes the paired raw word `bit1|bit16`. The observed bank1
mask `0011f1f1` inhibits semantic bit16 while leaving cause bit1 able to assert
IRQ111. Original1046bc6c reads and ACKs the complete raw word;1046efa8 consumes
bit16 and translates it to software event bit1. Publishing bit16 alone cannot
reach that callback under the original live mask.

Successful input DMA/composition supplies its causal frame event through this
same pending/mask/IRQ protocol. Original1046bc6c reads/ACKs the raw word and
invokes the registered callback with its own stack packet;1046efa8 and the
kernel event APIs produce the guest flag. Host code does not modify object+C,
object+10, software active bitmaps, IRQ registration tables or eventflag RAM.

The class remains unmigratable. Original GUI frame submission and physical LCD
identity remain unverified; no initialization or synthetic qtest is presented
as an original menu receipt. New image fixtures and sampler/table request
fixtures are handed to the sole native builder as one batch.
