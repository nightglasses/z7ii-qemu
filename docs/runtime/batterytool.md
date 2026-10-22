# Wire-only battery arithmetic tool

`nikon-battery` is a host tool for the first battery authentication exchange.
It accepts the exact 17 challenge bytes and searches a bounded interval of
the 31-bit first-scalar seed representatives. It can then derive a 29-byte
first-auth candidate after independently recomputing every challenge byte.
The arithmetic commands never open a firmware image, guest memory, a debugger,
socket or historical seed/reply file. The separate
[finite UART peer command](batterypeer.md) uses an explicit reproducible input
and synthetic profile through a normal socket backend.

The implementation is in `tools/nikon/battery.c`, its strict CLI in
`tools/nikon/batterycli.c`, and its native fixtures in
`tests/unit/nikon/battery.c`. The existing CMake source view and QEMU Meson
graph own the executable and unit target; no separate compiler script or
test runner is required. After normal configuration/build, select the fixture
through the existing entry point:

```sh
make check MTESTARGS='nikon_battery_unit'
```

The CLI uses POSIX signal handling; the current host receipt is macOS.
Windows CLI support and Linux execution are unverified. Portable arithmetic
coverage below refers to QEMU's generic carryless implementation.

## Input and output contract

The following examples use the predetermined seed-0 test vector, rather than
a captured guest seed. Replace `work/build/qemu` with the configured `BUILD_DIR`.

```sh
work/build/qemu/tools/nikon/nikon-battery selftest
work/build/qemu/tools/nikon/nikon-battery challenge 0
work/build/qemu/tools/nikon/nikon-battery search c1bac8b20c6ffe82fecd05132fbd7c1003 0 4 1
work/build/qemu/tools/nikon/nikon-battery reply c1bac8b20c6ffe82fecd05132fbd7c1003 0
```

The challenge is exactly 34 hexadecimal digits in wire byte order. Either
letter case is accepted; signs, whitespace, separators, partial hex pairs
and field values above 131 bits are rejected. All integer arguments are
decimal digits without signs, whitespace or radix prefixes. Diagnostic
`challenge` accepts seeds 0 through 4294967295. Search and reply use
representatives 0 through 2147483647. Search requires a nonempty
`[START, START+COUNT)` wholly inside that domain and 1 through 256 threads.
The count is checked by subtraction before any addition or partitioning.

`search` prints one JSON object containing status, selected arithmetic
backend, actual thread count, requested start/count, actual evaluated count,
elapsed host seconds and representative (or null). Search stops after the
first verified hit observed across its partitions; it does not promise the
smallest representative or complete coverage of the requested interval.
The representative is usable for that challenge; uniqueness of the full
32-bit seed is not claimed. The JSON contains no reply. `challenge` prints
one 34-digit line; `reply` prints one 58-digit line only after verification.
`selftest` checks eight predetermined Python-oracle vectors through both the
selected backend and the portable carryless path, and prints a small JSON
result. No command interprets an earlier search JSON file.

| Exit | Meaning | stdout |
|---|---|---|
| 0 | Verified hit, generated challenge/reply, or successful selftest | The command's result |
| 1 | Search exhausted its requested interval without a match | Search JSON with null representative |
| 2 | Invalid command, malformed input or out-of-domain argument | Empty |
| 3 | Verification, allocation, thread creation, arithmetic or output failure | Empty, except output failure may leave a partial result |
| 130 | SIGINT/SIGTERM cancellation | Empty |

Errors and cancellation diagnostics go to stderr. The library checks a
caller-supplied cancellation callback before each search candidate and
again before committing a hit. Reply generation checks it before
verification, before H calculation and before copying the finished packet.
Failed/cancelled reply calls leave the caller's output buffer unchanged.
The library has no persistent search or cancellation state.

## Arithmetic and verification boundary

