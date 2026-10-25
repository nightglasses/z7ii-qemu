# Battery compatibility warning boundary

The battery report reaches MAS; that is separate from its acceptance as usable
power. The retained 2026-10-21 original report explicitly contains unknown
type `ff` and main fault flag1. Those bytes suffice to explain BatCk status2.
The later [131 original glyphs](path.md) identify the communication warning,
and the graphics run's first MAS report confirms those same fields. Its EX
snapshot was captured at a later report generation; the upstream failing
transaction still needs a same-generation capture. A subsequent original run
proves three initialization transport failures and error8 on the EX battery
structure. The transport event is the original receive alarm. The later
[wire-seed experiment](seed.md) supplies a fresh random-dependent reply that
the original EX accepts, while all vCPUs are paused for host computation.
Real-time response and the subsequent0c/0e peer semantics remain unresolved.

All `10xxxxxx/bxxxxxxx` addresses below are MAS VA; `080xxxxx/200xxxxx`
are EX VA. Raw static receipts and historical-field extraction remain in
`work/runs/batterycompat-20261022/`. The initial static investigation made no
guest run, write or IDA change; the parent subsequently added the read-only
original-execution captures identified below.

## MAS decision, not a transport timeout

`103f8ec6` calls normalizer `103f8c1a` with report `b23bd29c` (112 bytes)
and normalized record `b23bcd9c`. Original Thumb instructions at
`103f8f0a..103f8f30` read **bytes** `b23bcda4`, `b23bcdbc`, `b23bcdd4`;
if any equals1 they store byte2 at `b18651d8`. The branch invokes callback
slot0 of `[b1865224]` with argument5 and publishes setting/event `1902=1`
through `103d26bc`. This is a guest classification result, not a missing IRQ.

| Normalized fault byte | Report source before optional MAS override |
|---|---|
| `b23bcda4` (`normalized+8`) | Boolean `report[8]` |
| `b23bcdbc` (`+20`) | `report[65]`, only when `report[64]==2`; otherwise0 |
| `b23bcdd4` (`+38`) | `report[89]`, only when `report[88]==2`; otherwise0 |

Offsets in the last two normalized rows are hexadecimal. Normalizer type
`normalized[4]` recognizes report type bytes `{00,02,10,12,13,15,20}`;
any other value maps to0. Unless a fault already exists, that unknown type
sets `b1865220=1` and all three normalized fault bytes to1 at
`103f8dce..103f8dd8`. Any fault forces all three normalized level bytes to5.
The level consumer `103f8ae4` selects main/slotA/slotB from `normalized[7]`;
`103f8ec6` separately raises callback23 when that level exceeds4.

One qualification is essential: `103f8c1a` ends with `103f8b50`. When
`b1865230` is nonzero, that routine applies MAS settings276/277/528 and may
override normalized type, main fault and level. Capture this flag; do not
activate these software overrides as a way to make the warning disappear.

Status2 can also outlive its initial cause. Only the **first** publication,
when word `b23bcd9c` was0, writes status1 on the no-fault branch and clears
callback5. A later no-fault report does not itself restore status1. Thus a
late status2 snapshot alone does not prove a current fault or current timeout.
The precise UI selection from these generic callbacks to the warning string
has not been traced by this investigation.

## Retained direct observation

`work/runs/batck-20261021/multi-frontend.log:4` records the original112-byte
MAS frontend payload, repeated at line14. Its relevant bytes are:

```text
report[04]=ff  report[08]=01  report[0c]=05
report[40]=00  report[41]=00  report[58]=00  report[59]=00
```

Line12 records final BatCk status2. The unknown main type and main fault
explain that outcome without assuming a missing client4 delivery. The same
log's much later EX snapshot is zero; it is not a time-paired observation of
the snapshot that produced this report. The graphics run must retain its own
first-report/normalization evidence before inheriting this diagnosis.

## Original EX field production

The original EX bytes `work/docsfix/exdis/ex.img` are disassembled locally in
`report.dis` and `snapshot.dis`; literals establish report base `20015fc0`
and input snapshot `20016040`, length60. Handler `08025268` receives event
`02020045`, copies60 bytes to that snapshot and posts local event15.
`08025544` builds the112-byte report and submits it through the ordinary
transport. No report needs to be supplied by the host.

When `snapshot[16]==0`, the main report is produced as follows:

- type `report[4]` first checks snapshot state byte2: state2 yields0, state5
  yields2. Otherwise snapshot type byte0 maps0→0 and2→2; all others yield`ff`;
