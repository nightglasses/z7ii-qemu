#!/usr/bin/env python3
"""
Robust, self-healing scanner for all 75 advertised Nikon vendor opcodes.
Handles endpoint stalls, timeouts, and transaction resets automatically.
"""

import json
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ptp_client import PTPClient

def probe_opcode(client, op):
    res = {
        "opcode": f"0x{op:04x}",
        "success": False,
        "resp_code": None,
        "params_returned": [],
        "data_len": 0,
        "data_preview_hex": None,
        "notes": []
    }

    try:
        data, resp = client.execute_transaction(op, [], timeout=1500)
    except Exception as e:
        res["notes"].append(f"Send failed: {e}")
        client.clear_halt()
        return res

    if resp:
        res["resp_code"] = f"0x{resp['code']:04x}"
        res["params_returned"] = [f"0x{p:04x}" for p in resp.get("params", [])]
        if resp["code"] == 0x2001:
            res["success"] = True
            res["notes"].append("Accepted with 0 params")
        elif resp["code"] == 0x2006:
            # Requires parameter; try [0]
            try:
                data2, resp2 = client.execute_transaction(op, [0], timeout=1500)
                if resp2:
                    res["resp_code"] = f"0x{resp2['code']:04x}"
                    res["params_returned"] = [f"0x{p:04x}" for p in resp2.get("params", [])]
                    if resp2["code"] == 0x2001:
                        res["success"] = True
                        res["notes"].append("Accepted with 1 param [0]")
                    else:
                        res["notes"].append(f"Param [0] -> 0x{resp2['code']:04x}")
            except Exception as e:
                client.clear_halt()
                res["notes"].append(f"Param [0] failed: {e}")
        else:
            res["notes"].append(f"Code: 0x{resp['code']:04x}")
    elif data:
        res["notes"].append("Data received without immediate response")
    else:
        res["notes"].append("Timeout / No response packet (may expect host data-out or async event)")
        client.clear_halt()

    if data and "payload" in data and data["payload"]:
        payload = data["payload"]
        res["data_len"] = len(payload)
        res["data_preview_hex"] = payload[:32].hex()

    return res

def main():
    profile_path = "/Users/auturora/.codex/worktrees/physical-ptp/nightglass-qemu/docs/public/evidence/physical/ptp_profile.json"
    with open(profile_path, "r", encoding="utf-8") as f:
        profile = json.load(f)

    all_ops = profile["device_info"]["operations_supported"]
    vendor_ops = [int(op, 16) for op in all_ops if int(op, 16) >= 0x9000]

    print(f"[*] Found {len(vendor_ops)} advertised vendor operations to probe.")
    results = {}

    with PTPClient() as client:
        client.clear_halt()
        client.open_session(1)
        print("[+] Session opened (tx=1)")

        for idx, op in enumerate(vendor_ops):
            print(f"[{idx+1}/{len(vendor_ops)}] Opcode 0x{op:04x}...", end=" ", flush=True)
            t0 = time.time()
            r = probe_opcode(client, op)
            dt = (time.time() - t0) * 1000
            r["latency_ms"] = round(dt, 2)
            results[f"0x{op:04x}"] = r
            print(f"resp={r['resp_code']} data_len={r['data_len']} notes={r['notes']}")

            # If there was a timeout/error, reset the session to ensure clean state for next op
            if r["resp_code"] is None:
                client.clear_halt()
                try:
                    client.close_session()
                except Exception:
                    pass
                time.sleep(0.1)
                try:
                    client.open_session(1)
                except Exception:
                    client.clear_halt()
                    client.open_session(1)

            time.sleep(0.05)

        try:
            client.close_session()
        except Exception:
            pass
        print("[+] Scan complete, session closed.")

    out_file = "/Users/auturora/.codex/worktrees/physical-ptp/nightglass-qemu/docs/public/evidence/physical/vendor_ops_scan.json"
    with open(out_file, "w", encoding="utf-8") as f:
        json.dump(results, f, indent=2, ensure_ascii=False)
    print(f"[+] Saved results to {out_file}")

if __name__ == "__main__":
    main()
