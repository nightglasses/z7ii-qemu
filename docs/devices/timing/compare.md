# Timer compare output

The saved firmware methods collected in source contract
`056ab54b0afc5906d02fa09619428f69c652f740` establish five compare-capable
channels, their boolean enable/polarity, full-word comparator, shared bitmap,
and separate status-bit4 callback. The original E876 method ends by writing
basic channel control zero, so compare operation has its own enable gate.
The physical clock, waveform, result producer, and extra-feature event remain
unknown. This layer defines an explicit functional profile for those interfaces.

Each of channels0..4 has one disconnected-by-default `compare` qdev output.
The existing nominal 3GHz clock advances a phase modulo encoded period plus
one, calculated in 64 bits. While master, shared bitmap, reset state, and
compare enable permit operation, output is `(phase < comparator) XOR polarity`.
An inactive output is low. Output changes first occur after elapsed virtual
time, never from configuration, result reads, or enabling alone. Timers use
the existing nanosecond scheduling bound: converted tick duration plus one
nanosecond. This bound and waveform are model assumptions, not measured hardware.

An elapsed period latches status bit4 and drives the existing channel IRQ until
guest W1C acknowledgement. Opcode0 repeats; opcode31 stops after one period;
opcode1 keeps its monotonic count and reports each elapsed compare period.
The primary basic/capture callback routing remains distinct. Pause preserves
count, stops the timer, and deasserts output; resume continues that phase.
Comparator or polarity updates preserve phase and schedule the next edge.
Channel or global reset cancels timers, clears feature state and pending IRQ,
and deasserts output without producing completion.

Second-bank offsets24/28/2C retain enable, polarity, and comparator. Offset30
returns the clock-derived phase, an explicit virtual result definition. Reads
have no side effects. The extra feature at38 accepts disable zero, and3C retains
its full configuration word. Nonzero extra activation, unused34, and result40
remain unsupported. Invalid widths, geometry, boolean values, and unsupported
activation are rejected before changing state.

The native C qtest in `tests/qtest/nikon/timer.c` covers waveform edges,
pause/resume/disable, one-shot and source1 period IRQ, W1C, reset cancellation,
all five feature geometries, and rejection preserving prior state. It uses a
synthetic four-byte ARM loop and upstream libqtest; it does not execute firmware
or establish physical timing. Build and execution results are pending.
