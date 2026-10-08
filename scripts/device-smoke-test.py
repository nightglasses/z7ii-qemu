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
from qemu.qmp import ExecuteError

BASE = 0x10020000
MEMC = 0x04264000
TIM = 0x153b0902
CLOCK = 0x04281000
COMPLETION = 0x04287000
TIMESTAMP = 0x04266000
SDI = 0x20020000
MXIC = 0x20016000
FILL = 0x20070000
USIO = 0x2c090000


def constant(reg, value):
    """Two fixed ARM MOVW/MOVT words, used only to construct test bytes."""
    low, high = value & 0xffff, value >> 16
    return [0xe3000000 | (low & 0xf000) << 4 | reg << 12 | low & 0xfff,
            0xe3400000 | (high & 0xf000) << 4 | reg << 12 | high & 0xfff]


def initialized(mode=0x21):
    return (constant(0, MEMC) + constant(1, mode) + [0xe5801000] +
            constant(1, TIM) + [0xe5801020])


def sdi_contract(arb_reset=None):
    """Synthetic whole bring-up from the independently recovered guest profile."""
    profile = json.loads((Path(__file__).resolve().parents[1] /
                          "tests/sdi-init-profiles.json").read_text())
    ports = (0x8000, 0x8800, 0x8c00, 0x9000, 0x9800, 0x9c00, 0xa000,
             0xa800, 0xac00, 0xb000, 0xb800, 0xbc00, 0xc000, 0xc800, 0xcc00)
    words = []
    for bank in range(2):
        base = SDI + bank * 0x20000

        def write(offset, value):
            return constant(0, base + offset) + constant(1, value) + [0xe5801000]

        def configure(predicate):
            return [word for offset, *values in profile["configurations"]
                    if predicate(offset) for word in write(offset, values[bank])]

        def submit(request, value, status, ack=None):
            code = write(request, value) + constant(0, base + status)
            code += [0xe5902000, 0xe3120001, 0x0afffffc]
            code += write(status if ack is None else ack, 1)
            return code

        words += write(0x3000, 1) + write(0x2000, 1)
        words += configure(lambda o: o in (0x100, 0x10c, 0x2408, 0x2414) or
                           0x1004 <= o <= 0x1014 or o >= 0x10000)
        words += write(0, 0) + write(0x2000, 0)
        words += submit(0x2100, 1, 0x2300)
        words += configure(lambda o: 0x3400 <= o < 0x4000)
        words += write(0x3000, 0) + submit(0x3100, 0x10001, 0x3300)
        for mode in range(5):
            words += submit(0x3118, 1 | mode << 16, 0x3318)
        words += constant(0, base + 0x1016c) + [0xe5900000 | (10 + bank) << 12]
        words += configure(lambda o: 0x1020 <= o < 0x2000 or
                           0x4400 <= o < 0x4500)
        words += write(0x4000, 0) + submit(0x4100, 1, 0x4300)
        if arb_reset is not None:
            words += configure(lambda o: 0x8000 < o < 0x8800)
            words += write(0x8000, 0) + write(0x8010, 1)
            if arb_reset == "completed":
                words += constant(0, base + 0x8030)
                words += [0xe5902000, 0xe3120001, 0x0afffffc]
            words += write(0x4000, 1)
            # Deterministic icount fixture: let any stale callback expire.
            words += constant(1, 48000) + [0xee0e1f12]
            words += constant(1, 1) + [0xee0e1f32]
            words += [0xee1e1f32, 0xe3110004, 0x0afffffc]
            words += constant(0, base + 0x8030) + [0xe5904000]
            words += constant(0, base + 0x8000) + [0xe5905000]
            words += constant(0, base + 0xe300) + [0xe5906000]
            return words
        for index, port in enumerate(ports):
            end = ports[index + 1] if index + 1 < len(ports) else 0xe000
            words += configure(lambda o, start=port, limit=end: start < o < limit)
            words += write(port, 0) + submit(port + 0x10, 1, port + 0x30)
        words += configure(lambda o: 0xe000 < o < 0x10000)
        words += write(0xe000, 0)
        # Preserve the original bank1 asymmetry: poll E300, ack CC30.
        words += submit(0xe100, 1, 0xe300, 0xcc30 if bank else 0xe300)
        words += constant(0, base + 0xe300) + [0xe5900000 | (8 + bank) << 12]
    return words


