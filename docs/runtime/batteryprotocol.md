# Original EX battery UART command inventory

This is a source inventory and finite virtual-peer design, not a completed-battery execution result or recovered physical battery specification. Primary source is unchanged `work/docsfix/exdis/ex.img`, base `08000000`, with retained `ex.dis`. Scoped evidence covers the battery implementation `08023500..08024c00`, all direct references to its transport/helpers in the retained full disassembly, and exact helper-pointer literals in the original image. No direct transport tail alias or exact transport/helper pointer literal was found; indirect/computed references and disassembly coverage limitations are not excluded. Receipts: `work/runs/batterycomplete-20261027/commands-{source,transport}.txt`, `commands-calls.json`, `commands-pointer-literals.json`. No guest run, build, IDA call or original-byte modification was performed.

Existing observed auth/class1/0c/0e and command02 acceptance remain separate evidence in [batterypeer.md](batterypeer.md), [seed.md](seed.md) and [batterycontrol.md](batterycontrol.md). The historical0b boundary has now been implemented with the full inventory below; the newer original continuous-query acceptance and its host serial pacing are recorded in batterypeer.md. Static inventory and original execution are separate evidence.

## Complete direct transport map

Hex command/header bytes below are wire bytes. Every multi-byte query/control packet ends in XOR of preceding bytes. `ACK` means the generic receive envelope only: byte1=6 and last byte=XOR of all earlier bytes. Byte0/2 are not extra original predicates. All successful receives require actual original UART completion; ACK is not permission to fabricate device events. Timeouts are kernel ticks.

| Helper entry | Transport callsite | Request | TX/RX | Timeout | Additional reply consumption |
|---|---|---|---|---|---|
|08024964|08024976|single00 attention operation|1/0|0|None; no reply; delay10 afterwards|
|080247ec|08024832|02 01 11 +17B challenge+XOR|21/29|440|Auth tag/R and retained-tail arithmetic; see seed.md|
|0802487c|080248ae|02 0c 00 0e|4/16|100|identity3..10, class11..12, opaque13..14|
|080245c4|080245fc|02 0e 01 +argument byte+XOR|5/5|100|ACK only; initializer supplies argument1|
|08024308|08024348|02 02 02 +control+pending-count+XOR|6/4|100|ACK only; successful pending-count debit|
|08024620|08024696|Same command02 shape, alternate input-record conditions|6/4|100|ACK only; same debit|
|08024242|0802427a|02 0b 00 09|4/14|100|Reads3,5,6..7,10..11,12|
|08024242|080242fc|Exact14B previous0b reply, unchanged|14/12|100|ACK only; result discarded; only when structure word60!=0|
|08024198|080241d6|02 0a 00 08|4/35|100|Reads LE16 at12/14/18 and byte32|
|08024384|080243b8|02 07 00 05|4/4|100|ACK only|
|080243dc|080244dc|02 89 0e +14B configuration+XOR|18/4|100|ACK only; conditional local byte47=byte46|
|080246c4|080247c8|Same89 shape, alternate config selection|18/4|100|ACK only|
|08024518|0802454c|02 26 00 24|4/4|100|ACK only|
|0802456e|080245a2|02 25 00 27|4/4|100|ACK only|

These are all14 direct `BL 08023824` sites in the retained disassembly; the echo is a distinct site, not another packet guessed from command byte. Transport `08023824..08023932` clears TX/RX buffers, copies the declared TX length, configures UART, transmits, receives the expected fixed count, and applies generic byte1/XOR predicates (`08023902..08023926`). It does not generally validate response byte2 as a length. Zero RX length skips receive-envelope validation. The one00 operation is called at seven explicit upper sites, not inserted before every packet/retry.

Most packet helpers return transport status unchanged; status1 adds delay100 ticks, and nonzero status reinitializes the route through `08023794`. Null structure-taking helpers return5. None contains its own three-attempt retry loop. Upper orchestrators below perform those loops, normally sleeping1 tick before each attempt; exhaustion reaches error handler `08024ac0`. Authentication uses its distinct once-generated challenge and440-tick attempts. The echo call's discarded result is an exception to ordinary helper result propagation.

## Fields the peer must explicitly model

Offsets are zero-based into complete replies; structure offsets are relative to the battery structure, e.g. observed `2001fa50`. Their names here describe computation, not chemistry, electrical units or a real manufacturer identity.