- main fault `report[8]` becomes1 for state byte2==2, or state byte2==9
  together with type byte0==0 (`080255f6/0802560a`);
- other branches clear that flag. These are finite firmware conditions;
  names such as authentication-failed are not independently established.

When `snapshot[16]!=0`, additional provider/class selection runs; recognized
type paths include`10/12/20`, and the default remains`ff`. If
`snapshot[16]==1`, slot blocks are copied: report64/65 from snapshot28/29,
and report88/89 from snapshot44/45 (`0802574c..08025754`, loop stride16).
The MAS slot normalization conditions above then apply.

`080278fc` builds the snapshot from original battery state, not directly from
the emulator. Its detailed path is gated by byte`2001fba6`; it consults
`2001fbce`, `2001fac8` and `2001fb08` through bounded source offsets, while
the fallback reads class byte`2001f5ac` and state at `2001fa50+29/+2a`
(hex offsets). The exact branch should be read from the retained disassembly
and current bytes, rather than naming these fields by guesswork. In the
fallback, class0 produces snapshot type`ff`; only class1..5 enters the small
conversion table.

The existing [factory-input contract](../inputs/ex/config.md) and
[monitor/status contract](../devices/expower/protocol.md) already model the
MAKE/MODE/INDI structure, selected ADC/GPIO levels, `6b` write/readback and
`0d` class response. They distinguish synthetic board inputs from original
factory data. Record4 gauge values remain erased and were historically inert
until host command118; do not blame or populate them merely from this warning.
The former missing-I2C and single-thread wait histories do not establish a
missing handshake on the present multi-thread run.

## Next decisive reads and repair boundary

The parent can obtain a read-only paired capture at MAS normalizer entry
`103f8c1a` and after `103f8ee8`, then at the first `103f8f30` decision:

- MAS `b23bd29c`112 bytes, `b23bcd9c`80 bytes, `b1865220`, `b1865230`;
- EX `20016040`60 bytes, `20015fc0`112 bytes, `2001fba6`, `2001f5ac`,
  `2001fa50`128 bytes and `2001f644`16 bytes;
- if needed, snapshot entry`080278fc`, callback copy`08025274` and report
  construction`08025544`, recording input values and normal return only.

This separates unknown class/type, explicit fault, optional slot fault,
software override and a latched prior error. Only after locating the producer
can a finite repair target a demonstrated physical input/peer transaction or
missing source-backed configuration field. Preserve negative tests for the
fault case and let original EX/MAS code rebuild and classify the report.
Changing normalized bytes, report contents, BatCk result, callbacks, PC or
function returns would bypass this boundary. No particular replacement ADC,
identity, authentication payload or factory value is justified by the evidence
collected here.

## Later graphics capture and upstream error classification

Direct observation: `work/runs/coloredpaths-20261022/battery.json` has first
MAS raw type`ff`, main fault1, normalized-before all zero, override0,
unknown-type flag0 and status-before0. Thus the explicit main fault causes
this first decision; the unknown-type fallback is not needed. The EX report
in that capture is already sequence3 with main fault0, so its snapshot cannot
be assigned as the producer of the earlier sequence1 report.

Static evidence: `080277c0` writes class byte`2001f5ac`. Configuration bytes
`2001f644[0]==1` or `[4]==1` select class3; `[4]` in{2,3} selects class1.
Otherwise state`2001fa79==2` selects1, state5 selects3, and other states
select0. The captured configuration bytes0/4 are both0 and state is9,
which explains the later class0 without assuming an ADC threshold failure.

The later state structure`2001fa50` has offsets(hex)
`14=ffffffff,29=09,2a=05,31=01,39=02`. This matches generic error handler
`08024ac0`'s error8 branch at`08024b1a`: state9, substate5, invalid word,
flag1 and diagnostic2. Its original ten-byte TBB table at`08024ae2` is
`10 11 11 11 11 11 33 11 1c 27`. This interpretation requires global byte
`2001f441!=1`; the alternate global mode has a different handler branch.
Matching retained state is an inference about its producer, not a trace.

