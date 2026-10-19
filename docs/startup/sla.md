# SLA loaded ARM execution domain

`nikonsla -bios decoded-container.bin` validates the complete five-component
decoded container, takes an owned immutable SLA slice, and exposes all
`0x86cd40` SLA bytes to upstream ARM TCG. It is a separate machine: MAS and SLA
have different content at the same numerical ROM VA and must not overwrite
each other's image or RAM. The reusable `nikon-sla-domain` device owns its own
MemoryRegion root, AddressSpace, CPUs and peripherals; `nikonsla` is its
standalone board wrapper. This profile is a loaded software domain rather
than a cold hardware reset claim.

The default input remains strict: external NOR `[0x10000000,0x10020000)` is
unmapped. An explicit `-machine nikonsla,boot-prefix=erased` selects a
counterfactual reset input of128KiB erased `0xff` bytes. Alternatively,
`boot-prefix=/absolute/path/prefix.bin` supplies an exact128KiB external file.
Files of any other size fail; absence does not select erased data or another
component. This source input is separate from the immutable package payload
and from recovered firmware source. The public `nikon_boot_prefix_load`
parser returns owned bytes, and `nikon_sla_create` references the optional
prefix alongside the payload. Embedded boards select this same input explicitly.

The input occupies only the private readonly NOR prefix. Both NOR regions
use the existing ROM-device and MEMC protocols with their own base addresses.
Neither option preloads C8 RAM, alters page tables or writes success state:
the original guest must submit COPY, receive its real result and remap itself.
Erased-mode execution is a run with a functional external assumption, not proof
of an original BootROM or a boot without assumptions. The strict-mode COPY
error remains available and unchanged. The focused fixture checks the whole
prefix/package crossing byte for byte, readonly CPU/DMA rejection, no destination
preseed, retained input on reset, exact-file validation and default failure.

Direct source evidence is the fixed whole-domain provider in Nightglass
`work/sla-whole-source-4/sla.S` and its row/context metadata, producer
`1076d278`, plus `work/sla-native-data-source-3/`. The peripheral dispatch
inventory uses `work/sla-whole-source-5/sla.S`, producer
`43d1909ecd2fb27e422263d01f9fcf2ab390efcd`. Subsequent official I2C/TIMING
callback qualification is in the source owner's
`work/sla-supplement/devices-callback-source/instructions.jsonl`.
The ROM first branch and reset
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
at O `0x13e04`. Upstream GICv1 has 256 interrupt IDs, secure extensions and
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

The adjacent peripheral initializers are bound from complete COPY1 registries.
Source `0x10067016` consumes the four `0x24`-byte COPY records at `0xc1005c48`
(original O `0x84211c`), IRQ45..48 and the register pointer table at
`0xc1005bb8` (O `0x84208c`). The shared four-channel COPY device maps privately
at `0x04251000`; its standard `memory` link selects the SLA root before
realization. DMA reads and writes use that domain, including ROM-device reads,
readonly failures and cancellation, rather than the MAS system AddressSpace.

The four `0x30`-byte I2C records at `0xc1005004` (COPY1 O `0x8414d8`) supply
instances0..3, bases `0x2c018000/0x2c019000/0x2c01a000/0x2c01b000` and
IRQ214..217. Each mature controller has its own disconnected I2CBus and uses
no DMA or global AddressSpace. The source-proved initializer and normal FIFO,
IRQ and cancel profile are supported; original transaction success is not
claimed. Official source5 still lacks configuration body `0x1005f9ac..1005fa3b`;
its final IRQ thunks have since received independent callback-source
qualification. Unsupported monitor/extended-address
variants retain their model failures pending that source qualification.

The twelve TIMING records at `0xc1005904` (COPY1 O `0x841dd8`, stride20)
reference the complete twelve-by23 register pointers at `0xc10054b0`
(O `0x841984`). ROM IRQ records at VA `0x1016afbc` (O `0x14afbc`) bind
IRQ172..183. One private mature controller maps its two regions at
`0x2c010000/0x2c011000` and connects all twelve IRQ outputs to the SLA GIC.
Its nominal3GHz clock and functional timing behavior remain explicit model
assumptions. Capture inputs and compare outputs have no external peer;
unsupported submission/extra-event modes still fail. The source owner's
callback supplement qualifies the wrappers `0x100603c0..10060432` through
the original IRQ table and constructor. Their common handler reads status
through object+8, writes the same word to acknowledge it, and optionally
calls the guest callback stored at object+`0x10` with argument at object+`0xc`.

