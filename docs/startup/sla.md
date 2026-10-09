# SLA loaded ARM execution domain

`nikonsla -bios decoded-container.bin` validates the complete five-component
decoded container, takes an owned immutable SLA slice, and exposes all
`0x86cd40` SLA bytes to upstream ARM TCG. It is a separate machine: MAS and SLA
have different content at the same numerical ROM VA and must not overwrite
each other's image or RAM. The reusable `nikon-sla-domain` device owns its own
MemoryRegion root, AddressSpace, CPUs and peripherals; `nikonsla` is its
standalone board wrapper. This profile is a loaded software domain rather
than a cold hardware reset claim.

Direct source evidence is the fixed whole-domain provider in Nightglass
`work/sla-whole-source-4/sla.S` and its row/context metadata, producer
`1076d278`, plus `work/sla-native-data-source-3/`. The ROM first branch and reset
vector independently converge on ARM VA `0x1002bcd4`; the vector is at
`0x10034100`. The complete component is placed at diagnostic PA `0x10020000`.
Its initial ROM section descriptors at parent O `0x10400` and `0x13040`
contain the corresponding identity ROM and COPY1 mappings. This placement
supports execution under the guest's own page tables; it does not establish
physical board ownership.

The functional CPU assumption is Cortex-A7, one core by default with a finite
two-core option. ARMv7-A, CP15, mixed ARM/Thumb, UQSUB8, UDIV, WFE, DMB and generic
timer operations occur in actual source. No exact physical core or core count
has been established. CBAR `0xf8100000` is a named profile input, compatible
with the source's CBAR-relative GIC access and the image's identity descriptor
at O `0x13e04`. Upstream GICv2 has 256 interrupt IDs, secure extensions and
the normal four CPU output connections. Architectural timers use upstream
CPU timer GPIOs and PPIs 30/27/26/29 with the explicit functional 48 MHz input.
These are reusable QEMU protocols, not measured board reset facts.

The startup dependency inventory reads the complete recovered entry body and
its actual callees. The entry installs ROM TTBR and VBAR, sets six banked SPs,
programs MEMC, temporarily switches SP to `0x04008f00`, uses GPIO startup
selection and dual SDI paths, commits two MXIC region transactions, requests
zero fill, executes both COPY loops, enters Thumb and copies a new translation
table through the ordinary ROM-to-RAM copy callee. Source `0x10020960` is an
ordinary return; no external routine is substituted there. The loaded profile
reuses the existing finite MEMC, clock, GPIO, system configuration, MXIC,
SDI and fill devices, retaining unsupported access failures.

| Source owner / operation | SLA diagnostic backing | Initial ownership |
| --- | --- | --- |
| COPY1 O `[0x83c4d4,0x854234)` | `[0xc1000000,0xc1017d60)` | Guest performs COPY |
| COPY2 O `[0x854240,0x854d14)` | `[0xc1920000,0xc1920ad4)` | Guest performs COPY |
| Third tuple, zero length | No new backing | No bytes manufactured |
| ZERO start/length from `0x1002c05c/60` | `0xc1017d80`, `0x8b8000` bytes | Guest fill rounds length to 16 KiB blocks |
| ROM table copy at `0x10020270` | `0xc18e4000`, `0x4000` bytes | Guest copy owns PTEs |
| Kernel bounds at `0x1002038c/90` | `[0xc1940000,0xc5000000)` | Guest allocator owns all metadata and allocation |
| Six SPs at `0x1002c024..38` | 16 KiB per mode/core below each top | Explicit finite virtual capacity |
| Early SP `0x04008f00` | `[0x04008e00,0x04008f00)` | Explicit 256-byte virtual caller-stack assumption |
| Secondary release at `0x04008ffc` | Four RAM bytes | Guest `0x1002d880` writes magic then SEV |
| Header/sender `0x10060cc0/0x100611de` | `[0xc60f0000,0xc60fc618)` | Guest owns header, 192 slots and queue indices |

