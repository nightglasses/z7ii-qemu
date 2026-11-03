# Physical PTP access surface

A physical Nikon Z 7II, opened over USB with the vendor PTP stack, exposes far more than
image transfer. The service-mode subcommands form a general memory and object access API:
RAM can be read and written, the boot region can be reached through the object services,
and the runtime guard that protects those regions can be reasoned about and, in one
specific place, worked around.

This section documents that surface command by command.

| Page | Content |
| --- | --- |
| [Operations](operations) | the outer operation table and each standard and vendor opcode |
| [Services](services) | the `0x94FD` / `0x94FE` service family, one entry per subcommand |
| [Memory](memory) | the address read and write primitives, the guard and its intervals |
| [Objects](objects) | registry records, object services and the descriptor-rewrite read |
| [Vendor properties](properties) | the 259 property codes, types and captured values |
| [F091 boundary](boundary) | the static analysis of the F091 command path |
| [Physical baseline](physical) | the camera's own capability profile and live-view path |
| [Physical captures](physicalmemory) | the archived RAM and NVRAM captures and their bounds |
| [Recipes](recipes) | ready-to-run sequences for the tasks this surface supports |
| [Writing a client](client) | transport, framing, session and failure handling |
| [Key recovery](recovery) | the end-to-end procedure that recovered the update key |

## Transport

| Item | Value |
| --- | --- |
| USB identity | `04b0:044b`, model string `NIKON DSC Z 7_2` |
| Endpoints | `0x02` bulk OUT, `0x81` bulk IN, `0x83` interrupt IN |
| Container | `u32 length, u16 type, u16 code, u32 transaction`, then payload |
| Types | 1 command, 2 data, 3 response, 4 event |

On macOS the PTP daemon holds interface 0, so a raw claim fails until it is killed; it
also respawns, which means the kill and the claim have to be raced or repeated.

## Session shape

```text
open 0x1002 [1]
  -> 0x94FD [0xF001,0,0,0,0]     service mode unlocked
  -> work (reads, writes, object services)
  -> 0x94FD [0xF002,0,0,0,0]     service mode locked
close 0x1003
```

Everything that changes state must be restored before the lock step, or the body must be
power-cycled. Nothing written to RAM survives a reboot.

## Quick start

1. Claim interface 0 and `OpenSession(1)`.
2. `0x1001` and check the model string — the addresses in this section were measured
   against `Z 7_2` reporting `V1.60`.
3. `0x94fd [0xf001,0,0,0,0]` to unlock the service family.
4. Read `0x94fd [0xf032,0xb1800474,1,0,0]`. A byte comes back.
5. Read `0x94fd [0xf032,0x10000000,1,0,0]`. An empty data container comes back — the
   guard refused the NOR window. That contrast is the core of the surface.
6. `0x94fd [0xf002,0,0,0,0]` and close.

If step 4 fails or step 5 returns data, stop: the firmware is not the one this section was
written against. The [recipes](recipes) page has the task-level sequences and
[writing a client](client) has the transport details.

## Glossary

| Term | Meaning here |
| --- | --- |
| service | a subcommand carried in parameter 0 of `0x94fd` / `0x94fe`, named `F0xx` |
| service mode | the unlocked state `F001` enables; required for the whole family |
| guard | the range check shared by the address read and write; it is a deny list |
| object | a registry entry with a base and a length; object services stream from it |
| descriptor | the RAM record holding an object's base and length |
| digest context | the update authentication state, where the seed is copied during a check |

## Response codes seen on this surface

| Code | Reading |
| --- | --- |
| `0x2001` | accepted; the data phase may still be empty |
| `0x2005` | not dispatched, or rejected by the row's validate callback |
| `0x201d` | parameter failure; also the inner parameter the address guard returns when it refuses |
| `0x2020` | object-destination refusal; every `SendObjectInfo` variant returned it |

A refusal is not always an error code. The address guard answers `0x2001` with an empty
data container and `0x201d` inside, so the data length is the thing to check, not only the
response code.

## Capability summary

| Capability | Mechanism | Status |
| --- | --- | --- |
| read RAM | `F032` address read | verified on the body |
| write RAM | `F033` address write, data phase through `0x94FE` | verified on the body |
| read the NOR boot region | object service after rewriting an object descriptor | verified on the body |
| read the configuration table | shrink object 8's length, then address access | verified on the body |
| write NOR | not exposed by any service found so far | not established |
| read the diagnostic log ring | ring memory is readable; the producer is not | partial |
| send a diagnostic console command | `0x92F0` with a host data phase | no response |
| upload a file to the card | `0x100C` / `0x100D`, both advertised | refused with `0x2020` |

## Boundaries

- The addresses and constants in this section were measured on a body reporting `V1.60`
  and checked against C 1.70 object code. Another firmware version may move them.
- Service mode and RAM writes change runtime state. Descriptor bytes and configuration
  bytes that were touched must be restored, or the body rebooted.
- A data phase sent to `0x94FD` instead of `0x94FE` hangs the session; the only recovery
  is a power cycle.
- Nothing in this section writes NOR, starts an update or touches a storage card.
