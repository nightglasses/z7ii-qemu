# 原版启动的功能模型

用户选择以固件软件协议为优先，允许明确命名的虚拟复位状态和完成时序。当前 machine 只有这一条功能模型路线；以前严格未知初态阶段的收据和测试仍属于各自固定提交，不转换成此模型的执行证据。目标对应 [实现系统控制与时钟启动设备](https://github.com/mashiro01/nightglass/issues/330) 与 [推进原版 RTOS 启动依赖](https://github.com/mashiro01/nightglass/issues/331)。

系统控制设备 `PA 0x04281000` 使用原 MAS 九条 PLL group 记录的三个控制字、六组停止位及 SET/CLEAR 别名。复位 `+28=0xffe`、`+2c=0x73`、`+30=3` 是虚拟板的“已见 group 停止且输出门控”假设；六组 gate 状态初值为 0，代表虚拟板门控未停止。它们不是 Nikon 或 BootROM 实测复位值。原 guest 仍必须执行字段 RMW、依赖条件检查、停止位和输出门控写入；模型不替 guest 清其他组，不把 gate bank4 的 `+48` 当 PLL lock 状态。

每组输出使用固定上游 [QEMU Clock API](https://github.com/qemu/qemu/blob/4fc49f46dc95d4a27de2509e7fceb2931e91faeb/include/hw/core/qdev-clock.h) 和 [虚拟时间 timer API](https://github.com/qemu/qemu/blob/4fc49f46dc95d4a27de2509e7fceb2931e91faeb/include/qemu/timer.h)。清除组停止位后，10 微秒虚拟时间完成内部 lock；只有 guest 又清输出门控后，输出 Clock 才更新为名义 48MHz。停止组会取消 timer 并停止输出。这一时序和频率均为功能模型假设，没有独立 lock-ready 寄存器，也不从原乘数字段 `0x163` 推导物理输出频率。当前输出还不驱动未实现的硬件消费者，不能据此评价 EXPEED 性能。

32 位小端访问限定于已有配置、控制和 gate 接口；配置只支持当前原初始化 profile，控制拒绝尚未实现的字段，其他 offset、宽度、命令仍返回 `MEMTX_ERROR` 并记录首个 CPU/PC/地址。PLL 软件接口和 SET/CLEAR 地址来源见研究固定提交 [943873c 的系统控制合同](https://github.com/mashiro01/nightglass/blob/943873c/research/z7ii/qemu-system-control-contract.md)；同族公开源码只支撑 IP 对照，不能证明 Nikon 芯片身份。

本组已完成增量 native 编译。原版执行与当前合成验证结果在完成后追加；编译通过本身不证明 RTOS 首派、菜单、真实 flash/calibration 或整个机身启动。
