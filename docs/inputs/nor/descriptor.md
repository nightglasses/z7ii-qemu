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
performs its normal validation, copy, callback and display-configuration
publication path.  Other records left erased remain explicit unknown inputs
and may expose later firmware error paths.
