# Native fixture lifetime and memory boundaries

The hosted Linux run at `568f7a54a3d7570c9d846fb2aa1419e9632ca80e`
built the ARM emulator successfully, then exposed three fixture lifetime
failures, one stale memory-boundary assertion, and a CMOS batch timeout.
These are synthetic test contracts; this validation does not execute a camera
firmware benchmark or establish physical-camera behavior.

## Temporary MemoryRegion lifetime

Gatework, gateplane and paired tests create temporary ownerless MemoryRegions.
The pinned upstream `memory_region_ref`/`memory_region_unref` do not reference an
ownerless region: its caller must keep its storage alive. A removed FlatView is
destroyed asynchronously through `call_rcu`, and destruction still reads that
region. Unmapping followed immediately by freeing an embedded or stack region
therefore leaves an invalid pointer. Linux allocator perturbation exposed this
as `object_unref` asserting that its reference count was positive, or as a
segmentation fault.

The fixtures now unmap, call upstream `drain_call_rcu`, and only then unparent
or release the region's storage. Gatework also explicitly removes its two
temporary device mappings before retirement. The shared grid fixture uses the
same lifetime rule. No device behavior or guest-ready state changes.

Local native validation passes all 33 gatework, 5 gateplane, 68 paired and 9
gridflow cases. The first three use the hosted failure's allocator perturbation
values, respectively 212, 168 and 26. This local result is on macOS; it does not
claim a Linux post-fix result before CI completes.

## Capture pool boundaries

The [finite RAM contract](../startup/ram.md) includes kind15 at board PA
`[0xcc700000, 0xdbb1d000)`: 74 units of `0x34c800` bytes. Its final byte
`0xdbb1cfff` is valid, and the independent kind28 pool begins at the next byte.
The old test incorrectly listed that final byte as a hole.

The corrected native case verifies every unit's first and last bytes,
independent neighboring storage, the real preceding hole `0xcc6fffff`, whole
DMA rejection across that hole, and cancellation of queued DMA by reset while
retaining existing RAM contents. This case passes locally without changing the
board's mapped ranges.

## Batch duration

CMOS now registers 153 cases. The hosted 60-second batch expired after 44 PASS;
the next `group/commit-input` case completes independently and passes locally.
The historical [Darwin timing record](../build.md#darwin-qtest-wake-ownership)
already needed a 480-second budget for 107 cases. The CMOS batch alone now uses
that explicit budget; other fixtures retain 60 seconds. No case is skipped,
and failed assertions, child termination and TAP completeness remain errors.
