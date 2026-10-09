#!/usr/bin/env python3
"""Two synthetic ARM cores exchange a word through real WFE/DSB/SEV."""

import argparse
import json
from pathlib import Path
import re
import struct
import tempfile
import time

from qemu.machine import QEMUMachine

BASE = 0x10020000


def constant(reg, value):
    low, high = value & 0xffff, value >> 16
    return [0xe3000000 | (low & 0xf000) << 4 | reg << 12 | low & 0xfff,
            0xe3400000 | (high & 0xf000) << 4 | reg << 12 | high & 0xfff]


def fixture():
    # Synthetic rendezvous flags use existing copy RAM, not invented MMIO.
    setup = constant(0, 0x04008ffc) + constant(1, 0xb1800000)
    setup += constant(2, 0xbabebabe)
    master = setup + [0xe3a03000, 0xe5803000, 0xf57ff04f, 0xe320f004,
                      0xe5914000, 0xe3540001, 0x1afffffc,
                      0xe5802000, 0xf57ff04f, 0xe320f004,
                      0xe5915004, 0xe3550001, 0x1afffffc, 0xeafffffe]
    secondary = setup + [0xe3a03001, 0xe5813000, 0xf57ff04f, 0xe320f004,
                         0xe320f002, 0xe5904000, 0xe1540002, 0x1afffffb,
                         0xe3a03000, 0xe5803000, 0xf57ff04f,
                         0xe3a03001, 0xe5813004, 0xe320f004, 0xeafffffe]
    secondary_index = 4 + len(master)
    # MRC MPIDR; AND selector; CMP zero; BNE to secondary.
    entry = [0xee107fb0, 0xe207700f, 0xe3570000,
             0x1a000000 | (secondary_index - 5)]
    words = entry + master + secondary
    return (struct.pack("<" + "I" * len(words), *words),
            BASE + (secondary_index - 1) * 4,
            BASE + (len(words) - 1) * 4)


def pc(vm, cpu_index=0):
    registers = vm.cmd("human-monitor-command", command_line="info registers",
                       cpu_index=cpu_index)
    return int(re.search(r"\bR15=([0-9a-fA-F]+)", registers)[1], 16)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("output", type=Path, help="new evidence directory")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    image, trace = output / "synthetic.bin", output / "guest.log"
    payload, master_pc, secondary_pc = fixture()
    image.write_bytes(payload)
    vm_args = ["-accel", "tcg,thread=single", "-smp", "2", "-bios", str(image),
               "-serial", "none", "-monitor", "none", "-S",
               "-D", str(trace), "-d", "guest_errors,int"]
    with tempfile.TemporaryDirectory(prefix="ng-", dir="/tmp") as sockets:
        with QEMUMachine(str(args.binary.resolve()), args=vm_args, name="smp",
                         base_temp_dir=sockets, log_dir=str(output),
                         monitor_address=str(Path(sockets) / "qmp.sock"),
                         qmp_timer=5) as vm:
            vm.set_machine("z7ii-mas-diagnostic")
            vm.launch()
            cpus = vm.cmd("query-cpus-fast")
            assert [cpu["cpu-index"] for cpu in cpus] == [0, 1], cpus
            vm.cmd("cont")
            vm.event_wait("RESUME", timeout=5)
            deadline = time.monotonic() + 5
            while pc(vm) != master_pc:
                assert vm.cmd("query-status")["running"], vm.get_log()
                if time.monotonic() >= deadline:
                    raise AssertionError("two-core guest rendezvous did not finish")
                time.sleep(0.01)
            vm.cmd("stop")
            vm.event_wait("STOP", timeout=5)
            assert pc(vm) == master_pc
            # QMP's HMP command is stateless; select the CPU in this same call.
            assert pc(vm, 1) == secondary_pc, (pc(vm, 1), secondary_pc)
            state = vm.cmd("human-monitor-command", command_line="info registers",
                           cpu_index=1)
            assert "R07=00000001" in state, state  # Genuine MPIDR selector.
            copied = output / "ram-readback.bin"
            vm.cmd("human-monitor-command",
                   command_line=f'pmemsave 0xb1800000 8 "{copied}"')
            assert copied.read_bytes() == struct.pack("<II", 1, 1)
            magic = output / "release-readback.bin"
            vm.cmd("human-monitor-command",
                   command_line=f'pmemsave 0x04008ffc 4 "{magic}"')
            assert magic.read_bytes() == bytes(4)  # Secondary cleared guest magic.
        assert vm.exitcode() == 0, vm.get_log()
        logs = (vm.get_log() or "") + trace.read_text()
        assert "transaction=error" not in logs, logs
        assert "Data Abort" not in logs, logs
        assert image.read_bytes() == payload
    result = {"scope": "synthetic two-core rendezvous, not original boot",
              "cpus": 2, "master_pc": master_pc, "secondary_pc": secondary_pc,
              "qemu_exit_code": vm.exitcode()}
    (output / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result))


if __name__ == "__main__":
    main()
