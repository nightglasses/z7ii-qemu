#!/usr/bin/env python3
"""
Full PTP capability, property and service mode probe for physical Nikon Z 7II.
Saves structured physical evidence for nightglass-qemu.
"""

import json
import os
import struct
import sys
import time

# Add tools/ptp to sys.path
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ptp_client import PTPClient

class BufferReader:
    def __init__(self, data):
        self.data = data
        self.offset = 0

    def remaining(self):
        return len(self.data) - self.offset

    def read_u8(self):
        if self.offset >= len(self.data):
            return 0
        v = self.data[self.offset]
        self.offset += 1
        return v

    def read_u16(self):
        if self.offset + 2 > len(self.data):
            return 0
        v = struct.unpack("<H", self.data[self.offset:self.offset+2])[0]
        self.offset += 2
        return v

    def read_u32(self):
        if self.offset + 4 > len(self.data):
            return 0
        v = struct.unpack("<I", self.data[self.offset:self.offset+4])[0]
        self.offset += 4
        return v

    def read_u64(self):
        if self.offset + 8 > len(self.data):
            return 0
        v = struct.unpack("<Q", self.data[self.offset:self.offset+8])[0]
        self.offset += 8
        return v

    def read_string(self):
        if self.offset >= len(self.data):
            return ""
        num_chars = self.read_u8()
        if num_chars == 0:
            return ""
        size = num_chars * 2
        if self.offset + size > len(self.data):
            size = max(0, len(self.data) - self.offset)
        raw = self.data[self.offset:self.offset + size]
        self.offset += size
        return raw.decode("utf-16le", errors="ignore").rstrip("\x00")

    def read_u16_array(self):
        cnt = self.read_u32()
        arr = []
        for _ in range(cnt):
            arr.append(self.read_u16())
        return arr

    def read_value_by_type(self, dtype):
        if dtype == 0x0001: # INT8
            v = struct.unpack("<b", self.data[self.offset:self.offset+1])[0]
            self.offset += 1
            return v
        elif dtype == 0x0002: # UINT8
            return self.read_u8()
        elif dtype == 0x0003: # INT16
            v = struct.unpack("<h", self.data[self.offset:self.offset+2])[0]
            self.offset += 2
            return v
        elif dtype == 0x0004: # UINT16
            return self.read_u16()
        elif dtype == 0x0005: # INT32
            v = struct.unpack("<i", self.data[self.offset:self.offset+4])[0]
            self.offset += 4
            return v
        elif dtype == 0x0006: # UINT32
            return self.read_u32()
        elif dtype == 0x0007: # INT64
            v = struct.unpack("<q", self.data[self.offset:self.offset+8])[0]
            self.offset += 8
            return v
        elif dtype == 0x0008: # UINT64
            return self.read_u64()
        elif dtype == 0xFFFF: # String
            return self.read_string()
        else:
            # Fallback
            return None

def parse_device_info(payload):
    r = BufferReader(payload)
    std_version = r.read_u16()
    vendor_ext_id = r.read_u32()
    vendor_ext_version = r.read_u16()
    vendor_ext_desc = r.read_string()
    functional_mode = r.read_u16()
    ops = r.read_u16_array()
    events = r.read_u16_array()
    props = r.read_u16_array()
    capture_fmts = r.read_u16_array()
    image_fmts = r.read_u16_array()
    mfg = r.read_string()
    model = r.read_string()
    device_version = r.read_string()
    serial_no = r.read_string()

    return {
        "standard_version": f"{std_version/100:.2f}",
        "vendor_extension_id": f"0x{vendor_ext_id:08x}",
        "vendor_extension_version": f"{vendor_ext_version/100:.2f}",
        "vendor_extension_desc": vendor_ext_desc,
        "functional_mode": functional_mode,
        "operations_supported": [f"0x{op:04x}" for op in ops],
        "events_supported": [f"0x{ev:04x}" for ev in events],
        "properties_supported": [f"0x{pr:04x}" for pr in props],
        "capture_formats": [f"0x{cf:04x}" for cf in capture_fmts],
        "image_formats": [f"0x{im:04x}" for im in image_fmts],
        "manufacturer": mfg,
        "model": model,
        "device_version": device_version,
        "serial_number": serial_no,
    }

def parse_storage_info(payload):
    r = BufferReader(payload)
    storage_type = r.read_u16()
    fs_type = r.read_u16()
    access_cap = r.read_u16()
    max_cap = r.read_u64()
    free_bytes = r.read_u64()
    free_images = r.read_u32()
    desc = r.read_string()
    label = r.read_string()

    storage_types = {1: "FixedROM", 2: "RemovableROM", 3: "FixedRAM", 4: "RemovableRAM"}
    fs_types = {1: "UndefinedFlat", 2: "Hierarchical", 3: "DCF"}
    access_caps = {0: "ReadWrite", 1: "ReadOnlyNoDelete", 2: "ReadOnlyWithDelete"}

    return {
        "storage_type": storage_types.get(storage_type, f"0x{storage_type:04x}"),
        "filesystem_type": fs_types.get(fs_type, f"0x{fs_type:04x}"),
        "access_capability": access_caps.get(access_cap, f"0x{access_cap:04x}"),
        "max_capacity_bytes": max_cap,
        "free_space_bytes": free_bytes,
        "free_space_images": free_images,
        "storage_description": desc,
        "volume_label": label,
    }

