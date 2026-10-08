# Nightglass QEMU 目录索引

| 入口 | 内容 |
|---|---|
| [README.md](README.md) | 项目范围、上游依赖和运行阶段 |
| [CONTRIBUTING.md](CONTRIBUTING.md) | Issue、分支、审查和合并流程 |
| [hw/AGENTS.md](hw/AGENTS.md) | Nikon machine 与设备实现规范 |
| [scripts/AGENTS.md](scripts/AGENTS.md) | 最小构建与运行入口规范 |
| [docs/AGENTS.md](docs/AGENTS.md) | 运行证据和模型边界规范 |
| [docs/memc-a7-diagnostic.md](docs/memc-a7-diagnostic.md) | MEMC/A7/GIC/timer 合同、初始假设和合成测试范围 |
| [docs/original-mas-memc-run.md](docs/original-mas-memc-run.md) | 原版越过 MEMC 与临时栈、下一时钟读阻断的实际证据 |
| [docs/original-mas-smp-run.md](docs/original-mas-smp-run.md) | 两核软件合同、精确 RAM、原版 MTTCG 等待与时钟阻断 |
| [docs/clock-gates-diagnostic.md](docs/clock-gates-diagnostic.md) | 六组时钟停止位别名、未知复位与合成合同验证 |
| [docs/completion-diagnostic.md](docs/completion-diagnostic.md) | 因果请求/延后完成/IRQ250/ack 合同、未知状态与实际验证 |
| [docs/functional-startup-model.md](docs/functional-startup-model.md) | 功能复位/时序假设、原版 PLL 越过与下一实际阻断 |
| [docs/timestamp-functional.md](docs/timestamp-functional.md) | 独立 Clock 时间计数、88B 精确栈和合成验证 |
| [docs/sdi-functional.md](docs/sdi-functional.md) | 双bank SDI/PHY/DDR 训练/port 状态机与原版下一阻断 |
| [docs/mxic-regions-functional.md](docs/mxic-regions-functional.md) | 有限 DDR backing 的区域事务、虚拟初态与真实总线约束 |
| [docs/fill-functional.md](docs/fill-functional.md) | 原版填充协议、有限 virtual BSS、DMA 总线结果和 ACK |
| [docs/private-peripherals-functional.md](docs/private-peripherals-functional.md) | 原首异常与页表相容 CBAR/GIC 候选、成熟 IRQ/timer 接线 |
| [docs/kernel-lowmem-functional.md](docs/kernel-lowmem-functional.md) | 原分配器软件 arena、精确边界和 guest 元数据生产 |
| [docs/timestamp-guest-protocol.md](docs/timestamp-guest-protocol.md) | 原guest stop/reset/program/start 对外部counter的完整接口 |
| [docs/usio-functional.md](docs/usio-functional.md) | 原串口初始化、真实发送缓冲/TDRE、chardev和功能时基 |
| [docs/sysconfig-functional.md](docs/sysconfig-functional.md) | 第二系统配置对象、有限字段和四bank原RMW请求状态 |
| [docs/gic-original-init.md](docs/gic-original-init.md) | 原active/EOI初始化与成熟GIC版本选择、真实PPI轨迹 |
| [docs/gpio-functional.md](docs/gpio-functional.md) | 十二port掩码输出、方向/功能/pull输入与启动选择字段 |
| [docs/mxic-config-functional.md](docs/mxic-config-functional.md) | active快照、draft槽表事务和独立live参数、原版下一控制组 |
| [docs/kernel-copy-and-percore-stacks.md](docs/kernel-copy-and-percore-stacks.md) | 原内核精确复制、每核虚拟模式栈和真实SGI/IRQ后继 |
