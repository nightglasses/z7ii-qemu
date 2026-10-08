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
from qemu.machine.qtest import QEMUQtestMachine
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
SYSCONFIG = 0x04280000
GPIO = 0x04282000
SPI = 0x2c0a0000


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
            # SDI completion is10us: settle for20us, not1ms of icount polls.
            # This still detects a stale callback surviving the reset.
            words += constant(1, 960) + [0xee0e1f12]
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


def mxic_config_fixtures():
    base = 0x20010000

    def write(address, value):
        return constant(0, address) + constant(1, value) + [0xe5801000]

    def read(address, reg):
        return constant(0, address) + [0xe5900000 | reg << 12]

    offsets = (0x38, 0x3c, *range(0x100, 0x120, 4),
               0x5004, *range(0x5020, 0x5040, 4))
    prepare = write(base + 0x60, 1)
    for offset in offsets:
        prepare += write(base + offset, 1 if offset == 0x100 else 0)
    for index in range(512):
        prepare += write(0x24074000 + index * 4, 1 if index == 0 else 0)
    transaction = prepare + read(base + 0x100, 4)
    transaction += write(base + 0x44, 1) + read(base + 0x100, 5)
    transaction += write(base + 0x44, 0) + write(base + 0x34, 1)
    transaction += read(base + 0x38, 6)
    transaction += constant(0, base + 0x38)
    transaction += [0xe5902000, 0xe3120001, 0x0afffffc]
    transaction += write(base + 0x60, 0) + write(base + 0x44, 1)
    transaction += read(base + 0x100, 7) + read(base + 0x4800, 8)
    direct = write(base + 0x2000, 0x18080000)
    direct += write(base + 0x2004, 0x100800)
    direct += write(base + 0x2008, 0x80800)
    direct += write(base + 0x2490, 0x500002) + read(base + 0x2490, 4)
    direct += read(base + 0x2000, 5) + read(base + 0x38, 6)
    arrays = write(MXIC + 0xa00, 1) + read(MXIC + 0xa00, 4)
    arrays += read(MXIC + 0xa7c, 5)
    selectors = []
    for bank in range(4):
        for offset in (*range(0, 0x5c, 4), *range(0xbc, 0xdc, 4)):
            selectors += write(base + 0x3000 + bank * 0x100 + offset, 0x32)
    selectors += read(base + 0x3000, 4) + read(base + 0x33d8, 5)
    rates = []
    for offset in (0x2648, 0x2490, 0x24a0, 0x2790, 0x2798,
                   0x2628, 0x2658, 0x2660, 0x27b0, 0x2618):
        rates += write(base + offset, 0x500002)
    rates += read(base + 0x2658, 4) + read(base + 0x2660, 5)
    return [
        ("mxic-config-transaction", transaction,
         {4: 1, 5: 0, 6: 0, 7: 1, 8: 1}, None),
        ("mxic-config-reset", transaction,
         {4: 1, 5: 0, 6: 0, 7: 1, 8: 1}, None),
        ("mxic-config-direct-parameters", direct,
         {4: 0x500002, 5: 0x18080000, 6: 1}, None),
        ("mxic-config-arrays", arrays, {4: 1, 5: 0}, None),
        ("mxic-config-selectors", selectors, {4: 0x32, 5: 0x32}, None),
        ("mxic-config-selector-hole", read(base + 0x305c, 4), {},
         "mxic-config-access-not-supported"),
        ("mxic-config-selector-field", write(base + 0x3000, 4), {},
         "mxic-config-access-not-supported"),
        ("mxic-config-rate-family", rates, {4: 0x500002, 5: 0x500002}, None),
        ("mxic-config-rate-field", write(base + 0x2660, 4), {},
         "mxic-config-access-not-supported"),
        ("mxic-config-old-address", read(base + 0x24a8, 4), {},
         "mxic-config-access-not-supported"),
        ("mxic-config-incomplete", write(base + 0x60, 1) +
         write(base + 0x34, 1), {}, "mxic-config-incomplete-transaction"),
        ("mxic-config-access-gate", write(0x24074000, 1), {},
         "mxic-slot-access-not-supported"),
        ("mxic-config-active-view", write(base + 0x44, 1) +
         write(base + 0x100, 1), {}, "mxic-config-active-view-write"),
        ("mxic-config-busy-write", prepare + write(base + 0x34, 1) +
         write(base + 0x38, 1), {}, "mxic-config-busy"),
        ("mxic-config-slot-field", write(base + 0x60, 1) +
         write(0x24074000, 0x80), {}, "mxic-slot-access-not-supported"),
        ("mxic-config-hole", read(base + 0x48, 4), {},
         "mxic-config-access-not-supported"),
        ("mxic-config-width", constant(0, base + 0x44) + [0xe5c01000], {},
         "mxic-config-access-not-supported"),
    ]


