# Storage

## Implementation

`hw/nikon/devices/storage.c` attaches QEMU's existing `nvme` PCI controller to
a caller-supplied PCI bus and `BlockBackend`. It does not reproduce NVMe registers,
queues or block I/O. The upstream controller owns namespace capacity, media I/O,
PRP validation, submission/completion queues, completion status, interrupt delivery,
flush, Abort, shutdown and reset. An error returned by the real backend cannot be
converted to successful firmware completion by this adapter.

`nikon.storage` now provides the source-local host ports on a private mature
GPEX PCI bus. The native endpoint has private model devfn8; that placement is
not an original physical BDF claim. The source CFG0 transaction targets the
external endpoint directly, independently of GPEX's root device. The `drive`
property is a BlockBackend ID string; only the native NVMe endpoint attaches
that backend. Missing media leaves the host offline. The endpoint's serial is
an explicit virtual identity, not an original card identity.

The adapter deliberately adds no parallel NVMe register state machine, card-capacity
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
read/write, `+18/+1c` map/unmap, and `+28` receives attribute-shaped calls. Its original
provider and the host configuration/translation/IRQ implementation must be
connected before the original guest can reach this endpoint. They are not
substituted with an invented ECAM aperture.

The original COPY0 source at payload offset `2379f94` maps to RAM `b1800000`.
At source `237a800`, the GUID bytes for RAM `b180086c` are
`00b2f54cb868a54c9eecb23e3f50029a`: EFI PCI I/O Protocol. Initialization opens
that exact protocol at `104d4f1c` and stores the returned interface at
`104d4fd4`. The original registry pair at `b18005f8` names this GUID and the
interface at `b18006f0`; lookup `107546c2` returns registered interfaces.
The complete interface callback table is available in the copied source data.
Its `107548xx`–`10754axx` callback bodies were decoded over their bounded original
byte range using the existing GNU ARM objdump, because that region has no saved
Listing in the current index. Mem.Read/Write `10754846`/`107548c0` use literal
`2a810000` plus the passed offset; Map `10754a34` returns the supplied address as
`{address, 0}` with an identity mapping handle. Config.Read/Write
`10754938`/`107549b6` use the original selector `b1800721` whose bytes are
`00010000` (selector bytes 1 and 2 are 1 and 0). Helper `10517b24` computes config addresses:

* selector byte 1 is zero: `2a900000 + selector[2]*1000 + offset`;
* otherwise: `2a7f8000 + selector[1]*8000 + selector[2]*1000 + offset`.

Thus the actual endpoint config aperture is `2a800000` and its BAR access is
`2a810000`; this is not permission to map an arbitrary full ECAM range. The
interface is a compact original port of PCI I/O, not the complete modern EFI
structure. At `+28`, `1052fa12` makes attribute-shaped calls but the actual
callback `10754ab0` returns zero without a hardware effect. At `+2c`, the
passthrough consumer calls `10754828`, which performs the original cache flush.
The original `10517b40` creates two outbound iATU regions at root-config offset
`900`: region 0 has CFG0 type 4, local range `[2a800000,2a810000)`, target zero;
region 1 has memory type 0, local range `[2a810000,2a900000)`, target `2a810000`.
Both enable control bit 31. The selector formula is not a proven standard BDF;
in particular, the actual CFG0 target is zero, not a guessed device-1 target.
The upstream DesignWare host has matching iATU register fields, but its config
access interprets target zero as bus 0/devfn 0 on its root bus. That resolves its
root bridge, whereas this original CFG0 transaction is intended for the external
endpoint. Reusing that host without resolving the transaction/bus relationship
would attach the correct NVMe protocol to the wrong config object.

Host `10517aa6` registers handle `149` at `b1801774`; this is a service ID, not
an IRQ number. `105162be` obtains that exact handle and stores it at `b1800744`.
The original initializes PHY state at `2b200000` and polls link-state value `11`
through `10517212`, up to 500 sleeps of 1 ms. Hardware reset values and link
training timing are not established by these source consumers.

