# Recovering the first battery challenge seed from the wire

A wire-only search has now recovered a representative from the captured
first challenge, independently verified against all17 wire bytes. Its
first128-bit scalar has at most **2^31 seed-equivalence classes**: toggling
the seed's top bit leaves all first eight output halfwords unchanged. The
earlier bounded Python result below is retained as the baseline; the native
recovery and original EX acceptance boundary are recorded below. A fresh
wire-only recovery has also passed the unchanged EX validator through UART,
with guest execution paused during the host search; this is timing-assisted
authentication acceptance, not real-time or complete battery initialization.

For functional startup, the later [reproducible-input peer](batterypeer.md)
removes search from the running transaction. It now passes original first-auth
and explicit class1 0c/0e initialization with no in-transaction debugger pause;
the complete source-backed finite UART command set now passes normal class1
query continuation using explicit telemetry and serial byte pacing. That is
a separate input profile and receipt,
not a new throughput or physical-timing claim for wire-only recovery.

## Inputs and exact reduction

The original seeder/generator and challenge arithmetic are documented in
[authentication.md](authentication.md), with original EX VA ranges and
the successful recorded-seed diagnostic. The present recovery study uses
only wire challenge`f2b22009879b787ac3fdf87578d6f64a06`, public G/H/c and
those algorithms. `run seedrecovery/bounded.py` never opens
the capture's seed answer, guest RAM, hardware RNG state or seedcheck output.
It imports the host field implementation and reads the retained original
EX only for public constants.

For seed`s`, define the original initialization recurrence:

```text
x_n = (A_n*s+B_n) mod 2^32
A_0=1, B_0=0
A_(n+1)=69069*A_n mod2^32
B_(n+1)=69069*B_n+66d619e1(hex) mod2^32
F(x)=low16(x)+high16(x) mod2^16
k_i = F(x_(23-i))+F(x_(54-i)) mod2^16, i=0..7
k = sum(k_i * 2^(16*i))
C = first17bytes(L(G,k))
```

The indices follow the original ring initialization in descending order and
initial pointers31/0; the first eight steps do not overlap updated ring
cells. This is specific to the first scalar after reseeding.

Split`s=l+2^16*h`. For fixed low halfword`l`, each scalar halfword is

```text
k_i = alpha_i*h + beta_i(l) mod2^16
alpha_i = A_(23-i)+A_(54-i) mod2^16
beta_i(l) = F((A_(23-i)*l+B_(23-i)) mod2^32)
          +F((A_(54-i)*l+B_(54-i)) mod2^32)
```

The exact alpha coefficients are`a8de,1e56,b1ae,fa66,6dfe,d7f6,b1ce,db06`.
Their gcd with65536 is2. Therefore`h` and`h+32768` give the same eight
halfwords; searching`s` in`[0,2^31)` covers the top-bit pairs. This proves
an upper bound, not injectivity of all remaining classes. A matching wire
challenge recovers a usable candidate scalar/equivalence class, not a unique
32-bit seed. Other possible scalar or elliptic x-coordinate collisions
must not be ruled out without checking.

The folded high halfword makes the full seed→scalar map non-affine over
both ordinary addition modulo2^128 and bitwise XOR. The script retains
explicit counterexamples and checks the split formula and top-bit identity
on512 predetermined inputs. These checks supplement the algebra; they are
not an exhaustive seed survey.

## Recovery method, cost and possible optimization

A correct baseline enumerates only the31-bit representatives, computes
the first scalar and`L(G,k)`, and compares all17 received challenge bytes.
Only a wire match permits subsequent scalar-dependent computation. This
would avoid reading guest random state and would not require the physical
battery's private key: after recovering the camera's scalar, its local
`L(H,k)` is computable from public H. That observation is an algorithmic
consequence, not a claim that the search has completed or that a generated
identity represents a physical battery.

