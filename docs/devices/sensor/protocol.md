# External CMOS frame input and readout

This device adds an executable frame endpoint to the [unchanged-firmware startup](/startup/first). It reads an explicitly supplied byte file through official QIOChannelFile and writes its bytes through the actual physical-memory DMA bus. It supplies exposure, readout, completion and error lifecycle outputs. It does not identify a physical sensor, invent a controller register map or connect an unbound guest interrupt.

## Current source boundary

The saved complete native source index is `nightglass` CODE `ec048f9fde8723ba2db9657f2e88410933b21099`, with the read-only local index at `run eventwait/gui-saved-facts/facts.sqlite` in `native-arm-exception-roots`. The bounded `Capture-consumer-source-projection.json` preserves complete original Listing/raw P-code/reference owners for MAS outer VA `103ba6ea` (O3778282) and `103f40ec`; requested `104909bc` has no containing current Listing/function. The first two owners have no direct reference targets in the currently known `04/20/24/2b/2c` peripheral ranges. Indirect runtime addresses and their callees remain separate evidence work. Capture requests and callbacks do not establish the CMOS slave address, CSI/receiver registers, exposure units, ROI, packing, physical buffers or completion IRQ.

The current machine has no source-bound CMOS controller. SDI `2002x` implements DDR training; it is not assigned a sensor role. IPUD `24020000` is a three-word packet transport, not an image ingress. Sampler `20201000` and the `2b` signal inputs retain their disconnected external-data/pin semantics. The DMP `2018` transfer has actual buffer-to-buffer byte geometry and numeric selectors, but is not an established sensor/ISP output. The frame endpoint therefore remains disconnected in default original runs. Machine/control decoding belongs to its own owner and must supply actual firmware geometry and routing when that chain is recovered.

## Implemented endpoint

`include/hw/nikon/sensor.h` exposes `nikon_sensor_begin_frame(dev, request, errp)` and cancellation. The request contains opaque `row_bytes`, `rows`, destination physical address, destination stride, and explicit virtual nanoseconds for exposure and each readout chunk. There is no inferred Bayer order, bit depth, packing, width/height conversion, crop or image-processing operation. Source rows are densely stored in the backend; destination row padding is untouched. An existing controller can construct this request from its proven configuration and consume the real DMA-written RAM. No private framebuffer, successful memory fallback or host write to a guest surface is used.

The QOM type is `nikon-sensor`, a bus-independent external DeviceState. Its `frame` property is the path of an explicitly supplied regular byte file, opened read-only by QIOChannelFile. Optional numeric request properties support explicit synthetic endpoint tests; `capture=true` invokes the same public API, and `capture=false` cancels/acknowledges. Read-only `phase` and `bytes-written` expose actual progress. These are host-side functional interfaces, not Nikon guest registers. Configuration defaults are zero, with no input backend; a capture without a complete configuration and real input is rejected before mutation.

| Phase | Cause and observable behavior |
| --- | --- |
| idle | Reset/cancel removes the timer and clears lifecycle outputs. Cancel retains already committed progress; reset clears progress. Neither operation erases RAM. |
| exposing | A validated request latches geometry, clears progress and asserts exposure-active. No byte is written before the supplied exposure deadline. A second capture is refused. |
| reading | Exposure expiry deasserts exposure-active and asserts readout-active. Each subsequent supplied deadline reads at most4096 backend bytes and writes one bounded chunk of the current row through `dma_memory_write`. |
| done | Only successful consumption and DMA writes of every complete row produce frame-done. Readout-active falls. Completion remains asserted until cancel, reset or a new valid capture. |
| error | Backend unavailability/changed length/read failure or a real destination bus failure ends the request, deasserts readout-active and asserts frame-error. No completion accompanies it. Accepted earlier DMA writes remain visible. |

Four ordinary QEMU GPIO outputs carry exposure-active, readout-active, frame-done and frame-error levels in that order. They are not GIC numbers, sensor pins, EXIU wires, guest event flags or physical timing measurements. A proven controller may consume these outputs, but no host callback invokes guest code or fabricates a frame interrupt. Cancellation/reset changes a generation; synchronous backend/bus calls recheck it before further state changes. The bus-independent endpoint registers its existing DeviceState Resettable object with the official system reset root and unregisters it on unrealize.

Preflight requires nonzero byte geometry and durations, stride at least the actual row size, exact input byte length, a destination span wholly within the32-bit physical-address range, and all exposure/readout deadlines fitting signed virtual time. The actual open file's byte length is checked around each read; each new capture rewinds that same file. Official `qio_channel_read_all` rejects EOF/short data rather than padding it. Readout uses normal-memory-only DMA attributes with an actual writable-RAM preflight: missing RAM, ROM and MMIO destinations do not become successful image buffers. Readout timing is the supplied per-chunk virtual law; it is not CMOS row rate, shutter timing or EXPEED performance. The endpoint does not transform pixels or infer successful ISP processing from an input frame.

## Tests and integration

`tests/qtest/nikon/sensor.c` contains six native cases using explicit synthetic geometry and repeatable file bytes: exposure-before-write and complete strided rows; two4096-byte chunks; active-request refusal/cancel/system-reset; missing input, bad stride, length/address/deadline mismatches; a causal unmapped destination error; and input truncation after one accepted row. Tests observe the endpoint's real output lines and actual RAM, preserving padding and bytes beyond completed progress. They run with the qtest accelerator and an enabled virtual clock, without executing camera instructions.

