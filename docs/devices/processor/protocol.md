# Processor bootstrap request boundary

`nikon-processor-control-functional` is a finite MAS-only configuration and
caller-owned bootstrap store, created only by `boot-shim=functional-v1`.
Its control aperture is PA `2c040000`; its separate 16 KiB byte aperture is
PA `04294000`. It is not a processor ISA, a physical IP identification,
a second general RAM provider, or a ready/completion simulator.

Original C1.70 evidence is summarized in [the runtime study](../../runtime/coprocessor.md).
The selected caller writes parameter `0c`, preparation `00/04/08`, copies
`4000` bytes into `04294000`, clears `14`, and modifies control `10`.
The model retains word writes at these six offsets with finite value guards:
`00/04/08` accept0/1, `0c` accepts a byte parameter, `10` accepts bits
0/16/17/24, and `14` accepts an aligned 32-bit address. Only `10` is readable
as owned configuration, not an independently produced busy/ready status.
Other offsets, directions, widths and unknown bits fail diagnostically.

The copy-selector path is the supported storage contract. Byte-aperture
reads/writes preserve the caller's exact bytes in little-endian order.
No host image is inserted. This functional 16 KiB view and retention across
reset do not establish physical capacity, reset contents, execution or an
address-mode processing provider.

A new control bit16 request requires explicit preparation `00=0,04=1,08=0`,
parameter and address writes. A named host-facing `request` output and a
nonzero monotonically changing token identify an owned pending attempt.
`nikon_processor_request()` copies its actual configuration and bootstrap
bytes without consuming it. Pending attempts reject changed configuration
and program writes; identical configuration writes do not publish duplicates.
Writes to00 delimit/cancel ownership; reset and unrealize cancel the request
and invalidate its token. `request-pending` is read-only QOM telemetry.
No execution provider is connected by the machine.

There is no timer, automatic busy clear, status24=4, ready-word write,
command reply, GIC interrupt or completion API. An unconnected request cannot
produce processor execution. The original ARM caller independently waits
for its ready marker and can naturally take its timeout/stop branch.
Migration is explicitly unsupported rather than silently losing a request.

`tests/unit/nikon/processor.c` checks real byte retention, configuration,
request snapshots/tokens, duplicate requests, absence of timed acknowledgement,
pending-write rejection, stop/reset/unrealize cancellation and negative
accesses. The old rejection behavior failed the initial word write before
the implementation passed. These native tests validate this declared
functional contract, not physical control/reset law or GUI acceptance.
