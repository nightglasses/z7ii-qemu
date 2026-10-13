# Descriptor17 external input

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
records.  This does not by itself reach display-configuration publication.
Other records left erased remain explicit unknown inputs and may expose later
firmware error paths.

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
not authority to synthesize them.

The Z6II C1.70 updater independently confirms the schema: its descriptor17
geometry, ID17--41 registry, 25-row manifest and ID34 callback are byte-
identical, and its translator uses the same field offsets and loop shapes.
It nevertheless obtains ID34 from the same external resource path and contains
no whole-record ROM default.  The public WonderLantern Z7 and Z9 sources also
contain no compatible object or factory-data image.  Consequently the current
source-backed boundary remains an authentic descriptor17 input containing
`[0x132cd700,0x132ce300)`, or a separately identified same-schema capture that
retains an explicit cross-model qualification.
