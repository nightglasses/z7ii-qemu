# 原生构建和依赖

`qemu` 是官方 [qemu/qemu](https://github.com/qemu/qemu) 的 Git submodule，固定在 `4fc49f46dc95d4a27de2509e7fceb2931e91faeb`（`v11.1.2`）。官方许可证、Git 历史和嵌套 ROM submodule 都属于上游；本仓库不复制官方源码，不维护另一个 QEMU fork。

它直接位于根 `qemu/`，不增加中间依赖目录。模拟器需要 QEMU 的 CPU/TCG、QOM、总线、IRQ、计时器与原生 executable 实现参与链接，因此依赖包括官方源码，不能只保留头文件。本项目源码只在根 `hw/nikon`、`include/hw/nikon` 和 `tests/*/nikon`，不得写进官方树。CMake 在生成源图之前检查官方 checkout 的 tracked 内容干净，并拒绝在依赖内发现自有 Nikon 目录；失败直接中止，不修改依赖或回退旧路径。

初始化 checkout 后执行：

```sh
git submodule update --init --recursive
make -j8
make check
make bench
```

需要 CMake 3.24 或更新版本，以及 QEMU 官方的 C 编译器、Make、Meson/Ninja、pkg-config、GLib、pixman、Cairo 1.16+ 和官方构建 Python 依赖。Nikon Z7II 的有限路径模块通过 Meson 显式链接 Cairo，只用 image/A8 coverage，不调用宿主字体；本次使用已安装的 Cairo 1.18.6，无依赖安装。使用默认编译器，或用 QEMU 官方 configure 参数指定交叉工具链；构建和测试没有本项目的 Python 调度器。

`hw/nikon`、`include/hw/nikon`、`tests/qtest/nikon`、`tests/unit/nikon` 是本仓库维护的普通源码。CMake 在 ignored `work/config/source` 创建原生源图：文件指向这两个 checkout，目录本身可写，Meson wraps 的下载不会修改 submodule。`hw/Kconfig`、`hw/meson.build`、`tests/qtest/meson.build`、`tests/unit/meson.build` 添加 Nikon 的原生入口；没有 patch、拷贝覆盖或兼容路径。

原版 VR 使用 RXv2 的 EXTB 控制寄存器，固定官方 RX 目标尚无此能力。全部 CPU 增量集中在 `cmake/rx.cmake`：生成 `target/rx/cpu.c`、`cpu.h`、`helper.c`、`translate.c`，为原生 RX 类型添加显式 `has-extb` profile、复位值、异常向量基址和原生 TCG 控制寄存器读写。生成内容与已实际编译的 `4751c8b` CPU 文件逐字节相同；源接缝必须在固定上游唯一匹配，否则配置直接失败。官方 submodule 保持干净，CPU 源单元由其正常 RX Meson 图编译，未另建 CPU 或通过外部 object 覆盖符号。

根 Makefile 调用 CMake 配置这个源图，随后使用 QEMU 官方 configure 和 Make/Meson/Ninja。`make check` 运行 Nikon 的原生 C qtest suite，`make bench` 运行同一套原生性能 fixture。上游 executable 和本仓库设备在同一原生构建中链接，不通过 DSO 私有 ABI 或 TCG 插件注入设备。

`BUILD_DIR` 指定 QEMU 输出目录，`CONFIG_DIR` 指定 CMake 配置目录，`CONFIGURE_FLAGS` 接受官方 configure 参数。更改编译选项或增加源文件后执行 `make configure`。现有源码编辑由原生 Ninja 依赖管理。

使用上游已有的 `MTESTARGS` 选择新测试，不另建测试运行器。完整首次构建仍执行 `make configure`、`make -j8`；新增 USIO 和 VR 测试可执行：

```sh
make check MTESTARGS='nikon_usio nikon_vr'
make check MTESTARGS='nikon --test-args="-p /arm/nikon/transfer/planes"'
```

`MTESTARGS` 由递归 Make 原样传给 QEMU 官方生成的测试目标；无参数的 `make check` 执行完整 Nikon suite。首次迁移 source-view 工程的实际构建由唯一 builder 完成，源码一致不冒充已完成新构建。

设备改动通过本仓库正常 PR 合并。上游依赖更新独立更新 gitlink，并记录新版本；不要修改或提交 submodule 工作树。当前迁移完整承接 `f44817e` 已编译 ARM/RX、loader、VR 精确 storage/offset alias、generic access、USIO 和 paired transfer planes，以及 26 个新增 case 的实际 PASS（之前 20 + USIO 4 + planes 1 + RX 1）。后续 `528446c` 文档收据同时同步。原版运行和完整 GUI 结果仍需对应实际运行记录，依赖迁移不冒充启动完成。
