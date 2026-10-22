# Timer compare output

The saved firmware methods collected in source contract
`056ab54b0afc5906d02fa09619428f69c652f740` establish five compare-capable
channels, their boolean enable/polarity, full-word comparator, shared bitmap,
and separate status-bit4 callback. The original E876 method ends by writing
basic channel control zero, so compare operation has its own enable gate.
The physical clock, waveform, result producer, and extra-feature event remain
unknown. This layer defines an explicit functional profile for those interfaces.

The later [divider/load audit](load.md) corrects Bchannel+4's interpretation:
it divides the nominal input clock by the encoded byte plus one. Historical
opcode-based one-shot/source1 behavior is removed, including for compare.

Each of channels0..4 has one disconnected-by-default `compare` qdev output.
The existing nominal 3GHz input advances a divided counter, whose phase is
modulo encoded period plus one, calculated in64 bits. While master, shared bitmap, reset state, and
compare enable permit operation, output is `(phase < comparator) XOR polarity`.
An inactive output is low. Output changes first occur after elapsed virtual
time, never from configuration, result reads, or enabling alone. Timers use
the existing nanosecond scheduling bound: converted tick duration plus one
nanosecond. This bound and waveform are model assumptions, not measured hardware.

An elapsed period latches status bit4 and drives the existing channel IRQ until
guest W1C acknowledgement. Every supported divider repeats the programmed
period; divider selection does not select one-shot or monotonic-counter behavior.
The primary basic/capture callback routing remains distinct. Pause preserves
count, stops the timer, and deasserts output; resume continues that phase.
Comparator or polarity updates preserve phase and schedule the next edge.
Switching back to basic operation schedules the next divided threshold.
Reenabling compare ignores periods elapsed while the feature
was disabled; period reprogramming cancels old edges and rearms the counter.
Channel or global reset cancels timers, clears feature state and pending IRQ,
and deasserts output without producing completion.

Second-bank offsets24/28/2C retain enable, polarity, and comparator. Offset30
returns the clock-derived phase, an explicit virtual result definition. Reads
have no side effects. The extra feature at38 accepts disable zero, and3C retains
its full configuration word. Nonzero extra activation, unused34, and result40
remain unsupported. Invalid widths, geometry, boolean values, and unsupported
activation are rejected before changing state.

The native C qtest in `tests/qtest/nikon/timer.c` covers waveform edges,
pause/resume/disable, divided periodic IRQ, W1C, reset cancellation,
all five feature geometries, and rejection preserving prior state. It uses a
synthetic four-byte ARM loop and upstream libqtest; it does not execute firmware
or establish physical timing. The current12-case focused result is qualified
in [load.md](load.md); earlier pending/historical receipts retain their scope.
