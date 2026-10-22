# Finite raw UART battery peer

`nikon-battery peer SOCKET SEED PROFILE_HEX` connects a finite host responder
to a normal Unix stream chardev. It precomputes the challenge and independently
verified first-auth reply before opening the socket. Its only inputs are the
explicit 32-bit seed, an explicit synthetic profile and subsequently received
wire bytes. It uses no guest memory, debugger, vCPU pause or software result
override.

This route is intended for a reproducible external-input experiment using
`nikon-ex-rng-functional.input-word`. `SEED` must match the first scalar's
actual reseed input; merely configuring that RNG input does not override the
wire comparison. The peer accepts authentication only when all 17 received
challenge bytes equal the precomputed challenge. See [seed.md](seed.md) for
the seed equivalence and first-auth arithmetic, and
[batterytool.md](batterytool.md) for host reconstruction and acceptance limits.

## Explicit profile and framing

`SEED` is strictly decimal 0 through 4294967295. `PROFILE_HEX` is exactly52
hex digits, with no prefix or separators. It supplies26 bytes in order:

| Bytes | Input |
|---|---|
| 0..7 | Explicit eight-byte synthetic identity |
| 8..9 | Class, little-endian 16-bit; only 1 or 3 is implemented |
| 10..11 | Opaque little-endian 16-bit value, with no claimed physical meaning |
| 12 | Charge input,0..100 |
| 13 | Status flags; bits5/6 feed the original diagnostic4 branch |
| 14..15 | LE16 raw status value copied to EX structure+24 |
| 16..17 | LE16 raw status value zero-extended to EX structure+20 |
| 18 | Signed correction input; this finite profile admits only0 |
| 19..20 | LE16 diagnostic-ratio numerator, nonzero |
| 21..22 | LE16 diagnostic-ratio denominator, at least the numerator |
| 23..24 | LE16 raw metrics value copied to EX structure+38 |
| 25 | Raw metrics byte; original thresholds170/180/190/200 select level0..4 |

For example, `01020304050607080100000064002823000000e803e803000000`
selects identity0102030405060708, class1, opaque0, charge100, flags0, raw status9000/0,
correction0, ratio1000/1000, raw metrics0 and level-input0. The raw9000 is
also an original fallback constant; no physical units are claimed. These
are declared fixture inputs, not recovered factory data or measured values.
The old identity-only24-digit format is removed. Nonzero correction is
rejected: the original signed table adjustment can otherwise produce a
zero divisor for unverified configuration/temperature combinations.
The original class1 identity branch can still reject a particular identity;
the peer does not suppress or override it. All arguments are validated before
connecting. The socket carries raw bytes; the peer does not implement QEMU
mux escapes or strip unrelated device output. Use a raw EX USART endpoint.

```sh
work/build/qemu/tools/nikon/nikon-battery peer work/battery.sock 0 01020304050607080100000064002823000000e803e803000000
```

Replace `work/build/qemu` with the configured `BUILD_DIR`. A matching
reproducible RNG/transport setup belongs to the machine launch configuration;
this command does not modify the emulator or launch a guest.

| Complete received request | Reply |
|---|---|
| `02 01 11`, 17 exact precomputed challenge bytes, XOR | Verified 29-byte first-auth packet |
| `02 0c 00 0e` after an authentication reply | `02 06 0c`, profile's twelve bytes, XOR (16 bytes) |
| `02 0e 01 01 0c` after a class1 profile reply | `02 06 01 00 05` (5 bytes) |
| `02 02 02`, control0..2, count0..255, XOR | `02 06 00 04` (4 bytes) |
| `02 0b 00 09` | Status envelope (14 bytes), using declared profile fields |
| Exact preceding14-byte status envelope echoed as a request | ACK envelope (12 bytes), one use |
| `02 0a 00 08` | Metrics envelope (35 bytes), using declared profile fields |
| `02 07 00 05`, `02 25 00 27`, `02 26 00 24` | `02 06 00 04` |
| `02 89 0e`, bounded14-byte configuration, XOR | `02 06 00 04` |

The arithmetic reply uses the documented first-auth retained-tail=0 contract.
Other envelopes use header02/06, payload-length byte and final XOR. Unconsumed
payload bytes are declared zero. For0b the consumed fields are reply3,5,6..7,
10..11 and12; for0a they are12..13,14..15,18..19 and32. ACK payloads have no
original decoder here. Configuration89 admits two LE16 values at payload0/2
up to999, byte4 in1..8, eight configuration bytes and byte13 in0..6. No
relationship to physical charging or aging is invented. The complete original
transport/caller inventory is in [batteryprotocol.md](batteryprotocol.md).

## Finite sequence and failure behavior

