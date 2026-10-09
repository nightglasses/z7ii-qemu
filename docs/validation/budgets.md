# 合成夹具的虚拟期限与实际诊断

本次只修复合成设备测试的等待预算。设备 C、原版 MAS 运行时序、5 秒宿主验收期限及 `-icount shift=0` 均未改变。CODE 为 `fb05c3f40a049e4cc9f3c0aa84237c2b7e5ea114`，修改仅在[固定版本的合成测试入口](https://github.com/mashiro01/nightglass-qemu/blob/fb05c3f40a049e4cc9f3c0aa84237c2b7e5ea114/scripts/device-smoke-test.py)，当前维护路径为 `scripts/devices.py`；这些四字节指令生成的小输入不是恢复固件。

GitHub Actions run `37840824780` 构建通过，随后停在 `timing-reset-cancels-pending: final loop not reached`。上传的该夹具 `synthetic.bin` 与修复前生成器 `2dd05337a79093039d065702a88a5b70970d1b20` 逐字节相等。上传日志没有失败时 PC，因此不能反推 Linux 的准确停点。只读回放同一输入时，原 2ms settle 期间 PC 在 `100200f4/100200f8/100200fc` 的 CNTP_CTL 轮询，最后于 3.678845 秒到 `10020118` 自环、R1=5、正常退出。该回放使用 native `578f2610ff7697e9be3c00ecaffdb88df126f975`，不是远端 publication binary；设备未出现 transaction error。证据在 `work/issue331/ci-timing-wait-probe-578/result.json`。

`icount shift=0` 的虚拟纳秒来自执行指令数；长忙等会把宿主工作量放大，宿主期限不能当作虚拟时间。本次按各设备现有 Clock/事件实现选取期限，取消/暂停仍在完成前发生，结果检查仍在被取消操作本应完成后发生。真实完成路径继续轮询设备因果产生的状态位，而不是固定睡眠后假定完成。对应上游语义见固定 [icount 文档](https://github.com/qemu/qemu/blob/4fc49f46dc95d4a27de2509e7fceb2931e91faeb/docs/devel/tcg-icount.rst)。

| 夹具族 | 当前模型期限与顺序 | 合成观察预算 |
|---|---|---|
| SDI arbitration reset | 每个延后事件 10us；pending 先 reset，completed 先真实等 done 再 reset | 已有修复采用 20us / CNTP 960 ticks；本次未再修改 |
| USIO reset pending | BGR `363`、独立 100MHz、8N1 为 86.8us；UPCL 在其前取消发送 | 100us / 4800 ticks，跨原发送 deadline |
| SPI disconnected slave | 无外部时钟，不应移出 TX 或产生 RX/done | 20us / 960 ticks |
| SPI reset pending | 明确选择 DIV=7 的 16bit 帧，48MHz 下为 5.334us；先 reset 再 reopen | 20us，跨旧帧 deadline，仍验收配置保留/FIFO 清空/no pending |
| I2C timeout disable | 已真实 ACK/HOLD 后，timeout=15 为 7.334us；随后写 0 取消 | 20us，跨旧 deadline 后检查仍 HOLD，再重新启用真实 timeout |
| I2C deadline replace | 旧 timeout=2 为 1.375us，替换 31 为 14.667us | 在 10us 检查旧 deadline 未终止，再真实轮询新 timeout |
| 十二通道 timing | 独立 3GHz，period=29999 即 10us；reset/local-disable/bitmap-pause 在期限前发生 | 20us；one-shot 检查不重启，periodic 真正等两次 expiry/ack，计数冻结/IRQ183 不省略 |
| Audio/HDMAC demand | 默认 divider 对 opaque word 的 drain 为 64/48MHz=1.334us；每 beat 仍检查 DREQ/容量，最多两队列各两 word | 20us；保留压力暂停、取消、reset、去使能、多通道与无输入样本断言 |
| 四通道 copy cancel | 513 bytes、每 chunk 至多 256 bytes，48MHz 下完整请求约 10.7us；取消先于首 chunk | 20us，仍检查目标未变化和虚拟 cancel/error 状态 |

这些期限仅来自虚拟设备实现，当前源码位于 `hw/nikon/devices/`：`usio.c` frame、`spi.c` frame schedule、`i2c.c` period/schedule、`timing.c` Clock/expiry、`audio.c` drain/`hdmac.c` beat 和 `copy.c` chunk schedule。不代表 Nikon 波特率、实机 latency、EXPEED 性能或物理音频格式。缩短 timing period 也同步把 one-shot count 断言改为 30000，未弱化结果断言。

其余 CNTP 长等待也已盘点：`secure-timer` 与原 active-init 夹具保留 48000 ticks，验证明确的 48MHz/1ms/PPI29 软件基线；`timestamp-whole-guest-init` 保留原同序 48000 ticks；`timestamp-elapsed-freeze-carry` 保留冻结、32bit 进位及 elapsed>=4800 的要求；`clock-pll-start` 保留既有非 icount 的锁定验收。它们未使用此次失败的 icount 忙等范式。其余完成器、MXIC、SPI、HDMAC、I2C、copy 成功路径以实际状态位/IRQ 为准，无额外 1ms settle。未全局调整 icount quantum，避免将低延迟 pending/cancel 顺序合并。

实际 focused 验证只选择生成指令或期望值改变的 21 个夹具，全部 PASS，1.953951 秒。native 为上述 `578f261`（该 binary 还含后续有限 NOR/ring；这些无关设备没有参与本次 fixture），fixture 为固定 `fb05c3f`，upstream 为 `4fc49f46dc95d4a27de2509e7fceb2931e91faeb`。结果 `work/issue331/wait-budget-fb05c3f/result.json` 包含逐项名称、退出码、范围及旧 CI 输入相等检查。最初资源路径准备失败发生在 fixture 构造阶段，尚未启动 QEMU；修正冻结生成器的测试资源路径后才执行该 21 项，未覆盖或改写旧收据。未重跑整套 CI 或原版 MAS，远端完整 CI 由主调度对 publication fixed head 执行。

将来的任一正常入口或 reset 后入口若在 5 秒期限内不到最终自环，薄 runner 会保存 `wait-failure.json`，包含 phase、期望 loop PC、最后一次真实寄存器和 QMP status，然后保持失败。这补足本次上传日志没有 PC 的诊断缺口，不增加延长期限、重试或 fallback。
