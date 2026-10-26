# Type9 and type8 streams and linked processing inputs

C1.70 MAS type9/resource45 imports a 136-byte logical-register stream after
pulsing PA `20100914`. Its source graph connects upstream type5/resource35,
three type8 branches and type11/resource62. The finite model imports both
type9 and type8 layouts across all eight source-selected ports. It retains
their routing and borrowed references; actual pixel processing and terminal
publication remain unsupported. The unchanged cold-neutral continuation
imports 17 packets/628 words before reaching type18/resource34 initialization.
See the [receipt](/evidence/imagerequest/linked.json) and
[startup frontier](../../validation/frontier.md).

Code, ROM and software addresses below are original C1.70 MAS virtual addresses
(VA); device and explicitly identified RAM addresses are board physical
addresses (PA). Addresses, offsets and masks are hexadecimal. Counts, lengths,
types and resource numbers are decimal. Ranges exclude their upper endpoint;
device accesses and stream words are aligned little-endian 32-bit.

## Source-selected ports and stream boundaries

ROM resource/backend tables select these ports and raw callback masks. Domain
indices are zero-based: domain1 is pending PA `2010001c`, domain2 is
PA `20100020`. Masks identify callback routing, not a completion-on-import rule.

| Type | Resource | Backend PA | Raw domain | Mask |
| --- | --- | --- | --- | --- |
| 8 | 41 | `201008a0` | 1 | `20` |
| 8 | 42 | `201008c0` | 1 | `40` |
| 8 | 43 | `201008e0` | 1 | `80` |
| 8 | 44 | `20100c80` | 2 | `10` |
| 9 | 45 | `20100900` | 1 | `100` |
| 9 | 46 | `20100ac0` | 1 | `400000` |
| 9 | 47 | `20100cc0` | 2 | `40` |
| 9 | 48 | `20100ce0` | 2 | `80` |

Callbacks `103bdc82` for type8 and `103bdc8a` for type9 take the node's
resource byte and payload pointer into common helper `103bded0`. It writes
backend+`14=1`, then `0`, then the pointer at backend+`10`. Constructors
`103b35d6` and `103b39e8` establish the following layouts and cache-clean
extents. Header counts include the header itself.

| Type | Payload byte offset | Header | Logical data destinations | Total stream bytes |
| --- | --- | --- | --- | --- |
| 8 | `0` | `111c0000` | `0..68`, 27 words | 128 |
| 8 | `70` | `01040078` | `78/7c/80`, 3 words | 128 |
| 9 | `0` | `01220000` | `0..80`, 33 words | 136 |

The type8 gap at logical `6c/70/74` is absent from the packet; it is not a set
of zero-valued register writes. Original execution supplies these headers for
resources41/42/43 and45. Applying the same layouts to resource44 and46..48 is
a bounded inference from their common type constructors and field extents;
those siblings are covered by native fixtures, not this original run. A
different caller template must remain an explicit unsupported variant.

## Type5 input, output routes and interpolation reference

The [type5 contract](fanout.md) resolves its logical `404=8` to resource45.
Type9 constructor `103b39e8` distinguishes the source ancestor from the linked
input stream. It follows graph adjacency through type5/6 when deriving the
source-resource field, while `103b2a3a` separately selects the input-stream code.
The observed type9 payload at VA `b2bd9e68` retains these fields:

| Type9 logical offset | Value | Source-backed interpretation and limit |
| --- | --- | --- |
| `0..c` group | `6a/ff/ff/ff` | Four downstream route words; `ff` is the constructor's absent-route value |
| `18` | `4080` | Bits4..7 select source ancestor resource8; other bits retained |
| `50` | `0088001e` | Borrowed-record metadata; complete hardware interpretation unresolved |
| `54` | `c326b400` | Reference inside the source-declared kind74 PA pool |
| `64` | `4` | Low5 bits select input stream4, mapped from resource35/type5 |
| `68` | `00040302` | Packed downstream branches2/3/4, resolving to type8 resources41/42/43 |

