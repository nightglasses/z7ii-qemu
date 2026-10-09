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
Consumed output prefixes are reclaimed before applying the queue bound.
If a connected wire cannot queue an actual SELECT or GPIO transition, it
reports a transport failure and closes. It cannot silently drop that edge
or reconstruct it from a later current-level replay.

The same transport binds the asynchronous I2C master through
`hw/nikon/peers/i2c.c`, using `nikon-i2c-peer` with strong `peer` and `i2c`
links. One operation may be outstanding: START, WRITE, READ, STOP or NACK.
This explicit asynchronous controller seam is necessary because the fixed
upstream `include/hw/i2c/i2c.h` supports asynchronous sends but its receive
callback returns a byte immediately. Waiting for another CPU's ISR cannot
use that synchronous callback or block the host main loop.

For I2C, envelope kind is 4, payload width is 8, and `line` contains phase
1 through 5 respectively. START carries the unshifted 7-bit address and
receive direction in bit 7; WRITE carries the actual byte. READ, STOP and
NACK carry zero. The final READ is followed by an explicit NACK operation,
then STOP when the guest releases HOLD. A reply echoes kind, phase, width
and token. READ returns the actual byte or `0x100` for NAK; other replies
contain zero for a completed bus action or `0x100` for NAK. A completed
STOP is not a slave acknowledgement or an application acknowledgement.
Schema validation rejects reserved fields, illegal phases and malformed
results. Mismatched or stale phase/token replies cannot complete an operation.
Disconnect does not synthesize a successful START, byte or STOP.

The source-qualified normal touch host uses MAS I2C0 at PA `2C018000`
with IRQ 214. Constructor VA `103DB654` obtains logical I2C0;
`103DBCEC` writes one command byte to unshifted address `55`, then reads
the requested length. Command `10` reads two bytes and command `11` reads
seven bytes. TPJ's initialized key table maps these to original handlers
`DFE` and `23D0`, which write output at `60040` from actual guest state.
The TPJ endpoint must run these original consumers and observe their output;
the transport does not construct touch records from the command number.
MAS resource 63 is GPIO6[6] with an actual event callback; resource AA is
GPIO10[13], which the original worker drives high before a delay. Physical
board wiring and the meaning of that control pin remain distinct evidence.

An I2C frontend can be attached with
`-device nikon-i2c-peer,peer=/machine/peripheral/wire0,i2c=/machine/i2c0`.
The machine owner provides the source-bound controller path. Native socket
fixtures exercise the controller and this wrapper; those tests do not prove
that the original TPJ has completed its boot or produced a touch event.

The touch profile also has two named wire roles: `NIKON_TOUCH_NOTIFY` (1)
and `NIKON_TOUCH_CONTROL` (2), carried as GPIO signals. TPJ's original
report-pending path `04DC` calls `1848` to clear `71010` bit 2; its
consumption path `21D2` calls `18A4` to set that bit. Startup sets it high.
These actual guest output writes provide the notify level. Pairing this
producer with MAS GPIO6[6] is an explicit functional board assumption,
supported by the complementary software paths rather than a board netlist.

The frontend exposes `notify[0]` output and `control[0]` input. Optional
`gpio`, `control` and `notify` properties bind the actual source pins;
`gpio=/machine/gpio,control=173,notify=102` selects MAS GPIO10[13] and
GPIO6[6]. The control signal is published only while the original guest
actually drives its GPIO, using the same direction/function query as the
SPI select frontend. Incoming notify signals drive the input without host
timer pulses or command-dependent levels. The control pin's hardware
meaning is not inferred from its name. A disconnected peer does not change
the notify level into an invented successful event.

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
