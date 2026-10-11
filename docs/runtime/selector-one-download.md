# Selector-one download arena

The unchanged Z7II C1.70 MAS startup creates a second download arena at
`0xb0900000`. It is a fixed firmware arena derived from `0xaf200000`, separate
from the selector-zero arena at `0xb0d00000` and from the six declared storage
classes.

## Firmware evidence

The initializer at `0x103a34c0` receives the fixed arena base `0xaf200000` and
writes the runtime object at `0xb1808c98`:

| Object field | Value | Derivation |
| --- | --- | --- |
| `+0x04` | `0xb0d00000` | base + `0x1b00000` |
| `+0x08` | `0xaf200000` | arena base |
| `+0x0c` | `0xb0900000` | base + `0x1700000` |
| `+0x10` | `0xb0100000` | base + `0x0f00000` |
| `+0x14` | `0xaf8d4000` | base + aligned `0x6d4000` |

The selector-one routine at `0x103a364e` rounds each payload size to a 4 KiB
boundary, starts at `base + 0x5000`, and rejects an aggregate span above
`0x400000`. A read-only runtime trace records the three actual COPY requests:

| Source | Destination | Length | Arena offset |
| --- | --- | --- | --- |
| `0x107e00f4` | `0xb0905000` | `0x34238` | `0x5000` |
| `0x1081432c` | `0xb093a000` | `0x33300` | `0x3a000` |
| `0x1087a92c` | `0xb096e000` | `0x2cbc8` | `0x6e000` |

The rounded aggregate extent is `0x9b000`, below the fixed 4 MiB bound. The
firmware also performs the runtime range check
`[0xb0900000, 0xb1000000)` at `0x103a369c`.

## QEMU model boundary

`z7ii.selector-one-download` is a dedicated 4 MiB writable RAM region. It is
attached directly to the system memory AddressSpace used by the COPY device.
It is not an alias of `z7ii.download`, is not folded into a DDR storage class,
does not seed any bytes, and does not map the surrounding `0xb0800000` hole.
The adjacent selector-zero region remains independently owned at
`0xb0d00000`.

The focused qtest `/arm/nikon/memory/selector-one-download` copies all three
source ranges, checks their destination offsets and bytes, verifies the exact
arena end, and proves that the adjacent selector-zero region does not alias
the selector-one arena. The test uses a deterministic guest ROM fixture only
to exercise the COPY transport; it is not a camera image or a prefilled
download buffer.


## Runtime validation

A 12-second multi-thread TCG run of the unchanged C1.70 MAS payload using this
model crossed all three selector-one COPY destinations. It did not report a
`b0905000` fault; execution continued to a later unrelated COPY boundary at
source `0x122acf40`, destination `0x50200000`, with `0x27800` bytes remaining.
The later address remains uncovered and is outside this change's ownership.
