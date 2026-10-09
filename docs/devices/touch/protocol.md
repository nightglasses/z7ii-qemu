# TPJ normal touch endpoint

This endpoint executes the original NDS32 guest through GNU libsim. It does
not construct a contact packet from a command number or replace a callback.
The normal host contract comes from MAS saved original instructions; the TPJ
contract comes from its complete original source proposal. The separate MAS
updater prefix25 and EX SPI frames are not normal touch transport.

## Source join

MAS constructor VA103DB654 gets logical I2C0 through VA10368B28. Low record
B1805AAC names controller PA2C018000, IRQ D6 and ISR1036D5FD. Normal read
VA103DBCEC writes one command byte, then reads the requested count; address
literal55 is copied unchanged to controller ADDR+8 through VA1036D414.
VA103DB3FA reads command10 as two bytes, then command11 as seven bytes for
at most two contacts. VA103DB3CA writes command plus one argument byte.
The source does not shift55 into another address. This functional profile
uses the existing controller's seven-bit I2C interface at address55.

TPJ key table bodyb444..b48c maps hex10 to slot1 and hex11 to slot2.
O120e/1212 stores callback dfe at RAM20dec+4; O1214/1218 stores23d0 at+8.
Vector slot11 reaches handler29ce: command60028 goes to GP[-167]=207fd,
6002c bit0 goes to GP[c9], then the original key scan dispatches the callback.
The endpoint's I2C read/write-to-6002c 1/0 assignment is an explicit functional
profile; the source proves its consumers, not the physical device datasheet.

Command10 callback dfe writes a word at60040. Its low byte is RAM20802's
contact count, except the GPce==1 initial/status branch forces zero. Byte1
contains the original status-bit assembly; byte2 is an original auxiliary
value. When GP625==2, the guest copies its seven-byte records20ddc to20c30,
resets the cursor20801, and makes207ff=20802 available to command11.
Callback23d0 copies exactly seven bytes from20c30+7*cursor to60040 with the
original memcpy, then increments the cursor while cursor<207ff.

The original two-contact producer2af0/2bcc uses records20984+6*i. It compacts
active records to20ddc+7*j: byte0 uses ID2099c+i and the original valid-state
arithmetic; bytes1/2 and3/4 are little-endian scaled/clamped coordinates from
2098a+6*i. The guest explicitly writes zero to bytes5/6. This matches MAS's
bit7 valid, low6 ID, little-endian X/Y and maximum-two-contact parsing.
The model does not supply those software records by writing guest RAM.

## Bus and event behavior

The standalone process accepts portable kind I2C with 8-bit payloads and
START/WRITE/READ/STOP/NACK phases. START selects the actual address and
direction. WRITE stores the actual command and received scratch bytes.
Repeated read START sets6002c and6001c; STOP after a control payload sets
60024 to its actual byte count and raises60020 alongside the command flag.
Both paths reach the original ISR and handlers rather than host function calls.

O1abc, O1ba4 and O62fa read60003 bit4 to guard `standby wake_grant`.
The functional wake predicate mirrors the exact OR of pending6001c..21 low
bits, which also supplies the native command IRQ. Guest acknowledgements clear
it naturally; other supplied60003 bits remain unchanged. Neither START nor
bus acceptance alone supplies a wake event. The software consumers are proven;
assigning this hardware bit to that pending condition is an explicit functional
assumption. It changes no guest RAM or standard power-consent policy.

Receive scratch60080..9f is finite retained storage with an explicit cold-zero
functional initial state. Untransmitted bytes keep that state or their prior
value. They are not counted as received bytes. Original29aa copies three bytes
to20850; normal two-byte command/argument consumers use its first byte. Longer
source packet consumers receive only bytes actually written by their sender.
No scratch initialization generates ready or IRQ.

Each new command clears the output-valid bitmap. Only actual guest register
writes to60040..5f set it. READ remains pending until its byte has been written;
backpressure preserves the token and byte, and cancellation does not advance
the reply cursor. Control-phase bus acceptance is distinct from a firmware
packet acknowledgement. Final READ is followed by an explicit NACK and STOP.

## Functional notify and control wiring

The original pending-report path04dc sets GPce and calls1848, which clears
71010 bit2. The consumed/no-pending path21d2 calls18a4, which sets that bit.
Startup also sets bit2. These real register writes produce the named NOTIFY
wire only after an actual guest write. Register backing has an explicit
idle-high functional initial state, but reset storage emits no signal. The board profile pairs
this source producer with MAS resource63, port6 pin6, whose real callback
VA103DB64B sets event1. This pairing is a functional assumption supported by
both software roles, not recovered netlist/pin documentation.

