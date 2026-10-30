# SLA result readout

The semantic name describes the observed configuration/request/result interface,
not a physical IP identity, pixel operation or known accumulation algorithm.
This is a functional model; it does not qualify GUI boot or real hardware.

## Original evidence

Component 04 SLA C1.70, original ROM VA base `10020000`. Existing SOURCE11
`sla.S` and `sla-rows.jsonl`, producer `ef6395d`, contain the decoded
consumer cohort `1009ACA0..1009B0FA`. The preceding family ends at `1009AC80`;
`1009B12C` begins a different dispatcher. The unrepresented interval
`1009B04A..1009B064` has no row in this source cohort; no additional executable
or physical function is claimed from that interval. Source ownership has been
asked to qualify its code/data classification through the existing pipeline.

Mode2 dispatcher `10093CEC` calls registration `1009ACE2`; `10093D50` calls
initialization `1009AD52`. Registration derives logical object `10F` at guest
RAM VA `C10015AC` from literal `C1001588+24` and registers IRQ115 with the Thumb
ISR `1009ACA1`. The ISR uses event identifier `41053001` for its guest callback.
Machine binding uses SLA-private PA `20200000`, region size `310`, IRQ115.
The model never calls or fabricates the guest callback.

| Original consumer | Hardware transaction |
| --- | --- |
| `9ACA0..9ACE0` | Read status +30, write same word back; inspect bits0/2/4; submit next request before guest callback |
| `9AD52..9AD7A` | Initialize guest RAM, obtain clock service E8, enable IRQ115; no direct MMIO |
| `9AD7E..9AE00` | Write two-bit shadows to +308/+30C, submit paired request |
| `9AE04..9AE66` | Stop paired request; later write1 to +4 |
| `9AE6C..9AF88` | Write all configuration fields listed below |
| `9AF8C..9AFA2` | Three fullword setters +28/+24/+20 |
| `9AFA4..9AFE6` | Read 40 words each at +100..19C and +200..29C, emit low16 / bits8..23 / low8 into three output halfword arrays |
| `9AFE8..9B020` | Read +4 bit0, update +30C/+308 and write1 to +304 |
| `9B066..9B0C4` | Write1/0 to +0 then +300; two-bit +308/+30C enable shadows |
| `9B0C6..9B0FA` | Build bitfields in guest shadow, write fullword +44 |

All actual MMIO transactions in this cohort are aligned 32-bit little endian.
The byte store at `9AE78` targets guest RAM `C100158C`, **not** MMIO +304.
Its fullword shadow subsequently reaches +40. All `C10015xx` shadows and the
callback pointer remain ordinary guest RAM, not device registers.

| Offset | Source-written mask |
| --- | --- |
| 20,24,28,80,84,88,94,98,9C,A0,A4,A8,AC,B0,B4,B8 | FFFFFFFF |
| 40 | 7FFF |
| 44 | 1773 |
| 48,4C,50,58,5C,60,64,68,6C,74,78,7C | FFFF |
| 54 | 773 |
| 70 | 3FFFBFD |
| 8C,90 | 3FFF |
| BC | FF |
| 304 | 1 |
| 308,30C | 3 |

## Functional conventions and provider boundary

Source-known configuration latches cold-reset to zero and read back written
fields. Reserved bits, holes, other widths and writes to result windows reject
with the common PC/address diagnostic. These cold values/readback laws are
functional conventions, not measured reset values. Configuration words +308
and +30C remain distinct; their relationship to physical interrupt routing is
unproven. They do not manufacture events or reinterpret status bits as masks.

Writes +0=1 then +300=1 produce a named `request` output and a new nonzero
generation. Either zero write cancels the generation. A repeated paired submit
creates a new generation even if the register latches already contain1.
These paired-generation/cancel laws are explicit functional conventions based
on the source's submit/stop sequence, not cycle-accurate hardware timing.

A real producer calls `nikon_results_publish` with that active generation,
two complete arrays of40 words and observed event bits from {0,2,4}. The model
copies the complete result set atomically, retains it across the next request,
records pending bits and raises IRQ for nonzero pending. W1C +30 acknowledges
only the supplied bits. Retention is necessary because the original ISR
submits again before invoking its guest result callback. There is no timer,
automatic completion, image generation or host-written firmware state.

The IRQ aggregation law (nonzero producer pending means IRQ) is a functional
convention. Actual individual mask/routing laws need producer-side evidence.
`nikon_results_state` supplies the observed +4 bit0 independently of requests.
The guest +4=1 command cancels requests and invalidates state/results/events;
it does not synthesize reset completion. Configuration latches are preserved
by this guest command; device cold-reset clears them. Those command/reset
effects are functional conventions, distinct from the observed write itself.

Before a producer supplies state, +4 reads reject. Before an accepted publish,
+30 and both result windows reject. Cancelled, replaced, completed or reset
generations cannot publish; unsupported event bits or missing arrays reject
without changing state. There is no connected producer in the current private
machine binding. Thus configuration can proceed, but results and physical
readiness remain blocked rather than fabricated.

## Native coverage

`tests/unit/nikon/readout.c` uses the complete ARM system library, official
`qemu_init` and real MMIO dispatch. It covers every source configuration field,
reserved bits, real widths, unknown holes, missing-provider errors, both full
40-word windows and the original extraction layout, state input, request
tokens, stale/cancelled results, partial W1C/IRQ, ISR resubmit retention,
guest reset, cold reset and unrealize cancellation. The sole builder compiled
the fixed `04c8164` results API and executed this native case with exit0 in0.65s;
the receipt is `run readoutprotocolnativereceipt.log`. It does not establish
an original hardware result producer or camera startup.