USIO registration `0x10058be2` consumes three `0x58`-byte COPY1 descriptors
at `0xc10059fc` (original O `0x841ed0`). Their offsets0/`0x10`/`0x20` and
DR literal `0x10058e4c=0x2c090004`, with actual halfword DR consumers,
bind three private ports at `0x2c090000/0x2c090010/0x2c090020` and
RX/TX IRQ pairs186/187,188/189,190/191. Port0 retains the explicit serial
backend used by the earlier polled startup callback; the other ports have no
input backend and produce no invented RX data. Mature FIFO/IRQ behavior is
used for the qualified transactions. The missing official low-level body
`0x10058e74..100591ae` remains a variant qualification limit; word MMIO and
unsupported clock/mode variants are not enabled merely from these descriptors.

The first mode1 registration `0x10057956` binds object104 at `0xc10015c8`
(COPY1 O `0x83da9c`), IRQ158 and the register literal
`0x10057ac8=0x2c01200c`. Its finite event interface maps privately at
`0x2c012000`, with source mask bits10..12 and pending bits1..3. Functional
zero reset and clear-on-zero semantics are explicit assumptions. The explicit
event publisher has no connected provider, so no interrupt source or timing
event is manufactured. Missing recovered setter bodies remain a qualification
limit; this mapping does not claim the entire physical event subsystem.

The other mode2 registrations `0x1005fe2c/0x100604fc` bind the finite notifier
at `0x2c0b0000`, IRQ218, and three counter windows at `0x2c015000` with offsets
0/`0x20`/`0x40`, IRQ169..171. COPY1 counter descriptor `0xc1005bac`
(O `0x842080`) contains those exact offsets; notifier software state at
`0xc1005480..0xc100549c` (O `0x841954..841970`) is initialized zero data.
Both private devices expose explicit opaque event/state inputs which remain
disconnected. Submit/configuration bits are retained without inventing a
clock, busy producer or automatic completion. Missing official helper bodies
remain independent source limits, rather than grounds for an inferred peer.

Source `0x10057b7e/0x10057bc8` binds two Audio records at `0xc1005100/5134`
(COPY1 O `0x8415d4/841608`, stride `0x34`), the register literal
`0x10057ed8=0x2c080000`, and IRQ167/168. The private mature passive FIFO
device supports the qualified release-only initialization; it performs no
AddressSpace transactions. Its DMA demand outputs remain disconnected.
Source Audio selectors3/7/5 overlap the SPI selector profile, and complete
Audio bank/handle assignment plus physical request mux selection are not yet
qualified. Connecting a second producer directly to one HDMAC input would
discard causal line state; no such wiring, inferred OR/mux or DMA completion
is supplied. Missing Audio setter bodies remain a source boundary.

The second registration dispatch `0x100548d8` independently calls
`0x1005bf0e/0x1005bf4c`, registering and enabling completion IRQ250 with
literal `0x1005c078=0x04287000`. The already instantiated private completion
device therefore connects its real IRQ output to the SLA GIC. The earlier
initialization still consumes its same completion protocol by polling; this
additional route supplies no request or completion event.

Second-phase mode2 registration `0x100608c6` consumes three command objects
at `0xc1006438` (COPY1 O `0x84290c`, stride20), with initialized masks
`0x0fff0000/0x01ff0001/0x01ff0002`. The complete75-word ROM pointer table at
VA `0x1016b2d8` (O `0x14b2d8`) binds three private register roots
`0x04250000/0x24061000/0x2c041000`. Their20 IRQ outputs map to IRQ49..64,
227/228 and247/248. The source protocol publishes and receives opaque word
messages; it does not establish a source/destination/count memory-copy engine.
The shared command device's typed request/provider seam stays disconnected,
so starting a command does not create a peer frame or perform a firmware clone.