The MAS worker drives resourceAA, port10 pin13, high at VA103DB4DE/E0. Its
named CONTROL wire is retained as an external level. No TPJ consumer proves
a reset/power effect, so a high level never changes PC, consent or guest flags.

The source one-shot counter uses5000c and50018. O7a0 stops bit0, writes the
zero-extended programmed count, then starts bit0. Expiry raises only50014
bit0 and the source slot12 IRQ. The actual O830 handler clears both flag and
start, then applies its own GPbd/ce changes and notify call. This model uses
programmed counts as GNU virtual ticks (zero takes one tick), an explicit
functional timing profile rather than a measured oscillator frequency.
Stop cancels the event; original stop/reprogram/start resets its deadline.
No timer writes guest RAM or assumes unobserved second/third counter controls.

Notification transitions are queued in guest-write order, including a low/high
pulse entirely inside one execution quantum. Transport backpressure retains the
first unsent edge. Sixty-four outstanding edges are the explicit native queue
capacity; exhausting it reports a boundary rather than silently dropping a
guest transition. Rewriting the same level produces no extra edge.

## Scan configuration requests

The whole Oa94a initialization routine writes80043 bit1, waits for80042 bit7,
and clears that completion through its original masked write. It then writes
80043 bit0, waits for80042 bit0, and clears that bit. The endpoint implements
each rising request as a native one-tick completion event; withdrawing a
request cancels its pending event. Completion flags start inactive and remain
until the guest acknowledges them. Timing and request names are functional
assumptions inferred from the source handshake, not a silicon datasheet.

Oa992..a9a8 retains the original byte configuration at80040/41. Oa9b6..a9e0
selects eleven entries through80068 and updates each byte through8006c. These
are finite retained configuration values, with a declared cold-zero initial
profile. They produce no scan sample, interrupt or touch record.

Oaaba programs exactly384 halfwords through index80204/data80208. The model
retains each actual guest-written value and rejects reads of unproduced entries.
Oacde and Oad4c consume/write sixteen successive values after selecting80214
bit0 streaming mode; mode zero uses an explicitly selected index each time.
This auto-increment contract comes from the complete loops, not a flat memory
bank assumption. Oaaee and Ob230 request80216 bit0 and poll for its clearing;
the native completion clears only that bit, preserving other control bits.
Ob0c4, Oa8b0 and Oa8e0 copy exactly nine configuration bytes into80118..120;
that finite write-only range retains only the bytes actually written by the guest.
Oa57a..a58e packs four source lookup pairs into byte801a0..a3. Oa4b6 selects
halfword801ea and writes halfword801e0 in nine64-entry pages. Its actual copied
204a8 counts are57,49..56; the inclusive inner loops read source tables selected
through20444/20468. All nine pointers and counts have original COPY producers.
The finite576-entry store retains actual guest configuration values and rejects
an index beyond that extent. Configuring these fields does not generate ADC data.
These control completions do not synthesize80050/72024 scan flags or sensor data.
Snapshots cannot inject80042/80216 completion state or replace the80208 indexed
data producer. Those fields follow their actual requests and guest data writes.

The first missing data producer or illegal field records its original PC.
A failed MMIO transfer still raises the real architectural bus exception. A zero-delay native stop event
then ends execution with a nonzero fault signal before the original exception
handler can repeatedly retry the same missing external field. No guest PC,
handler or flag is changed to accept the missing access.

## Normal external samples

Original3674 consumes a19-row,11-column matrix of little-endian words, exactly
344 hex bytes, plus11 auxiliary words, exactly2c hex bytes. Original3580
consumes the same matrix in column order; a786 consumes a bounded17-row subset.
Oa450 programs matrix pointers72040/44 to28000/29000 and auxiliary72080/84
to289cc/299cc. Oa41c programs baseline72048/88 to2a000/2a9cc or2b000/2b9cc.
Oa68e temporarily assigns the first two pairs to a baseline pair. Each of these
four source pairs has only its exact matrix and auxiliary ranges attached;
344..9cc is an unmapped hole. A read needs an actual sample or guest write,
including for baseline memory. Cold storage is not a zero-valued measurement.