All RAM starts with QEMU's functional zero assumption. COPY destinations have
no host-installed initialized data. No release magic, inner-RAM source, ring
metadata or peer replies are prefilled. RAM page padding is private storage;
only the declared byte range is mapped. MXIC enables finite endpoints using
its real draft/commit protocol. Fill uses actual DMA memory transactions and
reports their result, rather than acknowledging an unperformed request.

Whole source further resolves the formerly unexpanded `0x100200ac` callee:
it copies the translation table, releases the secondary CPU, then tail-calls
`0x100200a4` to `0x100356c8` and `0x10036758`, the native kernel initialization
chain. Configuration `0x100202f4` supplies arena bounds `0xc1940000/c5000000`;
`0x10035758..68` stores them at `0xc101689c/c10168a0`, and the arena initializer
`0x10039e68..f18` consumes those bounds and writes the guest's metadata. This
source-declared finite arena is backed without creating host allocator state.
The old local path's Thumb call at `0x10052f70` reaches ARM `0x10020178`, whose
original instruction is a self branch; `0x100200a8` is also an original self
branch after its kernel callee. Both remain unchanged. These observations
must not be turned into a claim that all entry execution stops at O `0x178`:
the complete kernel chain occurs in its earlier callee. Actual runtime receipts
determine which frontier is reached. COPY2's conditional Thumb
call at `0xc1920000` is a real execution edge, but its flag producers and
buffer `0xc13ea858` are not established by the two COPY ranges. The complete
entry's independent BSS fill does include that buffer in this functional
profile; that establishes backing without supplying a flag producer or
proving a runtime call. The whole component is available to execution; this is not
a claim that every instruction or camera subsystem has run.

The bounded peer worker candidate `0x10053638` consumes its registered
descriptor, calls `0x10060cee` to drive logical GPIO87 high, then `0x10061116`
to drive it low and check the peer header. Its GPIO object `0xc100377c` binds
port8 bit10, with the original direction table selecting output. The MAS
consumer uses logical GPIO60, object `0xb180399c`, port6 bit3, waiting first
for high then for low. Connecting these complementary software endpoints is
an explicit functional cross-chip wiring assumption; physical wiring remains
unestablished. The named `peer-ready[0]` output forwards the original SLA
GPIO8[10] level, without generating a host ready pulse.

These worker/wrapper ranges come from continuous original-byte Thumb
candidates and COPY1 descriptors, rather than official Listing ownership or
an observed worker execution. The current official whole provider has no
direct flow to bridge helpers `0x1006c40a/0x1006c772/0x1006c0ba`.
The candidate flag-zero path sets GPIO high, calls `0x1006c40a`, then calls
`0x1006c772` before setting it low. Their actual wait conditions, MMIO binding
and runtime consumption require qualification before declaring the handshake
complete. Original runtime receipts below end before this worker.

The independent official registration `0x1006ade8` and ISR `0x1006ad78`
bind IRQ127 and bridge pending/ACK at `0x2013d020/0x2013d028`. The private
domain therefore maps the shared bridge registers at `0x20130000` and connects
IRQ127 to its own GIC. `nikon_sla_bridge(domain)` borrows this device for peer
attachment. Its data windows are not mapped by this registration contract;
the source aperture and descriptor consumers remain separate requirements.

The exact ring extent is `0x18 + 0xc0 * 0x108 = 0xc618`. The SLA guest creates
its signatures and indices at `0xc60f0000`; nothing is prefilled. Its source
requests the MAS ring `0xb7cf0000` through local view `0x47cf0000`, while MAS
requests SLA through `0x460f0000`. These source views must be forwarded by the
real 2013 descriptor protocol to the borrowed peer AddressSpace. They are not
flat aliases inferred from a page-table entry. The four-byte early release is
an SLA CPU-local boot contract and must not be mistaken for this peer protocol.
External BootROM, calibration, lens/sensor inputs and
other uncovered transactions fail and report the original access context.

