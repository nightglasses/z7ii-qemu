# Complete software storage profile

The [MAS RAM backing alignment fix](../startup/ram.md) preserves all physical
intervals below using exact aliases into private, offset-adjusted RAM backing.
It resolves the original resource57 record0 host TLB assertion without mapping
padding or changing guest allocation boundaries. Real TCG and bus accesses
share the same cells, including after reset and MXIC decoder changes.

The additional exact [kind40 allocation](kind40.md) at
`[62059800,62221800)` is backed separately. Its original VA conversion and
section PTE are verified; the surrounding address class remains uncovered.
The independently owned [kind41 workspace](#kind41-processing-workspace)
continues at `[62221800,62621800)`; it does not grant backing to the rest of
the conversion class.
The [resource11/12 phased loader](rows.md) additionally owns contiguous
destination capacities `[50300000,50640000)`. NOR inputs populate only flash;
the original guest produces this RAM's content.

The Z7II functional board backs the six complete source-bound storage classes
21 through 26. This replaces allocation-sized module memory and separate kernel
copy, BSS, table and stack fragments. Each physical byte has one RAM cell shared
by CPU and DMA; each class is an independent endpoint of the existing MXIC
decoder.

| Source record | Physical range, exclusive end | MiB | Software role |
| --- | --- | ---: | --- |
| 21 | `[b1800000,b5f00000)` | 71 | Kernel copies, BSS, page tables and stacks |
| 22 | `[b5f00000,b7d00000)` | 30 | Private objects and message ring |
| 23 | `[b7d00000,baa00000)` | 45 | Module allocations |
| 24 | `[baa00000,bb300000)` | 9 | Relocated code |
| 25 | `[bb300000,bc100000)` | 14 | Graphics heap storage |
| 26 | `[bc100000,bd200000)` | 17 | Pixel heap storage |

The source contract is `nightglass` **973285c**, `research/z7ii/memory.md`.
One saved-source pass recovered all thirty records and their terminator from
175 original instructions and 93 actual word stores, with every instruction
checked directly against the original payload bytes. The same pass classified
all 4,096 original L1 descriptors. The kernel and graphics classes have complete
identity sections; the private, module and pixel logical views are respectively
`[14000000,15e00000)`, `[15e00000,18b00000)` and `[18b00000,19c00000)`.
Only the guest MMU translates those logical views. The model creates no physical
bus mirror and writes no page table entries.

Cold zero and whole-class backing capacity are explicit functional-board
assumptions. These 186 MiB do not establish installed DDR capacity, allocate an
original guest object, initialize a heap, perform a code copy, create a surface
or produce display pixels. The original guest performs all of those operations.
Address-class flags describe software address conversion and grant no RAM
permission. Other normal-PTE or catch-all classes retain their existing physical
owners; a PTE alone does not turn an unknown transaction into RAM.

The graphics heap's own arena ends at `bc020000`, while the pixel heap's arena
ends at `bd1e0000`. Their complete classes extend to `bc100000` and `bd200000`.
Those class tails are storage in this declared functional profile, without a
claim about their original purpose or pixel format. The original descriptor,
allocator pointers, metadata, guard checks and copies remain guest-authored.

### Kind41 processing workspace

The C1.70 MAS initializer VA `1026fbb0`, called lazily by `102712e6`, requests
`1030d96e(0,41,355600)` and stores the returned object at VA `b186ac88`.
Kind41 is decimal; addresses, masks and layout offsets here are hexadecimal.
ROM VA `11982938[41]` selects `11983488`, whose six words are
`{8,400000,400000,0,62221800,700}`. The source pool has one `400000`-byte
unit; all five mode override entries for this kind are NULL.

The [workspace receipt](/evidence/imagerequest/workspace.json) joins this
declaration to unchanged execution: the holder returns object `b2c8766c`,
whose buffer record contains PA `62221800` and capacity `400000`. The request
fits this owned unit; its smaller requested length does not shrink the
allocated capacity. The existing converter `103c5a7a` returns VA `36f21800`
using record15's `34600000 + PA -5f900000` formula. Original section PTEs
`62214426` and `62514426` independently map the logical base and later
record access back to this physical pool.

The initializer's complete declared layout is bounded by its request:

| Pool-relative interval, exclusive end | Source use |
|---|---|
| `[0,300000)` | Workspace passed by later `10270f1c` |
| `[300000,311600)` | 128 records of size `22c`, initialized by `102ed14c` |
| `[311600,311d68)` | Three elements of size `278` |
| `[311d80,3555d0)` | Four elements of size `10e14` |
| End `355600` | Requested extent including alignment slack |

The first constructor calls `102ebe2a(record+1bc,...)`, which invokes
`10361034(record+1bc,24)` with a decimal24-byte length. At the old fault,
`record=37221800`, so VA `372219bc` maps to PA `625219bc`. This is the first
small record clear, not a request to clear2GiB: the helper subtracts32 then
shifts its scratch length register left28 before the failing tail store,
producing the later sampled `r1=80000000` from entry length24.

The board backs exactly `[62221800,62621800)` through the existing
offset-adjusted QEMU RAM helper, with one shared cell for CPU and DMA.
Cold zero and persistence across machine reset are functional board choices,
not recovered physical power-on contents. The guest owns allocation,
construction and all later data. There is no PA mirror at either logical VA,
page-table installation, replacement allocation result, broader conversion
aperture, produced frame or completion event. Kind40 remains an independent
lower neighbor; the upper address at `62621800` remains uncovered.

The native case first fails at the original fault address on the preceding
model. Its passing scope covers every4KiB page, exact endpoints, neighbor
independence, reset persistence, actual HDMAC4099-byte cross-page DMA and
logical/upper-bound HMP transaction errors. The existing real-TCG RAM fixture
also checks four points from first byte-page to last word with CPU/bus
visibility across reset. These fixtures establish backing, separately from
the original continuation recorded in the receipt and startup frontier.

### Processing scratch arenas

The C1.70 MAS initializer VA `103a34c0` sets up two independent scratch
allocators alongside the download arenas. Addresses and byte sizes below are
hexadecimal; slot counts and allocation types are decimal. The object at VA
`b1808c98` receives `b0100000` at field `10` and `af8d4000` at field `14`.
It also initializes sixteen and four free descriptors, respectively, plus
their separate busy arrays. `103a354e(size,type)` dispatches allocation to
the original guest functions; QEMU does not replace that API or its results.

| Allocation type / function VA | Slot size | Slots | Board PA extent, exclusive end |
|---|---|---:|---|
| 1 / `103a37aa` | `80000` | 16 | `[b0100000,b0900000)` |
| 2 / `103a3718` | `4b000` | 4 | `[af8d4000,afa00000)` |

Both functions assign `base + slot*unit` and record the reserved byte count,
slot index, slot count and type in the returned descriptor. Their requested
count is `floor(size/unit)+1`, not the usual ceiling formula. The free-slot
arrays bound the declared storage capacities; this does not prove that the
original contiguous-span search rejects every oversized request. Original
allocation, locking, free lists, busy bits and errors remain guest execution.

The old original write at PA `b0100000` is the first decimal200-byte clear
in `10271abe`, with saved return `10271ae9`. The earlier kind41 initializer
also obtains a type1 scratch descriptor through `103a354e`. These facts join
the processing consumer to the existing source-declared pool; neither the
small clear length nor the fault address defines a one-off RAM window.

The functional board supplies exactly both ranges through ordinary shared
QEMU RAM. Cold zero and persistence across reset are explicit assumptions.
The type1 upper neighbor is the separately owned selector-one download arena;
type2 ends at the independent kind20 capture pool. Distinct MemoryRegions
preserve both ownership boundaries. The lower addresses remain uncovered;
there is no expansion of the enclosing address class, host allocator, object
injection, frame generation or completion event. The historical `b0800000`
hole now has a source-derived owner inside type1; the download arena itself
has not expanded.

The [scratch receipt](/evidence/imagerequest/arenas.json) records native
red→green and72 passing memory/sensor cases. Fixtures check all20 slot
boundaries with independent values, both adjacent owners, actual4099-byte
HDMAC transfers crossing pages, reset persistence and real HMP errors below
each range. The existing real-TCG case covers both pools, including the former
hole and final words, to check CPU/bus backing visibility. These are memory
fixtures, not validation of all guest allocation requests or image processing.

### Resource57 record0, record1 and record7 GUI slots

The same correct-base table and allocation path publish record0 through the
firmware cell at `b181d384`. In the unchanged runtime its value is
`bec5f200`; the record size is `4b0000`, yielding the exact physical slot
`[bec5f200,bf10f200)`. The engine0 fill producer at Thumb `VA102dbadc` then
configures width and stride `0b40`, row count `01e0`, and destination
`bec5f200` before `VA102dbba2` submits the request. Its zeroed prefix is
`[bec5f200,bedb0a00)`, length `151800`, corresponding to a 720x480 four-byte
canvas. The remainder `[bedb0a00,bf10f200)` is record-owned storage but is not
touched by this request.

The board backs only this exact record0 slot as cold-zero RAM. Its left neighbor
remains uncovered, while record1 below starts at the exact record0 end; contents
persist across machine reset. The guest still owns publication, initialization,
reuse and every later pixel. This canvas-sized request is upstream GUI storage;
it does not itself establish rear-LCD scanout, EVF selection or a visible host
frame.

After that request completes, the unchanged firmware calls the same executor
`VA10389e3a` with the same engine0 tuple and destination `c175d600`. Correct-base
ROM table `VA123b73ec` record7 is `{1,004b0000,b181d38c}`, and that runtime
pointer cell contains `c175d600`. The exact record7 slot is therefore
`[c175d600,c1c0d600)`. Its clear covers `[c175d600,c18aee00)`, again length
`151800`; `[c18aee00,c1c0d600)` remains untouched.

The board backs this exact record7 slot independently and preserves its
contents across reset. Record8's runtime base `c1c0d600` is exactly the record7
end and remains uncovered; neither record8 nor the whole resource57 arena is
inferred from record7. The guest continues to own both records' publication,
content and lifecycle.

After the record0 and record7 clears complete, the unchanged firmware reaches
the same engine0 configuration writer `VA102dbadc` and submit store
`VA102dbba2` a third time. The recovered tuple retains mask `11`, flags `1`,
zero pattern/dimension/parameter, but sets width and stride `0f00`, rows `021c`
and destination `bf10f200`. Correct-base ROM table `VA123b73ec` record1 is
`{1,001fa400,b181d390}`, and runtime pointer cell `b181d390` contains
`bf10f200`. The exact multiplication `0f00 * 021c = 1fa400` therefore clears
the whole record1 slot `[bf10f200,bf309600)`, a 960x540 four-byte surface.

The board backs precisely that cold-zero slot. Record0 ends at its left edge
and record5 begins at its right edge, so both adjacent bytes belong to separate
published records rather than holes or padding. Native bounds checks preserve
sentinels in both neighbors, and machine reset preserves record1 contents. The
guest remains responsible for publication, later drawing and any relation
between this upstream GUI surface and a selected physical output.

### Rear-LCD composition slot

The correct-base C1.70 ROM table at `VA123b73ec` contains nine 12-byte records
`{enabled,size,destination-pointer}`. Record5 at `123b7428` is
`{1,004b0000,b181d388}`. The original resource57 allocation route
`1063da70 -> 1030d96e -> 1030d85c` selects TBB index19 and returns the larger
owner allocation; the partition writer at `10634b04/10634b06` stores the
record5 current address `bf309600` through `b181d388`, then advances it by
`004b0000` to `bf7b9600`. These are MAS virtual addresses analyzed with ROM
base `10020000`; the published buffer addresses are physical operands.

The board therefore backs only resource57 record5
`[bf309600,bf7b9600)`, length `4b0000`, as cold-zero RAM. The name
`lcd-composition` describes its observed software use, not a claimed DDR part.
Guest code owns the allocation metadata, contents and reuse. In the observed
graphics job the first `2b0000` bytes, `[bf309600,bf5b9600)`, are a
1024x688 RGBA8888 target extent. Its path covers rows6 through687, so the
active write is `[bf30f600,bf5b9600)`, length `2aa000`. The top six-row
alignment area `[bf309600,bf30f600)` and remaining slot tail
`[bf5b9600,bf7b9600)` are storage but this job does not modify them. Neither
the record1 contents immediately before its base nor the uncovered address at
its right edge belongs to record5.

This is the primary/HAIMENMONI (rear LCD) upstream composition surface selected
by output-table bit1. It is not the final scanout arena below, does not itself
produce a console frame, and does not replace the firmware-owned consumer that
must later move or commit composed pixels into display output.

### Display buffer arena

An unchanged-firmware read-only run independently publishes two three-buffer
image-memory groups through `1068fbf0 -> 104d77b8 -> 104489d8`. The first group
has bases `cb4a7000`, `cb82b000`, and `cbbaf000`, each separated by `384000`;
the second has bases `cbf33000`, `cc18b000`, and `cc3e3000`, each separated by
`258000`. The last buffer therefore ends at `cc63b000`; the two groups form the
single exact physical interval `[cb4a7000,cc63b000)`, length `1194000`.

The firmware copies these addresses from its acquire request into the display
resource table at `b1d0d9d8`. In the explicit experimental TFT mode `46`, it
then creates its own `1024 x 682` image object at `b1d0f008`, selects
`cb4a7000` and `cb5fc000` as its two plane pointers, and writes the first pixel
at `1044603c`. The second pointer is exactly `155000` bytes after the first and
the touched two-plane extent is `1ff800`. These observations establish RAM
ownership and bounds; they do not establish a physical DDR part, pixel-format
name, natural display-state event, image completion, or a visible frame.

The board backs only the exact published union with cold-zero RAM. It does not
write the acquire records, image object, pointers, descriptors, or pixels, and
it does not grant the rest of the `c0000000` class. RAM contents persist across
a machine reset like the other software storage regions; guest initialization
owns reuse and clearing.

`tests/qtest/nikon/memory.c` covers every storage class's first and last word,
separate retention across adjacent classes, original copy/stack/heap
destinations, logical bus mirror exclusion, MXIC decode withdrawal/restoration
without losing RAM contents, both outer holes, exact resource57 records0,5 and7
and their neighbors (including the unmapped record8 base), all six
display-buffer bases, the display arena's exact neighbors, and reset retention.
Build and execution results are separately scoped below.

## Integrated execution

Native integration **106a0ce8641179294913c3d93e585968a29aaa97** built the full
profile and explicit bundle input. The sole builder reports the four native
memory cases passing. One unchanged original run then loaded the complete
decoded container through `-machine bundle=...`, continued both Cortex-A7 cores
through the upstream GDB stub, and stopped naturally at the next unsupported
transaction. No guest register, memory, breakpoint, callback or return was
written by the observer.

The actual trace enters GUI core **1020d41a** with `R0=b1818bd0` and `R1=1`
at `qemu.log:10298`, the original GUI message task **102942f8** on CPU0 at
line11920, and GUI core **1020d41a** again with `R1=2` at line26033. These are
executed task and initialization paths. They do not establish a surface, a
transfer packet, display output or a menu. At the stopped snapshot both secure
MMU banks have `TTBR0_S=b2b74000`, `TTBCR_S=0`, `DACR_S=55555555` and
`SCTLR_EL1_S=c5107d`; the selected GUI configuration contains the real task entry
`102942f9`. The pixel heap descriptor is still zero in this snapshot.

The next first unsupported access is CPU0 **PC103bc668**, **WRITE8 PA2c09000f=2**,
USIO, at line26601; the actual secure abort registers are `DFSR_S=1808` and
`DFAR_S=2c09000f`. This identifies a UART runtime/FIFO protocol dependency,
not a new storage allocation. The subsequent source-supported USIO model replaced
that polling transmit profile and the three source-bound ports are now connected.

The GDB command completed with exit0 in **0.380554292 s** of host wall time.
QEMU exited0 through QMP `quit`; direct comparison of the complete container
against its pre-run APFS clone passed. This host duration is not a guest clock
or performance measurement. Ignored receipts and original logs are
`run gui/original/{receipt.json,qemu.log,gdb.log,observer.gdb}`. The first host
launch rejected an overlong UNIX QMP socket path before guest initialization;
the actual run used the local TCP QMP endpoint. No GUI or whole-boot success is
claimed.

## Original synchronization after UART integration

Canonical integration **7622568d1ac2a44a4cecaf60d2dd9a3b3fd16d8d** builds the
three physical UARTs at `2c090000`, `2c090010` and `2c090020`, with RX/TX interrupt
pairs 186/187, 188/189 and 190/191. All five native UART cases pass. The original
complete container runs unchanged on this binary. Its thirty recorded callback
returns include role0 phase3 through slot2 and role1 phase3 through slot13;
role1 then actually enters slot14 at `10200462`. No unsupported transaction is
recorded. Neither a frame nor a complete boot is established by this run.

A read-only QMP stop followed by GDB captures both cores, all 512 source-sized
event records, all 512 source-sized task records, and 43 blocked task stacks.
Both cores are in original idle paths (`10107074` and `10107094`), with zero
current-task pointers. Task5 is queued on flag `01000003`, requiring bit `10`;
the flag's current pattern is zero. Task4 waits on bit2 of the same flag, while
the original main task waits for both bits `c`. Queue links and wait specifications
distinguish these flag waiters from objects in other RTOS namespaces.

The original initialization coordinator, task `010000b9`, has descriptor
`b20a16b0` and entry `10200ac3`. It reaches `10200ad4` and calls `1036820a(0)`;
its actual task record and stack show an indefinite wait on flag `01000015`,
mask1, mode1. That flag's pattern is also zero and its only queued waiter is
this coordinator. The full entry body and its later initialization calls are
captured together so the next dependency is the complete worker/producer
protocol, rather than another guessed memory range or a forced event bit.

Ignored evidence is `{receipt.json,gdb.log,events.bin
tasks.bin,stacks.log,producer.log,trace.json,waits.json}`. Complete GDB capture,
the additional stack capture and native mapped-byte disassembly all exit0;
QEMU exits0 and the input remains directly equal to the pre-run clone. The
earlier PTY interrupt ended the host debugger without a capture, and the QMP
client received a socket reset after its successful quit reply. Both are
recorded explicitly in the receipt. The guest was never restarted or modified
to obtain the subsequent complete stopped snapshot.