One explicit error8 producer is initialization`08023e04`: call`080247ec`
at`08023eaa` is retried three times while nonzero; exhaustion at`08023eb8`
sets error8 and clears structure+4 before tail-calling the error handler.
`080247ec` builds a21-byte request (`02 01 11`,17 source bytes, checksum
from`080235b2`), requests29 response bytes with timeout440 through`08023824`.
That routine programs9600 through`08046584`, transmits with`08046750`,
and validates response byte1==6 and the checksum (`08023902..08023924`).
Its receive/event path distinguishes result1/4, malformed response2 and
checksum mismatch3. It does not justify substituting arbitrary accepted
identity/authentication data. The precise peripheral instance and missing
peer behavior remain unproven; this is distinct from assuming the existing
monitor/I2C peer is at fault. Receipts: `handshake.dis`, original EX image
and full `work/docsfix/exdis/ex.dis`.

Minimal next read-only probes: `08024ac0` conditional on`r1==2001fa50`,
record`r0`,LR and`2001f441`; `08023eaa`/`08024836` record retry request
and return code. At`08023824`, retain request bytes(r1,r2), expected length
r3, and timeout at[sp]; at`080238ec` retain transport state and received
buffer before validation. Capture first snapshot construction`080278fc`
and report`08025544` with sequence so the error can be joined to MAS's first
normalization. Only that evidence can support a finite physical peer repair.

## Confirmed timeout and challenge/reply boundary

The parent's original execution receipt
`work/runs/batteryhandshake-20261022/handshake.json` confirms all three
21-byte/29-byte/440-timeout attempts returned transport4, followed by
`08024ac0(r1=2001fa50,r0=8)`. This closes the inference about the error8
producer above. It does not supply any accepted physical-battery reply.

Static original code distinguishes **event mask4** from driver error mask2.
`080235d4` initializes event object`2001f4f8` named`BatCmFg1`, then registers
timer`2001f504`, name`BatCmAl2`, callback`08023769`, through`080209f6`.
`08023864..0802386a` starts it with the requested timeout.
Callback`08023768` stops the associated source through`080405e4`, clears
peripheral CR1 bit`2000`, and signals mask4 through`08021556` at`0802378a`.
Wait result bit2 therefore becomes transport4 at`080238e4..080238ea`.
TX callback`080236a0` advances one byte at a time and switches to RX after
the last byte; RX callback`08023620` signals1 only after the expected count.
Driver errors signal2. A truncated or absent reply can reach this timeout;
no successful response validation took place on these attempts.

The29-byte successful reply has these directly consumed fields:

| Offset(decimal) | Consumer |
|---|---|
|1|Must equal6 in`08023902..08023908`|
|28|Must equal XOR of bytes0..27 (`080235b2`)|
|3..10|Copied to`2001f47c`, later compared as four halfwords|
|11..27|Copied to`2001f48e`, used by the challenge validator|

Bytes0/2 have no separate check in this transport/consumer path, but remain
covered by XOR. The copy is17 bytes while arithmetic consumes18-byte
quantities; capture the retained byte`2001f49f` before assuming it is zero.

There is a further computation beyond packet XOR. `08042606` produces eight
halfwords from`080200ec` and a zero ninth halfword at`2001f446`. `080425bc`
uses that quantity and ROM constant`0802002c` through`0804bb78` to produce
`2001f458`; the request carries its first17 bytes. `080425f0` uses the same
random quantity and ROM`08020000` to produce`2001f46a` through the same
arithmetic routine. After the reply, `08042622(expected=2001f47c,
reply=2001f48e,local=2001f46a)` rejects an all-zero18-byte reply, invokes
`0804a3c0`, and compares the first8 computed bytes with the expected reply
field. Failure yields error9 through`08023efc`, distinct from timeout8.
`0804bb78` iterates128 scalar bits with multiword arithmetic; `0804a3c0`
performs a multiword product/reduction. This is direct evidence of a
random-dependent challenge relation. The exact cryptographic construction,
peer secret and legitimate identity are not established by this scoped trace.

Only after that validation does`0802487c` issue a separate4-byte request
`02 0c 00 XOR`, expect16 bytes with timeout100, and store reply halfwords
at offsets11/13 into state+10/+8. Those fields feed subsequent state/type
branches. A finite peer implementation therefore needs source-backed
challenge behavior and later command responses; a fixed accepted29-byte
packet or a forced completion would bypass original validation.

New static receipts are`work/runs/batteryreply-20261022/{transport,reply,
replyconsumer,challenge,arithmetic,scalar}.dis` and`literals.json`, all derived
from the retained original EX image/listing. Minimal additional read-only
points: `08023768` with LR and timer/transport state`2001f4e8..2001f548`;
TX completion`080236fa`, RX callback`08023620` with byte count and driver
status, and the transport's peripheral pointer`[2001f518]`. These distinguish
missing wire receive from a disabled peripheral or premature timer. For any
real response capture, retain`2001f446..2001f49f`, all29 raw reply bytes and
return from`08042622`; no normal identity value is justified yet.

