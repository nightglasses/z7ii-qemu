# Virtual lens and scene input

The virtual camera needs both a lens control endpoint and an image source.
The lens exchanges identity, optical parameters, configuration, calibration,
operation status and motor history with the original firmware. Scene pixels
enter the CMOS/readout path. A lens notification is not a rendered frame or GUI
readiness, and replacing a warning cannot establish either path.

`hw/nikon/devices/lens.c` implements an explicit, finite normal-family virtual
50 mm F2.8 lens with fixed focal length and iris. Its identity `7ffe` is a
virtual input, not a recovered
physical product identifier. This profile does not advertise optional lens
families or implement autofocus movement, variable zoom, physical optics or a
3D application bridge. Unsupported packets fail at the endpoint boundary.

## Source and board connections

All firmware addresses below are **MAS C1.70 VA** unless marked PA.

| Source fact | Interpretation and implementation boundary |
|---|---|
| `10384440 → 10384274 → 102f0de8` | Shared-message descriptor13 carries `03050011`, then LensService publishes `03050001`. Payload bytes2/7 update the original lens command state. This is separate from AppControl `05020101`. |
| Adapter `10206f60..10207080` | The provider at `b18a7c80` resolves to the board adapter at `b18a7d00`; its request endpoint uses the existing message serializer. No guest cache or callback is replaced. |
| `10382f02 → 10368b0a(3)` | The observed serial map selects CSIO3, whose low object identifies **PA `2c0a0300`**. The command endpoint attaches to that controller's upstream SSI bus. |
| GPIO resource93 | The observed object selects port6 bit0, physical input index96. `1038305a` consumes its falling-edge phases. The device publishes a phase edge after processing the actual header or complete payload. |
| `10383388 → 10243468` | The separate logical2/CSIO2 stream uses a null TX pointer and a real 21000-halfword receive ring. External words enter the actual slave FIFO and existing DMA path. |

Example explicit attachment to the diagnostic board:

```sh
-machine z7ii-mas-diagnostic,virtual-lens=on
```

An absent endpoint retains the existing disconnected SPI behavior. This input
does not assign an unknown EX GPIO as lens detection or change shutter GPIO169.
The normal LensService notification must still come from its original producer.

## Complete ordinary initialization group

`1038301c` receives a 72-byte **software descriptor**, not a wire packet.
`10382a64` produces an 8-byte header: marker55, sequence, request mode, command,
subcommand, LE16 `(payload_bytes-2)/2`, and a negative-byte-sum checksum.
Payloads likewise contain marker55 and a final checksum; their full byte sum
must be zero modulo256. The native SSI endpoint exchanges 16-bit words.

| Wire command | Complete TX/RX length | Finite virtual processing |
|---|---:|---|
| `1f:00` |4| Conditional pre-identification probe with header modeff; validate probe1/2 and declare the ordinary family |
| `10:00` |28| Virtual identity and bounded ordinary-family capabilities |
| `11:00` |74| Fixed parameter page with optional features unadvertised |
| `12:00` |66| Coherent50 mm/F2.8 focal endpoints and ordinary CPU-lens code |
| `13:00` |14| Validate/store the body configuration and commit it after modeled processing latency |
| `24:00` |7426| Full main calibration and neutral lens-specific correction sections |
| `24:10` |3468| Full secondary coefficient page |
| `15:00` |6| Status derived from the actual pending/configured state |
| `21:00` |6| Store the actual transmitted synchronization anchor |
| `20:00` |258| Current operation and optical status page |
| `20:01` |24| Ordinary secondary state page |
| `27:00` |6| Virtual query of the stored anchor; physical meaning remains unresolved |
| `16:00` |4| Validate/apply stream enable or disable |
| `a0:00..07` |24| Validate the complete zero-travel aperture request and zero tail |
| `a1:00` |10| Validate zero aperture travel and zero additional parameters |
| `a3:00..01` |4| Validate zero-travel aperture requests |