def mxic_contract(disable=False, bad_geometry=False):
    # Independent original boot profile, not a read of device implementation.
    values = [0x60000000, 0x50000000, 0x50000000, 0xb0000000,
              0x50000000, 0, 0x30000, 0x40000000]
    if disable:
        values[2] = 0
    if bad_geometry:
        values[0] += 1
    words = []
    for index, value in enumerate(values):
        words += constant(0, MXIC + 0x20 + index * 4)
        words += constant(1, value) + [0xe5801000]
    words += constant(0, MXIC) + constant(1, 1) + [0xe5801000]
    for index, value in enumerate((0x60000000, 0x50000000, 0x50000000, 0xb0000000)):
        words += constant(0, MXIC + 0xd50 + index * 4)
        words += constant(1, value) + [0xe5801000]
    words += constant(0, MXIC + 0xd40) + constant(1, 1) + [0xe5801000]
    return words


def fill_contract(destination=0xb18aedc0, count=0x4ac):
    words = []
    for offset, value in ((0, 0), (0x14, 1), (0x20, 0x4000),
                          (0x24, 0x4000), (0x28, count), (0x18, 0),
                          (4, 0), (0x30, destination), (0x10, 1)):
        words += constant(0, FILL + offset) + constant(1, value) + [0xe5801000]
    return words


def usio_contract():
    words = []
    for offset, value, store in ((1, 0, 0xe5c01000), (1, 0x80, 0xe5c01000),
                                 (0, 1, 0xe5c01000), (2, 0, 0xe5c01000),
                                 (3, 0x80, 0xe5c01000), (0xc, 0, 0xe1c010b0),
                                 (6, 0x363, 0xe1c010b0), (0xc, 0xc, 0xe1c010b0),
                                 (0xc, 0x405, 0xe1c010b0), (1, 1, 0xe5c01000)):
        words += constant(0, USIO + offset) + constant(1, value) + [store]
    return words


