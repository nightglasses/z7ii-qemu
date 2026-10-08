#!/usr/bin/env python3
"""Small synthetic ARM contracts; no camera firmware or replacement runner."""

import argparse
import json
from pathlib import Path
import re
import struct
import tempfile
import time

from qemu.machine import QEMUMachine

BASE = 0x10020000
MEMC = 0x04264000
TIM = 0x153b0902
CLOCK = 0x04281000
COMPLETION = 0x04287000


def constant(reg, value):
    """Two fixed ARM MOVW/MOVT words, used only to construct test bytes."""
    low, high = value & 0xffff, value >> 16
    return [0xe3000000 | (low & 0xf000) << 4 | reg << 12 | low & 0xfff,
            0xe3400000 | (high & 0xf000) << 4 | reg << 12 | high & 0xfff]


def initialized(mode=0x21):
    return (constant(0, MEMC) + constant(1, mode) + [0xe5801000] +
            constant(1, TIM) + [0xe5801020])


def fixtures():
    # MODE read/modify/write; TIM readback; both ends of exactly 48 bytes RAM.
    profile = initialized() + [0xe5904000, 0xe5905020, 0xe3841008,
                               0xe5801000, 0xe5906000, 0xe3c11008,
                               0xe5801000, 0xe5907000]
    profile += constant(0, 0x04008ed0) + constant(1, 0x13579bdf)
    profile += [0xe5801000, 0xe580102c, 0xe5908000, 0xe590902c]

    # Enable secure PPI 29, let the upstream timer expire with CPU IRQ masked,
    # acknowledge it, disable the level, EOI, and inspect active state.
    timer = constant(2, 0x1d001000) + constant(1, 1) + [0xe5821000]
    timer += constant(1, 1 << 29) + [0xe5821100]
    timer += constant(0, 0x1d002000) + constant(1, 0xff) + [0xe5801004]
    timer += constant(1, 1) + [0xe5801000]
    timer += constant(1, 48000) + [0xee0e1f12]  # CNTP_TVAL
    timer += constant(1, 1) + [0xee0e1f32]  # CNTP_CTL.ENABLE
    timer += [0xee1e1f32, 0xe3110004, 0x0afffffc]  # Wait for ISTATUS.
    timer += [0xe590800c, 0xe5929300]  # IAR; GICD_ISACTIVER0
    timer += constant(1, 0) + [0xee0e1f32, 0xe5808010, 0xe592a300]
    timer += [0xee1ebf10, 0xee9fcf10]  # CNTFRQ; CBAR

    # All six gate banks: CLEAR determines every bit, SET and CLEAR aliases
    # then modify that same word. No reset value is supplied by the device.
    gates, gate_values = [], {}
    for bank in range(6):
        pattern = 0x13579bdf ^ (bank << 8)
        gates += constant(0, CLOCK + 0x38 + bank * 4)
        gates += constant(1, 0xffffffff) + [0xe5801200]
        gates += constant(1, pattern) + [0xe5801100]
        gates += [0xe5900000 | (bank + 2) << 12]
        gate_values[bank + 2] = pattern
    for bank in range(6):
        pattern = 0x13579bdf ^ (bank << 8)
        gates += constant(0, CLOCK + 0x38 + bank * 4)
        gates += constant(1, 0x0f0f0f0f) + [0xe5801200]
        gates += [0xe5900000 | (bank + 8) << 12]
        gate_values[bank + 8] = pattern & ~0x0f0f0f0f

    # RAM survives system_reset; the second entry therefore reads a gate word
    # before writing it, proving known bits from the first entry were cleared.
    gate_reset = constant(0, 0xb1800000) + [0xe5901000, 0xe3510001,
                                         0x0a000007, 0xe3a01001, 0xe5801000]
    gate_reset += constant(0, CLOCK) + constant(1, 0xffffffff)
    gate_reset += [0xe5801148, 0xea000002]
    gate_reset += constant(0, CLOCK) + [0xe5902048]

    # IRQ250 is a real GIC SPI: enable, target CPU0, submit, poll done, IAR,
    # ack at the device, and EOI. CPU IRQ is masked, as in the timer fixture.
    completion = constant(2, 0x1d001000) + constant(1, 1) + [0xe5821000]
    completion += [0xe5c218fa]  # GICD_ITARGETSR byte for IRQ250 = CPU0.
    completion += constant(1, 1 << 26) + [0xe582111c]
    completion += constant(3, 0x1d002000) + constant(1, 0xff) + [0xe5831004]
    completion += constant(1, 1) + [0xe5831000]
    completion += constant(0, COMPLETION) + [0xe5801008]
    completion += constant(1, 0x1110) + [0xe5801000, 0xe5906000]
    completion += constant(1, 0xa1) + [0xe5801010]
    completion += [0xe5904004, 0xe3140001, 0x0afffffc]
    completion += [0xe592721c, 0xe593800c, 0xe592931c]
    completion += constant(1, 1) + [0xe5801004, 0xe5905004, 0xe5838010]
    completion += [0xe592a31c, 0xe593b00c]
    completion += constant(1, 0) + [0xe5801008]  # Disable notification.
    completion += constant(1, 0x1010) + [0xe5801000]
    completion += constant(1, 0xa1) + [0xe5801010]
    completion += [0xe590c004, 0xe31c0001, 0x0afffffc]
    completion += constant(1, 1) + [0xe5801004, 0xe590d004]

    polling = constant(0, COMPLETION) + constant(1, 0) + [0xe5801008]
    polling += constant(1, 0x3100) + [0xe5801000]
    polling += constant(1, 0xa1) + [0xe5801010]
    polling += [0xe5904004, 0xe3140001, 0x0afffffc]
    polling += constant(2, 0x1d001000) + [0xe592721c]
    polling += constant(1, 1) + [0xe5801004, 0xe5905004]

    completion_reset = constant(0, 0xb1800000)
    completion_reset += [0xe5901000, 0xe3510001, 0x0a000007,
                         0xe3a01001, 0xe5801000]
    completion_reset += constant(0, COMPLETION)
    completion_reset += [0xe3a01000, 0xe5801000, 0xe5801008, 0xea000002]
    completion_reset += constant(0, COMPLETION) + [0xe5902008]

    return [
        ("profile-stack", profile,
         {4: 0x21, 5: TIM, 6: 0x29, 7: 0x21,
          8: 0x13579bdf, 9: 0x13579bdf}, None),
        ("secure-timer", timer,
         {8: 29, 9: 1 << 29, 10: 0, 11: 48000000, 12: 0x1d000000}, None),
        ("clock-six-banks", gates, gate_values, None),
        ("clock-reset", gate_reset, {}, None),
        ("clock-unknown-reset", constant(0, CLOCK) + [0xe5901048], {},
         "clock-stop-bits-uninitialized"),
        ("clock-partial-mask", constant(0, CLOCK) + constant(1, 1) +
         [0xe5801148, 0xe5901048], {}, "clock-stop-bits-uninitialized"),
        ("clock-zero-mask", constant(0, CLOCK) + constant(1, 0) +
         [0xe5801248, 0xe5901048], {}, "clock-stop-bits-uninitialized"),
        ("clock-alias-read", constant(0, CLOCK) + [0xe5901138], {},
         "clock-access-not-supported"),
        ("clock-direct-write", constant(0, CLOCK) + [0xe5801038], {},
         "clock-access-not-supported"),
        ("clock-byte-write", constant(0, CLOCK) + [0xe5c01138], {},
         "clock-access-not-supported"),
        ("clock-bank-boundary", constant(0, CLOCK) + [0xe5801150], {},
         "clock-access-not-supported"),
        ("clock-pll-unknown", constant(0, CLOCK) + [0xe590102c], {},
         "clock-access-not-supported"),
        ("completion-irq", completion,
         {4: 1, 5: 0, 6: 0x1110, 7: 1 << 26, 8: 250,
          9: 1 << 26, 10: 0, 11: 1023, 12: 1, 13: 0}, None),
        ("completion-poll", polling, {4: 1, 5: 0, 7: 0}, None),
        ("completion-reset", completion_reset, {}, None),
        ("completion-done-unknown", constant(0, COMPLETION) + [0xe5901004],
         {}, "completion-uninitialized-read"),
        ("completion-request-unknown", constant(0, COMPLETION) + [0xe5901000],
         {}, "completion-uninitialized-read"),
        ("completion-enable-unknown", constant(0, COMPLETION) + [0xe5901008],
         {}, "completion-uninitialized-read"),
        ("completion-unknown-submit", constant(0, COMPLETION) +
         constant(1, 0xa1) + [0xe5801010], {}, "completion-submit-state-unknown"),
        ("completion-unknown-domain", polling + [0xe590600c], {},
         "completion-domain-status-not-supported"),
        ("completion-bad-command", constant(0, COMPLETION) +
         constant(1, 0xa0) + [0xe5801010], {}, "completion-command-not-supported"),
        ("completion-bad-domain", constant(0, COMPLETION) +
         constant(1, 0x2100) + [0xe5801000], {},
         "completion-request-profile-not-supported"),
        ("completion-bad-request-bits", constant(0, COMPLETION) +
         constant(1, 2) + [0xe5801000], {},
         "completion-request-profile-not-supported"),
        ("completion-bad-enable", constant(0, COMPLETION) +
         constant(1, 2) + [0xe5801008], {}, "completion-irq-profile-not-supported"),
        ("completion-unknown-ack", constant(0, COMPLETION) +
         constant(1, 1) + [0xe5801004], {}, "completion-ack-not-supported"),
        ("completion-byte-write", constant(0, COMPLETION) + [0xe5c01000], {},
         "completion-access-not-supported"),
        ("completion-command-read", constant(0, COMPLETION) + [0xe5901010], {},
         "completion-access-not-supported"),
        ("mode-uninitialized", constant(0, MEMC) + [0xe5901000], {},
         "memc-uninitialized-read"),
        ("tim-uninitialized", constant(0, MEMC) + [0xe5901020], {},
         "memc-uninitialized-read"),
        ("bad-width", constant(0, MEMC) + constant(1, 0x21) + [0xe5c01000],
         {}, "memc-unsupported-access"),
        ("bad-offset", constant(0, MEMC) + [0xe5901004], {},
         "memc-unsupported-access"),
        ("unsupported-mode", constant(0, MEMC) + constant(1, 0x20) +
         [0xe5801000], {}, "memc-mode-profile-not-supported"),
        ("unsupported-tim", initialized() + constant(1, TIM + 1) +
         [0xe5801020], {}, "memc-timing-profile-not-supported"),
        ("nor-before-init", constant(0, BASE) + [0xe1c010b0], {},
         "nor-memc-not-initialized"),
        ("nor-protected", initialized(0x29) + constant(0, BASE) +
         [0xe1c010b0], {}, "nor-write-protected"),
        ("nor-no-backend", initialized() + constant(0, BASE) +
         [0xe1c010b0], {}, "nor-backend-not-supported"),
        ("stack-below", constant(0, 0x04008ecc) + [0xe5801000], {},
         "uncovered-address"),
        ("stack-above", constant(0, 0x04008f00) + [0xe5801000], {},
         "uncovered-address"),
        ("release-below", constant(0, 0x04008ff8) + [0xe5801000], {},
         "uncovered-address"),
        ("release-above", constant(0, 0x04009000) + [0xe5801000], {},
         "uncovered-address"),
    ]


