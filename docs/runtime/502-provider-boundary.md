# `0x50200000` provider boundary

This note extends the `0x50200000` runtime receipt with the recovered address
normalization and MXIC configuration path. It records source evidence only; it
does not install a new QEMU owner.

## Software address normalization

The MAS region table is rooted at `0xb2b4145c` and uses 12-byte records:

```text
[input_base, enabled, output_base]
```

The conversion wrapper at `0x103c5a7a` calls the interval query at
`0x103c5ad8`, checks record `+4` and `+8`, and returns either the original
input or:

```text
output_base + (input - input_base)
```

The initialized record relevant to this boundary is:

```text
input_base  = 0x50000000
enabled     = 1
output_base = 0x50200000
```

Recovered direct callers include `0x102416c4`, `0x10241728`,
`0x1024173c`, `0x102418e0`, `0x10241914`, and `0x1024192a`. Their common
shape is:

```text
descriptor = object[0xd0]
logical    = descriptor[0x14]
physical   = convert(logical)       // 0x103c5a7a
object[0x30] = physical
```

The descriptor pointer is also stored in the surrounding object. These callers
do not, in the recovered instruction windows, submit a DMA request, register
an IRQ, or bind the converted value to a QEMU or hardware provider. The
deepest recovered boundary is therefore:

```text
software normalization → object field +0x30
```

An indirect consumer may exist outside these direct windows; no such consumer
has been captured yet.

## MXIC configuration evidence

The original MAS startup body at `0x1002bd9c` writes the candidate region
configuration through the MXIC register block:

| Physical address | Value sequence |
| --- | --- |
| `0x20016020` | `(0x60000000, 0x50000000)` |
| `0x20016028` | `(0x50000000, 0xb0000000)` |
| `0x20016030` | `(0x50000000, 0x00000000)` |
| `0x20016038` | `(0x00030000, 0x40000000)` |
| `0x20016d50` | `(0x60000000, 0x50000000)` |
| `0x20016d58` | `(0x50000000, 0xb0000000)` |
| `0x20016000` | `1` (unit commit) |
| `0x20016d40` | `1` (unit commit) |

QEMU maps this device at `0x20016000`. The model uses these transactions to
enable or disable already attached, finite RAM endpoints. A region write does
not allocate the advertised range, create an alias, or establish a global DMA
address space.

The MXIC writes and the software table are related startup configuration, but
they do not prove that the conversion output `0x50200000` has a physical
backing. The current board still has no source-backed owner for
`0x50000000..0x51ffffff`; the unchanged C1.70 COPY request reaches
`0x50200000` and stops at the uncovered destination before completion.

## Current decision

The evidence closes the path:

```text
region table → conversion wrapper → descriptor/object field
MXIC startup writes → finite endpoint configuration
```

It does not close:

```text
object field → physical provider → DMA completion/IRQ → consumer
```

Until that final path is observed, adding generic RAM, a bridge alias, a
framebuffer, a synthetic provider, or a completion event would change the
firmware's behavior without a recovered hardware contract.