def fixtures():
    uart_tx = usio_contract() + constant(0, USIO + 4) + constant(1, 0x41)
    uart_tx += [0xe1c010b0] + constant(0, USIO + 3) + [0xe5d04000]
    # TDRE must be pending first, then timer/backend completion produces empty.
    uart_tx += [0xe5d05000, 0xe3150002, 0x0afffffc]
    uart_flush = usio_contract() + constant(0, USIO + 4) + constant(1, 0x41)
    uart_flush += [0xe1c010b0] + constant(0, USIO + 1)
    uart_flush += constant(1, 0x80) + [0xe5c01000]
    uart_flush += constant(1, 48000) + [0xee0e1f12]
    uart_flush += constant(1, 1) + [0xee0e1f32]
    uart_flush += [0xee1e1f32, 0xe3110004, 0x0afffffc]
    uart_flush += constant(0, USIO + 3) + [0xe5d04000]
    fill = []
    for address in (0xb18aedc0, 0xb2b5edbc, 0xb2b5e510,
                    0xb18aedb4, 0xb2b74000):
        fill += constant(0, address) + constant(1, 0x12345678) + [0xe5801000]
    fill += fill_contract()
    for reg, address in ((4, 0xb18aedc0), (5, 0xb2b5edbc),
                         (6, 0xb2b5e510), (9, 0xb18aedb4), (10, 0xb2b74000)):
        fill += constant(0, address) + [0xe5900000 | reg << 12]
    fill += constant(0, FILL + 8) + [0xe5907000]
    fill += constant(1, 1) + [0xe5801000, 0xe5908000]
    fill_error = fill_contract(0x04000000, 1)
    fill_error += constant(0, FILL + 8) + [0xe5904000]
    fill_error += constant(1, 0x10) + [0xe5801000, 0xe5905000]
    # MODE read/modify/write; TIM readback; both ends of exactly 88 bytes RAM.
    profile = initialized() + [0xe5904000, 0xe5905020, 0xe3841008,
                               0xe5801000, 0xe5906000, 0xe3c11008,
                               0xe5801000, 0xe5907000]
    profile += constant(0, 0x04008ea8) + constant(1, 0x13579bdf)
    profile += [0xe5801000, 0xe5801054, 0xe5908000, 0xe5909054]

    # Enable secure PPI 29, let the upstream timer expire with CPU IRQ masked,
    # acknowledge it, disable the level, EOI, and inspect active state.
    timer = constant(2, 0xf8101000) + constant(1, 1) + [0xe5821000]
    timer += constant(1, 1 << 29) + [0xe5821100]
    timer += constant(0, 0xf8102000) + constant(1, 0xff) + [0xe5801004]
    timer += constant(1, 1) + [0xe5801000]
    timer += constant(1, 48000) + [0xee0e1f12]  # CNTP_TVAL
    timer += constant(1, 1) + [0xee0e1f32]  # CNTP_CTL.ENABLE
    timer += [0xee1e1f32, 0xe3110004, 0x0afffffc]  # Wait for ISTATUS.
    timer += [0xe590800c, 0xe5929300]  # IAR; GICD_ISACTIVER0
    timer += constant(1, 0) + [0xee0e1f32, 0xe5808010, 0xe592a300]
    timer += [0xee1ebf10, 0xee9fcf10]  # CNTFRQ; CBAR

    # All six virtual gate banks start ungated. SET/CLEAR share the same state.
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
    # before writing it, proving the virtual reset restores ungated state.
    gate_reset = constant(0, 0xb1800000) + [0xe5901000, 0xe3510001,
                                         0x0a000007, 0xe3a01001, 0xe5801000]
    gate_reset += constant(0, CLOCK) + constant(1, 0xffffffff)
    gate_reset += [0xe5801148, 0xea000002]
    gate_reset += constant(0, CLOCK) + [0xe5902048]

    pll = constant(0, CLOCK) + [0xe5904028, 0xe590502c, 0xe5906030]
    pll += constant(1, 0x72) + [0xe580102c]  # Start group6, still output gated.
    pll += constant(1, 0x70) + [0xe580102c]  # Guest removes output gate.
    pll += constant(1, 48000) + [0xee0e1f12]
    pll += constant(1, 1) + [0xee0e1f32]
    pll += [0xee1e1f32, 0xe3110004, 0x0afffffc, 0xe590702c]

    # Independent nominal Clock, elapsed-time counter, disable/freeze and carry.
    timestamp = constant(0, TIMESTAMP) + [0xe5907000, 0xe590b010, 0xe5904008]
    timestamp += constant(3, 4800)
    timestamp += [0xe5901008, 0xe0412004, 0xe1520003, 0x3afffffb]
    timestamp += constant(1, 0) + [0xe5801000, 0xe5905008]
    timestamp += constant(1, 48000) + [0xee0e1f12]
    timestamp += constant(1, 1) + [0xee0e1f32]
    timestamp += [0xee1e1f32, 0xe3110004, 0x0afffffc, 0xe5906008]
    timestamp += constant(1, 0x11223344) + [0xe580100c]
    timestamp += constant(1, 0xfffffff0) + [0xe5801008]
    timestamp += constant(1, 48000000) + [0xe5801010]
    timestamp += constant(1, 3) + [0xe5801000]
    timestamp += constant(1, 48000) + [0xee0e1f12]
    timestamp += [0xee1e1f32, 0xe3110004, 0x0afffffc, 0xe590800c]

    counter_init = constant(0, TIMESTAMP)
    counter_init += [0xe5901000, 0xe3c11003, 0xe5801000, 0xe3a01000,
                     0xe5801008, 0xe580100c]
    counter_init += constant(1, 48000000) + [0xe5801010]
    counter_init += constant(2, 48000) + [0xee0e2f12]
    counter_init += constant(2, 1) + [0xee0e2f32]
    counter_init += [0xe5901000, 0xe3811003, 0xe5801000, 0xe5904008]
    counter_init += [0xee1e2f32, 0xe3120004, 0x0afffffc]
    counter_init += [0xe5905008, 0xe5906010, 0xe5907000]

    # IRQ250 is a real GIC SPI: enable, target CPU0, submit, poll done, IAR,
    # ack at the device, and EOI. CPU IRQ is masked, as in the timer fixture.
    completion = constant(2, 0xf8101000) + constant(1, 1) + [0xe5821000]
    completion += [0xe5c218fa]  # GICD_ITARGETSR byte for IRQ250 = CPU0.
    completion += constant(1, 1 << 26) + [0xe582111c]
    completion += constant(3, 0xf8102000) + constant(1, 0xff) + [0xe5831004]
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
    polling += constant(2, 0xf8101000) + [0xe592721c]
    polling += constant(1, 1) + [0xe5801004, 0xe5905004]

    completion_reset = constant(0, 0xb1800000)
    completion_reset += [0xe5901000, 0xe3510001, 0x0a000007,
                         0xe3a01001, 0xe5801000]
    completion_reset += constant(0, COMPLETION)
    completion_reset += [0xe3a01000, 0xe5801000, 0xe5801008, 0xea000002]
    completion_reset += constant(0, COMPLETION) + [0xe5902008]

    completion_default = constant(0, COMPLETION) + constant(1, 0xa1)
    completion_default += [0xe5801010, 0xe5904004, 0xe3140001, 0x0afffffc]
    completion_default += [0xe5905008, 0xe590600c]

    return [
        ("usio-whole-init", usio_contract() + constant(0, USIO) +
         [0xe5d04001, 0xe5d05003, 0xe1d060b6, 0xe1d070bc],
         {4: 1, 5: 2, 6: 0x363, 7: 0x401}, None),
        ("usio-real-transmit", uart_tx, {4: 0, 5: 2}, None),
        ("usio-reset-pending", uart_flush, {4: 2}, None),
        ("usio-disabled-transmit", constant(0, USIO + 4) +
         constant(1, 0x41) + [0xe1c010b0], {}, "usio-transmit-phase-not-supported"),
        ("usio-unsupported-baud", constant(0, USIO + 6) +
         constant(1, 1) + [0xe1c010b0], {}, "usio-value-profile-not-supported"),
        ("usio-uncovered-irq-mode", constant(0, USIO + 1) +
         constant(1, 5) + [0xe5c01000], {}, "usio-value-profile-not-supported"),
        ("usio-word-width", constant(0, USIO) + [0xe5901000], {},
         "usio-access-not-supported"),
        ("usio-hole", constant(0, USIO + 8) + [0xe1d010b0], {},
         "usio-access-not-supported"),
        ("profile-stack", profile,
         {4: 0x21, 5: TIM, 6: 0x29, 7: 0x21,
          8: 0x13579bdf, 9: 0x13579bdf}, None),
        ("secure-timer", timer,
         {8: 29, 9: 1 << 29, 10: 0, 11: 48000000, 12: 0xf8100000}, None),
        ("clock-six-banks", gates, gate_values, None),
        ("clock-reset", gate_reset, {}, None),
        ("clock-reset-state", constant(0, CLOCK) + [0xe5901048], {1: 0}, None),
        ("clock-partial-mask", constant(0, CLOCK) + constant(1, 1) +
         [0xe5801148, 0xe5901048], {1: 1}, None),
        ("clock-zero-mask", constant(0, CLOCK) + constant(1, 0) +
         [0xe5801248, 0xe5901048], {1: 0}, None),
        ("clock-alias-read", constant(0, CLOCK) + [0xe5901138], {},
         "clock-access-not-supported"),
        ("clock-direct-write", constant(0, CLOCK) + [0xe5801038], {},
         "clock-access-not-supported"),
        ("clock-byte-write", constant(0, CLOCK) + [0xe5c01138], {},
         "clock-access-not-supported"),
        ("clock-bank-boundary", constant(0, CLOCK) + [0xe5801150], {},
         "clock-access-not-supported"),
        ("clock-pll-start", pll, {4: 0xffe, 5: 0x73, 6: 3, 7: 0x70}, None),
        ("clock-pll-bad-fields", constant(0, CLOCK) + constant(1, 1) +
         [0xe5801028], {}, "clock-control-fields-not-supported"),
        ("clock-selector-word", constant(0, CLOCK) + constant(1, 0x82808888) +
         [0xe580101c, 0xe590201c], {2: 0x82808888}, None),
        ("clock-reserved-offset", constant(0, CLOCK) + [0xe5901034], {},
         "clock-access-not-supported"),
        ("timestamp-whole-guest-init", counter_init, {6: 48000000, 7: 3}, None),
        ("timestamp-unsupported-single-control-bit", constant(0, TIMESTAMP) +
         constant(1, 1) + [0xe5801000], {}, "timestamp-control-not-supported"),
        ("timestamp-removed-vendor-frequency", constant(0, TIMESTAMP + 0x20) +
         [0xe5901000], {}, "uncovered-address"),
        ("timestamp-elapsed-freeze-carry", timestamp,
         {7: 3, 8: 0x11223345, 11: 48000000}, None),
        ("timestamp-count-while-enabled", constant(0, TIMESTAMP) +
         [0xe5801008], {}, "timestamp-count-write-while-enabled"),
        ("timestamp-bad-frequency", constant(0, TIMESTAMP) +
         constant(1, 0) + [0xe5801000] + constant(1, 1) + [0xe5801010], {},
         "timestamp-frequency-profile-not-supported"),
        ("timestamp-bad-control", constant(0, TIMESTAMP) +
         constant(1, 4) + [0xe5801000], {}, "timestamp-control-not-supported"),
        ("timestamp-reserved-read", constant(0, TIMESTAMP) + [0xe5901004], {},
         "timestamp-access-not-supported"),
        ("timestamp-readonly-status", constant(0, TIMESTAMP) + [0xe5801004], {},
         "timestamp-access-not-supported"),
        ("timestamp-byte-write", constant(0, TIMESTAMP) + [0xe5c01000], {},
         "timestamp-access-not-supported"),
        ("kernel-arena-endpoints", constant(0, 0xb2c00000) +
         constant(1, 0x11223344) + [0xe5801000, 0xe5904000] +
         constant(0, 0xb5effffc) + constant(1, 0xaabbccdd) +
         [0xe5801000, 0xe5905000], {4: 0x11223344, 5: 0xaabbccdd}, None),
        ("kernel-arena-before", constant(0, 0xb2bffffc) + [0xe5901000], {},
         "uncovered-address"),
        ("kernel-arena-after", constant(0, 0xb5f00000) + [0xe5901000], {},
         "uncovered-address"),
        ("fill-bss-before-exact-window", constant(0, 0xb18aedbc) +
         [0xe5901000], {}, "uncovered-address"),
        ("fill-bss-after-exact-window", constant(0, 0xb2b5edc0) +
         [0xe5901000], {}, "uncovered-address"),
        ("fill-control-does-not-reset", fill_contract(0xb1800000, 1)[:-5] +
         constant(0, FILL) + constant(1, 1) + [0xe5801000, 0xe5904014,
                                              0xe5905020, 0xe5906024],
         {4: 1, 5: 0x4000, 6: 0x4000}, None),
        ("fill-virtual-rmw-reset", constant(0, FILL) +
         [0xe5904014, 0xe5905020, 0xe5906024], {4: 0, 5: 0, 6: 0}, None),
        ("fill-unsupported-ack", constant(0, FILL + 8) +
         constant(1, 0x11) + [0xe5801000], {}, "fill-ack-not-supported"),
        ("fill-real-bss-and-tail", fill,
         {4: 0, 5: 0, 6: 0, 7: 1, 8: 0, 9: 0x12345678, 10: 0x12345678}, None),
        ("fill-bus-error-ack", fill_error, {4: 0x10, 5: 0}, None),
        ("fill-withdrawn-ddr-error", mxic_contract(disable=True) +
         fill_contract(0xb1800000, 1) + constant(0, FILL + 8) + [0xe5904000],
         {4: 0x10}, None),
        ("fill-unacknowledged-result", fill_contract(0xb1800000, 1) +
         constant(0, FILL + 0x10) + constant(1, 1) + [0xe5801000], {},
         "fill-result-not-acknowledged"),
        ("fill-incomplete-request", constant(0, FILL + 0x10) +
         constant(1, 1) + [0xe5801000], {}, "fill-request-profile-not-supported"),
        ("fill-unsupported-count", constant(0, FILL + 0x28) +
         constant(1, 0) + [0xe5801000], {}, "fill-value-profile-not-supported"),
        ("fill-unsupported-pattern", constant(0, FILL + 0x18) +
         constant(1, 1) + [0xe5801000], {}, "fill-value-profile-not-supported"),
        ("fill-reserved-offset", constant(0, FILL + 0x0c) + [0xe5901000], {},
         "fill-access-not-supported"),
        ("fill-byte-write", constant(0, FILL + 0x10) + [0xe5c01000], {},
         "fill-access-not-supported"),
        ("mxic-region-commit-readback", mxic_contract() +
         constant(0, MXIC + 0x20) + [0xe5904000, 0xe590501c] +
         constant(0, MXIC + 0xd50) + [0xe5906000, 0xe590700c] +
         constant(0, 0xb1800000) + constant(1, 0x12345678) +
         [0xe5801000, 0xe5908000],
         {4: 0x60000000, 5: 0x40000000, 6: 0x60000000,
          7: 0xb0000000, 8: 0x12345678}, None),
        ("mxic-region-withdraw-backing", mxic_contract(disable=True) +
         constant(0, 0xb1800000) + [0xe5901000], {}, "uncovered-address"),
        ("mxic-incomplete-commit", constant(0, MXIC) + constant(1, 1) +
         [0xe5801000], {}, "mxic-incomplete-region-transaction"),
        ("mxic-bad-geometry", mxic_contract(bad_geometry=True), {},
         "mxic-region-geometry-not-supported"),
        ("mxic-bad-command", constant(0, MXIC) + constant(1, 2) +
         [0xe5801000], {}, "mxic-command-not-supported"),
        ("mxic-reserved-offset", constant(0, MXIC + 4) + [0xe5901000], {},
         "mxic-access-not-supported"),
        ("mxic-byte-write", constant(0, MXIC + 0x20) + [0xe5c01000], {},
         "mxic-access-not-supported"),
        ("sdi-two-bank-bringup", sdi_contract(),
         {8: 0, 9: 1, 10: 0, 11: 0}, None),
        ("sdi-arb-reset-pending", sdi_contract("pending"),
         {4: 0, 5: 1, 6: 0}, None),
        ("sdi-arb-reset-completed", sdi_contract("completed"),
         {4: 0, 5: 1, 6: 0}, None),
        ("sdi-reset-state", constant(0, SDI) + [0xe5904000] +
         constant(0, SDI + 0x2000) + [0xe5905000] +
         constant(0, SDI + 0x2300) + [0xe5906000], {4: 1, 5: 1, 6: 0}, None),
        ("sdi-request-before-configuration", constant(0, SDI + 0x2100) +
         constant(1, 1) + [0xe5801000], {}, "sdi-request-phase-not-supported"),
        ("sdi-training-before-init", constant(0, SDI + 0x3118) +
         constant(1, 1) + [0xe5801000], {}, "sdi-request-phase-not-supported"),
        ("sdi-training-bad-mode", constant(0, SDI + 0x3118) +
         constant(1, 0x50001) + [0xe5801000], {},
         "sdi-request-profile-not-supported"),
        ("sdi-training-result-before-request", constant(0, SDI + 0x1016c) +
         [0xe5901000], {}, "sdi-training-result-uninitialized"),
        ("sdi-unsupported-config", constant(0, SDI + 0x10010) +
         constant(1, 6) + [0xe5801000], {}, "sdi-config-profile-not-supported"),
        ("sdi-unsupported-ack", constant(0, SDI + 0x2300) +
         constant(1, 2) + [0xe5801000], {}, "sdi-ack-profile-not-supported"),
        ("sdi-reserved-offset", constant(0, SDI + 0x104) +
         [0xe5901000], {}, "sdi-access-not-supported"),
        ("sdi-byte-write", constant(0, SDI + 0x2100) + [0xe5c01000], {},
         "sdi-access-not-supported"),
        ("completion-irq", completion,
         {4: 1, 5: 0, 6: 0x1110, 7: 1 << 26, 8: 250,
          9: 1 << 26, 10: 0, 11: 1023, 12: 1, 13: 0}, None),
        ("completion-poll", polling, {4: 1, 5: 0, 7: 0}, None),
        ("completion-reset", completion_reset, {}, None),
        ("completion-reset-done", constant(0, COMPLETION) + [0xe5901004],
         {1: 0}, None),
        ("completion-reset-request", constant(0, COMPLETION) + [0xe5901000],
         {1: 0}, None),
        ("completion-reset-enable", constant(0, COMPLETION) + [0xe5901008],
         {1: 0}, None),
        ("completion-default-transaction", completion_default,
         {4: 1, 5: 0, 6: 0}, None),
        ("completion-applied-domain", polling + [0xe590600c],
         {4: 1, 5: 0, 6: 8, 7: 0}, None),
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
        ("completion-bad-ack", constant(0, COMPLETION) +
         constant(1, 2) + [0xe5801004], {}, "completion-ack-not-supported"),
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
        ("stack-below", constant(0, 0x04008ea4) + [0xe5801000], {},
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
    if name.startswith("sdi-arb-reset-") or name.startswith("usio-"):
        vm_args += ["-icount", "shift=0"]
    if name.startswith("usio-"):
        serial = output / "serial.bin"
        vm_args[vm_args.index("-serial") + 1] = f"file:{serial}"
    with tempfile.TemporaryDirectory(prefix="ng-", dir="/tmp") as sockets:
        with QEMUMachine(str(binary), args=vm_args, name="device",
                         base_temp_dir=sockets, log_dir=str(output),
                         monitor_address=str(Path(sockets) / "qmp.sock"),
                         qmp_timer=5) as vm:
            vm.set_machine("z7ii-mas-diagnostic")
            vm.launch()
            assert registers(vm)[15] == BASE
            if name == "clock-reset":
                try:
                    vm.cmd("migrate", uri="unix:/tmp/ng-no-migration-peer.sock")
                except ExecuteError as error:
                    assert "unmigratable" in str(error).lower() or \
                           "non-migratable" in str(error).lower(), error
                else:
                    raise AssertionError("stateful diagnostic board allowed migration")
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
                if name == "timestamp-whole-guest-init":
                    assert actual[5] > actual[4], actual
                if name == "timestamp-elapsed-freeze-carry":
                    assert actual[2] >= 4800, actual
                    assert actual[5] == actual[6], actual
                if name in ("clock-reset", "completion-reset"):
                    vm.cmd("system_reset")
                    vm.event_wait("RESET", timeout=5)
                    assert registers(vm)[15] == BASE
                    vm.cmd("cont")
                    vm.event_wait("RESUME", timeout=5)
                    deadline = time.monotonic() + 5
                    while registers(vm)[15] != loop_pc:
                        assert vm.cmd("query-status")["running"], vm.get_log()
                        assert time.monotonic() < deadline, "reset loop not reached"
                        time.sleep(0.01)
                    vm.cmd("stop")
                    vm.event_wait("STOP", timeout=5)
                    assert registers(vm)[2] == 0
            # Read the original ROM bytes through upstream HMP before shutdown.
            copied = output / "rom-readback.bin"
            vm.cmd("human-monitor-command",
                   command_line=f'pmemsave {BASE} {len(payload)} "{copied}"')
            assert copied.read_bytes() == payload, "guest changed synthetic ROM"
        assert vm.exitcode() == 0, vm.get_log()
        if name.startswith("usio-"):
            expected_serial = b"A" if name == "usio-real-transmit" else b""
            assert serial.read_bytes() == expected_serial, (name, serial.read_bytes())
        logs = (vm.get_log() or "") + trace.read_text()
        if reason:
            assert f"reason={reason}" in logs, logs
            assert "transaction=error" in logs, logs
        else:
            assert "transaction=error" not in logs, logs
            assert "Data Abort" not in logs, logs
            if name == "completion-irq":
                assert "applied=0x00000002 done=1" in logs
                assert "applied=0x00000000 done=1" in logs
            if name == "clock-pll-start":
                assert "PLL group=6 modeled-lock nominal-Hz=48000000" in logs
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
