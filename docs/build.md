# Native build and dependencies

## Handbook site

The handbook uses the existing Markdown tree as its single content source.
From the repository root, run `pnpm install --frozen-lockfile`, then
`pnpm docs:dev` for local reading or `pnpm docs:build` for validation.
`pnpm docs:preview` serves the built site. Generated pages and caches stay
under ignored `work/handbook/`. Local page links and Mermaid/Pele diagrams
must build successfully; a successful site build does not validate firmware
execution or replace the native checks below.

The [documentation standards](writing.md) define the review requirements.
`docs:build` runs the Node documentation-rule tests before VitePress. The same
build rejects missing navigation entries, orphan evidence-page entries, missing
or repeated attachments, forbidden article presentation and broken page targets.
Review changed anchors and rendered diagrams separately; these checks do not
validate decompilation claims or firmware behavior.

The site's palette adapts Microsoft's
[VS Code Dark 2026 theme](https://github.com/microsoft/vscode/blob/86db3b69224f75a786f815684bc96fc0eb5d9300/extensions/theme-defaults/themes/2026-dark.json).
Its Microsoft copyright and [MIT license](/theme-license.txt) are retained in
`docs/.vitepress/theme/LICENSE.txt`. Published evidence is tracked in
`docs/public/evidence/`; the [archive](validation/evidence.md) explains the
curated fields, native output excerpts and unavailable historical captures.

## Native emulator

`qemu` is an official Git submodule of [qemu/qemu](https://github.com/qemu/qemu), pinned to `4fc49f46dc95d4a27de2509e7fceb2931e91faeb` (`v11.1.2`). The official license, Git history, and nested ROM submodules belong upstream; this repository neither duplicates official sources nor maintains a separate QEMU fork.

The submodule resides directly at the root `qemu/` without intermediate dependency directories. The emulator requires QEMU's CPU/TCG, QOM, SysBus, IRQ, timers, and native executable implementations during linking; therefore, dependencies include official sources rather than mere header files. Project source code belongs exclusively to root `hw/nikon`, `include/hw/nikon`, `tests/*/nikon`, and `tools/nikon`, and must not be written into the upstream tree. Before generating the native source graph, CMake verifies that the tracked contents of the official checkout remain clean, and rejects any custom Nikon directory inside the dependency. Build configuration aborts immediately upon violation, without altering dependencies or falling back to legacy paths.

After initializing the checkout, execute:

```sh
git submodule update --init --recursive
make -j8
make check
make bench
```

Prerequisites include CMake 3.24 or newer, the official C compiler, Make, Meson/Ninja, pkg-config, GLib, pixman, Cairo 1.16+, and official Python build dependencies. Finite paths for the Nikon Z7II explicitly link Cairo via Meson, consuming only image/A8 coverage without invoking host font systems; Cairo 1.18.6 is verified without extra dependency installations. Use the default compiler or pass cross-toolchain parameters to QEMU's official configure script; build and test flows do not employ any custom Python runner.

`hw/nikon`, `include/hw/nikon`, `tests/qtest/nikon`, `tests/unit/nikon`, and `tools/nikon` are ordinary source trees maintained in this repository. CMake constructs a native source graph in ignored `work/config/source`: ordinary files link to both checkouts, the directory itself is writable, and Meson wrap downloads do not mutate the submodule. `hw/Kconfig`, `hw/meson.build`, `tests/qtest/meson.build`, and `tests/unit/meson.build` add native entry points for Nikon. The bounded source adaptations in `cmake/rx.cmake` and `cmake/qtest.cmake` generate their declared files in that graph; each replacement must match one exact pinned-source seam. Generated source and diagnostic copies are not tracked or written through upstream symlinks.

The original VR component utilizes the EXTB control register of RXv2, which the pinned upstream RX target lacks. All CPU increments are consolidated in `cmake/rx.cmake`: generating `target/rx/cpu.c`, `cpu.h`, `helper.c`, and `translate.c`, adding explicit `has-extb` profile flags, reset values, exception vector base addresses, and native TCG control register access to native RX types. Generated contents match compiled `4751c8b` CPU files byte-for-byte; source seams must uniquely match the pinned upstream checkout, or configuration fails immediately. The official submodule remains clean, and CPU source units compile through its standard RX Meson graph without creating separate CPUs or overriding symbols via external objects.

The root Makefile invokes CMake to configure this source graph, followed by QEMU's official configure, Make, Meson, and Ninja. `make check` executes Nikon's native C qtest suite, and `make bench` executes the identical native performance fixtures. Upstream executables and repository devices link within the same native build, without injecting devices via private DSO ABIs or TCG plugins.

`BUILD_DIR` designates the QEMU output directory, `CONFIG_DIR` designates the CMake configuration directory, and `CONFIGURE_FLAGS` accepts official configure arguments. Execute `make configure` after modifying compilation flags or introducing source files. Existing source edits are managed by native Ninja dependencies.

Select new tests using upstream's existing `MTESTARGS` rather than inventing an ad-hoc test runner. Full initial builds still invoke `make configure` and `make -j8`; newly added USIO and VR tests can be executed via:

```sh
make check MTESTARGS='nikon_usio nikon_vr'
make check MTESTARGS='nikon_battery_unit'
make check MTESTARGS='nikon --test-args="-p /arm/nikon/transfer/planes"'
```

`MTESTARGS` is passed verbatim by recursive Make to QEMU's officially generated test targets; invoking `make check` without parameters runs the entire Nikon test suite. The physical build of the initial source-view migration is executed by a single builder; matching source code does not substitute for a completed native build.

### Darwin qtest wake ownership

Darwin qtest uses one `QemuSemaphore` per DUMMY vCPU to preserve the
destination of a host wake. In the pinned dependency, DUMMY threads wait for
the same `SIGUSR1` signal. On a macOS 27.0 (26A5378j) host, an isolated
`pthread_kill`/`sigwait` control sent `SIGUSR1` to a thread which never waited;
a different thread then received it from `sigwait` before receiving any signal
of its own. This directly establishes cross-thread consumption on that host.
Apple's published [XNU signal implementation](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/bsd/kern/kern_sig.c#L1242-L1291)
provides a matching mechanism: `sigwait` searches pending sets across the
process's threads and clears the selected thread's pending bit. That public
XNU revision is source context, not a claim that it identifies the running
macOS 27 kernel binary.

The original five-CPU bundle fixture repeatedly hung during ordinary EX
sampling and during process cleanup. Sampling waited in
`ex_exception_poll` → `run_on_cpu` while DUMMY threads slept in `sigwait`.
A cleanup snapshot found a CPU with `stop=1`, `stopped=0`,
`thread_kicked=1`, `exit_request=1` and an empty work queue; subsequent kicks
were coalesced. The affected CPU varied. Together with the host control,
these observations explain how a wake consumed by another thread leaves a
target's coalescing flag set and suppresses its next notification.

`cmake/qtest.cmake` generates four source files from the pinned checkout:
`include/hw/core/cpu.h`, `accel/dummy-cpus.h`, `accel/dummy-cpus.c` and
`accel/qtest/qtest.c`. On Darwin, the existing qtest accelerator hooks
initialize each semaphore during CPU realization, before thread creation,
and post that CPU's semaphore from its kick hook. DUMMY consumes the token
through QEMU's semaphore API. The existing `halt_cond` broadcast, CPU work
queue, `thread_kicked` clearing, stop acknowledgement and virtual-clock
handling retain their normal roles. Counting tokens cannot be consumed by
another CPU's wait. TCG and the generic POSIX kick implementation retain
their existing paths; this adaptation does not alter guest events or IRQs.

Unrealization releases the semaphore after synchronous thread removal.
If a target already called `cpu_remove_sync`, its `unplug` flag records that
path; otherwise the qtest hook joins a created thread through that same
upstream API. In the pinned source, `cpu_remove_sync` is the sole producer
of `unplug=true`. Pause, resume and reset preserve the semaphore. Windows
keeps its existing implementation.

The upstream author's [unplug proposal](https://gitlab.com/farosas/qemu/-/commit/0ab6cea4d0e3f61caa51af15496aa0c89aad5df5)
moves CPU event processing within the DUMMY loop. An isolated A/B trial of
that proposal still hung in the Nikon fixture, so it is not included here.
The semaphore adaptation is a local host-runtime correction, not a claimed
fix from an official QEMU release.

The native `/arm/nikon/cmos/pipeline/cpu-lifecycle` regression creates the
synthetic five-CPU bundle, advances repeated EX millisecond polls, checks
normal QMP stop/resume, resets, and requires libqtest's normal process exit.
It does not disable timers, inject CPU work, force kicks, or accept a
SIGKILL exit. Existing paired boundary fixtures remain separate device
validation. Host-runtime fixture results do not establish original-firmware
startup, normal GUI output, or physical-camera behavior.

Host tools under `tools/nikon` compile through the same Meson graph and reuse `qemuutil`; their entry points register after the existing `pyvenv` subdirectory, preserving project tools even when upstream `--disable-tools` is specified. The default battery tool binary path is `work/build/qemu/tools/nikon/nikon-battery`; its interface and acceptance boundaries are documented in [batterytool.md](runtime/batterytool.md). Tools are not placed in the official submodule, and do not introduce separate compilation or test scripts.

Device changes merge via normal PRs in this repository. Upstream dependency updates independently advance gitlinks with recorded release versions; do not modify or commit within the submodule worktree. The current migration fully incorporates compiled ARM/RX, loader, VR precise storage/offset alias, generic access, USIO, paired transfer planes from `f44817e`, along with 26 newly added PASS receipts (20 prior + 4 USIO + 1 planes + 1 RX). Subsequent `528446c` documentation receipts are synchronized concurrently. Original execution and complete GUI results continue to require corresponding runtime records; dependency migration does not assert completed boot.