Build registration, compilation and execution belong to the sole build owner. The six frame-endpoint cases passed in2.95s, exit0, on QEMU `d3d69c38ed2845852ae6187c8bf038e8c2eb7e6a`, implementation `fad3bcffbe16edf978928c1a797259722bf229bc`. The preserved official receipt is `native-qemu-source/run validation/gui/sensorfinal.log`. This establishes the explicit endpoint, not a firmware sensor controller, image quality, full camera functionality or reconstructed-source acceptance.

The first native sensor binary compiled and linked, then its test process exited with SIGTRAP before reporting a passing case (`native-qemu-source/run validation/gui/domaincheck.log`, sensor section). Source inspection found a concrete fixture ownership error: QMP `%p` interpolation transfers the supplied QObject into the request; official `libqmp.c` frees that request after sending it, but the fixture freed its QBool again. The corrected fixture uses official `%i` boolean interpolation, whose type is explicitly QBool in `qobject/json-parser.c`; it allocates no separately owned value. Device lifecycle and all six test assertions were preserved. The first failure remains separate from the later passing receipt.

The ownership correction then passed the first five native cases and failed the final truncation assertion (`native-qemu-source/run validation/gui/runtimecases.log`): phase remained reading instead of error. Official `block.c` defines BlockBackend length in512-byte sectors, and disk reads can supply zero padding beyond the underlying EOF. Those disk semantics cannot implement an exact-byte frame contract. The current implementation removes the drive route and uses official QIOChannelFile/read-all with exact file size, preserving the truncation assertion. The later six-case pass includes the51-byte three-row positive sequence and the original EOF/no-DONE negative.

Next source dependencies are the actual sensor configuration/trigger writer, receiver-to-physical-buffer descriptor, encoding/ROI/stride producer, and consumer/IRQ route. ISP, display and storage owners must join those identities before connecting the endpoint. Default disconnection is preserved until then; an independently supplied frame is an external input, not a recovered factory component.

## Original SDES control protocol

Original MAS outer initializer `10295768` identifies its software driver as `SDES Driver` and registers handlers for IRQ `0x59` through `0x5d` (89–93). This is a firmware driver name, not identification of a particular CMOS chip or physical IP. Original pointer literals `10295ad0`, `10295aac`, `10295aa8` and `10295f6c` identify the separate banks below. Addresses are original firmware VA; machine PA placement uses the existing peripheral identity-map convention and belongs to machine glue.

| Region / proposed VA=PA | Implemented scope | Original source |
| --- | --- | --- |
| configuration / `20061000`, size `500` hex | Finite packed parameters and five byte-parameter groups; readback after valid writes | `1029581c`, `1029595c`, `10295a32`, `10295a64`, `10295b88`, `10295c10`, `10295c98`, `1029796e` |
| status / `20064000`, size `2c` hex | Status `+14` and acknowledgement | handler `10295752` |
| parameters / `20065000`, size `554` hex | Independent primary and secondary (`+400`) parameters and ten status words | `10295e9a`, `10295ef6`, handlers `10295738`, `102956f6`, `102956de`, `102956a0` |

| IRQ output / machine GIC input | Complete status-word order, original VA |
| --- | --- |
| `NIKON_SDES_STATUS_EVENT` / 89 | `20064014` |
| `NIKON_SDES_PRIMARY_EVENT` / 90 | `20065108` |
| `NIKON_SDES_PRIMARY_DETAIL_EVENT` / 91 | `20065024`, `20065110`, `20065118`, `2006511c` |
| `NIKON_SDES_SECONDARY_EVENT` / 92 | `20065508` |
| `NIKON_SDES_SECONDARY_DETAIL_EVENT` / 93 | `20065424`, `20065510`, `20065518`, `2006551c` |

Each original handler snapshots these words and writes the same values back before invoking its registered software callback. The functional model uses latched OR status and W1C acknowledgement to express that software contract; physical latch/reset values, event-bit meanings and interrupt masking are not independently measured. A group remains asserted until all its pending words are acknowledged. No parameter write, elapsed time or absent peer manufactures status.

The new `nikon-sdes` SysBus object lives in `sensor.c`, separate from the tested `nikon-sensor` byte-file endpoint. Public definitions are in `include/hw/nikon/sensor.h`. MMIO regions follow `NikonSdesRegion`; IRQ outputs follow `NikonSdesEvent`. Accepted accesses are aligned little-endian32-bit transactions. The finite offset set and generated-value bounds come from saved source. Unknown offsets and unsupported values return `MEMTX_ERROR`. Known configuration/control fields, including the original RMW values at `20061000`, `200610e8`, `200610f4`, `20065104` and `20065504`, use an explicit functional cold-zero reset assumption. This supports original RMW without inventing ready/data or a frame. Reset restores that profile, clears functional pending events and requires new publication. Data observations at configuration `+300..310`, `+364/+368`, selector `+414/+418`, and status-bank `+28` remain blocked rather than fabricated.

`nikon_sdes_publish(dev,event,words,count,errp)` requires exactly1/1/4/1/4 status words in table order and validates the entire packet before mutation. A provider must supply a real event and its complete status; no event is assigned frame-ready semantics. Named `status-data` inputs hold eleven32-bit words in table order; five `status-publish` rising-edge inputs call the same API. Publication requires every bit of every required word to have been explicitly supplied, including zeros, then consumes that qualification. These functional provider inputs are not352 invented physical pins. There is no default provider, connection from `nikon-sensor` completion, or host call of guest callbacks.

