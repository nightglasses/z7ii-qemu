# Z7II 原版两核入口与下一时钟阻断

2026-10-08；[QEMU: recover IRQ timer RTOS and interprocessor startup dependencies](https://github.com/mashiro01/nightglass/issues/331)、[QEMU: recover first flash-bus controller and temporary stack contract](https://github.com/mashiro01/nightglass/issues/330)。两核模型已实际构建，合成 WFE/DSB/SEV 握手通过。**原版 MTTCG 记录到 CPU1 的 WFE 后续 PC，同时 CPU0 仍在 `0x0428102c` 首次时钟读上失败。没有越过该阻断，没有验证原版 magic 释放、内核首派、tick 或 GUI。**

Machine C 固定 `942de88`，上游固定 `4fc49f46dc95d4a27de2509e7fceb2931e91faeb`。本实施线实际完成 macOS ARM 增量 Ninja、合成测试与原版观测；没有替换原指令、设置 PC 跳过 clock、填共享 magic 或改 `O0x178`。输入是原 `02_eg1840_mas_01700.payload.bin`，38,239,096 字节；两个原版收据均记录直接内容不变，入口仍 `20 2f 00 ea`，`O0x178` 仍 `fe ff ff ea`。

## 初始软件合同与映射

Nikon 原配置的真实文本生产链给出 `TSysPrNum=2`、`TKerPrNum=2`、`TTimPeriod=1`；descriptor `+4` 实际由 `O0x308` 写入 `O0x260` 重装函数，`+0xc` 才是停止函数 `O0x254`。CPU0 的原 `O0xd848` 向 `0x04008ffc` 写 `0xbabebabe`，随后 DSB/SEV；非零 selector 路径在 `O0xbfa4` WFE、DMB、读/比较、清零并返回共同入口。证据见[固定 RTOS 启动合同](https://github.com/mashiro01/nightglass/blob/aa759231295a8b20662ca35f3d313b1bff7035b2/research/z7ii/qemu-rtos-startup-contract.md)。这是软件默认值和生产/消费链，不是实机 CPU 型号、全部物理 core 数或 CBAR 的测量。

默认创建两个现成 Cortex-A7，两个 reset PC 均为候选 `0x10020000`、ARM state；MPIDR、CPU index、WFE/SEV、异常及内存序全部保留上游。A15MPCore `num-cpu=2`、`num-irq=256`，IRQ/FIQ/virtual 输出按官方[多核 VExpress 模式](https://github.com/qemu/qemu/blob/4fc49f46dc95d4a27de2509e7fceb2931e91faeb/hw/arm/vexpress.c#L214-L270)连线；每核 secure timer 仍由上游连接 PPI 29。CPU 型号、CBAR `0x1d000000` 和诊断物理装载仍是[已列模型假设](../devices/memc/protocol.md)。小型单核夹具明确指定 `-smp 1`。

仅新增 cold-zero RAM `[0x04008ffc,0x04009000)` 四字节，host 不写 magic。CPU1 SVC 的原 SP 为 `0xb2b84000`；入口、表迁移和 cache clean 的原 PUSH 闭包为 `8+8+max(8,56)=72` 字节，所以只映射 `[0xb2b83fb8,0xb2b84000)`。其余 CPU1 栈容量仍未提供，不能拿 `selector<<14` 的 stride 扩成 16 KiB 新 RAM。既有 CPU0 栈槽假设与复制窗口保持原范围。[固定 72 字节推导](https://github.com/mashiro01/nightglass/blob/b75f77140fa05d5d5e75e3a32ed0d58961ebc906/research/z7ii/qemu-rtos-startup-contract.md)

## 两次真实原版观测

薄入口 `scripts/run.py` 直接使用上游 `qemu.machine.QEMUMachine`，记录真实 argv、每核初末寄存器和直接字节比较。QMP 的 HMP 调用是[无状态接口](https://github.com/qemu/qemu/blob/4fc49f46dc95d4a27de2509e7fceb2931e91faeb/qapi/misc.json#L218-L258)，每核寄存器通过同一次调用的 `cpu-index` 指定，不能靠之前的 HMP `cpu 1` 命令保留选择。短 socket 位于 `/tmp/ng-*`。

| 记录 | 直接观察 |
|---|---|
| `work/issue331/original-two-core-942de/receipt.json`；TCG `thread=single` | 两个 CPU 已存在，但 CPU0 在 scheduler 切到 CPU1 前触发时钟读错误；CPU1 最终 PC 仍 `0x10020000`、SP=0。这次不能证明 CPU1 已执行 WFE |
| `work/issue331/original-two-core-mtcg-first/receipt.json`；TCG `thread=multi` | CPU1 实际走到 WFE 路径：最终 PC `0x1002bfa8`，恰为原 `0x1002bfa4 WFE` 的后续 DMB；SP `0xb2b84000`，R0=`0x04008ffc`，R2=`0xbabebabe`，CPSR=`0x200001d3`。它尚未读后清 magic 或进入共同入口；CPU1 的 72 字节栈映射在此轮尚未被实际消费 |
| 两次 CPU0 第一拒绝 | PC `0x1002d70c`，诊断 PA `0x0428102c`，32-bit read，`MEMTX_ERROR`；随后上游 Data Abort，最终 PC `0x10100010`、CPSR `0x600001d7`、SP `0xb2ba8000`、LR `0x1002d714` |
| 两次结束 | QMP status=`debug`，running=false，进程退出码 0，原 payload 直接内容相同；主机会话分别约 0.071 s / 0.274 s，均不是 boot 耗时或 EXPEED 性能 |

MTTCG 的 CPU1 停点说明原等待路径已到达，与 WFE 等待相符；收据只承诺观察到的 PC 和寄存器，不把这一点扩成 SMP 初始化、共享 magic 已由 CPU0 生产或首派成功。CPU0 尚未执行后面的 magic writer。原收据、日志和此前失败测试记录均保留，未覆盖。

## 验证范围与下一合同

Native 增量构建退出 0；`work/issue331/startup-single-smoke` 四字节 ARM self-loop 通过；`startup-device-smoke` 的 15 个合同全部退出 0，包括共享 word 外的上下边界失败；`startup-smp-r2` 两核合成握手通过。合成 master/secondary 仅由 guest 代码写 RAM flags、magic 与清零，通过真实 WFE/DSB/SEV 完成，未靠 QMP 注入 ready；它不是原版握手成功证据。首次合成测试因误用无状态 HMP CPU 选择而失败，记录保留在 `startup-smp-first`，修正的是观测 API，未修改 C 来绕断言。

下一项仍是 `0x04281000+0x2c` 在 MAS entry 前的真实初始值及整块寄存器语义。新公开 M10V/SC2000 的 `+0x28/+0x2c` 是不同 CLKSEL 布局，不能据它生成此处的 PLL reset 值；没有可信 reset 或 preloader 输入时，模型保持失败。后继 counter、domain completion、NOR、UART 与 IPC 会按真实命中顺序实现完整写/状态/IRQ/ack 合同，不能仅把读值设 ready。原 reset/kernel callback 的公开软件链可自然到 `O0x294→Thumb O0x1e0034`，仍需实际运行验收；它不授权 patch、跳过或删除原 `O0x178`。模型覆盖不证明真实 Flash、BootROM、calibration、硬件健康或 EXPEED 性能。
