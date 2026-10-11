# Runtime receipt: indirect `object + 0x30` reader

This receipt records a read-only GDB run against the unchanged decoded C1.70
MAS payload while running the complete decoded bundle. The run used the QEMU
binary produced from the current `main` lineage, with:

```text
-M z7ii-mas-diagnostic,bundle=decoded-container.bin
-accel tcg,thread=single -display none -serial none -monitor none
```

Breakpoints covered the recovered reader at `0x102a84cc`, its gate at
`0x102a80a8`, the path functions at `0x102a8076`, `0x102a7ee2`,
`0x102a7d4c`, `0x102a7c64`, the request builders at `0x102a7764` and
`0x10293362`, and the callback/finalizer addresses listed in the static
consumer receipt. No guest memory, event, IRQ, DMA completion, or framebuffer
was injected.

## Observed reader values

The reader at `0x102a84cc` was reached twice before the run reached its COPY
fault boundary. Both reads were from runtime-allocated object instances, not
the literal table root address:

| PC | object (`r4`) | `object+0x30` | `+0x0c` | `+0x10` | `+0x28` | `+0x2c` | LR |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `0x102a84cc` | `0xb51b94f0` | `0x00020000` | `0x00000000` | `0x133e0000` | `0xb185e974` | `0xb185e4f8` | `0x103bf42b` |
| `0x102a84cc` | `0xb51e5f50` | `0x00020000` | `0xb2b40000` | `0x133e0000` | `0xb185e974` | `0xb185e4f8` | `0x103bf42b` |

The corresponding first gate call had:

```text
pc=0x102a80a8 lr=0x102a84d3
r0=0x00020000 r1=0 r2=0xb51b94c8 r3=0xb51b94dc
r4=0xb51b94f0 r5=0 r6=0xb185e960
```

The gate was also called for unrelated request records with values such as
`0x00018d80`, `0x00180000`, `0x000e0000`, `0x0024fff8`, and `0x000dfff8`.
These records are retained in the raw run log but were not attributed to the
`0x50200000` COPY.

The recovered object words around the first reader were:

```text
0x00=0x00000000  0x04=0xb185e4f8  0x08=0x00000008  0x0c=0x00000000
0x10=0x133e0000  0x14=0x00000000  0x18=0x00020000  0x1c=0x00020000
0x20=0x00000000  0x24=0x00000008  0x28=0xb185e974  0x2c=0xb185e4f8
0x30=0x00020000  0x34=0x103bf48b
```

The value read at `object+0x30` was therefore `0x00020000`, not
`0x50200000`. This reader instance cannot be used as evidence that the
`0x50200000` destination is a framebuffer or DMA buffer.

## Downstream result

No breakpoint fired at `0x102a8076`, `0x102a7ee2`, `0x102a7d4c`,
`0x102a7764`, `0x10293362`, `0x102c426c`, `0x103a6708`, `0x102c429c`,
`0x103a67a2`, or `0x102c64fe` before the unchanged bundle reached these
runtime faults:

```text
COPY first DMA fault: source=0x10000000 destination=0xc8200000
COPY first DMA fault: source=0x122acf40 destination=0x50200000
                 remaining=0x00027800
```

The run therefore proves that the indirect reader exists and that its live
objects carry a small normalized value, but it does not reach the request
builder or any provider-side transaction for the `0x50200000` copy. There is
no runtime evidence here for an AddressSpace owner, DMA channel, IRQ, or
completion path.

## Boundary and next action

The strongest reproducible boundary is now:

```text
object + 0x30 (runtime values 0x00020000 in this run)
  -> 0x102a80a8 gate
  -> later request builder: not reached before COPY fault

0x122acf40 -> 0x50200000 COPY
  -> source-backed physical provider: not observed
```

This receipt does not justify adding RAM, an alias, a DMA bypass, an IRQ, a
completion event, or a framebuffer at `0x50200000`. The next useful run must
first clear the `0x50200000` source-backed provider boundary, then repeat the
same reader/builder breakpoints to determine whether a later object instance
is connected to the image/display path.
