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

`--samples file` is one finite external-input stream. Each884-byte record is
LE32 GNU virtual-tick delay followed by209 row-major matrix words and11
auxiliary words. Delay must be1..INT_MAX. There is no header, padding, contact
record, synthesized idle value, automatic repetition or default sample. The
whole file shape and all delays are checked before use; only one frame is held
in memory. File exhaustion leaves the guest waiting for further observations.
These are explicitly supplied functional measurements, not recovered physical
ADC encoding or a measured oscillator frequency.

Continuous enable is assigned to the actual Oab68 write of80003 bit6. Production
also waits for actual DMA-pointer writes and a released buffer. Disabling the
source cancels its native event; reenabling starts the retained frame's full
delay. A completed frame updates only that finite DMA pair, the72037 selector,
and80050 bit3. Native IRQ uses the original8004c mask. Original Oa0e acknowledges
the flag and alone writes its GP progress/counters, including20980. The next
frame waits for that acknowledgement. No I2C phase or sample registration
supplies readiness, notification, a contact packet or a guest software flag.

The special Oac1c request80218 bit0 is joined to Oa0e bit2's2082c consumer.
Oa6ea's72039 request with7203a bit0 clear surrounds two2082d waits:72024's
handler and80050 bit5 both clear that variable, but bit5 additionally updates
GPcf/d0 and calls b06a. Their calibration/freeze output consumers are distinct
from a normal matrix. Normal replay pauses during those requests and supplies
none of their completion bits. Withdrawing them resumes normal replay; it
cannot reinterpret a normal frame as a calibration result. Their specialized
input contract is being resolved from those original consumers separately.
Snapshot input cannot inject80050 completion flags.

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
The sole builder compiled, linked and executed all twelve fixture functions at
fixed source `a0b8e428` with exit0; the ignored receipt is
`work/tpjsamplesqualified.log`. Its six missing-input/invalid-range diagnostics
are expected negative assertions. The normal-sample and input-guard functions
They exercise two complete frames, every matrix/auxiliary value, masked IRQ,
ownership holds, cancel/resume, retained data and finite stream exhaustion.
No fixture result substitutes for original guest scan/contact execution.
