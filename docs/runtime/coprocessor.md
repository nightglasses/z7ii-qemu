# TL4-labelled coprocessor startup boundary

## Evidence scope

This is original MAS consumer/caller evidence, not accepted reconstruction
SOURCE/ABI, physical IP identification or a recovered processor ISA.
Original C1.70 MAS bytes use load base `0x10020000`, SHA256
`a218daf0234cebc11ed3b9277208a884eace242a32af3a60ef25081ce6221ae6`.
Raw listings, byte checks and observations remain under ignored
`work/research/bridgeports/` and `work/research/guiresume/coprocessor{1..6}/`.

All six eight-second observations use the same dirty-worktree ARM binary
`3848f3146a462f0649e586d548d1a264f1424949c17a67be24979eb8d06fdae9`,
unchanged complete bundle, functional, MTTCG, and explicitly enabled
[experimental notification profile](../devices/bridge/protocol.md).
Actual notification properties are true in both domains. They capture 13 COPY
requests and byte-exact loader extents, then the same first rejection:
MAS PC `0x1020b4da`, WORD write `0` to PA `0x2c04000c`.
There are no debugger writes/calls, child-constructor or graphics-transfer
observations. Fresh strict `coprocessordefault1` records both propertiesfalse,
zero COPY and failed loader comparisons, still stopping at `0x2013e000`.
This study does
not promote the experimental bank/reset/IRQ assumptions or deliver a frame.

## GUI lifecycle ordinal qualification

Phase3 must not be called teardown merely from nearby facade addresses.
Following the original veneers shows:

- `10360bb6 -> 103ba534` stores a callback field and returns1, not task destruction.
- `10360bf0 -> 103f604c` stores another field, not object release.
- `1036089a -> 1048d87c -> 10694f1c` binds indexed callbacks0/1.
- `1036086a -> 103fd05a` binds another indexed callback.

Runs5/6 observe the component phase3 return at `1020d58e`, r0=1, before
`1020b4da` executes the rejected write. Run6 additionally observes both
indexed bind returns0 and reads two actual `10204db5` callback pointers at
`b181b418`. These are real guest stores, not debugger registration. The
provisional teardown interpretation is therefore rejected for these calls.
This qualifies completion of this component's callback-configuration routine,
not all global phase3 participants, rendering, transfer or a GUI frame.

## Normal caller and program identity

Original RTOS object strings are exactly `TL4MTX1 ` and `TL4FLG1 ` at MAS VA
`0x10371e80/0x10371e90`; the M/F labels must not be interpreted as two physical
processor types. Other original strings include `TL4_0` and `TL4_1`.
They identify a software family, not a manufacturer/ISA binding.

The directly observed chain is:

```text
10371c90 -> 10371bee: validate and load the program resources
10371ca2: guest signals handle b1809774, mask200
10371cbe -> 10361116: wait on the same handle, mask200, timeoutfa0
10371cc2: returns1, output word200
10371e20 -> 10371b32: enter the subsequent startup lifecycle
1020b574: bind callback10371ae9
1020b316: prepare the control object
1020b4d6 -> 1020b4da: write the explicitly constructed parameter byte0
```

The live control object at `b1806ec4` has logical registry ID `14c`; its child
at `b1806f08` has ID `155`, packed selector `01ff0002`. The stack argument is
word1 followed by byte0, built by `10371b3e/10371b40`. Thus the first failed
value is not an arbitrary corrupt parameter. This does not prove the
experimental notification input matches physical hardware behavior.

`10371bee` reads the bootstrap halfword at source+4, follows its alignment
branches, reads entry word `28002060` at VA `122d486c`, doubles it to
`500040c0`, and requires strict bounds `(50004000,50004200)`.
It also compares the final sixteen bytes of these three original resources:

| Resource role in this consumer | Source VA | Length | Functional destination |
|---|---|---|---|
| Bootstrap overlay | 122d4750 | 3a50 | 50000000, after the program copy |
| Program resource | 122d81a0 | 57e30 | 50000000 |
| Data resource | 1232ffd0 | 69e70 | 50080000 |

The original entry condition and all three footer comparisons pass an
independent byte-reader evaluation. This evaluates the ARM consumer, not the
coprocessor instruction stream. It binds the two 500* extents to this software
startup family; it does not establish physical backing/reset/capacity or
framebuffer ownership. The observer's historical `plane500` dump label is
not evidence that the resource is a display plane. The separate 502* object
is not assigned to this family by proximity.

## Historical control boundary before the request model

The fixed literal at MAS VA `1020b5e4` is `2c040000`. Except for the observed
first PA write above, the following are static effective VA operands in
future/unreached paths, not measured physical transactions or register names:

| Offset | Original bounded operations |
|---|---|
| 00/04/08 | Boot/reset sequences store 0 or1; meanings/reset law unresolved |
| 0c | Word store of caller byte at argument+4; observed value0 |
| 10 | Replace bits0/16/24; set/clear bit17; `LSLS#15/BMI` polls bit16, not17 |
| 14 | Store supplied program address on the non-copy branch; clear on the copy branch |
| 20 | Modify bit0 or bit16, then poll24 |
| 24 | Read low bits0/1 and bits16..23; quiescent checks compare the latter with4 |
| 30 | Read bits0/8 into the output structure |
| 40 | Read status fields; RMW caller bits0/8 |

`1020b38a` receives program address `50000000` and a copy-selector argument.
Its copy branch invokes the ordinary memory-copy helper with VA `04294000`,
source program address and size4000, then clears14. None of the six pre-model observations reaches
this branch: the earlier0c write aborts in that binary. This does not qualify an
instruction-memory provider or permit automatic execution/completion.

The same routine calls system-config helper `103c5d86` with selector8a. Its
actual arithmetic selects request word `04280088`, bit10; it is covered by
the existing [system-config interface](../devices/sysconfig/protocol.md).
Selector138 is not independently established as GIC IRQ138, a frequency or
physical reset/clock effect.

## Startup marker and real message source

Before initialization, `10371e38` clears word `50080000`. After initialization,
`10371e44..10371e54` compares it against `53535353`, sleeping through the
original software delay up to ten iterations. Neither the boot operation nor
this ready poll was observed in those pre-model runs, because configuration
aborted first. The marker's
real writer remains unqualified; it must not be seeded as an input or host ACK.

There is also a real timeout branch: it calls the stop helper, clears frontend
flags, and reaches shared cleanup. Success and timeout converge before the
same frontend field at+20 is set to1; that store alone is not success proof.
Consequently these consumers
alone do not prove that GUI output requires successful coprocessor execution;
the natural failure/fallback path must be checked separately, not forced by a
constant status4 or an injected marker.

The original 25-pointer record at `1078aa58 + 2*100` and the live child selector
join this frontend to [command bank2](../devices/command/protocol.md): effective
root `2c041000`, two logical slot statuses, eight engine windows and an
ownership bitmap. Existing board mapping and IRQ table use247/248, not the
system-config selector8a. The controller block and message-window block are
separate interfaces.

`1020b51e` claims an engine, writes control3/parameter4 and actual counted
words, starts, waits for the guest event and releases. Callback `1020b262`
handles slot0 causes/ACK and slot1's nine actual words, then invokes the bound
`10371ae8` dispatcher. None is a recovered processing algorithm or permission
to manufacture the nine-word reply. The current command model already retains
real requests without completing them when no provider is connected; sharing
a numeric window between MAS and SLA does not establish a route.

## Request-only model and natural timeout observation

The later [bootstrap request model](../devices/processor/protocol.md) retains
actual caller configuration and bootstrap bytes without executing a core or
producing status, ready words, replies or guest IRQs. Its cancellation and
storage semantics are an explicit bounded functional contract, not recovered
physical reset/ACK law.

Two independent twelve-second `processorfinal1/2` observations use ARM SHA256
`ecb020af61b554c65d075640b6403095833e17b1b255eea812783a6d1d57a85f`,
the complete original bundle, functional, MTTCG and both experimental
notification propertiestrue. Each records14 COPY calls and three exact
loader extents. Each also verifies the actual 16 KiB bootstrap view against
its source, SHA256
`86370616c1195355141240a097a400f194604bb540256d34a9552bd072e39878`.
The original caller issues its run request, performs ten marker polls,
reaches timeout stop `10371e5c` and shared cleanup `10371e70`, then enters
graphics configuration `106ee04a`. Final `request-pending` isfalse.
This demonstrates natural continuation after processor timeout on this
experimental path; it does not demonstrate core execution or a CPU-only
rendered frame. The `COPROC_INIT_DONE` observer label at shared cleanup must
not be read as processor success.

After the finite empty-table configuration fields are supported, the next
first rejection is MAS PC `106ee3c6`, WORD write `bd0dff90` to PA `201810d4`.
The live original descriptor contains two source/extent pairs:
`bd0dff90/38` and `bd15ff90/1a6`. These were historically interpreted as56
and422 bytes and the register counts as halfwords; the read-only extent
qualification below supersedes both interpretations (full224/1688 bytes,
64-bit register units). `106ee04a` requests at offsets `10d0/10e0`.
At this pre-stream boundary their upload/processing and completion law was unimplemented;
merely accepting pointer writes
or generating IRQ129 would not implement it. There are still no child or
ordinary graphics-packet hooks, nor a natural first frame.

