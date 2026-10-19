# Native GUI integration

> Status: this file mixes historical receipts with the current green native
> checkpoint. No original-firmware GUI startup is claimed. The latest green
> native checkpoint is the 32/32 receipt near the bottom; the EX 0x6b read wait
> is a WIP frontier detail, not a completed camera boot. Receipts below are
> historical unless explicitly labeled current.

The integration uses QEMU 11.1.2 at upstream commit
`4fc49f46dc95d4a27de2509e7fceb2931e91faeb`. The configured development
build is `work/build/qemu`; its source is the ignored `work/config/source`
view composed from the root `qemu/` submodule. Maintained Nikon source lives in
the parent repository's `hw/nikon`, `include/hw/nikon` and `tests/*/nikon`; the
current official dependency is the read-only root `qemu/` submodule.

The historical `106a0ce` build compiled and linked the complete six-class
storage profile, five-component bundle input, GPIO, completion, HDMAC, bridge
and packet transfer. The original complete bundle of that build entered GUI
initialization phases 1 and 2 and the GUI message task, then stopped on an
unsupported USIO byte write. The corresponding original-run record is linked
from [storage](../runtime/storage.md); it does not establish visible GUI
output and is not current-release evidence.

The historical cases below passed across the initial batch and its necessary
corrections:

| Cases | Result | Local evidence under `work/validation/gui` |
|---|---|---|
| Memory class boundaries, independence, decode retention and holes: 4 | PASS | `memory.log` |
| GPIO explicit 192-pad input, completion domains and bridge events: 3 | PASS | `devices.log`, before its transfer failure |
| Packet byte copy, cancellation and equal-plane geometry: 3 | PASS | `transfercorrected.log`, 1.49 seconds |
| DMA pause, halt, cancel, reset, reentrant bus writes and Audio: 7 | PASS | `dmacorrected.log`, 2.06 seconds |
| Bundle parsing and invalid input rejection: 2 | PASS | `bundle.log`, before its VR record failure |
| VR record extraction and bounds: 1 | PASS | `recordscorrected.log`, 0.33 seconds |

The original 16 passing cases were not rerun. These 20 historical cases validate
native functional contracts; they are not measurements of camera hardware or a
claim that the complete camera boots.

The first Make invocation compiled the system and main qtest executables, but
its extra explicit parallel qtest targets selected an upstream implicit Make
rule and failed. The build continued with the upstream Ninja targets. The
initial device test failures had two independent causes: `-S` disabled virtual
timer execution in qtest fixtures, and a signed `ldl_le_p()` value was extended
while comparing VR addresses with `size_t`. Tests now observe actual board GIC
inputs instead of accessing the secure guest CPU interface. Each failed run
remains in the ignored logs; only the affected cases were repeated.

The next integration at `f44817e` also compiled both native ARM and RX system
targets and passed only its six new or previously failing cases:

| Cases | Result | Local evidence under `work/validation/gui` |
|---|---|---|
| USIO transmit, receive, idle, flushing and disconnected nine-bit lanes: 4 | PASS | `usio.log`, 1.44 seconds |
| Source-derived paired transfer planes: 1 | PASS | `planes.log`, 1.00 seconds |
| RX TCG reset, caller stack, EXTB read/write and relocated undefined exception: 1 | PASS | `vrcorrected.log`, 0.91 seconds |

The RX target is the existing upstream TCG implementation with explicit EXTB
support. Its first native fixture had its register-store operands reversed;
the corrected synthetic instructions and paused TCG launch establish the
intended CPU behavior. Original RX execution separately revealed the TLB's
page-offset requirement for unaligned RAM extents. Private backing with an
offset alias preserves each exact mapped extent without adding padding or
changing guest bytes. These results do not establish complete original RX
firmware execution, device peers or visible camera output.

## Native source graph before bridge ISR correction

The `f02f0c43cd4da62b3a469717accd86e03d540f0c` worktree with the local
section-bounded transfer and test changes builds from the maintained root
`qemu/` dependency through `work/config/source` into `work/build/qemu`.
`make check MTESTARGS='--print-errorlogs'` passes all 27 Nikon fixtures,
239 subtests, with no failures. Raw output and the structured Meson record
are retained as `work/research/guiresume/nativecheck3.log` and
`nativecheck3.json`. The official upstream submodule remains unchanged.