The protocol state is separate from socket I/O in `tools/nikon/batterypeer.c`.
Original EX08024964 sends a separate single00 operation (TX1/RX0/timeout0),
then delays10 ticks. Initializer08023e98 calls it once before the three auth
attempts; the 0c/0e helpers do not call it. The finite peer consumes each such
attention operation without a reply at any healthy frame boundary, including
repeated operations and initializer re-entry while waiting for0c. Its seven
original upper callsites have no shared one-use restriction. Phase is unchanged;
zeros inside a frame remain ordinary payload/length/checksum bytes.
The physical purpose of this separate operation is not established.
Its buffer is bounded to the maximum 21-byte request. Every split of a frame
is valid fragmentation, but no reply is available until a complete request
passes the exact header, XOR, payload and state checks. There is no mixed
stream resynchronization or permissive unknown-command handling.

An exact authentication retry or restart is accepted in any healthy phase
and starts the profile phase again. Only the configured challenge is accepted;
a fresh different challenge fails explicitly. Exact0c retries are accepted
after authentication. Both classes become operational after0c; class1's0e
is optional and repeatable, while class3 rejects it. The original identity
branch determines whether the guest itself calls0e. Ordinary operations and
retries remain available without forcing a guest initialization result.

Only a complete0b query grants one optional echo of that exact status reply.
Any other complete command, attention operation, restart or consumed echo
clears this grant. The peer does not inspect guest structure+60 to decide
whether the original firmware should use the14/12 transaction. Frame length,
XOR, configuration bounds and the echoed content remain checked before reply.
The stable virtual profile does not discharge, charge or silently change its
declared telemetry when an ACK-only control command arrives. Unknown commands
remain explicit errors; this inventory covers the14 original direct UART
transport calls, rather than an arbitrary permissive command range.

An invalid header, unknown command, bad XOR, challenge mismatch, wrong order
or unsupported payload terminates the session without a reply to that
request. An EOF within a partial frame fails; an EOF between frames exits
normally with the last protocol phase. A normal EOF does not imply firmware
acceptance, completed initialization, BatCk success or complete startup.

Received stream chunks, complete requests, sent replies and rejected partial
requests are logged in hex to stderr. Chunk logs retain bytes already received
after an unsupported command without interpreting a new command length.
Stdout contains only an EOF diagnostic JSON object,
never raw protocol data. Invalid CLI input exits 2; connection, I/O or
protocol failure exits 3; SIGINT/SIGTERM cancellation exits 130. Failure and
cancellation produce no stdout result. The loop uses QEMU's `qemu_socket` and nonblocking socket setup, then a
cancellable POSIX connect with bounded polling, checked short-write loops and
EINTR handling. Linux queue-full EAGAIN retries are distinct from a pending
EINPROGRESS connection and never infer success from an unconnected SO_ERROR0. SIGPIPE is suppressed per socket/send with `SO_NOSIGPIPE` or
`MSG_NOSIGNAL`, without changing the process-wide disposition. A platform
with neither facility fails explicitly before sending.

## Evidence and validation

The source-backed decoder/initializer boundary is retained in
`work/runs/batteryaccept-20261025/child-profile.md`, derived from original EX:

- EX VA `0802487c..080248da` consumes the 0c envelope and stores the class
  and opaque halfwords. Their parser accepts 16-bit storage; that does not
  establish physical meaning or a legitimate battery identity.
- `08023eea..08023f38` selects class1/class3 branches and caller-dependent
  channels. Class3 bypasses the state2-only identity/0e block.
- The initial class1 identity-mismatch path reaches `080245c4(structure,1)`;
  the request is the exact five-byte 0e request above. After generic transport
  success, `08023faa` stores constant 1 in the structure's first byte.

The native `nikon_batterypeer_unit` fixture covers profile parsing, attention,
maximum
frame fragmentation, complete request/reply bytes, all 17 challenge mismatch
positions with valid XOR, malformed headers/XOR, ordering, retries/restarts,
class1/class3, exact command02 and its state/payload/length/XOR rejections,
unsupported later commands, partial EOF, explicit reset,
cancellation before connection and invalid CLI arguments. Select it through
the existing workflow:

```sh
make check MTESTARGS='nikon_batterypeer_unit'
```

Synthetic fixture results and an original EX run are separate evidence.
The earlier accepted first-auth run paused vCPUs for about 1077 host seconds;
this new precomputed route does not inherit a timing or startup claim from
that receipt. A fresh unchanged-firmware run must independently record the
natural transport/validator returns, subsequent class/identity branches and
any later unsupported transaction. This finite profile provides no full
operational battery or whole-camera completion claim.

## Reproducible unchanged-EX acceptance

`work/runs/reproducible-20261026/` retains fresh source/build/native and original
receipts. RNG input0 produces challenge`c1bac8b20c6ffe82fecd05132fbd7c1003`.
The raw EX USART link is configured as documented in
[rng.md](../inputs/ex/rng.md); the peer precomputes before connecting.
The explicit profile is identity`0102030405060708`, class1 and opaque0.

`original.json` stops at08023fae, after the original store at08023faa.
Structure2001fa50 has byte0=1, authentication word+4=1, class+10=1,
opaque+8=0 and the declared identity at+12. The 0e helper returned0. Wire
logs retain the actual29/16/5-byte replies. No debugger stop occurs between
the first UART request and this completed initialization block. Original EX
issues two auth requests in this run; normal retry succeeds, rather than a
claim of first-attempt acceptance or recovered physical turnaround timing.
Why the first attempt did not complete is not established by this receipt.

