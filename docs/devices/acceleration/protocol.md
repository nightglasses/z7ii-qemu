# MAS acceleration input

This records the original MAS direction provider used by the ordinary
application completion aggregate. A finite upstream I2C peer now implements
the qualified variant1 sequence; no physical-device identity is claimed.
The application/runtime join is in [guiupdate.md](../../runtime/guiupdate.md).

The source is unchanged Z7II C1.70 MAS, mapped at `10020000`. Bounded source
receipts are `run guinormal/child-sensor.json` and
`child-bus.json`. Original execution, external record candidates and proposed
device behavior are distinguished below. No firmware or guest state was edited.

## Explicit input and implemented scope

`nikon-acceleration-functional` is attached to I2C2 at seven-bit address1d in
the complete-bundle composition. Raw MAS diagnostics leave it unattached.
By default`sample-present=false`: activation does not manufacture data-ready,
and sample reads NACK. A functional stationary input is supplied explicitly:

```sh
-global nikon-acceleration-functional.sample-present=on \
-global nikon-acceleration-functional.sample-x=0 \
-global nikon-acceleration-functional.sample-y=0 \
-global nikon-acceleration-functional.sample-z=1024
```

The signed raw axes must each fit12 bits. These numbers do not claim a physical
orientation or g calibration. With present input, accepted activation makes the
stable sample available immediately; conversion latency and continuously renewed
availability are declared functional assumptions. Status00 is08 only while
active with present input. Sample01 returns exactly six bytes in the source's
big-endian representation. Reading beyond the qualified extent reports an
uncovered access; the upstream I2C recv callback cannot NACK an individual byte.

Only the exact register/burst shapes below and the qualified programming bytes
are accepted. Control2a supports00/01/2c/2d; other data and partial/oversized
writes cannot install activation. Configuration bursts are accepted in standby.
There is no general register file or address68 variant. Writes are staged until
STOP; a new send START, invalid byte or reset discards staged data. Selector-only
STOP is retained for the original separate TX/RX calls; combined repeated START
also works. Reset clears activation and transaction state while retaining declared
external axes. The containing controller remains responsible for cancellation,
clocked bytes, IRQ and timeout; this peer never sets a guest-ready event.

Native unit coverage includes the complete sequence/configuration, signed12
extrema, missing input, invalid selectors/values/addresses, short/extra writes,
restart/reset cancellation, partial reads and all axis range boundaries. Native
qtest covers actual timed controller IRQs and HOLD TX completion before RX.
The unchanged original continuation is independently joined in
[guiupdate](../../runtime/guiupdate.md), including the next graphics stop.

## Selection and prerequisites

Initializer `103d4e42` obtains hardware unit6 through `10368b28(6)` and stores
the handle in word `b1807f34`. The handle is

```text
0x0040 | ((LE16[WORD(b180000c) + 12] - 256) << 16)
```

The high halfword selects the bus; unit6 is not itself the bus index.
`1038871c` takes that high halfword and calls `10370f78`. The current observed
variant byte `b1807efc=1` selects target address `0x1d`; the other original
variant uses address `0x68` and a different sample/register contract, outside
this proposed scope.