[authentication.md](authentication.md) records the reconstructed field
polynomial, public numeric G/H/c parameters, first-scalar recurrence and
Montgomery x-coordinate ladder. [seed.md](seed.md) proves the top-bit pairing:
seeds differing by bit 31 have the same first eight scalar halfwords. The
tool embeds only those numeric field parameters and arithmetic formulas;
it contains no firmware arrays or private battery scalar.

There is one search algorithm: enumerate representatives and run the
projective ladder. The fast domain multiplication uses a carryless
Karatsuba product and two polynomial folds. It reuses QEMU's
`crypto/clmul.h` rather than implementing host-specific instructions:
`clmul_64` selects PMULL/PCLMUL when available through QEMU's initialized
host CPU feature detection, and otherwise uses its generic C fallback.
QEMU's host CPU constructor completes before the CLI creates any worker
threads. The tool's Meson file adds no instruction-set flags. Selftest
explicitly calls `clmul_64_gen` as well, so portable arithmetic is exercised
even on an accelerated host.

Search rejects projective Z=0, then compares X with challenge times Z,
avoiding per-candidate inversion. A hit is not published until a full
55-cell initialization/writeback reconstruction and independent bit-serial
field multiplication/shift-reduction normalize the G ladder and match all
17 bytes. The verifier shares the documented ladder recurrence with the
search; the eight fixed expected x-coordinates originate in the separate
Python polynomial oracle retained in the seed research receipt. Thus the
second multiplication implementation checks the optimized arithmetic,
while the predetermined external vectors check the whole reconstruction.
There is no affine table/batch search framework or seed-range heuristic.

`reply` repeats that verification, computes L(H,k) using the bit-serial
arithmetic, chooses nonzero R=1, copies the first eight shared bytes as the
tag, and appends the packet XOR. Bytes 0/1/2 are 2/6/0: only byte 1 and the
XOR are established direct transport predicates; 2/0 are explicit framing
choices. The implicit eighteenth R byte is zero only under the documented
first-auth startup clearing/copy lifecycle. This API deliberately offers no
tail override and no later-auth lifecycle claim.

## Evidence and remaining scope

The native fixture covers the external oracle vectors, top-bit pairing,
strict parsing without partial writes, known and not-found small domains,
the final representative, overflow/out-of-range rejection, cancellation
before work and before result publication, restart after cancellation,
full-17-byte verification failure, all 29 bytes of the previously EX-accepted
wire vector, reply framing and CLI exit/output boundaries. Closed stdout
must return failure for every output command. These are synthetic host
fixtures, not firmware execution. The native `nikon_battery_unit` batch passes
all eight cases; receipts are in `work/runs/timingpreset-20261025/`.

The formal executable searched 4,194,304 predetermined nonmatching candidates
with 12 threads in 3.446039 host seconds, about 1.217 million candidates/s,
using QEMU's accelerated backend. A constant-rate full 31-bit representative
domain would take approximately 1764 seconds (29.4 minutes); that is an
extrapolation, not a deadline guarantee. A separate SIGINT run exits130 with
empty stdout. `benchmarklong.json` and `signal.json` retain the measurements.

The research tool's unchanged original EX acceptance is separately retained
in `work/runs/batteryaccept-20261025/parent-accept.json`: transport returned
0, the field validator returned 1, and EX entered the following 0c query.
That run paused all vCPUs for approximately 1077 host seconds. It proves
timing-assisted first-auth acceptance; it does not establish a real-time
responder, completed battery initialization, physical battery identity,
BatCk success or complete startup. The formal tool's native fixtures and
measurement above do not repeat that original-EX acceptance and do not
benchmark its portable backend.

The 0c identity/class fields, subsequent 0e exchange and later power transactions
remain outside the arithmetic API. The finite peer separately implements the
source-backed 0c/0e envelope with explicit profile inputs. It supplies no guest
pause control or guest deadline/clock manipulation. Its complete finite UART
inventory, explicit26-byte telemetry profile and9600-baud host serial pacing
are documented in [batterypeer.md](batterypeer.md). Original receive, timeout, retry and
rejection paths remain active.