| Source owner / operation | SLA diagnostic backing | Initial ownership |
| --- | --- | --- |
| COPY1 O `[0x83c4d4,0x854234)` | `[0xc1000000,0xc1017d60)` | Guest performs COPY |
| COPY2 O `[0x854240,0x854d14)` | `[0xc1920000,0xc1920ad4)` | Guest performs COPY |
| Third startup tuple, zero length | No startup copy | Later runtime capacity is qualified separately below |
| ZERO start/length from `0x1002c05c/60` | `0xc1017d80`, `0x8b8000` bytes | Guest fill rounds length to 16 KiB blocks |
| ROM table copy at `0x10020270` | `0xc18e4000`, `0x4000` bytes | Guest copy owns PTEs |
| Kernel bounds at `0x1002038c/90` | `[0xc1940000,0xc5000000)` | Guest allocator owns all metadata and allocation |
| Six SPs at `0x1002c024..38` | 16 KiB per mode/core below each top | Explicit finite virtual capacity |
| Early SP `0x04008f00` | `[0x04008e00,0x04008f00)` | Explicit 256-byte virtual caller-stack assumption |
| Secondary release at `0x04008ffc` | Four RAM bytes | Guest `0x1002d880` writes magic then SEV |
| Header/sender `0x10060cc0/0x100611de` | `[0xc60f0000,0xc60fc618)` | Guest owns header, 192 slots and queue indices |
| Constructor `0x100658e4`, classifier `0x10065ad8` | `[0xc8200000,0xc8900000)` | Ordinary private7MiB RAM, no code clone supplied |

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
complete. The first six runtime receipts end before this worker; the seventh
observes its actual execution, as recorded below.

The independent official registration `0x1006ade8` and ISR `0x1006ad78`
bind IRQ127 and bridge pending/ACK at `0x2013d020/0x2013d028`. The private
domain therefore maps the shared bridge registers at `0x20130000` and connects
IRQ127 to its own GIC. `nikon_sla_bridge(domain)` borrows this device for peer
attachment. Its data windows are not mapped by this registration contract;
the independent class0 converter `0x10060f9c` and CPU LDRD/STRD consumers
`0x10064728/0x1006474a` qualify the IO aperture `[0x40000000,0x50000000)`.
The domain maps that exact interval to bridge bank1 region2 at offset
`0x40000000`. This is a protocol IO window into the borrowed peer AddressSpace:
the bridge retains its source-bound masks, fields, RMW and reset semantics.
Cold zero mappings have no valid mask and do not forward; the static index8
field does not establish a new hardware variant. There is no extra host
publication scoreboard. The alias supplies no RAM, ring signatures or default
peer bytes. Incomplete recovered publisher bodies remain a source qualification
limit, independently of the qualified converter and CPU accessors.

The exact ring extent is `0x18 + 0xc0 * 0x108 = 0xc618`. The SLA guest creates
its signatures and indices at `0xc60f0000`; nothing is prefilled. Its source
requests the MAS ring `0xb7cf0000` through local view `0x47cf0000`, while MAS
requests SLA through `0x460f0000`. These source views must be forwarded by the
real 2013 descriptor protocol to the borrowed peer AddressSpace. They are not
flat aliases inferred from a page-table entry. The four-byte early release is
an SLA CPU-local boot contract and must not be mistaken for this peer protocol.
External BootROM, calibration, lens/sensor inputs and
other uncovered transactions fail and report the original access context.

The public factory `nikon_sla_create(parent, name, payload, boot_prefix,
boot_shim, bridge_notifications, num_cpus, serial, errp)` refs the complete
immutable SLA `GBytes` from the validated container. The composed
complete bundle composition supplies the bridge-notification choice before the
private SLA bridge is realized; standalone SLA has no peer to notify.
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

The native C qtest `vendor/qemu/tests/qtest/nikon/sla.c` constructs a synthetic
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

