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
本文件尚未宣称编译、fixture 或原版 TPJ 运行成功；由唯一构建 owner 返回实际结果后更新。
