# CI 范围

- 使用固定上游 QEMU、官方 configure/Ninja 和 qemu.machine.QEMUMachine；不新增 QMP 客户端或模拟器控制框架。
- CI 只生成并运行小型合成 ARM 夹具，不下载原厂固件，不调用 AI，不添加自设摘要计算或门禁。
- 检查 machine 注册、初始化、reset 与真实继续/停止；合成通过不代表原版 MAS 或完整机身启动通过。
- 构建/测试输出放 ignored work/；socket 使用短临时路径。保留失败日志，不覆盖本地原版运行记录。
