# The 0x50200000 arena and object consumer evidence

This page consolidates six historical investigations without merging their
claims. Code and object addresses are MAS VAs; physical COPY destinations
are PAs. The bounded [bundle loader contract](../startup/bootshim.md) owns
the declared COPY storage; this does not identify a physical framebuffer or
prove that every software `object+0x30` belongs to that arena. The observed
reader value below is `0x00020000`, not `0x50200000`.

## Read the evidence by boundary

| Boundary | Evidence |
|---|---|
| Physical destination and original ownership question | [Owner](#owner), [provider](#provider) |
| Original COPY request and its checkpoint | [COPY consumer](#copy) |
| Object construction and address conversion | [Object construction](#objects) |
| Linked software request chain | [Static consumer](#static) |
| Actual live object values and unreached downstream calls | [Runtime reader](#runtime) |

The sections retain their original run scope and unresolved questions.
They are historical receipts, not the current [startup frontier](../validation/frontier.md).

## `0x50200000` ownership boundary {#owner}

This note records the current boundary for the first multi-thread MAS COPY
request that reaches physical address `0x50200000`. It is an evidence record,
not a request to map an unverified RAM window.

### Runtime four-tuple

The unchanged Z7II C1.70 MAS run reached the following request before the
address was rejected by the QEMU uncovered-address owner:

| Field | Value |
| --- | --- |
| COPY channel | `0` |
| source | `0x122acf40` |
| destination | `0x50200000` |
| remaining length | `0x27800` |
| submit callsite | `0x10244966` → `0x103a3b66` |
| descriptor | `0x122d4740` |
| descriptor first word | `0x27800` |
| secure TTBR0 | `0xb2b74000` |
| L1 entry address | `0xb2b75408` |
| L1 entry | `0x50214422` |
| translation | `VA 0x50200000 → PA 0x50200000` |

The initial table copy also contains identity section entries from
`0x50000000` through `0x51ffffff`. A page-table entry proves the guest's
translation choice; it does not prove a physical RAM, device or DMA owner.

### QEMU owner audit

The current machine and device graph was checked at source level:

| Candidate | Source path | Result |
| --- | --- | --- |
| Storage classes | `hw/nikon/machines/z7ii.c:z7ii_storage` | Six RAM regions cover `0xb1800000..0xbd200000`; none covers `0x50200000`. |
| Selector-zero arena | `hw/nikon/machines/z7ii.c:z7ii_init` | Dedicated RAM is `0xb0d00000..0xb1100000`; no overlap. |
| Selector-one arena | `hw/nikon/machines/z7ii.c:z7ii_init` | Dedicated RAM is `0xb0900000..0xb0d00000`; no overlap. |
| MXIC endpoints | `hw/nikon/devices/mxic.c:nikon_mxic_attach_ram` | Only the six `z7ii_storage` regions are attached. `initial_regions` are decode transactions and allocate no memory. |
| Bridge peer aperture | `hw/nikon/machines/z7ii.c:peer_window` | Alias is `[0x40000000, 0x50000000)`; the exclusive upper bound excludes `0x50200000`. |
| SysBus and other board regions | `hw/nikon/machines/z7ii.c:z7ii_init` | No MMIO or subregion is installed in `0x50000000..0x51ffffff`. |

Therefore no existing source-backed endpoint can be reused for this request.
The MXIC profile's pair beginning at `0x50000000` is a virtual region-decode
transaction used to enable already-attached finite endpoints. It is not a
claim that the complete 32 MiB identity window is installed RAM.

### Required next evidence

Before changing the QEMU address map, the unchanged firmware run must identify
the physical provider for this request. The next receipt must include:

1. The code that creates or enables the `0x50000000..0x51ffffff` backing,
   including the relevant MXIC or board-decode registers and reset state.
2. The COPY master's address-domain/security attributes for this transaction,
   showing that it reaches the same endpoint as the CPU identity mapping.
3. A successful provider-side read/write or a classified hardware fault that
   distinguishes absent, disabled and protected memory.
4. A consumer or completion event after the copy, so the region is tied to a
   real firmware object rather than inferred from the PTE alone.

Until those facts are captured, adding generic RAM, a bridge alias, an MXIC
enable, a host-seeded buffer or a framebuffer at this address would change the
firmware's behavior without a recovered hardware contract.

### Current decision

The address remains an explicit uncovered boundary. No QEMU model change is
justified by the current evidence.

## `0x50200000` provider boundary {#provider}

This note extends the `0x50200000` runtime receipt with the recovered address
normalization and MXIC configuration path. It records source evidence only; it
does not install a new QEMU owner.

### Software address normalization

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

### MXIC configuration evidence

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

### Current decision

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

## 0x50200000 runtime consumer boundary {#copy}

This note records a read-only execution trace of the unchanged Z7II C1.70 MAS
payload. It describes the firmware's address interval lookup and the later
COPY submission. It does not assign a QEMU owner to the destination.

### Observed COPY request

The first uncovered request reaches the firmware COPY submit wrapper with:

| Field | Value |
| --- | --- |
| Submit callsite | `0x10244966` |
| Submit wrapper | `0x103a3b66` |
| Channel | `0` |
| Source | `0x122acf40` |
| Destination | `0x50200000` |
| Length | `0x00027800` |
| Descriptor | `0x122d4740` |
| Descriptor word 0 | `0x00027800` |
| Descriptor words 1–3 | `0x00000000` |

The payload is unchanged and the observer did not write guest memory or inject
an event, IRQ, completion, or framebuffer. QEMU reaches the COPY bus-error
boundary on the destination write. The firmware continues after status `1`, so
the QEMU first-fault record and the firmware's later submissions are separate
observations.

### Firmware interval table

The firmware builds an address interval table rooted at `0xb2b4145c`.

| Operation | Address |
| --- | --- |
| Builder entry | `0x103c585c` |
| Builder region writer | `0x103c58fc` |
| Query entry | `0x103c5ad8` |
| Query loop | `0x103c5ae0` |
| Next-record threshold load | `0x103c5ae8` |
| Query return address calculation | `0x103c5af0` |
| Record size | `0x0c` bytes |

The query starts with `index = 0`, computes `root + index * 0x0c`, and loads
the next record's first word at `record + 0x0c`. It subtracts one from that
threshold and advances while the result is below the input. The return value is
`root + index * 0x0c`. This is an address-class lookup. It does not by itself
identify a RAM bank, device, DMA AddressSpace, or completion provider.

The observed threshold sequence includes:

```text
0x14000000  0xb5f00000  0xb7d00000  0xbc100000
0x19c00000  0x50300000  0x50700000  0x5f900000
0xbd200000  0x50000000  0x50200000  0x50300000
0x50700000
```

The twelve-byte records use three words, `[input_base, enabled, output_base]`.
The conversion wrapper is `0x103c5a7a` (the frequently copied `0x103a5a7a`
spelling is not a function entry in the MAS image). It saves the input, calls
the query at `0x103c5ad8`, checks record `+4`, then checks record `+8`. If the
record is disabled or has a zero output base it returns the original input;
otherwise it returns `output_base + (input - input_base)`. The sibling helpers
`0x103c5a96`, `0x103c5ab0`, and `0x103c5ac6` call the same query and consume
the record `+4`/`+8` fields for predicates or status. The relevant initialized
records are:

| input base | enabled | output base | next input base |
| --- | ---: | --- | --- |
| `0x50000000` | `1` | `0x50200000` | `0x54900000` |
| `0x54900000` | `1` | `0x62700000` | `0xaf200000` |
| `0xaf200000` | `1` | `0xafa00000` | `0xb0000000` |
| `0xb0000000` | `1` | `0xb1000000` | `0xb1800000` |
| `0xb1800000` | `0` | `0xbaa00000` | `0xbb300000` |
| `0xbb300000` | `1` | `0xc3e00000` | `0xcc700000` |

This makes `0x50200000` a software conversion output base for the logical
class beginning at `0x50000000`. It does not identify a RAM bank, a display
buffer, a DMA AddressSpace, or a completion provider.

During the runtime query that was captured, the firmware searched for target
`0xbaa00000` at index `0x0a`; the next-record threshold load at `0x103c5ae8`
returned `0x50200000`, and the search continued. This shows that the table
contains a boundary at `0x50200000`; it does not show that the boundary is
backed by a guest RAM region.

### QEMU ownership conclusion

The current machine map has no source-backed owner for
`0x50000000..0x51ffffff`. The active guest translation observed for the COPY
destination is identity (`0x50200000 -> 0x50200000`), which only proves the
guest page-table mapping. It does not supply physical storage or an external
device provider.

Therefore the first unmodeled operation remains the COPY DMA write to
`0x50200000`. No RAM alias, framebuffer, device window, completion IRQ, or
synthetic provider is justified by this evidence. The next required evidence
is a runtime producer/consumer that assigns an owner and establishes its
capacity and access semantics.

## `object+0x30` consumer boundary {#objects}

This receipt follows the object field written by the MAS address-normalization
blocks that are adjacent to the `0x50200000` COPY boundary. It is based on the
unchanged C1.70 MAS assembly recovered in
`nightglass-firmware/src/mas_outer/mas_closure_10020000/assembly/000000.S` and
its literal resources. It does not add an address mapping or claim a DMA
provider.

### Object construction

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

### `+0x30` writes

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

### Consumer and owner result

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

## `object + 0x30` static consumer receipt {#static}

This receipt records the static follow-up from the six MAS conversion callers
documented in `arena502.md#provider`. It is intentionally limited to
references that can be recovered from the decoded C1.70 MAS payload. It does
not assign a QEMU memory owner and it does not authorize a synthetic DMA
completion.

### Input and method

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

### Confirmed reader and call chain

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

The `0x10293362` body gives the request shape: it allocates a private
`0xe0`-byte object, copies 24 bytes from the value passed in `r5` (the value
loaded from `object + 0x30`) into request offset `+0xa0`, and stores the source
object's `+0x28`, `+0x04`, and `+0x2c` at request offsets `+0xd4`, `+0xd8`, and
`+0xdc`. The source object's byte `+0x0c` selects callback pairs
`0x102c426c → 0x10365466` or `0x102c429c → 0x103a67a2`. The finalizer at
`0x10293312` clears/copies status fields, optionally calls `0x102c64fe`, and
frees the private request object.

The callback wrappers classify this as a software request/event lifecycle:
`0x10365466` operates on the callback object's `+0x28` synchronization field
through `0x10348c06` and `0x103b3dc0`; `0x103654cc`, `0x103654dc`,
`0x103654e4`, `0x103654ec`, and `0x103654f4` operate on its `+0x14c`
event/synchronization block through the `0x103410cc/d2/da/116/156/192`
primitives. The `0x102c429c` branch builds a 28-byte event record and uses
`0x103654dc` with a `0x1388` timeout. None of these wrappers access a physical
address window, a DMA channel, or a frame completion register.

The direct reader therefore establishes this boundary:

```text
converted physical value in object +0x30
  -> state/callback object dispatch
  -> local request construction
  -> internal builder / submit helper
```

It does not establish a physical provider for `0x50200000`.

### Exclusions

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

### Remaining boundary

The static result is a real consumer, but the provider is still unresolved:

```text
object +0x30
 -> 0x102a80a8 / 0x102a8076
 -> 0x102a7ee2, 0x102a7d4c, 0x102a7c64
 -> 0x102a7764 / 0x10293362
 -> 0xe0-byte request object + callback pairs
 -> software event/synchronization lifecycle
 -> provider, DMA channel and completion semantics: not part of this chain
```

No QEMU RAM, alias, framebuffer, DMA bypass or completion event should be
added from this receipt alone. The next useful evidence is the runtime value
and object identity at `0x102a84cc`, followed by the provider consumed by
`0x10293362`.

## Runtime receipt: indirect `object + 0x30` reader {#runtime}

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

### Observed reader values

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

### Downstream result

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

### Boundary and next action

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
