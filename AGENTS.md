# Nightglass QEMU 目录索引

| 入口 | 内容 |
|---|---|
| [README.md](README.md) | 项目范围、上游依赖和运行阶段 |
| [CONTRIBUTING.md](CONTRIBUTING.md) | Issue、分支、审查和合并流程 |
| [AGENTS.md](hw/nikon/AGENTS.md) | Nikon machine 与设备实现规范 |
| [AGENTS.md](docs/AGENTS.md) | 运行证据和模型边界规范 |
| [layout.md](docs/layout.md) | Nikon 机型连线、通用设备协议与公共接口的分层结构 |
| [build.md](docs/build.md) | 官方 QEMU submodule、Nikon 自有源码与原生构建 |
| [protocol.md](docs/devices/memc/protocol.md) | MEMC/A7/GIC/timer 合同、初始假设和合成测试范围 |
| [memc.md](docs/startup/memc.md) | 原版越过 MEMC 与临时栈、下一时钟读阻断的实际证据 |
| [smp.md](docs/startup/smp.md) | 两核软件合同、精确 RAM、原版 MTTCG 等待与时钟阻断 |
| [protocol.md](docs/devices/clock/protocol.md) | 六组时钟停止位别名、未知复位与合成合同验证 |
| [protocol.md](docs/devices/completion/protocol.md) | 因果请求/延后完成/IRQ250/ack 合同、未知状态与实际验证 |
| [protocol.md](docs/startup/protocol.md) | 功能复位/时序假设、原版 PLL 越过与下一实际阻断 |
| [protocol.md](docs/devices/timestamp/protocol.md) | 独立 Clock 时间计数、88B 精确栈和合成验证 |
| [protocol.md](docs/devices/sdi/protocol.md) | 双bank SDI/PHY/DDR 训练/port 状态机与原版下一阻断 |
| [protocol.md](docs/devices/mxic/protocol.md) | 有限 DDR backing 的区域事务、虚拟初态与真实总线约束 |
| [protocol.md](docs/devices/fill/protocol.md) | 原版填充协议、有限 virtual BSS、DMA 总线结果和 ACK |
| [peripherals.md](docs/startup/peripherals.md) | 原首异常与页表相容 CBAR/GIC 候选、成熟 IRQ/timer 接线 |
| [allocator.md](docs/runtime/allocator.md) | 原分配器软件 arena、精确边界和 guest 元数据生产 |
| [runtime.md](docs/devices/timestamp/runtime.md) | 原guest stop/reset/program/start 对外部counter的完整接口 |
| [protocol.md](docs/devices/usio/protocol.md) | 原串口初始化、真实发送缓冲/TDRE、chardev和功能时基 |
| [protocol.md](docs/devices/sysconfig/protocol.md) | 第二系统配置对象、有限字段和四bank原RMW请求状态 |
| [runtime.md](docs/devices/gic/runtime.md) | 原active/EOI初始化与成熟GIC版本选择、真实PPI轨迹 |
| [protocol.md](docs/devices/gpio/protocol.md) | 十二port掩码输出、方向/功能/pull输入与启动选择字段 |
| [protocol.md](docs/devices/config/protocol.md) | active快照、draft槽表事务和独立live参数、原版下一控制组 |
| [stacks.md](docs/runtime/stacks.md) | 原内核精确复制、每核虚拟模式栈和真实SGI/IRQ后继 |
| [runtime.md](docs/devices/clock/runtime.md) | 两bank运行时位请求、原RMW合同和下一真实外设 |
| [protocol.md](docs/devices/spi/protocol.md) | 十一通道SSI/FIFO/时钟/IRQ功能合同与PKGB构造真实轨迹 |
| [buffer.md](docs/runtime/buffer.md) | 原动态bump首个完整分配、只读调试观测和下一NOR数据来源 |
| [slot.md](docs/inputs/nv/slot.md) | 原registry独立NV槽、有限擦除态输入与原数据读后续停点 |
| [protocol.md](docs/devices/exiu/protocol.md) | 十六GPIO pad外部IRQ、边沿/电平/W1C和原版下一设备 |
| [protocol.md](docs/devices/hdmac/protocol.md) | 双八通道真实DMA总线/仲裁/IRQ和原版两核后继异常 |
| [protocol.md](docs/devices/i2c/protocol.md) | 四通道真实I2CBus/FIFO/IRQ、断连NACK与未绑定变体拒绝 |
| [records.md](docs/inputs/nv/records.md) | 独立256KiB记录输入、擦除态假设与原空记录/恢复失败边界 |
| [protocol.md](docs/devices/audio/protocol.md) | 双Audio不解释布局的FIFO与每beat HDMAC请求、原版下一双核依赖 |
| [protocol.md](docs/devices/timing/protocol.md) | 十二通道具名计时假设、真实Clock/IRQ与原版下一四通道设备 |
| [protocol.md](docs/devices/copy/protocol.md) | 四通道默认字节复制、真实DMA总线/IRQ与全局控制边界 |
| [footer.md](docs/inputs/nor/footer.md) | 原尾部ENDM比较、有限擦除态输入与下一真实外部槽 |
| [profile.md](docs/inputs/nor/profile.md) | 全33描述符有限外部输入、共享物理状态/保留洞与实际新对象范围 |
| [ring.md](docs/runtime/ring.md) | 原完整发送ring容量、guest索引/无对端回复与phase2实际入口 |
| [budgets.md](docs/validation/budgets.md) | 合成虚拟期限与取消顺序、CI忙等诊断和focused验证范围 |
| [observation.md](docs/runtime/wait/observation.md) | 原软件等待、事件标志与后续唤醒的实际研究结论 |
| [protocol.md](docs/devices/bridge/protocol.md) | 有限桥控制事务、真实IRQ/取消、外部GPIO依赖和原版下一并发停点 |
| [sla.md](docs/startup/sla.md) | 独立 SLA 载入域、原版复制/清零与精确 RAM |
| [ex.md](docs/startup/ex.md) | EX 处理器和功能外设模型、外部数据输入边界 |
| [protocol.md](docs/devices/sensor/protocol.md) | 原生帧输入、真实 DMA 和未连接固件控制器的边界 |
| [protocol.md](docs/devices/storage/protocol.md) | 成熟 NVMe 控制器、真实块介质与原生 PCI fixture |