The new transfer unit boundary test fails its byte assertion on the old
implementation (`transfer-red.log`) and passes after the fix
(`transfer-green2.log`). It verifies switching from a short ROMD alias to
adjacent RAM/ROMD, not copying hidden backing bytes, and rejecting inaccessible
sources, read-only destinations and MMIO without successful completion or
read side effects. The existing transfer qtests also pass in the full run.

The first full build exposed the pre-existing save unit fixture's missing
`qemu_main` symbol on Darwin; its first runnable attempt then failed with
unknown QOM type `nikon-save`. The fixture now uses the same machine-none,
qtest QEMU initialization and cleanup as the other device unit fixtures.
All three original assertions pass without changing the save device contract.
Those failed attempts remain in `nativecheck.log` and `nativecheck2.log`.

The rebuilt ARM binary SHA256 is
`13158f95a0c3313ee9d177f50ed1d55ce9a5a317a67e3bab106736fab71fc764`.
Two separate bounded complete-bundle runs pass the observed COPY and loader
byte checks but fail the transfer-producer/GUI-first-frame gate. The current
[EV3 wait boundary](../runtime/wait/ev3.md) records the actual unsatisfied
mask and scope. Native synthetic tests do not establish camera boot or GUI
acceptance.

## Bridge ISR correction revalidation

The same base `f02f0c43cd4da62b3a469717accd86e03d540f0c` with uncommitted
transfer, fixture and bridge changes again passes all 27 Nikon fixtures,
239 subtests with `make check MTESTARGS='--print-errorlogs'`. The final raw
and structured receipts are `work/research/ev3producer/nativecheck_final2.log`
and `nativecheck_final2.json`; the upstream submodule remains clean.

The bridge tests first fail on the old configuration IRQ cause
(`0x40 != 0x20`, `bridge-red.log`) and third peer-event mapping
(`0x20 != 0x10`, `event-red.log`). They pass after correcting both original
ISR sign-test bit numbers, without changing timing, reset, masking or W1C
expectations. The maintained bridge change contains no additional RX-word
backing or peer-bank assumption.

The final ARM binary SHA256 is
`f2512544ef07e70b703574fbb52e2a491bc5d43386ec17de55cf84ca030f2dbc`.
Two independent eight-second original complete-bundle runs confirm natural
bridge flag4 production and configuration-wait return, then reject the word
read at PA `0x2013e000` in MAS and SLA. Both final COPY/loader-byte and
transfer/first-frame gates fail; there are no observed COPY submissions.
The previous 14-COPY PASS belongs to its older binary and is not carried
forward. A withdrawn paired-word experiment remains ignored research only.
The [current EV3 frontier](../runtime/wait/ev3.md) separates these identities
and the still-unclosed getter/publisher contract. GUI acceptance remains
undelivered.

## Notification experiment and I2C divisor revalidation

The uncommitted native source graph now passes 28 Nikon fixtures and 242
subtests with `make check MTESTARGS='--print-errorlogs'`.
`work/research/bridgeports/nativecheck_final2.log/json` hold the final raw and
structured records; the official QEMU submodule remains unchanged.

The added bridge unit fixture first rejects the old unsupported flag read,
then verifies local set/clear masks and the default-off experimental connected
profile. It covers masking/W1C, duplicate and zero sets, both directions,
invalid accesses, reset and disconnect. These synthetic tests validate a
declared model, not the hypothesized notification wiring/reset/firstword.

The I2C divisor qtest fails on the old full-control-word guard (`0 != 4`),
then passes for the same finite TX/RX modes with clock bits across all four
empty buses. It expects address NACK, not fabricated application success;
a different unsupported monitor mode remains rejected. The native build and
active LSP probes confirm that initial missing-QEMU-header/GLib diagnostics
were parser-environment false positives, not code fixes to bypass failures.

