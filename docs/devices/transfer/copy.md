# RGBA layout transfer

The C1.70 MAS image preparation path uses the copy engine to convert linear
RGBA8888 words into GPU tiles before drawing. The finite model implements
both directions and optional origin reversal for 64×64, 128×128, 256×256 and
the existing 720×480 context. This is a source-backed functional protocol,
not a measurement of the physical engine's timing.

## Native producer and layout

The original `VA10683d6c` image preparation body constructs control 2 at
`VA10683eb4`; `VA10683f2c` calls the thin `VA1064d48a` branch into the type-3
record builder, `VA1064d37c`. That builder preserves the image-owner callback.
The subsequent original `VA106ee148` writes 32-bit little-endian fields at
board `PA20180c00..c38` and starts with `c18=1`.

A read-only hardware-breakpoint diagnostic captured this complete original
descriptor at builder entry:

| Field | Hexadecimal value |
| --- | --- |
| Source / destination | `bcad51a0` / `bcad1180` |
| Secondary addresses | `0` / `0` |
| Control | `2` |
| Source / destination dimensions | `00400040` / `00400040` |
| Owner callback argument | `bbfb4560` |

The source contained the complete sequential benchmark pattern, including
`3c6ef3ff, 3c8859ff, 3ca1bfff`; the destination was zero. This is an upload
preparation request, rather than a readback or an arbitrary control-bit probe.
The original request had been rejected by the previous model and had not
completed.

The independent `VA1068369c` producer composes bit 1 from layout direction
and bit 0 from relative origin. Its software counterpart is `VA106db38a`:

| Low control bits, selector 0 | Source | Destination | Linear-end Y |
| --- | --- | --- | --- |
| 0 | Tiled | Linear | Unchanged |
| 1 | Tiled | Linear | Reversed |
| 2 | Linear | Tiled | Unchanged |
| 3 | Linear | Tiled | Reversed |

Tiles are 8×8 pixels, arranged row-major. Within a tile, the low three x/y
bits are interleaved, x first. The complete original software body was
executed for all four variants and all three benchmark sizes, with aligned
origins at offsets 0, 4 and 12. The 64×64 cases used the actual captured
source. All 12 byte arrays and guards matched an independent bit-interleaving
calculation. In particular, the captured control-2 case's output word checksum
was `d7176dc5` under the fixture's explicit FNV-1a word convention. This is
an oracle comparison, not an original guest completion claim.

## Drawing and native readback

The exact texture format `156=001b0100` consumes tiled `RRGGBBAA` words.
The sampler converts those words to its internal RGBA byte representation;
the separately established A8 path remains unchanged.

The private 256×256 pbuffer's captured surface has layout `3fdf`, origin 0
and native format 0. Executing the original `VA105a701c` against its captured
surface and format objects returns pixel order 1. `VA106da650` reads its
tiled pixels; `VA10682ab8` decodes order 1, and `VA10684d36` writes the requested
OpenVG format 1. Three non-symmetric original-body probes verified, for example,
physical word `ff563412` returning application word `123456ff`.
The model therefore writes that pbuffer as tiled RGBA bytes. The existing
720×480 display-output contract retains its distinct representation.

## Lifecycle and validation

The added selector-0 profiles require aligned, non-overlapping, directly
backed primary ranges and no secondary planes. Unknown bits, conversion
selectors and shapes outside the stated finite contexts are rejected.
Control 8 retains the separate raw-byte contract. Each timer batch transforms
one eight-row band, rechecking source backing and destination writability.
IRQ130 is asserted only after the final write. Mapping/DMA failure gives no
completion; ACK, reset and unrealize cancel pending work. Clock and batch
latency are functional assumptions.

The native `/nikon/transfer/tiled-copy` fixture covers all 12 oracle outputs
and negative alignment, geometry, selector, unknown-bit, overlap, alias,
readonly, mapping-loss, cancellation and reset cases. The private-image fixture
actually executes control 2, consumes its tiled output in drawing, and verifies
the tiled target and allocation guards. These fixtures remain separate from
the full firmware collection and physical-camera acceptance.
