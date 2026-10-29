#!/usr/bin/env python3
"""
Dump full payloads of the 11 data-returning service keys discovered in 0x94fd.
"""

import json
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ptp_client import PTPClient

DATA_KEYS = [
    0xF006, 0xF04C, 0xF050, 0xF051, 0xF052, 0xF06E,
    0xF113, 0xF114, 0xF162, 0xF163, 0xF1F9
]

def main():
    out_dir = "/Users/auturora/.codex/worktrees/physical-ptp/nightglass-qemu/docs/public/evidence/physical/service_payloads"
    os.makedirs(out_dir, exist_ok=True)
    
    records = {}

    with PTPClient() as client:
        client.clear_halt()
        client.open_session(1)
        # Unlock
        client.execute_transaction(0x94fd, [0xF001, 0, 0, 0, 0])

        for k in DATA_KEYS:
            data, resp = client.execute_transaction(0x94fd, [k, 0, 0, 0, 0])
            code = hex(resp['code']) if resp else 'None'
            params = [hex(p) for p in (resp.get('params') or [])] if resp else []
            payload = data['payload'] if data else b""
            
            bin_name = f"payload_{k:04x}.bin"
            bin_path = os.path.join(out_dir, bin_name)
            with open(bin_path, "wb") as f:
                f.write(payload)

            records[f"0x{k:04x}"] = {
                "resp_code": code,
                "params": params,
                "data_length": len(payload),
                "file": f"docs/public/evidence/physical/service_payloads/{bin_name}",
                "preview_hex": payload[:64].hex()
            }
            print(f"[+] Key 0x{k:04x}: {len(payload)} bytes -> saved to {bin_name}")

        client.execute_transaction(0x94fd, [0xF002, 0, 0, 0, 0])
        client.close_session()

    manifest_path = os.path.join(out_dir, "manifest.json")
    with open(manifest_path, "w", encoding="utf-8") as f:
        json.dump(records, f, indent=2, ensure_ascii=False)
    print(f"[+] All payloads dumped successfully. Manifest written to {manifest_path}")

if __name__ == "__main__":
    main()