This run exposed the original timer initialization's GIC operations, but its
initial interpretation was incomplete. `10036CAC..10036CC0` reads the active
view, ORs the timer's bit29 and writes it back. A revision2 implementation
turns that store into SET_ACTIVE29, suppressing timer delivery; implementing
the write is therefore insufficient. The current functional profile uses the
mature revision1 read-only active view, as does the MAS profile. Upstream logs
the ignored writes at distributor offsets300..303. This is an explicit GIC
profile convention, not a claim that the physical controller revision has been
recovered. The firmware's separate `10036D14` read of write-only GICC_EOIR
also remains visible as an upstream guest error; no readback register is added.

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

The sixth original standalone run used coherent builder `041488c`, two-core
MTTCG and the complete decoded container. It crossed the whole SPI reset family
and next rejected CPU1 PC `0x10066c0c`, a 32-bit zero write to `0x0426f004`.
CPU0 remained in its normal IRQ path at `0x10038a48`; CPU1 reached its original
abort loop `0x1003410c`. The barrier counter was zero, the six ring-prefix words
were zero, and none of the three peer-worker breakpoints was reached. Both
processes exited0 by `monitor quit`; ignored receipts are `work/sla/original6*`.

The complete source GPIO/EXIU registration and configuration family
`0x10066b60..0x10066df4` supplies an independent SLA binding. Its table at
`0xc10019f4` has 232 `0x38`-byte records. Exactly eight carry external interrupt
roles: objects `0x54..0x5b`, GPIO5 bits7..14, EXIU bits0..7, IRQ65..72 and callback
pointers `0x10066ef9..0x10066f23` (COPY offsets `[0x83f128,0x83f2e8)`). The
initialization `0x10066c06` writes clear-control `+4=0`, W1C pending `+0x10=0xff`
and mask `+0=0xff`. Configuration `0x10066d6c` updates polarity `+0x14` and
edge/level `+0x18`, including mode2's inverted sampled GPIO level. The private
domain reuses the whole finite EXIU model at `0x0426f000`, connecting these eight
actual pad and GIC lines. It does not copy MAS's sixteen-line binding. The
callback records establish registration; execution of their ISR bodies remains
a separate runtime/source qualification. The native fixture drives all eight
real GPIO pads and checks masked pending, unmask-to-IRQ and W1C deassertion.

The seventh original standalone run used coherent ARM builder `615a8a94`,
two-core MTTCG and the complete original container. This binary includes the
first-phase private peripheral profiles through `3df4319` and their fixture
wrappers; it does not include the later completion IRQ250 route. Hardware
breakpoints observed CPU0 at `0x10053638`, CPSR `0x33`, SP `0xc3f04408`, then
`0x10060cee`, CPSR `0x20000033`, LR `0x10053653`, and `0x1006c0ba`, the same
Thumb mode, LR `0x10060d1f`, arguments `r0=0xc10064a0/r1=1/r2=0/r3=1`.
Mapped original instruction bytes were captured before the later remap.
The ring changed from six zero words to the guest-produced header
`0x00c00108/0xaa550001`, with queue indices zero. Breakpoints at
`0x1006c40a/0x1006c772/0x10061116` were not reached; GPIO high/low and peer
publication are not claimed.

The next real failure was CPU0 PC `0x10065ba0`, a halfword instruction fetch
at PA `0xc8265ba0`. Source `0x10065b68` changes the L1 section descriptor and
invalidates its translation. The actual descriptor at `0xc18e4400` became
`0xc820c426`; the following `0x10100000` section remained `0x1010c426`.
Registers `r4=0x10000000/r5=0xc8200000/r7=0x10300000` identify a three-MiB
code remap, with the failure after its first section. Both cores eventually
reached their original abort loop at `0x1003410c`. The declared private RAM
does not include `[0xc8200000,0xc8500000)`. The constructor's region descriptor
and the remap establish a requested physical view, but do not establish its
clone producer or justify host ROM seeding, a ROM alias, or additional RAM.
That entire storage contract is the next source qualification boundary.