The current ARM binary SHA256 is
`3848f3146a462f0649e586d548d1a264f1424949c17a67be24979eb8d06fdae9`.
Two separate original complete-bundle, functional, MTTCG eight-second
observations explicitly enable `notification`. Both enter GUI phase3 and
pass all three loader-byte comparisons; they capture 11/13 COPY requests and
no COPY first fault. They then reject WORD write `0` at PA `0x2c04000c`, MAS PC
`0x1020b4da`. Neither captures a child constructor or graphics transfer, and
both fail the first-frame gate. This is conditional software-path progress,
not default-profile boot or qualification of the experimental hardware laws.

The same binary's default run confirms both notification properties false,
rejects `0x2013e000`, captures zero COPY requests and fails the loader checks.
Ignored receipts `work/research/guiresume/{flagsi2c1,flagsi2c2,flagsdefault2}`
record each connection state, hashes, dirty source identity and bounded exit.
No GUI first-frame acceptance is claimed; the connected results do not
replace unconnected failures or the historical identities above.

## Read-only coprocessor startup qualification

Six later eight-second observations `coprocessor1..6` use this same binary,
complete bundle and the composed notification mapping. Both actual
notification properties are true in each receipt. Each captures 13 COPY submissions,
three byte-exact loader extents and the same rejected `2c04000c` write.
None observes a child constructor, graphics transfer or frame. No emulation
C, hardware reset/status behavior, backend, ISA decoder or ready producer was
added; the native suite above was not rerun merely for these research hooks.

The last two observe GUI component phase3 return1 before the controller
fault. The last also observes indexed callback bind returns0 and both actual
guest pointers in the callback array. Following the facade veneers rules out
the provisional interpretation of those calls as task destruction. This
qualifies a particular callback-configuration routine, not all global phase3
participants or framebuffer production.

Original byte checks corroborate the bootstrap entry arithmetic and matching
program/data footers; the ARM loader's mask200 signal/wait/return is observed.
This is program transport/validation, not coprocessor execution or a boot
marker produced by hardware. The [startup boundary](../runtime/coprocessor.md)
retains that pre-model unseen ready poll, original timeout path and
still-unqualified physical controller/provider law. A fresh run without the
connection, `coprocessordefault1`, records both properties false, zero COPY,
three failed loader comparisons and the unchanged `2013e000` rejection. All raw receipts and scripts remain under `work/`.

## Bootstrap request boundary and timeout continuation

The subsequent request-only processor device and finite TRANSFER setup
extension are separate from the earlier study and binary. Native bootstrap
and empty-configuration tests fail on the old rejection behavior, then pass
without ready words, automatic acknowledgements, processing replies or guest
completion IRQs. Negative halfword access is correctly checked as dispatch
`MEMTX_DECODE_ERROR`, not a device callback error.

A complete native check initially exposed `nikon_memory_unit` owner/region
teardown: QOM assertion in deferred flat-view destruction. The fixture now
explicitly removes mapped siblings while their embedded storage and owner
are alive, drains retired views, then releases ownership. Production DMA
behavior and all boundary/permission assertions are unchanged. Five isolated
repetitions and the complete rerun pass. This is fixture lifecycle repair,
not original-firmware red→green. Native host GDB bus-error and LLDB launch
incompleteness are inspection failures, not guest failures.

`make check MTESTARGS='--print-errorlogs'` passes29 fixtures /245 subtests,
zero failures, receipt `work/research/z9reference/nativecheck-green.{log,json}`.
The ARM binary is
`ecb020af61b554c65d075640b6403095833e17b1b255eea812783a6d1d57a85f`.
Two independent complete-bundle twelve-second experiments,
`processorfinal1/2`, verify14 COPY calls, three loader extents and the actual
16 KiB bootstrap bytes; both observe ten original marker polls, natural stop
and shared cleanup, then graphics configuration. Pending processor ownership
isfalse at the bounded snapshot. Actual notification properties aretrue in
both domains; this is not default/physical notification qualification.

Both experiments first reject a nonempty graphics source write at
`201810d4`, value`bd0dff90`, PC`106ee3c6`. The live descriptor extents56/422
were then interpreted as bytes; the later read-only qualification below
establishes words and full224/1688-byte inputs. Upload/processing/completion
is not implemented. COPY-only `first-fault=null` does not mean global fault-free boot. The observer now
records uncovered transactions separately. No child, ordinary graphics
packet, scanout or natural first frame is accepted.

