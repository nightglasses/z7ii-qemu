# Independent virtual timestamp counter

This is the fixed331abcd research/fixture stage. The current register contract
is [runtime.md](runtime.md); its original+10 producer supersedes the vendor
candidate+20/status interpretation below. Earlier receipts keep their scope.

This layer implements the increasing counter read by original MAS O1e0de6
at PA04266000+8 for [MEMC and early peripheral startup](/startup/memc).
Address placement and same-family IP association are functional hypotheses.
An original CNTFRQ=48MHz write does not establish this external clock, so the
model assigns an independent nominal48MHz QEMU Clock.

The same-family
[_timestamp.h](https://github.com/killsmm/m10v_mono/blob/cebff0686425b739bf63e87c911591ab30b03c1a/ca7_rtos/target/cpu_tkernel/basefw/MILB_API/MILB_Header/include/Arm/_timestamp.h)
defines CNTCR+0,CNTSR+4,CNTCVL/U+8/+c and CNTFID0+20 at a different physical
base. It is not a Nikon manual. Counting uses the pinned upstream
[SSECounter epoch/clock_ns_to_ticks method](https://github.com/qemu/qemu/blob/4fc49f46dc95d4a27de2509e7fceb2931e91faeb/hw/timer/sse-counter.c)
and [Clock API](https://github.com/qemu/qemu/blob/4fc49f46dc95d4a27de2509e7fceb2931e91faeb/include/hw/core/clock.h)
with virtual time. Reads do not advance the count; paused virtual time stops it.
No extra SSE scaling registers or broad read-zero/write-ignore behavior follows.

Virtual reset sets CNTCR.EN=1,count0,Clock48MHz. Clearing EN freezes the current
count; re-enable continues from it. Only disabled counters accept separate
low/high32-bit writes. CNTCR accepts EN/HDBG only. No external debug-halt wire
is bound, and CNTSR.DBGH reads0 as an explicit convention. CNTFID0 accepts
this nominal rate and disabled writeback48MHz only; other rates, control bits,
widths, offsets and read-only writes reject.

The temporary stack extends exactly to[04008ea8,04008f00),88 bytes:
wrapper PUSH8,SDI PUSH40,training PUSH40,poll leaf no PUSH. The original
instruction contract is pinned in
[194c71e](https://github.com/mashiro01/nightglass/blob/194c71e/research/z7ii/qemu-rtos-startup-contract.md).
There is no page-rounded or DDR-sized extension.

Native build exited0 and52 synthetic contracts passed. The counter fixture
waited for elapsed>=4800, disabled counting, waited on the architectural
timer and checked freeze. It also tested genuine high-word carry, nominal
frequency and invalid controls/widths/offsets/rates/enabled count writes.
Original code had not yet reached this counter at that checkpoint; SDI reset
was earlier. These results establish neither RTOS/menu nor physical clock,
DDR health or EXPEED performance.