Oa636 maps72037 bit0=1 to28000/289cc and bit0=0 to29000/299cc; O60c4 consumes
the opposite side. Oa660 holds the respective completed buffer with802b6
bit1/2; a818 temporarily holds both and restores its previous ownership state.
The functional producer alternates available slots and never overwrites a held
slot. Source-programmed DMA pointers must name an exact matching matrix and
auxiliary pair; invalid or mismatched destinations raise a boundary.

`--samples file` is one finite source-typed external-input stream. Each record
starts with LE32 kind and LE32 GNU virtual-tick delay, then its typed payload:

| Kind | Payload | Record bytes |
|---:|---|---:|
|1 normal|LE32 processing kind, LE32 processing delay,209 row-major LE32 matrix words,11 LE32 auxiliary words|896|
|2 capture|48-byte actual request tuple, then384 LE16 indexed results|824|
|3 reference|LE32 second-completion delay, then220 LE32 matrix/auxiliary words|892|
|4 quiesce|LE32 completion domain:1 BUFFER72024 bit0 or2 SCAN80050 bit5|12|

Scan, capture, reference and quiesce delays are1..INT_MAX. Normal processing kind is
NONE=0 with delay0, BUFFER=1 selecting72024 bit0, or SCAN=2 selecting80050 bit5;
the latter two require their own1..INT_MAX delay. All four normal header words
are mandatory, with no padding or former-format fallback. No contact record, synthesized idle
value, repetition or default input exists. The entire typed stream and delays
are checked before registration, then only one record stays resident. Its
kind waits for the corresponding actual source request; no request or absent
input produces no result. File exhaustion leaves the guest waiting. These are
explicit functional measurements, not recovered physical ADC encoding or a
measured oscillator frequency. The obsolete untyped format has been removed.

Continuous enable is assigned to the actual Oab68 write of80003 bit6. Production
also waits for actual DMA-pointer writes and a released buffer. Disabling the
source cancels its native event; reenabling starts the retained frame's full
delay. Actual runtime802b6 bit0 transition from1 to0 or72014=0 also pauses the
engine; bit0=1 or72014=1 releases that pause. Initialbit0=0 is not a stop
transition, preserving the original first scan before main sets it. A completed
frame updates only that finite DMA pair, the72037 selector,
and80050 bit3. Native IRQ uses the original8004c mask. Original Oa0e acknowledges
the flag and alone writes its GP progress/counters, including20980. The next
frame waits for that acknowledgement. No I2C phase or sample registration
supplies readiness, notification, a contact packet or a guest software flag.

Normal processing is a separate explicit input event, not an automatic result
of normal scan progress. At actual raw delivery, its slot records a new frame
generation and the already-produced80124/28/2c/30 and80116 configuration. A
provided BUFFER or SCAN event waits for7203a bit0 clear,8022b bit0 set, the same
72037-selected slot and its802b6 ownership mask, the same exact DMA pair, and
the unchanged configuration. Missing configuration backing is not an all-zero
request. Only then does the independent supplied processing delay begin. A
withdrawn mask, stopped engine, changed pair/configuration or special capture/
reference request cancels the event; a restored matching request may restart
its full delay. Replacing the raw frame cancels the old generation entirely.
Each supplied processing event completes once, through its explicitly selected
status domain and original ISR. No model path writes2082d or another guest flag.
NONE produces neither72024 nor a processing SCAN event.

The software proves these distinct consumers and ownership-mask writes; it does
not prove a physical pending-flag producer or that the mask means a hardware
handoff. Pairing a supplied event with the selected frame/mask is an explicit
functional model. Startup7778(0,5) uses a warmup and four independently acknowledged
processing measurements. The complete seven-frame functional profile uses NONE
for its first two and final frame, and BUFFER for frames3..6. It is not seven
automatic acknowledgements. The copied indexed initial table also has a real
guest producer: startup4ee→b36c→aade→aaba writes all384 values from RAM204fc..207fc,
originally copied from ROM b93c..bc3c, before the first ac9c read.

