# NDS32 原生系统执行

自有实现位于 `sim/nds32/`、`include/gdb/nds32system.h` 与 `cmake/nds32.cmake`。
官方 Andes provider 保持固定、只读；CMake 在 ignored source view 内接入原生 GNU 编译图。
没有新增 ISA 解码器，指令执行、有限地址映射、异常 unwind 与虚拟事件继续使用 GNU simulator。

## 体系结构依据

- [AndeStar ISA v1.3](https://download.paulk.fr/coreboot/mt8173-spm/documentation/isa/AndeStar_ISA_v1.3.pdf)，IRET 第 76–77 页，MFSR/MTSR 第 124/135 页，SETEND/SETGIE 第 162/163 页。
- [AndeStar SPA V2 v1.9](https://www.scribd.com/document/980985425/AndeStar-SPA-V2-UM026-V1-9)，§4.2、§4.4、§9.3；这是厂商手册的公开镜像，机身实现仍需独立证明。
- [Andes 优先级应用说明](https://www.andestech.com/wp-content/uploads/%E5%9C%A8%E6%99%B6%E5%BF%83%E5%B9%B3%E5%8F%B0%E8%BF%90%E8%A1%8C%E5%85%B7OSC%E7%9A%84FreeRTOS.pdf)，第 4–8 页。
- [Linux v5.17 NDS32 bitfield](https://github.com/torvalds/linux/blob/v5.17/arch/nds32/include/asm/bitfield.h) 和 [原生 IRQ 驱动](https://github.com/torvalds/linux/blob/v5.17/drivers/irqchip/irq-ativic32.c)。

0→1 保存 IPC/IPSW；1→2 另外保存 P_* 与 P0/P1；2→3 保留先前寄存器，仅保存 OIPC 与升高 INTL。
IRET 根据进入指令前的 INTL 恢复对应层级。异常使用 GNU `sim_engine_restart`，故故障指令不会继续写目标寄存器。
IRQ 在指令边界依据 GIE、mask、固定或两位可编程优先级与 CPL 选择；输入只能由实际设备改变。
level 输入写 pending 不能取消外部信号；edge latch 才由 W1C 清除。
原版四条 `standby wake_grant` 使用明确的 consent 与 wake 输入；外部 IRQ 即使被 mask 仍能唤醒已获 consent 的核心。
没有 consent 不前进到下一条指令，等待阶段仅处理 GNU 虚拟事件。

## TPJ 功能配置与边界

原版完整源证实：25 个向量槽，23 个 IRET，MASK/PEND 使用 `0xffff`，IVB 写零；
没有 PRI、CTRL、TRIGGER 或 MASK2/PEND2 消费者。向量数量不能证明硬件 NIVIC/version。
`--nds32-system` 明确选择 16 输入 IVIC v1、固定优先级、无 shadow SP、物理地址、little-endian 的功能配置；
这不证明 Nikon 芯片型号、原始 BootROM 或其复位值。原版软件负责自己的 SP、COPY、ZERO 与 IRQ 启用。

系统模式不再默认创建 64 MiB RAM。调用方必须提供有限 ROM/RAM，并通过
`nds32_system_attach()` 对每个实际设备区域绑定原生 read/write 回调。
没有回调或拒绝的宽度/地址产生总线异常，不返回零或 ACK。设备区域不可执行。
回调函数值由设备复制；调用方的 opaque 生命周期持续至 `sim_close()`。
超过六输入的 software interrupt 输出必须由调用方显式绑定，不猜测其板级连线。
`nds32_system_run()` 以 GNU 虚拟 instruction/event ticks 提供有限执行量；不是实测时钟模型。
开启 MMU translation 明确停止，不能将尚未实现的 MMU 冒充恒等映射。

## 接入与验证

构建 owner 提供 `NDS_UPSTREAM`、`native_file(path, content)` 与只读源码 view。
`cmake/nds32.cmake` 要求每个上游 seam 唯一匹配，接入 `system.o`、原生 boundary 硬件和 decoder hooks。
`configure.ac` 使用现成 `SIM_AC_OPTION_HARDWARE`；构建 owner 以兼容 Autoconf 再生成 configure/config.in，
使用 GNU 本身的 configure/Make 编译，不编辑官方依赖，也不使用 Python 包装。

`tests/unit/nds/system.c` 使用 GNU 现成 opcode 宏生成合成指令，通过真实 decoder 执行 IRET、IRQ 选择和总线异常。
断言覆盖嵌套恢复、overflow return、priority/CPL、level 与 edge 生命周期、真实回调 read 与 fault unwind。
唯一构建 owner 已完成官方 provider、system.o、libsim.a 与 run 的原生编译链接，硬件配置 WITH_HW=1。
无 ELF 的合成 fixture 明确传入 GNU 标准参数 `--endian=little`，五组完整断言已实际执行并 exit 0：
嵌套 IRET、IRQ priority/CPL、有限 MMIO 与 fault unwind、特权 SR 访问、standby consent/wake。
原始收据为构建 owner 的 ignored `work/validation/gui/ndsendiancheck.log`；原版 TPJ 尚未宣称启动成功。

## 原生进程入口

`sim/nds32/runner.c` 使用 BFD、GNU simulator 与 POSIX 非阻塞 socket/poll；没有新增执行器或构建包装。
完整 ELF 的入口为零，每个可分配段都必须可加载、有内容，VMA=LMA 且不重叠，覆盖全部 `[0,e000)`。
源码 owner 已用 GNU readelf 确认现有完整产物满足该条件；e000 是当前更新包的 TPJ 长度，不是已证明的芯片 ROM 容量。
加载后 ROM 只读且可执行；原软件完成 RAM COPY/ZERO 与栈初始化，不由 host 提前复制或设置 SP。

传输共享 `include/hw/nikon/wire.h`，连接 QEMU socket CharBackend server。
一笔 CLOCK 只能由真实设备移位结果完成；SIGNAL 传递物理线电平，CANCEL 取消对应 token。
输入按完整 40 字节帧校验；输出队列有限，部分读写与背压保持原事件，不生成 guest ACK 或协议包。
`--ticks` 只表示 GNU instruction/event quantum；`--consent 0|1` 明确给出外部电源许可，默认零。
`sim/nds32/tpj.c` 接入 11 个有限域：40000、41000、42000、50000、53000、60000、61000、70000、71000、72000、80000。
`fields.h` 保留完整源码库存里 194 个无跨调用/控制流合并的常量地址表达式及宽度；另接入已逐条证明的
flash helper、六半字变换、60000 ingress/packet/reply、800c0 初始化循环。空洞及未证明的宽度拒绝访问。
扫描 RAM 仅绑定原 CPU 循环写入的 `[2a000,2a344)`、`[2b000,2b344)`；不会扩成四个猜测的 4 KiB bank。
出处：[TPJ 全源协议合同](https://github.com/mashiro01/nightglass/blob/feat/tpj-complete-source/research/z7ii/tpj-source-device-contract.md)。

`--flash FILE` 明确提供完整 64 KiB 外部 NOR 内容；长度不完整直接拒绝。
另一个明确选择是 `--update FILE`：57344 字节更新组件放入 64 KiB 的 erased-FF 功能快照。
原检查常量 `1fd808=(2000−8)×ff` 证明 `[e000,fff8)` 的 erased 字节假定；
最后八字节选为 FF 是明确的虚拟外部输入假设，不能当作实机读取。该配置不会静默替代 `--flash`。
41005/06/07 的 55/aa/66 解锁驱动真实命令：ab/b9、06、3b、20、02；41004 为 count−1，
41012 是固定读 latch，原 helper 的第二次读取仍返回同一字节。擦除以已证明的 1000 stride 操作，
编程采用显式 NOR 按位与功能模型；awake、write-enable、有限 extent、完整 payload 均检查。
原组件 `cffc` 的 little-endian word 等于原 `[0,cffc)` 字节加和，`dffc` 等于 `[d000,dffc)` 字节加和。
Oca4 把两 word、两 footer 的逐字节和、erased tail 和最后八字节和相加，与 41018 比较。
41018 因而按当前完整 flash 的真实逐字节加和生产；真实 erase/program 后重新计算，不设置“校验通过”旗标。
`--save FILE` 导出完整且 idle 的 NOR 状态，重新导入后保留真实写入结果；原输入文件不会被改写。
完成使用 GNU 一 tick 事件，代表功能时序假设，不代表已测设备频率。缺 backing、超范围及未知命令保留 busy 并报告实际边界。

`--transform FILE` 是 24 字节的外部记录：六个 little-endian 输入半字，随后六个真实输出半字。
53002 请求必须与输入匹配，才由原生事件写入该记录的输出并置 53001 ready。
没有匹配记录时不会 ready、复制输入或猜测变换算法。

`--snapshot FILE` 每行是 `十六进制地址 十进制宽度 十六进制值`，只接入已证明的可读字段。
busy、flash read latch、flash sum 和变换 ready 不能由该文件注入；数据的来源由提供者负责。
6001c..21、50014/54/94、72024、721c0、80050 的输入走已证明的原 ISR 消费者；
inactive 事件、普通读写配置零初态及71010通知 idle-high backing 是明确的功能冷启动假设，不是物理复位证明。
通知 wire 必须等实际 guest 写71010之后输出，初始 backing 不会自动发 ready/高电平。
原 guest 清硬件 flag 后撤销 level IRQ，不由 host 修改 RAM 中的等待标志。

`--limit TICKS` 为首次原版观测提供有限指令/事件预算，输出实际 PC 和 sleep；没有 socket 也可独立运行。
设备遇到第一项无数据生产者/非法字段时输出实际 PC、地址、宽度和原因；这是实际依赖，不能当作完整启动收据。
正常触控使用共享 typed I2C START/WRITE/READ/NACK/STOP，与 MAS address55、cmd10/read2、cmd11/read7 匹配。
真实硬件字段驱动原 ISR 和回调，READ 只读取原 guest 写入的60040输出，不按命令号码生成回复。
见 [触控合同](../devices/touch/protocol.md)，其中列明 source 证据与功能连线假设。
SPI CLOCK 保留原 token 等待它自身的真实绑定，不借正常 I2C 或 EX frame；720xx 仍是配置/状态/缓冲域。

`tests/unit/nds/tpj.c` 使用合成外部文件，覆盖 NOR read latch、缺 payload 不完成、count−1/NOR 编程、
六半字外部输出、原事件 level 解除与精确 reply 尾界，以及缺 producer/寄存器空洞拒绝。
唯一构建 owner 已完成 runner 的成熟 GNU 编译链接，并实际执行上述 endpoint fixture exit 0。
原版首次有限 probe 加载完整 ELF 后在 O b3fe（原 COPY helper 内的 `bnes38`）停止；这是首个 4096 tick quantum。
该次日志的 unknown signal 0 来自 host 预算 callback 使用了错误的枚举：官方 `SIM_SIGNONE` 是 64，
不能传字面量零。修复只更正该停止信号，runner 的非零故障检查保持原样，不改 guest PC 或跳过 COPY。
`tests/unit/nds/budget.c` 独立检查两次预算停止/继续，以及实际非零 native stop 不被接受为预算完成。
预算修复的独立 fixture 已实际 exit0；原版自然继续到 Ob22 读取41011，报告 external flash backing absent。
typed I2C、实时 NOR sum/保存、显式 update 快照和原50018一轮计数/IRQ/ack组已经由唯一构建 owner 实际编译链接并执行六组 fixture exit0。
原完整 ELF 使用显式 `--update` 后自然通过 Oca4 的完整外部 NOR 校验，推进到 Oa966 读取80042。
原日志记录同一首次字段边界被异常处理重复进入；这不是全启动成功。原始收据为 ignored `work/tpjtypedcheck.log` 与 `work/tpjtypedoriginal.log`。
当前整组补齐 Oa94a 的80043请求/80042完成和确认、十一项通道配置、384项半字配置与流式索引、80216自清请求，
同时保留执行量子中的真实通知边沿并在首次设备边界后由原生事件停止。九组 focused fixture 已实际 exit0，
原版继续至 Oa588 WRITE8/PA801a0，单次真实 bus fault 后停止；收据为 ignored `work/tpjcontrollerqualified.log` 与 `work/tpjcontrolleroriginal.log`。
整源前查已覆盖后继四字节801a0..a3、九页801ea/801e0 sensing 配置，以及三处 standby 消费的60003 bit4功能谓词。
这些字段只保留实际 guest 配置或同一实际 command IRQ cause，不生成 ADC sample、扫描计数、720 ready 或触点。
正常 sample 生命周期已由唯一 builder 在固定 `a0b8e428` 实际编译链接并执行十二组 fixture exit0；
收据为 ignored `work/tpjsamplesqualified.log`，其中六条缺输入/非法边界诊断来自预期负向断言。
它验证有限外部 sample、DMA 指针、缓冲所有权与取消/恢复合同，不能据此宣布原版完整启动。
