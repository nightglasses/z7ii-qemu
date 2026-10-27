# Temporary paint modes in image conversions

The normal GUI now crosses the224/1736-byte YUVA107 packet that repeats
01841004=2 before its final mode2. The finite conversion family accepts all
four source-backed inherited modes0/2/4/6 at that temporary position, while
requiring the final low-byte state and every intervening command unchanged.
This applies to initialized YUV105, initialized YUVA107 and retained YUVA107
SRC_OVER. All20 native transfer subtests pass. Unchanged original execution
completes nine such mode2 conversions and later graphics work, then reaches
a216/344-byte A8 image prefix boundary, subsequently handled by the
[prefix extension](prefix.md). GUI first frame remains unverified.

## Observed original sequence

Code addresses below are unchanged C1.70 MAS VAs; command buffers are board PAs.
The baseline repeats the preceding224/1736 rejection at bd0dff90/bd15ff90:
CPU0 PC106ee3c6, WRITE32 PA201810e0=1,
`transfer-convert-program-not-supported`. After excluding validated operands,
only draw word74 differs from the scaled107 table:2 instead of0. Geometry,
matrix, f24 sampler, fixed LUT fields and final mode all match. Source is
bf309600, output planes cb82b000/cb980000 in this baseline.

| Draw word | Command | Payload | Producer observed in trace |
|---|---|---|---|
|73/74|01841004|2|10646062, returning to1068b92e (LR1068b92f)|
|91/92|01841004|2|10645c86, returning to106b4c1e (LR106b4c1f)|
|429/430|01a61004|1|10645c3c from106b40d2 (LR106b40d7)|

