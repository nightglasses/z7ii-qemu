# SLA loaded ARM execution domain

`nikonsla -bios decoded-container.bin` validates the complete five-component
decoded container, takes an owned immutable SLA slice, and exposes all
`0x86cd40` SLA bytes to upstream ARM TCG. It is a separate machine: MAS and SLA
have different content at the same numerical ROM VA and must not overwrite
each other's image or RAM. This profile is a loaded software domain rather
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

There is currently no proven MAS/SLA IPC endpoint contract in the complete
source recovery: no established shared PA backing, mailbox register protocol,
DMA ownership or cross-domain IRQ wire. The four-byte early release is an
SLA CPU-local boot contract and must not be mistaken for MAS IPC. A combined
machine must supply a separate CPU memory view and connect only subsequently
proven shared endpoints. External BootROM, calibration, lens/sensor inputs and
other uncovered transactions fail and report the original access context.

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
