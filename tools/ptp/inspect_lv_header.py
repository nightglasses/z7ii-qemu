#!/usr/bin/env python3
import sys, os, time, struct
sys.path.insert(0, '/Users/auturora/.codex/worktrees/physical-ptp/nightglass-qemu/tools/ptp')
from ptp_client import PTPClient

with PTPClient() as client:
    client.open_session(1)
    client.execute_transaction(0x9201)
    time.sleep(0.3)
    data, resp = client.execute_transaction(0x9203)
    client.execute_transaction(0x9202)
    client.close_session()

if data:
    payload = data["payload"]
    jpeg_idx = payload.find(b"\xff\xd8")
    header = payload[:jpeg_idx]
    print(f"Header length: {len(header)} bytes (0x{len(header):04x})")
    
    # Save raw header
    with open("/Users/auturora/.codex/worktrees/physical-ptp/nightglass-qemu/docs/public/evidence/physical/liveview_header.bin", "wb") as f:
        f.write(header)
        
    print("\nHex dump of first 128 bytes:")
    for row in range(0, min(128, len(header)), 16):
        hex_str = " ".join(f"{b:02x}" for b in header[row:row+16])
        ascii_str = "".join(chr(b) if 32 <= b <= 126 else "." for b in header[row:row+16])
        print(f"  {row:04x}: {hex_str:<48}  |{ascii_str}|")

    # Big endian words
    print("\nParsed as Big-Endian u32 words:")
    for i in range(0, min(64, len(header)), 4):
        val = struct.unpack(">I", header[i:i+4])[0]
        val16_1 = struct.unpack(">H", header[i:i+2])[0]
        val16_2 = struct.unpack(">H", header[i+2:i+4])[0]
        print(f"  Offset +{i:03d} (0x{i:02x}): 0x{val:08x} ({val:<10d}) | u16: 0x{val16_1:04x} ({val16_1}), 0x{val16_2:04x} ({val16_2})")