The special Oac1c request80218 bit0 is joined to Oa0e bit2's2082c consumer and
Ob20c's later802b6 bit6 poll. Its kind2 request tuple is the exact40 bytes
800c0..e7, then bytes72000,8019c,8021d, one reservedzero byte, LE16 frequency
80116 and LE16 control80220. Its source access is halfword read/write: Ob222/226
retains80214 in r0 across the bit0 poll, Ob238 forms r6=80220, and Ob23c/248
changes its low12 bits to7d8. Ob254/260 restores800 after the request while
preserving the high4 bits. This complete trace supplements the local-expression
inventory's O3014 read site. Every request byte must have a genuine producer;
write-only descriptors cannot silently use allocation zeros. RW fields retain
their explicitly assumed functional cold state or subsequent guest stores.
Completion must match that actual tuple; an
explicit mismatched result reports a boundary. Only after storing all384
results into the indexed configuration does it clear request80218 bit0 and
set80050 bit2/802b6 bit6. Original aeea(0,2) reads those384 halfwords into
RAM204fc..207fc, and ac9c adjusts two signed-magnitude components per halfword.
The endpoint never computes those coefficients or copies data into guest RAM.
Cancellation before completion consumes no input record. Self-clearing request,
done status and timing are explicit functional assignments supported by the
software handshake, not silicon measurements.

Oa6ea's72039 request with7203a bit0 clear surrounds two2082d waits. Kind3
requires both programmed pairs to name the same source2a/2b reference buffer.
Its actual supplied frame is written before72024 bit0 invokes the original
handler. That handler's real acknowledgement schedules a separate80050 bit5
completion after the supplied second delay. It cannot happen before the first
acknowledgement or collapse both waits into one pending event. Bit5's original
handler updates GPcf/d0/2082d and calls b06a. Restoring7203a bit0 or withdrawing
72039 cancels unfinished reference events. Normal input cannot answer these
special requests, and supplies none of their completion flags.

The independent ac9c freeze phase is kind4, not another raw frame. Its real
released-to-both-held802b6 bits1/2 transition must have the normal DMA pairs,
7203a bit0 clear,8022b bit0 clear,8019c lowbits2, and genuinely produced
80124/28/2c/30 and80116 configuration. The full configuration is retained for
this request generation. The explicitly provided completion domain and delay
are functional external hardware inputs; no physical pending producer was
recovered. Processing events in flight must finish or cancel before its delay
starts. Withdrawn masks, a stopped engine, different mode/pointers/configuration,
or a special request cancel this generation; restoring a level alone cannot
revive it. A fresh matching transition can consume the retained input once.
There is no fixed-delay automatic completion, cold-level ACK, default domain,
or NORMAL/NONE-to-quiesce fallback. Snapshot input cannot inject completion flags.

The original order matters: acac/acb4 sets both ownership masks; only acbc,
three guest instructions after acb4, stores2082d=1 before acc0/acc2 waits.
The removed one-tick event could run its original ISR before that store. The
explicit profile supplies4096 native ticks for this freeze, preserving source
order without observing PC or guest RAM to arm an event. After its real ISR,
ac9c reads16 indexed values and writes17, performs nine indexed RMWs, clears
both masks and returns to b200's actual primary80218 capture request. This
quiesce input supplies no indexed results, coefficients or capture completion.
The whole caller chain is essential:7778 sets7203a bit0 at7972, but after that
helper returns its outer caller7d30 clears the same bit at7d5c, before b17a and
ac9c. The exact freeze predicate is therefore zero; accepting both values would
hide the missing caller analysis. Other last writers establish normal pointer
pairs at a45a/a462/a46c/a474,72014=1 at a5cc,8022b bit0 clear at7982,
8019c lowbits2 at b1c0, four produced descriptor words at aa0e/aa1e/aa2a/aa38,
and a genuine80116 halfword write at b1ec. The latter may be zero and remains
valid because producer presence is tracked independently of its value.

The complete native event audit is:

| Input / native event | Arm and output | Cancellation / restart |
|---|---|---|
|NORMAL raw|Provided record, active engine, genuine exact DMA pair and released slot; writes220 words then bit3|Stop, slot hold or special request deschedules without consuming; restore restarts full supplied delay; delivery replaces that slot generation|
|NORMAL processing|That actual generation/configuration, matching selected ownership and processing mode; supplied BUFFER or SCAN flag only|Changed mask, pair, configuration, stop or special request cancels timer; matching restore can resume same generation, replacement removes it; one completion|
|QUIESCE|Fresh matching both-held freeze transition plus provided domain/delay; waits in-flight processing, emits only selected flag|Mode/configuration/pair change, stop or mask withdrawal invalidates generation; fresh transition required; EOF supplies no event|
|CAPTURE|Actual80218 request and matching48-byte tuple; writes384 indexed results before bit2/bit6|Withdraw request cancels without consuming; a new request restarts full supplied delay; missing/mismatched request fields are strict boundaries|
|REFERENCE first phase|Actual72039 request and selected equal baseline pairs; writes220 words before72024|Withdraw72039 or restore7203a cancels without consuming unfinished input|
|REFERENCE second phase|Original guest72024 acknowledgment, then supplied separate delay; bit5 only|Withdraw reference request or source enable cancels; no generic quiesce event shares this timer|
|Endpoint shutdown|Deschedule all native callbacks and invalidate processing/quiesce/reference requests|No callback retains a freed device or emits a completion after close|