The public factory `nikon_sla_create(parent, name, payload, num_cpus, serial,
errp)` refs the complete immutable SLA `GBytes` from the validated container.
The caller may unref its own payload after creation. The standalone wrapper
validates its container once and passes that same component boundary.
`nikon_sla_address_space(domain)` returns a borrowed stable peer AddressSpace;
the parent owns the device for its entire lifetime. ARM CPU memory links use
that domain's root, with local MP affinities 0/1. Upstream GIC `first-cpu-index`
uses the first actual global CPU index, so embedding after MAS CPUs does not
change SLA's banked interrupt identity. Every peripheral is mapped to the
private root and MXIC only gates that domain's explicit RAM. Fill's standard
pre-realize `memory` link selects this same root for DMA and guards. Both HDMAC
banks use this same standard link, including actual peripheral FIFO accesses. The
standalone wrapper exposes an alias solely to its own board tools; embedded
domains expose no alias to MAS. The glue owner may connect named output
`peer-ready[0]` to MAS GPIO input99 and attach the borrowed AddressSpace to
2013 forwarding; live peer-producer execution remains a runtime requirement.

The native C qtest `tests/qtest/nikon/sla.c` constructs a synthetic
full-sized SLA in a correctly checksummed five-component container. It checks
component isolation, final-byte availability, unseeded COPY/release RAM,
independent writable COPY backing, both kernel arena endpoints and causal
fill-DMA completion or failure beyond the arena. It contains
no original firmware or source opcode arrays. Qtest does not demonstrate
original instruction execution; the original-run receipts remain in `work/`
and are described separately below.

The first actual native run used the single builder's `2c0bae3` ARM binary and
the complete original decoded container, one Cortex-A7 under TCG, with native
HMP observation and guest-error logging. HMP confirmed initial PC
`0x1002bcd4`, CPSR `0x400001d3` (ARM secure SVC32). The original instructions
programmed MEMC mode `0x21`, timing `0x153b0902` and clock groups 6/5/7 before
the first rejected transaction: CPU0 PC `0x10023730`, PA `0x200300b8`, write
32-bit value `1`, `sdi-config-profile-not-supported`. This is an actual SDI
configuration difference from the earlier MAS profile's value `3`, not an
observed O `0x178` startup loop. Receipts are ignored
`work/sla/original1.json` and `work/sla/original1.log`. The builder also passed
the original native SLA loaded-domain qtest in 1.01 seconds. This run predates
the queued kernel-arena addition and complete SLA SDI profile; no COPY,
allocator, peer or full-boot success is inferred from it.

The second actual run used the builder's immutable SLA `82171ac` and SDI
`154c855` batch on canonical `b89ee41` plus its integration changes, with two
cores under single-thread TCG. Both SDI banks completed their original training
and MXIC committed 17 finite DDR endpoints per unit. Fill zeroed the actual
`0x8b8000` BSS with bus result 0 and status 1. A native GDB hardware breakpoint
at `0x100356c8` confirmed that the first 16 bytes of each COPY target matched
its original ROM source, the guest wrote release magic `0xbabebabe`, and the
new translation table contained copied descriptors. At `0x10036758`, the
previously zero kernel arena contained guest allocator metadata, the shared
SLA-core init flag was 1, and the guest had installed GIC CPU/distributor bases
`0xf8102000/0xf8101000` and secure physical timer IRQ 29. CPU1 consumed the
release and reached its original WFE callee `0x10050060`. These are observed
native instructions and sampled RAM, not a full-byte COPY comparison or full
application boot claim. Receipts remain in ignored `work/sla/original2*`.

