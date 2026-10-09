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

`machines/vr.c` 新增 `nikonvr` 单域加载后 profile，使用完整容器的原 VR 记录 body，按原 13 条 COPY / 11 条 ZERO 目的区间分配合并的有限 RAM，由 guest 执行复制/清零。没有在 host 预填应用、INTB 或任务对象。包中 `fff00010` 第一条 PUSHM 依赖调用者的 R0；缺少包外 loader 时，本 profile 明确采用 caller-SP=`2000` 和 4 KiB 虚拟栈假设。入口与 ROM 地址也属于已恢复软件映射的加载后模型，不冒充机身 reset vector。

原 VR bootstrap 的 `fff00027` 实际 `MVTC r14,EXTB`。固定上游 RXv1 已有 MVTC/MVFC 编解码，但将控制寄存器 13 丢弃；这会静默丢失 RXv2 的异常向量基址。源码扩展用显式 `has-extb` 能力保存完整寄存器，在 reset 时取官方 `ffffff80` 初态，并让成熟异常分派读取真实 EXTB。RX62N 默认关闭能力，`nikonvr` 明确开启。此扩展是现有 CPU 框架的控制寄存器能力，并不宣称实现了全部 RXv2 指令或某个真实芯片型号。

当前保存的 `application.S` 和 `bootstrap.S` 已一次遍历归并指令族；普通算术、控制流、字符串、位操作均存在于固定上游 RX translator，明确新增项是 EXTB。这是源码族覆盖判断，实际编码、访存、异常、时序与运行行为仍需原版执行对照。VR 访问 `80020` 系统配置、`8c000` ports、`c1200/c1280` SPI 等外设，不能搬用 RX62N board 的外围布局。未提供的外设或 `fffc8000` 包外接口必须由自然访问指出来源，再接真实软件协议，不能配置成功位或预注入回复。

上游 `qemu-system-arm` 与 `qemu-system-rx` 是分别构建的 target。全链路连接应复用现成 QEMU chardev/SSI/I2C 的真实传输机制和已恢复 guest 协议，不把其它 ISA 的假 CPU 放进 ARM 进程。多 target 之间的虚拟时间、暂停/恢复与输入顺序也必须由模型合同明示。

MAS 的 VR 传输已静态绑定 SPI channel4 (`0x2c0a0400`)、双 DMA、两缓冲、请求 op18 的 268 B 消息与完整 262 B 记录复制。默认消息前缀 `f7000801`，最后两字节 `00` 和记录负和校验；主机推进记录依赖真实回复首 byte=2。这些字节依据来自 Nightglass `vr-transport-contract.md` / `vr-record-loader.md`，不能生成无来源 ACK。TPJ 请求后端与 EX/VR 对端运行时需沿原 guest 消费者接线；当前 IPUD 的三 word external frame 是功能 transport API，尚不证明它就是这几个组件的物理链路。

原生单元测试覆盖全五视图及 CRC known-answer `123456789 → 31c3`、目录/边界/CRC/尾部损坏和每个截断位置的拒绝，以及 VR 首记录保留、body 顺序和断序拒绝。源码作者不另开构建；由唯一构建负责人将这些 C 测试接入官方 Meson/Make 并报告实际运行。输入完整不等于 CPU 或 GUI 完成，尚未运行的测试不写成通过。

公开依据：[QEMU RX 系统文档](https://www.qemu.org/docs/master/system/target-rx.html)、[QEMU target 列表](https://www.qemu.org/docs/master/about/emulation.html)、[Andes 原作者 GNU NDS32 simulator 提交](https://sourceware.org/pipermail/gdb-patches/2013-July/104886.html)、[Andes 当前 SDK 的实际 RISC-V 目标表](https://github.com/andestech/nds-gnu-toolchain)。本地固定上游 `4fc49f46dc95d4a27de2509e7fceb2931e91faeb` 的源码是实际 API 依据。

EXTB依据：[Renesas RXv2 向量与 reset 初值](https://tool-support.renesas.com/autoupdate/support/onlinehelp/ja-JP/csp/V8.07.00/CS%2B.chm/Coding-RI600V4.chm/Output/SYSTEM.html)。原软件表与源输入固定 Nightglass `fdbe3f3ed8c6523bb85885920c049fd92c2ae130`，本地 `work/vr-rx-source-1/startup-table-observation.json`、`work/vr-rx-source-project-1/{application,bootstrap}.S`。原厂 ASM 仍在 ignored work，未进入模拟器 Git。