The matching strict `processordefault1` run retains both notification
propertiesfalse and the `2013e000` rejection, zero COPY and failed loader
comparisons. Its all-zero bootstrap equality is vacuous: no copy/request was
observed and it is not acceptance evidence. COPY-only telemetry and its
`NO_FAULT_OBSERVED_NOT_GUI_ACCEPTANCE` gate must not hide a separate uncovered
TRANSFER transaction.

## Actual stream capture and independent display boundary

The later [graphics request frontend](../devices/transfer/protocol.md) captures
actual source bytes but does not interpret or complete a graphics command.
Two twelve-second `graphicsstream1/2` runs use ARM SHA256
`df46dc78f70c5de5b503a71d39e4614e37cffb9b53e36841285ed147af0dfd96`,
complete bundle, functional, MTTCG and both notification propertiestrue.
They retain14 COPY calls, three exact loader extents and the original processor
timeout path. Both frontend byte hashes match guest-memory prefixes read
with the same assumed lengths, not independently established full extents:
56-byte stream0 prefix
`0080399088dd39b3d9cb98cb95c84b752b220cc2ec732ebc0283cdbbc9c4f4dd`,
422-byte stream1 prefix
`d25b94bcac8874727f96d9b845bbd20a5783767e5162e766e7131c99dfbbfb46`.
Both remain pending with tokens1/2. This is historical prefix transport
ownership, not full-bound, processing or instruction acceptance. The shared
length assumption invalidates the earlier independent-full-input inference;
the recorded bytes/hashes and pending-state observations remain historical.

The actual subsequent `106ee1f8 -> 106ee436` wait is entered with command
ordinal10 (`r0=a`), not2. The context field set to2 at `106eeab8` is not the
caller's command ordinal. Static ordinal10 completion checks software bit0;
if status58 bit26 is set, it requests1214=01000000 and polls the same bit.
None of this establishes the missing producer or permits constant idle status.
Neither a wait return nor IRQ129 handler entry is observed. The observer's
old `TRANSFER_COMPLETION` entry label is now distinguished as
`GRAPHICS_WAIT_ENTRY` for non-copy commands; it never proves completion merely
by reaching a waiting function.

Meanwhile another MAS task independently reaches the first rejection at
PC`1046e19a`: WORD write0 to PA`2014d200`. Original `1046e188` readsD100,
conditionally stores1 there, stores its full r1 argument atD200, calls
`103dc538`, then stores0 toD100; the already-set path storesD200 directly.
The current display device exposes only104 bytes atD000, soD200 is outside
its aperture. This software sequence does not yet name the parameter's
physical effect, reset or legal values. It is a separate active gap, not
proof that the only remaining dependency is a graphics IRQ.

The matching strict eight-second `graphicsstreamdefault1` remains blocked on
MAS/SLA2013e000, captures zero COPY/failed loader extents and has no pending
graphics streams. Neither experimental run observes a child constructor,
ordinary graphics packet or natural first frame. Native front-end tests and
experimental progression do not qualify the notification assumptions.

## Superseding read-only stream extent qualification

Parent evidence `work/research/guiresume/graphicsextent1/gdb.log` follows
original descriptor routine MAS VA `1068193e/10681976`, packet-ready
`1064d2d4`, configuration `106ee04a` and register writer `106ee3c4`.
The descriptor output identifies32-bit cursor words, not byte lengths:

| Stream | Source VA | Descriptor words | Register count (64-bit units) | Full byte extent |
|---|---|---|---|---|
| 0 | bd0dff90 | 56 (`38`) | 28 (`1c`) at10dc | 224 (`e0`) |
| 1 | bd15ff90 | 422 (`1a6`) | 211 (`d3`) at10ec | 1688 (`698`) |

`GRAPHICS_WORD_INPUT` reads the full sources and reports hashes
`dfab760ac62e7517cd579df15f45f8f9b77ba701ddd16db336c667ac2d65965c`
and `230a6b145f165a123d8f3234d8422df6eca00429def03b3e63c591d64756e01d`
respectively. These are read-only probe observations, not corrected frontend
outputs or new hash gates. The old56/422-byte dumps are prefixes; agreement
between a frontend and a probe using the same mistaken length is not
independent full-bound proof. Historical `graphicsstream1/2` receipts are
preserved above with that limitation explicitly superseded.

At this read-only qualification stage the frontend still computed `2*count`,
not the observed `8*count`; that probe alone added no native/runtime PASS.
The corrected capture and its separate receipts follow below. Neither stage
provides processor execution, graphics processing, fence, status58 or IRQ129,
resolves independentD200 or promotes default-off notification hypotheses.
Strict/defaultPA2013e000 rejection remains.