def registers(vm):
    response = vm.cmd("human-monitor-command", command_line="info registers")
    return {int(reg): int(value, 16) for reg, value in
            re.findall(r"\bR(\d+)=([0-9a-fA-F]+)", response)}


def run(binary, output, name, words, expected, reason):
    output.mkdir()
    image, trace = output / "synthetic.bin", output / "guest.log"
    loop_pc = BASE + len(words) * 4
    payload = struct.pack("<" + "I" * (len(words) + 1), *words, 0xeafffffe)
    image.write_bytes(payload)
    vm_args = ["-accel", "tcg,thread=single", "-smp", "1", "-bios", str(image),
               "-display", "none", "-serial", "none", "-monitor", "none",
               "-S", "-D", str(trace), "-d", "guest_errors,int"]
    with tempfile.TemporaryDirectory(prefix="ng-", dir="/tmp") as sockets:
        with QEMUMachine(str(binary), args=vm_args, name="device",
                         base_temp_dir=sockets, log_dir=str(output),
                         monitor_address=str(Path(sockets) / "qmp.sock"),
                         qmp_timer=5) as vm:
            vm.set_machine("z7ii-mas-diagnostic")
            vm.launch()
            assert registers(vm)[15] == BASE
            vm.cmd("cont")
            vm.event_wait("RESUME", timeout=5)
            if reason:
                vm.event_wait("STOP", timeout=5)
                assert vm.cmd("query-status")["status"] == "debug"
            else:
                deadline = time.monotonic() + 5
                while registers(vm)[15] != loop_pc:
                    assert vm.cmd("query-status")["running"], vm.get_log()
                    if time.monotonic() >= deadline:
                        raise AssertionError(f"{name}: final loop not reached")
                    time.sleep(0.01)
                vm.cmd("stop")
                vm.event_wait("STOP", timeout=5)
                actual = registers(vm)
                assert actual[15] == loop_pc, actual
                for reg, value in expected.items():
                    assert actual[reg] == value, (name, reg, actual, expected)
                if name in ("clock-reset", "completion-reset"):
                    vm.cmd("system_reset")
                    vm.event_wait("RESET", timeout=5)
                    assert registers(vm)[15] == BASE
                    vm.cmd("cont")
                    vm.event_wait("RESUME", timeout=5)
                    vm.event_wait("STOP", timeout=5)
                    assert vm.cmd("query-status")["status"] == "debug"
                    reason = ("clock-stop-bits-uninitialized" if name == "clock-reset"
                              else "completion-uninitialized-read")
            # Read the original ROM bytes through upstream HMP before shutdown.
            copied = output / "rom-readback.bin"
            vm.cmd("human-monitor-command",
                   command_line=f'pmemsave {BASE} {len(payload)} "{copied}"')
            assert copied.read_bytes() == payload, "guest changed synthetic ROM"
        assert vm.exitcode() == 0, vm.get_log()
        logs = (vm.get_log() or "") + trace.read_text()
        if reason:
            assert f"reason={reason}" in logs, logs
            assert "transaction=error" in logs, logs
        else:
            assert "transaction=error" not in logs, logs
            assert "Data Abort" not in logs, logs
            if name == "completion-irq":
                assert "applied=0x00000002 known=0x00000002 done=1" in logs
                assert "applied=0x00000000 known=0x00000002 done=1" in logs
    return {"fixture": name, "expected_error": reason, "qemu_exit_code": 0}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("output", type=Path, help="new output directory")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    results = [run(args.binary.resolve(), output / name, name, words,
                   expected, reason) for name, words, expected, reason in fixtures()]
    result = {"scope": "synthetic contracts only; no firmware boot proof",
              "results": results}
    (output / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result))


if __name__ == "__main__":
    main()
