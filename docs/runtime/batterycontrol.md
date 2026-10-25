# EX command02 boundary

The unchanged EX capture `run reproducible/nextquery.json` reaches transport`08023824`, caller LR`0802434d`, with TX6/RX4/timeout100 and request`02 02 02 00 00 02`. Authentication+4 and initialized byte0 are1, class+10 is1. This is a request observation, not acceptance of a command02 reply. Earlier authentication/profile support is described in [batterypeer.md](batterypeer.md).

## Original helper

Primary source is the original EX image`ex.img` (VA base`08000000`) and its retained`ex.dis`, lines12539–12589. `08024308..0802437e` takes selector r0, channel r1 and control byte r2. It constructs:

| Request byte | Source |
|---|---|
|0..2|Constants`02 02 02`|
|3|Low byte of entry r2; no helper range validation|
|4|Normally0; only entry selector==1 and channel==0 selects`min(word[2001f438],255)`|
|5|XOR of bytes0..4 via`080235b2`|

`08024348` calls the transport with entry channel, TX6, RX4 and100 kernel ticks. Return0 is success. Return1 delays100 ticks at`08024364`, and every nonzero result reinitializes the hardware route through`08023794` at`0802436a` before returning the same status. The helper itself has no retry loop.

On success, `08024374..0802437c` subtracts the transmitted byte4 from word`2001f438` only if the word is at least that byte; otherwise it leaves the word unchanged. This proves a bounded pending-count debit, not its physical units/purpose. For the observed zero byte4 this does not change the word.

## Reply and upper callers

Generic transport`08023824..08023932` requires ordinary receive completion, byte1=6 (`08023902..0802390a`) and final byte=XOR of the preceding bytes (`0802390c..08023926`). For RX4 this is byte3=XOR(bytes0..2). Command02's helper does **not** fetch or decode the receive buffer, check response byte0/2, or write a new battery identity/class/state from a response payload. Thus an ACK envelope has a source-backed predicate; interpreting a payload byte as readiness would be invented.

The retained direct BL callsites are`08023b98`, `08023c9c`, `08023d2e` and`08024aa8`. The first three select this helper only when an input record's byte0 is zero; otherwise they call the distinct`08024620` command helper. All pass control byte0 and use the battery structure's channel+40. Their selector comes from their own entry parameter, so no claim is made that all calls use selector1. Each surrounding loop delays1 tick per attempt and retries up to3; exhausted failures flow to`08024ac0`. Success continues original queries rather than completing the whole battery workflow: e.g.`08023d34` next calls`08024242`, and`08023ca2` does the same. The remaining route at`08024a2c` passes its entry selector into request byte3 and similarly retries3; its local caller`08024a08..08024a24` selects1 or2 according to its input condition. Therefore nonzero control values are statically possible but are not justified by the present observed wire tuple.

## Earlier command02-only finite contract

Accept only the captured six-byte request`020202000002` in the already completed class1 profile/observation phase, including exact repeats; reject changed payloads, length, XOR, order and class3 until independently supported. Reply with a declared synthetic four-byte envelope such as`02 06 00 04`: only byte1 and XOR are original predicates, while bytes0/2 are explicit external framing choices. Do not supply additional status/ready fields or assert physical control semantics. Exact repeats may be answered because upper retries are original; accepting one command02 does not establish the following query contracts, operational battery behavior or full startup.

The complete [batteryprotocol.md](batteryprotocol.md) inventory supersedes that earlier zero-only contract: the current peer accepts source-backed control0/1/2 and pending-count0..255 together with all other mapped commands. Their physical meanings remain unknown. The static helper analysis used the retained original EX disassembly; the separate parent-owned implementation and original execution are recorded below.

## Observed command02 acceptance and next0b boundary

Parent's unchanged EX `run batterycontrol/control.json` records one failed command02 return4 followed by original retry success0 at`08024372`, received`02060004` and pending-count0. The next actual transport entry is caller LR`0802427f`, request`020b0009`, TX4/RX14/timeout100. This proves this finite ACK's acceptance and continuation, not successful0b or complete battery operation.

Original helper`08024242..08024306` constructs the fixed0b request and calls`08023824` at`0802427a`. Null structure returns5. A nonzero transport result returns unchanged after hardware reinitialization; result1 additionally delays100 ticks. On success the generic14-byte envelope requires byte1=6 and byte13=XOR(bytes0..12). The following direct payload consumers occur at`0802429e..080242d6`:

| Reply bytes | Stored/consumed |
|---|---|
|3|If structure byte48==0, stores structure byte26, capped at100; may first preserve previous byte26 into byte50 when word60!=0 and word52==0|
|5|Structure byte28, later treated as flags|
|6..7|LE16 to structure halfword24|
|10..11|LE16 zero-extended into structure word20|
|12|Structure byte27; subsequent arithmetic reads it signed|

Reply bytes0/2 have no extra helper predicate;4/8/9 have no direct read in this decoder. That does not establish valid physical values or permit treating their meanings as known. Unlike command02, this response directly influences derived battery state.

`080240cc` immediately adjusts byte26 using byte27, input-record byte0, structure byte46 and signed table coefficients rooted at`2001f4a0`. It treats byte27<=-3 specially, clamps a negative correction to0, then performs ratio arithmetic with denominator`100-correction`; no generic safe physical range for arbitrary table/profile inputs was established here. If byte26 is0 it returns without this calculation. `08023a5c` subsequently computes byte45=`halfword36*100/halfword34` when both operands are nonzero, maps it through four table thresholds to diagnostic byte44, and forces diagnostic4 when structure word28 bit5 or6 is set. These are source-backed arithmetic/control uses; percentage-like scaling alone does not establish units, voltage, temperature, chemistry or a normal value.

There is a further conditional transaction at`080242da..080242fc`: if structure word60!=0, the helper copies **all14 received bytes**, unchanged, into a cleared local buffer and transmits those14 bytes expecting12 more, again timeout100. Its return is discarded before`08023a5c`; no second-response payload decoder occurs here. Whether this branch applies to the captured run requires word60, which is outside the retained48-byte structure snapshot. It must not be silently omitted or assigned a value to avoid the branch.

The callers already retry this helper up to3 on failure (e.g.`08023d36..08023d4c`), then use the original error route. No original battery packet/factory record establishing these physical payload values was obtained. The current [batterypeer.md](batterypeer.md) instead supplies explicitly declared virtual values in an arithmetic-safe26-byte profile, covers the conditional14/12 exchange and records successful original normal query continuation. This does not recover a physical battery specification.
