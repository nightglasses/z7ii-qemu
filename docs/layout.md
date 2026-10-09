# Nikon machine 与功能设备结构

Issue [394](https://github.com/mashiro01/nightglass/issues/394) 将 Nikon 实现与官方 QEMU submodule 分开管理。维护文件名不使用连字符，优先使用一个清楚单词；重命名不改变既定 QOM 类型和函数 prefix。首个 machine 为 Z7II；现有 `z7ii-mas-diagnostic` 启动入口、CPU 假设、固件放置、访问门禁及 IRQ 连线保持。通用设备使用 Nikon QOM 类型，旧设备路径和旧 QOM 名称已删除。

`hw/nikon/machines/z7ii.c` 负责 Z7II 的 CPU、内存布局、固件装载和设备连线。`hw/nikon/devices/` 负责各功能协议；MEMC、时钟控制和请求完成状态机也从 machine 中分离。公共类型和必要接口位于 `include/hw/nikon/devices.h`。绝对 MMIO 布局只在 machine 中指定，通用设备从实际映射的 MemoryRegion 取得错误地址。QEMU 的 QOM、SysBus、MemoryRegion、Clock、IRQ、timer 和 BH 仍是唯一执行框架。

地址编号不代表物理 IP 身份。以下名称来自当前已实现的软件协议；不同寄存器及事件语义仍保持独立，没有为统一命名而合并状态机。

| 原模块 | 当前源文件 | 协议范围 |
|---|---|---|
| control2010 | `gate.c` | 配置、事件掩码、外部门及延迟停止确认 |
| control2014 | `banks.c` | 两对象配置、两 pending bank 及分层 IRQ |
| control2016 | `dispatcher.c` | 字节寄存器、子 pending 与顶层 IRQ 路由 |
| control20201 | `sampler.c` | 一组外部数据采样与独立事件 |
| control20202 | `staging.c` | 分阶段配置、请求及事件状态 |
| control20203 | `parameters.c` | 参数配置及独立外部事件 |
| control20204 | `table.c` | 配置表及三组外部数据采样 |
| control20205 | `shared.c` | 共享物理参数字及外部事件 |
| control2038 | `pairs.c` | 两 bank 的配置、掩码和 pending |
| control2406 | `status.c` | 有限配置及明确不可用的请求状态 |

这些名字不证明 Nikon 的 SoC 型号、物理复位值或完整硬件能力。现有协议文档中的原始地址、观察结果、假设、失败和历史运行身份全部保留。重构后的类型名和日志前缀变化不将旧 receipt 升级为新构建证明。

固定 QEMU `v11.1.2` 位于 `qemu/` 官方 Git submodule，许可证、上游 Git 历史和嵌套依赖保留。Nikon 文件属于本仓库的 `hw/nikon`、`include/hw/nikon` 与 `tests/qtest/nikon`。CMake 生成 ignored 原生构建源图和三个 Kconfig/Meson 接入点，上游工作树保持干净。根 `Makefile` 随后使用官方 configure 和 Make/Meson/Ninja 构建；`make check` 和 `make bench` 进入 Nikon 原生 C qtest 链。具体依赖和所有权见 [build.md](build.md)。没有 fork、patch、源码复制或本项目 Python 调度器。依赖结构迁移不宣称完成原版完整启动。

事件路由器模块来自固定提交 `a955007714bead0742fc6ee1f1be2602d75faf14`，当前文件名为 `router.c`。该固定阶段的完整代码与 13 个原合成夹具结果保留为历史证据，并在 Z7II machine 将其三个 SysBus IRQ 接至 GIC86/87/88。实际 native 构建仍由整合阶段执行；没有另外的 constructor 或机型兼容入口。
