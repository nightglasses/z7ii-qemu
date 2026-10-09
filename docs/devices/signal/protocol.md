# Five-route signal-control functional profile

This is the historical **77cf5db** stage. Its unsupported pending read and unwired outputs describe that fixed implementation and its unchanged receipts. Later original ISR/registration evidence restores pending read and shared GIC237 wiring in [input.md](input.md); the original source gap is no longer the current interface boundary.

The original `2b000000` first read now belongs to a complete finite input/sampling class, rather than an isolated zero response. Native C and fixture recipe are **77cf5db90badf3cb19bc29a2f888dab6c6f74101**, based on the previously reviewed packet **248d07d8fa0648dd52101ab5d2abcef5581dfee7**. The source contract is `nightglass` research commit **797fdd0190ced9698202523edd7249c6d6c40426**, `research/z7ii/qemu-five-route-signal-control-contract.md`. Its ten saved bodies cover 856 bytes/319 instructions, including guarded five-selector tables; the initializer has a separate saved-native 9-block/86-byte/28-instruction closure. These facts establish software geometry, not physical device identity.

The model uses official qdev inputs, Clock, QEMUTimer and SysBus pending outputs. It maps only `[2b000000,2b00001c)`. Every operation requires aligned 32-bit IO. Unsupported accesses return a real transaction error before changing state. Snapshot/migration is explicitly blocked with official unmigratable VMState.

| Word offset | Whole interface and functional behavior |
| --- | --- |
| 00 | Five route bits in logical order `0,1,4,2,3`; the original clear/set pulse is preserved. In this virtual profile, zero holds reset and one releases sampling. |
| 04 | Gate bits `4,5,3,2,2`; logical routes 3 and 4 share the same gate. One enables sampling in this profile. |
| 08 | Write-only W1C command for ten private edge-pending bits. Readback remains unsupported. |
| 0c | Rising/falling pairs `(0,1),(2,3),(8,9),(6,7),(4,5)`. One enables the corresponding pending output contribution. |
| 10 | Read-only raw low five bits and retained sampled high bits 8..12; these are virtual bank meanings, not permanent readiness. |
| 14 | Debounce enable in the five route bits and two-bit codes at `8,12,24,16,20`, legal mask `0333331f`. |
| 18 | Full 32-bit configuration latch for guest read/mask/write; it generates no input transition, IRQ, completion or clock change. Its physical field effects remain unknown. |

All reset/configuration words start zero. The five external input pins start disconnected and low; controller reset preserves any live provider levels. This profile assumes reset/gate/mask polarity, clear behavior, raw/filtered bank meanings and a separate **1 MHz** nominal sample clock. Debounce codes select 1/2/4/8 ticks. These are functional assumptions, not measured Nikon reset values, silicon frequency, physical debounce law or reuse of the 48 MHz architectural counter.

An external transition alone changes raw input. An enabled sampler either follows immediately or schedules a finite debounce deadline. A bounce cancels/restarts that deadline. A stable sample changes filtered state and latches the corresponding rising/falling pending bit. Pending outputs combine the retained pair with the current mask; mask updates and W1C do not change input state. Held reset or gate closure cancels sampling, clears that route's pending and filters it to zero. Release, gate reopening and debounce reconfiguration rebase existing raw state without inventing an edge. Reconfiguration cancels the old deadline; already latched pending survives active debounce changes until explicit clear/reset/gate closure.

The five pending outputs are deliberately **unwired from the GIC**. Original callback storage has no established consumer or GIC route. Neither pulse/configuration nor input sampling calls a software callback or writes a guest flag. The neighboring `2b001000` request/result blocks and the distinct `2b100000`/`2b200000` bases remain unsupported. PCI capability software nearby and the different Socionext EXSTOP layout do not establish a physical IP binding.

## Fixed validation and original execution

One incremental native build succeeded against pinned upstream **4fc49f46dc95d4a27de2509e7fceb2931e91faeb**. The 19 C/header files then located in `hw/arm/z7ii*` directly equal their upstream build inputs. Upstream checkpatch reports **0 errors and one added-file MAINTAINERS reminder**, rather than a clean zero-warning result. Logs are `work/issue331/signal-77cf5db-build.log` and `signal-77cf5db-checkpatch.log`.

`work/issue331/signal-77cf5db/result.json` records **20/20 focused checks**, **1.5383462499594316 s** host wall time, on macOS. Fourteen executing ARM fixtures verify all original pulse bits/config fields, configuration latch, transactional invalid-field preservation, widths, command/status direction and finite unsupported neighbors. Six official qtest fixtures drive the real five qdev inputs and advance the virtual clock: every route permutation and both edges, shared gate 3/4, masked pending and selective clear, all four debounce codes on all routes, bounce/reconfiguration, and reset cancellation/live-input preservation. They intercept real pending outputs while leaving board GIC wiring disconnected. No firmware runs in these six qtest fixtures; their edges are explicit test inputs, not evidence of original external signals. The focused runner verifies unique names, exact frozen recipe and all 19 direct source comparisons. Full unrelated smoke/earlier firmware stages were not repeated or relabeled.

One changed-stage original run is `work/issue331/original-signal-77cf5db/receipt.json`: native **77cf5db90badf3cb19bc29a2f888dab6c6f74101**, exit 0, **0.2432476670946926 s** host wall time, and unchanged 38,239,096-byte MAS payload under direct content comparison. Its assumptions list contains this complete signal profile; `harness.log` retains the upstream command and `qemu.log` retains trace filtering.

The run crossed the previous first-read stop `CPU1 PC105166be / PA2b000000`. Its next first uncovered access is `qemu.log:16819`: **CPU1 PC104b7910, WRITE64 PA b776ac48**, followed by **DFSR1808 / DFAR1586ac48** at line16823. Final ABT LR104b7918 is fault-PC-plus-eight, not a normal caller LR. This is a new object/address provenance dependency; no single-word or NULL expansion is implied. The receipt does not prove a five-route external transition, device IRQ, all runtime methods, physical PCI identity, complete phase success or menu. `whole_boot_verified` remains false.