Five bounded official source exports using TOOL `e9b738a423db452bc1061809fcc01656e59d6631` are saved in `nightglass/run controller`: session93262 exited0 in6.553s, with five owners and191 original Listing/official block CODE instructions, without analysis or a new checkpoint. Four candidate C bodies are `ok`; `10295e9a` retains its scope rejection. Original Listing/bytes remain authoritative. Source-qualified direct branch `10297976` (O2586998, bytes `fef7f6b9`, TMode1) was then recovered by TOOL `f5f8d0e` into `run precision/raw/1029796e-original.c`: it writes five byte values at offsets `400..410` plus the selector offset from `10295d5a`. Original table `1078a844` begins `20061400/420/440/460/4a0`, giving offsets `0/20/40/60/a0` after the original subtraction. This qualifies configuration groups; their status values require a real producer.

The recovered capture route reaches software workers `103250f2`/`103259cc`, then `10233570`/`10233db8`, `10398206`/`103986ca`, and backend worker `1026c66e`. One concrete outlet is `102d9c54`→`102d9a2c(2)`→`102ff398(1)`→`1029790a`, writing SDES parameters. The full word at `20061250` comes from a zero-extended mode-table halfword in `102fefc0`, not a proven physical frame address. Actual chain `102feda2`→`103063bc`→`10297914`→`10295c04` writes a table pointer and aligned byte span at `20061264/268`; original arithmetic is `align16((pointer&15)+2*count)`. This establishes a configuration-data input, not image output DMA. The separate eleven-channel pointer table `1078a92c` identifies `200630xx` control/strobe registers; its pixel-readout relationship remains unproved and is not absorbed by proximity.

Three new focused qtest cases cover independent configuration banks without IRQ, complete eleven-word publication and partial W1C, and incomplete/stale publication plus reset. They passed individually in0.69s each, exit0, on integrated QEMU `7f241eb9d9c046db4cc3c3ccf5719697ae8ea02c` (SDES `229b68af57333e2791641377416337cb0b51bde4`, machine glue `85bac47`, named-IRQ fixture correction `f8672cacdf2590814cfc6eb6cf29c1973536a7f3`). The sole builder saved `sdesconfigurationfixedreceipt.log`, `sdesstatusfixedreceipt.log` and `sdespublicationfixedreceipt.log` under `qemu-submodule/work`. Their first attempt compiled/linked but failed before assertions because unnamed IRQ interception found no outputs; official SysBus IRQs are named `sysbus-irq`. The cold-zero configuration extension subsequently passed in1.17s on integrated ARM `49e63fc9b3cd1e43c0f15da6de7f6169a4503895`; `sdescoldconfigurationnativereceipt.log` records that distinct run. The six prior endpoint cases are unchanged. Actual exposure trigger, reset profile, sensor serial slave, pixel geometry/encoding, frame destination and readout completion cause are still required before joining this controller to frame input, ISP, display or save. This implements the source-bound control/status layer, not completed original CMOS capture.

## Global sequence start and stop

The C1.70 MAS helpers `102976b6` and `102976c2` perform RMW/strobe pairs
at board PA `200632d0/d4` and `200632e0/e4`. These belong to the separate
eleven-selector table at MAS VA `1078a92c`, implemented by
`NikonSdesChannels` in `sensor.c`. They do not publish SDES IRQ89–93 status.
Addresses, register masks and event masks are hexadecimal; channel indices,
bit positions, IRQ numbers, timeouts and error IDs are decimal.

**Source facts:** `102d986e(0)` clears Sensor event `0x10`, submits mask `0x101`
through `102ff522 ->102976c2`, then waits with timeout150 in original API
units. Its other arm submits `0x107` and waits for event `0x20`. The original
router ISR `102dc5c0` invokes registered callback `102d6b6a`: raw bit18
sets event `0x10`, and raw bit10 sets event `0x20`, at Sensor object VA `b18a764c`.
Thus these completions belong to router IRQ87, not the SDES status banks.
An instrumented pre-model probe isolates the first wait returning0 with
accumulated status0 before OR `100`; the upper mapper reports Sensor state9
and ID63.

