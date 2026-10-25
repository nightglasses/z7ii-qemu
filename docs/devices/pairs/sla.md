# SLA paired banks and sampling families

This audit consumes the existing official SOURCE11 `sla.S` and original
`sla-rows.jsonl` in `zero-domain-source/run sla-whole-source-11`. It does not
decode gaps or treat registry objects as executable method pointers. The models
are reused by their finite protocol, not by coincident IRQ numbers.

| Model | Source registration / initialization | Object | Physical banks | IRQ |
| --- | --- | --- | --- | --- |
| paired-event-banks | `10096450` / `10096534` | `147:C1001430`, `148:C1001438` | `20380000`, `203C0000`, each `5E00` bytes | 99, 100 |
| data-sampler | `10099578` / `100995A4` | `112:C1001448` | `20201000`, `1A4` bytes | 116 |
| table-sampler | `10099976` / `100999BA` | `10C:C1001460` | `20204000`, `42C` bytes | 121 |
| shared-parameter-events | `10099E62` / `10099EDC` | `10D:C100147C` | `20205000`, `318` bytes | 122 |
| parameter-events | `1009A4F8` / `1009A546` | `111:C100156C` | `20203000`, `100` bytes | 124 |

All accesses are aligned little-endian words. Each device owns only its MMIO,
GPIO and IRQ state. None accesses guest memory, creates a DMA engine, or requires
a global address space. Clock/reset service calls are separate devices. Their
registry callbacks do not supply an external sample producer.

## Paired banks

The source family extends from `100963DC` through `1009950A`, beyond the initial
profile literals at `100967DC`. `100963DC` and `10096418` consume bank 1 and bank 0
raw word `24` and acknowledge the exact word they read. `10096450` registers
handlers `10096419` / `100963DD` for IRQ 99 / 100. `10096534` clocks the block,
writes both banks' `0C=1`, `00=04=3`, releases the clock and enables the IRQs.
`100965CC` / `100968C0` also write mask `E8=FFFFFF71`; `F0` receives `8E` or
`FC400`. The mask inhibits delivery, not raw pending retention.

The [eight-field packet](parameters.md) independently matches SLA `10096d20`
to MAS `1036a2ac`. It adds the omitted `a8` member in both banks; the other
seven members already belong to this retained configuration profile. Neither
the original load widths nor one observed value establish physical legal-bit
masks or a completion producer.

`10096CBA..100994C2` supplies the scalar, packed packet and word-array setters.
The old model accepted only exact initial profile values and stopped at `680`.
The shared model now retains the complete source-proved configuration words,
including their bank stride `40000`. Packing is a firmware producer convention;
it is not evidence of hardware reserved-bit masks. Ordinary configuration writes
therefore retain the whole word. The opaque `00/04=3`, `0C=1`, bank-0 `D0<=1`,
and `F0` command shapes remain constrained. No control write creates an event.

The `100994E8` helper copies `size / 4` words from the caller's packet into a
selected bank. It is a CPU store loop, not DMA. Its literal-backed table spans
are finite: `1100..113C`, `1200..1244`, `1300..1320`, `1328..1348`,
`1350..1370`, `1378..1398`, `1D38..1E34`, `1F00..1F40`, `1F48..1F88`,
nine-word groups at `1F90/1FB8/1FE0/2008/2030/2058/2080/20A8`,
`20D0..20DC`, `2100..22FC`, `2400..288C`, `2890..2C94`,
`3900..42B8`, `42C0..44C0`, `44C8..49E8`, `49F0..4D0C`,
`4D14..512C`, `5134..5A54`, `5A5C..5B64`, and `5C00..5DFC`.
The individual four-byte gaps between table groups remain inaccessible.
The indirect table selection at `10098A00/10098A20` additionally consumes the
original pointer array `1016AF08`; destinations require its actual snapshot,
not inference from adjacent table addresses.

