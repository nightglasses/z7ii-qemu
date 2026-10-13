# Explicit EX analog counts

The combined `z7ii` diagnostic machine accepts `ex-adc-counts=1:3500`.
Entries are `channel:raw-count`, separated by semicolons. Only source-bound
ADC1 channels 0 through 3 are admitted, with unique entries and integer counts
0 through 4095. Omitted channels remain disconnected. A complete bundle is
required; configuration must precede machine initialization. An empty value
disconnects all optional machine-supplied channels.

Each listed count is delivered through the existing private EX ADC input
before realization. The EX backend owns conversion, IRQ and retained-input
reset behavior. A held count does not complete anything until the original
guest requests a conversion. No default count, EOC, guest event, task or
destination memory is substituted.

The original input-worker initializer at EX ROM VA `0803e2b0` naturally
requests descriptor ID `0x129`, bound to ADC1 at EX peripheral address
`0x40012000`, channel 2. The source binding agrees with the initialized
descriptor records `0x127..0x12a` for channels `0..3` described in
[the EX startup contract](../startup/ex.md). The new static and original-run
observation includes this input-worker request; the earlier wrapper's selected
indices `0,1,3` did not cover every consumer. The underlying ADC model already
accepts channel 2. The machine property now permits its explicit count too,
so the caller can supply that missing external input without changing guest
state or completing a conversion directly. With no provider, the ADC retains
its existing no-EOC boundary and the original unbounded poll can keep waiting.

This channel binding does not identify a physical quantity, voltage, neutral
state or suitable raw count for channel 2. Every supplied value remains an
explicit input assumption; no count is selected by default. The focused
synthetic qtest `/arm/nikon/combined/ex-adc-channels` checks that `2:1234` is
accepted and stored and that channel 4 is rejected. Its arbitrary test count
has no asserted physical or original-consumer meaning.

The illustrative `1:3500` configuration is an ADC1 channel1 software-band0
probe. Saved original consumers classify 3500 in band0 at initialization and
in the recurring classifier. It does not mean normal voltage, measured camera
input or a neutral state. Other channels remain absent. The bounded consumer
contract is preserved at source `f15d7c7ade6a8d40b36ca6018dfa7d488ffdf2a2`,
`docs/runtime/reset.md`; the original record IDs and thresholds are summarized
in [the EX startup contract](../startup/ex.md).

The backend's necessary `/arm/nikon/ex/adc` test passed on source
`2051f8cafdf346baae1939a967bab98c887ec610`, with exit0 in1.02s
(`work/exadcnativereceipt.log`). The optional combined machine configuration
compiled and linked (`work/adcoptionbuild.log`). Saved GUI option checks reject
out-of-range, unknown-channel, duplicate and fractional counts. The actual QMP
receipt `work/gui/analog/options.json` confirms a post-initialization setter is
rejected and does not change the stored count. The lifecycle check uses QEMU's
machine-initialized phase, from fixed source
`983c305d556dd1271d25797e1bc25637326e3062`.
The optional configuration still requires a separate original whole-run
observation. Synthetic backend
qualification does not prove that the original ADC poll, watchdog service,
or GUI startup has continued. This integration reuses the saved qualification;
it does not start another VM or repeat the backend tests.