**0c / initialization:** `080248d0..080248d8` stores LE16 reply11 to structure+10 and LE16 reply13 to+8 without range checks. Initializer `08023e04` performs auth, then0c; class1 selects state2, class3 selects state5, with channel0/1 derived from caller input. Class3 sets halfword24=9000 and skips the state2 identity/0e block. State2 copies reply3..10 into+12/+16 and rejects equality with ROM `080513fc` eight bytes `80860426e2005a38`; the first mismatch exits this comparison loop, not a256-entry inventory scan. It then sends0e argument1. Success stores constant1 to structure byte0. Opaque+8 has no established meaningful consumer in this scope.

**0b:** `0802429e..080242d6` stores reply5→byte28, LE16 reply6→halfword24, LE16 reply10→word20 (zero extended), reply12→byte27. If byte48==0, reply3→byte26 with cap100; under word60!=0 and word52==0 the prior byte26 is first retained in byte50. Helper `080240cc` adjusts byte26 using byte27 as signed and table coefficients selected using input-record byte0/structure byte46. It contains division by `100-correction`, so a profile must account for the actual coefficient bounds rather than assume any payload is harmless. If byte26==0 this correction returns immediately. Reply4/8/9 have no direct read in this decoder. Word60!=0 triggers the exact14/12 echo described above; the peer must remember the exact sent0b frame rather than interpret that frame's byte2 as a command request length. It then calls `08023a5c`.

**0a:** `080241fa..08024240` stores LE16 reply12→halfword36, LE16 reply14→halfword34, LE16 reply18→halfword38, reply32→byte32. The final byte32 also sets byte46 to4/3/2/1/0 using thresholds>=200/>=190/>=180/>=170/<170. Other reply payload bytes have no direct helper consumer. `08023a5c` computes byte45=`halfword36*100/halfword34` only when both halfwords are nonzero; compares byte45 with four configured thresholds to set diagnostic44=0..4; word28 bits5/6 force diagnostic4. `080239ec` separately maps corrected byte26 through five configured thresholds to substate42=0..5, with word52/byte50 history and byte48 behavior. Thus a percentage-like encoding is an inference; the literal arithmetic and thresholds are established.

**02:** `08024308` control is low8(entry r2); pending-count is normally0, or `min(word[2001f438],255)` when entry selector1/channel0. `08024620` selects pending-count only for selector2/channel1 and input-record bytes1..3 all1 or all2; otherwise0. Both subtract the sent pending-count from that word only on successful ACK and when word>=sent count. The upper paths inspected supply control0 or1/2. `0802393c` adds to this counter; no physical unit is assigned.

**89:** Both builders take fourteen configuration bytes from original config/global inputs, not from a battery response. With config base`2001f4a0`, payload0..3 encode two LE16 values: offsets22/26 when a sampled GPIO is nonzero, offsets20/24 otherwise, each capped999. Payload4 is config byte9 restricted1..8, default8. Payload5..10 copy config bytes12..17. Payload11/12 copy bytes1/0 of global`2000ed89`. Payload13 depends on structure byte46: builder`080243dc` selects config10 for mode0, config11 for modes1..3, config38 for mode4; all are restricted1..6, default3 for modes0/4 and default1 for modes1..3. Builder`080246c4` selects config39/config40/config41 with defaults1 for modes0/4 and3 for modes1..3. Values outside mode0..4 leave payload13=0. See `ex.dis` lines12625..12744 and12923..13023. On first builder's ACK, word60==1 and substate42!=5 cause byte47=byte46 at `08024504..08024508`. No reply data is decoded. These exact generated envelopes can be recognized without reading guest state.

**07 /25 /26 /0e /echo:** No helper payload decoder beyond generic ACK was found. Their effects come from original calling code and successful transaction status, not host-supplied readiness fields.25/26 have distinct upper wrappers and must remain distinct recognized commands; physical shutdown/sleep/resume names are not established just from byte values.

## Upper workflow, aliases and retries