The next actual rejected transaction was CPU0 PC `0x10020134`, a 32-bit read
from `0x04266000`. The complete `0x1002012c/0x10020158` counter lifecycle
obtains this address through the guest's COPY1 data at `0xc1000000`, disables
control bits 0/1, zeroes both count words, programs 48 MHz and then enables
control 3. The profile now maps the existing finite timestamp QOM device at
that exact address, with its virtual-time clock and real stopped/running count.

The runtime callback `0x10020294` also supplies literal `0x2c090000` to
`0x10020180`, whose byte/halfword writes implement the existing USIO protocol.
`0x100201ec` polls status byte bit 1 and writes the TX halfword. This isolated
SLA port reuses the shared native USIO device and optional `-serial` backend;
no UART IRQ connection has been established for these polling callees.
The native qtest verifies timestamp advance and stop, plus the isolated port's
TX-ready read. Source `0x10043af4`, named `mailbox` by the initialization string,
allocates 36-byte RTOS objects through the ordinary guest allocator and links
them through COPY1 globals `0xc1017c10/0xc1017c18`. Its name supplies no
physical cross-domain mailbox contract. The `TSmpcallIpiIrq/Level` configuration
and GIC paths operate on SLA core-local state, also without a proven MAS wire.

The third original run used machine `46874de` with the builder's passing native
counter/USIO fixture. A hardware breakpoint reached the actual InitTask at
`0x10035678`, CPSR `0x13` (ARM SVC32), with guest-allocated SP `0xc19b5008`.
The kernel's `0x10052eb8` argument is a task descriptor: task-create consumer
`0x1003bfc4/8` copies its entry field at offset 8 into the TCB, rather than
executing the descriptor address. The actual InitTask calls `0x1003aa38` and
`0x10036630` before tail-calling `0x1003ab10`. Its newly confirmed source root
and remaining descriptor resources are owned by the recovery producer.
Original application initialization then reached `0x100537a4`, and the next
rejected transaction was CPU0 PC `0x10053860`, a 32-bit zero write to
`0x04287000`. CPU1 progressed into its RTOS path at `0x10038a68`. Native GDB
stopped both CPUs for observation and quit the VM; no firmware branch was
changed. The UART file remained empty at this frontier. Ignored receipts are
`work/sla/original3*`.

This run also exposed the original timer initialization's GIC operations:
`0x10036cc0` writes GICD_ISACTIVER, which upstream QEMU implements for revision
2; the initial revision-1 profile logged rejected distributor offsets
`0x300..303`. The current functional profile therefore selects upstream GICv2
and verifies its IIDR in the native fixture. The firmware's separate
`0x10036d14` read of write-only GICC_EOIR remains visible as an upstream guest
error; this profile does not manufacture a readback register or silence it.
GICv2 is an execution capability supported by these source accesses, not a
new claim about the physical chip.

The private-domain native fixture passed with the named peer output following
GPIO direction and masked latch writes (builder `56a99b9`). Original two-core
standalone runs `original4` and controlled `original4b` instead observed CPU0
polling `0xc1017d40 == 1` at `0x10050038`, while CPU1 was halted after WFE at
`0x1005006c`. The worker breakpoints were not reached. Both retained the
complete image and guest-created state. The controlled retry made no debugger
MMIO reads and ended by `monitor quit`, with both processes exiting0. The first
run's later debugger read of banked GIC MMIO crashed the observer; that is
recorded separately from guest behavior in ignored runtime receipts.

Whole source has release SEV/WFE at `0x1002d890/0x1002bff0` and kernel barrier
SEV/WFE at `0x10050014/0x10050068`. The secondary's counter producer is
`0x10052b08`, using LDREX/STREX before waiting for the primary to clear it.
Upstream ARM TCG broadcasts SEV to all ARM CPUs, including separately mapped
domains. A four-core embedded run is therefore a distinct execution context;
its progress does not prove this two-core barrier or a physical cross-domain
event topology. No host event or counter value is injected to release it.

