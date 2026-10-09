# SLA router source audit

The fixed provider is `zero-domain-source/work/sla-whole-source-11/sla.S`
with its original `sla-rows.jsonl`. Recovered `1009EDD4..1009F4AA` instructions
and literals `9F1B8..9F1E0`, `9F4C4/9F4C8` establish the finite interface
below. This is an audit against the mature `router.c`, not a new physical
identity inferred from its address or IRQ numbers.

`9EE4C` registers object `11A` at `C10062BC`, then handlers `9EE13`,
`9EDE9`, `9EDD5` for IRQ86,87,88. `9EE7E` stores guest service references
152/E8/156 and enables IRQ86..93. Only the first three have handlers and MMIO
consumers established in this family; enabling the other numbers does not
establish five additional peripheral outputs.

| Entry | Source-proved behavior |
| --- | --- |
| `9EE12`, IRQ86 | Read `20060038` once; loop low eleven bits, acknowledge each set bit with its single-bit write; call registered guest callbacks with event `41080C00` and the saved full pending word. |
| `9EDE8`, IRQ87 | Read `20060044`, write the same word back, pass saved word to two optional guest callbacks with event `41080C01`. |
| `9EDD4`, IRQ88 | Read `20060050`, write the same word back, optional guest callback with event `41080C02`. |
| `9F030` | Extract six bits at16 and fifteen bits at0 from packed argument; selector0 writes them to `00/08`, selector1 to `04/0C`; other selectors issue no MMIO. |
| `9F0CC`, `9F0EC` | Mutex-protected full-word OR/clear-mask RMW at `34`. |
| `9F15A` | Copy six addresses from table `1016B0D8`; write `r3 | (r2 << 4)` through the selected address. |
| `9F17C`, `9F19A` | Select an address from the following two six-word tables, then write unclamped argument `r2`. |
| `9F1E4` | Select an address from six-word table `1016B120`, then write unclamped argument `r2`. |
| `9F438` | Read `B0`; return whether its current value is nonzero. No independent ready producer is established. |
| `9F448` | Copy eleven callback masks from `1016B180`; OR masks of supplied nonnull callbacks into `34`, storing callback pointers in guest object RAM. Other supplied callbacks also remain in guest object RAM. |

The firmware's guest callbacks and mutexes stay in guest code/RAM. The host
model does not execute callbacks, synthesize a software event flag, supply
request data or alter guest task state.

## Reuse and remaining source boundary

The mature `TYPE_NIKON_PERIPHERAL_EVENT_ROUTER` covers every direct operation
above, including full-word mask RMW, three separate pending words and selective
W1C. The private SLA binding is MMIO0 `20060000`, size `330`; SysBus outputs
0/1/2 connect to IRQ86/87/88. Machine mapping belongs to the SLA owner. The
75 named `external-pending` inputs remain disconnected by default.

The inherited finite configuration masks and six lanes are enumerated in
`protocol.md`. Cold configuration/pending zero, configuration readback,
one-enabled masks and explicit rising-edge providers are functional model
conventions. Mask40/4C filtering is a convention, not physical gating proved
by these handlers. A configuration write or clock advance cannot produce
pending state. Masking retains pending; acknowledgment clears only selected
bits; reset clears configuration/pending while retaining actual input levels.

SOURCE11's larger family projection still omits `9F064..9F0CB`,
`9F10C..9F159`, `9F1FE..9F437`, and the pointed-to table contents above.
Existing older continuous diagnostic material mentioned in `protocol.md` is
not promoted into officially qualified instructions here. The sole source
consumer has been asked once for already-qualified whole bodies and exact
table facts. No new decoding/index or inferred function entry was performed.
Requests `28/2C/30`, unknown status/data, holes and unsupported widths remain
rejected. Neither the table pointers nor a nonzero retained B0 value proves
an activation, transfer or physical ready protocol.

The standalone helper `nikon_router_test(QTestState *, const char *, uint64_t)`
in `tests/qtest/nikon/router.c` is for a fresh private SLA VM, recommended
`/machine/sla/router`, base `20060000`. It checks the source-shaped packed
configuration and inherited finite fields/lanes, all 75 explicit providers,
three independent masked IRQs, the exact two acknowledgment styles, no event
from clock/configuration, and reset with a held provider. It does not exercise
unknown activation or pretend to establish a physical event producer. Native
compilation and execution belong to the sole builder; no local result is
claimed by this child.
