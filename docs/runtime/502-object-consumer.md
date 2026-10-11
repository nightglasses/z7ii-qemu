# `object+0x30` consumer boundary

This receipt follows the object field written by the MAS address-normalization
blocks that are adjacent to the `0x50200000` COPY boundary. It is based on the
unchanged C1.70 MAS assembly recovered in
`nightglass-firmware/src/mas_outer/mas_closure_10020000/assembly/000000.S` and
its literal resources. It does not add an address mapping or claim a DMA
provider.

## Object construction

The caller at `0x10241c6e` derives the object pointer as:

```text
sl      = *resource(0x10221ed0) = 0xb2a9adac
r7      = sl + 0x20
r3      = r7 + ((11 * index) << 3) = sl + 0x20 + index * 0x58
```

It then calls `0x102415d6` with `r3` as the destination object. The function
keeps `r4 = r3` and fills the object at offsets `0x00..0x2c`, followed by
`0x34..0x64` in the mode-specific blocks.

The resource literals used by this family are:

| Firmware object | Value | Use in this family |
| --- | --- | --- |
| `0x102216fc` | `0xb2a9ad9c` | SoC/configuration pointer table |
| `0x1022170c` | `0xb18a729c` | callback/metadata pointer used by the mode blocks |
| `0x10221714` | `0xb18a6ef8` | secondary metadata table |
| `0x10221ed0` | `0xb2a9adac` | object table root |
| `0x10221ed4` | `0xb18a6ef8` | same secondary metadata table |

## `+0x30` writes

The following recovered blocks all have the same shape:

```text
descriptor = object_source[0xd0 or 0xd4]
logical    = *descriptor[0x14]
physical   = mas_fn_103c5a7a(logical)
object[0x30] = physical
```

The blocks are `0x102416c4`, `0x10241728`, `0x1024173c`, `0x102418e0`,
`0x10241914`, and `0x1024192a`. Their remaining instructions store the source
descriptor pointer in a small index table or branch to the common mode tail.

The surrounding recovered object family contains no direct load from the same
object's `+0x30` after these stores. It continues to populate `+0x34..+0x64`
and then returns or enters a mode loop. The `0x10241cc8` block reads
`b18a6ef8 + 0x10 + index*4` only as a pointer-presence check; it does not read
the constructed object's `+0x30`.

## Consumer and owner result

The object pointer escapes through the `b2a9adac + 0x20` table and the
`b18a6ef8` metadata table. This is an indirect registration/lookup boundary,
not a provider binding. The recovered windows contain no call that:

- submits a COPY/DMA request using `object+0x30`;
- registers an IRQ or completion callback for the field;
- installs a QEMU/board AddressSpace or a physical memory owner; or
- proves that the converted value is a framebuffer or image buffer.

The current unchanged-bundle runtime receipt still stops at the separate COPY
request to `0x50200000` before an indirect reader of this object family has been
captured. Therefore the strongest reproducible boundary is:

```text
descriptor[0x14]
  -> 0x103c5a7a address normalization
  -> object table entry +0x30
  -> indirect table consumer not yet observed
```

This evidence does not justify adding RAM, an alias, a DMA completion, an IRQ,
or a framebuffer at `0x50200000`. The next runtime run must break on the
object-table lookup/registration path and record the object pointer, the first
read of `+0x30`, and any provider-side transaction before changing QEMU.

