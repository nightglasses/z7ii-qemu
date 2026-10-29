# Two-engine copy and zero-fill on the virtual DDR bus

The finite model supports source-produced byte-row copies and zero fills on
either engine, without restricting them to historical framebuffer addresses.
The [row contract](#source-produced-byte-rows) separates pitch from active width;
the [copy contract](#source-produced-two-dimensional-copy) adds source ownership.
Earlier startup/GUI receipts below retain their original profile limits.

This layer continues [MEMC and early peripheral startup](/startup/memc). The [fixed original-byte producer/poll contract](https://github.com/mashiro01/nightglass/blob/7bd3a3ed298b2fd0f24d1cc1bf0698aee148f28d/research/z7ii/qemu-dma-fill-contract.md) establishes candidate PA `0x20070000`, the complete startup configuration and result acknowledgement. The later original boolean writer also establishes that `+0=0` is not proof of an all-register reset. Public Socionext XDMAC/HDMAC layouts conflict; this is a software-protocol functional model, not a claimed physical IP identification.

The initial zero-fill profile explicitly assumes cold-zero RMW upper bits, idle status
and zero fill with `+14=1`, pattern `+18=0`, dimension `+1c=0`, parameter
`+2c=0` and control `+0=0` (offsets hexadecimal). The historical startup tuple
has equal pitch/row bytes `4000`. The control boolean is stored and never resets
other fields. Nonzero patterns, unsupported dimensions/flags, zero rows,
unaligned destinations, unknown accesses and incomplete configuration fail.
These bounds do not declare other physical modes invalid.

The board explicitly allocates one finite virtual DDR backing `[0xb18aedc0,0xb2b5edc0)`, length `0x012b0000`, and attaches it to the existing region decode plan. This allocation is an additional virtual-board RAM assumption, not physical DDR-capacity evidence. It matches the declared direct-count interpretation of the original `0x4ac` blocks and includes the subsequently proved original CRT/kernel globals. The DMA device never allocates RAM or clips a request to this backing. The original length argument is absent from the register interface: the implemented profile really fills the extra `0x8b0` tail beyond `0xb2b5e510`. The eight-byte gap after COPY0 remains uncovered. Guest code still produces COPY0, CRT context, kernel ready, SMP release and task state.

Word-one submit calls upstream [dma_memory_set](https://github.com/qemu/qemu/blob/4fc49f46dc95d4a27de2509e7fceb2931e91faeb/include/system/dma.h#L175-L189) on the board physical `AddressSpace`, using normal-memory-only transaction attributes. It performs real bus writes and respects withdrawn RAM decode; it does not use CPU debug writes or host RAM pointers. Successful `MemTxResult` produces done bit0, a bus error produces error bit4, and no success bit accompanies an error. A partial failing bus operation is not rolled back. The original poll prioritizes error bit4 and acknowledges by writing `0x10`; done is acknowledged with one. Only these two W1C values are supported. An unacknowledged result rejects reconfiguration/restart. The absence of an IRQ contract is preserved: no IRQ is invented.

The fill executes synchronously during submit, following an existing QEMU DMA-device pattern. This is an explicit functional timing assumption; status reads never perform work, and done cannot precede the bus operation. It does not reproduce physical DMA/CPU-copy overlap or measure EXPEED throughput. Reset clears only device state, not BSS RAM. Unsaved device state is marked unmigratable through upstream VMState. Unknown devices and unsupported NOR writes remain errors.

Native build exited zero. Fourteen focused synthetic checks passed across `run memc/fill-aligned-alias/result.json` (nine) and `fill-exact-boundaries/result.json` (five): full original-size transfer clears first/last/tail markers and preserves neighboring mappings; bus failure and withdrawn DDR produce error instead of done; W1C, incomplete/repeated submission, pattern/count/offset/width refusal, cold RMW words and boolean-control preservation are checked. The initial large-RAM direct mapping experiment triggered an upstream TLB page-offset assertion; its log in `fill-first` is preserved. The corrected implementation uses a page-congruent official RAM alias with inaccessible host backing padding. Exact reads immediately before and after the declared guest window still fail. This is focused device validation, not an original-firmware execution result.

Original MAS at C revision `59711d4c31645768a84207ece774acd6c0d5d20c` configured the device and performed a real fill: destination `0xb18aedc0`, bytes `0x12b0000`, bus result zero, done status one. The unchanged continuation then entered the CPU1 DataAbort gateway, not a normal kernel callee. Its first uncovered access was CPU1 PC `0x10103638`, word write `0xff` at `0xb2b83f9c`, below the previously mapped 72-byte secondary SVC prefix. CPU0 stopped at PC `0x101034fc`, SP `0xb2b87f9c`, CPSR `0x60000093`; CPU1 had taken DataAbort by the final snapshot. The fixed raw payload remained directly equal and QEMU exited zero. Receipt/log: `run memc/original-fill-59711d4/receipt.json` and `qemu.log`; wall time 0.124748 seconds is host elapsed time. This demonstrates original progression beyond the early secondary wait under the declared board profile; it does not verify first task dispatch, ticks, menu, physical DMA geometry or timing. The existing exception log reveals an earlier cause: CPU0 first DataAbort has DFSR `0x805`, DFAR `0x1d001000`; CPU1 first DataAbort has DFSR `0x805`, DFAR `0x1d002004`. The later `0xb2b83f9c` transaction is saving an original exception frame, so extending its RAM alone would conceal the earlier translation failure. The next requirement is a private-peripheral mapping consistent with the unchanged guest page table, not a host-filled release, kernel-ready flag or PTE patch.

## DMA lifecycle audit

The earlier paragraphs and receipts describe the initial startup layer. The current two-engine interface additionally retains the saved runtime register setters/getters, engine stride40hex, stop command0 with virtual stopped readback2, result mask and acknowledgement. Its ISR acknowledgement can clear the combined done/error mask11hex, and real pending status intersected with the guest mask drives the board's GIC103/104. This is the runtime extension already present at the task base; the historical statement about no IRQ applies only to the initial startup layer. No new fill C behavior or native run is added by the saved DMA and audio lifecycle audit.

Both MAS VA `102DBBAA` and SLA VA `1006ACDE` initialize the fill engine by stopping the engine (`command = 0`), polling for idle (`command & 3 == 2`), and writing `+0 = 1` (engine enable). Writes of 0 and 1 to offset 0 are accepted; values greater than 1 return bus errors. The start check accepts requests with control 0 or 1, matching both cold-boot zero fill and runtime initialization.

The saved runtime consumers cover configuration, submit/stop, mask/status and
ISR paths. Nonzero patterns, other dimension modes and zero-count encoding
remain unsupported. A RAM decode/bus failure cannot become done. The current
zero mode remains synchronous and does not establish physical concurrency or
a completed frame.

## Source-produced byte rows

Original C1.70 MAS VA `10389e3a` builds the DMA parameters from a caller's
request. Its destination is `base + x + pitch*y`; request halfwords at decimal
byte offsets2/4/6 supply pitch/row bytes/rows. Writer VA `102dba60` publishes
those fields at selected-engine offsets `20/24/28`. Thus the old C field names
width/stride were reversed, hidden by all earlier equal-valued tuples.

Each engine now clears `row_bytes` bytes at `destination + row*pitch` for
`row=0..count-1`. All three fields are unsigned16-bit; pitch must be at least
the positive row width and count must be positive. Destination remains aligned
to4 bytes. The full last-row end must fit the32-bit PA space before DMA starts.
Equal pitch/width uses the existing contiguous DMA path; otherwise row padding
is untouched. Both engines use the same producer and operation, with independent
results, masks and IRQ103/104.

Control, configuration coverage, zero-mode fields, real RAM-only transactions,
ACK/stop/reset and unmigratable-state rules remain unchanged. Writable RAM must
exist in the selected execution domain. Missing/read-only/MMIO backing gives a
real bus-error result; previously completed rows/spans remain written. A request
whose final byte is in range can therefore still fail during DMA. No address
whitelist, backing allocation, completion timer or guest callback is supplied.

The observed next request is CPU1 VA PC `102dbba2`, WRITE32 PA `20070010=1`,
with mask `11`, flags1, other zero-mode fields0, pitch/row bytes `30`, rows
`2a` and PA destination `bff44340`. Its complete extent is
`[bff44340,bff44b20)`. It is an ordinary48-byte by42-row clear, not a new
opcode. The [row receipt](/evidence/imagerequest/fill.json) separates native
validation from the subsequent unchanged-firmware continuation.

The old model fails the legal padded-row fixture. After correction, four
selected-domain fill subtests and four individually selected startup/GUI board
cases pass. Padding/guards, both engines, odd widths, partial row bus errors,
readonly/missing storage and lifecycle boundaries are covered. A broader board
run is not a full-suite PASS: after40 cases QEMU is killed by signal9 in an
unrelated image case. A repeated-filter attempt also has invalid TAP plans;
only the correctly selected successful invocations are acceptance evidence.

The unchanged60-host-second continuation now executes the48×42 request and
reports real bus failure, then reaches CPU1 VA PC `1051e322`, WRITE8 PA
`bff43ac2=80`. This exposes missing storage independently of the corrected
operation. No result is changed to success to bypass it. The later complete
GUI-pool backing is tracked by the [startup frontier](../../validation/frontier.md).

## Source-produced two-dimensional copy

**Source facts.** C1.70 MAS VA `102dba60` publishes a36-byte lower packet P.
The handle byte at decimal offset4 chooses one of the two engines, with the
same register layout at PA `20070000 + engine*40`:

| Packet field, decimal byte offset | Relative register, hexadecimal | Role established by the producer |
| --- | --- | --- |
| P.byte0 | `14` bit0 | One for fill, zero for copy |
| P.byte1 | `14` bit4 | Independent caller-provided boolean |
| P.u16+4 | `1c` | Copy source pitch |
| P.u16+6 | `20` | Destination pitch |
| P.u16+8 | `24` | Active row bytes |
| P.u16+10 | `28` | Row count |
| P.u32+12 | `2c` | Copy source address |
| P.u32+16 | `30` | Destination address |
| P.byte2 and P.u32+20 | `18` bits24..25 and0..23 | Pattern fields, zero in supported copy/clear packets |
| P.u32+32 | `04` | Result mask |

The two-handle copy constructors at VA `1038a354` and `1038a442` set P.byte0
to zero, retain the caller's first byte in P.byte1, and construct both addresses
from independent base, pitch and coordinate fields. For their caller packet Q,
all offsets below are decimal:

```text
source_pitch      = Q.u16+2
destination_pitch = Q.u16+4
row_bytes         = Q.u16+6
rows              = Q.u16+8
source            = Q.u32+12 + source_pitch * Q.u16+22 + Q.u16+20
destination       = Q.u32+16 + destination_pitch * Q.u16+26 + Q.u16+24
```

The fill branches at VA `10389d08` and `10389e3a` instead set P.byte0
to one and forward the same independent caller byte to P.byte1. They populate
destination geometry and the pattern fields. Thus flags `0/10` select copy
and flags `1/11` select fill in this finite model. Other flag bits remain
errors. The source does not establish bit4's physical role. **The explicit
functional hypothesis is that either value leaves the current byte-copy or
zero-fill result unchanged.** This is not a claim that the hardware bit is a
cache control, byte-swap control or physically a no-op. The normal firmware's
subsequent consumption of copied filesystem data can validate or falsify it.
Nonzero patterns remain unsupported for both flag pairs.

For copy, row i reads exactly `row_bytes` bytes from
`source + i*source_pitch` through QEMU `dma_memory_read`, then writes those bytes
to `destination + i*destination_pitch` through `dma_memory_write`. Source and
destination coordinates are byte offsets; unaligned byte addresses are accepted
for copy. Both unsigned16-bit pitches must be at least the positive row width;
row count must be positive. Both last-row ends must fit the32-bit selected
physical address space. Source pitch and address must have been configured,
in addition to the existing shared fields. Register retention across an ACK
still permits a later command to use a complete existing configuration.

Every active source and destination row is checked in the device's selected
`AddressSpace` before a copy writes any output. Padding is neither required to
have RAM backing nor read/written. Readable direct RAM, including read-only
source RAM, is valid input; every destination span must be writable direct RAM.
Missing, MMIO or protected active backing produces the existing bus-error result
without done. A source/destination overlap in actual RAM backing, including an
alias at a different PA, rejects before DMA or a completion event. Disjoint
parts of the same backing remain valid. Rejecting overlap is a finite boundary;
the source does not establish hardware memmove behavior. The implementation
uses one row of host scratch storage, not a host clone of the address space.

Both engines remain synchronous and independent. Success status1 and the
existing mask/IRQ law apply only after every actual DMA row succeeds. Error
status10 uses the existing virtual bus code at bit16; it does not claim a
recovered physical source/destination error classification. A mapping failure
found by copy preflight changes no destination bytes. Any unexpected bus failure
after preflight preserves already completed row writes; no rollback or success
is fabricated. Zero-fill's existing partial-failure behavior is retained.
ACK, stop0/readback2, reset, unacknowledged-result refusal and private-domain
ownership are unchanged.

The repeatable original stop is CPU1 VA PC `102dbb66`, WRITE32 PA
`20070050=1`, after the normal formatted MBR/FAT32 input clears the storage
rejection condition. Engine1 has flags10, pattern0, source/destination pitches
512, row width512, count1, source PA `b8108380` and destination PA `b6e9f340`.
This is one ordinary copy in the family above, not an address or512-byte
whitelist. Its source extent is `[b8108380,b8108580)` and destination extent
is `[b6e9f340,b6e9f540)`. The source geometry proves the operation; passing the
firmware's later filesystem consumer remains separate execution evidence.

The new public QOM/MMIO fixture in `tests/unit/nikon/fill.c` and copy case in
`tests/qtest/nikon/fill.c` specify both flag pairs and engines, independent row
pitches, byte alignment, guards/padding, exact boundaries, read-only sources,
read-only destinations, holes, physical/backing-alias overlap, malformed fields,
IRQ/mask/ACK, stop and reset. The old implementation fails the first legal
copy submission. After the correction, the complete native `nikon_fill_unit`
executable passes all six groups in0.64 host seconds. The board qtest copy
case is added but is not claimed as a separately passing invocation.

An unchanged-firmware continuation with the same cold-neutral profile and a
normal preformatted FAT32 slot0 image passes this copy and reaches natural
M/A/S/P setting changes. MENU subsequently reaches a separate176/328-byte
graphics-prefix rejection. The [input receipt](/evidence/input/turnover.json)
keeps those execution observations separate from the native DMA checks; this
is not complete filesystem, menu or physical bit4 acceptance.

## Historical exact GUI canvas clears

An unchanged C1.70 run reaches the engine0 configuration writer at Thumb
`VA102dbadc` and the submit store at `VA102dbba2` (caller LR `10389e29`). Its
register tuple is mask `11`, flags `1`, pattern `0`, dimension `0`, width and
stride `0b40`, row count `01e0`, parameter `0`, and destination `bec5f200`.
The direct byte-count interpretation is exactly `0b40 * 01e0 = 151800`
bytes: 720 four-byte pixels per row for 480 rows. The destination is the start
of resource57 record0's independently recovered slot
`[bec5f200,bf10f200)`. This identifies a firmware-owned RGBA-sized GUI canvas
clear, not final LCD scanout or a host-rendered frame.

After record0 completes, the unchanged firmware reaches the same executor
`VA10389e3a`, configuration writer and engine0 tuple with only destination
changed to `c175d600`. Correct-base ROM table `VA123b73ec` record7 is
`{enabled=1,size=004b0000,pointer-cell=b181d38c}`; the runtime cell contains
`c175d600`. Its exact slot is `[c175d600,c1c0d600)`, the active clear is
`[c175d600,c18aee00)`, and the remaining tail is untouched.

After those two clears, the unchanged firmware makes a third call through the
same `VA102dbadc` configuration writer and `VA102dbba2` submit store. Engine0
again has mask `11`, flags `1`, pattern and dimension `0`, and parameter `0`,
but width and stride are `0f00`, row count is `021c`, and destination is
`bf10f200`. Correct-base ROM table `VA123b73ec` record1 is
`{enabled=1,size=001fa400,pointer-cell=b181d390}` and the runtime cell contains
that destination. Thus `0f00 * 021c = 1fa400` clears the entire exact
`[bf10f200,bf309600)` record1 slot: 960 four-byte pixels per row for 540 rows.
Its left neighbor is record0 and its right neighbor is record5; neither is
part of this request.

The held-F4 mode1 path subsequently submits the720x480 tuple to resource57
record5, the independently allocated LCD composition slot
`[bf309600,bf7b9600)`. The model additionally admits that exact base; only
`[bf309600,bf45ae00)` is cleared. The upper producer, IRQ/consumer chain and
new native/original receipts are joined in [fill.md](../../runtime/fill.md).
No new geometry, format or general destination rule is introduced.

At these historical checkpoints the model admitted only the listed engine0
tuples. Their neighboring geometry/address and engine1 refusals were model
limitations, superseded by the source-produced row contract above. The later
[complete kind57 arena](../../runtime/storage.md#kind57-gui-arena) also backs
records6/8 and the owned tail, replacing the former internal holes. Actual
storage failures outside this owner still produce bus errors.

Current native fixtures retain each exact clear extent, slot-tail and neighbor
sentinels, IRQ/ACK/reset checks, writable/readonly selected address spaces and
partial boundary failures. They now test invalid geometry/overflow/alignment
rather than rejecting legal neighboring requests solely by their address.
These are synthetic device/board checks, not an unchanged-firmware display
result.

## Private execution domains

The device has the standard QEMU `memory` link property, whose target is a
`MemoryRegion`. Set it before device realization to the executing component's
physical root. Every realization creates a fresh heap `AddressSpace` over that
root. Unrealize retires it with QEMU's `address_space_destroy_free()`, which
keeps its storage alive until the deferred RCU callback completes even if the
device is immediately realized again or finalized. Only that address space is
retired; the linked board memory remains
owned by its board. An unset link selects the system root used by MAS.

Every destination lookup and DMA write uses this selected domain. Each mapped
span must be writable RAM; read-only RAM, ROM, MMIO and unmapped destinations
produce the existing bus-error result and cannot yield done. Earlier successful
spans of a request remain written if a subsequent span fails. The RAM mapping
belongs to the component: an equal physical address in MAS and SLA does not
share backing unless the board deliberately connects it. In particular, a fill
performed by a private SLA instance must not clear MAS memory.

This change affects address-space ownership only. The original two-engine
register protocol, synchronous zero mode, result/ACK/mask and reset behavior
are retained. Native dual-domain validation is supplied separately; this source
change alone does not establish original SLA execution or GUI progress.

The native system-library retirement fixture in `tests/unit/nikon/memory.c`
alternates two independent real roots across 32 Fill/HDMAC realizations. It
drains prior RCU callbacks after the new realization, then submits a real fill
and verifies only the selected root was zeroed. It also finalizes the devices
before draining their final retirement callbacks. The sole builder compiled
the real ARM system-library fixture and passed the focused lifecycle case,
exit0 and0.79 seconds, recorded in canonical
`run memoryunitlifecyclereceipt.log`. The earlier combined run passed its
separate readonly-boundary case but aborted on the fixture's attempted parent
bus reattachment; it is not a passing lifecycle receipt. Canonical QOM memory
paths and the standard `realized` property now preserve both original memory
selection and existing bus membership across the actual repeated lifecycle.
No older Fill/HDMAC case or original firmware run was repeated for this fix.
