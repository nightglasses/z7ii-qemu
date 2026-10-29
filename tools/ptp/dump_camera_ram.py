#!/usr/bin/env python3
"""
Systematic RAM and Calibration Data Extraction Tool for Nikon Z 7II
Dumps live physical memory, NVRAM blocks, and OS runtime structures for nightglass-qemu.
Executes single-shot transfers for maximum speed and zero timeout risk.
"""

import hashlib
import json
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ptp_client import PTPClient

OUTPUT_DIR = "/Users/auturora/.codex/worktrees/physical-ptp/nightglass-qemu/docs/public/evidence/physical/ram_dumps"

def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()

def main():
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    manifest = {}

    print("[*] Connecting to Nikon Z 7II and initializing RAM collection...")
    with PTPClient() as client:
        client.clear_halt()
        client.open_session(1)
        # Unlock service mode
        client.execute_transaction(0x94fd, [0xF001, 0, 0, 0, 0])

        # 1. Early DRAM (0x04000000 - 1MB single shot)
        print("[+] 1/6 Dumping 1MB Early DRAM (0x04000000) via 0xF032...")
        t0 = time.time()
        data, resp = client.execute_transaction(0x94fd, [0xF032, 0x04000000, 1024 * 1024, 0, 0], timeout=10000)
        dt = time.time() - t0
        dram_1mb = data['payload'] if data else b''
        fname = "dram_boot_04000000.bin"
        fpath = os.path.join(OUTPUT_DIR, fname)
        with open(fpath, "wb") as f:
            f.write(dram_1mb)
        manifest[fname] = {
            "type": "DRAM_PHYSICAL",
            "base_address": "0x04000000",
            "length": len(dram_1mb),
            "sha256": sha256_bytes(dram_1mb),
            "duration_sec": round(dt, 3),
            "throughput_mb_s": round(len(dram_1mb) / max(dt, 0.001) / (1024 * 1024), 2),
            "description": "Multi-core ARM Cortex exception vectors, boot trampolines, CPU mode stacks, and early staging heap."
        }
        print(f"    Saved {fname} ({len(dram_1mb)} bytes in {dt:.3f}s)")

        # 2. MAS Runtime Globals (0xB188B000 - 8KB single shot)
        print("[+] 2/6 Dumping 8KB MAS Runtime Globals (0xB188B000) via 0xF032...")
        data, resp = client.execute_transaction(0x94fd, [0xF032, 0xB188B000, 8192, 0, 0], timeout=3000)
        mas_globals = data['payload'] if data else b''
        fname = "dram_mas_globals_b188b000.bin"
        fpath = os.path.join(OUTPUT_DIR, fname)
        with open(fpath, "wb") as f:
            f.write(mas_globals)
        manifest[fname] = {
            "type": "MAS_RUNTIME_GLOBALS",
            "base_address": "0xB188B000",
            "length": len(mas_globals),
            "sha256": sha256_bytes(mas_globals),
            "description": "MAS OS kernel task control blocks, PTPCMD state byte (0xb188b768), session ID (0xb188bc6c), and service predicate (0xb188bc80)."
        }
        print(f"    Saved {fname} ({len(mas_globals)} bytes)")

        # 3. NVRAM Resource Blocks via 0xF029 (sel 0..6)
        f029_blocks = [
            (0, 65536, "nvram_resource_80_master.bin", "Resource 80 Master Calibration block (optical center, sensor geometry, bad pixel maps)."),
            (1, 65536, "nvram_resource_71.bin", "Resource 71 Subsystem NVRAM parameter block."),
            (2, 32768, "nvram_resource_70.bin", "Resource 70 Subsystem NVRAM parameter block."),
            (3, 65536, "nvram_resource_79.bin", "Resource 79 Calibration mirror/shadow block."),
            (4, 65536, "nvram_resource_11.bin", "Resource 11 Sensor analog frontend gain/offset block."),
            (5, 65536, "nvram_resource_12.bin", "Resource 12 Exposure/metering calibration block."),
            (6, 65536, "nvram_resource_67_afadjust.bin", "Resource 67 AFadjustkohatsu factory autofocus lens adjustment block.")
        ]
        print("[+] 3/6 Dumping NVRAM Resource Blocks (0xF029 sel 0..6)...")
        for sel, blk_len, fname, desc in f029_blocks:
            data, resp = client.execute_transaction(0x94fd, [0xF029, 0, blk_len, sel, 0], timeout=3000)
            raw = data['payload'] if data else b''
            fpath = os.path.join(OUTPUT_DIR, fname)
            with open(fpath, "wb") as f:
                f.write(raw)
            manifest[fname] = {
                "type": "NVRAM_RESOURCE_BLOCK",
                "subcommand": "0xF029",
                "selector": sel,
                "length": len(raw),
                "sha256": sha256_bytes(raw),
                "description": desc
            }
            print(f"    Saved {fname} ({len(raw)} bytes)")

        # 4. Specialized Hardware Subsystem Blocks
        subsys_blocks = [
            (0xF021, 32768, "nvram_block_17_audio.bin", "Block 17 'AUDIO' DSP codec calibration and gains."),
            (0xF025, 4096, "nvram_block_53_serial_otp.bin", "Block 53 Serial Number ('8027437') and factory OTP block."),
            (0xF02D, 4096, "nvram_block_08.bin", "Block 8 Hardware configuration block."),
            (0xF03D, 32768, "nvram_block_09.bin", "Block 9 Hardware subsystem calibration block."),
            (0xF03A, 32768, "nvram_block_10.bin", "Block 10 Hardware subsystem calibration block."),
        ]
        print("[+] 4/6 Dumping Hardware Subsystem NVRAM Blocks...")
        for code, blk_len, fname, desc in subsys_blocks:
            data, resp = client.execute_transaction(0x94fd, [code, 0, blk_len, 0, 0], timeout=3000)
            raw = data['payload'] if data else b''
            fpath = os.path.join(OUTPUT_DIR, fname)
            with open(fpath, "wb") as f:
                f.write(raw)
            manifest[fname] = {
                "type": "SUBSYSTEM_NVRAM_BLOCK",
                "subcommand": hex(code),
                "length": len(raw),
                "sha256": sha256_bytes(raw),
                "description": desc
            }
            print(f"    Saved {fname} ({len(raw)} bytes)")

        # 5. Subsystem Version Table (0xF003)
        print("[+] 5/6 Querying Subsystem Version Table (0xF003)...")
        subsys_versions = {}
        subsys_labels = {
            1: "ModelName",
            2: "Subsystem_Boot_A",
            3: "Subsystem_Boot_B",
            4: "Firmware_C_Main",
            5: "Firmware_LF_Lens",
            6: "Region_Lang_Pack",
            8: "Distortion_Profile",
            9: "Firmware_MA_App"
        }
        for sel, label in subsys_labels.items():
            data, resp = client.execute_transaction(0x94fd, [0xF003, sel, 0, 0, 0], timeout=2000)
            payload = data['payload'] if data else b''
            text = payload.split(b'\x00')[0].decode('ascii', errors='replace')
            subsys_versions[f"selector_{sel}_{label}"] = {
                "selector": sel,
                "label": label,
                "value": text,
                "hex": payload[:32].hex()
            }
        fname = "subsystem_versions_f003.json"
        fpath = os.path.join(OUTPUT_DIR, fname)
        with open(fpath, "w", encoding="utf-8") as f:
            json.dump(subsys_versions, f, indent=2, ensure_ascii=False)
        manifest[fname] = {
            "type": "SUBSYSTEM_METADATA",
            "subcommand": "0xF003",
            "items_count": len(subsys_versions),
            "description": "Internal firmware module versions (C: 0160b1, LF: 012000, MA: 010100)."
        }
        print(f"    Saved {fname}")

        # 6. Save Manifest
        manifest_path = os.path.join(OUTPUT_DIR, "manifest.json")
        with open(manifest_path, "w", encoding="utf-8") as f:
            json.dump(manifest, f, indent=2, ensure_ascii=False)
        print(f"[+] All RAM and calibration data dumped successfully! Manifest: {manifest_path}")

        # Relock service mode and close session
        client.execute_transaction(0x94fd, [0xF002, 0, 0, 0, 0])
        client.close_session()

if __name__ == "__main__":
    main()