**Functional hypotheses:** the `d0/d4` pair starts selected sequences and
`e0/e4` stops them. Each commit consumes its mask, so a later RMW starts a
fresh request. The eleven selectors bound the accepted masks to `7ff`.
Each active selected channel takes1µs of virtual time to stop. Channel8
publishes router bit18; channel0 publishes bit10. This interpretation also
matches the independently implemented Z7
[sequence experiment](https://github.com/WonderLantern/z7-qemu/blob/97abf0b81cd3211f8374a38987a0beaf4549b90f/qemu/nikon-z7-lab.c#L1890),
which explicitly labels its own timing as assumed. It corroborates a finite
functional interpretation, not a physical timing specification.

The model tracks all eleven running/stopping bits. A mask store alone does
no work. WRITE32 `1` to its commit applies the selected transition; other
values, widths, unaligned accesses and unknown mask bits reject. Stop affects
only running channels; empty or already stopped requests publish nothing.
Each newly stopping channel retains its own deadline; a repeated pending
stop cannot postpone it or borrow another channel's earlier deadline.
Restart cancels only the selected unfinished stops. One QEMU virtual timer
serves the earliest deadline, and expiry updates actual sequence state before
publishing only the two established causes. Other channels stop without
invented causes. Reset clears masks/state and cancels all deadlines; finalize
frees the timer. Read-only QOM `running`/`stopping` expose model state.

Machine wiring sends `stopped[0/1]` to router domain1 bits18/10. Existing
router masking, pending/W1C, GIC delivery, original ISR and callback own the
rest of the path. No guest flag is set by the host. Configuration, data DMA,
pixel work and frame completion remain separate; sequence stop creates none.

The [sequence receipt](/evidence/imagerequest/sequence.json) records native
red→green for consumed masks and staggered stops, followed by36 sensor and33
memory cases. The fixtures cover every selector, actual router pending,
inactive/duplicate requests, partial/full restart cancellation, independent
deadlines, malformed transactions, reset/reuse and unchanged RAM/SDES status.
Its original continuation uses unchanged firmware and the same explicit
resource35/cold-neutral inputs. The following capture-group result boundary
is tracked in the [startup frontier](../../validation/frontier.md); passing
sequence tests do not establish normal GUI startup or physical fidelity.

## Original capture configuration and commit family

The [mask and enable audit](control.md) supersedes independent0/4 storage.
The current [run/stop profile](runstop.md) also replaces08 retention and the
restricted0c mask with the whole11-unit control family and explicit deferred
quiescence hypotheses. All25 sensor cases pass; original execution crosses
the old0c rejection without generating data or a completed frame. The original
coefficient/publication audit and historical receipts below retain their
fixed scope; current control semantics and runtime limits are in runstop.md.

The same saved `ec048f9fde8723ba2db9657f2e88410933b21099` source closes a separate control outlet: `102d8606`/`102d8810`/`102d8c54`→`102dab42`/`102dabca`→23-pointer configuration packet→`103003dc`. Its callback field is RAM `b18a7798`, derived from original literal `10300450`=`b18a778c`. Initializer `1023954a`, at callsite `102396b4`, registers original literal `10239a00` (bytes `2d912310`, Thumb `1023912d`) through `103981cc`→`1026d72a`/`102fe10e`, only when that callback field is zero. `1023912c` copies the packet into local storage and invokes `10238fbc`, whose nonnull pointer checks select twenty accessors `1020c46a..1020c536`. These are software provenance and configuration facts, not an observation that original capture has run.

`nikon-capture-control`, also in `sensor.c`, has one region of size `1180` hex. Machine placement at original VA `20068000` uses the existing explicit VA=PA convention. The device accepts aligned little-endian32-bit accesses only. The finite configuration stores are offsets `0`, `4`, `8`, `c`, `18`, `88`, `8c`, `100`, `104`, `500`, `508..81c`, `a00..afc`, `c00..e3c`, `f00..100c`, and `1100..117c`, plus the eight masks below. It uses an explicit functional cold-zero reset profile for only these known control/configuration words, then retains accepted writes. Other offsets and unknown data observations remain bus errors. In particular `504`, `900` and gaps between coefficient tables are not included merely because they lie in this aperture. Registers `0/4` are bounded to12 bits and `88` to8 bits by their saved stores; the packed tables retain full32-bit values rather than assigning guessed Boolean or pixel meanings.

Original pointer literals `10209bb0/b4/b8` identify `20068500`, `20068810`, `20068610`; `10209e68..88` identify `200686e4`, the six-subtable pointer array `1078a7fc`, `200687f8`, `20068900`, `20068a00`, `20068c00`, `20068d20`, `20068f00`, and `20069100`. The six actual pointers are `2006871c/738/754/770/78c/7a8`; each receives seven words. Accessors write packed halfwords, scalar fields, two72-word tables, four16-word planes, four17-word planes and a final32-word table. No saved store in this packet writes the `20068900` table, so that address is not enabled. These are configuration/coefficient tables; they do not establish RAW encoding, image dimensions or frame-buffer addresses.

The packet then calls `1020bfaa(1)`→`1020ab28`, storing at `20068100`; an additional packet field selects `1020bfb6(1)`→`1020ab20`, storing at `20068104`. Both accessors accept the original full word. Each explicit command write publishes its32 data bits through named `command-data` outputs (first/second words in order), then pulses the corresponding `command-publish` output. A real provider can inspect known configuration with `nikon_capture_configuration(dev,offset,value)`, including the explicit functional cold-zero profile. These are synchronous functional interfaces, not physical pins, guessed start bits, DMA or completed processing. No provider is attached by default; commands alone create neither status nor a frame.

| Machine IRQ | Handler | Status offsets | Paired mask offsets |
| --- | --- | --- | --- |
| 94 | `1020a9b2` | `210`, `230` | `200`, `220` |
| 95 | `1020a960` | `250` | `240` |
| 96 | `1020a8fa` | `270`, `290` | `260`, `280` |
| 97 | `1020a896` | `2b0`, `2d0` | `2a0`, `2c0` |
| 98 | `1020a836` | `2f0` | `2e0` |

Initializer `1020aa18` registers these five handlers. Each snapshots the complete listed words, ANDs them with mask shadows at RAM `b18067e4+38..54`, writes the original status back, and dispatches its raw/filtered software callbacks. `1020ab7c` changes hardware masks by RMW and copies their values into those shadows. The functional model latches actual external publication with OR, implements W1C status and defines IRQ as nonzero `pending & explicitly_written_mask`;1 enables. This mask/level/latch law expresses the software contract and remains a model assumption about physical hardware. Known masks use the explicit cold-zero profile, allowing original RMW while initially disabling every IRQ. Clearing a mask preserves pending status; re-enabling exposes it until acknowledgement. Reset clears pending and all provider qualification, restores known configuration/masks to functional zero and lowers command/event outputs.

`nikon_capture_publish` requires exactly2/1/2/2/1 words for `NikonCaptureEvent` in table order. Named `status-data` supplies eight32-bit words, and five `status-publish` rising edges use the same API. Every required bit, including zero, must be explicitly supplied before publication; a successful packet consumes qualification. Partial or stale packets do not become events. These256 functional data inputs are not inferred hardware wires. No status bit is labelled exposure, frame-ready or readout-complete without its actual producer.

Three new focused tests target `/machine/capture-control`: every accepted coefficient-table word and both independent command-data outputs/reset without IRQ; complete eight-word publication with mask-disable/re-enable and partial W1C; incomplete/stale publication and reset. The sole builder compiled integrated ARM `49e63fc9b3cd1e43c0f15da6de7f6169a4503895` successfully and these cases passed in1.33/1.17/1.17s, exit0, with implementation `2c7dfd7b50b2531ca213e5daa5aef68745eb131c` and MAS glue `58e0a31`. Receipts are `qemu-submodule/run captureconfigurationnativereceipt.log`, `capturestatusnativereceipt.log` and `capturepublicationnativereceipt.log`. They do not repeat the six validated endpoint cases. The serial-control dependency is separately source-bound through `102fe1ea`→`1030621e`→`10367fe0`→`10370a14`: logical instance7 is mapped by signed board-map byte `+e` plus `17` hex, using literal `10368b98`=`b1800008` and pointer `[b1800014]`. The actual selected peer, exposure/readout law, pixel format, receiver destination and downstream processing/display/save join are still unbound. Register configuration, a commit pulse and software callback provenance do not complete that missing image pipeline.

The independently restored SLA SOURCE11 `sla.S` also qualifies both shared device types. Its fixed TOOL producer is `ef6395d18386f58f8551fc663df309ee7f8475f5` (project10 `run sla-source-project-10`) and canonical SOURCE is `9a9fd191f4cbe4ed0f0a38c2a3fc345ea2f0ad30`; project11/12 change ROM COPY resources while preserving these source rows. Its initializer `1009e27c` registers IRQ89–93, with literal callbacks `1009e267/24d/20b/1f3/1b5`; handlers `1009e1b4..1009e27a` read and acknowledge the same eleven SDES words. All stores in `1009e330`, `1009e470`, `1009e4c2`, `1009e724`, `1009e9ae` and `1009ea0a..1009eba4` fit the existing finite SDES fields and generated-value bounds. Initializer `1009cfd0` registers IRQ94–98, with callbacks `1009cf6b/cf19/ceb3/ce4f/cdef`; `1009cdee..1009cfd0` independently demonstrates the same eight status words, W1C sequence and masks. Literal `1009d25c` is `20068000`; `1009d134` performs all eight hardware-mask RMW operations and saves independent shadows at `c1005d3c+38..54`. Two additional source-known controls are accepted: `1009d056` writes `fff` to `+18`, and `1009d3c0` writes `c` to `+c`. They have explicit cold-zero configuration/readback and bounded12-bit/2-bit values respectively; no undocumented effect clears status or completes a frame. SLA must instantiate its own devices, address space and GIC wiring, without sharing MAS configuration, pending events or providers. This is a source-based match, not a conclusion from matching IRQ numbers.

## Source-bound readout profile

The unchanged MAS builder at `0x1023869e` creates the record later passed to
`0x1020c7c6`. Its exact field and pointer arithmetic is recorded in
[`../readout/mapping.md`](../readout/mapping.md). The record contains
aligned spans and double-indirect pointers derived from firmware RAM tables.
These are source facts only: current evidence does not identify them as image
width, height, stride, Bayer format, DMA direction, or buffer rotation. The
QEMU readout model therefore keeps the profile provider-independent and does
not copy these values into the CMOS endpoint. A runtime observation must
follow the record consumer to a proven transfer or completion producer before
any field receives an imaging name.

## Buffer selectors and readout parameters

`nikon-readout-control` is a separate SysBus object with two finite regions: `NIKON_READOUT_BUFFERS`, size `384` hex, proposed VA=PA `20069800`; and `NIKON_READOUT_PARAMETERS`, size `404` hex, proposed VA=PA `2006b000`. Its named request/ready/completion lines are observation boundaries, not GIC wires. Aligned little-endian32-bit accesses retain only source-known configuration with explicit functional cold-zero reset. Other offsets, invalid scalar widths and unpublished data observations return `MEMTX_ERROR`. Neither region extends the capture-control aperture or creates an image by accepting configuration.

The saved original MAS source `ec048f9fde8723ba2db9657f2e88410933b21099` closes the buffer producer `102384d4`→`1023847a`→`102cd1ce(0,handle)`: each returned record contributes its `+c` word. Four words are sent through `1020c358` to selector0/1/2/3 banks at `20069850/20069950/20069a50/20069b50`; the second bank adds `10` hex. `1020c366` writes selector0 bank at `2006983c` and selector2 bank through RMW at `20069a3c`. For the special handle literal `10238714`=`b26b3b74`, the producer translates each word with `103880e4(2,address,0)` and rotates them to old3/0/1/2. That translator performs actual range lookup and address arithmetic; these local stores alone do not prove every stored address is physical or identify planes/encoding. The caller commits `20068100` with `100` or `80` hex, separately from the readout request.

Accessors `1020c334/340/34c` supply finite scalar configuration at buffer offsets `8/108/208/308` (16-bit), `14/40/214/240/280/380` (full word), `18/44/48/4c/218/224/244/248/24c` (8-bit), and `3c/23c` (bank/packed selector fields). Query `102092fa`, using literal `1020935c`=`20069870`, reads4/2/4/2 actual words for selectors0..3. Selector0 reads offsets `70/74/78/7c`; selector1 `178/17c`; selector2 shares `70` with selector0 then reads `274/278/27c`; selector3 `378/37c`. These eleven words are externally supplied observations. `nikon_readout_publish` accepts exactly the selected complete query, validates it before mutation and preserves the shared-word identity. There is no default observation provider, fabricated zero data or automatic IRQ.

The parameter contract comes from `1020c788/794/79e/7a8/7ba/7c6/8a8`. Literal `1020b21c`=`2006b000` anchors the129-word zero-extended halfword coefficient loop at offsets `0..200` hex. All original stores in `310..3fc` hex are retained, including packed8/10/16/24-bit fields with their actual masks. Independently, fixed SLA source `1009c76e`, `1009c832`, `1009c866` and tail wrapper `1009c96c`, with literals `1009ca04`=`2006b000` and `1009ca08`=`2006b3b8`, produce the same `318..3bc` subset and generated-value bounds. This is a full accessor-family match, without inferring an IP identity from its address. Existing `20060000` routing/IRQ86–88 remains independent.

`1023869e` builds a profile passed to `1020c7c6`. In that actual producer, `2006b3e8` and `2006b3f4` come from allocator objects' `+4` pointers; `3ec` is `align256(2*profile.u16[14])`, `3f0` is that span times `profile.byte[c0]*2`, and `3f8` is `align256((3*profile.u16[14])>>1)` (field offsets hexadecimal). These prove the bounded pointer/span record without proving physical DMA direction, image dimensions or color format. Record bytes `34/35` produce the two observed packed values `00400040` and `00010001` at `3fc`. `1023c610` alternates a bank and writes another record's `+c` word through `1020c7ba` to `3c4/3c8`, then calls `1020c7b2` and commits `20068100=2`. Original instructions at `1020a3b6..c8` show a conditional, not a polling loop: load `3c0`, compare with1, return if unequal, otherwise store1 to `2006b400` at `1020a3c6`. Firmware itself writes `3c0=1` while configuring the descriptor.

The complete bundle composition assigns that finite C1.70 record a functional, not physical, producer contract. Initial wrapper `10238876` builds the descriptor then calls `1020c728`: shared commit100/104 mask2 followed by effective unit2 run. The model snapshots parameters on either selected commit and reserves the first request only on effective run rising. Repeated high levels do not request. Subsequent `2006b400=1` commands snapshot the current staging record, consistent with the marker preceding its following commit. This first-run interpretation is an explicit functional hypothesis supported by the producer/consumer ordering; it does not synthesize a `b400` write. Eight profile fields (`394/3c0/3e8/3ec/3f0/3f4/3f8/3fc`) must have been explicitly written. Enable `3c0` must equal1 and mode `394[23:16]` must be0 or1.

`3e8` is the functional primary destination, `3ec` its 256-byte-aligned stride, and `3f0` its exact span. `3f4/3f8` are a required, range-validated secondary pair but are not written. Only `00400040` and `00010001` at `3fc` are accepted. Geometry is two-byte samples, `width = stride / 2`, `height = span / stride`; these are not physical sensor dimensions or pixel-format claims. Span is bounded to64 MiB, both address ranges are checked, and the CMOS engine performs timed DMA only to the primary writable guest buffer. An explicit PGM/PPM source is used when configured; otherwise the bundle uses the deterministic CMOS provider.

Provider ready is an ownership observation and does not rewrite guest `3c0`.
The request reserves its immutable descriptor/generation immediately, including
while waiting for input. Busy pulses cannot replace that ownership. As an
explicit functional input association, the bundle connects the existing event
router's automatic TG rising edge to the pipeline: the next fresh edge admits
one owned request to the CMOS engine. There is no queued credit for earlier or
busy edges, no admission on repeated high or forced router levels, and no DMA
before input. The existing TG period remains33333334ns. Processing on that
edge sees the previous completed input; the new transfer finishes afterwards.
The engine retains1ns exposure and1ns per bounded DMA chunk, not per frame;
this is a deterministic quantum, not physical sensor bandwidth.

Auxiliary physical unit2 stop/reset/disable releases its waiting generation or
cancels the CMOS timer only when that auxiliary transfer owns it. It does not
cancel independent main acquisition or invalidate a delivered main frame.
Machine reset clears ownership/input data, preserves the externally driven
input level, and does not cancel unrelated CMOS work for a merely waiting
request. Cancellation and errors produce no completion. A request counted as
accepted/reserved may later fail CMOS admission checks at the input boundary;
that increments rejection and releases ownership. Only actual successful DMA
DONE for the submitted live generation publishes capture IRQ94 raw0.bit3 in
mode0 or bit5 in mode1; unrelated DONE while waiting is ignored. Original
`102391b2` checks that cause, the aggregate predicate and software mode before
setting its owner event bit29; `10238d4a` then calls `1023c610`. Joining actual
transport completion to those hardware causes is an explicit functional
hypothesis. Mask, pending, GIC and W1C behavior use the existing capture device.
The host never writes the owner event, calls an ISR/callback or changes a guest
PC/task. Unsupported profiles or non-RAM destinations remain rejected, with a
diagnostic reason and no completion.

Without the bundle the provider remains disconnected: configuration, commit/run and request observation do not perform DMA or publish an IRQ. Native pipeline fixtures cover first-run with `b400` still0, selected commit/staging isolation, mode0/1 completion causes, repeated high, busy ownership, format/geometry/RAM rejection and cancellation by stop/reset/disable. The input-boundary fixture additionally covers pending ownership, immutable mapping/mode, no-input silence, ignored unrelated DONE, discarded early/busy edges, pending cancellation/reset and actual automatic-TG board wiring versus forced levels. It fails on the former immediate-submit model; the scoped17 CMOS,33 Sensor and1 readout-unit batch passes. These tests establish the finite contract, separately from unchanged-firmware progress and physical fidelity. [Readout ownership](../../runtime/readoutstate.md#current-cold-neutral-producer-and-completion-join) records the original source chain; the [coordination audit](../../runtime/imagerequest.md#acquisition-on-the-normal-input-clock) and [receipt](/evidence/imagerequest/coordination.json) qualify original-firmware execution.

### Shared acquisition input synchronization

The C1.70 source route `102391b2 -> owner1 ->10238d4a ->041a000f ->1022f318`
reaches the shared tail `1022f526 ->1022f37c`, setting mask `4` on FE wait
object MAS VA `b1887d9c`. This resolves the software producer of the OnVd
AND wait for mask `5`. It is not the property notification's worker mask `20`,
and it is not a unit1/2 processing-completion cause.

**Functional hypothesis, implemented:** the bundle's virtual camera acquisition
supplies both the existing auxiliary transport and input synchronization. The
capture controller's effective physical run bit0 reaches pipeline `input-run`.
Qualification starts at successful CMOS submission on a fresh automatic TG
edge, **not** at earlier request reservation. Only a qualified generation's
actual successful DMA, immutable snapshot and accepted readout completion can
add raw word0 bit0 to the existing auxiliary cause. A falling run0 revokes the
current generation permanently; restarting or starting during DMA cannot
restore it. That revocation does not cancel auxiliary work owned by run bit1.
A start while merely waiting for acquisition may qualify the next fresh edge.

CMOS submission now returns a nonzero producer-generation token. Before a DONE
signal consumes its request, the pipeline requires the same token and actual
CMOS DONE phase. A premature external DONE is ignored without retiring the
live transfer; cancellation, error, reset and a newer provider generation do
not become success. No input status is generated from an old frame, run intent,
commit, elapsed time, repeated/forced/busy TG, invalid input or a failed snapshot.
Reset clears in-flight qualification and tokens; the existing input GPIO levels
and monotonic diagnostic counters follow the established pipeline lifecycle.
The read-only `input-notifications` count measures qualified status publications,
including masked ones, rather than guest callback invocations.

The additional native `pipeline/input-sync` case is red on baseline raw0=`20`
instead of `21`. A further red catches premature DONE during active acquisition.
The passing 22-CMOS/33-Sensor batch verifies actual RAM before sync, mask enable/
W1C, no added frontend processing bits, stop/restart and late-start revocation,
fresh-input requalification, unbound input, cancellation and reset. Existing
no-input, busy/stale, DMA-error and statistics cases also pass. The
[runtime audit](../../runtime/imagerequest.md#notification-routing-and-actual-submission)
records original ISR delivery and successful FE synchronization separately.
Neither those observations nor this shared virtual input identify the physical
main stream, unit1/2 pixel operations, their result buffers or complete GUI boot.

### Independent main acquisition

The first-read transition exposes a separate input lifetime. Its original
run mask becomes1d, retaining main input bit0 and frontend bits2/3 while
clearing auxiliary readout bit1. The preceding provider only acquired frames
for auxiliary requests. Auxiliary stop invalidated the shared generation,
leaving the main-input and statistics units running without a producer.
The [source and execution receipt](/evidence/imagerequest/acquisition.json)
separates that baseline, native regressions and the finite continuation.

Code/data addresses here are original C1.70 MAS VAs; register addresses are
board PAs, with masks/offsets hexadecimal and dimensions/counts decimal.

**Source facts:** `1023abaa` selects `1023aafe` for software unit1.
`1023aafe` independently installs its callback mask and calls `1020c444`;
the latter selects shared commit1 and `102097cc` enables the corresponding
capture unit. `1023a9d2 ->1020c3ec/c3fa` programs the frontend geometry and
uses `1020c3c0` for commit4. The retained packet at PA `20069d00/04`
contains input extent/origin fields, `20069e78/7c` the output fields and
`20069f00` the geometry control. This is separate from the auxiliary
`2006b3c8` request descriptor and its physical unit2 lifecycle.

The observed first read requests 8288 by58 input and2072 by58 output,
zero origins, correction control `20068500=0` and geometry control11.
Its event mask `20068200=10000020` excludes raw0.bit0 and bit6. The
`1023abaa` mask builder toggles bit6 for the mode1 statistics record.
Mode1's source consumer uses endpoint1 ring completion rather than per-frame
retirement. A masked raw status is retained by the capture controller, but
cannot be a requirement for a guest ACK that this mode does not request.

**Finite functional hypothesis:** on a fresh automatic TG rising edge,
committed correction0 and zero-origin, non-expanding geometry define an
internal LE16 gray stream at the frontend output dimensions. Both commit
ports snapshot only their selected main/geometry groups. All three effective
run gates1/4/8 must be active. Width/height are bounded to4096 and the frame
to4M samples; unknown correction arithmetic, disabled geometry, nonzero
origins and expansion are rejected. The externally selected PGM/PPM scene
and existing normalized sampling supply pixels. A fixture provider remains
explicit. No implicit scene is supplied when the provider is unbound.

This direct normalized stream simplifies the inline identity/geometry stages;
it is not a physical Bayer/readout reconstruction. The same CMOS engine,
provider-generation token, exposure timer and row-bounded4096-byte chunks
produce its bytes into an owned internal buffer. It does not allocate an
invented raw guest RAM aperture or prefill a GUI surface. Only a real DONE
for that token copies a completed frame into the processing input namespace
and publishes raw0.bit0. A premature DONE leaves the active job intact.
Statistics and paired processing retain their own configured DMA/result and
completion requirements; acquisition emits none of their completion causes.

The single engine is not preempted. One fresh automatic TG rise admits a
finite batch of at most two views: the eligible MAIN snapshot and the
auxiliary generation already owned at that edge. MAIN runs first; its actual
DONE, ERROR or CANCEL releases the engine to that admitted auxiliary without
another TG edge. An invalid MAIN profile does not discard valid auxiliary
ownership, and invalid auxiliary transport does not discard eligible MAIN.
This serial two-view batch is a functional acquisition assumption motivated
by the independent source unit lifecycles; it is not physical sensor timing
or proof that these views represent one measured exposure.

Earlier/busy/repeated-high edges create no credits. An auxiliary request
arriving after batch admission waits for a later fresh TG even if the current
MAIN view subsequently completes. A cancelled queued auxiliary generation
cannot be replaced inside that batch by a newer request. No generic request
queue or accumulated TG scheduler is introduced.
Main and auxiliary deliveries use one monotonic input-generation namespace,
and each processing job copies its selected input. Masked main notifications
coalesce in retained status without stopping acquisition. A pending **enabled**
raw6 data notification backpressures statistics alone; a masked raw6 does not
block mode1's ring completion. The earlier acquisition/statistics cross-gate
was a finite scheduling assumption, superseded by independent packet ownership:
MAIN continues fresh eligible acquisitions and capture-work blocks only on
its raw0 mask18 or raw4 mask80. Each processing job already owns a private input
copy. This separation is a functional inference from the independent unit
controls and consumers, not a physical cross-unit timing measurement.
Mask changes do not themselves
produce a frame, numerical result or completion.

Only a successful immutable snapshot and normal capture publication pulse
`input-ready`. MAIN additionally pulses `main-input-ready`, after its dedicated
MAIN snapshot is visible. These outputs carry actual input availability,
never a TG promise, readiness fabrication or downstream completion. The pulse
handlers can snapshot the published bytes before the admitted auxiliary
begins. Failed, cancelled, stale or merely submitted views produce no pulse.

Changed selected configuration or a falling main/geometry run gate cancels
only active main ownership. Equal commits and staging preserve the active
frame. Auxiliary stop cancels only its own pending/active request; it cannot
invalidate a main frame already delivered to the shared input. A failed
auxiliary snapshot/completion also cannot replace that frame. Reset clears
ownership, configuration validity and input data, preserving the external TG
level: a repeated high after reset is not a new rising edge. No callback,
guest event object, queue, latch, program counter or guest counter is modified
by the host to complete the transaction.

Native red cases establish three independent faults: main-only run1d
produces zero frames instead of one; a masked stream cannot finish its
configured endpoint ring; and cancelling an active auxiliary DMA loses a
previously delivered main frame before statistics can consume it. The focused
fixtures check actual scene-dependent scalar/output bytes, no auxiliary
request or raw RAM write for main acquisition, cross-channel arbitration and
cancellation, equal/changed commits, staging, each run gate, reset and fresh
edges, missing provider and unsupported geometry/correction. These are finite
device tests, separately from original-firmware startup.

The later `/nikon/cmos/pipeline/two-views` fixture checks one fresh TG against
the former alternating-view behavior: both actual auxiliary DMA and one MAIN
snapshot must complete, with two CMOS frame completions and no second TG.
Its companion fixtures cover busy-edge discard, a late/replaced auxiliary
request, independent MAIN/auxiliary cancellation, changed MAIN configuration,
whole-model reset and invalid profiles in either domain. The existing MAIN
domain fixture still checks that auxiliary pixels cannot replace the dedicated
MAIN input consumed by capture processing. Native results for this batch
change are reported separately from the historical execution below.

The earlier applicable batch passed32 CMOS,33 sensor and32 memory cases. In a final
45-host-second unchanged-firmware cold-neutral window,985 independent main
frames complete with no main rejection/cancellation, after290 auxiliary DMAs.
Acquisition therefore survives the auxiliary stop. Statistics completes290
earlier jobs and rejects984 later jobs at the sequential QMP sampling point.
Its first active endpoint now rejects PA `5df45400`, span300, because the
resolved region is `z7ii.uncovered`, rather than rejecting the disabled
outputs. The diagnostic remains explicit and publishes no result/completion.

Source `1030ad70` selects ordinary kind18 for up to6 requested units, but
kind32 above6, with stride12000 and total size `12000 * count`. The final
kind32 base table VA `b26b4bd0` contains `5df45400`, matching endpoint1's
committed address and the root descriptor. At this acquisition revision, its
complete backing/allocation and address conversion are the next boundary;
this change does not map a single failing address or extend the surrounding
DDR class. The first read
remains pending, information storage remains zero and both heads lack a
console. These observations do not establish FECLCTRK admission or GUI boot.
The later [pool continuation](../../runtime/capturedata.md#large-count-grid-storage-and-the-remaining-address-contract)
resolves that storage boundary and records the next RGB address-control limit.