The same binary's unconnected eight-second `processordefault1` retains
notification properties false, zero COPY, failed loader comparisons and first rejection
`2013e000`. Its zero-filled bootstrap/source equality does not prove a copy.
The [processor contract](../devices/processor/protocol.md) and
[runtime evidence](../runtime/coprocessor.md) distinguish ownership/storage,
real timeout continuation, missing execution and the new graphics boundary.
The fixed Z9 reference supplied design context only; its AArch64 addresses
and synthetic service/peer fixtures were not ported. Final active LSP checks
are clean for six changed C/header paths; machine and transfer-fixture probes
lack `qemu/osdep.h` in their standalone include context and produce cascading
unknown-type diagnostics. Those are environment limitations, not an all-path
LSP-clean receipt; no fake includes were added. Native compilation, the full
suite and `git diff --check` pass; the official submodule remains clean.

## Owned graphics streams without processing completion

A later finite frontend admits the original two source/count/start triples
and captures real bytes, but leaves them unprocessed. These are historical
receipts under the superseded halfword-count assumption, not validation of
full original extents. Native red `stream-red.log` rejects10d4; green tests
verify real56/422-byte immutable snapshots, independent pending
ownership/tokens, negative transactions,
side-effect-free MMIO rejection, reset/unrealize cancellation and absence of
all three GIC IRQs. ROMD-boundary tests also verify section-exact stream
capture. The first build attempt lacked the newly added header in the ignored
source graph; `make prepare` resolved this infrastructure failure before the
behavioral red run. It is not firmware red→green.

`stream-nativecheck.{log,json}` records29 fixtures /246 subtests, allOK.
Active LSP probes confirm the three changed implementation/header/fixture
paths clean. The unchanged machine path retains its earlier standalone
include-context limitations; no machine code or official QEMU source was
modified for this stream extension.

Two independent twelve-second `graphicsstream1/2` observations use ARM
`df46dc78f70c5de5b503a71d39e4614e37cffb9b53e36841285ed147af0dfd96`
and actual notification propertiestrue. Both retain14 COPY calls, exact
loader/bootstrap bytes and the natural processor timeout. Both submitted
stream hashes match guest-memory prefixes using the same assumed56/422-byte
lengths; this is not independent full-bound proof. Both are pending at the
snapshot. The original graphics wait is entered with ordinal10, with no
observed return or IRQ129 handler. The distinct first rejection is another
MAS task's WORD write2014d200=0 at1046e19a. Thus there is both missing graphics
processing/completion and an independent display-parameter aperture gap;
these receipts do not isolate a single sole wait dependency or prove a frame.

The same binary's unconnected `graphicsstreamdefault1` retains notifications
false,
zero COPY, failed loader comparisons, no pending streams and2013e000
rejections. The [runtime evidence](../runtime/coprocessor.md) records the
actual stream identities, corrected command ordinal and separate display
boundary. All raw byte dumps, watchpoint receipts and original listings
remain ignored under `work/`; no command arrays or CPU/ISA decoder were added.

## Historical read-only stream extent qualification