Capture, reference and quiesce records never reinterpret NORMAL values or copy
one output shape into another. All events enter the source status/IRQ consumers;
none writes a guest software wait flag. Pending flags remain latched until the
original register acknowledgment, and unrelated I2C transactions never complete
a measurement.

The runner prints one receipt on every simulator exit. It distinguishes peer
EOF, partial-frame EOF, hangup, invalid schema/phase, I/O failure, guest stop,
source boundary, finite budget and host signal. PC, native `sim_events_time`,
sleep and actual IRQ pending state are observed through existing GNU APIs.
START/WRITE/READ/STOP/NACK/cancel counts describe accepted wire requests; byte
counts describe actual socket transfers. A second endpoint line records actual
command dispatch, guest60040 writes and their last PC, queued genuine READ
bytes, and completed source-typed measurements. Queued bytes alone do not
prove the peer received them. These receipts edit no guest state, create no
hot-loop trace, and distinguish a connected socket from executed callbacks.
One shutdown-only source line additionally reads four finite guest RAM bytes:
20980 progress,20055 threshold,2048c scan flag and2082d pending flag. Its validity
bits identify successful native memory reads, including partial setup failures;
readable mapped cold zeros do not prove initialization or a guest store.
These are observed values rather than values inferred from a PC or frame count.
Only when actual80218 bit0 remains pending, the endpoint prints its retained48B
request tuple once. Its present mask records already-established field backing;
missing bytes print `--`, and the format-reserved byte is zero. RW cold state is
still an explicit functional assumption. This observation bypasses no guest bus
permissions, supplies no measurement and writes no request or completion flag.
If a freeze transition occurred or a QUIESCE record remains loaded, a separate
shutdown-only line reports its actual edge count, last/current predicate masks,
source register values, pointer pairs and already-produced configuration. Mask
bits0..9 are: request backing present, engine active, no special request, source
enable, both-held mask,7203a mode,8022b mode,8019c mode, exact normal pairs, and
configuration present. All ten bits yield3ff. Register values are retained model
observations; the present predicates separately identify valid backing. This
diagnostic changes no acceptance rule, supplies no completion, and does not
observe guest PC or software wait flags to arm a request.

## Validation

