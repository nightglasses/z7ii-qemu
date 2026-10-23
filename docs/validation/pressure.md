# Native peer queue pressure

The independent `tests/qtest/nikon/pressure.c` fixture uses an ordinary Unix
CharBackend and the source-bound GPIO10[13] CONTROL output. It receives the
initial driven HIGH and an actual accepted I2C START, then stops reading.
Actual LOW/HIGH PDR transitions fill the operating-system socket buffers and
the transport's production bounded queue. No test-only queue capacity,
readiness property or injected peer failure exists.

The fixture observes the official SocketChardev `connected` property becoming
false, drains the old socket to real EOF and verifies the complete frames form
an ordered LOW/HIGH prefix. A close may truncate the final frame; the unsent
tail is never turned into a completion or carried into another connection.
The accepted I2C request stays active without COMP/NAK while disconnected.

Before native reconnect the guest makes the physical source pad an input and
alternate function. Reconnect must carry neither stale HIGH nor an automatic
retry of the accepted START. A late old reply cannot finish that request.
Only guest FIFO clear cancels it; a subsequent real START/WRITE/STOP sequence
must complete through fresh matching replies. The host replies are synthetic
fixture bytes, not original TPJ application acknowledgements.

The workload is bounded by 16,384 GPIO transition pairs and five seconds, with
a normal 250-ms socket reconnect interval. These are fixture liveness limits,
not measurements of Nikon timing or assumptions about its physical queue.
This case requires the production transport's explicit connected-queue-full
disconnect contract. It is run alone by the sole native builder; previous I2C
transaction, strobe, binding and SPI cases are not repeated for this fixture.
