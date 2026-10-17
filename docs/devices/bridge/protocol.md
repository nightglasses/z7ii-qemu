# Finite 2013 bridge-control protocol

Native `d48ac22d6c0fb82dc22038cedc5d7cfa446ffc04` adds one SysBus device at PA`20130000`, reusing upstream Clock, virtual timers and mature GIC IRQ127. The [fixed original bridge contract](https://github.com/mashiro01/nightglass/blob/8831c9d1964b7dcac4ce9971bd0dea3968fd0b2e/research/z7ii/qemu-bridge2013-contract.md) recovers reset/configuration, context selection, requests, waits, IRQ acknowledgement and five mapping arrays. That earlier bounded2190B/882-row receipt has six metadata intersections and records VA`102ddfe6` as a raw candidate without an enclosing accepted native function. The later official incremental recovery is described below. No matching physical IP has been identified.

Supported configuration/state starts at zero in this named functional profile. The complete recovered configuration word ports retain all32 bits; initializer samples are not field limits. Command, selector, access width and lifecycle constraints remain explicit. Two banks contain four contexts and two lane groups. The selector accepts a single high bit16–19, never an invented low-bit interface. `C128` selects lanes using bits0–3/12–15; selected lanes with both original configuration ports88/8c written progress from1 to3 after480 ticks of an independent48MHz Clock, a10us virtual assumption. Unselected lanes retain their state. Lane completion does not generate peer event1. Both initial`f00f` and late`e00e` requests execute this same selection protocol.

The complete recovered late methods are supported: `DE378` sets both banks' four context`AC` words to1 and bank1`C910` to4; `DE666` clears contexts1–3 `AC` to0 in both banks, requests`e00e`, waits for lane status3, then writes4 to both banks'`4120/4124`. `AC` is a retained lifecycle/configuration input; clearing it does not invent permanent lane disable or peer readiness. Their physical effects remain unbound.

The reset pulse at`2013c5e0` cancels lane, control and configuration timers and clears pending state. Routing words and board reset have separate explicit lifetimes. Control requests4/6 require the recovered routing pair and completed bank1 lane request; deferred completion sets IRQ status bit1. Request6 additionally publishes bit0 at **PA`20134908`**, the original bank0 poll address. Pending, enable and W1C acknowledgement at`2013d020/d024/d028` drive the real GIC input; the guest ISR owns event flags. Command-to-IRQ association and latency are virtual conventions, not hardware measurements.

`C13C=2` is a separate configuration handshake: it requires completed lane configuration, schedules a10us callback, and produces IRQ bit5. It does **not** require or commit the five mapping arrays. The original peer caller performs `E3B2 → E69E`, waits for GPIO to be low, then `E666 → E69E → 8816A`; both `E69E` handshakes precede `DDC76` array publication. `DDC76` writes40 independent configuration words and returns without requesting a commit. The recovered producer fills the first three arrays and zeroes the final two. Array writes/readback do not generate IRQs or peer contents. The obsolete draft/active-array transaction and its invented prerequisite were removed. The source-qualified forwarding layer below now consumes those words without introducing another commit.

Unsupported offsets, widths, values, missing configuration and commands return `MEMTX_ERROR`; migration and snapshots are unavailable. The [fixed GPIO producer](https://github.com/mashiro01/nightglass/blob/57546e1cbed483ac82dff7246c5d8dccbad02279/research/z7ii/qemu-rtos-startup-contract.md) binds service60 to GPIO port6 bit3, input with no pull, IRQ77. The original wrapper waits for that external input before its later control request. Disconnected low supplies no peer handshake; bridge status/IRQ do not change this input. No host peer signature, ready event, consumer or backing is supplied.

## Verification and original prefix

Incremental native compilation and upstream checkpatch pass, with0 errors/0 warnings. `work/issue331/bridge-lifecycle-d48ac22/result.json` binds exact native/fixtures`d48ac22d6c0fb82dc22038cedc5d7cfa446ffc04` and pinned QEMU`4fc49f46dc95d4a27de2509e7fceb2931e91faeb`:23 focused synthetic PASS in1.967728s. The whole-lifecycle fixture checks both banks'`AC=1`, `C910=4`, `AC=0`, late`e00e` completion, both`4120/4124` pairs and configuration handshake. Other cases verify array publication after handshake with no array IRQ, real IRQ127/IAR/EOI, pending lane/control/configuration cancellation, board reset with a second guest entry, and rejection of invalid state/value/selector/command/offset/width. Two negative cases read back the prior valid configuration after rejection. These are control contracts, not peer/data-transfer or original-firmware verification.

The earlier16+4 synthetic receipts against native228a7d7 remain historical. Their map-commit interpretation and its prerequisite were corrected by d48ac22. The first draft also shared a Python list between fixtures; that failure artifact was retained, its owned synthetic process terminated, and the list corrected. No historical receipt is relabeled as current proof. The full smoke entry contains each ring fixture once and asserts unique names before launching any VM.

The original run `work/issue331/original-bridge-228a7d7/{receipt.json,qemu.log}` binds **earlier native`228a7d7d804ef3c66fbabbbbc4cb96bcded81584`**: exit0,0.188778s host observation, complete direct payload equality true and whole-boot verification false. It was not repeated solely to test these later methods. Future runner metadata is corrected; its original metadata remains unchanged.

| Original trace | Observation |
|---|---|
| Lines10028–10033 | CPU0 reaches wrapper`10387e36`, original SVC SP`b51f4400`, caller LR`10200c4f` |
| Lines10159–10164 | CPU0 enters`102ddfe6`: R0=`b1806f48`, R1=0, R2=1, R3=1, SP=`b51f43f0`, LR=`10387e67` |
| Lines10559–10562 | Concurrent CPU1 first unsupported transaction: PC`1036e7f8`, WRITE32 PA`2c011604`=1, followed by original Data Abort |

The prior first store at`2013c5e0` no longer rejected that run. Initializer return and arrival at its GPIO wait are not yet established. CPU1's timing opcode is the next whole-device dependency. No original instruction, task PC, PTE, handler, flag or O178 was changed. Menu, peer startup, physical clocks and bridge transfer performance are unverified.

## External event lifecycle

The 2026-10-09 serial-group inventory uses the same fixed original contract, including its IRQ127 section. The original ISR consumes raw bits0/2/4 in addition to internal bits1/5: bit0 sets guest flag1; bit2 writes1 through bankmask2 to **PA`2013829c`**; bit4 tests mode1 and **PA`2013c908.bit2`** before setting flag8. These are source consumers, not recovered physical producers.

The new finite functional layer exposes three named `peer-event` qdev input lines, in raw-bit order0/2/4, and one independent `peer-ready` level input. A rising event edge outside reset latches that raw bit; masking suppresses the IRQ output without discarding pending, and guest W1C clears only selected pending. A held input does not retrigger after ACK. Reset cancels pending and the retained acknowledgement but preserves externally driven input levels; releasing reset does not create an edge. Cold inputs are disconnected low. `peer-ready` supplies only the read-only bank1 C908 bit2, independently of bank0 4908 completion, and does not generate an event. The ISR's aligned word write1 at3829c is a retained acknowledgement; reads, other values and writes during held reset remain unsupported. This write does not manufacture readiness or relatch pending.

Input meaning, edge choice and reset lifetime are explicit virtual interface laws. The standalone device supplies no producer for these external `peer-event` or `peer-ready` inputs. Mapping publication and acknowledgement do not drive them. This input layer creates no peer memory, frame, signature or reply; the forwarding layer below separately accesses real peer storage. Physical link behavior remains unbound. The earlier d48ac22 verification above remains historical proof of that exact binary, not validation of the later input or forwarding layers.

The whole inventory retains the existing source boundaries: physical mapping effects and arbitrary generic setter command meanings remain unbound. Known configuration ports retain full words without assigning additional command meanings. The new data-path interpretation below is a functional law inferred from the original producer and reader; actual IRQ127 dispatch and peer startup still require separate original-run evidence.

## Source-qualified peer windows

The retained official incremental source at `nightglass/work/worktrees/native-arm-exception-roots/work/incremental/closure/` now contains original decompiled C and exact Listing rows for the complete bridge family. Original native functions `102dcd5c`, `102ddc76`, `102ddfe6`, `102de336`, `102de3b2`, `102de600`, `102de666`, `102de69e`, `10387e36`, `1038816a` and `1038825e` completed without timeout, cancellation or start failure. Completed decompilation does not imply source acceptance: saved C, exact instruction ranges and physical hardware behavior remain separate. The old882-row candidate receipts above keep their original identities.

`1038816a` clears the whole160B descriptor packet, validates each24B region and emits up to eight entries. For entry i the arrays contain: `[i] = remote_base >> 12`, `[8+i] = ~(chunk_length-1) >> 12`, `[16+i] = local_base >> 12`; the final two arrays stay guest-zero. `102ddc76` publishes all five arrays through word ports50b0..514c in the selected bank. The default MAS selector0 record maps local`40000000..50000000` to remote`c0000000..d0000000`; its requested ring`c60f0000/C618` becomes local`460f0000`. The [original peer reader](https://github.com/mashiro01/nightglass/blob/57546e1cbed483ac82dff7246c5d8dccbad02279/research/z7ii/qemu-rtos-startup-contract.md) reads that local view and compares the peer header. It does not reuse the MAS sending ring at`b7cf0000`.

The device exposes two independent, unbacked local-address windows as SysBus regions1/2; region0 remains its64KiB registers. A machine maps only its source-qualified aliases, never either whole4GiB window over CPU RAM or ROM. `nikon_bridge_attach_peer(dev, bank, AddressSpace*)` borrows an executing peer's private address space, whose lifetime must exceed bridge accesses. NULL leaves the bank disconnected. No peer bytes are copied or created by attaching it.

Each window transaction resolves the current guest-published remote/mask/local triple and uses upstream `address_space_rw` for the whole access. The supported functional interpretation is aligned power-of-two chunks,20-bit page words and zero final attribute arrays. Empty, malformed, overlapping or unknown-attribute entries cannot silently select a peer; held reset or a disconnected provider returns `MEMTX_ERROR`. Downstream transaction errors propagate. This law is source-informed and has no physical IP/manual binding. Array writes do not produce completion, IRQ, a header or an extra commit. Reset cancels local descriptors and timers while preserving the borrowed connection and the peer's own storage.

The new native C cases extend the finite configuration/lane/reset coverage and use a synthetic five-component bundle plus the standard MAS machine's real private SLA provider. They check word/unaligned halfword/64-bit forwarding, alias reconfiguration reaching the same peer bytes, bank isolation, unknown attributes and reset without erasing peer RAM. They contain no factory firmware or peer signature. Compilation and actual execution of these new cases are pending the single builder and machine/provider integration; none of the older receipts is relabeled as their validation. GPIO60/SLA handshake and external peer-event producers remain machine-owned dependencies rather than default-ready behavior in this device.

The single builder subsequently compiled and linked implementation `f9096c43d15dfbd54e2b4bc99fa78326d0f5567f` with the qualified paired machine/provider stage `f114`. Its retained `work/worktrees/qemu-submodule/work/pairedbridgereceipt.log` records exit0 and exactly two native PASS: `/arm/nikon/bridge` and `/arm/nikon/bridge/peer-window`. They verify the implemented control/input protocols and real private-provider forwarding with synthetic bytes. They do not establish original peer startup, GUI completion or execution of a firmware page publisher. The compile failure caused by the new MemoryRegion and old context-word array sharing the name `window` remains historical; f909 distinguishes the MemoryRegion as `aperture` without changing the old context array or guards.

## Connected control requests

The recovered MAS `102de336` mode0 writes command6 and waits for local flag2, while mode1 first waits for flag1, writes command4 and then waits for local flag2. The recovered `102de69e` mode0 writes configuration2 and waits for flag4; mode1 waits for flag4 and clears it. The named functional paired-link law connects these complementary waits. Physical transport, SLA MMIO/IRQ bindings and its corresponding functions must be established independently; this interface does not copy MAS board addresses into the SLA domain.

Only an accepted command6 reaching its real deferred completion emits one `peer-start[0]` pulse; command4 emits no start. Only configuration2 reaching deferred completion emits one `peer-configuration[0]` pulse. Local IRQ bits1/5 and command6's bank0 completion remain unchanged. A machine may connect those outputs to the other live bridge's `peer-control[0]`/`[1]` inputs, respectively. A rising input outside reset latches raw bit0/5 and updates the real IRQ output; masking and W1C preserve the normal pending protocol. No event flag, GPIO, peer byte or signature is written by the host interface. Disconnected outputs produce no peer effects. Reset cancels the pending timers, hence their future pulses; externally held input levels survive reset without creating a new edge.

Native C cases cover input masking/W1C/held levels and reset cancellation. A synthetic QOM loopback routes each real output to input0 to distinguish emitted pulses from local IRQ completion: command6 yields local bit1 plus input bit0, configuration2 yields local bit5 plus input bit0, and command4 remains local only. This explicit test circuit is neither an original peer nor a hardware-wire claim. The single builder must execute these cases together with the new forwarding/provider fixture before reporting them as verified.

## SLA page-register qualification

The retained official SLA source5 family provides an independent register binding. `1006bdd0` loads literal `201350b0`, selects `index*4`, and calls `1006c7e8`; selector2 adds8000 and writes the bank1 port. `1006bde4` reads all five groups of eight words through the corresponding `1006c7fa` selector. This is the same finite40-word layout, not evidence of a separate page-versus-mask variant.

The full dynamic producer `10061342` gets its entry index from region record+14. At `1006139a` it reads `shadow[index+8]`, shifts that mask by12, and at `1006139e` rejects zero before reaching the later page STORE. Only after the region/range checks does it update region+0c, write a remote page to `shadow[index]` at `100613dc`, and call `1006bdd0` at `100613e6`. The five frozen24B records all contain index8; that initial value does not itself prove a valid published entry or that a page write to mask0 executed. The current retained original observation has zero mappings/shadow and unchanged index8, so the nonzero-mask precondition has not been met. No variant, host page, or new STORE-history scoreboard is introduced into the device.

The original SLA region inputs are `[class,span,local,remote,chunks,index]`: `[4,04000000,0,e6000000,2,8]`, `[5,10000000,08000000,a8000000,2,8]`, `[6,08000000,18000000,50000000,1,8]`, `[7,20000000,20000000,c0000000,2,8]` and `[0,10000000,40000000,b0000000,1,8]`. Serializing these records into the established three page arrays produces eight chunks, with class0 at slot7; this is a source-informed fixture, not a captured initialization result. The full SLA initializer that assigns starting indices remains a separate source frontier.

Class0's converter `10060f9c` and wrappers `10064728/1006474a` consume the resulting local address through real CPU LDRD/STRD leaves. Thus a source-qualified SLA IO aperture at40000000..50000000 can expose bank1 region2 with alias offset40000000. Its original47C andB7C section descriptors are both identity mappings: the aperture is device forwarding, not an MMU/RAM alias. An address such as47cf0000 routes to real MAS b7cf0000 only when the current guest table supplies a valid matching row and an attached MAS AddressSpace. Empty, malformed or unmatched rows retain transaction errors.

The additional native C case `/arm/nikon/bridge/sla-page-table` uses both real borrowed address spaces. It programs the private SLA bridge through the MAS window, rejects a cold index8-only negative packet without changing the MAS ring, stores/reads all40 words of the eight-chunk fixture, forwards class0 reads/writes into real MAS storage, changes/restores the remote page, and verifies reset leaves the peer bytes intact while removing the mapping. Compilation/execution of this additional case is pending the single builder with the source-qualified SLA IO aperture; the earlier two native PASS are retained without relabeling.

## Original ISR bit-number correction

The preceding current bit names correct an off-by-one interpretation in the
older contract; historical receipts retain their original identities. Direct
C1.70 byte disassembly at the actual MAS load base `0x10020000` shows MAS
VA `0x102dcce8` and SLA VA `0x1006adbc` testing `LSLS raw,#26` followed by
`BPL` to skip the flag4 setter. The sign test selects raw bit5 (`0x20`), not
bit6 (`0x40`). Their conditional flag8 paths at MAS `0x102dccca` and SLA
`0x1006ad9e` similarly use `LSLS #27`, selecting raw bit4, not bit5.
These are original-byte consumers, not accepted reconstruction source or a
physical interrupt-producer binding.

The read-only `producer3` observation catches both executing ISRs with
raw `0x40` and neither flag4 branch. Regression tests fail on the old
configuration cause (`0x40 != 0x20`) and old third peer-event mapping
(`0x20 != 0x10`). The implementation corrects these causes and their named
inputs; it does not add a producer, completion timer, guest flag write or
ready edge. Masking, W1C, deferred completion and reset cancellation remain
covered. All 27 native fixtures, 239 subtests pass in
`work/research/ev3producer/nativecheck_final2.log`.

Two final unchanged complete-bundle runs (`work/research/guiresume/final1/`
and `final2/`) now catch raw `0x20`, both flag4 branches, the natural MAS
setter for handle `0xb1806f44`, and return from the configuration wait.
Their next first rejected read is PA `0x2013e000`: MAS PC `0x102de73a`,
SLA PC `0x1006c80e`. Getter base `0x20136000` with bankmask2 adds `0x8000`.
The associated original getter/publisher ranges are bounded, but their bank
relationship, reset semantics and real data producer are not yet closed.
Unknown accesses remain errors. Subsequent NOR writes follow the abort and
are not evidence that a NOR writer would fix this startup dependency.

An experimental paired-word reflection advanced EV3 mask `0x10`, then failed
on the bank0 getter. It was withdrawn from maintained source because the
cross-bank law remains unqualified. Its ignored patch and receipts are
research only, not accepted startup evidence. No final run reaches the
transfer producer or a natural GUI frame; final COPY/loader checks fail
before their submissions rather than inheriting the older 14-COPY PASS.

## Opt-in notification hypothesis

Original MAS/SLA bytes support notification-mask intent rather than two
ordinary stored mailbox words: the set wrapper publishes `[0,0,mask,0]`, the
clear wrapper `[0,0,0,mask]`, and the getter wrapper returns the second of two
words. The sender selects bank0; the receiver callback selects bank1, clears
its selected notification bit and handles the queued packet. These establish
software roles, not the physical bank connection, first getter word, IRQ
producer or hardware reset law.

The maintained local bank0 flag cell at `+0x6004` therefore supports word-only
OR-set at `+0x6008` and clear-mask at `+0x600c`, with functional empty reset
state. Readback from the first word `+0x6000` remains rejected by default;
receive-bank accesses also remain rejected without an experimental connection.
Other directions, widths and neighbors are not generic storage.

`notification` is a default-off research property. The composed machine's
explicit `functional-profile=functional-gui` sets the property on both MAS
and SLA bridges before realize and then connects the opted-in pair using
`nikon_bridge_connect_notifications()`. A direct QOM global remains a focused
device-test mechanism rather than the documented machine profile. The profile
hypothesizes local
bank0 to peer bank1 reflection, a flags mirror in the unused first getter
word, empty reset state, and sticky IRQ cause bit15 on newly set bits. It is
not the withdrawn stored-word experiment and is not qualified physical
hardware behavior. It does not write guest event/task/ready objects or pixels;
its hardware-facing assumptions still require independent evidence.

After the independent [I2C divisor correction](../i2c/protocol.md), two bounded
original observations under this profile reach GUI phase3 entry, with
byte-exact loader extents. The next rejected operation is MAS PC `0x1020b4da`,
WORD write `0` to PA `0x2c04000c`. Default-profile revalidation still rejects
`0x2013e000`, with no inherited COPY pass. Neither profile observes a graphics
transfer producer or first frame; [native validation](../../validation/native.md)
keeps these outcomes separate. The later
[coprocessor startup study](../../runtime/coprocessor.md) joins the rejected
configuration write to an original TL4-labelled frontend, distinguishes
program COPY from execution, and observes GUI component phase3 return without
a graphics transfer or frame. It qualifies no additional notification law.