## Corrected full stream capture without processing completion

The parent accepted count-unit correction `8e113e58` computes `8*units64`
within the unchanged functional 128-KiB bound per stream. Its actual native
red→green and full-suite receipts are recorded in
[native validation](../validation/native.md#corrected-stream-count-unit-revalidation),
separately from the earlier prefix receipts and read-only study.

Two independent twelve-second complete-original-bundle observations,
`work/research/guiresume/extentfixed1/2`, use ARM SHA256
`0398c59a4da249213e15e475ef8ccc9e8b763ccbc758d315162be823b1f688ad`,
functional, MTTCG and both actual notification propertiestrue. Their
receipts retain dirty source identity and actual argv; these are explicitly
experimental notifications, not strict/default or physical qualification.
Each records 14 COPY calls, three exact loader extents and byte-exact 16-KiB
bootstrap storage, ten ready-marker polls, natural timeout stop and cleanup.
No processor success is inferred.

Both follow the original descriptor's 56/422 32-bit words into register
counts of 28/211 64-bit units, hence full 224/1688-byte sources. The device-owned
stream SHA256 properties match the producer-bounded `graphics-words0.bin` and
`graphics-words1.bin` inputs in each run:
`dfab760ac62e7517cd579df15f45f8f9b77ba701ddd16db336c667ac2d65965c`
and `230a6b145f165a123d8f3234d8422df6eca00429def03b3e63c591d64756e01d`.
These now describe full capture by the corrected frontend, not only probe
observations; no new hash gate is introduced. Both streams retain pending
tokens1/2. The original graphics wait is entered with ordinal10, but no wait
return, IRQ129 handler or natural first frame is observed. Full transport
ownership is not graphics processing, fence or completion.

Another MAS task writes WORD zero to PA`2014d200` at PC`1046e19a` while D100
is asserted. Persistent Ghidra analysis plus a raw Thumb direct-call scan finds
`1044a4c4` as the sole direct caller of `1046e188`; it supplies r1=0, and the
callee clears D100 after the existing delay wrapper. The finite virtual
contract therefore admits only zero at D200 and gives it no event, IRQ or
completion effect; nonzero values remain errors. The paired raw wrappers
`1044a4ca -> 1046e3b8` and `1044a520 -> 1046e3ac` similarly bound C0A0 to its
observed zero/one bit0 writes. Their physical meanings and reset values remain
unknown.

Static Thumb recovery also corrects the previously ranked image-start
hypothesis. `1075b7ce -> 10446878` registers callback `10445d1b` with literal
descriptor string `tftdbg`. The sole exact producers of event types
`42060002` and `42060004` are in that handler; its dispatch-state transitions
lead through `10448f92`, `104498a8`, `1044a134` and `10449fc6` to the
`1046bfxx` image-start wrappers. This is a debug-command path, not evidence of
a natural boot-frame producer. The real startup frontier remains the
`20180000` graphics processing/status/IRQ chain.

The same binary's strict eight-second `extentdefault1` records both
notification propertiesfalse, zero COPY calls, three failed loader comparisons
and no pending streams, still rejecting MAS/SLA PA`2013e000`. Its zero-filled
bootstrap equality is vacuous: no copy/request is observed. No default boot,
notification hardware law, processing provider or first frame is accepted.

## Z9 reference and remaining qualification

The user-requested reference is
[`wonderlantern/z9-qemu`](https://github.com/wonderlantern/z9-qemu/tree/deb3a75df1795924e9be8f814d9a24ce6b439a67)
at fixed commit `deb3a75df1795924e9be8f814d9a24ce6b439a67`.
Its four-context AArch64 board and explicit bootstrap/service/peer fixtures
are not a matching Z7II ARM board. It offers a useful separation between
configuration/reset lifecycle and actual processing/completion, but no
qualified replacement for this controller. No Z9 addresses, status values,
synthetic inputs or service-core patches were transplanted.

Next statically close nonempty graphics processing and its actual completion
producer, then continue to real rendering/scanout. C010 and PA`2c012000`
remain strict unresolved boundaries; no external initial-value profile is part
of the product model. The opaque byte frontend is not a processing implementation. Processor execution,
bit17 acknowledgement, other control/status paths, the real `53535353`
writer and command provider still remain unqualified. Natural timeout
continuation removes the earlier argument that full processor ISA execution
must precede all GUI progress; it does not prove execution is unnecessary
for every display operation. Do not manufacture ready/status, replies, IRQs
or framebuffer data to make the next boundary pass.
