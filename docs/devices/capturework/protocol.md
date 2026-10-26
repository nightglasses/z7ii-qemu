# Finite grayscale capture stream

The bundle's capture-work device consumes qualified camera input and writes
the paired output ring before publishing unit1/2/200 processing causes. This
is a finite grayscale model with explicit arithmetic assumptions; group
payload work and physical DSP fidelity remain separate boundaries.

## Source-established submission and storage

MAS VAs `1023994c ->10239900` connect the six-word flag200 packet to buffer
selector2. Descriptor byte24 passes through `1023c830`: value3 selects
encoding0, value2 selects2, others select1. The producer calculates row stride
as align16 of `2*width`, `(7*width)>>2` or `(3*width)>>1`, respectively.
This establishes16/14/12-bit byte geometry, without physical sample ordering.

Selector2 writes stride at PA `20069a08`, controls at `a14/a18/a24/a40/a4c`,
bank/start/count at `a3c/a44/a48`, and four addresses per set at `a50..a5c`
and `a60..a6c` (common PA prefix20069). The normal output is1380x918,
stride820 hex, initial index0, count4, with set0 addresses
`54900000/56b0d000/58d1a000/5af27000`. Each padded output spans1d22c0 hex.

Allocation table `11982938[16] ->119832a8` declares kind16 with total8834000
hex, unit220d000 hex, count4 and base54900000. All five mode override entries16
are NULL. The submitted addresses equal `base+i*unit`; the live address
classifier leaves this class unchanged. The board supplies exactly PA
`[54900000,5d134000)`, ending at kind17, as ordinary shared CPU/DMA RAM.
Reset preserves bytes. This does not grant the enclosing DDR class or use
the historical software translation table's earlier enabled mapping.

Unit1 uses commit1/run1. Its14 independently supplied flags are packed by
`102097fa`; observed PA `20068500=0` disables all of them, without proving a
physical whole-block bypass. Unit2 `1023a9d2 ->102094aa/10209578` selects ROM
geometry/coefficient packets, commit4/run units4 and8. Its observed geometry
is2760x918 to1380x918, with local PA `20069f00=11`. Paired flag200 uses
commit80/run200 and local PA `2006952c` bit1. Software flags, commit masks and
physical run masks are distinct namespaces.

Raw0.bit3/bit4 reach software units1/2 and FE fence masks100000/200000.
For paired flag200, `1023931c` consumes **raw4.bit7**, sets software bit200
and FE mask4000000. Raw7.bit8 posts another owner path, not this ACK.
Group raw3.bit0 / mask1000000 remains a separate producer.

## Implemented functional assumptions

The bundle's separate `capture-processing` device joins existing capture,
parameters, buffers and completed input. It adds no physical MMIO:

1. It consumes a fresh immutable LE16 grayscale acquisition whose input
   qualification held continuously from CMOS submission to completion. This
   reuses the declared shared virtual camera input, not a recovered physical
   main-stream wire.
2. Zero correction flags mean identity forwarding. Unit2 performs nearest
   sampling to its committed output geometry. The independently supplied RAW
   descriptor may apply a further positive integer reduction on either axis;
   both dimensions must be aligned, nonzero and no larger than unit2 output.
   Composing these two nearest stages gives the existing whole-image sampler
   exactly for integer reductions. ROM color/filter coefficients
   are retained without executing physical filtering or Bayer demosaic.
   Nonzero correction flags, crop offsets, expansion, nonintegral further
   reduction and unsupported enables do not qualify.
3. Paired control bit2 enables a256-entry LE16 LUT indexed by the high sample
   byte. Otherwise output uses the high16/14/12 sample bits. Consecutive
   samples form a little-endian bit stream. LUT arithmetic, ordering and
   sampling are **falsifiable functional hypotheses**, not recovered RAW
   packing. The shared9530 pair remains retained without physical arithmetic.
   Policy0 omits both its write and the LUT producer in original `1023994c`;
   neither is required for the direct high-bit packing path.
4. The job preflights every selected ring slot's writable-RAM span and
   pairwise non-overlap, then writes each actual row payload, preserving
   padding. Only after every row succeeds does it publish raw0 mask18 and
   raw4 mask80 through normal mask/GIC/W1C/ISR handling and advance the ring.

