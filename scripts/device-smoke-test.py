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

    return mxic_config_fixtures() + [
        ("nv-slot69-erased-bounds", constant(0, 0x133e0000) +
         [0xe5904000, 0xe5d05000, 0xe1d060b0] +
         constant(0, 0x133ffffc) + [0xe5907000],
         {4: 0xffffffff, 5: 0xff, 6: 0xffff, 7: 0xffffffff}, None),
        ("nv-slot69-below", constant(0, 0x133dfffc) + [0xe5901000], {},
         "uncovered-address"),
        ("nv-slot69-above", constant(0, 0x13400000) + [0xe5901000], {},
         "uncovered-address"),
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
    slave += constant(1, 48000) + [0xee0e1f12]
    slave += constant(1, 1) + [0xee0e1f32]
    slave += [0xee1e1f32, 0xe3110004, 0x0afffffc]
    slave += constant(0, SPI) + [0xe590402c, 0xe5905030, 0xe590603c]

    # Pending master frame is cancelled by reset; configuration survives reopen.
    cancel = setup + write(8, 255) + write(0, 0x1234) + write(0x3c, 1)
    cancel += write(0x60, 1) + write(0x60, 0)
    cancel += constant(1, 48000) + [0xee0e1f12]
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
        ("record-slot-below", constant(0, 0x12dffffc) + [0xe5901000], {},
         "uncovered-address"),
        ("record-slot-above", constant(0, 0x12e40000) + [0xe5901000], {},
         "uncovered-address"),
        ("record-slot-alternate-unavailable", constant(0, 0x131e0000) + [0xe5901000], {},
         "uncovered-address"),
        ("record-slot-before-memc", constant(0, 0x12e00000) + [0xe1c010b0], {},
         "nor-memc-not-initialized"),
        ("record-slot-write-protected", initialized(0x29) +
         constant(0, 0x12e00000) + [0xe1c010b0], {}, "nor-write-protected"),
        ("record-slot-command-unsupported", initialized() +
         constant(0, 0x12e00aaa) + constant(1, 0xaa) + [0xe1c010b0], {},
         "nor-backend-not-supported"),
    ]


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
    cores = "2" if name.startswith("percore-stack-two-") else "1"
    vm_args = ["-accel", "tcg,thread=single", "-smp", cores, "-bios", str(image),
               "-display", "none", "-serial", "none", "-monitor", "none",
               "-S", "-D", str(trace), "-d", "guest_errors,int"]
    if name.startswith(("sdi-arb-reset-", "usio-", "mxic-config-", "spi-", "hdmac-", "i2c-")):
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
                if name in ("clock-reset", "completion-reset", "mxic-config-reset",
                            "clock-bitcontrol-reset", "spi-board-reset",
                            "exiu-board-reset", "hdmac-board-reset", "i2c-board-reset",
                            "record-slot-board-reset"):
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
                    after_reset = registers(vm)
                    if name in ("mxic-config-reset", "record-slot-board-reset"):
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
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    results = [run(args.binary.resolve(), output / name, name, words,
                   expected, reason) for name, words, expected, reason in
               fixtures() + spi_fixtures() + exiu_fixtures() + hdmac_fixtures() +
               i2c_fixtures() + record_slot_fixtures()]
    result = {"scope": "synthetic contracts only; no firmware boot proof",
              "results": results}
    (output / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result))


if __name__ == "__main__":
    main()