Subsequent whole source qualification establishes the allocation class:
constructor record19 is `[0xc8200000,0,0]`, record20 starts at `0xc8900000`,
and classifier `0x10065ad8` uses the next base minus one as the upper bound.
The domain therefore backs exactly seven MiB of ordinary private RAM, attached
to its existing MXIC decoder. It starts with functional cold zero and retains
contents on normal soft reset. No ROM bytes, host COPY, TTBR change or copy ACK
populate it. The native fixture checks both exact ends, retained contents and
unmapped adjacent words. This closes the allocation boundary, while code
content production remains separate. The requested clone/remap source includes
`[0x10000000,0x10020000)`, a128KiB prefix outside the package ROM projection;
that prefix is still uncovered. Later class1 descriptor ranges must be
classified through their actual bus consumers rather than added as RAM.

An observer Ctrl-C ended the first RTK/GDB process with exit1. QMP `stop`
and a fresh debugger captured the final CPU registers and RAM page table;
neither guest memory nor firmware was changed. QMP `quit` ended QEMU with
exit0, and the final debugger exited0. Ignored receipts are
`work/sla/original7*`; no banked GIC MMIO was read.

## Signal and lane initialization

The bounded five-context run of canonical `87810ead` explicitly selected the
erased boot prefix. Its original SLA COPY made real progress from package ROM
into the local-code RAM: `0xc8220000` held the package's first instruction word,
`0xea002f33`. The transfer was still in progress at the final snapshot, with
source `0x1018d980`, destination `0xc838d980` and residual `0x17267f`; this is
not a complete clone or successful-remap receipt. The first actual new failure
was CPU3 at `0x100898aa`, writing word `0xf` to private PA `0x28000000`.
The subsequent abort path's NOR command is a consequence of that failure.
Ignored receipts are `work/gui/functional/{early.log,gdb.log,qemu.log}` in the
observer worktree. The run quit before any binary replacement.

The SOURCE11 official Listing supplies the complete `0x100897e4..0x10089b32`
family and its callers `0x10088d3c/0x10088d60`. Initializer `0x1008987c`
registers object `0x14d` at `0xc1000e90` and IRQ237 with handler
`0x100897e5`. Initializer `0x1008989e` writes word `0xf` to `0x28000000`,
word `0x3c` to `0x2b000004`, then enables that IRQ. The ISR reads pending
`0x2b000008` and writes the same word back to acknowledge it, before invoking
the five optional guest callbacks. Its callback-table order differs from the
physical pending-pair order. These Listing rows preserve the producer's
`source_accepted:false` executable-admission metadata; the runtime fault above
independently demonstrates the original initializer's actual execution.

The private domain reuses the mature Signal functional model at `0x2b000000`
(seven word registers) and `0x28000000` (one separate configuration word).
All five real outputs feed upstream `TYPE_OR_IRQ` with five lines, then private
GIC IRQ237. External signal providers remain disconnected. The model's reset,
gate/mask polarity, edge/debounce clock and W1C conventions remain explicit
functional assumptions described in [Signal inputs](../devices/signal/input.md).
Writing the initialization words does not generate a pending event.

Method `0x10089b06` separately selects `0x2b001000` or `0x2b001100` and issues
five configuration-word writes. The finite Lanes model exposes exactly two
write-only `0x14`-byte apertures, with boolean control words and two opaque
parameter words. It has no IRQ, provider, completion or automatic ready state.
The next `0x10089c74` family targets `0x2b200000`; its Storage PHY contract is
audited independently and is not mapped through the Signal or Lanes devices.

The focused synthetic `/nikon/sla/signal` fixture exercises the exact initializer,
all five event/ACK paths and both lane writers. A separate fresh interception
in the same case observes the private GIC input through the actual five-way OR,
which stays high while any pending route remains. This new mapper and fixture
await the sole builder's native qualification; no original-firmware continuation
is claimed by the synthetic case.

## Private storage and staged configuration

The complete Storage PHY/DBI family is source-audited independently. Callers
`0x10088a82` and `0x10093476` pass COPY1 objects `0xc1000720` and `0xc1001230`;
their original words at O `0x83cbf4` and `0x83d704` both select root mode0,
which the original wrapper converts to PHY mode4. The private domain maps
only DBI `0x2a900000/0x1000`, configuration `0x2a800000/0x10000`, memory
`0x2a810000/0xf0000` and PHY `0x2b200000/0x400`. No source evidence binds the
MAS platform aperture `0x2b100000` to SLA, so it remains uncovered here.