The later parent probe `work/research/guiresume/graphicsextent1/gdb.log`
establishes descriptor extents56/422 as32-bit words and register counts28/211
as64-bit units, hence full224/1688-byte sources. The old56/422-byte captures
are prefixes, not independently validated full inputs. The
[runtime qualification](../runtime/coprocessor.md#superseding-read-only-stream-extent-qualification)
records the actual descriptor/port observations and full-word input hashes;
at this stage those hashes were observed only by the read-only probe, not a
fixed frontend. The historical29-fixture/246-subtest PASS above remains a
receipt for that binary and its synthetic old-length contract, not full-extent
acceptance. The separate corrected implementation and receipts follow.

## Corrected stream count-unit revalidation

The parent accepted `8e113e58` changes capture to `8*units64`, preserving the
functional 128-KiB limit and unprocessed pending ownership. The actual native
red `work/research/z9reference/extent-red.log` fails the updated submission
comparison on the old implementation (`56 != 224`). `extent-green.log` then
passes all four transfer unit cases, including full 224/1688-byte snapshots
and `stream-extent-boundaries`. The latter checks both stream banks, finite
count/access/setup guards, the exact allocation limit and over-limit counts,
and whole-extent MMIO preflight without read side effects or guest IRQs.
Existing reset/unrealize and ownership-isolation checks remain intact.

The focused memory fixture now also checks the finite source-bounded display
scalars. D200 accepts only the sole direct caller's observed zero while D100
remains independently retained; C0A0 accepts only the paired zero/one bit0
writes. Reset returns the virtual retained fields to zero, nonzero D200 and
other C0A0 bits fail, and table offset zero remains independent of the C0A0
slot. These checks establish neither a physical reset value nor any event,
completion or IRQ effect.

Persistent Ghidra Thumb marking and raw-byte verification further show that
the previously ranked `1046bfxx` image-start route is downstream of the
registered `tftdbg` handler. It is excluded from natural-startup acceptance.
No modeled graphics completion or external C010/control initial-value profile
is included in this validation scope.

The parent incremental build and
`make check MTESTARGS='--print-errorlogs'` receipt
`work/research/z9reference/extent-nativecheck.log` passes 29 fixtures / 247
subtests, zero failures. Parent active root LSP probes are clean 3/3 for the
transfer implementation, header and unit fixture. These are scoped native
contract/build checks, not firmware or physical-camera red→green. No
additional native run accompanies this documentation-only correction.

Two separate twelve-second original complete-bundle experiments,
`extentfixed1/2`, use ARM SHA256
`0398c59a4da249213e15e475ef8ccc9e8b763ccbc758d315162be823b1f688ad`
with functional, MTTCG and both notification propertiestrue. Each retains 14
COPY calls, three exact loader extents, actual 16-KiB bootstrap equality and
ten marker polls followed by natural timeout/stop. Device stream hashes match
producer-bounded full 224/1688-byte inputs; the
[corrected runtime receipt](../runtime/coprocessor.md#corrected-full-stream-capture-without-processing-completion)
records source/count units and actual hashes. Tokens1/2 remain pending at the
ordinal10 graphics wait with no observed wait return, IRQ129 or frame.

The first uncovered access is still WORD write0 to PA`2014d200` at
MAS PC`1046e19a`. The new hooks observe caller VA`1044a4c4`, r1=0, and the
already-loaded D100 value at VA`1046e190`, r0=0, without debugger MMIO reads or
bypass. This is parameter/caller evidence, not a D200 implementation or
permission to admit zero as a discard. The same binary's unconnected eight-second
`extentdefault1` keeps both notification properties false, zero COPY, failed
loader comparisons, no pending streams and PA`2013e000` rejection; its
all-zero bootstrap equality is vacuous. These corrected capture receipts do
not provide graphics processing/fence/status58/IRQ129, processor execution or
independentD200, and do not promote notification hypotheses or accept GUI
startup. No self-designed hash gates or fabricated success were added.

## Profile-selector removal revalidation

The `04ec65c1` selector removal (old `7d055a2a`) deletes the machine-level
`functional-profile`, `nor-profile` and `cmos-provider` selectors. The
complete bundle now selects the composed board path; raw `-bios` remains a
separate MAS-only diagnostic input. The following EX I2C cleanup removes the
dead `last_write` field and the duplicate bundle-input selection call, and
the controller uses ACK/STOP-bounded FIFO progress instead of a per-address
receive-length table.

`make check` passes all 32 Nikon executables with zero failures; the raw
record is `work/build/qemu/meson-logs/testlog.txt`. The rebuilt ARM binary
SHA256 is
`b3d97591b347fad34e78140e86947fb275dc6daecf5c04c295b6f9a397303fe4`
(historical selector-removal checkpoint `04ec65c1`/`01daed54`).
The previously failing `/arm/nikon/ex/io` three-byte expander read passes.
These are native contract/build receipts; they do not establish
original-firmware startup or GUI output. The older 29-fixture/247-subtest
receipts above remain historical.

## EX 0x6b/0x0d device-model correction

The EX-side IDB shows that `0x0802b014` writes one descriptor byte under
command 0/1/3, reads it back, and requires equality after the mode mask. The
descriptor builders `0x08028ed8` and `0x08028cf0` supply zero or `0xa0`, not
fixed `1/3/1`. The `0x6b` model was therefore changed from a constant
responder to a register file: setup writes are echoed on the next read, and
command `0x0b` remains a separate ready status of `1`.

The `0x0d` model now exposes an explicit class register: class 0 `0x00`,
class 1 `0xa9`, class 2 `0xc9`, class 3 `0xe9`. Class 1 is the bundle
default, the `class` QOM property is a board input, and reset preserves it.

The focused `/nikon/ex/power-devices` case covers write/read equality for
commands 0/1/2/3, the `0x0b` finite ready/zero response, the `0x0d` class
table (0/1/2/3) and reset-preserved class selection, the out-of-range command
NACK, and reset clearing. `make check` passes all 32 Nikon executables with
zero failures; the rebuilt ARM binary SHA256 is
`05b82111753f96cd8b9c8464bf40ff2b95ba969dc52846a0d5c6f2a78df41425`
(historical 0x0d checkpoint `6a363d24`). This is a native model receipt and a
static IDB comparison, not a fresh original-firmware power-snapshot receipt.

## EX I2C STOP/RXNE revalidation

The EX I2C controller previously cleared RXNE/BTF and dropped the received
byte on a CR1 STOP. The 0x4f readback waits for RXNE or STOPF after that STOP
and therefore never returned. The controller now sets STOPF, preserves
RXNE/BTF and the received byte, and retires RXNE/BTF on the last DR read.
SR1 followed by SR2 now clears ADDR and STOPF, matching the HAL event
acknowledge. The `/arm/nikon/ex/stop-preserves-rx` qtest covers the STOP
written before the DR read and that SR1/SR2 clear.

`make check` passes all 32 Nikon executables with zero failures; the raw
record is `work/build/qemu/meson-logs/testlog.txt` and the rebuilt ARM binary
SHA256 is
`838805c46707e5b2669914d77b76a6dc4d42f8deed2a9b101986211e1d5cc231`
(release checkpoint `948f8a11`).
An original-firmware hardware-breakpoint probe on that bundle reaches the
0x6b read start with descriptor `0x2001fdcc` and its free-bus exit, and the
I2C receive handler then takes the `SR1` RXNE-with-BTF branch
(`0x08046bfe`) instead of the single-byte completion branch
(`0x08046cd0`), which leaves the driver state at `0x22` and prevents the
read-back comparison from ever running. That receive-prefetch boundary is
resolved by the [shift-byte change below](#ex-i2c-receive-shift-byte); this
checkpoint keeps only the diagnosis.

## EX I2C receive shift byte

The controller previously queued two receive bytes as soon as the address
matched, so a one-byte reader saw `SR1` RXNE together with BTF. The
acknowledge bit for a received byte is sampled at the end of that byte, so a
reader that clears ACK during the address phase never receives a second byte
and BTF stays clear, while a reader that keeps ACK does. The model now
delivers one byte at address match and the acknowledged shift byte after nine
`CCR`-scaled periods of the APB1 clock (100 kHz until `CCR` is programmed),
and cancels it when the guest clears ACK or ends the transfer.

`/nikon/ex/i2c-single-byte-read` covers the original 0x6b sequence (`SR1`
`0x42`, then `0x40`, then RXNE without BTF across a stepped clock, then STOPF
preserving the received byte). The existing `/nikon/ex/io` three-byte HAL
finish now steps one byte time before it expects BTF.

`make check` passes all 32 Nikon executables with zero failures; the rebuilt
ARM binary SHA256 is
`5157574bb9753ab2a785ba1e30b81b8d4d3d847422071fa1f2344bbdcceb9920`
(rebuild of the `0debf1e2` code together with these documentation edits).
An original-firmware hardware-breakpoint probe on the
complete bundle now reaches the 0x6b read helper's event wait, the setup
comparison at `0x0802b0d6` and its success exit at `0x0802b0e4` for commands
0/1/3 and later repeats, and the periodic `0x0d` status service at
`0x0802b6f0`, across a 300 s window. That is the first time the original
read-back equality and status-classification path runs without a fabricated
value; it is still not a complete original-firmware qualification of the
monitor service.