The initial strict peer rejected the leading00. A read-only access watch and
local original disassembly identify its actual source: store0804a0da,
TX handler080236a0, CR1=3488hex; upper helper08024964 supplies the single
zero. `attention-red.json` records the parser failure before adding that
finite operation; all nine peer native cases then pass. Earlier attempts
which assumed the first byte was a framed02 are not acceptance receipts.

That stage's `nextquery.json` independently reaches the next transport call at
08023824, caller LR0802434d: request`02 02 02 00 00 02`, TX6/RX4,
timeout100 kernel ticks. The newer finite extension and original acceptance
are retained in `work/runs/batterycontrol-20261026/`: all eleven peer native
cases pass after the recorded old-parser failure. Original helper08024308
returns0 at08024372 with received bytes02060004 and pending count0. One
earlier helper return4 is retained; the original retry path then succeeds.
No guest result, counter or callback was supplied from the host.

The subsequent original request is`02 0b 00 09`, TX4/RX14/timeout100 ticks,
caller LR0802427f. That command decodes state data and remains unsupported;
the peer explicitly stops there. Neither receipt establishes BatCk success
or complete battery operation. See [batterycontrol.md](batterycontrol.md).

The preceding native batches passed five RNG, two routing, eight arithmetic/CLI
and nine peer cases; the command02 extension raises the peer batch to eleven. `socket-smoke.json` additionally checks actual fragmented Unix I/O,
the complete supported sequence, unknown command, partial EOF and SIGINT.
The macOS backlog test connected before cancellation, so it does not prove
Linux's queue-full EAGAIN path. That path is bounded and source-reviewed;
Linux execution and physical serial timing remain unverified.

## Complete finite protocol and continuous UART acceptance

`work/runs/batterycomplete-20261027/` retains the expanded profile, complete
command fixtures and fresh unchanged-firmware execution. The earlier0b stop
above is historical; every shape in the14-call original inventory is now
implemented, including both02/89 builders, optional0b echo and25/26 controls.
These ACK-only operations preserve the stable declared virtual telemetry;
they do not claim a recovered electrochemical or physical charging model.

Sending each entire reply immediately over the socket was insufficient for
continuous operation. The original full-query routine first returned0 at
`08023df0`, but later exhausted retries with error4 at `08024ac0` while
upstream USART traced383 receives dropped while RX was disabled. These
receipts remain `complete*` and `error*`; protocol recognition alone was not
the accepted continuous result.

EX `08023878..08023888` programs9600 baud,9-bit word length including even
parity, and one stop bit: eight data bits use11 wire bits per character.
The peer now waits at least `ceil(11*1000000/9600)` host microseconds before
each reply byte. It restarts this deadline after every successful byte, so
host stalls never cause a catch-up burst. Cancellation, short writes,
EINTR/EAGAIN and socket errors retain their checked paths. This is host
monotonic-clock serial pacing; do not pause the VM during a transaction or
infer deterministic guest timing or measured physical turnaround from it.

With the example profile, `paced.json` reaches the full-query return0 and
`paced-wire.json` records72 complete requests in the following20-host-second
window: auth1,0c1,0e1,02=32,0b=33,0a=2,89=2, with the peer alive and no
rejected command. The final read-only `paced-snapshot.json` has original EX
state2, corrected charge100, substate0, diagnostic0 and ratio100. The actual
EX report reaches MAS: BatCk at `b18651d8` is1. GUI gate and pending3 remain0;
this is battery-report acceptance, not GUI first-frame or complete startup.

Native coverage and original coverage remain separate: the native command
sequence exercises07,25,26 and the optional echo even when the selected
original baseline does not request those branches. Class3 framing is tested
natively; class1 is the accepted original continuous profile here.

The separate explicit flag10(hex) profile run `flag*` requests07 through
the original optional branch, returns0 from the full query and retains
state2/substate0/diagnostic0/BatCk1 after5hostseconds. It logs27 requests,
no rejected command and no USART-drop trace. `joined-snapshot.json` also
retains the actual MAS raw report and both classification flags: unknown-type0
and override0. See [batteryreport.md](batteryreport.md) for the verified
raw-to-normalized fields; a different EX diagnostic buffer is not substituted
for that actual MAS input.

The native batch now has18 passing peer cases, including one real Unix-socket
CLI exchange covering the complete command sequence and clean EOF. Its paced
29-byte auth reply takes about40.6ms, above the30ms minimum fixture bound.
Eight arithmetic/CLI cases also pass. The recorded status-query negative
control fails the complete-operation fixture when0b support is absent.
One initial socket fixture build lacked its poll header; the retained failure
is not counted as a successful test. Real cancellation after the first paced
reply byte exits130 with no stdout result (`pacing-cancel.json`).
