# Independent RGB grid processing

Original C1.70 MAS context8 submits a separate eleven-node graph from three
statistics-produced RGB planes to a kind25 buffer. Its normal completion
releases a software work-graph dependency. The current gate model imports
these commands but executes only the independent context9 MAIN graph.
This page records the source and execution boundary; it does not describe an
implemented grid operator or physical image-processor specification.

## Missing completion and retirement

The normal five-CPU MTTCG cold-neutral profile at `aafb8aba` still aborts in
the General-pool aligned allocation wrapper at MAS VA `0x1034fe78`, DFAR
`0x3c`, after 39.69 host seconds. The underlying allocator returns zero;
the wrapper aligns it and attempts its metadata write. After the OnVd core,
VA `0x103d0db0` allocates a `0x2cc` descriptor and `0x3e44` work record per
request. Retirement requires every one of eleven software lanes to finish.
Read-only translated snapshots observe active-record counts 58, 326 and 575,
while both submission lists are empty. These are sequential observations,
not an atomic throughput measurement.

The oldest sampled lane2 record has passed microstep0 and waits in microstep1
at VA `0x102682ae`. Its actual wait call is at VA `0x1026830e`: event
`0xb1864850`, pattern `0x81000000`, timeout 1000 in the native wait API.
Bit `0x01000000` is normal completion; `0x80000000` is cancellation.
A separate eight-PC diagnostic trace
records sixteen waits, all timing out, and zero entries to completion callback
VA `0x10262dfc`. That callback decrements an initial discard counter or, once
it is zero, sets the normal event bit through VA `0x103610cc`. The discard
counter remains one in a separate read-only snapshot because the callback
has not entered. Lane0 and later records then wait on lane2's dependency.

The [curated diagnostic receipt](/evidence/runtime/grid.json) records the
probe filter, timeout registers and sampled interrupt results.
The trace records GDB global thread numbers 1/2; zero callback entries is
qualified to that filter. Host durations include debugger stops.

The source route, with MAS addresses expressed as VA, is:

| Actual producer or consumer | Condition and result |
| --- | --- |
| Gate ISR `0x103bce38` | Reads PA `0x20100020`, raw2 |
| Whole-frame callback `0x103b5148` | raw2 bit2 maps to flags bit0; raw2 bit3 maps to flags bit7 |
| Dispatcher `0x10388916` / `0x10388880` | Selects context8 for the first route, context9 for the second |
| Callback bridge `0x102e1c9a` / `0x102e3f04` | Resolves the owner's request packet and its callback |
| Context8 packet callback `0x10262dfc` | After the initial discard, publishes `0x01000000` |

The diagnostic trace observes 192 sampled ISR entries with raw2 bit3 and
none with bit2. The existing MAIN executor's bit3 is appropriate to context9;
adding bit2 to that same transaction would acknowledge the unexecuted graph.
Neither a common cause with the separate AF history overwrite nor complete
mode-change recovery has been established.

## Graph and activation

The context8 graph is source-declared and directly captured through its
request packet. Its principal type/resource path is:

`0/1 → 4/37 → 15/69 → 12/65 → 17/72 → 17/73 → 11/61 → 1/20 → 7/40 → 10/53 → 0/2`.

This shorthand is not the complete topology: type7/resource40 also connects
back to type11/resource61's second input. The original builders at VA
`0x103b3226` and `0x103b4214` establish those outgoing/incoming references.
The edge may be a filter/control handshake; it does not by itself establish
temporal RAM feedback or permission to add history.

The eight imported command ports at PA `0x20100000` plus offsets `0x800`,
`0xd00`, `0xa40`, `0xa00`, `0xa20`, `0xdc0`, `0x880` and `0x9c0` retain their
source-shaped packets. They are distinct from the ten context9 MAIN ports.
Ordinary start bits alone do not establish this continuous request.

VA `0x10264972` activates unit8 only for frame-mode values 2, 6, 7 and 29.
It aligns and rotates the frame's RGB-descriptor queue, prepares four input
and output phases through VA `0x102643e2`, submits through VA `0x102bc1ce`,
then installs the phase map through VA `0x102bddbc`. The later
`0x102be02c` update toggles a map/control bank. It is not another MAIN producer.
For unit8, packet byte51 selects input0; VA `0x10389b30` updates PA
`0x20102004.bit0`, and VA `0x103bd188` selects the map at configuration offset
`0x2200` or `0x2300`. This map bank is distinct from phase and input freshness.

Callback flags bit2 sets a map-refresh latch at VA `0xb18648c8`; the waiting
worker then rotates and rebuilds four maps. The source raw2-to-flags table
maps raw2 bit6 to that flag. A four-successful-phase watermark could explain
its production, but that cadence remains a hypothesis requiring actual work,
DMA and phase tests. It cannot be emitted merely because four timers elapsed.

