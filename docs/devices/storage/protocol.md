# Storage

## Implementation

`hw/nikon/devices/storage.c` attaches QEMU's existing `nvme` PCI controller to
a caller-supplied PCI bus and `BlockBackend`. It does not reproduce NVMe registers,
queues or block I/O. The upstream controller owns namespace capacity, media I/O,
PRP validation, submission/completion queues, completion status, interrupt delivery,
flush, Abort, shutdown and reset. An error returned by the real backend cannot be
converted to successful firmware completion by this adapter.

The bus owner must provide the actual PCI host configuration aperture, memory
window, DMA address space and IRQ routing. The adapter chooses none of those
addresses. A missing bus or backend is an attachment error. The serial is an
explicit virtual-device identity; it does not claim an original card identity.

The adapter deliberately adds no parallel register state machine, card-capacity
guess, host filesystem shortcut, synthetic write acknowledgement or firmware
request-ring reply. The real guest filesystem must create its own on-media bytes.
The DDR device called SDI is unrelated to this storage endpoint.

## Original firmware contract

These execution addresses belong to the C1.70 MAS outer image. The local original
Listing/P-code projection is
`nightglass/work/worktrees/qemu-rtos-startup/work/storage/source/`:
101 complete entry providers, 3,883 unique original instructions, 330 direct calls,
2,835 expression nodes and 43 indirect transfers. The original instruction bytes,
mode, reference sites and expression producers remain in the projection. This is
analysis evidence, not recovered source acceptance or a runtime observation.

| Source entry | Actual consumer behavior | Model boundary |
|---|---|---|
| `105142ac` | Block I/O media ID, exact block-size divisibility, 64-bit LBA range checks; invokes `10513a02` | Namespace/media supplied by BlockBackend |
| `1051460c` | Block I/O2 token/event path and block-size/LBA checks; synchronous or asynchronous transfer | Native command completion, not a token fabricated in host code |
| `105138e0` | Opcode 2; namespace, starting LBA and zero-based block count populated in a command packet | NVMe Read |
| `1051396c`, `105143a0` | Opcode 1 Write packet; block callback invokes Write and then Flush on success | NVMe Write |
| `10513c74`, `10514356` | Opcode 0 packet; block callback forwards the resulting status | NVMe Flush |
| `10515704` | Constructs 64-byte SQ entries, consumes 16-byte CQ entries and phase/CID/status; maps transfer buffers through PCI I/O; submission doorbell `1000 + qid*8*2^DSTRD`, completion doorbell `1000 + (2*qid+1)*4*2^DSTRD` | Native NVMe SQ/CQ/PCI DMA |
| `105152d8` | Builds 4-KiB PRP-list pages containing 64-bit physical page addresses and chained list pages | Native NVMe PRP validation/DMA |
| `1052f152`, `1052f17e` | PCI I/O BAR 0 read/write of CC at offset `14` | Native NVMe CC |
| `1052f272` | PCI I/O BAR 0 CSTS read at offset `1c` | Native NVMe CSTS |
| `1052f42c`, `1052f4b0` | Disable/enable and poll CSTS.RDY with bounded CAP timeout | Native NVMe controller state |
| `1052f546` | Request shutdown in CC and poll CSTS.SHST | Native NVMe shutdown |
| `1052fa12` | PCI I/O attributes; CAP inspection; six 4-KiB queue pages; admin depth 2; AQA/ASQ/ACQ; Identify and synchronous/asynchronous I/O queues | Native NVMe initialization/Identify/admin queue |
| `104d4580` | Identify Namespace; derives block size from LBAF.LBADS and last LBA from NSZE; registers Block I/O protocol callbacks | Native namespace Identify data |
| `10515e24`, `10515e98` | Consumes/constructs UEFI NVMe namespace device path type 3, subtype `17`, length 16 | Guest driver behavior; no host-created firmware object |

This combination establishes an actual NVMe driver, rather than relying on a
string named NVMe. The original debug strings identify `NvmExpressBlockIo.c`,
`NvmExpressPassthru.c` and `NvmExpressHci.c`. The corresponding mature driver is
available in [TianoCore EDK II](https://github.com/tianocore/edk2/tree/master/MdeModulePkg/Bus/Pci/NvmExpressDxe).
The observation does not establish an exact EDK II version or equivalence of
every original instruction to the current upstream source.

Original initialization caller `104d4e8a` calls `1052fa12` at `104d5010`.
The PCI I/O object lives at controller offset `10`: methods `+0/+4` are BAR
read/write, `+18/+1c` map/unmap, and `+28` controls PCI attributes. Its original
provider and the host configuration/translation/IRQ implementation must be
connected before the original guest can reach this endpoint. They are not
substituted with an invented ECAM aperture.

The original COPY0 source at payload offset `2379f94` maps to RAM `b1800000`.
At source `237a800`, the GUID bytes for RAM `b180086c` are
`00b2f54cb868a54c9eecb23e3f50029a`: EFI PCI I/O Protocol. Initialization opens
that exact protocol at `104d4f1c` and stores the returned interface at
`104d4fd4`. The original registry pair at `b18005f8` names this GUID and the
interface at `b18006f0`; lookup `107546c2` returns registered interfaces.
The complete interface callback table is available in the copied source data;
its `107548xx`–`10754axx` callback bodies need a bounded original-byte decode
because that region has no saved Listing in the current index. No mapping is
inferred from the missing index entries.

The earlier DebugTool save chain reaches local file-write entry `105bc2ec` or a
peer message-ring command. `105bc2ec` constructs and queues a software request;
the filesystem-to-NVMe namespace dispatch and actual captured-frame caller remain
separate source bindings. Thus an implemented NVMe endpoint is not a claim that
the original camera has saved a RAW/JPEG frame.

## Native verification

`tests/qtest/nikon/storage.c` uses the upstream ARM `virt` PCI host strictly as a
test fixture. It uses real PCI memory decoding/bus mastering, depth-2 admin and
I/O queues, the original 64/16-byte entry geometry and 4-KiB pages. Its three
cases cover:

* Identify of actual namespace capacity/block size, a 12-KiB patterned write using a PRP list, Flush, direct verification of all
  bytes in the backing file, and a read into cleared guest RAM.
* Invalid namespace and out-of-range read errors; Abort of an already completed
  CID reports that it was not aborted; CC disable reaches CSTS.RDY=0.
* Read-only media rejects a write while retaining successful reads.

These cases exercise the mature controller protocol, not the adapter's eventual
board-specific call site or original frame-save pipeline. They do not yet test
Abort racing an outstanding asynchronous request, card hot removal, or the
original interrupt route. Build and execution are delegated to the single native
build owner; no test execution is asserted in this source commit.

See [QEMU NVMe documentation](https://www.qemu.org/docs/master/system/devices/nvme.html)
for the mature controller, namespace and BlockBackend interfaces. The project's
fixed upstream source remains the authority for available properties and APIs.
