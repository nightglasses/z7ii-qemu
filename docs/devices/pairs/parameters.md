# Paired-bank parameter packets and consumption

The packet-stage observation below is superseded by the later
[type5/type6 stream import](../gate/fanout.md), which crosses its historical
downstream reset stop. The packet retention and start/wait boundary remain
unchanged; the [frontier](../../validation/frontier.md) owns current startup.
The separate [table-publication contract](tables.md) covers both complete
source-selected table families before the same independent processing start.

C1.70 MAS writes PA `203800a8` as one field of an eight-field parameter
packet. The same-architecture SLA setter writes the identical group. The
shared paired-bank model now retains the complete group in both banks; its
other seven fields were already supported. Original cold-neutral execution
crosses the former `a8` stop and submits another processing slot before stopping
at a different command-buffer reset. Parameter retention does not implement
image processing or generate completion. See the
[receipt](/evidence/imagerequest/parameters.json) and
[startup frontier](../../validation/frontier.md).

Source addresses below are virtual addresses (VA) in original C1.70 MAS, or
explicitly named SLA. Device addresses are board physical addresses (PA).
Addresses and register offsets are hexadecimal; structure offsets, lengths,
counts and type/resource numbers are decimal. Accesses are aligned
little-endian 32-bit words.

## Packet producer and field mapping

MAS setter `1036a2ac`, independently matched by SLA `10096d20`, selects
`20380000 + (object.byte4 << 18)`. The finite board has bank indices0/1 at
`20380000/203c0000`, with their existing separate IRQs99/100. The setter
unconditionally emits the following eight writes, in this order, from the
first16 bytes of a larger parameter block `P`.

| Bank offset | Source field | Source load width | Observed bank0 value |
| --- | --- | --- | --- |
| `88` | `P[0]` | Byte | `0` |
| `94` | `P+4` | Halfword | `0` |
| `98` | `P+6` | Halfword | `400` |
| `9c` | `P+8` | Halfword | `309` |
| `a0` | `P[1]` | Byte | `3` |
| `a8` | `P[2]` | Byte | `4` |
| `8c` | `P[10]` | Byte | `3` |
| `84` | `P+12` | Word | `1` |

Packet padding at bytes3/11 is not consumed by this setter. `a8` is an
ordinary byte-valued member of this software packet, not a ninth completion
word. Its write is not conditional on bank0, the observed value4 or a busy
query. The source load width does not prove a physical legal-bit mask: the
existing SOURCE11 contract retains full32 configuration words, and the shared
implementation keeps that convention for this group. The values `400/309`
are not sufficient evidence for a particular pixel extent, nor does word
`84=1` justify treating that word as a DMA address.

Wrapper `102e5826` acts only on a non-null parameter pointer. It calls this
setter first, then installs the remaining fields of a 68-byte block through
the adjacent setters `1036a33e/1036a38c/1036a3da/1036a4a6` and scalar/geometry
helpers. Their current destinations are already part of the shared finite
configuration profile; the audit found `a8` as the omitted member of this
first group. It does not open the intervening result words or request holes.

The upper configurator `102e49fe` obtains `P` from the first pointer in its
job configuration. General constructor `102c0a5c` binds those pointers through
`102bfeda`, initially placing the 68-byte block at the start of its backing
store. `102bf33c` may instead select the caller's supplied block at context
offset860. `102c0c82` initializes the first eight fields to a disabled/default
shape; `102c009a` is a separate reconstruction/update constructor with the
same first-pointer layout. Thus the small fields are produced by original
software or a caller-supplied parameter block, not values the emulator must
invent. The non-default observed bytes are retained as observations; their
ultimate format/geometry equations have not been recovered from this setter.

## Configuration and later consumption are separate

Workers `102e5168` and `102e505c` distinguish operation `42190801` from
`42190802`. The former configures through `102e49fe` and then enters
`102e4f5a`; the latter configures, records software configured state and
reports the original software callback without starting a data operation.
Other requests require that configured state before entering `102e4724`.
The exact observed route is qualified in the receipt; these software callbacks
are not host-generated device completions.

Configurator `102e49fe` can first call `10369b58` for ordinary clock/reset
initialization. It installs the core block when its first pointer is non-null,
then other non-null sub-packets. Bank1 with job field204 set uses a distinct
subset; the final retained `e4` field is the boolean conjunction of job
fields204/208. These decimal job offsets are software conditions, not board
registers.

Data start is later and explicit: `102e4f5a` or `102e4724` calls `10369faa`,
which writes selected-bank `10=1` then `08=1`. Wait helper `102e451e` consumes
the original worker events; processing, cancellation and status retrieval
follow their own paths. None of those conditions follows merely from writing
`a8` or from retaining all eight words. Requests and result words without a
modeled data producer remain errors. In particular, this change does not
implement the image operation behind `08/10`, a successful result read or
hardware processing-slot retirement.

## Finite state and assumptions

`hw/nikon/devices/pairs.c` retains each field independently in both existing
banks. There is no DMA or packet parser at this boundary: the original CPU
performs all eight stores. No host transaction rollback is invented for a
partially written group. Unknown offsets, adjacent holes `80/90/a4/b4`,
unaligned accesses and byte/halfword device accesses remain errors; invalid
writes preserve the existing configuration and pending state.

Cold zero, retained readback and clearing configuration on whole-device reset
are explicit functional assumptions shared with the earlier profile. The
added `a8` store has no modeled event or completion side effect. This is a
bounded configuration hypothesis, supported by its placement in the packet
setter and the separate later start/wait chain; it is not a recovered silicon
register specification. A real consumer requiring different field semantics
or a configuration-triggered data result would require extending the contract
with actual input/output evidence.

Writing either bank's packet preserves both raw pending words, inhibition
masks and the other bank's fields. Elapsed time does not publish a result,
clear busy, release a type0 slot or create an IRQ. The existing explicit
fixture pending inputs remain disconnected during original execution.

## Validation and next boundary

Before the change, the native regression fails the HMP read at `203800a8`.
Afterward all23 `nikon_sla` subtests pass. The expanded paired-bank fixture
covers all eight fields in both banks, all256 values of the source byte2
domain, full32 retained values, invalid widths/alignment, adjacent holes,
independent pending/IRQ preservation, elapsed-time stability and reset.
This is a synthetic shared-device contract test, not original SLA image
processing evidence.

The normal unchanged-firmware run uses the same bounded resource35 input,
cold-neutral resource80, quadrant scene, healthy paced UART, held F4 and
five-CPU MTTCG profile. In a60-host-second window without GDB pauses it retains
the eight bank0 values above; bank1 remains zero, both raw pending words are
zero and bank0 mask is `ffffff71`. The original graph loader still records
eight packets/269 words. Slots1/2/8 are busy, with no automatic retirement.
It completes128 main acquisitions and434 statistics jobs, with zero rejections
and no statistics cancellations. The main acquisition path has two cancellations.

The new first uncovered access is CPU1 PC `103bdedc`, WRITE32 PA
`20100e74=1`. Resource tables identify processing type5/resource35, backend
`20100e60`, setter `103bddea ->103bded0`; the setter selects SYS54 for
resource35 or SYS55 for its sibling before the command-reset/import sequence.
This processing resource35 is distinct from NOR resource35 used as an input
profile. The current command loader does not support that type. Error
property6433 and both GUI information records remain zero; neither display
head has a console. Normal GUI publication and image-processing completion
remain unverified. Sequential QMP and read-only GDB observations are not atomic
or physical performance measurements.
