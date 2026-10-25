# Temporary solid state in YUV clears

The normal GUI's 224/1648-byte YUVA107 clear now completes through original
IRQ129. Both initialized clear formats105/107 accept temporary low-byte mode0
or8, followed by the exact final8 write. The applicable transfer batch passes
22 subtests in1.67s. Unchanged original execution records147 IRQ129 entries
from the new clear through later graphics; the next first uncovered access
is timing channel4's extra-feature enable, not another graphics command.
GUI readiness and first-frame status remain unchanged.

## Observed producer and replacement

Code addresses are unchanged C1.70 MAS VAs; buffers and MMIO are board PAs.
The preceding[target checkpoint](target.md) captures complete224/1648 streams
at `bd0dff90/bd15ff90`, format107, allocation1024x688, inline rectangle
`(0,6,1024,682,-1024)`, planes `cb4a7000/cb5fc000` and key10001. Only fixed
draw word66 differs from the older clear table:0 instead of8. That model
rejects CPU0 PC `106ee3c6`, WRITE32 PA `201810e0=1`, with
`transfer-stream-program-not-supported`.

Clear producer `1068bd90` calls `10645c86(context,0)` at `1068bdee`.
That helper stores0 at private+192 and emits bank0/header `01841004` using:

```
(private191 != 0 ? 8 : 0) | (2 * argument) | (private188 != 0)
```

The temporary write therefore preserves only the previous solid flag in bit3
plus the separate bit0. Later `1068bd90` calls `10646062(context,1)` at
`1068be22`. It stores1 at private+191 and emits the same header using:

```
8 | (2 * private192) | (private188 != 0)
```

No intervening producer changes private192 from0. The existing finite clear
requires final mode8 and hence private188 clear; the earlier write can be
exactly0 or8. Unlike the[image conversion case](mode.md), this producer does
not preserve an arbitrary cached image-mode field. Modes2/4/6/10/12/14 are
not part of this clear family.

The baseline trace observes temporary0 from LR `1068bdf3` and final8 from
LR `1068be27`, with the same context `b1800c1c` and bank0. An earlier successful
clear uses8 at both positions. The paired commands put these low-byte writes
at draw65/66 and388/389; the sole `1a6=1` trigger is407/408. Both headers use
mask1, so final8 replaces the same byte before any draw consumes it. The
source helpers append commands; they do not execute the intermediate mode.

## Finite model and limits

The shared105/107 clear validator permits exactly0/8 at word66. It still
requires header65=`01841004`, final388/389=`01841004/8`, all intervening state,
LUT upload locations/bounds, output formats, geometry, keys and ending.
Changed byte enables, an earlier trigger, odd/image/high bits and a changed
final mode reject. There is no generic last-write rule. Output arithmetic,
plane layout, preflight/mapping checks, captured immutable commands and
reset/unrealize cancellation are unchanged.

The functional assumption is that a low-byte mode fully replaced before
drawing has no eager side effect. The producer sequence supports that bounded
assumption; native equivalence is not physical GPU timing or precision
evidence. Existing limited-range BT.601 black and transparent107 alpha remain
functional conventions. Firmware bytes and guest events, PC, readiness and
returns are untouched.

## Validation and next boundary

The new temporary0 native case is red against the old model. The clear fixture
now has40 variants,12 new, covering both0/8 profiles, complete planes and guards,
odd/image/high bits, wrong temporary/final masks, early draw, final-mode rejection,
immutable capture and existing geometry/LUT/range/alias boundaries. Reset,
map loss, backing alias and unrealize now also exercise temporary0. The final
applicable `make check MTESTARGS=nikon_transfer_unit` passes22 subtests in1.67s.

The separate normal original run uses the preceding declared TEMP, descriptor18,
resource12, acceleration `(0,0,1024)`, RNG0, cold F4, five-CPU MTTCG and normally
paced9600-baud healthy UART inputs. The0→8 clear enters original IRQ129 before
the next START. Later paths/images/conversions/composites also complete, with
147 handler entries from this clear through the final successful176/1552 job.
The first new diagnostic is CPU0 PC `1036e9a8`, WRITE32 PA `2c011538=1`,
`timing-access-or-feature-not-supported`: bankB channel4's +38 extra-feature
enable. Its enabled behavior requires a separate producer/configuration/result
contract; no completion/status is supplied here. The captured last graphics
pair is completed work, not a rejected transfer submission.

Final application bytes are01/03, primary request0, BatCk00010001 and GUI
gate/pending3/display-ready0. UART rejects no request; peer and QEMU remain
alive until controlled cleanup. This does not establish a GUI first frame.
Receipts: `run clearmode-20261023/` (source projections, baseline-mode
join, native red/green, final normal argv/trace/snapshot/streams, jobs and
progress). The previous actual baseline streams remain in `singletarget-20261023`.

The later integrated cached-matrix and panel-observer changes have23 transfer
and3 panel native subtests passing. Final original receipts in
`run graphicsstate-20261023/` repeat this0→8 job,147 IRQ129 entries,
the same timing diagnostic and the same readiness snapshot. The new matrix
combinations have synthetic coverage; no per-combination original execution
or GUI first frame is inferred from them.