The original registers and enables IRQs `f2`, `f3`, `f4`, `f6`, with handlers
`10517a7e`, `10517a18`, `105179f0`, `10517980`. Handler `f3` reads/acknowledges
PHY status `2b200024/2b20002c` through `1051703e`; status word bits 28/29 feed
host callbacks. Their installed bridge `105156e0/105156f2` reaches the original
BootServices event and sets flags `200/400`. Handler `f6` reads root MSI status
`2a900830`. The original CQ-create body `1052f79a` creates queue IDs 1/2 with
PC=1, IEN=1 and interrupt vector 0. These are actual IRQ/event consumers; they
must be joined to real PCI interrupt causes by the host frontend. Merely wiring
a guessed NVMe GIC line would skip these consumers.

The DebugTool save chain reaches local file-write entry `105bc2ec` or a peer
message-ring command. The local file request worker `1044b300`, command
`42091001`, reaches `1044b270` → `1047109c` → `104666fe` → `104655c2` →
`1047e768`, which invokes the registered sector callback at
`b2b464c0 + volume*88 + 1c`. The original provider registry `10481aec` consumes
COPY0 table `b1808ac4`; the XQD-named registration's write `104825b2` reaches
`1049d800`. In mode 1, `104b7ab6` installs table `b1801738`, whose write callback
is `104b7a42`. That callback invokes `104d5a3a`, which loads Block I/O from
`b18012ec`, obtains the media ID and block size, converts sector count to bytes
and directly calls `105143a0` at `104d5a56`. The matching read path reaches
`105142ac`. This is an actual file-sector-NVMe bridge; it is not inferred from
the XQD label or physical card specification. The CF-named table's current
callback is a stub and must not be relabeled as NVMe.

Normal file-object write `10567d8e` → `105671ba` also uses the original write
API `105bea78` through literal `10567fa8=105bea79`, then updates file position
and size. The exact captured-frame producer, format and original save completion
are still to be joined. Thus this implemented and tested endpoint does not claim
that the original camera has saved a RAW/JPEG frame. The complete host and
consumer receipts are in the same ignored work tree under `storage/providers/`
and `storage/consumer/`; no firmware image is committed.

## Source-local host implementation

| SysBus port | Source address/extent | Implemented semantics |
|---|---|---|
| 0 DBI | `2a900000`, 4KiB | Native root PCI configuration below100; finite iATU900..91c, MSI820..830, source168/80c/8a8/8bc |
| 1 CFG0 | `2a800000`, 64KiB | Source region0 type4/target0 selects the external native endpoint's 4KiB config; other slots reject transactions |
| 2 memory | `2a810000`, 960KiB | Source region1 type0/target2a810000 aliases native PCI BAR memory; disabled windows reject transactions |
| 3 PHY | `2b200000`, 1KiB | Only source-declared offsets accepted; controls, finite status, causal training/cancel and W1C events |
| 4 platform | `2b100000`, 16 bytes | Complete10516a28/3e/4c configuration fields; retained values without invented readiness or IRQ |

Four SysBus IRQ outputs are ordered `f2/f3/f4/f6` (242/243/244/246). Real native
INTx assertion/deassertion latches PHY24 bits28/29 and drives f3. Native PCI MSI
writes latch DBI830, subject to enable828 and mask82c, and drive f6; W1C830
acknowledges them. f2/f4 remain low without an implemented source-proven cause;
they are not used as a substitute completion interrupt. Upstream NVMe supplies
MSI-X rather than ordinary MSI. The firmware's capability-ID5 branch remains
conditional; no fake MSI capability is installed.

The required pre-realize QOM `memory` link selects the actual CPU-domain memory
region. MAS machine glue supplies `get_system_memory()`; an isolated SLA domain
must supply its own root region. Realization rejects a missing link. PCI DMA uses
a private identity address space aliasing that explicitly selected memory.
Its only overlay is the programmed four-byte MSI destination at DBI820/824.
That overlay is absent from CPU memory: CPU data at `b1800740` stays RAM and
cannot trigger a PCI interrupt. All other command/data DMA retains real memory
transaction results and the existing native PRP validation.