### Arithmetic relation: what a wire-only peer would need

Further static decoding identifies`0804a3c0` as carryless field
multiplication. Reduction`0804a5b0` keeps three bits of limb8 and folds
bit131 to bits8,3,2,0: the polynomial is
`p(x)=x^131+x^8+x^3+x^2+1`. Values use nine little-endian16-bit limbs.
Addition`0804a70c` is XOR; `0804a624` squares through bit expansion and
the same reduction. These are original-instruction deductions, not a
comparison with a host crypto library.

`0804bb78` maintains two projective pairs, initializes them from field0/1
(`08020050/08020062`), consumes128 scalar bits, and finishes with field
inversion`0804a2bc` and multiplication. Its helpers implement, in this field:

```text
D(X,Z) = ((X² + c·Z²)², X²·Z²), c = ROM18bytes@0802001a
A((X1,Z1),(X2,Z2),u):
    t=X1·Z2; v=Z1·X2; w=(t+v)²
    return (t·v + u·w, w)
```

These recurrences and conditional updates are the binary-curve Montgomery
x-coordinate ladder; that mathematical identification is an inference from
the exact helper operations. Define the observed routine as`L(u,k)` without
depending on a named curve or an unverified secret:

```text
G = 1f6085d2f5b6c44ed32f25501ba620070600 (ROM0802002c, LE)
H = cf4e3fc5d93b3a3f762f86000f0fdc0f0000 (ROM08020000, LE)
wire request[3..19] = first17bytes(L(G,k))
local S = L(H,k)
reply R = reply[11..27] plus retained eighteenth byte
accept iff R != 0 and reply[3..10] == first8bytes(R·S mod p)
```

The request exposes`L(G,k)`, not scalar`k`. The ROM contains public field
parameters and the two base quantities; this trace has not identified a
private scalar relating them. In the usual ladder interpretation a peer
would require a provisioned scalar`d` with`H=[d]G`, allowing it to compute
the shared x-coordinate from the wire challenge, or another established
equivalent construction. No such scalar or original battery transcript is
present in this scoped evidence. The firmware arithmetic alone therefore
does not establish a legitimate29-byte reply generator. A newly selected
virtual battery identity cannot replace the firmware's fixed`H`; reading
guest random/state to construct the reply would also bypass the physical
interface. An explicit virtual peer remains blocked on source-backed key
provisioning and later command semantics, not on packet framing or XOR.

Additional receipts:`constants.txt`, `reduction.dis`, `ladderhelpers.dis`.
No candidate success reply was generated and no cryptographic library,
guest execution or firmware modification was used.

## Observed initialization transport failure

The unchanged bundle and same functional descriptor input were run with one
read-only hardware breakpoint at a time. In
`work/runs/batteryhandshake-20261022/handshake.json`, the three consecutive
calls at EX VA`08024832` use structure`2001fa50`, channel argument0,
tx length21, expected rx length29 and timeout440. Every transmit byte is
identical across attempts (retained only in the ignored receipt), and each
normal return at`08024836` has result4. The next matching error handler entry
`08024ac0` has r0=8, r1=`2001fa50`, LR=`08027157`, global`2001f441=0`.
The structure before this handler still has state2/substate5. Thus this run
directly confirms the three-failure/error8 producer, beyond matching a later
state9 snapshot. It does not identify a physical connector or establish a
valid identity/authentication reply.

Original transport instructions at`080238d2..080238ea` select result4 when
event bit2 is set and bits0/1 are clear; response/checksum validation is
skipped after that error. At that checkpoint the producer was not yet traced;
the later original timer capture below and static registration close that
question. The request's source17 bytes and checksum are retained, while the
response relation above exposes the remaining key-provisioning boundary.

`work/runs/batteryfirst-20261022/first.json` is an exploratory capture at the
first observed `08025544` call and its natural return, followed by the first
MAS normalizer entry. The EX report is already sequence3 while MAS receives
sequence1. It is expressly **not** a sequence-matched report-production proof.
The MAS first-publication evidence in `coloredpaths-20261022/battery.json`
remains the basis for mainfault1, disabled override and subsequent status2.

## Original alarm capture and machine connection

