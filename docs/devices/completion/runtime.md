# Encoded domain completion transactions

This change addresses [Issue399](https://github.com/mashiro01/nightglass/issues/399), based on native QEMU integration `193ee98138c85c2b2848836952228078a64e4f20`. It extends the existing completion transaction, without changing machine wiring or the guest's RTOS state. The [older diagnostic report](protocol.md) retains its historical strict reset and caller-subset limits; the [current functional reset](../../startup/protocol.md) remains request/done/enable/applied zero.

## Complete setter and caller boundary

The [fixed original-byte contract](https://github.com/mashiro01/nightglass/blob/2556cce6f5452674e0d8ddae53f5e102858fa75a/research/z7ii/qemu-system-control-contract.md) identifies MAS outer VA `10386602` / O`366602`: it updates start/stop bit8, clears request bits0..3, writes mode1 in bits4..7, and replaces bits12..14 with `(packed_argument >> 16) & 7`. It enables notification, clears the guest event flag, submits command `a1`, and waits. The saved full-c-1 original C contains this same selector expression; it is corroboration of the byte contract, not independent accepted source or ABI proof.

The complete upper enable/disable paths `1038665a` and `103866a2` bypass hardware when the argument's high16 equals4. Other arguments consult applied status and maintain per-domain software user masks before deciding whether to submit. The previously selected immediate callers use domains0/1/3 and software-bypassed4. That subset does not define register permissions. This implementation accepts the full three-bit selector in the existing finite request mask `7110`; it does not add a host-side user-mask manager, require every caller to submit, or reinterpret the low byte as a hardware domain.

All eight encoded selectors now use the same causal transaction: a valid command schedules the existing QEMU BH; that event updates one selected applied-state bit, sets done, and drives SPI250 only when notification is enabled. Guest W1C clears done/withdraws the level, while the real GIC still requires EOI. Status reads have no completion side effect. Pending mutation, unknown fields/commands/widths and offsets retain their rejection. The model neither changes guest RAM flags nor calls an ISR or business callback.

The eight-bit functional state and zero reset are model conventions. Physical domain identities, electrical effects, errors, latency and concurrency rules remain unconfirmed. Synthetic domain4 exercises the lower encoded interface; it is not evidence that the upper guest domain4 path submits hardware. No new actual runtime request for domains2/4/5/6/7 has been observed or claimed.

## Memory and clock startup audit

The seven owned devices were checked together against their saved complete startup contracts and prior immutable execution reports. Existing passed paths were not rebuilt or rerun. The audit separates software interface coverage from physical effects:

| Device | Recovered consumer / existing protocol | Preserved limit |
| --- | --- | --- |
| [MEMC](../memc/protocol.md) | First component entry writes mode21 and timing153b0902; guest WEOFF RMW uses21/29 | No NOR program/erase backend; x16 writes still reject, and erased external inputs do not identify a flash chip |
| [Clock](../../startup/protocol.md) | Nine-record PLL group RMW, ten indexed selector words, six SET/CLEAR banks; runtime [word masks](../clock/mask.md) and [two bit-control words](../clock/runtime.md) | Functional lock interval/nominal output only; no physical divider/frequency, extra ready bit, or clock consumer inferred |
| [Timestamp](../timestamp/runtime.md) | Early elapsed-low-word polling; later guest stop/reset/count/frequency/start | Independent nominal48MHz virtual Clock, not proof of an oscillator shared with CPU/DDR; no read-driven progress |
| [SDI](../sdi/protocol.md) | Whole wrapper394: two banks, PHY/DDR init, five training modes, arbitration, fifteen ports and final interface | Original48 requests previously passed under finite captured profiles and explicit virtual training/timing assumptions; other runtime configuration remains unknown |
| [MXIC region decode](../mxic/protocol.md) | Full12-word region producer and two commits; runtime pair snapshot | Finite existing backing coverage and declared geometry only; the separate [configuration/slot device](../config/protocol.md) owns snapshot/draft/512-slot transactions, not this region device |
| [SYS configuration](../sysconfig/runtime.md) | Five bootstrap words plus complete indexed bit-RMW helper; independent direct fields | Full guest word retention, explicit reset/strap inputs; no fabricated reset/gate/IRQ effect |
| Completion | Early polling domains0/1/3 and runtime request/status/eventflag/IRQ250 chain | Three-bit interface now complete; no new runtime IRQ250 or phase/menu success claim |

RTOS fixed `c515ebbd33cd8e14f2bdd111538fe72157ffdf7d` retains the actual phase3 role1 slot8 return. Its saved `pkg-phase3-post-slot8-remaining-initialization-native-inventory-first.json` selects eight complete aggregate roots (54/44/44/44/52/52/42/42 bytes), all with zero direct-flow frontier. None directly accesses MMIO: they call table getters and descriptor methods indirectly. The companion `pkg-phase3-post-slot8-complete-table-backlog-first.json` expands236 ordered method rows. Default targets and COPY0 tables alone do not prove live field contents or the entire indirect call closure. Those field-bound methods remain the proper next source evidence; no arbitrary low-register expansion follows from an aggregate callback's existence. The last reported actual stop `2c011424` belongs to the separate timing owner.

## Native test content and validation status

`vendor/qemu/tests/qtest/nikon/completion.c` supplies `nikon_test_completion(QTestState *)` to the sole native suite owner. It requires a fresh stopped synthetic machine supplied by that suite, then tests all eight start states and reverse-order stop states, preserving unaffected domains; disabled notifications; real GIC IAR250/W1C/EOI; completed-state reset; and a new post-reset request. It waits for asynchronous BH completion and the official RESET event rather than assuming a visible pending read. It does not claim deterministic reset while a BH is pending.

This child contains C and documentation only. Test registration, compilation and execution belong to the single firmware_check owner. No native build, new Ghidra/decompile run, original-firmware run, or passing-test result is claimed by this change. Physical reset/clock/DDR acceptance, full indirect initialization, task-mode IRQ250 progression and menus remain separate open evidence.
