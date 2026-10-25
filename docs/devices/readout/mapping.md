# MAS readout functional mapping

This is the C1.70 MAS mapping used by the complete-bundle readout pipeline,
separate from the [SLA result device](protocol.md). Addresses and expressions
retain their evidence labels in the source manifest below. Functional sample
geometry, input association and completion causes are explicit model choices;
physical Bayer encoding and sensor timing remain unproven.

The site imports `profile.json` directly, so the readable view and maintained
manifest share one source. See the [sensor contract](../sensor/protocol.md),
[producer join](../../runtime/readoutstate.md) and
[statistics profile](../statistics/protocol.md) for the current lifecycle.

<<< ./profile.json
