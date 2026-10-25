# MXIC active read view during reconfiguration

The next timing boundary recorded below is now resolved by the
[divider/load audit](../timing/load.md): raw31 is divide-by50, and the next
original rejection is a capture-control mask at PA20068000. The MXIC receipts
and the historical next-stop description below retain their own fixed scope.

The first configuration unit's TRVSW at board PA `20010044` selects readback;
it does not globally prohibit writes. Configuration fields and the table still
write retained draft state, live selectors/routing/parameters keep their existing
targets, and only an original TCFTRG request plus deferred completion publishes
the selected draft portions. This corrects the earlier active-view write ban.
No new register ranges, field values, ready causes or software events are added.
The separate second-unit contract is unchanged by this evidence.

## Original source and observed call

All code addresses here are C1.70 MAS VA; register addresses are board PA.
Parent-owned `run mxic-20261022/` retains the original source recovery,
read-only TCG traces, launch arguments, UART logs and final GDB snapshots.

The pre-change `switch` run observes `103d1f10(2,0)` called from `10322886`
at `1032299e`, then `102070c0(0)` with LR `103d1fb3`. Its first selector
store is PC `102070de`, WRITE32 `20013000=10` (hex). The existing selector
mask `33` accepts this value, but the model rejects it first with
`mxic-config-active-view-write`. The final read-only snapshot has TRVSW=1,
TCFLG idle=1, TAEN=0 and prior completion pending=1. There is no busy
configuration waiting to finish at this stop.

The complete initializer `102070c0` has no TRVSW write. It programs all 124
selectors and 32 separate MXIC boolean words, waits for idle, opens TAEN,
writes nineteen saved configuration fields and 512 saved slots, programs four
live routing words, requests configuration, waits for completion, closes TAEN,
then programs TLVL and the ten MAS TRMC destinations. Supporting just the
first selector store would leave the same invalid read-view restriction on
the subsequent draft/table/request sequence.

The early snapshot entry `10207310` explicitly restores view0 before its
initial call. Later getters such as `103a111e/103a1800/103a18ec/103a1ab0`
select a read view and leave it selected. They establish persistent-view
source usage, but were not observed in the bounded trace and are not claimed
as the producer of this run's view1. That producer is still unjoined; the
observed value at the initializer and its complete no-view-write sequence
establish the immediate software/device contract.

The fixed [manufacturer driver](https://github.com/killsmm/m10v_mono/blob/cebff0686425b739bf63e87c911591ab30b03c1a/ca7_rtos/target/cpu_tkernel/basefw/MILB_API/Project/ImageMacro/src/im_mxic.c)
calls `Im_MXIC_Set_Reg_Read_Switch` a read-value selector. Several setters
temporarily select view0, so that comparison alone does not prove hardware
write permissions. The Nikon call sequence and runtime join support this
finite functional correction; no physical Nikon register identity or silicon
access timing is established by the comparison.

## Native and unchanged-firmware validation

`tests/qtest/nikon/config.c` adds `readview`, reproducing the complete MAS
initializer shape with synthetic field values while leaving TRVSW=1. On
the old implementation it fails selector readback `0 != 10` hex
(`native-red.log`). After the correction the single applicable native batch
`make check "MTESTARGS=nikon --test-args='-p /arm/nikon/config'"` passes all
eight cases (`native-green.log`). The new case checks all selectors,
nineteen fields, 512 slots, routing, deferred publication at 9999/10000ns,
post-commit live parameters and preservation of the selected view. Busy,
TAEN, reserved selector bits and word width still protect writes; the
existing cases retain partial publication, IRQ/W1C and reset cancellation.
The transaction case additionally proves that a write under active readback
changes draft while leaving active state intact.

The post-change `baseline` and `green` runs use the same unchanged bundle,
TEMP type3 paired NOR inputs, raw acceleration `(0,0,1024)`, fixed virtual
RNG0, healthy class1 UART peer and held cold F4 input. There are no
in-transaction debugger pauses. The targeted `green` trace observes the
second `102070c0(0)` call, its request/completion and final parameter stores,
then original execution resumes at `103d1fb2`. TRVSW remains1, TCFLG is idle,
TAEN is closed and completion is pending. No host clears the view or advances
a guest callback. The peer has no rejected request; BatCk1, power1,
application bytes02/01 and primary request state1 remain valid.

The new first rejection is CPU0 PC `1036e81a`, WRITE32 `2c011008=80` hex:
the shared [timing load](../timing/load.md) selects channel7. The complete
original setter `1036e7d4`, called from `10371704` at `10371796`, writes
opcode31 and period `fffffffe`, stages count `fffff45b` at `2c011004`, then
submits mask80. Final channel7 reset/control/count are0; it is configured
and stopped. The current load predicate rejects `opcode != 0`, so this
one-shot initial-count use is the next finite-model dependency. At the
nominal virtual clock its requested distance would be2980 ticks; no expiry
or callback for this request has occurred. The opcode31 load behavior still
requires its own native and original-execution validation.

GUI gate, pending slot3, display-ready and sampled surface words remain0.
This is progress past a configuration transaction, not a GUI first frame,
scanout, complete boot or physical-camera validation.