Attached media, PHY mode4/startbit4, control+c bits0/1, and +50bit8 are required
before a training request can complete. The function model uses an explicitly
assumed 1ms virtual training delay, state11, DBI98 low4=2, and boolean PHY210=1
after success. These are software-compatible functional assumptions, not measured
PCI link speed, timing or performance. PHY +58 state fields never become ready
from reads alone. Missing media remains offline. The named GPIO output `link`
index0 is high only in state11; machine glue connects it to the existing signal
device's route3 input, which owns filtering and raw2b000010 bit10. The host never
overlaps or seeds that signal register bank.

Cancellation is requested by a rising PHY3c bit4 or PHY50 bit16. It first invokes
native PCI/NVMe reset, whose existing controller path drains namespaces and cancels
in-flight commands before freeing queues. Native PCI configuration/BARs reset.
Only then does a 1ms virtual completion publish state15 and PHY2c bit26 or bit30,
respectively, through f3. The consumed request bit clears on completion as an
explicit functional assumption, allowing repeated source cancellations. An ACK
cannot retrain; a fresh explicit start is required.
Reset or loss of the programmed link controls cancels pending timers and drops
link/IRQ outputs. Diagnostic status fields without a proven producer have a
declared cold-zero functional reset value, not a claimed hardware reset value.
Unlisted PHY/DBI offsets and config outside the actual external slot reject
transactions; unsupported iATU regions do not map memory. Snapshot migration is
explicitly unavailable for this functional host.

The independent platform producer `104b7b88` writes the four fields at2b100000
before invoking the storage path: +0 has bits8/0, +4 has bit0, +8 carries its
source word, and +c has bits20/16. They are modeled together rather than leaving
four unmapped writes. Their physical clock/power identity and relation to link
training are not proven, so stored configuration never manufactures a completion
or performance measurement. Source DBI168/80c are also present because the full
training producer10517c6a accesses them before PHY start.

## SLA source profile

The existing SLA SOURCE11 Listing was audited as a complete related family,
rather than assigning a role from address similarity. Source ROM execution
addresses below use `O = VA - 10020000` in component04. The bounded original
projection is `nightglass/work/worktrees/qemu-rtos-startup/work/storage/sla/`:
`family.json` retains 4,050 original rows and incoming reference sites;
`phy.json` retains the 821 low-level rows and every observed PHY-offset access.
The existing source has not been accepted as recovered, compiled source, and
these static results do not establish original SLA runtime consumption.

| SLA source family | Established software contract | Shared model |
|---|---|---|
| `10089c74/10089d52` | Packed controls at PHY+c/+10, including the normal/inverted setter polarity | Existing finite control fields |
| `10089dbe/10089e16/10089e46` | PHY+4/+8 controls and status-bit extraction | Existing fields; no invented event producer |
| `10089e6a/10089e86/10089ef8` | Mode/start, +14/+18, +24/+28/+2c event masks | Existing configuration and W1C events |
| `1008a230/1008a24e/1008a290/1008a2aa` | Masked status reads and acknowledgements, including cancellation events | Existing finite event protocol |
| `1008a326/1008a3b0/1008a422` | +3c/+4c/+50/+54 controls and +58/+5c state decoding | Existing request controls and state fields |
| `1008a476` | Complete packed configuration at +100/+104 and +118..+15c | All declared offsets already supported |
| `1008a930/1008a938` | +210 link and +218 diagnostic decoding | Existing status interface |
| `1008ad34/1008ad50/1008ae7a/1008b8c6` | Config selector, DBI/BAR and iATU setup | Existing finite DBI/CFG0/memory ports |
| `1008b0e8/1008b32e` | Root initialization, explicit start, up to500 sleeps of1ms waiting for decoded state11 | Attached counterpart required; no read-triggered readiness |
| `1008b958/1008ba86` | Peer/root cancellation through +50bit16/+3cbit4; consume state15 | Existing causal cancellation protocol |

