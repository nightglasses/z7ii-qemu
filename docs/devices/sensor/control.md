# Capture-control masks and enables

The current [run/stop profile](runstop.md) additionally models the complete
08/0c unit family and source-mapped quiescence notifications under explicit
functional hypotheses. All25 sensor qtests pass. The buffer-clear boundary
below is the earlier reset/enable-only checkpoint, not the current frontier.

The normal GUI-request continuation now passes the capture-control initializer.
The previous rejection of WRITE32 board PA `20068000=fffffd40` was a model
restriction: a complemented unit mask was checked as a plain12-bit integer.
The finite contract below replaces independent storage of the reset command
words with paired set/clear state. It supplies no capture completion or frame.

All code addresses are original C1.70 MAS VA. Register offsets below are
relative to capture-control board PA `20068000`. The source recovery, actual
arguments, native logs and unchanged-bundle runs remain under parent-owned
`run capturemask/`.

## Producer and complete control family

Original `10239740` receives flags `2eb`. Mapper `102396e8` converts those
software flags into unit mask `2bf`; it does not directly use flags as hardware
bit identities. At `10239756`, the caller passes `~2bf` through tail wrapper
`1020c042` to `1020a50c(b1806814,fffffd40,0)`, LR `1023975b`. The final argument
selects a write to offset0 rather than4. Thus upper ones are produced by the
original complement operation, not a separate request to enable unknown units.

Initializer `1020aacc` writes `fff` to0 then4. Per-unit initializers
`1020c1ee/37e/41a/542/5ae/61a/6fe/7d8` and `1020c050` similarly write a
selected mask to0 then4 before configuring their subordinate block. Their
enable routines call `1020a518(mask,1)`, which writes offset10; shutdown uses
the same helper with0, writing14. Independent helper `1020a524` reads18 and
sets or clears selected bits by RMW. These establish distinct paired control
commands, retained RMW control and interrupt-mask families.

| Offset | Source-backed interface / finite functional behavior |
| --- | --- |
| `0 / 4` | `1020a50c`: assert / release selected reset bits; retain one12-bit bitmap with OR / AND-not |
| `10 / 14` | `1020a518`: enable / disable selected units; retain an independent12-bit bitmap |
| `18` | `1020a524`: retained12-bit RMW control, independent of both bitmaps |
| `20 / 24 / 28` | `1020bfc2`: raw word parameters at20/24, separate boolean enable at28; current initializer supplies31/15/1 |
| `60` | `1020ab3c`: retained raw word; no inferred processing effect |
| `70` | `1020bfde`: boolean bits5/1 and selector bits4:3, mask`3a`; current initializer supplies18 hex |
| `200 + 20*i`, eight words | Existing independent IRQ masks; IRQ follows actual published pending AND these masks |

The paired commands accept a12-bit mask or its32-bit complemented form:
upper20 bits must be all zero or all one, and only the low12 select units.
Other upper-bit patterns still fail before mutation. This finite normalization
comes from the normal and complement producer shapes; it is not a general
discard-upper-bits aperture. Width remains aligned32-bit. RMW18 still rejects
out-of-field bits,28 rejects nonbooleans and70 rejects bits outside`3a`.
Unknown offsets, including1c/2c/64, remain errors. Other existing coefficient,
command, status and provider interfaces keep their prior contracts.

Reset/enable bitmap names and polarity describe the observed initialize/disable
sequence as functional semantics; physical unit names and silicon reset effects
remain unbound. Both aliases mirror their effective bitmap on functional
readback and through `nikon_capture_configuration`. This readback is a declared
model convention, not an observed original read of every alias. Functional
cold reset clears these states and restores known configuration to zero.

No unit-bit mapping to coefficient tables, pending words, physical clocks or
DMA is established here. Consequently these controls retain state without
inventing a table reset, clearing externally published pending status, issuing
a command pulse, raising IRQ or completing a frame. Exact effects on an actual
processing provider require a separate source-bound mapping. The existing
100/104 explicit command publication and eight IRQ masks remain independent.

## Earlier validation and next boundary

The unchanged-bundle `red.py` run fails its exact complemented-store assertion.
The new native `controls` case then fails on the old implementation, reading0
instead of`d40` (`native-red.log`). The final focused command
`make check "MTESTARGS=nikon_sensor --test-args='-p /arm/nikon/sensor/capture'"`
passes all four cases (`native-final.log`). Coverage includes the complement,
all12 individual reset/enable bits, preservation of other bits, RMW and adjacent
controls, invalid upper bits/width/fields, no implicit IRQ, preservation of
real published pending state, and whole-device reset. The existing cases cover
coefficient/command state, complete publication, IRQ masks/W1C and stale or
incomplete provider packets. This is a focused synthetic contract result.

The post-change original runs keep the unchanged bundle, paired TEMP type3 NOR
inputs, raw acceleration `(0,0,1024)`, fixed virtual RNG0, complete class1 UART
peer and held cold F4. GDB takes only a final read-only snapshot. Original
execution passes the complemented mask, adjacent controls, selected per-unit
reset/enable pairs and reaches `10239740`'s return sequence. Final capture
reset bitmap is`d40`, enabled bitmap`2bf`, RMW18=`3e`,20/24/28=`1f/f/1`,70=`18`;
all eight pending words remain0. No host supplies a completion or software event.

The next first uncovered write is CPU0 PC `10361044`, board PA `62059800=0`,
inside the normal clear helper. The targeted trace joins caller `10238314` at
`10238360` (LR `10238365`) to `10361034(VA36d59800,1c8000)`. This is a
1,867,776-byte clear. The source allocates kind40 through `1030d96e` when holder
`b26b48d8` is empty, dereferences its buffer record, and applies address
translator `103c5a7a` before the clear. That translator uses a12-byte range
record returned by `103c5ad8`, not a constant bit-clearing operation. Buffer
holder `b26b48d8` ends at object`b2c87608`, whose+4 link`b2c87528` contains
`62059800` in the final read-only snapshot. That is a sampled ownership link,
not proof of the selected translation record at the earlier call. Buffer
ownership, the selected translation record and guest VA→PA mapping must be
joined before adding RAM or an alias. The new PA is not admitted by proximity.

Application bytes02/01 and primary request state1 remain. The peer reports no
rejected request. The later snapshot has power0 and the word containing BatCk
`00010101`; it is not relabeled as the earlier BatCk1/power1 checkpoint.
GUI gate, pending3 and display-ready remain0. No GUI first frame, scanout,
complete boot or physical-camera validation is established.
