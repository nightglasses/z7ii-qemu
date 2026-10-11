# `object + 0x30` static consumer receipt

This receipt records the static follow-up from the six MAS conversion callers
documented in `502-provider-boundary.md`. It is intentionally limited to
references that can be recovered from the decoded C1.70 MAS payload. It does
not assign a QEMU memory owner and it does not authorize a synthetic DMA
completion.

## Input and method

The input is the decoded MAS payload
`02_eg1840_mas_01700.payload.bin`, loaded at `0x10000000`. The payload was
disassembled as ARM Thumb code with `arm-none-eabi-objdump`; the scan first
selected word loads using an immediate offset `0x30`, then followed the
register/dataflow windows from the six conversion callers:

```text
0x102416c4  0x10241728  0x1024173c
0x102418e0  0x10241914  0x1024192a
```

Literal `#0x30` loads elsewhere in the image were not attributed to this
object unless the input register was linked to the object field or a recovered
caller/callee chain. Byte/halfword loads, stack offsets and unrelated EX/SLA
structures were excluded.

## Confirmed reader and call chain

One linked reader is present in the MAS image:

```text
object + 0x30
  0x102a84cc: ldr r0, [r4, #0x30]
  0x102a84ce: bl  0x102a80a8
                 |
                 +-- state gate on argument +0x10
                 +-- argument +0x0c -> child +0x418
                 +-- 0x102a8076
                       +-- 0x102a7ee2
                       +-- 0x102a7d4c
                       +-- 0x102a7c64
```

The called routines operate on a software object graph. They read state and
callback fields (`+0x0c`, `+0x10`, child `+0x418`, and related `+0x41c`
fields), construct local command/request records, and call the internal
builder `0x102a7764` followed by `0x10293362`. The recovered windows do not
call `0x103a3b66`, the COPY submit wrapper, nor an identified DMA register
block or IRQ completion routine.

The direct reader therefore establishes this boundary:

```text
converted physical value in object +0x30
  -> state/callback object dispatch
  -> local request construction
  -> internal builder / submit helper
```

It does not establish a physical provider for `0x50200000`.

## Exclusions

The six conversion caller windows only write the normalized value into the
object field; they do not themselves read it back. The following were not
accepted as consumers:

* unrelated `ldr/ldrh/ldrb [register, #0x30]` instructions whose base register
  cannot be linked to these objects;
* stack/local structures using offset `0x30`;
* EX and other component listings;
* the generic COPY wrapper at `0x103a3b66`, which is reached by a separate
  descriptor submission path and has no static read of this field in the
  recovered window;
* page-table identity mappings and MXIC region descriptors, which describe
  address intent/configuration rather than a QEMU `MemoryRegion` owner.

## Remaining boundary

The static result is a real consumer, but the provider is still unresolved:

```text
object +0x30
 -> 0x102a80a8 / 0x102a8076
 -> 0x102a7ee2, 0x102a7d4c, 0x102a7c64
 -> 0x102a7764 / 0x10293362
 -> provider, DMA channel and completion semantics: not recovered
```

No QEMU RAM, alias, framebuffer, DMA bypass or completion event should be
added from this receipt alone. The next useful evidence is the runtime value
and object identity at `0x102a84cc`, followed by the provider consumed by
`0x10293362`.

