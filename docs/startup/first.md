# Z7II C1.70 原版 MAS 首次诊断执行

2026-10-08；[运行 Z7II 原版 QEMU machine 并恢复启动设备依赖](https://github.com/mashiro01/nightglass/issues/326)。**原版 MAS 已实际执行，首次未覆盖写入被明确拒绝并停止。整体启动、`O0x178` 到达及重建版对照均未验证。** 本文只记录诊断基线和下一项硬件合同，不实现设备。

固定 machine 提交 `4f9ccc6c9b5b66a02aace86a9a6c0dd77953db0a`，上游 QEMU `4fc49f46dc95d4a27de2509e7fceb2931e91faeb`（运行版本 `11.1.2 / v11.1.2-dirty`，含本地 machine 集成）。主代理完成 macOS ARM 原生构建及真实运行；作者读取原始收据与日志核对，未另跑 QEMU。原始记录保留在 ignored `work/issue326/r4-4f9/{receipt.json,qemu.log}`；此前失败记录未覆盖。

输入为 Nightglass ignored `work/z7ii-1.70/02_eg1840_mas_01700.payload.bin`，38,239,096 字节；收据记录运行前后直接内容相同。它是已脱包 MAS component payload，不是更新整包，也不是父 `O[0x208853c,0x228cf3c)` 的 `mas-inner`。[原点与映射](https://github.com/mashiro01/nightglass/blob/main/research/z7ii/mas-mapping.md)、[嵌套镜像](https://github.com/mashiro01/nightglass/blob/main/research/z7ii/subimages.md)

实际启动参数为 `-M z7ii-mas-diagnostic -accel tcg,thread=single -bios <上述 payload> -display none -serial none -monitor none -S -qmp unix:<短路径>/qmp.sock,server=on,wait=off -D <run>/qemu.log -d in_asm,int,guest_errors`；完整绝对路径 argv 在原始收据中。控制端用官方 QMP继续执行，读取停止状态/寄存器，最后退出 QEMU，进程退出码为 0。

| 直接观察 | 结果 |
|---|---|
| 初始状态 | `prelaunch`；PC `0x10020000`，CPSR `0x400001d3`，所有通用寄存器及 SP 为零；ARM/SVC/secure，默认 Cortex-A9 reset 状态 |
| 原始入口 | 日志记录首条 `20 2f 00 ea`，随后进入 `0x1002bc88` 并执行 CP15 初始化、MPIDR selector 和六模式 SP 设置 |
| 首次拒绝事务 | CPU0，PC `0x1002bda4`，诊断 PA `0x04264000`，write，4 字节，值 `0x21`；`MEMTX_ERROR` |
| 异常和停止 | 上游 ARM 接受 Data Abort；QMP stop reason/status 均为 `debug`，running=false；最终 PC `0x10103610`，CPSR `0x600001d7`，SP `0xb2ba8000`，LR `0x1002bdac` |
| 主机会话时间 | 收据 `wall_seconds=0.11494487500749528`，包含控制与退出；不是固件启动耗时或 EXPEED 性能 |

复现初始假设保持可见：单核 Cortex-A9；候选 MAS 程序 VA `0x10020000` 被用作诊断物理装载地址；上游默认 CPU reset；两个静态复制目标、16 KiB TTBR0 迁移目的窗口和六个 CPU0 栈槽各 16 KiB，仅按[装载参数](cpuboard.md)有限映射。无 BootROM、真实 loader、IRQ、timer、IPC 或握手设备；未预填 RAM 复制内容、未置 ready magic、未改原始自环。由于错误事务先触发异常，再处理异步 stop，最终 PC 与访问发起 PC不同。

## `0x04264000` 的下一项合同

以下是对原始字节的有限静态分析，与上述实际到达范围分开。仅用既有 Capstone 对指定 ARM/Thumb窗口解码，不运行 guest、不运行 Ghidra/native 构建、不修改输入。`O` 为 MAS payload 偏移；RAM2 VA 按静态复制投影 `0xb2bb0000+(O-0x2428d80)`，尚未实际到达。[复制与配置依据](https://github.com/mashiro01/nightglass/blob/main/research/z7ii/mas-hardware-configuration.md)

| 原版消费 | 确定的软件合同 |
|---|---|
| ARM `O0xbd9c..0xbdac` | `O0xbff0=0x04264000` 被加载为基址；`O0xbda4` 写 `base+0=0x21`；`O0xbff4=0x153b0902` 被加载，`O0xbdac` 向 `base+0x20` 写该值。两次都是 32-bit STR；该直接入口没有读回或 ready 条件。后者尚未执行。 |
| ARM `O0xbdb0..0xbdbc` | 保存原 SP；从 `O0xbff8` 装入临时 SP `0x04008f00`；`BL O0x2b8→O0x914`，原始后者为 BX LR；再调用 `O0xd700`。临时栈窗口目前未映射，不能用六模式栈替代它。 |
| RAM2 Thumb VA `0xb2bb0692..0xb2bb06aa` | literal 父 `O0x2429444` / RAM2 VA `0xb2bb06c4` 同为 `0x04264000`；读 `base+0`、清 bit3、写回，传 `0x432` 给 `0xb2bb06cc`，将返回值保存至 `0xb1806f5c`。 |
| RAM2 Thumb VA `0xb2bb06ac..0xb2bb06bc` | 同一 `base+0` 读→置 bit3→写；从 `0xb1806f5c` 读保存值，尾转同一 `0xb2bb06cc`。这是真实 RMW 消费，不能假定寄存器始终为零；两条路径的到达条件/调用者仍待核。 |
| RAM2 Thumb VA `0xb2bb06cc..0xb2bb0712` | 从 TTBR0取16 KiB对齐表基址；从 `+0x400` 起修改64个word的低20位，保留原高位，随后缓存维护和 DSB/ISB，返回第一项原低20位。按短描述符条件，这是 VA `0x10000000..0x13ffffff` 的页表属性切换，不证明 flash 容量为64 MiB。 |
| 紧邻 RAM2 VA `0xb2bb0714` 起 | 观察到16-bit写 `0x10000aaa=0xaa`、`0x10000554=0x55`、随后 `0x10000aaa=0x80`。与 AMD/CFI NOR命令上下文相符；这支持“外部 NOR/flash-bus控制候选”的推断，但不绑定确切控制器、器件或 bit3 名称。 |

原载荷中对该完整32-bit literal的直接字节搜索仅找到 `O0xbff0` 和 `O0x2429444`。这不是完整 xref/访问清单：计算地址、MOVW/MOVT、间接调用或其他执行域仍可能产生访问。`0x21` 置 bits0/5，`0x153b0902` 的字段语义、reset 值、读值、状态副作用、IRQ/时序及物理地址绑定仍未知；上述代码没有提供它们。

`O0xd700..0xd764` 属于另一个基址 `O0xd768=0x04281000`：先把 `+0x2c` bits16..24替换为`0x163`，清bit0；清`+0x28` bit10；忙等40,000次；再清`+0x2c` bit1、`+0x28` bit11、`+0x2c` bits4..6，忙等8,000次并返回。这里是软件延迟循环 `O0xd800`，不是读 status 的 ready轮询。它不能用来命名 `0x04264000`。既有配置函数的66次word写则针对 `0x04280030/34/38` 和 `0x04282000` 五列寄存器组；Socionext pinctrl相似性只适用于该布局，不能扩成首次阻断设备已识别。[既有66写研究](https://github.com/mashiro01/nightglass/blob/main/research/z7ii/mas-hardware-configuration.md#配置表的字段和真实写入)、[厂商 pinctrl 原始提交](https://lkml.iu.edu/1811.2/01402.html)

公开上游没有在本次有限核查中建立可直接使用的 `0x04264000` 设备身份。[QEMU固定 `pflash_cfi02.c`](https://github.com/qemu/qemu/blob/4fc49f46dc95d4a27de2509e7fceb2931e91faeb/hw/block/pflash_cfi02.c#L498-L512)已有 AA/55 unlock、program/erase/CFI状态机，可在核定NOR型号、几何、bus width、bank/alias/权限和缺失flash范围后复用，不能拿它代替外部bus控制寄存器。[固定 `arm_sysctl.c`](https://github.com/qemu/qemu/blob/4fc49f46dc95d4a27de2509e7fceb2931e91faeb/hw/misc/arm_sysctl.c#L123-L144)的`+0=ID/+0x20=LOCK`合同与本序列未匹配；[Socionext作者 Linux M10V DTS](https://github.com/torvalds/linux/blob/ffc253263a1375a65fa6c9f62a893e9767fbebfa/arch/arm/boot/dts/socionext/milbeaut-m10v.dtsi#L56-L74)也给出不同地址，不能搬入该基址。RTOS内核/API匹配不会补出这些SoC寄存器语义。

下一步应先核上述 RAM2 控制路径的调用者/恢复语义、NOR ID/CFI消费者和真实bus映射/寄存器资料，再定义仅覆盖已证32-bit访问的设备及未覆盖条件。还须恢复临时栈 `0x04008f00` 的边界和独立 `0x04281000` 初始化合同。当前 machine 保留首次阻断，不新增通用regfile、自动ready或影射现成板。QEMU覆盖结果不能证明真实Flash/BootROM/calibration/硬件健康或EXPEED性能。
