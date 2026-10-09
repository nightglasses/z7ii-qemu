# Z7II C1.70 原版 MAS 越过 MEMC 初始化

2026-10-08；[QEMU: recover first flash-bus controller and temporary stack contract](https://github.com/mashiro01/nightglass/issues/330)、[QEMU: recover IRQ timer RTOS and interprocessor startup dependencies](https://github.com/mashiro01/nightglass/issues/331)。**原版 MAS 已越过两次 MEMC 初始化写和临时栈上的首个 PUSH，下一真实阻断为诊断 PA `0x0428102c` 的 32-bit 读。整体启动、`O0x178` 到达、原版 RTOS/GIC/timer 初始化和重建版对照均未验证。**

固定 machine 提交 `40fb318220fa48aae235029c73f0cd06d2bcc431`，上游 QEMU `4fc49f46dc95d4a27de2509e7fceb2931e91faeb`。主代理完成 macOS ARM 原生增量构建、原版执行和合成测试；本文作者只读取收据及有限日志，没有另跑 QEMU。原版记录保留在 ignored `work/issue326/r5-40fb/{receipt.json,qemu.log}`，前次记录未覆盖。输入仍为 Nightglass `work/z7ii-1.70/02_eg1840_mas_01700.payload.bin`，38,239,096 字节；收据 `payload_direct_content_unchanged=true`。本轮没有修改原版指令、入口或 `O0x178` 自环。

实际参数为 `-M z7ii-mas-diagnostic -accel tcg,thread=single -bios <上述原版 payload> -display none -serial none -monitor none -S -qmp unix:<短路径>/qmp.sock,server=on,wait=off -D <run>/qemu.log -d in_asm,int,guest_errors`，完整 argv 在收据中。控制端继续执行，读取 debug 停止状态及寄存器，再退出；进程退出码 0。原生构建通过并不等于原版启动通过。

| 直接观察 | 结果 |
|---|---|
| Reset | `prelaunch`；PC `0x10020000`，CPSR `0x400001d3`，通用寄存器与 SP 为零；上游 Cortex-A7 默认 ARM/SVC/secure reset |
| MEMC MODE0 | CPU0 PC `0x1002bda4`，32-bit store 至 `0x04264000`，值 `0x21`；模型日志 `accepted-profile` |
| MEMC TIM0 | CPU0 PC `0x1002bdac`，32-bit store 至 `0x04264020`，值 `0x153b0902`；模型日志 `accepted-profile` |
| 后续路径 | 到达 `0x100202b8→0x10020914`，原 BX LR 返回，再进入 `0x1002d700`；跨过 `PUSH {r4,lr}` 后到达后续读。只证明本次已到达的栈写被接受，不证明整个 48 字节窗口或后续服务已由原版使用 |
| 下一拒绝事务 | CPU0，PC `0x1002d70c`（MAS `O0xd70c`），PA `0x0428102c`，read，4 字节；`MEMTX_ERROR`，`reason=uncovered-address`。错误日志的 `value=0` 是无效事务输出，不是成功读回零 |
| 异常和停止 | 上游 Data Abort；DFSR `0x1008`，DFAR `0x0428102c`；QMP reason/status=`debug`，running=false；最终 PC `0x10100010`，CPSR `0x600001d7`，SP `0xb2ba8000`，LR `0x1002d714` |
| 主机会话时间 | `wall_seconds=0.10279012494720519`，包含控制与退出；不是固件启动时间或 EXPEED 性能 |

本轮使用的单核 Cortex-A7、CBAR `0x1d000000`、CNTFRQ 48 MHz、A15MPCore 256 个总 IRQ、同源 MEMC IP 布局绑定及零初始化 48 字节临时 RAM，仍是[明确诊断假设](../devices/memc/protocol.md)。两个 MODE/TIM 初始化值被接受，只证明原版在这些假设下跨过首轮阻断；不确认真实 Nikon CPU、控制器身份、reset 值或 NOR 设备。NOR 写、clock、IPC 和 BootROM 仍未实现。

`0x0428102c` 是原字节中 `0x04281000` 初始化序列的首次读。后续静态代码会修改 `+0x2c/+0x28` 的字段并执行软件延迟，但本轮在第一次读便停止，后续写和延迟没有执行。[前次有限静态分析](first.md#0x04264000-的下一项合同)已将该块与 MEMC 分开。下一步需要该独立时钟块的寄存器字段、reset/read 值、写约束和状态副作用证据；不能以通用 regfile 或 ready 值越过此读。Data Abort 先于异步 debug stop，所以最终 PC 与拒绝访问发起 PC 不同。

合成记录 `work/issue330/device-smoke-40fb/result.json` 中 13 个 fixture 全部退出码 0：MODE/TIM 读回及 WEOFF RMW、未初始化读、未覆盖宽度/offset/profile、NOR 初始化/写保护/未实现后端错误，以及临时栈上下边界均符合预期；secure timer fixture 经真实上游 PPI 29 观察 IAR/active/EOI 状态并检查 CNTFRQ/CBAR。这些是小型合成 ARM 测试，不是原版 RTOS 到达证据，也不证明完整 flash 写入或 SMP/IPC。QEMU 结果只降低已覆盖的软件风险，不证明真实 Flash、BootROM、calibration、硬件健康或 EXPEED 性能。
