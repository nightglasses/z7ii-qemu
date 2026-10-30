# Synthetic EX factory configuration

The C1.70 update contains the complete 512 KiB EX component, but its factory
configuration window at `0x08004000..0x08020000` is erased in that payload and
in the assembled container (direct observation: the whole window is zero in
`ex.img`, and IDA renders the window as a single bogus
function `sub_800407C` over instructions decoded from zero bytes).  The EX
firmware nevertheless reads this window during cold start, so the complete
bundle composition installs a **class-factory** structure: model data the
firmware's own validators accept, not a recovered Nikon factory dump.  It never
changes the original EX component.

## What the firmware reads

Three adjacent blocks with a four-byte ASCII magic, a total size at `+4`, and
that size echoed four bytes before the block end:

```text
block   VMA range              magic   size     records
A       0x08004000..0x08004e2c  "MAKE"  0x0e2c  record 1 @+0x10, 0x0e18
B       0x08008000..0x080091bc  "MODE"  0x11bc  records 3..6 @+0x10/0x100/0x300/0x340
                                                sizes 0x0f0/0x200/0x040/0xe78
C       0x0800c000..0x0800c018  "INDI"  0x0018  record 8 @+0x10, 0x0004
```

Each block satisfies `size = 16 + sum(record sizes) + 4`.  The magics are the
four-byte prefixes of the string `"MAKEMODEINDI"` at `0x0804e208`; the record
boundaries are not stored in flash.  They come from the firmware's own
nine-entry, twenty-byte record table at `0x2000e79c`, which cold boot produces
by decompressing a ROM image (`0x08003838` LZ decoder); a runtime probe reads
back `{id, flash pointer, SRAM staging pointer, length, flags}` for ids 0..8
with exactly the lengths above.

Block format evidence: `sub_80294b0` compares the primary block's first word
against `"MAKE"` and the firmware's block check `sub_802ed04` requires both the
magic and the echo of the `+4` size field, for which it knows the echo offsets
`+0xe28`, `+0x11b8` and `+0x14` (VA `0x0802ed04`).  `sub_802ede0` walks the
record table to compute each record pointer as `block + 16 + sum(previous
lengths)`, and `sub_802f84c` copies the three blocks into an SRAM staging area
at `0x20038000` using the block sizes.

Consumed record-1 bytes (offset relative to `0x08004010`):

```text
+276       channel 0     -> 0x2001f644, limit 1        (sub_8029418)
+278/+279  mode word      -> 0x2001f646, per-byte limit 1; +279 == 5 arms the
                             0x2000ed90 setup gate     (sub_8029418)
+541       power gate     -> 0x200138d6; arms the rail monitor that logs
                             20001 and resets with status 14 on a fault
                                                       (sub_8032c3c, sub_80426b8)
```

The qemu model writes exactly these four bytes and leaves every other record
byte erased, so unknown factory fields stay zero instead of becoming counts,
pointers or DMA lengths.  Note that `0x08008100..0x08008300`, previously modeled
as a separate "auxiliary view", is byte-for-byte record 4 of block B.

## Model behavior

`nikon_ex_config_compose()` (`hw/nikon/components/exconfig.c`, declared in
`include/hw/nikon/exconfig.h`) erases each of the three blocks, then writes its
magic, its size field, the trailing size echo, and the record-1 fields.  Bytes
outside the three blocks are untouched, so erased payload regions stay erased.
A flash image too small for the complete span is rejected without any write; the
machine reports that case instead of patching a short image.

## Record-4 consumers (battery gauge profile) and why they stay erased

`sub_80294f4` copies record 4 into a 71-byte shadow block at `0x2001fa08..` and
clamps it on the way in.  The shadow block is then read field by field:

