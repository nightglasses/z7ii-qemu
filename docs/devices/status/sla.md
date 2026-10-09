# SLA second-phase status and software objects

Official source5 dispatch `100548D8` selects mode2 at `10054918`; it calls
`10059240` at `10054924`, then `1005955C` at `1005492C`. The matching
mode2 initializer branch starts at `10054964`. These are separate from
the mode1 system-configuration registration at `100548FE`.

| Object | Actual registration | Source initialization | Mapping and IRQ evidence |
|---|---|---|---|
| `14B` | `10059240 → 1006664E`, literal `10059544=C1006400` | `100592DC` enables software IRQ229 and resolves services `E8/154/152` into object `+4/+8/+C` | Literal `10059558=24060000` supplies the status getter's MMIO base. IRQ229 registration alone does not bind this MMIO device to a physical interrupt output. |
| `14C` | `1005955C → 1006664E`, literal `100598E4=C100641C` | `100595F0` resolves services `E8/155/152` into object `+4/+8/+C` | `C144DC44` is a software work buffer selected by `100598E8`. This family has no IRQ definition or enable operation in its recovered initializer. It requires guest RAM and services, not another MMIO device. |

Object14B's complete handler `10059230..1005923E` reads the callback from
`C1006400+10`, and if nonzero tail-calls it with `r0=E5, r1=0`. The
definition site `1005924C..10059250` uses `10059548=10059231` and
`1005EA6A`; initialization `100592DC..100592E4` uses `10066B24` to enable
229. Neither handler nor initializer reads, clears or acknowledges 2406
status. No cause-to-IRQ producer or hardware acknowledgement has been
proved, so `TYPE_NIKON_REQUEST_STATUS` retains no IRQ output and the SLA
machine must not connect this software registration to GIC229.

The complete MMIO getter `10059514..10059540` performs aligned word reads
at `24060024` and `24060030`. It writes status24 bit0/bit1 as words to the
caller's output `+0/+4`, bits2..3 as a byte to `+8`, and status30 bit0/bit4/
bit8 as words to `+C/+10/+14`. There are no MMIO writes or read side
effects in this getter. The existing finite request-status profile returns
unavailable zero from both words and therefore covers this recovered
interface without producing ready, data, a callback or a completion.

The complete software callback `10059262..100592DA` works through service
154, reads nine software words on its nonzero branch, or processes service
state and sets object `+18`'s software event on its zero branch. The complete
request `100594B2..10059506` programs that same service and waits on the
guest software event. These are guest operations; the host must not set
the event or equate service154 completion with 2406 status. Object14C's
`10059576..100595EE` and `100595F0..10059624` use the analogous software
155/work-buffer path with an event at object `+14`; `E8` there is a
registry lookup argument, not an IRQ229 or IRQ232 declaration.

Existing `status.c` remains one strict 34-byte region mapped privately at
`24060000`. Its earlier MAS-only configuration profile remains finite:
word0 bool, words8/10/14 opaque fullword, wordC zero-extended byte, word20
unchanged-control only, status24/30 unavailable. Cold/readback zero remains
a functional assumption, not a measured physical reset. SLA source5 and
source10 do not yet qualify the missing `10059320..100594B1` or
`1005962A..100598C7` method bodies, so the existing MAS profile is not
promoted to a proved complete SLA runtime protocol. The sole source consumer
has one whole-family supplement request; actual method-table/BLX or natural
execution witnesses are required before adding those bodies or behavior.

The exact source5 functions and literals are preserved in `sla.S` and
`sla-rows.jsonl`, source HEAD `8c451b553c582b8fd30224e3347e30cc602eea83`.
Latest officialsource10 still lacks those method bodies. Getter
`1005E8DC` returns `C1016738` via literal `1005E90C`; that returned
address must not be substituted for the actual registry object `C1006400`
or assumed to be a method table without a reader.

`tests/qtest/nikon/status.c` supplies `nikon_status_test(q,status,system)`
for a fresh synthetic SLA VM. It exercises the private finite status
configuration, absence of time/read-triggered status, the complete getter's
two input words, and the five-bank system-control RMW used by SLA source.
Native build and execution belong to the sole builder; this document does
not claim those new checks have run.