def fixtures():
    percore = []
    for top in (0xb2bb0000, 0xb2ba8000, 0xb2ba0000,
                0xb2b98000, 0xb2b90000, 0xb2b88000):
        for address in (top - 0x8000, top - 0x4004):
            percore += constant(0, address) + constant(1, 0x1234) + [0xe5801000]
    percore += constant(0, 0xb2b93ff8) + [0xe5801000, 0xe5904000]
    sys_boot = []
    boot_values = (0x73ffffff, 0xffffffff, 0x1ffc1ff, 0x31ffffff, 0x4ee)
    for bank, value in enumerate(boot_values):
        sys_boot += constant(0, SYSCONFIG + 0x78 + bank * 4)
        sys_boot += constant(1, value) + [0xe5801000, 0xe5900000 | (bank + 4) << 12]
    # Mutate a known field; all remaining guest-initialized bits must survive.
    sys_boot += constant(0, SYSCONFIG + 0x7c) + [0xe5901000]
    sys_boot += constant(2, 1) + [0xe1c11002, 0xe5801000, 0xe5909000]
    gpio_all, gpio_expected = [], {}
    for port in range(12):
        for bank, value in enumerate((0xffff0000 | (0x100 + port),
                                      0xffff, 0, 0, 0)):
            gpio_all += constant(0, GPIO + bank * 0x100 + port * 4)
            gpio_all += constant(1, value) + [0xe5801000]
    for port in range(12):
        reg = port + 2
        gpio_all += constant(0, GPIO + port * 4) + [0xe5900000 | reg << 12]
        gpio_expected[reg] = 0x100 + port
    gpio_pads = []
    for offset, value in ((0, 0xffff0003), (0x100, 3), (0x200, 2),
                          (0x300, 6), (0x400, 4)):
        gpio_pads += constant(0, GPIO + offset) + constant(1, value) + [0xe5801000]
    gpio_pads += constant(0, GPIO) + [0xe5904000]
    # Unselected data bits do not overwrite the latch, and direction changes
    # expose the actual stored latch rather than caching the last PDR read.
    gpio_pads += constant(1, 0x00010002) + [0xe5801000]
    gpio_pads += constant(0, GPIO + 0x100) + constant(1, 0xffff) + [0xe5801000]
    gpio_pads += constant(0, GPIO + 0x200) + constant(1, 0) + [0xe5801000]
    gpio_pads += constant(0, GPIO) + [0xe5905000]
    sysconfig = []
    for offset, value in ((4, 0x12345678), (8, 0xabcdef01), (0x24, 1),
                          (0x28, 0x123), (0x30, 0x410), (0x74, 0x21)):
        sysconfig += constant(0, SYSCONFIG + offset) + constant(1, value)
        sysconfig += [0xe5801000]
    sysconfig += constant(0, SYSCONFIG + 0x30) + [0xe5904000]
    sysconfig += constant(0, SYSCONFIG + 0x74) + [0xe5905000]
    sys_banks = []
    for reg, offset, value in ((4, 0x7c, 0x1001e07), (5, 0x80, 0x1ff01c0),
                               (6, 0x84, 0x1803dfc0), (7, 0x88, 0x444)):
        sys_banks += constant(0, SYSCONFIG + offset) + [0xe5901000]
        sys_banks += constant(2, value) + [0xe1811002, 0xe5801000]
        sys_banks += [0xe5900000 | reg << 12, 0xe1c11002, 0xe5801000]
    sys_banks += constant(0, SYSCONFIG + 0x88) + [0xe5908000]
    uart_tx = usio_contract() + constant(0, USIO + 4) + constant(1, 0x41)
    uart_tx += [0xe1c010b0] + constant(0, USIO + 3) + [0xe5d04000]
    # TDRE must be pending first, then timer/backend completion produces empty.
    uart_tx += [0xe5d05000, 0xe3150002, 0x0afffffc]
    uart_flush = usio_contract() + constant(0, USIO + 4) + constant(1, 0x41)
    uart_flush += [0xe1c010b0] + constant(0, USIO + 1)
    uart_flush += constant(1, 0x80) + [0xe5c01000]
    # BGR 0x363 at the virtual 100 MHz input gives an 86.8 us frame.
    # Observe beyond that cancelled deadline, without a 1 ms busy wait.
    uart_flush += constant(1, 4800) + [0xee0e1f12]
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
    # Original init writes active29 and then EOIR29 without an IAR.
    # On the selected mature pre-v2 interface, active registers are read-only.
    legacy_timer = constant(2, 0xf8101000) + constant(1, 1 << 29)
    legacy_timer += [0xe5821300] + constant(0, 0xf8102000)
    legacy_timer += constant(1, 29) + [0xe5801010] + timer

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

    return mxic_config_fixtures() + ring_fixtures() + [
        ("nv-slot69-erased-bounds", constant(0, 0x133e0000) +
         [0xe5904000, 0xe5d05000, 0xe1d060b0] +
         constant(0, 0x133ffffc) + [0xe5907000],
         {4: 0xffffffff, 5: 0xff, 6: 0xffff, 7: 0xffffffff}, None),
        ("nv-slot69-before-memc", constant(0, 0x133e0000) + [0xe1c010b0], {},
         "nor-memc-not-initialized"),
        ("nv-slot69-protected", initialized(0x29) + constant(0, 0x133e0000) +
         [0xe1c010b0], {}, "nor-write-protected"),
        ("nv-slot69-no-command-backend", initialized() +
         constant(0, 0x133e0aaa) + constant(1, 0xaa) + [0xe1c010b0], {},
         "nor-backend-not-supported"),
        ("module-buffer-entire-allocation", constant(0, 0xb7d00000) +
         constant(1, 0x1234) + [0xe5801000, 0xe5904000] +
         constant(0, 0xb809fffc) + [0xe5801000, 0xe5905000],
         {4: 0x1234, 5: 0x1234}, None),
        ("module-buffer-below", constant(0, 0xb7cffffc) + [0xe5801000], {},
         "uncovered-address"),
        ("module-buffer-above", constant(0, 0xb80a0000) + [0xe5801000], {},
         "uncovered-address"),
        ("module-buffer-region-withdrawn", mxic_contract(disable=True) +
         constant(0, 0xb7d00000) + [0xe5801000], {}, "uncovered-address"),
        ("clock-bitcontrol-rmw", constant(0, CLOCK + 0x58) + [0xe5902000] +
         [0xe3823008, 0xe5803000, 0xe5904000, 0xe3843080, 0xe5803000,
          0xe5904000, 0xe3c43008, 0xe5803000, 0xe5905000] +
         constant(0, CLOCK + 0x5c) + constant(1, 0x80000000) +
         [0xe5801000, 0xe5906000], {2: 0, 4: 0x88, 5: 0x80, 6: 0x80000000}, None),
        ("clock-bitcontrol-reset", constant(0, CLOCK + 0x58) + [0xe5902000] +
         constant(1, 0x20) + [0xe5801000], {2: 0}, None),
        ("clock-bitcontrol-width", constant(0, CLOCK + 0x58) + [0xe5c01000], {},
         "clock-access-not-supported"),
        ("clock-bitcontrol-hole", constant(0, CLOCK + 0x54) + [0xe5901000], {},
         "clock-access-not-supported"),
        ("clock-bitcontrol-no-alias", constant(0, CLOCK + 0x158) + [0xe5801000], {},
         "clock-access-not-supported"),
        ("percore-stack-two-banks", percore, {4: 0x1234}, None),
        ("percore-stack-two-below", constant(0, 0xb2b7fffc) + [0xe5801000], {},
         "uncovered-address"),
        ("percore-stack-single-gap", constant(0, 0xb2b80000) + [0xe5801000], {},
         "uncovered-address"),
        ("kernel-copy-bounds", constant(0, 0xbab00000) + constant(1, 0x1234) +
         [0xe5801000, 0xe5904000] + constant(0, 0xbab21534) +
         [0xe5801000, 0xe5905000], {4: 0x1234, 5: 0x1234}, None),
        ("kernel-copy-below", constant(0, 0xbaaffffc) + [0xe5801000], {},
         "uncovered-address"),
        ("kernel-copy-above", constant(0, 0xbab21538) + [0xe5801000], {},
         "uncovered-address"),
        ("kernel-copy-region-withdrawn", mxic_contract(disable=True) +
         constant(0, 0xbab00000) + [0xe5801000], {}, "uncovered-address"),
        ("sysconfig-whole-bootstrap-preserve", sys_boot,
         {4: boot_values[0], 5: boot_values[1], 6: boot_values[2],
          7: boot_values[3], 8: boot_values[4], 9: 0xfffffffe}, None),
        ("sysconfig-unproved-bootstrap-bit", constant(0, SYSCONFIG + 0x78) +
         constant(1, 1) + [0xe5801000], {}, "sysconfig-bank-bits-not-supported"),
        ("sysconfig-unproved-post-bootstrap-bit", sys_boot +
         constant(0, SYSCONFIG + 0x7c) + [0xe5901000] +
         constant(2, 8) + [0xe0211002, 0xe5801000], {},
         "sysconfig-bank-bits-not-supported"),
        ("gpio-all-twelve-ports", gpio_all, gpio_expected, None),
        ("gpio-direction-function-pull-and-mask", gpio_pads, {4: 5, 5: 2}, None),
        ("gpio-boot-pinmux-selectors", constant(0, SYSCONFIG + 0x34) +
         [0xe5901000] + constant(2, 0x50000000) + [0xe1811002, 0xe5801000,
                                                        0xe5904000] +
         constant(0, SYSCONFIG + 0x38) + constant(1, 0x660000) +
         [0xe5801000, 0xe5905000], {4: 0x50000000, 5: 0x660000}, None),
        ("gpio-unimplemented-port", constant(0, GPIO + 0x30) + [0xe5901000], {},
         "gpio-access-not-supported"),
        ("gpio-bank-hole", constant(0, GPIO + 0x40) + [0xe5901000], {},
         "gpio-access-not-supported"),
        ("gpio-unknown-width", constant(0, GPIO) + [0xe5c01000], {},
         "gpio-access-not-supported"),
        ("gpio-high-config-bits", constant(0, GPIO + 0x100) +
         constant(1, 0x10000) + [0xe5801000], {}, "gpio-field-not-supported"),
        ("sysconfig-direct-interface", sysconfig, {4: 0x410, 5: 0x21}, None),
        ("sysconfig-four-bank-rmw", sys_banks,
         {4: 0x1001e07, 5: 0x1ff01c0, 6: 0x1803dfc0, 7: 0x444, 8: 0}, None),
        ("sysconfig-uninitialized-config", constant(0, SYSCONFIG + 4) +
         [0xe5901000], {}, "sysconfig-before-guest-write"),
        ("sysconfig-unknown-bank-bit", constant(0, SYSCONFIG + 0x80) +
         constant(1, 1) + [0xe5801000], {}, "sysconfig-bank-bits-not-supported"),
        ("sysconfig-unsupported-field", constant(0, SYSCONFIG + 0x24) +
         constant(1, 2) + [0xe5801000], {}, "sysconfig-field-not-supported"),
        ("sysconfig-byte-width", constant(0, SYSCONFIG + 0x30) +
         [0xe5c01000], {}, "sysconfig-access-not-supported"),
        ("sysconfig-unknown-index", constant(0, SYSCONFIG + 0xc) +
         [0xe5801000], {}, "sysconfig-access-not-supported"),
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
        ("secure-timer-original-active-init", legacy_timer,
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


def spi_fixtures():
    def write(offset, value):
        return constant(0, SPI + offset) + constant(1, value) + [0xe5801000]

    setup = write(0x60, 0) + write(0xc, 0x22) + write(0x10, 0xf00)
    transfer = setup + write(0x4c, 1) + write(0x28, 0x1ff)
    transfer += write(0, 0xa55a) + write(0x3c, 1)
    transfer += constant(0, SPI + 0x14)
    transfer += [0xe5901000, 0xe3110020, 0x1afffffc]
    transfer += constant(0, SPI) + [0xe5904024, 0xe5905004,
                                   0xe5906014, 0xe5907048]
    transfer += write(0x28, 0x1ff) + constant(0, SPI) + [0xe5908024]

    # A slave has no external clock: queued TX remains, with no RX/done.
    slave = write(0x60, 0) + write(0x10, 0xf00) + write(0, 0x1234)
    slave += write(0x3c, 1) + write(0x64, 1)
    slave += constant(1, 960) + [0xee0e1f12]  # 20 us, no external clock.
    slave += constant(1, 1) + [0xee0e1f32]
    slave += [0xee1e1f32, 0xe3110004, 0x0afffffc]
    slave += constant(0, SPI) + [0xe590402c, 0xe5905030, 0xe590603c]

    # Pending master frame is cancelled by reset; configuration survives reopen.
    # DIV 7 makes this 16-bit frame 5.334 us; reset precedes expiry.
    cancel = setup + write(8, 7) + write(0, 0x1234) + write(0x3c, 1)
    cancel += write(0x60, 1) + write(0x60, 0)
    cancel += constant(1, 960) + [0xee0e1f12]  # Beyond the old frame deadline.
    cancel += constant(1, 1) + [0xee0e1f32]
    cancel += [0xee1e1f32, 0xe3110004, 0x0afffffc]
    cancel += constant(0, SPI) + [0xe590402c, 0xe5905030, 0xe5906024,
                                 0xe590700c, 0xe590803c]

    irq = constant(2, 0xf8101000) + constant(1, 1) + [0xe5821000]
    irq += [0xe5c218c1]  # IRQ193 target CPU0, not an invented ISR call.
    irq += constant(1, 2) + [0xe5821118]
    irq += constant(3, 0xf8102000) + constant(1, 0xff) + [0xe5831004]
    irq += constant(1, 1) + [0xe5831000]
    irq += setup + write(0x28, 0x1ff) + write(0x20, 0x10)
    irq += write(0, 0x1234) + write(0x3c, 1) + constant(0, SPI)
    irq += [0xe5906024, 0xe3160010, 0x0afffffc, 0xe593400c]
    irq += write(0x28, 0x1ff) + [0xe5834010, 0xe593500c]

    overflow = setup + write(0x3c, 1)
    for index in range(33):
        overflow += write(0, index) + constant(0, SPI + 0x14)
        overflow += [0xe5901000, 0xe3110004, 0x0afffffc]
    overflow += constant(0, SPI) + [0xe5904030, 0xe5905014, 0xe5906024]
    overflow += write(0x28, 0x80) + constant(0, SPI) + [0xe5907014]

    channels = []
    for channel in range(11):
        channels += constant(0, SPI + channel * 0x100)
        channels += constant(1, 1) + [0xe5801060, 0xe5900000 | (channel + 2) << 12 | 0x48]
    return [
        ("spi-board-reset", constant(0, SPI) + [0xe5902008] +
         write(8, 7), {2: 0}, None),
        ("spi-all-eleven-channel-depths", channels,
         {reg: 32 for reg in range(2, 13)}, None),
        ("spi-common-irq-through-gic", irq, {4: 193, 5: 1023}, None),
        ("spi-real-fifo-overflow-and-ack", overflow,
         {4: 32, 5: 0x1cc, 6: 0x9f, 7: 0xcc}, None),
        ("spi-master-disconnected-transfer", transfer,
         {4: 0x17, 5: 0, 6: 0x2c, 7: 32, 8: 0}, None),
        ("spi-slave-no-external-clock", slave, {4: 1, 5: 0, 6: 1}, None),
        ("spi-reset-cancels-retains-config", cancel,
         {4: 0, 5: 0, 6: 0, 7: 0x22, 8: 0}, None),
        ("spi-unknown-width", constant(0, SPI + 0x60) + [0xe5c01000], {},
         "spi-access-or-phase-not-supported"),
        ("spi-hole", constant(0, SPI + 0x50) + [0xe5901000], {},
         "spi-access-or-phase-not-supported"),
        ("spi-dma-unsupported", write(0xc, 0x420), {},
         "spi-access-or-phase-not-supported"),
        ("spi-reset-command", write(0x60, 2), {},
         "spi-access-or-phase-not-supported"),
    ]


def exiu_fixtures():
    exiu = 0x0426f000

    def write(address, value):
        return constant(0, address) + constant(1, value) + [0xe5801000]

    def read(address, reg):
        return constant(0, address) + [0xe5900000 | reg << 12]

    # Actual GPIO5 pin7 route generates a rising transition, not a host IRQ.
    high = write(GPIO + 0x114, 0x80) + write(GPIO + 0x14, 0x00800080)
    low = write(GPIO + 0x14, 0x00800000)
    edge = high + read(exiu + 0xc, 4) + read(exiu + 8, 5)
    edge += write(exiu, 0xfffe) + read(exiu + 8, 6)
    edge += write(exiu + 0x10, 1) + read(exiu + 8, 7)
    edge += low + read(exiu + 0xc, 8)

    level = write(exiu + 0x18, 0xfffe) + write(exiu, 0xfffe) + high
    level += write(exiu + 0x10, 1) + read(exiu + 8, 4)
    level += low + read(exiu + 8, 5)
    level += write(exiu + 0x10, 1) + read(exiu + 8, 6)

    routes = write(GPIO + 0x114, 0xff80) + write(GPIO + 0x118, 0x7f)
    routes += write(GPIO + 0x14, 0xff80ff80) + write(GPIO + 0x18, 0x007f007f)
    routes += write(exiu, 0) + read(exiu + 8, 4)

    irq = write(0xf8101000, 1) + constant(2, 0xf8101000)
    irq += constant(1, 1) + [0xe5c21841]  # IRQ65 target CPU0.
    irq += write(0xf8101108, 2) + write(0xf8102004, 0xff)
    irq += write(0xf8102000, 1) + write(exiu, 0xfffe) + high
    irq += read(0xf810200c, 4) + write(exiu + 0x10, 1)
    irq += write(0xf8102010, 65) + read(0xf810200c, 5)
    falling = high + write(exiu + 0x14, 0xfffe) + write(exiu + 0x10, 1)
    falling += low + read(exiu + 0xc, 4)
    irq80 = write(0xf8101000, 1) + constant(2, 0xf8101000)
    irq80 += constant(1, 1) + [0xe5c21850]
    irq80 += write(0xf8101108, 0x10000) + write(0xf8102004, 0xff)
    irq80 += write(0xf8102000, 1) + write(exiu, 0x7fff)
    irq80 += write(GPIO + 0x118, 0x40) + write(GPIO + 0x18, 0x00400040)
    irq80 += read(0xf810200c, 4) + write(exiu + 0x10, 0x8000)
    irq80 += write(0xf8102010, 80) + read(0xf810200c, 5)
    return [
        ("exiu-board-reset", read(exiu + 0xc, 2) + high, {2: 0}, None),
        ("exiu-falling-transition", falling, {4: 1}, None),
        ("exiu-real-gic-route80", irq80, {4: 80, 5: 1023}, None),
        ("exiu-edge-mask-latch-ack", edge, {4: 1, 5: 0, 6: 1, 7: 0, 8: 0}, None),
        ("exiu-level-reassert-and-clear", level, {4: 1, 5: 1, 6: 0}, None),
        ("exiu-all-sixteen-pad-routes", routes, {4: 0xffff}, None),
        ("exiu-real-gic-route65", irq, {4: 65, 5: 1023}, None),
        ("exiu-config-no-edge", write(exiu + 0x14, 0) +
         read(exiu + 0xc, 4), {4: 0}, None),
        ("exiu-unknown-width", constant(0, exiu) + [0xe5d01000], {},
         "exiu-access-not-supported"),
        ("exiu-unknown-channel", write(exiu, 0x10000), {},
         "exiu-access-not-supported"),
        ("exiu-software-source-unsupported", write(exiu + 4, 1), {},
         "exiu-access-not-supported"),
        ("exiu-software-command-unsupported", write(exiu + 0x1c, 1), {},
         "exiu-access-not-supported"),
        ("exiu-status-write-unsupported", write(exiu + 8, 1), {},
         "exiu-access-not-supported"),
    ]


def hdmac_fixtures():
    dma = 0x2c020000
    source, destination = 0xb1800100, 0xb1800200

    def write(address, value):
        return constant(0, address) + constant(1, value) + [0xe5801000]

    def read(address, reg):
        return constant(0, address) + [0xe5900000 | reg << 12]

    def setup(cb=0x08180000, sa=source, da=destination, ca=0xa0000001):
        return (write(dma, 0x90000000) + write(dma + 0x18, sa) +
                write(dma + 0x1c, da) + write(dma + 0x14, cb) +
                write(dma + 0x10, ca))

    def wait_status():
        return constant(0, dma + 0x14) + [0xe5902000, 0xe3120807, 0x0afffffc]

    seed = write(source, 0x11223344) + write(source + 4, 0x55667788)
    copy = seed + setup() + wait_status()
    copy += read(destination, 4) + read(destination + 4, 5) + read(dma + 0x14, 6)
    copy += write(dma + 0x14, 0x08000000) + read(dma + 0x14, 7)
    fixed = seed + setup(cb=0x0a180000) + wait_status()
    fixed += read(destination, 4) + read(destination + 4, 5)
    fixed += read(dma + 0x18, 6) + read(dma + 0x1c, 7)
    external = setup(cb=0x28180000, ca=0x81000001)
    external += read(dma + 0x14, 4) + read(dma + 0x10, 5) + read(destination, 6)
    cancel = seed + setup(ca=0xa000ffff) + write(dma + 0x10, 0x2000ffff)
    cancel += read(dma + 0x14, 4) + read(destination, 5)
    globals = write(dma, 0x10000000) + read(dma, 4)
    globals += write(dma + 0x10000, 0x90000000) + read(dma + 0x10000, 5)
    globals += write(dma + 0x80, 7) + read(dma + 0x80, 6)
    globals += write(dma + 0x10080, 9) + read(dma + 0x10080, 7)

    def arbitration(rotating):
        code = seed + write(dma, 0x90000000)
        # Warm-up channel1 leaves rotating cursor2. Fixed mode ignores it.
        code += write(dma + 0x28, source) + write(dma + 0x2c, destination)
        code += write(dma + 0x24, 0x08180000) + write(dma + 0x20, 0xa0000000)
        code += constant(0, dma + 0x24) + [0xe5902000, 0xe3120807, 0x0afffffc]
        code += write(dma, 0)
        for channel, sa in ((1, source), (3, source + 4)):
            base = dma + 0x10 + channel * 0x10
            code += write(base + 8, sa) + write(base + 12, destination)
            code += write(base + 4, 0x08180000) + write(base, 0xa0000000)
        code += write(dma, 0x90000000 if rotating else 0x80000000)
        for channel in (1, 3):
            code += constant(0, dma + 0x14 + channel * 0x10)
            code += [0xe5902000, 0xe3120807, 0x0afffffc]
        return code + read(destination, 4)

    irq = write(0xf8101000, 1) + constant(2, 0xf8101000)
    irq += constant(1, 1) + [0xe5c21896]  # IRQ150 targets CPU0.
    irq += write(0xf8101110, 0x400000) + write(0xf8102004, 0xff)
    irq += write(0xf8102000, 1) + seed + setup() + wait_status()
    irq += read(0xf810200c, 4) + write(dma + 0x14, 0x08000000)
    irq += write(0xf8102010, 150) + read(0xf810200c, 5)
    narrow = seed + setup(cb=0x00180000, ca=0xa0000003) + wait_status()
    narrow += read(destination, 4)
    narrow += setup(cb=0x04180000, da=destination + 4) + wait_status()
    narrow += read(destination + 4, 5)

    def armed_byte(external=False):
        return (write(dma, 0) + write(dma + 0x18, source + 1) +
                write(dma + 0x1c, destination + 1) + write(dma + 0x14, 0) +
                write(dma + 0x10, 0x81000000 if external else 0xa0000000))

    recovered = seed + armed_byte() + write(dma + 0x18, source)
    recovered += write(dma + 0x1c, destination) + write(dma + 0x14, 0x08000000)
    recovered += write(dma, 0x80000000) + wait_status() + read(destination, 4)
    self_cancel = write(0xf8101000, 1) + constant(2, 0xf8101000)
    self_cancel += constant(1, 1) + [0xe5c21896]
    self_cancel += write(0xf8101110, 0x400000) + write(0xf8102004, 0xff)
    self_cancel += write(0xf8102000, 1) + write(source, 0)
    self_cancel += setup(cb=0x08080000, da=dma, ca=0xa0000000)
    self_cancel += wait_status() + read(dma + 0x10, 4) + read(dma + 0x14, 5)
    self_cancel += read(dma, 6) + read(0xf810200c, 7)
    self_eb = write(source, 0x20000001) + write(source + 4, 0x08000000)
    self_eb += setup(cb=0x08080000, da=dma + 0x10) + wait_status()
    self_eb += read(dma + 0x10, 4) + read(dma + 0x14, 5)
    return [
        ("hdmac-armed-alignment-rejected", armed_byte() +
         write(dma + 0x14, 0x08000000), {},
         "hdmac-access-or-command-not-supported"),
        ("hdmac-external-armed-alignment-rejected", armed_byte(True) +
         write(dma + 0x14, 0x08000000), {},
         "hdmac-access-or-command-not-supported"),
        ("hdmac-armed-alignment-recovered", recovered, {4: 0x11223344}, None),
        ("hdmac-self-global-disable-cancels", self_cancel,
         {4: 0x20000000, 5: 0x080a0000, 6: 0, 7: 1023}, None),
        ("hdmac-self-eb-clear-stops-next-beat", self_eb,
         {4: 0x20000001, 5: 0x080a0000}, None),
        ("hdmac-fixed-arbitration", arbitration(False), {4: 0x55667788}, None),
        ("hdmac-rotating-arbitration", arbitration(True), {4: 0x11223344}, None),
        ("hdmac-real-gic-irq150", irq, {4: 150, 5: 1023}, None),
        ("hdmac-byte-and-halfword-counts", narrow,
         {4: 0x11223344, 5: 0x11223344}, None),
        ("hdmac-board-reset", read(dma + 0x14, 2) + seed + setup(), {2: 0}, None),
        ("hdmac-real-word-copy-and-ack", copy,
         {4: 0x11223344, 5: 0x55667788, 6: 0x081d0000, 7: 0x08000000}, None),
        ("hdmac-fixed-source", fixed,
         {4: 0x11223344, 5: 0x11223344, 6: source, 7: destination + 8}, None),
        ("hdmac-external-request-does-not-complete", external,
         {4: 0x28180000, 5: 0x81000001, 6: 0}, None),
        ("hdmac-cancel-before-copy", cancel, {4: 0x081a0000, 5: 0}, None),
        ("hdmac-both-banks-last-channel", globals,
         {4: 0x10000000, 5: 0x90000000, 6: 7, 7: 9}, None),
        ("hdmac-source-bus-error", setup(sa=0xdead0000) + wait_status() +
         read(dma + 0x14, 4), {}, "uncovered-address"),
        ("hdmac-destination-bus-error", seed + setup(da=0xdead0000) +
         wait_status() + read(dma + 0x14, 4), {}, "uncovered-address"),
        ("hdmac-unknown-width", constant(0, dma) + [0xe5d01000], {},
         "hdmac-access-or-command-not-supported"),
        ("hdmac-hole", read(dma + 4, 4), {},
         "hdmac-access-or-command-not-supported"),
        ("hdmac-ds-unsupported", write(dma, 0x40000000), {},
         "hdmac-access-or-command-not-supported"),
        ("hdmac-reload-unsupported", write(dma + 0x14, 0x00800000), {},
         "hdmac-access-or-command-not-supported"),
        ("hdmac-transform-unsupported", write(dma + 0x14, 0x40000000), {},
         "hdmac-access-or-command-not-supported"),
    ]


def record_slot_fixtures():
    readback = constant(0, 0x12e00000) + [0xe5902000, 0xe5904004]
    readback += constant(0, 0x12e3fffc) + [0xe5905000]
    return [
        ("record-slot-erased-header-and-end", readback,
         {2: 0xffffffff, 4: 0xffffffff, 5: 0xffffffff}, None),
        ("record-slot-board-reset", readback,
         {2: 0xffffffff, 4: 0xffffffff, 5: 0xffffffff}, None),
        ("record-slot-alternate-erased", constant(0, 0x131e0000) +
         [0xe5904000, 0xe5905004] + constant(0, 0x1321fffc) + [0xe5906000],
         {4: 0xffffffff, 5: 0xffffffff, 6: 0xffffffff}, None),
        ("record-slot-alternate-board-reset", constant(0, 0x131e0000) +
         [0xe5902000] + constant(0, 0x1321fffc) + [0xe5904000],
         {2: 0xffffffff, 4: 0xffffffff}, None),
        ("record-slot-alternate-before-memc", constant(0, 0x131e0000) +
         [0xe1c010b0], {}, "nor-memc-not-initialized"),
        ("record-slot-alternate-protected", initialized(0x29) +
         constant(0, 0x131e0000) + [0xe1c010b0], {}, "nor-write-protected"),
        ("record-slot-alternate-command-unsupported", initialized() +
         constant(0, 0x131e0aaa) + constant(1, 0xaa) + [0xe1c010b0], {},
         "nor-backend-not-supported"),
        ("record-slot-before-memc", constant(0, 0x12e00000) + [0xe1c010b0], {},
         "nor-memc-not-initialized"),
        ("record-slot-write-protected", initialized(0x29) +
         constant(0, 0x12e00000) + [0xe1c010b0], {}, "nor-write-protected"),
        ("record-slot-command-unsupported", initialized() +
         constant(0, 0x12e00aaa) + constant(1, 0xaa) + [0xe1c010b0], {},
         "nor-backend-not-supported"),
    ]


def footer_slot_fixtures():
    base = 0x12a20000
    readback = constant(0, base) + [0xe5902000]
    readback += constant(0, base + 0x1fffc) + [0xe5904000]
    return [
        ("record-footer-erased-first-and-last", readback,
         {2: 0xffffffff, 4: 0xffffffff}, None),
        ("record-footer-board-reset", readback,
         {2: 0xffffffff, 4: 0xffffffff}, None),
        ("record-footer-before-memc", constant(0, base) + [0xe1c010b0], {},
         "nor-memc-not-initialized"),
        ("record-footer-protected", initialized(0x29) + constant(0, base) +
         [0xe1c010b0], {}, "nor-write-protected"),
        ("record-footer-program-unsupported", initialized() +
         constant(0, base + 0xaaa) + constant(1, 0xaa) + [0xe1c010b0], {},
         "nor-backend-not-supported"),
    ]


def external_nor_fixtures():
    manifest = json.loads((Path(__file__).resolve().parents[1] /
                           "tests/nor-external-profile.json").read_text())
    views = []
    for row in manifest["descriptors"]:
        if row[0] in manifest["excluded_descriptors"]:
            continue
        for start in map(lambda word: int(word, 16), row[3:5]):
            if start:
                views.append((start, start + int(row[5], 16)))
    unions = []
    for start, end in sorted(set(views)):
        if unions and start <= unions[-1][1]:
            unions[-1][1] = max(unions[-1][1], end)
        else:
            unions.append([start, end])
    assert unions == [[int(start, 16), int(end, 16)] for start, end in
                      manifest["canonical_physical_windows"]]
    assert len(views) == 35 and len(set(views)) == 34
    assert sum(end - start for start, end in unions) == 22544384
    code = constant(5, 0xffffffff)
    for start, end in views:
        for address in (start, end - 4):
            code += constant(0, address) + [0xe5904000, 0xe1540005, 0x1afffffe]
    reset = constant(0, unions[0][0]) + [0xe5902000]
    reset += constant(0, unions[-1][1] - 4) + [0xe5904000]
    result = [
        ("external-nor-whole-descriptor-views", code, {4: 0xffffffff}, None),
        ("external-nor-board-reset", reset,
         {2: 0xffffffff, 4: 0xffffffff}, None),
        # Descriptor4 has zero span, but descriptor3 independently covers it.
        ("external-nor-zero-view-covered-by-parent", constant(0, 0x129a0000) +
         [0xe5904000], {4: 0xffffffff}, None),
        ("external-nor-excluded-loader", constant(0, 0x10000000) +
         [0xe5901000], {}, "uncovered-address"),
        ("external-nor-excluded-missing-image-tail", constant(0, 0x12497b78) +
         [0xe5901000], {}, "uncovered-address"),
        ("external-nor-no-writer", initialized() + constant(0, 0x13e80aaa) +
         constant(1, 0xaa) + [0xe1c010b0], {}, "nor-backend-not-supported"),
    ]
    for index, (start, end) in enumerate(unions):
        for label, address in (("below", start - 4), ("above", end)):
            result.append((f"external-nor-window{index}-{label}",
                           constant(0, address) + [0xe5901000], {},
                           "uncovered-address"))
        result.append((f"external-nor-window{index}-crossing-end",
                       constant(0, end - 2) + [0xe5901000], {},
                       "uncovered-address"))
    return result


def i2c_fixtures():
    base = 0x2c018000

    def write(offset, value, bank=0):
        return constant(0, base + bank * 0x1000 + offset) + constant(1, value) + [0xe5801000]

    def read(offset, reg, bank=0):
        return constant(0, base + bank * 0x1000 + offset) + [0xe5900000 | reg << 12]

    def wait(mask, bank=0):
        return constant(0, base + bank * 0x1000 + 0x10) + [0xe5902000, 0xe3120000 | mask, 0x0afffffc]

    nack = write(0, 6) + write(0x24, 4) + write(8, 0x50) + wait(4)
    nack += read(0x10, 4) + read(0x20, 5) + read(4, 6)
    nack += write(0x10, 4) + read(0x10, 7)
    geometry = []
    for bank in range(4):
        geometry += write(0x28, 0x2ff, bank) + write(0, 6, bank)
        geometry += write(8, 0x50, bank) + wait(4, bank) + read(0x10, 4 + bank, bank)
    endpoint = write(0, 6)
    for byte in (0x10, 0x5a, 0xa5):
        endpoint += write(0xc, byte)
    endpoint += write(8, 0x50) + wait(1) + write(0x10, 3)
    # Hold the real bus across pointer TX and repeated START RX.
    endpoint += write(0, 0x16) + write(0xc, 0x10) + write(8, 0x50) + wait(2)
    endpoint += read(4, 4) + write(0x10, 3) + write(0, 0x17)
    endpoint += write(0x14, 2) + write(8, 0x50) + write(0, 7) + wait(1)
    endpoint += read(0xc, 5) + read(0xc, 6) + read(4, 7)
    fifo = write(0, 6)
    for byte in range(17):
        fifo += write(0xc, byte)
    fifo += read(0x14, 4) + read(0x10, 5) + write(0, 0x46)
    fifo += read(0x14, 6) + read(0xc, 7) + read(0x10, 8)
    cancel = write(0, 6) + write(8, 0x50) + write(0, 0x46)
    cancel += read(0x10, 4) + read(4, 5)
    timeout = write(0, 0x16) + write(0x1c, 2) + write(8, 0x50) + wait(8)
    timeout += read(0x10, 4) + read(4, 5)
    pending_release = write(0, 0x16) + write(8, 0x50) + write(0, 6) + wait(4)
    pending_release += read(0x10, 4) + read(4, 5)

    def delay(ticks):
        return (constant(1, ticks) + [0xee0e1f12] + constant(1, 1) +
                [0xee0e1f32, 0xee1e1f32, 0xe3110004, 0x0afffffc])

    # At 48 MHz / 22, timeout 15 expires after 7.334 us. The 20 us
    # observation must cross that old deadline after disabling it.
    timeout_disable = write(0, 0x16) + write(0x1c, 15) + write(8, 0x50) + wait(2)
    timeout_disable += write(0x1c, 0) + delay(960)
    timeout_disable += read(0x10, 4) + read(4, 5)
    timeout_disable += write(0x1c, 2) + wait(8) + read(0x10, 6) + read(4, 7)
    timeout_extend = write(0, 0x16) + write(0x1c, 2) + write(8, 0x50) + wait(2)
    # Replacement 31 gives 14.667 us: sample at 10 us, after the old
    # timeout 2 deadline (1.375 us), before the replacement deadline.
    timeout_extend += write(0x1c, 31) + delay(480) + read(0x10, 4)
    timeout_extend += read(4, 5) + wait(8) + read(0x10, 6) + read(4, 7)
    irq = write(0, 6, 3) + write(0x24, 4, 3)
    irq += constant(0, 0xf8101000) + constant(1, 1) + [0xe5801000]
    irq += constant(2, 0xf8101000) + [0xe5c218d9]  # IRQ217 -> CPU0.
    irq += constant(0, 0xf8101118) + constant(1, 1 << 25) + [0xe5801000]
    irq += constant(0, 0xf8102004) + constant(1, 0xff) + [0xe5801000]
    irq += constant(0, 0xf8102000) + constant(1, 1) + [0xe5801000]
    irq += write(8, 0x50, 3) + wait(4, 3)
    irq += constant(0, 0xf810200c) + [0xe5904000]
    irq += write(0x10, 4, 3)
    irq += constant(0, 0xf8102010) + constant(1, 217) + [0xe5801000]
    irq += constant(0, 0xf810200c) + [0xe5905000]
    return [
        ("i2c-disconnected-nack-and-w1c", nack, {4: 4, 5: 0x2fb, 6: 0, 7: 0}, None),
        ("i2c-four-instance-idr-and-nack", geometry, {4: 4, 5: 4, 6: 4, 7: 4}, None),
        ("i2c-endpoint-tx-rx-repeated-start", endpoint, {4: 0x100, 5: 0x5a, 6: 0xa5, 7: 8}, None),
        ("i2c-endpoint-starvation-timeout", timeout, {4: 10, 5: 0}, None),
        ("i2c-pending-hold-release-real-nack", pending_release, {4: 4, 5: 0}, None),
        ("i2c-endpoint-timeout-disable-reenable", timeout_disable,
         {4: 2, 5: 0x100, 6: 10, 7: 0}, None),
        ("i2c-endpoint-timeout-deadline-replaced", timeout_extend,
         {4: 2, 5: 0x100, 6: 10, 7: 0}, None),
        ("i2c-fifo-overflow-underflow-clear", fifo, {4: 15, 5: 0x40, 6: 0, 7: 0, 8: 0xc0}, None),
        ("i2c-cancel-before-address", cancel, {4: 0, 5: 0}, None),
        ("i2c-real-gic-irq217", irq, {4: 217, 5: 1023}, None),
        ("i2c-board-reset", read(0, 2) + write(0, 6), {2: 0}, None),
        ("i2c-slave-no-external-master", write(0, 4) + write(8, 0x50) + read(4, 4) + read(0x10, 5), {4: 0, 5: 0}, None),
        ("i2c-monitor-unsupported", write(0, 0x26) + write(8, 0x50), {}, "i2c-access-or-variant-not-supported"),
        ("i2c-bit7-unsupported", write(0, 0x80, 3), {}, "i2c-access-or-variant-not-supported"),
        ("i2c-extended-address-unsupported", write(0, 2) + write(8, 0x50), {}, "i2c-access-or-variant-not-supported"),
        ("i2c-hole-unsupported", read(0x2c, 4), {}, "i2c-access-or-variant-not-supported"),
        ("i2c-width-unsupported", constant(0, base) + [0xe5d01000], {}, "i2c-access-or-variant-not-supported"),
        ("i2c-pending-direction-change-rejected", write(0, 6) + write(8, 0x50) + write(0, 7), {}, "i2c-access-or-variant-not-supported"),
    ]


def timing_fixtures():
    a, b = 0x2c010000, 0x2c011000

    def write(address, value):
        return constant(0, address) + constant(1, value) + [0xe5801000]

    def read(address, reg):
        return constant(0, address) + [0xe5900000 | reg << 12]

    # Independent 3 GHz timing device: a 30,000-tick period is 10 us.
    # Settle for 20 us using the upstream 48 MHz architectural timer.
    def delay(ticks=960):
        return (constant(1, ticks) + [0xee0e1f12] + constant(1, 1) +
                [0xee0e1f32, 0xee1e1f32, 0xe3110004, 0x0afffffc])

    def setup(channel=0, opcode=0x31, period=29999):
        offset = 0x100 + channel * 0x100
        return (write(a + 4, 1) + write(b + offset, 1) +
                write(b + offset, 0) + write(b + offset + 4, opcode) +
                write(b + offset + 8, period) + write(a + offset, 1) +
                write(b, 1 << channel))

    def wait(channel=0):
        return (constant(0, a + 0x110 + channel * 0x100) +
                [0xe5902000, 0xe3120001, 0x0afffffc])

    once = setup() + wait() + read(b + 0x10c, 4)
    once += write(a + 0x110, 1) + delay() + read(a + 0x110, 5)
    periodic = setup(opcode=0) + wait() + write(a + 0x110, 1)
    periodic += wait() + read(a + 0x110, 4)
    paused = setup() + write(b, 0) + read(b + 0x10c, 4)
    paused += delay() + read(b + 0x10c, 5)
    paused += [0xe1540005, 0x03a06001, 0x13a06000] + read(a + 0x110, 7)
    reset = setup() + write(a, 1) + write(a, 0)
    reset += write(a + 4, 1) + write(b, 1) + delay()
    reset += read(a + 0x110, 4) + read(b + 0x108, 5)
    irq = write(0xf8101000, 1) + constant(2, 0xf8101000)
    irq += constant(1, 1) + [0xe5c218b7]
    irq += write(0xf8101114, 0x800000) + write(0xf8102004, 0xff)
    irq += write(0xf8102000, 1) + setup(11) + wait(11)
    irq += read(0xf810200c, 4) + write(a + 0xc10, 1)
    irq += write(0xf8102010, 183) + read(0xf810200c, 5)
    disabled = setup() + write(a + 0x100, 0) + delay()
    disabled += read(a + 0x110, 4)
    # Source1 is free-running with a once-per-programmed-threshold report.
    # MAX encodes 2**32 ticks; a short observation must not wrap it to zero.
    direct = setup(5, 1, 0xffffffff) + delay()
    direct += read(a + 0x610, 4) + read(b + 0x60c, 5)
    direct += [0xe3550000, 0x13a06001, 0x03a06000]
    direct += read(b + 0x604, 7) + read(b + 0x608, 8)
    threshold = setup(5, 1) + wait(5) + write(a + 0x610, 1) + delay()
    threshold += read(a + 0x610, 4) + read(b + 0x60c, 5)
    threshold += constant(0, 30000) + [0xe1550000, 0x83a06001, 0x93a06000]
    gates = setup(5, 1, 0xffffffff) + write(b, 0) + read(b + 0x60c, 4)
    gates += delay() + read(b + 0x60c, 5)
    gates += [0xe1540005, 0x03a06001, 0x13a06000]
    gates += write(b, 0x20) + delay() + write(a + 4, 0) + read(b + 0x60c, 7)
    gates += delay() + read(b + 0x60c, 8)
    gates += [0xe1570008, 0x03a09001, 0x13a09000, 0xe1570004,
              0x83a0a001, 0x93a0a000]
    local = setup(5, 1) + wait(5) + write(a + 0x610, 1)
    local += write(a + 0x600, 0) + read(b + 0x60c, 4) + delay()
    local += read(b + 0x60c, 5) + [0xe1540005, 0x03a06001, 0x13a06000]
    local += write(a + 0x600, 1) + delay() + read(a + 0x610, 7)
    local += read(b + 0x60c, 8) + [0xe1580005, 0x83a09001, 0x93a09000]
    reprogram = setup(5, 1) + wait(5) + write(a + 0x610, 1)
    reprogram += write(b + 0x608, 59999) + wait(5) + read(a + 0x610, 4)
    reprogram += write(a + 0x610, 1) + delay() + read(a + 0x610, 5)
    reset_direct = setup(5, 1) + write(b + 0x600, 1) + write(b + 0x600, 0)
    reset_direct += delay() + read(a + 0x610, 4) + read(b + 0x60c, 5)
    direct_irq = write(0xf8101000, 1) + constant(2, 0xf8101000)
    direct_irq += constant(1, 1) + [0xe5c218b1]
    direct_irq += write(0xf8101114, 0x20000) + write(0xf8102004, 0xff)
    direct_irq += write(0xf8102000, 1) + setup(5, 1) + wait(5)
    direct_irq += read(0xf810200c, 4) + write(a + 0x610, 1)
    direct_irq += write(0xf8102010, 177) + read(0xf810200c, 5)
    error = "timing-access-or-feature-not-supported"
    return [
        ("timing-source1-max-is-not-immediate", direct,
         {4: 0, 6: 1, 7: 1, 8: 0xffffffff}, None),
        ("timing-source1-threshold-once-keeps-counting", threshold,
         {4: 0, 6: 1}, None),
        ("timing-source1-bitmap-and-master-pause", gates,
         {6: 1, 9: 1, 10: 1}, None),
        ("timing-source1-local-resume-no-repeat", local,
         {6: 1, 7: 0, 9: 1}, None),
        ("timing-source1-reprogram-new-threshold", reprogram,
         {4: 1, 5: 0}, None),
        ("timing-source1-reset-cancels-threshold", reset_direct,
         {4: 0, 5: 0}, None),
        ("timing-source1-real-irq177", direct_irq, {4: 177, 5: 1023}, None),
        ("timing-source1-opcode-reject-preserves-state",
         setup(5, 1, 0xffffffff) + write(b + 0x604, 2), {}, error),
        ("timing-source1-active-change-reject-preserves-state",
         setup(5, 1, 0xffffffff) + write(b + 0x604, 0x31), {}, error),
        ("timing-source1-capture-value-reject-preserves-state",
         setup(5, 1, 0xffffffff) + write(b + 0x610, 2), {}, error),
        ("timing-source1-capture-mode-reject-preserves-state",
         setup(5, 1, 0xffffffff) + write(b + 0x610, 1) + write(a + 0x608, 0x20),
         {}, error),
        ("timing-source1-capture-armed-opcode-reject-preserves-state",
         setup(5, 1, 0xffffffff) + write(b + 0x610, 1) + write(b, 0) +
         write(b + 0x604, 0), {}, error),
        ("timing-capture-source-preflight", setup(5, 0x31, 0xffffffff) +
         write(b + 0x610, 1), {}, error),
        ("timing-capture-result-readonly", write(b + 0x614, 0), {}, error),
        ("timing-capture-absent-capability", read(b + 0x114, 4), {}, error),
        ("timing-source1-bitmap-reject-preserves-state",
         setup(5, 1, 0xffffffff) + write(b, 0x1020), {}, error),
        ("timing-one-shot-expiry-ack", once, {4: 30000, 5: 0}, None),
        ("timing-periodic-expiry-after-ack", periodic, {4: 1}, None),
        ("timing-bitmap-pauses-counter", paused, {6: 1, 7: 0}, None),
        ("timing-reset-cancels-pending", reset, {4: 0, 5: 0}, None),
        ("timing-last-channel-real-irq183", irq, {4: 183, 5: 1023}, None),
        ("timing-local-control-stops", disabled, {4: 0}, None),
        ("timing-board-reset", read(a + 4, 2) + write(a + 4, 1), {2: 0}, None),
        ("timing-unbound-opcode", write(b + 0x104, 2), {},
         "timing-access-or-feature-not-supported"),
        ("timing-optional-feature-enable", write(b + 0x124, 1), {},
         "timing-access-or-feature-not-supported"),
        ("timing-feature-hole", write(b + 0xc24, 0), {},
         "timing-access-or-feature-not-supported"),
        ("timing-shared-submit-unknown", write(b + 8, 1), {},
         "timing-access-or-feature-not-supported"),
        ("timing-counter-readonly", write(b + 0x10c, 0), {},
         "timing-access-or-feature-not-supported"),
        ("timing-unknown-width", constant(0, a) + [0xe5d01000], {},
         "timing-access-or-feature-not-supported"),
    ] + [("timing-capture-input-" + name, [], {}, None) for name in (
        "disconnected", "edges-and-irq-output", "two-channels-disable",
        "gates-no-config-edge", "reset-retains-external-level",
        "period-reprogram-clears-samples")]


def audio_fixtures():
    audio, dma, source = 0x2c080000, 0x2c030000, 0xb1800100

    def write(address, value):
        return constant(0, address) + constant(1, value) + [0xe5801000]

    def read(address, reg):
        return constant(0, address) + [0xe5900000 | reg << 12]

    # A virtual audio word drains in 64 / 48 MHz = 1.334 us. Even the
    # two-channel fixture drains within 20 us, including DMA beat delays.
    def delay(ticks=960):
        return (constant(1, ticks) + [0xee0e1f12] + constant(1, 1) +
                [0xee0e1f32, 0xee1e1f32, 0xe3110004, 0x0afffffc])

    def request(channel=0, selector=3, destination=audio + 0x13c):
        base = dma + 0x10 + channel * 16
        return (write(dma, 0x80000000) + write(base + 8, source) +
                write(base + 12, destination) + write(base + 4, 0x29180000) +
                write(base, 0x80000001 | selector << 24))

    seed = write(source, 0x11223344) + write(source + 4, 0x55667788)
    enabled = write(audio + 0x108, 0xa000)
    pressure = seed + enabled + request() + delay()
    pressure += read(dma + 0x14, 4) + read(dma + 0x18, 5)
    pressure += read(audio + 0x120, 6)
    resumed = pressure + write(audio + 0x114, 0x40000000) + delay()
    resumed += read(dma + 0x14, 7) + read(dma + 0x10, 8)
    canceled = pressure + write(dma + 0x10, 0x03000001)
    canceled += write(audio + 0x114, 0x40000000) + delay()
    canceled += read(dma + 0x14, 7) + read(dma + 0x18, 8)
    reset = pressure + write(audio, 1) + write(audio, 0) + delay()
    reset += read(dma + 0x14, 7) + read(audio + 0x120, 8)
    reset += enabled + write(audio + 0x114, 0x40000000) + delay()
    reset += read(dma + 0x14, 9)
    deassert = pressure + write(audio + 0x108, 0)
    deassert += write(audio + 0x114, 0x40000000) + delay()
    deassert += read(dma + 0x14, 7) + read(dma + 0x18, 8)
    multiple = seed + enabled + request() + request(1) + delay()
    multiple += read(dma + 0x14, 4) + read(dma + 0x24, 5)
    multiple += write(audio + 0x114, 0x40000000) + delay()
    multiple += read(dma + 0x14, 6) + read(dma + 0x24, 7)
    drain = write(audio + 0x10c, 0x04800000) + enabled
    drain += write(audio + 0x104, 0xabcdef01) + read(audio + 0x120, 4)
    drain += write(audio + 0x114, 0x40000000) + delay()
    drain += read(audio + 0x120, 5) + read(audio + 0x110, 6)
    drain += write(audio + 0x108, 0) + write(audio + 0x110, 0)
    drain += read(audio + 0x110, 7)
    rx_wait = seed + write(audio + 0x108, 0x5000)
    rx_wait += request(selector=5, destination=audio + 0x13c) + delay()
    rx_wait += read(dma + 0x14, 4) + read(dma + 0x18, 5)
    irq = write(0xf8101000, 1) + constant(2, 0xf8101000)
    irq += constant(1, 1) + [0xe5c218a7]
    irq += write(0xf8101114, 0x80) + write(0xf8102004, 0xff)
    irq += write(0xf8102000, 1) + write(audio + 0x108, 0x2800)
    irq += read(0xf810200c, 4) + write(audio + 0x108, 0x2000)
    irq += write(0xf8102010, 167) + read(0xf810200c, 5)
    capacity = write(audio + 0x10c, 0x02000000)
    for word in range(16):
        capacity += write(audio + 0x104, word)
    separate = write(audio + 0x108, 0x2002)
    separate += write(audio + 0x130, 0x12345678) + read(audio + 0x120, 4)
    separate += write(audio + 0x134, 0x87654321) + read(audio + 0x120, 5)
    separate += write(audio + 0x114, 0x40000000) + delay()
    separate += read(audio + 0x120, 6)
    return [
        ("audio-stereo-rejects-prior-state", write(audio + 0x10c, 0x07800000) +
         write(audio + 0x10c, 0x0f800000), {},
         "audio-access-or-profile-not-supported"),
        ("audio-real-gic-irq167", irq, {4: 167, 5: 1023}, None),
        ("audio-sixteen-stage-capacity", capacity + read(audio + 0x120, 4),
         {4: 0xffffffff}, None),
        ("audio-sixteen-stage-full-rejects-write", capacity +
         write(audio + 0x104, 17), {}, "audio-access-or-profile-not-supported"),
        ("audio-separate-opaque-pair-drain", separate, {4: 1, 5: 3, 6: 0}, None),
        ("audio-fifo-pressure-pauses-demand", pressure,
         {4: 0x29180000, 5: source + 4, 6: 3}, None),
        ("audio-real-clock-resumes-demand", resumed,
         {7: 0x291d0000, 8: 0x03000000}, None),
        ("audio-cancel-paused-demand", canceled,
         {7: 0x291a0000, 8: source + 4}, None),
        ("audio-reset-deasserts-and-resumes-demand", reset,
         {7: 0x29180000, 8: 0, 9: 0x291d0000}, None),
        ("audio-disabled-request-remains-paused", deassert,
         {7: 0x29180000, 8: source + 4}, None),
        ("audio-multiple-channel-fifo-pressure", multiple,
         {4: 0x29180000, 5: 0x29180000, 6: 0x291d0000, 7: 0x291d0000}, None),
        ("audio-opaque-fifo-drain-and-w0c", drain,
         {4: 3, 5: 0, 6: 0x2020000, 7: 0x2000000}, None),
        ("audio-disconnected-input-no-demand", rx_wait,
         {4: 0x29180000, 5: source}, None),
        ("audio-board-reset", read(audio, 2) + write(audio, 3), {2: 0}, None),
        ("audio-extension-unsupported", read(audio + 0x15c, 4), {},
         "audio-access-or-profile-not-supported"),
        ("audio-empty-input-unsupported", read(audio + 0x100, 4), {},
         "audio-access-or-profile-not-supported"),
        ("audio-unknown-width", constant(0, audio) + [0xe5d01000], {},
         "audio-access-or-profile-not-supported"),
    ]


def copy_fixtures():
    base, source, destination = 0x04251000, 0xb1800100, 0xb1800400

    def write(address, value):
        return constant(0, address) + constant(1, value) + [0xe5801000]

    def read(address, reg):
        return constant(0, address) + [0xe5900000 | reg << 12]

    def setup(channel=0, length=7, sa=source, da=destination):
        regs = base + 0x10 + channel * 0x30
        code = write(base, 0x10000000)
        for offset, value in ((0, length - 1), (4, sa), (8, da),
                              (12, 0x30f00), (16, 0x30f00), (20, 0),
                              (24, 0x9000)):
            code += write(regs + offset, value)
        return code

    def start(channel=0):
        return write(base + 0x28 + channel * 0x30, 0x11109003)

    def wait(channel=0):
        return constant(0, base + 0x30 + channel * 0x30) + [
            0xe5902000, 0xe312000f, 0x0afffffc]

    def delay():
        # 513 bytes in <=256-byte chunks finish in about 10.7 us if
        # cancellation fails. Observe after that deadline, not at 1 ms.
        return (constant(1, 960) + [0xee0e1f12] + constant(1, 1) +
                [0xee0e1f32, 0xee1e1f32, 0xe3110004, 0x0afffffc])

    seed = write(source, 0x11223344) + write(source + 4, 0x55667788)
    unaligned = seed + setup(sa=source + 1, da=destination + 1, length=6)
    unaligned += start() + wait() + read(destination, 4)
    unaligned += read(destination + 4, 5)
    exact = seed + setup() + start() + wait()
    exact += read(destination, 4) + read(destination + 4, 5)
    exact += read(base + 0x30, 6) + read(base + 0x10, 7)
    exact += write(base + 0x30, 0) + read(base + 0x30, 8)
    cancelled = seed + setup(length=513) + start() + write(base, 0) + delay()
    cancelled += read(destination, 4) + read(base + 0x30, 5)
    channels = seed + setup(0) + setup(3, da=destination + 16)
    channels += start(0) + start(3) + wait(0) + wait(3)
    channels += read(destination, 4) + read(destination + 16, 5)
    irq = write(0xf8101000, 1) + constant(2, 0xf8101000)
    irq += constant(1, 1) + [0xe5c21830]  # IRQ48 CPU0 target byte.
    irq += write(0xf8101104, 0x10000) + write(0xf8102004, 0xff)
    irq += write(0xf8102000, 1) + seed + setup(3) + start(3) + wait(3)
    irq += read(0xf810200c, 4) + write(base + 0xc0, 0)
    irq += write(0xf8102010, 48) + read(0xf810200c, 5)
    reset = read(base, 2) + setup(length=513) + start()
    chunks = seed + write(source + 512, 0xabcdef01) + setup(length=513)
    chunks += start() + wait() + read(destination, 4)
    chunks += read(destination + 512, 5) + read(base + 0x14, 6)
    selectors = seed + setup() + write(base, 0x11100000) + read(base, 4)
    selectors += read(base + 0x30, 5) + start() + wait()
    selectors += read(destination, 6) + read(base, 7)
    cases = [
        ("copy-default-byte-tail-and-ack", exact,
         {4: 0x11223344, 5: 0x00667788, 6: 8, 7: 0, 8: 0}, None),
        ("copy-unaligned-byte-addresses", unaligned,
         {4: 0x11223300, 5: 0x00667788}, None),
        ("copy-independent-first-last-channels", channels,
         {4: 0x11223344, 5: 0x11223344}, None),
        ("copy-multiple-chunks-exact-final-byte", chunks,
         {4: 0x11223344, 5: 1, 6: source + 513}, None),
        ("copy-global-selectors-retained-no-submit", selectors,
         {4: 0x11100000, 5: 0, 6: 0x11223344, 7: 0x11100000}, None),
        ("copy-real-gic-irq48", irq, {4: 48, 5: 1023}, None),
        ("copy-global-disable-cancels", cancelled, {4: 0, 5: 1}, None),
        ("copy-board-reset", reset, {2: 0}, None),
        ("copy-unmapped-source-causal-error", setup(sa=0xdead0000) + start() +
         wait() + read(base + 0x30, 4) + read(destination, 5),
         {4: 1, 5: 0}, None),
        ("copy-unmapped-destination-causal-error", seed + setup(da=0xdead0000) +
         start() + wait() + read(base + 0x30, 4), {4: 1}, None),
        ("copy-mmio-source-not-default-memory", setup(sa=base) + start() +
         wait() +
         read(base + 0x30, 4), {4: 1}, None),
        ("copy-fixed-pattern-unsupported", setup() +
         write(base + 0x20, 0x30f04),
         {}, "copy-access-or-profile-not-supported"),
        ("copy-start-before-config", write(base, 0x10000000) + start(), {},
         "copy-access-or-profile-not-supported"),
        ("copy-overflow-preflight", setup(length=7, da=0xfffffffc) + start(),
         {},
         "copy-access-or-profile-not-supported"),
        ("copy-unknown-channel-hole", read(base + 0x34, 4), {},
         "copy-access-or-profile-not-supported"),
        ("copy-unknown-width", constant(0, base) + [0xe5d01000], {},
         "copy-access-or-profile-not-supported"),
        ("copy-unknown-global-selector", write(base, 0x11100001), {},
         "copy-access-or-profile-not-supported"),
    ]
    return cases


def bridge_fixtures():
    base = 0x20130000

    def write(offset, value):
        return constant(0, base + offset) + constant(1, value) + [0xe5801000]

    def read(offset, reg):
        return constant(0, base + offset) + [0xe5900000 | reg << 12]

    def delay():
        # 20 us observes after the named 10 us apply deadline.
        return (constant(1, 960) + [0xee0e1f12] + constant(1, 1) +
                [0xee0e1f32, 0xee1e1f32, 0xe3110004, 0x0afffffc])

    def prepare(bank=1, reset=True):
        words = []
        if reset:
            words += write(0xc5e0, 0xf000f)
            words += write(0xc5f4, 0x1b) + write(0xc5f8, 0x1b)
            words += write(0xc5e0, 0)
        for context in range(4):
            for group in range(2):
                offset = bank * 0x8000 + context * 0x400 + group * 0x200
                words += write(offset + 0x88, 2) + write(offset + 0x8c, 3)
        return words

    def wait_lane(offset=0x8104):
        return constant(0, base + offset) + [
            0xe5902000, 0xe3520003, 0x1afffffc]

    ready = prepare() + write(0xc128, 0xf00f) + wait_lane()
    cold = read(0x8104, 4) + read(0x8304, 5) + read(0xd020, 6)
    selected = prepare() + write(0xc128, 0x1002) + wait_lane(0x8504)
    selected += read(0x8304, 4) + read(0x8504, 5) + read(0x8104, 6)
    selected += read(0x8704, 7) + read(0xd020, 8)
    windows = prepare()
    for context in range(4):
        windows += write(0xc5f0, 0x10000 << context)
        for offset, value in ((0x81c8, 0x44), (0x81cc, 0x1c), (0x81ec, 0x20)):
            windows += write(offset, value)
    windows += read(0x81c8, 4) + read(0x81cc, 5) + read(0x81ec, 6)
    control = ready + write(0xd024, 0xffff) + write(0xc90c, 6)
    control += delay() + read(0x4908, 4) + read(0xd020, 5)
    control += write(0xd028, 2) + read(0xd020, 6)
    control += write(0xc90c, 4) + delay() + read(0xd020, 7)
    packet = ready.copy()
    for index in range(40):
        packet += write(0xd0b0 + index * 4, index + 1 if index < 24 else 0)
    handshake = ready + write(0xc13c, 2) + delay() + read(0xd020, 4)
    mapping = handshake + write(0xd028, 0x40)
    mapping += packet[len(ready):] + read(0xd0b0, 5) + read(0xd10c, 6)
    mapping += read(0xd020, 7)
    cancelled = prepare() + write(0xc128, 0xf00f)
    cancelled += write(0xc5e0, 0xf000f) + write(0xc5e0, 0) + delay()
    cancelled += read(0x8104, 4) + read(0xd020, 5)
    cancelled_control = ready + write(0xc90c, 6)
    cancelled_control += write(0xc5e0, 0xf000f) + delay()
    cancelled_control += read(0x4908, 4) + read(0xd020, 5)
    cancelled_configuration = ready + write(0xc13c, 2) + write(0xc5e0, 0xf000f)
    cancelled_configuration += delay() + read(0xd020, 4) + read(0xd0b0, 5)
    lifecycle = prepare(0) + prepare(1, reset=False)
    for bank in range(2):
        for context in range(4):
            lifecycle += write(bank * 0x8000 + context * 0x400 + 0xac, 1)
    lifecycle += write(0xc910, 4) + read(0xc910, 4) + read(0x8cac, 5)
    for bank in range(2):
        for context in range(1, 4):
            lifecycle += write(bank * 0x8000 + context * 0x400 + 0xac, 0)
        lifecycle += write(bank * 0x8000 + 0x4128, 0xe00e)
    lifecycle += wait_lane(0x504) + wait_lane(0x8504)
    for bank in range(2):
        lifecycle += write(bank * 0x8000 + 0x4120, 4)
        lifecycle += write(bank * 0x8000 + 0x4124, 4)
    lifecycle += write(0xc13c, 2) + delay() + read(0xd020, 6)
    lifecycle += write(0xd028, 0x40) + read(0x8cac, 7)
    lifecycle += read(0xc120, 8) + read(0xc124, 9)
    irq = write(0xd024, 2) + ready
    irq += constant(0, 0xf8101000) + constant(1, 1) + [0xe5801000]
    irq += constant(2, 0xf8101000) + [0xe5c2187f]
    irq += constant(0, 0xf810110c) + constant(1, 0x80000000) + [0xe5801000]
    irq += constant(0, 0xf8102004) + constant(1, 0xff) + [0xe5801000]
    irq += constant(0, 0xf8102000) + constant(1, 1) + [0xe5801000]
    irq += write(0xc90c, 6) + delay()
    irq += constant(0, 0xf810200c) + [0xe5904000]
    irq += write(0xd028, 2)
    irq += constant(0, 0xf8102010) + constant(1, 127) + [0xe5801000]
    irq += constant(0, 0xf810200c) + [0xe5905000]
    error = "bridge-access-or-protocol-not-supported"
    return [
        ("bridge-cold-no-ready-or-peer-event", cold, {4: 0, 5: 0, 6: 0}, None),
        ("bridge-selected-lanes-only-no-event1", selected,
         {4: 3, 5: 3, 6: 0, 7: 0, 8: 0}, None),
        ("bridge-high-selector-window", windows, {4: 0x44, 5: 0x1c, 6: 0x20}, None),
        ("bridge-control-command-and-w1c", control, {4: 1, 5: 2, 6: 0, 7: 2}, None),
        ("bridge-handshake-before-arrays-no-array-irq", mapping,
         {4: 0x40, 5: 1, 6: 24, 7: 0}, None),
        ("bridge-late-whole-lifecycle", lifecycle,
         {4: 4, 5: 1, 6: 0x40, 7: 0, 8: 4, 9: 4}, None),
        ("bridge-real-gic-irq127", irq, {4: 127, 5: 1023}, None),
        ("bridge-reset-cancels-lanes", cancelled, {4: 0, 5: 0}, None),
        ("bridge-reset-cancels-control", cancelled_control, {4: 0, 5: 0}, None),
        ("bridge-reset-cancels-configuration", cancelled_configuration, {4: 0, 5: 0}, None),
        ("bridge-board-reset", read(0xc5e0, 2) + write(0xc5e0, 0xf000f),
         {2: 0}, None),
        ("bridge-control-overlap-rejected", ready + write(0xc90c, 6) +
         write(0xc90c, 4), {}, error),
        ("bridge-map-trailing-array-profile", write(0xd110, 1), {}, error),
        ("bridge-request-before-configuration", write(0xc128, 0xf00f), {}, error),
        ("bridge-configuration-before-lanes", write(0xc13c, 2), {}, error),
        ("bridge-context-mode-reject-preserves-state", write(0x80ac, 1) +
         write(0x80ac, 2), {}, error),
        ("bridge-late-field-reject-preserves-state", write(0xc120, 4) +
         write(0xc120, 5), {}, error),
        ("bridge-low-selector-unsupported", write(0xc5f0, 1), {}, error),
        ("bridge-unknown-config-value", write(0x8088, 9), {}, error),
        ("bridge-unknown-command", ready + write(0xc90c, 5), {}, error),
        ("bridge-other-bank-ready-not-alias", read(0xc908, 4), {}, error),
        ("bridge-unknown-offset", read(0x829c, 4), {}, error),
        ("bridge-unknown-width", constant(0, base + 0xd020) + [0xe5d01000], {}, error),
    ]


def registers(vm):
    response = vm.cmd("human-monitor-command", command_line="info registers")
    return {int(reg): int(value, 16) for reg, value in
            re.findall(r"\bR(\d+)=([0-9a-fA-F]+)", response)}


def ring_fixtures():
    base, size = 0xb7cf0000, 0xc618
    header = constant(0, base) + constant(1, 0x00c00108)
    header += [0xe5801000] + constant(1, 0xaa550001)
    header += [0xe5801004, 0xe3a01000, 0xe5801008, 0xe5801010]
    header += [0xe5904000, 0xe5905004, 0xe5906008, 0xe5907010]
    slots = constant(0, base + 0x18) + constant(1, 0x1234)
    slots += [0xe5801000, 0xe5904000]
    slots += constant(0, base + size - 4) + [0xe5801000, 0xe5905000]
    producer = constant(0, base) + constant(1, 1)
    producer += [0xe5801008, 0xe5904008, 0xe5905010]
    return [
        ("ring-original-header", header,
         {4: 0x00c00108, 5: 0xaa550001, 6: 0, 7: 0}, None),
        ("ring-entire-slot-capacity", slots, {4: 0x1234, 5: 0x1234}, None),
        ("ring-no-host-consumer", producer, {4: 1, 5: 0}, None),
        ("ring-below", constant(0, base - 4) + [0xe5801000], {},
         "uncovered-address"),
        ("ring-above", constant(0, base + size) + [0xe5801000], {},
         "uncovered-address"),
        ("ring-region-withdrawn", mxic_contract(disable=True) +
         constant(0, base) + [0xe5801000], {}, "uncovered-address"),
    ]


def run_timing_capture(binary, output, name):
    """Drive external pins through upstream qtest; never write sampled results."""
    output.mkdir()
    image = output / "synthetic.bin"
    payload = struct.pack("<I", 0xeafffffe)
    image.write_bytes(payload)
    trace = output / "guest.log"
    a, b = 0x2c010000, 0x2c011000
    commands = []
    irq_levels = {}
    with tempfile.TemporaryDirectory(prefix="ng-", dir="/tmp") as sockets:
        with QEMUQtestMachine(str(binary), args=[
                "-bios", str(image), "-smp", "1", "-display", "none",
                "-serial", "none", "-monitor", "none", "-D", str(trace),
                "-d", "guest_errors"], name="capture", base_temp_dir=sockets,
                qmp_timer=5) as vm:
            vm.set_machine("z7ii-mas-diagnostic")
            vm.launch()
            children = vm.cmd("qom-list", path="/machine/unattached")
            device = [x["name"] for x in children
                      if x["type"] == "child<z7ii-timing-functional>"]
            assert len(device) == 1, children
            path = "/machine/unattached/" + device[0]

            def command(text):
                response = vm.qtest(text).strip()
                events = []
                while response.startswith("IRQ "):
                    events.append(response)
                    _, action, index = response.split()
                    irq_levels[int(index)] = action == "raise"
                    # The official Python client returns one protocol line.
                    # A qtest comment has no response or side effect, so it
                    # lets us consume asynchronous IRQs before the queued OK.
                    response = vm.qtest("# consume pending command response").strip()
                commands.append([text, response, events])
                (output / "qtest-commands.json").write_text(
                    json.dumps(commands, indent=2) + "\n")
                assert response.startswith("OK"), response
                return response

            def write(address, value):
                command(f"writel {address:#x} {value:#x}")

            def read(address):
                return int(command(f"readl {address:#x}").split()[1], 16)

            def step(ns=1000):
                command(f"clock_step {ns}")

            def pin(index, level):
                command(f"set_irq_in {path} capture {index} {level}")

            def setup(channel=5, control=7):
                offset = 0x100 + channel * 0x100
                write(a + 4, 1)
                write(b + offset, 1)
                write(b + offset, 0)
                write(b + offset + 4, 1)
                write(b + offset + 8, 0xffffffff)
                write(b + offset + 16, 1)
                write(a + offset, control)
                write(b, read(b) | (1 << channel))

            setup()
            if name.endswith("disconnected"):
                setup(6)
                step()
                for offset in (0x614, 0x618, 0x714, 0x718):
                    assert read(b + offset) == 0
                assert read(a + 0x610) == read(a + 0x710) == 0
                assert read(b + 0x60c) > 0
            elif name.endswith("edges-and-irq-output"):
                # qtest bus requests are Non-secure; GIC Group0 IAR tests
                # use the separate executing secure ARM IRQ177 fixture.
                command(f"irq_intercept_out {path}")
                step()
                count = read(b + 0x60c)
                pin(0, 1)
                assert read(b + 0x614) == count > 0
                assert read(b + 0x618) == 0 and read(a + 0x610) == 2
                assert irq_levels[5]
                write(a + 0x610, 2)
                assert not irq_levels[5]
                step()
                pin(0, 1)
                assert read(b + 0x614) == count and read(a + 0x610) == 0
                count = read(b + 0x60c)
                pin(0, 0)
                assert read(b + 0x618) == count and read(a + 0x610) == 4
                assert irq_levels[5]
            elif name.endswith("two-channels-disable"):
                setup(6)
                step()
                pin(0, 1)
                assert read(a + 0x610) == 2 and read(a + 0x710) == 0
                assert read(b + 0x714) == 0
                step()
                pin(1, 1)
                assert read(b + 0x714) == read(b + 0x70c) > 0
                assert read(a + 0x710) == 2
                write(a + 0x610, 2)
                write(b + 0x610, 0)
                pin(0, 0)
                assert read(b + 0x618) == 0 and read(a + 0x610) == 0
            elif name.endswith("gates-no-config-edge"):
                write(a + 0x600, 1)
                step()
                pin(0, 1)
                assert read(b + 0x614) == 0 and read(a + 0x610) == 0
                write(a + 0x600, 7)
                assert read(a + 0x610) == 0
                pin(0, 0)
                assert read(b + 0x618) == read(b + 0x60c) > 0
                write(a + 0x610, 4)
                write(b, 0)
                pin(0, 1)
                assert read(b + 0x614) == 0 and read(a + 0x610) == 0
                write(b, 0x20)
                assert read(a + 0x610) == 0
            elif name.endswith("reset-retains-external-level"):
                step()
                pin(0, 1)
                write(b + 0x600, 1)
                assert read(b + 0x614) == read(b + 0x618) == 0
                assert read(a + 0x610) == 0
                setup()
                step()
                pin(0, 1)
                assert read(b + 0x614) == 0 and read(a + 0x610) == 0
                pin(0, 0)
                assert read(b + 0x618) == read(b + 0x60c) > 0
                vm.cmd("system_reset")
                vm.event_wait("RESET", timeout=5)
                assert read(b + 0x614) == read(b + 0x618) == 0
                assert read(a + 0x610) == 0
            elif name.endswith("period-reprogram-clears-samples"):
                step()
                pin(0, 1)
                assert read(b + 0x614) > 0 and read(a + 0x610) == 2
                write(b + 0x608, 0xffffffff)
                assert read(b + 0x614) == read(b + 0x618) == 0
                assert read(a + 0x610) == 0
                step()
                pin(0, 1)
                assert read(b + 0x614) == 0
                pin(0, 0)
                assert read(b + 0x618) == read(b + 0x60c) > 0
            else:
                raise AssertionError(name)
            logs = vm.get_log() or ""
            (output / "qemu.log").write_text(logs)
        assert vm.exitcode() == 0, vm.get_log()
    (output / "qtest-commands.json").write_text(json.dumps(commands, indent=2) + "\n")
    assert "transaction=error" not in logs + trace.read_text()
    assert image.read_bytes() == payload
    return {"fixture": name, "expected_error": None, "qemu_exit_code": 0,
            "scope": "qtest external capture inputs and official virtual clock only"}


def run(binary, output, name, words, expected, reason):
    if name.startswith("timing-capture-input-"):
        return run_timing_capture(binary, output, name)
    output.mkdir()
    image, trace = output / "synthetic.bin", output / "guest.log"
    loop_pc = BASE + len(words) * 4
    payload = struct.pack("<" + "I" * (len(words) + 1), *words, 0xeafffffe)
    image.write_bytes(payload)
    cores = "2" if name.startswith("percore-stack-two-") else "1"
    vm_args = ["-accel", "tcg,thread=single", "-smp", cores, "-bios", str(image),
               "-display", "none", "-serial", "none", "-monitor", "none",
               "-S", "-D", str(trace), "-d", "guest_errors,int"]
    if name.startswith(("sdi-arb-reset-", "usio-", "mxic-config-", "spi-",
                        "hdmac-", "i2c-", "audio-", "timing-", "copy-", "bridge-")):
        vm_args += ["-icount", "shift=0"]
    if name.startswith("i2c-endpoint-"):
        vm_args += ["-device", "at24c-eeprom,bus=z7ii-i2c0,address=80,rom-size=256"]
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
                if name in ("hdmac-source-bus-error", "hdmac-destination-bus-error"):
                    status_file = output / "dma-status.bin"
                    vm.cmd("human-monitor-command", command_line=
                           f'pmemsave 0x2c020014 4 "{status_file}"')
                    expected_status = 0x081b0000 if "source" in name else 0x081c0000
                    assert struct.unpack("<I", status_file.read_bytes())[0] == expected_status
                if name in ("hdmac-armed-alignment-rejected",
                            "hdmac-external-armed-alignment-rejected"):
                    state_file = output / "armed-state.bin"
                    vm.cmd("human-monitor-command", command_line=
                           f'pmemsave 0x2c020010 16 "{state_file}"')
                    ca = 0x81000000 if name.startswith("hdmac-external-") else 0xa0000000
                    assert struct.unpack("<IIII", state_file.read_bytes()) == \
                           (ca, 0, 0xb1800101, 0xb1800201)
                if name == "audio-stereo-rejects-prior-state":
                    state_file = output / "audio-mode.bin"
                    vm.cmd("human-monitor-command", command_line=
                           f'pmemsave 0x2c08010c 4 "{state_file}"')
                    assert struct.unpack("<I", state_file.read_bytes())[0] == 0x07800000
                if name in ("bridge-context-mode-reject-preserves-state",
                            "bridge-late-field-reject-preserves-state"):
                    address, value = ((0x201380ac, 1) if "context" in name
                                      else (0x2013c120, 4))
                    state_file = output / "bridge-configuration.bin"
                    vm.cmd("human-monitor-command", command_line=
                           f'pmemsave {address} 4 "{state_file}"')
                    assert struct.unpack("<I", state_file.read_bytes())[0] == value
                if name.startswith("timing-source1-") and "reject-preserves" in name:
                    state_file = output / "timing-source1-state.bin"
                    vm.cmd("human-monitor-command", command_line=
                           f'pmemsave 0x2c011604 8 "{state_file}"')
                    assert struct.unpack("<II", state_file.read_bytes()) == (1, 0xffffffff)
                    vm.cmd("human-monitor-command", command_line=
                           f'pmemsave 0x2c011000 4 "{state_file}"')
                    expected_bitmap = 0 if "armed-opcode" in name else 0x20
                    assert struct.unpack("<I", state_file.read_bytes())[0] == expected_bitmap
            else:
                deadline = time.monotonic() + 5
                while (actual := registers(vm))[15] != loop_pc:
                    assert vm.cmd("query-status")["running"], vm.get_log()
                    if time.monotonic() >= deadline:
                        (output / "wait-failure.json").write_text(json.dumps({
                            "phase": "initial", "loop_pc": loop_pc,
                            "registers": actual,
                            "status": vm.cmd("query-status"),
                        }, indent=2) + "\n")
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
                if name in ("clock-reset", "completion-reset", "mxic-config-reset",
                            "clock-bitcontrol-reset", "spi-board-reset",
                            "exiu-board-reset", "hdmac-board-reset", "i2c-board-reset",
                            "record-slot-board-reset", "audio-board-reset",
                            "record-slot-alternate-board-reset",
                            "timing-board-reset",
                            "copy-board-reset", "record-footer-board-reset",
                            "external-nor-board-reset", "bridge-board-reset"):
                    vm.cmd("system_reset")
                    vm.event_wait("RESET", timeout=5)
                    assert registers(vm)[15] == BASE
                    vm.cmd("cont")
                    vm.event_wait("RESUME", timeout=5)
                    deadline = time.monotonic() + 5
                    while (after_reset := registers(vm))[15] != loop_pc:
                        assert vm.cmd("query-status")["running"], vm.get_log()
                        if time.monotonic() >= deadline:
                            (output / "wait-failure.json").write_text(json.dumps({
                                "phase": "after-reset", "loop_pc": loop_pc,
                                "registers": after_reset,
                                "status": vm.cmd("query-status"),
                            }, indent=2) + "\n")
                            raise AssertionError("reset loop not reached")
                        time.sleep(0.01)
                    vm.cmd("stop")
                    vm.event_wait("STOP", timeout=5)
                    after_reset = registers(vm)
                    if name in ("mxic-config-reset", "record-slot-board-reset",
                                "record-slot-alternate-board-reset",
                                "record-footer-board-reset",
                                "external-nor-board-reset"):
                        for reg, value in expected.items():
                            assert after_reset[reg] == value, after_reset
                    else:
                        assert after_reset[2] == 0
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
    cases = (fixtures() + spi_fixtures() + exiu_fixtures() + hdmac_fixtures() +
             i2c_fixtures() + record_slot_fixtures() + audio_fixtures() +
             timing_fixtures() + copy_fixtures() + footer_slot_fixtures() +
             external_nor_fixtures() + bridge_fixtures())
    names = [case[0] for case in cases]
    assert len(names) == len(set(names)), "duplicate synthetic fixture names"
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    results = [run(args.binary.resolve(), output / name, name, words,
                   expected, reason) for name, words, expected, reason in cases]
    result = {"scope": "synthetic contracts only; no firmware boot proof",
              "results": results}
    (output / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result))


if __name__ == "__main__":
    main()
