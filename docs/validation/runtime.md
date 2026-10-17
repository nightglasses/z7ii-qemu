# Qualified reset, timer, capacity and analog inputs

This normal integration starts from main
`04346e0cd5483e94671f41cc3c2e7412d70f8756`, after PR33, and selects only the
qualified runtime delta from `b352a81571e0c4f7bdd085fa9ff2663e9aae3c18`.
Production and native test changes are the same fixed source, applied as path
differences to main, with the separately qualified ADC lifecycle guard from
`983c305d556dd1271d25797e1bc25637326e3062`. The mainline lookup disconnect fix,
bundle, peer, storage,
previous qualification pages, GNU state and official submodule are retained.
Later GUI shared-status and expanded peer-memory changes are outside this scope.

## Selected implementation and fixtures

| Paths | Resulting behavior |
| --- | --- |
| `hw/nikon/machines/ex.c`, `include/hw/nikon/ex.h` | Keyed EX reset retires on its own vCPU thread and reaches CPU/NVIC/SysTick; held external ADC inputs survive processor reset and can be explicitly disconnected |
| `hw/nikon/machines/z7ii.c` | `functional-gui` installs one deterministic EX board-input record; the private EX still owns ADC conversion and completion |
| `hw/nikon/machines/sla.c`, `include/hw/nikon/sla.h` | Mature GICv1 active-view profile permits the real secure timer; exactly `[C1920B00,C1934AE8)` gains private deferred-copy capacity |
| `tests/qtest/nikon/ex.c`, `tests/qtest/nikon/sla.c` | Focused ADC, actual secure timer and complete bounded deferred-copy fixtures |
| `tests/unit/nikon/exreset.c`, `tests/unit/nikon/meson.build` | Native full-system local EX reset fixture and normal unit registration |

Supporting source and model boundaries are in
[EX reset](../runtime/reset.md), [explicit analog input](../runtime/analog.md),
[EX startup](../startup/ex.md), and [SLA startup](../startup/sla.md).
The directory index only gains these new entries; it does not replace or
remove previous mainline entries.

## Existing actual qualification

The integration reads and reuses the sole builder's existing native receipts.
It does not rerun builds, tests, or VMs. All listed cases completed with exit0.
Receipts and their associated build logs remain ignored canonical `work/`
artifacts, rather than committed binaries or copied log dumps.

| Focused native case | Receipt | Duration |
| --- | --- | --- |
| `/nikon/ex/local-system-reset` | `work/exresetownernativereceipt.log` | 0.57 s |
| `/arm/nikon/ex/adc` | `work/exadcnativereceipt.log` | 1.02 s |
| `/arm/nikon/sla/secure-tick` | `work/slasecureticknativereceipt.log` | 1.42 s |
| `/arm/nikon/sla/loaded-domain` | `work/slagicprofileloadednativereceipt.log` | 0.41 s |
| `/arm/nikon/sla/deferred-copy` | `work/sladeferrednativereceipt.log` | 1.49 s |

These receipts qualify MCU-local and board reset, retained analog inputs with
real guest conversion requests, actual secure physical timer delivery through
the selected GIC profile, and private full-extent copying with adjacent gaps
still rejected. The ARM/fixture compile and link receipts are described in the
corresponding source contracts. The machine count property is an explicit
input boundary, not a default calibration profile or evidence of normal power.

The optional machine count property also compiled and linked in the saved
`work/adcoptionbuild.log`. Its lifecycle guard uses QEMU's actual
`PHASE_MACHINE_INITIALIZED` instead of treating bundle presence as a phase.
The GUI worker's `work/gui/analog/options.json` records an initialized-machine
getter returning `1:3500`, rejection of a subsequent setter to `1:2900`, and
the getter still returning `1:3500`. Four CLI negative cases (out-of-range
count, unknown channel, duplicate channel and fractional count) each exited1.
These option observations were made with the separately compiled `f03b796a`
source, which includes the exact guard; its other peer-memory changes are not
selected here. No additional integration build or VM was started.

The GIC choice, cold RAM, functional clocks and optional analog counts remain
declared model assumptions. Original reboot, continued CPU memcpy, original
ADC progress and GUI completion require their separate runtime observations.
No firmware instructions, CPU returns, PTEs, completion flags or image results
are supplied to make these tests pass.
