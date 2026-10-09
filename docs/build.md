# 原生构建和依赖

`vendor/qemu` 是官方 [qemu/qemu](https://github.com/qemu/qemu) 的 Git submodule，固定在 `4fc49f46dc95d4a27de2509e7fceb2931e91faeb`（`v11.1.2`）。官方许可证、Git 历史和嵌套 ROM submodule 都属于上游；本仓库不复制官方源码，不维护另一个 QEMU fork。

初始化 checkout 后执行：

```sh
git submodule update --init --recursive
make -j8
make check
make bench
```

需要 CMake 3.24 或更新版本，以及 QEMU 官方的 C 编译器、Make、Meson/Ninja、pkg-config、GLib、pixman 和官方构建 Python 依赖。使用默认编译器，或用 QEMU 官方 configure 参数指定交叉工具链；构建和测试没有本项目的 Python 调度器。

`hw/nikon`、`include/hw/nikon`、`tests/qtest/nikon` 是本仓库维护的普通源码。CMake 在 ignored `work/config/source` 创建原生源图：文件指向这两个 checkout，目录本身可写，Meson wraps 的下载不会修改 submodule。仅 `hw/Kconfig`、`hw/meson.build` 和 `tests/qtest/meson.build` 在这个构建目录生成，并添加 Nikon 的原生入口；没有 patch、拷贝覆盖或兼容路径。

根 Makefile 调用 CMake 配置这个源图，随后使用 QEMU 官方 configure 和 Make/Meson/Ninja。`make check` 运行 Nikon 的原生 C qtest suite，`make bench` 运行同一套原生性能 fixture。上游 executable 和本仓库设备在同一原生构建中链接，不通过 DSO 私有 ABI 或 TCG 插件注入设备。

`BUILD_DIR` 指定 QEMU 输出目录，`CONFIG_DIR` 指定 CMake 配置目录，`CONFIGURE_FLAGS` 接受官方 configure 参数。更改编译选项或增加源文件后执行 `make configure`。现有源码编辑由原生 Ninja 依赖管理。

设备改动通过本仓库正常 PR 合并。上游依赖更新独立更新 gitlink，并记录新版本；不要修改或提交 submodule 工作树。原版运行和完整 GUI 结果仍需对应实际运行记录，依赖迁移不冒充启动完成。