def main():
    print("[*] Connecting to physical camera via libusb...")
    evidence = {}

    with PTPClient() as client:
        print("[+] Opened USB device and claimed interface 0")

        # 1. DeviceInfo
        info_payload = client.get_device_info()
        if not info_payload:
            print("[-] Failed to read DeviceInfo")
            return 1
        dev_info = parse_device_info(info_payload)
        evidence["device_info"] = dev_info
        print(f"[+] Camera: {dev_info['manufacturer']} {dev_info['model']} ({dev_info['device_version']})")
        print(f"[+] Serial: {dev_info['serial_number']}")
        print(f"[+] Supported operations: {len(dev_info['operations_supported'])}")
        print(f"[+] Supported properties: {len(dev_info['properties_supported'])}")

        # 2. Open session
        print("[*] Opening PTP session...")
        rc = client.open_session(1)
        print(f"[+] OpenSession returned: 0x{rc:04x}")
        evidence["session_open_rc"] = f"0x{rc:04x}"

        # 3. Storage info
        storages = client.get_storage_ids()
        print(f"[+] Discovered storages: {[hex(s) for s in storages]}")
        evidence["storages"] = {}
        for s in storages:
            data, resp = client.execute_transaction(0x1005, [s])
            if data:
                s_info = parse_storage_info(data["payload"])
                evidence["storages"][f"0x{s:08x}"] = s_info
                print(f"    Slot 0x{s:08x}: {s_info['storage_type']}, {s_info['filesystem_type']}, Cap={s_info['max_capacity_bytes']/(1024**3):.2f} GB, Free={s_info['free_space_bytes']/(1024**3):.2f} GB, Label='{s_info['volume_label']}'")

        # 4. Device Properties
        prop_names = {
            0x5001: "BatteryLevel",
            0x5002: "FunctionalMode",
            0x5003: "ImageSize",
            0x5004: "CompressionSetting",
            0x5005: "WhiteBalance",
            0x5007: "FNumber",
            0x5008: "FocalLength",
            0x500A: "FocusMode",
            0x500B: "ExposureMeteringMode",
            0x500C: "FlashMode",
            0x500D: "ExposureTime",
            0x500E: "ExposureProgramMode",
            0x500F: "ExposureIndex",
            0x5010: "ExposureBiasCompensation",
            0x5011: "DateTime",
            0x5013: "StillCaptureMode",
            0x5018: "BurstNumber",
            0x501E: "FocusDistance",
            0x501F: "FocusMeteringMode",
            0xD303: "VendorD303",
            0xD406: "VendorD406_MTPClientString",
            0xD407: "VendorD407_HostStatus",
        }

        print("[*] Querying supported device properties...")
        evidence["properties"] = {}
        for p_hex in dev_info["properties_supported"]:
            p = int(p_hex, 16)
            name = prop_names.get(p, f"Unknown_{p_hex}")
            desc_payload = client.get_device_prop_desc(p)
            val_payload = client.get_device_prop_value(p)

            p_entry = {
                "name": name,
                "code": p_hex,
                "raw_val_hex": val_payload.hex() if val_payload else None,
            }

            if desc_payload and len(desc_payload) >= 5:
                r = BufferReader(desc_payload)
                prop_code = r.read_u16()
                dtype = r.read_u16()
                getset = r.read_u8()
                def_val = r.read_value_by_type(dtype)
                cur_val = r.read_value_by_type(dtype)
                form_flag = r.read_u8()
                p_entry["data_type"] = f"0x{dtype:04x}"
                p_entry["get_set"] = "GetSet" if getset == 1 else "GetOnly"
                p_entry["default_value"] = str(def_val)
                p_entry["current_value"] = str(cur_val)
                p_entry["form_flag"] = form_flag

            evidence["properties"][p_hex] = p_entry
            print(f"    Prop {p_hex} ({name:25s}): current={p_entry.get('current_value')} (raw={p_entry['raw_val_hex']})")

        # 5. Live View Inspection (Test before service mode dispatch)
        print("[*] Testing Live View streaming (0x9201 / 0x9203 / 0x9202)...")
        lv_started = client.start_live_view()
        evidence["live_view"] = {"started": lv_started}
        print(f"[+] StartLiveView result: {lv_started}")

        if lv_started:
            time.sleep(0.3) # brief settle time
            lv_payload = client.get_live_view_frame()
            if lv_payload:
                total_len = len(lv_payload)
                jpeg_offset = lv_payload.find(b"\xff\xd8")
                header_data = lv_payload[:jpeg_offset] if jpeg_offset != -1 else lv_payload[:384]

                lv_info = {
                    "payload_total_bytes": total_len,
                    "header_length_reported": struct.unpack(">I", lv_payload[:4])[0] if len(lv_payload) >= 4 else None,
                    "jpeg_length_reported": struct.unpack(">I", lv_payload[4:8])[0] if len(lv_payload) >= 8 else None,
                    "jpeg_offset_actual": jpeg_offset,
                    "jpeg_size_actual": total_len - jpeg_offset if jpeg_offset != -1 else 0,
                    "frame_width": struct.unpack(">H", lv_payload[8:10])[0] if len(lv_payload) >= 10 else None,
                    "frame_height": struct.unpack(">H", lv_payload[10:12])[0] if len(lv_payload) >= 12 else None,
                    "sensor_width": struct.unpack(">H", lv_payload[12:14])[0] if len(lv_payload) >= 14 else None,
                    "sensor_height": struct.unpack(">H", lv_payload[14:16])[0] if len(lv_payload) >= 16 else None,
                    "center_x": struct.unpack(">H", lv_payload[20:22])[0] if len(lv_payload) >= 22 else None,
                    "center_y": struct.unpack(">H", lv_payload[22:24])[0] if len(lv_payload) >= 24 else None,
                    "focus_box_w": struct.unpack(">H", lv_payload[24:26])[0] if len(lv_payload) >= 26 else None,
                    "focus_box_h": struct.unpack(">H", lv_payload[26:28])[0] if len(lv_payload) >= 28 else None,
                    "header_prefix_hex": header_data[:32].hex(),
                }
                evidence["live_view"]["frame_info"] = lv_info
                print(f"[+] Captured LiveView Frame: {lv_info['frame_width']}x{lv_info['frame_height']} (Sensor: {lv_info['sensor_width']}x{lv_info['sensor_height']}), JPEG={lv_info['jpeg_size_actual']} B")

                if jpeg_offset != -1:
                    img_path = "/Users/auturora/.codex/worktrees/physical-ptp/nightglass-qemu/docs/public/evidence/physical/liveview_sample.jpg"
                    with open(img_path, "wb") as f:
                        f.write(lv_payload[jpeg_offset:])
                    evidence["live_view"]["saved_image"] = "docs/public/evidence/physical/liveview_sample.jpg"
                    print(f"[+] Saved sample frame to {img_path}")

                    hdr_path = "/Users/auturora/.codex/worktrees/physical-ptp/nightglass-qemu/docs/public/evidence/physical/liveview_header.bin"
                    with open(hdr_path, "wb") as f:
                        f.write(header_data)
                    evidence["live_view"]["saved_header"] = "docs/public/evidence/physical/liveview_header.bin"

            client.end_live_view()
            print("[+] EndLiveView completed")

        # 6. Service mode handshake verification
        print("[*] Verifying Service Mode / Display Command Handshake (0x94fd)...")
        service_evidence = {}
        t0 = time.time()
        d1, r1 = client.execute_transaction(0x94fd, [0xF001, 0, 0, 0, 0])
        dt1 = (time.time() - t0) * 1000
        service_evidence["f001_service_unlock"] = {
            "opcode": "0x94fd",
            "subcmd": "0xF001",
            "resp_code": f"0x{r1['code']:04x}" if r1 else None,
            "params": [f"0x{p:04x}" for p in (r1.get("params") or [])],
            "latency_ms": round(dt1, 2),
        }
        print(f"    0x94fd [0xF001]: resp=0x{r1['code']:04x} params={[hex(p) for p in (r1.get('params') or [])]} ({dt1:.2f} ms)")

        t0 = time.time()
        d2, r2 = client.execute_transaction(0x94fd, [0xF091, 0, 0, 0, 0])
        dt2 = (time.time() - t0) * 1000
        service_evidence["f091_display_service"] = {
            "opcode": "0x94fd",
            "subcmd": "0xF091",
            "resp_code": f"0x{r2['code']:04x}" if r2 else None,
            "params": [f"0x{p:04x}" for p in (r2.get("params") or [])],
            "latency_ms": round(dt2, 2),
        }
        print(f"    0x94fd [0xF091]: resp=0x{r2['code']:04x} params={[hex(p) for p in (r2.get('params') or [])]} ({dt2:.2f} ms)")
        evidence["service_mode"] = service_evidence

        # 7. Close session
        print("[*] Closing PTP session...")
        rc = client.close_session()
        print(f"[+] CloseSession returned: 0x{rc:04x}")

    # Write evidence file
    evidence_out = "/Users/auturora/.codex/worktrees/physical-ptp/nightglass-qemu/docs/public/evidence/physical/ptp_profile.json"
    with open(evidence_out, "w", encoding="utf-8") as f:
        json.dump(evidence, f, indent=2, ensure_ascii=False)
    print(f"[+] Successfully wrote evidence to {evidence_out}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
