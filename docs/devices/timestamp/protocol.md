# 独立虚拟 timestamp counter

This is the fixed `331abcd` research/fixture stage. The current canonical register contract is [runtime.md](runtime.md); its original +10 producer supersedes the vendor-candidate +20/status mapping described here. Earlier receipts remain unchanged.

本层推进 [系统控制与时钟启动设备](https://github.com/mashiro01/nightglass/issues/330)，实现原 MAS `O1e0de6` 所用 `PA04266000 +8` 递增计数接口。地址放置和同族 IP 绑定属于功能模型假设。原 CNTFRQ=48MHz 写者不能证明此外部计数器使用同一物理时钟，本模型给它独立的名义 48MHz QEMU Clock。

同族保留源码 [`_timestamp.h:119..124`](https://github.com/killsmm/m10v_mono/blob/cebff0686425b739bf63e87c911591ab30b03c1a/ca7_rtos/target/cpu_tkernel/basefw/MILB_API/MILB_Header/include/Arm/_timestamp.h#L119-L124) 定义 CNTCR `+0`、CNTSR `+4`、CNTCVL/U `+8/+c`、CNTFID0 `+20`。它的真实板地址不同，不能冒充 Nikon 寄存器手册。计数采用固定上游 [`SSECounter` 的时间 epoch 与 `clock_ns_to_ticks` 算法](https://github.com/qemu/qemu/blob/4fc49f46dc95d4a27de2509e7fceb2931e91faeb/hw/timer/sse-counter.c#L140-L172)，直接调用 [Clock API](https://github.com/qemu/qemu/blob/4fc49f46dc95d4a27de2509e7fceb2931e91faeb/include/hw/core/clock.h) 和 QEMU 虚拟时间。读次数不增加计数；停止 guest 的虚拟时间也停止计数。没有套用 SSE 的额外 scaling 寄存器或宽 read-zero/write-ignore 行为。

虚拟复位明确设 CNTCR.EN=1、计数=0、独立 Clock=48MHz。软件清 EN 时保存当时计数，重新启用时从该值继续。停用状态允许分别写低/高 32 位；启用状态写计数失败。CNTCR 只支持 EN/HDBG 两位；没有外部 Halt-on-Debug 引线，CNTSR.DBGH 读零与上游同样明确。CNTFID0 只支持此名义频率 profile 的读取，以及停用状态写回 48MHz；其他频率、控制位、宽度、offset 和只读状态写入均拒绝。

临时栈同步扩到精确 `[04008ea8,04008f00)`88 B：原 wrapper PUSH8、SDI callee PUSH40、训练 callee PUSH40，轮询 leaf 不 PUSH。原 native 指令合同来自研究 [194c71e](https://github.com/mashiro01/nightglass/blob/194c71e/research/z7ii/qemu-rtos-startup-contract.md)。边界外仍失败，没有按页或 DDR 容量扩大 RAM。

增量 native 编译 exit 0；`work/issue330/timestamp-functional-first/result.json` 实际 52 个合成契约通过。计数专属夹具由 guest 等待差值达到4800，然后禁用计数并用上游架构 timer 等待，再验证数值保持；它还设置低字接近溢出并验证高字真实进位，检查名义频率以及不支持控制/宽度/offset/频率/启用写计数的拒绝。此层尚未有原版到达 counter 的实际证据，最近原版仍先止于 SDI reset。测试不证明 RTOS、菜单、真实时钟、DDR 训练健康或 EXPEED 性能。