Graph resolver `103b2e86` and selector `1038944a` choose downstream slot codes
from ROM `10789cc0 +4*(resource-13)+slot`. Resource62's row at VA `10789d84`
contains `6a/6b/ff/ff`, so route `6a` identifies type11/resource62 slot0.
Routing helper `103b2592` builds the packed branch fields. Neither the ancestor
selector8 nor the separate input selector4 establishes a type5 bypass or
identity transform. Other control/format words retain their full32-bit values
without speculative arithmetic.

The [kind74 storage contract](../../runtime/interpolation.md) establishes PA
`[c24cb400,c33cb400)` and software-generated 40-byte records. Its producer
chain ends at `10404354`, which writes eight 23-bit interpolated fields and a
tag at byte offset `20`, leaving the last word untouched. In this continuation,
the first16 records at PA `c326b400` all have tag `306` and a zero last word.
Their nonzero fields and tag agree with that software record family. This
observation supports the reference association; it does not prove the hardware
fixed-point format, resampling equation or complete referenced extent.

Type8's observed logical `50=00010001` accompanies logical `54` references
`b2bda128/b2bda1c8/b2bda268`. Their borrowed records are retained inputs,
not command-import DMA targets. The complete source-selected slot8 base/plane
association, type5 table arithmetic, type9 transfer/output format and type8
processing remain unresolved. Those are the required evidence for an actual
processor; no identity/black result is supplied in their place.

## Finite lifecycle and terminal ownership

`hw/nikon/devices/gate.c` and `gatepacket.c` reuse the shared
[reset and atomic-import lifecycle](commands.md#finite-import-lifecycle).
Reset clears only the selected command snapshot and pointer; deassertion arms
one import. The exact complete prefix must come from readable RAM through
QEMU DMA. Invalid extent, alignment, header/count/final marker, held reset,
unarmed and repeated submissions reject without a partial installation.
Import retains borrowed references without following them or claiming they
are valid execution buffers. Activation still rejects explicitly.

The maintained port set has37 entries. Its read-only QOM `configured-mask`
therefore uses64 bits, with 64-bit shifts and isolated reset clearing; this
diagnostic width does not change the firmware's register widths or RAM bounds.
Cold empty state, configuration readback and one-import admission remain
functional conventions rather than recovered physical status timing.

Worker `103b4adc` configures the whole graph, then `103b4cea` reverse-starts
processing nodes through `103bd07c` and forms its terminal wait from resources
49..56. IRQ102 handler `103bce38 ->103bde3e` dispatches original callbacks;
normal software release `10388b3c ->1038897e` clears software ownership and
wakes the waiter without a hardware slot-query write. Type9's raw mask `100`
does not itself establish a terminal result. Actual output DMA and its result
condition must precede any modeled completion or type0 slot retirement.
Command reset, elapsed time, W1C and stop acknowledgment provide none of them.

## Validation and original continuation

The old device fails the focused HMP reset read at PA `20100914`. The accepted
native batch passes four decoder and23 SLA subtests. It checks all37 ports,
both new layouts, every shorter prefix, all header-bit corruptions, the type8
gap and atomic failure. High-bit configured-mask/reset isolation, RAM DMA
boundaries and unsupported activation are covered. A deliberately non-RAM
borrowed reference remains retained without import-time dereference. No
elapsed-time event/IRQ or command-reset slot retirement occurs. These are
synthetic contracts, not original image-processing results.

With unchanged firmware and bounded NOR resource35/cold-neutral resource80,
quadrant scene, paced healthy UART/RNG0/held F4/five-CPU MTTCG, the60-host-second
run imports17 packets/628 words. Type9/resource45, three type8 ports and later
already-supported nodes cross configuration; slots1/2/8 stay busy. There is no
GDB pause inside the window; subsequent QMP and read-only GDB are sequential.

The first uncovered access becomes CPU1 PC `103bddd2`, WRITE32 PA
`201002c0=1`. ROM selects type18/resource34, backend `20100920`, callback
`103bdd8c`. That callback clears SYS41..44, checks `20100148.bit8` and, when
absent, writes `201002c0=1` before calling `10361034(24010000,4096)`, then the
common reset/import helper. This identifies the next complete initialization
contract, before graph activation. The run completes128 main acquisitions and
529 statistics jobs, with zero rejections/statistics cancellations and two
main cancellations. Error property6433, GUI readiness and both information
records remain zero; neither head has a console. Actual processing, slot reuse
and normal GUI publication remain unverified.
