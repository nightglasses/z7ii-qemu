# Dual-bank SDI/PHY/DDR functional model

This layer continues [issue331](https://github.com/mashiro01/nightglass/issues/331)
after857ce78 stopped at PC10024c9c/WRITE32 PA20023000=1. The dual-bank
bases, PHY/DDR reset/request/flag and DDRL groups in pinned
[m8m-regs.h](https://github.com/hypoxic/stubby-1.60.1/blob/d670fe97469f4fbac35fcf67fbab81c7ea84f153/src/m8m-regs.h)
match the instruction shape. The release/init/training/W1C sequence in
[m8m-ddr.c](https://github.com/hypoxic/stubby-1.60.1/blob/d670fe97469f4fbac35fcf67fbab81c7ea84f153/src/m8m-ddr.c)
supports a same-IP hypothesis, not Nikon silicon or physical reset identity.
Addresses and values below are hexadecimal unless marked decimal.

## Source and configuration

The saved native CALL closure for MAS wrapper394 covers94 basic blocks and
5564 bytes. Read-only official Ghidra Instruction.getPcode and
SymbolicPropogator.flowConstants results are pinned in
[6a6b721](https://github.com/mashiro01/nightglass/blob/6a6b721/research/z7ii/qemu-rtos-startup-contract.md).
SLA source1076d278 adds complete functions1002361c,10023d10,10024cb8,100253a0
and training helpers10024588,100248f8,10025c1c,10025f8c.

The model replaces individual-value whitelists with110 named words in38
groups: TOP/rank, clock, PHY AC, DDR MR/geometry/AC/refresh/ZQ/command,
arbitration, ports and DDRL wait/impedance/manual delay/search/eye/pattern.
Unknown offsets are errors, not RAM.

Two independent SysBus instances in `hw/nikon/devices/sdi.c` use MAS/SLA
PAs20020000/20040000 and aligned little-endian32-bit transactions.

| Operation | Required configuration/state |
|---|---|
| PHY init | Complete PHY/clock/DDRL fields, TOP/PHY released; nonzero RANKSEL matching both TOP+10c rank-enable fields |
| DDR init | PHY complete, DDR released, complete configuration, nonzero geometry/ZQ/command, three equal256MiB-aligned address bounds |
| Training0..4 | DDR initialized, previous completion ACKed, complete five-mode order |
| Arbitration | All five training modes succeeded |
| Fifteen ports/final interface | Arbitration complete, own release/configuration/clock applied |

Pending requests, unacknowledged done, incomplete fields, reserved bits,
unknown offset/width/command reject with MEMTX_ERROR.

## Reset, timing and training assumptions

TOP/subblock reset starts asserted; named configuration words are0 but
their applied markers are false. Pending/done start0. These are virtual reset
conventions. Assert cancels the block and dependent timers/state. PHY/DDR
reset clears training; arbitration reset preserves training but cancels all
downstream state. Release does not complete a request.

Each valid request completes after10us of QEMU virtual time, not measured
physical latency. Reads have no side effects. W1C clears only that flag;
ACK while pending rejects, while ACK of an already-clear flag is valid.
Original SLA final polls use E300 then ACK CC30; the model preserves E300
rather than correcting the guest's asymmetric ACK.

The functional training backend uses applied manual write-delay as each
lane's stable target. Mode0 requires its programmed WL search interval to
contain that target; empty/out-of-range gives TRAINERR.WL_ERR=4. Modes3/4
require lane eye-enable and nonzero search parameters or report RDE/WDE error.
Modes1/2 use applied gate/synchronizer configuration.

TRAINERR belongs only to a completed training. Reads reject while a new
request is pending, after configuration changes or PHY/DDR reset. Failure
still produces actual done/W1C and retains five-mode sequencing. After all
five finish, software may reprogram and retry from mode0; only total success
admits arbitration. Results follow configuration/timers, not a forced success
branch. No electrical eye, physical health or memory traffic is modeled.

## Original SLA families

GPIO PA0428202c bit2 selects the family. Clear chooses1002361c(0,1) /
10023d10(1), then bank selector2; set chooses10024cb8(0,1) /100253a0(1),
then selector2. Nonzero init r0 skips cold PHY setup but still reinitializes
DDR; retained configuration must actually be applied.

The primary family uses rank1/TOP101, DDR3504=2030100 and
3508/350c/3514=40000000. The alternative uses rank3/TOP303,
3504=2030101 and all three bounds30000000. Both execute all five training
modes and three candidate WL windows. Helper10020920(statusPA,1,02625a00)
polls up to40,000,000 software iterations; this is not a clock, IRQ or host
deadline.

None of the four source groups programs DMA payloads or IRQ-enable.
No peer, IRQ wiring or DMA completion is inferred. Packed timing/geometry/
pattern words retain the whole guest value where physical bitfields are
unknown; known clock/rank/lane/eye fields enforce their masks. RAM ownership
remains the machine's separate copy/stack contract, not an SDI-created DDR
aperture. Hardware IRQ-enable, runtime traffic and PHY result data remain
producer/consumer boundaries.

## Historical validation

Original MTTCG code07342da exited0 with unchanged38,239,096-byte input;
host session0.832005s. Software traversed both banks,24 requests/completions
each,48 total. Next rejection: CPU0 PC1002bdd8/WRITE32 PA20016020=60000000.
Data Abort ended at PC10100010/SPb2ba8000; CPU1 remained
PC1002bfa8/SPb2b84000. First dispatch, SMP release and menu were unverified.

The earlier14d3ada run rejected port6 because the model wrongly extended8c00
configuration through9400 into the next port.07342da uses actual next-port
bounds, without new success values or guest bypass. The E300/CC30 asymmetry
remains intact.

The older62-case sdi-functional-smoke-reset receipt belongs to the individual
value model. The replacement native C qtest `tests/qtest/nikon/sdi.c` covers
both MAS/SLA single/dual rank families, five-mode training,15 ports/final
interface,9999ns pending versus10000ns done, readback, full retry, bad ranks/
bounds/order/width, pending configuration/ACK and DDR/arbitration reset
cancellation. At its source-author checkpoint, compilation/execution was
assigned to the build owner and not claimed complete.

Review correction: arbitration reset+4000 cancels every downstream port/final
timer and clears their reset/completion/applied state, retaining PHY/DDR
training. Two deterministic cases cover reset before expiry and after
completion; these are synthetic reset checks, not another original run.