Status consumers use `100994E2`. Their result words, including `18`, `118`,
`350..360`, `388..390`, `3B8..3C0`, `3CC..3D4`, `3E4`, `3F4..3FC`,
`474..484`, `4CC..4DC`, `680`, `77C/780`, `9DC/9E0`, `BC0/BC4/BC8/BD0`,
`E1C` and `F30`, have no recovered result producer and remain strict errors.
Requests `08/10/54/60/6C/78` likewise remain unsupported. `580` is now proved
as a full-word setter by `10097934`; the source does not prove an event side effect.

The existing functional provider `pending-bit[0..31]` latches bank-0 causes;
`[32..63]` latches bank-1 causes. Each rising input sets its raw bit. `24` is
W1C, `E8` is a full retained inhibition mask, and the IRQ is `raw & ~mask`.
This GPIO edge policy is an explicit virtual law; firmware ISR reads do not
prove physical edge versus level behavior. Providers default disconnected.

## Other four complete methods

The existing data sampler covers `10099524..100998E2`, including mode setter
`10099880`, complete scalar/array configuration, and request `1009986E`.
Initialization selects `44=2`, `48=0`; request mode selects `44=3`, `48=7`
before `14=1`. An armed sample requires a fresh complete 32-bit `data-bit`
publication followed by `external-event[0]`. It atomically retains `1A0` and
sets raw bit 0. Event inputs 1 and 2 set bits 1 and 6. `2C` is W1C `43`;
ACK and the ISR's `00=1/0` pulse do not invalidate the sample.

The existing table sampler covers `1009990C..10099DE6`: both bit-0 control
RMW helpers, complete packed scalars, coefficient arrays, all 128 packed table
words at `200..3FC`, and request `10099D56`. Request requires `44` bit 16
before `14=1`. All 96 sample bits must be freshly published before event 0
captures `194/198/19C` and sets bit 8. Event 1 / 2 set bits 17 / 25. `2C`
is W1C `02020100`. The ISR acknowledges before reading retained samples.
Counter/result reads `408/428` remain unknown and rejected.

The shared model covers `10099E62..1009A480`, including all actual configuration
methods, shared scalar and coefficient words, the six software flag publication
into `308/30C/310/314`, and `04` bit-0 RMW. The registry's handler pointer is
`10099DF5`, from literal `1009A200`; its missing body is submitted once to the
sole source qualification owner. No missing ISR behavior is invented. Event
inputs 0 / 1 set raw bits 0 / 2; `2C` is W1C `5`. `304` activation and its
result are unknown. Packet counts in the firmware are not an invented hardware
count register; only already proved shared physical words are accessible.

The parameter model covers `1009A490..1009AC80`: initialization publishes the
software shadow at `10`, packet setters retain the scalar/boolean fields, and
six four-word arrays occupy `A0..FC`. The large `1009A672..1009AA82` BFI
sequence modifies RAM shadows, not additional MMIO registers. Event inputs 0 / 1
set raw bits 0 / 10; `2C` is W1C `401`. `08` activation remains unknown.
The ISR's `00=1/0` pulse retains configuration and pending causes.

For these four models, `20` is a retained parameter, not an IRQ mask. Pending
bits are ORed for IRQ delivery. Known configuration resets to functional cold
zero. Reset clears raw/mask/armed/sample-valid state, preserves external input
levels and does not create fresh edges. No elapsed time alone publishes sample
data, readiness or completion. These are functional assumptions, not a claim
about silicon power-on state or physical sample timing.

## Native handoff

`tests/qtest/nikon/samplers.c` exports `nikon_pairs_test(q,path,first,second)`,
`nikon_sampler_test(q,path,base)`, `nikon_table_test(q,path,base)`,
`nikon_shared_test(q,path,base)` and `nikon_parameters_test(q,path,base)`.
Each requires a fresh VM and intercepts that device's named `sysbus-irq` once.
They cover actual initialization/configuration, finite table endpoints, bank
independence, default disconnected providers, fresh complete sample publication,
partial W1C retention and reset. The sole builder executed the private pairs
case and full provider-topology case with exit0 in1.34s each;
`run slapairsnativereceipt.log` and `run slaprovidertopologynativereceipt.log`
record those separate invocations. The four unchanged event protocols were
not rerun; topology exercises their real private-GIC event/ACK paths.
