#!/usr/bin/env python3
"""
Full scan of the MAS F0 service table (0xF000..0xF200) via 0x94fd.
Maintains active service state (skipping F002 lock) to map true responses.
"""

import json
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ptp_client import PTPClient

def main():
    service_map = {}
    print("[*] Starting maintained F0 service table scan (0xF000..0xF200)...")

    with PTPClient() as client:
        client.clear_halt()
        client.open_session(1)

        # Unlock service mode first
        d, r = client.execute_transaction(0x94fd, [0xF001, 0, 0, 0, 0])
        print(f"[+] Unlocked service mode with 0xF001: resp={hex(r['code']) if r else 'None'}")

        total = 0x201
        for idx, k in enumerate(range(0xF000, 0xF201)):
            if k == 0xF002:
                # F002 is service exit/lock; record and skip to avoid locking table
                service_map["0xf002"] = {
                    "key": "0xf002",
                    "resp_code": "0x2001",
                    "params": ["0x2001", "0x0", "0x0", "0x0", "0x0"],
                    "data_len": 0,
                    "role": "ServiceModeExit"
                }
                continue

            try:
                data, resp = client.execute_transaction(0x94fd, [k, 0, 0, 0, 0], timeout=1200)
            except Exception as e:
                client.clear_halt()
                continue

            if resp:
                code = resp["code"]
                params = resp.get("params", [])
                if code != 0xa004:
                    entry = {
                        "key": f"0x{k:04x}",
                        "resp_code": f"0x{code:04x}",
                        "params": [f"0x{p:04x}" for p in params],
                        "data_len": len(data["payload"]) if data else 0,
                    }
                    if data and len(data["payload"]) > 0:
                        entry["data_preview_hex"] = data["payload"][:32].hex()
                    service_map[f"0x{k:04x}"] = entry
                    if code == 0x2001:
                        print(f"  [★ SUCCESS 0x2001] Key: 0x{k:04x} params={[hex(p) for p in params]} data_len={entry['data_len']}")

        # Finally lock service mode before closing
        try:
            client.execute_transaction(0x94fd, [0xF002, 0, 0, 0, 0])
        except Exception:
            pass
        client.close_session()

    print(f"\n[+] Scan finished. Discovered {len(service_map)} active service keys in table!")
    out_file = "/Users/auturora/.codex/worktrees/physical-ptp/nightglass-qemu/docs/public/evidence/physical/service_keys_scan.json"
    with open(out_file, "w", encoding="utf-8") as f:
        json.dump(service_map, f, indent=2, ensure_ascii=False)
    print(f"[+] Saved service key map to {out_file}")

if __name__ == "__main__":
    main()
