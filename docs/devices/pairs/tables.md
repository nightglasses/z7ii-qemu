# Paired-bank table publication

Original C1.70 MAS publishes36 configuration tables through CPU stores into
each selected paired bank. They occupy the exact continuous bank-relative
range `[1400,1d38)`:22 tables of64 bytes followed by14 of68 bytes. The finite
model retains all590 words in either bank without starting processing,
producing results or publishing events. This covers the whole source-selected
family rather than the first observed value at PA `20381400`.

The [receipt](/evidence/imagerequest/tables.json) separates source, native checks
and original continuation. Source code/ROM addresses are C1.70 MAS virtual
addresses (VA); device addresses are board physical addresses (PA). Addresses,
register offsets and masks are hexadecimal; counts, lengths and software
structure offsets are decimal. Ranges exclude their upper endpoint. Device
accesses are aligned little-endian32-bit.

## Producers and complete extents

Setter `1036bf88` copies22 pointers from ROM VA `1078a688` into its local
array, selects the caller's index and passes64 bytes to common helper
`1036ca74`. Setter `1036bfae` copies14 pointers from VA `1078a6e0` and passes
68 bytes. The helper loads each source word and stores it to the selected
destination plus `object.byte4 <<18`. The board has bank0/1 at PA
`20380000/203c0000`. These are CPU writes, not a device-fetched descriptor or
DMA transaction.

| Setter | ROM entries | Destination relative to selected bank | Length per table | Complete bank-relative range |
| --- | --- | --- | --- | --- |
| `1036bf88` | 22 | `1400 + i*40`, i=0..21 | 64 bytes | `[1400,1980)` |
| `1036bfae` | 14 | `1980 + i*44`, i=0..13 | 68 bytes | `[1980,1d38)` |

The groups meet exactly at `1980`, with no internal holes. The second group's
last table begins at `1cf4`, and its last word is `1d34`. The existing
configuration group beginning at `1d38` follows immediately. Earlier offsets
`139c..13fc` and the later `1e38` remain holes; the new family does not create
a general register aperture between unrelated groups.

Upper configurator `102e49fe` reads the job's pointer-array field at decimal
offset104, skips NULL entries and calls `1036bf88` for each present row.
It then does the same for field108 and `1036bfae`, before continuing the
already-supported later configuration groups. The bytes are supplied by
original software or its caller, not synthesized by the device. The observed
`00f600fa` is a parameter word; this copy path alone does not establish a
width/height interpretation or a physical processing format.

## Configuration lifetime and unsupported processing

`hw/nikon/devices/pairs.c` retains aligned full32-bit words independently in
each bank. Source `1036ca74` copies words without masking them, so no legal-bit
mask is inferred from one observed value. Writes have no host-side transaction
rollback: a caller publishing only some rows has updated only those rows.
Cold zero, retained readback and whole-device reset clearing are the existing
explicit functional conventions, not measured physical power-on behavior.

Updating tables preserves other fields, the other bank, inhibition masks,
pending bits and IRQ levels. Time does not make configuration complete an
image job. Invalid widths, unaligned accesses and adjacent holes remain errors.
The model adds no RAM, DMA source, guest callback, pending cause or slot release.

Actual start is separate: the [parameter/start contract](parameters.md#configuration-and-later-consumption-are-separate)
locates `10369faa`, which writes selected-bank `10=1` and `08=1` after
configuration. The later [pixel contract](pixels.md) defines the finite data
operation, input/output association, result and cancellation conditions. The
[request contract](requests.md) admits pending starts and finish requests;
tables do not supply
those results merely by being present. Active-work cancellation and publication
coverage are also governed by that pixel contract; a new coverage bit matters
even when the stored parameter value remains zero.

## Native validation

The old model fails the focused HMP read at bank0+`1400`. The accepted native
batch passes all23 SLA subtests. Its paired-bank fixture writes and compares
every one of the590 words in both banks using distinct full32-bit patterns,
checks bank isolation, traverses the unequal-stride group seams and verifies
reset clears the full added extent. Wrong widths/alignment and surrounding
holes still reject without changing retained data.

The fixture also rewrites both complete table groups with already-latched
events, then advances time. Pending values, inhibition masks and IRQ levels
stay unchanged until the existing explicit W1C path acknowledges them. These
are synthetic device-contract checks, not original image processing or
physical table-function verification.

## Unchanged-firmware continuation

The same bounded NOR resource35/cold-neutral resource80, quadrant scene,
healthy paced UART/RNG0/held F4/five-CPU MTTCG profile runs for60 host seconds
without an in-window GDB pause. Bank0 retains499 nonzero words across the full
590-word extent; its first word is `00f600fa` and last word is `21f`. Bank1's
same extent remains zero. Zero entries do not establish absent source rows or
an identity transform. Subsequent QMP/read-only GDB observations are sequential,
not atomic and not physical processing rates.

The first uncovered transaction becomes CPU1 PC `10369fb4`, WRITE32 PA
`20380010=1`, in the independently located start helper `10369faa`. The graph
still retains18 packets/649 words and busy slots1/2/3/4/5/8. This crosses the
table publication family and reaches actual paired-processing start; neither
that operation nor the graph's later reverse-start is completed by this model.

The run completes128 main acquisitions and463 statistics jobs, with zero
rejections/statistics cancellations and one main cancellation. Error6433,
GUI readiness and information records remain zero; neither display head has
a console. The next required capability is paired input/operation/output and
its real completion/cancellation lifecycle. No menu, normal GUI publication or
reusable processing-slot cycle is claimed.
