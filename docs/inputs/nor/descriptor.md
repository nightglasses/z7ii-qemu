# Descriptor17 and descriptor18 external inputs

The Z7II C1.70 resource registry defines descriptor ID17 as the finite external
NOR span `[0x132c0000,0x132e0000)`.  Its original loader checks the final four
bytes against the firmware literal `ENDM`, then copies selected records and
publishes their ordinary resource callbacks.  An erased span therefore follows
the guest's failure path; an NOR protocol implementation cannot reconstruct the
missing contents.

`-machine z7ii-mas-diagnostic,descriptor17-input=FILE` installs one explicit,
exactly 128 KiB file into this span.  Omission keeps the existing erased-FF
cells.  A wrong file size or an unreadable file fails machine creation.  The
input is copied into the same external-NOR backing used by the command model,
so guest programming, erasure and reset persistence retain their existing
semantics.  No adjacent bytes, guest RAM, registry pointer, readiness mask,
callback, event or IRQ is populated by this option.  The original firmware
remains responsible for `ENDM`, record selection, copying and notification.

The property deliberately accepts an opaque board input rather than declaring
its contents to be factory data.  In particular, repository fixtures contain
only synthetic bytes.  Original Nikon records, extracted firmware bytes and
generated images remain outside Git under `work/`.

Descriptor18 is the separate finite NOR span `[0x132e0000,0x13380000)`.
`descriptor18-input=FILE` accepts exactly640 KiB; its original footer is at
`0x1337fffc`. It uses the same backing and lifecycle as descriptor17, and the
two properties can be supplied together. Neither property replays its input
on reset. Missing, short and oversized files are rejected; omitted spans remain
erased. The guest still validates the footer and publishes records itself.

## Acceleration configuration and calibration

The original manifest copies resource21 from descriptor17 offset`a400`,256 bytes,
and resource57 from descriptor18 offset`100`,192 bytes. Directory entries have
28-byte strides at MAS VA`b185eb70`; their+4 field is an offset, not a length.
Their directory spans512/256 bytes must not replace the manifest copy lengths.
The resource callback waits for both completion bits before installing either
pointer; supplying descriptor17 alone therefore does not initialize this device.

Z7 C3.80 has same-schema ROM defaults at payload offsets`1501304/1501404`.
Its installer`10232616` copies256/192 bytes and calls the two acceleration
setters. The second resource ID there is44; Z7II uses57. Compatibility is based
on the padded`ACCELE` marker, type words1/2, copy lengths and consumer field
offsets, rather than matching numeric IDs. Unit gains at+56/+60/+64 are65536;
offsets and the inspected correction terms are zero. These remain cross-model
functional candidates, not authentic Z7II factory calibration.

The explicit experiment under`work/runs/guinormal-20261022/` adds only the256-byte
configuration to the existing functional descriptor17 and the192-byte calibration
plus`ENDM` to an otherwise erased descriptor18. Unchanged execution observes both
`04400043` callbacks, successful mask3 wait, and pointer installation at
owner+424/+428. The subsequent device and ordinary-request continuation is recorded
in [acceleration](../../devices/acceleration/protocol.md) and
[guiupdate](../../runtime/guiupdate.md). No original record bytes are embedded
in the emulator or committed in fixtures.

## TEMP admission record

Resource19 is a96-byte`TEMP` record at descriptor17 offset`a100`.
The original threshold comparator reads its+44 field as signed16. ErasedFF
there becomes -1, causing measured0 to reject the normal display start with
return4. The record pointer is MAS VA`b1807f68`; `b1807f70` is a gate word,
not a second pointer. This rejection is invalid persistent input, not evidence
of a physically unhealthy measurement.

Z7 ROM defaults at payload offsets`15014c4/1501524` are complete96-byte type4/
type3 records. Its original resource19 installer selects between them using
board item338. Z7II's setter/marker check and inspected comparison consumers
match the96-byte layout; no physical type or unit equivalence is claimed.
Explicit unchanged-record experiments with each candidate independently make
the normal display request return0, then stop at the same MXIC configuration
boundary. Neither candidate is silently selected or embedded in QEMU. See
[guiupdate](../../runtime/guiupdate.md) for accepted-request observations and
the remaining board-selection/factory-calibration limits.

## Cross-model functional display seed

Static comparison with an operator-supplied Z7 C3.80 MAS payload identifies
three original Z7 ROM defaults whose resource IDs, copy lengths, padded markers
and downstream consumer offsets match Z7II C1.70:

