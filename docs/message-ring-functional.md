# Original shared sending ring

Code `578f2610ff7697e9be3c00ecaffdb88df126f975` supplies one finite RAM backing at `[b7cf0000,b7cfc618)`. This is the original sending ring's complete software extent, not a one-word response or a guessed64KiB page. The [fixed storage sender evidence](https://github.com/mashiro01/nightglass/blob/cf1e65f16a056e08fdc30e45388ff9248beea688/research/z7ii/storage-path.md#L55-L64) identifies the control block, slot arithmetic, maximum body and notification boundary. The [fixed ring source/actual contract](https://github.com/mashiro01/nightglass/blob/09acd18b392f6f0171a5c0739883c58e2c4afed2/research/z7ii/qemu-rtos-startup-contract.md) retains the initializer/caller's raw42B/16B candidate identity and the HBP cleanup limitation. The sender at VA`10388326` has316B/125 instructions in the saved native body; VA`103a8326` was an earlier verbal conversion error and is not used as an accepted source.

The extent is `0x18 + 0xc0 * 0x108 = 0xc618`: a24B global header and192 physical slots, each with an8B header and up to256B body. The producer at+8 wraps at192; the consumer at+10 participates in the original full test, which leaves one empty slot and permits191 pending messages. The original initializer writes+0=`00c00108`, +4=`aa550001`, +8=0 and+10=0. It does not initialize+c/+14. The byte backing's cold zero state, including those two fields, is an explicit virtual-board assumption. The software geometry is not a measurement of Nikon DDR capacity or proof of physical interprocessor ownership.

The board reuses upstream MemoryRegion RAM and attaches the backing to the existing finite MXIC decode path. The unchanged guest owns every header, index, body copy and synchronization operation. There is no host producer, consumer, signature seed, reply or peer-ready flag. The actual remote consumer and notification contract remain uncovered. A valid RAM write or initialized ring therefore does not imply message delivery, file-system success or card writing. Boundary holes remain errors and withdrawing the DDR decode makes this backing inaccessible.

## Exact observations

The prior native1a42 run first rejected the initializer's first store. A single read-only hardware breakpoint before that store, `work/issue331/diagnostic-store-hbp-1a42e38`, captured CPU0 PC`10387e28`, Thumb SVC CPSR`60000033`, SP`b51f43f0` and recent LR`10387e21`. A128B stack read was permitted only after checking it against the existing kernel arena; its saved return`10201465` joins the original caller's CALL at`10201460`. The final LLDB detach command omitted its required boolean and returned1, after the breakpoint/register/stack observations had completed. The official QEMUMachine context shut down QEMU with exit0; direct original payload equality remained true. The raw receipt is retained with a separate observation note, and LLDB's incorrect ARM disassembly is not used as evidence.

Incremental native578f261 built with exit0. `work/issue331/ring-578f261/result.json` records six focused synthetic PASS in0.889639s: original header writes/readback, complete first-to-last slot extent, independently unchanged consumer, lower/upper boundary rejection and DDR decode withdrawal. These are tiny synthetic memory contracts, not recovered camera firmware or a remote-peer test.

The new unchanged original run `work/issue331/original-ring-578f261/{receipt.json,qemu.log}` binds exact native578f261 and pinned QEMU`4fc49f46dc95d4a27de2509e7fceb2931e91faeb`. It records exit0,0.240034500s host observation, direct complete payload equality true and wholebootfalse. Its future trace entry additionally observes original phase callbacks and this initializer; earlier receipts are unchanged.

| Original trace | Actual observation |
|---|---|
| Lines9770–9775 | CPU0 enters`10200c62`, R4=role0, R5=slot0, R6=phase2 |
| Lines9776–9781 | CPU1 enters`1020081c`, R4=role1, R5=slot0, R6=phase2 |
| Lines9831–9873 | Original arg1 caller`10201458` executes CALL`10201460` into initializer`10387e08`; LR=`10201465` |
| Lines9934–9939 | Original return block`10201464` is reached after the initializer's complete header writes |
| Lines9958–9962 | New first uncovered transaction: CPU0 PC`102de016`, WRITE32 PA`2013c5e0`=`000f000f`, followed by original Data Abort, DFSR`1808` |

This proves phase2 entry and completion of this initializer, not completion of phase2 or all tasks. The next dependency is the complete2013 device family. No guest instruction, task context, IRQ handler, event flag or O178 is changed; no menu, valid external NV, real flash health or EXPEED performance is claimed.