Storage receives the standard `memory` link to the SLA private root before
realization. Its actual DMA uses that domain. The host has no drive or NVMe
endpoint; PHY/DBI realization does not create media, readiness or a peer.
Its four source interrupt identifiers `0xf2/0xf3/0xf4/0xf6` connect to private
GIC IDs242/243/244/246. The real `link` output feeds Signal route3, using the
explicit functional pairing of the original PHY state with Signal bit2. That
connection does not produce a link while the no-media host remains offline.
Other Signal inputs remain disconnected. The separate lane configuration
apertures are not a PCI platform or fabricated storage controller.

The next actual rejection in bounded canonical `cfd9ab3f` was CPU3
`0x10093e48`, writing word0 to `0x20202018`; the former Signal rejection was
absent. This whole family reuses the existing Staging functional model at
`0x20202000/0x254`. Source `0x10093e0e` registers object14E at `0xc1001278`
and IRQ123, whose handler `0x10093dc0` reads and acknowledges real status at
`0x2020202c`. Pending causes are bits1/2/16, with same-value W1C ACK and
explicit cancellation/enable/mask restoration. This is independent of the
earlier callback-only IRQ229 registration. The unique device audit found no
missing production fields across the complete `0x10093dc0..0x10094320` family.
The private mapper connects IRQ0 to its own GIC123 and leaves all three
`external-event` inputs disconnected. The fresh synthetic fixture calls the
whole-family helper and checks that the disconnected Storage host supplies
no Signal link event. Its native qualification remains pending.

The next complete source audit establishes three independent SLA bindings,
without borrowing MAS register ownership. SOURCE11 is fixed TOOL producer
`ef6395d18386f58f8551fc663df309ee7f8475f5` (project10), canonical recovered
SOURCE `9a9fd191f4cbe4ed0f0a38c2a3fc345ea2f0ad30`. The getter and method arrays
above lead to these original bodies:

| Source family | Private regions | Actual IRQ outputs |
| --- | --- | --- |
| 9EE4C / 9EE7E, handlers9EE12/9EDE8/9EDD4 | Router 20060000/330 | 86..88 |
| 9E27C / 9E2BE, complete9E1B4..9EBA4 | SDES configuration20061000/500, status20064000/2C, parameters20065000/554 | 89..93 |
| 9CFD0 / 9D018, five handlers and9D056/9D3C0 | CaptureControl20068000/1180 | 94..98 |

All numbers in the region column are hexadecimal. Router's three handlers
acknowledge pending words at38/44/50; the first iterates its low eleven causes.
Its extra IRQ enable calls89..93 do not create additional Router outputs.
SDES owns its separate eleven-word external status snapshots. CaptureControl
owns eight status words grouped2/1/2/2/1, and supports the actual SLA initialization
writes `+18=FFF` and `+C=C`. Known control/configuration coldzero values are a
declared functional reset assumption; unknown external data is not supplied.
All three are private QOM instances with independent pending state and GIC
connections. No physical sensor, frame, automatic IRQ or completion provider is
attached. The distinct source references2006B000/2006B3B8 remain outside this
binding; they are not absorbed into CaptureControl or assigned pixel semantics.
Router's larger unqualified configuration gaps retain their existing rejection
boundary. The fresh Router fixture uses its complete source-audited helper;
shared SDES/CaptureControl tests independently validate their event contracts.
Native qualification of this private three-family graph remains pending.

A further complete source audit of `1009C76E/1009C832/1009C866/1009C96C`
qualifies the separate ReadoutControl parameter bank `2006B000/404`. Literal
`9CA04` supplies its base and `9CA08` supplies `2006B3B8`. The domain maps only
region `NIKON_READOUT_PARAMETERS` of an independent instance. Region0's
`20069800/384` buffer protocol has a mature MAS implementation, but its earlier
SLA9B consumers still require their own complete audit, so it remains unmapped
in this layer. There is no IRQ. Known configuration coldzero is an explicit
functional assumption; external raw data is not provided. Command `2006B400=1`
produces a real request output with no connected provider, rather than completion.

