# Writing a client

The camera speaks plain PTP over USB. No vendor SDK is needed: any library that can claim
an interface and move bulk data works. This page records the parts that are easy to get
wrong.

## Discovery and claim

| Step | Detail |
| --- | --- |
| Find the device | vendor `0x04b0`, product `0x044b`, class 6 / subclass 1 / protocol 1 |
| Open | ordinary open; the device is not exclusive at open time |
| Claim | interface 0; on macOS this fails with `-3` while the system PTP daemon holds it |
| Endpoints | `0x02` bulk OUT, `0x81` bulk IN, `0x83` interrupt IN |

The system daemon respawns after being killed, so the reliable pattern is: kill it, then
claim immediately, and retry the pair if the claim still fails. Do not fight it with a
sleep between the two.

## Framing

```text
offset 0  u32 length        total container length including this header
offset 4  u16 type          1 command, 2 data, 3 response, 4 event
offset 6  u16 code          opcode for command/data, response code for response,
                            event code for event
offset 8  u32 transaction   echoed by the response; 0xffffffff for events
offset 12 payload           parameters (u32 array) or data bytes
```

A command and its data phase share one transaction id. The response echoes it. A response
whose id does not match belongs to an earlier request and the stream is out of step.

Bulk transfers: the host may write the command and the data container as separate
transfers, and may read one container per transfer or in pieces. A read loop must consume
exactly `length` bytes across transfers and stop, otherwise the next response is read as
part of the previous payload.

## Transaction shapes

| Kind | Host sends | Camera sends |
| --- | --- | --- |
| no data | command | response |
| data in | command | data container, then response |
| data out | command, then data container | response |

`0x94FE` is the only opcode in the service family that accepts a data-out phase. Sending
data to `0x94FD` produces a camera that waits forever and a host that times out; the
recovery is a power cycle.

## Timeouts

Use a short timeout for command-only requests and a longer one for anything that copies or
streams. On this body a refused read still answers quickly, so a timeout means the camera
is waiting for something, not that the request is slow. Treat a timeout as a protocol
error and stop; retrying into a waiting device is what turns a mistake into a hang.

## Reference skeleton

```text
open(vid, pid); claim(0)
tx = 1
cmd(op, params):     write container(type=1, op, tx, params)
data(op, payload):   write container(type=2, op, tx, payload)
resp():              read container; assert type == 3 and id == tx
event():             read interrupt endpoint; container type 4

open_session():      cmd(0x1002, [1]); resp()
unlock_service():    cmd(0x94fd, [0xf001,0,0,0,0]); resp()
read_mem(a, n):      cmd(0x94fd, [0xf032,a,n,0,0])
                     container = read(); if type == 2: payload = read_payload()
                     resp()
write_mem(a, bytes): cmd(0x94fe, [0xf033,a,len(bytes),0,0]); data(0x94fe, bytes); resp()
```

The write path must put the opcode in the data container's `code` field. The camera uses
it to route the payload to the right receive callback.

## Session discipline

1. `OpenSession(1)`; there is one session and it is not concurrent.
2. Unlock service mode before anything in the service family.
3. Save every byte before writing it, and verify by reading back.
4. Lock service mode and close the session before detaching.
5. If a request is left half-issued, close the handle and reopen; a session left open
   makes the next `OpenSession` answer `0x201e`.

## When something goes wrong

| Symptom | Reading | Action |
| --- | --- | --- |
| `0x2005` | opcode not dispatched, or validate callback refused | check the opcode and parameter count |
| `0x2001` with an empty data container, parameter `0x201d` | address guard refused the range | the range is protected, not broken |
| `0x2020` | object transfer refused | the card-upload path is disabled on this body |
| no response, timeout | camera is waiting for a data phase that was never routed | power cycle |
| bulk OUT `-9` | endpoint stalled after a malformed request | clear the halt and reopen, or power cycle |
| claim `-3` | the system daemon owns the interface | kill it and claim again |

## What to verify first

1. `0x1001` returns a device info dataset with model `Z 7_2`.
2. `0x1004` lists two storage ids.
3. `0x94fd [0xf001,0,0,0,0]` answers `0x2001`.
4. A read of a RAM address outside the guard returns bytes, and a read inside the guard
   returns the empty-container signature.

If any of those four disagree, stop: the body may be a different version and every address
in this section should be treated as unknown.
