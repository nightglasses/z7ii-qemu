# Repeated full-target path clipping

The C1.70 Retouch menu's initialized two-path drawing job uses 336 setup
bytes and 608 draw bytes. Its second full-path draw repeats a five-word
clipping update between the existing 24-word state prefix and 31-word path
latch. The preceding vector parser expected the latch immediately and
rejected the second stream's START at MAS VA `0x106ee3c6`, board PA
`0x201810e0`, with `transfer-path-program-not-supported`.

The independently captured setup and draw operands select RGBA 720x480,
target `0xbec5f200`, precision-two geometry `0x07800b40`, two distinct path
scratch operands and paired keys `0x10001/0x10002`. The inserted block is:

| Command | Operand | Meaning in this profile |
| --- | --- | --- |
| `0x00c0f00e` | `0x02cf0000`, `0x01df0000` | Inclusive X range0..719 and Y range0..479 |
| `0x01001004` | `1` | One clip rectangle |

The native producer at `0x106b5636..0x106b5664` loads X/Y and unsigned
width/height from a rectangle, computes `x+width-1` and `y+height-1`, and
packs each minimum into the low16 bits and inclusive maximum into the
high16 bits. It submits twice the rectangle count as coordinate words at
`0x106b566c` and the count separately at `0x106b5684..0x106b568c`.

The finite vector model now recognizes this complete repeated full-target
block, independently of the optional color-transform-disable update. Each
may occur once at the state seam, in either order. The range and count must match the established720x480 target;
the model does not discard arbitrary clipping state. Different ranges,
counts or command encodings remain explicit errors. The admitted block
restates the full drawing region, so all path coverage and composition still
run through the existing rasterizer. Validation of the complete batch occurs
before any target bytes change or IRQ129 is raised.

The generated native `initialized-paths` fixture reproduces the336/608
schema without embedding captured firmware arrays. It additionally covers
the independent color-disable plus clipping combination, immutable command
snapshots, X/Y/count/header/geometry/format rejection, output mapping loss,
reset and unrealization. The prior-image variant runs through `path-state`.
These are device-model regressions; acceptance of the modified benchmark
package and physical-camera execution require their separate receipts.
