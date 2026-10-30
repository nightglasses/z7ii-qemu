#!/usr/bin/env python3
"""
Exhaustively dump all 259 Nikon vendor device properties discovered via 0x90ca.
Retrieves description, data type, access mode, default value, and current value.
"""

import json
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ptp_client import PTPClient
from probe_full import BufferReader

def main():
    # Read the 259 property codes from payload_90ca.bin
    bin_90ca = "/Users/auturora/.codex/worktrees/physical-ptp/nightglass-qemu/docs/public/evidence/physical/payload_90ca.bin"
    with open(bin_90ca, "rb") as f:
        raw = f.read()

    cnt = struct.unpack("<I", raw[:4])[0]
    props = [struct.unpack("<H", raw[4+2*i:6+2*i])[0] for i in range((len(raw)-4)//2)]
    print(f"[*] Loaded {len(props)} vendor property codes to query.")

    prop_database = {}

    with PTPClient() as client:
        client.clear_halt()
        client.open_session(1)

        for idx, p in enumerate(props):
            p_hex = f"0x{p:04x}"
            if idx % 30 == 0:
                print(f"[*] Progress: {idx}/{len(props)} (current: {p_hex})...")

            entry = {"code": p_hex}

            # 1. GetDevicePropDesc (0x1014)
            d_desc, r_desc = client.execute_transaction(0x1014, [p], timeout=1500)
            if d_desc and len(d_desc["payload"]) >= 5:
                payload = d_desc["payload"]
                r = BufferReader(payload)
                prop_code = r.read_u16()
                dtype = r.read_u16()
                getset = r.read_u8()
                def_val = r.read_value_by_type(dtype)
                cur_val = r.read_value_by_type(dtype)
                form_flag = r.read_u8()

                entry["data_type"] = f"0x{dtype:04x}"
                entry["access"] = "GetSet" if getset == 1 else "GetOnly"
                entry["default_value"] = str(def_val)
                entry["current_value"] = str(cur_val)
                entry["form_flag"] = form_flag

            # 2. GetDevicePropValue (0x1015)
            d_val, r_val = client.execute_transaction(0x1015, [p], timeout=1500)
            if d_val and d_val["payload"]:
                entry["raw_value_hex"] = d_val["payload"].hex()

            prop_database[p_hex] = entry

        client.close_session()

    out_file = "/Users/auturora/.codex/worktrees/physical-ptp/nightglass-qemu/docs/public/evidence/physical/vendor_properties_259.json"
    with open(out_file, "w", encoding="utf-8") as f:
        json.dump(prop_database, f, indent=2, ensure_ascii=False)
    print(f"[+] Successfully exported all {len(prop_database)} vendor properties to {out_file}!")

if __name__ == "__main__":
    main()
