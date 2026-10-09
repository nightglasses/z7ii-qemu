# 双 bank SDI/PHY/DDR 功能模型

本层继续 [原版 RTOS 启动依赖](https://github.com/mashiro01/nightglass/issues/331)，接续原版 `857ce78` 首停点 `PC10024c9c / WRITE32 PA20023000=1`。保留公开源 [`m8m-regs.h`](https://github.com/hypoxic/stubby-1.60.1/blob/d670fe97469f4fbac35fcf67fbab81c7ea84f153/src/m8m-regs.h#L4-L84) 的双 bank base、PHY/DDR reset/request/flag 和 DDRL 时序组与原指令相容；[`m8m-ddr.c`](https://github.com/hypoxic/stubby-1.60.1/blob/d670fe97469f4fbac35fcf67fbab81c7ea84f153/src/m8m-ddr.c#L90-L209) 的 release/init/training/W1C 顺序支持同 IP 假设。这不是 Nikon 整芯片或物理 reset 的鉴定。

原 wrapper `394` 的完整保存 native CALL 闭包自动选择 94 BB、5564 B。配置 whitelist 来源是 Ghidra 官方 `Instruction.getPcode` 和 `SymbolicPropogator.flowConstants` 的只读结果 `qemu-rtos-startup/work/sdi-ghidra-official-validated-first/contract.json`，94 BB 原字节/A32 context 核对通过，研究正式冻结 [6a6b721](https://github.com/mashiro01/nightglass/blob/6a6b721/research/z7ii/qemu-rtos-startup-contract.md)。110 个非协议 offset 的每 bank 支持值最多三个，含原训练 retry profile。地址和值按官方 STORE observations 汇总；条件候选写不是一次真实轨迹，未知轮询地址没有当零填入。每次原 poll 的 r0/r1/r2 都来自真实调用参数。未冻结自写语义实验不作为模型证据。

`hw/nikon/devices/sdi.c` 使用两个独立上游 SysBus 实例，分别映射 `20020000` 与 `20040000`，各实例只支持有限 word 接口。模型保存实际 guest 配置及配置完成标记；PHY init 要求 cold 配置、TOP/PHY release，DDR init 要求 PHY 已完成及 DDR release，mode0..4 训练要求 DDR init 且按原序列进行。训练完成后才允许仲裁与十五个软件 port、最终 interface 初始化。请求尚在进行或完成未 ack 时再次提交会失败；配置未齐、未支持值、offset、宽度和命令仍 `MEMTX_ERROR`。不把整个窗口当 RAM 或任意 regfile。

TOP/子块 reset 初值为 asserted，具名配置字初值0但配置完成标记为 false，done/pending 初值0。reset assert 会取消所属 timer 和依赖块状态；release 本身不完成 init。每个合法请求由 QEMU 虚拟 timer 在10微秒后产生对应 done；读 callback 不改变状态，写1确认才清 done。五模式训练 backend 明确假设虚拟训练成功，TRAINERR 只在训练完成后可读0；它不能证明物理 DDR/calibration 健康。原代码仍实际执行每次 request、poll、ack、error判断和后续配置，没有 host 改 PC/调用 ISR/填 RAM magic。

此功能模型不依据训练建立宽 RAM，也不声称支持所有 DDR 运行配置、真实地址仲裁、流量或中断。现有稀疏 RAM 的范围仍由原复制/栈合同定义。未知域留给后继真实访问收敛。增量 native 已编译通过。实际证据如下。

源码07342da 的原版 MTTCG 实际运行保存于 `work/issue331/original-sdi-07342da/receipt.json`，exit0，宿主 wall0.832005秒，38,239,096 B直接内容比较未变。guest实际经过两个bank，各24次请求/虚拟完成，共48次；新首阻断 `CPU0 PC1002bdd8 / WRITE32 PA20016020 / value60000000`。随后DataAbort最终PC10100010/SPb2ba8000，CPU1仍PC1002bfa8/SPb2b84000早期等待。没有RTOS首派、SMP释放或菜单完成宣称。宿主wall不代表EXPEED性能。

第一次源码14d3ada 原版收据 `original-sdi-14d3ada-first` 保留：port6拒绝是本模型把8c00配置范围错误延伸到9400、包含下一port的配置。07342da已统一按各真实下一port边界确定配置前提，未添加成功值或绕过guest。原bank1最后真实poll E300、却ack CC30的不对称也保留；模型不会替guest清E300。

`work/issue331/sdi-functional-smoke-reset/result.json` 的62个当前合成契约通过。整双bank夹具覆盖配置、PHY/DDR初始化、五训练mode、仲裁、十五port和最后interface，验证bank1 E300仍done1；拒绝夹具覆盖未配置request、训练前读取、错误mode/配置/ack、宽度和保留offset。配置夹具来自独立官方静态观察，未从C实现读取表构造期望；没有原厂指令数组。

Review correction: asserting arbitration reset at `+0x4000` also cancels every downstream port/final timer and clears their reset, completion and applied states; the PHY/DDR training result is retained. Two deterministic synthetic fixtures cover reset before a port timer expires and after port completion. These are device reset checks, not a new original-firmware run.