| Entry / gate | Original sequence and important branches |
|---|---|
|08023e04 (direct caller08027152)|Generate challenge once; attention00; auth up to3;0c up to3; class1/state2 identity check then0e(1) up to3; class3/state5 bypasses identity/0e. Initialization success is not periodic-query success.|
|08023cec|Optional local fallback check; command02 or alternate02 according to input-record byte0;0b; optional07 if structure byte28 bit4;0a;89 variant;0a;0b. Each step up to3 attempts; failure08023de6→08024ac0.|
|08023df4 /08024068|State2-gated tail aliases to08023cec, passing check flag1/0 respectively.|
|08023c54|Attention00;02 variant selected by input-record byte0;0b;0a; recompute diagnostic. Each step up to3. Alias08023c3a permits state2, or state9/channel0, then tail-jumps here at08023c4e.|
|08023b2e|Optional attention if entry second argument3;0b;02 variant; if word60==1 and substate!=5 then0a, and89 when byte47 differs from46. Per-step up to3. State2-only alias08023bfa→08023c32 tail-jumps here after selecting its attention/control argument.|
|0802394c (caller080249d2)|Skips UART work when word60==1 and substate42==5. Otherwise attention00;89 variant; if channel0 then0b then0a; update diagnostic. Each up to3.|
|08024078|Stores its two configuration bytes to2000ed89; state2/local check gate;89 variant up to3.|
|08024a2c|Local check and history gate can skip UART (delay2000 then clear word52); otherwise attention00,02 variant with control1/2, up to3 attempts. Call selector at08024a08..08024a24 supplies1/2 according to its input condition.|
|080248dc|State2/local-check gate; attention00;26 up to3; failure→08024ac0. Direct outer caller0802a352.|
|08024920|State2/local-check gate; attention00;25 up to3; failure→08024ac0. Direct outer callers0802a11a/0802a618.|

`commands-calls.json` supplies all retained direct helper callsites and tail aliases with exact original listing line numbers. State5 class3 skips many state2-only UART wrappers; `08023af8` instead writes its finite local byte26=100 and zero derived fields. This does not turn class3 into a recovered real battery model or prove every outer caller suppresses every packet. Generic error handler `08024ac0` changes state/substate/error fields according to actual result and configuration; the peer must let original timeout, retry and rejection execute rather than writing those fields itself.

## One bounded virtual-battery contract

A complete **functional protocol** implementation can cover the above inventory together, while keeping normal payloads explicit external inputs. It needs no guest RAM, PC, nonce oracle or ready override:

1. Retain existing seed-matched first-auth mathematics and explicit identity/class/opaque profile. Reject a mismatched challenge; preserve original retry/restart boundaries.
2. Add explicit immutable0a35-byte and0b14-byte response profile records (or their consumed fields with documented padding), validate envelope and arithmetic-safe ranges at configuration time, and make unused bytes declared synthetic framing. Do not invent factory-normal values. Consistent halfword34/36 pairs and safe correction coefficients must be assessed against the configured original tables; zero values may exercise fallback/empty paths and are not automatically a healthy battery.
3. Recognize every known command above with fixed lengths and exact constant headers; distinguish07/25/26/0e ACKs and02 control0/1/2 with pending-count0..255.89 has bounded fields as above; retain/record the14-byte settings and validate the generated ranges rather than accept arbitrary unknown opcodes. Full-wire captures can tighten per-profile settings, but no new transport query is required to discover their existence.
4. Retain the last exact0b response for the optional14/12 echo. Accept only that complete echo and answer a12-byte generic envelope with declared unused payload. Echo may or may not occur depending on original word60, so do not force that guest condition. A repeated exact0b request remains a query, not an echo.
5. Handle explicit attention00 only at allowed frame boundaries; no reply. Support the mapped original ordering alternatives and exact retries, including periodic re-entry and25/26 sequences; reject malformed lengths/XOR, unexpected state, unknown command and contradictory profile changes. No arbitrary reply-length inference from response byte2.

For peer sequencing, a completed0c response can enable known operational commands for either declared class, with0e optional and class1-only. This avoids imposing a mandatory class1 ACK phase on every possible caller; it does not claim that the original state2 initializer skips its0e requirement or accepts a blocked identity. Original acceptance milestones are distinct: successful initialization writes byte0=1 at`08023faa`, with authenticated word+4 still1. `08023fae` restores saved registers, including r0, so its returned r0 is not a success predicate; outer return site`08027156` is suitable for checking structure state. A full `08023cec` query sequence reaches normal return`08023df0` instead of error tail`08023de6`; its component helper results and copied fields should also be retained. MAS BatCk normalization/status is a subsequent independently observed consumer, not an inference from an EX helper return.

This is a finite source-backed set of protocol shapes and consumers, with explicitly synthetic battery data. Source alone does not select a legitimate physical profile, guarantee the original higher-level state machines call all paths, or prove successful normal init/periodic/shutdown execution. Completion claims require one original execution batch covering the intended workflows and reporting the selected synthetic profile; battery protocol coverage and whole-camera startup remain separate claims.
