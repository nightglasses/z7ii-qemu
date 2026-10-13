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
without losing RAM contents, both outer holes, all six display-buffer bases,
the display arena's exact neighbors, and reset retention. Build and execution
results are separately scoped below.

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
`work/gui/original/{receipt.json,qemu.log,gdb.log,observer.gdb}`. The first host
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

Ignored evidence is `work/gui/namespace/{receipt.json,gdb.log,events.bin,
tasks.bin,stacks.log,producer.log,trace.json,waits.json}`. Complete GDB capture,
the additional stack capture and native mapped-byte disassembly all exit0;
QEMU exits0 and the input remains directly equal to the pre-run clone. The
earlier PTY interrupt ended the host debugger without a capture, and the QMP
client received a socket reset after its successful quit reply. Both are
recorded explicitly in the receipt. The guest was never restarted or modified
to obtain the subsequent complete stopped snapshot.
