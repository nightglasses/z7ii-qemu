# SLA event gate source contract

Provider: `zero-domain-source/sla.S` and original
`sla-rows.jsonl`, recovered `100943CE..100953E2` family. The c245 original
runtime first stopped at CPU3 `PC100944D2`, W32 `20100000=0`. No new decoder,
native collection, original run or build was executed by this owner.

Reuse `TYPE_NIKON_EVENT_GATE`: one aligned little-endian word MMIO region,
`20100000..20102928`, one SysBus IRQ output to IRQ102. No DMA address-space
link, transfer backend or global address space is needed for this finite
interface. The SLA owner supplies private mapping/wiring; `pending-bit[96]`
inputs remain disconnected by default.

`943CE` registers146 at `C10012C4` (literal9473C=C100138C minusC8), then
registers IRQ102 with Thumb handler94339. Service state at `C10012B4` holds
references to152/E8. `944AC` resolves them, calls `94458` to clear SYS indices
20/21/22/38 and a13-byte ROM index table at1016AEE0, enables packed E8 gates
20000/20003/20002/20001 through943E8, writes control00=0, reverses those gates,
then enables IRQ102. These are guest service calls, not extra registers in
the gate aperture. `944E8`/`944F8` perform service setup/teardown and software
callback-table clearing; the model does not replace those guest operations.

| Source methods | MMIO or guest operation |
| --- | --- |
| `9453A` | Global04 RMW clears low two bits then sets1. |
| `9454A` | Guest descriptor loop invokes guest handlers from ROM table1016AB20. |
| `9457C` | Indirect word write1 through a ROM-selected target; target facts needed separately. |
| `9458A/945A2` | Input2000/2100 RMW sets/clears low nibble while retaining higher bits. |
| `945B6` | Input packet at2000 or2100: words00/04/08/10/14/18; writes20A4=0 first. |
| `94640` | Input04 RMW: `old & ~1 | r2`, with no clamp on the OR operand. |
| `94688` | Conditional64-word writes into separate2200/2300 maps. |
| `946C4/946D6` | Pulse20A4=1,0; indexed read from2040 respectively. |
| `946E0` | Parameter packet selects2600/2640: 00/04/0C/10/18 after clearing2620/2660. |
| `94788/9479C` | Full-word write/zero to2608 or2648. |
| `947B2/947CC` | Pulse2620/2660; request2680=1 respectively. |
| `947D8/9481E` | Packed parameters2684(full word),2690(halfword shape). |
| `94836/948A6` | Seven packed2820..2838 writes/readback; setter ends with submit2800=1. Whole byte operands justify full words except final halfword-shaped2838. |
| `948FE/9491A/9493A/94970` | ROM-indexed configuration/readback; address tables need separate facts. |
| `94996` | Submit2800=1. |
| `949A2/949DA/949EC` | Three full-word masks0C/10/14 and protected OR/clear-mask operations. |
| `949B2/949BA` | Global04=0; compound pulses20A4/2620/2660 then global08=0. |
| `94A52/94ACA..94C40` | SYS-controlled zero/copy of four external1024-byte storage areas24010000/400/800/C00, plus monitor reads200/220..2AC. No gate DMA engine is established. |
| `94C44` | Monitor reads1B4/1B8/1BC. |
| `94C7A` | Full-word configuration30/34 RMW with shifted byte operands. |
| `94CD2..94E16` | Packed configurationF4/F8/FC/100/104/108/10C/120..12C. |
| `94E30..94F16` | ROM/software descriptor table selection and retained descriptor mutation. |
| `94F18/94F1E..950BA` | Software descriptor store, ROM-targeted13-word configuration and selector2C; an8-counter-tick delay follows. |
| `950BE/950E4/95102` | ROM-targeted state read, pulsesE74/E94, and global00 boolean write. |
| `9533E` | Guest callbacks selected by software event masks, event41051002. |
| `953D0` | ROM-targeted pulse at+14 and full-word+10 parameter write. |

The shared implementation already covers all resolved finite fields. One
production correction removes the old3FFF limit from input2004/2104: complete
method94640 preserves arbitrary high bits and ORs a full argument, so the
packet setter's generated14-bit value cannot constrain the whole interface.
This is retained configuration; it supplies no pending, data or completion.

The later MAS [type0 slot contract](slots.md) adds sixteen complete parameter
packets, selector2c and retained busy ownership to the shared class. This is
source/native MAS scope, not an original SLA slot-cycle observation. Request2800
records an outstanding submit only. Unknown activation, pulses and monitor/data
remain strict errors:20A4/2620/2660 assert1,2680, global00=1 and E74/E94 are not
assigned fabricated operating effects. The model cannot claim
successful execution of a whole compound method that reaches one of those
unsupported operations. The later MAS [command contract](commands.md) adds
source-bounded import, LUT and24010000 workspace storage to the shared class;
its native SLA fixture is not original SLA processing evidence. The adjacent
95408 family uses literal24073400 and is separate.

## Functional conventions and proof limits

SOURCE11 lacks the registered94338..943C8 ISR body and95112..9533D closure.
Those exact missing bodies have been sent once to the sole source consumer,
with the actual943CE registration and typed Thumb pointer as qualification
evidence. They are not reconstructed from gaps. ROM index/pending/configuration
tables are not inferred from their addresses.

Cold zero, retained readback, explicit external rising-edge inputs, one-enabled
masks, W1C and the already mature nominal20us stop acknowledgment are model
conventions. Global04=0 schedules one deferred raw18.bit0 acknowledgment;
reopening before its deadline cancels it, and ACK18.bit0 retires it. The stop
uses mask0C.bit0 independently of the closed external gate. This existing
internal control acknowledgment is not a new physical producer or a data/frame
completion claim. Uncommanded clock advance, configuration and readback have
no pending side effect. No host callback or guest eventflag write is supplied.

New standalone helper `nikon_gate_test(QTestState *, const char *, uint64_t)`
in `tests/qtest/nikon/gate.c`, recommended fresh VM path `/machine/sla/gate`,
base20100000, covers the exact initializer, both whole input/parameter groups,
full-word RMW regression, finite table/packed configuration, three raw/mask
domains, shared IRQ retention, selective same-word ACK, no uncommanded event,
explicit baseline stop delay, and reset/held-provider behavior. Native build
and execution belong to the sole builder; this child does not claim a result.
