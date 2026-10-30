# Qualified private control interfaces

This integration starts at main `e897da9412383a572d6a1f3333a9d769c8e8d751`
and selects only the qualified controller code, fixtures and contracts from
fixed snapshot `7f52950536f8adc2d0922ba85840d03dc7ea10f7`. Its twenty-one code,
header and native registration paths match that snapshot. Twelve source
contracts are included; previous validation records and directory entries
remain intact. GNU, SPI, VR and later peripheral work are excluded.

Existing PR29/30 COPY, I2C and command fixes remain unchanged, as do PR31's
dual-host storage identity, prefix parser, input lifecycle, update endpoint,
display correction and native build graph. Official submodules are read-only.
Integration does not compile, repeat tests, change a VM or replace its binary.

## Included contracts and dependencies

- Capture and SDES retain finite source-backed configuration and require
  explicitly supplied complete publication before raising status. MAS and
  SLA instantiate independent devices and GIC routes.
- Readout has separate finite buffer-selector and parameter regions. Explicit
  requests do not perform guessed pixel DMA or create observations.
- Private SLA Router reuses the existing finite routing device. Gate reuses
  the existing request/event model, including its explicitly commanded nominal
  stop acknowledgement; disconnected external inputs create no events.
- Phase-two flags and status retain source-defined masks and fields. The
  configuration/MXIC transactions use real timers, selective retained draft
  state, independent completion causes and W1C. SDI runtime fields preserve
  bank independence and source access widths.
- Mature common devices move to the ARM memory source set where needed by
  both MAS and SLA. SLA selects the existing sensor support. Its qtest target
  registers the new router/gate/flags/status helpers beside existing fixtures;
  no provider fork, extra build runner or production test hook is added.

## Existing actual native receipts

All listed focused invocations exited0. Receipts are ignored files beneath
the sole compiler's canonical `work/` directory; integration only reads them.
The first ten are the new Gate/phase-two cases. Earlier controller passes are
retained separately and are not claimed to have been repeated.

| Case | Receipt | Duration |
|---|---|---|
| Private SLA Gate | `slagatenativereceipt.log` | 1.32 s |
| Private SLA status | `slastatusnativereceipt.log` | 1.31 s |
| Private SLA flags | `slaflagsnativereceipt.log` | 1.32 s |
| Private SLA configuration links | `slaconfiglinksnativereceipt.log` | 1.31 s |
| Partial configuration | `configpartialnativereceipt.log` | 1.20 s |
| Independent second configuration | `configsecondnativereceipt.log` | 1.20 s |
| Live parameters | `configparametersnativereceipt.log` | 1.20 s |
| Selective transaction | `configselectivetransactionnativereceipt.log` | 1.20 s |
| SDI runtime bank0 / bank1 | `sdiruntimebank0fixedreceipt.log`, `sdiruntimebank1fixedreceipt.log` | 0.74 / 0.74 s |
| Capture configuration / status / publication | `captureconfigurationnativereceipt.log`, `capturestatusnativereceipt.log`, `capturepublicationnativereceipt.log` | 1.33 / 1.17 / 1.17 s |
| Readout configuration / buffers / request | `readoutconfigurationnativereceipt.log`, `readoutbuffersnativereceipt.log`, `readoutrequestnativereceipt.log` | 1.22 / 1.22 / 1.22 s |
| Private SLA Router | `slarouternativereceipt.log` | 1.29 s |
| SDES cold configuration | `sdescoldconfigurationnativereceipt.log` | 1.17 s |

The early SDI fixture invocations failed before completing their assertions.
Only the separate corrected bank receipts above qualify those runtime assertions.
Historical source-contract notes predate some native qualifications; this
record states the fixed integrated scope and actual receipt results.

`gatephase2fixedbuild.log` records the native ARM and fixture links for this
fixed cohort. Earlier Capture/Router and Readout native passes have their own
listed receipts; their synthetic source contracts are not original execution
witnesses.

## Bounded original observation

The separate five-context whole-bundle observation used fixed `7f529505` and
explicit independent counterfactual erased-FF prefixes. Its ignored receipt
is `local-gdb-observers/run gui/gates/receipt.json`. Original Gate
initialization crossed naturally, without requiring the modeled stop ACK.
At 129,998,000 virtual ns, CPU3 reached PC `10096542`, PA `2038000c`, W32 one.
After one continuation the observer captured all contexts at 157,411,000 ns,
quit through QMP, confirmed the process absent and the original bundle unchanged.

The known SLA payload matched 1,827,424 bytes through its current COPY pointer;
the whole clone was still incomplete. MAS's COPY controller reported zero
remaining bytes, but its destination was not independently compared. The
display had no QemuConsole and the GUI coordinator object was not yet created.
The accompanying GNU client belongs to a separate newer source cohort and is
not imported into this controller integration. No subsequent eight-family,
SPI ownership or optional VR profile work is included.

These native register/provider fixtures do not establish physical timing,
original pixel production, a complete image pipeline, original full boot,
a visible menu or firmware acceptance on the experimental camera.