The initializer's variant selector is a GPIO read through service5,
`10368ad2(5) ->103a047c ->10385d76`. Its observed object at MAS VA`b18025b4`
describes P0.5; input mode, enabled pull-up and PDR PA`04282000` produce1.
Neither the ACCELE type word nor configuration resource21 is that selector.
The [captured calibration importer](../../inputs/nor/descriptor.md#captured-accele-import-and-explicit-units)
therefore makes unit retargeting explicit rather than changing the type or pin
to force a preferred branch. Its converted values are a declared virtual input;
the original raw12 peer and its status/activation protocol remain unchanged.

Configuration requires both original resource pointers at owner+424 and+428,
where owner is word `b1807f00`. Resource21 supplies device configuration and
resource57 supplies calibration. The original callback waits for both ordinary
resource-completion bits before installing either pointer. Resource57 belongs
to external descriptor18. Absence of its completion is ahead of device access;
it must not be repaired by a device response, guest pointer or readiness event.

## Primitive transaction shape

The20-byte request passed to `1038871c(handle, request)` has these fields:

| Offset | Width | Meaning on these callers |
|---|---|---|
| 0 | byte | 1 |
| 2 | halfword | Target address: `001d` for variant1 |
| 4 | word | TX pointer |
| 8 | word | RX pointer, or0 for write |
| 12 | word | TX byte length |
| 16 | word | RX byte length, or0 for write |

`103d4848(reg, bytes, length)` writes `[reg, bytes...]`, with TX length
`length+1` and no RX. `103d4752` writes the single selector byte01, then reads
six bytes. `103d57e6` writes selector00, then reads one status byte. The decimal29
in these request structures is the target address, not the status register.

Transfer result0 means success. Each helper locks through `103886a8`, retries
`1038871c` at most three times, and unlocks through `103886e6`. A lock,
transfer or unlock failure returns failure and prevents publication of the
received sample. `10370f78` itself can retry its TX/RX submission sequence
three times. Its lower `10370ee8` waits for the existing controller completion
event, checks the completion result and calls `1036d4a8` on timeout. Those
controller/IRQ semantics remain the controller's responsibility; an acceleration
peer must not directly set the guest direction or completion aggregate.

## Original variant1 sequence

`103d5278` selects `103d51cc` for variant1. Its initial sequence is:

1. Write register2a with byte01.
2. Sleep4 ticks, read status register00, and repeat while status bit08 is0.
3. Read six sample bytes starting at register01.
4. After successful sample processing, write register2a with byte00.

A failed status/sample/write helper exits through the original failure route.
The status polling loop has no additional finite poll count in this wrapper;
each individual transaction still has the lower completion timeout/retry path.

The ordinary read task starts at `103d4a6c` and programs the following bursts
through `103d48d0`. Values are drawn from resource21 bytes at four-byte strides;
the right column is the explicitly qualified Z7 ROM default candidate, not
recovered Z7II factory programming.

| Register | Data length | Resource21 byte offsets | Candidate bytes |
|---|---:|---|---|
| `0e` | 2 | 76,80 | `00 00` |
| `11` | 1 | 84 | `00` |
| `15` | 1 | 88 | `00` |
| `17` | 2 | 92,96 | `00 00` |
| `1d` | 4 | 100,104,108,112 | `00 00 00 00` |
| `23` | 15 | 116 through172, step4 | Seven00, `2c 02`, six00 |
| `2a` | 1 | 144 OR1 | `2d` |

The byte at resource+144 is masked with `fe` in the register23 burst, then
written with bit0 set in the final register2a write. “OR1” means numeric bit0;
no physical control-bit name is inferred. It waits `max(81-config.word32,0)`
ticks after configuration. With the candidate word32=50 that is31 ticks.
The loop then sleeps word32 ticks before each selector01/six-byte read.
These tick counts do not establish physical milliseconds or sampling frequency.

Task continuation at `103d4d08` compares word `b1807f04` to1 and branches
back while it differs. A value1, failed setup or failed sample read reaches
`103d4d14`, clears capability word `b1807f08` and sleeps1000 ticks repeatedly.
This cancellation/failure path must remain effective; it is not an invitation
to maintain capability or synthesize a completion after cancellation.

## Sample representation and publication

Variant1 interprets each pair as big-endian signed16, then divides its magnitude
by16 and restores the sign. For values whose low four bits are0 this is exactly
a signed12-bit sample, range -2048 through2047. For other negative values the
original code truncates toward0, so an arithmetic right shift would differ.
The finite input should encode each signed12 sample as `(sample & 0xfff)<<4`,
big endian, in X/Y/Z order. A physically calibrated unit such as g is not
established by this source.

First-read helper `103d4e7a` computes the sample/angle state, copies its public
cache, and writes capability word1 at `103d4f9a`. A change of the direction
getter invokes `103d569a` and registered event `42051201`; periodic callbacks
can also supply `42051200`. `103eeaec` admits the first such event even when
its derived byte is0. Only the unchanged original consumer then calls
`10350d9e`, contributing aggregate bit2. No direct ready event is needed or
permitted at the device interface.

## Physical boundaries and open assumptions

The implemented peer exposes an explicitly configured stable three-axis raw
sample through the existing I2C controller. Mapping register2a bit0 to
activation and returning status08 for a fresh available sample is a functional
model hypothesis inferred from the polling sequence; physical reset defaults,
conversion latency, overrun behavior and register side effects remain unknown.
Do not broaden this to a256-byte read/write register file or infer unknown
registers, addresses, burst lengths or control modes. The address68 variant
requires separate evidence.

Reset must reset the peer's programmed/active state and any partially accepted
selector/write transaction, retaining only its declared external sample input.
Cancellation or a STOP/error must discard an incomplete transaction and must
not publish a sample or late completion. Those are required model boundaries,
not claims about an uninspected physical reset circuit.

A fixture PASS alone does not prove original startup or a first display frame.