The fifth original standalone run used the builder's `f114196` binary, two SLA
CPUs and standard `-accel tcg,thread=multi`. It reached the hardware breakpoint
at `0x100538b0`, CPSR `0x60000033` (Thumb SVC32), SP `0xc19b4fe0`, with the
barrier counter `0xc1017d40` cleared by the guest and the secondary executing
its normal IRQ path. This is a separate natural scheduling comparison, with
no MAS CPU, host flag or firmware edit. It passed the complete MXIC configuration
family and next rejected CPU1's original `0x1005ed02` write of 1 to SPI5 reset
at `0x2c0a0560`. The peer worker breakpoints were not reached. Both processes
exited0 through `monitor quit`; ignored receipts are `work/sla/original5*`.
Use `-smp 2 -accel tcg,thread=multi` for this loaded two-core profile. The
earlier single-thread barrier receipt remains a distinct scheduling limitation.

The complete SPI initialization and operating family is source
`0x1005ec54..0x1005f632`, rather than a special SPI5 reset exception. COPY1
registry `0xc10015e8` has eleven `0x58`-byte records (parent payload offsets
`[0x83dabc,0x83de84)`). For channel `i=0..10`, it supplies MMIO
`0x2c0a0000 + 0x100*i`, first IRQ `193+2*i`, second IRQ `192+2*i`, and both
callbacks reach the common pending/W1C ISR `0x1005f2f8`. The shared SPI
device's aggregate IRQ uses the first callback line; the second remains idle,
an explicit functional routing convention rather than a measured physical split.
Each private instance retains its SSI peer seam, reset/cancel lifecycle,
bounded FIFOs, 48 MHz functional master clock and ordinary failure paths.
No protocol reply is installed for a disconnected peripheral.

Source mode4 at `0x1005f1fc..0x1005f23c` enables CTRL bit `0x400` and EN bit0.
The actual external DMA consumers `0x1005c70e/0x1005c786` obtain RX FIFO `+4`
from `0x1005f286` and TX FIFO `+0` from `0x1005f28c`. Their eleven records at
`0xc1007238`, stride `0x68` (payload `[0x84370c,0x843b84)`), supply byte
`+0x20` TX selector `3+2*i` and byte `+0x21` RX selector `4+2*i`.
The driver `0x1005f782` writes these into CA bits24..28. It uses halfword beats,
fixed peripheral address and optional advancing/fixed RAM endpoint, terminal
count minus one, and real completion/error interrupt enables. No request level
is substituted for SPI's actual enabled FIFO-room/data signals.

The independent HDMAC registry `0xc1004db8` consists of two banks with eight
`0x24`-byte records each (payload `[0x84128c,0x8414cc)`). Bank0 binds
`0x2c020000`, IRQ150..157 and logical objects `0x12a..0x131`; bank1 binds
`0x2c030000`, IRQ142..149 and objects `0x132..0x139`. Its pointer table at
`0xc1004cb4` (payload `[0x841188,0x841288)`) supplies channel CA/CB/SA/DA
at `base+0x10+0x10*channel`, offsets0/4/8/12. Source
`0x1005f644..0x1005f8aa` consumes the complete geometry. The domain connects
each SPI TX/RX demand to its corresponding selector in both private HDMAC
banks. This shared-selector fanout is an explicit functional request topology
assumption; registry and consumer fields establish the selector values, without
establishing the physical fanout. Transfers use actual selected AddressSpace
transactions and propagate missing/readonly destination errors.

The native fixture exercises all eleven reset/capacity windows and TX request
and RX selectors, alternating both private banks. An armed halfword transfer remains
pending while SPI EN is zero; enabling SPI permits the actual RAM-to-FIFO beat
and only then records HDMAC completion. After the actual master frame, ordinary
disconnected upstream SSI supplies MISO-low to the RX FIFO; its real data demand
drives the RX halfword transfer and replaces a RAM sentinel. This tests causal private wiring with
synthetic data, independently of the original runtime receipt above.
