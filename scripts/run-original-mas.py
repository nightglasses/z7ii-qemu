#!/usr/bin/env python3
"""Bounded unchanged-MAS observation using upstream QEMUMachine and QMP."""

import argparse
import json
import logging
from pathlib import Path
import subprocess
import tempfile
import time

from qemu.machine import QEMUMachine


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("payload", type=Path)
    parser.add_argument("output", type=Path, help="new evidence directory")
    parser.add_argument("--machine-revision", required=True, help="normal Git ref")
    parser.add_argument("--tcg-thread", choices=("single", "multi"), default="single",
                        help="upstream TCG thread mode, recorded in the receipt")
    parser.add_argument("--trace-kernel", action="store_true",
                        help="upstream CPU/instruction trace for MAS kernel prefix only")
    parser.add_argument("--trace-irqs", action="store_true",
                        help="upstream GIC enable, input, acknowledge and control trace")
    parser.add_argument("--trace-pkgb", action="store_true",
                        help="upstream trace of PKGB constructor and SPI init candidates")
    parser.add_argument("--seconds", type=float, default=5,
                        help="bounded host observation interval")
    args = parser.parse_args()
    if not 0 < args.seconds <= 60:
        parser.error("--seconds must be in (0, 60]")
    binary, payload, output = (p.resolve() for p in
                               (args.binary, args.payload, args.output))
    before = payload.read_bytes()
    output.mkdir(parents=True, exist_ok=False)
    logging.basicConfig(filename=output / "harness.log", level=logging.DEBUG)
    vm_args = ["-accel", f"tcg,thread={args.tcg_thread}", "-bios", str(payload),
               "-serial", "none", "-monitor", "none", "-S",
               "-D", str(output / "qemu.log"), "-d", "guest_errors,int"]
    if args.trace_kernel or args.trace_pkgb:
        vm_args[-1] = "guest_errors,int,in_asm,cpu,exec"
        filters = ["0x10100000+0x20000"] if args.trace_kernel else []
        if args.trace_pkgb:
            filters += ["0x10200c8c+0x94", "0x10200684+0x2e",
                        "0x10201010+0x40", "0x10242fda+0x68"]
        vm_args += ["-dfilter", ",".join(filters)]
    if args.trace_irqs:
        for event in ("gic_enable_irq", "gic_set_irq", "gic_acknowledge_irq",
                      "gic_cpu_write", "gic_dist_write"):
            vm_args += ["-trace", f"enable={event}"]
    receipt = {
        "machine_revision": args.machine_revision,
        "upstream_revision": "4fc49f46dc95d4a27de2509e7fceb2931e91faeb",
        "binary": str(binary), "machine": "z7ii-mas-diagnostic",
        "guest_arguments": vm_args,
        "full_command_record": "harness.log (upstream launch log)",
        "payload": str(payload), "payload_bytes": len(before),
        "entry_bytes": before[:4].hex(" "),
        "o178_bytes": before[0x178:0x17c].hex(" "),
        "assumptions": [
            "two upstream Cortex-A7; CBAR=0xf8100000; CNTFRQ=48MHz",
        "upstream whole GICv1/TZ at original CBAR offsets; 256 SGI/PPI/SPI IDs; CPU EL2 unchanged, no GIC virtualization interface",
            "candidate MAS VA placed at diagnostic PA 0x10020000",
            "upstream CPU reset state; guest copies all copy destinations",
            "six modes per guest core, virtual 16KiB stack capacity per mode; early temporary RAM exactly88B",
            "early shared release RAM exactly four bytes; no host magic",
            "same-IP MEMC binding with only documented functional profiles",
            "virtual PLL reset words 0xffe/0x73/3; six gate banks start ungated",
            "two runtime system-control RMW request banks+58/+5c reset zero; physical roles unbound, no inferred rate/ready",
            "PLL outputs nominal 48MHz after modeled 10us virtual lock; not physical rates",
            "independent virtual timestamp Clock 48MHz; reset count zero, control3 enabled; guest config at +10",
            "SDI0/1 asserted resets; applied config profiles; 10us virtual completion and successful training backend",
            "completion request/done/enable/domain state reset zero; causal deferred transactions, GIC SPI250",
            "original lowmem allocator arena [b2c00000,b5f00000), 51MiB virtual RAM, no host metadata",
            "original kernel copy destination [bab00000,bab21538), finite virtual RAM; guest performs copy and MMU changes",
            "virtual byte-block zero-fill direct count; finite BSS [b18aedc0,b2b5edc0), real DMA bus result",
        "virtual initial region decode from guest boot profile; byte/64KiB ranges constrain existing DDR backings only",
        "polling USIO channel0; nominal independent 100MHz, one-word TX, 8N1 virtual timing; no invented RX/IRQ",
        "finite second system configuration; virtual strap/control reset zero, guest five-bank bootstrap and bounded four-bank field RMW",
        "twelve GPIO ports with masked latch/direction/function/pull; unconnected pads follow pulls or virtual low, no key matrix",
        "MXIC active/draft config and 512 slots reset zero; idle available, TAEN-gated complete transaction, 10us virtual apply; independent live parameters",
        "eleven SPI channels: virtual 32-word FIFOs, asserted reset preserving config on channel reset, independent 48MHz input; master SSI disconnected MISO-low, no external slave clock; common pending IRQ routes first original ISR line only",
        ],
        "whole_boot_verified": False,
    }
    start = time.monotonic()
    vm = None
    try:
        receipt["qemu_version"] = subprocess.check_output(
            [str(binary), "--version"], text=True)
        # Keep socket below the macOS sun_path limit even in deep worktrees.
        with tempfile.TemporaryDirectory(prefix="ng-", dir="/tmp") as sockets:
            with QEMUMachine(str(binary), args=vm_args, name="original",
                             base_temp_dir=sockets, log_dir=str(output),
                             monitor_address=str(Path(sockets) / "qmp.sock"),
                             qmp_timer=5) as vm:
                vm.set_machine("z7ii-mas-diagnostic")
                vm.launch()
                receipt["initial_status"] = vm.cmd("query-status")
                receipt["initial_cpus"] = vm.cmd("query-cpus-fast")
                receipt["initial_registers"] = vm.cmd(
                    "human-monitor-command", command_line="info registers")
                receipt["initial_registers_by_cpu"] = {
                    str(cpu["cpu-index"]): vm.cmd(
                        "human-monitor-command", command_line="info registers",
                        cpu_index=cpu["cpu-index"])
                    for cpu in receipt["initial_cpus"]}
                vm.cmd("cont")
                vm.event_wait("RESUME", timeout=5)
                deadline = time.monotonic() + args.seconds
                while True:
                    status = vm.cmd("query-status")
                    if not status["running"]:
                        receipt["stop_reason"] = status["status"]
                        break
                    if time.monotonic() >= deadline:
                        vm.cmd("stop")
                        vm.event_wait("STOP", timeout=5)
                        receipt["stop_reason"] = "bounded_observation_timeout"
                        break
                    time.sleep(0.05)
                receipt["final_status"] = vm.cmd("query-status")
                receipt["final_cpus"] = vm.cmd("query-cpus-fast")
                receipt["final_cpu_summary"] = vm.cmd(
                    "human-monitor-command", command_line="info cpus")
                receipt["final_registers"] = vm.cmd(
                    "human-monitor-command", command_line="info registers")
                receipt["final_registers_by_cpu"] = {
                    str(cpu["cpu-index"]): vm.cmd(
                        "human-monitor-command", command_line="info registers",
                        cpu_index=cpu["cpu-index"])
                    for cpu in receipt["final_cpus"]}
    except Exception as exc:
        receipt["error"] = repr(exc)
    finally:
        receipt["exit_code"] = vm.exitcode() if vm else None
        receipt["wall_seconds"] = time.monotonic() - start
        receipt["payload_direct_content_unchanged"] = payload.read_bytes() == before
        receipt["qemu_output"] = vm.get_log() if vm else None
        (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
        print(json.dumps(receipt))
    if ("error" in receipt or receipt["exit_code"] != 0 or
            not receipt["payload_direct_content_unchanged"]):
        raise SystemExit(2)


if __name__ == "__main__":
    main()