`10311c84` selects the pre-identification path through `10311442` when its
connection provider has not already admitted the lens. `1037bbfc` builds
`1f:00` with modeff and probe byte 1/2. Reply byte 1 bit 0 selects the ordinary
or alternate family; the virtual endpoint declares ordinary 0, with reply
byte 2 bit 0 set for its supported negotiation endpoint. `1034da8a` updates
the original strategy object and `1037c396` selects the subsequent header
mode. This is a device capability choice, not an unconditional command ACK.
The transmitted probe's byte2 must be zero in this finite profile. That is an
explicit input boundary, not a proven silicon-reserved field.

The model supports this group together rather than admitting one observed
address or individual reply at a time. It does not acknowledge nonzero aperture
travel, alternate-family commands or advertised capabilities without
their processing models.

`10311d02` and its completion handlers establish this initialization order.
The original status consumers remain responsible for progression. In particular,
`15:00` byte1 reports pending operation work, byte2 distinguishes unconfigured
state, and `20:00` bytes169/203 reflect the same configuration and pending work.
Configuration receipt does not immediately publish completed status.

`13:00` preserves the 12 configuration bytes produced by the original packed
bitfields and two 32-bit parameters. This is a virtual configuration commit,
not a claim of mechanical movement. The explicit processing latency is 2 ms.
Header/payload processing produces a notify transition after 1 ms.
These latencies are functional input choices, not measured lens timing.
The same latency applies to every supported packet. The earlier 10-microsecond
profile intermittently retained four header RX words at the GPIO-driven
cancellation. The source-joined explanation is that the phase interrupt can
overtake the original RX handler in MTTCG; this ordering is an inference. The 1 ms phase
models finite controller processing below the source's 5 ms header wait; it
never inspects guest event state or waits for a requested callback. A native
deadline fixture checks both the pre-deadline high level and the subsequent
processing edge. This does not establish physical CPU or lens performance.

The original pre-transfer hook `103832dc` rewrites the `21:00` timestamp and
checksum before transmission. The endpoint stores those actual MOSI bytes;
it does not interpret the constructor's `ffffffff` placeholder as a movement.

## Optical fields and calibration

`1034afbe` joins the static lens fields to the EXIF lens specification.
The integer/fraction pairs at `12:00` offsets 7/5 and11/9 use a fraction
denominator 65535. Both integers are 50 and fractions zero for this fixed lens.
Offsets1/3 describe aperture at the two focal endpoints. Their APEX encoding
is `(LE16-384)/192`; both are 960 for Av3/F2.8. The second value is **not** a
closed-iris limit. The dynamic biased APEX fields at `20:00` offsets
1/9/15/162/164 and `20:01` offsets 1/3 also report 960. The relative fields at
`20:00` offsets197/199/201/204 report zero. Their exact consumers establish
these encodings; not every coordinate's physical name has been recovered.

`10395052 → 1034ca9e` selects policies labeled `ApertureFailureValue`,
`LvApexCalc` and `LvFirstAeCalc`. Command37 then reaches `10310f64`, rescales
the aperture target to denominator 192 and dispatches A0/A1/A3 as alternative
control encodings. These are ordinary aperture operations even with optional
capabilities zero. Offset 199 supplies the positive travel clamp; zero forces
zero closure travel for the fixed virtual iris. The finite model accepts only
complete zero-target, zero-parameter requests after configuration. An observed
original A0 buffer contains the full zero tail and valid checksum. Unknown
nonzero A0 tail or A1 parameters remain explicit errors.

Zero travel requires no actuator motion, changes no aperture coordinate and
leaves action busy bits4/3 at `20:00[203]` clear. This is distinct from a
nonzero motion acknowledged without processing. The original firmware still
receives and validates the complete response and polls its own status path.

The long calibration packets are complete initialized device replies. They
contain finite coefficients, not shortened acknowledgements or copied factory
tables. The following source paths qualify their chosen virtual contents:

| Section | Source-backed consequence of the chosen input |
|---|---|
| Main coefficient header0 | `103665c4 → 1036658c` selects the firmware's own Q10 unity gain curves and clears the lens-specific derived coefficients. Other user image settings remain firmware policy. |
|179-byte response block | `102cb62a` tests two81-byte zero spans. `102cb1e4` then selects its explicit no-apply path and Q15 unity factors. This is a sentinel, not a zero active matrix. |
|216-byte response block, representation1 | Three blocks encode higher-order radial polynomial coefficients. `1040e1d6` computes `r' = r × (1+d)`; zero coefficients give `d=0` and `r'=r`. The normal fit uses15 distinct nonzero source samples and a nonzero denominator. No identity-matrix guess is involved. |
| Secondary coefficient page | The page retains its full3456-byte finite coefficient storage and zero lens-specific polynomials. Header0 is not described as a proven disable flag. Original metadata/default/offset selection remains in the firmware. |

This is a coherent virtual calibration input for the cold-neutral functional
profile, not physical calibration accuracy or proof that every user correction
setting is equivalent to no correction. The original parsers and numerical
consumers still run.

## Real-time state and lifecycle

`16:00` can enable streaming only with an explicit `stream` controller link.
The virtual source supplies 42-byte records at a nominal 1 ms cadence, with
4-microsecond 16-bit clock words. These are declared virtual input clocks.
The receive controller must actually be enabled, released, in slave mode and
requesting that word width. A refused word stays pending without consuming
FIFO data, manufacturing a completion or overwriting a full receiver.

The whole record contains marker55 and a zero byte sum. Byte1 is a wrapping
history sequence. Bytes2..5 hold the stationary int32 motor coordinate0;
bytes6/7 contain coherent stationary status and no direction-limit assertion.
Byte8 and the two opaque32-bit fields at13/17 are defined zero for this virtual
stationary profile. Remaining reserved bytes are zero and checksum-covered.
`10348c8c` and `1037b886..1037b94e` establish the original field projection.
Motor coordinate is distinct from scene distance, focal length and aperture.

Stream acceptance counts only complete records actually clocked into the
native receiver. Disable pauses the producer while retaining an unfinished
record: re-enable resumes its remaining words, and repeated enable is
idempotent. Neither operation inserts another marker into an accepted prefix
or silently clears the receiver. Reset cancels its pending
word, phase and configuration timers. Reset restores the declared initial
profile. A bad header, checksum, length, unsupported command or invalid control
enters an explicit error state, reports the reason and produces no successful
phase edge. Reset is required to recover that failed session.

The read-only `completed`, `rejected`, `configuration-generation`,
`stream-frames`, `aperture-noops` and `runtime` properties expose device work and packet phases.
They are diagnostics, not guest
readiness or GUI completion.

## Scene-provider direction

The intended integration keeps the two inputs separate and coordinated:

```mermaid
flowchart LR
  Scene[3D scene provider] --> CMOS[CMOS and RAW input]
  CMOS --> Pipeline[Original readout and image processing]
  Pipeline --> GUI[Original GUI and scanout]
  Firmware[Original lens commands] --> Lens[Virtual lens state]
  Lens -. focal / aperture / focus state .-> Scene
  Lens --> Transport[SPI replies and motor history]
  Transport --> Firmware
```

The existing PGM/PPM provider is the reproducible scene input today. A future
3D application adapter should submit frames with explicit dimensions, format,
ownership and capture time to that input boundary; lens state should describe
the same capture generation. Pixels must continue through original acquisition,
processing and scanout. No host-rendered GUI or direct framebuffer replacement
is part of this design. No application-specific bridge is implemented here.

## Validation boundary

The native lens fixtures exercise the complete ordinary command transcript,
framing, optical endpoints, real configuration busy-to-complete processing,
synchronization, malformed packet rejection, partial reset and receiver-bound
stream lifecycle. They use the actual upstream SSI bus and native SPI receive
FIFO. These synthetic fixtures do not prove that unchanged original firmware
has accepted the virtual lens, published its GUI cache or displayed scene pixels.
The current original-run result is tracked separately in the
[startup frontier](../../validation/frontier.md).