The literal at SLA VA`1008a028`, O`6a028`, contains bytes`0000202b`, establishing
PHY base`2b200000`. The two concrete full-initialization callers `10088a82`
and `10093476` pass selectors `c1000720` and `c1001230`. Original COPY1
`O83c4d4 -> c1000000` supplies bytes`00000000` at offsets`83cbf4/83d704`.
Both use mode0: `1008b0e8` selects PHY mode4, programs root DBI/windows, then
sets PHY0 to14. The separate nonzero-mode branch exists but does not prove a
connected peer. The config selector's byte1/byte2 formula is the same as MAS;
neither a standard BDF nor an early external config transaction is inferred.

`1008acb6/1008acf4` register/enable the same four source IRQs f2/f3/f4/f6.
`1008ac28` translates the cancellation status words into event flags2/1.
SLA guard `1008ba1e` checks host+14 and a filtered external route status;
it does not impose MAS's extra DBI98 low-nibble2 check. The model does not
introduce that MAS software guard into SLA.

SLA machines may reuse ports DBI`2a900000`, CFG0`2a800000`, memory`2a810000`
and PHY`2b200000`, with the extents above, in their private address space.
Before realization they must link `memory` to `NikonSLADomain.system`. They
must not map the MAS platform port at`2b100000`: scoped SLA initialization
instead uses the existing signal route at`2b000004/2b000010` and helper
`10089b06` selects `2b001000` or`2b001100`. Literal`10089c00/O69c00` is
`0010002b`. The missing Listing interval`10089b34..10089c74` cannot be assigned
the MAS platform identity merely because its original bytes are present.

The PHY/DBI model can be realized without a drive; it then exposes the finite
register contract while staying offline. A private SLA mapping does not justify
attaching another card, sharing MAS media, asserting an external link, or
connecting an unproved peer. Machine IRQ and signal wiring remain the machine
owner's responsibility. Private-domain DMA selection is implemented by the
explicit memory link; its SLA runtime behavior has not yet been exercised.

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

Three additional source-host cases use the diagnostic Z7II machine, a four-byte
loop BIOS and the same real backend:

* Missing media does not train or generate a completion/IRQ.
* Original two-window iATU/config/PHY sequence selects native NVMe rather than the
  root bridge; real sector Write/Flush reaches backend bytes, native INTx drives
  original f3 events, cancellation drains/resets before completion and requires
  explicit restart/configuration.
* The real native MSI-X vector issues a PCI write to the original MSI destination;
  receiver masking/W1C drives f6, while CPU RAM at that address remains unchanged.

The sole builder compiled and linked the complete host with the actual board
mapping, then ran all three added host cases successfully in 0.71s. The local
receipt is `work/worktrees/native-qemu-source/work/validation/gui/storageqomfinal.log`.
The tests do not claim original frame-save execution, racing Abort, or card hot
removal. Build and execution are delegated to the single native
build owner. The sole build owner ran these three cases successfully in 0.78 s;
Identify is part of the roundtrip case. This result applies to the ARM `virt`
fixture and upstream NVMe endpoint, without extending it to original Z7II host
execution or a captured-frame save.

See [QEMU NVMe documentation](https://www.qemu.org/docs/master/system/devices/nvme.html)
for the mature controller, namespace and BlockBackend interfaces. The project's
fixed upstream source remains the authority for available properties and APIs.

The private AddressSpace selection was compiled as part of fixed source
`7f241eb9`. Its necessary native host regression exited0 in1.32s, and the
cancellation roundtrip exited0 in0.15s: `work/storagedomainhostnativereceipt.log`
and `work/storagedomainroundtripnativereceipt.log`. These are the existing
synthetic host interfaces; they do not establish original SLA DMA execution,
physical link training or an original frame save.
