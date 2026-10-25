# Qualified private provider layer

This ordinary child starts at main `dbbc4429c794996d3af8364ca711fc67df7faa10`
and selects the eighteen explicit provider-layer files from qualified snapshot
`fb176bdc934c38dcd0df3133a5bd00b29280d167`: thirteen implementation/header/test
registration paths and five source contracts. Production and fixtures match
that fixed source. Previous main source, validation notes, directory entries,
GNU, official providers and native build graph remain intact.

## Eight independent private objects

| Object | SLA-private registers | IRQ |
|---|---|---|
| Paired event banks | `20380000`, `203c0000`, two finite banks | 99 / 100 |
| Data sampler | `20201000` | 116 |
| Table sampler | `20204000` | 121 |
| Shared parameter events | `20205000` | 122 |
| Parameter events | `20203000` | 124 |
| Results | `20200000` | 115 |
| Byte lookup | control `24073400`, auxiliary `24073000` | 106 |
| Word lookup | control `24072400`, auxiliary `24072000` | 105 |

Each object owns local state and explicit provider inputs. Configuration,
request, elapsed time and disconnected input do not manufacture samples or
completion. The results interface is `nikon_results_generation`,
`nikon_results_publish` and `nikon_results_state`, exactly the qualified
`04c8164` namespace; it has no compatibility alias and is separate from the
existing ReadoutControl API. Results are published atomically for a real active
generation and retained across the original ISR's resubmission.

Lookup maintains finite profiles, staged configuration, externally supplied
data and explicit operation ownership. Its cold idle convention is documented;
stopping active work still requires an actual idle publication or reset.
Known sparse table intervals do not grant access to their holes. No typed
results or lookup provider is connected by the private machine binding.

The machine reuses mature sampler/table/shared/parameter models and moves
their compilation to the existing ARM memory source set. Unit fixtures reuse
the full-system QOM/MMIO graph; the SLA qtest target adds the shared samplers
helper. No new dependency, provider fork, build runner or production test hook
is introduced.

## Actual native qualification

The sole compiler linked the fixed ARM source and full-system units in
`run providerlayerfixedbuild.log`. The earlier API-name collision remains
separate in `run providerlayerbuild.log`; it is not counted as a passing build.
Seven necessary native cases passed through four invocations:

| Cases | Canonical ignored receipt | Duration / result |
|---|---|---|
| Results protocol, one case | `run readoutprotocolnativereceipt.log` | 0.65 s / exit0 |
| Lookup profiles/provider lifecycle, four cases | `run lookupprotocolnativereceipt.log` | 0.65 s / exit0 |
| Private paired banks, one case | `run slapairsnativereceipt.log` | 1.34 s / exit0 |
| Complete private provider topology, one case | `run slaprovidertopologynativereceipt.log` | 1.34 s / exit0 |

Topology checks all eight children/configuration and six real event-input paths
through the private GIC with acknowledgement. Typed-only IRQ105/106/115 are
verified at the shared unit outputs, not claimed as private-GIC injections.
The four unchanged event protocols were not repeated. Integration only reads
these existing receipts; it performs no compilation, testing or VM operation.

## Bounded original execution and COPY comparison

The separate fixed-source five-context observation is preserved under
`local-gdb-observers/run gui/providers/`: `receipt.json`, `launch.json`, `gdb.log`,
`telemetry.json`, `qemu.log` and `qmp.json`. Independent MAS and SLA prefix
inputs were explicitly counterfactual erased FF. At 58.905961 virtual seconds,
the original had no MMIO rejection, MAS/SLA were in kernel idle and the known
COPY counts were zero. QMP quit succeeded and the observer confirmed the
process absent and original input bundle unchanged. QMP reported no display
QemuConsole; no GUI completion is established.

The observer completed direct byte comparisons, reported in
[unchanged-firmware startup](/startup/first).
`expected-full.bin` is the original SLA package payload `[0,0x6e0000)`;
`full-known.bin` is actual private guest memory `[0xc8220000,0xc8900000)`.
Their 7,208,960 bytes compared equal with exit0; the `0x2e0000` subset also
compared equal. This is an actual package-byte COPY witness, independent of
the controller's zero counts. The preceding `[0xc8200000,0xc8220000)` remains
128 KiB of explicitly counterfactual FF, not recovered BootROM instructions.
The EX AIRCR path remains a source-analysis question, not evidence of an EX
hard fault or successful whole boot. No later EX fix is imported.

Later SPI, VR, EX boot changes and GNU updates are outside this fixed layer.
Native functional success does not establish physical timing, original pixel
production, GUI completion or firmware acceptance on the experimental camera.
