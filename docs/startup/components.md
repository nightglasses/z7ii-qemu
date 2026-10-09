# 五组件输入与执行域

2026-10-09。当前运行 machine 仍是 MAS 组件装载后的双 Cortex-A7 模型。到达 GUI、完整五组件装载、五组件协同运行是不同结果。

`hw/nikon/components/bundle.c` 读取已有 Nightglass 提取结果中的 `decoded-container.bin`。这是去掉 32 B 外部 wrapper 后完整解码的容器，包含五个目录项、每组件尾部 CRC、整包 CRC 和尾部填充；不是五文件路径拼接，也不接受原始 XOR 更新文件。格式来源是 Nightglass 的 `src/nightglass/container.py` 与 C1.70 样本登记。原有解包是输入准备工具；QEMU 装载、测试与构建不通过 Python 控制器执行。

解析先检查整个容器，再公开五个不可变 `GBytes` 视图。检查实际目录名称、顺序、连续来源范围、保留字段、组件 CRC-16/XMODEM 和整包 CRC。组件尾 CRC 不进入执行镜像；VR 首记录和所有六字节前缀仍保留在 VR 组件输入中。长度来自实际目录，不把原版长度固定成自定义固件的容量限制。未来其它机型的格式与板级 profile 必须有独立实际依据。

`nikon_vr_body` 另生成记录 1..N 的 256 B body 连续视图，并单独返回原记录 0 的完整 262 B。它验证 `04 01` 前缀和连续四字节地址字段，拒绝断序、截断和地址溢出。该转换不是对端成功回复，不写 Flash，不启动 CPU；调用者不能据转换成功宣称主机更新协议已经完成。

| 包内域 | 成熟执行机制 | 当前装载/连接职责 |
|---|---|---|
| MAS outer / inner | 上游 ARM TCG、GIC、架构 timer | outer 已由 machine 执行；inner 复制和释放必须来自 guest 的实际请求。不能独立预填 inner RAM 再宣称整体释放成功。 |
| SLA | 上游 ARM TCG，隔离地址空间 | 与 MAS ROM 数值地址相同而内容不同；独立 CPU memory view 或独立 ARM machine 保持域隔离。不能覆盖 MAS ROM。 |
| EX | 上游 ARMv7M、NVIC、SysTick | 原向量和 copy/unpack/clear 由 guest 消费；具体 Cortex-M 型号、时钟及 MCU 寄存器是待软件访问绑定的功能 profile。 |
| VR | 上游 RX TCG、ICU、timer | 固定上游有 `target/rx` 与 `hw/rx`；基于原 record body 和 source entry 建独立加载后 machine，不能照搬 RX62N 板的 Flash 大小、外围基址或异常 trampoline。 |
| TPJ | NDS32 成熟实现来源需固定 | 当前 QEMU 上游没有 NDS32 target。Andes 原作者曾提交 GNU simulator；该历史补丁不是已合入、已构建的实现，不能用当前 Andes V5 RISC-V SDK 冒充本镜像 NDS32。 |

上游 `qemu-system-arm` 与 `qemu-system-rx` 是分别构建的 target。全链路连接应复用现成 QEMU chardev/SSI/I2C 的真实传输机制和已恢复 guest 协议，不把其它 ISA 的假 CPU 放进 ARM 进程。多 target 之间的虚拟时间、暂停/恢复与输入顺序也必须由模型合同明示。

MAS 的 VR 传输已静态绑定 SPI channel4 (`0x2c0a0400`)、双 DMA、两缓冲、请求 op18 的 268 B 消息与完整 262 B 记录复制。默认消息前缀 `f7000801`，最后两字节 `00` 和记录负和校验；主机推进记录依赖真实回复首 byte=2。这些字节依据来自 Nightglass `vr-transport-contract.md` / `vr-record-loader.md`，不能生成无来源 ACK。TPJ 请求后端与 EX/VR 对端运行时需沿原 guest 消费者接线；当前 IPUD 的三 word external frame 是功能 transport API，尚不证明它就是这几个组件的物理链路。

原生单元测试覆盖全五视图及 CRC known-answer `123456789 → 31c3`、目录/边界/CRC/尾部损坏和每个截断位置的拒绝，以及 VR 首记录保留、body 顺序和断序拒绝。源码作者不另开构建；由唯一构建负责人将这些 C 测试接入官方 Meson/Make 并报告实际运行。输入完整不等于 CPU 或 GUI 完成，尚未运行的测试不写成通过。

公开依据：[QEMU RX 系统文档](https://www.qemu.org/docs/master/system/target-rx.html)、[QEMU target 列表](https://www.qemu.org/docs/master/about/emulation.html)、[Andes 原作者 GNU NDS32 simulator 提交](https://sourceware.org/pipermail/gdb-patches/2013-July/104886.html)、[Andes 当前 SDK 的实际 RISC-V 目标表](https://github.com/andestech/nds-gnu-toolchain)。本地固定上游 `4fc49f46dc95d4a27de2509e7fceb2931e91faeb` 的源码是实际 API 依据。
