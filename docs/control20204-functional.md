# Finite 20204 configuration tables and conditional samples

Native C and fixture recipe are `60181a235db11d4fb4db225a657b33349dbdfecd`, following the independent 20201 report `772ccd644ad1d2f63d24c6514b0a0669a7a1949c`. Corrected source is `b733f32adf11615aa81c0ecedef2a523072a9b82`, `research/z7ii/qemu-20204-control-contract.md` in the source worktree. The two original-byte windows cover 1,204 B / 487 instructions, excluding their pools and the neighboring clock-service family. Actual method103b04ee is separately saved as1BB/14B/6 instructions/zero frontiers; it reads00, ORs bit0 and writes back. Other initializer/ISR/configuration bodies retain native coverage gaps; continuous diagnostic decoding is not accepted native CFG, general EA or P-code. Physical IP, reset, frame format, external wiring and activation semantics remain unknown.

One SysBus device owns `[20204000,2020442c)`, with official IRQ connected to original GIC121. All supported operations are aligned32bit; holes, other widths and unbound producers reject. Cold zero and configuration readback are named virtual conventions, not physical reset or legal-mask proof. Original generated fields and ranges are preserved:

| Configuration | Accepted shape |
|---|---|
| 00/04/08 | opaque bit0 RMW, preserve other old bits; no reset, ACK, sample or pending effect |
| 40 /44 /48 | changes restricted to7003f /70003 /3 respectively, preserving other old bits |
| 20 /400 /420 | fullword parameters;20 has no inferred IRQ-mask role |
| 4c/50,54,58,d4/190 | masks7fff,0fffffff,3fff,1fffffff |
| 184/1a0,188,18c,404/424 | bool,low4,low8,low16 respectively |
| 5c..94 and d8..110 | two separate15-word arrays,mask3fff |
| 98..d0 and114..14c | two separate15-word arrays,maskf |
| 150..180 |13-word array,maskf |
| 200..3fc |128-word table,mask3fff3fff |

The corrected first array is5c..94, not60..98. It does not alias the next98..d0 array. Configuration1a0 is separate from sampled data194/198/19c. Table writes retain their real MMIO order without inventing a hidden transaction or completion; temporary44bit16 changes do not generate events. Unknown14 activation and408/428 data reads remain unsupported, preserving prior state. Preceding separate guest configuration writes remain intact, without an undocumented whole-packet rollback.

The named virtual pending law uses rising `external-event` pins0/1/2 for raw bits8/17/25 and W1C2c ACK. IRQ reflects latched pending directly;20 is not used to mask it. Event0 only latches bit8 after all96 `data-bit` pins have been explicitly published since reset; it atomically snapshots three current provider words. Data reads before such a valid event reject. Publication alone, configuration, controls, reads and elapsed time do not produce pending or refresh samples. ACK retains the snapshot because the original ISR acknowledges before reading all three words; held-high event inputs do not reassert after ACK. The fixture interface is a pure virtual data provider, not recovered image/frame wiring or guessed DMA input. Original runs disconnect every external input. Guest ISR processing, error tests and callbacks remain original guest code.

Reset clears stored configuration, pending, publication tracking and sample validity while retaining actual provider/event pin levels, avoiding synthetic edges. A complete new publication and a fresh edge are required for new data. This stateful class is explicitly unmigratable. No host code, PTE, object, guest flag, callback, signature, peer response or ready/completion is supplied.

Incremental native build exited zero with one job at upstream `4fc49f46dc95d4a27de2509e7fceb2931e91faeb`. All25 board C/header inputs were compared directly to the upstream files used by the build and are equal. Checkpatch reported zero errors and one new-file MAINTAINERS reminder. `work/issue331/control20204-60181a2/result.json` records29 unique Mac-local focused checks, all passed in `2.200973166967742` seconds:25 ARM fixtures exercise original RMW initialization, every finite configuration/table/array field, retained RMW updates, independent1a0, unprovided data, activation/field/width/hole/boundary failures. Four no-firmware qtests verify all configuration readback and disconnected stability,94/98 isolation, partial96-bit publication rejection, atomic three-word snapshots, independent pending bits and IRQ output, ACK-before-data retention, controls preserving pending/sample, preflight preserving all state, valid recovery, and reset with held levels requiring republication. They do not establish original GIC IAR/EOI, guest callbacks or physical external data delivery. There were no failed focused runs in this stage; old22/24/full suites were not repeated.

The one changed-stage original observation is `work/issue331/original-control20204-60181a2/receipt.json`, exit zero after `0.3763521659420803` seconds. The38,239,096-byte MAS input passed direct initial/final equality, and whole-boot remains false. It crossed the previous20204000 boundary and next stopped at CPU1 `PC102dbd48`, READ32 `20205308`, reason `uncovered-address`, qemu.log line19752; line19756 records `DFSR1008` / `DFAR20205308`. Existing aggregate and wrapper filters were reused without replaying an earlier run. The next whole family was handed to the source owner; slot/phase/menu completion is not inferred, and this Mac-local packet is not Linux CI.