`work/runs/imagetransfer-20261023/alarm.json` directly stops at EX VA
`08023768`, LR`0804ab53`, before the alarm clears the peripheral and posts
event4. Transport bytes at`2001f4e8` are channel identifier3a, tx length15,
tx count15, expected rx1d, actual rx00 (hex). The full21-byte request was
transmitted; no receive byte arrived before this alarm. The registered timer
callback and event producer above therefore explain the observed transport4,
separately from protocol/checksum rejection.

Transport driver pointer`[2001f4f0]=2000e6dc`; its entry's peripheral base is
EX PA`40011000`, and the active configuration at`2001f518` agrees. The machine
source `hw/nikon/machines/ex.c` maps this as USART1 using upstream
`TYPE_STM32F2XX_USART`, connecting its chardev to`serial_hd(0)`. The exact
original-run argv has`-serial none`, so no external battery endpoint supplies
receive bytes. The upstream
`qemu/hw/char/stm32f2xx_usart.c` emits TX to that
chardev and sets RXNE only on received input; it does not invent a battery
reply. Existing I2C monitor/status peer behavior does not fill this UART seam.

The initial host-only stage below precedes the timing-assisted UART acceptance
recorded in the following section. It supplies no complete real-time peer.
The [seed study](seed.md) offers another route: search at most2^31 first-scalar
seed classes against the UART challenge, then compute the verification
quantity from public H. That native recovery succeeds on the captured challenge
in18.589 seconds, with no guest-private seed/nonce input; all17 wire bytes
match independent Python recomputation. It needs no battery private scalar.
The pure-host first-auth29-byte candidate passes reconstructed tag/framing/XOR
checks, using a source-backed startup-zero tail. At that stage it had not been
sent to EX, and no live UART responder or later0c query was implemented. Measured four-thread
speed is453,385/s, roughly79 minutes for all2^31 classes: one early hit does
not establish timely response to another challenge under the original timeout.
Physical hardware is not required to continue graphics or this host search.
Complete battery acceptance remains unestablished.
One recorded success packet alone would not establish replies for fresh
random challenges. No UART response was sent in that initial host-only stage;
no guest event, BatCk status or software override was inserted.

The subsequent [algorithm/source inventory](authentication.md) reconstructs
the field and scalar ladder as executable host code with an independent
affine oracle. It finds the same public G/H/c constants in Z7II, Z6II and Z7
EX payloads; the bounded local/public-source search has not identified a
private scalar or usable wire responder. Those host arithmetic checks do not
constitute original EX acceptance or an implemented battery peer.

The parent additionally captures the natural successful reseed call at EX
`08023e68` and its following first request at`08024832`, without modifying
either. `work/runs/authseed-20261023/seed.json` records that original branch.
The host reconstruction reproduces all17 challenge bytes from that observed
single32-bit seed; [authentication.md](authentication.md) records the
comparison and its limits. This establishes the successful seed path and
challenge arithmetic on this run. It supplies no battery-side key or
wire-only source for the seed, and no response was generated or injected.
The subsequent wire-only investigation first checks32 candidates, then builds
the native search, recovers from0 over a full configured domain, and constructs
the host-only first-auth candidate. The observed-seed answer file is never a
search/response input. [seed.md](seed.md) records measured native rates, natural
hit verification and the later timing-assisted original-acceptance receipt.

## Original acceptance and the following query

`work/runs/batteryaccept-20261025/parent-accept.json` joins an actual UART
29-byte reply to transport return0 at EX VA`08024836`, validator return1 at
`08023ed4`, structure`2001fa50+4=1` and entry`0802487c` for0c. Host search
uses only the fresh wire challenge and public arithmetic; no guest-private
seed/nonce or successful software result is read or injected. The1077-second
computation occurs with all vCPUs paused, so this proves algorithm and original
receiver acceptance under timing assistance, not a timely live peer.

The following original wire capture contains three`02 0c 00 0e` requests,
then a fresh authentication cycle; no follow-up reply was sent. Original0c
requires16 received bytes, byte1=6 and XOR at byte15, with timeout100 ticks.
It copies little-endian halfword+11 into structure+10 (class branch) and
halfword+13 into structure+8 (meaning still unidentified). Class1/3 have
specific original branches; the identity bytes3..10 are also consumed in a
later branch. A subsequent0e exchange expects5 bytes before final success.
These parser constraints do not alone supply a justified virtual-battery
identity/profile. An isolated successful first-auth reply does not clear
BatCk or establish a normal-startup report.
