# Qualified peripheral integration

This integration starts at merged main `ed152b29e6d8d508c976df819f115eb77793dd9f`.
The existing HDMAC/Fill memory-lifetime fixes and native memory unit remain unchanged.
Only fixed Git snapshots supply code; the live compiler worktree is not a source donor.

## Source provenance

| Scope | Fixed source |
|---|---|
| ARM/RX devices, machines, typed peers, headers and native qtest registration | `615a8a94d05dd44986b2fab28a66f3dc1ab7231c` |
| Final qtest assertions in `events.c`, `ex.c`, `sla.c` | `3327b09ea3fe5fb59f7744e378596552c1ae5538` |
| GNU normal sample lifecycle: `runner.c`, `tpj.c`, `tpj.h`, `boundary.c`, unit `tpj.c`, touch contract | `a0b8e428eba91f9505677aea6d18e85d5e0bccea` |
| COPY validation contract and seven directory-index entries | `b5d2cf24a76ca50f4a843b4f3de310c790c07339` |

The three final qtests correct named IRQ interception, the EX fixture's positive
clock interval and COPY's unsigned ROMD word comparison. They do not change
production devices. GNU has twelve qualified fixture functions at `a0b8e428`;
the older GNU snapshot in `615a8a94` has ten and is not attributed the newer result.
Touch/startup documentation below is updated with the actual qualification receipt.
The main directory index retains its existing timing capture entry.

## Functional scope

- GPIO keeps the source-backed packed mux fields and descriptor readback.
  Typed I2C and SPI peers use actual native buses, finite framing, cancellation
  and reconnect/select behavior; bus acceptance does not manufacture guest replies.
- SLA owns a private COPY address space and native I2C, timing, USIO and audio
  wiring. Events, notifier and counter inputs are supplied explicitly; IRQ causes
  and acknowledgement follow the modeled registers.
- EX retains actual external input and power state. VR accepts finite external
  spans and flash overlays with explicit provenance, bounds and conflicting-input
  rejection. These are functional profiles, not recovered physical reset values.
- GNU normal samples have finite matrix/auxiliary extents, real guest-programmed
  destinations, buffer ownership, cancel/resume and EOF behavior. The native
  boundary node uses GNU's base-zero unit-address decoder correctly.

## Actual native receipts

The sole compiler produced these ignored receipts under its canonical `work/`
directory. Integration reads them without rebuilding or repeating device tests.
Each focused qtest below exited 0.

| Contract | Receipt | Duration |
|---|---|---|
| GPIO mux / descriptor readback | `gpiomuxqualifiedreceipt.log`, `gpiodescriptorsreceipt.log` | 0.42 / 0.43 s |
| Private SLA COPY / existing COPY large jump | `copyprivatequalifiedreceipt.log`, `copymemoryregressionreceipt.log` | 0.72 / 7.44 s |
| I2C strobe / GPIO input | `i2cstrobesreceipt.log`, `i2cgpioqualifiedreceipt.log` | 1.09 / 0.31 s |
| SPI reconnect and select | `spireconnectqualifiedreceipt.log` | 0.20 s |
| EX external input / power / I/O reset | `exinputqualifiedreceipt.log`, `expowerqualifiedreceipt.log`, `exioresetreceipt.log` | 0.57 / 0.57 / 1.09 s |
| SLA events / notifier / counter | `slaeventsqualifiedreceipt.log`, `slanotifierreceipt.log`, `slacounterreceipt.log` | 0.82 / 1.23 / 1.22 s |
| VR external span / flash overlay | `vrinputscalar0receipt.log`, `vrinputscalar1receipt.log` | 0.52 / 0.66 s |
| VR cancel/bounds / conflict / rejection | `vrinputscalar2receipt.log`, `vrinputscalar3receipt.log`, `vrinputrejectionreceipt.log` | 0.52 / 0.60 / 1.44 s |

`i2ctypedfirstreceipt.log` records eight passing typed-I2C cases, then a ninth
fixture failure caused by requesting a zero clock interval. It is not a passing
whole invocation. The corrected ninth case has the separate passing GPIO receipt.
`privateprofilebuild.log` records the native ARM/RX production compile/link batch;
the compiler reported its 21 steps completed with exit 0.

`tpjsamplesqualified.log` records the native GNU compile/link and `tpjcheck`.
The compiler confirmed twelve fixture functions completed with exit 0. Its six
missing-input/invalid-range diagnostics are expected negative assertions, not
evidence of original-firmware startup failure or success.

These receipts establish the stated native functional contracts. They do not
establish full original boot, a visible GUI, physical timing or completed original
scan/contact execution. Future IRQ250, VR peer, GNU specialized-input/shutdown
and transport changes are not part of this fixed integration.