```text
rec4 +64   -> 0x2001fa08  (no direct reader; only the block's base is loaded)
rec4 +136  -> 0x2001fa24  sub_8028408, sub_802a888
rec4 +148  -> 0x2001fa28  sub_80286bc, sub_802a57c
rec4 +168  -> 0x2001fa2c  sub_8026a70   (sample -> 15-step conversion; guard
                                       0x2001fa2c > sample returns 9000)
rec4 +272  -> 0x2001fa38  sub_8029c80   (timeout multiplier 7200 * value)
rec4 +288  -> 0x2001fa3c  sub_80276c8, sub_8027760, sub_802a230
rec4 +292  -> 0x2001fa3e  sub_802a230
rec4 +300  -> 0x2001fa42  sub_802a230
rec4 +304  -> 0x2001fa44  sub_802a230
rec4 +308..324 -> 0x2001fa45..fa49  sub_8028bec, 8c58, 8d10, 8e28
rec4 +332  -> 0x2001fa4b  sub_8028bec, 8c58, 8e28
rec4 +348  -> 0x2001fa4e  gauge command byte 4 (also sub_8028cf0)
rec4 +352/+356 -> 0x2001fa4c/+4d  sub_8028d10, sub_8028bec, 8c58, 8e28
```

This is a battery/rail gauge profile: `sub_802a230` compares the record-4
thresholds against the runtime measurement `0x2001f5ca` and drives the
`0x2000ed90` gate through `sub_802b40c`; `sub_80276c8` checks that gate first and
logs 19001/19003/19005 around the same comparison; `sub_8028d10`/`sub_8028e28`
build the gauge command from `+308..+356`.

**These paths never execute on the observed cold start.** The state machine is
reached only from host message `id 118` (`case 0x76` of the dispatcher at
`0x08027e58`), and probes on `0x0802a230`, `0x08028e28`, `0x08028d10` and
`0x080276c8` record **zero hits** in a 150 s window; the host lane was previously
observed carrying ids 101/102/103/131/1114/1115, not 118.  Erased record 4 is
therefore inert today, and it is not what keeps the power-state publisher from
starting.

One latent model difference is recorded rather than fixed: with
`rec4 +272 == 0` the timeout test `++0x2001f608 >= 7200 * 0` succeeds
immediately, so if the gauge ever reaches `0x2001f5da == 3` the firmware logs
19003 and enters error state 6.  Revisit only if `id 118` starts arriving.

## Verification boundary

`tests/unit/nikon/exconfig.c` asserts the firmware's own acceptance criterion
(magic, size field, echo, `size = 16 + records + 4`), the record-1 field
placement, erased tails, untouched spans outside the blocks, determinism and the
short-image rejection.  The block check `sub_802ed04` itself is **not** reached
inside the observed cold-start window: probes at `0x0802ed04`, `0x08024c70`
(record consumer) and `0x0802f84c` (block copy) show the copy and the consumer
running while the validator stays unexecuted, and the region hash driver
`sub_80429dc`/`sub_8042a50` finds its engine state area `0x2000f6a0..` zero so it
returns without hashing.  Acceptance is therefore verified against the
validator's documented criterion and the data the consumers read at runtime, not
by an observed pass of `sub_802ed04`.

Still unknown, and left erased on purpose: record 1 `+528`/`+529`, record 4
`+64..+356` values (consumers listed above, none reached), records 3/5/6
contents, record 8's class byte (zero maps to the `0x0804e444` default class
`0x88`), the block header bytes `+8..+15`, and whether `sub_802ed04` runs on any
later host command.

The same complete-bundle composition carries one deterministic board-input
record: ADC1 channel `0` uses the full-scale raw count `4095` because the
configured power path accepts channel 0 only above the firmware threshold
`3819`; channels `1..3` use raw count `3500`. Expander pads `0`, `1`, `3` and
`10` are held low; omitted pads remain electrically disconnected. GPIOD.13 is
high, and the declared power-good wires are held high. The previous low drive
was corrected in `6b0b4653`: the firmware treats all seven low as a rail fault
and requests a reset with status 14. The `0x0d`
status endpoint defaults to class 1; its `class` property is a functional
board input and reset preserves it. These values are model data for the
bundle board record, not recovered factory calibration or a physical voltage
claim.

No external config file, byte patch, machine profile selector, guest event or
report is supplied at runtime; the finite board values above are owned by the
built-in bundle composition.  The guest still performs all validation, GPIO
polling, ADC conversion, debounce, event generation and report construction.
The raw MAS input path has no private EX component and therefore does not
create this configuration view.
