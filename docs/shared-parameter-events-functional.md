# Finite 20205 shared configuration and IRQ profile

Native C and fixture recipe are `a6a66d2292ade9fd52392a8d77982c453b8675c5`, following the independent 20204 report `082ad81eae7ed264bd113616982206b47fc775a9`. Source is `9e5907ae00e1432af9eb1c7c2098ab1f717cc8e3`, which corrects the count-observability wording of `3be4610c8b5c66aa53e1719c31d39bfcfa340d77`; see `research/z7ii/qemu-20205-control-contract.md` in the source worktree. Raw original-byte windows cover1,646B/638 instructions, excluding the pool and next object family. The separately saved subconfig102dbe9c is7BB/322B/123 instructions/zero frontiers, with instruction bytes directly equal to the diagnostic report. The actual initializer102dbd2c retains its saved-native coverage gap. Raw continuity is not accepted native CFG, general EA or P-code; physical IP, reset, frame/transfer endpoints and timing remain unknown.

The new SysBus device owns `[20205000,20205318)` and connects its IRQ to original GIC122. All supported operations are aligned32bit. Cold zero and retained readback are virtual conventions, not physical reset or legal-mask proofs. Known generated scalar and RMW fields are implemented as one state per physical word:

| Field | Accepted shape |
|---|---|
| 00/300 | independent retained bool controls; original1→0 pulses have no inferred reset, ACK or event effect |
| 04/310/314 and308/30c | RMW changed bit0 and low2 respectively, retaining other old bits |
| 20/58 | fullword parameters;20 is not an IRQ mask |
| 40/44/88/90/120 | low2 |
| 48/50 and4c/54 | masks3ffe and1ffe respectively |
| 5c/60 | RMW changed masks17ff0/fffffff0, retaining untouched bits |
| 64,68/84/8c/124,6c/70/74,78/7c/80 | masks3ff,bool,7ff,7fff respectively |
| 11c | complete byte: original setter's second write overwrites its first bool write |
| 94..b0,dc..f0,128..144,170..184 | finite low4 physical words, including subconfig scalar/nominal dynamic arrays |
| b4..d8,f4..118,148..16c,188..1ac | four fixed ten-word low7 arrays |

The source dynamic packet `n=word(packet+4)` has no original bound check or independent complete MMIO count register. Three-word source groups explain the nominal array layout; they are not a hardware/API capacity or a model count guard. A fourth dynamic write can name the next already-known physical word, such as9c+12=a8. MMIO cannot distinguish that from a legitimate next-array write, so both access the same state. Tests demonstrate replacement in that shared word, not detection or acceptance of a particular packet count. Unknown addresses and fields still reject; the model never invents a hidden count, duplicate array backend, or host packet validation. Known neighboring accesses do not prove a valid packet or transaction.

Unknown304 activation and its unbound read remain unsupported and reject before modifying configuration, pending or IRQ. Preceding separate guest writes remain intact. Both subconfigs preserve their full known scalar/array state without manufacturing DMA, image completion or mode-dependent events;84 bool and11c fullbyte do not establish physical work modes.

Two optional `external-event` fixture pins latch raw bits0 and2 on rising edges. Raw2c reads are side-effect free, and ACK is a named W1C law. IRQ reflects any pending implemented bit directly;20 does not mask it. Original runs disconnect both inputs. Controls00/300, configuration, mode bytes, reads and elapsed time never produce or clear pending. The original guest ISR retains responsibility for ACK, clearing308/30c/310/314, issuing opaque controls and invoking its callback; no guest flag or callback is injected. Reset clears stored configuration/pending and lowers IRQ while retaining actual input levels so held inputs do not invent edges. This stateful class is explicitly unmigratable.

Incremental native build exited zero with one job at upstream `4fc49f46dc95d4a27de2509e7fceb2931e91faeb`. All26 board C/header inputs directly equal the upstream build inputs. Checkpatch reported zero errors and one new-file MAINTAINERS reminder. `work/issue331/control20205-a6a66d2/result.json` records26 unique Mac-local focused checks, all passed in `1.892643416998908` seconds:23 ARM fixtures cover original308 BIC initialization, complete scalar/subarray shapes,11c fullbyte overwrite, independent paired controls, retained RMW, shared neighbor state, unsupported304/holes/widths/boundary and invalid generated fields. Three no-firmware qtests cover full configuration readback, known-word sharing without invented count rejection, disconnected stability, two pending bits/IRQ/selective ACK, guest-shaped ISR control/config writes preserving pending, preflight retaining all state, valid recovery and reset with held levels. They do not prove original IAR/EOI, callback delivery, physical events or hardware dynamic capacity. There were no failed focused runs; previous suites were not repeated.

The one changed-stage original observation is `work/issue331/original-control20205-a6a66d2/receipt.json`, exit zero after `0.4574591249693185` seconds. The38,239,096-byte MAS input passed direct initial/final equality; whole-boot remains false. It crossed the previous20205308 boundary and next stopped at CPU1 `PC1036cb7e`, WRITE32 `20203010=0`, reason `uncovered-address`, qemu.log line19768; line19772 records `DFSR1808` / `DFAR20203010`. Existing aggregate/wrapper filters were reused, and the next whole family was handed to its source owner. No completed slot, phase, menu or firmware usability is inferred; this Mac-local packet is not Linux CI.
