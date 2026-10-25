# Kind40 storage and address conversion

The subsequent source-stream boundary is now resolved by the
[resource11/12 producer and storage join](rows.md), including all eight layouts.
With explicit valid empty-list inputs, the next rejection is system
configuration PA`04280064`. The paragraphs below retain the kind40 checkpoint.

The normal GUI initializer at MAS Thumb VA`10238314` requests allocation kind40
(decimal) through `1030d96e(0,40,1c8000)`. It retains the returned object in
`b26b48d8` and passes the physical pointer reached through object+4 to
`103c5a7a`. The selector `1030d85c` retains kind40; its shared return tail must
be included when inspecting this function. This is a guest-owned allocation,
not a framebuffer publication or a display-ready signal.

In the unchanged execution, the holder points to `b2c87608`, object+4 points
to `b2c87528`, and that cell contains PA`62059800`. The translator's range
lookup `103c5ad8` selects record15 at `b2b41510` from the table at `b2b4145c`:

| Field | Original value |
| --- | --- |
| Physical class start | `5f900000` |
| Conversion type | `1` |
| Logical class start | `34600000` |
| Next class start | `62700000` |

The translator therefore returns `34600000 + 62059800 - 5f900000 = 36d59800`.
The original CPU's secure TTBR0 is `b2b74000`, TTBCR is0, and the L1 descriptor
for VA`36d59800` is `62014426`. Its section translation independently gives
PA`62000000 | 00059800 = 62059800`. The address conversion is correct; the
physical storage backend was missing. A class conversion record or PTE alone
does not establish ownership of the complete46MiB class.

The board now backs only `[62059800,62221800)`, length`1c8000`, with ordinary
QEMU RAM. Initial zero is an explicit functional storage assumption; bytes
persist across machine reset. There is no physical bus mirror at VA`36d59800`,
no page-table installation, no allocation-result injection and no MXIC class
expansion. The guest remains responsible for allocation, clearing and content.

The later [kind41 workspace](storage.md#kind41-processing-workspace) starts
at its exact upper boundary. It is a separate source-owned pool, not an
extension of kind40. Historical outer-hole observations below predate that
independent mapping; current fixtures preserve distinct values on both sides.

The native old-model test fails on the first RAM readback. All19 cases in
`nikon_memory` then pass, including first/middle/last independent cells, exact
outer holes, no physical VA mirror and reset retention. Qtest checks physical
storage; the original PTE observation establishes CPU translation separately.
Receipts are in `run kindforty/` (`native-red.log`,
`native-green.log`, `trace.json`, `translation-calls.json`, `green.json` and
the source/TCG records).

With the unchanged bundle, explicit TEMP/acceleration records, raw axes
`(0,0,1024)`, RNG input0, complete UART peer and held cold F4, the original
`10361034` clear now returns to `10238364`. The next first uncovered access is
CPU0 VA PC`1023c3fe`, READ32 PA`50560000`. This is inside `1023c3d2`, called
from the same initializer: it copies up to128 words per row from three source
streams, stopping each row at `ffffffff`, and alternates streams every8 rows.
The producer, ownership, content and full bounds of these source streams remain
to be established; this read is not admitted as generic zero RAM.

The final post-stop application bytes remain02/01 and primary request state1;
gate, pending3 and display-ready remain0. The later BatCk-containing word is
`00010101` and power is0, distinct from the earlier healthy request-admission
checkpoint. No GUI first frame or scanout is established by this continuation.
