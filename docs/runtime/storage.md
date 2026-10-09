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
RAM contents, and both outer holes. These are native qtest sources queued for
the sole build owner. No build, test pass, GUI or menu result is claimed for this
model revision before the integrated batch executes.