| ID | Marker | Record offset in descriptor17 | Copy length |
|---:|---|---:|---:|
| 24 | `HAIMENMONI` | `0xaa00` | `0x400` |
| 25 | `HDMI` | `0xaf00` | `0x100` |
| 27 | `EVF` | `0xb200` | `0x100` |

Packing those exact objects at the listed offsets and placing `ENDM` at
`0x1fffc` is a source-backed **cross-model functional experiment**.  It is not
a byte-accurate Z7II factory image or physical-camera evidence.  The emulator
does not embed, synthesize or silently select these records.  They must be
provided through the explicit file property, after which unchanged Z7II code
performs its normal validation, fixed copy and resource callbacks for those
records. The current multi-thread [publication probe](../../runtime/resource.md)
observes ID24/25/27 callbacks, the display resource-wait result changing from
0 to 1, and rear-panel table installation at `b1808508=b7f54a00`. The same
partial seed then enters the erased ID34 translator before worker startup.
This is resource-publication evidence, not a complete display configuration
or first frame. Other erased records remain explicit unknown inputs.

## Phase-3 ID34 boundary

Passing the outer `ENDM` check makes every record selected for a loader phase
ready; it does not validate each record body.  Phase 3 therefore publishes
ID34 from descriptor offset `0xd700` with copy length `0xc00`, even when that
body is erased.  Its original callback and translator are:

```text
0x1020fa27 -> 0x10217942 -> 0x1021796a
```

The translator treats ID34 words `+0x1c`, `+0x20`, `+0x24` and `+0x28` as
unsigned table dimensions.  Fixed source and destination strides constrain
them respectively to at most 30, 20, 8 and 8.  An erased record supplies
`0xffffffff`; the nested `+0x24/+0x28` loop then overruns its fixed derived
workspace.  This is invalid persistent input, not a missing device-ready bit
or IRQ.  The bounds alone do not define the remaining numeric tables and are
insufficient to certify a complete record. A constructed functional input can
select values from explicitly stated hypotheses and validate them through the
original consumers; see [functional.md](functional.md).

The fresh official Z6II C1.70 comparison confirms the schema: its descriptor17
geometry, ID17--41 registry and 25-row manifest match, and native translator
instructions use the same field offsets and loop shapes. Its identified path
still obtains ID34 externally. Fresh Z7 C3.80 bytes confirm the three display
defaults above, but its ID34 registry span is `0x1900`, unlike Z7II's `0xd00`;
the resource number alone is not schema compatibility. No usable whole ID34
record was recovered from these paths or the inspected public Z7/Z9 inputs.
This is not an exhaustive proof of ROM-default absence; see the qualified
[inventory](id34.md). Reproducing factory calibration requires an authentic
descriptor17 input containing
`[0x132cd700,0x132ce300)`, or a separately identified same-schema capture that
retains an explicit cross-model qualification.

That factory-data requirement is distinct from functional emulation. The first
explicit [constructed profile](functional.md) now makes the original ID34
mapper return with a nonempty 30-by-20 to 6-by-5 grid, and advances the manager
to current worker 4/state 2. A finite graphics-model correction subsequently
allows the1024x688 and720x480 clears, a gray panel and131 original glyph fills
through IRQ129. The glyphs render the battery communication warning;
five subsequent gray rectangles and an inline A8 warning icon also complete.
The224/1736-byte RGBA-input/YUV105-output pair also completes, followed by the
same-length720x480 scaling to YUVA107 and the176/1552-byte warning-source
SRC_OVER continuation using retained107 state. The following run returns
to worker0 with GUI readiness still unarmed. The selected
numeric values remain hypotheses;
neither original factory fidelity nor a GUI first frame is claimed.

The [field analysis](../../runtime/id34.md) establishes four rectangle-coordinate
tables, two coefficient tables, 83 mapper scalar assignments and the separate
`+0x774` parameter value. It separates the immediate dimension overrun from later
grid-processing requirements; dimension bounds do not define a bootable record.

The current multi-thread reproduction and the separate partial-seed experiment
are recorded in [resource.md](../../runtime/resource.md). In the erased-input
baseline, the actual `ENDM` comparison returns -1 and validation returns 0
before record copying. With the partial seed, the ID34 inner loop executes
well beyond its fixed eight-word row and is followed by an exception-context
save fault. The exact corruption chain to that later fault remains untraced;
the invalid record dimensions themselves are directly established.
