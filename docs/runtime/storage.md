# Complete software storage profile

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

`tests/qtest/nikon/memory.c` covers every class's first and last word, separate
retention across adjacent classes, original copy/stack/heap destinations,
logical bus mirror exclusion, MXIC decode withdrawal/restoration without losing
RAM contents, and both outer holes. Build and execution results are separately
scoped below.

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
not a new storage allocation. A separate owner is replacing the prior polling
transmit value profile with the complete source-supported USIO model.

The GDB command completed with exit0 in **0.380554292 s** of host wall time.
QEMU exited0 through QMP `quit`; direct comparison of the complete container
against its pre-run APFS clone passed. This host duration is not a guest clock
or performance measurement. Ignored receipts and original logs are
`work/gui/original/{receipt.json,qemu.log,gdb.log,observer.gdb}`. The first host
launch rejected an overlong UNIX QMP socket path before guest initialization;
the actual run used the local TCP QMP endpoint. No GUI or whole-boot success is
claimed.
