# Original zero-fill protocol on the virtual DDR bus

This layer continues [QEMU: recover first flash-bus controller and temporary stack contract](https://github.com/mashiro01/nightglass/issues/330). The [fixed original-byte producer/poll contract](https://github.com/mashiro01/nightglass/blob/7bd3a3ed298b2fd0f24d1cc1bf0698aee148f28d/research/z7ii/qemu-dma-fill-contract.md) establishes candidate PA `0x20070000`, the complete startup configuration and result acknowledgement. The later original boolean writer also establishes that `+0=0` is not proof of an all-register reset. Public Socionext XDMAC/HDMAC layouts conflict; this is a software-protocol functional model, not a claimed physical IP identification.

The virtual profile explicitly assumes zero reset upper bits for the three RMW words, status idle, direct byte-block count, and zero fill when `+0x14=1`, `+0x18=0`, `+4=0`, `+0=0`. The startup form retains `+0x20=+0x24=0x4000` as equal byte width/stride and its existing finite nonzero count range. The later GUI form below is one additional exact request, not a general width or row-count facility. The control boolean is stored and never resets other fields. Nonzero patterns, other dimensions/flags, count-zero encoding, unaligned destinations, unsupported offsets/widths/commands and incomplete configuration fail. These constraints bound the implemented profiles; they do not declare other real IP modes invalid. Reads expose only the actual RMW words and result status.

The board explicitly allocates one finite virtual DDR backing `[0xb18aedc0,0xb2b5edc0)`, length `0x012b0000`, and attaches it to the existing region decode plan. This allocation is an additional virtual-board RAM assumption, not physical DDR-capacity evidence. It matches the declared direct-count interpretation of the original `0x4ac` blocks and includes the subsequently proved original CRT/kernel globals. The DMA device never allocates RAM or clips a request to this backing. The original length argument is absent from the register interface: the implemented profile really fills the extra `0x8b0` tail beyond `0xb2b5e510`. The eight-byte gap after COPY0 remains uncovered. Guest code still produces COPY0, CRT context, kernel ready, SMP release and task state.

Word-one submit calls upstream [dma_memory_set](https://github.com/qemu/qemu/blob/4fc49f46dc95d4a27de2509e7fceb2931e91faeb/include/system/dma.h#L175-L189) on the board physical `AddressSpace`, using normal-memory-only transaction attributes. It performs real bus writes and respects withdrawn RAM decode; it does not use CPU debug writes or host RAM pointers. Successful `MemTxResult` produces done bit0, a bus error produces error bit4, and no success bit accompanies an error. A partial failing bus operation is not rolled back. The original poll prioritizes error bit4 and acknowledges by writing `0x10`; done is acknowledged with one. Only these two W1C values are supported. An unacknowledged result rejects reconfiguration/restart. The absence of an IRQ contract is preserved: no IRQ is invented.

The fill executes synchronously during submit, following an existing QEMU DMA-device pattern. This is an explicit functional timing assumption; status reads never perform work, and done cannot precede the bus operation. It does not reproduce physical DMA/CPU-copy overlap or measure EXPEED throughput. Reset clears only device state, not BSS RAM. Unsaved device state is marked unmigratable through upstream VMState. Unknown devices and unsupported NOR writes remain errors.

Native build exited zero. Fourteen focused synthetic checks passed across `work/issue330/fill-aligned-alias/result.json` (nine) and `fill-exact-boundaries/result.json` (five): full original-size transfer clears first/last/tail markers and preserves neighboring mappings; bus failure and withdrawn DDR produce error instead of done; W1C, incomplete/repeated submission, pattern/count/offset/width refusal, cold RMW words and boolean-control preservation are checked. The initial large-RAM direct mapping experiment triggered an upstream TLB page-offset assertion; its log in `fill-first` is preserved. The corrected implementation uses a page-congruent official RAM alias with inaccessible host backing padding. Exact reads immediately before and after the declared guest window still fail. This is focused device validation, not an original-firmware execution result.

Original MAS at C revision `59711d4c31645768a84207ece774acd6c0d5d20c` configured the device and performed a real fill: destination `0xb18aedc0`, bytes `0x12b0000`, bus result zero, done status one. The unchanged continuation then entered the CPU1 DataAbort gateway, not a normal kernel callee. Its first uncovered access was CPU1 PC `0x10103638`, word write `0xff` at `0xb2b83f9c`, below the previously mapped 72-byte secondary SVC prefix. CPU0 stopped at PC `0x101034fc`, SP `0xb2b87f9c`, CPSR `0x60000093`; CPU1 had taken DataAbort by the final snapshot. The fixed raw payload remained directly equal and QEMU exited zero. Receipt/log: `work/issue330/original-fill-59711d4/receipt.json` and `qemu.log`; wall time 0.124748 seconds is host elapsed time. This demonstrates original progression beyond the early secondary wait under the declared board profile; it does not verify first task dispatch, ticks, menu, physical DMA geometry or timing. The existing exception log reveals an earlier cause: CPU0 first DataAbort has DFSR `0x805`, DFAR `0x1d001000`; CPU1 first DataAbort has DFSR `0x805`, DFAR `0x1d002004`. The later `0xb2b83f9c` transaction is saving an original exception frame, so extending its RAM alone would conceal the earlier translation failure. The next requirement is a private-peripheral mapping consistent with the unchanged guest page table, not a host-filled release, kernel-ready flag or PTE patch.

## Issue397 current class audit

The earlier paragraphs and receipts describe the initial startup layer. The current two-engine interface additionally retains the saved runtime register setters/getters, engine stride40hex, stop command0 with virtual stopped readback2, result mask and acknowledgement. Its ISR acknowledgement can clear the combined done/error mask11hex, and real pending status intersected with the guest mask drives the board's GIC103/104. This is the runtime extension already present at the task base; the historical statement about no IRQ applies only to the initial startup layer. No new fill C behavior or native run is added by [QEMU：整批闭合 DMA 与音频的启动协议](https://github.com/mashiro01/nightglass/issues/397).

The saved runtime consumers cover configuration, submit/stop, mask/status and ISR paths; they do not establish a complete nonzero-pattern or general dimension/stride transfer law. Those modes and count-zero encoding remain future protocol work, explicitly rejected before a transfer rather than approximated as zero fill. A RAM decode/bus failure is a real backing dependency and cannot become done. The current zero profile remains synchronous and does not establish physical concurrency. No saved source joins fill completion to event/EXIU or a completed frame.

## Exact GUI canvas clear

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

The functional model accepts only those complete engine0 tuples at their
firmware-published destinations, in addition to the older startup form. For
the record0/record7 tuple, widths `0b3f`/`0b41`, a different stride, row counts
`01df`/`01e1`, a nearby or other destination and engine1 all fail before DMA.
For record1, the corresponding `0eff`/`0f01` widths, changed stride,
`021b`/`021d` row count, changed destination and engine1 fail likewise.
Record8's published base `c1c0d600`, equal to the record7 slot end, remains
unmapped. A valid request still uses the same normal-memory-only DMA traversal,
synchronous done/error result, IRQ103 mask, acknowledgement, stop and reset
laws. Missing, read-only or partially mapped storage therefore produces the
real bus-error result; no profile match can turn an absent backing into
completion.

Native fixtures separately check each exact clear extent, preserved slot tails
and record1's mapped-neighbor sentinels, IRQ103/ACK, reset retention, rejected
neighboring dimensions and destinations, record0/record7 external holes
including the record8 base, writable/readonly selected address spaces, a
partial boundary failure and a wholly unmapped GUI destination.
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
`work/memoryunitlifecyclereceipt.log`. The earlier combined run passed its
separate readonly-boundary case but aborted on the fixture's attempted parent
bus reattachment; it is not a passing lifecycle receipt. Canonical QOM memory
paths and the standard `realized` property now preserve both original memory
selection and existing bus membership across the actual repeated lifecycle.
No older Fill/HDMAC case or original firmware run was repeated for this fix.
