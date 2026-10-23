# SYS configuration runtime

## Complete three-word indexed configuration interface

The C1.70 MAS primitive at `103c5d68..103c5d72` loads a word from its
third argument and stores it at PA `04280064 + 4 * index`. Its three
explicit wrappers at `1029765a/1029766c/1029767e` supply indices0/1/2.
All three ports `64/68/6c` now accept full-word replacement, including
index1 whose wrapper has no identified incoming reference in this audit.
There is no primitive-side mask, register RMW, alias strobe or completion.
This is the complete recovered wrapper family, not a wildcard indexed aperture.

The upper helpers modify guest shadow words before submitting the whole word:

| Helper | Shadow word | Operation | Port |
|---|---|---|---|
| `102fe72a` | `b18a77ac` | replace bit4 from input bit0 | `64` |
| `102fe744` | `b18a77ac` | replace bits6..9 from input bit0 | `64` |
| `102fe76a` | `b18a77b0` | replace bits25..28 from input bit0 | `6c` |
| `102fe790` | `b18a77b0` | clear low20; conditionally OR `000ffff0` | `6c` |

These operations preserve other cached bits. Their masks describe software
operations, not hardware writable permissions; restricting the decoder to
their union would incorrectly reject the original full-word interface.
The neighboring original primitives were also checked: `30` is a direct word
store, `34+4*index` is the separate selector family, `74` packs two nibbles,
and `78..88` is the existing five-word RMW family below. They do not alias
the three new ports.

Full-word readback is the device's existing functional retention convention;
physical readable/writable fields and downstream electrical effects are not
recovered. Reads require a previous aligned guest word store. Reset invalidates
all three words; no manufacturer reset value is supplied. Other offsets,
widths and alignment remain errors. There is no asynchronous work to cancel,
IRQ, timer, clock, peripheral reset or host-written guest event.

The old model rejects both original `103c5d6e / WRITE32 04280064=10` and
the new native case's first singleton-bit write. With the complete family,
`make check MTESTARGS=nikon_memory` passes all25 native cases. The new
`sysconfig-controls` case covers all32 bits of each port, whole-word
replacement, independent words, invalid-width/unaligned-write preservation,
neighbor holes and repeat use after reset. qtest's zero result for an errored
read does not independently prove its bus transaction status.

Unchanged-bundle continuation with the existing explicit NOR inputs, cold held
F4, RNG0 and normally paced UART peer passes the old store. Readback and the
guest shadow both equal `10`. The bounded TCG trace joins
`102d93a2` → `102fe72a(1)` → `1029765a(10)` →
`103c5d68(index0, &10)`, then reaches the conditional `b18a7718 != 0`
branch at `102d9448`. That calls `102fea1e(1,1)` → `102974de` →
`102dc888`; the next first rejection is CPU0 `102dc88c`, WRITE32
PA `20060028=1`, the separate peripheral-event-router request interface.
Its neighboring `2c/30` source helpers also issue write1 requests; they
cannot be equated to retained configuration or completed events from that
observation alone. The request producer/effect contract remains to be joined.

Receipts are in ignored `work/runs/sysconfig-20261022/`: original/native reds,
native green, source projection, call chains and final read-only snapshot.
The UART peer has no rejected requests. Application bytes remain `02/01`,
primary request state1, BatCk-containing word `00010101` and power0.
Gate/pending3/display-ready remain0. Some sampled composition words now contain
`ff000000`; scanout's sampled word remains0. These are memory observations,
not a verified GUI frame or whole boot.

## Earlier five-word interface and receipts

The current model keeps complete guest-written 32-bit values at `04280078`, `0428007c`, `04280080`, `04280084`, and `04280088`. Boot initialization and dynamic bit set/clear use the same storage. The former caller-sample masks and special bootstrap values have been removed. Only aligned word accesses to this finite set are accepted; other SYS interfaces retain their independent constraints.

Source is fixed at `76ab603f7a041de1a46311722cfe526a681596e4`, `research/z7ii/qemu-system-control-contract.md`, in the source worktree. The previously saved whole helper `103c5d86..103c5dc0` generates `1 << (index & 31)` without a bit whitelist. The 91 immediate callers describe observed uses, not register permissions. The previous immutable original run `3511ea6ae870ab96f19c0588db3912185d23198e` stopped at `103c5db0`, writing `0428007c=fcfffff8`; its saved mask `02000000` and zero flag identify a bit25 clear. Entry arguments and ordinary higher caller were not captured.

Full-word retention and initial/reset zero are explicit functional conventions. Physical writable fields and their effects remain unknown. These five words do not generate modeled IRQ, completion, rate, or peripheral reset effects. The guest still executes its own RMW and retains all unaffected bits; no host caller, field, or completion is supplied.

Native C is `6c7c9a04d5b73dca07d05eef70b5b851be971713`; final fixture recipe is `ef1b85ae03eb8428c5a50ca02e0f7c08d110bbd8`. The upstream pin remains `4fc49f46dc95d4a27de2509e7fceb2931e91faeb`. Incremental compilation changed only the historical source filename `z7ii-sysconfig.c` (now maintained as `hw/nikon/devices/sysconfig.c`); build exited zero and the C patch passed checkpatch with zero errors and zero warnings. All 21 board C/header inputs were compared directly with the built upstream files and were equal.

The 18 unique Mac-local focused checks passed in `0.8973133750259876` seconds, recorded in `work/issue331/sysconfig-fullword-ef1b85a-native-6c7c9a0/result.json`. They cover original five-word initialization, the sampled caller subset, arbitrary writes before/after bootstrap, actual bit25 clear with other bits preserved, and the unchanged direct SYS interfaces. A no-firmware qtest checks every one of 32 bits in each of the five words through clear/set/clear, reads all five after every write, checks repeated-read retention, advances one second without a change, and checks board reset zero. ARM byte/halfword/hole/outer/strap-write negatives read back the complete five-word pre-state after the debug stop. Direct-bus invalid widths and alignment also leave the state unchanged.

The initial focused invocation at native/recipe `6c7c9a0` passed 14 checks before an incorrect alignment-error expectation failed. Its directory and `sysconfig-fullword-6c7c9a0-focused.log` remain preserved. ARM Device-memory unaligned STR faults before the MMIO callback (`DFSR801`, `DFAR0428007d`); the subsequent unmapped exception-vector fetch produces the harness debug stop. Recipe `ef1b85a` explicitly checks that architectural cause and the preserved five-word state, while qtest separately exercises the device's alignment guard. This fixture-only correction did not change or rebuild native C, and the final result has its own new directory.

One changed-stage original run used native `6c7c9a0` and the final recipe's identical runner assumptions. `work/issue331/original-sysconfig-fullword-6c7c9a0/receipt.json` records exit zero, `0.2861022079596296` seconds, and direct initial/final equality of the 38,239,096-byte MAS input. It crossed the old bit25 configuration boundary. The next access is CPU1 `PC103bcfd2`, WRITE32 `20100000=0`, reason `uncovered-address`, at `qemu.log` line18852; line18856 records `DFSR1808`, `DFAR20100000`. This is a separate device dependency and has been handed to its source owner. It is not evidence of slot6 completion, valid peer/NV communication, a whole boot, or a menu. Existing FILL and earlier receipts retain their original identities; no old full suite was rerun and no Linux CI result is asserted here.
