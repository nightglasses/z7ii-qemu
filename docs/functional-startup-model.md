# 原版启动的功能模型

用户选择以固件软件协议为优先，允许明确命名的虚拟复位状态和完成时序。当前 machine 只有这一条功能模型路线；以前严格未知初态阶段的收据和测试仍属于各自固定提交，不转换成此模型的执行证据。目标对应 [实现系统控制与时钟启动设备](https://github.com/mashiro01/nightglass/issues/330) 与 [推进原版 RTOS 启动依赖](https://github.com/mashiro01/nightglass/issues/331)。

完成器 `PA04287000` 也采用唯一 coherent virtual reset：request/done/IRQ-enable/domain-applied 字均为0，代表虚拟域关闭、无完成、通知禁用。原 guest 设置请求后写 `CMD=a1`，官方 QEMU BH 事件才应用域 start/stop、置 done，之后按 guest IRQ-enable 驱动真实 GIC SPI250。`+c` 返回已应用域状态；其余位为此功能接口的保留0假设。读 status 不触发完成，guest W1C ack 与 GIC EOI 保持原流程，host 不设 RTOS event flag 或调用 handler。默认IRQ禁用使原早期 polling 能按自身代码工作；物理reset/域名称/延迟仍未鉴定。旧 unknown-reset 分支已删除，无 parallel strict fallback。

系统控制设备 `PA 0x04281000` 使用原 MAS 九条 PLL group 记录的三个控制字、六组停止位及 SET/CLEAR 别名。复位 `+28=0xffe`、`+2c=0x73`、`+30=3` 是虚拟板的“已见 group 停止且输出门控”假设；六组 gate 状态初值为 0，代表虚拟板门控未停止。它们不是 Nikon 或 BootROM 实测复位值。原 guest 仍必须执行字段 RMW、依赖条件检查、停止位和输出门控写入；模型不替 guest 清其他组，不把 gate bank4 的 `+48` 当 PLL lock 状态。

每组输出使用固定上游 [QEMU Clock API](https://github.com/qemu/qemu/blob/4fc49f46dc95d4a27de2509e7fceb2931e91faeb/include/hw/core/qdev-clock.h) 和 [虚拟时间 timer API](https://github.com/qemu/qemu/blob/4fc49f46dc95d4a27de2509e7fceb2931e91faeb/include/qemu/timer.h)。清除组停止位后，10 微秒虚拟时间完成内部 lock；只有 guest 又清输出门控后，输出 Clock 才更新为名义 48MHz。停止组会取消 timer 并停止输出。这一时序和频率均为功能模型假设，没有独立 lock-ready 寄存器，也不从原乘数字段 `0x163` 推导物理输出频率。当前输出还不驱动未实现的硬件消费者，不能据此评价 EXPEED 性能。

32 位小端访问限定于已有配置、控制和 gate 接口。十个配置字 `+00..24` 按原 indexed setter 的整字 STR 和 getter 的 LDR 保留配置值；getter 中 OR companion mask 仍由原 guest 执行，模型不强置那些读位。配置路由对物理速率的作用没有解码，Clock 仍用独立名义输入。控制拒绝尚未实现的字段，其他 offset、宽度、命令仍返回 `MEMTX_ERROR` 并记录首个 CPU/PC/地址。PLL 软件接口和 SET/CLEAR 地址来源见研究固定提交 [943873c 的系统控制合同](https://github.com/mashiro01/nightglass/blob/943873c/research/z7ii/qemu-system-control-contract.md)；同族公开源码只支撑 IP 对照，不能证明 Nikon 芯片身份。

源码 `857ce78` 增量 native 编译 exit 0。实际 MTTCG 原版运行保存于 `work/issue330/original-functional-pll-857ce78/receipt.json`，QEMU exit 0、debug stop，38,239,096 B 原载荷直接内容比较未变，O178 仍 `fe ff ff ea`。CPU0 已越过以前 `PC1002d70c / READ32 0428102c` 的阻断；新首个未知访问是 `PC10024c9c / WRITE32 PA20023000 / value1`，对应原 SDI/DDR reset assert。CPU1 仍在早期 release 等待，没有声称已完成 SMP 释放或 RTOS 首派。

`work/issue330/functional-pll-smoke-first/result.json` 的 45 个小型合成夹具实际全部通过，包括六 bank mask、功能复位、PLL 初态与 guest 启动写序列、10us 虚拟 lock callback、保留的 MEMC/NOR 拒绝、GIC/timer 和严格阶段 completion 合同。旧 unknown-reset 断言已改为此唯一功能模型的复位断言，没有增添兼容模式。编译、此首停点与合成合同都不证明菜单、真实 flash/calibration 或整个机身启动。

完成器功能复位增量native exit0，`work/issue330/completion-functional-reset-first/result.json` 的60个当前合成契约全通过。默认request事务、域3状态真实变化、reset清状态、原GIC250 start/stop/ack/EOI及不支持profile都实际检查；只替换历史unknown-reset夹具，没有声称原版已经到该完成器。