The remaining source-qualified second-phase graph is a separate queued layer:
Flags201A0300/20 with IRQ118 and disconnected event inputs; Status24060000/34
with no physical IRQ229; the two existing SDI banks with output0 to IRQ125/126;
and the existing MXIC/config pair with its original region and view connections.
These additions do not wait for external event or DRAM data producers and do
not supply any such data. SDI exposes eight actual pending/W1C words per bank;
DDR initialization and training retain their existing causal completion events,
while unknown MRR data remains unsupported.

MXIC output0 feeds config's named `region[0]` input; config output0 alone feeds
GIC128. Config's named `view[0]` output feeds MXIC's named `view[0]` input, selecting
the actual first-region table view. Both devices remain private and use no DMA
AddressSpace. The complete source Listing grammar for5A354/5A5E2 and parameter
families is admitted as a functional protocol profile; its original
`source_accepted:false` executable-reassembly boundary remains distinct from
that model support. The focused private fixture tests view selection and two
independent timed completion causes, including ACK of one cause while the other
keeps IRQ128 asserted. Fresh Flags and Status helpers independently exercise
their finite protocols. Qualification of this separate layer is pending; it
does not alter the already delivered Storage/Staging/Router/capture layer.
## Private EventGate control

The bounded canonical `c245` five-context run crossed the preceding Staging
initializer. Its first actual SLA rejection was CPU3 at `0x100944d2`, writing
word0 to PA `0x20100000` at virtual114282000ns. The original Listing joins
caller `0x10093d60` to initializer `0x100944ac`; literal `0x10094738` supplies
that address. Registration `0x100943ce` uses logical object146 at `0xc10012c4`
and IRQ102. These source facts use the fixed SOURCE11 producer
`ef6395d18386f58f8551fc663df309ee7f8475f5` and recovered source
`9a9fd191f4cbe4ed0f0a38c2a3fc345ea2f0ad30`.

The domain reuses `TYPE_NIKON_EVENT_GATE` at private `0x20100000/0x2928`, with
its single output connected to private GIC102. No DMA, global address-space
access or data backend is required by this finite control model. All96
external pending-bit inputs remain disconnected. The existing20us stop
acknowledgement is an explicit functional timing convention; it is not a
new physical producer inferred from the initialization write. Unknown submit
pulses and data remain rejected. The shared owner audits the full recovered
`0x100943ce..0x100953e2` family and separately tracks missing ISR/wait bodies.
The following `0x10095408` family targets `0x24073400` and is not covered by
this mapping.

A fresh synthetic `/nikon/sla/gate` wrapper calls the shared whole-protocol
fixture through `/machine/sla/gate`. This mapper and fixture require sole-builder
qualification; no compile or native pass is claimed here. In the original run,
COPY remained active with status0 and residual `0x136b8f` at the final
snapshot. The known package prefix at `0xc8220000` matched only partially;
neither a complete clone nor a successful remap is established. One ordinary
continue entered the NOR abort consequence, after which QMP quit released the
binary. Ignored receipts stay in the observer's `work/gui/peripherals`.

## Whole private provider layer

The next private layer contains eight independent objects. Paired event banks
map `20380000` and `203C0000`, each with finite size `5E00`, to IRQ99/100.
Sampler, table, shared and parameter controls map `20201000`, `20204000`,
`20205000` and `20203000` to IRQ116/121/122/124. These models own local state;
they neither perform DMA nor borrow the global address space.

The independent `results` object uses `TYPE_NIKON_READOUT` at `20200000/310`
and IRQ115. Its typed `nikon_results_*` API is distinct from ReadoutControl at
`2006B000`. Byte and word lookup profiles use control regions `24073400/F4`
and `24072400/F4`, auxiliary regions `24073000/200` and `24072000/200`, and
IRQ106/105. No typed result or lookup provider is connected. Unknown observed
status and result fields retain MEMTX_ERROR. Lookup cold idle and ownership
are explicit functional conventions; a request does not fabricate completion.