The observed continuous input words at PA `0x20102000/04/10/14/18` are
`0x131`, `0xc2`, `0x74011`, `0x4100`, `7`; the independent unit1 controls at
PA `0x20102640/44/48/4c/50/58` are `0x202`, `40`, `3`, `2`, `0`, `3`.
VA `0x103bd1e0` binds that control bank to the packet's type7/resource40
selection; the decimal value40 is a resource identifier, not the image height.
These are observations of the selected profile, not a universal register
equation or permission to accept arbitrary control combinations.

## Input and output ownership

The source allocator at VA `0x1030af00` reserves six kind19 units of `0x36000`
bytes. Descriptor construction at VA `0x1030ae70` exposes three planes at
relative offsets `0`, `0x12000`, `0x24000`, with little-endian 16-bit cells and
pitch aligned to 16 bytes. Statistics DMA endpoints2/3/4 supply these planes.
The observed bases are PA `0x5d1dc000`, `0x5d1ee000`, `0x5d200000`, each
with unit stride `0x36000` and six slots. The four selected phases are a view
of that ring, not four independent new frames. The active slot1 shape is
60 × 40 with 128-byte plane pitches.

Kind25 is allocated by VA `0x1030bb26`; VA `0x1030ba84` constructs one-base
descriptors with pitch `align16(3 * width / 2)`. The observed slot2 is 60 × 40,
pitch96. Its four `0xd800`-byte units cover PA `[0x5f914400,0x5f94a400)`.
The active row data totals 3600 bytes within a 3840-byte pitched footprint per
selected unit; the allocation does not authorize writing its padding or
unused tail. The source formula establishes
1.5 bytes per cell, but does not by itself distinguish packed12 scalar data
from an aggregate 12-bpp color format.

The same kind25 descriptors also reach paired registers `0x398..0x3a4` in
mode2. The current paired model interprets these as packed12 grid outputs;
that direction is a functional hypothesis. Simultaneous unit8 setup means
these cannot simply become two uncoordinated writers. The paired consumer
direction, output format and phase freshness must be joined before enabling
grid DMA or its completion.

## Numerical evidence and unresolved equations

The original packets are not all neutral. Type17 logical fields
`0x20..0x3c` pack sixteen signed 15-bit lanes. Identity templates establish a
4 × 4 arrangement; discarding its fourth row would be
unjustified. Resource72's first three rows fit a Q12 RGB-to-luma/chroma
transform with row sums 4096, 0, 0; its fourth row is zero.
Resource73 has different coefficients, bounds and a nonzero final control
block. Both packets originate in a preinitialized graph, with type17 resource73
at ROM VA `0x1247ca60`. The constructor at VA `0x103b49b0` patches graph
routing bytes in logical register0. Its changing low byte and byte2 do not
establish a numerical enable or bypass.

Type15's three gains 512, candidate shifts 9 and upper clamps `0x3fff` fit a
finite gain/shift/clamp operation. Type12 supplies six 1024-entry LUT banks;
the alternating banks form staggered monotone knots. Index scale, fractional
interpolation, clipping and rounding are not recovered CPU equations.
Type7's 17 halfword coefficients expand to a symmetric 5 × 5 kernel whose
sum is 2048, consistent with its three shift11 fields. Type4's four extra
configured rows fit a two-row halo around that filter, but do not establish
permission to read beyond the active input or the edge policy.

These mathematical fits support testable finite operators; they do not justify
ignoring resource73, applying an invented bypass or declaring the graph complete.
The existing statistics model emits 16-bit MAIN means; actual sampled values
exceed `0x3fff`. Source storage permits the full u16 domain. Type15 provides
explicit downstream clamps, so the model must consume the complete input
and apply a declared gain/clip operation rather than reject high values.
A 14-bit post-clamp LUT domain remains a numerical interpretation.
Unchanged-firmware graph retirement remains unaccepted.

The pinned [Z7 reference execution](https://github.com/WonderLantern/z7-qemu/blob/110c8ab93278e32870ae136533b5ee8b17db19b3/qemu/nikon-z7-isp.h#L48-L113)
directly generates MAIN NV12 and adds bit2 when its first command bank is
enabled. Its [port handler](https://github.com/WonderLantern/z7-qemu/blob/110c8ab93278e32870ae136533b5ee8b17db19b3/qemu/nikon-z7-lab.c#L1611-L1705)
only retains this graph's configuration. It provides no executed unit8 operator
or independent result DMA, so its combined completion is not transferable.
