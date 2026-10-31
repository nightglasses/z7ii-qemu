# Native image-memory initialization

C1.70 `vgCreateImage` uses a separate memory-initialization port in the transfer
block at MAS PA `0x20180000`. The finite zero-fill profile now executes real
DMA before raising IRQ 131. It is independent from the byte-copy port at
`+0xc00` and the paired drawing streams at `+0x10d0/+0x10e0`. This functional
profile does not identify the physical transfer IP or establish chip timing.

The native constructor at MAS VA `0x106846c0` builds its descriptor around
`0x1068483c..0x10684870`: mode zero, the image allocation's data start and
exclusive end, zero fill value and command `1` or `0x201`. Dispatcher
`0x1064d406` selects type 4 and producer `0x106ee0d2` writes these four
little-endian 32-bit registers:

| Offset | Descriptor word | Finite profile |
| --- | --- | --- |
| `0x10` | `+0x04` | First byte to initialize |
| `0x14` | `+0x08` | Exclusive end address |
| `0x18` | `+0x0c` | Zero fill value |
| `0x1c` | `+0x10` | Command `0x0f100001` or `0x0f100201`; zero acknowledges/cancels |

A modified C1.70 benchmark's first 64-by-64 image allocation reaches this
same mode-zero branch with descriptor words `{0, start, start+16384, 0,
0x0f100201}`. The upstream dispatcher at `0x1064d46c..0x1064d470` ORs
the constructor's low command `1` or `0x201` with `0x0f100000` before placing
it in the producer record. Both resulting complete commands are admitted;
the producer also has a `0x0f100201` default literal at `0x106ee284`.
The preceding finite model rejected the observed `+0x1c` write at
`0x106ee3c6`; admitting this exact command retains all operand and memory
checks. Its upper command bits are not generalized to other operations.

The source nonzero-mode branch also writes `+0xe0/+0xe4/+0xe8/+0xec`; its
strided/format semantics are not admitted by this profile. Nonzero fill data,
other command values, incomplete configuration, unaligned bounds, reversed
or empty ranges and unbacked writable ranges remain errors. A 16 MiB per-job
work bound is a functional resource limit, not a recovered hardware capacity.

An accepted command schedules zero writes through QEMU DMA in chunks up to
4096 bytes. Each chunk rechecks effective writable backing. No byte changes
and no interrupt occurs merely because START was written. IRQ 131 is asserted
after the whole range is initialized. The C1.70 installed handler at VA
`0x106ee3e2` writes zero to `+0x1c`, then posts native event bit 2; the type-4
wait checks that same bit. Zero control, device reset and unrealization cancel
future work and deassert only this completion source. A cancelled job retains
any prefix that was actually transferred.

The native `/nikon/transfer/image-memory-init` fixture fails on the preceding
model's first `+0x10` write. The implemented profile clears an 8200-byte
interior range for both complete commands, preserves guard bytes, waits for real DMA
completion, acknowledges IRQ 131 and checks cancellation before and after a
chunk plus reset. Eight negative cases, including the obsolete OR-before
constructor values `1` and `0x201`, preserve memory and keep the interrupt
low. This is a synthetic device fixture; actual image API readback and a
complete benchmark capture require the separate original-code execution run.