The official incremental ARM build and both full-system unit links passed
after the results API was given its independent namespace. The new readout
protocol unit passed one case in0.65s; the lookup unit passed four cases in0.65s.
The new private pairs case and concentrated provider-topology case each passed
in1.34s. Receipts are `work/readoutprotocolnativereceipt.log`,
`work/lookupprotocolnativereceipt.log`, `work/slapairsnativereceipt.log` and
`work/slaprovidertopologynativereceipt.log`; the successful build is
`work/providerlayerfixedbuild.log`. The first API-collision build failure is
preserved separately in `work/providerlayerbuild.log`.

The topology case checks all eight actual private children and configuration,
plus six real event-input paths through the private GIC and their ACKs.
IRQ105/106/115 are checked at shared unit outputs; this case does not claim
private GIC injection for the three typed-only provider devices. The other
four unchanged event protocols were not rerun. These native results do not
establish completion of the original firmware clone or a GUI console.

## Secure timer active-view profile

The bounded `cda9` causes capture joined secure Thumb PC `1006CC50`, R0=1
and LR=`1006C291` to task9, ID=`01000009`, exinf=`C167B7B8`, callback
`10053639`. Its TCB at `C19776C8` was waiting with waitspec=`C1017D1C`; its
timer node remained linked to the original expiry queue with deadline2.
Tick increment was1 but software time was0 after31.233497 virtual seconds.
This is an unretired initial delay, not evidence of a missed peer high pulse.
Source `36C90..36D28` clears pending and configures the interrupt after the
active-view RMW without clearing ACTIVE29 created by the old revision2 profile.

The private GIC now uses the mature revision1 read-only active view. The new
`/nikon/sla/secure-tick` test executes self-written synthetic ARM code under
TCG on the actual private CPU. It performs that read/OR/write sequence and
records active0, programs the actual48MHz secure physical timer for48000 ticks,
then records guest IAR29 and the completed handler after timer disable/EOI.
The host only controls the VM and reads observation RAM; it injects neither
an IRQ nor a completion flag.

Official incremental ARM/fixture compile and link passed in
`work/slasecuretickbuild.log`. The new secure-tick case passed in1.42s exit0
(`work/slasecureticknativereceipt.log`). The changed loaded-domain IIDR
contract passed in0.41s exit0 (`work/slagicprofileloadednativereceipt.log`).
No unrelated old device groups were rerun. These results qualify the
functional controller profile; original software time progress and GUI startup
remain separate runtime observations.

## Deferred initialized capacity

The bounded original `b3ebd9e` run reached the first destination store of
the complete CPU memcpy routine at `101675CC`. At that instruction R1 and R2
had already advanced/decremented inside the routine; they are not entry inputs.
The independent input contract `16dda58d64dc9efa3537695bc32a1a2890eed597`
joins SOURCE14's complete prologue to the first-fault registers and recovers
the whole invocation: source `10874D40`, destination `C1920B00`, length
`13FE8`. Its source ends at `10888D28`, entirely inside the existing SLA
component. The ordinary caller/table/length publisher remains unqualified.

One ordinary private RAM region now covers exactly `[C1920B00,C1934AE8)`.
It is cold zero and retained across soft reset. The existing COPY2 interval
ends at `C1920AD4`; its alignment gap and the space after `C1934AE8` remain
unmapped. Startup's third tuple names the same bases with length0, so it does
not populate this later region. No package bytes, PTEs or completion flags are
supplied by the host.

Official incremental ARM/fixture compile and link passed in
`work/sladeferredbuild.log`. The sole new `/nikon/sla/deferred-copy` case
passed in1.49s exit0 (`work/sladeferrednativereceipt.log`). Synthetic data
spanning the complete source extent is copied through the actual private
COPY controller and compared byte for byte, with IRQ45 and reset retention
checked. Transfers into either adjacent gap report bus error and full residual.
This qualifies bounded capacity and private copying; original CPU memcpy
continuation remains a separate runtime result.
