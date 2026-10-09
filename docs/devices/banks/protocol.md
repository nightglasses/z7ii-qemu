# Finite 2014 dual-object configuration and IRQ routing

Native C and fixture recipe are `bd405abdeb46a5173a4e909caccc49f783ddae73`, following the independently frozen 20203 report `13e005093706c5565b85d831eded2149c77d26cc`. Source is `6e340b77ddc7fd78e14a926c9c9deecfed4634f0`; see `research/z7ii/qemu-2014-control-contract.md` in the source worktree. The source collection contains75 metadata-selected methods and12 anchored leaves, 9,038B/3,463 continuous diagnostic rows with coverage gaps retained. Its file name `family-complete` does not establish all methods, general EA, native CFG or P-code. The separately selected initializer1046bd52 is4BB/164B/61 instructions/zero frontiers with original instruction bytes directly equal to the diagnostic collection. Physical IP, legal fields, reset effects, clock, endpoint and completion producers remain unknown.

Two original28B COPY0 objects have deltas0/4000. The device has two sparse banks covering `[20140000,20144000)` and `[20144000,20148000)`, with each bank containing four original4KiB-address-separated unit bases. Mapped address ranges do not make every offset accessible. Three additional narrow regions expose only B000, D000/D100 and E000 retained words; their intervening holes reject. D100 and E000 are separate storage. Every supported operation is aligned32bit. Cold zero and retained readback are named virtual conventions, not physical reset claims.

Supported bank-relative offsets are:

| Word | Accepted shape |
|---|---|
| 0/1000/2000/3000/1224 | bool retained controls, without reset, ACK or event effects |
| 100/2100/3100 | stop0 only; no bound status producer |
| 1100 | full u8-derived configuration readback, not status or ACK |
| 1200 | generated mask37f0311f with low2=0 idle/stop only |
| 1204/1240/1250/1424 | u16 |
| 1600/1604 | masks3fff/101 |
| 1400/140c | u8 |
| 1404/1408 | masks1f001f1f/1f1f |
| 1410/1414/1418 | masks301/00ff01ff/00ff00ff |
| 500/2880/3880/120c/141c/1420 | fullword parameters |
| 1210/1214 | virtual inhibit mask and raw pending/W1C |

Global B000/D000/D100/E000 shapes are111/11/bool/bool respectively. These generated shapes bound this functional profile; they are not authoritative hardware allowed-bit masks. Original getter UXTB operations remain guest operations and do not narrow the underlying fullword parameters. Unexplained table methods, C6CA and later unbound fields, data getters, global D004/D200 requests and other holes remain unsupported.

The main unit's +100 readback selects IRQ111/112/113 for values1/2/3 in original registration code. The virtual model uses that same retained configuration to route active pending; zero and other u8 values have no route. Three outputs aggregate the two banks, so ACK, masking or rerouting one bank cannot erase another bank's contribution to a shared route. It does not modify guest object+C, IRQ registrations, callbacks or software active bitmaps.

Sixty-four optional `pending-bit` fixture inputs latch one raw bit per bank on rising edges. Original runs leave them disconnected. Fullword +210 mask uses a named mask1-inhibits law; +214 ACK uses named W1C. Reads are side-effect free. Configuration, opaque pulses and elapsed time never manufacture pending, low2==3, D004==3, frames or guest callback completion. The original guest ISR reads/ACKs raw and invokes its callback with the original stack packet; the device does not inject that packet or guest flags. Reset clears configuration, globals, masks, pending and outputs while retaining provider levels so a held input cannot invent a new edge. The class is explicitly unmigratable.

Source configuration C3C2 and runtime BF0C/BF58 use the same +200 word. MMIO cannot identify their callers. This finite layer uniformly rejects any nonzero low2 value, including1/11/2/3, before changing state; it supports low2=0 idle configuration/stop and does not claim all C3C2 modes or a complete start/pause protocol. The three other unit +100 words similarly accept stop0 only. Their command words are distinct from main +100 IRQ selection. Unknown activation is not made successful by retaining a command or returning constant ready3. Separate valid writes before a later rejected transaction remain intact.

The one-job incremental native build exited zero at upstream `4fc49f46dc95d4a27de2509e7fceb2931e91faeb`. All28 board C/header inputs directly equal the upstream build inputs. Checkpatch reported zero errors and one new-file MAINTAINERS reminder. `work/issue331/control2014-bd405ab/result.json` records28 unique Mac-local focused checks, all passed in `2.030028749955818` seconds:24 ARM fixtures cover the original route/control initialization, both settled configuration banks, fullword versus software-byte extraction, idle without ready, independent globals, unsupported activation/data/hole/boundary/widths and invalid fields. Four no-firmware qtests cover complete readback, bank isolation, D100/E000 separation, disconnected clock stability, two-bank shared-route aggregation, mask/ACK/selective rerouting, opaque controls preserving pending, preflight preserving all state, recovery, reset with held levels, pending before route configuration and unassigned routes. These explicitly exercise virtual inputs/outputs, not original ISR or physical events. There were no failed focused runs; old suites were not repeated.

The one changed-stage original observation is `work/issue331/original-control2014-bd405ab/receipt.json`, exit zero after `0.30211895902175456` seconds. The38,239,096-byte MAS input passed direct initial/final equality; whole-boot remains false. It crossed the previous20141100 boundary and next stopped at CPU1 `PC10480abc`, READ8 `2016c000`, reason `uncovered-address`, qemu.log line19854; line19858 records `DFSR1008` / `DFAR2016c000`. The next whole family was handed to its source owner. The original first route byte2 was not host-provided; the second packet byte and subsequent methods are not inferred from the focused tests. Existing aggregate filters and the new candidate-only10754f68+1c filter were used in this changed run without replaying an older model just to add logging. No completed outer slot, phase, menu or usable firmware is inferred; this packet is Mac-local verification, not Linux CI.
