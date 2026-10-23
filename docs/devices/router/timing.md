# Finite router timing and requests

This functional model closes PA `20060028/2c/30` together. Original MAS
instructions establish the command formats and guest consumers; the phase,
completion and input cadence below are explicit, falsifiable hypotheses. They
are not measurements of the physical Z7II timing generator.

## Evidence and interpretation

All code addresses in this section are C1.70 MAS VAs. The
[source audit](runtime.md) records the complete request producers and IRQ
consumers. `102d93a2` requests28, performs force70=`2→3→0`, requests28 again,
clears control00 low two bits, enables special11 through10 and waits for
CSENEV01 bit8. Original IRQ87 callback `102d6b6a` maps raw44 bit7 to
CSENEV01 bit4 and bit4 to bit8, after their optional countdowns.

The stop path `102d986e(1)` clears CSENEV01 bit20, clears special11,
stops the sequence, submits2c through `102fea0e`, then waits for bit20.
The callback maps raw44 bit10 to that flag. This supports a synchronization
completion interpretation more strongly than the interpretation of30.
The30 producer selects special12 or follows the compound A4 update; the
IRQ88 handler reads/ACKs raw50 and optionally dispatches an object callback.
No recovered bit-specific consumer establishes a completion cause for30.

The fixed Z7 C3.80 implementation uses the same
[paired-control gate](https://github.com/WonderLantern/z7-qemu/blob/97abf0b81cd3211f8374a38987a0beaf4549b90f/qemu/nikon-z7-lab.c#L1455)
and has an alternating80/10
[diagnostic timing source](https://github.com/WonderLantern/z7-qemu/blob/97abf0b81cd3211f8374a38987a0beaf4549b90f/qemu/nikon-z7-lab.c#L1338).
This corroborates the functional direction; it does not prove the physical
rate, phase mapping or request effects.

| Interface | Finite functional hypothesis | Discriminating prediction |
|---|---|---|
| WRITE32 `28=1` | Resynchronize the boundary phase; preserve configuration and latched pending | No ACK, ready or frame appears from resynchronization alone |
| WRITE32 `2c=1` | Resynchronize and complete a register synchronization fence after1us virtual time | Raw44 bit10 latches, even after special11 was disabled; original callback can release bit20 |
| WRITE32 `30=1` | Complete an independent auxiliary configuration fence after1us | Raw50 bit0 latches without changing A4, enabling special12, transferring data or producing a frame |
| Enabled virtual TG input | Alternate high/low boundaries, raw44 bit7/4 | Original ISR/callback releases bit4/8 waits through normal IRQ delivery |

The30 bit0 identity and both1us fence delays are virtual conventions, with
weaker evidence than the2c consumer join. They must be revised if a recovered
consumer requires a different status identity or ordering. Native fixtures
validate this declared contract; only28 and the TG source were observed in
the bounded original continuation before its next error.

## State and boundaries

`input-period-ns` is a qdev virtual input:0 disconnects the periodic/forced
boundary source; values at least2 supply one complete cycle. Value1 fails
realization. The MAS board supplies33333334ns, approximately30Hz; private
SLA retains0. This rate is not decoded from retained clock field08.

Automatic boundaries require input present, special11 enabled, control00
low two bits clear and force70 bit1 clear. Low phase lasts ceil(period/2),
high phase floor(period/2); absolute virtual deadlines avoid accumulated
rounding drift. Force70 bit1 selects bit0 as the held level. With input and
special11 enabled, only actual level transitions latch causes; release
returns low and resumes automatic timing when its gates permit. Other70
bits remain retained without an assigned effect. Stopping/disabling cancels
the boundary timer and returns phase low without a new completion.

The two fence classes have independent timers. Repeated outstanding requests
of the same class coalesce without postponing the first deadline; a later
request of the other class cannot complete early. Resynchronization does not
erase pending or cancel fences. Mask writes retain pending and recalculate
IRQ; W1C clears only selected causes. Reset and unrealize cancel all three
timers and clear command identities/configuration/pending. Reset preserves
the configured input period and actual external-provider levels.

Only aligned WRITE32 value1 is accepted at these three ports. Reads, other
values, unsupported widths, holes and invalid fields remain errors before
mutation. The model uses QEMU virtual timers and the existing pending/mask/GIC
path. It never writes guest flags, invokes guest callbacks or supplies DMA,
sensor pixels, a completed frame or scanout.

## Validation and original continuation

`make check MTESTARGS='nikon_sla nikon_memory'` passes23 SLA plus25 memory
cases. New native reds reproduce the unsupported request and a shared-fence
deadline bug. Coverage includes all three commands, simultaneous/staggered
deadlines, duplicate coalescing, mask/W1C, phase boundaries, gates/force,
absent input, invalid accesses/period, reset and unrealize/re-realize.

Final-binary execution keeps original bundle bytes unchanged and uses the
declared TEMP3/descriptor18, empty descriptor12, acceleration(0,0,1024), RNG0,
healthy paced UART peer and cold held-F4 inputs. After8hostseconds, the
read-only snapshot and bounded TCG register trace show:

- Original IRQ87 handler `102dc5c0` dispatches `102d6b6a` with event41080c01
  and raw80/10, twice each. No callback is invoked by the host.
- The bit8 timed wait returns1 at `102d94a2`; the later wait returns1 at
  `102d9510`. Application bytes advance from02/01 to03/02 and primary
  request state from1 to11.
- The next first rejection is CPU0 PC `1020a832`, WRITE32 PA
  `2006800c=1`, reason `capture-control-access-or-reset-value-not-supported`.
  Its original primitive `1020a830` stores argument R1 through base20068000
  offset0c; the whole08/0c control family is the next investigation.
- The battery peer rejects no request. GUI gate, pending3 and display-ready
  remain0; no GUI first frame or physical timing correctness is established.

Ignored receipts: `work/runs/routerfunctional-20261022/`, including native
red/staggered-red, `native-final-receipt.log`, `final-argv.json`, `final.trace`,
`final.json`, `final-process.json` and `final-callchain.json`. The earlier
`router-20261022` run remains the paired-control-only checkpoint.
