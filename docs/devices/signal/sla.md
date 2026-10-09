# Complete SLA five-route source audit

The actual original878 stop was CPU3 PC`100898AA`, aligned W32
`28000000=F`. The subsequent abort-route `10000AAA=AA` is not a protocol
request or an acknowledgement source. No guest PC special case, RAM patch,
NOR acknowledgement or relaxed access guard is needed to implement the
original device interface.

Officialsource11 `sla.S` lines306367..307334 and its preserved original
rows contain the complete source family `100897E4..10089B32`. Registration
`1008987C` creates a guest mutex at `C1000E94`, registers object14D at
`C1000E90`, then defines IRQ237 with handler `100897E5`. The complete
initializer `1008989E..100898B4` writes `28000000=F`, writes `2B000004=3C`,
then enables IRQ237. Source rows and the actual MMIO witness remain distinct
evidence; an instruction's `source_accepted:false` is not silently promoted
to an accepted whole executable image by this audit.

| Full method | Source-proved device interface |
|---|---|
| `100897E4` | Reads word8, writes the same value to acknowledge pending, dispatches five 2-bit cause pairs through guest callbacks. |
| `100898C0` | Word0 per-selector clear-then-set pulse. Selector order corresponds to physical bits0/1/4/2/3. |
| `1008991C` | Word4 RMW for gates: selector0/1 use bits4/5, selector2 uses bit3, selectors3/4 share bit2. |
| `1008995E` | WordC RMW for five pending pairs, word8 acknowledgement for disabled causes, stores a guest callback pointer separately. |
| `10089A0E` | Reads word10 shifted by selector0/1/4/2/3 and masks101. |
| `10089A3C` | Pulses word0 around word14 RMW: per-selector boolean at bit0/1/4/2/3 and two-bit field at8/12/24/16/20. |
| `10089AE2` | Word18 masked RMW, preserving bits outside the caller's mask. |
| `10089B06` | Separate two-lane configuration writer, detailed in `docs/devices/lanes/protocol.md`. |

All device operations in this whole family are aligned32. The common ISR
dispatches physical pairs0/1 to callback0,2/3 to callback1,4/5 to callback3,
6/7 to callback4,8/9 to callback2, with event argument `41090001` from
literal89BF0. The producer selector order and callback array permutation
are separate source facts. The model preserves physical pending bits and
does not reorder callback storage or call those callbacks from the host.

The existing `TYPE_NIKON_SIGNAL` implements these seven words, the separate
write-only opaque configuration input, five external signals, debounce and
five IRQ contributions. Its precise masks and selector permutations match
the complete SLA source. Reuse requires private MMIO0 at`2B000000`(1C),
MMIO1 at`28000000`(4), and five SysBus outputs through mature `OR_IRQ` to
the private GIC's SPI237. The machine owner supplies those mappings and
aggregation, without sharing MAS state. Default inputs remain disconnected.
Configuration inputF has no event, readiness or interrupt side effect.

The model's reset polarity, one-enabled gates/masks, raw-low/filtered-high
readback, W1C edge latching and independent1MHz debounce Clock are retained
explicit functional assumptions. They are not new physical identity claims.
The newly source-proved lane writer is a separate finite device; it does
not widen the signal region, provide sampled inputs or complete a request.
The neighboring `2B200000` family is left to the mature storage owner.

`nikon_signal_test(q,path,signal,configuration,lanes)` in the new native
helper `tests/qtest/nikon/signal.c` exercises the exact initializer,
both lane paths, all five independently produced pending pairs, selective
acknowledgement and original read/writeback acknowledgement. It requires
a fresh synthetic private SLA VM and uses the actual named `sysbus-irq`
outputs. The sole builder compiled and executed this focused group with exit0
in1.69s; its ignored receipt is `work/slasignalnativereceipt.log`. This does not
rerun previous suites or establish original downstream engine execution.
