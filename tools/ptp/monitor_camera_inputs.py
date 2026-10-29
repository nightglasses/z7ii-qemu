#!/usr/bin/env python3
"""
Real-time Camera Physical Input & Event Monitor for Nikon Z 7II
Captures live button presses, dial clicks, mode switches, and orientation changes.
Maps physical inputs to QEMU input controller profiles (hw/nikon/devices/input.c).
"""

import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ptp_client import PTPClient

WATCH_PROPS = {
    0x500E: ("ExposureProgramMode", "u16"),
    0xD0FC: ("UserMode_Dial", "u8"),
    0x500D: ("ExposureTime_Shutter", "u32"),
    0x5007: ("FNumber_Aperture", "u16"),
    0x500F: ("ExposureIndex_ISO", "u16"),
    0xD104: ("AFLockStatus", "u8"),
    0xD105: ("AELockStatus", "u8"),
    0xD106: ("FVLockStatus", "u8"),
    0xD10E: ("CameraOrientation", "u8"),
    0xD112: ("TVLockSetting", "u8"),
    0xD113: ("AVLockSetting", "u8"),
}

PROP_NAMES = {
    0x5007: "FNumber (Aperture Dial)",
    0x500D: "ExposureTime (Shutter Dial)",
    0x500E: "ExposureProgramMode (P/S/A/M)",
    0x500F: "ExposureIndex (ISO Dial)",
    0xD0FC: "UserMode (Mode Dial)",
    0xD104: "AFLockStatus (AF-ON / Half-Press)",
    0xD105: "AELockStatus (AE-L / Lock Button)",
    0xD106: "FVLockStatus (Flash Lock)",
    0xD10E: "CameraOrientation (Tri-axis Accel)",
}

def decode_prop_value(data: bytes, dtype: str):
    if not data:
        return None
    # Skip prop desc header if raw GetDevicePropValue:
    # GetDevicePropValue directly returns the packed value
    try:
        if dtype == "u8":
            return struct.unpack("<B", data[:1])[0]
        elif dtype == "u16":
            return struct.unpack("<H", data[:2])[0]
        elif dtype == "u32":
            return struct.unpack("<I", data[:4])[0]
    except Exception:
        return data.hex()
    return data.hex()

def query_prop_value(client, prop_code, dtype):
    data, resp = client.execute_transaction(0x1015, [prop_code], timeout=500)
    if data and data['payload']:
        return decode_prop_value(data['payload'], dtype)
    return None

def main():
    duration = 30
    if len(sys.argv) > 1:
        try:
            duration = int(sys.argv[1])
        except ValueError:
            pass

    print(f"[*] Starting live input monitor for {duration} seconds...")
    print(f"[*] Tip: You can turn the Mode Dial, rotate Command Dials, half-press Shutter, press AF-ON, or tilt the camera!")
    print("-" * 75)

    with PTPClient() as client:
        client.clear_halt()
        client.open_session(1)

        # Baseline snapshot
        state = {}
        for p, (name, dt) in WATCH_PROPS.items():
            val = query_prop_value(client, p, dt)
            state[p] = val
            print(f"  [Baseline] {name:25s} = {val}")
        print("-" * 75)
        print(f"[*] Monitoring live inputs... (Press Ctrl+C to stop early)\n")

        t_end = time.time() + duration
        event_count = 0

        while time.time() < t_end:
            # 1. Check EP_INT Interrupt endpoint
            evt = client.read_event(timeout=100)
            if evt:
                event_count += 1
                code = evt['code']
                params = evt['params']
                p_hex = [hex(x) for x in params]
                t_str = time.strftime("%H:%M:%S")
                if code == 0x4006 and len(params) > 0: # DevicePropChanged
                    prop_id = params[0]
                    prop_name = PROP_NAMES.get(prop_id, f"Prop 0x{prop_id:04X}")
                    # Fetch new value
                    dt = WATCH_PROPS.get(prop_id, (None, "u32"))[1]
                    new_val = query_prop_value(client, prop_id, dt)
                    old_val = state.get(prop_id)
                    state[prop_id] = new_val
                    print(f"[{t_str}] >>> [EVENT 0x4006] {prop_name} CHANGED: {old_val} -> {new_val}")
                else:
                    print(f"[{t_str}] >>> [EVENT {hex(code)}] params={p_hex}")

            # 2. Poll 0x90C7 for vendor events
            d, r = client.execute_transaction(0x90C7, timeout=100)
            if d and len(d['payload']) > 2:
                payload = d['payload']
                num_events = struct.unpack("<H", payload[:2])[0]
                t_str = time.strftime("%H:%M:%S")
                for i in range(num_events):
                    offset = 2 + i * 8
                    if offset + 8 <= len(payload):
                        ecode, epid, eparam = struct.unpack("<HHI", payload[offset:offset+8])
                        p_name = PROP_NAMES.get(epid, f"0x{epid:04X}")
                        if ecode == 0x4006:
                            dt = WATCH_PROPS.get(epid, (None, "u32"))[1]
                            new_val = query_prop_value(client, epid, dt)
                            old_val = state.get(epid)
                            if new_val != old_val:
                                state[epid] = new_val
                                print(f"[{t_str}] >>> [0x90C7 Vendor] {p_name} CHANGED: {old_val} -> {new_val}")

            time.sleep(0.05)

        print("-" * 75)
        print(f"[*] Monitoring complete. Captured {event_count} events.")
        client.close_session()

if __name__ == "__main__":
    main()