Native fixture tests require a real decoder store before an I2C READ can
return data. They cover address NAK, real command/direction fields, stalled
read, output queue backpressure, explicit NACK/STOP, received count, retained
scratch and notify changes caused only by register writes. Synthetic fixture
code is not evidence that the original touch ISR has completed startup.
Additional assertions check one-shot IRQ level/ack, canceled requests and
original reprogram sequencing without elapsed-wall-time sleeps.
The new focused group covers both scan request/ack handshakes, canceled start,
all eleven channel entries,384 guest-written configuration values, streaming
read/write, preserved notification pulses under backpressure, and the first
real missing-field stop. The sensing group additionally covers all nine pages
and their finite bound. Ten fixture functions, including the wake predicate,
were actually compiled, linked and executed with exit0 by the sole builder.
The unchanged original whole ELF with explicit update snapshot and consent1
ran100000 GNU ticks with exit0, no fault or boundary, budget PC0506 and sleep0.
That is the real scan-counter/report wait, not evidence of completed scanning.
The first normal-sample layer's twelve fixture functions were then actually
compiled, linked and executed with exit0. Its finite-region registration exposed
GNU's base0 unit-address parsing; explicit0x-prefixed names fixed the framework
identity collision without widening memory. The new source-typed lifecycle's
fifteen fixture functions were actually executed with exit0 and seven expected
negative boundaries. It adds transition-only
quiescence, all384 captured results, request cancellation, separate reference
ack/delay ordering, alongside complete normal-frame/mask/buffer checks.
The first execution rejected a fixture store to80220 because the initial model
had only the local O3014 read site. The first fixture correction supplied the
external control through the existing snapshot API; that fixed group passed.
The subsequent full retained-register trace above proves guest halfword stores
as well. Its focused follow-up restores the actual capture/restore RMW sequence
with high-bit preservation was actually executed successfully. Exact address/width
guards and all transfer assertions remain enabled. Fixture diagnostics include
address, width and actual transferred length. The same unchanged original with
no samples ran100000 native ticks to PC0506, sleep0 and IRQ0; shutdown receipts
reported zero wire commands, guest replies and completed samples, with actual
guest notification state known/high. This is a finite progress wait, not a
touch callback or whole-startup result.
The value-producer audit also rejects an otherwise matching all-zero external
capture tuple if the guest never wrote its descriptor. Its separate negative
case preserves the complete-but-mismatched tuple check; both require no result
or completion flag to be produced. The full corrected fifteen-function group
actually passed with eight expected negative boundaries preserved.
The same request-producer rule applies to Oa74c..a75a's six transform input
halfwords and O0bf8's flash program count-minus-one: absent guest stores cannot
silently become zero input or a one-byte program. The original missing-result
case still uses a complete guest-written tuple. Oa68e reference scheduling checks
both genuinely programmed reference pairs independently of normal slot ownership;
held normal buffers cannot suppress its distinct request. That fifteen-function
group actually passed, preserving ten expected negative boundaries.
The subsequent explicit normal-processing group has sixteen fixture functions.
It covers NONE, each completion domain, full
independent delay, mask/configuration cancellation, stop/restart, frame replacement,
one completion per generation, missing descriptor backing, and distinct engine
quiescence without a second SCAN completion for the same work.
An explicit finite synthetic stream of two normal frames, each4096 virtual ticks
and220 provided words of1000(hex), was then executed against the original whole
ELF. It ended at1000000 native ticks, PC780a, IRQ0, normal=2, capture/reference=0
and no boundary. O7806 had cleared2048c after a9e6's configuration writes;
O780a waits for the next actual scan ISR to set it. The finite input was exhausted.
The shutdown-only native read observed20980=02,20055=01,2048c=00,2082d=00,
with all four address reads successful. Thus the original progress counter really
exceeded its threshold; that conclusion is supported by observed RAM values.
No wire command or guest reply ran in this standalone probe. The unchanged
no-sample profile still waits at0506. Neither probe claims physical idle samples,
touch coordinates or completed startup.
The first seven-raw-frame probe ended at78e4 with20980=08,2048c=01,2082d=01:
normal scan progress had occurred, but the independent processing acknowledgements
had never been supplied. Increasing the frame count or budget would not fix that
missing protocol phase. The source-qualified replacement profile keeps seven
normal frames and explicitly supplies only the four BUFFER events.
That complete896-byte NORMAL format and CFG correction was actually compiled,
linked and executed: sixteen fixture functions returned normally, with eleven
intentional negative boundaries and the architectural-fault assertion preserved.
The unchanged whole ELF then consumed all seven normal frames and exactly four
BUFFER completions within the same2000000-tick budget. It ended atacc2 with
20980=08,20055=01,2048c=01,2082d=01, no fault or boundary, and no80218 request yet.
This exposed the fixed-one-tick freeze race described above. The explicit kind4
quiesce implementation and source caller freeze-mode correction were then
qualified at `3935ace9`: all sixteen native fixture functions exited0, retaining
eleven expected negative boundaries and the architectural-fault assertion.
The ignored receipt is `work/tpjfreezemodenative.log`. A separate unchanged
whole-ELF probe with an explicit fourteen-record startup stream reached the
2,000,000-tick budget at PC8ec8 with normal9/capture2/reference2/BUFFER4/quiesce1,
status0 and no boundary. It executed no wire command or guest reply; see
`peer-transport/work/runtime/touch/startup.log`. This does not establish complete
startup, physical input values or original touch packet execution.

The whole CFG permission audit retained all5040 memory sites, with2696 proven
constant expressions and2344 genuinely unresolved addresses. Its338 existing
native rows were checked without treating absence of a local expression as a
hardware permission. It proved the missing80190 word reads and the additional
80064 byte store,8019a halfword read,80202 byte read and802a1 byte read now mapped
at their exact widths. Lexical-only candidates remain separate from permission
proof. Newly mapped read fields receive no invented external values.
No fixture result substitutes for original guest scan/contact execution.
