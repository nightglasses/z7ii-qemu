# Memory

Two service subcommands touch arbitrary memory: `F032` reads, `F033` writes. Both are
gated by one function at `VA 0x104b0098`, and understanding that gate is the whole game.

## Wire format

```text
command : u32 length, u16 1, u16 opcode, u32 transaction, u32 param[]
data    : u32 length, u16 2, u16 opcode, u32 transaction, u8 payload[]
response: u32 length, u16 3, u16 code,   u32 transaction, u32 param[]
```

| Intent | Request |
| --- | --- |
| read memory | `0x94FD [0xF032, address, length, 0, 0]` |
| write memory | `0x94FE [0xF033, address, length, 0, 0]` plus the payload |

The write **must** use `0x94FE`. Only that opcode registers the receive callback
(`0x107722f0 → 0x104c6aec`). A data phase sent with `0x94FD` is never consumed: the
camera waits for data that will not be dispatched and the session hangs until a power
cycle.

## `F032` address read

The validate callback checks the parameters against the guard and, on success, the
executor copies from the requested address into a data container. That copy is a plain
CPU copy (`O0x4b0196 blx 0x10360fac`), not a DMA transfer, so the memory itself is always
reachable; only the guard decides whether the copy is performed.

## `F033` address write

The executor is `memcpy(params[1], data_phase, params[2])` (`VA 0x104b02c8`). The validate
callback requires `0 < length <= 0x400` and the same guard.

This is a byte-granular write with a 1 KiB ceiling per request, which is enough to patch a
descriptor, a gate byte or a small structure. It is not a flash programmer: writes land in
RAM and disappear on reboot.

## The guard

The guard behaves as a **deny list**:

- a request that falls inside a listed interval is refused;
- a request outside every interval reaches the executor.

Refusal does not look like an error. The response is `0x2001` with an **empty data
container** and the inner parameter `0x201d`, so the data length is the diagnostic signal,
not the response code. An opcode that is not dispatched at all answers `0x2005` instead.

Intervals that were identified:

| Interval | Kind | Note |
| --- | --- | --- |
| `[0x10000000,0x14000000)` | static | the ROM and NOR projection; boot-region address reads never succeed |
| `[0xB2BB0000,0xB2BB105B)` | static | the MAS RAM2 copy segment |
| `[0xBAA00000,0xBB300000)` | static | the relocated code class |
| `[0xB1000000,0xB1800000)` | static | the MAS inner image |
| `[0xB20CFD14,0xB20EFD10)` | runtime | object 8's cache: base from its record `+8`, length `0x1FFFC` |

The runtime interval is not a constant. It is read from object 8's descriptor on every
call, which is what makes it patchable — see [Objects](objects).

## Observed map

| Address | `F032` result |
| --- | --- |
| `0x10000000`, `0x10000080`, `0x10000088`, `0x10020000` | empty data container, `0x201d` |
| `0x04000000` | data; the bytes are the reset vector `b4f09fe5feffffea` |
| `0x04100000`, `0x04200000` | data |
| `0xB1800474`, `0xB180048C` | data; the diagnostic gates |
| `0xB18B9DA8` | data; the log ring |
| `0xB185E7DC`, `0xB185E8D8` | data; object descriptors |
| `0xB20C0000`, `0xB20CE000` | data |
| `0xB20D0000`, `0xB20D2894`, `0xB20D2D6A`, `0xB20E0000` | empty data container, `0x201d` |
| `0xB20F2890`, `0xB20F3000`, `0xB2100000` | data |

This matches the intervals above: the address map is open except for the listed windows,
so ordinary RAM is freely readable and writable, and the boot region is not.

## Restore discipline

Every write changes live state. The safe pattern is:

1. read and save the bytes you are about to change;
2. write;
3. verify by reading back;
4. restore the saved bytes;
5. verify the restore;
6. `0x94FD [0xF002,0,0,0,0]` and close.

If a step fails, stop and power-cycle rather than continuing: a partially patched
descriptor can make later reads answer with misleading data.
