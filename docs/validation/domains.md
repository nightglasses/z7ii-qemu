# Qualified private domains and external inputs

This integration starts at merged main `7f8fa9bc98fff35b95ce321208bb183fbbd145c9`
and takes only the QEMU-owned changes from fixed native source
`87810ead59cbcdb17e17e68dcc7359aa85fe5ba2`. Its source, headers, native test
registration and fixtures match that snapshot except for the two review
corrections qualified below. The sole compiler's live
worktree, binaries and running original-firmware observation are not modified.

The previous I2C conflict rollback, COPY effective-section preflight and actual
DMA span fixes remain unchanged, as do the official QEMU/GNU gitlinks and
native source graph. The prior peripheral validation record and capture index
are retained. This group excludes all newer GNU source/tests and their two
contracts, and introduces no MXIC, status or SDI changes.

## Qualified cohorts

| Cohort | Integrated behavior |
|---|---|
| Private EX | Complete bundle attaches two MAS, two private SLA and one private EX CPU; EX SRAM/NVIC/DMA remain private. Four actual SysBus SPI ports guard realization reentry. Source-complementary SSI and GPIO wiring are explicit functional assumptions. |
| External keys | Seventeen source pads receive actual host input, with retained levels across global, MCU and RCC reset. The native lifecycle fixture roots real receivers; it does not assign menu meanings or inject guest events. |
| SLA local code and command | A source-classified 7 MiB ordinary RAM range starts cold; no code clone is supplied. Three finite command register roots and twenty IRQ outputs retain their actual request/provider causality. The external provider remains disconnected. |
| SLA prefix | Strict input leaves the external 128 KiB unavailable. Explicit erased input or an exact-size external file maps only the private read-only prefix; the guest still performs COPY. The composed machine uses the same parser. |
| Peer pressure | A connected full output queue disconnects through the real CharBackend. Reconnect cannot replay stale physical levels or complete an old accepted request. |
| VR upload | Finite source-record staging, sector-backed persistence and per-record durable progress use actual guest storage and native BlockBackend I/O. Eligibility does not synthesize an original firmware acknowledgement. |

The qualified VR staging, sector and progress production/test files are all
present in `87810ead`; they match the production/test cohort at `d2d94c46`.
Its later documentation records the actual qualification. No future resident
endpoint implementation is included.

## Actual native receipts

The following ignored receipts are under the sole compiler's canonical
`work/` directory. Integration reads the existing evidence and does not repeat
the builds or tests. Every focused invocation listed here exited 0.

| Native case | Receipt | Duration |
|---|---|---|
| Five-context composition | `excombinedfirstreceipt.log` | 1.28 s |
| EX width-correct SSI word ports | `exwordportguardreceipt.log` | 1.11 s |
| Rooted key receiver/reset lifecycle | `inputrootedreceipt.log` | 0.62 s |
| Command causality | `commandnativereceipt.log` | 0.57 s |
| SLA cold local code RAM | `slalocalcodereceipt.log` | 1.29 s |
| SLA exact/erased external prefix | `slabooteinputfinalreceipt.log` | 1.80 s |
| Real-socket queue pressure/reconnect | `peerpressurefirstreceipt.log` | 1.65 s |
| VR bounded staging | `vruploadstagingdeclaredreceipt.log` | 0.44 s |
| VR sector persistence | `vrstoresectorreceipt.log` | 1.25 s |
| VR durable record progress | `vrrecordprogressreceipt.log` | 0.81 s |
| VR short capture rejection | `vrshortcapturereceipt.log` | 0.91 s |
| Command RX-to-TX frame reuse | `commandreceivereceipt.log` | 0.76 s |

Independent review found two lifecycle defects. VR capture rejection could
retain a previous F7 chunk's durable eligibility; the correction from
`17f5e4cdf2a4a5fb71196b4159f2e2a52df3200f` invalidates that eligibility
for a rejected new F7, including short DMA input, without invalidating F8.
Command RX retirement and a new TX claim could retain old received frame
words; `fe4677d` clears those frames and requires all nine current words
before START. Both focused cases above passed with the actual native
compile/link recorded in `commandvrp2build.log`. Unchanged cases were not
repeated. Neither correction claims original-firmware protocol completion.

The composition receipt precedes the final EX SysBus reentry guard correction;
the later EX word receipt qualifies that correction. The early unrooted input
lifecycle receipt failed and is not reported as passing: the rooted final
fixture has the separate 0.62-second receipt above. Previous external-input
and peripheral batches are retained, not claimed to have been rerun.

`exwordportguardbuild.log` records the EX production ARM link.
`prefixpressurebuild.log` records the later ARM/RX/common transport link; the
compiler confirmed completion with exit 0. Device implementation and fixture
success establish only their stated functional contracts, not original full
boot, original peer replies, a visible GUI or measured hardware performance.