The bounded experiment evaluated **32 candidates, seeds0..31**, on the
captured challenge. None matched. It took0.71764 seconds,44.59 candidates/s,
using the readable pure-Python field/ladder implementation. Linear full-space
extrapolation is48,160,011 seconds, approximately557 days for this single
process; half-space expectation would be approximately279 days only under
an additional uniformly positioned-target assumption. This tiny benchmark
is a baseline, not a lower bound or an optimized native/GPU prediction.
No4-billion/2-billion search or answer-based interval selection was done.

Concrete optimization directions are fixed-base G window precomputation,
carryless native field arithmetic, parallel candidate batches, and avoiding
per-candidate inversion: compare projective`X` with`C*Z` instead. These
preserve the wire-only contract. They are not implemented or timed here.
Worst-case one-day coverage would need about24,855 candidates/s; one-hour
coverage about596,524/s. A full17-byte challenge plus4-byte representative
table would occupy42GiB before indexing and overhead, and still requires
the expensive offline construction.

The16+15-bit decomposition does **not** immediately give a2^16
meet-in-the-middle method: each limb wraps modulo2^16 independently, while
the scalar ladder consumes the concatenated integer and exposes only its
elliptic x-coordinate. A decomposition`k(l,h)=u(l)+v(h)` cannot be assumed.
Interval pruning would require bounds informative after the elliptic map;
the received x-coordinate supplies no established ordering/prefix oracle.
No faster algebraic method or search-complexity lower bound is established
by this investigation.

## Primary reference implementations