Both mode writes use bank0 and context b1800c1c. The complete draw stream has
only one1a6 trigger, at429. The earlier mode therefore cannot be consumed by
an intermediate draw in this grammar. The shared append helper106453cc stores
the header and scalar in the selected command buffer; it does not perform the
draw. The [fixed Z7 decoder](https://github.com/WonderLantern/z7-qemu/blob/97abf0b81cd3211f8374a38987a0beaf4549b90f/qemu/nikon-z7-gpu.h)
corroborates byte masks and drawing at1a6; it is related software, not a silicon
measurement.01841004 selects the complete low byte of184, so the second write
replaces the first low-byte mode before drawing.

## Source-backed upstream explanation

106b4b28 selects image mode v19=1 for its normal-image branch, calls
1068b8ae(context,2,matrix,v19) at106b4c08, then10645c86(context,v19) at106b4c1a.
1068b8ae's fourth argument1 forces its solid-paint selector to0, and at1068b92a
it calls10646062(context,0). That helper clears private+191 and writes:

```
(solid ? 8 : 0) | (2 * private192) | (private188 != 0)
```

Thus the temporary mode still contains the previous cached image-mode field.
In this baseline that field is1, giving2 with the two Boolean fields clear.
The final snapshot has private bbfebad0, private+191=0, private+192=1 and
private+188=0. The first-write payload and source formula establish the cached
value before overwrite separately from that final snapshot. Earlier successful
conversions use cached mode0 at the same position. A changed private render
state is not evidence of a different conversion operation.

10645c86 then stores v19 in private+192 and emits the same formula. For this
profile the final result is2. Its callers emit mode0 or1 from Boolean branches,
or1/2/3 from image-mode selection; the inherited mode field is consequently
bounded0..3 in the recovered producer family. With the profile's bit0 and
solid bit3 clear, this gives exactly0/2/4/6 for the earlier write. The model
keeps those flag restrictions explicit; it does not accept arbitrary low-byte
states merely because another write follows. The earliest allocation/restoration
of each private cache is not needed to consume this completely replaced field
and is not claimed to have been traced here.

## Finite contract and limits

The shared converter field validator permits only bits1..2 of logical draw
word74. Full initialization uses physical word74; the retained176/1552 form,
which omits45 prefix words, uses physical word29. The header at73 and final
01841004=2 at91/92 remain exact, as do all commands before the sole draw
trigger. An odd value, solid/high bits, changed mask/register, early trigger,
changed final mode or malformed ending still rejects. This is a finite producer
grammar, not a generic register interpreter or broad last-write exception.

Sampling, matrix/LUT interpretation, plane layout, validated source/targets,
alias/range checks, immutable captured commands, deferred mapping checks,
YUVA107 context publication and IRQ/reset/cancellation are unchanged. In
particular this correction does not introduce color-transform arithmetic or
new geometry/filter combinations. The functional assumption is that an unused
mode write has no eager side effect beyond storing that byte. This is supported
by the producer/consumer sequence and tested output equivalence, but physical
hardware timing and numerical precision remain unmeasured.

## Validation and next original boundary

The new valid scaled fixture sets temporary mode2 and fails against the old
model with MEMTX_ERROR. The final applicable `nikon_transfer_unit` batch passes
all20 subtests in1.38s. The three conversion cases now contain29 YUV105,
30 scaled107 and32 retained SRC_OVER variants: all four temporary modes with
existing independent color/sampler and full-plane/guard checks; changed headers,
premature draw and active mode rejection; immutable capture, late input changes,
mapping loss and reset/unrealize. There are23 new variants across these cases.
These native results are synthetic evidence, separate from original execution.

The final unchanged original run keeps the prior declared descriptor17 TEMP,
descriptor18/resource12, acceleration(0,0,1024), RNG0, cold F4, five-CPU MTTCG
and normally paced9600 healthy UART inputs. Trace joins three earlier0→2
conversions, followed by nine2→2 conversions, each followed by original IRQ129
before the next START. It records84 handler entries from the first newly
accepted2→2 conversion through later paths/images/conversions/composites to
the next error. No firmware byte, PC, software event, readiness or return value
is injected. Batch boundaries and operands remain actual-run observations.

That run's next first rejection is CPU1 PC106ee3c6, WRITE32 PA201810e0=1,
`transfer-stream-truncated`, on216/344-byte streams at bd0e0490/bd1605b0.
The setup has an extra complete cached41..49 matrix write before the existing
44-word inline image body; draw has an extra01848004=0 before the84-word
image body. Active matrix diagonal(1,-1,1), translation(616,44), inline
coordinates(0,0,38,36,-38), A8 source b97ac500 with extent40x48, target
bec5f200 and paintff000000 suggest the existing image operation with additional
dirty state. Temporary/final modes are8/0e, key10007. This prefix/overwrite
interpretation is joined by the later[prefix analysis](prefix.md); equal body
shape alone is not acceptance evidence. The truncation message comes from fallback
length validation, not proof of missing guest bytes.

Final GUI gate/pending3/display-ready remain0, BatCk00010001, application03/02
and primary request11. QEMU and peer remain alive until controlled cleanup;
the peer reports no protocol rejection. Receipts in
`run convertpaint/` include native red/focused/green logs,
original red/green argv/traces/read-only snapshots, source projections,
raw streams, producer joins, packet/mode/IRQ progress and UART/QMP receipts.


## Sampling domain and output layout

A later unchanged C1.70 MAS menu run submits another224/1736-byte packet and
rejects at MAS VA`106ee3c6`, WRITE32 board PA`201810e0=1`, with
`transfer-convert-program-not-supported`. Its normal-image grammar is already
supported: temporary184=0, final184=2, no185 paint write, and the complete
existing color-disable, color matrix and both LUT tables. No additional paint
or prefix is missing.

The new combination is the existing720 × 480 scaled input domain with output
format105. Both output-format commands carry105. The active path matrix,
f24 sample coefficients, source active/allocation extent`02d001e0` and paired
filter controls7 exactly match the established scaled107 profile. The source
is board PA`bf309600`; output planes at`cc18b000` and`cc235800` differ by
`aa800`, exactly1024 × 682 bytes. The previous parser incorrectly equated
scaled sampling with format107 and would also have assigned an oversized
first plane. The model now validates source-domain state separately from the
complete output-format words and their plane sizes.

The finite combinations remain explicit:

| Operation | Source domain | Output | Plane bytes | Final184 |
|---|---|---|---|---|
| Initialized normal SRC |1024 × 688 allocation,1024 × 682 active|105|698368 +698368|2|
| Initialized normal SRC |720 × 480, existing scaled transform|105|698368 +698368|2|
| Initialized normal SRC |720 × 480, existing scaled transform|107|1396736 +698368|2|
| Retained SRC_OVER |720 × 480, existing scaled transform|107|1396736 +698368|2|
| Initialized clear |Existing clear path, no sampled image|105 or107|As above|8|

Normal conversions retain temporary0/2/4/6 and the exact final2; clear retains
its separately validated temporary0/8 and final8. The short RGBA/A8 image
sampler's explicit paint/modulation mode0e is a separate[grammar](image.md).
It is not added to the converter, whose color matrix, LUT values, mask bits,
source format, blend state and command ordering remain exact. Unobserved
107/non-scaled geometry and format106 remain unsupported.

The executor reuses the existing scaled coordinates, path coverage, SRC
weights and limited-range BT.601 functions. Format105 stores one Y byte per
pixel followed by interleaved U/V bytes for each horizontal pair; it has no
stored alpha. The same SRC equation therefore treats its previous destination
alpha as255 and writes no alpha plane. Source alpha and path coverage still
participate in the existing weights: a fully covered sample with zero alpha
converts black, while nonzero source alpha retains its straight RGB color.
Fractional boundary coverage includes the old opaque destination using the
same equation as107. These alpha/precision choices are functional assumptions
consistent with the existing converter and fixed Z7 model, not recovered
physical-GPU arithmetic. Nominal105's existing alpha limit remains unchanged.

The native`/nikon/transfer/scaled-yuv105` fixture covers the complete two-plane
extent and unchanged guards/tail, all four temporary states, alpha0/64/128/255,
opaque and transparent boundary cases, immutable commands and late source
changes. Its reference shares the separately tested path mask, computes sample
coordinates with doubles and independently computes opaque-destination SRC,
BT.601 and105 packing. Literal blue anchors are Y41/U240/V110. Format mismatch,
unknown format, altered color matrix/LUT/filter/geometry, incorrect plane delta,
range/alias failure, map loss, reset and unrealize remain negative cases.
The107 fixture retains its previous pixel and alpha checks; native results
and original menu completion are separate validation claims.
