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
    parser.add_argument("--trace-records", action="store_true",
                        help="upstream trace of original record scanner and bank coordinator")
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
    if args.trace_kernel or args.trace_pkgb or args.trace_records:
        vm_args[-1] = "guest_errors,int,in_asm,cpu,exec"
        filters = ["0x10100000+0x20000"] if args.trace_kernel else []
        if args.trace_pkgb:
            filters += ["0x10200c8c+0x94", "0x10200684+0x2e",
                        "0x10201010+0x40", "0x10242fda+0x68",
                        "0x10200c62+0x4", "0x10200c10+0x4",
                        "0x1020081c+0x4", "0x10200972+0x4",
                        "0x10200c48+0x2", "0x10201458+0x10",
                        "0x10387e08+0x2a", "0x10387e36+0x64",
                        "0x102ddfe6+0x4", "0x10200bca+0x46",
                        "0x102018e4+0x30", "0x103ff8c4+0x38"]
        if args.trace_records:
            filters += ["0x102a7d82+0xaa", "0x102a822c+0x1ec",
                        "0x102a85f4+0xb0", "0x1010261e+0x76",
                        "0x10101cc2+0x4e", "0x10101cb8+0xa",
                        "0x10101f1e+0x18", "0x102a87ae+0xe",
                        "0x102a7d56+0x2c", "0x102a874e+0x2c",
                        "0x102a807e+0x2a", "0x102a80a8+0x4e",
                        "0x102a84a2+0x5a", "0x103bf3c6+0x6c"]
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
        "20203000 control/GIC124: virtual cold-zero finite actual-MMIO retained configuration (10/14/20/24/48/68/70/88/90 fullwords and six4word arrays),boolean/u8 setter shapes; packedBFI softwareB180shadow/packet remains guestRAM not MMIO fields/byte access;00/04opaque controls without reset/event/ACK effects,20 not mask,raw2c W1C and two disconnected rising fixture events bits0/10;reset keeps input levels,unknown08/holes reject,prior separate20OR400 write not rolled back,no frame/DMA/data/hostcallback",
        "20205000 control/GIC122: virtual cold-zero finite generated config, retained00/300 bool pulses without reset/event/ACK effects,04bit0 and308/30c low2/310314bit0/5c60 config RMW preserve other old bits;20 fullword nonmask,subconfig11c completeu8 and all known arrays share physicalword state; originaldynamic packet n has no bound/MMIO count register, nominal3-word layout not a hardware/API/model countguard and knownneighbor word cannot identify overlongarray writes; defaultdisconnected rising fixture events bits0/2,W1C raw2c,reset keeps levels,no frames/DMA/callbacks,unknown304/holes reject",
        "20204000 control/GIC121: virtual cold-zero retained bit0 controls00/04/08 without reset/event effects, finite generated config/128word table/five distinct arrays (5c..94 distinct from98..d0),40/44/48 preserve outside changed fields;20 fullword parameter not mask; raw2c W1C retains three data snapshots194/198/19c, three disconnected fixture events bits8/17/25; event8 requires complete explicit96-bit publication, no default valid sample or frame inference;14 activation/408/428 data and holes unsupported, reset preserves pin levels and requires republication",
        "20201000 control/GIC116: virtual cold-zero finite retained config and sequential opaque44/48 selections,20 is a fullword parameter without masking; raw2c W1C retains conditional1a0 sample, three disconnected fixture events bits0/1/6; event0 snapshots only after explicit complete32-bit provider publication, no default valid data or physical frame inference; reset requires fresh publication and preserves pin levels,00 assert/14 activation and holes unsupported, no pulse reset/completion/host callbacks",
        "20380000/203c0000 finite controls/GIC99/100: named cold-zero retained opaque00/04/0c/F0 and exact original single/dual configuration, full32 E8 mask1 inhibition and raw24 W1C, explicit rising pending-bit fixture inputs disconnected in original; controls/read/config never generate ready/pending/reset effects, unknown08/10/54/60/6c/78/580 activation rejects before state changes; no frame, host callbacks or physical IP identity",
        "20100000 control/GIC102: named zero reset, finite retained configuration/table readback, three full32 masks/raw-pending domains; mask1 enable and rising fixture pending-bit inputs/W1C ACK are virtual laws with unknown physical events; default inputs disconnected, config/deasserted controls never generate pending, unknown activation/pulse assert/data monitors reject, no frame/timer completion or host callback",
            "20070000/20070040 two fill engines/IRQ103/104: virtual zero reset, retained boolcontrol00, synchronous official RAM-only zero DMA in exact byte-block mode1 width=stride4000 positive count, no guessed patterns/copy/format; stop10=0 causally publishesstate2 with no deferred work and separate raw-result ACK08; mask04 bits0/4 enable corresponding real bus done/error, error0c destination code1 atbits16:17 is virtual, remaining encodingsunknown; configsource masks retained, unsupported activation preflight preserves state, no callback/guestflag injection",
            "20202000 control/GIC123: named inactive zero reset, finite firmware-generated config value masks (not physical writable-mask proof), staged/applied bracket10 and applied config readback as virtual conventions; 18 enables,08/0c/14 arm distinct externally completed requests,00/04 or disable cancel requests without pending ACK; explicit rising fixture inputs complete08/14/0c and latchbits1/2/16, mask1 enables and2c W1C are virtual laws; default inputs disconnected, no frame/timer completion, guest callback or event injection, physical identity unknown",
            "24020000 IPUD-named transport/GIC101: virtual zero reset,32-word TX/RX FIFO, three-word staged CMD2 commit, independent48MHz nominal clock/32ticks per consumed word, disconnected TXsink/emptyRX; status reflects occupancy, TXspacebit2 only on real consumption crossing29, RXbit0 only explicit complete externalpacket, W1C ACK/raw-mask/full32 MASK; configs exactly1D00/7FFFF with no readback; CMD4 ACK-only never secondpop; optional chardev uses named12byte little-endian externalframe law, absent from originalrun/nopeerreply; RMU80 unavailable0 and84..94 activationunsupported, no remoteDMA/events/physicalidentity",
            "2013 finite bridge control protocol at20130000, GIC127; named cold-zero supported config/reset, exact current-profile words, high16 one-context selector, independent48MHz/10us lane/control/configuration apply; lane status3, command4/6 event2 and configuration-command2 event4 are virtual causal conventions; five arrays are independent configuration and generate no IRQ/backing; no peer event1/signature/backing or physical IP identity",
            "2b000000 five-route signal-control virtual profile: disconnected low inputs; zero reset/config, 0-held reset and 1-enabled gate/masks, W1C edge pending, raw-low/filtered-high status, independent 1MHz debounce with 1/2/4/8 ticks; configuration rebases without edges; unfiltered private pending read and W1C write; five outputs aggregate through official OR_IRQ to original GIC237; separate28000000 4-byte aligned32 write-only full-value input latch reset0 with no IRQ/ACK/forwarding/security effects; no physical IP identity or adjacent2b001000 support",
            "original software address-class pair declares physical[b5f00000,b7d00000) and CPU logical[14000000,15e00000), 30MiB cold-zero virtual RAM; CPU alias uses original guest MMU without physical-bus140 mirror; includes message-ringB7CF/C618 in same storage, no separate ring RAM or host object/ID/signature/indices; this is not proven physical heap capacity",
            "clock-controller04281034 is an independent full32bit word-mask configuration latch with named virtual reset0; original OR/BIC requests retain other bits, no derived frequency/ready/event or alias to58/5c/stopbanks; physical writable fields and effects remain unknown",
            "four finite I2C instances at2c018000..2c01b000/GIC214..217; idle zero control, empty16-byte FIFOs, mask2ff, disconnected official I2CBus, nominal independent48MHz; monitor/bit7/10-bit operations unsupported",
            "two eight-channel HDMAC blocks reset zero, GIC150..157/142..149; nominal independent48MHz, real non-reload bus copies; external requests rechecked per beat, Audio bank1 selectors3/7/5; unsupported transforms/pause/halt",
            "two Audio channels at2c080100/200, GIC167/168; virtual zero reset, opaque32-bit FIFO words, disconnected input, independent48MHz divided output sink; original AUCR enable-command variant and unknown AUMD23/26 retained; no physical PCM packing/codec inference",
            "twelve-channel timing at2c010000/2c011000, IRQ172..183; virtual zero reset, gated bitmap counters, independent3GHz nominal Clock, opcode0 periodic/31 one-shot/1 free-running direct-source with once-per-programmed-threshold statusbit0/W1C hypotheses; opcode1 MAX+1 uses 64-bit ticks, counter low32 readback, no periodic retry after ACK; channels5/6 mode0 capture uses two disconnected qdev input signals, gated rising/falling edges latch actual source1 count to separate result words and statusbits1/2; capture config changes create no edge, reset/program clear samples, other capture mode/feature commands/shared submits reject before state mutation; physical input wiring and capture semantics unbound; no DMA copying",
    "four-channel default byte-copy at04251000/GIC45..48; virtual zero reset, exact30f00/30f00/0/mode1 profile, independent48MHz byte pacing, direct ordinary RAM/ROMD source and writable ordinary RAM destination via real DMA bus; whole remaining extents rechecked before each beat, MMIO/holes/readonly destinations reject causally; status8 only after all bytes, bus-error/cancel1 and remaining-minus-one conventions; physical roles/effects of global28/20/24 unknown: virtual28 enable/cancel,20/24 latched configuration inputs; fill/alternate formats unsupported",
            "16 EXIU inputs from original GPIO5[7:15]/GPIO6[0:6], GIC65..80; virtual masked/rising-edge reset, pending mask-filter convention, self-clearing W1C command-port readback zero with asserted-level re-latch; physical command readback unbound, no software source or physical key provider",
            "two upstream Cortex-A7; CBAR=0xf8100000; CNTFRQ=48MHz",
        "upstream whole GICv1/TZ at original CBAR offsets; 256 SGI/PPI/SPI IDs; CPU EL2 unchanged, no GIC virtualization interface",
            "candidate MAS VA placed at diagnostic PA 0x10020000",
            "whole external NOR descriptor-erased-FF input:33 source descriptors/81 default IDs, exclude0/1/2/zero4; 35 views share five finite physical windows [12960000,13240000),[132c0000,13380000),[133a0000,13500000),[13540000,13e40000),[13e80000,14000000),22544384B; aliases share cells, alternatives independent; actual MAS ROM/holes preserved, no real NV/ENDM/calibration/CFI writer; MEMC rejects programming",
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
            "original software address-class code destination [baa00000,bb300000), finite9MiB cold-zero virtual RAM preserving BABkernel positions; guest copies kernel21538 and observed selector0 [10200000,10500000) toBAC before own section remap; selector1/2 destinations are staticdeclared only, source10000000..10020000 remains unknown; no host code/alias/PTE injection or physical DDR inference",
            "first four observed original bump allocations form finite virtual RAM [b7d00000,b80dc3c0), with original identity-section hypothesis and 64-byte guest alignment; no later heap capacity or host allocation metadata",
            "original shared sending ring [b7cf0000,b7cfc618): header18 + 192 slots*108, maximum body100; virtual cold-zero RAM, guest header/magic/producer/consumer, no host peer consumption or reply",
            "virtual byte-block zero-fill direct count; finite BSS [b18aedc0,b2b5edc0), real DMA bus result",
        "virtual initial region decode from guest boot profile; byte/64KiB ranges constrain existing DDR backings only",
        "polling USIO channel0; nominal independent 100MHz, one-word TX, 8N1 virtual timing; no invented RX/IRQ",
        "finite second system configuration; virtual strap/config reset zero; five aligned full32 words share guest bootstrap and dynamic bit RMW; physical writable bits/effects unknown, no modeled IRQ/ready/clock/reset effect",
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