The directly relevant public implementation is
[JKWTCN/z72-qemu controller battery code](https://github.com/JKWTCN/z72-qemu/blob/687c6f9457221eaf543968e3307159e219890dfb/qemu/nikon-z72-controller-battery.c).
It reconstructs the same field and ladder, but
`battery_answer_authentication()` reads controller RAM at`2001f446` and
`2001f458` through`address_space_read` before answering. Its
[author's protocol note](https://github.com/JKWTCN/z72-qemu/blob/687c6f9457221eaf543968e3307159e219890dfb/research/z7ii/battery_serial.md)
explicitly calls this an experimental state bridge and distinguishes it
from deriving a battery response from the serial request. It provides a
useful arithmetic/framing comparison, **not** a wire-only seed recovery
implementation. Exact source copies are retained in the ignored study
directory; no code was adopted into the device.

[YCX81/z8-qemu boot code](https://github.com/YCX81/z8-qemu/blob/f56290e3f4d5ca8fe52e9528b5ea6a8e163a12dd/tools/z8_boot.py)
supplies a synthetic112-byte family4 battery report; its
[resource note](https://github.com/YCX81/z8-qemu/blob/f56290e3f4d5ca8fe52e9528b5ea6a8e163a12dd/docs/z8-battery-resources.md)
describes virtual resource thresholds. Neither inspected mechanism recovers
a seed from this controller UART challenge.

The fixed
[WonderLantern Z7 board source](https://github.com/WonderLantern/z7-qemu/blob/97abf0b81cd3211f8374a38987a0beaf4549b90f/qemu/nikon-z7-lab.c)
and code searches did not identify a battery seed-recovery implementation.
The complete tree for
[RdWing/z8-qemu](https://github.com/RdWing/z8-qemu/tree/5828aec2b9eaf96193cd25be99b527f7a3ba4cae)
was inventoried, but its large main source could not be fetched through the
connected tool; battery code search had no results. These scoped negative
searches do not prove that all versions, branches or dependencies lack one.

Receipts are`bounded.py`, `bounded.json`, `source-observations.json` and
the fixed Z72 reference source copies under
`run seedrecovery/`. Only pure-host bounded computation and
primary-source reads were performed; no build, guest run, firmware change,
private-state response oracle or successful reply generation occurred.

## Native search implementation and first measurements

The next stage is an isolated host tool in
`run seednative/{search.c,validate.py,commands.md}`. It accepts
only the17-byte wire challenge, a bounded representative-seed interval and
thread count. It contains public G/c, PRNG reconstruction and field/ladder
arithmetic; it has no H calculation, guest memory access or response path.

The optimized field backend uses ARM PMULL (or x86 PCLMUL), Karatsuba for
the low128 bits, sparse multiplication for the top three field bits,
specialized squaring and two-stage reduction. The original projective
ladder is retained. Candidates are compared with`X==C*Z`, eliminating
per-candidate field inversion. Eight predetermined seed vectors must match
the separate Python field oracle before timing or recovery results count.
The original capture's seed-answer/nonce files are not inputs to validation.

Fixed-base windowing has not been silently substituted: the original
x-coordinate differential addition requires a known point difference.
Generic window addition would require another independently validated
coordinate implementation. Parent-owned compile/validation/timing commands
are recorded in`commands.md`. The parent compiled the ARM PMULL backend and
the eight predetermined vectors passed comparison with the independent
Python oracle. Search comparison additionally rejects zero projective Z
before testing`X==C*Z`; this guard must be present in the rebuilt search.

Initial parent measurements over seeds0..65535 found no match:
`bench65536-one.json` records112,675 candidates/s with one thread;
`bench65536-four.json` records440,146/s with four threads. These two batches
ran concurrently, so shared-resource effects and the short interval limit
the extrapolation. At those measured rates, a2^31 representative sweep
would take approximately5.29 hours or1.36 hours respectively, assuming the
rate were sustained. This is a major improvement over the Python baseline,
not proof of completed recovery. An exclusive longer batch and independently
verified natural search hit were pending at this initial measurement stage.

The first-eight ring shortcut is exact: descending initialization stores
`ring[j]=F(x_(54-j))`; initial pointers31/0 access31..38 and0..7, which do
not overlap cells already updated during those eight steps. A natural
candidate hit must be checked by independently rebuilding the original
ring and recomputing all17 wire bytes using Python`ladder(G,k)`. No known
seed-answer value or guest-private data is needed for that check.

## Native natural recovery and first-auth candidate

The parent's exclusive1M-candidate runs measured117,655.705/s on one thread
and453,384.550/s on four threads (`bench1m-one.json`, `bench1m-four.json`).
Their elapsed times are approximately8.912/2.313 seconds. Sustained full
2^31-class time would be about5.07/1.32 hours, not a guarantee for another
host or an assertion that the complete domain was searched.

The next parent run requested the whole representative domain from0 with
four contiguous partitions and a60-second wall cap. It naturally found
representative2082591 after18.589 seconds and8,345,376 actual candidate
evaluations, then stopped. Thus the requested range covered all2^31
representatives, but the completed work did **not** exhaust that range.
`recovery-run.json` retains argv/cap/normal exit; `recovery.json` retains
the hit/count/timing. No answer-based interval or guest-state input was used.
`verify_hit.py` rebuilt the original ring with writeback and independently
recomputed the Python G ladder; `verified.json` confirms all17 challenge
bytes match. This is a recovered representative, not a claim of unique
32-bit seed or a recovered battery private key.

`reply.py` is a parent-executed, pure-host first-auth candidate generator.
Its inputs are the natural search JSON and UART challenge only. It verifies
the challenge again before calculating`S=L(H,k)`. Choosing field`R=1`
gives`tag=first8bytes(S)`; the29-byte array contains byte1=6 and a final
XOR of preceding28 bytes. The transport's direct checks are those predicates
and the expected byte count; bytes0/2 have no additional tested predicate
in this path. The chosen values2/0 are explicit framing choices, not a claim
that the original validator independently requires them.

The retained eighteenth byte of R now has a source-backed first-auth
initialization argument. Original startup table at EX VA`0800391c` contains
`{080528e4,2000ee10,000141fc,080038b0}`. The original handler`080038b0`
sets its output registers to zero and clears the indicated range
`[2000ee10,2002300c)`, including`2001f49f`. The table walker`08003804`
runs before application entry`08020084`. On the traced first-auth path,
the18-byte random/challenge/local outputs end at`f458/f46a/f47c`; reply
tag writes8 bytes to`f47c`, and the17-byte R copy ends exclusively at
`f49f`. None of those writes changes the zero padding byte. Receipts
`tail-startup.dis/json` retain original instructions, table words and ranges.
This argument is limited to the traced first-auth lifecycle; arbitrary
later writes or another entry/reset path must not inherit it without review.

The generator uses that static tail0 contract; it never accepts a guest-tail
snapshot as a response-generation input. It asserts nonzero R, canonical
padding, exact29-byte length, byte1=6, packet XOR and the recovered field
product/tag relation. It emits a host candidate and its checks only. Until
the original EX receives it naturally and returns success, static/host
validation is **not** original firmware acceptance. It also does not supply
the later0c query response, its identity/class halfwords, later power
transactions or physical battery behavior. No bytes were sent to the guest
by this research worker.

## Original acceptance, follow-up query and timeout boundary

Static original EX receipts are in`run batteryaccept/`.
Transport success and authentication success have different return values:
`080247ec` returns0 at`08024836` after receiving29 bytes and passing the
generic reply byte1/XOR checks; `08042622` returns1 at`0804266e` only after
its field/tag comparison. `08023ed4` stores that result into the battery
structure+4, and zero takes`08023efc` to error9. This return/store/branch
sequence is the decisive read-only acceptance probe, not a desired guest
result to supply from the host.

After field acceptance, original`0802487c` constructs`02 0c 00 0e` (the
fourth byte is XOR), submits4 bytes and requests16 with timeout100. On
transport success, `080248d0` stores the little-endian reply halfword11/12
to structure+10; `080248d6` stores halfword13/14 to structure+8. Generic
transport checks require reply byte1==6 and byte15==XOR(bytes0..14). No
additional numeric range check on either halfword occurs in this decoder.
Their complete16-bit storage range is therefore not evidence that every
value represents a usable physical battery.

The immediate class consumer is at`08023eea..08023f38`. Class1 with a
nonzero byte at the caller-supplied pointer r8 selects`08023ff8`: channel
byte+40=1, state+41=2, substate+42=5, diagnostic+44=4. Class1 with that
byte zero uses the fallback`08023fe2` and channel0. Class3 selects
`0802402e`/`0802404a`: state5, substate0, diagnostic0, channel0/1 according
to the same caller byte, and halfword+24=9000. Other class values also use
the fallback state2/channel0/diagnostic4. These are observed class/channel
branches, not independently recovered model names or battery chemistry.
The other response halfword at+8 has no further consumer established by
this scoped initialization trace; its physical units and valid identity
encoding remain unknown.

Initialization still has checks after the0c transaction. For state2,
`08023f54..08023f62` copies reply bytes3..10 into structure+12/+16.
`08023f72` compares those8 bytes with the ROM entry at`080513fc`; equality
clears structure+4 and reaches error9. The retained entry starts
`80860426e2005a38`. Do not call the surrounding ROM an authenticated
identity inventory: the actual branch bytes at`08023f7a..08023f7e` must be
respected rather than assuming a256-entry scan from the counter width.
On the traced initial mismatch path, `08023f96` then calls`080245c4` with
argument1: request`02 0e 01 01 0c`, response5 bytes, timeout100, again using
the generic byte1/XOR checks. After success`08023faa` stores the constant1 (`fp=1`) at
structure+0; it does not copy response byte1. Retries/exhaustion can still clear structure+4 and report an
error. Thus29-byte field acceptance alone does not prove completed battery
initialization, later power-query support or an accepted physical identity.

The authentication payload is generated once before the retry loop at
`08023e9e..08023eb6`; all three attempts read the same17 bytes from
`2001f458`. Each retry creates a new transport timeout, but does not call
the scalar generator again. A response must match that actual request;
reusing an accepted reply for an unrelated future challenge is unsupported.

Timeout440 is a **kernel tick count**. `08020a40` passes the delay to
`0804d2b4`; instructions`0804d31a..0804d320` schedule the deadline as
`[20022d58]+delay+1`. Original SysTick setup`0804b826..0804b832` writes
LOAD49999 and CTRL7, selecting the processor clock: one tick is50,000 CPU
clock cycles. The nominal conversion at50MHz would be1ms/tick, but the
physical/model core-clock relationship must be established separately;
the delay must not be asserted to equal440 wall milliseconds. The timer
starts before transmission, so both21-byte TX and29-byte RX consume its
budget. At9600baud and11 line bits/byte, their aggregate wire serialization
alone is about57.3ms, before turn-around and other delays.

A wire-only recovery that takes18.6 host seconds does not establish a
physical real-time response within that timer. Feasible functional options
include an offline public-seed challenge→representative table (42GiB packed
before indexing/overhead for all2^31 representatives), or actual host
computation in an explicitly declared virtual-time device model. A host
computation can consume wall time without corresponding guest virtual CPU
ticks, but that distinction must be reported; it is not physical speed.
The original serial RX timing, timeout, retries and rejection conditions
must still run naturally. Delaying/canceling the original timer, supplying
its event, changing guest state/PC or reading guest nonce would bypass that
boundary. A successful bounded calculation for one capture is not a claim
that every challenge is recoverable within a fixed host/physical deadline.

### Fresh UART validation setup and remaining profile unknowns

The parent subsequently replaced the sparse top-three-bit multiply with
the same PMULL primitive in`batteryaccept/parent-fast.c`.
Independent read-only diff confirms this is its only arithmetic change;
the operands remain canonical top-three-bit quantities and the zero-Z
guard remains. Eight oracle vectors passed. The exclusive four-thread
1,048,576-candidate measurement in`parent-fast-bench.json` is595,459.390/s
over1.760953 seconds. A full-domain sustained-rate extrapolation is about
one hour; this does not establish sub-second physical response capability.

The parent UART diagnostic uses the composed machine's original EX:
standalone EX did not reach authentication during the30-second observation.
Because MAS and EX share serial backend index0, the parent uses QEMU's
original mux with EX focus. The input escapes literal Ctrl-A as two bytes.
`parent.py` retains raw mixed output and extracts a21-byte02/01/11 frame,
checks XOR, and supplies only its17-byte challenge to native recovery.
Its current parser chooses the first matching header and then checks XOR;
it is fail-fast rather than a general mixed-stream resynchronizer.

Hardware breakpoint`08023746` pauses vCPU execution only after the original
transport reports TX length/count21/21 and expected RX29. The host queues
the calculated29 bytes through the UART socket and resumes unchanged EX.
The existing STM32 USART source allows one input byte while RXNE is clear;
reading DR clears RXNE and calls`qemu_chr_fe_accept_input`, so remaining
socket bytes are delivered through ordinary frontend backpressure. The
parent debugger reads transport/return/received-byte diagnostics and does
not set guest memory, registers, PC, callbacks or flags. Its stop points are
transport return`08024836`, validator return site`08023ed4` and entry to
the next query`0802487c`. The sources were independently inspected, but
this setup description does not itself claim the validation run succeeded.
Pausing guest virtual execution for the search expressly prevents this
diagnostic from proving real-time response or complete startup.

A further scoped search of the original battery initializer, query helpers,
state consumers and snapshot producer still did not establish a meaningful
downstream read of the0c response's structure+8 word. Pointer-based copies
or consumers outside those inspected paths have not been excluded. There
is therefore no source-backed reason here to label it a voltage, capacity,
version or capability word, or to adopt a reference peer's0x28 as a recovered
normal value. The known16-bit storage and class1/3 branches define a decoder
boundary, not a complete legitimate virtual battery profile. Any identity,
class and later power-query values must remain explicit external inputs
with separately demonstrated consumers; this study supplies none merely
to clear a policy result.

### Experimental affine batch evaluator

`batteryaccept/batch.c` provides a separate host-only vectors and
small-benchmark experiment, not a replacement for the validated search.
It reconstructs G's affine y on the independently checked a=0 curve using
half-trace, verifies the curve equation, and builds eight16-bit fixed-base
tables. Each table contains multiples of`[2^(16*i)]G`, including an explicit
identity entry. Binary fill generates the tables in batches, avoiding one
inversion per table entry; the expected storage is about28MiB.

Each candidate adds eight selected affine points. A batch shares one
inversion across up to1024 nonzero addition/doubling denominators; identity,
equal-x doubling, inverse pairs and x=0 doubling are explicit cases. The
ordinary affine addition path uses the original field arithmetic and
`lambda=(y1+y2)/(x1+x2)`,
`x3=lambda²+lambda+x1+x2`,
`y3=lambda*(x1+x3)+x3+y1`. This reduces repeated candidate arithmetic but
introduces table/cache traffic and batch setup costs that require measurement.

The tool intentionally exposes only`vectors`, `verify` and`benchmark`
(at most1,048,576 predetermined candidates). `verify` compares1024 dispersed
public test seeds with the previously checked projective ladder and checks
the affine curve equation. The parent completed these admission checks:
`parent-batchcheck.json` transcribes eight predefined vectors passing the
independent Python oracle and all1024 dispersed native ladder comparisons
passing, including curve and degeneracy checks. Table construction took
0.029850 seconds in that verification run. A separate single-process1M
benchmark measured1,899,959.050 evaluations/s over0.551894 seconds. Table build
time is reported separately from candidate evaluation time. No guest nonce,
RNG seed answer or authentication reply is an input/output of this tool.

The parent's`parent-batchbench.json` records eight independent benchmark
processes, each evaluating the same predetermined1,048,576 seeds. Their
combined8,388,608 evaluations took0.626148 seconds wall time, an aggregate
13,397,164.885 evaluations/s. Each process reports about0.031–0.032 seconds
of table construction and29,360,128 table bytes. This is a short, repeated
host benchmark, not distinct search-domain coverage: its records explicitly
say experimental and`verified_against_ladder=false`. It has no search mode,
found representative, UART acceptance or real-time response result; those
claims cannot be inferred from its throughput.

### Original EX UART acceptance receipt

The fresh captured challenge was`0e2267559ba4ee60b78850de270cf63104`.
The parent configured a search from0 over all2^31 representatives, using12
threads and only this wire challenge as the target. It naturally found
representative2112874933 after1,726,661,050 evaluations in1077.039967 seconds,
measured at1,603,154.110 candidates/s. This stopped on a match; it did not
exhaust the domain. `parent-recovery.json` retains the actual counts and hit.

`parent-run.log` records1077.154 seconds of wall time while guest execution
was paused for recovery. After the calculated29 bytes were queued through
the UART socket and the unchanged guest resumed, `parent-accept.json`
records original transport return0 at`08024836`, received bytes
`0206009a07031015ec521b01000000000000000000000000000000003b`,
original validator return1, structure`2001fa50`+4=1, and entry to the next
query at`0802487c`. These are observed original EX acceptance results, rather
than only host arithmetic predicates. No research-worker UART injection or
guest state change was performed; the parent conducted this diagnostic.

`parent-afterwire.json` records three subsequent`02 0c 00 0e` requests and
then a new repeated authentication request after no follow-up response was
sent. Thus the accepted29-byte reply advances the original protocol to0c,
but does not complete that transaction or battery initialization. The
1077-second debugger pause makes this explicitly **timing-assisted**:
neither a real-time response within timeout440, a complete battery profile,
nor complete startup is established. The non-secret retained tail-byte
diagnostic remains separate from response generation; it is not an input
to the wire-only recovery or reply generator.
