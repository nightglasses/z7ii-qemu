#!/usr/bin/env python3
import sys, os
sys.path.insert(0, '/Users/auturora/.codex/worktrees/physical-ptp/nightglass-qemu/tools/ptp')
from ptp_client import PTPClient

with PTPClient() as client:
    client.open_session(1)
    
    # Unlock service mode with 0xF001
    d, r = client.execute_transaction(0x94fd, [0xF001, 0, 0, 0, 0])
    print(f"Unlock 0xF001: resp={hex(r['code']) if r else 'None'} params={[hex(p) for p in (r.get('params') or [])] if r else []}")
    
    # Test specific service keys from displaywait.md and REFS.tsv
    keys = [0xF001, 0xF079, 0xF091, 0xF092, 0xF093, 0xF094, 0xF165, 0xF166]
    for k in keys:
        data, resp = client.execute_transaction(0x94fd, [k, 0, 0, 0, 0])
        code = hex(resp['code']) if resp else 'None'
        params = [hex(p) for p in (resp.get('params') or [])] if resp else []
        d_len = len(data['payload']) if data else 0
        print(f"Key {hex(k)}: resp={code} params={params} data_len={d_len}")
        
    client.close_session()
