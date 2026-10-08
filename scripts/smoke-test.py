#!/usr/bin/env python3
"""Synthetic ARM B-to-self smoke, using the pinned upstream QEMUMachine."""

import argparse
import json
from pathlib import Path
import re
import tempfile
import time

from qemu.machine import QEMUMachine


def registers(vm):
    text = vm.cmd("human-monitor-command", command_line="info registers")
    pc = re.search(r"\bR15=([0-9a-fA-F]+)", text)
    psr = re.search(r"\bPSR=([0-9a-fA-F]+)", text)
    assert pc and psr, f"ARM registers missing from HMP response: {text}"
    return {"pc": int(pc[1], 16), "cpsr": int(psr[1], 16)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("output", type=Path, help="new output directory")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    image = output / "arm-self-loop.bin"
    # ARM little-endian B .; synthetic test bytes, not recovered firmware.
    image.write_bytes(bytes.fromhex("fe ff ff ea"))
    trace = output / "guest.log"
    vm_args = ["-accel", "tcg,thread=single", "-bios", str(image),
               "-serial", "none", "-monitor", "none", "-S",
               "-D", str(trace), "-d", "in_asm,guest_errors,int"]

    # Only the transient socket lives in /tmp; all test evidence stays in work/.
    with tempfile.TemporaryDirectory(prefix="ng-", dir="/tmp") as sockets:
        with QEMUMachine(str(args.binary.resolve()), args=vm_args,
                         name="smoke", base_temp_dir=sockets,
                         monitor_address=str(Path(sockets) / "qmp.sock"),
                         log_dir=str(output), qmp_timer=5) as vm:
            vm.set_machine("z7ii-mas-diagnostic")
            vm.launch()
            initial_status = vm.cmd("query-status")
            assert initial_status["status"] == "prelaunch", initial_status
            initial = registers(vm)
            assert initial["pc"] == 0x10020000, initial
            assert initial["cpsr"] & 0x20 == 0, initial  # ARM, not Thumb.
            assert initial["cpsr"] & 0x1f == 0x13, initial  # SVC reset mode.
            vm.cmd("cont")
            vm.event_wait("RESUME", timeout=5)
            running = vm.cmd("query-status")
            assert running["running"] is True, running
            # Wait for the one guest block, not an arbitrary host sleep. The
            # upstream log unlock flushes this trace (util/log.c).
            deadline = time.monotonic() + 5
            while "0x10020000:" not in trace.read_text():
                if time.monotonic() >= deadline:
                    raise AssertionError("synthetic entry was not translated")
                time.sleep(0.01)
            vm.cmd("stop")
            vm.event_wait("STOP", timeout=5)
            stopped = vm.cmd("query-status")
            assert stopped["status"] == "paused", stopped
            assert stopped["running"] is False, stopped
            final = registers(vm)
            assert final == initial, {"initial": initial, "final": final}
        assert vm.exitcode() == 0, vm.get_log()
        logs = (vm.get_log() or "") + trace.read_text()
        assert "uncovered access" not in logs, logs
        assert "transaction=error" not in logs, logs
        assert "Data Abort" not in logs, logs
        assert image.read_bytes() == bytes.fromhex("fe ff ff ea")

    result = {"scope": "synthetic ARM self-loop only; not firmware boot",
              "initial": initial, "running": running, "stopped": stopped,
              "final": final, "qemu_exit_code": vm.exitcode()}
    (output / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result))


if __name__ == "__main__":
    main()
