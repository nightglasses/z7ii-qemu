# Three-cause event interface

The SLA mode1 source registers object `104` at `C10015C8` through
`10057956 → 1006664E`, registers the real handler `100578FD` for IRQ158
through `10057968 → 1005EA6A`, and enables that interrupt through
`10057970 → 10066B24`. The complete original handler `100578FC..10057954`
reads the word selected by literal `10057AC8=2C01200C`, extracts bits1..3,
flips those three bits in the read value and writes it back. It then calls
the guest callback at object `+4`, when nonzero, with `r0=410A2401` and
the original cause bits in `r1`. The model does not populate that callback.

The complete official `100579FE..10057A1A` updates bits10..12 in the word
four bytes before that status word. These facts are from officialsource5
`sla.S` lines113391..113575 and literals1115470..1115488, with original
Listing rows retained in `sla-rows.jsonl`, source worktree HEAD
`8c451b553c582b8fd30224e3347e30cc602eea83`.

The COPY1 producer copies payload `O83C4D4` to `C1000000`; the descriptor
at payload `O83DA9C` starts with `104,0,0,0,0,0,0,0`. This is read-only
raw-data provenance, not executable-source qualification or model reset
evidence. The mode1 call sites are `100547F6` and `1005485E`. Device
absolute mapping and IRQ158 connection belong to the private SLA machine;
the general device exposes one 16-byte region and one IRQ output.

The implemented functional profile is a causal three-input event latch.
An external provider explicitly publishes already-produced cause bits1..3
through `nikon_events_publish()`. The named `publish` input transports the
same complete cause bitmask for providers and the native fixture; each
delivery is an explicit publication, with zero carrying no event.
Control bits10..12 enable those corresponding causes. Masking retains a
pending event, enabling a retained event raises the IRQ, and status writes
use W0C: ones preserve pending causes and zeros clear them. This supports
the original read/XOR-`e`/write ISR sequence without turning the written
ones for absent causes into new events. Reads, elapsed time and status
writes never generate events. Several causes aggregate onto the one IRQ.

The W0C interpretation, one-to-one enable correspondence
and cold masked state are explicit functional assumptions. The software
alone does not prove physical reset, timing, electrical behavior, or the
origin of the three events. No GPIO edge/level interpretation is assigned.
Reset clears enables and pending causes. No producer
is attached by default. No timer, audio engine, sample data, ready flag or
automatically successful transaction is inferred from the address or IRQ.

Only the two complete official 32-bit interfaces are bound. Other offsets,
unsupported bits and other access widths return `MEMTX_ERROR` through the
standard access diagnostic. This is a finite interface, not a completed
peripheral function. The source consumer has the one requested
qualification supplement for `1005797A..100579FD` and
`10057A1C..10057AC7`; these bytes are absent from current official rows and
are not promoted from raw code. Their actual executable roots and complete
data/control consumers are needed before adding any further behavior.

`tests/qtest/nikon/events.c` supplies `nikon_events_test(q,path,base)` for
the machine's native synthetic SLA fixture. It checks all three causes,
masked retention, enabling a retained cause, original ISR acknowledgement,
partial acknowledgement, zero publication, reset and the absence of
events from reads, status writes or elapsed time. Compilation and execution
are assigned to the sole native builder; no result is claimed here.
