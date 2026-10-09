# 双 bank SDI/PHY/DDR 功能模型

本层继续 [原版 RTOS 启动依赖](https://github.com/mashiro01/nightglass/issues/331)，接续原版 `857ce78` 首停点 `PC10024c9c / WRITE32 PA20023000=1`。保留公开源 [`m8m-regs.h`](https://github.com/hypoxic/stubby-1.60.1/blob/d670fe97469f4fbac35fcf67fbab81c7ea84f153/src/m8m-regs.h#L4-L84) 的双 bank base、PHY/DDR reset/request/flag 和 DDRL 时序组与原指令相容；[`m8m-ddr.c`](https://github.com/hypoxic/stubby-1.60.1/blob/d670fe97469f4fbac35fcf67fbab81c7ea84f153/src/m8m-ddr.c#L90-L209) 的 release/init/training/W1C 顺序支持同 IP 假设。这不是 Nikon 整芯片或物理 reset 的鉴定。

MAS wrapper `394` 的完整保存 native CALL 闭包为 94 BB、5564 B，Ghidra 官方 `Instruction.getPcode` 和 `SymbolicPropogator.flowConstants` 的只读结果冻结于 [6a6b721](https://github.com/mashiro01/nightglass/blob/6a6b721/research/z7ii/qemu-rtos-startup-contract.md)。新增 SLA 证据为整个重建源码 `1076d278` 的 `sla.S`，逐函数完整阅读 `1002361c`、`10023d10`、`10024cb8`、`100253a0` 与训练 helper `10024588`、`100248f8`、`10025c1c`、`10025f8c`。当前实现移除逐值 whitelist，按 TOP/rank、clock、PHY AC、DDR MR/geometry/AC/refresh/ZQ/command、arbitration、port、DDRL wait/impedance/manual lane delay/search window/eye/pattern 等 38 组保存 110 个具名 word。组外 offset 仍拒绝，不能把整个窗口当 RAM。

`hw/nikon/devices/sdi.c` 使用两个独立上游 SysBus 实例，MAS/SLA 的 PA base 分别为 `20020000` 与 `20040000`。只支持对齐的 little-endian 32-bit 访问。PHY init 要求全部 PHY/clock/DDRL 配置、TOP/PHY release，并校验 `RANKSEL` 非零且与 TOP `+10c` 的两个 rank-enable 字段一致。DDR init 要求 PHY 已完成、DDR release、完整 DDR 配置、非零 geometry/ZQ/command 和三组相等且 256MiB 对齐的地址边界。mode0..4 训练要求 DDR init、完成前一次 W1C，并按完整顺序执行。全部五种训练成功才允许仲裁；十五个 port 与最终 interface 要求仲裁完成、自己的 release/configuration/clock 已应用。请求 pending 或 done 未 ack、配置未齐、保留位、未知 offset/宽度/命令仍返回 `MEMTX_ERROR`。

TOP/子块 reset 初值 asserted；具名配置字初值0而配置完成标记 false；done/pending 初值0。这是功能复位假设。reset assert 取消本块与依赖块 timer/state；PHY/DDR reset 清除训练结果；仲裁 reset 保留训练但取消全部下游。release 不产生完成。每个合法请求由 QEMU virtual timer 在10微秒后产生本块 done，这是名义功能时序而非物理时间测量。读取无副作用，写1只清该 flag；pending ACK 失败；对已清 flag 的 W1C 合法。原 SLA 两个 train 函数的最终 poll 为 E300、随后 ACK CC30；模型保留 E300 done，不代写正确 ACK。

训练结果使用明确的功能 backend 假设：每个 lane 的已应用 manual write-delay 作为稳定校准 target，mode0 的已编程 WL 搜索区间必须包含 target；空区间或越界产生 `TRAINERR.WL_ERR=4`。mode3/4 要求每 lane eye-control enable 与非零搜索参数，否则分别产生 RDE/WDE error。mode1/2 使用完整已应用的 gate/synchronizer 配置。TRAINERR 只属于完成的本次训练；新请求 pending、训练配置变化或 PHY/DDR reset 后读取会阻断。失败仍产生真实 done/W1C，五模式顺序仍继续；全部五次完成后可重编窗口并从 mode0 重试，只有全组成功才能启用仲裁。结果没有从固件成功分支硬编码：可消费的错误/重试路径由实际配置与 timer 产生。此 backend 不模拟电气 DDR 眼图、芯片健康或实体 memory 流量。

SLA selector 是 GPIO `PA0428202c & 0x4`（bit2）。clear 选择 `1002361c(0,1)` / `10023d10(1)`，继而相同函数 bank selector2；set 选择 `10024cb8(0,1)` / `100253a0(1)`，继而 selector2。init 的非零 r0 分支跳过 cold PHY 配置但仍执行 DDR reinit；两族保留已有配置并要求其确实已应用。主族应用 rank1/TOP101、DDR `3504=2030100` 和 `3508/350c/3514=40000000`；替代族应用 rank3/TOP303、DDR `3504=2030101` 和三个 `30000000` 边界。两族执行完整相同五训练mode与三轮候选 WL window，而非只增加两个停点值。helper `10020920(statusPA,1,02625a00)` 读取 flag 并最多迭代40,000,000次；limit 是软件次数，不是 IRQ、时钟或 host deadline。

四组源没有 DMA transfer 命令、传输 payload 或 IRQ-enable 编程。当前模型不添加 peer、IRQ 接线或 DMA 完成；实际路径消费本块 request/flag/W1C/TRAINERR。具名 packed timing/geometry/pattern 字段保留 guest 完整 word，物理位布局未确认时不推断 reserved mask；已定义 clock/rank/lane/eye 字段按功能 mask 校验。当前有限 RAM 仍由 machine 的独立复制/栈合同定义，SDI 成功不自动建立广域 RAM。未知硬件 IRQ enable、运行时流量与 PHY result-data 地址需要完整生产者/消费者证据。

源码07342da 的原版 MTTCG 实际运行保存于 `work/issue331/original-sdi-07342da/receipt.json`，exit0，宿主 wall0.832005秒，38,239,096 B直接内容比较未变。guest实际经过两个bank，各24次请求/虚拟完成，共48次；新首阻断 `CPU0 PC1002bdd8 / WRITE32 PA20016020 / value60000000`。随后DataAbort最终PC10100010/SPb2ba8000，CPU1仍PC1002bfa8/SPb2b84000早期等待。没有RTOS首派、SMP释放或菜单完成宣称。宿主wall不代表EXPEED性能。

第一次源码14d3ada 原版收据 `original-sdi-14d3ada-first` 保留：port6拒绝是本模型把8c00配置范围错误延伸到9400、包含下一port的配置。07342da已统一按各真实下一port边界确定配置前提，未添加成功值或绕过guest。原bank1最后真实poll E300、却ack CC30的不对称也保留；模型不会替guest清E300。

旧 `work/issue331/sdi-functional-smoke-reset/result.json` 的62个合成契约属于逐值模型的历史验证，不能作为新族实现的收据。新 native C qtest `tests/qtest/nikon/sdi.c` 从独立 MAS/SLA source 配置建立两个 bank 的单/双 rank 完整初始化、五mode训练、十五port与最终接口；覆盖 9999ns 未完成 / 10000ns 完成、配置回读、失败后完整五mode retry、rank/边界不一致拒绝、顺序/宽度拒绝、pending 配置/ACK拒绝与 DDR/仲裁 reset 取消。编译和运行由唯一 builder 完成，提交时不冒称已通过。

Review correction: asserting arbitration reset at `+0x4000` also cancels every downstream port/final timer and clears their reset, completion and applied states; the PHY/DDR training result is retained. Two deterministic synthetic fixtures cover reset before a port timer expires and after port completion. These are device reset checks, not a new original-firmware run.
