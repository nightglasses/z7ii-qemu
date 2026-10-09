# Peer wires

`hw/nikon/peers/transport.c` provides `nikon-peer`, a target-independent
native QEMU character frontend. `hw/nikon/peers/spi.c` binds the ARM SPI
controller through `nikon-spi-peer`. Both object links are required. A peer
without a character backend is a valid disconnected endpoint.

The wire protocol is a 40-byte host envelope defined in
`include/hw/nikon/wire.h`. It is not a Nikon guest packet. Its little-endian
fields carry an unchanged clock word, select level, or GPIO level. The
timestamp records sender virtual time; it does not advance the other CPU.
Transport uses native nonblocking character writes and a bounded output
queue. One clock request may be outstanding per peer.

An accepted clock completes only after an actual reply with matching token
and width. Fragmented input is accumulated before decoding. Wrong or late
reply tokens cannot complete a new transfer. Disconnect cancels the remote
endpoint and discards transport buffers; it does not manufacture an RX word
or completion interrupt. Reconnection permits retry of a previously rejected
submission, and does not replay an accepted clock. Reset or disable cancels
through the SPI owner's token lifecycle. If a cancel cannot be queued, the
wire closes rather than leaving a stale remote transaction live.

The endpoint callback may hold an incoming clock until the original guest
ISR and DMA have armed the peripheral. It then clocks the actual device and
calls `nikon_peer_reply` with the resulting word. SELECT and GPIO callbacks
are physical input levels and are never firmware acknowledgements. The
endpoint operations are copied, but callback opaque state must outlive the
peer. A frontend retains its peer and controller through QOM object links;
unrealize first unregisters its callback and detaches/cancels the controller.
One frontend may register the state callback on each peer.
Reset can discard an incoming clock with `nikon_peer_endpoint_cancel`.
Removing an endpoint cancels its incoming request before clearing the callback.
Neither operation replies or completes the remote master: its guest must
cancel, reset or handle the absent response through its own firmware.

Example native frontend attachment:

```
-chardev socket,id=p0,path=/tmp/nikon.sock
-device nikon-peer,id=wire0,chardev=p0
-device nikon-spi-peer,peer=/machine/peripheral/wire0,spi=/machine/spi0
```

The frontend also exposes the named `select[0]` input. The optional strong
`gpio` link and `pin` property bind a source GPIO inside the frontend, for
example `gpio=/machine/gpio,pin=177` for the source-proved VR select output.
Both properties must be supplied together. Only an actual guest output with
GPIO direction and function configured supplies a select level. Floating
reset-low is suppressed rather than being treated as a selection. On socket
connection the frontend republishes the last genuinely driven current level,
not a made-up pulse. Direct machine use of `select[0]` has the same requirement
to connect an actual producer.

Machine topology is a separate source-derived contract. MAS channel 0 is
the EX transmit path into EX SPI1 RX; MAS channel 1 is the EX receive path
from EX SPI4 TX. The EX checksum, DMA completion and task events remain
original guest code. Connecting a transport alone does not establish boot,
board GPIO pairing, firmware readiness, or the other processor's execution.
The native real-socket fixture belongs to the shared SPI device owner and
exercises this same frontend rather than a private synthetic QOM command.