Both command ports latch independently. Staging/unselected commits preserve
the job; equal commits preserve ring phase. Changed modeled snapshots cancel
active work and prevent replay of already delivered input. All four run gates must
hold. Stop/disable/unit reset/system reset cancels the1ms virtual processing
timer without a data cause; restart cannot replay old input. Pending frontend,
paired and statistics data provide backpressure. The immutable job survives
source RAM reuse. Time/TG/commit alone cannot create a frame. Unsupported
geometry/LUT/format/ring/RAM/overlap/wrap rejects without completion.

There is no group completion, guest callback, cache write, display enable or
guessed953c-family result. Configuration retention alone remains silent.

The [paired pixel operator](../pairs/pixels.md) receives a typed RAW descriptor
only after every row is written. It records the actual final width, height,
pitch, packing, address, ring slot and monotonically increasing generation.
The descriptor is withdrawn before overwriting that slot; changed committed
configuration and reset invalidate all available descriptors. Already admitted
paired work owns a RAW snapshot, rather than mutable producer RAM.

### Independent geometry producers

MAS VA `1023994c` obtains the final width and height from its input descriptor
at software byte offsets40/42. Through `1020c2d4 ->102090a0`, these become PA
`20069518/1c`. Unit2's separate ROM selection at VA `1023a9d2` reaches
`102094aa/10209578` and PA `20069d00/04`, `20069e78/7c`. No source invariant
requires the final RAW height to equal unit2 output height.

The original paired-start mode supplies2760×1556 to1380×1556 at unit2, followed
by1380×778 RAW12. The old equality check rejected every such input and could
leave paired requests waiting despite earlier populated RAW RAM. Integer
reduction removes that model restriction as a class. Nearest sampling over
the whole image, its phase and omission of physical filtering remain explicit
functional choices; source establishes the independent dimensions, not the
physical decimation kernel. RAW metadata describes778 rows, each with2070
payload bytes and10 preserved padding bytes at pitch2080.

## Historical stream validation

The old model fails the new fixture at the absent processing device. The
kind16 fixture separately fails COPY with status1 instead of8 at PA549007f1.
The native batch passes32 Memory,25 CMOS and33 Sensor subtests. Subsequent
focused cases add three packing widths, second-bank selection, ring wrap,
equal commits, fresh input and backpressure. Literal12-bit bytes, row guards,
immutable snapshots, stop/disable/reset and negative RAM/configuration bounds
are checked separately from original firmware.

The [stream receipt](/evidence/imagerequest/stream.json) separates source,
assumptions and unchanged-firmware observations. A40-host-second CPU/nochain
cold-neutral run with explicit scene32/192/64/224 completes300 auxiliary
transfers,299 statistics jobs and299 new stream jobs, writing568177740 payload
bytes with no reported rejection/cancellation. Original setters100000,
200000 and4000000 execute. All32 observed OnVd mask5 resumes return1;
33 aggregate05b00000 waits still return0. Of its five requested stages,
group mask1000000 alone has no observed setter.

A separate low-trace40-second run produces297 stream jobs and sampled output
in all four slots. Its submitted LUT maps the scene values to464/918/617/972;
observed packed pairs are `d0011d/966339/699226/ccc33c`. Both heads still lack
a console and the computed GUI caches remain unpublished. Group kind23 payload
work, the FECLCTRK admission guard and any future paired numeric-result reads
remain open. These observations validate the declared functional profile,
not physical calibration or complete boot. Raw records stay ignored under
`run maininput/`; six loader COPY comparisons were not repeated here.

The final40-second run with frontend/paired backpressure enabled completes390
auxiliary transfers,390 statistics jobs and388 stream jobs, writing737300880
payload bytes, with zero reported rejection/cancellation. All36 observed OnVd
mask5 resumes succeed;38 aggregate waits still return0. The same nonzero packed
quadrants persist in all four slots. Counts under different trace filters are
not rates or a full stability claim; the remaining boundary is unchanged.
The final applicable native batch passes all32 Memory,27 CMOS and33 Sensor
subtests (92 total), including the added freshness/backpressure cases.
The additional no-LUT fixture first fails on unchanged sentinel42405 instead
of sample0: requiring a write to optional9530 incorrectly inhibited policy0.
The repaired admission requires the six parameter words and local952c only;
all three bit widths are also verified without any LUT/9530 writes.
All27 CMOS subtests pass again after this qualification repair; the earlier
32 Memory and33 Sensor batch keeps its unchanged-device scope.
